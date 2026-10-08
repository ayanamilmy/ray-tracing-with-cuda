#!/bin/sh
set -eu
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -lt 2 ]; then
 printf 'Usage: sh pack_entry_frame.sh ASSET_DIR OUTPUT.npt [WIDTH HEIGHT]\n' >&2
 exit 2
fi
ASSET_DIR=$1
PACKAGE_PATH=$2
WIDTH=${3:-1800}
HEIGHT=${4:-2400}
"$PORT_DIR/build/pack_scene" 16 0 0 "$WIDTH" "$HEIGHT" "$ASSET_DIR/character.obj" \
 --mmd-materials "$ASSET_DIR/character.zzz.json" --asset-toon --neutral-sky --no-tonemap \
 --character-scene --no-ground --headless --depth 8 --aperture 0 \
 --camera -.05 1.13 3.9 --target -.05 .87 0 --fov 32 \
 --light-direction -.12 .15 1 --sun-angle 20 --sun-intensity 4.5 \
 --sphere-intensity 0 --environment-strength .5 --exposure 1 --outline-scale .6 \
 --no-anisotropy --no-dynamic-nose --no-rim --output "$PACKAGE_PATH"
