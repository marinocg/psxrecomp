# PSn00bSDK Hello World ISO (PSX)

This demo provides a complete starter project that builds a PlayStation 1 hello world executable and packs it into a bootable CD image (`HELLOWLD.iso`) using **PSn00bSDK** and **mkpsxiso**.

## Project layout

```text
examples/demos/helloworld/
├── Makefile
├── README.md
├── mkpsxiso.xml
├── system.cnf
├── src/
│   └── main.c
```

Toolchain setup scripts are shared for all demos in `examples/demos/toolchains/`.

## Build steps

```bash
cd examples/demos/helloworld
make
```

Generated outputs:
- `HELLOWLD.EXE`
- `HELLOWLD.iso`
- `HELLOWLD.cue`

Clean outputs:

```bash
make clean
```

## Linux setup

```bash
cd examples/demos/toolchains/linux
./install-psn00bsdk.sh
```

> Linux script behavior: it attempts a prebuilt PSn00bSDK release first, then falls back to source build if no matching release asset is found.

> WSL/Ubuntu note: if setup fails with `Could not find ... mipsel-none-elf-gcc`, install `gcc-mipsel-none-elf` and `binutils-mipsel-none-elf`, then re-run the setup script.

## macOS setup

```bash
cd examples/demos/toolchains/macos
./install-psn00bsdk.sh
```

## Windows setup (PowerShell)

```powershell
cd examples\demos\toolchains\windows
powershell -ExecutionPolicy Bypass -File .\install-psn00bsdk.ps1
```

> Windows script behavior: it attempts a prebuilt PSn00bSDK release first, then falls back to source build if no matching release asset is found.

After setup, open a new shell and verify:

```bash
mipsel-none-elf-gcc --version
mkpsxiso -h
```
