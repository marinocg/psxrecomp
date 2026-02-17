#!/usr/bin/env python3
"""Trace the main function to understand GPU variable layout."""
import struct

with open('out/recompiled-demos-windows-latest/inputs/GPUTEST.iso', 'rb') as f:
    data = f.read()

psx_offset = data.find(b'PS-X EXE')
sector_num = psx_offset // 2352
dest_addr = struct.unpack_from('<I', data, sector_num * 2352 + 24 + 0x18)[0]
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

# PSn00bSDK GPUTEST main function layout analysis

# After 0x800100A8: ADDIU s0, s0, 0x5540
# s0 = 0x80010000 + 0x5540 = 0x80015540
# s0 now points to the GpuContext struct.

# GpuContext layout (PSn00bSDK typical double-buffer setup):
# We need to figure out if it's: disp[0]+disp[1]+draw[0]+draw[1]+active
# or: draw[0]+disp[0]+draw[1]+disp[1]+active

# DISPENV = 20 bytes (RECT disp + RECT screen + 4 bytes flags)
# DRAWENV = 64 bytes (see above)

# From the main function calls:
# First SetDefDrawEnv: a0 = s0_orig + 0x5540 (= s0 before adjustment)
#   But s0 wasn't adjusted yet at 0x8001003C
#   At 0x80010014: LUI s0, 0x8001 => s0 = 0x80010000
#   At 0x8001003C: ADDIU a0, s0, 0x5540 => a0 = 0x80015540

# So FIRST SetDefDrawEnv target = 0x80015540 (draw[0]? or disp[0]?)
# SetDefDrawEnv writes clip + tw + tpage/dtd fields to a DRAWENV struct
# If draw[0] is at GpuCtx+0 = 0x80015540, then it writes there

# SECOND SetDefDrawEnv: at 0x80010054/0x80010068/0x8001006C
# LUI a0, 0x8001; ADDIU a0, a0, 0x5554 => a0 = 0x80015554
# Offset from GpuCtx: 0x5554 - 0x5540 = 0x14 = 20

# THIRD call (SetDefDispEnv at 0x80010088):
# LUI a0, 0x8001; ADDIU a0, a0, 0x5568 => a0 = 0x80015568
# Offset from GpuCtx: 0x5568 - 0x5540 = 0x28 = 40

# FOURTH call (SetDefDispEnv at 0x800100A4):
# LUI a0, 0x8001; ADDIU a0, a0, 0x55A8 => a0 = 0x800155A8
# Offset from GpuCtx: 0x55A8 - 0x5540 = 0x68 = 104

# So the layout is:
# GpuCtx + 0  = 0x80015540: first arg to SetDefDrawEnv  (DRAWENV? 20 bytes for DISPENV?)
# GpuCtx + 20 = 0x80015554: second arg to SetDefDrawEnv (another DRAWENV/DISPENV?)
# GpuCtx + 40 = 0x80015568: first arg to SetDefDispEnv  (DISPENV? 64 bytes for DRAWENV?)
# GpuCtx + 104= 0x800155A8: second arg to SetDefDispEnv (another)

# Wait, that doesn't make sense. SetDefDrawEnv writes DRAWENV (64 bytes).
# If draw[0] is at offset 0, draw[1] is at offset 20... that overlaps!

# Let me check: DISPENV is actually only 12 bytes or 20 bytes?
# PSn00bSDK psxgpu.h: DISPENV has RECT disp(8) + RECT screen(8) + 4 bytes = 20 bytes
# DRAWENV has 28 bytes fields + 36 bytes DR_ENV = 64 bytes

# Hmm, but SetDefDrawEnv at GpuCtx+0 with DRAWENV=64 bytes would overlap
# with SetDefDrawEnv at GpuCtx+20. That can't be right.

# Let me re-examine. The GPUTEST example uses:
# db.draw[0], db.draw[1], db.disp[0], db.disp[1], db.activeBuffer
# where DB is the double-buffer struct.

# Looking at it differently:
# SetDefDrawEnv(&db.draw[0], x, y, w, h)   => draw[0] at GpuCtx+0
# SetDefDrawEnv(&db.draw[1], x, y, w, h)   => draw[1] at GpuCtx+20
# Wait, 20? DRAWENV is 64 bytes, not 20!

