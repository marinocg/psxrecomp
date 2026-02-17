#!/usr/bin/env python3
"""Phase 3: Deep analysis of the unsupported CALL targets.

Key insight from Phase 2: ALL targets are mid-function addresses reached
via JALR (indirect call), not JAL. The recompiler's JAL scan won't find
them. The question is: does the recompiler's code/data segmentation and
function boundary analysis recognize the code at those addresses?
"""
import struct
import os

REG = [
    "zero","at","v0","v1","a0","a1","a2","a3",
    "t0","t1","t2","t3","t4","t5","t6","t7",
    "s0","s1","s2","s3","s4","s5","s6","s7",
    "t8","t9","k0","k1","gp","sp","fp","ra",
]

KNOWN_HW = {
    0x1F801070: "I_STAT",       0x1F801074: "I_MASK",
    0x1F801810: "GP0",          0x1F801814: "GP1 (GPUSTAT)",
    0x1F8010A0: "DMA2_MADR",    0x1F8010A4: "DMA2_BCR",
    0x1F8010A8: "DMA2_CHCR",    0x1F8010F0: "DPCR",
    0x1F8010F4: "DICR",
}

# PSX SDK function signatures (instruction patterns)
PSX_SDK_SIGS = {
    # LoadImage: GPU DMA transfer from CPU to VRAM
    "LoadImage": [
        ("LUI", "0xbf80"),   # Load I/O base
        ("LW", "1814"),       # Read GPUSTAT
        ("SW", "1810"),       # Write GP0
        ("SW", "10a0"),       # DMA2 base addr
        ("SW", "10a4"),       # DMA2 block control
        ("SW", "10a8"),       # DMA2 channel control
    ],
    "DrawSync": [
        ("LUI", "0xbf80"),
        ("LW", "1814"),       # Read GPUSTAT
        ("AND",),             # Mask bits
    ],
    "VSync": [
        ("LUI", "0xbf80"),
        ("LW", "1814"),       # GPUSTAT
    ],
}

