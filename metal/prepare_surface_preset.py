"""Create clearly labelled, artist-authored surface maps for the Metal MMD demo.

These are NOT extracted game normal/PBR maps. The source MMD package contains
base-color/toon/sphere textures but no normal/metallic/roughness maps. We retain
its actual PMX outline settings and author a restrained fabric/gold preset.
Requires Pillow and NumPy. Never modifies the original OBJ, MTL, PMX or blend.
"""
import argparse
import importlib.util
import json
from pathlib import Path

import numpy as np
from PIL import Image

parser = argparse.ArgumentParser()
for name in ['asset', 'pmx', 'parser', 'output']:
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
asset, out = Path(args.asset).resolve(), Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'authored_maps').mkdir(exist_ok=True)
spec = importlib.util.spec_from_file_location('mmd_reader', args.parser)
pmx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pmx)
model = pmx.load(str(Path(args.pmx).resolve()))
document = json.loads((asset / 'character.mmd.json').read_text())
base_textures = {}
current = None
for line in (asset / 'character.mtl').read_text().splitlines():
    if line.startswith('newmtl '):
        current = line[7:]
    elif line.startswith('map_Kd '):
        base_textures[current] = asset / line[7:]

metal_candidates = {13, 14, 15, 16, 17, 18, 22}
fabric = {15, 16, 22}
for index, original in enumerate(model.materials):
    key = 'm_%02d' % index
    mat = document['materials'][key]
    for field in ['toon_texture', 'sphere_texture']:
        if mat[field]:
            mat[field] = str((asset / mat[field]).resolve())
    mat.update(edge_enabled=original.enabled_toon_edge,
               edge_size=original.edge_size, edge_color=list(original.edge_color),
               metallic=0, roughness=0.72, normal_strength=0,
               reflection_mix=0.12, normal_texture=None,
               metallic_texture=None, roughness_texture=None,
               metallic_channel=0, roughness_channel=0, normal_flip_y=False)
    # Preserve the authored eye/mouth layers and additive hair pass without PBR.
    if index <= 9 or index == 23:
        mat['reflection_mix'] = 0
    elif index in {10, 11, 12, 21}:
        mat['reflection_mix'] = 0.12
        mat['roughness'] = 0.72
    elif index in {19, 20}:
        mat['reflection_mix'] = 0.08
        mat['roughness'] = 0.65
    if index not in metal_candidates:
        continue
    image = Image.open(base_textures[key]).convert('RGBA')
    # Control-map resolution is independent of base-color resolution.
    image.thumbnail((1024, 1024), Image.Resampling.LANCZOS)
    rgba = np.asarray(image, dtype=np.float32) / 255
    r, g, b = rgba[..., 0], rgba[..., 1], rgba[..., 2]
    # Authored heuristic: only warm yellow/gold pigment is selected. This is
    # an editable demo mask, not a claim about the original game's material IDs.
    gold = np.clip(np.minimum.reduce([(r-b-.10)/.18, (g-b-.04)/.12,
                                     (r-.25)/.15, (g-.20)/.15]), 0, 1)
    gold *= (rgba[..., 3] > 0.1)
    metallic = gold * .95
    roughness = .78 * (1-gold) + .26 * gold
    for suffix, pixels in [('metallic', metallic), ('roughness', roughness)]:
        path = out / 'authored_maps' / (key + '_' + suffix + '.png')
        Image.fromarray(np.rint(pixels*255).astype(np.uint8)).save(path)
        mat[suffix + '_texture'] = str(path)
    mat['metallic'], mat['roughness'], mat['reflection_mix'] = 1, 1, .65
    if index in fabric:
        height, width = gold.shape
        u = (np.arange(width, dtype=np.float32)+.5) / width
        v = (np.arange(height, dtype=np.float32)+.5) / height
        # Small cloth relief: a smooth fold plus finer weave. Blue is reconstructed
        # so every texel is a unit tangent-space normal. Metal stays smooth.
        nx = np.broadcast_to(.15*np.sin(2*np.pi*12*u) + .035*np.sin(2*np.pi*96*u), (height,width)) * (1-gold)
        ny = np.broadcast_to((.10*np.sin(2*np.pi*10*v) + .035*np.sin(2*np.pi*96*v))[:,None], (height,width)) * (1-gold)
        nz = np.sqrt(np.maximum(1-nx*nx-ny*ny,0))
        rgb = np.stack([nx,ny,nz],axis=-1)*.5+.5
        path = out / 'authored_maps' / (key + '_normal.png')
        Image.fromarray(np.rint(rgb*255).astype(np.uint8)).save(path)
        mat['normal_texture'], mat['normal_strength'] = str(path), .5

document['version'] = 2
document['surface_map_provenance'] = 'Artist-authored procedural cloth normals and color-selected gold masks; not extracted game data.'
document['outline_provenance'] = 'Original PMX enabled_toon_edge, edge_size and edge_color; Visible-depth outlines; screen width = edge_size * image_height / 720 pixels, times outline_scale.'
document['character_self_shadows'] = False
document['notes'] = 'Toon with GGX direct highlights and approximate sky reflections. No traced object reflections in the Toon branch.'
(out / 'character.surface.json').write_text(json.dumps(document, ensure_ascii=False, indent=2))
print('SURFACE_PRESET_READY', out / 'character.surface.json')
