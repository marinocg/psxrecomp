# PSXRecomp Architecture

## Overview

PSXRecomp uses a multi-stage pipeline to convert PlayStation 1 games into native C++ code.

## Pipeline Stages

### 1. Disc Or Workspace Input
**Input**: PSX ISO/BIN/CUE image, exported `resources/` workspace, or direct PS-X EXE  
**Output**: Selected PS-X EXE bytes for analysis

The ISO parser reads PlayStation CD-ROM images using the ISO 9660 filesystem format. It handles PSX-specific extensions like Mode 2 sectors and XA data.

Key responsibilities:
- Parse ISO 9660 directory structure
- Locate SYSTEM.CNF configuration file
- Extract PSX-EXE executable file
- Handle multi-track and multi-session discs
- Load `index/recomp_inputs.json` when the detected workspace root is an exported
  resources workspace and resolve `fs/<boot exe>` without the original ISO

### 2. Executable Loading
**Input**: PSX-EXE file  
**Output**: Memory image with code sections

The executable loader parses the PSX-EXE format and loads code into the simulated PSX memory space.

PSX-EXE format:
```
Offset  Size  Description
0x00    8     "PS-X EXE" header
0x08    4     Initial PC
0x0C    4     Initial GP
0x10    4     Load address
0x14    4     File size
...
```

### 3. Disassembly
**Input**: MIPS R3000 binary code  
**Output**: Decoded instruction stream

The disassembler converts raw MIPS machine code into structured instruction objects. The current
implementation decodes core R3000 integer instructions, branch/jump targets, COP0 register moves,
basic COP0 control transfer decode (`RFE`, `CFC0`, `CTC0`), and full COP2/GTE command mnemonics,
while flagging delay slots (including the owning instruction) for downstream analysis.

Challenges:
- Identifying code vs. data
- Handling branch delay slots
- Resolving indirect jumps
- Detecting function boundaries

Before final function-boundary closure, the pipeline also performs iterative pointer harvesting from
initialized 32-bit words in the loaded image. Referenced descriptor tables, local jump tables, and
code-built callback addresses are rescanned after each segmentation pass, and only promoted when
they still decode as plausible callable code.

### 4. Control Flow Analysis
**Input**: Instruction stream  
**Output**: Control flow graph (CFG)

Builds a graph of basic blocks connected by control flow edges.

If an indirect `JALR` target still cannot be resolved after harvesting, generated diagnostics record
the caller PC, containing function, source register, whether the target lies inside the executable
image, whether it was already harvested, and nearby pointer-table words when the originating
descriptor slot can be recovered.

Basic block properties:
- Single entry point
- Single exit point
- No internal branches
- Linear instruction sequence

### 5. IR Generation
**Input**: Control flow graph  
**Output**: Intermediate representation

Converts MIPS instructions to a platform-independent IR.

IR features:
- SSA form (Single Static Assignment)
- Explicit register dataflow
- Memory operations as load/store
- Hardware calls as intrinsics
- COP0 operations for exception-critical paths (`MFC0`, `MTC0`, `RFE`)

### 6. Optimization
**Input**: IR  
**Output**: Optimized IR

Optional optimization passes:
- Constant propagation
- Dead code elimination
- Common subexpression elimination
- Loop invariant code motion
- Register allocation hints

### 7. Code Generation
**Input**: Optimized IR  
**Output**: C++ source code

Generates readable C++ that preserves game behavior.

Output structure (illustrative pseudocode):
```cpp
// Generated from GAME.EXE

#include "psxrecomp/runtime.h"

namespace game {

// Memory regions
extern u8 g_ram[2*1024*1024];

// Recompiled functions
void func_80010000();
void func_80010100();

// Main entry point
void main() {
    // Initialize PSX system
    psxrecomp::runtime::PsxSystem system;
    system.initialize();
    
    // Run game code
    func_80010000();
}

} // namespace game
```

### 8. Compilation
**Input**: Generated C++ files  
**Output**: Native executable

