#!/bin/sh
set -eu
METAL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TASK_ROOT=$(CDPATH= cd -- "$METAL_DIR/../.." && pwd)
STUDIO_OUTPUT=${1:-"$TASK_ROOT/outputs/ye-shunguang-render/ye_shunguang_full_toon_path_studio_2k_256spp.png"}
if [ "$#" -gt 0 ]; then shift; fi
exec "$METAL_DIR/build/metal_accum" 16 0 0.08 2560 1440 \
  "$TASK_ROOT/outputs/ye-shunguang-metal-zzz-v6/character.obj" \
  --mmd-materials "$TASK_ROOT/outputs/ye-shunguang-metal-zzz-v6/character.zzz.json" \
  --toon-profiles "$METAL_DIR/presets/ye_shunguang_toon_profiles.json" \
  --character-scene --toon-path-tracing --headless --frames 16 --depth 8 --aperture 0 --fov 28 \
  --camera 0 0.98 4.15 --target 0 0.82 -0.18 --light-direction 0 0.4 1 \
  --sun-intensity 0.25 --sphere-intensity 0.3 --environment-strength 0.2 --exposure 1.1 \
  --area-light -1.8 2.6 2.6 0 1 0 1.2 1.8 1 0.97 0.92 22 \
  --area-light 2.3 1.9 2.1 0 1 0 1.5 1.8 0.86 0.93 1 4 \
  --area-light 3.4 2.2 -1.2 0 1 0 0.8 2 0.9 0.95 1 22 \
  --output "$STUDIO_OUTPUT" "$@"
