# Full Game Support Roadmap

This roadmap outlines the major milestones required to reach full commercial game support.

## Current Status
- [ ] Stable end-to-end pipeline for a limited demo title.
- [ ] Baseline runtime subsystems for GPU, SPU, and CD-ROM.

## Phase 1: Compatibility Foundation
- [ ] Expand instruction, IR, and recompiler coverage for common game code paths.
- [ ] Strengthen runtime memory model and MMIO accuracy.
- [ ] Add robust logging, tracing, and diagnostics.

## Phase 2: Hardware Completeness
- [ ] Implement GPU and SPU feature-complete behavior.
- [ ] Add CD-ROM audio/data streaming and XA decoding.
- [ ] Emulate timers, interrupts, and DMA with correct scheduling.

## Phase 3: Performance & Stability
- [ ] Introduce caching and optimization passes in the recompiler.
- [ ] Add save states and deterministic execution hooks.
- [ ] Build automated regression test suites across multiple titles.

## Phase 4: Release Readiness
- [ ] Document compatibility list and known issues.
- [ ] Add packaging/build scripts for supported platforms.
- [ ] Provide end-user configuration and debugging tools.
