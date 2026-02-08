# ISO/BIN Parser Roadmap

This roadmap tracks the current ISO/BIN parser state and planned work to cover the PlayStation CD
image formats needed by the recompiler pipeline.

## Current Status
- [x] Primary Volume Descriptor (PVD) detection across 2048-byte and 2352-byte sector layouts.
- [x] ISO 9660 directory record parsing and hierarchical path traversal.
- [x] File extraction by path and sector streaming.
- [x] SYSTEM.CNF BOOT line parsing with fallback to root .EXE scan.
- [ ] BIN/CUE parsing for multi-track images and track-to-sector mapping.
- [ ] Mode 2 Form 1/Form 2 sector handling (XA data/audio awareness).
- [ ] Joliet or multi-extent file support (beyond basic ISO 9660 records).

## Phase 1: Format Coverage
- [ ] Parse CUE sheets and associate BIN files with track metadata.
- [ ] Determine data track start LBA from CUE or other metadata.
- [ ] Support Mode 2 Form 1 (2048-byte user data) and Form 2 (2324-byte user data).

## Phase 2: Filesystem Enhancements
- [ ] Parse and use ISO 9660 path tables for faster lookups.
- [ ] Handle multi-extent files and continuation records.
- [ ] Add Joliet directory entries for long filenames when present.

## Phase 3: Robustness & Tooling
- [ ] Add explicit error reporting/logging for malformed images.
- [ ] Validate volume metadata (volume size, logical block size) before reads.
- [ ] Add unit tests with small ISO/BIN fixtures and SYSTEM.CNF variations.

## Phase 4: Integration Features
- [ ] Provide convenience helpers to locate PSX-EXE across disc layouts.
- [ ] Expose track metadata (data vs audio) for later runtime integration.
