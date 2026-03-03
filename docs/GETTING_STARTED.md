# Getting Started with PSXRecomp Development

This guide will help you get started with developing PSXRecomp.

## Prerequisites

Before you begin, ensure you have:

- **C++ Compiler**: GCC 8+, Clang 7+, or MSVC 2019+
- **CMake**: Version 3.15 or higher
- **Git**: For version control
- **Text Editor/IDE**: VS Code, CLion, Visual Studio, or your preferred editor

### Optional Tools

- **Valgrind**: For memory leak detection (Linux)
- **GDB/LLDB**: For debugging
- **clang-format**: For code formatting
- **Doxygen**: For generating documentation

## Setting Up Your Development Environment

### Clone the Repository

```bash
git clone https://github.com/marinocg/psxrecomp.git
cd psxrecomp
```

### Build the Project

```bash
# Create and enter build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
cmake --build .

# Alternatively, use make directly on Unix-like systems
make

# Or ninja if you prefer
cmake -G Ninja ..
ninja
```

### Project Structure

```
psxrecomp/
├── src/                    # Source code
│   ├── iso/               # ISO parsing
│   ├── disasm/            # Disassembler
│   ├── ir/                # Intermediate representation
│   ├── recompiler/        # Code generation
│   ├── runtime/           # Runtime library
│   └── main.cpp           # CLI entry point
├── include/               # Public headers
│   └── psxrecomp/         # API headers
├── tests/                 # Tests
│   ├── unit/              # Unit tests
│   └── integration/       # Integration tests
├── examples/              # Examples and demos
├── docs/                  # Documentation
├── tools/                 # Utility tools
├── CMakeLists.txt         # Build configuration
├── README.md              # Project overview
├── CONTRIBUTING.md        # Contribution guidelines
├── agents.md              # Agent collaboration guide
└── LICENSE                # License file
```

## Development Workflow

### 1. Create a Feature Branch

```bash
git checkout -b feature/your-feature-name
```

### 2. Make Your Changes

Follow the coding standards in [agents.md](../agents.md):
- Use C++17 features
- Follow the naming conventions (camelCase for functions, PascalCase for classes)
- Add comments for complex logic
- Write tests for new functionality

### 3. Build and Test

```bash
# Build
cd build
cmake --build .

# Run tests
ctest --output-on-failure

# Or run specific tests
./tests/unit/iso_parser_test
./tests/unit/disasm_test
```

### 4. Format Your Code

```bash
# Using clang-format (if configured)
clang-format -i src/**/*.cpp include/**/*.h
```

### 5. Commit Your Changes

```bash
git add .
git commit -m "Add ISO parser implementation

- Implement ISO 9660 directory parsing
- Add support for Mode 2 sectors
- Add tests for ISO parser"
```

### 6. Push and Create Pull Request

```bash
git push origin feature/your-feature-name
```

Then create a pull request on GitHub.

## Development Tasks

### Adding a New Component

1. **Create header** in `include/psxrecomp/component/`
2. **Create implementation** in `src/component/`
3. **Add to CMakeLists.txt**
4. **Write tests** in `tests/unit/`
5. **Update documentation**

Example:
```bash
# Create files
touch include/psxrecomp/iso/iso_parser.h
touch src/iso/iso_parser.cpp
touch tests/unit/iso_parser_test.cpp

# Add to CMakeLists.txt
# Add your source files to the appropriate *_SOURCES list and ensure
# they are part of the corresponding component library target
# (for example, psxrecomp_iso / psxrecomp_disasm / psxrecomp_ir /
# psxrecomp_recompiler / psxrecomp_runtime).
```

### Running Individual Tests

```bash
cd build

# List available tests
ctest -N

# Run specific test
ctest -R iso_parser_test -V

# Run with verbose output
ctest --output-on-failure
```

### Debugging

#### Using GDB (Linux/macOS)

