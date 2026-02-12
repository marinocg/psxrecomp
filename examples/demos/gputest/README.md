# PSn00bSDK GPU test ISO demo (PSX)

This demo builds a PlayStation 1 GPU test executable and packages it as a bootable CD image with **PSn00bSDK** + **mkpsxiso**.

## Project layout

```text
examples/demos/gputest/
├── Makefile
├── README.md
├── mkpsxiso.xml
├── system.cnf
├── src/
│   └── main.c
```

Toolchain setup scripts are shared for all demos in `examples/demos/toolchains/`.

## Build

```bash
cd examples/demos/gputest
make
```

Generated outputs:
- `GPUTEST.EXE`
- `GPUTEST.iso`
- `GPUTEST.cue`

Clean:

```bash
make clean
```

## Platform setup

### Linux

```bash
cd examples/demos/toolchains/linux
./install-psn00bsdk.sh
```

> Linux script behavior: it attempts a prebuilt PSn00bSDK release first, then falls back to source build if no matching release asset is found.

> WSL/Ubuntu note: if setup fails with `Could not find ... mipsel-none-elf-gcc`, install `gcc-mipsel-none-elf` and `binutils-mipsel-none-elf`, then re-run the setup script.

### macOS

```bash
cd examples/demos/toolchains/macos
./install-psn00bsdk.sh
```

### Windows (PowerShell)

```powershell
cd examples\demos\toolchains\windows
powershell -ExecutionPolicy Bypass -File .\install-psn00bsdk.ps1
```

> Windows script behavior: it attempts a prebuilt PSn00bSDK release first, then falls back to source build if no matching release asset is found.

After setup, open a new shell and verify:

```bash
mipsel-none-elf-gcc --version
mkpsxiso -h
test -f "$PSN00BSDK/include/psxapi.h"
```
