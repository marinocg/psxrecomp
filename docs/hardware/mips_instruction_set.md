# MIPS R3000 Instruction Set Quick Reference

Quick reference for MIPS R3000 instructions used in PSX games.

## Arithmetic Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| ADD        | R-type | rd = rs + rt (trap on overflow) | `add $t0, $t1, $t2` |
| ADDU       | R-type | rd = rs + rt (no trap) | `addu $t0, $t1, $t2` |
| ADDI       | I-type | rt = rs + imm (trap on overflow) | `addi $t0, $t1, 100` |
| ADDIU      | I-type | rt = rs + imm (no trap) | `addiu $sp, $sp, -8` |
| SUB        | R-type | rd = rs - rt (trap on overflow) | `sub $t0, $t1, $t2` |
| SUBU       | R-type | rd = rs - rt (no trap) | `subu $t0, $t1, $t2` |

## Logical Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| AND        | R-type | rd = rs & rt | `and $t0, $t1, $t2` |
| ANDI       | I-type | rt = rs & imm | `andi $t0, $t1, 0xFF` |
| OR         | R-type | rd = rs \| rt | `or $t0, $t1, $t2` |
| ORI        | I-type | rt = rs \| imm | `ori $t0, $t1, 0x1000` |
| XOR        | R-type | rd = rs ^ rt | `xor $t0, $t1, $t2` |
| XORI       | I-type | rt = rs ^ imm | `xori $t0, $t1, 1` |
| NOR        | R-type | rd = ~(rs \| rt) | `nor $t0, $t1, $t2` |

## Shift Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| SLL        | R-type | rd = rt << shamt | `sll $t0, $t1, 2` |
| SLLV       | R-type | rd = rt << rs | `sllv $t0, $t1, $t2` |
| SRL        | R-type | rd = rt >> shamt (logical) | `srl $t0, $t1, 2` |
| SRLV       | R-type | rd = rt >> rs (logical) | `srlv $t0, $t1, $t2` |
| SRA        | R-type | rd = rt >> shamt (arithmetic) | `sra $t0, $t1, 2` |
| SRAV       | R-type | rd = rt >> rs (arithmetic) | `srav $t0, $t1, $t2` |

## Load/Store Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| LB         | I-type | rt = MEM[rs+offset] (signed byte) | `lb $t0, 0($a0)` |
| LBU        | I-type | rt = MEM[rs+offset] (unsigned byte) | `lbu $t0, 0($a0)` |
| LH         | I-type | rt = MEM[rs+offset] (signed halfword) | `lh $t0, 0($a0)` |
| LHU        | I-type | rt = MEM[rs+offset] (unsigned halfword) | `lhu $t0, 0($a0)` |
| LW         | I-type | rt = MEM[rs+offset] (word) | `lw $t0, 0($a0)` |
| LWL        | I-type | Load word left (unaligned) | `lwl $t0, 0($a0)` |
| LWR        | I-type | Load word right (unaligned) | `lwr $t0, 3($a0)` |
| SB         | I-type | MEM[rs+offset] = rt (byte) | `sb $t0, 0($a0)` |
| SH         | I-type | MEM[rs+offset] = rt (halfword) | `sh $t0, 0($a0)` |
| SW         | I-type | MEM[rs+offset] = rt (word) | `sw $t0, 0($a0)` |
| SWL        | I-type | Store word left (unaligned) | `swl $t0, 0($a0)` |
| SWR        | I-type | Store word right (unaligned) | `swr $t0, 3($a0)` |

## Branch Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| BEQ        | I-type | if (rs == rt) branch | `beq $t0, $t1, label` |
| BNE        | I-type | if (rs != rt) branch | `bne $t0, $t1, label` |
| BLEZ       | I-type | if (rs <= 0) branch | `blez $t0, label` |
| BGTZ       | I-type | if (rs > 0) branch | `bgtz $t0, label` |
| BLTZ       | I-type | if (rs < 0) branch | `bltz $t0, label` |
| BGEZ       | I-type | if (rs >= 0) branch | `bgez $t0, label` |
| BLTZAL     | I-type | if (rs < 0) branch and link | `bltzal $t0, label` |
| BGEZAL     | I-type | if (rs >= 0) branch and link | `bgezal $t0, label` |

