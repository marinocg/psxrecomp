# Recompiler Roadmap

This roadmap tracks the current state of the C++ recompiler and the remaining work needed to
support end-to-end static recompilation.

## Current Status
- [x] IR-to-C++ lowering and emission pipeline with structured blocks.
- [x] Lowering for core IR ops (move/add/sub/bitwise/compare/load/store/branch/jump/call/return).
- [x] Backend helpers for PSX memory accesses and address-based intrinsic dispatch.
- [x] Peephole optimizations for zero-value arithmetic and redundant moves.
- [x] Source-address comments and label metadata in generated output.
- [ ] Phi node lowering and SSA-aware temporaries in C++.
- [ ] Support for additional IR ops (shifts, mult/div, HI/LO semantics).

## Phase 1: Baseline Code Generation
- [x] Define a C++ emitter API with structured blocks and expressions.
- [x] Lower core IR opcodes (move, add/sub, bitwise, compare, loads/stores) to C++.
- [x] Emit control flow for branches, jumps, and returns.
- [x] Emit function signatures and basic calling conventions.
- [ ] Lower PHI nodes and SSA values into concrete temporaries.
- [ ] Support new IR opcodes as the IR pipeline expands (shifts, mult/div).

## Phase 2: Runtime Integration
- [x] Hook memory accesses to runtime RAM and MMIO helpers.
- [x] Emit address-based intrinsic dispatch for GPU/SPU/CD-ROM calls.
- [x] Add support for global data and static tables.
- [ ] Introduce explicit intrinsic lowering once IR gains MMIO intrinsics.

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
