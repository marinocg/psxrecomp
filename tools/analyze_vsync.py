#!/usr/bin/env python3
"""Analyze PSX demo binaries to find VSync/DrawSync implementation patterns."""
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

    if op == 0:  # SPECIAL
        if func == 0: return f"SLL r{rd},r{rt},{sa}"
        if func == 2: return f"SRL r{rd},r{rt},{sa}"
        if func == 3: return f"SRA r{rd},r{rt},{sa}"
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
        return f"REGIMM rt={rt}"
    if op == 2: return f"J 0x{(addr & 0xF0000000) | target:08x}"
    if op == 3: return f"JAL 0x{(addr & 0xF0000000) | target:08x}"
    if op == 4: return f"BEQ r{rs},r{rt},{simm}"
    if op == 5: return f"BNE r{rs},r{rt},{simm}"
    if op == 6: return f"BLEZ r{rs},{simm}"
    if op == 7: return f"BGTZ r{rs},{simm}"
    if op == 8: return f"ADDI r{rt},r{rs},{simm}"
    if op == 9: return f"ADDIU r{rt},r{rs},{simm}"
    if op == 0xA: return f"SLTI r{rt},r{rs},{simm}"
    if op == 0xB: return f"SLTIU r{rt},r{rs},{simm}"
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
    """Extract PSX-EXE code from an ISO or BIN image."""
    with open(bin_path, "rb") as f:
        data = f.read()

    # Detect sector size: BIN=2352 bytes, ISO=2048 bytes
    # Check if first bytes are CD sync pattern (BIN format)
    is_bin = len(data) >= 16 and data[:12] == bytes([0x00, 0xFF, 0xFF, 0xFF,
                                                      0xFF, 0xFF, 0xFF, 0xFF,
                                                      0xFF, 0xFF, 0xFF, 0x00])
    if is_bin:
        sector_size = 2352
        data_offset = 24  # Skip sync(12) + header(4) + subheader(8)
        data_len = 2048
    else:
        sector_size = 2048
        data_offset = 0
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
            code = bytes(code[:text_size])
            return text_start, text_size, entry, code

    return None, None, None, None


