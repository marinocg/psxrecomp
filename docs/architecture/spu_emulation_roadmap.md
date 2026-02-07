# SPU Emulation Layer Roadmap

This roadmap outlines the steps needed to implement the PSX SPU emulation layer in the runtime.

## Current Status
- [ ] SPU register map and voice/channel model definitions.
- [ ] Audio backend integration.
- [ ] SPU architecture proposal documented.

## Phase 1: Core SPU Model
- [ ] Define SPU registers, voices, and ADSR envelopes.
- [ ] Implement SPU RAM read/write and DMA interactions.
- [ ] Decode ADPCM samples and handle pitch stepping.

## Phase 2: Audio Output
- [ ] Mix voices into a stereo output buffer.
- [ ] Add reverb and volume controls.
- [ ] Integrate with platform audio backend (SDL, etc.).

## Phase 3: Timing & Accuracy
- [ ] Model SPU timing and IRQ behavior.
- [ ] Handle key-on/key-off and channel state transitions.
- [ ] Validate against known SPU test ROMs.

## Phase 4: Validation & Testing
- [ ] Add unit tests for ADPCM decoding.
- [ ] Add regression tests for envelope edge cases.
- [ ] Add audio output comparison tests.
