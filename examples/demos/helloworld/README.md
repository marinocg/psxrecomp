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
└── toolchains/
    ├── linux/install-psn00bsdk.sh
    ├── macos/install-psn00bsdk.sh
    └── windows/install-psn00bsdk.ps1
```

## Requirements

- PSn00bSDK toolchain (`mipsel-none-elf-gcc` and PSX libraries)
- `mkpsxiso`
- `make`

> The included platform scripts install PSn00bSDK to a local folder and print the environment variables you should export in your shell.

---

## Build steps (all platforms)

From repository root:

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

---

## Linux setup

```bash
cd examples/demos/helloworld/toolchains/linux
./install-psn00bsdk.sh
```

Then open a new shell (or source your profile) and verify:

```bash
mipsel-none-elf-gcc --version
mkpsxiso --version
```

---

## macOS setup

```bash
cd examples/demos/helloworld/toolchains/macos
./install-psn00bsdk.sh
```

Then open a new shell (or source your profile) and verify:

```bash
mipsel-none-elf-gcc --version
mkpsxiso --version
```

---

## Windows setup (PowerShell)

```powershell
cd examples\demos\helloworld\toolchains\windows
powershell -ExecutionPolicy Bypass -File .\install-psn00bsdk.ps1
```

After installation, open a new PowerShell window and verify:

```powershell
mipsel-none-elf-gcc --version
mkpsxiso --version
```

---

## Testing the generated ISO

You can boot `HELLOWLD.iso` in emulators such as DuckStation/PCSX-Redux or on hardware using your preferred ODE/modchip workflow.

