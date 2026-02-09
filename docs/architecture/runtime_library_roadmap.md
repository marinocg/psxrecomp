# Runtime Library Roadmap

This roadmap covers the runtime library that backs recompiled code with PSX hardware abstractions.

## Current Status
- [x] Core runtime interfaces for RAM, MMIO, and system boot.
- [x] Module boundaries for GPU, SPU, CD-ROM, and input.

## Phase 1: Core System
- [x] Define memory map constants and RAM access helpers.
- [x] Provide system boot/reset routines.
- [x] Add a simple logging/tracing facility for runtime events.

## Phase 2: Peripheral Skeletons
- [x] GPU command FIFO placeholder with register stubs.
- [x] SPU register stubs and basic timing hooks.
- [x] CD-ROM command/status interface stubs.
- [x] Controller input abstraction and state storage.

## Phase 3: Integration & Accuracy
- [x] Implement DMA interactions across devices.
- [x] Add interrupt controller behavior and event scheduling.
- [x] Validate MMIO side effects with known test ROMs.

## Phase 4: Tooling & Tests
- [x] Add unit tests for memory map edge cases.
- [x] Add integration tests for device interactions.
- [x] Add regression tests for timing-sensitive behavior.
- [ ] Add structured runtime logging with configurable verbosity levels.
- [ ] Implement debug overlays for performance, frame timing, and counters.
- [ ] Provide memory dump tooling (RAM, VRAM, SPU RAM) for diagnostics.

## Phase 5: Accuracy & Compatibility
- [ ] Expand MMIO coverage for GPU/SPU/CD-ROM registers.
- [ ] Implement BIOS/syscall layer for common kernel services.
- [ ] Add save-state serialization and determinism checks.
- [ ] Add resource pack loader for non-code assets (textures, audio, movies).
