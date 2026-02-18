#!/usr/bin/env python3
"""Debug: decode instructions around the entry point."""
import struct
import sys

iso_path = sys.argv[1] if len(sys.argv) > 1 else "/psxrecomp/out/recompiled-demos-windows-latest/inputs/HELLOWLD.iso"
with open(iso_path, "rb") as f:
    all_data = f.read()

exe_offset = all_data.find(b"PS-X EXE")
load_addr = struct.unpack_from("<I", all_data, exe_offset + 0x18)[0]
load_size = struct.unpack_from("<I", all_data, exe_offset + 0x1C)[0]
program = all_data[exe_offset + 0x800 : exe_offset + 0x800 + load_size]

def decode(addr, w):
    opcode = (w >> 26) & 0x3F
    rs = (w >> 21) & 0x1F
    rt = (w >> 16) & 0x1F
    rd = (w >> 11) & 0x1F
    func = w & 0x3F
    imm = w & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    sa = (w >> 6) & 0x1F
    
    if opcode == 0:
        names = {8: f"JR r{rs}", 9: f"JALR r{rs}", 0x21: f"ADDU r{rd},r{rs},r{rt}",
                 0x25: f"OR r{rd},r{rs},r{rt}", 0: f"SLL r{rd},r{rt},{sa}",
                 0x23: f"SUBU r{rd},r{rs},r{rt}", 0x24: f"AND r{rd},r{rs},r{rt}",
                 0x2a: f"SLT r{rd},r{rs},r{rt}", 0x2b: f"SLTU r{rd},r{rs},r{rt}"}
        return names.get(func, f"SPECIAL func={func}")
    names = {
        2: lambda: f"J {((addr+4)&0xF0000000)|((w&0x03FFFFFF)<<2):#010x}",
        3: lambda: f"JAL {((addr+4)&0xF0000000)|((w&0x03FFFFFF)<<2):#010x}",
        4: lambda: f"BEQ r{rs},r{rt},{simm}",
        5: lambda: f"BNE r{rs},r{rt},{simm}",
        6: lambda: f"BLEZ r{rs},{simm}",
        7: lambda: f"BGTZ r{rs},{simm}",
        9: lambda: f"ADDIU r{rt},r{rs},{simm}",
        10: lambda: f"SLTI r{rt},r{rs},{simm}",
        11: lambda: f"SLTIU r{rt},r{rs},{simm}",
        12: lambda: f"ANDI r{rt},r{rs},{imm:#06x}",
        13: lambda: f"ORI r{rt},r{rs},{imm:#06x}",
        15: lambda: f"LUI r{rt},{imm:#06x}",
        35: lambda: f"LW r{rt},{simm}(r{rs})",
        43: lambda: f"SW r{rt},{simm}(r{rs})",
        40: lambda: f"SB r{rt},{simm}(r{rs})",
        41: lambda: f"SH r{rt},{simm}(r{rs})",
        33: lambda: f"LH r{rt},{simm}(r{rs})",
        36: lambda: f"LBU r{rt},{simm}(r{rs})",
        37: lambda: f"LHU r{rt},{simm}(r{rs})",
        32: lambda: f"LB r{rt},{simm}(r{rs})",
    }
    if opcode == 1:
        if rt == 16: return f"BLTZAL r{rs},{simm}"
        if rt == 17: return f"BGEZAL r{rs},{simm}"
        if rt == 0: return f"BLTZ r{rs},{simm}"
        if rt == 1: return f"BGEZ r{rs},{simm}"
        return f"REGIMM rt={rt}"
    if opcode in names:
        return names[opcode]()
    return f"op={opcode}"

# Decode 32 instructions from entry
print("=== Instructions at entry 0x80012940 ===")
for i in range(32):
    addr = 0x80012940 + i * 4
    off = addr - load_addr
    if off < 0 or off + 4 > len(program):
        continue
    w = struct.unpack_from("<I", program, off)[0]
    print(f"  {addr:#010x}: {w:#010x}  {decode(addr, w)}")

# Check if entry has a prologue
print("\n=== Prologue check at 0x80012940 ===")
off_entry = 0x80012940 - load_addr
w = struct.unpack_from("<I", program, off_entry)[0]
opcode = (w >> 26) & 0x3F
rs = (w >> 21) & 0x1F
rt = (w >> 16) & 0x1F
simm = (w & 0xFFFF) if (w & 0xFFFF) < 0x8000 else (w & 0xFFFF) - 0x10000
if opcode == 9 and rs == 29 and rt == 29 and simm < 0:
    print(f"  YES: ADDIU SP, SP, {simm}")
else:
    print(f"  NO: not ADDIU SP, SP, -N")

# Find the next function start after 0x80012940 from prologues
print("\n=== Next prologue after 0x80012940 ===")
for i in range(1, 200):
    addr = 0x80012940 + i * 4
    off = addr - load_addr
    if off + 12 > len(program):
        break
    w = struct.unpack_from("<I", program, off)[0]
    opcode = (w >> 26) & 0x3F
    rs = (w >> 21) & 0x1F
    rt = (w >> 16) & 0x1F
    simm = (w & 0xFFFF) if (w & 0xFFFF) < 0x8000 else (w & 0xFFFF) - 0x10000
    if opcode == 9 and rs == 29 and rt == 29 and simm < 0:
        # Check next 2 for SW RA
        for j in range(1, 3):
            off2 = off + j * 4
            w2 = struct.unpack_from("<I", program, off2)[0]
            op2 = (w2 >> 26) & 0x3F
            rs2 = (w2 >> 21) & 0x1F
            rt2 = (w2 >> 16) & 0x1F
            if op2 == 43 and rs2 == 29 and rt2 == 31:
                print(f"  Prologue at {addr:#010x}")
                break
        else:
            continue
        break

# Check if 0x80012940 is in a code range
# For that we need to check: what are JAL targets near entry?
print("\n=== JAL targets from entry function ===")
for i in range(200):
    addr = 0x80012940 + i * 4
    off = addr - load_addr
    if off + 4 > len(program):
        break
    w = struct.unpack_from("<I", program, off)[0]
    opcode = (w >> 26) & 0x3F
    if opcode == 3:  # JAL
        tgt = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
        print(f"  {addr:#010x}: JAL {tgt:#010x}")
    elif opcode == 0 and (w & 0x3F) == 8:
        rs = (w >> 21) & 0x1F
        if rs == 31:
            print(f"  {addr:#010x}: JR RA (return)")
            break
