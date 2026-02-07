# GPU Emulation Layer Roadmap

This roadmap tracks the steps needed to implement a PSX GPU emulation layer for the runtime.

## Current Status
- [ ] GPU register map definitions and command list decoding.
- [ ] Rendering backend integration.

## Phase 1: Command & Register Coverage
- [ ] Define GPU register set and status flags.
- [ ] Parse GPU command packets (GP0/GP1) into structured commands.
- [ ] Implement basic VRAM read/write operations.

## Phase 2: Rendering Pipeline (Accuracy First)
- [ ] Implement a software rasterizer path for correctness (golden reference).
- [ ] Support basic primitives (triangles, quads, sprites).
- [ ] Build image comparison tooling against the software path.

## Phase 3: Backend Architecture & Semi-Accurate Renderer
- [ ] Define a renderer interface for pluggable backends (software + hardware).
- [ ] Add an optional GPU-accelerated backend (OpenGL/Vulkan/Metal).
- [ ] Implement a semi-accurate hardware backend that targets speed while staying close to reference output.

## Phase 4: Timing & Effects
- [ ] Model GPU timing and command FIFO behavior.
- [ ] Implement blending, texture page, and CLUT handling.
- [ ] Add interlacing and display timing behavior.

## Phase 5: Validation & Testing
- [ ] Add GPU command trace tests.
- [ ] Compare output with known-good emulator captures.
- [ ] Add regression tests for edge-case primitives.

## Phase 6: Enhancement & Modding Options (Opt-In)
- [ ] Provide a post-processing/modding pipeline (shaders, upscaling, filters).
- [ ] Add texture pack support (override textures by hash/ID).
- [ ] Support additional backends/features (raytracing experiments, platform-specific renderers).
- [ ] Expose configuration presets to toggle accuracy vs. enhancements.