The generated C++ is compiled using standard compilers (GCC, Clang, MSVC) with the runtime library.

## Resource Extraction Index (REX1)

For ISO-like inputs, the pipeline emits a stable resource index under `resources/index/`:

- `disc_tree.json` (full recursive ISO tree + extents)
- `disc_meta.json` (disc/track metadata)
- `recomp_inputs.json` (boot + executable set with PS-X EXE header metadata)
- `catalog.json` (unified exported-file catalog with extents, SHA-1, and type sniffing)
- `resources_manifest.json` (tooling entrypoint + export summary + export policy metadata)

Filesystem exports are written under `resources/fs/` using ISO-relative paths.
Data-track exports are written under `resources/disc/`:

- `data_track.bin` (LBA-indexed sector stream; `2048` user-data or `2352` raw, based on mode)
- `disc_layout.json`
- `disc_hashes.json`

Generated runners consume this exported blob directly (`resources/disc/data_track.bin`) instead of
opening the original ISO/BIN at runtime.

Optional embedded carving outputs are written under `resources/embedded/by_container/...` when
`PSXRECOMP_RES_EMBEDDED_SCAN=1` (or `true`) is enabled.

The pipeline also accepts an exported resources workspace as input:

- `psxrecomp game.iso -o out/`
- `psxrecomp out/<module>/<disc>/<tag>/resources -o out_rerun/`

In workspace mode, executable selection is driven by `index/recomp_inputs.json` (relative to the
detected workspace root), and existing resource index/filesystem artifacts are copied forward to the
new output.

Export policy is controlled by `PipelineOptions::ResourceExportOptions`:

- `minimal`: always-included set only.
- `smart`: always-included set + capped small-file export.
- `full`: export full disc filesystem.

The top-level pipeline `manifest.json` includes:

- `output.resourceRoot`
- `output.resourceManifest`

See [Resource Extraction Layout 1.0](resource_extraction_layout.md) for schemas and examples.

## Memory Model

PSXRecomp maintains the PSX memory layout:

```
0x00000000 - 0x001FFFFF: Main RAM (2MB)
0x1F800000 - 0x1F8003FF: Scratchpad (1KB)
0x1F801000 - 0x1F801FFF: I/O Ports
0x1FC00000 - 0x1FC7FFFF: BIOS ROM (512KB)
```

Memory accesses in recompiled code are translated to:
```cpp
// Original PSX: lw $t0, 0($a0)
u32 value = system.read<u32>(a0);

// Original PSX: sw $t1, 4($a0)
system.write<u32>(a0 + 4, t1);
```

## Hardware Emulation

Recompiled code calls into the runtime library for hardware access:

- **GPU**: GP0/GP1 command decode and software rasterization reference path, with backend scaffolding for future API parity
- **SPU**: Voice/decode/mix core with pluggable audio backend interface
- **CD-ROM**: Command/data FIFO + DMA transport with baseline XA streaming controls
- **Controllers**: Runtime input abstraction scaffolding
- **Memory Cards**: Initialization stubs; sector I/O behavior still incomplete

## Challenges

### Self-Modifying Code
PSX games sometimes modify code at runtime. Detection strategies:
1. Write protection on code pages
2. Trap and recompile modified code
3. Fall back to interpretation for dynamic regions

### Timing Accuracy
PSX has precise CPU/GPU timing. Strategies:
1. Cycle-accurate mode (slower but accurate)
2. Frame-based synchronization (faster)
3. Configurable accuracy levels

### Hardware Quirks
PSX hardware has many quirks that games depend on:
- GPU rasterization bugs
- DMA timing
- CPU pipeline stalls
- Interrupt handling

These must be preserved in the runtime library.

## Future Enhancements

- JIT compilation for dynamic code regions
- Profile-guided optimization
- Multi-threaded recompilation
- Advanced GPU features (resolution scaling, texture filtering)
- Shader-based rendering for effects
