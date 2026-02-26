#!/usr/bin/env python3
"""
find_drawsync.py — Extract PSX-EXE from an ISO and search for DrawSync polling loops.

DrawSync(0) in PSn00bSDK typically:
  1. Loads a "GPU busy" byte via LBU
  2. Loops back if non-zero (BNE/BNEZ)
  3. Has a timeout counter (~1M iterations) decremented with ADDIU reg, reg, -1

We look for tight loops containing LBU + branch-back + counter patterns.
"""

import struct
import sys
import os

from mips_decode import REG_NAMES, decode_instr


# ── ISO / PSX-EXE extraction ─────────────────────────────────────────────────

def extract_psx_exe_from_iso(iso_path):
    """Extract the PSX-EXE binary from a Mode-2 or Mode-1 ISO image."""
    data = open(iso_path, "rb").read()
    iso_size = len(data)
    print(f"[*] ISO size: {iso_size} bytes ({iso_size // 1024} KB)")

    # Try to find PSX-EXE header directly in the data
    # PSX-EXE starts with "PS-X EXE" (8 bytes)
    marker = b"PS-X EXE"
    positions = []
    offset = 0
    while True:
        pos = data.find(marker, offset)
        if pos == -1:
            break
        positions.append(pos)
        offset = pos + 1

    if not positions:
        print("[!] No PS-X EXE marker found in ISO!")
        sys.exit(1)

    print(f"[*] Found {len(positions)} PS-X EXE marker(s) at offset(s): {[f'0x{p:X}' for p in positions]}")

    # Use the first PS-X EXE marker
    exe_header_offset = positions[0]

    # PSX-EXE header is 2048 bytes (0x800)
    header = data[exe_header_offset:exe_header_offset + 0x800]
    if len(header) < 0x800:
        print("[!] Not enough data for EXE header")
        sys.exit(1)

    # Parse header fields
    # 0x00: "PS-X EXE" (8 bytes)
    # 0x10: initial PC
    # 0x14: initial GP
    # 0x18: load address (destination in RAM)
    # 0x1C: file size (text section size, i.e., actual code+data length)
    # ...
    initial_pc = struct.unpack_from("<I", header, 0x10)[0]
    initial_gp = struct.unpack_from("<I", header, 0x14)[0]
    load_addr  = struct.unpack_from("<I", header, 0x18)[0]
    file_size  = struct.unpack_from("<I", header, 0x1C)[0]

    print(f"[*] EXE Header:")
    print(f"    Initial PC:   0x{initial_pc:08X}")
    print(f"    Initial GP:   0x{initial_gp:08X}")
    print(f"    Load Address: 0x{load_addr:08X}")
    print(f"    Text Size:    0x{file_size:X} ({file_size} bytes)")

    # The code follows immediately after the 2048-byte header
    code_start = exe_header_offset + 0x800
    code = data[code_start:code_start + file_size]
    if len(code) < file_size:
        print(f"[!] Warning: only got {len(code)} bytes of code (expected {file_size})")

    print(f"[*] Extracted {len(code)} bytes of code starting at ISO offset 0x{code_start:X}")
    return code, load_addr, initial_pc


# ── Pattern search ────────────────────────────────────────────────────────────

