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
- [x] Validate COP2/GTE command paths with focused demo coverage (`examples/demos/gtelab_auto`) for transfer, transform, and lighting op sequences.
- [x] Add runtime GTE device skeleton in `PsxSystem` with separate data/control banks, COP2-only accessors, placeholder FIFOs, and busy-cycle tracking.
- [x] Lower COP2 transfer ops (`MFC2`/`MTC2`/`CFC2`/`CTC2`) through IR/codegen into the runtime GTE device with `Status.CU2` guard behavior (`CoprocessorUnusable` when disabled).
- [x] Lower COP2 memory-backed data-register transfers (`LWC2`/`SWC2`) through dedicated IR/codegen paths so later GTE stall modeling stays distinct from generic load/store lowering.
- [x] Lower decoded GTE command opcodes through a generic `GTE_EXEC` IR op and implement the first transform/depth execution subset (`RTPS`/`RTPT`/`NCLIP`/`AVSZ3`/`AVSZ4`/`MVMVA`) against raw instruction bits in the runtime.
- [x] Flesh out the lighting/color command family (`DPCS`, `INTPL`, `NCDS`, `CDP`, `NCDT`, `NCCS`, `CC`, `NCS`, `NCT`, `DCPL`, `DPCT`, `GPF`, `GPL`, `NCCT`) with RGB FIFO, far/background color, and lighting/color matrix usage.
- [x] Add GTE timing/state fidelity hooks: command cycle countdown, CPU stall on COP2 reads / next command while busy, no stall on writes, IRGB/ORGB delayed read behavior, LZCS/LZCR register handling, and save-state serialization of in-flight GTE state.

## Phase 6: BIOS Coverage Expansion

- [x] Implement BIOS vector dispatch framework (`callBiosVector` for A0/B0/C0).
- [x] Implement functional string/memory BIOS functions (strcmp, strcpy, memcpy, memset, bzero).
- [x] Implement GPU BIOS helpers (GPU_cw, GPU_cwp, send_gpu_linked_list, GPU_init, GPU_sync).
- [x] Implement functional kernel event handling, including blocking `WaitEvent` for `NoCallback` events.
- [x] Implement pad/controller/memory-card init stubs.
- [x] Implement system initialization C0 stubs.
- [x] Add BIOS trace support (`PSXRECOMP_TRACE_BIOS` env var).
- [x] Refactor BIOS code into dedicated `psx_system_bios.cpp` module.
- [x] Align BIOS internal CD-ROM helpers with PSX-SPX: boot-time `_96_init` state, `A0(0x71)` re-init behavior, bug-compatible `A0(0x72)` `_96_remove`, and BIOS-owned `F0000003` event lifecycle.
- [x] Implement `printf` (A0:0x3F) logging support for common string/integer/pointer specifiers; richer format coverage may still need expansion.
- [ ] Implement threading functions (OpenThread, CloseThread, ChangeThread).
- [ ] Implement timer functions (init_timer, get_timer, enable/disable_timer_irq).
- [x] Implement BIOS-facing CD-ROM helper calls (`CdInit`, `CdRemove`, `CdAsyncSeekL`, `CdAsyncGetStatus`, `CdAsyncReadSector`, `CdAsyncSetMode`, `CdInitSubFunc`).
- [x] Implement a read-only BIOS file/device layer (`FileOpen`, `FileSeek`, `FileRead`, `FileClose`, `firstfile`, `nextfile`) backed directly by mounted ISO 9660 disc contents.
- [ ] Implement memory card sector read/write.
- [ ] See full checklist: [BIOS Functions Roadmap](bios_functions_roadmap.md)

## Phase 7: COP0 and Exception Semantics

- [x] Add runtime COP0 register model for PSX-critical registers (`Status`, `Cause`, `EPC`, `BadVAddr`).
- [x] Implement `mfc0`/`mtc0` behavior for supported registers (`Status`, `Cause`, `EPC`, `BadVAddr`).
- [x] Implement exception entry bookkeeping:
  - status mode stack push (low 6 bits),
  - `Cause.ExcCode` write,
  - delay-slot `BD` write,
  - `EPC` write (`pc` or `pc-4` in delay slot),
  - optional `BadVAddr` capture.
- [x] Implement `rfe` restore semantics for status mode stack pop.
- [x] Wire COP0 into `PsxSystem` reset path and save-state serialization/deserialization.
- [x] Validate COP0 flow with runtime/unit tests and focused demos (`examples/demos/cop0test`, `examples/demos/cop0lab_auto`).
- [x] Route recompiled non-BIOS syscall codes through COP0 exception entry and vector dispatch.
- [x] Gate IRQ delivery/exception entry with COP0 status/pending model (`Status.IEc`, `Status.IM2`, `Cause.IP2`) while preserving runtime callback/critical-section guards.
- [x] Make IRQ service own COP0 exception exit (`rfe`) so Status mode bits are restored even when IRQ callbacks return normally or abort via `ReturnFromException`.
- [x] Restrict `mtc0 Cause` writes to software-interrupt bits without clobbering hardware pending bits.
- [x] Seed minimal boot-time COP0 Status defaults for demos (`IEc=1`, `IM2=1`, `KUc=0`).
- [ ] Unify BEV vector selection behavior across all exception/IRQ dispatch paths.
- [ ] Implement precise reset/boot-time COP0 defaults to improve emulator/hardware numeric parity (beyond the current minimal demo-safe defaults).
- [ ] Add runtime trace hooks for COP0 reads/writes (`mfc0`/`mtc0`) keyed by current PC for debugging.
