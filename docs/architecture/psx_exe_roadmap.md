# PSX-EXE Loader Roadmap

This roadmap captures the current state of PSX-EXE parsing and the steps needed to support full
executable loading in the recompiler pipeline.

## Current Status
- [x] Header validation ("PS-X EXE" magic, core fields, title extraction).
- [x] Load-size inference when header reports zero.
- [x] RAM-range validation for load, BSS, stack, and entry point fields.
- [x] Program payload extraction into a contiguous buffer.
- [ ] Support for validating and preserving header metadata beyond core fields.
- [ ] Load helpers that map program data directly into a memory image.

## Phase 1: Format Completeness
- [ ] Capture additional header fields (e.g., saved registers or vendor data if present).
- [ ] Emit structured diagnostics for invalid headers (field-by-field error info).
- [ ] Validate alignment requirements for load and BSS segments consistently.

## Phase 2: Loader Integration
- [ ] Build a memory-image loader that places program data at loadAddress.
- [ ] Zero-initialize the BSS region in the memory image.
- [ ] Provide entry-point descriptors (PC, GP, SP) for downstream stages.

## Phase 3: Robustness & Testing
- [ ] Add unit tests for header parsing edge cases (zero sizes, misaligned fields).
- [ ] Add integration tests with known-good PSX-EXE fixtures.
- [ ] Add logging for rejected executables and mismatched sizes.
