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

The repository workflow `.github/workflows/demo_iso_validation.yml` now runs this
normalizer against executable candidates in `${RUNNER_TEMP}/demo_iso_validation_out`
before the demo decompilation/validation step.

If your generation step uses a different folder, update `DEMO_OUT_DIR` in that
workflow.
