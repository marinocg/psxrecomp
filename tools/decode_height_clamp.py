#!/usr/bin/env python3
"""Decode the height-clamping part of func_0x800108E8 in detail."""
import struct

with open('out/recompiled-demos-windows-latest/inputs/GPUTEST.iso', 'rb') as f:
    data = f.read()

psx_offset = data.find(b'PS-X EXE')
sector_num = psx_offset // 2352
file_size = struct.unpack_from('<I', data, sector_num * 2352 + 24 + 0x1C)[0]

text_data = bytearray()
text_sector = sector_num + 1
bytes_remaining = file_size
while bytes_remaining > 0:
    sec_start = text_sector * 2352
    chunk_size = min(2048, bytes_remaining)
    text_data.extend(data[sec_start + 24 : sec_start + 24 + chunk_size])
    bytes_remaining -= chunk_size
    text_sector += 1

base = 0x80010000

def decode(word, addr):
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    sa = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000

    if word == 0: return "NOP"
    if op == 0:
        if funct == 0x21: return f"ADDU r{rd}, r{rs}, r{rt}"
        if funct == 0x25: return f"OR r{rd}, r{rs}, r{rt}"
        if funct == 0x24: return f"AND r{rd}, r{rs}, r{rt}"
        if funct == 0x23: return f"SUBU r{rd}, r{rs}, r{rt}"
        if funct == 8: return f"JR r{rs}"
        if funct == 9: return f"JALR r{rd}, r{rs}"
        if funct == 0: return f"SLL r{rd}, r{rt}, {sa}"
        if funct == 2: return f"SRL r{rd}, r{rt}, {sa}"
        if funct == 3: return f"SRA r{rd}, r{rt}, {sa}"
        if funct == 0x2A: return f"SLT r{rd}, r{rs}, r{rt}"
        if funct == 0x2B: return f"SLTU r{rd}, r{rs}, r{rt}"
        return f"SPECIAL funct={funct:#x} (0x{word:08X})"
    if op == 2: return f"J 0x{(addr&0xF0000000)|((word&0x3FFFFFF)<<2):08X}"
    if op == 3: return f"JAL 0x{(addr&0xF0000000)|((word&0x3FFFFFF)<<2):08X}"
    if op == 4: return f"BEQ r{rs}, r{rt}, 0x{addr+4+(simm<<2):08X}"
    if op == 5: return f"BNE r{rs}, r{rt}, 0x{addr+4+(simm<<2):08X}"
    if op == 6: return f"BLEZ r{rs}, 0x{addr+4+(simm<<2):08X}"
    if op == 7: return f"BGTZ r{rs}, 0x{addr+4+(simm<<2):08X}"
    if op == 9: return f"ADDIU r{rt}, r{rs}, {simm} (0x{imm:04X})"
    if op == 0xA: return f"SLTI r{rt}, r{rs}, {simm}"
    if op == 0xB: return f"SLTIU r{rt}, r{rs}, {simm}"
    if op == 0xC: return f"ANDI r{rt}, r{rs}, 0x{imm:04X}"
    if op == 0xD: return f"ORI r{rt}, r{rs}, 0x{imm:04X}"
    if op == 0xF: return f"LUI r{rt}, 0x{imm:04X}"
    if op == 0x20: return f"LB r{rt}, {simm}(r{rs})"
    if op == 0x21: return f"LH r{rt}, {simm}(r{rs})"
    if op == 0x23: return f"LW r{rt}, {simm}(r{rs})"
    if op == 0x24: return f"LBU r{rt}, {simm}(r{rs})"
    if op == 0x25: return f"LHU r{rt}, {simm}(r{rs})"
    if op == 0x28: return f"SB r{rt}, {simm}(r{rs})"
    if op == 0x29: return f"SH r{rt}, {simm}(r{rs})"
    if op == 0x2B: return f"SW r{rt}, {simm}(r{rs})"
    return f"??? op={op:#x} (0x{word:08X})"

# Decode from 0x80010A90 to end of function
print("=== Height clamp area (0x80010A90 - 0x80010AD8) ===")
for i in range(0, 0x80010AD8 - 0x80010A90, 4):
    off = 0x80010A90 - base + i
    if off + 4 > len(text_data):
        break
    word = struct.unpack_from('<I', text_data, off)[0]
    addr = base + off
    print(f"  0x{addr:08X}: {decode(word, addr)}")

# Also decode from 0x80010AD0 which seems to be a fall-through fragment
print()
print("=== Fall-through fragment (0x80010AD0 - 0x80010AD8) ===")
for i in range(0, 8, 4):
    off = 0x80010AD0 - base + i
    word = struct.unpack_from('<I', text_data, off)[0]
    addr = base + off
    print(f"  0x{addr:08X}: {decode(word, addr)}")
