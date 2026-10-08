#!/bin/sh
# HoyoToon source response inside full path transport. Render setup is local.
set -eu
METAL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TASK_ROOT=$(CDPATH= cd -- "$METAL_DIR/../.." && pwd)
RENDER_OUTPUT=${1:-"$TASK_ROOT/outputs/ye-shunguang-render/ye_shunguang_hoyotoon_path_fullbody_2k_256spp.png"}
if [ "$#" -gt 0 ]; then shift; fi
exec "$METAL_DIR/build/metal_accum" 16 0 0.08 2048 2048 \
  "$TASK_ROOT/outputs/ye-shunguang-metal-hoyotoon-path/character.obj" \
  --mmd-materials "$TASK_ROOT/outputs/ye-shunguang-metal-hoyotoon-path/character.zzz.json" \
  --asset-toon --neutral-sky --no-tonemap --character-scene --headless --frames 16 \
  --trace-tile-pixels 32768 --depth 8 --aperture 0 --fov 28 \
  --camera 0 0.98 4.15 --target 0 0.87 -0.18 \
  --light-direction 0 0.4 1 --sun-intensity 6.283185307 --sphere-intensity 0 \
  --environment-strength 0.15 --exposure 1 --output "$RENDER_OUTPUT" "$@"
