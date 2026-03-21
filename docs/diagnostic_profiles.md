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

Use watchpoints when you know a RAM cell, MMIO register, or small structure
that should hold a pointer, counter, state flag, interrupt mask, or consumer-side latch.

- `kind`: `ram_write`, `ram_read`, `mmio_write`, or `mmio_read`.
- `range`: inclusive RAM range to watch.
- `predicate`: optional guard such as `aligned_pointer_in_region`.
- `action`: `log`, `summarize`, `trap`, or `trap_on_first_violation`.

Current runtime behavior:

- `ram_write` events record `old` and `new` values
- `ram_read` events record the observed `value`
- `mmio_write` and `mmio_read` events record the observed transfer `value`
- step-budget stall reports include the most recent watchpoint events when any
  profile watchpoint fired during the run

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
- logs named register snapshots on entry when the profile supplies `registers`
- logs inline RAM samples when the profile supplies `memory_samples`
- logs configured GPU `GP1` MMIO reads while the tracepoint is active
- optional `capture_context` adds caller PC, resume address, callback generation,
  and IRQ snapshot to entry/exit logs
- optional `caller_histogram` keeps a compact caller count summary
- optional `max_log_events` hard-caps per-tracepoint emitted log lines; once the
  cap is reached the runtime emits one `event=log_limit` line and keeps only a
  compact suppressed-count summary
- optional `repeat_threshold` emits a repeat signature when the same caller,
  callback context, pending IRQ state, and captured register snapshot re-enter
  a range repeatedly; once the threshold is exceeded, the runtime suppresses
  further per-visit entry/exit/branch/MMIO logs for that unchanged signature
  and keeps only a compact suppressed-repeat summary
- optional `log_branches` now emits a branch-decision event when the profile
  also supplies `branch_taken_pc` and `branch_not_taken_pc`; the log includes
  the branch PC, the observed next PC, the taken/not-taken result, and the
  captured operand registers
- step-budget stall reports include recent callback-path summaries with
  callback entry/exit PCs, descriptor/return-site context, IRQ snapshots,
  callback-generation changes, repeat counts, per-callback RAM write deltas,
  and committed register deltas
- callback RAM-write summaries now separate likely callback stack traffic from
  persistent RAM writes so nearby stack frames do not masquerade as game-state
  latches during investigation

### `validators`, `boundaries`, `metadata_watches`, `suspect_functions`

These sections are available for deeper investigations and semantic auditing.
They are useful when you already know the RAM structures you want to validate,
but they are not required for basic tracepoint/watchpoint workflows.

## rev2 profile

`profiles/rev2.diag.json` is aimed at the current `rev2` investigation.

It focuses on:

- the bounded delay helper at `0x15f944-0x15f99c`
- its two current caller functions at `0x15ee6c-0x15f0e8` and
  `0x15f0ec-0x15f2a8`
- the callback-owned state block at `0x15d8d0-0x15df84`
- the hot loop around `0x160840-0x16088c`, which includes the stalled PC
  `0x16085c`
- the two indirect-call windows at `0x15d7b4` and `0x16116c`
- the two RAM cells currently feeding one of those indirect calls:
  `0x80166cc8` and `0x80166cf8`
- the callback busy latch at `0x801654ea`
- the callback counter block around `0x801665b0`
- the consumer snapshot/compare path around `0x15d630-0x15d704`
- the delay-helper re-entry branch at `0x15f984-0x15f990`

That combination is useful for answering two questions:

1. Is the runner spinning in the same loop we see in the stall summary?
2. Which code path last populated the indirect-call slot/context cells before
   the stall?
3. Is the callback only toggling a short-lived busy latch, or is it also
   advancing a persistent counter/state word that mainline should consume?
4. Does the consumer side read the callback-owned state directly, or does it
   branch on a copied baseline that never reaches the expected value?

## Focused rev2 profiles

Use the focused `rev2` profiles when the broad profile has already narrowed the
problem to one subsystem:

- `profiles/rev2.dispatch.diag.json`: callback dispatch, first-level IRQ
  handlers, and interrupt-controller state.
- `profiles/rev2.next.diag.json`: delay-helper churn and the nearby consumer
  path.
