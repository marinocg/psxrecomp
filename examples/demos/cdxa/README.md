# PSn00bSDK CD-ROM Stress Demo (`CDXA`)

This demo is a sustained read stress test inspired by `examples/cdrom/cdxa`.

What it does:
- Locates `STRESS.XA` on disc.
- Continuously reads it in fixed-size chunks using asynchronous `CdRead` + `CdReadSync(1)` polling.
- Loops cleanly at end-of-file and keeps running.
- Tracks timeout/deadlock conditions where reads do not complete in time.

Pass criteria:
- Continuous looping over EOF.
- `Read errors == 0` and `Timeout errors == 0`.
- `RESULT: PASS (sustained loop stable)` once warmup loops complete.

Build:
```bash
cd examples/demos/cdxa
make
```

Outputs:
- `CDXA.EXE`
- `CDXA.iso`
- `CDXA.cue`
