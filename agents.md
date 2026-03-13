# Agent Collaboration Guide

This document provides guidelines for AI agents working on the psxrecomp project to ensure effective collaboration and consistent code quality.

## Project Overview

PSXRecomp is a static recompiler that converts PlayStation 1 games into native C++ binaries. The project requires deep understanding of:

- MIPS R3000 architecture
- PlayStation 1 hardware (GPU, SPU, CD-ROM, memory map)
- Binary file formats (ISO 9660, PSX-EXE)
- Static recompilation techniques
- Intermediate representations (IR)

## Key Conventions

### Code Style

- **Language**: Modern C++17 (or C++20 where available)
- **Naming**:
  - Classes/Structs: `PascalCase` (e.g., `IsoParser`, `MipsDisassembler`)
  - Functions/Methods: `camelCase` (e.g., `parseIso()`, `disassembleFunction()`)
  - Variables: `camelCase` (e.g., `instructionPointer`, `memoryAddress`)
  - Constants: use `UPPER_SNAKE_CASE` for global hardware constants and `camelCase` for local/test constants (e.g., `RAM_SIZE`, `totalSectors`)
  - Enum constants: `PascalCase` (e.g., `Data`, `Audio`)
  - Namespaces: primarily `lower_case`, with PascalCase allowed for grouped domains like `MemoryMap` or `Registers`
  - Private members: prefix with `m_` (e.g., `m_data`, `m_parser`)
- **Formatting**: Use clang-format with the provided `.clang-format` file
- **Linting**: Keep clang-tidy limited to the agreed-upon subset in `.clang-tidy` (clang-analyzer, minimal modernize checks, and naming enforcement)
- **Headers**: Use `#pragma once` for header guards

### Project Structure

```
src/
├── iso/          - ISO/BIN file parsing and extraction
├── disasm/       - MIPS disassembler (decode machine code)
├── ir/           - Intermediate representation
├── recompiler/   - C++ code generation from IR
├── runtime/      - Runtime library (PSX hardware abstraction)
└── main.cpp      - CLI entry point

include/
└── psxrecomp/    - Public API headers

tests/
├── unit/         - Unit tests for individual components
└── integration/  - End-to-end integration tests

examples/
└── demos/        - Example recompiled code and demos
```

### Testing

- Write unit tests for all new functionality
- Use Google Test framework (or compatible)
- Test file naming: `*_test.cpp`
- Aim for >80% code coverage for critical paths
- Include edge cases and error conditions
- Use the repository Makefile targets (`configure`, `build`, `test`, `lint`) for quick discovery.
- Prefer Ninja builds for faster iteration when available:
  - Configure: `cmake -S . -B build -G Ninja -DBUILD_TESTS=ON -DBUILD_CLI=ON`
  - Build: `cmake --build build`
  - Test: `ctest --test-dir build --output-on-failure`

### Documentation

- Use Doxygen-style comments for public APIs
- Document complex algorithms inline
- Update README.md when adding major features
- Keep docs/ folder updated with architectural decisions
- Review and update roadmap/status docs (master roadmap, gaps, implementation status) when adding
  significant features or milestones.

### Git Workflow

- Keep commits atomic and focused
- Write clear commit messages:
  ```
  Short summary (50 chars or less)
  
  Detailed explanation if needed. Reference issues with #123.
  ```
- Branch naming: `feature/description` or `fix/description`
- Always update tests with code changes

## Component Responsibilities

### ISO Parser (`src/iso/`)

- Parse ISO 9660 filesystem
- Extract files from PSX CD images
- Handle BIN/CUE format
- Locate PSX executable (PSX-EXE)

**Key considerations**: Handle multi-track CDs, XA audio data, Mode 2 sectors

### Disassembler (`src/disasm/`)

- Decode MIPS R3000 instructions
- Handle branch delay slots
- Identify function boundaries
- Resolve jump tables and indirect calls

**Key considerations**: Co-processor instructions (GTE), cache control, exceptions

### IR Generator (`src/ir/`)

- Convert disassembled code to IR
- Perform control flow analysis
- Build call graph
- Track register usage and data flow

**Key considerations**: Handle self-modifying code, memory-mapped I/O, DMA

### Recompiler (`src/recompiler/`)

- Generate C++ from IR
- Optimize generated code
- Preserve game semantics
- Handle PSX-specific behaviors

**Key considerations**: Timing accuracy, memory aliasing, hardware quirks

### Runtime Library (`src/runtime/`)

