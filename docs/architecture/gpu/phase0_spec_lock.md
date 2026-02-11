# GPU Phase 0 Spec Lock (Baseline Contract)

This document closes **Phase 0 — Spec Lock & Test Corpus** for the GPU emulation roadmap.
It defines what behavior is locked before implementation-heavy phases begin.

## 1) Canonical behavior matrix

The matrix below is intentionally scoped to command families required by the current runtime
interfaces: GP0 draw/env/transfer families and GP1 control family.

### GP0 draw commands

| Family | Command range/examples | Required decode behavior | Required runtime behavior | Determinism requirement |
|---|---|---|---|---|
| Flat polygons | `0x20` triangle, `0x28` quad | Exact packet length and opcode dispatch. | Vertex order must be preserved; renderer receives canonical primitive with decoded parameters. | Same input trace must yield identical primitive stream and framebuffer hash. |
| Rectangles/sprites | `0x64`, plus fixed-size rectangle variants (future) | Decode packet headers and coordinate words without lossy reinterpretation. | Coordinate clipping and drawing area constraints must be applied according to active draw state. | Primitive decode and clipping decisions are stable across backends. |
| Line primitives | `0x40–0x5F` (future) | Reject malformed packet lengths and keep FIFO alignment. | Line stepping conventions are backend-independent in `reference` mode. | Hash-stable line output in reference backend. |
| Draw environment | `0xE1`..`0xE5` | Single-word command parsing with no packet bleed. | Draw mode/texture window/drawing area/offset update visible state immediately for following draw commands. | State transitions must be replay-safe in trace playback. |

### GP0 transfer commands

| Family | Command range/examples | Required decode behavior | Required runtime behavior | Determinism requirement |
|---|---|---|---|---|
| CPU -> VRAM transfer setup | `0xA0` family (future) | Setup packet size and payload count tracked explicitly. | Enter transfer payload state machine until expected words consumed. | Payload boundaries are exact and reproducible. |
| VRAM -> CPU transfer setup | `0xC0` family (future) | Decode setup packet and expected readback size. | Read path must expose data in documented packing order. | Readback sequence reproducible for fixed VRAM state. |
| VRAM -> VRAM blit | `0x80` family (future) | Decode source/destination/size words and clamp/wrap bounds. | Blit semantics follow PSX-visible overlap and wrapping behavior. | Blit result hash-stable in reference backend. |

### GP1 control commands

| Family | Command range/examples | Required decode behavior | Required runtime behavior | Determinism requirement |
|---|---|---|---|---|
| Reset + command buffer control | `0x00`, `0x01`, `0x02` | Parse opcode and immediate bits exactly. | Reset must restore documented state and clear transient packet state. | Replay from reset point yields same status and frame hashes. |
| Display enable/mode/range | `0x03`, `0x05`, `0x06`, `0x07`, `0x08` | Decode without collapsing field bits. | Active display state influences output metadata (resolution/interlace/field). | Golden metadata fields match across runs. |
| DMA direction + IRQ ack | `0x04`, `0x10` (read register), IRQ ack behavior | Bit-accurate decode for direction and ack control bits. | GPUSTAT/DMA interaction must reflect configured direction and command readiness. | Status traces identical for deterministic command streams. |

## 2) Accuracy tiers and allowed deltas

Three tiers are locked to prevent ambiguous quality targets during implementation.

| Tier | Purpose | Required guarantees | Allowed deltas |
|---|---|---|---|
| `reference` | Ground-truth software path and CI baseline. | Deterministic raster output, deterministic command timing model (project-defined), strict draw-state semantics. | **No visual delta** for conformance scenes; metadata hash must match golden exactly unless scene explicitly marks non-deterministic fields. |
| `semi-accurate` | Fast path with bounded drift for development. | Command decode/register behavior must match reference; may simplify raster edge cases. | Per-frame pixel mismatch <= 0.5% on corpus scenes, no missing primitives, no metadata field drift for resolution/crop/interlace. |
| `enhanced` | Opt-in user features outside parity mode. | Must preserve gameplay-visible correctness and command ordering; all enhancements toggleable. | Visual deltas allowed by design, but enhanced mode is excluded from parity gates and must never run in reference CI jobs. |

## 3) Unsupported behavior policy

Unsupported behavior handling is standardized to keep failures actionable:

| Scenario | Release behavior | Debug behavior | Telemetry requirement |
|---|---|---|---|
| Unimplemented but skippable GPU command | Emit warning once per opcode family and continue with conservative fallback. | Emit warning + structured trace marker. | Counter incremented in per-frame GPU stats. |
| Command with unsafe/unknown side effects | Fallback to no-op with explicit warning. | Hard-fail (`assert`/fatal) behind debug guard to force implementation before parity promotion. | Include opcode, packet words, and active draw state in log. |
| Corrupt packet stream (size mismatch) | Drop packet and resynchronize at next command boundary; emit warning. | Hard-fail with packet history snapshot. | Record packet history window and FIFO depth. |
| Unsupported enhanced-only feature requested in reference mode | Ignore enhancement flag and log policy warning. | Same as release. | Emit tier-mismatch event. |

Debug hard-fail points are expected during Phase 1–3 bring-up and should be removed/reduced only
after parity corpus pass rates are stable.

## 4) Artifact links for Phase 0

- Trace corpus index: `tests/fixtures/gpu_trace_corpus/manifest.json`
- Scene descriptors: `tests/fixtures/gpu_trace_corpus/scenes/*.json`
- Golden frame metadata samples: `tests/fixtures/gpu_trace_corpus/golden_frames/*.json`
- Metadata schema: `docs/architecture/gpu/golden_frame_metadata.schema.json`

These artifacts are intentionally versioned to unblock deterministic replay tooling in Phase 7.
