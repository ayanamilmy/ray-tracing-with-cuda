#!/bin/sh
# Faster first deployment: checked precompiled GPU PTX, ordinary Clang host compilation.
set -eu
[ "$(uname -s)" = Linux ] || { printf 'The OptiX host build requires Linux and NVIDIA drivers.\n' >&2; exit 2; }
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CUDA_TOOLKIT=${CUDA_TOOLKIT:-/usr/local/cuda}
CUDA_HOST_CLANG=${CUDA_HOST_CLANG:-clang++}
OPTIX_ROOT=${OPTIX_ROOT:-$(sh "$PORT_DIR/optix/fetch_headers.sh")}
python3 "$PORT_DIR/optix/use_prebuilt.py"
"$CUDA_HOST_CLANG" -std=c++20 -O3 -fno-fast-math -ffp-contract=off \
 -I"$OPTIX_ROOT/include" -I"$CUDA_TOOLKIT/include" "$PORT_DIR/optix/render.cpp" \
 -L"$CUDA_TOOLKIT/lib64" -L"$CUDA_TOOLKIT/lib64/stubs" \
 -Wl,-rpath,"$CUDA_TOOLKIT/lib64" -lcudart -lcuda -ldl -o "$PORT_DIR/build/optix_render"