def find_drawsync_patterns(code, base_addr):
    """Search for DrawSync-like tight polling loops."""
    n_words = len(code) // 4
    words = struct.unpack_from(f"<{n_words}I", code)

    print(f"\n[*] Scanning {n_words} instructions for DrawSync patterns...\n")

    candidates = []

    # Strategy 1: Find LBU followed by a backward branch (BNEZ/BNE) within a few instructions
    for i in range(n_words - 8):
        w = words[i]
        op = (w >> 26) & 0x3F

        if op != 0x24:  # LBU
            continue

        rs_lbu = (w >> 21) & 0x1F
        rt_lbu = (w >> 16) & 0x1F
        imm_lbu = w & 0xFFFF

        # Look ahead up to 6 instructions for a backward branch on the same register
        for j in range(1, min(7, n_words - i)):
            w2 = words[i + j]
            op2 = (w2 >> 26) & 0x3F

            is_backward_branch = False
            branch_reg = -1

            if op2 == 0x05:  # BNE
                branch_rs = (w2 >> 21) & 0x1F
                branch_rt = (w2 >> 16) & 0x1F
                simm = w2 & 0xFFFF
                if simm >= 0x8000:
                    simm -= 0x10000
                if simm < 0:  # backward branch
                    is_backward_branch = True
                    branch_reg = branch_rs if branch_rt == 0 else branch_rs

            if op2 == 0x04:  # BEQ (could be BEQZ skip pattern)
                pass

            if op2 == 0x01:  # REGIMM (BGEZ, BLTZ, etc.)
                pass

            if is_backward_branch and (branch_reg == rt_lbu or branch_rs == rt_lbu or branch_rt == rt_lbu):
                # Found LBU + backward BNE on same register
                pc_lbu = base_addr + i * 4
                pc_branch = base_addr + (i + j) * 4

                # Check for counter (ADDIU reg, reg, -1) nearby
                counter_info = None
                for k in range(max(0, i - 10), min(n_words, i + 15)):
                    wk = words[k]
                    opk = (wk >> 26) & 0x3F
                    if opk == 0x09:  # ADDIU
                        rsk = (wk >> 21) & 0x1F
                        rtk = (wk >> 16) & 0x1F
                        immk = wk & 0xFFFF
                        if rsk == rtk and immk == 0xFFFF:  # ADDIU reg, reg, -1
                            counter_info = (base_addr + k * 4, REG_NAMES[rtk])

                # Try to find the LUI that sets up the base address for the LBU
                lui_info = None
                for k in range(max(0, i - 20), i):
                    wk = words[k]
                    opk = (wk >> 26) & 0x3F
                    if opk == 0x0F:  # LUI
                        rtk = (wk >> 16) & 0x1F
                        if rtk == rs_lbu:
                            lui_imm = wk & 0xFFFF
                            full_addr = (lui_imm << 16) + (imm_lbu if imm_lbu < 0x8000 else imm_lbu - 0x10000)
                            lui_info = (base_addr + k * 4, full_addr)

                candidates.append({
                    "lbu_pc": pc_lbu,
                    "lbu_reg": REG_NAMES[rt_lbu],
                    "lbu_base": REG_NAMES[rs_lbu],
                    "lbu_offset": imm_lbu if imm_lbu < 0x8000 else imm_lbu - 0x10000,
                    "branch_pc": pc_branch,
                    "counter": counter_info,
                    "lui": lui_info,
                    "index": i,
                    "branch_index": i + j,
                })

    # Strategy 2: search for ADDIU reg, reg, -1 (countdown) near a backward BNEZ
    print(f"[*] Found {len(candidates)} LBU + backward-branch candidates\n")

    # Now filter / rank candidates
    best = []
    for c in candidates:
        score = 0
        if c["counter"]:
            score += 10  # Has countdown — strong DrawSync signal
        if c["lui"]:
            addr = c["lui"][1]
            if 0x80000000 <= addr <= 0x801FFFFF:
                score += 5  # Reasonable KSEG0 address
        if c["branch_pc"] - c["lbu_pc"] <= 16:
            score += 3  # Tight loop
        c["score"] = score
        best.append(c)

    best.sort(key=lambda x: -x["score"])

    for idx, c in enumerate(best[:10]):
        print(f"─── Candidate {idx+1} (score={c['score']}) ───")
        print(f"  LBU at 0x{c['lbu_pc']:08X}: LBU ${c['lbu_reg']}, {c['lbu_offset']}(${c['lbu_base']})")
        if c["lui"]:
            print(f"  LUI at 0x{c['lui'][0]:08X} → full address of polled byte: 0x{c['lui'][1]:08X}")
        print(f"  Branch at 0x{c['branch_pc']:08X}")
        if c["counter"]:
            print(f"  Counter ADDIU at 0x{c['counter'][0]:08X} (reg=${c['counter'][1]})")

        # Find function start: scan backwards for ADDIU $sp, $sp, -N (stack frame setup)
        func_start = None
        search_start = max(0, c["index"] - 80)
        for k in range(c["index"], search_start, -1):
            wk = words[k]
            opk = (wk >> 26) & 0x3F
            rsk = (wk >> 21) & 0x1F
            rtk = (wk >> 16) & 0x1F
            immk = wk & 0xFFFF
            if opk == 0x09 and rsk == 29 and rtk == 29:  # ADDIU $sp, $sp, -N
                if immk >= 0x8000:  # negative immediate
                    func_start = k
                    break

        if func_start is None:
            # Also try looking for JR $ra before current position (previous function end)
            for k in range(c["index"], search_start, -1):
                wk = words[k]
                if wk == 0x03E00008:  # JR $ra
                    # Function likely starts 2 instructions after (delay slot + first instr)
                    func_start = k + 2
                    break

        if func_start is not None:
            func_pc = base_addr + func_start * 4
            print(f"\n  Likely function entry: 0x{func_pc:08X}")
            print(f"  Disassembly (40 instructions from entry):")
            for di in range(40):
                if func_start + di >= n_words:
                    break
                dw = words[func_start + di]
                dpc = base_addr + (func_start + di) * 4
                dis = decode_instr(dw, dpc)
                marker = ""
                if dpc == c["lbu_pc"]:
                    marker = "  ◄── LBU (poll busy byte)"
                if dpc == c["branch_pc"]:
                    marker = "  ◄── backward branch (loop)"
                if c["counter"] and dpc == c["counter"][0]:
                    marker = "  ◄── counter decrement"
                print(f"    0x{dpc:08X}: {dw:08X}  {dis}{marker}")
        else:
            # Just dump around the LBU
            print(f"\n  Context (20 instructions around LBU):")
            start = max(0, c["index"] - 5)
            for di in range(20):
                if start + di >= n_words:
                    break
                dw = words[start + di]
                dpc = base_addr + (start + di) * 4
                dis = decode_instr(dw, dpc)
                print(f"    0x{dpc:08X}: {dw:08X}  {dis}")

        print()

    # Strategy 3: Also look for known GPU I/O port polling (0x1F801814 — GPU status register)
    print("\n─── Additional: Searching for GPU status register (0x1F801814) references ───")
    for i in range(n_words):
        w = words[i]
        op = (w >> 26) & 0x3F
        if op == 0x0F:  # LUI
            rt = (w >> 16) & 0x1F
            imm = w & 0xFFFF
            if imm == 0x1F80:
                pc = base_addr + i * 4
                # Check next few instrs for load from 0x1814
                for j in range(1, 8):
                    if i + j >= n_words:
                        break
                    w2 = words[i + j]
                    op2 = (w2 >> 26) & 0x3F
                    rs2 = (w2 >> 21) & 0x1F
                    if op2 in (0x23, 0x24, 0x25, 0x20, 0x21) and rs2 == rt:
                        imm2 = w2 & 0xFFFF
                        if imm2 == 0x1814:
                            rt2 = (w2 >> 16) & 0x1F
                            pc2 = base_addr + (i + j) * 4
                            print(f"  LUI+LW GPU_STAT at 0x{pc:08X}..0x{pc2:08X} (into ${REG_NAMES[rt2]})")
                            # Dump context
                            start = max(0, i - 2)
                            for di in range(25):
                                if start + di >= n_words:
                                    break
                                dw = words[start + di]
                                dpc = base_addr + (start + di) * 4
                                dis = decode_instr(dw, dpc)
                                print(f"    0x{dpc:08X}: {dw:08X}  {dis}")
                            print()

    # Strategy 4: Look for LUI 0x000F (loading ~0x000F4240 = 1,000,000 timeout)
    print("\n─── Searching for timeout constant ~1,000,000 (0x000F4240) ───")
    for i in range(n_words - 2):
        w = words[i]
        op = (w >> 26) & 0x3F
        if op == 0x0F:  # LUI
            imm = w & 0xFFFF
            if imm == 0x000F:
                rt = (w >> 16) & 0x1F
                # Check for ORI/ADDIU with 0x4240
                for j in range(1, 5):
                    if i + j >= n_words:
                        break
                    w2 = words[i + j]
                    op2 = (w2 >> 26) & 0x3F
                    rs2 = (w2 >> 21) & 0x1F
                    rt2 = (w2 >> 16) & 0x1F
                    imm2 = w2 & 0xFFFF
                    if op2 in (0x09, 0x0D) and rs2 == rt:  # ADDIU or ORI
                        if imm2 == 0x4240:
                            pc = base_addr + i * 4
                            val = (imm << 16) | imm2
                            print(f"  Found timeout constant {val} (0x{val:08X}) at 0x{pc:08X}")
                            # Dump context
                            start = max(0, i - 4)
                            for di in range(30):
                                if start + di >= n_words:
                                    break
                                dw = words[start + di]
                                dpc = base_addr + (start + di) * 4
                                dis = decode_instr(dw, dpc)
                                print(f"    0x{dpc:08X}: {dw:08X}  {dis}")
                            print()

    # Also search for smaller timeouts
    print("\n─── Searching for other large constants loaded via LUI + ORI/ADDIU ───")
    for i in range(n_words - 2):
        w = words[i]
        op = (w >> 26) & 0x3F
        if op == 0x0F:  # LUI
            imm = w & 0xFFFF
            rt = (w >> 16) & 0x1F
            if 0x0001 <= imm <= 0x0020:  # Moderate upper half (64K..2M range)
                for j in range(1, 4):
                    if i + j >= n_words:
                        break
                    w2 = words[i + j]
                    op2 = (w2 >> 26) & 0x3F
                    rs2 = (w2 >> 21) & 0x1F
                    rt2 = (w2 >> 16) & 0x1F
                    imm2 = w2 & 0xFFFF
                    if op2 in (0x09, 0x0D) and rs2 == rt and rt2 == rt:
                        val = (imm << 16) | imm2
                        if 100000 <= val <= 2000000:
                            pc = base_addr + i * 4
                            print(f"  Constant {val} at 0x{pc:08X} (${REG_NAMES[rt]})")

    return best


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    iso_path = "/psxrecomp/out/recompiled-demos-windows-latest/inputs/HELLOWLD.iso"
    if not os.path.exists(iso_path):
        # Try local path
        iso_path = "out/recompiled-demos-windows-latest/inputs/HELLOWLD.iso"

    print(f"[*] Analyzing {iso_path}")
    code, base_addr, entry_pc = extract_psx_exe_from_iso(iso_path)
    candidates = find_drawsync_patterns(code, base_addr)

    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    if candidates:
        top = candidates[0]
        print(f"  Top DrawSync candidate (score={top['score']}):")
        print(f"    LBU polls: ${top['lbu_reg']} from {top['lbu_offset']}(${top['lbu_base']})")
        if top["lui"]:
            print(f"    Polled address: 0x{top['lui'][1]:08X}")
        if top["counter"]:
            print(f"    Counter reg: ${top['counter'][1]} at 0x{top['counter'][0]:08X}")
        print(f"    Loop branch at: 0x{top['branch_pc']:08X}")
    else:
        print("  No candidates found!")


if __name__ == "__main__":
    main()
