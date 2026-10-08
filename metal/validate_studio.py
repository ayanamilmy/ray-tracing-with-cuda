#!/usr/bin/env python3
"""Independent lighting reference + actual GPU checks for the Metal studio features."""
import argparse,json,subprocess
from pathlib import Path
from validate_toon_paths import verify_sampling
import numpy as np
from PIL import Image


def decode(x):
    x=x/255
    return np.where(x<=.04045,x/12.92,((x+.055)/1.055)**2.4)


def reference(center,target,width,height,Le):
    center=np.asarray(center,float); n=np.asarray(target,float)-center;n/=np.linalg.norm(n)
    axis=np.array([0,1,0]) if abs(n[1])<.95 else np.array([1,0,0])
    u=np.cross(axis,n);u/=np.linalg.norm(u);v=np.cross(n,u)
    grid=(np.arange(800)+.5)/800*2-1;xx,yy=np.meshgrid(grid,grid)
    points=center+xx[:,:,None]*u*width/2+yy[:,:,None]*v*height/2
    d=points-np.array([0,1,0]);distance=np.linalg.norm(d,axis=2);l=d/distance[:,:,None]
    cos_surface=np.maximum(l[:,:,2],0);cos_light=np.maximum(np.sum(-l*n,axis=2),0)
    return float(np.mean(cos_surface*cos_light/(distance*distance))*width*height*Le/np.pi)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    binary=Path(__file__).resolve().parent/'build/metal_accum'
    (a.output/'wall.mtl').write_text('newmtl white\nKd 1 1 1\nnewmtl black\nKd 0 0 0\n')
    wall='mtllib wall.mtl\nv -1 0 0\nv 1 0 0\nv 1 2 0\nv -1 2 0\nusemtl white\nf 1 2 3\nf 1 3 4\nv -10 .001 -10\nv 10 .001 -10\nv 10 .001 10\nv -10 .001 10\nusemtl black\nf 5 7 6\nf 5 8 7\n'
    (a.output/'wall.obj').write_text(wall)
    blocked=wall+'v -10 1.8 -10\nv 10 1.8 -10\nv 10 1.8 10\nv -10 1.8 10\nf 9 11 10\nf 9 12 11\n'
    (a.output/'blocked.obj').write_text(blocked)
    base=[str(binary),'64','0','0','64','64','--character-scene','--headless','--frames','64',
          '--aperture','0','--fov','5','--camera','0','1','3','--target','0','1','0',
          '--sun-intensity','0','--sphere-intensity','0','--environment-strength','0','--depth','1']
    lamp=['--area-light','0','2.6','1.2','0','1','0','1','1','1','1','1','2']
    second=['--area-light','1.5','2.4','1','0','1','0','.7','.9','1','1','1','2']
    results={}
    def run(name,extra):
        path=a.output/(name+'.png');subprocess.run(base+extra+['--output',str(path)],check=True)
        return np.asarray(Image.open(path).convert('RGB'),float)
    expected=reference([0,2.6,1.2],[0,1,0],1,1,2)
    for name,args in [('area_nee',[str(a.output/'wall.obj'),'--no-tonemap']+lamp),
                      ('area_mis',[str(a.output/'wall.obj'),'--no-tonemap','--depth','2']+lamp),
                      ('two_lights',[str(a.output/'wall.obj'),'--no-tonemap']+lamp+second)]:
        image=run(name,args);actual=float(decode(image[29:35,29:35]).mean())
        target=expected+(reference([1.5,2.4,1],[0,1,0],.7,.9,2) if name=='two_lights' else 0)
        assert abs(actual-target)<max(.002,target*.07),(name,actual,target)
        results[name]={'GPU_linear_mean':actual,'surface_integral_reference':target}
    image=run('blocked',[str(a.output/'blocked.obj'),'--no-tonemap']+lamp)
    assert image[29:35,29:35].max()==0,'Blocker must extinguish the lamp contribution.'
    results['occlusion_center_max']=float(image[29:35,29:35].max())
    hdr=['--area-light','0','1','0','0','1','3','1','1','1','.5','.25','4']
    clipped=run('clipped',hdr+['--no-tonemap']);mapped=run('shoulder',hdr)
    assert np.all(clipped[32,32]==255)
    peak=.7+.3*(1-np.exp(-(4-.7)/.3));expected_rgb=np.array([peak,peak/2,peak/4])
    assert np.max(abs(decode(mapped[32,32])-expected_rgb))<.01
    results['HDR_RGB_after_shoulder']=mapped[32,32].tolist()
    # Editable diffuse bands are normalized independently of the light estimator.
    preset=json.loads((Path(__file__).resolve().parent/'presets/ye_shunguang_toon_profiles.json').read_text())
    mu=(np.arange(1000000)+.5)/1000000
    def J(x):return x*x/.24 if x<=.12 else x-.06
    results['diffuse_integrals']={}
    for name,p in preset['profiles'].items():
        t1,t2,dark,mid=p['bands'];normalization=2*(dark*J(t1)+mid*(J(t2)-J(t1))+J(1)-J(t2))
        q=np.where(mu<t1,dark,np.where(mu<t2,mid,1))
        integral=float(2*np.mean(mu*q/(normalization*np.maximum(mu,.12))))
        assert abs(integral-1)<1e-5;results['diffuse_integrals'][name]=integral
    results['profile_sampling']={name:verify_sampling(profile,hair=name=='hair') for name,profile in preset['profiles'].items()}
    (a.output/'validation.json').write_text(json.dumps(results,indent=2))
    print('Passed: area-light reference, multi-light selection PDF, MIS, occlusion, HDR hue preservation, profile normalization.')
if __name__=='__main__':main()