- Emulate PSX hardware interfaces
- GPU rendering (OpenGL/Vulkan backend)
- SPU audio synthesis
- Input/output abstraction
- Save state management

**Key considerations**: Performance, accuracy trade-offs, cross-platform compatibility

## Common Tasks for Agents

### Adding a New MIPS Instruction

1. Add opcode definition to `include/psxrecomp/instructions.h`
2. Implement disassembly in `src/disasm/mips_decoder.cpp`
3. Add IR conversion in `src/ir/ir_generator.cpp`
4. Implement C++ generation in `src/recompiler/codegen.cpp`
5. Add test cases in `tests/unit/disasm_test.cpp`

### Adding a Hardware Component

1. Create interface in `include/psxrecomp/runtime/`
2. Implement in `src/runtime/`
3. Add initialization in `src/runtime/psx_system.cpp`
4. Document hardware behavior in `docs/hardware/`
5. Add example usage in `examples/`

### Debugging Recompiled Code

- Compare execution traces with emulator
- Check for timing-sensitive code
- Verify memory layout matches PSX
- Test with known-good ROM images first
- Use diagnostic profiles in `profiles/` via `PSXRECOMP_DIAG_PROFILE` when an issue is game-specific; see `docs/diagnostic_profiles.md`

## Best Practices

1. **Incremental Development**: Build features incrementally, testing at each step
2. **Reference Implementations**: Study existing emulators (PCSX, DuckStation) for hardware details
3. **Performance**: Profile before optimizing, focus on hot paths
4. **Portability**: Test on multiple platforms (Windows, Linux, macOS)
5. **Error Handling**: Always validate input, handle malformed ISO files gracefully
6. **Memory Safety**: Use smart pointers, avoid raw memory manipulation where possible
7. **Logging**: Add detailed logging for debugging (use levels: DEBUG, INFO, WARN, ERROR)
8. **Maintainability**: Refactor large files into focused helpers or smaller modules to keep functions readable and testable

## Resources for Agents

### Essential Documentation

- **PSX-SPX**: Comprehensive PSX hardware documentation
  - http://problemkaputt.de/psx-spx.htm
  
- **MIPS R3000 Manual**: CPU instruction set
  - https://www.linux-mips.org/wiki/R3000

- **ISO 9660**: CD-ROM filesystem format
  - http://wiki.osdev.org/ISO_9660

### Reference Projects

- **PCSX-Redux**: Modern PSX emulator
- **DuckStation**: High-accuracy PSX emulator  
- **N64Recomp**: Similar static recompilation project for N64

### Test ROMs

- Use homebrew ROMs for initial testing
- Keep commercial ROM testing private
- Document test cases in `tests/test_roms/README.md`

## Communication

### For Agent-to-Agent

- Document major decisions in `docs/decisions/`
- Use clear variable and function names (self-documenting code)
- Add TODO comments with context: `// TODO: Handle XA-ADPCM audio format`

### For Agent-to-Human

- Report progress in pull requests
- Summarize changes in commit messages
- Flag breaking changes or API modifications
- Ask for clarification on ambiguous requirements

## Quality Checklist

Before completing a task:

- [ ] Code builds without warnings
- [ ] All tests pass
- [ ] New tests added for new functionality
- [ ] Documentation updated
- [ ] Code follows project style
- [ ] No memory leaks (run valgrind or sanitizers)
- [ ] Cross-platform compatibility checked
- [ ] Security considerations reviewed (file parsing, memory access)

## Getting Help

- Check existing code for patterns
- Review `docs/` for architectural decisions
- Look at test cases for usage examples
- Consult PSX-SPX documentation for hardware details
- Review similar recompilation projects for techniques

## Anti-Patterns to Avoid

❌ **Don't**: Hardcode game-specific patches (aim for generic solutions)  
❌ **Don't**: Assume little-endian host architecture  
❌ **Don't**: Mix emulation and recompilation approaches  
❌ **Don't**: Skip error handling for "impossible" cases  
❌ **Don't**: Ignore timing-sensitive code  
❌ **Don't**: Break existing tests without good reason

✅ **Do**: Write portable, maintainable code  
✅ **Do**: Test edge cases thoroughly  
✅ **Do**: Document non-obvious decisions  
✅ **Do**: Keep components loosely coupled  
✅ **Do**: Preserve game behavior accurately

## Version History

- **v1.0** (2026-02-07): Initial agent collaboration guidelines

---

**Remember**: The goal is to create a robust, accurate, and performant static recompiler that brings PSX games to modern platforms while preserving their original behavior. Quality over speed, accuracy over approximation.
