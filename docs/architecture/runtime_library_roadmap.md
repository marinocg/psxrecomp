# Runtime Library Roadmap

This roadmap covers the runtime library that backs recompiled code with PSX hardware abstractions.

## Current Status
- [ ] Core runtime interfaces for RAM, MMIO, and system boot.
- [ ] Module boundaries for GPU, SPU, CD-ROM, and input.

## Phase 1: Core System
- [ ] Define memory map constants and RAM access helpers.
- [ ] Provide system boot/reset routines.
- [ ] Add a simple logging/tracing facility for runtime events.

## Phase 2: Peripheral Skeletons
- [ ] GPU command FIFO placeholder with register stubs.
- [ ] SPU register stubs and basic timing hooks.
- [ ] CD-ROM command/status interface stubs.
- [ ] Controller input abstraction and state storage.

## Phase 3: Integration & Accuracy
- [ ] Implement DMA interactions across devices.
- [ ] Add interrupt controller behavior and event scheduling.
- [ ] Validate MMIO side effects with known test ROMs.

## Phase 4: Tooling & Tests
- [ ] Add unit tests for memory map edge cases.
- [ ] Add integration tests for device interactions.
- [ ] Add regression tests for timing-sensitive behavior.
