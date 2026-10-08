#!/usr/bin/env python3
"""Transport checks for the full Toon mode; writes small actual GPU renders.
Requires numpy/Pillow, a built metal_accum, and a prepared character package.
"""
import argparse,json,subprocess
from pathlib import Path
import numpy as np
from PIL import Image


def verify_sampling(profile=None, hair=False):
    # Compare numerical hemispherical integration with the actual mixture strategy,
    # including invalid specular samples. Checking a white furnace catches lost PDFs.
    rng=np.random.default_rng(492)
    n=800000; prob=.15; ax=.50; ay=.09; rough=.6
    bands=(.3,.65,.12,.55);spec_scale=1;normalization=1.1426
    if profile:
        bands=profile['bands'];rough=max(rough,profile['lobe'][3]);width=.12+.5*rough
        ax=np.clip(width*profile['lobe'][0],.05,.9);ay=np.clip(width*profile['lobe'][1],.05,.9)
        spec_scale=profile['lobe'][2]
        def J(x):return x*x/.24 if x<=.12 else x-.06
        t1,t2,dark,mid=bands;normalization=2*(dark*J(t1)+mid*(J(t2)-J(t1))+J(1)-J(t2))
    def band_response(mu):
        t1,t2,dark,mid=bands
        q=np.where(mu<t1,dark,np.where(mu<t2,mid,1))
        width=max(0,min(.045,.49*(t2-t1),t1-.12,1-t2))
        if hair and width>1e-6:
            def step(t):
                x=np.clip((mu-t+width)/(2*width),0,1)
                return x*x*(3-2*x)
            q=dark+(mid-dark)*step(t1)+(1-mid)*step(t2)
        return q
    def evaluate(l,v):
        nl=l[:,2]; nv=v[2]
        hh=l+v; hh/=np.linalg.norm(hh,axis=1)[:,None]
        nh=hh[:,2]; vh=np.maximum(hh@v,1e-6)
        radial_squared=(hh[:,0]/ax)**2+(hh[:,1]/ay)**2
        D=np.where(radial_squared<1,(2*(1-radial_squared) if hair else 1)/(np.pi*ax*ay),0.)
        D=np.where(nh>0,D,0.)
        a2=rough**4
        G=.5/np.maximum(nl*np.sqrt(nv*nv*(1-a2)+a2)+nv*np.sqrt(nl*nl*(1-a2)+a2),1e-6)
        F=.04+.96*(1-vh)**5
        q=band_response(nl)
        f=(1-F)*q/(np.pi*normalization*np.maximum(nl,.12))+D*G*F*spec_scale
        pdf=(1-prob)*nl/np.pi+prob*D*nh/(4*vh)
        valid=nl>0
        return np.where(valid,f,0),np.where(valid,pdf,0)
    results={}
    for cosine in (1.,.5,.15):
        v=np.array([np.sqrt(1-cosine*cosine),0,cosine])
        # Dense deterministic quadrature: not a second copy of the random estimator.
        mu=(np.arange(700)+.5)/700; phi=(np.arange(1000)+.5)*2*np.pi/1000
        mm,pp=np.meshgrid(mu,phi,indexing='ij')
        l=np.stack([np.sqrt(1-mm*mm)*np.cos(pp),np.sqrt(1-mm*mm)*np.sin(pp),mm],axis=-1).reshape(-1,3)
        f,_=evaluate(l,v); integral=float(np.mean(f*l[:,2])*2*np.pi)
        u=rng.random(n); phi=rng.random(n)*2*np.pi
        l=np.column_stack([np.sqrt(u)*np.cos(phi),np.sqrt(u)*np.sin(phi),np.sqrt(1-u)])
        spec=rng.random(n)<prob
        # The tapered hair disk has radial CDF 2*s-s*s; retain uniform disks elsewhere.
        radius=np.sqrt(u/(1+np.sqrt(1-u)) if hair else u)
        hx=ax*radius*np.cos(phi);hy=ay*radius*np.sin(phi)
        h=np.column_stack([hx,hy,np.sqrt(1-hx*hx-hy*hy)])
        outgoing=2*(h@v)[:,None]*h-v
        l[spec]=outgoing[spec]
        f,pdf=evaluate(l,v)
        weights=np.divide(f*np.maximum(l[:,2],0),pdf,out=np.zeros(n),where=pdf>1e-8)
        estimate=float(weights.mean());error=float(weights.std()/np.sqrt(n))
        assert abs(estimate-integral)<max(.015,6*error),(cosine,estimate,integral,error)
        assert np.all(np.isfinite(weights))
        results[str(cosine)]={'quadrature':integral,'importance_estimate':estimate,'standard_error':error,
                              'null_fraction':float(np.mean(l[:,2]<=0))}
    # Diffuse energy without Fresnel/tint is exactly normalized (finite at grazing).
    mu=(np.arange(1000000)+.5)/1000000
    q=band_response(mu)
    integral=float(2*np.mean(mu*q/(normalization*np.maximum(mu,.12))))
    assert abs(integral-1)<1e-5
    return {'diffuse_integral':integral,'sampling':results}


def main():
    a=argparse.ArgumentParser();a.add_argument('--asset',type=Path,required=True)
    a.add_argument('--output',type=Path,required=True);args=a.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    binary=Path(__file__).resolve().parent/'build/metal_accum'
    common=[str(binary),'16','0','0','320','180',str(args.asset/'character.obj'),
            '--mmd-materials',str(args.asset/'character.zzz.json'),'--character-scene','--headless','--frames','4',
            '--aperture','0','--fov','24','--camera','.03','1.425','.55','--target','0','1.405','-.055',
            '--light-direction','0','.4','1']
    configurations={'dark':['--depth','8','--sun-intensity','0','--sphere-intensity','0','--environment-strength','0'],
                    'depth1':['--depth','1','--environment-strength','0','--sphere-intensity','0'],'depth8':['--depth','8','--environment-strength','0','--sphere-intensity','0'],'continuous':['--depth','8','--no-toon','--environment-strength','0','--sphere-intensity','0']}
    arrays={};report={'math':verify_sampling(),'renders':{}}
    for name,extra in configurations.items():
        output=args.output/(name+'.png')
        subprocess.run(common+extra+['--output',str(output)],check=True)
        arrays[name]=np.asarray(Image.open(output).convert('RGB'),dtype=float)
        report['renders'][name]=json.loads(output.with_suffix('.json').read_text())
    assert arrays['dark'].max()==0,'No-light scene must be entirely black.'
    report['dark_max']=float(arrays['dark'].max())
    for name,left,right in [('depth_difference','depth1','depth8'),('toon_difference','continuous','depth8')]:
        diff=float(np.abs(arrays[left]-arrays[right]).mean());assert diff>.2,(name,diff)
        report[name]=diff
    (args.output/'validation.json').write_text(json.dumps(report,indent=2))
    print('Passed: zero-light black output, bounce-depth response, Toon/continuous comparison, and sampling integration.')
if __name__=='__main__':main()