- `profiles/rev2.request_path.diag.json`: the `0x154718..0x154778` parser loop,
  the `0x15abf8/0x15ace0` result-ack path, the `0x15d1b8/0x15d248`
  `REQUEST/BFRD + DMA3` path, and the `0x80166440/0x80166480/0x80166490`
  state block.
- `profiles/rev2.gen120.parser.diag.json`: exact-PC parser probes at
  `0x154718`, `0x15473c`, `0x15475c`, and `0x154778`, plus the
  `REQUEST/BFRD`, `HCLRCTL`, and `0x80166440/0x80166480/0x80166490` state
  correlation needed for the gen-120 parser-consumer investigation.
- `profiles/rev2.gen120.story.diag.json`: a semantic story profile for the
  final unacked `INT1` generation that watches the `0x80166490` header slot,
  the nearby `0x80166440/0x80166480` stream/frame state cells, and the
  `REQUEST/BFRD` and `HCLRCTL` paths so one run can answer whether the header
  arrives, is reread, advances counters, and reaches request/ack.
- `profiles/rev2.last_meaningful_sector.diag.json`: a compact late-buffer
  profile that correlates recent sector-sized CD DMA destinations with the
  source `INT1` generation/LBA, payload fingerprints, and later CPU reads so
  one run can explain whether buffers like `0x80187158` are parser-consumed or
  side buffers unrelated to the ack path.
- `profiles/rev2.decoder_handoff.diag.json`: merged decoder-handoff investigation for `0x8015221C`, `0x8015A00C`, the producer windows at `0x80161DA8/0x80161F38/0x8016226C`, the upstream hotspots at `0x80159DCC` and `0x8015A394`, and the `0x8015452C` decoder loop itself. It combines compact selector/producer tracepoints with an end-of-run provenance summary for the `0x80070400` input window, a zero-payload guard, and output-span accounting so one run can say whether the decoder got the wrong slot, a zero-only payload, or a runaway segment without multi-GB output.
- `profiles/rev2.slot0_callback_chain.diag.json`: focused slot-0 provenance for the current `rev2` failure. It traces the setup/publish chain at `0x80152B90`, `0x80159E8C`, and `0x80159D9C`, watches the callback globals at `0x80187958/5C/68`, and logs whether slot `0` ever receives descriptor length/metadata or payload bytes before `0x8015A00C` claims it.
- `profiles/rev2.queue-stage.diag.json`: queue-state investigation for the same handoff, with wider watches over `0x80185524..0x80185558` and `0x80187958..0x80187973`, plus targeted tracepoints through `0x8015A0EC` so one run can show whether the descriptor-production stage ever sets a nonzero header or callback state before `0x80159D9C` publishes slot `0`.
- `profiles/rev2.queue_helper.diag.json`: queue-helper investigation for the current `rev2` failure. It traces the helper/selector family at `0x8015B140` and `0x8015B688`, the callback helpers at `0x8015C76C` and `0x8015C7A0`, and the live descriptor-copy hotspots at `0x80159DCC` and `0x8015A394`, while watching slot-0 descriptor/payload writes and the queue-control globals. Use it when the broad slot/provenance profiles have already shown an empty decoder input and you need the first helper-stage transition that failed.
- `profiles/rev2.hotloop_state.diag.json`: exact-PC hot-loop probes at
  `0x154718`, `0x15473c`, `0x15475c`, and `0x154778` with per-visit register
  snapshots, plus the `REQUEST/BFRD`, `HCLRCTL`, and
  `0x80166440/0x80166480/0x80166490` state correlation needed for the final
  unacked-INT1 parser-state investigation.
- `profiles/rev2.hotloop_branches.diag.json`: branch-decision probes for the
  conditional hot-loop edges at `0x154720`, `0x154734`, and `0x154778`, with
  compared-register snapshots and taken/not-taken results.
- `profiles/rev2.dma_setup.diag.json`: DMA setup investigation around the
  control-object family at `0x801666b0..0x801666d0`, the later programming
  window at `0x15f410..0x15f68c`, and DMA channel MMIO writes.
- `profiles/rev2.spu_init.diag.json`: SPU control/status investigation for the
  same `0x801666b0..0x801666d0` object once it is known to point at the SPU
  block and `DPCR`.

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
