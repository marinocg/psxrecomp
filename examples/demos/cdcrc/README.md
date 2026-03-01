# PSn00bSDK CD-ROM CRC Diagnostic (`CDCRC`)

This microdemo reads a fixed 16-sector range from disc and verifies CRC32.

What it does:
- Locates `CRCSEED.BIN`.
- Reads 16 sectors once per second.
- Computes CRC32 and compares against hardcoded expected value:
  - `EXPECTED_CRC32 = 0xF105FC36`
- Displays explicit GREEN/RED status and pass/fail result.

Pass criteria:
- Exact CRC match.
- No mismatch drift across repeated iterations.
- No read errors.

Build:
```bash
cd examples/demos/cdcrc
make
```

Outputs:
- `CDCRC.EXE`
- `CDCRC.iso`
- `CDCRC.cue`
