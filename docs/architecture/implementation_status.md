# Implementation Status Report

This report estimates current implementation coverage across major subsystems and lists
what is present vs. missing. Percentages are coarse estimates intended for planning.

## Overall completion (estimate)
- **Project-wide completion:** ~25%
- **End-to-end playable pipeline:** ~20%

## Subsystem status (estimate)

### Pipeline & Tooling (~45%)
**Present**
- Deterministic pipeline output layout with manifest emission.
- Stable EXE candidate selection with structured diagnostics.
- Multi-disc metadata surfaced in pipeline output and runtime hooks.

**Missing**
- Automated build/run of emitted C++ artifacts.
- CLI automation around compiling generated output in CI.

### ISO/BIN Parsing (~85%)
**Present**
- ISO 9660 parsing, track handling, file extraction, and path table lookups.
- PSX EXE discovery heuristics, resource export helpers, and sector caching.
- XA Mode 2 validation plus improved CUE parsing (sessions/pregaps).

**Missing**
- Broader mixed-mode validation beyond current XA checks.
- Additional malformed image recovery and diagnostics.

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
- Integration of analysis helpers into the pipeline/call graph builder.
- Broader decode coverage for edge-case encodings and validation in real binaries.

### IR Pipeline (~45%)
**Present**
- IR data structures, CFG builder, SSA conversion, verification utilities.
- Initial MIPS→IR translation for a subset of opcodes.

**Missing**
- Full instruction coverage, delay-slot semantics, memory-mapped IO modeling.
- Optimization passes and full SSA integration in pipeline output.

### Recompiler / Codegen (~35%)
**Present**
- Basic C++ emission for a small IR subset.
- Minimal runtime calls for load/store and intrinsics.

**Missing**
- ABI/calling conventions, register allocation hints, structured control flow output.
- Broader opcode lowering and optimized emission.

### Runtime Library (~30%)
**Present**
- Core PSX system scaffolding (memory, basic subsystems).

**Missing**
- Accurate DMA/interrupt/timer scheduling.
- Full MMIO map coverage, BIOS/syscall support.

### GPU Emulation (~10%)
**Present**
- Early scaffolding and documentation.

**Missing**
- Command decoding, VRAM semantics, rasterization correctness, timing.

### SPU Emulation (~5%)
**Present**
- Early scaffolding and documentation.

**Missing**
- Voice synthesis, envelopes, XA audio path, mixing/timing.

### CD-ROM (~15%)
**Present**
- Basic data path with ISO parser integration.

**Missing**
- XA streaming, command timing, sector validation, error conditions.

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
