#!/bin/sh
# Linux + NVIDIA driver + CUDA Toolkit + LLVM Clang with the NVPTX backend.
set -eu
if [ "$(uname -s)" != Linux ]; then
 printf 'The NVIDIA build requires Linux, CUDA Toolkit and LLVM Clang with NVPTX. Use build_mac.sh for local validation.\n' >&2
 exit 2
fi
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CUDA_TOOLKIT=${CUDA_TOOLKIT:-/usr/local/cuda}
CUDA_ARCH=${CUDA_ARCH:-sm_89}
CUDA_CLANG=${CUDA_CLANG:-clang++}
mkdir -p "$PORT_DIR/build"
"$CUDA_CLANG" -std=c++20 -O3 -fno-fast-math -ffp-contract=off \
 --cuda-path="$CUDA_TOOLKIT" --cuda-gpu-arch="$CUDA_ARCH" \
 "$PORT_DIR/src/render.cu" -L"$CUDA_TOOLKIT/lib64" -Wl,-rpath,"$CUDA_TOOLKIT/lib64" -lcudart -pthread \
 -o "$PORT_DIR/build/nvidia_render"
