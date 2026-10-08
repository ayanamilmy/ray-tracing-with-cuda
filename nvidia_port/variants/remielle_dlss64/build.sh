#!/usr/bin/env bash
set -euo pipefail
TASK_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
: "${PORT_ROOT:=$(cd "$TASK_DIR/.." && pwd)}"
: "${BUILD_DIR:=$TASK_DIR/build}"
: "${CUDA_ROOT:=/usr/local/cuda}"
: "${OPTIX_INCLUDE:?Set OPTIX_INCLUDE to OptiX 9.0 include directory}"
: "${DLSS_ROOT:?Set DLSS_ROOT to NVIDIA DLSS 310.5.3 SDK directory}"
python3 "$TASK_DIR/prepare.py" --port-root "$PORT_ROOT" --output "$BUILD_DIR"
"$CUDA_ROOT/bin/nvcc" -std=c++20 -arch=compute_120 --ptx --fmad=false \
  -I "$OPTIX_INCLUDE" -I "$BUILD_DIR" "$BUILD_DIR/build/optix/nvcc/device.cu" -o "$BUILD_DIR/device.ptx"
"$CUDA_ROOT/bin/nvcc" -std=c++17 -arch=sm_120 --fmad=false \
  -I "$OPTIX_INCLUDE" -I "$DLSS_ROOT/include" -c "$TASK_DIR/reconstruction.cu" -o "$BUILD_DIR/reconstruction.o"
clang++ -std=c++20 -O3 -ffp-contract=off -I "$CUDA_ROOT/include" -I "$OPTIX_INCLUDE" \
  -I "$BUILD_DIR" -I "$BUILD_DIR/optix" "$BUILD_DIR/optix/render_sequence.cpp" "$BUILD_DIR/reconstruction.o" \
  "$DLSS_ROOT/lib/Linux_x86_64/libnvsdk_ngx.a" -L "$CUDA_ROOT/lib64" -lcudart -lcuda -ldl -lpthread \
  -o "$BUILD_DIR/optix_sequence_reconstruction"
cp "$DLSS_ROOT/lib/Linux_x86_64/rel/libnvidia-ngx-dlssd.so.310.5.3" "$BUILD_DIR/libnvidia-ngx-dlssd.so"
echo "Built $BUILD_DIR/optix_sequence_reconstruction"
