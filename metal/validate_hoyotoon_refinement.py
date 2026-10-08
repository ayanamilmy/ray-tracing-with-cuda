#!/usr/bin/env python3
"""Independent finite-sun integral, MIS, occlusion, and actual-character path checks."""
import argparse, json, math, subprocess
from pathlib import Path
import numpy as np
from PIL import Image


def decode(a):
    a=a.astype(float)/255
    return np.where(a<=.04045,a/12.92,((a+.055)/1.055)**2.4)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    metal=Path(__file__).resolve().parent
    binary=metal/'build/metal_accum'
    (out/'materials.json').write_text('{"materials": {"white":{}, "black":{}}}')
    (out/'wall.mtl').write_text('newmtl white\nKd 1 1 1\nnewmtl black\nKd 0 0 0\n')
    wall='mtllib wall.mtl\nv -1 1 -100\nv 1 1 -100\nv 1 3 -100\nv -1 3 -100\nusemtl white\nf 1 2 3\nf 1 3 4\n'
    (out/'wall.obj').write_text(wall)
    (out/'blocked.obj').write_text(wall+'v -1 1 -1\nv 4 1 -1\nv 4 1 1\nv -1 1 1\nusemtl black\nf 5 6 7\nf 5 7 8\n')
    base=[str(binary),'64','0','0','48','48',str(out/'wall.obj'),'--mmd-materials',str(out/'materials.json'),
          '--character-scene','--headless','--frames','32','--aperture','0','--fov','1',
          '--camera','0','3','6','--target','0','0','0','--sun-intensity','.9',
          '--sphere-intensity','0','--environment-strength','0','--ground-albedo','1','--no-tonemap']
    results={}
    def run(name,extra,command=base):
        path=out/(name+'.png')
        with (out/(name+'.log')).open('w') as log:
            subprocess.run(command+list(extra)+['--output',str(path)],check=True,stdout=log,stderr=subprocess.STDOUT)
        return np.asarray(Image.open(path).convert('RGB')),json.loads(path.with_suffix('.json').read_text())
    # For a Lambert plane: integral of cosine over a uniform circular cap is
    # Omega*(1+cos(radius))/2 * dot(normal,axis), when the cap stays above it.
    for radius in [0,8,16,24]:
        expected=.9/math.pi*(1+math.cos(math.radians(radius)))/2/math.sqrt(2)
        for depth in [1,2]:
            a,_=run(f'sun-{radius}-depth{depth}',['--light-direction','1','1','0','--sun-angle',str(radius),'--depth',str(depth)])
            actual=float(decode(a[8:40,8:40]).mean())
            assert abs(actual-expected)<.006,(radius,depth,actual,expected)
            results[f'sun-{radius}-depth{depth}']={'GPU_linear_mean':actual,'analytic_mean':expected}
    blocked_base=base.copy();blocked_base[6]=str(out/'blocked.obj')
    a,_=run('blocked',['--light-direction','1','1','0','--sun-angle','24','--depth','1'],blocked_base)
    assert a[8:40,8:40].max()==0,'Finite sunlight leaked through a real blocker'
    results['blocker_max']=int(a[8:40,8:40].max())
    root=metal.parent.parent
    cmd=json.loads((root/'outputs/hoyotoon-comparison-4k/metal-command.json').read_text())
    cmd[4:6]=['160','90'];cmd[cmd.index('--frames')+1]='4';cmd[cmd.index('--depth')+1]='3'
    original,report=run('character',['--sun-angle','16'],cmd)
    half,_=run('half-light',['--sun-angle','16','--sun-intensity',str(math.pi),'--environment-strength','.075'],cmd)
    # depth3 avoids roulette: equal RNG samples must scale linearly in light.
    # Ignore clipped channels; PNG quantization is bounded after decoding.
    valid=(original<250)
    err=float(np.abs(decode(half)-.5*decode(original))[valid].max())
    assert err<.009,('Linear radiance scaling',err)
    dark,_=run('zero-light',['--sun-angle','16','--sun-intensity','0','--environment-strength','0','--no-emission','--depth','8'],cmd)
    assert dark.max()==0,'Untraced additive colour appeared with all emission disabled'
    tiled,_=run('other-tiles',['--sun-angle','16','--trace-tile-pixels','1024'],cmd)
    assert np.array_equal(tiled,original),'Tile size changed the Monte Carlo estimator'
    shallow,_=run('depth1',['--sun-angle','16','--depth','1'],cmd)
    deep,_=run('depth8',['--sun-angle','16','--depth','8'],cmd)
    delta=float(np.abs(deep.astype(float)-shallow.astype(float)).mean())
    assert delta>.05,'The full path integrator did not continue after the primary hit'
    assert report['path_outline_geometry'] and report['character_transport'].startswith('Full path integration')
    results.update(zero_light_max=int(dark.max()),linear_scaling_max_error=err,tile_identical=True,
                   depth1_to_depth8_mean_byte_change=delta,outline_transport=report['path_outline_scattering'])
    (out/'validation.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(results,indent=2))

if __name__=='__main__':main()
