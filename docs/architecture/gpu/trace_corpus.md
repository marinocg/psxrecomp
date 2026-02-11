# GPU Trace Corpus (Phase 0 Baseline)

This corpus definition standardizes scene categories used for parity and regression testing.

## Coverage targets

The initial Phase 0 corpus includes three representative scene classes:

1. **2D UI-heavy**
   - Prioritizes rectangles, sprites, CLUT updates, and drawing area changes.
2. **3D geometry-heavy**
   - Prioritizes triangle/quad throughput, texture page changes, and semi-transparency ordering.
3. **FMV overlay**
   - Prioritizes transfer operations and overlay primitives during display mode changes.


## Current corpus maturity

The Phase 0 JSON files are **descriptor/golden metadata inputs**, not a complete raw trace archive yet.
Their immediate purpose is to lock contracts for tooling and CI design; full trace replay ingestion is
implemented later in Phase 7.

## Storage layout

- Corpus manifest: `tests/fixtures/gpu_trace_corpus/manifest.json`
- Per-scene descriptors: `tests/fixtures/gpu_trace_corpus/scenes/`
- Golden metadata examples: `tests/fixtures/gpu_trace_corpus/golden_frames/`

### Intended use of each JSON artifact

- `manifest.json`
  - Entry point consumed by replay/capture tooling to enumerate scenes and expected golden artifacts (and currently used as the contract definition before replay tooling lands).
  - Defines coverage dimensions (category + GP0/GP1 family tags) used for roadmap progress reporting.
- `scenes/*.json`
  - Per-scene workload descriptors used by trace-replay runners to select and label command streams; in Phase 0 these are planning descriptors that lock required scene coverage.
  - Declares command-mix expectations so corpus growth can be audited against targeted command families.
- `golden_frames/*.json`
  - Canonical per-frame output expectations used by automated frame comparison jobs; in Phase 0 these serve as schema-valid sample goldens until capture automation is wired.
  - Stores display semantics + hashes so parity gates can detect both pixel drift and metadata drift.

## Scene acceptance rules

A scene is accepted into the corpus when it provides:

- A deterministic command trace source identifier (`traceId`).
- Category tag (`ui_2d`, `geometry_3d`, `fmv_overlay`).
- Minimum command counts for the target command families.
- At least one golden metadata record conforming to
  `golden_frame_metadata.schema.json`.
- Notes for known unsupported behavior (if any) and expected fallback policy.

## Promotion workflow

1. Add scene descriptor to `scenes/`.
2. Add/refresh golden metadata in `golden_frames/`.
3. Update `manifest.json` coverage counters.
4. Run standard lint/tests.
5. Mark roadmap and status docs as updated.

This corpus is intentionally small for Phase 0 and is expected to grow before CI parity gates are
introduced in Phase 7.
