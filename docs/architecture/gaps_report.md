# Current Gaps Report

This document lists the known gaps between the current implementation and the expected
end-to-end static recompilation workflow. It complements the workstream roadmaps and the
master roadmap.

## Gap scorecard (estimate)

| Area                           | Estimate | Gap trend    |
| ------------------------------ | -------: | ------------ |
| Pipeline & Tooling             |     ~62% | Improving    |
| Disassembly & Analysis         |     ~75% | Improving    |
| IR & Optimization              |     ~85% | Improving    |
| Recompiler / Code Generation   |     ~80% | Improving    |
| Runtime Library                |     ~75% | Improving    |
| GPU Emulation                  |     ~76% | Improving    |
| SPU Emulation                  |     ~45% | Moderate gap |
| CD-ROM Runtime                 |     ~42% | Moderate gap |
| Testing & Validation           |     ~42% | Improving    |
| Documentation & Dev Experience |     ~70% | Improving    |

## Pipeline & Tooling (~62%)

- Pipeline now emits deterministic bundles (C++/CMake/manifest/resources/runtime copy), but it still does not auto-build/run generated outputs from the main CLI flow.
- CI recompile-demos workflow now installs SDL2 on all three platforms (Linux, macOS, Windows) for display presenter support in generated binaries.
- Structured diagnostics are available for EXE and ISO paths, but richer remediation hints and per-stage timing telemetry are still limited.

## Disassembly & Analysis (~75%)

- Entry-function fall-through merge now correctly handles entry stubs that lack terminators before the next prologue.
- Delay-slot semantics are still not fully modeled in all CFG/IR edge cases.
- Jump table and code/data heuristics need broader validation against real game binaries.
- Coprocessor-heavy and hand-written assembly patterns remain under-tested.

## IR & Optimization (~85%)

- BIOS JAL targets (A0/B0/C0 vector addresses) are now lowered to CALL ops, ensuring correct dispatch through `callBiosVector`.
- IR lowering coverage is much broader, but not complete for all PSX instruction patterns and coprocessor behavior.
- Byte/halfword/unaligned memory semantics are still simplified in parts of lowering/codegen.
- Optimization passes exist, but there is no profile-guided or game-specific tuning layer yet.

## Recompiler / Code Generation (~80%)

- Generated C++ is now structured and buildable with bundled runtime code; block-external continuation dispatch, self-loop prevention, register init (SP/GP/FP/RA), and RAM init image emission are all in place.
- Debug environment variables (`PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`) allow runtime introspection of generated binaries.
- Runner catch block now reports GPU command count, framebuffer pixel stats, and optional PPM framebuffer dump for post-mortem analysis.
- ABI/calling-convention fidelity is still incomplete for complex binaries.
- Inlining/regalloc-style hints and deeper code quality optimizations are limited.

## Runtime Library (~75%)

- MMIO coverage and runtime scaffolding improved, but many device-accurate edge cases are still missing.
- DMA/interrupt routing is present; cycle-accurate timing remains incomplete.
- BIOS vector framework implemented with 48 functions across A0/B0/C0 tables (~22% of known BIOS surface). Functional implementations exist for string/memory ops, GPU helpers, events, and system init stubs. See [BIOS Functions Roadmap](bios_functions_roadmap.md) for the full checklist.
- BIOS trace support via `PSXRECOMP_TRACE_BIOS` env var aids debugging.
- BIOS vector framework now covers 50 functions (16 A0, 23 B0, 11 C0), including new GPU_init (A0:70h) and GPU_sync (B0:46h) stubs.
- Remaining gap: ~168 BIOS functions still unimplemented (printf, threading, CD-ROM init, memory card I/O).

## GPU / SPU / CD-ROM (GPU ~74% / SPU ~5% / CD-ROM ~42%)

- GPU: Phase 3 reference rasterization is now feature-complete (triangle/quad/line/sprite rules, clipping/offset/texture-window state, texture sampling, CLUT, blending, mask bits, and dithering paths). GP0(02h) Fill Rectangle now conforms to PSX-SPX: raw VRAM coordinates, no draw-area clipping, no mask-bit interaction. Timing/display synchronization and cross-emulator capture parity remain open.
- SPU: voice synthesis, envelopes, and full audio path are still missing.
- CD-ROM: runtime command/data FIFOs, DMA transfer path, and baseline XA ReadN/ReadS streaming are implemented; timing fidelity, validation, and XA decode remain open.

## Testing & Validation (~42%)

- 19 unit tests now pass consistently, covering ISO parsing, fixture-based pipeline checks, codegen lowering, BIOS vector dispatch, runtime system initialization, entry-function merge, and GPU fill-rect PSX-SPX correctness.
- GPU parity groundwork now includes a versioned trace corpus + golden metadata format; however, there is still no automated golden-output emulator parity suite.
- No standardized catalog of larger real-world demo/game regression inputs in CI.
- End-to-end runtime playback validation remains mostly manual.

## Documentation & Dev Experience (~70%)

- Master roadmap, sub-roadmaps, implementation status, and BIOS functions roadmap are current.
- Agent collaboration guide (`agents.md`) provides onboarding context.
- More operator-facing troubleshooting docs are needed for malformed disc images and mixed-format edge cases.
