# Tools

This directory contains utility tools for the psxrecomp project.

## Current Tools

### Planned Tools

1. **iso_dump** - Extract and analyze PSX ISO files
   - List files in ISO
   - Extract individual files
   - Display ISO metadata

2. **mips_disasm** - Standalone MIPS disassembler
   - Disassemble binary files
   - Output assembly or annotated C++
   - Symbol mapping

3. **psx_analyzer** - Analyze PSX executables
   - Find function boundaries
   - Generate call graphs
   - Identify data sections

4. **test_rom_gen** - Generate simple test ROMs
   - Create minimal test cases
   - Target specific features
   - Automated test generation

## Tool Development

Each tool should:
- Have its own subdirectory
- Include a README with usage
- Be buildable independently
- Follow project coding standards

## Building Tools

```bash
cd build
cmake -DBUILD_TOOLS=ON ..
cmake --build .

# Tools will be in build/tools/
ls build/tools/
```

## Usage

Tools will be documented individually as they're developed.

Example (planned):
```bash
# Dump ISO contents
./tools/iso_dump game.iso

# Disassemble PSX-EXE
./tools/mips_disasm game.exe -o game.asm

# Analyze executable
./tools/psx_analyzer game.exe --call-graph
```

## Contributing Tools

When adding a new tool:
1. Create a subdirectory: `tools/toolname/`
2. Add CMakeLists.txt
3. Write README.md with usage
4. Add to main CMakeLists.txt
5. Document in this file

---

*Tools will be implemented as the project matures.*
