# First Recompiled Demo Roadmap

This roadmap tracks the milestones needed to ship a first end-to-end recompiled demo executable.

| Phase                           | Estimate |
| ------------------------------- | -------: |
| Overall demo readiness          |     ~68% |
| Phase 1: Pipeline MVP           |     ~90% |
| Phase 2: Runtime Bring-up       |     ~70% |
| Phase 3: Validation & Packaging |     ~45% |

## Current Status (~68%)

- [x] Minimal pipeline that loads PSX-EXE, disassembles, and emits runnable C++ bundles.
- [x] Generated runners include register init, RAM init image, and BIOS vector dispatch.
- [x] Demo corpus selected and reproducible (`ADVHELLO`, `COP0TEST`, `GPUTEST`, `HELLOWLD`, `MEMTEST`, `CDBROWSE`, `CDCRC`, `CDXA`, `GTELAB`).
- [x] Demo verification artifacts include per-demo screenshots and a machine-readable summary CSV.
- [x] CD-focused demos (`CDBROWSE`, `CDCRC`, `CDXA`) now render under the bounded-step validation harness in addition to recompile/resource-extraction checks.
- [x] `GTELAB` now recompiles, builds, and renders under the bounded-step validation harness, making it a practical end-to-end COP2/GTE regression target instead of a compile-only stretch demo.
- [ ] Expected-output parity gates against external emulator traces remain pending.

## Phase 1: Pipeline MVP (~90%)

- [x] Pick a small homebrew PSX-EXE as the target demo.
- [x] Ensure ISO/EXE loader can extract the demo payload (including fixture-driven ISO workflows).
- [x] Disassemble the demo and generate baseline IR.
- [x] Emit C++ that builds (with SDL2 display support via CI workflow).
- [x] Emit C++ that runs and renders under bounded-step verification harness.
- [ ] Emit C++ that runs to natural completion without step-budget limits.

## Phase 2: Runtime Bring-up (~70%)

- [x] Provide minimal runtime stubs for required MMIO accesses.
- [x] Implement BIOS vector framework with 50 stub/functional functions.
- [x] Add logging to validate control flow and memory access (debug env vars: `PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`, `PSXRECOMP_TRACE_BIOS`).
- [x] Add focused COP0 demos (`cop0test`, `cop0lab_auto`) for register/IRQ/exception validation (`MFC0`/`MTC0`/`RFE`, exception resume, IRQ gating/SW pending probes).
- [x] Add focused GTE demo (`gtelab_auto`) for COP2 transfer and transform/lighting validation (`MTC2`/`MFC2`, `CTC2`/`CFC2`, `LWC2`/`SWC2`, `RTPS`/`RTPT`, `NCLIP`, `AVSZ3`/`AVSZ4`, `MVMVA`, `NCDS`).
- [ ] Document deterministic local verification workflow for all demos in one command path.

## Phase 3: Validation & Packaging (~45%)

- [ ] Add automated differential checks against reference emulator traces/screenshots.
- [x] CI workflow compiles generated artifacts on Linux/macOS/Windows.
- [ ] Add automated run/validation (with artifact screenshots + summary checks) in CI.
- [ ] Document known limitations and next steps.
