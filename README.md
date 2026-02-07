# PSX Recompiler (psxrecomp)

[![CI](https://github.com/marinocg/psxrecomp/actions/workflows/ci.yml/badge.svg)](https://github.com/marinocg/psxrecomp/actions/workflows/ci.yml)

A static recompiler for PlayStation 1 (PSX) games that converts ISO game files into statically recompiled C++ binaries, enabling them to run on modern systems.

## Overview

PSXRecomp aims to bring classic PlayStation games to modern platforms through static recompilation. Unlike emulation, which interprets instructions at runtime, static recompilation converts the original MIPS R3000 assembly code into native C++ code that can be compiled for various target platforms.

## Features

- 🎮 **PSX ISO Parsing**: Extract and parse PlayStation ISO/BIN files
- 🔄 **MIPS R3000 Disassembly**: Disassemble PSX executable code
- ⚡ **Static Recompilation**: Convert MIPS assembly to optimized C++ code
- 🎯 **Cross-platform**: Generated code can run on Windows, Linux, macOS
- 🔧 **Modular Architecture**: Clean separation of concerns for easy extension

## Project Status

**⚠️ Early Development** - This project is in its initial stages. Contributions are welcome!

## Architecture

The recompilation process follows these stages:

```
PSX ISO → ISO Parser → Executable Extractor → MIPS Disassembler → 
    IR Generator → Optimizer → C++ Code Generator → Native Binary
```

### Core Components

1. **ISO Parser**: Reads PSX CD-ROM image formats (ISO, BIN/CUE)
2. **Executable Loader**: Extracts and parses PSX-EXE format
3. **Disassembler**: Converts MIPS R3000 machine code to assembly
4. **IR Generator**: Creates intermediate representation of the code
5. **Recompiler**: Generates equivalent C++ code
6. **Runtime Library**: Provides PSX hardware abstractions (GPU, SPU, etc.)

## Building

### Prerequisites

- C++17 compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.15 or higher
- Git

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/marinocg/psxrecomp.git
cd psxrecomp

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
cmake --build .

# Run tests
ctest
```

## Usage

```bash
# Basic usage (planned)
psxrecomp input.bin -o output_dir

# With options
psxrecomp input.bin -o output_dir --optimize --preserve-symbols
```

## Project Structure

```
psxrecomp/
├── src/              # Source files
│   ├── iso/          # ISO/BIN parsing
│   ├── disasm/       # MIPS disassembler
│   ├── ir/           # Intermediate representation
│   ├── recompiler/   # C++ code generation
│   └── runtime/      # Runtime library for recompiled code
├── include/          # Public headers
├── examples/         # Example code and demos
├── docs/             # Documentation
├── tests/            # Unit and integration tests
└── tools/            # Utility tools
```

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

For agent collaboration, see [agents.md](agents.md).

## Roadmap

- [x] Initial project structure
- [x] ISO/BIN parser implementation
- [x] PSX-EXE loader
- [ ] Basic MIPS R3000 disassembler
- [ ] IR design and implementation
- [ ] Simple function recompiler
- [ ] Runtime library basics
- [ ] First recompiled demo
- [ ] GPU emulation layer
- [ ] SPU emulation layer
- [ ] Full game support

## Resources

- [PSX Specifications](http://problemkaputt.de/psx-spx.htm)
- [MIPS R3000 Reference](https://www.linux-mips.org/wiki/R3000)
- [PSX ISO Format](http://wiki.osdev.org/ISO_9660)

## License

[MIT License](LICENSE) - See LICENSE file for details

## Acknowledgments

- PSX homebrew and reverse engineering community
- No$ PSX documentation by Martin Korth
- All contributors to this project

## Contact

- GitHub Issues: [Report bugs or request features](https://github.com/marinocg/psxrecomp/issues)
- Discussions: [Join the conversation](https://github.com/marinocg/psxrecomp/discussions)
