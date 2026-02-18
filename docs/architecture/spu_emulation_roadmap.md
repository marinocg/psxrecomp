# SPU Emulation Layer Roadmap

This roadmap outlines the steps needed to implement the PSX SPU emulation layer in the runtime.

## Current Status
- [x] SPU register map and voice/channel model definitions.
- [x] Audio backend integration.
- [x] SPU architecture proposal documented.

## Phase 1: Core SPU Model
- [x] Define SPU registers, voices, and ADSR envelopes.
- [x] Implement SPU RAM read/write and DMA interactions.
- [x] Decode ADPCM samples and handle pitch stepping.

## Phase 2: Audio Output
- [x] Mix voices into a stereo output buffer.
- [x] Add reverb and volume controls.
- [x] Integrate with platform audio backend (SDL, etc.).

## Phase 3: Timing & Accuracy
- [ ] Model SPU timing and IRQ behavior.
- [x] Handle key-on/key-off and channel state transitions.
- [ ] Validate against known SPU test ROMs.

## Phase 4: Validation & Testing
- [x] Add unit tests for ADPCM decoding.
- [ ] Add regression tests for envelope edge cases.
- [ ] Add audio output comparison tests.
