#!/bin/sh
# Applicable source controls and identified texture candidates, full path integration; 2048x1710, 256 spp.
# Camera, front key light and neutral environment are render setup, not recovered game data.
set -eu
METAL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TASK_ROOT=$(CDPATH= cd -- "$METAL_DIR/../.." && pwd)
ASSET_OUTPUT=${1:-"$TASK_ROOT/outputs/ye-shunguang-render/ye_shunguang_all_asset_path_2k_256spp.png"}
if [ "$#" -gt 0 ]; then shift; fi
exec "$METAL_DIR/build/metal_accum" 16 0 0.08 2048 1710 \
  "$TASK_ROOT/outputs/ye-shunguang-metal-zzz-v6/character.obj" \
  --mmd-materials "$TASK_ROOT/outputs/ye-shunguang-metal-zzz-v6/character.zzz.json" \
  --asset-toon --neutral-sky --no-tonemap --character-scene --headless --frames 16 \
  --trace-tile-pixels 32768 --depth 8 --aperture 0 --fov 28 --camera 0.03 1.48 0.80 --target 0 1.44 -0.08 \
  --light-direction 0 0.4 1 --sun-intensity 6.283185307 --sphere-intensity 0 --environment-strength 0.15 --exposure 1 \
  --output "$ASSET_OUTPUT" "$@"
