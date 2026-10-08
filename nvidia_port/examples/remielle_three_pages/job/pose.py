"""Evaluate official base + default + swimsuit curves and Unity mesh skinning."""
from pathlib import Path
import json
import numpy as np
from rig_math import qmat
D=Path(__file__).resolve().parent.parent/'data'
def unit(x):return x/np.maximum(np.linalg.norm(x,axis=-1,keepdims=True),1e-15)
def blend_values(a,b,w):
 v=a*(1-w)+b*w
 if a.ndim==2 and a.shape[-1]==10:
  qa=a[:,:4];qb=b[:,:4];dot=np.sum(qa*qb,axis=-1);qb=np.where((dot<0)[:,None],-qb,qb);dot=np.abs(dot).clip(0,1);angle=np.arccos(dot);sn=np.sin(angle);safe=np.where(sn<1e-8,1,sn)
  q=(np.sin((1-w)*angle)[:,None]*qa+np.sin(w*angle)[:,None]*qb)/safe[:,None];q=np.where((sn<1e-8)[:,None],qa*(1-w)+qb*w,q);v[:,:4]=unit(q)
 return v
class PoseEngine:
 def __init__(self,*unused):
  self.a=json.load(open(D/'avatar.json'));self.paths=[self.a['tos'][str(h)] for h in self.a['skeleton']['ids']];self.lookup={p:i for i,p in enumerate(self.paths)};self.hashes={h:i for i,h in enumerate(self.a['skeleton']['ids'])};self.default=np.array(self.a['default'],float);self.rest=self.world(self.default);self.reflect=np.diag([-1.,1,1]);self.clips={};self.meshes=[];self.names=[];self.parts=[]
  morphs=json.load(open(D/'morphs.json'));bindings=json.load(open(D/'render-bindings.json'));self.excluded=[];self.rest_errors=[]
  for name,b in bindings.items():
   if not b['enabled'] or name=='Remielle_HairShadow':self.excluded.append(name);continue
   m={k:v for k,v in np.load(D/'meshes'/(name+'.npz')).items()};bones=np.array([self.hashes[int(h)] for h in m['bone_name_hashes']]);m['rig_bones']=bones;m['morphs']=morphs[name];m['name']=name
   products=self.rest[bones]@m['bind_poses'];err=np.max(np.abs(products-products[0]));assert err<1e-4,(name,err);self.rest_errors.append(float(err))
   tris=[];mats=[]
   for i,mat in enumerate(b['materials']):
    tri=m[f'triangles_{i}'][:,[0,2,1]];p=m['vertices'][tri];valid=np.linalg.norm(np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]),axis=1)>1e-12;tri=tri[valid];tris.append(tri);mats.extend([mat['name']]*len(tri))
   m['triangles']=np.concatenate(tris);self.names.extend(mats);self.parts.extend([name]*len(mats));self.meshes.append(m)
  self.material_names=list(dict.fromkeys(self.names));self.mid=np.array([self.material_names.index(x) for x in self.names]);self.head=self.lookup[next(p for p in self.paths if p.endswith('/Bip001 Head'))]
 def world(self,local):
  result=[]
  for i,v in enumerate(local):
   m=np.eye(4);m[:3,:3]=qmat(v[3:7])@np.diag(v[7:]);m[:3,3]=v[:3];p=self.a['skeleton']['nodes'][i][0];result.append(result[p]@m if p>=0 else m)
  return np.array(result)
 def load(self,clip,part):
  key=clip+part
  if key not in self.clips:
   self.clips[key]=({k:v for k,v in np.load(D/'animations'/(key+'.npz')).items()},json.load(open(D/'animations'/(key+'.json'))))
  return self.clips[key]
 def sample(self,arr,frame):
  at=np.clip(frame,0,len(arr)-1);lo=int(at);return blend_values(arr[lo],arr[min(lo+1,len(arr)-1)],at-lo)
 def local_pose(self,clip,frame):
  local=np.concatenate([self.default[:,3:7],self.default[:,:3],self.default[:,7:]],axis=1);scalar={};missing=[]
  for suffix in ['-body','-default','']:
   anim,info=self.load(clip,suffix);trans=self.sample(anim['transforms'],frame);values=self.sample(anim['scalars'],frame)
   for path,t in zip(info['paths'],trans):
    i=self.lookup.get(path)
    if i is None:missing.append(path);continue
    local[i]=t
   for binding,value in zip(info['scalar_bindings'],values):
    if binding['cls']==137:scalar[(self.a['tos'][str(binding['path'])].split('/')[-1],binding['attribute'])]=float(value)
  return local,scalar,missing
 def evaluate(self,clip,frame,blend=None):
  local,scalar,missing=self.local_pose(clip,frame)
  if blend:
   prev,oldframe,w=blend;old,oldscalar,_=self.local_pose(prev,oldframe)
   # Clips omit different constant tracks. Blend poses aligned to the avatar,
   # never animation arrays whose track ordering or length can differ.
   local=blend_values(old,local,w)
   scalar={k:oldscalar.get(k,0)*(1-w)+scalar.get(k,0)*w for k in oldscalar.keys()|scalar.keys()}
  local=np.concatenate([local[:,4:7],local[:,:4],local[:,7:]],axis=1)
  world=self.world(local);positions=[];normals=[];attributes=[];uvs=[];used=0
  for m in self.meshes:
   vertices=m['vertices'].copy()
   for shape in m['morphs']:
    w=scalar.get((m['name'],shape['hash']),0)/shape['full_weight'];used+=int(abs(w)>1e-8)
    if w:vertices[m[f'morph_{shape["index"]}_indices']]+=w*m[f'morph_{shape["index"]}_positions']
   matrices=world[m['rig_bones']]@m['bind_poses'];skin=np.sum(matrices[m['bone_indices']]*m['bone_weights'][:,:,None,None],axis=1);linear=skin[:,:3,:3];pp=np.einsum('nij,nj->ni',linear,vertices)+skin[:,:3,3];pp=pp@self.reflect
   nn=unit(np.einsum('nij,nj->ni',np.linalg.inv(linear).transpose(0,2,1),m['normals'])@self.reflect);tt=unit(np.einsum('nij,nj->ni',linear,m['tangents'][:,:3])@self.reflect);hand=-m['tangents'][:,3]
   col=m['colors'].astype(float)/(255 if np.issubdtype(m['colors'].dtype,np.integer) else 1);assert col.min()>=0 and col.max()<=1;blue=np.rint(col[:,2]*255).astype(int);base=m['uv0'];uv1=m.get('uv1',base);uv2=m.get('uv2',base);uv3=m.get('uv3',base);at=np.column_stack([tt,hand,col,uv1,(blue&16)!=0,.9-.2*(blue&3),uv2,uv3]);tri=m['triangles']
   positions.append(pp[tri]);normals.append(nn[tri]);attributes.append(at[tri].reshape(-1,3,4,4));uvs.append(base[tri])
  head=self.reflect@world[self.head,:3,:3]@np.linalg.inv(self.rest[self.head,:3,:3])
  return dict(positions=np.concatenate(positions),normals=np.concatenate(normals),attributes=np.concatenate(attributes),uv=np.concatenate(uvs),head_forward=head@np.array([0,0,1]),head_right=head@np.array([1,0,0]),report=dict(clip=clip,source_frame=frame,source_seconds=frame/60,active_morphs=used,origin_only_tracks_skipped=len(missing),mesh_rest_error_max=max(self.rest_errors)))
 def write_obj(self,v,out,*unused):
  import shutil,struct
  out=Path(out);out.mkdir(exist_ok=True,parents=True);doc=json.load(open(D/'materials.json'));doc['original_animation']=v['report']
  def resolve_paths(value):
   if isinstance(value,dict):return {k:resolve_paths(x) for k,x in value.items()}
   if isinstance(value,list):return [resolve_paths(x) for x in value]
   if isinstance(value,str) and value.startswith(('textures/','materials/')):return str((D/value).resolve())
   return value
  doc=resolve_paths(doc)
  for name,entry in doc['materials'].items():
   if 'head_forward' in entry:entry['head_forward']=v['head_forward'].tolist();entry['head_right']=v['head_right'].tolist()
  (out/'character.zzz.json').write_text(json.dumps(doc,indent=2));(out/'character.mtl').write_text('\n'.join('map_Kd '+str((D/line[7:]).resolve()) if line.startswith('map_Kd textures/') else line for line in (D/'character.mtl').read_text().splitlines())+'\n')
  lines=['mtllib character.mtl'];pp=v['positions'].reshape(-1,3);nn=v['normals'].reshape(-1,3);uv=v['uv'].reshape(-1,2)
  lines.extend('v %.9g %.9g %.9g'%tuple(p) for p in pp);lines.extend('vt %.9g %.9g'%tuple(p) for p in uv);lines.extend('vn %.9g %.9g %.9g'%tuple(p) for p in nn);current=None
  for i,name in enumerate(self.names):
   if name!=current:lines.append('usemtl '+name);current=name
   lines.append('f '+' '.join(f'{k}/{k}/{k}' for k in range(i*3+1,i*3+4)))
  (out/'character.obj').write_text('\n'.join(lines)+'\n');(out/'attributes.bin').write_bytes(b'ZZZATTR3'+struct.pack('<I',len(self.names))+v['attributes'].astype('<f4').tobytes())
