"""Build a backend-local static package from HoyoToon FBX+PNG+Unity JSON.
Original files are read only. FBX geometry uses its bind pose by default; --pose
bakes explicitly authored bone edits. Animation/game scripting are not inferred.
Requires numpy.
"""
from pathlib import Path
import argparse,json,struct,hashlib
import numpy as np
from fbx_static import Reader,child,scalar
args=argparse.ArgumentParser();args.add_argument('--source',required=True);args.add_argument('--output',required=True);args.add_argument('--face-lightmap',choices=['Female_Face_Lightmap.png','Female_Face_Lightmap_02.png'],default='Female_Face_Lightmap_02.png');args.add_argument('--extended',action='store_true');args.add_argument('--matcap-bindings',help='Explicit candidate PathID-to-filename JSON, only with --extended');args.add_argument("--pose",help="Authored bone rotations and optional weapon placement JSON; not original animation");a=args.parse_args()
src=Path(a.source).resolve();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
if a.matcap_bindings and not a.extended:raise ValueError('--matcap-bindings requires --extended')
matcap_preset=json.loads(Path(a.matcap_bindings).read_text()) if a.matcap_bindings else None
fbx=src/'Avatar_Female_Size02_Zhenzhen.fbx';reader=Reader(fbx);tree=reader.all();objs=next(x for x in tree if x['name']=='Objects')['children'];byid={x['props'][0]:x for x in objs};con=[x['props'] for x in next(x for x in tree if x['name']=='Connections')['children']]
pose_document=json.loads(Path(a.pose).read_text()) if a.pose else None
if pose_document:
 from fbx_pose import SkinPose
 skin_pose=SkinPose(objs,con,pose_document)
def name(n):return n['props'][1].split('\x00')[0]
def unit(v):
 norm=np.linalg.norm(v);return v/norm if norm>1e-15 else np.array([0.,0.,1.])
def rotation(v):return np.array([v[0],v[2],-v[1]])
def array(n,kind,index,key,width,vertex,corner,polygon):
 layer=next((c for c in n['children'] if c['name']==kind and c['props'][0]==index),None)
 if not layer:return None
 values=scalar(layer,key);values=values.reshape(-1,width);mapping=scalar(layer,'MappingInformationType');ref=scalar(layer,'ReferenceInformationType');i={'ByVertice':vertex,'ByVertex':vertex,'ByPolygonVertex':corner,'ByPolygon':polygon,'AllSame':0}[mapping]
 if ref=='IndexToDirect':
  ix=scalar(layer,key+'Index',scalar(layer,key[:-1]+'Index'))
  if ix is None:raise ValueError('Missing layer indices')
  i=int(ix[i])
 return values[i].copy()
materials={};diffuse={}
for n in objs:
 if n['name']!='Texture':continue
 for c in con:
  if len(c)>3 and c[1]==n['props'][0] and c[3]=='DiffuseColor':diffuse[name(byid[c[2]])]=scalar(n,'FileName')
