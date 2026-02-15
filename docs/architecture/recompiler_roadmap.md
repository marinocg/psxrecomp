# Recompiler Roadmap

This roadmap tracks the current state of the C++ recompiler and the remaining work needed to
support end-to-end static recompilation.

## Current Status
- [x] IR-to-C++ lowering and emission pipeline with structured blocks.
- [x] Lowering for core IR ops (move/add/sub/bitwise/compare/load/store/branch/jump/call/return).
- [x] Lowering for expanded IR ops (shifts, mult/div, HI/LO moves, syscalls, MMIO intrinsics).
- [x] Phi-node lowering and SSA-aware temporaries in C++.
- [x] Backend helpers for PSX memory accesses and address-based intrinsic dispatch.
- [x] Peephole optimizations for zero-value arithmetic and redundant moves.
- [x] Source-address comments and label metadata in generated output.
- [x] Compile-time logging hooks and warnings summary in generated modules.

## Phase 1: Baseline Code Generation
- [x] Define a C++ emitter API with structured blocks and expressions.
- [x] Lower core IR opcodes (move, add/sub, bitwise, compare, loads/stores) to C++.
- [x] Emit control flow for branches, jumps, and returns.
- [x] Emit function signatures and basic calling conventions.
- [x] Lower PHI nodes and SSA values into concrete temporaries.
- [x] Support new IR opcodes as the IR pipeline expands (shifts, mult/div).

## Phase 2: Runtime Integration
- [x] Hook memory accesses to runtime RAM and MMIO helpers.
- [x] Emit address-based intrinsic dispatch for GPU/SPU/CD-ROM calls.
- [x] Add support for global data and static tables.
- [x] Introduce explicit intrinsic lowering once IR gains MMIO intrinsics.

## Phase 3: Optimization & Readability
- [x] Add peephole optimizations during emission.
- [x] Preserve control-flow structure for readable output.
- [x] Add debug metadata (source addresses, labels, comments).

## Phase 4: Validation & Testing
- [x] Add unit tests for IR-to-C++ lowering.
- [x] Add end-to-end tests for small PSX-EXE samples.
- [x] Add compile-and-run checks for generated code.

## Phase 5: Packaging & Integration
- [x] Emit resource manifest for non-code assets (textures/audio/movies).
- [x] Add build rules to bundle non-code resources with generated output.
- [x] Add compile-time logging and warnings summary for generated code.
- [x] Provide build-time configuration hooks (optimizations, logging, checks).

## Phase 6: Runtime Correctness & Debug Tooling
- [x] Fix block-external continuation dispatch to prevent early termination.
- [x] Add self-loop prevention in split-block lowering.
- [x] Emit initial register state (SP, GP, FP, RA) in generated runner `main()`.
- [x] Emit RAM init image so recompiled code starts with correct memory contents.
- [x] Support debug environment variables (`PSXRECOMP_MAX_STEPS`, `BREAK_PC`, `TRACE_MMIO`, `TRACE_CALLS`).
- [x] Refactor codegen into focused modules: `codegen.cpp`, `codegen_build.cpp`, `codegen_runner.cpp`.
- [x] Add SDL2 presenter support in generated CMakeLists.txt (`find_package(SDL2 QUIET)`).
- [ ] Add headless frame-capture mode for automated validation.
- [ ] Implement callee-saved register preservation across function calls.
