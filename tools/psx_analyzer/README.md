# psx_analyzer: PS-X EXE normalization helper

This helper fixes a common extraction issue for PSX demos where executable
candidates are stored with raw CD sector wrappers. In that situation, tools that
expect `PS-X EXE` at byte 0 fail with errors like:

- `No valid PSX executable candidate found`
- `magic: Missing PS-X EXE signature`

## Usage

```bash
python3 tools/psx_analyzer/normalize_psx_exe.py HELLOWLD.EXE -o HELLOWLD.normalized.EXE
```

For CI automation:

```bash
python3 tools/psx_analyzer/normalize_psx_exe.py HELLOWLD.EXE \
  -o HELLOWLD.normalized.EXE --json
```

## What it does

The script tries the following strategies in order:

1. Direct check for `PS-X EXE` (normal executable).
2. Prefix trim when magic appears later in the file.
3. Mode 2 raw sector strip (`2352 -> 2048` bytes per sector), then check again.

If normalization succeeds, the output begins at the `PS-X EXE` header and can be
used by strict parsers/decompilers.


## CI workflow integration

The repository workflow `.github/workflows/demo_iso_validation.yml` now integrates
normalization into the demo pipeline itself:

1. Prepare demo staging content.
2. Normalize staged executable files in `staging_dir` **before ISO packing**.
3. Pack ISOs from normalized staging content.
4. Run recompiler validation.
5. Run emulator smoke validation (expected to fail on black-screen regressions).

This avoids a "spare tool" flow and ensures the fixed executable is what gets
packed into the generated image that the emulator boots.
