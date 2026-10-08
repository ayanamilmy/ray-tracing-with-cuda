"""Export a static MMD package for the Metal backend, without saving the source blend.

Run with Blender --background --factory-startup --python this_file --
  --blend source.blend --pmx model.pmx --parser pmx_reference.py --output directory
The parser is the existing MMD Tools PMX reader, not a new game unpacker.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys

import bpy
from mathutils import Vector

args_parser = argparse.ArgumentParser()
for name in ['blend', 'pmx', 'parser', 'output']:
    args_parser.add_argument('--' + name, required=True)
args = args_parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
source = Path(args.blend).resolve()
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
spec = importlib.util.spec_from_file_location('mmd_pmx_reader', args.parser)
pmx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pmx)
model = pmx.load(str(Path(args.pmx).resolve()))
out = Path(args.output).resolve()
(out / 'textures').mkdir(parents=True, exist_ok=True)

bpy.ops.wm.open_mainfile(filepath=str(source))
bpy.context.scene.frame_set(1)
bpy.context.view_layer.update()
character = bpy.data.objects['叶瞬光 / official model']
evaluated = character.evaluated_get(bpy.context.evaluated_depsgraph_get())
snapshot = evaluated.to_mesh(preserve_all_data_layers=True,
                             depsgraph=bpy.context.evaluated_depsgraph_get())
if len(snapshot.vertices) != len(model.vertices):
    raise RuntimeError('Vertex order/count must match PMX to restore the highlight shell')

def position(v):
    p = evaluated.matrix_world @ v.co
    return Vector((p.x, p.z, -p.y))

positions = [position(v) for v in snapshot.vertices]
normal_matrix = evaluated.matrix_world.to_3x3().inverted().transposed()
def normal(n):
    n = normal_matrix @ n
    return Vector((n.x, n.z, -n.y)).normalized()

# Retain the evaluated body's split normals and original material ordering.
body_normals = []
for poly in snapshot.polygons:
    body_normals.append([normal(snapshot.corner_normals[i].vector) for i in poly.loop_indices])

def image(index):
    if index < 0:
        return None
    src = Path(model.textures[index].path)
    dest = out / 'textures' / src.name
    if not src.is_file():
        raise FileNotFoundError(src)
    shutil.copyfile(src, dest)
    return 'textures/' + src.name

materials = {}
mtl = ['# MMD static package, Metal backend only.']
for i, mat in enumerate(model.materials):
    name = 'm_%02d' % i
    base = image(mat.texture)
    toon = None if mat.is_shared_toon_texture else image(mat.toon_texture)
    sphere = image(mat.sphere_texture)
    if mat.is_shared_toon_texture and mat.toon_texture >= 0:
        raise RuntimeError('Supply the shared MMD toon texture explicitly')
    materials[name] = {
        'source_name': mat.name, 'alpha': mat.diffuse[3],
        'toon_texture': toon, 'sphere_texture': sphere,
        'sphere_mode': mat.sphere_texture_mode,
        'additive_overlay': mat.name == '髪+',
        'double_sided': mat.is_double_sided,
        'self_shadow': mat.enabled_self_shadow,
        'casts_shadow': mat.enabled_self_shadow_map,
    }
    mtl += ['newmtl ' + name, 'Kd ' + ' '.join(str(x) for x in mat.diffuse[:3]),
            'd ' + str(mat.diffuse[3]), 'illum 1']
    if base:
        mtl.append('map_Kd ' + base)
    mtl.append('')
(out / 'character.mtl').write_text('\n'.join(mtl), encoding='utf8')

# The original Blender study retained all vertices, but omitted the 髪+ faces.
# Restore those faces using the evaluated positions, so the shell has the SAME pose.
# Compute smooth shell normals from its deformed geometry instead of using rest normals.
shell_faces = []
offset = 0
for mat in model.materials:
    count = mat.vertex_count // 3
    if mat.name == '髪+':
        shell_faces = model.faces[offset:offset + count]
    offset += count
shell_normals = [Vector((0, 0, 0)) for v in model.vertices]
for a, b, c in shell_faces:
    n = (positions[b] - positions[a]).cross(positions[c] - positions[a])
    for idx in [a, b, c]:
        shell_normals[idx] += n
for n in shell_normals:
    if n.length_squared > 1e-20:
        n.normalize()

removed = 0
counts = {}
body_index = 0
with (out / 'character.obj').open('w', encoding='utf8') as file:
    file.write('mtllib character.mtl\n')
    for p in positions:
        file.write('v %.9g %.9g %.9g\n' % tuple(p))
    # PMX uses top-down V. OBJ and the renderer use bottom-up V.
    for v in model.vertices:
        file.write('vt %.9g %.9g\n' % (v.uv[0], 1 - v.uv[1]))
    normal_id = 0
    face_offset = 0
    for i, mat in enumerate(model.materials):
        file.write('usemtl m_%02d\n' % i)
        count = mat.vertex_count // 3
        kept = 0
        for face in model.faces[face_offset:face_offset + count]:
            if mat.name == '髪+':
                normals = [shell_normals[k] for k in face]
            else:
                normals = body_normals[body_index]
                body_index += 1
            a, b, c = [positions[k] for k in face]
            if (b - a).cross(c - a).length_squared <= 1e-24:
                removed += 1
                continue
            ids = []
            for idx, n in zip(face, normals):
                if n.length_squared <= 1e-20:
                    n = (b - a).cross(c - a).normalized()
                file.write('vn %.9g %.9g %.9g\n' % tuple(n))
                normal_id += 1
                ids.append('%d/%d/%d' % (idx + 1, idx + 1, normal_id))
            file.write('f ' + ' '.join(ids) + '\n')
            kept += 1
        counts['m_%02d' % i] = kept
        face_offset += count
if body_index != len(snapshot.polygons):
    raise RuntimeError('Body face order mismatch')
evaluated.to_mesh_clear()
if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
    raise RuntimeError('Source blend unexpectedly changed')
report = {'version': 1, 'materials': materials, 'face_counts': counts,
          'removed_zero_area_faces': removed, 'frame': 1,
          'base_uv_only': True, 'source_blend_sha256': source_hash,
          'source_pmx': str(Path(args.pmx).resolve()),
          'notes': 'MMD toon and additive sphere overlay; not original game/HoyoToon shading.'}
(out / 'character.mmd.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print('MMD_PACKAGE_READY', json.dumps({'triangles': sum(counts.values()),
      'highlight_triangles': counts.get('m_23', 0), 'removed': removed, 'output': str(out)}))
