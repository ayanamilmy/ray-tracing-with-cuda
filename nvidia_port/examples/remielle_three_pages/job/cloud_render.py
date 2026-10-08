import os,sys,time,subprocess,shutil,json,struct
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
from PIL import Image
from pose import PoseEngine
from timeline import state,FRAMES,FPS,DURATION
from frame_transport import send_frame,receive_image,save_result,MappedTransport
D=Path(__file__).resolve().parent;root=Path(os.environ.get('NPT_ROOT','/root/autodl-tmp/ye-benchmark'));out=Path(os.environ.get('NPT_OUTPUT_DIR',str(D.parent)));out.mkdir(parents=True,exist_ok=True);video=out/'frames';video.mkdir(exist_ok=True)
card=int(sys.argv[1]);probe='--probe' in sys.argv;env=dict(os.environ,CUDA_VISIBLE_DEVICES=str(card))
transport=os.environ.get('NPT_TRANSPORT','mmap');assert transport in ['file','pipe','mmap'];pipe=transport=='pipe';shared=transport=='mmap';async_save=os.environ.get('NPT_ASYNC_SAVE','1')=='1';benchmark='--benchmark' in sys.argv
base=(D.parent/'scene-3024x1964.npt').read_bytes();counts=struct.unpack_from('<4I',base,8)
geom=np.frombuffer(base,dtype='<f4',offset=536,count=counts[0]*96).reshape(-1,24,4).copy();meta=geom.view('<u4')[:,23];body_ids=np.where(meta[:,0]==1)[0];hulls=np.where(meta[:,0]==2)[0]
matstart=536+counts[0]*384+counts[1]*48
mats=np.frombuffer(base,dtype='<f4',offset=matstart,count=counts[2]*524).reshape(-1,131,4).copy();offsets=json.loads((D/'material-offsets.json').read_text())
def field(name):return offsets[name]//16
def unit(a):return a/np.maximum(np.linalg.norm(a,axis=-1,keepdims=True),1e-15)
pose_engine=PoseEngine(root/'results/cloud-animation/original')
packet=video/f'framegpu{card}.bin';params=base[24:536]
mapped=MappedTransport(16+512+geom.nbytes+mats.nbytes,(3024,1964),out) if shared else None
binary=Path(os.environ.get('NPT_BINARY',str(root/'nvidia_port/build/optix_sequence')));ptx=Path(os.environ.get('NPT_PTX',str(root/'nvidia_port/build/optix/nvcc/device.ptx')));asset=D/f'asset-{card}'
renderlog=(out/f'gpu{card}-engine.log').open('wb');errors=(out/f'gpu{card}-engine-errors.log').open('wb')
engine=subprocess.Popen([str(binary),str(ptx),str(D.parent/'scene-3024x1964.npt'),'--pipe' if pipe else '--mmap' if shared else '-','16','262144'],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=renderlog if pipe else errors,bufsize=-1)
def wait_for(tag):
 while True:
  line=engine.stdout.readline()
  if not line:raise RuntimeError('Renderer exited '+str(engine.poll()))
  renderlog.write(line);renderlog.flush()
  if line.rstrip().startswith(tag.encode()):return
if not pipe:wait_for('READY')
frames=[f for f in ([0,480,930] if card==0 else [121,571,1021]) if not (video/f'{f:04d}.png').exists()] if probe else [f for f in range(card,FRAMES,2) if not (video/f'{f:04d}.png').exists()]
if benchmark:frames=[int(f) for f in os.environ.get('NPT_BENCH_FRAMES','0,121,571').split(',')]
def bake_frame(frame,asset):
 st=state(frame/FPS)
 with (video/f'{frame:04d}-bake.log').open('w') as log:
  import contextlib
  with contextlib.redirect_stdout(log):v=pose_engine.evaluate(st['clip'],st['source_frame'],st['blend'])
 return v,st