# Hmm, unless the struct is packed differently. Let me check the actual
# PSn00bSDK struct sizes more carefully.

# Actually, looking at the offsets:
# arg0 = base + 0x5540 (offset 0)
# arg1 = base + 0x5554 (offset 20 = 0x14)
# arg2 = base + 0x5568 (offset 40 = 0x28)
# arg3 = base + 0x55A8 (offset 104 = 0x68)

# The gap between arg0 and arg1 is 20 bytes.
# But DRAWENV is 64 bytes!
# Unless... the first two are DISPENV (20 bytes each) and the last two are DRAWENV (64 bytes each)?

# Let me re-check which function is called:
# 0x8001003C: a0 = s0 + 0x5540
# 0x8001004C: JAL 0x80010C10 (SetDefDrawEnv)
# => SetDefDrawEnv(GpuCtx+0, 0, 0, 320, 240) with h=240 on stack

# 0x80010054/0x8001006C: a0 = 0x80015554
# 0x80010068: JAL 0x80010C10 (SetDefDrawEnv)
# => SetDefDrawEnv(GpuCtx+20, 0, 240, 320, 240)

# But the gap is only 20 bytes for a 64-byte struct. That means:
# Either the struct is smaller, or the first two structs are DISPENV not DRAWENV.

# Wait, I need to check if 0x80010C10 is SetDefDrawEnv or SetDefDispEnv.

# SetDefDrawEnv writes halfwords at offsets 0,2,4,6,8,10,12,14,16,18
# That's 10 halfwords = 20 bytes max offset (but offset 18 is the last, which is tpage)
# Actually it writes up to offset 18 (tpage) = 10 halfwords, but also zeroes tw and clears dtd/tpage
# If it only writes 20 bytes of header fields, then the DR_ENV at +28 is separate
# So the "effective" write area is up to offset ~23 (dtd at +22, dfe at +23)

# But the struct is still 64 bytes total with DR_ENV at +28

# Could the GPUTEST demo use a DIFFERENT struct layout than standard PSn00bSDK?
# Let me look at what the third/fourth functions (SetDefDispEnv) expect:

# 0x80010070/0x80010088: a0 = 0x80015568
# 0x80010084: JAL 0x80010AD8 (SetDefDispEnv - a different function!)

# So the layout is:
# offset 0 (0x5540): SetDefDrawEnv target #1 (DRAWENV)  -- but only 20 bytes gap to next
# offset 20 (0x5554): SetDefDrawEnv target #2 (DRAWENV) -- 20 bytes gap
# offset 40 (0x5568): SetDefDispEnv target #1 (DISPENV) -- 64 bytes gap
# offset 104 (0x55A8): SetDefDispEnv target #2 (DISPENV) -- ??

# WAIT. That would mean DISPENV is 64 bytes? No, DISPENV is 20 bytes.
# The gap from 0x5568 to 0x55A8 is 64 bytes. That's the size of... DRAWENV!

# I think I had it backwards! Let me reconsider:
# The demo struct might be: DISPENV disp[2] (20 bytes each) + DRAWENV draw[2] (64 bytes each)
# offset 0: DISPENV disp[0] = 20 bytes
# offset 20: DISPENV disp[1] = 20 bytes
# offset 40: DRAWENV draw[0] = 64 bytes
# offset 104: DRAWENV draw[1] = 64 bytes
# offset 168: int activeBuffer = 4 bytes

# But then the FIRST two calls go to SetDefDrawEnv with DISPENV addresses!
# Unless... I got the functions mixed up. Let me check.

# func_0x80010C10: called with (offset 0, 0, 0, 320, 240) and (offset 20, 0, 240, 320, 240)
# func_0x80010AD8: called with (offset 40, 0, 240, 320, 240) and (offset 104, 0, 0, 320, 240)

# If func_0x80010C10 is SetDefDispEnv (20 bytes), that would make sense:
#   SetDefDispEnv(&disp[0], 0, 0, 320, 240)
#   SetDefDispEnv(&disp[1], 0, 240, 320, 240)

