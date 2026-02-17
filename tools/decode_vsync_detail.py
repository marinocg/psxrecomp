#!/usr/bin/env python3
"""Detailed decode of VSync inner loop from HELLOWLD.iso.
Decodes 0x80010480 - 0x80010680, annotating:
  - LUI base addresses
  - MMIO accesses (0xBF8xxxxx / 0x1F8xxxxx)
  - s3 (r19) tracking: where it's loaded and how it's used
  - AND + BEQ patterns (GPU_STAT polling)
"""
import struct
import sys

REGS = {0:"zero",1:"at",2:"v0",3:"v1",4:"a0",5:"a1",6:"a2",7:"a3",
        8:"t0",9:"t1",10:"t2",11:"t3",12:"t4",13:"t5",14:"t6",15:"t7",
        16:"s0",17:"s1",18:"s2",19:"s3",20:"s4",21:"s5",22:"s6",23:"s7",
        24:"t8",25:"t9",26:"k0",27:"k1",28:"gp",29:"sp",30:"fp",31:"ra"}

def rn(r):
    return REGS.get(r, f"r{r}")

def decode(addr, w):
    opcode = (w >> 26) & 0x3F
    rs = (w >> 21) & 0x1F
    rt = (w >> 16) & 0x1F
    rd = (w >> 11) & 0x1F
    func = w & 0x3F
    imm = w & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    sa = (w >> 6) & 0x1F
    target = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)

    if opcode == 0:
        names = {
            0:  f"SLL {rn(rd)},{rn(rt)},{sa}",
            2:  f"SRL {rn(rd)},{rn(rt)},{sa}",
            3:  f"SRA {rn(rd)},{rn(rt)},{sa}",
            4:  f"SLLV {rn(rd)},{rn(rt)},{rn(rs)}",
            6:  f"SRLV {rn(rd)},{rn(rt)},{rn(rs)}",
            8:  f"JR {rn(rs)}",
            9:  f"JALR {rn(rd)},{rn(rs)}",
            0xC: f"SYSCALL",
            0xD: f"BREAK",
            0x10: f"MFHI {rn(rd)}",
            0x12: f"MFLO {rn(rd)}",
            0x18: f"MULT {rn(rs)},{rn(rt)}",
            0x19: f"MULTU {rn(rs)},{rn(rt)}",
            0x1a: f"DIV {rn(rs)},{rn(rt)}",
            0x1b: f"DIVU {rn(rs)},{rn(rt)}",
            0x20: f"ADD {rn(rd)},{rn(rs)},{rn(rt)}",
            0x21: f"ADDU {rn(rd)},{rn(rs)},{rn(rt)}",
            0x22: f"SUB {rn(rd)},{rn(rs)},{rn(rt)}",
            0x23: f"SUBU {rn(rd)},{rn(rs)},{rn(rt)}",
            0x24: f"AND {rn(rd)},{rn(rs)},{rn(rt)}",
            0x25: f"OR {rn(rd)},{rn(rs)},{rn(rt)}",
            0x26: f"XOR {rn(rd)},{rn(rs)},{rn(rt)}",
            0x27: f"NOR {rn(rd)},{rn(rs)},{rn(rt)}",
            0x2a: f"SLT {rn(rd)},{rn(rs)},{rn(rt)}",
            0x2b: f"SLTU {rn(rd)},{rn(rs)},{rn(rt)}",
        }
        if w == 0:
            return "NOP"
        return names.get(func, f"SPECIAL func={func:#x}")

    if opcode == 1:
        if rt == 0: return f"BLTZ {rn(rs)},{addr + 4 + (simm << 2):#010x}"
        if rt == 1: return f"BGEZ {rn(rs)},{addr + 4 + (simm << 2):#010x}"
        return f"REGIMM rt={rt}"

    # COP0
    if opcode == 0x10:
        if rs == 0: return f"MFC0 {rn(rt)},cop0_{rd}"
        if rs == 4: return f"MTC0 {rn(rt)},cop0_{rd}"
        if rs == 0x10:
            if func == 0x10: return "RFE"
        return f"COP0 rs={rs} func={func:#x}"

    # COP2 (GTE)
    if opcode == 0x12:
        if rs == 0: return f"MFC2 {rn(rt)},gte_{rd}"
        if rs == 2: return f"CFC2 {rn(rt)},gte_{rd}"
        if rs == 4: return f"MTC2 {rn(rt)},gte_{rd}"
        if rs == 6: return f"CTC2 {rn(rt)},gte_{rd}"
        return f"COP2 {w & 0x1FFFFFF:#x}"

    names = {
        2:  lambda: f"J {target:#010x}",
        3:  lambda: f"JAL {target:#010x}",
        4:  lambda: f"BEQ {rn(rs)},{rn(rt)},{addr + 4 + (simm << 2):#010x}",
        5:  lambda: f"BNE {rn(rs)},{rn(rt)},{addr + 4 + (simm << 2):#010x}",
        6:  lambda: f"BLEZ {rn(rs)},{addr + 4 + (simm << 2):#010x}",
        7:  lambda: f"BGTZ {rn(rs)},{addr + 4 + (simm << 2):#010x}",
        8:  lambda: f"ADDI {rn(rt)},{rn(rs)},{simm}",
        9:  lambda: f"ADDIU {rn(rt)},{rn(rs)},{simm}",
        10: lambda: f"SLTI {rn(rt)},{rn(rs)},{simm}",
        11: lambda: f"SLTIU {rn(rt)},{rn(rs)},{simm}",
        12: lambda: f"ANDI {rn(rt)},{rn(rs)},{imm:#06x}",
        13: lambda: f"ORI {rn(rt)},{rn(rs)},{imm:#06x}",
        14: lambda: f"XORI {rn(rt)},{rn(rs)},{imm:#06x}",
        15: lambda: f"LUI {rn(rt)},{imm:#06x}",
        32: lambda: f"LB {rn(rt)},{simm}({rn(rs)})",
        33: lambda: f"LH {rn(rt)},{simm}({rn(rs)})",
        34: lambda: f"LWL {rn(rt)},{simm}({rn(rs)})",
        35: lambda: f"LW {rn(rt)},{simm}({rn(rs)})",
        36: lambda: f"LBU {rn(rt)},{simm}({rn(rs)})",
        37: lambda: f"LHU {rn(rt)},{simm}({rn(rs)})",
        38: lambda: f"LWR {rn(rt)},{simm}({rn(rs)})",
        40: lambda: f"SB {rn(rt)},{simm}({rn(rs)})",
        41: lambda: f"SH {rn(rt)},{simm}({rn(rs)})",
        42: lambda: f"SWL {rn(rt)},{simm}({rn(rs)})",
        43: lambda: f"SW {rn(rt)},{simm}({rn(rs)})",
        46: lambda: f"SWR {rn(rt)},{simm}({rn(rs)})",
        0x32: lambda: f"LWC2 {rn(rt)},{simm}({rn(rs)})",
        0x3A: lambda: f"SWC2 {rn(rt)},{simm}({rn(rs)})",
    }
    if opcode in names:
        return names[opcode]()
    return f"op={opcode} raw={w:#010x}"


