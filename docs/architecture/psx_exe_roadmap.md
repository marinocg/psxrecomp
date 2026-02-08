# PSX-EXE Loader Roadmap

This roadmap captures the current state of PSX-EXE parsing and the steps needed to support full
executable loading in the recompiler pipeline.

## Current Status
- [x] Header validation ("PS-X EXE" magic, core fields, title extraction).
- [x] Load-size inference when header reports zero.
- [x] RAM-range validation for load, BSS, stack, and entry point fields.
- [x] Program payload extraction into a contiguous buffer.
- [x] Support for validating and preserving header metadata beyond core fields.
- [x] Load helpers that map program data directly into a memory image.

## Phase 1: Format Completeness
- [x] Capture additional header fields (e.g., saved registers or vendor data if present).
- [x] Emit structured diagnostics for invalid headers (field-by-field error info).
- [x] Validate alignment requirements for load and BSS segments consistently.

## Phase 2: Loader Integration
- [x] Build a memory-image loader that places program data at loadAddress.
- [x] Zero-initialize the BSS region in the memory image.
- [x] Provide entry-point descriptors (PC, GP, SP) for downstream stages.

## Phase 3: Robustness & Testing
- [x] Add unit tests for header parsing edge cases (zero sizes, misaligned fields).
- [x] Add integration tests with known-good PSX-EXE fixtures.
- [x] Add logging for rejected executables and mismatched sizes.
