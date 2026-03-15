#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-imx6ull"
TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/imx6ull-armhf.cmake"

if ! command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
  echo "error: arm-linux-gnueabihf-gcc not found"
  exit 1
fi

if ! command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
  cat <<'EOF'
error: arm-linux-gnueabihf-g++ not found

Install the missing cross C++ toolchain first:
  sudo apt-get update
  sudo apt-get install -y \
    g++-arm-linux-gnueabihf \
    gcc-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    libstdc++-9-dev-armhf-cross \
    pkg-config-arm-linux-gnueabihf
EOF
  exit 1
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DCMAKE_SYSROOT=

cmake --build "${BUILD_DIR}" -j"$(nproc)"