# And func_0x80010AD8 is SetDefDrawEnv (64 bytes):
#   SetDefDrawEnv(&draw[0], 0, 240, 320, 240) -- note y=240 for second framebuffer
#   SetDefDrawEnv(&draw[1], 0, 0, 320, 240)

# Let me verify by checking func_0x80010C10 size:
# From manifest: func_0x80010C10 goes from 0x80010C10 to 0x80010C40 = 48 bytes = 12 instructions
# That's very small - SetDefDispEnv is small (just writes 4 halfwords for RECT disp + RECT screen)

# func_0x80010AD8 goes from 0x80010AD8 to 0x80010B28 = 80 bytes = 20 instructions
# That's about right for SetDefDrawEnv which writes more fields

print("=== CORRECTED Layout Analysis ===")
print()
print("func_0x80010C10 (12 instructions) = SetDefDispEnv (small)")
print("func_0x80010AD8 (20 instructions) = SetDefDrawEnv (larger)")
print()
print("GpuContext struct layout:")
print("  offset   0 (0x5540): DISPENV disp[0] = 20 bytes -> SetDefDispEnv(0, 0, 320, 240)")
print("  offset  20 (0x5554): DISPENV disp[1] = 20 bytes -> SetDefDispEnv(0, 240, 320, 240)")
print("  offset  40 (0x5568): DRAWENV draw[0] = 64 bytes -> SetDefDrawEnv(0, 240, 320, 240)")
print("  offset 104 (0x55A8): DRAWENV draw[1] = 64 bytes -> SetDefDrawEnv(0, 0, 320, 240)")
print("  offset 168 (0x55E8): int activeBuffer = 4 bytes -> initialized to 0")
print()

# Now the critical stores in main:
# After 0x800100A8: ADDIU s0, s0, 0x5540 => s0 = 0x80015540
# 0x800100B8: SW r2, 64(s0)  => s0+64 = 0x80015580 = draw[0]+24 = draw[0].isbg
# 0x800100BC: SW r2, 128(s0) => s0+128 = 0x800155C0 = draw[1]+24 = draw[1].isbg
# 0x800100C4: SW r0, 168(s0) => s0+168 = 0x800155E8 = activeBuffer = 0

# r2 = LUI 0x2800 | ADDIU 1 = 0x28000001
# Stored as 32-bit word at draw[0]+24:
# PSX is little-endian:
# byte +24 (isbg) = 0x01
# byte +25 (r0)   = 0x00
# byte +26 (g0)   = 0x00
# byte +27 (b0)   = 0x28

print("Main sets:")
print("  draw[0].isbg = 1, draw[0].r0 = 0, draw[0].g0 = 0, draw[0].b0 = 0x28 (=40)")
print("  draw[1].isbg = 1, draw[1].r0 = 0, draw[1].g0 = 0, draw[1].b0 = 0x28 (=40)")
print("  activeBuffer = 0")
print()

# Now: func_0x80010C44 is called with a0 = s0 = GpuCtx (0x80015540)
# Looking at what _gpu_senv does: it reads from r4+0, r4+2, r4+4, etc.
# r4 = a0 = 0x80015540 = GpuCtx base
# But _gpu_senv expects a DRAWENV pointer!
# GpuCtx+0 is DISPENV disp[0], NOT DRAWENV draw[0]!

# WAIT. Let me re-read the call at 0x800100C0:
# 0x800100B4: OR a0, s0, r0 => a0 = s0 = 0x80015540
# 0x800100C0: JAL 0x80010C44 (_gpu_senv)
# But _gpu_senv reads DRAWENV fields from a0!
# If a0 = GpuCtx = 0x80015540, and draw[0] is at GpuCtx+40 = 0x80015568...
# Then _gpu_senv is reading from the WRONG struct (DISPENV instead of DRAWENV)!

# UNLESS _gpu_senv is NOT being called with DRAWENV as first arg.
# Let me check the PSn00bSDK source for PutDrawEnv:
# PutDrawEnv(DRAWENV* env) calls _gpu_senv(env, ...) with the DRAWENV pointer
# BUT the call in main is: _gpu_senv(GpuCtx, ...)
# This seems wrong... unless the recompiler changed the argument.

# Actually wait - let me re-read the code. At 0x800100C0:
# The JAL is after the stores. The convention might be different.
# Let me check what function is actually at 0x80010C44.

