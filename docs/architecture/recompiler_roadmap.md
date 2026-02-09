# Recompiler Roadmap

This roadmap tracks the current state of the C++ recompiler and the remaining work needed to
support end-to-end static recompilation.

## Current Status
- [x] IR-to-C++ lowering and emission pipeline.
- [x] Backend helpers for PSX memory accesses and intrinsic calls.

## Phase 1: Baseline Code Generation
- [x] Define a C++ emitter API with structured blocks and expressions.
- [x] Lower core IR opcodes (move, add/sub, bitwise, loads/stores) to C++.
- [x] Emit control flow for branches, jumps, and returns.
- [x] Emit function signatures and basic calling conventions.

## Phase 2: Runtime Integration
- [x] Hook memory accesses to runtime RAM and MMIO helpers.
- [x] Emit runtime calls for GPU/SPU/CD-ROM intrinsics.
- [x] Add support for global data and static tables.

## Phase 3: Optimization & Readability
- [x] Add peephole optimizations during emission.
- [x] Preserve control-flow structure for readable output.
- [x] Add debug metadata (source addresses, labels, comments).

## Phase 4: Validation & Testing
- [x] Add unit tests for IR-to-C++ lowering.
- [ ] Add end-to-end tests for small PSX-EXE samples.
- [x] Add compile-and-run checks for generated code.

## Phase 5: Packaging & Integration
- [ ] Emit resource manifest for non-code assets (textures/audio/movies).
- [ ] Add build rules to bundle non-code resources with generated output.
- [ ] Add compile-time logging and warnings summary for generated code.
- [ ] Provide build-time configuration hooks (optimizations, logging, checks).
