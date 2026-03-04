# Current Gaps Report

This document lists the known gaps between the current implementation and the expected
end-to-end static recompilation workflow. It complements the workstream roadmaps and the
master roadmap.

## Gap scorecard (estimate)

| Area                           | Estimate | Gap trend    |
| ------------------------------ | -------: | ------------ |
| Pipeline & Tooling             |     ~64% | Improving    |
| Disassembly & Analysis         |     ~77% | Improving    |
| IR & Optimization              |     ~87% | Improving    |
| Recompiler / Code Generation   |     ~82% | Improving    |
| Runtime Library                |     ~77% | Improving    |
| GPU Emulation                  |     ~76% | Improving    |
| SPU Emulation                  |     ~45% | Moderate gap |
| CD-ROM Runtime                 |     ~42% | Moderate gap |
| Testing & Validation           |     ~46% | Improving    |
| Documentation & Dev Experience |     ~70% | Improving    |

## Pipeline & Tooling (~64%)

- Pipeline now emits deterministic bundles (C++/CMake/manifest/resources/runtime copy), but it still does not auto-build/run generated outputs from the main CLI flow.
- CI recompile-demos workflow now installs SDL2 on all three platforms (Linux, macOS, Windows) for display presenter support in generated binaries.
- Recompiled demo verification now emits screenshot artifacts and a summary CSV for ADVHELLO/COP0TEST/GPUTEST/HELLOWLD/MEMTEST.
- Structured diagnostics are available for EXE and ISO paths, but richer remediation hints and per-stage timing telemetry are still limited.

## Disassembly & Analysis (~77%)

- Entry-function fall-through merge now correctly handles entry stubs that lack terminators before the next prologue.
- Delay-slot semantics are still not fully modeled in all CFG/IR edge cases.
- Jump table and code/data heuristics need broader validation against real game binaries.
- Coprocessor-heavy and hand-written assembly patterns remain under-tested.

## IR & Optimization (~87%)

- BIOS JAL targets (A0/B0/C0 vector addresses) are now lowered to CALL ops, ensuring correct dispatch through `callBiosVector`.
- COP0 IR now supports `MFC0`/`MTC0`/`RFE`; remaining COP0 control-flow variants (`CFC0`/`CTC0`, `BC0F`/`BC0T`) are still pending.
- IR lowering coverage is much broader, but not complete for all PSX instruction patterns and coprocessor behavior.
- Byte/halfword/unaligned memory semantics are still simplified in parts of lowering/codegen.
- Optimization passes exist, but there is no profile-guided or game-specific tuning layer yet.

## Recompiler / Code Generation (~82%)

- Generated C++ is now structured and buildable with bundled runtime code; block-external continuation dispatch, self-loop prevention, register init (SP/GP/FP/RA), and RAM init image emission are all in place.
- Debug environment variables (`PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`) allow runtime introspection of generated binaries.
- Runner catch block now reports GPU command count, framebuffer pixel stats, and optional PPM framebuffer dump for post-mortem analysis.
- Frame presenter/dump path now supports `PSXRECOMP_RENDER_DEBUG_OVERLAY=1` for HUD compositing (including FPS), and runtime logger filtering honors `PSXRECOMP_LOG_LEVEL`.
- Syscall lowering now routes non-BIOS syscall codes into COP0 exception entry and vector dispatch so exception handlers execute correctly in recompiled demos.
- ABI/calling-convention fidelity is still incomplete for complex binaries.
- Inlining/regalloc-style hints and deeper code quality optimizations are limited.

## Runtime Library (~77%)

- MMIO coverage and runtime scaffolding improved, but many device-accurate edge cases are still missing.
- DMA/interrupt routing is present; cycle-accurate timing remains incomplete.
- Minimal COP0 runtime semantics are now implemented (`Status`/`Cause`/`EPC`/`BadVAddr`, exception entry mode stack, delay-slot EPC/BD bookkeeping, `RFE` restore).
- BIOS vector framework now covers 50 functions (16 A0, 23 B0, 11 C0; ~23% of known BIOS surface), including GPU_init (A0:70h), GPU_sync (B0:46h), and `send_gpu_linked_list` (A0:4Bh) support.
- BIOS trace support via `PSXRECOMP_TRACE_BIOS` env var aids debugging.
- COP0 now gates IRQ exception entry on `Status.IEc` + (`Status.IM` & `Cause.IP`) and mirrors IRQ-controller pending state into `Cause.IP2` (with `mtc0 Cause` limited to software IP bits).
- Remaining COP0 gap: BEV vector parity across all exception paths and closer reset/boot-state parity with emulator/hardware defaults.
- Remaining gap: ~168 BIOS functions still unimplemented (printf, threading, CD-ROM init, memory card I/O).

## GPU / SPU / CD-ROM (GPU ~76% / SPU ~45% / CD-ROM ~42%)

- GPU: Phase 3 reference rasterization is now feature-complete (triangle/quad/line/sprite rules, clipping/offset/texture-window state, texture sampling, CLUT, blending, mask bits, and dithering paths). GP0(02h) Fill Rectangle now conforms to PSX-SPX: raw VRAM coordinates, no draw-area clipping, no mask-bit interaction. Runtime command/transfer correctness now also covers DMA6 OTC ordering-table clear, fixed packet lengths for key GP0 primitive families, and deterministic per-command CLUT/TPAGE snapshots for sprite-heavy paths. Timing/display synchronization and cross-emulator capture parity remain open.
- SPU: Phase 1-2 core path is implemented (voices, ADSR, decode, mixing, backend hookup), but timing/IRQ, XA decode handoff, and hardware-parity validation remain open.
- CD-ROM: runtime command/data FIFOs, DMA transfer path, and baseline XA ReadN/ReadS streaming are implemented; timing fidelity, validation, and XA decode remain open.

## Testing & Validation (~46%)

- 22 unit tests now pass consistently, covering ISO parsing, fixture-based pipeline checks, codegen lowering, COP0 semantics, BIOS vector dispatch, runtime system initialization, entry-function merge, and GPU fill-rect PSX-SPX correctness.
- Current demo verification corpus (ADVHELLO, COP0TEST, GPUTEST, HELLOWLD, MEMTEST) reports `PASS` render checks with per-demo screenshots and summary metadata.
- GPU parity groundwork now includes a versioned trace corpus + golden metadata format; however, there is still no automated golden-output emulator parity suite.
- No standardized catalog of larger real-world demo/game regression inputs in CI.
- End-to-end runtime playback validation remains mostly manual.

## Documentation & Dev Experience (~70%)

- Master roadmap, sub-roadmaps, implementation status, and BIOS functions roadmap are current.
- Agent collaboration guide (`agents.md`) provides onboarding context.
- More operator-facing troubleshooting docs are needed for malformed disc images and mixed-format edge cases.
