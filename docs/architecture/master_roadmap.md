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
- [x] IR pipeline ([ir_roadmap.md](ir_roadmap.md))
- [x] Recompiler/codegen ([recompiler_roadmap.md](recompiler_roadmap.md))
- [x] Opcode coverage tracking ([opcode_coverage_roadmap.md](opcode_coverage_roadmap.md))
- [x] Runtime library ([runtime_library_roadmap.md](runtime_library_roadmap.md))
- [x] BIOS function coverage ([bios_functions_roadmap.md](bios_functions_roadmap.md))
- [ ] GPU emulation ([gpu_emulation_roadmap.md](gpu_emulation_roadmap.md))
- [ ] SPU emulation ([spu_emulation_roadmap.md](spu_emulation_roadmap.md))
- [ ] CD-ROM emulation ([cdrom_emulation_roadmap.md](cdrom_emulation_roadmap.md))
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

### M2: Functional MIPS→IR Coverage (Dependency Gate Complete)

Goal: translate common game code paths into IR with correct control flow.

**Deliverables**

- [x] Support for full integer ALU set, shifts, mult/div, branches, jumps, loads/stores.
- [x] Correct delay slot handling for CFG and IR lowering.
- [x] Function boundary detection and call graph discovery.
- [x] IR modeling for system calls and BIOS stubs.

**Dependencies (complete these roadmaps)**

- [x] Disassembler completeness (`mips_r3000_disassembler_roadmap.md`)
- [x] IR pipeline expansion (`ir_roadmap.md`)
- [x] Recompiler baseline codegen (`recompiler_roadmap.md`)

Status note: M2 dependency gate is complete. Remaining instruction/coprocessor edge-case coverage is tracked under M5 compatibility hardening.

### M3: Correct Runtime Semantics (Dependency Gate Complete; Accuracy Ongoing)

Goal: make generated code interact with a faithful runtime model of PSX hardware.

**Deliverables**

- [x] Accurate RAM, scratchpad, and MMIO behavior.
- [x] DMA, interrupts, timers, and scheduler correctness.
- [x] CD-ROM data path with XA streaming basics.
- [x] GPU command packet ingestion and minimal rasterization.
- [x] SPU voice synthesis scaffolding, ADSR/mixing core, and backend audio hooks (simplified reference path).
- [x] Debug tooling: runtime logs, memory dumps, and performance overlays.
- [x] BIOS vector framework with 50 implemented functions across A0/B0/C0 tables.
- [x] Block-external continuation dispatch and self-loop prevention in codegen.
- [x] Initial register state emission (SP, GP, FP, RA) and RAM init image in generated runners.
- [x] Debug environment variables (`PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`, `PSXRECOMP_TRACE_BIOS`).
- [x] Codegen and runtime refactoring into focused modules (codegen_build, codegen_runner, psx_system_bios).

**Dependencies (complete these roadmaps)**

- [x] Runtime library roadmap (`runtime_library_roadmap.md`)
- [x] GPU emulation roadmap (`gpu_emulation_roadmap.md`) (M3 dependency gate satisfied via command ingestion + minimal rasterization; later hardware/parity phases remain for M5+).
- [x] SPU emulation roadmap (`spu_emulation_roadmap.md`) (M3 dependency gate satisfied via Phase 1-2 core/mixer/backend path; timing/XA/parity remain for later milestones).
- [x] CD-ROM emulation roadmap (`cdrom_emulation_roadmap.md`) (M3 dependency gate satisfied via baseline command/data/XA transport; timing/parity/decode remain for later milestones).

Status note: M3 dependency gates for runtime, GPU, SPU, and CD-ROM are now satisfied; remaining hardware-accuracy work tracks in M5+ roadmaps.

### M4: Recompiled Demo Title (In progress)

Goal: run a controlled demo binary end-to-end with deterministic output.

**Deliverables**

- [x] Known-good demo ROM/EXE pipeline build (current corpus: ADVHELLO, COP0TEST, GPUTEST, HELLOWLD, MEMTEST, CDBROWSE, CDCRC, CDXA, GTELAB).
- [x] CI recompile-demos workflow builds generated artifacts with SDL2 display support on all platforms.
- [x] COP0 exception-path demo (`COP0TEST`) passes functional criteria (`MFC0`/`MTC0`/`RFE`, syscall exception, resume path).
- [ ] Recompiled output runs to completion in CI.
- [x] Test harness for frame/render validation (summary report + screenshot artifacts per demo).

**Dependencies (complete these roadmaps)**

- [x] M2 functional IR coverage dependency gate.
- [x] M3 runtime semantics dependency gate.
- [ ] Demo roadmap (`demo_roadmap.md`)

### M5: Early Game Compatibility (Planned)

Goal: basic compatibility with a subset of non-commercial or permissibly tested games.

**Deliverables**

- [ ] Expanded instruction coverage (including edge cases).
- [ ] COP0 compatibility hardening (BEV vector selection, delay-slot exception parity, CFC0/CTC0 stubs, BC0F/BC0T support; `Status.IM`/`Cause.IP` gating is now wired).
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
- [x] Runtime library (`runtime_library_roadmap.md`)
- [ ] GPU emulation (`gpu_emulation_roadmap.md`)
- [ ] SPU emulation (`spu_emulation_roadmap.md`)
- [ ] Demo milestone (`demo_roadmap.md`)
- [ ] Full game support (`full_game_support_roadmap.md`)

## Risk and dependency highlights

- **Instruction coverage edge cases** remain a key risk for M4+ stability and M5 compatibility.
- **COP0 reset/boot-state and interrupt timing parity** remains a key risk for exact emulator-level numeric parity.
- **MMIO accuracy** is critical for GPU/SPU/CD-ROM correctness in M3+.
- **Function boundary discovery** impacts CFG building and codegen quality.
- **Testing infrastructure** must scale before M5 to prevent regressions.

## Tracking recommendations

- Update `implementation_status.md` after each milestone change.
- Add small, focused regression tests whenever new instruction/runtime behavior lands.
- Keep each workstream roadmap aligned to this milestone ordering.
