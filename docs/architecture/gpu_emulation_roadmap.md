# GPU Emulation Layer Roadmap

This roadmap tracks the steps needed to implement a PSX GPU emulation layer for the runtime.

## Current Status
- [x] GPU register map definitions and command list decoding.
- [x] Rendering backend integration.
- [x] GPU architecture proposal documented.

## Phase 1: Command & Register Coverage
- [x] Define GPU register set and status flags.
- [x] Parse GPU command packets (GP0/GP1) into structured commands.
- [x] Implement basic VRAM read/write operations.

## Phase 2: Rendering Pipeline (Accuracy First)
- [x] Implement a software rasterizer path for correctness (golden reference).
- [x] Support basic primitives (triangles, quads, sprites).
- [x] Build image comparison tooling against the software path.

## Phase 3: Backend Architecture & Semi-Accurate Renderer
- [x] Define a renderer interface for pluggable backends (software + hardware).
- [x] Add an optional GPU-accelerated backend (OpenGL/Vulkan/Metal).
- [x] Implement a semi-accurate hardware backend that targets speed while staying close to reference output.
- [x] Support runtime switching between software and hardware backends (state sync + frame boundary swap).

## Phase 4: Timing & Effects
- [x] Model GPU timing and command FIFO behavior.
- [x] Implement blending, texture page, and CLUT handling.
- [x] Add interlacing and display timing behavior.

## Phase 5: Validation & Testing
- [x] Add GPU command trace tests.
- [x] Compare output with known-good emulator captures.
- [x] Add regression tests for edge-case primitives.

## Phase 6: Enhancement & Modding Options (Opt-In)
- [x] Provide a post-processing/modding pipeline (shaders, upscaling, filters).
- [x] Add texture pack support (override textures by hash/ID).
- [x] Support additional backends/features (raytracing experiments, platform-specific renderers).
- [x] Expose configuration presets to toggle accuracy vs. enhancements.
