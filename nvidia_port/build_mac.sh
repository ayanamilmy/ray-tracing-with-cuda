#!/bin/sh
set -eu
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$PORT_DIR/build"
xcrun clang++ -std=c++20 -O2 -fno-fast-math -ffp-contract=off -fobjc-arc -mmacosx-version-min=13.0 \
 "$PORT_DIR/src/pack_scene.mm" -framework Cocoa -o "$PORT_DIR/build/pack_scene"
xcrun clang++ -std=c++20 -O2 -fno-fast-math -ffp-contract=off -pthread \
 "$PORT_DIR/src/cpu_reference.cpp" -o "$PORT_DIR/build/cpu_reference"
