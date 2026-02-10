# Current Gaps Report

This document lists the known gaps between the current implementation and the expected
end-to-end static recompilation workflow. It complements the workstream roadmaps and the
master roadmap.

## Pipeline & Tooling
- Pipeline emits C++ artifacts and manifests but does not yet build/run them automatically.
- Structured diagnostics are available, but CLI build automation is still missing.
- No CI coverage for compiling generated artifacts end-to-end.

## Disassembly & Analysis
- Delay-slot semantics are not fully modeled in CFG and IR lowering.
- Disassembler analysis helpers exist but are not yet wired into pipeline CFG/IR stages.
- Jump table and code/data heuristics need validation against real binaries.

## IR & Optimization
- IR lowering covers only a subset of MIPS instructions.
- No explicit modeling of HI/LO registers, mult/div results, or coprocessor state.
- SSA construction exists but not fully integrated into the pipeline output.
- Optimization passes (constant folding, DCE, CSE, LICM) are still missing.

## Recompiler / Code Generation
- C++ emission handles a small IR subset and emits minimal runtime interactions.
- No ABI/calling convention model for PSX functions.
- No support for inlining, register allocation hints, or structured control flow.
- No support for address translation beyond simple masking.

## Runtime Library
- Incomplete MMIO map coverage; GPU/SPU/CD-ROM interfaces are skeletal.
- DMA, timers, and interrupts need correctness and scheduling accuracy.
- No BIOS emulation or syscall layer for common kernel services.
- Limited logging/tracing for behavior verification.

## GPU / SPU / CD-ROM
- GPU: missing command decoding, rasterization accuracy, and VRAM behavior.
- SPU: missing voice synthesis, envelopes, and XA audio path.
- CD-ROM: missing XA streaming, command timing, and data/sector validation.

## Testing & Validation
- No end-to-end recompiled demo tests or golden output validation.
- Coverage is limited to unit tests; no integration tests with ROM input.
- Lacks standardized test ROM catalog and regression workflows.

## Documentation & Dev Experience
- No unified milestone tracker (addressed in `master_roadmap.md`).
- Existing roadmaps are detailed but not yet mapped to a single timeline.
