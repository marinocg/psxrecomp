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
| Runtime Library      |                 ~81% |
| GPU Emulation        |                 ~76% |
| SPU Emulation        |                 ~45% |
| CD-ROM               |                 ~48% |

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
- Callback/indirect-call target harvesting now distinguishes likely function entries from data pointers, allowing IRQ/draw callback paths (including `gtelab_auto`) to recompile without promoting adjacent `.rodata` blobs into code.
- Entry-function fall-through merge: when the entry point lacks a control-flow terminator before the next prologue, the two regions are merged into a single function boundary.
- Focused GTE validation demo (`examples/demos/gtelab_auto`) now exercises COP2 transfer/control and transform/lighting instruction mixes (`MTC2/MFC2`, `CTC2/CFC2`, `LWC2/SWC2`, `RTPS/RTPT`, `NCLIP`, `AVSZ3/AVSZ4`, `MVMVA`, and the lighting/color family through shaded primitive output).

**Missing**

- Broader decode coverage for edge-case encodings and validation in real binaries.

### IR Pipeline (~87%)

**Present**

- IR data structures, CFG builder, SSA conversion, and verification utilities.
- Function boundary detection and call graph discovery in the pipeline.
- MIPS→IR lowering for arithmetic/logical ops, shifts, mult/div, HI/LO moves, branches, jumps,
  calls, returns, syscalls, COP0 `MFC0`/`MTC0`/`RFE`, and MMIO intrinsics with non-nop delay slots, including link-register semantics for `JAL`/`JALR` and register-target `JR` lowering.
- MIPS→IR lowering now covers COP2 register transfers (`MFC2`/`MTC2`/`CFC2`/`CTC2`), memory-backed GTE data-register transfers (`LWC2`/`SWC2`), and generic GTE command execution (`GTE_EXEC`) using the raw 32-bit instruction encoding.
- BIOS JAL targets (addresses in the `A0`/`B0`/`C0` vector range) are now lowered to `CALL` IR ops instead of `SYSCALL`, routing them through `callBiosVector` for correct dispatch.
- Optimization passes (constant folding, DCE, CSE, LICM) integrated into the pipeline.

**Missing**

- Remaining COP0/COP branch lowering (`CFC0`/`CTC0`, `BC0F`/`BC0T`) and richer memory width semantics in backend lowering (byte/halfword/unaligned currently map to generic LOAD/STORE IR ops).

### Recompiler / Codegen (~82%)

**Present**

- Structured C++ emission for core IR ops with control flow and phi-node lowering.
- Runtime helpers for memory access, MMIO intrinsics, syscalls, and address-based dispatch.
- COP0 lowering now emits runtime calls for `mfc0`/`mtc0`/`rfe`, and syscall lowering routes non-BIOS syscall codes through COP0 exception entry + vector dispatch.
- COP2 transfer lowering now emits guarded runtime GTE calls (`gte().mfc2`/`mtc2`/`cfc2`/`ctc2`), dedicated guarded memory-backed data-register transfers for `LWC2`/`SWC2`, and generic guarded `gte().exec(rawEncoding)` lowering for decoded GTE command opcodes.
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
- Runner now mounts the exported runtime disc blob from `resources/disc/data_track.bin` (using `resources/disc/disc_layout.json` for `2048` vs `2352` sector layout), with `PSXRECOMP_DISC_IMAGE` as an explicit override, and reports successful mounts in stdout.
- Refactored codegen into focused modules: `codegen.cpp`, `codegen_build.cpp`, `codegen_runner.cpp`.

**Missing**

- Higher-level ABI conventions (stack, callee-saved handling), remaining COP0 branch/control transfer variants, aggressive inlining heuristics, and edge-case GTE accuracy work beyond the current transform/lighting coverage (for example, more exhaustive FLAG corner cases and rare register timing quirks).

### Runtime Library (~77%)

**Present**

