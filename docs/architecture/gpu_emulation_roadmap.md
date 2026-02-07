# GPU Emulation Layer Roadmap

This roadmap tracks the steps needed to implement a PSX GPU emulation layer for the runtime.

## Current Status
- [ ] GPU register map definitions and command list decoding.
- [ ] Rendering backend integration.

## Phase 1: Command & Register Coverage
- [ ] Define GPU register set and status flags.
- [ ] Parse GPU command packets (GP0/GP1) into structured commands.
- [ ] Implement basic VRAM read/write operations.

## Phase 2: Rendering Pipeline
- [ ] Implement a software rasterizer path for correctness.
- [ ] Add optional GPU-accelerated backend (OpenGL/Vulkan).
- [ ] Support basic primitives (triangles, quads, sprites).

## Phase 3: Timing & Effects
- [ ] Model GPU timing and command FIFO behavior.
- [ ] Implement blending, texture page, and CLUT handling.
- [ ] Add interlacing and display timing behavior.

## Phase 4: Validation & Testing
- [ ] Add GPU command trace tests.
- [ ] Compare output with known-good emulator captures.
- [ ] Add regression tests for edge-case primitives.
