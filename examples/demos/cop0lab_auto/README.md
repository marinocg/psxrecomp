# PSn00bSDK COP0 Auto Lab (no controllers)

`cop0lab_auto` is an autonomous COP0/IRQ/exception validation demo for PSXRecomp runtime work.

It boots, initializes GPU + font, runs a fixed test suite step-by-step, prints PASS/FAIL/SKIP
for each test, shows live register/MMIO telemetry, and auto-restarts after ~3 seconds.

## Key behavior

- No controller input required.
- No `VSync()` dependency for pacing.
- Frame pacing is done by polling VBlank through MMIO (`I_STAT`/`I_MASK`):
  - acknowledge VBlank in `I_STAT`
  - busy-wait for next `I_STAT & 1`
- Exception tests are automatically gated:
  - if exception wrapper install/probe fails, phase-2 exception tests are marked `SKIP` and the suite continues.
- IRQ gating test `T14` uses a VBlank callback counter probe and does not require
  exception-vector patching.

## Tests included

- Phase 0: `T00`, `T01`
- Phase 1: `T10`..`T15`
- Phase 2 (gated): `T20`..`T26`

## Build

```bash
cd examples/demos/cop0lab_auto
make
```

Outputs:

- `COP0LAB.EXE`
- `COP0LAB.iso`
- `COP0LAB.cue`

## Notes

- Exception suite default is disabled (`COP0LAB_ENABLE_EXCEPTION_SUITE=0`) to avoid
  emulator-specific crashes while running baseline COP0/IRQ probes.
- With exception suite disabled, `T15` and `T20`..`T26` are expected to report `SKIP`.
- To compile with exception tests enabled, pass:

```bash
make CFLAGS_EXTRA='-DCOP0LAB_ENABLE_EXCEPTION_SUITE=1'
```
