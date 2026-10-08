#!/usr/bin/env python3
"""Run within the original repository to verify every pre-existing file remains unchanged."""
from pathlib import Path
import hashlib,json,sys
port=Path(__file__).resolve().parents[1];repo=port.parent
baseline=json.loads((port/'snapshot/original-files-sha256.json').read_text())
changed=[];missing=[]
for name,want in baseline.items():
 path=repo/name
 if not path.is_file():missing.append(name)
 elif hashlib.sha256(path.read_bytes()).hexdigest()!=want:changed.append(name)
result={'files_checked':len(baseline),'changed':changed,'missing':missing}
print(json.dumps(result,indent=2));sys.exit(bool(changed or missing))
