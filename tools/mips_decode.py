#!/usr/bin/env python3
"""Common MIPS I decode helpers used by analysis scripts."""

OPCODES = {
    0x00: "SPECIAL",
    0x01: "REGIMM",
    0x02: "J",
    0x03: "JAL",
    0x04: "BEQ",
    0x05: "BNE",
    0x06: "BLEZ",
    0x07: "BGTZ",
    0x08: "ADDI",
    0x09: "ADDIU",
    0x0A: "SLTI",
    0x0B: "SLTIU",
    0x0C: "ANDI",
    0x0D: "ORI",
    0x0E: "XORI",
    0x0F: "LUI",
    0x10: "COP0",
    0x11: "COP1",
    0x12: "COP2",
    0x13: "COP3",
    0x20: "LB",
    0x21: "LH",
    0x22: "LWL",
    0x23: "LW",
    0x24: "LBU",
    0x25: "LHU",
    0x26: "LWR",
    0x28: "SB",
    0x29: "SH",
    0x2A: "SWL",
    0x2B: "SW",
    0x2E: "SWR",
    0x30: "LWC0",
    0x31: "LWC1",
    0x32: "LWC2",
    0x38: "SWC0",
    0x39: "SWC1",
    0x3A: "SWC2",
}

SPECIAL_FUNCS = {
    0x00: "SLL",
    0x02: "SRL",
    0x03: "SRA",
    0x04: "SLLV",
    0x06: "SRLV",
    0x07: "SRAV",
    0x08: "JR",
    0x09: "JALR",
    0x0C: "SYSCALL",
    0x0D: "BREAK",
    0x10: "MFHI",
    0x11: "MTHI",
    0x12: "MFLO",
    0x13: "MTLO",
    0x18: "MULT",
    0x19: "MULTU",
    0x1A: "DIV",
    0x1B: "DIVU",
    0x20: "ADD",
    0x21: "ADDU",
    0x22: "SUB",
    0x23: "SUBU",
    0x24: "AND",
    0x25: "OR",
    0x26: "XOR",
    0x27: "NOR",
    0x2A: "SLT",
    0x2B: "SLTU",
}

REGIMM_FUNCS = {
    0x00: "BLTZ",
    0x01: "BGEZ",
    0x10: "BLTZAL",
    0x11: "BGEZAL",
}

REG_NAMES = [
    "zero",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra",
]


def decode_instr(word, pc):
    """Decode a single MIPS I instruction word into a human-readable string."""
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    sa = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    target = (word & 0x03FFFFFF) << 2 | (pc & 0xF0000000)

    if word == 0:
        return "NOP"

    if op == 0x00:
        name = SPECIAL_FUNCS.get(funct, f"SPECIAL_0x{funct:02X}")
        if funct in (0x00, 0x02, 0x03):
            return f"{name} ${REG_NAMES[rd]}, ${REG_NAMES[rt]}, {sa}"
        if funct == 0x08:
            return f"{name} ${REG_NAMES[rs]}"
        if funct == 0x09:
            return f"{name} ${REG_NAMES[rd]}, ${REG_NAMES[rs]}"
        if funct in (0x10, 0x12):
            return f"{name} ${REG_NAMES[rd]}"
        if funct in (0x11, 0x13):
            return f"{name} ${REG_NAMES[rs]}"
        if funct in (0x18, 0x19, 0x1A, 0x1B):
            return f"{name} ${REG_NAMES[rs]}, ${REG_NAMES[rt]}"
        return f"{name} ${REG_NAMES[rd]}, ${REG_NAMES[rs]}, ${REG_NAMES[rt]}"

    if op == 0x01:
        name = REGIMM_FUNCS.get(rt, f"REGIMM_0x{rt:02X}")
        branch_target = pc + 4 + (simm << 2)
        return f"{name} ${REG_NAMES[rs]}, 0x{branch_target:08X}"

    if op in (0x02, 0x03):
        name = OPCODES[op]
        return f"{name} 0x{target:08X}"

    if op in (0x04, 0x05):
        name = OPCODES[op]
        branch_target = pc + 4 + (simm << 2)
        if op == 0x04 and rs == 0 and rt == 0:
            return f"B 0x{branch_target:08X}"
        if op == 0x05 and rt == 0:
            return f"BNEZ ${REG_NAMES[rs]}, 0x{branch_target:08X}"
        if op == 0x04 and rt == 0:
            return f"BEQZ ${REG_NAMES[rs]}, 0x{branch_target:08X}"
        return f"{name} ${REG_NAMES[rs]}, ${REG_NAMES[rt]}, 0x{branch_target:08X}"

    if op in (0x06, 0x07):
        name = OPCODES[op]
        branch_target = pc + 4 + (simm << 2)
        return f"{name} ${REG_NAMES[rs]}, 0x{branch_target:08X}"

    if op in (0x08, 0x09, 0x0A, 0x0B):
        name = OPCODES[op]
        return f"{name} ${REG_NAMES[rt]}, ${REG_NAMES[rs]}, {simm}"

    if op in (0x0C, 0x0D, 0x0E):
        name = OPCODES[op]
        return f"{name} ${REG_NAMES[rt]}, ${REG_NAMES[rs]}, 0x{imm:04X}"

    if op == 0x0F:
        return f"LUI ${REG_NAMES[rt]}, 0x{imm:04X}"

    if op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26):
        name = OPCODES[op]
        return f"{name} ${REG_NAMES[rt]}, {simm}(${REG_NAMES[rs]})"

    if op in (0x28, 0x29, 0x2A, 0x2B, 0x2E):
        name = OPCODES[op]
        return f"{name} ${REG_NAMES[rt]}, {simm}(${REG_NAMES[rs]})"

    if op in (0x30, 0x31, 0x32, 0x38, 0x39, 0x3A):
        name = OPCODES.get(op, f"OP_0x{op:02X}")
        return f"{name} ${rt}, {simm}(${REG_NAMES[rs]})"

    if op in (0x10, 0x12):
        name = OPCODES.get(op, f"COP{op - 0x10}")
        if rs == 0x00:
            return f"MFC{op - 0x10} ${REG_NAMES[rt]}, ${rd}"
        if rs == 0x04:
            return f"MTC{op - 0x10} ${REG_NAMES[rt]}, ${rd}"
        if rs & 0x10:
            return f"COP{op - 0x10} 0x{word & 0x01FFFFFF:07X}"
        return f"{name} 0x{word:08X}"

    return f"??? 0x{word:08X}"
