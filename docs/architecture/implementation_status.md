# Implementation Status Report

This report estimates current implementation coverage across major subsystems and lists
what is present vs. missing. Percentages are coarse estimates intended for planning.

## Overall completion (estimate)

- **Project-wide completion:** ~61%
- **End-to-end playable pipeline:** ~54%

## Subsystem status (estimate)

## Subsystem scorecard (estimate)

| Area                 | Estimated completion |
| -------------------- | -------------------: |
| Pipeline & Tooling   |                 ~64% |
| ISO/BIN Parsing      |                 ~85% |
| PSX-EXE Loader       |                 ~90% |
| Disassembler         |                 ~77% |
| IR Pipeline          |                 ~87% |
| Recompiler / Codegen |                 ~82% |
| Runtime Library      |                 ~77% |
| GPU Emulation        |                 ~76% |
| SPU Emulation        |                 ~45% |
| CD-ROM               |                 ~42% |

### Pipeline & Tooling (~64%)

**Present**

- Deterministic pipeline output layout with manifest emission.
- Stable EXE candidate selection with structured diagnostics.
- Multi-disc metadata surfaced in pipeline output and runtime hooks.
- Bundle output includes resources plus runtime source/include copies for standalone CMake builds.
- Core build now compiles reusable component libraries (`psxrecomp_iso`, `psxrecomp_disasm`,
  `psxrecomp_ir`, `psxrecomp_recompiler`, `psxrecomp_runtime`) once per build directory and links
  them into CLI/test targets to avoid repeated recompilation per unit-test executable.
- Fixture generator + validation scripts exist for malformed/good/rich ISO scenarios.
- CI recompile-demos workflow builds generated C++ artifacts with SDL2 presenter support on Linux, macOS, and Windows.
- Demo verification harness now emits per-demo render screenshots and a summary report (`out/recompiled-demos-ubuntu-latest/verification/reports/summary.csv`).

**Missing**

- Automated build/run of emitted C++ artifacts from the main CLI path.
- End-to-end deterministic golden-output comparison in CI.

### ISO/BIN Parsing (~85%)

**Present**

- ISO 9660 parsing, track handling, resilient sector-layout detection (2048/2336/2352/2448), file extraction, and path table lookups.
- PSX EXE discovery heuristics, resource export helpers, and sector caching.
- XA Mode 2 validation, robust raw-sector PVD probing (Mode 1 + Mode 2 offsets), plus improved CUE parsing (sessions/pregaps).
- XA resource discovery fallback for 2048-byte ISO images (extent-based when raw subheaders are unavailable).
- Recursive ISO tree enumeration now walks directory records from root (independent of path table), and pipeline output emits `resources/index/{disc_tree,disc_meta,resources_manifest}.json` plus policy-driven `resources/fs/` exports (minimal/smart/full), with streaming file export (no full-file buffering).
- Resource index now includes `resources/index/recomp_inputs.json` with boot executable mapping, exported `SYSTEM.CNF`, and metadata for the discovered executable set to support no-ISO recompilation reruns.
- Recompilation pipeline now accepts exported workspace directories as direct input (`.../resources`),
  loads executable bytes via `resources/index/recomp_inputs.json`, and carries forward
  `disc_tree/disc_meta` reference metadata without requiring the original ISO/BIN.
- Resource index now includes `resources/index/catalog.json` as a unified exported-file truth table with deterministic IDs, ISO extents, SHA-1 hashes, and conservative content sniffing (`psx_exe`, `tim`, `str`, `xa`/`xa_maybe`).
- Optional embedded TIM carving now scans bounded large container files (`.BIN/.DAT/...`) via
  ISO range reads and emits deterministic outputs under
  `resources/embedded/by_container/<containerId>/tim/`, with extracted-hit metadata merged into
  `resources/index/catalog.json`.
- Deterministic data-track export now emits `resources/disc/data_track.bin` plus
  `resources/disc/{disc_layout,disc_hashes}.json` (auto-selecting `2048` user sectors or
  `2352` raw sectors), enabling LBA-addressable disc-less workflows while preserving
  file-extent mapping and blob integrity metadata.

**Missing**

- Broader mixed-mode validation beyond current XA checks.
- Additional malformed image recovery and diagnostics for uncommon disc layouts.

### PSX-EXE Loader (~90%)

**Present**

- Header parsing, load address validation, memory image creation.
- Overlay-aware segment mapping, syscall metadata extraction, symbol export hooks.

**Missing**

- Additional diagnostics for edge cases and BIOS integration hooks.

### Disassembler (~77%)

**Present**

- Core integer instruction decoding, COP0 transfer/control decode (`MFC0`/`MTC0`/`CFC0`/`CTC0`/`RFE`), COP2/GTE mnemonics.
- Delay slot flagging and target resolution helpers.
- Function boundary discovery heuristics and indirect jump/jump table detection.
- Code-vs-data segmentation helpers for mixed sections.
- Entry-function fall-through merge: when the entry point lacks a control-flow terminator before the next prologue, the two regions are merged into a single function boundary.

**Missing**

- Broader decode coverage for edge-case encodings and validation in real binaries.

