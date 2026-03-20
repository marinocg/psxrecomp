# PSX Hardware Reference

Quick reference for PlayStation 1 hardware components relevant to static recompilation.

## CPU: MIPS R3000A

- **Architecture**: 32-bit RISC
- **Clock Speed**: 33.8688 MHz
- **Pipeline**: 5 stages
- **Instruction Set**: MIPS I
- **Coprocessors**:
  - COP0: System Control (exceptions, cache)
  - COP2: GTE (Geometry Transform Engine)

### Registers

- 32 general-purpose registers ($0-$31)
- $0 is hardwired to zero
- $31 is used for return addresses (by convention)

### Branch Delay Slots

All branch and jump instructions have a delay slot - the instruction immediately following the branch always executes before the branch takes effect.

```
beq $t0, $t1, target
nop              # Delay slot - always executes
```

## Memory Layout

```
Region              Range                     Size        Description
Main RAM            0x00000000-0x001FFFFF     2MB         Main memory
Expansion 1         0x1F000000-0x1F7FFFFF     8MB         Expansion region 1
Scratchpad          0x1F800000-0x1F8003FF     1KB         Fast CPU cache
Hardware Regs       0x1F801000-0x1F802FFF     8KB         I/O ports
Expansion 2         0x1F802000-0x1F803FFF     8KB         Expansion region 2
BIOS ROM            0x1FC00000-0x1FC7FFFF     512KB       System BIOS
```

PSX-SPX notes that the 2MB main RAM is mirrored across the first 8MB of
physical address space by default, so `0x00000000-0x007FFFFF` aliases the same
underlying RAM.

PSX-SPX also documents six extra waitstates for CPU data reads from main RAM.
Ordinary writes are buffered through the write queue, so the runtime only adds
extra CPU cycle cost on RAM reads. This keeps software timeouts and VBlank
polling loops closer to hardware behavior.

## GPU: Geometry Transform Engine

- **Resolution**: 256x224 to 640x480
- **Colors**: 16.7 million (24-bit)
- **VRAM**: 1MB
- **Framebuffer**: Double buffered
- **Primitives**: Points, lines, triangles, rectangles
- **Texture**: 4-bit, 8-bit, 15-bit

### GPU Commands

Commands are written to 0x1F801810:

- Drawing commands (polygons, sprites, lines)
- VRAM transfer commands
- GPU control commands

### Runtime Conformance Notes

- `GP0(02h)` (Quick Rectangle Fill) uses raw VRAM coordinates and ignores draw
  area clipping, draw offset, and mask checks from `GP0(E6h)`.
- `GP0(02h)` aligns X down to 16-pixel boundaries and rounds width up to a
  16-pixel multiple; zero width/height acts as a no-op.
- `GPUSTAT` bit 22 reflects the vertical interlace flag from `GP1(08h)`, not
  current VBlank state.
- `GPUREAD` is treated as a latched read register: `GP1(10h)` internal-register
  queries update it immediately, unsupported indices preserve the previous
  latch, and `GPUSTAT` bit 27 only describes VRAM-to-CPU transfer readiness.
- `GP1(04h)` controls the DMA/data-request view reflected in `GPUSTAT`, but
  DMA2 RAM-to-GPU payload words still enter the GP0 input path even if software
  toggles `GP1(04h)` around the transfer setup.
- `GPUSTAT` bit 31 stays low during VBlank; during active display it follows
  scanline parity in progressive 240-line modes and the current field in
  interlaced modes, matching the PSX-SPX timing notes used by SDK poll loops.
- `GP1(00h)` reset returns the control path to the documented post-reset
  baseline with `GPUSTAT = 0x14802000`.
- BIOS `A0(4Bh)` (`send_gpu_linked_list`) now follows the DMA2 register path
  (`GP1(04h)=2`, DMA control setup, CHCR start) rather than direct software
  pushing of OT words.
- BIOS `A0(4Eh)` (`gpu_sync`) is modeled separately from B0 file APIs and turns
  GPU DMA direction off after synchronization when DMA mode is active.
- Kernel hardware-event classes follow PSX-SPX mapping for `SIO/SPU/PIO`, and
  `Timer2` uses the same class ID as `Timer1` (BIOS quirk).

### Display Modes

- NTSC: 60Hz, 480i
- PAL: 50Hz, 576i
- Progressive modes available

## MDEC: Macroblock Decoder

- **Registers**: `0x1F801820` (command/data), `0x1F801824` (status/control)
- **DMA Channels**: DMA0 (MDEC In, RAM→MDEC), DMA1 (MDEC Out, MDEC→RAM)
- **Function**: Decompresses MPEG-style macroblocks for FMV playback