- Core PSX system scaffolding (memory, basic subsystems).
- Minimal COP0 runtime device with `Status`/`Cause`/`EPC`/`BadVAddr` register backing, exception entry bookkeeping, and `RFE` mode restore behavior.
- Runtime GTE device with separate COP2 data/control register banks, real screen/depth/RGB FIFO movement, raw-command execution decode, transform/depth command subset (`RTPS`/`RTPT`/`NCLIP`/`AVSZ3`/`AVSZ4`/`MVMVA`), lighting/color command family (`DPCS`, `INTPL`, `NCDS`, `CDP`, `NCDT`, `NCCS`, `CC`, `NCS`, `NCT`, `DCPL`, `DPCT`, `GPF`, `GPL`, `NCCT`), and busy-cycle tracking wired through `PsxSystem`.
- GTE timing fidelity now includes command-cycle countdown, CPU stall on COP2 reads / next command while busy, no stall on COP2 writes, delayed IRGB/ORGB visibility, LZCS/LZCR register behavior, and save-state serialization/restoration of in-flight GTE state.
- COP0 now exposes `cop2Enabled()` (`Status.CU2`) so generated COP2 register accesses can trap correctly when the GTE is disabled.
- COP0 interrupt wiring now mirrors IRQ-controller pending state into `Cause.IP2`, preserves hardware IP bits when software writes `Cause` via `mtc0`, and gates IRQ delivery/exception entry with `Status.IEc` + `Status.IM2` (plus runtime callback/critical-section guards).
- IRQ delivery now restores COP0 `Status` via a guaranteed `serviceInterrupts()` epilogue (`rfe`) instead of relying on BIOS `B0:17` to manage COP0 state.
- The generated callback bridge now commits `HookEntryInt` longjmp-style resumes instead of restoring the pre-callback snapshot, so `v0=1`, `ra`, `sp`, `fp`, `gp`, and `s0..s7` survive `ReturnFromException` back into the resumed context.
- Boot now seeds minimal COP0 Status defaults for BIOS-style IRQ flow (`IEc=1`, `IM2=1`, `KUc=0`) before entering recompiled code.
- DMA interactions, interrupt signaling, and scheduler hooks wired through runtime flow.
- Structured runtime logging with per-category events and configurable verbosity.
- Debug overlay counters for frame timing, DMA transfers, and interrupt activity.
- Debug overlay text now includes FPS derived from the last frame cycle count.
- Diagnostic memory dump support for RAM, VRAM, and SPU RAM plus save-state serialization/checksum, now including persisted in-flight GTE timing/register state.
- Resource pack loader for runtime assets (textures/audio/movie payload containers).
- BIOS vector framework (`callBiosVector`) handling A0/B0/C0 vectors with 71 implemented functions (30 A0, 30 B0, 11 C0).
- Functional string/memory BIOS functions (`strcmp`, `strncmp`, `strcpy`, `strlen`, `bcopy`, `memcpy`, `memset`, `bzero`) plus common `printf` formatting support.
- GPU BIOS helpers (GPU_cw, GPU_cwp, GPU_init, GPU_sync) forwarding GP0/GP1 commands.
- Functional kernel event handling (`OpenEvent`, `CloseEvent`, `WaitEvent`, `TestEvent`, `EnableEvent`, `DisableEvent`, `DeliverEvent`, `UnDeliverEvent`) with blocking `WaitEvent` support for `NoCallback` events.
- Pad/controller initialization stubs (InitPad, StartPad, InitCard, StartCard).
- System initialization stubs for C0 vector (EnqueueTimerAndVblankIrqs, SysEnqIntRP, InstallExceptionHandlers, etc.).
- Read-only BIOS file/device layer backed by mounted-disc ISO 9660 traversal (`FileOpen`, `FileSeek`, `FileRead`, `FileClose`, `firstfile`, `nextfile`).
- BIOS-facing CD-ROM helper coverage for `CdInit`, `CdRemove`, `CdAsyncSeekL`, `CdAsyncGetStatus`, `CdAsyncReadSector`, `CdAsyncSetMode`, and `CdInitSubFunc`.
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
- CD-ROM MMIO now routes through index-selected register banking (`1F801800h` index selector + banked `+1..+3` ports), matching PSX register layout.
- CD-ROM IRQs now use queued delivery: current IRQ type stays visible until ACK, then next queued IRQ promotes.
- CD-ROM boot-critical command set now includes `Getstat`, `Setloc`, `SeekL`, `ReadN/ReadS`, `Pause/Stop`, `Setmode`, `GetlocL/GetlocP`, `GetTN/GetTD`, and `GetID` with command-specific response byte shapes.
- CD-ROM read pipeline now uses Setloc MSF->LBA (with 00:02:00 pregap handling), sector cadence timing, disc-backed sequential reads, and RDDATA overread padding behavior for 0x800/0x924 sector modes.
- CD-ROM->DMA handshake path now refills read sectors during DMA underflow in active reads, reducing channel-3 stalls in DMA-only polling loops while preserving sector word boundaries.
- DMA-readable data FIFO path now supports CD-ROM->RAM transfer semantics for streamed sectors.
- Save-state now preserves CD-ROM internal runtime state (command/response/data FIFOs, active/pending IRQ state, LBA/mode execution fields, and partially-consumed sector buffering) to avoid post-load desync.
- XA streaming now validates Mode2/Form2 subheaders, exposes XA payload bytes from raw sectors, and applies Setfilter file/channel matching for XA ADPCM-shaped sectors.
- XA ADPCM sectors now decode to PCM and feed a CD-audio mixer input in SPU via a bounded ring buffer.
- Disc swap/lid behavior now models a shell-open status transition on disc changes and returns INT5 errors for no-disc and read-failure conditions.
- BIOS A0 CD helpers now layer on top of the runtime CD-ROM device and kernel events so demos can drive reads through BIOS calls instead of treating them as unconditional failure paths.
- Demo validation note: `CDBROWSE` behavior is verified against `examples/demos/cdbrowse/CDBROWSE.iso` (SHA-1 `d7b4a16d74ea3dc1ee44cf377842abe3bb006df6`); older artifact inputs under `out/recompiled-demos-ubuntu-latest/inputs/CDBROWSE.iso` (SHA-1 `9ff0fd85cbd8f94e927601df9531357acb61a6c1`) are a different binary and can freeze in non-representative paths.

**Missing**

- Command timing state machine fidelity (seek latency, busy windows, retries).
- Detailed error condition coverage for malformed/unsupported sector states.

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
