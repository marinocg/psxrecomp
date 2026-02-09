# PSXRecomp Master Roadmap

This roadmap consolidates all component roadmaps into a single, trackable plan from prototype to
full commercial game support. It references the detailed sub-roadmaps for each subsystem and
highlights dependencies between workstreams.

## How to use this roadmap
- Track completion at the **Milestones** level in this document.
- Use the linked **Workstreams** for detailed tasks and checklists.
- Update **Implementation Status** regularly as gaps close.

## Workstreams (detailed roadmaps)
- ISO/BIN parsing: `docs/architecture/iso_bin_roadmap.md`
- PSX-EXE loading: `docs/architecture/psx_exe_roadmap.md`
- Disassembler: `docs/architecture/mips_r3000_disassembler_roadmap.md`
- IR pipeline: `docs/architecture/ir_roadmap.md`
- Recompiler/codegen: `docs/architecture/recompiler_roadmap.md`
- Runtime library: `docs/architecture/runtime_library_roadmap.md`
- GPU emulation: `docs/architecture/gpu_emulation_roadmap.md`
- SPU emulation: `docs/architecture/spu_emulation_roadmap.md`
- Demo milestone: `docs/architecture/demo_roadmap.md`
- Full game support: `docs/architecture/full_game_support_roadmap.md`
- Pipeline overview: `docs/architecture/pipeline.md`

## Milestones

### M0: Project Baseline (Complete)
- ISO parser and PSX-EXE loader exist.
- Disassembler covers core integer MIPS, COP0 moves, COP2/GTE commands.
- Initial IR, CFG, SSA, verification passes implemented.
- Basic codegen can emit C++ stubs.

### M1: Deterministic Pipeline (In progress)
Goal: reliably go from ISO/EXE input to generated C++ artifacts without manual steps.

**Deliverables**
- End-to-end pipeline with artifact emission and diagnostics.
- Consistent module naming and output structure.
- Deterministic handling of multiple EXE candidates.
- Structured warnings/errors surfaced to CLI.

**Dependencies**
- ISO/BIN parser improvements (`iso_bin_roadmap.md`)
- PSX-EXE loader robustness (`psx_exe_roadmap.md`)

### M2: Functional MIPS→IR Coverage (In progress)
Goal: translate common game code paths into IR with correct control flow.

**Deliverables**
- Support for full integer ALU set, shifts, mult/div, branches, jumps, loads/stores.
- Correct delay slot handling for CFG and IR lowering.
- Function boundary detection and call graph discovery.
- IR modeling for system calls and BIOS stubs.

**Dependencies**
- Disassembler completeness (`mips_r3000_disassembler_roadmap.md`)
- IR pipeline expansion (`ir_roadmap.md`)

### M3: Correct Runtime Semantics (In progress)
Goal: make generated code interact with a faithful runtime model of PSX hardware.

**Deliverables**
- Accurate RAM, scratchpad, and MMIO behavior.
- DMA, interrupts, timers, and scheduler correctness.
- CD-ROM data path with XA streaming basics.
- GPU command packet ingestion and minimal rasterization.
- SPU voice and streaming scaffolding (even if simplified).

**Dependencies**
- Runtime library roadmap (`runtime_library_roadmap.md`)
- GPU/SPU roadmaps (`gpu_emulation_roadmap.md`, `spu_emulation_roadmap.md`)

### M4: Recompiled Demo Title (Planned)
Goal: run a controlled demo binary end-to-end with deterministic output.

**Deliverables**
- Known-good demo ROM/EXE pipeline build.
- Recompiled output builds and runs in CI.
- Test harness for frame/time-based validation.

**Dependencies**
- M2 functional IR coverage
- M3 runtime semantics for required subsystems
- Demo roadmap (`demo_roadmap.md`)

### M5: Early Game Compatibility (Planned)
Goal: basic compatibility with a subset of non-commercial or permissibly tested games.

**Deliverables**
- Expanded instruction coverage (including edge cases).
- Runtime correctness for GPU/SPU/CD-ROM interactions.
- Instrumentation for tracing and regression testing.

**Dependencies**
- M4 demo milestone
- Full-game roadmap (`full_game_support_roadmap.md`)

### M6: Full Commercial Game Support (Planned)
Goal: broad compatibility, performance, and tooling for real-world usage.

**Deliverables**
- Comprehensive CPU, GPU, SPU, and CD-ROM accuracy.
- Performance optimizations in recompiler and runtime.
- Compatibility matrix and user-facing tooling.

**Dependencies**
- All workstreams completed and validated.

## Risk and dependency highlights
- **Instruction coverage gaps** directly block M2 and downstream milestones.
- **MMIO accuracy** is critical for GPU/SPU/CD-ROM correctness in M3+.
- **Function boundary discovery** impacts CFG building and codegen quality.
- **Testing infrastructure** must scale before M5 to prevent regressions.

## Tracking recommendations
- Update `docs/architecture/implementation_status.md` after each milestone change.
- Add small, focused regression tests whenever new instruction/runtime behavior lands.
- Keep each workstream roadmap aligned to this milestone ordering.
