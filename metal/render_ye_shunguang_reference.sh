#!/bin/sh
# All scene choices are authored against a user screenshot, not official values.
set -eu
if [ "$#" -lt 2 ]; then echo 'Usage: render_ye_shunguang_reference.sh POSED_ASSET_DIR OUTPUT.png [renderer flags]' >&2; exit 2; fi
METAL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ASSET_DIR=$1
OUTPUT_PATH=$2
shift 2
mkdir -p "$(dirname "$OUTPUT_PATH")"
exec "$METAL_DIR/build/metal_accum" 16 0 0 "${WIDTH:-1800}" "${HEIGHT:-2400}" "$ASSET_DIR/character.obj" \
 --mmd-materials "$ASSET_DIR/character.zzz.json" --asset-toon --neutral-sky --no-tonemap \
 --character-scene --no-ground --headless --frames "${FRAMES:-16}" --trace-tile-pixels 32768 --depth 8 \
 --aperture 0 --fov 28 --camera .13 1.24 2.32 --target 0 1.10 0 \
 --light-direction -.12 .15 1 --sun-angle 20 --sun-intensity 4.5 --sphere-intensity 0 \
 --environment-strength .5 --exposure 1 --outline-scale .6 \
 --no-anisotropy --no-dynamic-nose --no-rim --output "$OUTPUT_PATH" "$@"
