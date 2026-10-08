#!/bin/sh
# First paid run: small packaged frame; verify intersections, then measure both backends.
set -eu
[ "$#" -ge 2 ] || { printf 'Usage: benchmark.sh small-scene.npt output-directory [passes]\n' >&2; exit 2; }
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SCENE=$1
RESULT_DIR=$2
PASSES=${3:-16}
mkdir -p "$RESULT_DIR"
"$PORT_DIR/build/optix_render" "$PORT_DIR/build/optix/device.ptx" "$SCENE" "$RESULT_DIR/verified.ppm" 1 32768 - verify
"$PORT_DIR/build/optix_render" "$PORT_DIR/build/optix/device.ptx" "$SCENE" "$RESULT_DIR/software.ppm" "$PASSES" 32768 "$RESULT_DIR/software.f32" software
"$PORT_DIR/build/optix_render" "$PORT_DIR/build/optix/device.ptx" "$SCENE" "$RESULT_DIR/optix.ppm" "$PASSES" 32768 "$RESULT_DIR/optix.f32"
python3 "$PORT_DIR/optix/compare.py" "$RESULT_DIR/software.ppm" "$RESULT_DIR/optix.ppm" "$RESULT_DIR/comparison.json"
python3 - "$RESULT_DIR" <<'PY'
import json,sys
from pathlib import Path
p=Path(sys.argv[1]);software=json.loads((p/'software.ppm.json').read_text());rt=json.loads((p/'optix.ppm.json').read_text())
ratio=software['render_seconds']/rt['render_seconds']
result={'software_bvh_seconds':software['render_seconds'],'optix_seconds':rt['render_seconds'],'rt_over_software_speedup':ratio,'note':'Both run in the same OptiX raygen/compiled module; only intersection backend changes. Small-frame result; measure a 3K frame separately before extrapolating full animation.'}
(p/'benchmark.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
PY
