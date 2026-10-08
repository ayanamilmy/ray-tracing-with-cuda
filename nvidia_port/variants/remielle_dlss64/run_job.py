#!/usr/bin/env python3
"""Run an existing prepared three-page job through the separate reconstruction backend.

The original job source and scene are read-only. Output must be a new directory.
Two DLSS workers use contiguous ranges and 32 warm-up frames, rather than
interleaving two unrelated reconstruction histories into the final video.
"""
import os
import sys
import json
import struct
import subprocess
from pathlib import Path

job=Path(sys.argv[1]).resolve();card=int(sys.argv[2]);options=sys.argv[3:]
sys.path.insert(0,str(job.parent))
from timeline import FRAMES,FPS
mode=os.environ.get('NPT_RECONSTRUCTION','dlss')
workers=int(os.environ.get('NPT_WORKERS','2'))
if workers not in (1,2) or not 0<=card<workers:raise SystemExit('Use one or two workers and a valid GPU index')
out=Path(os.environ['NPT_OUTPUT_DIR']).resolve()
if out==job.parent.parent or job.parent.parent in out.parents:
    raise SystemExit('Choose a separate output directory; the original result directory is protected')
out.mkdir(parents=True,exist_ok=True)
binary=Path(os.environ['NPT_BINARY']).resolve();ptx=Path(os.environ['NPT_PTX']).resolve()
scene=job.parent.parent/'scene-3024x1964.npt'
data=bytearray(scene.read_bytes());original=struct.unpack_from('<4I',data,24+96)
ow=int(os.environ.get('NPT_OUTPUT_WIDTH',original[0]));oh=int(os.environ.get('NPT_OUTPUT_HEIGHT',original[1]))
if mode=='dlss':
    query=subprocess.check_output([str(binary),'--dlss-optimal-size',str(ow),str(oh)],env=dict(os.environ,CUDA_VISIBLE_DEVICES=str(card)))
    settings=json.loads(query);iw=settings['input_width'];ih=settings['input_height']
else:iw=ow;ih=oh
spp=int(os.environ.get('NPT_SPP','256'))
sample=16 if spp>0 and spp%16==0 else spp
if sample not in (1,4,9,16):raise SystemExit('SPP must be 1, 4, 9, or a positive multiple of 16')
passes=spp//sample
struct.pack_into('<4I',data,24+96,iw,ih,sample,original[3])
prepared=out/f'scene-reconstruction-gpu{card}.npt';prepared.write_bytes(data)
source=job.read_text()
source=source.replace("'-c:v','libx264','-preset','medium','-crf','17'", "'-c:v','hevc_nvenc','-preset','p5','-rc','constqp','-qp','18','-bf','0','-tag:v','hvc1'")
source=source.replace("'-movflags','+faststart'", "'-movflags','+faststart+use_metadata_tags','-metadata','com.apple.quicktime.full-frame-rate-playback-intent=1'")
source=source.replace('remielle-swimsuit-three-pages-3k-120fps-256spp.mp4','remielle-swimsuit-three-pages-dlss-3k-120fps-64spp.mp4')

def change(old,new):
    global source
    if source.count(old)!=1:raise SystemExit('Prepared job format changed; update adapter anchor: '+old[:70])
    source=source.replace(old,new)
# The scene expression occurs twice: read and process argument.
expression="(D.parent/'scene-3024x1964.npt')"
if source.count(expression)!=2:raise SystemExit('Expected two scene references in the prepared job')
source=source.replace(expression,f"(Path({str(prepared)!r}))")
change('(3024,1964),out',f'({ow},{oh}),out')
change('hw=3024/1964*hh',f'hw={ow}/{oh}*hh')
change("'16','262144'",f"'{passes}','262144'")
os.environ.update(NPT_RECONSTRUCTION=mode,NPT_OUTPUT_WIDTH=str(ow),NPT_OUTPUT_HEIGHT=str(oh),NPT_TRANSPORT='mmap')
if mode=='dlss':
    start=FRAMES*card//workers;end=FRAMES*(card+1)//workers;stride=1
    existing=out/'frames'
    missing=[f for f in range(start,end) if not (existing/f'{f:04d}.png').exists()]
    # Warm up each missing span without overwriting previously saved frames.
    warmup_frames=set()
    for index,frame in enumerate(missing):
        if index==0 or frame!=missing[index-1]+1:
            warmup_frames.update(range(max(0,frame-32),frame))
    warmup_frames.difference_update(missing)
    selected=sorted(set(missing)|warmup_frames)
    warmup=out/'warmup'/f'gpu{card}';warmup.mkdir(parents=True,exist_ok=True)
    source=source.replace("video/f'{frame:04d}-bake.log'","(_npt_warmup if frame in _npt_warmup_frames else video)/f'{frame:04d}-bake.log'")
    source=source.replace("png=video/f'{frame:04d}.png'","png=(_npt_warmup if frame in _npt_warmup_frames else video)/f'{frame:04d}.png'")
else:
    start=0;stride=workers;warmup=out/'warmup'/f'gpu{card}';warmup_frames=set();selected=[f for f in range(card,FRAMES,workers) if not (out/'frames'/f'{f:04d}.png').exists()]
frame_line=next(l for l in source.splitlines() if l.startswith('frames='))
probe_expression=frame_line.split(' if probe else ')[0].split('=',1)[1]
change(frame_line,'frames='+probe_expression+' if probe else _npt_frames')
change("str(mapped.paths[1])+'\\n'", "str(mapped.paths[1])+('\\tRESET' if index==0 or frame!=frames[index-1]+_npt_stride or frame in _npt_cuts else '')+'\\n'")
os.environ['NPT_FRAME_DELTA_MS']=str(1000*stride/FPS)
cuts=set(int(v) for v in os.environ.get('NPT_RESET_FRAMES','').split(',') if v)
if '--dry-run' in options:
    print(json.dumps(dict(worker=card,first=selected[:3],last=selected[-3:],frames=len(selected),warmup=sorted(warmup_frames))));raise SystemExit(0)
sys.argv=[str(job),str(card)]+options
print(json.dumps(dict(reconstruction=mode,input=[iw,ih],output=[ow,oh],spp=spp,worker=card,frames=len(selected),history_stride=stride)),flush=True)
exec(compile(source,str(job),'exec'),{'__file__':str(job),'__name__':'__main__','_npt_frames':selected,'_npt_stride':stride,'_npt_start':start,'_npt_warmup':warmup,'_npt_warmup_frames':warmup_frames,'_npt_cuts':cuts})
