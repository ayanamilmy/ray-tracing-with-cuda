#!/usr/bin/env python3
"""Convert the pinned Metal snapshot into this folder's Clang CUDA/CPU kernel."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
s=(root/'snapshot/pathtracer.metal').read_text()
s=s.replace('#include <metal_stdlib>','').replace('#include "shared_types.h"','#include "types.h"').replace('using namespace metal;','namespace pt {')
s=re.sub(r'\[\[[^\]]+\]\]', '', s)
s=re.sub(r'\b(thread|device|constant)\s+', '', s)
s=s.replace('float PI =','constexpr float PI =').replace('float3 LIGHT_CENTER =','constexpr float3 LIGHT_CENTER =').replace('float LIGHT_RADIUS =','constexpr float LIGHT_RADIUS =').replace('float3 LIGHT_EMISSION =','constexpr float3 LIGHT_EMISSION =')
s=s.replace('__attribute__((noinline))', 'PT_NOINLINE')
s=re.sub(r'\binline\b','PT_FN',s)
s=s.replace('kernel void','PT_FN void')
constructors={'float2':'f2','float3':'f3','float4':'f4','uint2':'u2','uint4':'u4','int2':'i2','bool3':'b3','uchar4':'c4'}
# Declarations with scalar broadcasts use value syntax that C++ vectors don't have.
for t,f in constructors.items():
 s=re.sub(r'\b'+t+r'\s+(\w+)\(([01])\)',lambda m:t+' '+m[1]+'='+f+'('+m[2]+')',s)
for name in ['beta','attenuation']:
 s=re.sub(r'\b'+name+r'\(([01])\)',lambda m:name+'=f3('+m[1]+')',s)
for t,f in constructors.items(): s=re.sub(r'\b'+t+r'\(',f+'(',s)
s+='\n} // namespace pt\n'
(root/'include/pathtracer.h').write_text('// Generated from snapshot/pathtracer.metal by tools/translate.py; edit the snapshot or translator, then regenerate.\n#pragma once\n'+s)
