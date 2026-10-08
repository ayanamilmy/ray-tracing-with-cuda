#!/bin/sh
set -eu
[ "$(uname -s)" = Linux ] || { printf 'Run this build on Linux with an NVIDIA GPU driver.\n' >&2; exit 2; }
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CUDA_TOOLKIT=${CUDA_TOOLKIT:-/usr/local/cuda}
CUDA_CLANG=${CUDA_CLANG:-clang++-21}
CUDA_ARCH=${CUDA_ARCH:-sm_120}
OPTIX_ROOT=${OPTIX_ROOT:-$(sh "$PORT_DIR/optix/fetch_headers.sh")}
"$CUDA_CLANG" --version
python3 "$PORT_DIR/optix/make_kernel.py"
"$CUDA_CLANG" -std=c++20 -O3 -fno-fast-math -ffp-contract=off \
 --cuda-path="$CUDA_TOOLKIT" --cuda-gpu-arch="$CUDA_ARCH" --cuda-device-only -S \
 -I"$OPTIX_ROOT/include" "$PORT_DIR/optix/device.cu" -o "$PORT_DIR/build/optix/device.ptx"
# Host code is ordinary C++; driver stubs are for linking only, never a runtime rpath.
"$CUDA_CLANG" -std=c++20 -O3 -fno-fast-math -ffp-contract=off \
 -I"$OPTIX_ROOT/include" -I"$CUDA_TOOLKIT/include" "$PORT_DIR/optix/render.cpp" \
 -L"$CUDA_TOOLKIT/lib64" -L"$CUDA_TOOLKIT/lib64/stubs" \
 -Wl,-rpath,"$CUDA_TOOLKIT/lib64" -lcudart -lcuda -ldl -o "$PORT_DIR/build/optix_render"
printf 'Built %s\n' "$PORT_DIR/build/optix_render"
