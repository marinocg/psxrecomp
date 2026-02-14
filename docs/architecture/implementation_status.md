# Implementation Status Report

This report estimates current implementation coverage across major subsystems and lists
what is present vs. missing. Percentages are coarse estimates intended for planning.

## Overall completion (estimate)
- **Project-wide completion:** ~52%
- **End-to-end playable pipeline:** ~40%

## Subsystem status (estimate)

## Subsystem scorecard (estimate)
| Area | Estimated completion |
|---|---:|
| Pipeline & Tooling | ~55% |
| ISO/BIN Parsing | ~85% |
| PSX-EXE Loader | ~90% |
| Disassembler | ~75% |
| IR Pipeline | ~85% |
| Recompiler / Codegen | ~75% |
| Runtime Library | ~68% |
| GPU Emulation | ~74% |
| SPU Emulation | ~45% |
| CD-ROM | ~42% |

### Pipeline & Tooling (~55%)
**Present**
- Deterministic pipeline output layout with manifest emission.
- Stable EXE candidate selection with structured diagnostics.
- Multi-disc metadata surfaced in pipeline output and runtime hooks.
- Bundle output includes resources plus runtime source/include copies for standalone CMake builds.
- Fixture generator + validation scripts exist for malformed/good/rich ISO scenarios.

**Missing**
- Automated build/run of emitted C++ artifacts from the main CLI path.
- CI automation around compiling generated output from representative fixture sets.

### ISO/BIN Parsing (~85%)
**Present**
- ISO 9660 parsing, track handling, resilient sector-layout detection (2048/2336/2352/2448), file extraction, and path table lookups.
- PSX EXE discovery heuristics, resource export helpers, and sector caching.
- XA Mode 2 validation, robust raw-sector PVD probing (Mode 1 + Mode 2 offsets), plus improved CUE parsing (sessions/pregaps).
- XA resource discovery fallback for 2048-byte ISO images (extent-based when raw subheaders are unavailable).

**Missing**
- Broader mixed-mode validation beyond current XA checks.
- Additional malformed image recovery and diagnostics for uncommon disc layouts.

### PSX-EXE Loader (~90%)
**Present**
- Header parsing, load address validation, memory image creation.
- Overlay-aware segment mapping, syscall metadata extraction, symbol export hooks.

**Missing**
- Additional diagnostics for edge cases and BIOS integration hooks.

### Disassembler (~75%)
**Present**
- Core integer instruction decoding, COP0 moves, COP2/GTE mnemonics.
- Delay slot flagging and target resolution helpers.
- Function boundary discovery heuristics and indirect jump/jump table detection.
- Code-vs-data segmentation helpers for mixed sections.

**Missing**
- Broader decode coverage for edge-case encodings and validation in real binaries.

### IR Pipeline (~85%)
**Present**
- IR data structures, CFG builder, SSA conversion, and verification utilities.
- Function boundary detection and call graph discovery in the pipeline.
- MIPS→IR lowering for arithmetic/logical ops, shifts, mult/div, HI/LO moves, branches, jumps,
  calls, returns, syscalls, and MMIO intrinsics with non-nop delay slots.
- Optimization passes (constant folding, DCE, CSE, LICM) integrated into the pipeline.

**Missing**
- Coprocessor-specific IR modeling and richer memory width semantics in backend lowering (byte/halfword/unaligned currently map to generic LOAD/STORE IR ops).

### Recompiler / Codegen (~75%)
**Present**
- Structured C++ emission for core IR ops with control flow and phi-node lowering.
- Runtime helpers for memory access, MMIO intrinsics, syscalls, and address-based dispatch.
- Peephole optimizations, logging hooks, and debug metadata in generated output.
- End-to-end pipeline validation and compile-and-run checks in unit tests.
- Workflow artifact reporting for unsupported opcode warnings from recompiled demo JSON logs.

**Missing**
- Higher-level ABI conventions (stack, callee-saved handling) and aggressive inlining heuristics.

### Runtime Library (~68%)
**Present**
- Core PSX system scaffolding (memory, basic subsystems).
- DMA interactions, interrupt signaling, and scheduler hooks wired through runtime flow.
- Structured runtime logging with per-category events and configurable verbosity.
- Debug overlay counters for frame timing, DMA transfers, and interrupt activity.
- Diagnostic memory dump support for RAM, VRAM, and SPU RAM plus save-state serialization/checksum.
- Resource pack loader for runtime assets (textures/audio/movie payload containers).

**Missing**
- Cycle-exact timer edge behavior still needs hardware-trace validation.
- Broader BIOS function coverage and return-value semantics.

### GPU Emulation (~74%)
**Present**
- GP0/GP1 packet decoding into structured command traces.
- Expanded GP0 packet sizing/decoding coverage across draw, transfer, environment, and misc command families.
- Complete GP1 control decode coverage for reset, DMA direction, display range/mode controls, and IRQ acknowledgment.
- Register/status handling with FIFO depth, malformed packet tracking, and timing-oriented status updates.
- Pixel-accurate Phase 3 software rasterizer paths are implemented for triangles/quads/lines/sprites with top-left edge rules, draw-area clipping, draw offsets, texture-window addressing, and deterministic primitive ordering.
- Phase 2 VRAM transfer pipeline is implemented: CPU->VRAM payload state machine, VRAM->CPU readback sequencing, VRAM->VRAM blits, edge wrapping, and mask/packing behavior.
- Pluggable GPU renderer interface with software and semi-accurate backend scaffolding.
- Runtime backend switching and frame comparison helpers for validation workflows.
- Software renderer now covers texture sampling modes (4/8/16-bit), CLUT lookups, texture page selection, semi-transparency modes, mask-bit behavior, dithering toggles, and color modulation paths.
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
1. **Expand MIPS→IR coverage** (ALU, shifts, mult/div, load/store variants, branches).
2. **Function boundary detection + call graph** to stabilize CFG generation.
3. **Delay-slot semantics and control-flow accuracy** across IR and codegen.
4. **Runtime MMIO correctness** (DMA, interrupts, timers, CD-ROM basics).
5. **GPU command ingestion and minimal rasterization path**.
6. **SPU voice + XA audio support** to unblock audio-heavy titles.
7. **Recompiler optimizations and tooling** (profiling, inlining, caching).
8. **End-to-end demo validation** with deterministic regression tests.

## References
- Master roadmap: `docs/architecture/master_roadmap.md`
- Gap analysis: `docs/architecture/gaps_report.md`