def build_material(key):
 if key in materials:return
 path=src/'Materials'/f'{key}.json';j=json.loads(path.read_text());s=j['m_SavedProperties'];floats=s.get('m_Floats') or {};colors=s.get('m_Colors') or {}
 typ=1 if 'Face' in key else 2 if any(x in key for x in ['Eye_UI','Eyebrows']) else 4 if 'Hair' in key else 0
 base=diffuse.get(key)
 if not base:raise ValueError('No confirmed FBX diffuse binding: '+key)
 prefix=Path(base).stem.removesuffix('_D')
 binding={'diffuse':str(src/base),'diffuse_evidence':'FBX DiffuseColor connection'}
 entry={'zzz':True,'zzz_type':typ,'alpha':1,'double_sided':True,'casts_shadow':True,'edge_enabled':typ not in [2,3],'edge_size':floats.get('_OutlineWidth',1),'edge_color':[0,0,0,1],'source_material_json':str(path),'source_floats':floats,'source_colors':colors,'reflection_mix':0,'zzz_alpha':key.endswith('_T_UI')}
 for field,suffix in [('normal_texture','N'),('metallic_texture','M'),('roughness_texture','A')]:
  p=src/'Textures'/f'{prefix}_{suffix}.png'
  if p.exists() and typ not in [1,2]:entry[field]=str(p);binding[field]={'file':str(p),'evidence':'same texture family as confirmed diffuse; Unity PathID filename mapping absent'}
 if typ==1:
  face=src/'Textures'/a.face_lightmap
  if not face.exists():raise ValueError('Missing face lightmap: '+str(face))
  entry['face_lightmap']=str(face);entry['head_forward']=[0,0,1];entry['head_right']=[1,0,0]
  if pose_document:
   frame=skin_pose.bone_frame('Bip001 Head')[:3,:3]
   rest=skin_pose.render_from_world[:3,:3]@skin_pose.bind[skin_pose.names['Bip001 Head']][:3,:3]
   head_rotation=frame@np.linalg.inv(rest)
   entry['head_forward']=(head_rotation@np.array([0,0,1])).tolist();entry['head_right']=(head_rotation@np.array([1,0,0])).tolist()
  binding['face_lightmap']={'file':str(face),'evidence':'explicit candidate selection; original Unity texture ID mapping unavailable','alternatives':['Female_Face_Lightmap.png','Female_Face_Lightmap_02.png']}
 if a.extended:
  # Face alpha encodes nose detail, not whole-face transparency. Hair alpha
  # used for eye visibility is independent of its ordinary surface coverage.
  role=2 if 'Eyebrows' in key else 1 if 'Eye_UI' in key else 0
  clip=bool(floats.get('_AlphaClip',0))
  blend=key.endswith('_T_UI') or role!=0
  entry.update(zzz_extended=True,opacity_mode=1 if clip else 2 if blend else 0,alpha_source=1 if key.endswith('_T_UI') else 0,eye_role=role,eye_reveal_depth=0.08,effect_time=0)
  binding['visibility']={'evidence':'Metal explicit policy: _AlphaClip -> clipping; _T_UI -> A.R blending; eye/brow -> D.a blending; Face D.a reserved for nose line','source_cull':floats.get('_Cull',0)}
  slots=[];records=[]
  for i in range(5):
   field='_MatCapTex'+(str(i+1) if i else '')
   env=(s.get('m_TexEnvs') or {}).get(field,{})
   ref=env.get('m_Texture',{})
   target=(matcap_preset or {}).get('path_ids',{}).get(str(ref.get('m_PathID',0))) if not ref.get('IsNull',True) else None
   if target:
    target_path=src/'Textures'/target
    if not target_path.is_file():raise ValueError('Missing MatCap candidate: '+str(target_path))
    slots.append(str(target_path))
    records.append({'slot':i,'source_reference':ref,'file':str(target_path),'evidence':matcap_preset.get('evidence','explicit authored binding')})
   else:slots.append(None)
  entry['matcap_textures']=slots
  binding['matcap']={'bindings':records,'status':'candidate validation preset, not verified original game pairing' if records else 'unbound; filename-to-Unity-ID mapping unavailable'}
 if pose_document:
  override=pose_document.get('materials',{}).get(key,{})
  entry['authored_floats']=override.get('floats',{})
  entry['authored_colors']=override.get('colors',{})
 entry['binding_evidence']=binding;materials[key]=entry
mtl=[];obj=['mtllib character.mtl'];attrs=[];counts={};excluded=[];idx=0;min_height=0.037648990750312805
for n in objs:
 if n['name']!='Geometry' or len(n['props'])<3 or n['props'][2]!='Mesh':continue
 key=name(n)
 is_weapon='Weapon' in key
 selected_weapon=pose_document and key==pose_document.get('weapon',{}).get('mesh')
 if (is_weapon and not selected_weapon) or any(x in key for x in ['_Pro','HairShadow','Leg_FX','_Jiao']):excluded.append(key);continue
 owners=[c[2] for c in con if len(c)>2 and c[1]==n['props'][0] and c[2] in byid and byid[c[2]]['name']=='Model'];assert len(owners)==1
 mats=[name(byid[c[1]]) for c in con if len(c)>2 and c[2]==owners[0] and c[1] in byid and byid[c[1]]['name']=='Material']
 # Check exported mesh transform: these geometries share bind-pose coordinates.
 props={x['props'][0]:x['props'][4:] for x in (child(byid[owners[0]],'Properties70') or {'children':[]})['children']}
 if np.linalg.norm(props.get('Lcl Translation',[0,0,0]))>1e-5:raise ValueError('Unexpected mesh translation')
 verts=scalar(n,'Vertices').reshape(-1,3)
 posed_verts,normal_transforms=skin_pose.deform(n,verts) if pose_document else (verts,None)
 normal_matrices=np.linalg.inv(normal_transforms).transpose(0,2,1) if pose_document else None
 indices=scalar(n,'PolygonVertexIndex');polygon=0;corners=[];kept=0
 for corner,index in enumerate(indices):
  end=index<0;vertex=int(-index-1 if end else index);corners.append((vertex,corner))
  if not end:continue
  ml=next(x for x in n['children'] if x['name']=='LayerElementMaterial');mi=scalar(ml,'Materials');mat=mats[int(mi[polygon] if scalar(ml,'MappingInformationType')=='ByPolygon' else mi[0])];build_material(mat)
  for k in range(1,len(corners)-1):
   tri=[corners[0],corners[k],corners[k+1]];positions=[rotation(posed_verts[v])+[0,min_height,0] for v,c in tri]
   if np.linalg.norm(np.cross(positions[1]-positions[0],positions[2]-positions[0]))<1e-12:continue
   obj.append('usemtl '+mat);record=[];faces=[]
   for (v,c),pos in zip(tri,positions):
    raw_normal=array(n,'LayerElementNormal',0,'Normals',3,v,c,polygon)
    ns=unit(rotation(normal_matrices[v]@raw_normal if pose_document else raw_normal))
    tangent=array(n,'LayerElementTangent',0,'Tangents',3,v,c,polygon);bitangent=array(n,'LayerElementBinormal',0,'Binormals',3,v,c,polygon)
    if pose_document:tangent=normal_transforms[v]@tangent;bitangent=normal_transforms[v]@bitangent
    tangent=unit(rotation(tangent));bitangent=unit(rotation(bitangent));hand=1. if np.dot(np.cross(ns,tangent),bitangent)>=0 else -1.
    color=array(n,'LayerElementColor',0,'Colors',4,v,c,polygon);uv=array(n,'LayerElementUV',0,'UV',2,v,c,polygon)
    # Preserve the original four UV sets used by HoyoToon's selectors. Do not
    # reinterpret or author their values; UV3 is decoded by the backend exactly
    # as vs_outline does, even when this FBX duplicates another UV layer.
    uv1=array(n,'LayerElementUV',1,'UV',2,v,c,polygon)
    if uv1 is None:uv1=uv
    blue=int(round(float(color[2])*255));face_flag=1. if blue&16 else 0.
    uv2=array(n,'LayerElementUV',2,'UV',2,v,c,polygon)
    uv3=array(n,'LayerElementUV',3,'UV',2,v,c,polygon)
    if uv2 is None:uv2=uv
    if uv3 is None:uv3=uv
    record.extend([*tangent,hand,*color,*uv1,face_flag,0.9-0.2*(blue&3),*uv2,*uv3]);idx+=1
    obj.extend(['v '+' '.join(f'{x:.9g}' for x in pos),'vt '+' '.join(f'{x:.9g}' for x in uv),'vn '+' '.join(f'{x:.9g}' for x in ns)])
    faces.append(f'{idx}/{idx}/{idx}')
   obj.append('f '+' '.join(faces));attrs.append(record);kept+=1
  corners=[];polygon+=1
 counts[key]=kept
