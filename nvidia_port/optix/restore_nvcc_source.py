#!/usr/bin/env python3
"""Restore the saved nvcc-adapted input sources; never modify the root CUDA project."""
from pathlib import Path
import hashlib,json,shutil
root=Path(__file__).resolve().parents[1]
source=root/'optix/nvcc_source'
manifest=json.loads((source/'manifest.json').read_text())
for name,want in manifest['sha256'].items():
    if hashlib.sha256((source/name).read_bytes()).hexdigest()!=want:
        raise SystemExit('Saved source checksum mismatch: '+name)
out=root/'build/optix/nvcc';out.mkdir(parents=True,exist_ok=True)
for name in manifest['sha256']:
    shutil.copy2(source/name,out/name)
print('Restored verified nvcc source inputs to '+str(out))
