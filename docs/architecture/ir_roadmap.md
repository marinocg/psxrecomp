# IR Implementation Roadmap

This document captures the staged plan for implementing the IR pipeline in psxrecomp. It builds
on the architecture pipeline stages and is intended to track near-term milestones.

## Current Status
- [x] Core IR data structures (Value, Instruction, BasicBlock, Function, Program, Builder).
- [x] Basic IR pretty-printing for instructions and blocks.
- [x] Control-flow construction, SSA, and verification passes.

## Phase 1: Control-Flow Analysis
- [ ] Build basic blocks from the disassembly stream.
- [ ] Resolve branch targets and fallthrough edges.
- [ ] Encode CFG edges and successor lists.
- [ ] Add function boundary detection heuristics (entry points, call targets, prologue patterns).

## Phase 2: IR Generation
- [ ] Lower MIPS instructions into IR opcodes (arithmetic, load/store, branches, calls, returns).
- [ ] Capture explicit register dataflow in IR values.
- [ ] Introduce SSA form with phi nodes at block joins.
- [ ] Model memory-mapped IO as intrinsic IR operations.

## Phase 3: IR Verification & Diagnostics
- [ ] Add SSA validation (dominance and phi placement checks).
- [ ] Add IR validation for operand count/kinds.
- [ ] Improve IR pretty-printing for debugging.

## Phase 4: Optimization Passes
- [ ] Constant folding and propagation.
- [ ] Dead code elimination.
- [ ] Common subexpression elimination.
- [ ] Loop-invariant code motion.

## Phase 5: Code Generation
- [ ] Lower IR to readable C++.
- [ ] Emit runtime calls for memory accesses and intrinsics.
- [ ] Preserve control-flow structure during emission.

## Phase 6: Runtime Integration
- [ ] Map IR intrinsics to runtime interfaces (GPU, SPU, CD-ROM, controllers).
- [ ] Maintain PSX memory model in generated code.