def decode(addr, w):
    opcode = (w >> 26) & 0x3F
    rs = (w >> 21) & 0x1F
    rt = (w >> 16) & 0x1F
    rd = (w >> 11) & 0x1F
    sa = (w >> 6) & 0x1F
    func = w & 0x3F
    imm = w & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    jt = ((addr + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
    if w == 0: return "NOP"
    if opcode == 0:
        m = {0:"SLL",2:"SRL",3:"SRA",8:"JR",9:"JALR",0xC:"SYSCALL",0xD:"BREAK",
             0x10:"MFHI",0x12:"MFLO",0x18:"MULT",0x19:"MULTU",0x1A:"DIV",0x1B:"DIVU",
             0x20:"ADD",0x21:"ADDU",0x23:"SUBU",0x24:"AND",0x25:"OR",0x26:"XOR",
             0x27:"NOR",0x2A:"SLT",0x2B:"SLTU"}
        if func in (8,9):
            return f"{m[func]} {REG[rs]}" if func==8 else f"JALR {REG[rd]},{REG[rs]}"
        if func in (0,2,3): return f"{m.get(func,'?')} {REG[rd]},{REG[rt]},{sa}"
        if func in m: return f"{m[func]} {REG[rd]},{REG[rs]},{REG[rt]}"
        return f"SPECIAL {func:#x}"
    if opcode==2: return f"J {jt:#010x}"
    if opcode==3: return f"JAL {jt:#010x}"
    tbl = {4:"BEQ",5:"BNE",6:"BLEZ",7:"BGTZ",9:"ADDIU",0xA:"SLTI",0xB:"SLTIU",
           0xC:"ANDI",0xD:"ORI",0xF:"LUI",0x20:"LB",0x21:"LH",0x23:"LW",0x24:"LBU",
           0x25:"LHU",0x28:"SB",0x29:"SH",0x2B:"SW"}
    if opcode in (4,5):
        bt=addr+4+(simm<<2)
        return f"{tbl[opcode]} {REG[rs]},{REG[rt]},{bt:#010x}"
    if opcode in (6,7):
        bt=addr+4+(simm<<2)
        return f"{tbl[opcode]} {REG[rs]},{bt:#010x}"
    if opcode==0xF: return f"LUI {REG[rt]},{imm:#06x}"
    if opcode in (9,0xA,0xB): return f"{tbl[opcode]} {REG[rt]},{REG[rs]},{simm}"
    if opcode in (0xC,0xD): return f"{tbl[opcode]} {REG[rt]},{REG[rs]},{imm:#06x}"
    if opcode in tbl and opcode >= 0x20:
        return f"{tbl[opcode]} {REG[rt]},{simm}({REG[rs]})"
    if opcode == 0x10:
        if rs==0: return f"MFC0 {REG[rt]},cop0r{rd}"
        if rs==4: return f"MTC0 {REG[rt]},cop0r{rd}"
        if rs>=0x10 and func==0x10: return "RFE"
        return f"COP0 {rs},{rt},{rd}"
    if opcode == 1:
        nm = {0:"BLTZ",1:"BGEZ"}.get(rt,f"REGIMM{rt}")
        return f"{nm} {REG[rs]},{addr+4+(simm<<2):#010x}"
    return f"({w:#010x})"

def extract_exe(path):
    with open(path,"rb") as f: data=f.read()
    off = data.find(b"PS-X EXE")
    if off < 0: return None
    return {
        "entry": struct.unpack_from("<I",data,off+0x10)[0],
        "load":  struct.unpack_from("<I",data,off+0x18)[0],
        "size":  struct.unpack_from("<I",data,off+0x1C)[0],
        "prog":  data[off+0x800 : off+0x800+struct.unpack_from("<I",data,off+0x1C)[0]],
    }

def rw(exe, va):
    o = va - exe["load"]
    if 0 <= o and o+4 <= len(exe["prog"]):
        return struct.unpack_from("<I", exe["prog"], o)[0]
    return None

def is_valid_mips(w):
    """Heuristic: does this word look like a valid MIPS I instruction?"""
    if w == 0: return True  # NOP
    opc = (w >> 26) & 0x3F
    if opc == 0:  # SPECIAL
        func = w & 0x3F
        return func in (0,2,3,4,6,7,8,9,0xC,0xD,0x10,0x11,0x12,0x13,
                        0x18,0x19,0x1A,0x1B,0x20,0x21,0x22,0x23,0x24,
                        0x25,0x26,0x27,0x2A,0x2B)
    if opc == 1: return True  # REGIMM
    if opc in (2,3): return True  # J, JAL
    if 4 <= opc <= 0xF: return True  # branches, arith imm, LUI
    if 0x10 <= opc <= 0x13: return True  # COP0-COP3
    if 0x20 <= opc <= 0x2B: return True  # loads/stores
    if opc in (0x32, 0x3A): return True  # LWC2, SWC2
    return False

def analyze_code_quality(exe, addr, count=20):
    """Count how many of `count` words starting at addr are valid MIPS."""
    valid = 0
    for i in range(count):
        w = rw(exe, addr + i*4)
        if w is not None and is_valid_mips(w):
            valid += 1
    return valid, count

# ─────────────────────────────────────────────────────────────────────

DEMOS = {
    "HELLOWLD": {"target": 0x800108e4, "caller_pc": 0x80010774},
    "GPUTEST":  {"target": 0x80011b04, "caller_pc": 0x80010778},
    "ADVHELLO": {"target": 0x80010cf0, "caller_pc": 0x80010b80},
}

iso_dir = "/psxrecomp/out/recompiled-demos-windows-latest/inputs"

print("=" * 72)
print("  PSX Demo CALL Target Deep Analysis")
print("=" * 72)

for name, info in DEMOS.items():
    target = info["target"]
    iso = os.path.join(iso_dir, f"{name}.iso")
    exe = extract_exe(iso)
    if not exe: continue

    print(f"\n{'='*72}")
    print(f"  {name}")
    print(f"  entry={exe['entry']:#010x}  load={exe['load']:#010x}  "
          f"size={exe['size']}  end={exe['load']+exe['size']:#010x}")
    print(f"  target={target:#010x}")
    print(f"{'='*72}")

    # ── Code validity around target ─────────────────────────────
    v_at, c_at = analyze_code_quality(exe, target, 20)
    print(f"\n  Code validity at target: {v_at}/{c_at} valid MIPS instructions")

    # Look backwards to find where valid code starts
    func_entry = target
    for back in range(1, 200):
        va = target - back * 4
        w = rw(exe, va)
        if w is None: break
        # Check if this is after a JR ra + delay slot (previous function end)
        w_prev = rw(exe, va - 4)
        if w_prev is not None:
            if (w_prev >> 26) & 0x3F == 0 and (w_prev & 0x3F) == 8 and ((w_prev >> 21) & 0x1F) == 31:
                # va-4 is JR ra, va is delay slot, va+4 is new function start
                func_entry = va + 4
                break
        # ADDIU sp,sp,-N
        opc = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        simm = (w & 0xFFFF)
        if simm >= 0x8000: simm -= 0x10000
        if opc == 9 and rs == 29 and rt == 29 and simm < 0:
            func_entry = va
            break

    print(f"  Likely function entry: {func_entry:#010x} "
          f"(target is +{target - func_entry} bytes in)")

    # ── Full function decode ────────────────────────────────────
    print(f"\n  ┌─ FUNCTION from {func_entry:#010x}")

    # Track LUI values and identify HW accesses
    lui = {}
    hw_accesses = []
    jal_targets = []
    has_jr_ra = False
    func_end = None
    stack_frame = None

    for i in range(200):
        va = func_entry + i * 4
        w = rw(exe, va)
        if w is None: break
        if not is_valid_mips(w) and i > 5:
            print(f"  │  {va:#010x}: {w:#010x}  <invalid — likely data boundary>")
            func_end = va - 4
            break

        text = decode(va, w)
        opc = (w >> 26) & 0x3F
        rs = (w >> 21) & 0x1F
        rt = (w >> 16) & 0x1F
        rd = (w >> 11) & 0x1F
        imm = w & 0xFFFF
        simm = imm if imm >= 0x8000 and (imm - 0x10000) or True else imm
        simm = imm if imm < 0x8000 else imm - 0x10000

        # Track LUI
        if opc == 0xF:
            lui[rt] = imm << 16

        # Track HW register accesses
        hw_note = ""
        if opc in (0x23, 0x2B, 0x21, 0x25, 0x28, 0x29) and rs in lui:
            full = (lui[rs] + simm) & 0xFFFFFFFF
            if full in KNOWN_HW:
                hw_note = f"  ; {KNOWN_HW[full]}"
                hw_accesses.append((va, full, KNOWN_HW[full]))
        if opc == 0xD and rs in lui:  # ORI
            full = lui[rs] | imm
            if full in KNOWN_HW:
                hw_note = f"  ; → {KNOWN_HW[full]}"

        # Stack frame
        if opc == 9 and rs == 29 and rt == 29 and simm < 0:
            stack_frame = -simm
            hw_note += f"  ; frame={-simm}"

        # JAL
        if opc == 3:
            jt = ((va+4)&0xF0000000)|((w&0x03FFFFFF)<<2)
            jal_targets.append((va, jt))

        # JR ra
        if opc == 0 and (w & 0x3F) == 8 and rs == 31:
            has_jr_ra = True

        marker = ""
        if va == target: marker = "  <<<TARGET"
        if va == func_entry and func_entry != target: marker += "  <<<ENTRY"
        print(f"  │  {va:#010x}: {w:#010x}  {text}{hw_note}{marker}")

        # Stop after JR ra + delay slot (unless it's a conditional return)
        if has_jr_ra and opc == 9 and rs == 29 and rt == 29 and simm > 0:
            func_end = va
            # But check if there's more code (multiple return paths)
            next_w = rw(exe, va + 4)
            if next_w is not None and is_valid_mips(next_w) and next_w != 0:
                has_jr_ra = False
                continue
            break

    print(f"  └{'─'*60}")

    # ── Identification ──────────────────────────────────────────
    print(f"\n  ═══ ANALYSIS ═══")

    if hw_accesses:
        print(f"  Hardware register accesses:")
        for ha, hf, hn in hw_accesses:
            print(f"    {ha:#010x}: {hn} ({hf:#010x})")

    gp0 = any("GP0" in n for _,_,n in hw_accesses)
    gp1 = any("GP1" in n or "GPUSTAT" in n for _,_,n in hw_accesses)
    dma2 = any("DMA2" in n for _,_,n in hw_accesses)
    irq = any("I_STAT" in n or "I_MASK" in n for _,_,n in hw_accesses)

    if gp1 and dma2:
        func_name = "GPU DMA transfer (LoadImage / MoveImage / StoreImage)"
        print(f"\n  ★ IDENTIFIED: {func_name}")
        print(f"    This function reads GPUSTAT, writes GP0 commands,")
        print(f"    and controls DMA channel 2 (GPU DMA).")
        print(f"    It is the PSX SDK's LoadImage() or similar VRAM")
        print(f"    transfer function from libgpu.")
    elif gp1 and not dma2:
        func_name = "GPU status poll / DrawSync"
        print(f"\n  ★ IDENTIFIED: {func_name}")
        print(f"    Polls GPUSTAT register, likely DrawSync() or")
        print(f"    GPU busy-wait loop.")
    elif irq:
        func_name = "Interrupt handler / IRQ management"
        print(f"\n  ★ IDENTIFIED: {func_name}")
    elif not hw_accesses:
        # Check what the function does - look at the target vicinity
        # For ADVHELLO, the target is NOP followed by LUI v1,0x8001; LW; JR ra; SW
        # This is a simple getter/setter pattern
        w0 = rw(exe, target)
        w1 = rw(exe, target + 4)
        w2 = rw(exe, target + 8)
        w3 = rw(exe, target + 12)
        if w0 == 0 and w3 is not None and (w3 >> 26) & 0x3F == 0 and (w3 & 0x3F) == 8:
            func_name = "Simple getter/setter (read-old, write-new, return old)"
            print(f"\n  ★ IDENTIFIED: {func_name}")
            print(f"    Pattern: NOP; LUI+LW (read old value); JR ra; SW (write new)")
            print(f"    This is likely a PSX SDK variable access function")
            print(f"    (e.g., SetDispMask, GetVideoMode, or similar).")
        else:
            func_name = "Unknown (no HW accesses, no clear pattern)"
            print(f"\n  ? UNIDENTIFIED: {func_name}")

    print(f"\n  Internal calls (JAL) from this function:")
    for ja, jt in jal_targets:
        print(f"    {ja:#010x}: JAL {jt:#010x}")

    print(f"\n  Mid-function entry analysis:")
    if target != func_entry:
        print(f"    Target {target:#010x} is a BRANCH TARGET within the function")
        print(f"    starting at {func_entry:#010x}.")
        # Find what branches to the target
        for i in range(200):
            va = func_entry + i*4
            w = rw(exe, va)
            if w is None: break
            opc = (w >> 26) & 0x3F
            if opc in (4, 5, 6, 7, 1):
                simm = (w & 0xFFFF)
                if simm >= 0x8000: simm -= 0x10000
                bt = va + 4 + (simm << 2)
                if bt == target:
                    print(f"    Branch at {va:#010x} -> target: {decode(va, w)}")
    else:
        print(f"    Target IS the function entry point.")

    # ── Why the recompiler doesn't find it ──────────────────────
    print(f"\n  Why the recompiler misses this:")
    print(f"    1. No JAL instruction targets {target:#010x} directly")
    print(f"    2. The function at {func_entry:#010x} is called via JALR")
    print(f"       (indirect call through a register)")
    print(f"    3. The address {target:#010x} is a mid-function point,")
    print(f"       not a function entry")

    # Check if the function start has any JAL references
    jals_to_start = []
    for i in range(exe["size"] // 4):
        va = exe["load"] + i*4
        w = struct.unpack_from("<I", exe["prog"], i*4)[0]
        if (w >> 26) & 0x3F == 3:
            jt = ((va+4)&0xF0000000)|((w&0x03FFFFFF)<<2)
            if jt == func_entry:
                jals_to_start.append(va)

    if jals_to_start:
        print(f"\n    Function entry {func_entry:#010x} IS a JAL target:")
        for j in jals_to_start:
            print(f"      JAL at {j:#010x}")
        print(f"    → The recompiler should find the function entry,")
        print(f"      but the JALR at runtime jumps to a MID-FUNCTION address.")
        print(f"      The codegen needs mid-function entry support for {target:#010x}.")
    else:
        print(f"\n    Function entry {func_entry:#010x} is also NOT a JAL target.")
        print(f"    → The function is ONLY reachable via JALR or function pointers.")
        print(f"    → The recompiler's code-pointer harvesting from DATA regions")
        print(f"      (pipeline.cpp:330-380) should find it if there's a pointer in data.")

    # Search for the function address in the data section
    print(f"\n  Searching for {func_entry:#010x} as a data word in the binary...")
    func_entry_bytes = struct.pack("<I", func_entry)
    target_bytes = struct.pack("<I", target)
    for needle, label in [(func_entry_bytes, "func_entry"), (target_bytes, "target")]:
        for i in range(0, exe["size"] - 3, 4):
            w = struct.unpack_from("<I", exe["prog"], i)[0]
            if w == struct.unpack("<I", needle)[0]:
                va = exe["load"] + i
                print(f"    Found {label} ({struct.unpack('<I', needle)[0]:#010x}) "
                      f"at data address {va:#010x} (offset {i:#x})")

print("\n\n" + "=" * 72)
print("  FINAL SUMMARY")
print("=" * 72)
print("""
  ┌────────────┬───────────────────────────────────────────────────────┐
  │  Demo      │  Finding                                             │
  ├────────────┼───────────────────────────────────────────────────────┤
  │ HELLOWLD   │  Target 0x800108e4 is a NOP in a GPU status polling  │
  │            │  loop. The function reads GPUSTAT (0xBF801814) and    │
  │            │  waits for GPU ready. Called via JALR, the target is  │
  │            │  a mid-function branch target (loop body).            │
  │            │  Function: DrawSync / GPU busy-wait                   │
  ├────────────┼───────────────────────────────────────────────────────┤
  │ GPUTEST    │  Target 0x80011b04 is deep inside a GPU DMA transfer │
  │            │  function (LoadImage). The function at 0x80011a04     │
  │            │  sets up DMA2 (GPU DMA channel), writes to GP0, GP1, │
  │            │  DMA2_MADR, DMA2_BCR, DMA2_CHCR.                     │
  │            │  Function: LoadImage / GPU DMA VRAM transfer          │
  ├────────────┼───────────────────────────────────────────────────────┤
  │ ADVHELLO   │  Target 0x80010cf0 is a simple getter/setter that    │
  │            │  reads/writes a variable. Followed by functions that  │
  │            │  access I_MASK (0x1F801074).                          │
  │            │  Function: SetDispMask or similar SDK accessor        │
  ├────────────┼───────────────────────────────────────────────────────┤
  │ ALL        │  All targets are called via JALR (indirect calls).   │
  │            │  All are mid-function addresses, not function starts. │
  │            │  The recompiler needs to either:                      │
  │            │  (a) Discover these via code-pointer harvesting, or   │
  │            │  (b) Support mid-function entry in the dispatcher     │
  └────────────┴───────────────────────────────────────────────────────┘
""")
