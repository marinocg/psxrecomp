# First Recompiled Demo Roadmap

This roadmap tracks the milestones needed to ship a first end-to-end recompiled demo executable.

| Phase | Estimate |
|---|---:|
| Overall demo readiness | ~40% |
| Phase 1: Pipeline MVP | ~60% |
| Phase 2: Runtime Bring-up | ~25% |
| Phase 3: Validation & Packaging | ~15% |

## Current Status (~40%)
- [x] Minimal pipeline that loads PSX-EXE, disassembles, and emits runnable C++ bundles.
- [ ] Demo selection and expected output definition (fixture-driven path is in place; rich emulator-playback demo remains pending).

## Phase 1: Pipeline MVP (~60%)
- [ ] Pick a small homebrew PSX-EXE as the target demo.
- [x] Ensure ISO/EXE loader can extract the demo payload (including fixture-driven ISO workflows).
- [ ] Disassemble the demo and generate baseline IR.
- [ ] Emit C++ that builds and runs to completion.

## Phase 2: Runtime Bring-up (~25%)
- [ ] Provide minimal runtime stubs for required MMIO accesses.
- [ ] Add logging to validate control flow and memory access.
- [ ] Document build/run steps for the demo.

## Phase 3: Validation & Packaging (~15%)
- [ ] Compare output or memory traces with a reference emulator.
- [ ] Add automated build/test for the demo in CI.
- [ ] Document known limitations and next steps.
