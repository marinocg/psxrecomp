#!/usr/bin/env python3
"""Find callers of the event dispatch function (0x80010398) in HELLOWLD.iso
and analyze the VSync loop that calls it repeatedly."""
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
            8:  f"JR {rn(rs)}",
            9:  f"JALR {rn(rd)},{rn(rs)}",
            0x10: f"MFHI {rn(rd)}",
            0x12: f"MFLO {rn(rd)}",
            0x18: f"MULT {rn(rs)},{rn(rt)}",
            0x19: f"MULTU {rn(rs)},{rn(rt)}",
            0x1a: f"DIV {rn(rs)},{rn(rt)}",
            0x1b: f"DIVU {rn(rs)},{rn(rt)}",
            0x20: f"ADD {rn(rd)},{rn(rs)},{rn(rt)}",
            0x21: f"ADDU {rn(rd)},{rn(rs)},{rn(rt)}",
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
        35: lambda: f"LW {rn(rt)},{simm}({rn(rs)})",
        36: lambda: f"LBU {rn(rt)},{simm}({rn(rs)})",
        37: lambda: f"LHU {rn(rt)},{simm}({rn(rs)})",
        40: lambda: f"SB {rn(rt)},{simm}({rn(rs)})",
        41: lambda: f"SH {rn(rt)},{simm}({rn(rs)})",
        43: lambda: f"SW {rn(rt)},{simm}({rn(rs)})",
    }
    if opcode in names:
        return names[opcode]()
    return f"op={opcode} raw={w:#010x}"


def extract_exe_from_iso(iso_path):
    """Extract PS-X EXE code from an ISO image (2048-byte sectors)."""
    with open(iso_path, "rb") as f:
        data = f.read()

    sector_size = 2048
    data_offset = 0
    data_len = 2048

    # Check for BIN format (2352-byte sectors with CD sync pattern)
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

            print(f"Found PS-X EXE at sector {sector} (offset {offset:#x})")
            print(f"  Load addr: {text_start:#010x}")
            print(f"  Text size: {text_size} ({text_size:#x})")
            print(f"  Entry:     {entry:#010x}")

            # Extract code from sectors following the header
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


