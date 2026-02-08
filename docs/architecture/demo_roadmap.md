# First Recompiled Demo Roadmap

This roadmap tracks the milestones needed to ship a first end-to-end recompiled demo executable.

## Current Status
- [ ] Minimal pipeline that loads PSX-EXE, disassembles, and emits runnable C++.
- [ ] Demo selection and expected output definition.

## Phase 1: Pipeline MVP
- [ ] Pick a small homebrew PSX-EXE as the target demo.
- [ ] Ensure ISO/EXE loader can extract the demo payload.
- [ ] Disassemble the demo and generate baseline IR.
- [ ] Emit C++ that builds and runs to completion.

## Phase 2: Runtime Bring-up
- [ ] Provide minimal runtime stubs for required MMIO accesses.
- [ ] Add logging to validate control flow and memory access.
- [ ] Document build/run steps for the demo.

## Phase 3: Validation & Packaging
- [ ] Compare output or memory traces with a reference emulator.
- [ ] Add automated build/test for the demo in CI.
- [ ] Document known limitations and next steps.
