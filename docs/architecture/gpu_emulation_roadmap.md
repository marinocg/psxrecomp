# GPU Emulation Layer Roadmap

This roadmap tracks the work needed to implement a production-grade PSX GPU emulation layer in the runtime. It is organized into explicit phases with trackable checkboxes for both:
- an **accuracy-first software renderer** (reference path), and
- a **high-performance hardware renderer** (validated against the software reference).

## Current Status Snapshot

### Foundation
- [x] Basic GPU register/status scaffolding exists.
- [x] Baseline GP0/GP1 packet parsing exists for a limited command subset.
- [x] VRAM write buffer exists (simplified semantics).

### Reference Software Renderer
- [x] Software renderer interface implementation exists.
- [x] Basic primitive stubs are rendered for smoke tests.
- [ ] Pixel-accurate PSX rasterization rules are not yet implemented.

### Hardware Renderer
- [x] Backend abstraction exists.
- [x] Semi-accurate backend scaffold exists.
- [ ] No true GPU API backend (OpenGL/Vulkan/Metal) is implemented.

### Validation
- [x] Unit tests exist for command tracing + basic renderer parity checks.
- [ ] No capture-based parity suite against known-good emulator output yet.

---

## Phase 0 — Spec Lock & Test Corpus (Prerequisite)
Goal: lock expected behavior before deeper implementation.

- [x] Define canonical GPU behavior matrix by command family (GP0 draw, GP0 transfer, GP1 control).
- [x] Define required accuracy tiers (`reference`, `semi-accurate`, `enhanced`) and allowed deltas.
- [x] Build trace corpus from representative games/demos (2D UI-heavy, 3D geometry-heavy, FMV-overlay) used as deterministic replay inputs for Phase 7 trace harness + CI drift checks.
- [x] Add golden frame metadata format (resolution, crop, interlace field, CRC/hash) used as the canonical per-frame contract for capture comparison thresholds and parity gating.
- [x] Document unsupported behavior policy (warn, fallback, hard-fail in debug).


Phase 0 spec artifacts:
- `docs/architecture/gpu/phase0_spec_lock.md` (normative behavior/tier/fallback rules consumed by implementation and review checklists)
- `docs/architecture/gpu/trace_corpus.md` (how corpus JSON files are curated/promoted for parity workloads)
- `docs/architecture/gpu/golden_frame_metadata.schema.json` (schema that replay/capture tooling validates against before comparing hashes)
- `tests/fixtures/gpu_trace_corpus/` (versioned manifest/scenes/golden metadata JSON fixtures that define the Phase 0 contract; fully automated replay consumption is delivered in Phase 7)

## Phase 1 — Command Processor & Register Correctness
Goal: command ingestion reflects PSX packet semantics and register behavior.

- [x] Complete GP0 packet sizing/decoding coverage (draw, transfer, env, misc).
- [x] Complete GP1 control command coverage (reset, DMA direction, display range/mode, interrupt ack).
- [x] Implement command FIFO behavior and command consumption boundaries accurately.
- [x] Implement GPUSTAT bit-accurate behavior for FIFO/DMA/display/interlace state.
- [x] Implement DMA direction + linked-list handling interactions with GPU command ingestion.
- [x] Add exhaustive command decoder tests (valid + malformed packets).

## Phase 2 — VRAM & Transfer Pipeline
Goal: VRAM behavior matches hardware-visible semantics.

- [x] Implement CPU->VRAM transfer mode semantics (setup + payload state machine).
- [x] Implement VRAM->CPU readback mode semantics.
- [x] Implement VRAM->VRAM blit semantics.
- [x] Implement transfer clipping/wrapping behavior (1024x512 bounds).
- [x] Validate transfer endian/packing behavior and masking rules.
- [x] Add deterministic VRAM operation tests (including edge wrapping cases).

## Phase 3 — Reference Software Rasterizer (Accuracy First)
Goal: establish a trusted, deterministic software baseline.

