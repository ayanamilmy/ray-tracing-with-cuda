#!/usr/bin/env python3
"""Refuse a stale precompiled shader if any of its source inputs changed."""
from pathlib import Path
import hashlib,json,shutil
root=Path(__file__).resolve().parents[1]
manifest=root/'optix/prebuilt/manifest.json'
if not manifest.is_file(): raise SystemExit('Prebuilt PTX not present; use optix/build.sh with LLVM 21.')
data=json.loads(manifest.read_text())
for name,want in data['sha256'].items():
    path=root/name
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=want:
        raise SystemExit('Prebuilt PTX source mismatch: '+name+'; rebuild with optix/build.sh.')
output=root/'build/optix';output.mkdir(parents=True,exist_ok=True)
shutil.copyfile(root/'optix/prebuilt/sm_120.ptx',output/'device.ptx')
print('Verified precompiled sm_120 PTX and all source inputs; copied to build/optix/device.ptx.')
