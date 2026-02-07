---
name: ISO/BIN parser implementation
about: Implement the PSX ISO/BIN parser described in the README.
title: "Implement PSX ISO/BIN parser"
labels: enhancement
---

## Summary

Implement the ISO/BIN parser so PSX CD images can be opened, validated, and queried for
files and PSX executables as described in the README.

## Scope

- Parse ISO 9660 primary volume descriptor.
- Support raw BIN images (Mode 2 sectors) and standard ISO images.
- Load root directory and directory records.
- Extract files by path.
- Locate PSX executable via `SYSTEM.CNF` or fallback `.EXE` scan.

## Acceptance Criteria

- `IsoParser::open()` validates the image and reads the volume descriptor.
- `IsoParser::extractFile()` returns the correct byte content for a path.
- `IsoParser::findExecutable()` resolves the PSX executable path.
- Unit tests cover the parser with a minimal ISO fixture.

## Notes

- Keep parsing resilient to malformed directory entries.
- Track Mode 2 sector offsets (2352 bytes, 24-byte user data offset).
