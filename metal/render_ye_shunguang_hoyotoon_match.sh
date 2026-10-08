#!/bin/sh
# Local visual-match lighting. All values here are authored scene choices,
# not recovered game lighting or official material parameters.
set -eu
METAL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(dirname "$METAL_DIR")
ASSET_DIR="$REPO_DIR/../outputs/ye-shunguang-metal-hoyotoon-path"
OUTPUT_PATH=${1:-"$REPO_DIR/../outputs/hoyotoon-refined-path-4k/metal-refined-4k-256spp.png"}
if [ "$#" -gt 0 ]; then shift; fi
mkdir -p "$(dirname "$OUTPUT_PATH")"
exec "$METAL_DIR/build/metal_accum" 16 0 0 "${WIDTH:-3840}" "${HEIGHT:-2160}" "$ASSET_DIR/character.obj" \
  --mmd-materials "$ASSET_DIR/character.zzz.json" --asset-toon --neutral-sky --no-tonemap \
  --character-scene --headless --frames 16 --trace-tile-pixels 32768 --depth 8 \
  --aperture 0 --fov 28 --camera .03 1.5 1.38 --target .02 1.365 -.1 \
  --light-direction 0 .15 1 --sun-angle 12 --sun-intensity 5.35 \
  --sphere-intensity 0 --environment-strength .15 --exposure 1 --ground-albedo .665 \
  --outline-scale .85 --no-anisotropy --no-dynamic-nose --output "$OUTPUT_PATH" "$@"
