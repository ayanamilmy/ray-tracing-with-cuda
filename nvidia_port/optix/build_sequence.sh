#!/bin/sh
set -eu
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CUDA_TOOLKIT=${CUDA_TOOLKIT:-/usr/local/cuda}
CUDA_HOST_CLANG=${CUDA_HOST_CLANG:-clang++}
OPTIX_ROOT=${OPTIX_ROOT:-$(sh "$PORT_DIR/optix/fetch_headers.sh")}
"$CUDA_HOST_CLANG" -std=c++20 -O3 -fno-fast-math -ffp-contract=off \
 -I"$OPTIX_ROOT/include" -I"$CUDA_TOOLKIT/include" "$PORT_DIR/optix/render_sequence.cpp" \
 -L"$CUDA_TOOLKIT/lib64" -L"$CUDA_TOOLKIT/lib64/stubs" \
 -Wl,-rpath,"$CUDA_TOOLKIT/lib64" -lcudart -lcuda -ldl -o "$PORT_DIR/build/optix_sequence"