# Hmm, actually 0x80010C44 is _gpu_senv which is the "set environment" function
# that writes GP1 display configuration. It could also be PutDispEnv.
# Let me check what PutDispEnv does in PSn00bSDK.

# Actually, looking at the calling convention:
# Before the JAL 0x80010C44:
# a0 = s0 = GpuCtx base = 0x80015540
# The function reads from a0+0, a0+2, a0+4, etc.
# These are at GpuCtx+0 = disp[0].disp.x, disp[0].disp.y, etc.

# So maybe func_0x80010C44 is NOT _gpu_senv but PutDispEnv!
# It reads display rect and sets GP1 display parameters.
# That would make perfect sense!

# And then func_0x800108E8 is _build_drawenv_ot which constructs the DR_ENV.
# It's called from func_0x80010B8C which is called from main.

print("CRITICAL INSIGHT:")
print("  func_0x80010C44 is likely PutDispEnv (sets GP1 display config)")
print("  func_0x800108E8 is _build_drawenv_ot (builds DR_ENV packet)")
print("  func_0x80010B8C calls func_0x800108E8 (_build_drawenv_ot)")
print()

# Now let me trace where _build_drawenv_ot is called from main:
# 0x800100C8: LW a0, 168(s0)   => a0 = activeBuffer (0)
# 0x800100D0: SLL a0, a0, 6    => a0 = 0 * 64 = 0
# 0x800100D4: ADDIU a0, a0, 40 => a0 = 40 (offset of draw[0])
# 0x800100DC: ADDU a0, s0, a0  => a0 = GpuCtx + 40 = 0x80015568 = draw[0]!
# 0x800100D8: JAL 0x80010B8C   => calls with a0 = draw[0]

print("Call to func_0x80010B8C (calls _build_drawenv_ot):")
print("  a0 = GpuCtx + 40 + activeBuffer*64 = draw[activeBuffer]")
print("  When activeBuffer=0: a0 = 0x80015568 = draw[0]")
print()

# And _build_drawenv_ot (func_0x800108E8) receives r5 = DRAWENV pointer
# It reads clip.x from r5+0, clip.y from r5+2, etc.
# draw[0] = GpuCtx+40 = 0x80015568
# draw[0].clip.x = GpuCtx+40 = offset 40 from GpuCtx base

# SetDefDrawEnv was called with (GpuCtx+40, 0, 240, 320, 240)
# So draw[0].clip.x=0, clip.y=240, clip.w=320, clip.h=240

# NOW: the DMA trace showed FillRect with zero position and size.
# If the DRAWENV is correctly written by SetDefDrawEnv, why are the values zero?

# Let me check: does func_0x80010AD8 (SetDefDrawEnv) write to the right place?
print("SetDefDrawEnv calls:")
print("  SetDefDrawEnv(GpuCtx+40, 0, 240, 320, 240)  => draw[0]")
print("  SetDefDrawEnv(GpuCtx+104, 0, 0, 320, 240)   => draw[1]")
print()

# Now I need to verify: in the generated C++ code, does func_0x80010AD8
# correctly write halfwords to the DRAWENV address?
# The key is what address is computed for the DRAWENV.

# In the generated main function, the calls use:
# regs[4] = regs[16] + immediate
# where regs[16] was set to LUI 0x8001 = 0x80010000
# But WAIT - at 0x80010070:
# LUI a0, 0x8001 => a0 = 0x80010000
# ADDIU a0, a0, 0x5568 => a0 = 0x80015568 => draw[0]

# And at 0x800100A8: ADDIU s0, s0, 0x5540
# s0 = 0x80010000 + 0x5540 = 0x80015540 = GpuCtx

# Then at 0x800100D8: JAL 0x80010B8C with a0 = s0 + 40 = 0x80015568 = draw[0]

# So both addresses match. The write and read should go to the same place.

# THE REAL QUESTION: Does func_0x80010B8C pass r5 = DRAWENV pointer to func_0x800108E8?
# func_0x80010B8C receives a0 (=r4) = draw[0] pointer
# It needs to set up r5 = DRAWENV pointer before calling func_0x800108E8

print("Need to check func_0x80010B8C to verify r5 is set correctly for _build_drawenv_ot")
