# ISO/BIN Parser Roadmap

This roadmap tracks the current ISO/BIN parser state and planned work to cover the PlayStation CD
image formats needed by the recompiler pipeline.

## Current Status
- [x] Primary Volume Descriptor (PVD) detection across 2048-byte and 2352-byte sector layouts.
- [x] ISO 9660 directory record parsing and hierarchical path traversal.
- [x] File extraction by path and sector streaming.
- [x] SYSTEM.CNF BOOT line parsing with fallback to root .EXE scan.
- [x] BIN/CUE parsing for multi-track images and track-to-sector mapping.
- [x] Mode 2 Form 1/Form 2 sector handling (XA data/audio awareness).
- [x] Joliet or multi-extent file support (beyond basic ISO 9660 records).

## Phase 1: Format Coverage
- [x] Parse CUE sheets and associate BIN files with track metadata.
- [x] Determine data track start LBA from CUE or other metadata.
- [x] Support Mode 2 Form 1 (2048-byte user data) and Form 2 (2324-byte user data).

## Phase 2: Filesystem Enhancements
- [x] Parse and use ISO 9660 path tables for faster lookups.
- [x] Handle multi-extent files and continuation records.
- [x] Add Joliet directory entries for long filenames when present.

## Phase 3: Robustness & Tooling
- [x] Add explicit error reporting/logging for malformed images.
- [x] Validate volume metadata (volume size, logical block size) before reads.
- [x] Add unit tests with small ISO/BIN fixtures and SYSTEM.CNF variations.

## Phase 4: Integration Features
- [x] Provide convenience helpers to locate PSX-EXE across disc layouts.
- [x] Expose track metadata (data vs audio) for later runtime integration.
- [x] Add export helpers for non-code resources (TIM textures, STR videos, XA audio).

## Phase 5: Advanced Formats & Performance
- [x] Support multi-session discs and mixed-mode edge cases.
- [x] Add multi-disc set handling (disc swaps, shared metadata, cumulative track tables).
- [x] Add sector caching and streaming to reduce redundant reads.
- [x] Improve BIN/CUE parsing for uncommon cue syntax and pregap variants.
- [x] Validate XA audio sector metadata for runtime streaming.
- [x] XA resource fallback for 2048-byte ISO images when raw subheaders are unavailable.
- [x] Add fixture tooling for malformed/good/rich ISO generation and validation scripts.
- [ ] Integrate multi-disc set with runtime disc swap workflows.
