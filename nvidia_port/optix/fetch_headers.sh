#!/bin/sh
# Official NVIDIA headers only, pinned release and SHA-256. No system installation.
set -eu
PORT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK_DIR="$PORT_DIR/build/optix-sdk"
if [ -f "$SDK_DIR/include/optix.h" ]; then
 printf '%s\n' "$SDK_DIR"
 exit 0
fi
mkdir -p "$PORT_DIR/build"
ARCHIVE="$PORT_DIR/build/optix-dev-v9.0.0.tar.gz"
curl -fL --retry 2 https://codeload.github.com/NVIDIA/optix-dev/tar.gz/refs/tags/v9.0.0 -o "$ARCHIVE"
WANT=069a5860040ea611e7eb6317f8e3bb0f0d54a5acac744568f7290d7cb8711c05
if command -v sha256sum >/dev/null 2>&1; then
 GOT=$(sha256sum "$ARCHIVE" | cut -d ' ' -f 1)
else
 GOT=$(shasum -a 256 "$ARCHIVE" | cut -d ' ' -f 1)
fi
[ "$GOT" = "$WANT" ] || { printf 'OptiX archive checksum mismatch\n' >&2; exit 1; }
mkdir -p "$SDK_DIR"
tar -xzf "$ARCHIVE" --strip-components=1 -C "$SDK_DIR"
printf '%s\n' "$SDK_DIR"
