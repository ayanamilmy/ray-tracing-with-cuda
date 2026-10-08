#!/usr/bin/env python3
"""Read PPM without optional dependencies; report, never assume bitwise GPU equivalence."""
import json, math, sys
from pathlib import Path
def ppm(path):
    data=Path(path).read_bytes();header=data.split(b'\n',3)
    if len(header)!=4 or header[0]!=b'P6' or header[2]!=b'255':
        raise ValueError('Expected renderer P6 PPM')
    width,height=map(int,header[1].split());pixels=header[3]
    if len(pixels)!=width*height*3: raise ValueError('Truncated PPM')
    return width,height,pixels
a,b=ppm(sys.argv[1]),ppm(sys.argv[2])
if a[:2]!=b[:2]: raise ValueError('Image dimensions differ')
total=squares=maximum=identical=0
for i in range(0,len(a[2]),3):
    same=True
    for j in range(3):
        d=abs(a[2][i+j]-b[2][i+j]);total+=d;squares+=d*d;maximum=max(maximum,d);same&=d==0
    identical+=same
report={'comparison':'Software BVH versus OptiX RT Core, same CUDA integrator and seeds',
        'width':a[0],'height':a[1],'mean_absolute_error_8bit':total/len(a[2]),
        'rmse_8bit':math.sqrt(squares/len(a[2])),'maximum_channel_difference':maximum,
        'identical_pixel_fraction':identical/(a[0]*a[1]),
        'note':'GPU triangle intersection precision and traversal order may change individual paths; review images and query validation.'}
Path(sys.argv[3]).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