### Runtime Conformance Notes

- Status register after control-reset matches PSX-SPX documented `0x80040000`
  (bit 31 = FIFO empty, bits 18-16 = block 4/Cr, all other bits zero including
  depth field at bits 26-25).
- Command 1 (decode macroblock) sets busy, accepts the parameter count from bits
  0-15, and generates placeholder (zero) output once all parameters are consumed.
  Actual MPEG decode is not yet implemented; the handshake (busy set/clear,
  output-request assertion, DMA1 drainability) is fully modeled.
- Commands 2 and 3 (set quantization/scale table) accept 16/32 or 32 parameter
  words respectively and populate internal tables used by future decode commands.
- No-function commands (0, 4-7) reflect bits 0-15 and 25-28 of the command word
  into the corresponding status fields without setting busy.
- Control register bit 30 enables the data-in DMA request (status bit 28);
  bit 29 enables the data-out DMA request (status bit 27).
- DMA0 transfers feed parameters to the MDEC via `writeDma()`; DMA1 transfers
  drain output via `readDma()`.  Both run synchronously within `handleDmaTransfer`.

## SPU: Sound Processing Unit

- **Channels**: 24 simultaneous voices
- **Sample Rate**: 44.1 KHz
- **Memory**: 512KB sound RAM
- **Effects**: Reverb, ADPCM decompression

### Runtime Conformance Notes

- SPU MMIO is modeled as a 16-bit register bank. `LW`/`SW` over the SPU range
  are handled as two ordered 16-bit register accesses.
- SPU byte writes follow the PSX-SPX bus note used by SDK code: odd-byte
  writes are ignored, even-byte writes hit the addressed 16-bit register.
- `SPUCNT` and `SPUSTAT` are modeled as an active handshake, not passive
  storage. `SPUCNT[5:0]` takes effect after a hardware delay, and `SPUSTAT`
  reports the applied mode bits rather than the most recent CPU write.
- `SPUSTAT` is treated as CPU read-only. The runtime currently exposes the
  documented busy flag, DMA read/write request bits, DMA-request summary,
  IRQ flag, and applied `SPUCNT[5:0]` mode field used by common bring-up loops.
- `1F801DA6` (transfer address) keeps the visible register value stable while
  transfers advance an internal current address. The register value is in
  8-byte units, so the internal SPU RAM byte address is `value * 8`.
- `1F801DA8` is modeled as the transfer FIFO used by manual writes, and
  `1F801DAC` transfer control currently supports the normal `0004h` path used
  by BIOS/libsnd-style manual and DMA transfers.
- `1F801DA4` is modeled as the SPU IRQ address register in 8-byte units. The
  runtime latches `SPUSTAT.bit6` when a voice ADPCM fetch or a RAM
  transfer/DMA access hits that byte address.
- `PMON`, `NON`, and `EON` update per-voice runtime masks instead of behaving
  like passive storage, so init-time pitch-mod/noise/reverb setup is visible to
  later playback paths.
- `SPUCNT[3:0]` routing bits are part of the delayed low-bit apply path. CD
  audio output now respects the modeled enable/reverb routing bits, and the
  external-routing bits are tracked for readback/state fidelity.
- `ENDX` is modeled as voice status rather than writable storage. `KON` clears
  the keyed voice bits, and ADPCM loop-end blocks set them once playback
  reaches the end of the current 28-sample block.
- ADPCM block flags follow PSX-SPX bit assignments: bit0=`loop-end`,
  bit1=`loop-repeat`, bit2=`loop-start`. `loop-start` captures the repeat
  address, `loop-end|loop-repeat` jumps back to the repeat address, and
  `loop-end` without repeat drives the voice into release/zero-level behavior.
- `SPUCNT.bit6` is treated as the IRQ enable/acknowledge control, so clearing
  it drops the latched `SPUSTAT.bit6` flag.
- Current ADSR and current main-volume reads are backed by live runtime state
  rather than dead registers, which matches the init/polling patterns used by
  common SDK code.

### Audio Formats

- ADPCM: 4-bit compressed
- 16-bit PCM

## CD-ROM Drive

- **Speed**: 2x (300 KB/s)
- **Format**: CD-ROM XA (Mode 2)
- **Sector Size**: 2048 or 2352 bytes
- **Audio**: CD-DA and XA-ADPCM

### Disc Formats

- ISO 9660 filesystem
- PSX-specific extensions
- Multi-session support

### MMIO Register Bank (0x1F801800-0x1F801803)

