#!/usr/bin/env python3
"""Render a back-facing quad: each original UV set selects a different colour.

This tests the actual GPU path, material selectors and ZZZATTR3 ABI together.
All assets here are generated test data, separate from the character package.
"""
import argparse
import json
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    metal = Path(__file__).resolve().parent
    colours = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
    texture = Image.new('RGBA', (4, 4))
    for y in range(4):
        for x in range(4):
            index = (2 if y < 2 else 0) + (1 if x >= 2 else 0)
            texture.putpixel((x, y), (*colours[index], 255))
    texture.save(out / 'quadrants.png')
    (out / 'quad.mtl').write_text('newmtl test\nKd 1 1 1\nmap_Kd quadrants.png\n')
    obj = ['mtllib quad.mtl', 'usemtl test']
    for x, y in [(-.7, 1.3), (.7, 1.3), (.7, 2.7), (-.7, 2.7)]:
        obj += [f'v {x} {y} 0', 'vt .25 .25', 'vn 0 0 -1']
    obj += ['f 1/1/1 2/2/2 3/3/3', 'f 1/1/1 3/3/3 4/4/4']
    (out / 'quad.obj').write_text('\n'.join(obj) + '\n')
    vertex = [1, 0, 0, 1, 1, 1, 0, 1, .75, .25, 0, .9, .25, .75, .75, .75]
    (out / 'attributes.bin').write_bytes(b'ZZZATTR3' + struct.pack('<I', 2) +
                                        np.asarray(vertex * 6, dtype='<f4').tobytes())
    material = {'m_Name': 'UV-test', 'm_SavedProperties': {'m_Floats': {
        '_DoubleSided': 1, '_DoubleUV': 0, '_Cull': 0, '_Outline': 0, '_RimGlow': 0,
        '_MatCap': 0, '_Emission': 0}, 'm_Colors': {}, 'm_TexEnvs': {}}}
    package = {'triangle_attributes': 'attributes.bin', 'materials': {'test': {
        'zzz': True, 'zzz_extended': True, 'zzz_type': 2, 'opacity_mode': 0,
        'edge_enabled': False, 'source_material_json': str(out / 'source.json')}}}
    (out / 'materials.json').write_text(json.dumps(package))
    command = [str(metal / 'build/metal_accum'), '4', '0', '0', '48', '48', str(out / 'quad.obj'),
               '--mmd-materials', str(out / 'materials.json'), '--asset-toon', '--neutral-sky',
               '--no-tonemap', '--character-scene', '--headless', '--frames', '1', '--depth', '1',
               '--aperture', '0', '--fov', '28', '--camera', '0', '2', '3', '--target', '0', '2', '0',
               '--light-direction', '0', '0', '1', '--sun-intensity', '6.283185307',
               '--sphere-intensity', '0', '--environment-strength', '0', '--no-outline']
    report = {}
    for selector in range(4):
        material['m_SavedProperties']['m_Floats']['_DoubleUV'] = selector
        (out / 'source.json').write_text(json.dumps(material))
        image = out / f'uv{selector}.png'
        subprocess.run(command + ['--output', str(image)], check=True)
        pixel = np.asarray(Image.open(image).convert('RGB'))[24, 24].astype(int)
        assert np.max(np.abs(pixel - colours[selector])) <= 1, (selector, pixel)
        report[f'uv{selector}'] = pixel.tolist()
    (out / 'validation.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
