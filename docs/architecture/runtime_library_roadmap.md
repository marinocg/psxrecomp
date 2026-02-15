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
- [x] Add structured runtime logging with configurable verbosity levels.
- [x] Implement debug overlays for performance, frame timing, and counters.
- [x] Provide memory dump tooling (RAM, VRAM, SPU RAM) for diagnostics.

## Phase 5: Accuracy & Compatibility
- [x] Expand MMIO coverage for GPU/SPU/CD-ROM registers.
- [x] Implement BIOS/syscall layer for common kernel services.
- [x] Add save-state serialization and determinism checks.
- [x] Add resource pack loader for non-code assets (textures, audio, movies).

## Phase 6: BIOS Coverage Expansion
- [x] Implement BIOS vector dispatch framework (`callBiosVector` for A0/B0/C0).
- [x] Implement functional string/memory BIOS functions (strcmp, strcpy, memcpy, memset, bzero).
- [x] Implement GPU BIOS helpers (GPU_cw, GPU_cwp).
- [x] Implement event management stubs (OpenEvent, CloseEvent, WaitEvent, TestEvent, EnableEvent, DisableEvent).
- [x] Implement pad/controller/memory-card init stubs.
- [x] Implement system initialization C0 stubs.
- [x] Add BIOS trace support (`PSXRECOMP_TRACE_BIOS` env var).
- [x] Refactor BIOS code into dedicated `psx_system_bios.cpp` module.
- [ ] Implement `printf` (A0:0x3F) with format string support.
- [ ] Implement threading functions (OpenThread, CloseThread, ChangeThread).
- [ ] Implement timer functions (init_timer, get_timer, enable/disable_timer_irq).
- [ ] Implement CD-ROM BIOS functions (CdInit, CdRemove).
- [ ] Implement memory card sector read/write.
- [ ] See full checklist: [BIOS Functions Roadmap](bios_functions_roadmap.md)