## Jump Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| J          | J-type | Jump to address | `j label` |
| JAL        | J-type | Jump and link (call) | `jal function` |
| JR         | R-type | Jump to register | `jr $ra` |
| JALR       | R-type | Jump to register and link | `jalr $t0` |

## Compare Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| SLT        | R-type | rd = (rs < rt) ? 1 : 0 (signed) | `slt $t0, $t1, $t2` |
| SLTU       | R-type | rd = (rs < rt) ? 1 : 0 (unsigned) | `sltu $t0, $t1, $t2` |
| SLTI       | I-type | rt = (rs < imm) ? 1 : 0 (signed) | `slti $t0, $t1, 100` |
| SLTIU      | I-type | rt = (rs < imm) ? 1 : 0 (unsigned) | `sltiu $t0, $t1, 100` |

## Load Immediate

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| LUI        | I-type | rt = imm << 16 | `lui $t0, 0x8001` |

Note: `li` (load immediate) is a pseudo-instruction that expands to `lui` + `ori`

## Multiply/Divide

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| MULT       | R-type | HI:LO = rs * rt (signed) | `mult $t0, $t1` |
| MULTU      | R-type | HI:LO = rs * rt (unsigned) | `multu $t0, $t1` |
| DIV        | R-type | LO = rs / rt; HI = rs % rt (signed) | `div $t0, $t1` |
| DIVU       | R-type | LO = rs / rt; HI = rs % rt (unsigned) | `divu $t0, $t1` |
| MFHI       | R-type | rd = HI | `mfhi $t0` |
| MTHI       | R-type | HI = rs | `mthi $t0` |
| MFLO       | R-type | rd = LO | `mflo $t0` |
| MTLO       | R-type | LO = rs | `mtlo $t0` |

## System Instructions

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| SYSCALL    | R-type | System call exception | `syscall` |
| BREAK      | R-type | Breakpoint exception | `break` |

## Coprocessor 0 (System Control)

| Instruction | Format | Operation | Example |
|------------|--------|-----------|---------|
| MFC0       | - | rt = COP0[rd] | `mfc0 $t0, $12` |
| MTC0       | - | COP0[rd] = rt | `mtc0 $t0, $12` |
| TLBP       | - | Probe TLB | `tlbp` |
| TLBR       | - | Read indexed TLB entry | `tlbr` |
| TLBWI      | - | Write indexed TLB entry | `tlbwi` |
| TLBWR      | - | Write random TLB entry | `tlbwr` |
| RFE        | - | Return from exception | `rfe` |

## Encoding Formats

### R-Type
```
[op: 6][rs: 5][rt: 5][rd: 5][shamt: 5][funct: 6]
```

### I-Type
```
[op: 6][rs: 5][rt: 5][immediate: 16]
```

### J-Type
```
[op: 6][target: 26]
```

## Important Notes

1. **Branch Delay Slot**: All branch/jump instructions execute the next instruction before branching
2. **Register $0**: Always contains zero, writes are ignored
3. **Unaligned Access**: Use LWL/LWR and SWL/SWR pairs for unaligned word access
4. **Overflow**: Only ADD, ADDI, SUB trap on overflow; U versions don't
5. **Multiply/Divide**: Results go to HI/LO registers, retrieved with MFHI/MFLO

## Disassembler Coverage

The current disassembler implementation focuses on the core R3000 integer instruction set plus
COP0 register moves and TLB/system ops (MFC0/MTC0/CFC0/CTC0, TLBP/TLBR/TLBWI/TLBWR, RFE) plus full COP2/GTE
command decoding (MFC2/MTC2/CFC2/CTC2,
GTE command mnemonics, and LWC2/SWC2). Common pseudo-instructions (`nop`, `move`, `li`) are emitted
where appropriate. Unknown or unsupported encodings are surfaced as `UNKNOWN` instructions so
downstream analysis can
differentiate unimplemented opcodes from valid decodes.

## Planned Instruction Coverage

The remaining MIPS R3000 coverage will be implemented in phases to keep the decoder maintainable:

1. **System/exception instructions**: TLB/cache ops (if needed for PSX), additional COP0 moves, and
   verified exception variants with proper formatting.
2. **Expand COP2/GTE formatting**: add operand detail/flag decoding for GTE commands and align output
   with canonical register naming.
3. **Validation suite growth**: expand opcode tables and add fixtures for unaligned access pairs,
   corner-case branch targets, and known PSX BIOS instruction sequences.