def find_functions_calling(code, text_start, target_addr):
    """Find all JAL instructions targeting a specific address."""
    results = []
    for i in range(0, len(code) - 3, 4):
        w = struct.unpack_from("<I", code, i)[0]
        op = (w >> 26) & 0x3F
        if op == 3:  # JAL
            target = ((text_start + i) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
            if target == target_addr:
                results.append(text_start + i)
    return results


def main():
    iso_dir = "/psxrecomp/out/recompiled-demos-windows-latest/inputs"
    demos = {
        "HELLOWLD": {"spin_pc": 0x800107a8},
        "GPUTEST":  {"spin_pc": 0x800107c4},
        "MEMTEST":  {"spin_pc": 0x800107d4},
        "ADVHELLO": {"spin_pc": 0x80010bb4},
    }

    for name, info in demos.items():
        bins = glob.glob(os.path.join(iso_dir, f"**/*{name}*.iso"), recursive=True)
        if not bins:
            bins = glob.glob(os.path.join(iso_dir, f"**/*{name}*.bin"), recursive=True)
        if not bins:
            print(f"\n{name}: No ISO/BIN file found")
            continue

        text_start, text_size, entry, code = extract_exe_code(bins[0])
        if code is None:
            print(f"\n{name}: Could not extract EXE")
            continue

        print(f"\n{'='*70}")
        print(f"  {name}: text=0x{text_start:08x} size={text_size} entry=0x{entry:08x}")
        print(f"  Spin PC: 0x{info['spin_pc']:08x}")
        print(f"{'='*70}")

        # Find all MMIO access points (LUI rX, 0xBF80)
        mmio_addrs = []
        for i in range(0, len(code) - 3, 4):
            w = struct.unpack_from("<I", code, i)[0]
            if (w >> 26) & 0x3F == 0xF and (w & 0xFFFF) == 0xBF80:
                mmio_addrs.append(text_start + i)

        print(f"\n  MMIO access points (LUI rX,0xBF80): {len(mmio_addrs)}")

        # Look specifically for GPU status reads (0x1F801814)
        # Pattern: LUI rX, 0xBF80 ... LW rY, 0x1814(rX)
        print("\n  GPU status (0x1F801814) read locations:")
        for i in range(0, len(code) - 3, 4):
            w = struct.unpack_from("<I", code, i)[0]
            op = (w >> 26) & 0x3F
            if op == 0x23:  # LW
                imm = w & 0xFFFF
                if imm == 0x1814:
                    addr = text_start + i
                    print(f"    0x{addr:08x}: LW r{(w>>16)&0x1F}, 0x1814(r{(w>>21)&0x1F})")

        # Look for timer reads (0x1F801110 = timer1 counter)
        print("\n  Timer1 counter (0x1F801110) read locations:")
        for i in range(0, len(code) - 3, 4):
            w = struct.unpack_from("<I", code, i)[0]
            op = (w >> 26) & 0x3F
            if op == 0x23:  # LW
                imm = w & 0xFFFF
                if imm == 0x1110:
                    addr = text_start + i
                    print(f"    0x{addr:08x}: LW r{(w>>16)&0x1F}, 0x1110(r{(w>>21)&0x1F})")

        # Look for I_STAT reads (0x1F801070)
        print("\n  I_STAT (0x1F801070) read locations:")
        for i in range(0, len(code) - 3, 4):
            w = struct.unpack_from("<I", code, i)[0]
            op = (w >> 26) & 0x3F
            if op == 0x23:  # LW
                imm = w & 0xFFFF
                if imm == 0x1070:
                    addr = text_start + i
                    print(f"    0x{addr:08x}: LW r{(w>>16)&0x1F}, 0x1070(r{(w>>21)&0x1F})")

        # Decode the function containing the spin PC
        spin_off = info['spin_pc'] - text_start
        # Find the function start (scan backwards for ADDIU r29,r29,<negative>)
        func_start = spin_off
        for i in range(spin_off, max(spin_off - 0x200, 0), -4):
            w = struct.unpack_from("<I", code, i)[0]
            op = (w >> 26) & 0x3F
            rs = (w >> 21) & 0x1F
            rt = (w >> 16) & 0x1F
            imm = w & 0xFFFF
            # Look for ADDIU sp,sp,<negative> (function prologue)
            if op == 9 and rs == 29 and rt == 29 and imm >= 0x8000:
                func_start = i
                break

        print(f"\n  Function around spin PC (0x{text_start + func_start:08x} - ...):")
        # Decode from function start to function start + 200 instructions
        for j in range(80):
            ioff = func_start + j * 4
            if ioff + 4 <= len(code):
                w = struct.unpack_from("<I", code, ioff)[0]
                a = text_start + ioff
                marker = " <<< SPIN" if a == info['spin_pc'] else ""
                print(f"    0x{a:08x}: 0x{w:08x}  {decode(a, w)}{marker}")

        # Find who calls the spin function
        spin_func_addr = text_start + func_start
        callers = find_functions_calling(code, text_start, spin_func_addr)
        if callers:
            print(f"\n  Callers of 0x{spin_func_addr:08x}:")
            for caller in callers:
                print(f"    JAL at 0x{caller:08x}")
                # Decode a few instructions around the caller
                caller_off = caller - text_start
                for j in range(-2, 6):
                    ioff = caller_off + j * 4
                    if 0 <= ioff < len(code) - 3:
                        w = struct.unpack_from("<I", code, ioff)[0]
                        a = text_start + ioff
                        print(f"      0x{a:08x}: {decode(a, w)}")


if __name__ == "__main__":
    main()
