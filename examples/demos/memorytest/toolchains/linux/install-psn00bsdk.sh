#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="${SCRIPT_DIR}/psn00bsdk"
INSTALL_PREFIX="${SDK_ROOT}/local"

sudo apt-get update
sudo apt-get install -y   build-essential   cmake   git   libelf-dev   ninja-build   pkg-config   python3   python3-pip   make

if ! command -v mipsel-none-elf-gcc >/dev/null 2>&1; then
  echo "mipsel-none-elf-gcc not found. Attempting to install Debian/Ubuntu cross compiler packages..."
  sudo apt-get install -y gcc-mipsel-none-elf binutils-mipsel-none-elf || true
fi

if ! command -v mipsel-none-elf-gcc >/dev/null 2>&1; then
  echo "ERROR: mipsel-none-elf-gcc is still not available." >&2
  echo "Install a PSX-compatible MIPS ELF toolchain first, then re-run this script." >&2
  echo "On WSL/Ubuntu, this usually means installing gcc-mipsel-none-elf and binutils-mipsel-none-elf." >&2
  exit 1
fi

if [[ ! -d "${SDK_ROOT}" ]]; then
  git clone --recursive https://github.com/Lameguy64/PSn00bSDK.git "${SDK_ROOT}"
fi

cd "${SDK_ROOT}"
cmake -S . -B build -G Ninja
cmake --build build -j"$(nproc)"
cmake --install build --prefix "${INSTALL_PREFIX}"

if ! command -v mkpsxiso >/dev/null 2>&1; then
  sudo apt-get install -y mkpsxiso || true
fi

PROFILE_LINE_1="export PSN00BSDK=${INSTALL_PREFIX}"
PROFILE_LINE_2='export PATH="$PSN00BSDK/bin:$PATH"'

echo "${PROFILE_LINE_1}"
echo "${PROFILE_LINE_2}"
echo "Add those lines to ~/.bashrc or ~/.zshrc and restart your shell."
