# PSXRecomp Master Roadmap

This roadmap consolidates all component roadmaps into a single, trackable plan from prototype to
full commercial game support. It references the detailed sub-roadmaps for each subsystem and
highlights dependencies between workstreams.

## How to use this roadmap
- Track completion at the **Milestones** level in this document.
- Use the linked **Workstreams** for detailed tasks and checklists.
- Update **Implementation Status** regularly as gaps close.

## Workstreams (detailed roadmaps)
- [x] ISO/BIN parsing ([iso_bin_roadmap.md](iso_bin_roadmap.md))
- [x] PSX-EXE loading ([psx_exe_roadmap.md](psx_exe_roadmap.md))
- [x] Disassembler ([mips_r3000_disassembler_roadmap.md](mips_r3000_disassembler_roadmap.md))
- [ ] IR pipeline ([ir_roadmap.md](ir_roadmap.md))
- [ ] Recompiler/codegen ([recompiler_roadmap.md](recompiler_roadmap.md))
- [ ] Runtime library ([runtime_library_roadmap.md](runtime_library_roadmap.md))
- [ ] GPU emulation ([gpu_emulation_roadmap.md](gpu_emulation_roadmap.md))
- [ ] SPU emulation ([spu_emulation_roadmap.md](spu_emulation_roadmap.md))
- [ ] Demo milestone ([demo_roadmap.md](demo_roadmap.md))
- [ ] Full game support ([full_game_support_roadmap.md](full_game_support_roadmap.md))
- [ ] Pipeline overview ([pipeline.md](pipeline.md))

## Milestones

### M0: Project Baseline (Complete)
- [x] ISO parser and PSX-EXE loader exist.
- [x] Disassembler covers core integer MIPS, COP0 moves, COP2/GTE commands.
- [x] Initial IR, CFG, SSA, verification passes implemented.
- [x] Basic codegen can emit C++ stubs.

### M1: Deterministic Pipeline (Complete)
Goal: reliably go from ISO/EXE input to generated C++ artifacts without manual steps.

**Deliverables**
- [x] End-to-end pipeline with artifact emission and diagnostics.
- [x] Consistent module naming and output structure.
- [x] Deterministic handling of multiple EXE candidates.
- [x] Multi-disc awareness (disc set identification and swap metadata).
- [x] Wire multi-disc sets into runtime disc swap workflows.
- [x] Structured warnings/errors surfaced to CLI.

**Dependencies (complete these roadmaps)**
- [x] ISO/BIN parser improvements (`iso_bin_roadmap.md`)
- [x] PSX-EXE loader robustness (`psx_exe_roadmap.md`)
- [x] Disassembler analysis helpers (`mips_r3000_disassembler_roadmap.md`)

### M2: Functional MIPS→IR Coverage (In progress)
Goal: translate common game code paths into IR with correct control flow.

**Deliverables**
- [ ] Support for full integer ALU set, shifts, mult/div, branches, jumps, loads/stores.
- [ ] Correct delay slot handling for CFG and IR lowering.
- [ ] Function boundary detection and call graph discovery.
- [ ] IR modeling for system calls and BIOS stubs.

**Dependencies (complete these roadmaps)**
- [x] Disassembler completeness (`mips_r3000_disassembler_roadmap.md`)
- [ ] IR pipeline expansion (`ir_roadmap.md`)
- [ ] Recompiler baseline codegen (`recompiler_roadmap.md`)

Status note: IR and recompiler dependency roadmaps have been refreshed to reflect current
implementation coverage and remaining gaps for M2.

### M3: Correct Runtime Semantics (In progress)
Goal: make generated code interact with a faithful runtime model of PSX hardware.

**Deliverables**
- [ ] Accurate RAM, scratchpad, and MMIO behavior.
- [ ] DMA, interrupts, timers, and scheduler correctness.
- [ ] CD-ROM data path with XA streaming basics.
- [ ] GPU command packet ingestion and minimal rasterization.
- [ ] SPU voice and streaming scaffolding (even if simplified).
- [ ] Debug tooling: runtime logs, memory dumps, and performance overlays.

**Dependencies (complete these roadmaps)**
- [ ] Runtime library roadmap (`runtime_library_roadmap.md`)
- [ ] GPU emulation roadmap (`gpu_emulation_roadmap.md`)
- [ ] SPU emulation roadmap (`spu_emulation_roadmap.md`)

### M4: Recompiled Demo Title (Planned)
Goal: run a controlled demo binary end-to-end with deterministic output.

**Deliverables**
- [ ] Known-good demo ROM/EXE pipeline build.
- [ ] Recompiled output builds and runs in CI.
- [ ] Test harness for frame/time-based validation.

**Dependencies (complete these roadmaps)**
- [ ] M2 functional IR coverage (this milestone)
- [ ] M3 runtime semantics (this milestone)
- [ ] Demo roadmap (`demo_roadmap.md`)

### M5: Early Game Compatibility (Planned)
Goal: basic compatibility with a subset of non-commercial or permissibly tested games.

**Deliverables**
- [ ] Expanded instruction coverage (including edge cases).
- [ ] Runtime correctness for GPU/SPU/CD-ROM interactions.
- [ ] Instrumentation for tracing and regression testing.
- [ ] Optional game metadata registry for reproducible builds and compatibility tracking.

**Dependencies (complete these roadmaps)**
- [ ] M4 demo milestone (this milestone)
- [ ] Full game support roadmap (`full_game_support_roadmap.md`)

### M6: Full Commercial Game Support (Planned)
Goal: broad compatibility, performance, and tooling for real-world usage.

**Deliverables**
- [ ] Comprehensive CPU, GPU, SPU, and CD-ROM accuracy.
- [ ] Performance optimizations in recompiler and runtime.
- [ ] Compatibility matrix and user-facing tooling.

**Dependencies (complete these roadmaps)**
- [x] ISO/BIN parsing (`iso_bin_roadmap.md`)
- [x] PSX-EXE loading (`psx_exe_roadmap.md`)
- [x] Disassembler (`mips_r3000_disassembler_roadmap.md`)
- [ ] IR pipeline (`ir_roadmap.md`)
- [ ] Recompiler/codegen (`recompiler_roadmap.md`)
- [ ] Runtime library (`runtime_library_roadmap.md`)
- [ ] GPU emulation (`gpu_emulation_roadmap.md`)
- [ ] SPU emulation (`spu_emulation_roadmap.md`)
- [ ] Demo milestone (`demo_roadmap.md`)
- [ ] Full game support (`full_game_support_roadmap.md`)

## Risk and dependency highlights
- **Instruction coverage gaps** directly block M2 and downstream milestones.
- **MMIO accuracy** is critical for GPU/SPU/CD-ROM correctness in M3+.
- **Function boundary discovery** impacts CFG building and codegen quality.
- **Testing infrastructure** must scale before M5 to prevent regressions.

## Tracking recommendations
- Update `implementation_status.md` after each milestone change.
- Add small, focused regression tests whenever new instruction/runtime behavior lands.
- Keep each workstream roadmap aligned to this milestone ordering.