assert len(body_ids)==len(pose_engine.names), 'Original scene and FBX topology differ'
pool=ThreadPoolExecutor(max_workers=1)
save_pool=ThreadPoolExecutor(max_workers=1);saved=None;timings=[];job_started=time.perf_counter();measure_started=None
pending=pool.submit(bake_frame,frames[0],D/f'asset-{card}-0') if frames else None
for index,frame in enumerate(frames):
 png=video/f'{frame:04d}.png'
 t=time.perf_counter()
 if benchmark and index==1:
  if saved is not None:saved.result()
  measure_started=time.perf_counter();t=measure_started
 pose,st=pending.result()
 pos=pose['positions'].astype(np.float32);nor=pose['normals'].astype(np.float32);attrs=pose['attributes'].astype(np.float32)
 origin=st['origin'];right=st['right'];up=st['up'];back=st['back']
 pp=np.frombuffer(params,dtype='<f4').reshape(-1,4).copy();focus=np.linalg.norm(st['origin']-st['target']);hh=np.tan(np.radians(st['fov']/2));hw=3024/1964*hh
 pp[0,:3]=origin;pp[1,:3]=origin-focus*back-focus*hw*right-focus*hh*up
 pp[2,:3]=2*hw*focus*right;pp[3,:3]=2*hh*focus*up;pp[4,:3]=right;pp[5,:3]=up;pp[10,:3]=st['light'];pp[15,3]=4*np.pi*np.sin(np.radians(10/4))**2
 frame_params=pp.tobytes()
 g=geom.copy();mi=meta[body_ids,1];rules=mats.view('<u4')[mi,field('zzz_outline_rules')]
 for v in range(3):
  g[body_ids,v,:3]=pos[:,v];g[body_ids,3+v,:3]=nor[:,v];g[body_ids,11+v]=attrs[:,v,0]
  tangent=attrs[:,v,0,:3];uv0=g[body_ids,6,:2] if v==0 else g[body_ids,6,2:] if v==1 else g[body_ids,7,:2]
  uv=np.where((rules[:,0]==0)[:,None],uv0,np.where((rules[:,0]==1)[:,None],attrs[:,v,2,:2],np.where((rules[:,0]==2)[:,None],attrs[:,v,3,:2],attrs[:,v,3,2:])))
  z=np.sqrt(np.maximum(0,1-np.minimum(1,np.sum(uv*uv,axis=1))))
  direction=uv[:,0,None]*tangent+uv[:,1,None]*attrs[:,v,0,3,None]*np.cross(nor[:,v],tangent)+z[:,None]*nor[:,v]
  g[body_ids,8+v,:3]=unit(direction)
 source=meta[hulls,3]>>8;hm=meta[hulls,1];hr=mats.view('<u4')[hm,field('zzz_outline_rules')];g[hulls]=g[source]
 g.view('<u4')[hulls,23]=meta[hulls]
 negative=np.sum(np.cross(g[source,1,:3]-g[source,0,:3],g[source,2,:3]-g[source,0,:3])*np.sum(g[source,3:6,:3],axis=1),axis=1)<0
 g.view('<u4')[hulls,23,2]=(meta[hulls,2]&np.uint32(0xfffffffb))|np.where(negative,4,0).astype(np.uint32)
 for v in range(3):
  p=g[source,v,:3];direction=g[source,8+v];delta=p-origin
  z=np.where(hr[:,3]!=0,direction[:,:3]@back,-.0001)
  n=unit(np.stack([direction[:,:3]@right,direction[:,:3]@up,z],axis=1))
  fov=np.where(hr[:,2]!=0,1,2.414*np.tan(st['fov']*np.pi/360));off=(.001+.009*np.clip(1+(delta@back)*fov,0,1))*.01
  g[hulls,v,:3]=p+delta*off[:,None]+mats[hm,field('edge'),1,None]*direction[:,3,None]*(n[:,0,None]*right+n[:,1,None]*up+z[:,None]*back)
 materials=mats.copy()
 for i,name in enumerate(pose_engine.material_names):
  if 'Face' not in name:continue
  for mid in np.unique(mi[pose_engine.mid==i]):
   materials[mid,field('zzz_head_forward'),:3]=unit(pose['head_forward']);materials[mid,field('zzz_head_right'),:3]=unit(pose['head_right'])
 if shared:mapped.write(counts,frame_params,g,materials)
 elif not pipe:
  with packet.open('wb') as f:
   f.write(b'NPTFRM01');f.write(struct.pack('<2I',counts[0],counts[2]));f.write(frame_params);f.write(memoryview(g).cast('B'));f.write(memoryview(materials).cast('B'))
 if index+1<len(frames):pending=pool.submit(bake_frame,frames[index+1],D/f'asset-{card}-{(index+1)%2}')
 if pipe:
  send_frame(engine.stdin,counts,frame_params,g,materials)
  image,report=receive_image(engine.stdout)
 elif shared:
  engine.stdin.write((str(mapped.paths[0])+'\t'+str(mapped.paths[1])+'\n').encode());engine.stdin.flush();wait_for('DONE\t')
  image,report=mapped.receive()
 else:
  ppm=video/f'cloudgpu{card}.ppm'
  engine.stdin.write((str(packet)+'\t'+str(ppm)+'\n').encode());engine.stdin.flush();wait_for('DONE\t')
  with Image.open(ppm) as source:image=source.copy()
  report=json.loads(Path(str(ppm)+'.json').read_text())
 report.update(frame=frame,time_seconds=frame/FPS,clip=st['clip'],source_frame=st['source_frame'],page=st['page'])
 if saved is not None:saved.result()
 if async_save:saved=save_pool.submit(save_result,image,png,report,t)
 else:save_result(image,png,report,t)
 elapsed=time.perf_counter()-t;timings.append(dict(frame=frame,dispatch_seconds=elapsed,render_seconds=report['render_seconds']))
 print(frame,round(elapsed,3),'save_queued' if async_save else 'saved',flush=True)
pool.shutdown(wait=True)
if saved is not None:saved.result()
save_pool.shutdown(wait=True)
steady_seconds=time.perf_counter()-measure_started if measure_started is not None else None
if not pipe:engine.stdin.write(b'QUIT\n');engine.stdin.flush()
engine.stdin.close();engine.wait();assert engine.returncode==0
if mapped is not None:mapped.close()
renderlog.close();errors.close()
(out/f'timing-gpu{card}.json').write_text(json.dumps(dict(transport=transport,async_save=async_save,legacy_sync=env.get('NPT_LEGACY_SYNC')=='1',wall_seconds=time.perf_counter()-job_started,steady_seconds=steady_seconds,frames=timings),indent=2))
print('ALL_FRAMES_DONE',card,flush=True)
if card==0 and not probe and not benchmark:
 while len(list(video.glob('[0-9][0-9][0-9][0-9].png')))<FRAMES:time.sleep(5)
 subprocess.run(['ffmpeg','-y','-framerate','120','-i',str(video/'%04d.png'),'-c:v','libx264','-preset','medium','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(out/'remielle-swimsuit-three-pages-3k-120fps-256spp.mp4')],check=True);print('VIDEO_DONE',flush=True)
