# PSn00bSDK CD-ROM Smoke Demo (`CDBROWSE`)

This demo is a CD filesystem smoke test for PSXRecomp CD-ROM work.

What it does:
- Calls `CdInit()`.
- Repeatedly reads and parses ISO9660 sectors directly (PVD at LBA 16 + root directory extent) using `CdRead`.
- Displays parsed root directory entries and volume label.
- Hashes parsed listing results and flags instability if metadata drifts between refreshes.

Pass criteria:
- Stable root directory listing across repeated refreshes.
- No sector-read or parse failures.
- No hangs while refreshing.

Build:
```bash
cd examples/demos/cdbrowse
make
```

Outputs:
- `CDBROWSE.EXE`
- `CDBROWSE.iso`
- `CDBROWSE.cue`
