#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_ROOT="${SCRIPT_DIR}/psn00bsdk"
INSTALL_PREFIX="${SDK_ROOT}/local"

sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  curl \
  git \
  libelf-dev \
  ninja-build \
  pkg-config \
  python3 \
  python3-pip \
  unzip \
  make

mkdir -p "${SDK_ROOT}" "${INSTALL_PREFIX}"

install_from_prebuilt() {
  local release_json asset_url asset_name archive_path extract_dir

  release_json="$(curl -fsSL https://api.github.com/repos/Lameguy64/PSn00bSDK/releases/latest)" || return 1

  asset_url="$(python3 - <<'PY' "$release_json"
import json, re, sys
release = json.loads(sys.argv[1])
for asset in release.get('assets', []):
    name = asset.get('name', '')
    if re.search(r'linux', name, re.I) and re.search(r'(x86_64|amd64)', name, re.I):
        if re.search(r'\.(tar\.gz|tgz|zip)$', name, re.I):
            print(asset.get('browser_download_url', ''))
            break
PY
)"

  if [[ -z "${asset_url}" ]]; then
    return 1
  fi

  asset_name="$(basename "${asset_url}")"
  archive_path="${SDK_ROOT}/${asset_name}"
  extract_dir="${SDK_ROOT}/_prebuilt_extract"

  echo "Downloading prebuilt PSn00bSDK asset: ${asset_name}"
  curl -fL "${asset_url}" -o "${archive_path}"

  rm -rf "${extract_dir}"
  mkdir -p "${extract_dir}"

  case "${asset_name}" in
    *.zip) unzip -q "${archive_path}" -d "${extract_dir}" ;;
    *.tar.gz|*.tgz) tar -xzf "${archive_path}" -C "${extract_dir}" ;;
    *) return 1 ;;
  esac

  local candidate
  candidate="$(find "${extract_dir}" -type f -name psxapi.h | head -n1 | xargs -r dirname | xargs -r dirname)"
  if [[ -z "${candidate}" || ! -f "${candidate}/include/psxapi.h" || ! -f "${candidate}/lib/psx.ld" ]]; then
    return 1
  fi

  rm -rf "${INSTALL_PREFIX}"/*
  cp -a "${candidate}"/* "${INSTALL_PREFIX}/"

  if [[ ! -f "${INSTALL_PREFIX}/include/psxapi.h" || ! -f "${INSTALL_PREFIX}/lib/psx.ld" ]]; then
    return 1
  fi

  return 0
}

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

if ! install_from_prebuilt; then
  echo "No suitable prebuilt Linux release found; building PSn00bSDK from source."

  if [[ ! -d "${SDK_ROOT}/.git" ]]; then
    rm -rf "${SDK_ROOT}"
    git clone --recursive https://github.com/Lameguy64/PSn00bSDK.git "${SDK_ROOT}"
  fi

  cd "${SDK_ROOT}"
  cmake -S . -B build -G Ninja
  cmake --build build -j"$(nproc)"
  cmake --install build --prefix "${INSTALL_PREFIX}"
fi

if [[ ! -f "${INSTALL_PREFIX}/include/psxapi.h" || ! -f "${INSTALL_PREFIX}/lib/psx.ld" ]]; then
  echo "ERROR: PSn00bSDK installation is incomplete: expected ${INSTALL_PREFIX}/include/psxapi.h and ${INSTALL_PREFIX}/lib/psx.ld" >&2
  echo "Try re-running the installer; if it still fails, delete ${SDK_ROOT} and run again to force a clean source build." >&2
  exit 1
fi

if ! command -v mkpsxiso >/dev/null 2>&1; then
  sudo apt-get install -y mkpsxiso || true
fi

PROFILE_LINE_1="export PSN00BSDK=${INSTALL_PREFIX}"
PROFILE_LINE_2='export PATH="$PSN00BSDK/bin:$PATH"'

echo "${PROFILE_LINE_1}"
echo "${PROFILE_LINE_2}"
echo "Add those lines to ~/.bashrc or ~/.zshrc and restart your shell."
