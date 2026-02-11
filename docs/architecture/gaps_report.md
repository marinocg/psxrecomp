# Current Gaps Report

This document lists the known gaps between the current implementation and the expected
end-to-end static recompilation workflow. It complements the workstream roadmaps and the
master roadmap.

## Gap scorecard (estimate)
| Area | Estimate | Gap trend |
|---|---:|---|
| Pipeline & Tooling | ~55% | Improving |
| Disassembly & Analysis | ~75% | Improving |
| IR & Optimization | ~85% | Improving |
| Recompiler / Code Generation | ~75% | Improving |
| Runtime Library | ~60% | Moderate gap |
| GPU Emulation | ~45% | Significant gap |
| SPU Emulation | ~5% | Major gap |
| CD-ROM Runtime | ~15% | Major gap |
| Testing & Validation | ~35% | Significant gap |
| Documentation & Dev Experience | ~65% | Improving |

## Pipeline & Tooling (~55%)
- Pipeline now emits deterministic bundles (C++/CMake/manifest/resources/runtime copy), but it still does not auto-build/run generated outputs from the main CLI flow.
- Structured diagnostics are available for EXE and ISO paths, but richer remediation hints and per-stage timing telemetry are still limited.
- No CI job currently compiles generated artifacts from representative disc fixtures end-to-end.

## Disassembly & Analysis (~75%)
- Delay-slot semantics are still not fully modeled in all CFG/IR edge cases.
- Jump table and code/data heuristics need broader validation against real game binaries.
- Coprocessor-heavy and hand-written assembly patterns remain under-tested.

## IR & Optimization (~85%)
- IR lowering coverage is much broader, but not complete for all PSX instruction patterns and coprocessor behavior.
- Byte/halfword/unaligned memory semantics are still simplified in parts of lowering/codegen.
- Optimization passes exist, but there is no profile-guided or game-specific tuning layer yet.

## Recompiler / Code Generation (~75%)
- Generated C++ is now structured and buildable with bundled runtime code, but ABI/calling-convention fidelity is still incomplete for complex binaries.
- Inlining/regalloc-style hints and deeper code quality optimizations are limited.
- Address translation and MMIO behavior still rely on simplified assumptions in some paths.

## Runtime Library (~60%)
- MMIO coverage and runtime scaffolding improved, but many device-accurate edge cases are still missing.
- DMA/interrupt routing is present; cycle-accurate timing remains incomplete.
- BIOS/syscall coverage is partial.

## GPU / SPU / CD-ROM (GPU ~45% / SPU ~5% / CD-ROM ~15%)
- GPU: command/raster accuracy and VRAM behavior are incomplete.
- SPU: voice synthesis, envelopes, and full audio path are still missing.
- CD-ROM: XA resource extraction works for raw and 2048-byte ISO workflows, but runtime XA streaming/decoding/timing is still incomplete.

## Testing & Validation (~35%)
- Strong unit coverage exists (including fixture-based ISO/pipeline checks), but there is still no golden-output emulator parity suite.
- No standardized catalog of larger real-world demo/game regression inputs in CI.
- End-to-end runtime playback validation remains mostly manual.

## Documentation & Dev Experience (~65%)
- Master roadmap exists, but milestone burn-down and release criteria are still lightweight.
- More operator-facing troubleshooting docs are needed for malformed disc images and mixed-format edge cases.
