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
- `GPUSTAT` bit 22 indicates VBlank phase, and bit 31 odd/even field toggles
  each frame (used by SDK VSync loops in both progressive and interlaced modes).
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

## SPU: Sound Processing Unit

- **Channels**: 24 simultaneous voices
- **Sample Rate**: 44.1 KHz
- **Memory**: 512KB sound RAM
- **Effects**: Reverb, ADPCM decompression

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
