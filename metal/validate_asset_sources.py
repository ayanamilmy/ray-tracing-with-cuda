#!/usr/bin/env python3
"""GPU checks for source precedence, missing bindings and real path illumination."""
import argparse, json, subprocess
from pathlib import Path
import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--asset', type=Path, help='Prepared asset directory (default: previous source package)')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    metal = Path(__file__).resolve().parent
    root = metal.parent.parent
    package = (args.asset.resolve() if args.asset else root / 'outputs/ye-shunguang-metal-zzz-v6') / 'character.zzz.json'
    obj = package.with_name('character.obj')
    base = [str(metal / 'build/metal_accum'), '16', '0', '0', '96', '80', str(obj),
            '--asset-toon', '--neutral-sky', '--no-tonemap', '--character-scene',
            '--headless', '--frames', '4', '--depth', '3', '--aperture', '0', '--fov', '28',
            '--camera', '.03', '1.48', '.80', '--target', '0', '1.44', '-.08',
            '--sun-intensity', '0', '--sphere-intensity', '0', '--exposure', '1', '--no-emission']

    def run(name, strength, material=package, extra=()):
        output = out / (name + '.png')
        subprocess.run(base + ['--mmd-materials', str(material), '--environment-strength', str(strength),
                              '--output', str(output)] + list(extra), check=True)
        return np.asarray(Image.open(output).convert('RGB')), json.loads(output.with_suffix('.json').read_text())

    original, report = run('source', .6)
    tiled, _ = run('smaller-tiles', .6, extra=['--trace-tile-pixels', '4096'])
    assert np.array_equal(original, tiled), 'GPU strip submission changed pixel sampling.'
    dark, _ = run('zero-lights', 0, extra=['--depth', '8'])
    assert dark.max() == 0, 'Source response must not add untraced emission or camera colour.'
    half, _ = run('half-environment', .3)
    def linear(x):
        x = x.astype(float) / 255
        return np.where(x <= .04045, x / 12.92, ((x + .055) / 1.055) ** 2.4)
    error = np.abs(linear(half) - .5 * linear(original))
    assert error.max() < .007, ('Radiance must scale with incident light before roulette.', error.max())
    doc = json.loads(package.read_text())
    doc['triangle_attributes'] = str(package.parent / doc['triangle_attributes'])
    # Alter only an isolated package copy. The original asset is never overwritten.
    # Source JSON overrides copied scalars/tints while all prepared controls remain active.
    for entry in doc['materials'].values():
        entry['source_floats'] = {'_SpecIntensity': 99, '_AlbedoSmoothness': .99}
        entry['source_colors'] = {'_Color': {'r': 0, 'g': 1, 'b': 0, 'a': 1}}
    copied = out / 'copied-materials.json'
    copied.write_text(json.dumps(doc))
    altered, _ = run('untrusted-copy', .6, copied)
    assert np.array_equal(original, altered), 'Copied parameters affected source mode.'
    sources = report['asset_source_report']
    for entry in doc['materials'].values():
        raw = json.loads(Path(entry['source_material_json']).read_text())
        recorded = sources[raw['m_Name']]
        assert recorded['floats'] == raw['m_SavedProperties']['m_Floats']
        assert recorded['colors'] == raw['m_SavedProperties']['m_Colors']
    assert not report['typed_toon_profiles'] and report['matcap_enabled'] and report['face_sdf_enabled']
    ablations = {}
    for name, switch in [('normal', '--no-normal'), ('light-control', '--no-light-control'),
                         ('material-ID', '--no-material-id'), ('specular', '--no-specular-mask'),
                         ('matcap', '--no-matcap'), ('face-SDF', '--no-face-sdf'), ('rim', '--no-rim'),
                         ('outline', '--no-outline'), ('opacity', '--no-transparency')]:
        image, _ = run('without-' + name, .6, extra=[switch])
        delta = float(np.mean(np.abs(image.astype(float) - original.astype(float))))
        assert delta > .0001, (name, 'has no visible effect on the actual asset')
        ablations[name] = delta
    strict, _ = run('strict-source', .6, extra=['--strict-texture-bindings'])
    altered_doc = json.loads(copied.read_text())
    for entry in altered_doc['materials'].values():
        for field in ('normal_texture', 'metallic_texture', 'roughness_texture', 'face_lightmap'):
            entry[field] = str(out / 'does-not-exist.png')
            entry.setdefault('binding_evidence', {})[field] = {'confirmed': False}
    copied.write_text(json.dumps(altered_doc))
    strict_copy, _ = run('strict-untrusted-copy', .6, copied, ['--strict-texture-bindings'])
    assert np.array_equal(strict, strict_copy)
    base.remove('--no-emission')
    try:
        emission, _ = run('source-emission-only', 0, extra=['--depth', '8'])
    finally:
        base.append('--no-emission')
    # Actual portrait masks need not expose emissive texels. Record the result
    # rather than enabling or repainting source material to force a visible glow.
    rejected = subprocess.run(base + ['--mmd-materials', str(package), '--toon-profiles',
                                     str(metal / 'presets/ye_shunguang_toon_profiles.json')], capture_output=True)
    assert rejected.returncode != 0 and b'forbids authored profile' in rejected.stderr
    result = {'zero_light_max': int(dark.max()), 'linear_light_scaling_max_error': float(error.max()),
              'tile_submission_identical': True, 'untrusted_copy_identical': True, 'raw_materials_verified': len(sources),
              'authored_profile_override_rejected': True, 'actual_asset_ablation_mean_byte_changes': ablations,
              'portrait_emission_only_max_byte': int(emission.max()),
              'loaded_texture_count': len(report['loaded_texture_files'])}
    (out / 'validation.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
