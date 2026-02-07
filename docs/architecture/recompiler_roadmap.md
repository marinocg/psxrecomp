# Recompiler Roadmap

This roadmap tracks the current state of the C++ recompiler and the remaining work needed to
support end-to-end static recompilation.

## Current Status
- [ ] IR-to-C++ lowering and emission pipeline.
- [ ] Backend helpers for PSX memory accesses and intrinsic calls.

## Phase 1: Baseline Code Generation
- [ ] Define a C++ emitter API with structured blocks and expressions.
- [ ] Lower core IR opcodes (move, add/sub, bitwise, loads/stores) to C++.
- [ ] Emit control flow for branches, jumps, and returns.
- [ ] Emit function signatures and basic calling conventions.

## Phase 2: Runtime Integration
- [ ] Hook memory accesses to runtime RAM and MMIO helpers.
- [ ] Emit runtime calls for GPU/SPU/CD-ROM intrinsics.
- [ ] Add support for global data and static tables.

## Phase 3: Optimization & Readability
- [ ] Add peephole optimizations during emission.
- [ ] Preserve control-flow structure for readable output.
- [ ] Add debug metadata (source addresses, labels, comments).

## Phase 4: Validation & Testing
- [ ] Add unit tests for IR-to-C++ lowering.
- [ ] Add end-to-end tests for small PSX-EXE samples.
- [ ] Add compile-and-run checks for generated code.
