# Tools

This directory contains utility tools for the psxrecomp project.

## Current Tools

### `iso_fixture_generator.py` (implemented)

Generates deterministic ISO fixtures for debugging and validation:
- `bad_invalid_pvd.iso`: intentionally malformed image (parser should fail).
- `good_minimal.iso`: valid ISO with `SYSTEM.CNF` + minimal PS-X EXE.
- `good_demo.iso` (optional): valid ISO packaging a caller-provided demo EXE.
- `good_demo_with_assets.iso` (optional): demo EXE plus placeholder TIM/STR/XA assets (with --with-assets).

See: `tools/iso_fixture_generator.md`.

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

### `validate_iso_fixtures.sh` (implemented)

End-to-end validation helper that:
1. Generates bad/good fixtures.
2. Confirms bad fixture fails in CLI.
3. Confirms good fixture recompiles.
4. Configures/builds generated standalone CMake bundle.

Usage:
```bash
tools/validate_iso_fixtures.sh ./build/psxrecomp /tmp/psxrecomp_iso_validation
```

CI coverage:
- `.github/workflows/fixture-e2e.yml` now runs this script end-to-end for pull requests that touch the recompiler pipeline inputs and on `main`, while branch pushes use a lightweight build+targeted-test job to reduce Actions credits.
- Artifacts include fixture JSON outputs (`bad.json`, `good.json`, optional `good_rich.json`) and `fixtures_manifest.json` for triage.
