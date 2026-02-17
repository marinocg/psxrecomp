#!/usr/bin/env python3
"""Decode MIPS code around the step-exhaustion PC for each demo."""
import struct
import sys
import os

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
        names = {0: f"SLL r{rd},r{rt},{sa}", 8: f"JR r{rs}", 9: f"JALR r{rd},r{rs}",
                 0x21: f"ADDU r{rd},r{rs},r{rt}", 0x23: f"SUBU r{rd},r{rs},r{rt}",
                 0x24: f"AND r{rd},r{rs},r{rt}", 0x25: f"OR r{rd},r{rs},r{rt}",
                 0x2a: f"SLT r{rd},r{rs},r{rt}", 0x2b: f"SLTU r{rd},r{rs},r{rt}",
                 0x0d: "BREAK"}
        return names.get(func, f"SPECIAL func={func:#x}")
    names = {
        2: lambda: f"J {((addr+4)&0xF0000000)|((w&0x03FFFFFF)<<2):#010x}",
        3: lambda: f"JAL {((addr+4)&0xF0000000)|((w&0x03FFFFFF)<<2):#010x}",
        4: lambda: f"BEQ r{rs},r{rt},{simm}",
        5: lambda: f"BNE r{rs},r{rt},{simm}",
        6: lambda: f"BLEZ r{rs},{simm}",
        7: lambda: f"BGTZ r{rs},{simm}",
        9: lambda: f"ADDIU r{rt},r{rs},{simm}",
        10: lambda: f"SLTI r{rt},r{rs},{simm}",
        12: lambda: f"ANDI r{rt},r{rs},{imm:#06x}",
        13: lambda: f"ORI r{rt},r{rs},{imm:#06x}",
        15: lambda: f"LUI r{rt},{imm:#06x}",
        35: lambda: f"LW r{rt},{simm}(r{rs})",
        43: lambda: f"SW r{rt},{simm}(r{rs})",
        40: lambda: f"SB r{rt},{simm}(r{rs})",
        41: lambda: f"SH r{rt},{simm}(r{rs})",
        36: lambda: f"LBU r{rt},{simm}(r{rs})",
        37: lambda: f"LHU r{rt},{simm}(r{rs})",
    }
    if opcode == 1:
        if rt == 0: return f"BLTZ r{rs},{simm}"
        if rt == 1: return f"BGEZ r{rs},{simm}"
        return f"REGIMM rt={rt}"
    if opcode in names:
        return names[opcode]()
    return f"op={opcode}"

demos = {
    "HELLOWLD": 0x10398,
    "GPUTEST": 0x1039c,
    "MEMTEST": 0x103b0,
    "ADVHELLO": 0x107ac,
}

iso_dir = sys.argv[1] if len(sys.argv) > 1 else "/psxrecomp/out/recompiled-demos-windows-latest/inputs"

for name, phys_pc in demos.items():
    iso_path = os.path.join(iso_dir, f"{name}.iso")
    if not os.path.exists(iso_path):
        print(f"[{name}] ISO not found: {iso_path}")
        continue

    with open(iso_path, "rb") as f:
        data = f.read()

    exe_offset = data.find(b"PS-X EXE")
    if exe_offset < 0:
        print(f"[{name}] PS-X EXE marker not found")
        continue

    load_addr = struct.unpack_from("<I", data, exe_offset + 0x18)[0]
    load_size = struct.unpack_from("<I", data, exe_offset + 0x1C)[0]
    entry_addr = struct.unpack_from("<I", data, exe_offset + 0x10)[0]
    program = data[exe_offset + 0x800 : exe_offset + 0x800 + load_size]

    virt_pc = phys_pc | 0x80000000
    print(f"\n{'='*60}")
    print(f"  {name}: load={load_addr:#x} size={load_size} entry={entry_addr:#x}")
    print(f"  Spin PC: {virt_pc:#010x} (physical {phys_pc:#x})")
    print(f"{'='*60}")

    print(f"\n  Code around spin PC:")
    for i in range(-20, 30):
        addr = virt_pc + i * 4
        off = addr - load_addr
        if 0 <= off < len(program) and off + 4 <= len(program):
            w = struct.unpack_from("<I", program, off)[0]
            marker = " <<< SPIN" if addr == virt_pc else ""
            print(f"    {addr:#010x}: {w:#010x}  {decode(addr, w)}{marker}")

    # Find all I_STAT related code (0x1F801070)
    print(f"\n  I_STAT (0x1F801070) accesses:")
    for i in range(load_size // 4):
        addr = load_addr + i * 4
        w = struct.unpack_from("<I", program, i * 4)[0]
        opcode_check = (w >> 26) & 0x3F
        if opcode_check == 15:  # LUI
            lui_imm = w & 0xFFFF
            if lui_imm == 0x1F80:
                print(f"    {addr:#010x}: LUI with 0x1F80 (I/O base)")
                # Check next few instructions for ADDIU/ORI to form I_STAT
                for j in range(1, 4):
                    if i + j < load_size // 4:
                        w2 = struct.unpack_from("<I", program, (i+j)*4)[0]
                        addr2 = load_addr + (i+j)*4
                        print(f"    {addr2:#010x}: {w2:#010x}  {decode(addr2, w2)}")
                print()
