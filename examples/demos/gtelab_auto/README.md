# PSn00bSDK GTE Auto Lab (`GTELAB`)

`gtelab_auto` is an autonomous GTE validation/demo workload for PSXRecomp runtime and recompiler work.

## Behavior

- No controller input required.
- Uploads fixed matrices, vectors, and color state to COP2/GTE.
- Prints live register probe values each frame.
- Draws a small rotating lit 3D scene.

## COP2/GTE coverage exercised

- `MTC2` / `MFC2`
- `CTC2` / `CFC2`
- `LWC2` / `SWC2`
- `RTPS` / `RTPT`
- `NCLIP`
- `AVSZ3` / `AVSZ4`
- `MVMVA`
- `NCDS` lighting path

## Build

```bash
cd examples/demos/gtelab_auto
make
```

Outputs:

- `GTELAB.EXE`
- `GTELAB.iso`
- `GTELAB.cue`