- [x] Implement pixel-accurate triangle rasterization rules (edge inclusion/top-left conventions).
- [x] Implement quad decomposition/raster behavior consistent with PSX ordering.
- [x] Implement line rendering rules (including degenerate and steep-slope cases).
- [x] Implement sprite drawing with correct texel fetch behavior.
- [x] Implement drawing area clip, draw offset, and texture window behavior.
- [x] Implement texture sampling modes (4/8/16-bit), CLUT lookup, and texture page selection.
- [x] Implement transparency/blending modes and mask bit behavior.
- [x] Implement dithering and color modulation paths.
- [x] Implement semi-transparency ordering behavior and relevant edge cases.
- [x] Add software renderer conformance tests per primitive/effect type.

## Phase 4 — Display, Timing, and Synchronization
Goal: synchronize command processing and display output semantics.

- [ ] Implement display range and display mode behavior (resolution, interlace, field toggling).
- [ ] Implement frame/scanline timing model integrated with scheduler ticks.
- [ ] Model command throughput limits and busy/ready timing transitions.
- [ ] Integrate GPU IRQ signaling behavior (where applicable).
- [ ] Add timing regression tests (FIFO drain rates, interlace field cadence).

## Phase 5 — Hardware Renderer Architecture
Goal: deliver a performant renderer that remains behaviorally close to reference.

- [ ] Define backend-agnostic render graph/command translation layer.
- [ ] Implement resource lifetime model (texture atlas/cache, command buffers, frame targets).
- [ ] Implement synchronization model between emulated GPU state and host API state.
- [ ] Implement capability abstraction for OpenGL/Vulkan/Metal feature variance.
- [ ] Add backend startup/shutdown recovery and device-loss handling.
- [ ] Add backend telemetry hooks (draw calls, batches, cache hit rates, stalls).

## Phase 6 — Hardware Backend Implementation(s)
Goal: implement real GPU API backend(s) with parity gates.

- [ ] Implement first concrete backend (choose one: OpenGL/Vulkan/Metal).
- [ ] Implement primitive pipeline parity with software renderer output.
- [ ] Implement texture/CLUT/blending parity paths.
- [ ] Implement transfer/upload strategy for VRAM representation on host GPU.
- [ ] Implement fallback paths for unsupported host features.
- [ ] Add backend-specific correctness tests and stress cases.

## Phase 7 — Cross-Backend Validation & Tooling
Goal: make parity measurable and enforceable in CI.

- [ ] Add automated frame capture comparison (software vs hardware) with threshold policy.
- [ ] Add trace replay harness for deterministic GPU command streams.
- [ ] Add known-good external capture comparison suite (per title/test-scene).
- [ ] Add per-command and per-frame debugging overlays (register state, transfer state, primitive stats).
- [ ] Add CI gating for regression thresholds (hard fail on severe drift).

## Phase 8 — Performance Optimization (Without Breaking Parity)
Goal: improve throughput while preserving validated correctness.

- [ ] Profile software renderer hotspots and optimize scan conversion/fetch paths.
- [ ] Add command batching and state-change minimization in hardware backend.
- [ ] Add texture cache and CLUT cache optimization.
- [ ] Add configurable quality/performance knobs with explicit parity impact notes.
- [ ] Add benchmark suite (micro + scene-level + title-level).

## Phase 9 — Opt-In Enhancements & Modding Layer
Goal: support non-accurate enhancements without contaminating reference mode.

- [ ] Add strict separation between `accurate` and `enhanced` pipelines.
- [ ] Implement post-processing chain hooks (scalers/CRT/etc.) for enhanced mode.
- [ ] Implement texture override pipeline (hash/ID mapping) with cache invalidation rules.
- [ ] Add user-facing renderer preset profiles and docs.
- [ ] Add validation to guarantee enhancements are disabled in reference parity runs.

---

## Milestone Gates

### Gate A: Reference-Ready
- [ ] Phases 0–4 completed.
- [ ] Software renderer passes conformance + capture comparison baseline.

### Gate B: Hardware-Ready
- [ ] Phases 5–7 completed.
- [ ] Hardware backend parity within approved thresholds on corpus.

### Gate C: Production-Ready
- [ ] Phase 8 completed with benchmark targets met.
- [ ] Phase 9 completed with strict mode separation validated.

## Ongoing Checklist (Every GPU PR)
- [ ] Added/updated tests for touched GPU command families.
- [ ] Verified no regressions in software reference parity.
- [ ] Verified docs/roadmap progress checkboxes updated.
- [ ] Verified no unbounded memory growth paths introduced.
