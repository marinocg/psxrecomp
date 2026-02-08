# MIPS R3000 Disassembler Roadmap

This roadmap tracks the current decode coverage of the MIPS R3000 disassembler and the next steps
needed to support full PSX workloads.

## Current Status
- [x] Core R-type, I-type, and J-type instruction decoding for arithmetic, logic, load/store,
      branches, and jumps.
- [x] REGIMM branch decoding (BLTZ/BGEZ and link variants).
- [x] COP0 decode for move/control ops and basic TLB/RFE instructions.
- [x] COP2 (GTE) command decoding plus MFC2/MTC2/CFC2/CTC2, LWC2, SWC2.
- [x] Delay-slot tracking and basic target address calculation.
- [x] Assembly string rendering for supported opcodes.
- [ ] Coverage for remaining R3000 opcodes (e.g., unaligned cache ops, break variants).
- [ ] Pseudo-instruction formatting beyond simple move/li/nop cases.

## Phase 1: Opcode Coverage
- [ ] Add missing R3000 opcodes (e.g., LUI variants, traps, cache if applicable on PSX).
- [ ] Confirm COP0 instruction set coverage against PSX CPU docs.
- [ ] Add any remaining COP2/GTE command decodes and operand formatting.

## Phase 2: Analysis Helpers
- [ ] Annotate instructions with computed branch targets and delay-slot metadata in helpers.
- [ ] Add helpers for identifying load/store sizes and addressing modes.
- [ ] Add symbol/label formatting hooks for higher-level disassembly output.

## Phase 3: Validation & Testing
- [ ] Add unit tests for opcode decoding tables and edge cases.
- [ ] Add golden disassembly tests for representative PSX binaries.
- [ ] Add fuzz/invalid instruction tests to ensure UNKNOWN handling is stable.
