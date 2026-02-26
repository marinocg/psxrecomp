# PSn00bSDK COP0 Test ISO (PSX)

This demo is a focused COP0 probe for PSXRecomp runtime validation:

- Reads COP0 `Status`, `Cause` and `EPC` with `mfc0`
- Writes `Status` (sets IEc) with `mtc0`
- Installs a tiny exception handler that records `Cause`/`EPC`, returns to `EPC+4` with `jr` + `rfe`
- Triggers a `syscall` exception and verifies the program continues

## Build

```bash
cd examples/demos/cop0test
make
```

Outputs:
- `COP0TEST.EXE`
- `COP0TEST.iso`
- `COP0TEST.cue`

## Clean

```bash
make clean
```