### IR Pipeline (~87%)

**Present**

- IR data structures, CFG builder, SSA conversion, and verification utilities.
- Function boundary detection and call graph discovery in the pipeline.
- MIPS→IR lowering for arithmetic/logical ops, shifts, mult/div, HI/LO moves, branches, jumps,
  calls, returns, syscalls, COP0 `MFC0`/`MTC0`/`RFE`, and MMIO intrinsics with non-nop delay slots, including link-register semantics for `JAL`/`JALR` and register-target `JR` lowering.
- BIOS JAL targets (addresses in the `A0`/`B0`/`C0` vector range) are now lowered to `CALL` IR ops instead of `SYSCALL`, routing them through `callBiosVector` for correct dispatch.
- Optimization passes (constant folding, DCE, CSE, LICM) integrated into the pipeline.

**Missing**

- Remaining COP0/COP branch lowering (`CFC0`/`CTC0`, `BC0F`/`BC0T`) and richer memory width semantics in backend lowering (byte/halfword/unaligned currently map to generic LOAD/STORE IR ops).

### Recompiler / Codegen (~82%)

**Present**

- Structured C++ emission for core IR ops with control flow and phi-node lowering.
- Runtime helpers for memory access, MMIO intrinsics, syscalls, and address-based dispatch.
- COP0 lowering now emits runtime calls for `mfc0`/`mtc0`/`rfe`, and syscall lowering routes non-BIOS syscall codes through COP0 exception entry + vector dispatch.
- Peephole optimizations, logging hooks, and debug metadata in generated output.
- End-to-end pipeline validation and compile-and-run checks in unit tests.
- Workflow artifact reporting for unsupported opcode warnings from recompiled demo JSON logs, including per-run trend snapshots and top-family prioritization.
- Block-external continuation dispatch ensuring cross-block control flow terminates correctly.
- Self-loop prevention in split-block lowering to avoid infinite loops in generated runners.
- Initial register state emission (SP, GP, FP, RA) in generated runner `main()`.
- RAM init image emission so recompiled code starts with the correct memory contents.
- Debug environment variables (`PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`) for runtime introspection.
- Runner catch block now emits GPU command count, framebuffer pixel count, VRAM word count, command trace, and optional PPM framebuffer dump (`PSXRECOMP_DUMP_FRAMEBUFFER`).
- Runner supports `PSXRECOMP_RENDER_DEBUG_OVERLAY=1` to composite the debug HUD into both live presentation and framebuffer dumps.
- Runner now applies `PSXRECOMP_LOG_LEVEL` at runtime (`debug`/`info`/`warn`/`error` or `0..3`) so logger filtering matches user configuration.
- Refactored codegen into focused modules: `codegen.cpp`, `codegen_build.cpp`, `codegen_runner.cpp`.

**Missing**

- Higher-level ABI conventions (stack, callee-saved handling), remaining COP0 branch/control transfer variants, and aggressive inlining heuristics.

### Runtime Library (~77%)

**Present**

- Core PSX system scaffolding (memory, basic subsystems).
- Minimal COP0 runtime device with `Status`/`Cause`/`EPC`/`BadVAddr` register backing, exception entry bookkeeping, and `RFE` mode restore behavior.
- COP0 interrupt wiring now mirrors IRQ-controller pending state into `Cause.IP2`, gates IRQ exception entry with `Status.IEc` + (`Status.IM` & `Cause.IP`), and preserves hardware IP bits when software writes `Cause` via `mtc0`.
- DMA interactions, interrupt signaling, and scheduler hooks wired through runtime flow.
- Structured runtime logging with per-category events and configurable verbosity.
- Debug overlay counters for frame timing, DMA transfers, and interrupt activity.
- Debug overlay text now includes FPS derived from the last frame cycle count.
- Diagnostic memory dump support for RAM, VRAM, and SPU RAM plus save-state serialization/checksum.
- Resource pack loader for runtime assets (textures/audio/movie payload containers).
- BIOS vector framework (`callBiosVector`) handling A0/B0/C0 vectors with 50 implemented functions (16 A0, 23 B0, 11 C0).
- Functional string/memory BIOS functions (strcmp, strcpy, memcpy, memset, bzero).
- GPU BIOS helpers (GPU_cw, GPU_cwp, GPU_init, GPU_sync) forwarding GP0/GP1 commands.
- Event management stubs (OpenEvent, CloseEvent, WaitEvent, TestEvent, EnableEvent, DisableEvent, DeliverEvent).
- Pad/controller initialization stubs (InitPad, StartPad, InitCard, StartCard).
- System initialization stubs for C0 vector (EnqueueTimerAndVblankIrqs, SysEnqIntRP, InstallExceptionHandlers, etc.).
- BIOS trace support via `PSXRECOMP_TRACE_BIOS` environment variable.
- Refactored runtime into focused modules: `psx_system.cpp` and `psx_system_bios.cpp`.
- COP0 state is serialized in save-state snapshots and restored on load.

**Missing**