for key,entry in materials.items():mtl.extend(['newmtl '+key,'Kd 1 1 1','d 1','illum 1','map_Kd '+entry['binding_evidence']['diffuse'],''])
(out/'character.obj').write_text('\n'.join(obj)+'\n');(out/'character.mtl').write_text('\n'.join(mtl))
with (out/'attributes.bin').open('wb') as f:f.write(b'ZZZATTR3');f.write(struct.pack('<I',len(attrs)));f.write(np.array(attrs,dtype='<f4').tobytes())
(out/'character.zzz.json').write_text(json.dumps({'asset_kind':'zzz_authored_pose' if pose_document else 'zzz_static_bind_pose','authored_pose':pose_document,'pose_validation':skin_pose.report if pose_document else None,'triangle_attributes':'attributes.bin','source_fbx':str(fbx),'source_fbx_sha256':hashlib.sha256(fbx.read_bytes()).hexdigest(),'coordinate_transform':'FBX geometry (x,z,-y), feet +0.03764899m','excluded_meshes':excluded,'mesh_triangle_counts':counts,'materials':materials,'extended':a.extended,'matcap_preset':matcap_preset,'scope':'Static FBX D/N/M/A and face SDF. Extended: face G highlights, raw D.a nose line, layered opacity/clipping/culling, depth-limited hair eye reveal, explicit five-slot MatCap and screen-depth rim. Unity stencil/animation/FX not implemented.' if a.extended else 'v5 static package with face SDF and hair-only face shadows.'},ensure_ascii=False,indent=2))
if pose_document:
 print('Authored pose: FBX cluster skinning; normal/tangent frames updated; source UV/color retained')
print('Prepared',len(attrs),'triangles,',len(materials),'materials; excluded',excluded)

(out/'README.md').write_text('''# Metal ZZZ static asset package

Original FBX/PNG/JSON are read only. Geometry is the static bind pose.

Extended mode uses source face G and raw D.a for highlights/nose line, source cull state, A.R for transparent body/hair and D.a for eye/brow coverage. Eye visibility is a Metal ray adaptation, limited to hair occluders and 8 cm depth. It is not the Unity stencil pipeline.

MatCap paths are explicit candidates if a preset is supplied: anonymous Unity texture IDs cannot prove filename correspondence. The binding_evidence entries record this per material. All original scalar/color properties remain preserved.

Lighting used for validation is front-upper directional (0,0.4,1); it differs from the old overhead scene.
''')

if pose_document:
 (out/'README.md').write_text('''# Metal authored static pose

The original FBX/PNG/material JSON are read only. This package uses FBX cluster weights and inherited bone rotations to bake a manually authored pose. Normals use inverse-transpose skin transforms; tangents/binormals follow the linear skin transform. Weapon placement and scalar/color overrides are explicitly authored in character.zzz.json, not recovered game animation or official values. UV layers and vertex colors are preserved. Rendering still traces the posed geometry at every bounce.
''')
