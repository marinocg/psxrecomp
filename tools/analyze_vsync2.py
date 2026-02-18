#!/usr/bin/env python3
"""Find VSync function in PSX demos by looking for Timer1 counter reads."""
import struct
import os
import glob


def decode(addr, word):
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    imm = word & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    target = (word & 0x03FFFFFF) << 2
    sa = (word >> 6) & 0x1F
    func = word & 0x3F

    if op == 0:
        if func == 0: return f"SLL r{rd},r{rt},{sa}"
        if func == 2: return f"SRL r{rd},r{rt},{sa}"
        if func == 8: return f"JR r{rs}"
        if func == 9: return f"JALR r{rd},r{rs}"
        if func == 0x21: return f"ADDU r{rd},r{rs},r{rt}"
        if func == 0x23: return f"SUBU r{rd},r{rs},r{rt}"
        if func == 0x24: return f"AND r{rd},r{rs},r{rt}"
        if func == 0x25: return f"OR r{rd},r{rs},r{rt}"
        if func == 0x2A: return f"SLT r{rd},r{rs},r{rt}"
        if func == 0x2B: return f"SLTU r{rd},r{rs},r{rt}"
        return f"SPECIAL func=0x{func:x}"
    if op == 1:
        if rt == 0: return f"BLTZ r{rs},{simm}"
        if rt == 1: return f"BGEZ r{rs},{simm}"
    if op == 2: return f"J 0x{(addr & 0xF0000000) | target:08x}"
    if op == 3: return f"JAL 0x{(addr & 0xF0000000) | target:08x}"
    if op == 4: return f"BEQ r{rs},r{rt},{simm}"
    if op == 5: return f"BNE r{rs},r{rt},{simm}"
    if op == 6: return f"BLEZ r{rs},{simm}"
    if op == 7: return f"BGTZ r{rs},{simm}"
    if op == 9: return f"ADDIU r{rt},r{rs},{simm}"
    if op == 0xC: return f"ANDI r{rt},r{rs},0x{imm:04x}"
    if op == 0xD: return f"ORI r{rt},r{rs},0x{imm:04x}"
    if op == 0xF: return f"LUI r{rt},0x{imm:04x}"
    if op == 0x20: return f"LB r{rt},{simm}(r{rs})"
    if op == 0x21: return f"LH r{rt},{simm}(r{rs})"
    if op == 0x23: return f"LW r{rt},{simm}(r{rs})"
    if op == 0x24: return f"LBU r{rt},{simm}(r{rs})"
    if op == 0x25: return f"LHU r{rt},{simm}(r{rs})"
    if op == 0x28: return f"SB r{rt},{simm}(r{rs})"
    if op == 0x29: return f"SH r{rt},{simm}(r{rs})"
    if op == 0x2B: return f"SW r{rt},{simm}(r{rs})"
    return f"op={op} raw=0x{word:08x}"


def extract_exe_code(bin_path):
    with open(bin_path, "rb") as f:
        data = f.read()
    is_bin = len(data) >= 16 and data[:12] == bytes([0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00])
    sector_size = 2352 if is_bin else 2048
    data_offset = 24 if is_bin else 0
    data_len = 2048

    for sector in range(16, min(len(data) // sector_size, 200)):
        offset = sector * sector_size + data_offset
        if offset + 8 <= len(data) and data[offset:offset+8] == b"PS-X EXE":
            text_start = struct.unpack_from("<I", data, offset + 0x18)[0]
            text_size = struct.unpack_from("<I", data, offset + 0x1C)[0]
            entry = struct.unpack_from("<I", data, offset + 0x10)[0]
            code = bytearray()
            text_sectors = (text_size + data_len - 1) // data_len
            for s in range(text_sectors + 2):
                sec_num = sector + 1 + s
                sec_off = sec_num * sector_size + data_offset
                if sec_off + data_len <= len(data):
                    code.extend(data[sec_off:sec_off+data_len])
            return text_start, text_size, entry, bytes(code[:text_size])
    return None, None, None, None


def main():
    iso_dir = "/psxrecomp/out/recompiled-demos-windows-latest/inputs"
    
    # Just analyze HELLOWLD in detail
    bins = glob.glob(os.path.join(iso_dir, "HELLOWLD.iso"))
    if not bins:
        print("No HELLOWLD.iso found")
        return

    text_start, text_size, entry, code = extract_exe_code(bins[0])
    if code is None:
        print("Could not extract EXE")
        return

    print(f"HELLOWLD: text=0x{text_start:08x} size={text_size} entry=0x{entry:08x}")

    # Find Timer1 counter reads (0x1110 offset from 0xBF80xxxx)
    # This is how PSn00bSDK VSync works - reads Timer1 (hblank counter)
    print("\n=== Functions that read Timer1 counter (0xBF801110) ===")
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        if op == 0x23 and (w & 0xFFFF) == 0x1110:
            addr = text_start + i
            # Decode 40 instructions around this point
            print(f"\n  Timer1 read at 0x{addr:08x}:")
            start = max(0, i - 20*4)
            for j in range(60):
                ioff = start + j * 4
                if 0 <= ioff < len(code) - 3:
                    w2 = struct.unpack_from("<I", code, ioff)[0]
                    a = text_start + ioff
                    marker = " <<<" if a == addr else ""
                    print(f"    0x{a:08x}: {decode(a, w2)}{marker}")

    # Find the main loop by looking at the entry point
    entry_off = entry - text_start
    print(f"\n=== Entry point at 0x{entry:08x} ===")
    for j in range(20):
        ioff = entry_off + j * 4
        if 0 <= ioff < len(code) - 3:
            w = struct.unpack_from("<I", code, ioff)[0]
            a = text_start + ioff
            print(f"  0x{a:08x}: {decode(a, w)}")

    # Trace calls from entry to find the main function 
    print(f"\n=== JAL calls from entry region ===")
    for j in range(20):
        ioff = entry_off + j * 4
        if 0 <= ioff < len(code) - 3:
            w = struct.unpack_from("<I", code, ioff)[0]
            op = (w >> 26) & 0x3F
            if op == 3:  # JAL
                target = (entry & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
                # Decode 40 instructions at target
                toff = target - text_start
                if 0 <= toff < len(code) - 3:
                    print(f"\n  JAL target 0x{target:08x}:")
                    for k in range(40):
                        kioff = toff + k * 4
                        if 0 <= kioff < len(code) - 3:
                            w2 = struct.unpack_from("<I", code, kioff)[0]
                            a = text_start + kioff
                            print(f"    0x{a:08x}: {decode(a, w2)}")

    # Find the "drawing in progress" flag write (SB r3, offset(r12) where r3=1)
    # This tells us the flag's RAM address
    print(f"\n=== DrawSync flag locations (SB to offset from 0x8001xxxx) ===")
    flag_addrs = set()
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        if op == 0x28:  # SB
            rs = (w >> 21) & 0x1F
            rt = (w >> 16) & 0x1F
            imm = w & 0xFFFF
            simm = imm if imm < 0x8000 else imm - 0x10000
            # Look for the pattern: SB to the DrawSync flag
            # The flag address for HELLOWLD should be 0x800176AD  
            if imm == 0x76AD or simm == 30381:
                addr = text_start + i
                flag_addrs.add(addr)
                print(f"  0x{addr:08x}: SB r{rt},{simm}(r{rs})")


if __name__ == "__main__":
    main()
