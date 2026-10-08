import struct,zlib,json
from pathlib import Path
import numpy as np
class Reader:
 def __init__(self,p):
  self.b=p.read_bytes();self.o=23;self.version=self.read('I');self.wide=self.version>=7500
 def read(self,f):
  n=struct.calcsize('<'+f);v=struct.unpack_from('<'+f,self.b,self.o);self.o+=n;return v[0] if len(v)==1 else v
 def prop(self):
  t=chr(self.read('B'))
  fm={'Y':'h','C':'?','I':'i','F':'f','D':'d','L':'q'}
  if t in fm:return self.read(fm[t])
  if t in 'SR':
   n=self.read('I');v=self.b[self.o:self.o+n];self.o+=n;return v.decode('utf8','replace') if t=='S' else v
  if t in 'fdlibc':
   n,enc,size=self.read('III');raw=self.b[self.o:self.o+size];self.o+=size
   if enc:raw=zlib.decompress(raw)
   return np.frombuffer(raw,dtype={'f':'<f4','d':'<f8','l':'<i8','i':'<i4','b':'u1','c':'u1'}[t],count=n)
  raise ValueError(t)
 def node(self):
  end,n,plen=self.read('QQQ' if self.wide else 'III');length=self.read('B')
  if not end:return None
  name=self.b[self.o:self.o+length].decode();self.o+=length;props=[self.prop() for _ in range(n)];children=[]
  while self.o<end:
   x=self.node()
   if x is None:break
   children.append(x)
  self.o=end;return {'name':name,'props':props,'children':children}
 def all(self):
  out=[]
  while True:
   n=self.node()
   if n is None:break
   out.append(n)
  return out

def child(n,key):return next((x for x in n['children'] if x['name']==key),None)
def scalar(n,key,default=None):
 x=child(n,key);return x['props'][0] if x and x['props'] else default

def analyze(p):
 r=Reader(p);tree=r.all();objects=next(n for n in tree if n['name']=='Objects')['children'];byid={n['props'][0]:n for n in objects}
 con=next(n for n in tree if n['name']=='Connections')['children'];connections=[x['props'] for x in con]
 report={'file':p.name,'fbx_version':r.version,'counts':{},'meshes':[],'materials':[],'textures':[],'bones':[],'animation_stacks':[]}
 for n in objects:
  kind=n['name'];report['counts'][kind]=report['counts'].get(kind,0)+1;prop=n['props'];name=prop[1].replace('\x00\x01','::') if len(prop)>1 and isinstance(prop[1],str) else str(prop[0])
  if kind=='Geometry' and len(prop)>2 and prop[2]=='Mesh':
   verts=scalar(n,'Vertices');indices=scalar(n,'PolygonVertexIndex');layers=[]
   for l in n['children']:
    if l['name'] not in ['LayerElementUV','LayerElementColor','LayerElementNormal','LayerElementTangent','LayerElementBinormal','LayerElementMaterial']:continue
    arrays={}
    for a in l['children']:
     if a['props'] and isinstance(a['props'][0],np.ndarray):
      v=a['props'][0];arrays[a['name']]={'elements':len(v),'min':float(v.min()) if len(v) else None,'max':float(v.max()) if len(v) else None}
    layers.append({'kind':l['name'],'index':l['props'][0],'name':scalar(l,'Name'),'mapping':scalar(l,'MappingInformationType'),'reference':scalar(l,'ReferenceInformationType'),'arrays':arrays})
   polys=int((indices<0).sum());lengths=np.diff(np.r_[-1,np.flatnonzero(indices<0)])
   owners=[c[2] for c in connections if len(c)>2 and c[1]==prop[0] and c[2] in byid and byid[c[2]]['name']=='Model']
   mats=[]
   for model in owners:
    for c in connections:
     if len(c)>2 and c[2]==model and c[1] in byid and byid[c[1]]['name']=='Material':mats.append(byid[c[1]]['props'][1].replace('\x00\x01','::'))
   report['meshes'].append({'name':name,'id':prop[0],'vertices':len(verts)//3,'polygons':polys,'triangles_after_triangulation':int(np.maximum(lengths-2,0).sum()),'bounds':[verts.reshape(-1,3).min(axis=0).tolist(),verts.reshape(-1,3).max(axis=0).tolist()],'materials':mats,'layers':layers})
  if kind=='Material':report['materials'].append({'id':prop[0],'name':name})
  if kind=='Texture':report['textures'].append({'id':prop[0],'name':name,'file':scalar(n,'FileName'),'relative_file':scalar(n,'RelativeFilename'),'connections':[c for c in connections if len(c)>2 and c[1]==prop[0]]})
  if kind=='Model' and len(prop)>2 and prop[2]=='LimbNode':report['bones'].append(name)
  if kind=='AnimationStack':report['animation_stacks'].append(name)
 return report
if __name__=='__main__':
 root=Path('outputs/ye-shunguang-hoyotoon-assets');result=[analyze(p) for p in (root/'original').glob('*.fbx')];(root/'fbx_inventory.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
 for r in result:
  print(r['file'],r['counts']);print('MESHES',[(m['name'],m['vertices'],m['triangles_after_triangulation'],[(x['kind'],x['index']) for x in m['layers']]) for m in r['meshes']]);print('TEXTURES',r['textures'][:3]);print('MATERIALS',r['materials'][:4])
