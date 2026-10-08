#!/usr/bin/env python3
"""GPU tests for the authored extensions. Original character assets are read only."""
import argparse
import json
import math
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
    root = metal.parent.parent
    fixture = out / 'fixture'
    fixture.mkdir(exist_ok=True)
    Image.new('RGBA', (2, 2), (100, 100, 100, 255)).save(fixture / 'base.png')
    Image.new('RGBA', (2, 2), (128, 128, 128, 255)).save(fixture / 'N.png')
    Image.new('RGBA', (2, 2), (128, 0, 180, 255)).save(fixture / 'M.png')
    Image.new('RGBA', (2, 2), (255, 200, 0, 255)).save(fixture / 'A.png')
    Image.new('RGBA', (2, 2), (255, 255, 255, 0)).save(fixture / 'nose.png')

    def prepare(kind, anisotropy=.75, tangent=(1, 0, 0), normal=(0, 0, 1)):
        floats = {'_Cull': 0, '_Outline': 0, '_RimGlow': 0, '_MatCap': 0, '_Emission': 0,
                  '_UseBumpMap': 0, '_Glossiness': 1, '_SpecIntensity': .03, '_Anisotropy': anisotropy,
                  '_NoseLineHoriDisp': .92, '_NoseLineLkDnDisp': .62}
        material = {'m_Name': 'synthetic', 'm_SavedProperties': {'m_Floats': floats,
                                                              'm_Colors': {}, 'm_TexEnvs': {}}}
        (fixture / 'source.json').write_text(json.dumps(material))
        obj = ['mtllib quad.mtl', 'usemtl test']
        for x, y in [(-.7, 1.3), (.7, 1.3), (.7, 2.7), (-.7, 2.7)]:
            obj += [f'v {x} {y} 0', 'vt .5 .5', 'vn ' + ' '.join(map(str, normal))]
        obj += ['f 1/1/1 2/2/2 3/3/3', 'f 1/1/1 3/3/3 4/4/4']
        (fixture / 'quad.obj').write_text('\n'.join(obj) + '\n')
        (fixture / 'quad.mtl').write_text('newmtl test\nKd 1 1 1\nmap_Kd ' +
                                        ('nose.png' if kind == 1 else 'base.png') + '\n')
        vertex = [*tangent, 1, 1, 1, 0, 1, .5, .5, 0, .9, .5, .5, .5, .5]
        (fixture / 'attributes.bin').write_bytes(b'ZZZATTR3' + struct.pack('<I', 2) +
                                               np.asarray(vertex * 6, dtype='<f4').tobytes())
        entry = {'zzz': True, 'zzz_extended': True, 'zzz_type': kind, 'opacity_mode': 0,
                 'source_material_json': str(fixture / 'source.json')}
        if kind == 4:
            entry.update(normal_texture=str(fixture / 'N.png'), metallic_texture=str(fixture / 'M.png'),
                         roughness_texture=str(fixture / 'A.png'))
        (fixture / 'materials.json').write_text(json.dumps({'triangle_attributes': 'attributes.bin',
                                                          'materials': {'test': entry}}))

    def run(name, obj, materials, camera=(0, 2, 3), target=(0, 2, 0), size=(64, 64),
            samples=4, frames=1, depth=1, intensity=1, environment=0, extra=()):
        output = out / (name + '.png')
        cmd = [str(metal / 'build/metal_accum'), str(samples), '0', '0', *map(str, size), str(obj),
               '--mmd-materials', str(materials), '--asset-toon', '--neutral-sky', '--no-tonemap',
               '--character-scene', '--headless', '--frames', str(frames), '--depth', str(depth),
               '--aperture', '0', '--fov', '28', '--camera', *map(str, camera), '--target', *map(str, target),
               '--light-direction', '0', '0', '1', '--sun-intensity', str(intensity),
               '--sphere-intensity', '0', '--environment-strength', str(environment),
               '--trace-tile-pixels', '32768', '--output', str(output), *extra]
        subprocess.run(cmd, check=True)
        return np.asarray(Image.open(output).convert('RGB'))

    def synth(name, **kwargs):
        return run(name, fixture / 'quad.obj', fixture / 'materials.json', extra=('--no-outline', *kwargs.pop('extra', ())), **kwargs)

    def difference(a, b):
        return float(np.mean(np.abs(a.astype(float) - b.astype(float))))

    report = {}
    prepare(4)
    x = synth('hair-tangent-x')
    isotropic = synth('hair-disabled', extra=('--no-anisotropy',))
    prepare(4, anisotropy=0)
    zero = synth('hair-zero')
    assert np.array_equal(zero, isotropic), 'Zero anisotropy must exactly preserve the original response'
    prepare(4, tangent=(0, 1, 0))
    y = synth('hair-tangent-y')
    report['hair_changes_with_parameter'] = difference(x, isotropic)
    report['hair_changes_with_strand_direction'] = difference(x, y)
    assert report['hair_changes_with_parameter'] > .01
    assert report['hair_changes_with_strand_direction'] > .01
    report['anisotropy_zero_matches_disabled'] = True

    prepare(1)
    front = synth('nose-front', intensity=2 * math.pi)
    front_old = synth('nose-front-old', intensity=2 * math.pi, extra=('--no-dynamic-nose',))
    assert np.array_equal(front[28:36, 28:36], front_old[28:36, 28:36]), 'Frontal nose should retain its mask'
    side = synth('nose-side', camera=(math.sqrt(3), 2, 3), intensity=2 * math.pi)
    side_old = synth('nose-side-old', camera=(math.sqrt(3), 2, 3), intensity=2 * math.pi, extra=('--no-dynamic-nose',))
    report['nose_yaw_mean_change'] = difference(side, side_old)
    assert report['nose_yaw_mean_change'] > 1
    prepare(1, normal=(0, math.sqrt(3) / 2, .5))
    top = synth('nose-down', camera=(0, 2 + 3 * math.sqrt(3), 3), intensity=2 * math.pi)
    top_old = synth('nose-down-old', camera=(0, 2 + 3 * math.sqrt(3), 3), intensity=2 * math.pi, extra=('--no-dynamic-nose',))
    report['nose_down_mean_change'] = difference(top, top_old)
    assert report['nose_down_mean_change'] > 1
    no_line = synth('nose-off', intensity=2 * math.pi, extra=('--no-nose-line',))
    no_line_old = synth('nose-off-old', intensity=2 * math.pi, extra=('--no-nose-line', '--no-dynamic-nose'))
    assert np.array_equal(no_line, no_line_old), 'No-nose-line must take precedence'

    package = root / 'outputs/ye-shunguang-metal-hoyotoon-path'
    for view, camera in [('front', (.03, 1.48, .80)), ('side', (.48, 1.48, .70)), ('down', (0, 2.35, .50))]:
        kwargs = dict(camera=camera, target=(0, 1.44, -.08), size=(320, 267), samples=16,
                      frames=4, depth=8, intensity=2 * math.pi, environment=.15)
        current = run('character-' + view, package / 'character.obj', package / 'character.zzz.json', **kwargs)
        previous = run('character-' + view + '-old', package / 'character.obj', package / 'character.zzz.json',
                       extra=('--no-anisotropy', '--no-dynamic-nose'), **kwargs)
        report['character_' + view + '_mean_change'] = difference(current, previous)
        if view in ('side', 'down'):
            static_nose = run('character-' + view + '-static-nose', package / 'character.obj',
                              package / 'character.zzz.json', extra=('--no-dynamic-nose',), **kwargs)
            report['character_' + view + '_nose_only_mean_change'] = difference(current, static_nose)
            if view == 'side':
                assert report['character_side_nose_only_mean_change'] > 0, 'Yaw gate must affect the real asset'
            # In this steep portrait the original normal/D.a mask already hides
            # the line. A zero additional down-gate change is correct; its active
            # behavior is verified above with a visible-mask synthetic fixture.
    assert report['character_front_mean_change'] > .0001, 'New hair response must affect the real asset'
    dark = run('character-zero-lights', package / 'character.obj', package / 'character.zzz.json',
               camera=(.03, 1.48, .80), target=(0, 1.44, -.08), depth=8, intensity=0,
               extra=('--no-emission',))
    assert dark.max() == 0, 'Authored response must not bypass traced lighting'
    report['zero_lights_max'] = int(dark.max())
    (out / 'validation.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
