# Diagnostic Profiles

Diagnostic profiles let you enable targeted runtime diagnostics for a specific
game or investigation without hardcoding game-specific logic into the runtime.

## How to apply a profile

Set `PSXRECOMP_DIAG_PROFILE` to the JSON file you want the generated runner to
load:

```bash
PSXRECOMP_DIAG_PROFILE=/work/profiles/rev2.diag.json ./your_runner
```

Generated runners print a confirmation line when the profile loads
successfully.

## Profile file location

- Shared/default profile: `profiles/common/default.diag.json`
- Game-specific profiles: `profiles/*.diag.json`

Keep profiles focused on diagnosis. They should surface state transitions,
watched writes, and suspicious call paths; they should not change runtime
behavior.

## Supported sections

### `watchpoints`

Use RAM write watchpoints when you know a cell or small structure that should
hold a pointer, counter, or state flag.

- `kind`: currently `ram_write` is the practical path.
- `range`: inclusive RAM range to watch.
- `predicate`: optional guard such as `aligned_pointer_in_region`.
- `action`: `log`, `summarize`, `trap`, or `trap_on_first_violation`.

### `tracepoints`

Use PC-range tracepoints to mark hot loops, callback windows, or indirect call
sites.

Important: the runtime matches the normalized PCs reported by the generated
runner and stall summaries, not the original KSEG0 load addresses from the PS-X
EXE. For example, a game instruction at `0x8016085c` may appear at runtime as
`0x16085c`, and the tracepoint range must use the latter form.

Current runtime behavior:

- logs entry into a traced PC range
- logs exit from a traced PC range
- logs configured GPU `GP1` MMIO reads while the tracepoint is active

The `registers` and `log_branches` fields are accepted by the profile parser so
the intent stays documented, but the runtime currently emits entry/exit events
only.

### `validators`, `boundaries`, `metadata_watches`, `suspect_functions`

These sections are available for deeper investigations and semantic auditing.
They are useful when you already know the RAM structures you want to validate,
but they are not required for basic tracepoint/watchpoint workflows.

## rev2 profile

`profiles/rev2.diag.json` is aimed at the current `rev2` investigation.

It focuses on:

- the hot loop around `0x160840-0x16088c`, which includes the stalled PC
  `0x16085c`
- the two indirect-call windows at `0x15d7b4` and `0x16116c`
- the two RAM cells currently feeding one of those indirect calls:
  `0x80166cc8` and `0x80166cf8`

That combination is useful for answering two questions:

1. Is the runner spinning in the same loop we see in the stall summary?
2. Which code path last populated the indirect-call slot/context cells before
   the stall?

## Typical Docker workflow

```bash
docker run --rm --entrypoint bash -v "$PWD":/work -w /work psxrecomp-local -lc '
set -euo pipefail
PSXRECOMP_DIAG_PROFILE=/work/profiles/rev2.diag.json \
PSXRECOMP_PRESENT_FRAMEBUFFER=0 \
PSXRECOMP_MAX_STEPS=20000000 \
./path/to/generated_runner > /work/out/some-run/run.log 2>&1 || true
'
```
