# Shared demo toolchain setup

These scripts are shared by all demo projects under `examples/demos/*`:

- `linux/install-psn00bsdk.sh`
- `macos/install-psn00bsdk.sh`
- `windows/install-psn00bsdk.ps1`

Run the setup script for your platform once, then build any demo.

Linux and Windows scripts try to use prebuilt PSn00bSDK releases first, then fall back to source builds when no matching asset is available.
