#!/bin/sh
set -eu
[ "$(uname -s)" = Linux ] || { printf 'Cloud preflight requires Linux.\n' >&2; exit 2; }
CUDA_TOOLKIT=${CUDA_TOOLKIT:-/usr/local/cuda}
CUDA_CLANG=${CUDA_CLANG:-clang++-21}
if [ "${1:-}" = host ]; then CUDA_CLANG=${CUDA_HOST_CLANG:-clang++}; fi
command -v nvidia-smi
nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv
test -f "$CUDA_TOOLKIT/include/cuda_runtime.h"
"$CUDA_CLANG" --version
if [ "${1:-}" != host ]; then
 test -f "$CUDA_TOOLKIT/nvvm/libdevice/libdevice.10.bc"
 "$CUDA_CLANG" --print-targets | grep 'nvptx' || { printf 'Clang has no NVPTX backend\n' >&2; exit 1; }
fi
python3 - <<'PY'
import ctypes,subprocess
versions=subprocess.check_output(['nvidia-smi','--query-gpu=driver_version','--format=csv,noheader'],text=True).splitlines()
if any(int(v.split('.')[0])<570 for v in versions): raise SystemExit('OptiX 9 requires NVIDIA R570 or later.')
ctypes.CDLL('libcuda.so.1')
ctypes.CDLL('libnvoptix.so.1')
print('NVIDIA CUDA driver and OptiX driver library are visible.')
PY
