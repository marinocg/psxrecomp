#!/usr/bin/env python3
"""Normalize PS-X EXE files extracted from PSX images.

Some extraction pipelines hand out executables with per-sector headers still
attached (e.g. Mode 2 raw sectors with 24-byte prefixes). This tool detects and
removes those wrappers so downstream decompilation can recognize the PS-X EXE
magic.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

MAGIC = b"PS-X EXE"
HEADER_SIZE = 0x800
RAW_SECTOR_SIZE = 2352
RAW_USERDATA_OFFSET = 24
RAW_USERDATA_SIZE = 2048


def sector_strip_mode2_raw(data: bytes) -> bytes:
    """Strip 24-byte prefixes from raw 2352-byte sectors."""
    if len(data) < RAW_SECTOR_SIZE or len(data) % RAW_SECTOR_SIZE != 0:
        return b""

    chunks = []
    for index in range(0, len(data), RAW_SECTOR_SIZE):
        sector = data[index : index + RAW_SECTOR_SIZE]
        chunks.append(sector[RAW_USERDATA_OFFSET : RAW_USERDATA_OFFSET + RAW_USERDATA_SIZE])

    return b"".join(chunks)


def locate_psx_exe(data: bytes) -> int:
    """Return best magic offset or -1 when not found."""
    preferred_offsets = [0, RAW_USERDATA_OFFSET, 0x10, 0x18, 0x800, 0x818]
    for offset in preferred_offsets:
        if offset + len(MAGIC) <= len(data) and data[offset : offset + len(MAGIC)] == MAGIC:
            return offset

    scan_limit = min(len(data), 1024 * 1024)
    return data[:scan_limit].find(MAGIC)


def normalize_executable(data: bytes) -> tuple[bytes, dict]:
    diagnostics: dict[str, object] = {
        "inputLength": len(data),
        "strategy": "direct",
        "magicOffset": -1,
    }

    magic_offset = locate_psx_exe(data)

    # If magic sits on a raw-sector userdata boundary, prefer sector stripping.
    if magic_offset >= 0 and len(data) % RAW_SECTOR_SIZE == 0 and magic_offset % RAW_SECTOR_SIZE == RAW_USERDATA_OFFSET:
        stripped = sector_strip_mode2_raw(data)
        stripped_offset = locate_psx_exe(stripped)
        if stripped_offset >= 0:
            diagnostics["strategy"] = "strip-mode2-raw+trim-prefix"
            diagnostics["magicOffset"] = stripped_offset
            diagnostics["strippedLength"] = len(stripped)
            return stripped[stripped_offset:], diagnostics

    if magic_offset >= 0:
        diagnostics["magicOffset"] = magic_offset
        diagnostics["strategy"] = "trim-prefix" if magic_offset != 0 else "direct"
        return data[magic_offset:], diagnostics

    stripped = sector_strip_mode2_raw(data)
    if stripped:
        stripped_offset = locate_psx_exe(stripped)
        if stripped_offset >= 0:
            diagnostics["strategy"] = "strip-mode2-raw+trim-prefix"
            diagnostics["magicOffset"] = stripped_offset
            diagnostics["strippedLength"] = len(stripped)
            return stripped[stripped_offset:], diagnostics

    raise ValueError("Unable to locate PS-X EXE magic in input data.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Normalize PS-X EXE files for analysis/decompilation")
    parser.add_argument("input", type=Path, help="Path to extracted executable candidate")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Path to write normalized executable")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable summary")
    args = parser.parse_args()

    source = args.input.read_bytes()
    normalized, diagnostics = normalize_executable(source)

    if len(normalized) < HEADER_SIZE:
        raise ValueError("Normalized data is too short to contain a valid PS-X EXE header.")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(normalized)

    summary = {
        "success": True,
        "input": str(args.input),
        "output": str(args.output),
        "diagnostics": diagnostics,
    }

    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        print(f"Normalized PS-X EXE written to: {args.output}")
        print(f"Strategy: {diagnostics['strategy']}")
        print(f"Magic offset: {diagnostics['magicOffset']}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