- `0x1F801800` (R): status register, low bits report index.
- `0x1F801800` (W): index selector (`0..3`) for offsets `+1..+3`.
- `0x1F801801` (R): response FIFO.
- `0x1F801801` (W, index 0): command register.
- `0x1F801802` (R): data FIFO.
- `0x1F801802` (W, index 0): parameter FIFO.
- `0x1F801802` (W, index 1): interrupt enable.
- `0x1F801803` (R, index 0/2): interrupt enable.
- `0x1F801803` (R, index 1/3): interrupt flags.
- `0x1F801803` (W, index 1): interrupt flag acknowledge.
- IRQ delivery is queued: a later IRQ type becomes visible only after ACK clears the current one.
- BIOS/kernel CD events are not a 1:1 mirror of higher-level libcd completion semantics: the raw subtype mapping is `INT3->0x0010`, `INT2->0x0020`, `INT1->0x0040`, `INT4->0x0080`, `INT5->0x8000`, while BIOS helper APIs may translate specific INT3 completions onto `0x0020`.
- Finite `ReadN/ReadS` streams terminate with `INT4/DataEnd` after the last accepted/buffered data phase is exhausted; end-of-stream should clear the read-active state rather than leaving the controller spinning on more `INT1`s.
- `HCLRCTL` drains unread result bytes for the acknowledged IRQ; software must
  not expect post-ACK reads from the old response FIFO contents.
- `BFRD` requests accept the sector for the currently pending `INT1` and lock that
  transfer window, so a fresh request can replace unread tail bytes from the
  previous sector when software mixes header probes with payload DMA.
- The controller state is best modeled as three separate ownership stages:
  buffered-next sector, published/current-INT1 sector, and currently draining
  sector. With `BFRD` cleared, a newer `INT1` only advances the published
  sector; with `BFRD` still armed, that newer `INT1` can replace the readable
  block and discard the older sector's unread tail.
- Even if later sectors are already buffered, the next `INT1` is not surfaced in
  the same instant as the ACK; there is a short post-ACK gap where `BFRD` still
  refers to the old interrupt's sector, matching PSX-SPX host-transfer notes.
- During `ReadN`/`ReadS`, the drive buffers only a small number of sectors; if
  software falls behind, older sectors can be dropped instead of creating an
  unbounded `INT1` backlog. PSX-SPX explicitly warns that sector overrun skips
  data without reporting an error.

## Controllers

### Standard Controller

- D-Pad (4 directions)
- 4 face buttons (△, ○, ×, □)
- 4 shoulder buttons (L1, R1, L2, R2)
- Start, Select

### DualShock

- All standard controller features
- 2 analog sticks
- Rumble motors
- Analog button pressure (later models)

## Memory Cards

- **Capacity**: 128KB (8192 blocks of 128 bytes)
- **Format**: 15 save slots per card
- **Interface**: Serial
- **Speed**: ~64 bytes per frame

## DMA: Direct Memory Access

7 DMA channels for high-speed transfers:

1. MDECin (MDEC decompression)
2. MDECout
3. GPU (commands and VRAM)
4. CD-ROM
5. SPU
6. PIO (Extension port)
7. OTC (Ordering table)

## Interrupts

IRQ sources:

- VBLANK (vertical blank)
- GPU (command complete)
- CD-ROM
- DMA
- Timers (3x)
- Controller/Memory Card
- SPU

BIOS software IRQ notes:

- `SysEnqIntRP` / `SysDeqIntRP` manage BIOS interrupt callback chains by priority.
- Per PSX-SPX, the BIOS exception handler walks those priority chains before `HookEntryInt`.
- The BIOS CD-ROM handlers are part of the priority-0 chain, so CD IRQ work happens before lower-priority user callbacks observe IRQ2.

## Timers

3 hardware timers:

- Timer 0: System clock or GPU dot clock
- Timer 1: System clock or horizontal blank
- Timer 2: System clock or system clock/8

## Useful Memory Addresses

```
0x1F801070: Interrupt Status Register
0x1F801074: Interrupt Mask Register
0x1F801810: GPU Command/Data
0x1F801814: GPU Status Register
0x1F801C00: SPU Control
0x1F801800: CD-ROM index/status port (banked at +0..+3)
```

## Notes for Recompilation

### Critical Behaviors

- Write-through cache behavior
- Unaligned memory access handling
- Interrupt timing
- DMA completion timing
- GPU/SPU synchronization

### Performance Considerations

- DMA is faster than CPU copy
- GPU command buffering
- SPU streaming for large audio
- CD-ROM prefetching

### Common Patterns

- Double buffering for graphics
- Ring buffers for audio
- Linked lists in VRAM (GPU ordering tables)
- Memory card async I/O
