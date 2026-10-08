"""GPU image integration checks for Metal's opt-in ZZZ extension.
Requires Pillow and numpy; writes only the requested output directory.
"""
from pathlib import Path
import argparse,json,subprocess
import numpy as np
from PIL import Image
parser=argparse.ArgumentParser();parser.add_argument('--asset',required=True);parser.add_argument('--output',required=True);parser.add_argument('--synthetic-only',action='store_true');args=parser.parse_args()
root=Path(__file__).resolve().parent.parent;out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True);asset=Path(args.asset).resolve()
exe=root/'metal/build/metal_accum';report={}
def run(name,package=asset,flags=(),camera=(.16,1.5,1.38),target=(.02,1.365,-.1),size=(960,540),spp=16):
 cmd=[str(exe),str(spp),'0','0',*map(str,size),str(package/'character.obj'),'--mmd-materials',str(package/'character.zzz.json'),'--character-scene','--headless','--frames','1','--aperture','0','--fov','28','--camera',*map(str,camera),'--target',*map(str,target),'--light-direction','0','0.4','1','--output',str(out/(name+'.png')),*flags]
 result=subprocess.run(cmd,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 print(name+': '+result.stdout.strip(),flush=True)
 if result.returncode:raise RuntimeError(name+' failed')
 return np.asarray(Image.open(out/(name+'.png')).convert('RGB'))
def difference(a,b):
 d=np.abs(a.astype(int)-b.astype(int));return {'changed_pixels':int(np.any(d,axis=2).sum()),'max_byte_difference':int(d.max()),'mean_byte_difference':float(d.mean())}
if args.synthetic_only:
 report=json.loads((out/'validation.json').read_text())
else:
 base=run('front')
 for field,flag in [('face_highlight','--no-face-highlight'),('nose_line','--no-nose-line'),('eye_layers','--no-eye-layers'),('transparency','--no-transparency'),('matcap','--no-matcap'),('rim','--no-rim')]:
  report[field]=difference(base,run('no-'+field,flags=(flag,)))
 for name,camera in [('side',(3.95,1.05,.85)),('back',(0,.98,-4.55))]:run(name,camera=camera,target=(0,.82,-.18))
 # A real old-format image must remain identical, including stochastic samples.
 legacy=asset.parent/'ye-shunguang-metal-zzz'
 reference=np.asarray(Image.open(out.parent/'toon-v5-backup/old-package.png').convert('RGB'))
 # Do the compatibility run with the reference's exact original CLI, without light override.
 cmd=[str(exe),'16','0','0.15','640','360',str(legacy/'character.obj'),'--mmd-materials',str(legacy/'character.zzz.json'),'--character-scene','--headless','--frames','1','--aperture','0','--fov','28','--camera','.16','1.5','1.38','--target','.02','1.365','-.1','--output',str(out/'legacy-exact.png')]
 r=subprocess.run(cmd,cwd=root,text=True,capture_output=True);print('legacy-exact: '+r.stdout,flush=True)
 if r.returncode:raise RuntimeError(r.stderr)
 report['legacy']=difference(reference,np.asarray(Image.open(out/'legacy-exact.png').convert('RGB')))
 assert report['legacy']['changed_pixels']==0,report['legacy']
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
# Synthetic overlapping quads isolate alpha, cutoff, and culling from lighting.
syn=out/'synthetic';syn.mkdir(exist_ok=True)
Image.new('RGBA',(2,2),(255,0,0,128)).save(syn/'red.png');Image.new('RGBA',(2,2),(0,0,255,255)).save(syn/'blue.png');Image.new('RGBA',(2,2),(0,255,0,255)).save(syn/'green.png');Image.new('RGBA',(2,2),(128,0,0,255)).save(syn/'mask.png')
def entry(typ=2,role=0,cull=0,mode=0):return {'zzz':True,'zzz_extended':True,'zzz_type':typ,'alpha':1,'edge_enabled':False,'opacity_mode':mode,'eye_role':role,'roughness_texture':str(syn/'mask.png') if typ==4 else None,'source_floats':{'_Cull':cull,'_RimGlow':0,'_MatCap':0,'_Outline':0,'_Cutoff':.75},'source_colors':{}}
def scene(front,occluder=False,far=False):
 obj=['mtllib character.mtl'];idx=0
 for mat,z in [('front',0)]+([('block',-.01)] if occluder else [])+[('back',-.2 if far else -.02)]:
  obj.append('usemtl '+mat)
  for x,y in [(-.7,1.3),(.7,1.3),(.7,2.7),(-.7,2.7)]:obj.extend([f'v {x} {y} {z}','vt .5 .5','vn 0 0 1'])
  obj.extend([f'f {idx+1}/{idx+1}/{idx+1} {idx+2}/{idx+2}/{idx+2} {idx+3}/{idx+3}/{idx+3}',f'f {idx+1}/{idx+1}/{idx+1} {idx+3}/{idx+3}/{idx+3} {idx+4}/{idx+4}/{idx+4}']);idx+=4
 (syn/'character.obj').write_text('\n'.join(obj)+'\n');(syn/'character.mtl').write_text('newmtl front\nKd 1 1 1\nmap_Kd '+str(syn/('green.png' if front['zzz_type']==4 else 'red.png'))+'\nnewmtl back\nKd 1 1 1\nmap_Kd '+str(syn/'blue.png')+'\nnewmtl block\nKd 1 1 1\nmap_Kd '+str(syn/'green.png')+'\n')
 (syn/'character.zzz.json').write_text(json.dumps({'materials':{'front':front,'back':entry(role=1),'block':entry(typ=0)}}))
def synthetic(name,flags=()):return run(name,package=syn,flags=('--no-outline','--no-pbr',*flags),camera=(0,2,3),target=(0,2,0),size=(160,160),spp=4)
scene(entry(mode=2));a=synthetic('alpha-blend');p=a[80,80].astype(int);report['alpha_blend_center']=p.tolist();assert np.max(np.abs(p-[188,0,187]))<=2,p
scene(entry(mode=1));a=synthetic('alpha-clip');assert a[80,80,2]>250 and a[80,80,0]<2
scene(entry(cull=1));a=synthetic('cull-front');assert a[80,80,2]>250 and a[80,80,0]<2
scene(entry(cull=2));a=synthetic('cull-back');assert a[80,80,0]>250 and a[80,80,2]<2
scene(entry(typ=4));eye=synthetic('eye-reveal');off=synthetic('eye-hidden',('--no-eye-layers',));report['synthetic_eye_reveal']=difference(eye,off);assert eye[80,80,2]>off[80,80,2]+30
scene(entry(typ=4),occluder=True);blocked=synthetic('eye-body-blocked');blocked_off=synthetic('eye-body-blocked-off',('--no-eye-layers',));assert np.array_equal(blocked,blocked_off)
scene(entry(typ=4),far=True);far=synthetic('eye-too-far');far_off=synthetic('eye-too-far-off',('--no-eye-layers',));assert np.array_equal(far,far_off)
# All five MatCap regions must select their own explicit texture slot.
colors=[(255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,0,255),(0,255,255,255)]
paths=[]
Image.new('RGBA',(2,2),(255,255,255,255)).save(syn/'matcap-mask.png')
for i,c in enumerate(colors):
 path=syn/f'matcap-{i}.png';Image.new('RGBA',(2,2),c).save(path);paths.append(str(path))
for i,id_value in enumerate([.9,.7,.5,.3,.1]):
 Image.new('RGBA',(2,2),(round(id_value*255),0,0,255)).save(syn/'material.png')
 m=entry(typ=0);m['matcap_textures']=paths;m['metallic_texture']=str(syn/'material.png');m['roughness_texture']=str(syn/'matcap-mask.png');m['source_floats']['_MatCap']=1
 scene(m);a=synthetic(f'matcap-slot-{i}')
 assert np.max(np.abs(a[80,80].astype(int)-np.array(colors[i][:3])))<=1,(i,a[80,80])
report['five_matcap_slots']=True
report['synthetic_checks']={'alpha_blend':True,'alpha_clip':True,'front_cull':True,'back_cull':True,'hair_reveal':True,'body_occluder_reject':True,'depth_limit_reject':True}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
