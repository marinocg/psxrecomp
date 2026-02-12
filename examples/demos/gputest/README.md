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
└── toolchains/
    ├── linux/install-psn00bsdk.sh
    ├── macos/install-psn00bsdk.sh
    └── windows/install-psn00bsdk.ps1
```

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
cd examples/demos/gputest/toolchains/linux
./install-psn00bsdk.sh
```

> WSL/Ubuntu note: if setup fails with `Could not find ... mipsel-none-elf-gcc`, install `gcc-mipsel-none-elf` and `binutils-mipsel-none-elf`, then re-run the setup script.

### macOS

```bash
cd examples/demos/gputest/toolchains/macos
./install-psn00bsdk.sh
```

### Windows (PowerShell)

```powershell
cd examples\demos\gputest\toolchains\windows
powershell -ExecutionPolicy Bypass -File .\install-psn00bsdk.ps1
```

After setup, open a new shell and verify:

```bash
mipsel-none-elf-gcc --version
mkpsxiso -h
```
