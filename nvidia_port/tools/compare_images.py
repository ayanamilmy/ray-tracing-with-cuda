#!/usr/bin/env python3
"""Usage: compare_images.py CPU.ppm METAL.png REPORT.json [SIDE_BY_SIDE.png]. Needs Pillow and numpy."""
import sys,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
cpu=Image.open(sys.argv[1]).convert('RGB');metal=Image.open(sys.argv[2]).convert('RGB')
if cpu.size!=metal.size: raise ValueError('Image sizes differ')
a=np.asarray(cpu).astype(float);b=np.asarray(metal).astype(float);d=np.abs(a-b)
report={'comparison':'CPU execution of translated CUDA routines versus Metal; not NVIDIA GPU validation','width':cpu.width,'height':cpu.height,'mean_absolute_error_8bit':float(d.mean()),'rmse_8bit':float(np.sqrt(np.mean(d*d))),'maximum_channel_difference':int(d.max()),'identical_pixel_fraction':float(np.all(d==0,axis=-1).mean()),'p99_channel_difference':float(np.percentile(d,99))}
Path(sys.argv[3]).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if len(sys.argv)>4:
 out=Image.new('RGB',(cpu.width*2,cpu.height+30),'#222222');out.paste(metal,(0,30));out.paste(cpu,(cpu.width,30));draw=ImageDraw.Draw(out);draw.text((8,8),'Existing Metal',fill='white');draw.text((cpu.width+8,8),'Isolated port / CPU',fill='white');out.save(sys.argv[4])