```bash
cd build
gdb ./psxrecomp
(gdb) run input.bin -o output
(gdb) break main
(gdb) continue
```

#### Using LLDB (macOS)

```bash
cd build
lldb ./psxrecomp
(lldb) run input.bin -o output
(lldb) breakpoint set --name main
(lldb) continue
```

#### Using Visual Studio (Windows)

1. Open the project in Visual Studio
2. Set breakpoints in the code
3. Press F5 to start debugging

### Memory Leak Detection

#### Valgrind (Linux)

```bash
cd build
valgrind --leak-check=full ./psxrecomp test.bin
```

#### AddressSanitizer (All platforms)

```bash
cd build
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -g" ..
cmake --build .
./psxrecomp test.bin
```

## Understanding the Codebase

### Key Concepts

1. **ISO Parsing**: Reading PSX CD-ROM images
   - Files: `src/iso/`, `include/psxrecomp/iso/`
   - Handles ISO 9660 filesystem

2. **Disassembly**: Converting MIPS machine code to instructions
   - Files: `src/disasm/`, `include/psxrecomp/disasm/`
   - Decodes MIPS R3000 instructions

3. **IR Generation**: Creating intermediate representation
   - Files: `src/ir/`, `include/psxrecomp/ir/`
   - Platform-independent representation

4. **Recompilation**: Generating C++ code
   - Files: `src/recompiler/`, `include/psxrecomp/recompiler/`
   - Converts IR to C++

5. **Runtime**: PSX hardware emulation
   - Files: `src/runtime/`, `include/psxrecomp/runtime/`
   - GPU, SPU, memory management

### Reading the Code

Start with:
1. `README.md` - Project overview
2. `agents.md` - Coding conventions and architecture
3. `docs/architecture/pipeline.md` - System architecture
4. `include/psxrecomp/` - Public API headers
5. `examples/demos/` - Example code

## Common Development Tasks

### Adding a MIPS Instruction

1. Add opcode to `include/psxrecomp/disasm/instruction.h`
2. Implement decoder in `src/disasm/mips_disassembler.cpp`
3. Add IR generation
4. Add C++ code generation
5. Write test

### Adding a Hardware Component

1. Create header in `include/psxrecomp/runtime/`
2. Implement in `src/runtime/`
3. Integrate with `PsxSystem`
4. Add tests
5. Document in `docs/hardware/`

### Writing Tests

PSXRecomp unit tests are currently simple C++ executables that use `assert()` for validation.
Keep tests focused, cover edge cases (branch targets, delay slots, unknown opcodes), and add new
test executables to `CMakeLists.txt` as needed.

## Useful Resources

### Documentation
- [Architecture](../architecture/pipeline.md)
- [Hardware Reference](../hardware/psx_reference.md)
- [MIPS Instructions](../hardware/mips_instruction_set.md)

### External Resources
- [PSX-SPX Documentation](http://problemkaputt.de/psx-spx.htm)
- [MIPS R3000 Manual](https://www.linux-mips.org/wiki/R3000)
- [ISO 9660 Specification](http://wiki.osdev.org/ISO_9660)

### Similar Projects
- [PCSX-Redux](https://github.com/grumpycoders/pcsx-redux) - PSX emulator
- [DuckStation](https://github.com/stenzek/duckstation) - PSX emulator
- [N64Recomp](https://github.com/Mr-Wiseguy/N64Recomp) - N64 static recompiler

## Getting Help

- **Documentation**: Check `docs/` directory
- **Issues**: [GitHub Issues](https://github.com/marinocg/psxrecomp/issues)
- **Discussions**: [GitHub Discussions](https://github.com/marinocg/psxrecomp/discussions)
- **Agent Guidelines**: See [agents.md](../agents.md)

## Next Steps

Once you're set up:

1. Read through the existing code
2. Try building a simple component
3. Write some tests
4. Contribute your improvements!

Happy coding! 🎮