def extract_exe_from_iso(iso_path):
    """Extract PS-X EXE code from an ISO image."""
    with open(iso_path, "rb") as f:
        data = f.read()

    sector_size = 2048
    data_offset = 0
    data_len = 2048

    if len(data) >= 16 and data[:12] == bytes([0x00, 0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0xFF, 0x00]):
        sector_size = 2352
        data_offset = 24
        data_len = 2048

    for sector in range(16, min(len(data) // sector_size, 300)):
        offset = sector * sector_size + data_offset
        if offset + 8 <= len(data) and data[offset:offset + 8] == b"PS-X EXE":
            text_start = struct.unpack_from("<I", data, offset + 0x18)[0]
            text_size = struct.unpack_from("<I", data, offset + 0x1C)[0]
            entry = struct.unpack_from("<I", data, offset + 0x10)[0]

            print(f"PS-X EXE header at sector {sector} (offset {offset:#x})")
            print(f"  Load addr : {text_start:#010x}")
            print(f"  Text size : {text_size} ({text_size:#x})")
            print(f"  Entry     : {entry:#010x}")

            code = bytearray()
            text_sectors = (text_size + data_len - 1) // data_len
            for s in range(text_sectors + 2):
                sec_num = sector + 1 + s
                sec_off = sec_num * sector_size + data_offset
                if sec_off + data_len <= len(data):
                    code.extend(data[sec_off:sec_off + data_len])
            code = bytes(code[:text_size])
            return text_start, text_size, entry, code

    return None, None, None, None


def main():
    iso_path = "/Users/marinocg/trash/psxrecomp/out/recompiled-demos-windows-latest/inputs/HELLOWLD.iso"

    text_start, text_size, entry, code = extract_exe_from_iso(iso_path)
    if code is None:
        print("ERROR: Could not extract PS-X EXE")
        sys.exit(1)

    text_end = text_start + text_size

    # ================================================================
    # Decode range 0x80010480 - 0x80010680
    # ================================================================
    DECODE_START = 0x80010480
    DECODE_END   = 0x80010680

    print(f"\nCode range: {text_start:#010x} - {text_end:#010x}")
    print(f"\n{'='*90}")
    print(f"  FULL DECODE: {DECODE_START:#010x} - {DECODE_END:#010x}")
    print(f"  ({(DECODE_END - DECODE_START) // 4} instructions)")
    print(f"{'='*90}\n")

    # Track LUI values for annotation
    lui_vals = {}  # reg -> upper16

    for off in range(DECODE_START - text_start, DECODE_END - text_start, 4):
        if off < 0 or off + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off

        opcode = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        rd = (w >> 11) & 0x1F
        imm = w & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000
        func = w & 0x3F

        dis = decode(addr, w)
        annotation = ""

        # Track LUI
        if opcode == 15:  # LUI
            upper = imm << 16
            lui_vals[rt] = upper
            annotation = f"  ; {rn(rt)} = {upper:#010x}"

        # Track ORI after LUI (immediate constant build)
        if opcode == 13:  # ORI
            if rs in lui_vals:
                full = lui_vals[rs] | imm
                annotation = f"  ; {rn(rt)} = {full:#010x}"

        # Track ADDIU after LUI
        if opcode == 9:  # ADDIU
            if rs in lui_vals:
                full = (lui_vals[rs] + simm) & 0xFFFFFFFF
                annotation = f"  ; {rn(rt)} = {full:#010x}"

        # Annotate LW/SW with MMIO addresses
        if opcode in (35, 43, 32, 33, 36, 37, 40, 41):  # LW/SW/LB/LH/LBU/LHU/SB/SH
            if rs in lui_vals:
                effective = (lui_vals[rs] + simm) & 0xFFFFFFFF
                if (effective & 0xFF000000) in (0xBF000000, 0x1F000000):
                    mmio_names = {
                        0xBF801810: "GP0 (GPU Command/Data)",
                        0xBF801814: "GP1 (GPU Control/Status = GPU_STAT)",
                        0x1F801810: "GP0 (GPU Command/Data)",
                        0x1F801814: "GP1 (GPU Control/Status = GPU_STAT)",
                        0x1F801070: "I_STAT (IRQ Status)",
                        0x1F801074: "I_MASK (IRQ Mask)",
                        0xBF801070: "I_STAT (IRQ Status)",
                        0xBF801074: "I_MASK (IRQ Mask)",
                    }
                    name = mmio_names.get(effective, "MMIO")
                    annotation = f"  ; [{effective:#010x}] = {name}"

        # Annotate AND involving s3 or s7
        if opcode == 0 and func == 0x24:  # AND
            if rs == 19 or rt == 19 or rd == 19:
                annotation += f"  ; *** AND with s3 ***"
            if rs == 23 or rt == 23:
                annotation += f"  ; involves s7"

        # Annotate BEQ/BNE involving key regs
        if opcode in (4, 5):
            if rs == 19 or rt == 19:
                annotation += f"  ; branch on s3"
            if rs == 23 or rt == 23:
                annotation += f"  ; branch on s7"

        # Mark JAL targets
        if opcode == 3:
            target = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            if target == 0x80010398:
                annotation = f"  ; <<< event dispatch"

        print(f"  {addr:#010x}: {w:08x}  {dis:45s}{annotation}")

    # ================================================================
    # Track s3 (r19) specifically
    # ================================================================
    print(f"\n{'='*90}")
    print(f"  s3 (r19) REGISTER TRACKING")
    print(f"{'='*90}\n")
    print("Searching entire code for instructions that WRITE to r19 (s3)...\n")

    # Scan a wider range for s3 setup: 0x80010400 - 0x80010680
    SCAN_START = 0x80010400
    SCAN_END   = 0x80010680

    for off in range(SCAN_START - text_start, SCAN_END - text_start, 4):
        if off < 0 or off + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off
        opcode = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        rd = (w >> 11) & 0x1F
        func = w & 0x3F
        imm = w & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000

        writes_r19 = False

        # R-type: rd is destination
        if opcode == 0 and func not in (8, 9, 0xC, 0xD, 0x18, 0x19, 0x1a, 0x1b):
            if rd == 19:
                writes_r19 = True

        # I-type where rt is destination: LUI, ADDIU, ORI, ANDI, LW, LB, etc.
        if opcode in (8, 9, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 36, 37, 38):
            if rt == 19:
                writes_r19 = True

        if writes_r19:
            dis = decode(addr, w)
            note = ""
            if opcode == 15:  # LUI
                note = f"  s3 upper = {imm << 16:#010x}"
            if opcode == 13:  # ORI
                note = f"  s3 |= {imm:#06x}"
            if opcode == 9:  # ADDIU
                note = f"  s3 += {simm}"
            print(f"  {addr:#010x}: {w:08x}  {dis:45s} *** WRITES s3 *** {note}")

    # ================================================================
    # Track s7 (r23) specifically
    # ================================================================
    print(f"\n{'='*90}")
    print(f"  s7 (r23) REGISTER TRACKING")
    print(f"{'='*90}\n")
    print("Searching for instructions that WRITE to r23 (s7)...\n")

    for off in range(SCAN_START - text_start, SCAN_END - text_start, 4):
        if off < 0 or off + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off
        opcode = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        rd = (w >> 11) & 0x1F
        func = w & 0x3F
        imm = w & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000

        writes_r23 = False
        if opcode == 0 and func not in (8, 9, 0xC, 0xD, 0x18, 0x19, 0x1a, 0x1b):
            if rd == 23:
                writes_r23 = True
        if opcode in (8, 9, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 36, 37, 38):
            if rt == 23:
                writes_r23 = True
        if writes_r23:
            dis = decode(addr, w)
            note = ""
            if opcode == 15:
                note = f"  s7 upper = {imm << 16:#010x}"
            if opcode == 35:  # LW
                if rs in lui_vals:
                    eff = (lui_vals[rs] + simm) & 0xFFFFFFFF
                    note = f"  s7 = [{eff:#010x}]"
            print(f"  {addr:#010x}: {w:08x}  {dis:45s} *** WRITES s7 *** {note}")

    # ================================================================
    # Summary: AND + BEQ patterns
    # ================================================================
    print(f"\n{'='*90}")
    print(f"  AND + BEQ/BNE PATTERN SEARCH in VSync area")
    print(f"{'='*90}\n")

    for off in range(SCAN_START - text_start, SCAN_END - text_start, 4):
        if off < 0 or off + 8 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off
        opcode = (w >> 26) & 0x3F
        func = w & 0x3F

        if opcode == 0 and func == 0x24:  # AND
            rs = (w >> 21) & 0x1F
            rt = (w >> 16) & 0x1F
            rd = (w >> 11) & 0x1F
            # Check next instruction
            w2 = struct.unpack_from("<I", code, off + 4)[0]
            op2 = (w2 >> 26) & 0x3F
            if op2 in (4, 5):  # BEQ or BNE
                dis1 = decode(addr, w)
                dis2 = decode(addr + 4, w2)
                rs2 = (w2 >> 21) & 0x1F
                rt2 = (w2 >> 16) & 0x1F
                print(f"  {addr:#010x}: {dis1}")
                print(f"  {addr+4:#010x}: {dis2}")
                print(f"    Pattern: AND {rn(rd)} = {rn(rs)} & {rn(rt)}, then {'BEQ' if op2==4 else 'BNE'} {rn(rs2)},{rn(rt2)}")
                print()

    # ================================================================
    # Also look BEFORE VSync function for s3 setup (function prologue)
    # ================================================================
    print(f"\n{'='*90}")
    print(f"  WIDER SEARCH: s3 writes from 0x80010000 to 0x80010500")
    print(f"{'='*90}\n")

    WIDE_START = 0x80010000
    WIDE_END   = 0x80010500
    lui_track = {}

    for off in range(WIDE_START - text_start, WIDE_END - text_start, 4):
        if off < 0 or off + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off
        opcode = (w >> 26) & 0x3F
        rs_r = (w >> 21) & 0x1F
        rt_r = (w >> 16) & 0x1F
        rd_r = (w >> 11) & 0x1F
        func = w & 0x3F
        imm = w & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000

        # Track all LUI
        if opcode == 15:
            lui_track[rt_r] = imm << 16

        writes_r19 = False
        if opcode == 0 and func not in (8, 9, 0xC, 0xD, 0x18, 0x19, 0x1a, 0x1b):
            if rd_r == 19:
                writes_r19 = True
        if opcode in (8, 9, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 36, 37, 38):
            if rt_r == 19:
                writes_r19 = True

        if writes_r19:
            dis = decode(addr, w)
            note = ""
            if opcode == 15:
                note = f"  s3 = {imm << 16:#010x} (upper)"
            if opcode == 13:  # ORI
                if rs_r in lui_track:
                    full = lui_track[rs_r] | imm
                    note = f"  s3 = {full:#010x}"
                else:
                    note = f"  s3 |= {imm:#06x}"
            if opcode == 9:  # ADDIU
                if rs_r in lui_track:
                    full = (lui_track[rs_r] + simm) & 0xFFFFFFFF
                    note = f"  s3 = {full:#010x}"
                else:
                    note = f"  s3 += {simm}"
            if opcode == 35:  # LW
                if rs_r in lui_track:
                    eff = (lui_track[rs_r] + simm) & 0xFFFFFFFF
                    note = f"  s3 = mem[{eff:#010x}]"
            # Show context: 2 instructions before
            for ctx in range(-2, 1):
                ctx_off = off + ctx * 4
                if 0 <= ctx_off < len(code) - 3:
                    cw = struct.unpack_from("<I", code, ctx_off)[0]
                    ca = text_start + ctx_off
                    cd = decode(ca, cw)
                    marker = " <<<" if ctx == 0 else "    "
                    print(f"  {ca:#010x}: {cw:08x}  {cd:45s}{marker}{note if ctx == 0 else ''}")
            print()


if __name__ == "__main__":
    main()
