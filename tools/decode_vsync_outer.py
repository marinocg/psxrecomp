#!/usr/bin/env python3
"""Decode the complete VSync function from HELLOWLD.iso (0x80010500 - 0x80010680).

Focus on understanding the outer loop logic:
  - What is s0 (r16) set to at the start?
  - How does the outer loop counter check work?
  - What is the EXACT condition that causes VSync(0) to return?
  - What is the frame counter address in RAM (vsync_counter)?

VSync(mode=0) is called with a0 (r4) = 0.
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
            7:  f"SRAV {rn(rd)},{rn(rt)},{rn(rs)}",
            8:  f"JR {rn(rs)}",
            9:  f"JALR {rn(rd)},{rn(rs)}",
            0xC: "SYSCALL",
            0xD: "BREAK",
            0x10: f"MFHI {rn(rd)}",
            0x11: f"MTHI {rn(rs)}",
            0x12: f"MFLO {rn(rd)}",
            0x13: f"MTLO {rn(rs)}",
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
        if rt == 16: return f"BLTZAL {rn(rs)},{addr + 4 + (simm << 2):#010x}"
        if rt == 17: return f"BGEZAL {rn(rs)},{addr + 4 + (simm << 2):#010x}"
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

    # Detect raw 2352-byte sectors (sync pattern)
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
            text_size  = struct.unpack_from("<I", data, offset + 0x1C)[0]
            entry      = struct.unpack_from("<I", data, offset + 0x10)[0]

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
    # Decode range: complete VSync function 0x80010500 - 0x80010680
    # ================================================================
    DECODE_START = 0x80010500
    DECODE_END   = 0x80010680

    print(f"\nCode range: {text_start:#010x} - {text_end:#010x}")
    print(f"\n{'='*100}")
    print(f"  COMPLETE VSync FUNCTION: {DECODE_START:#010x} - {DECODE_END:#010x}")
    print(f"  ({(DECODE_END - DECODE_START) // 4} instructions)")
    print(f"  VSync(mode) called with a0 = mode.  VSync(0) means a0 = 0.")
    print(f"{'='*100}\n")

    # Track LUI values for annotation
    lui_vals = {}   # reg -> upper16 value
    # Track known RAM addresses
    ram_symbols = {}

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
        sa = (w >> 6) & 0x1F

        dis = decode(addr, w)
        annotation = ""

        # ----- Track LUI -----
        if opcode == 15:  # LUI
            upper = imm << 16
            lui_vals[rt] = upper
            annotation = f"  ; {rn(rt)} = {upper:#010x}"

        # ----- Track ORI after LUI -----
        if opcode == 13:  # ORI
            if rs in lui_vals:
                full = lui_vals[rs] | imm
                annotation = f"  ; {rn(rt)} = {full:#010x}"
                if rt == rs:
                    lui_vals[rt] = full

        # ----- Track ADDIU after LUI -----
        if opcode == 9:  # ADDIU
            if rs in lui_vals:
                full = (lui_vals[rs] + simm) & 0xFFFFFFFF
                annotation = f"  ; {rn(rt)} = {full:#010x}"
                if rt == rs:
                    lui_vals[rt] = full

        # ----- Annotate LW/SW with effective addresses -----
        if opcode in (35, 43, 32, 33, 36, 37, 40, 41):
            if rs in lui_vals:
                effective = (lui_vals[rs] + simm) & 0xFFFFFFFF
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
                if effective in mmio_names:
                    annotation = f"  ; [{effective:#010x}] = {mmio_names[effective]}"
                elif (effective & 0xFF000000) in (0x80000000,):
                    annotation = f"  ; [{effective:#010x}] RAM"
                    # Track potential vsync_counter
                    if rt == 16 or rd == 16:  # s0
                        annotation += " (involves s0)"
                elif (effective & 0xFF000000) in (0xBF000000, 0x1F000000):
                    annotation = f"  ; [{effective:#010x}] MMIO"

        # ----- Annotate branches with target labels -----
        if opcode in (4, 5, 6, 7):
            branch_target = addr + 4 + (simm << 2)
            if DECODE_START <= branch_target < DECODE_END:
                rel = branch_target - DECODE_START
                annotation += f"  ; -> offset +{rel:#x}"
            if rs == 0 and rt == 0 and opcode == 4:
                annotation += "  (unconditional branch)"

        # ----- Annotate BGEZ / BLTZ -----
        if opcode == 1:
            branch_target = addr + 4 + (simm << 2)
            if DECODE_START <= branch_target < DECODE_END:
                rel = branch_target - DECODE_START
                annotation += f"  ; -> offset +{rel:#x}"

        # ----- Mark key registers -----
        # s0 = r16 tracking
        if opcode == 0:
            if func not in (8, 9, 0xC, 0xD, 0x18, 0x19, 0x1a, 0x1b):
                if rd == 16:
                    annotation += "  ; *** writes s0 ***"
            if func in (0x24, 0x25, 0x26, 0x2a, 0x2b):  # AND/OR/XOR/SLT/SLTU
                if rs == 16 or rt == 16:
                    annotation += "  ; uses s0"
        if opcode in (8, 9, 10, 11, 12, 13, 14, 15):
            if rt == 16:
                annotation += "  ; *** writes s0 ***"
        if opcode in (35, 32, 33, 36, 37):  # loads
            if rt == 16:
                annotation += "  ; *** loads into s0 ***"

        # a0 = r4 tracking (mode argument)
        if opcode == 0 and func not in (8, 9, 0xC, 0xD, 0x18, 0x19, 0x1a, 0x1b):
            if rd == 4 or rs == 4 or rt == 4:
                if "s0" not in annotation:
                    annotation += "  ; involves a0"
        if opcode in (4, 5):
            if rs == 4 or rt == 4:
                annotation += "  ; branches on a0"

        # ----- JAL targets -----
        if opcode == 3:
            target = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            annotation += f"  ; call {target:#010x}"

        # ----- JR ra = return -----
        if opcode == 0 and func == 8 and rs == 31:
            annotation += "  ; <<< RETURN >>>"

        # ----- Stack frame -----
        if opcode == 9 and rs == 29 and rt == 29:  # ADDIU sp,sp
            if simm < 0:
                annotation += f"  ; prologue: allocate {-simm} bytes"
            else:
                annotation += f"  ; epilogue: free {simm} bytes"

        # ----- SW ra / LW ra on stack -----
        if opcode == 43 and rt == 31 and rs == 29:
            annotation += "  ; save ra"
        if opcode == 35 and rt == 31 and rs == 29:
            annotation += "  ; restore ra"
        if opcode == 43 and rs == 29 and rt in (16,17,18,19,20,21,22,23,30):
            annotation += f"  ; save {rn(rt)}"
        if opcode == 35 and rs == 29 and rt in (16,17,18,19,20,21,22,23,30):
            annotation += f"  ; restore {rn(rt)}"

        print(f"  {addr:#010x}: {w:08x}  {dis:45s}{annotation}")

    # ================================================================
    # ANALYSIS: s0 (r16) writes in VSync
    # ================================================================
    print(f"\n{'='*100}")
    print(f"  s0 (r16) REGISTER TRACKING in VSync")
    print(f"{'='*100}\n")

    for off in range(DECODE_START - text_start, DECODE_END - text_start, 4):
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

        writes_s0 = False
        if opcode == 0 and func not in (8, 9, 0xC, 0xD, 0x18, 0x19, 0x1a, 0x1b):
            if rd == 16: writes_s0 = True
        if opcode in (8, 9, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 36, 37, 38):
            if rt == 16: writes_s0 = True

        if writes_s0:
            dis = decode(addr, w)
            note = ""
            if opcode == 15:
                note = f"  s0 upper = {imm << 16:#010x}"
            if opcode == 9:  # ADDIU
                note = f"  s0 += {simm}"
            if opcode == 35:  # LW
                note = f"  s0 = load from memory"
            if opcode == 0 and func == 0x21:  # ADDU
                note = f"  s0 = {rn(rs)} + {rn(rt)}"
            print(f"  {addr:#010x}: {w:08x}  {dis:45s} *** WRITES s0 *** {note}")

    # ================================================================
    # ANALYSIS: LW / SW to RAM addresses (find vsync_counter)
    # ================================================================
    print(f"\n{'='*100}")
    print(f"  RAM ACCESS TRACKING (find vsync_counter)")
    print(f"{'='*100}\n")

    lui_scan = {}
    for off in range(DECODE_START - text_start, DECODE_END - text_start, 4):
        if off < 0 or off + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off
        opcode = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        imm = w & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000

        if opcode == 15:  # LUI
            lui_scan[rt] = imm << 16

        if opcode in (35, 43):  # LW / SW
            if rs in lui_scan:
                effective = (lui_scan[rs] + simm) & 0xFFFFFFFF
                op_name = "LW" if opcode == 35 else "SW"
                dis = decode(addr, w)
                region = "RAM" if (effective >> 24) == 0x80 else "MMIO" if (effective >> 24) in (0xBF, 0x1F) else "???"
                print(f"  {addr:#010x}: {dis:45s} ; {op_name} {rn(rt)} @ {effective:#010x} ({region})")

    # ================================================================
    # ANALYSIS: Branch structure (control flow)
    # ================================================================
    print(f"\n{'='*100}")
    print(f"  BRANCH / JUMP ANALYSIS")
    print(f"{'='*100}\n")

    for off in range(DECODE_START - text_start, DECODE_END - text_start, 4):
        if off < 0 or off + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, off)[0]
        addr = text_start + off
        opcode = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        imm = w & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000
        func = w & 0x3F

        is_branch = False
        kind = ""
        target_addr = 0

        if opcode in (4, 5, 6, 7):  # BEQ/BNE/BLEZ/BGTZ
            is_branch = True
            target_addr = addr + 4 + (simm << 2)
            names = {4: "BEQ", 5: "BNE", 6: "BLEZ", 7: "BGTZ"}
            kind = names[opcode]
        if opcode == 1:  # BLTZ/BGEZ
            is_branch = True
            target_addr = addr + 4 + (simm << 2)
            kind = "BLTZ" if rt == 0 else "BGEZ"
        if opcode == 2:  # J
            is_branch = True
            target_addr = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            kind = "J"
        if opcode == 3:  # JAL
            is_branch = True
            target_addr = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            kind = "JAL"
        if opcode == 0 and func == 8:  # JR
            is_branch = True
            target_addr = 0
            kind = f"JR {rn(rs)}"

        if is_branch:
            dis = decode(addr, w)
            direction = ""
            if target_addr:
                if target_addr < addr:
                    direction = " (backward = loop)"
                elif target_addr > addr:
                    direction = " (forward = skip)"
                if DECODE_START <= target_addr < DECODE_END:
                    direction += f"  [within VSync: offset +{target_addr - DECODE_START:#x}]"
                else:
                    direction += f"  [outside VSync]"
            print(f"  {addr:#010x}: {dis:45s} -> {target_addr:#010x}{direction}")


if __name__ == "__main__":
    main()