- Cycle-exact timer edge behavior still needs hardware-trace validation.
- Precise exception-vector selection parity across all exception sources is still incomplete.
- Reset/boot-state parity with reference emulators/hardware (initial COP0 snapshots and timing alignment) is not yet finalized.
- Remaining BIOS function coverage (~168 functions still unimplemented); see [BIOS Functions Roadmap](bios_functions_roadmap.md).

### GPU Emulation (~76%)

**Present**

- GP0/GP1 packet decoding into structured command traces.
- Expanded GP0 packet sizing/decoding coverage across draw, transfer, environment, and misc command families.
- Complete GP1 control decode coverage for reset, DMA direction, display range/mode controls, and IRQ acknowledgment.
- Register/status handling with FIFO depth, malformed packet tracking, and timing-oriented status updates.
- Pixel-accurate Phase 3 software rasterizer paths are implemented for triangles/quads/lines/sprites with top-left edge rules, draw-area clipping, draw offsets, texture-window addressing, and deterministic primitive ordering.
- GP0(02h) Fill Rectangle now conforms to PSX-SPX hardware behavior: uses raw VRAM coordinates (no draw offset), bypasses draw-area clipping, and ignores mask-bit settings.
- Phase 2 VRAM transfer pipeline is implemented: CPU->VRAM payload state machine, VRAM->CPU readback sequencing, VRAM->VRAM blits, edge wrapping, and mask/packing behavior.
- Pluggable GPU renderer interface with software and semi-accurate backend scaffolding.
- Runtime backend switching and frame comparison helpers for validation workflows.
- Software renderer now covers texture sampling modes (4/8/16-bit), CLUT lookups, texture page selection, semi-transparency modes, mask-bit behavior, dithering toggles, and color modulation paths.
- Runtime GPU fixes now include DMA6 OTC ordering-table clear, correct GP0 packet lengths for key primitive families, sprite opcode coverage for `0x74-0x77`/`0x7C-0x7F` (SPRT_8/SPRT_16), per-command texture/clut state snapshots, and raw-VRAM-backed texture/CLUT sampling.
- Conformance unit coverage now exercises Phase 3 primitive/effect behavior including degenerate lines, quad decomposition, clip/offset rules, texturing, blending, and mask interactions.
- DMA direction-aware GPU ingestion plus GPU linked-list DMA path handling in runtime DMA transfers.
- Exhaustive decoder tests now cover valid and malformed GP0/GP1 command packet streams.
- Detailed phased execution plan with checkboxes for software accuracy + hardware parity delivery.
- Phase 0 spec lock is complete: behavior matrix, accuracy tiers, trace corpus, golden frame metadata schema, and unsupported behavior policy are now versioned.

**Missing**

- Hardware API-specific backend implementation (OpenGL/Vulkan/Metal).
- Capture replay tooling to execute the new trace corpus in automated parity tests (planned for Phase 7).
- Full display/timing synchronization semantics from Phase 4 are still pending (scanline cadence, throughput limits, IRQ timing).
- External capture-based parity gates integrated into CI.

### SPU Emulation (~45%)

**Present**

- M3 dependency gate coverage is in place for Phase 1-2 scope (core voice model, mixing path, backend hookup).
- SPU register map constants plus per-voice channel model/state tracking.
- Core ADSR envelope state machine with key-on/key-off transitions.
- SPU RAM transfer cursor with DMA read/write support and RAM dump integration.
- ADPCM block decode path with pitch stepping and loop/end flag handling.
- Stereo voice mixer, master volume controls, and basic feedback reverb ring.
- Pluggable audio backend interface with deterministic null/capture backend behavior.
- Unit coverage for register writes, DMA interactions, key lifecycle, decode/mix flow.

**Missing**

- XA/CD-ROM streaming path into SPU voice/mixer flow.
- Timing/IRQ accuracy model and hardware-verified envelope/reverb edge behavior.
- Golden audio capture parity tests against hardware traces.

### CD-ROM (~42%)

**Present**

- Command/parameter/response FIFOs are wired through MMIO with interrupt flag handling.
- DMA-readable data FIFO path now supports CD-ROM->RAM transfer semantics for streamed sectors.
- Basic XA streaming controls are implemented (Setmode XA bit + ReadN/ReadS cadence with payload pumping).

**Missing**

- XA-ADPCM decode handoff into SPU playback.
- Command timing state machine fidelity (seek latency, busy windows, retries).
- Sector/subheader validation and detailed error condition coverage.

## Recommended implementation order

1. **End-to-end demo execution** from generated runner (hang-free completion + CI run step).
2. **GPU timing/display synchronization** (scanline cadence, throughput limits, IRQ timing).
3. **SPU accuracy hardening** (timing/IRQ behavior and XA/CD-ROM decode handoff).
4. **CD-ROM fidelity** (command timing windows, retries, sector/subheader validation).
5. **BIOS coverage expansion** for frequently used runtime services (`printf`, thread/timer APIs, CD/memory-card ops).
6. **Automated parity validation** with capture replay and golden-output gates in CI.

## References

- Master roadmap: `docs/architecture/master_roadmap.md`
- Gap analysis: `docs/architecture/gaps_report.md`
