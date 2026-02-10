# GPU Emulation Layer Roadmap

This roadmap tracks the steps needed to implement a PSX GPU emulation layer for the runtime.

## Current Status
- [x] GPU register map definitions and initial command decoding.
- [x] Rendering backend interface integration.
- [x] GPU architecture proposal documented.

## Phase 1: Command & Register Coverage
- [x] Define GPU register set and status flags.
- [x] Parse a baseline subset of GPU command packets (GP0/GP1) into structured commands.
- [x] Implement basic VRAM write operations.
- [ ] Implement robust VRAM readback and transfer semantics.

## Phase 2: Rendering Pipeline (Accuracy First)
- [x] Implement a software rasterizer baseline path for correctness checks.
- [x] Support basic primitives (triangles, quads, sprites, fill rectangle).
- [x] Build frame comparison tooling against the software path.
- [ ] Improve primitive raster rules to be pixel-accurate with PSX edge rules.

## Phase 3: Backend Architecture & Semi-Accurate Renderer
- [x] Define a renderer interface for pluggable backends (software + hardware).
- [x] Implement a semi-accurate backend scaffold using the common renderer interface.
- [x] Support runtime switching between software and semi-accurate backends.
- [ ] Add a true GPU-accelerated backend (OpenGL/Vulkan/Metal).

## Phase 4: Timing & Effects
- [x] Model baseline GPU timing and command FIFO consumption behavior.
- [x] Add texture page and CLUT state handling hooks.
- [x] Add interlacing/display mode state handling.
- [ ] Implement blending rules and full display timing behavior.

## Phase 5: Validation & Testing
- [x] Add GPU command trace tests.
- [x] Add renderer parity checks using frame comparison helpers.
- [x] Add regression tests for representative primitive paths.
- [ ] Compare output with known-good emulator captures in automated tests.

## Phase 6: Enhancement & Modding Options (Opt-In)
- [x] Keep runtime resource-pack support available for future GPU asset overrides.
- [ ] Provide a post-processing/modding pipeline (shaders, upscaling, filters).
- [ ] Add texture pack support (override textures by hash/ID).
- [ ] Support additional backends/features (raytracing experiments, platform-specific renderers).
- [ ] Expose configuration presets to toggle accuracy vs. enhancements.