def find_all_jal_to(code, text_start, target_addr):
    """Find all JAL instructions targeting a specific address."""
    results = []
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        if op == 3:  # JAL
            addr = text_start + i
            jal_target = (addr & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            if jal_target == target_addr:
                results.append(addr)
    return results


def disassemble_range(code, text_start, start_addr, count, highlights=None):
    """Disassemble 'count' instructions starting at start_addr."""
    if highlights is None:
        highlights = {}
    off = start_addr - text_start
    for i in range(count):
        ioff = off + i * 4
        if ioff < 0 or ioff + 4 > len(code):
            continue
        w = struct.unpack_from("<I", code, ioff)[0]
        addr = text_start + ioff
        marker = highlights.get(addr, "")
        print(f"  {addr:#010x}: {w:08x}  {decode(addr, w):40s} {marker}")


def find_function_start(code, text_start, addr):
    """Scan backwards from addr for ADDIU sp,sp,<negative> (function prologue)."""
    off = addr - text_start
    for i in range(off, max(off - 0x400, 0), -4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        imm = w & 0xFFFF
        if op == 9 and rs == 29 and rt == 29 and imm >= 0x8000:
            return text_start + i
    return addr


def main():
    iso_path = "/Users/marinocg/trash/psxrecomp/out/recompiled-demos-windows-latest/inputs/HELLOWLD.iso"

    EVENT_DISPATCH = 0x80010398
    VSYNC_RANGE = (0x80010500, 0x80010600)
    DRAWSYNC_APPROX = 0x800107a0

    print("=" * 78)
    print("  HELLOWLD.iso - Finding callers of event dispatch (0x80010398)")
    print("=" * 78)

    text_start, text_size, entry, code = extract_exe_from_iso(iso_path)
    if code is None:
        print("ERROR: Could not extract PS-X EXE from ISO")
        sys.exit(1)

    text_end = text_start + text_size
    print(f"Code range: {text_start:#010x} - {text_end:#010x}\n")

    # ================================================================
    # PART 1: Find ALL JAL instructions targeting 0x80010398
    # ================================================================
    print("=" * 78)
    print(f"  PART 1: All JAL instructions targeting {EVENT_DISPATCH:#010x}")
    print("=" * 78)

    callers = find_all_jal_to(code, text_start, EVENT_DISPATCH)
    print(f"\nFound {len(callers)} JAL(s) to {EVENT_DISPATCH:#010x}:\n")

    for caller_addr in callers:
        print(f"\n--- JAL at {caller_addr:#010x} ---")
        # Show 15 instructions before and 15 after the JAL
        start = caller_addr - 15 * 4
        highlights = {caller_addr: " <<< JAL 0x80010398"}
        disassemble_range(code, text_start, start, 31, highlights)

    # ================================================================
    # PART 2: Disassemble the event dispatch function itself
    # ================================================================
    print("\n" + "=" * 78)
    print(f"  PART 2: Event dispatch function at {EVENT_DISPATCH:#010x}")
    print("=" * 78)

    func_start = find_function_start(code, text_start, EVENT_DISPATCH)
    print(f"\nFunction starts at {func_start:#010x}:")
    disassemble_range(code, text_start, func_start, 60,
                      {EVENT_DISPATCH: " <<< EVENT_DISPATCH entry"})

    # ================================================================
    # PART 3: VSync function area (0x80010500-0x80010600)
    # ================================================================
    print("\n" + "=" * 78)
    print(f"  PART 3: VSync area ({VSYNC_RANGE[0]:#010x} - {VSYNC_RANGE[1]:#010x})")
    print("=" * 78)

    # Find the function containing VSync (scan back from 0x80010518)
    vsync_approx = 0x80010518
    vsync_func_start = find_function_start(code, text_start, vsync_approx)
    print(f"\nVSync function starts at {vsync_func_start:#010x}")
    print("Disassembling VSync and surrounding code:")

    # Show plenty of context - 80 instructions from function start
    highlights = {}
    # Find all JALs in this range
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        addr = text_start + i
        if VSYNC_RANGE[0] - 0x80 <= addr <= VSYNC_RANGE[1] + 0x80:
            if op == 3:  # JAL
                jal_target = (addr & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                highlights[addr] = f" <<< JAL {jal_target:#010x}"

    disassemble_range(code, text_start, vsync_func_start, 100, highlights)

    # ================================================================
    # PART 4: DrawSync function area
    # ================================================================
    print("\n" + "=" * 78)
    print(f"  PART 4: DrawSync area (near {DRAWSYNC_APPROX:#010x})")
    print("=" * 78)

    drawsync_func_start = find_function_start(code, text_start, DRAWSYNC_APPROX)
    print(f"\nDrawSync function starts at {drawsync_func_start:#010x}")

    highlights = {}
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        addr = text_start + i
        if drawsync_func_start <= addr <= drawsync_func_start + 0x200:
            if op == 3:
                jal_target = (addr & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                highlights[addr] = f" <<< JAL {jal_target:#010x}"

    disassemble_range(code, text_start, drawsync_func_start, 80, highlights)

    # ================================================================
    # PART 5: Main function / callers of VSync and DrawSync
    # ================================================================
    print("\n" + "=" * 78)
    print("  PART 5: Callers of VSync and DrawSync")
    print("=" * 78)

    # Find callers of VSync function
    vsync_callers = find_all_jal_to(code, text_start, vsync_func_start)
    print(f"\nCallers of VSync ({vsync_func_start:#010x}): {len(vsync_callers)}")
    for c in vsync_callers:
        print(f"  JAL at {c:#010x}")
        # Show context
        func_s = find_function_start(code, text_start, c)
        print(f"    (in function starting at {func_s:#010x})")
        disassemble_range(code, text_start, c - 8 * 4, 20,
                          {c: " <<< JAL VSync"})
        print()

    # Find callers of DrawSync function
    drawsync_callers = find_all_jal_to(code, text_start, drawsync_func_start)
    print(f"\nCallers of DrawSync ({drawsync_func_start:#010x}): {len(drawsync_callers)}")
    for c in drawsync_callers:
        print(f"  JAL at {c:#010x}")
        func_s = find_function_start(code, text_start, c)
        print(f"    (in function starting at {func_s:#010x})")
        disassemble_range(code, text_start, c - 8 * 4, 20,
                          {c: " <<< JAL DrawSync"})
        print()

    # ================================================================
    # PART 6: Entry point and main loop
    # ================================================================
    print("\n" + "=" * 78)
    print(f"  PART 6: Entry point ({entry:#010x}) and main function")
    print("=" * 78)

    entry_func_start = find_function_start(code, text_start, entry)
    print(f"\nEntry starts at {entry_func_start:#010x}:")
    disassemble_range(code, text_start, entry_func_start, 80)

    # ================================================================
    # PART 7: Full main loop function decode (0x80010058-0x80010260)
    # ================================================================
    print("\n" + "=" * 78)
    print("  PART 7: Full main loop function (0x80010058 - 0x80010268)")
    print("=" * 78)
    disassemble_range(code, text_start, 0x80010058, 130)

    # ================================================================
    # PART 8: Decode the DrawSync(0) function at 0x80010798
    # ================================================================
    print("\n" + "=" * 78)
    print("  PART 8: DrawSync(0) path starting at 0x80010798")
    print("=" * 78)
    print("\nNote: DrawSync entry at 0x80010798 (a0!=0 branch at top)")
    print("  When a0==0: falls through to 0x800107a0")
    print("  The loop at 0x800107a8-0x800107c0 polls [0x800176ad] (event count)")
    print("  This is the same byte the event dispatcher writes to!")

    # Find all JALR instructions in the VSync function
    print("\n" + "=" * 78)
    print("  PART 9: All indirect calls (JALR) in VSync function")
    print("=" * 78)
    for off in range(0x80010518 - text_start, 0x80010698 - text_start, 4):
        if off + 4 <= len(code):
            w = struct.unpack_from("<I", code, off)[0]
            if (w >> 26) & 0x3F == 0 and (w & 0x3F) == 9:  # JALR
                addr = text_start + off
                rs = (w >> 21) & 0x1F
                print(f"\n  JALR at {addr:#010x}: JALR ra,{rn(rs)}")
                print(f"    Register {rn(rs)} loaded from: (check above context)")
                # Show 5 instructions of context
                disassemble_range(code, text_start, addr - 5*4, 12)


if __name__ == "__main__":
    main()
