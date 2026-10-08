#!/bin/sh
set -eu
METAL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$METAL_DIR/build"
cp "$METAL_DIR/pathtracer.metal" "$METAL_DIR/build/pathtracer.metal"
cp "$METAL_DIR/shared_types.h" "$METAL_DIR/build/shared_types.h"
if xcrun -sdk macosx metal --version >/dev/null 2>&1; then
    xcrun -sdk macosx metal -std=metal3.0 -fno-fast-math -c "$METAL_DIR/pathtracer.metal" -o "$METAL_DIR/build/pathtracer.air"
    xcrun -sdk macosx metallib "$METAL_DIR/build/pathtracer.air" -o "$METAL_DIR/build/pathtracer.metallib"
else
    # Do not let an older compiled library shadow the newly copied shader source.
    rm -f "$METAL_DIR/build/pathtracer.metallib"
    printf 'Offline Metal compiler unavailable; shaders will compile through Metal at startup.\n'
fi
xcrun -sdk macosx clang++ -std=c++17 -O3 -fno-fast-math -fobjc-arc -mmacosx-version-min=13.0 \
    "$METAL_DIR/main.mm" -framework Cocoa -framework Metal -framework ImageIO \
    -o "$METAL_DIR/build/metal_accum"
printf 'Built %s\n' "$METAL_DIR/build/metal_accum"
