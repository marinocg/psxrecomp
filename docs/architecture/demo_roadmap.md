# First Recompiled Demo Roadmap

This roadmap tracks the milestones needed to ship a first end-to-end recompiled demo executable.

| Phase | Estimate |
|---|---:|
| Overall demo readiness | ~55% |
| Phase 1: Pipeline MVP | ~75% |
| Phase 2: Runtime Bring-up | ~55% |
| Phase 3: Validation & Packaging | ~25% |

## Current Status (~55%)
- [x] Minimal pipeline that loads PSX-EXE, disassembles, and emits runnable C++ bundles.
- [x] Generated runners include register init, RAM init image, and BIOS vector dispatch.
- [ ] Demo selection and expected output definition (fixture-driven path is in place; rich emulator-playback demo remains pending).

## Phase 1: Pipeline MVP (~75%)
- [ ] Pick a small homebrew PSX-EXE as the target demo.
- [x] Ensure ISO/EXE loader can extract the demo payload (including fixture-driven ISO workflows).
- [x] Disassemble the demo and generate baseline IR.
- [x] Emit C++ that builds (with SDL2 display support via CI workflow).
- [ ] Emit C++ that runs to completion without hangs.

## Phase 2: Runtime Bring-up (~55%)
- [x] Provide minimal runtime stubs for required MMIO accesses.
- [x] Implement BIOS vector framework with 48 stub/functional functions.
- [x] Add logging to validate control flow and memory access (debug env vars: `PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`, `PSXRECOMP_TRACE_BIOS`).
- [ ] Document build/run steps for the demo.

## Phase 3: Validation & Packaging (~25%)
- [ ] Compare output or memory traces with a reference emulator.
- [x] CI workflow compiles generated artifacts on Linux/macOS/Windows.
- [ ] Add automated run/validation for the demo in CI.
- [ ] Document known limitations and next steps.
