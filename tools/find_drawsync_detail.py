#!/usr/bin/env python3
"""Detailed DrawSync disassembly — dump from 0x800108C8 (the real entry) through the function end."""
import struct, os, sys

REG_NAMES = [
    "zero","at","v0","v1","a0","a1","a2","a3",
    "t0","t1","t2","t3","t4","t5","t6","t7",
    "s0","s1","s2","s3","s4","s5","s6","s7",
    "t8","t9","k0","k1","gp","sp","fp","ra",
]

OPCODES = {
    0x00: "SPECIAL", 0x01: "REGIMM", 0x02: "J", 0x03: "JAL",
    0x04: "BEQ", 0x05: "BNE", 0x06: "BLEZ", 0x07: "BGTZ",
    0x08: "ADDI", 0x09: "ADDIU", 0x0A: "SLTI", 0x0B: "SLTIU",
    0x0C: "ANDI", 0x0D: "ORI", 0x0E: "XORI", 0x0F: "LUI",
    0x10: "COP0", 0x20: "LB", 0x21: "LH", 0x23: "LW",
    0x24: "LBU", 0x25: "LHU", 0x28: "SB", 0x29: "SH", 0x2B: "SW",
}

SPECIAL_FUNCS = {
    0x00: "SLL", 0x02: "SRL", 0x03: "SRA",
    0x08: "JR", 0x09: "JALR",
    0x20: "ADD", 0x21: "ADDU", 0x23: "SUBU",
    0x24: "AND", 0x25: "OR", 0x26: "XOR", 0x27: "NOR",
    0x2A: "SLT", 0x2B: "SLTU",
}

def decode(w, pc):
    op = (w >> 26) & 0x3F
    rs = (w >> 21) & 0x1F
    rt = (w >> 16) & 0x1F
    rd = (w >> 11) & 0x1F
    sa = (w >> 6) & 0x1F
    funct = w & 0x3F
    imm = w & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    target = (w & 0x03FFFFFF) << 2 | (pc & 0xF0000000)

    if w == 0: return "NOP"
    if op == 0:
        name = SPECIAL_FUNCS.get(funct, f"SPECIAL_{funct:#x}")
        if funct in (0,2,3): return f"{name} ${REG_NAMES[rd]}, ${REG_NAMES[rt]}, {sa}"
        if funct == 8: return f"JR ${REG_NAMES[rs]}"
        if funct == 9: return f"JALR ${REG_NAMES[rd]}, ${REG_NAMES[rs]}"
        return f"{name} ${REG_NAMES[rd]}, ${REG_NAMES[rs]}, ${REG_NAMES[rt]}"
    if op in (2,3): return f"{OPCODES[op]} 0x{target:08X}"
    if op == 4:
        bt = pc + 4 + (simm << 2)
        if rs == 0 and rt == 0: return f"B 0x{bt:08X}"
        if rt == 0: return f"BEQZ ${REG_NAMES[rs]}, 0x{bt:08X}"
        return f"BEQ ${REG_NAMES[rs]}, ${REG_NAMES[rt]}, 0x{bt:08X}"
    if op == 5:
        bt = pc + 4 + (simm << 2)
        if rt == 0: return f"BNEZ ${REG_NAMES[rs]}, 0x{bt:08X}"
        return f"BNE ${REG_NAMES[rs]}, ${REG_NAMES[rt]}, 0x{bt:08X}"
    if op in (6,7):
        bt = pc + 4 + (simm << 2)
        return f"{OPCODES[op]} ${REG_NAMES[rs]}, 0x{bt:08X}"
    if op in (8,9,0xA,0xB): return f"{OPCODES[op]} ${REG_NAMES[rt]}, ${REG_NAMES[rs]}, {simm}"
    if op in (0xC,0xD,0xE): return f"{OPCODES[op]} ${REG_NAMES[rt]}, ${REG_NAMES[rs]}, 0x{imm:04X}"
    if op == 0xF: return f"LUI ${REG_NAMES[rt]}, 0x{imm:04X}"
    if op in (0x20,0x21,0x23,0x24,0x25): return f"{OPCODES[op]} ${REG_NAMES[rt]}, {simm}(${REG_NAMES[rs]})"
    if op in (0x28,0x29,0x2B): return f"{OPCODES[op]} ${REG_NAMES[rt]}, {simm}(${REG_NAMES[rs]})"
    return f"??? 0x{w:08X}"

iso_path = "/psxrecomp/out/recompiled-demos-windows-latest/inputs/HELLOWLD.iso"
data = open(iso_path, "rb").read()
marker = data.find(b"PS-X EXE")
code = data[marker + 0x800:]
base = 0x80010000

# Dump from 0x800108C8 for 60 instructions to get the whole DrawSync function
start_addr = 0x800108C8
offset = start_addr - base
n = 60

print(f"DrawSync function disassembly starting at 0x{start_addr:08X}:")
print(f"{'='*72}")
print()

for i in range(n):
    pc = start_addr + i * 4
    w = struct.unpack_from("<I", code, offset + i * 4)[0]
    dis = decode(w, pc)
    
    # Annotate key instructions
    ann = ""
    if pc == 0x800108C8: ann = "  ; DrawSync entry: if (a0 != 0) goto mode1"
    if pc == 0x800108CC: ann = "  ; v1 = 0x100000 (1,048,576 = timeout counter)"
    if pc == 0x800108D0: ann = "  ; goto poll_loop"
    if pc == 0x800108D4: ann = "  ; a0 = 0x8001xxxx (high half of busy-byte addr)"
    if pc == 0x800108D8: ann = "  ; if counter == 0, exit poll loop"
    if pc == 0x800108E0: ann = "  ; ◄ POLL: load GPU busy byte from 0x800176AD"
    if pc == 0x800108E8: ann = "  ; mask to byte"
    if pc == 0x800108EC: ann = "  ; ◄ LOOP: if busy != 0, go back"
    if pc == 0x800108F0: ann = "  ; (delay slot) counter--"
    if pc == 0x800108F4: ann = "  ; final check of busy byte"
    if pc == 0x80010900: ann = "  ; if still busy, goto error/timeout path"
    if pc == 0x80010908: ann = "  ; load GPU status register base (0xBF801814)"
    if pc == 0x8001090C: ann = "  ; read GPU status register (GPUSTAT)"
    if pc == 0x80010910: ann = "  ; mask = 0x60000000 (DMA+CMD ready bits)"
    if pc == 0x80010914: ann = "  ; status & mask"
    if pc == 0x80010918: ann = "  ; if both bits clear, we're done"
    
    print(f"  0x{pc:08X}: {w:08X}  {dis}{ann}")

# Also compute the timeout counter value
print()
print(f"{'='*72}")
print(f"ANALYSIS:")
print(f"  LUI $v1, 0x0010 at 0x800108CC loads v1 = 0x00100000 = {0x100000}")
print(f"  This is the timeout counter: {0x100000} iterations = 1,048,576")
print(f"")
print(f"  GPU busy byte address: 0x800176AD")
print(f"    LUI $a0, 0x8001  →  a0 = 0x80010000")
print(f"    LBU $v0, 30381($a0)  →  0x80010000 + 0x76AD = 0x800176AD")
print(f"")
print(f"  DrawSync entry point: 0x800108C8")
print(f"  Polling loop: 0x800108D8..0x800108F0 (tight loop)")
print(f"  After loop exits, checks GPUSTAT register at 0x1F801814 (via KSEG1: 0xBF801814)")
