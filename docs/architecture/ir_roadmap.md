# IR Implementation Roadmap

This document captures the staged plan for implementing the IR pipeline in psxrecomp. It builds
on the architecture pipeline stages and is intended to track near-term milestones.

## Current Status
- [x] Core IR data structures (Value, Instruction, BasicBlock, Function, Program, Builder).
- [x] Basic IR pretty-printing for instructions and blocks.
- [x] Control-flow construction with basic blocks, successor lists, and CFG diagnostics.
- [x] SSA conversion and verification (phi placement + dominance checks).
- [x] Function boundary detection heuristics (entry points, call targets, prologue patterns).
- [x] MIPS→IR lowering for arithmetic/logical ops, immediates, loads/stores, branches, jumps,
      calls, returns, shifts, mult/div, HI/LO moves, and syscalls.
- [x] Delay-slot semantics for non-nop delay-slot instructions.
- [x] Memory-mapped IO modeled as explicit IR intrinsics.

## Phase 1: Control-Flow Analysis
- [x] Build basic blocks from the disassembly stream.
- [x] Resolve branch targets and fallthrough edges.
- [x] Encode CFG edges and successor lists.
- [x] Add function boundary detection heuristics (entry points, call targets, prologue patterns).

## Phase 2: IR Generation
- [x] Lower arithmetic/logical instructions (add/sub/and/or/xor + immediates) into IR opcodes.
- [x] Lower load/store address computations and memory ops into IR.
- [x] Lower branches, jumps, calls, returns, and syscalls into IR control-flow ops.
- [x] Lower shift/rotate instructions (SLL/SRL/SRA/SLLV/SRLV/SRAV).
- [x] Lower mult/div instructions (MULT/MULTU/DIV/DIVU + HI/LO transfers).
- [x] Model memory-mapped IO as intrinsic IR operations.
- [x] Model non-nop delay-slot semantics in IR.

## Phase 3: IR Verification & Diagnostics
- [x] Add SSA validation (dominance and phi placement checks).
- [x] Add IR validation for operand count/kinds.
- [x] Improve IR pretty-printing for debugging.

## Phase 4: Optimization Passes
- [x] Constant folding and propagation.
- [x] Dead code elimination.
- [x] Common subexpression elimination.
- [x] Loop-invariant code motion.

## Phase 5: Code Generation
- [x] Lower IR to readable C++ for the supported opcode set.
- [x] Emit runtime calls for memory accesses and address-based intrinsics.
- [x] Preserve control-flow structure during emission.

## Phase 6: Runtime Integration
- [x] Map explicit IR intrinsics to runtime interfaces (GPU, SPU, CD-ROM, controllers).
- [x] Maintain PSX memory model in generated code with explicit MMIO reads/writes.
