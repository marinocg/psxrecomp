# IR Implementation Roadmap

This document captures the staged plan for implementing the IR pipeline in psxrecomp. It builds
on the architecture pipeline stages and is intended to track near-term milestones.

## Current Status
- [x] Core IR data structures (Value, Instruction, BasicBlock, Function, Program, Builder).
- [x] Basic IR pretty-printing for instructions and blocks.
- [x] Control-flow construction with basic blocks, successor lists, and CFG diagnostics.
- [x] SSA conversion and verification (phi placement + dominance checks).
- [x] Initial MIPS→IR lowering for arithmetic/logical ops, immediates, loads/stores, branches,
      jumps, calls, and returns (nop-only delay slots).
- [ ] Function boundary detection heuristics (entry points, call targets, prologue patterns).
- [ ] Delay-slot semantics beyond nop (non-trivial delay-slot instructions).

## Phase 1: Control-Flow Analysis
- [x] Build basic blocks from the disassembly stream.
- [x] Resolve branch targets and fallthrough edges.
- [x] Encode CFG edges and successor lists.
- [ ] Add function boundary detection heuristics (entry points, call targets, prologue patterns).

## Phase 2: IR Generation
- [x] Lower arithmetic/logical instructions (add/sub/and/or/xor + immediates) into IR opcodes.
- [x] Lower load/store address computations and memory ops into IR.
- [x] Lower branches, jumps, calls, and returns into IR control-flow ops.
- [ ] Lower shift/rotate instructions (SLL/SRL/SRA/SLLV/SRLV/SRAV).
- [ ] Lower mult/div instructions (MULT/MULTU/DIV/DIVU + HI/LO transfers).
- [ ] Model memory-mapped IO as intrinsic IR operations.
- [ ] Model non-nop delay-slot semantics in IR.

## Phase 3: IR Verification & Diagnostics
- [x] Add SSA validation (dominance and phi placement checks).
- [x] Add IR validation for operand count/kinds.
- [x] Improve IR pretty-printing for debugging.

## Phase 4: Optimization Passes
- [ ] Constant folding and propagation.
- [ ] Dead code elimination.
- [ ] Common subexpression elimination.
- [ ] Loop-invariant code motion.

## Phase 5: Code Generation
- [x] Lower IR to readable C++ for the currently supported opcode set.
- [x] Emit runtime calls for memory accesses and address-based intrinsics.
- [x] Preserve control-flow structure during emission.

## Phase 6: Runtime Integration
- [ ] Map explicit IR intrinsics to runtime interfaces (GPU, SPU, CD-ROM, controllers).
- [ ] Maintain full PSX memory model in generated code (DMA/timers/interrupt-aware).
