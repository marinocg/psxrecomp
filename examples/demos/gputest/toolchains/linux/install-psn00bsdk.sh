#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="${SCRIPT_DIR}/psn00bsdk"

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  git \
  libelf-dev \
  pkg-config \
  python3 \
  python3-pip

if [[ ! -d "${SDK_ROOT}" ]]; then
  git clone --recursive https://github.com/Lameguy64/PSn00bSDK.git "${SDK_ROOT}"
fi

cd "${SDK_ROOT}"
cmake -S . -B build
cmake --build build -j"$(nproc)"
cmake --install build --prefix "${SDK_ROOT}/local"

if ! command -v mkpsxiso >/dev/null 2>&1; then
  sudo apt-get install -y mkpsxiso || true
fi

PROFILE_LINE_1="export PSN00BSDK=${SDK_ROOT}/local"
PROFILE_LINE_2='export PATH="$PSN00BSDK/bin:$PATH"'

echo "${PROFILE_LINE_1}"
echo "${PROFILE_LINE_2}"
echo "Add those lines to ~/.bashrc or ~/.zshrc and restart your shell."
