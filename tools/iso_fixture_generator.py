#!/usr/bin/env python3
"""Generate reproducible PSX ISO fixtures for parser/pipeline debugging.

Produces baseline fixtures:
- bad_invalid_pvd.iso: intentionally malformed image (no ISO-9660 PVD)
- good_minimal.iso: valid ISO with SYSTEM.CNF + minimal PS-X EXE payload

Optional richer fixtures:
- good_demo.iso: valid ISO containing a caller-supplied demo EXE
- good_demo_with_assets.iso: valid ISO containing demo EXE + sample assets
  (TIM/STR/XA placeholders) to exercise resource discovery/export paths.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence

SECTOR_SIZE = 2048
PVD_SECTOR = 16
TERM_SECTOR = 17
PATH_TABLE_SECTOR = 18
ROOT_DIR_SECTOR = 20
TOTAL_SECTORS = 128


@dataclass
class IsoFile:
    name: str
    data: bytes


def write_le16(buf: bytearray, off: int, value: int) -> None:
    buf[off : off + 2] = struct.pack("<H", value)


def write_le32(buf: bytearray, off: int, value: int) -> None:
    buf[off : off + 4] = struct.pack("<I", value)


def write_path_table_entry(buf: bytearray, offset: int, name: bytes, extent: int, parent: int) -> int:
    name_len = len(name)
    buf[offset] = name_len
    buf[offset + 1] = 0
    write_le32(buf, offset + 2, extent)
    write_le16(buf, offset + 6, parent)
    if name_len:
        buf[offset + 8 : offset + 8 + name_len] = name
    offset += 8 + name_len
    if name_len % 2 == 1:
        buf[offset] = 0
        offset += 1
    return offset


def write_directory_record(buf: bytearray, offset: int, name: bytes, extent: int, size: int, flags: int) -> int:
    name_len = len(name)
    rec_len = 33 + name_len + (1 if name_len % 2 == 0 else 0)
    buf[offset] = rec_len
    buf[offset + 1] = 0
    write_le32(buf, offset + 2, extent)
    write_le32(buf, offset + 10, size)
    buf[offset + 25] = flags
    buf[offset + 26] = 0
    buf[offset + 27] = 0
    write_le16(buf, offset + 28, 1)
    buf[offset + 32] = name_len
    if name_len:
        buf[offset + 33 : offset + 33 + name_len] = name
    return rec_len


def create_minimal_psx_exe() -> bytes:
    header_size = 0x800
    load_size = 0x100
    exe = bytearray(header_size + load_size)
    exe[0:8] = b"PS-X EXE"
    write_le32(exe, 0x18, 0x80010000)  # t_addr
    write_le32(exe, 0x1C, load_size)  # t_size
    write_le32(exe, 0x10, 0x80010000)  # pc0
    payload = b"\x00\x00\x00\x00" * 4 + b"\x08\x00\xE0\x03" + b"\x00\x00\x00\x00"
    exe[header_size : header_size + len(payload)] = payload
    return bytes(exe)


def create_sample_assets() -> List[IsoFile]:
    tim = b"\x10\x00\x00\x00" + b"PSXRECOMP_TIM_PLACEHOLDER"
    str_data = b"PSXRECOMP_STR_PLACEHOLDER_STREAM"
    xa_data = b"PSXRECOMP_XA_PLACEHOLDER_AUDIO"
    return [
        IsoFile("ASSETS/TEXTURE.TIM", tim),
        IsoFile("ASSETS/INTRO.STR", str_data),
        IsoFile("ASSETS/THEME.XA", xa_data),
    ]


def build_valid_iso(label: str, system_cnf_boot: str, files: Sequence[IsoFile]) -> bytes:
    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    # PVD
    pvd_off = PVD_SECTOR * SECTOR_SIZE
    image[pvd_off] = 1
    image[pvd_off + 1 : pvd_off + 6] = b"CD001"
    image[pvd_off + 6] = 1
    image[pvd_off + 8 : pvd_off + 8 + 11] = b"PLAYSTATION"
    label_b = label.encode("ascii", errors="ignore")[:31]
    image[pvd_off + 40 : pvd_off + 40 + len(label_b)] = label_b
    write_le32(image, pvd_off + 80, TOTAL_SECTORS)
    write_le16(image, pvd_off + 120, 1)
    write_le16(image, pvd_off + 124, 1)
    write_le16(image, pvd_off + 128, SECTOR_SIZE)

    # root-only path table
    pt_off = PATH_TABLE_SECTOR * SECTOR_SIZE
    pt_end = write_path_table_entry(image, pt_off, b"\x00", ROOT_DIR_SECTOR, 1)
    write_le32(image, pvd_off + 132, pt_end - pt_off)
    write_le32(image, pvd_off + 140, PATH_TABLE_SECTOR)

    rr = pvd_off + 156
    image[rr] = 34
    write_le32(image, rr + 2, ROOT_DIR_SECTOR)
    write_le32(image, rr + 10, SECTOR_SIZE)
    image[rr + 25] = 0x02
    write_le16(image, rr + 28, 1)
    image[rr + 32] = 1
    image[rr + 33] = 0

    t_off = TERM_SECTOR * SECTOR_SIZE
    image[t_off] = 255
    image[t_off + 1 : t_off + 6] = b"CD001"
    image[t_off + 6] = 1

    # allocate sectors
    next_sector = ROOT_DIR_SECTOR + 2
    file_layout: list[tuple[IsoFile, int, int]] = []
    for file_entry in files:
        sectors = max(1, math.ceil(len(file_entry.data) / SECTOR_SIZE))
        if next_sector + sectors >= TOTAL_SECTORS:
            raise ValueError("Fixture files do not fit image; increase TOTAL_SECTORS.")
        file_layout.append((file_entry, next_sector, len(file_entry.data)))
        next_sector += sectors

    # write root dir
    root_off = ROOT_DIR_SECTOR * SECTOR_SIZE
    cursor = root_off
    cursor += write_directory_record(image, cursor, b"\x00", ROOT_DIR_SECTOR, SECTOR_SIZE, 0x02)
    cursor += write_directory_record(image, cursor, b"\x01", ROOT_DIR_SECTOR, SECTOR_SIZE, 0x02)
    for file_entry, sector, size in file_layout:
        name = file_entry.name.upper().replace("/", "_") + ";1"
        cursor += write_directory_record(image, cursor, name.encode("ascii"), sector, size, 0x00)

    # write file contents
    for file_entry, sector, _size in file_layout:
        off = sector * SECTOR_SIZE
        image[off : off + len(file_entry.data)] = file_entry.data

    # write SYSTEM.CNF into its file if present
    for file_entry, sector, _size in file_layout:
        if file_entry.name.upper() == "SYSTEM.CNF":
            cnf = f"BOOT = cdrom:\\{system_cnf_boot}\n".encode("ascii")
            off = sector * SECTOR_SIZE
            image[off : off + len(cnf)] = cnf
            break

    return bytes(image)


def build_bad_iso() -> bytes:
    data = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    marker = b"NOT_AN_ISO9660_IMAGE"
    data[SECTOR_SIZE : SECTOR_SIZE + len(marker)] = marker
    return bytes(data)


def write_outputs(output_dir: Path, demo_exe: Path | None, with_assets: bool) -> List[dict[str, str]]:
    output_dir.mkdir(parents=True, exist_ok=True)
    artifacts: List[dict[str, str]] = []

    bad_path = output_dir / "bad_invalid_pvd.iso"
    bad_path.write_bytes(build_bad_iso())
    artifacts.append({"file": bad_path.name, "description": "Malformed ISO fixture: intentionally lacks ISO-9660 PVD."})

    minimal_files = [
        IsoFile("SYSTEM.CNF", b""),
        IsoFile("GAME.EXE", create_minimal_psx_exe()),
    ]
    good_path = output_dir / "good_minimal.iso"
    good_path.write_bytes(
        build_valid_iso("PSXRECOMP_GOOD_MIN", "GAME.EXE;1", minimal_files)
    )
    artifacts.append({
        "file": good_path.name,
        "description": "Valid ISO with SYSTEM.CNF + minimal PS-X EXE; good for parser/pipeline sanity.",
    })

    if demo_exe is not None:
        demo_files = [IsoFile("SYSTEM.CNF", b""), IsoFile("DEMO.EXE", demo_exe.read_bytes())]
        demo_iso = output_dir / "good_demo.iso"
        demo_iso.write_bytes(build_valid_iso("PSXRECOMP_DEMO", "DEMO.EXE;1", demo_files))
        artifacts.append({
            "file": demo_iso.name,
            "description": "Valid ISO with caller-supplied demo EXE payload.",
        })

        if with_assets:
            rich_files = list(demo_files) + create_sample_assets()
            rich_iso = output_dir / "good_demo_with_assets.iso"
            rich_iso.write_bytes(build_valid_iso("PSXRECOMP_DEMO_ASSETS", "DEMO.EXE;1", rich_files))
            artifacts.append({
                "file": rich_iso.name,
                "description": "Valid ISO with demo EXE + placeholder TIM/STR/XA assets for resource extraction checks.",
            })

    notes = [
        "good_minimal.iso is intentionally tiny and does not render graphics/audio in emulator.",
        "Use --demo-exe for emulator-visible behavior; add --with-assets to include sample TIM/STR/XA files.",
    ]
    info = {"artifacts": artifacts, "notes": notes}
    (output_dir / "fixtures_manifest.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")

    lines = ["PSXRecomp ISO fixtures", "=====================", ""]
    lines.extend([f"- {item['file']}: {item['description']}" for item in artifacts])
    lines.extend([
        "",
        "Validation workflow:",
        "  1) bad_invalid_pvd.iso should fail parser/open with diagnostics.",
        "  2) good_minimal.iso should pass parser/pipeline and emit C++ output.",
        "  3) good_demo_with_assets.iso (if present) should export TIM/STR resources and compile output bundle.",
    ])
    (output_dir / "README.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    return artifacts


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate reproducible PSX ISO debug fixtures.")
    parser.add_argument("--output-dir", required=True, help="Directory to write fixtures into.")
    parser.add_argument("--demo-exe", default="", help="Optional path to a PS-X EXE demo payload.")
    parser.add_argument(
        "--with-assets",
        action="store_true",
        help="When used with --demo-exe, also generate good_demo_with_assets.iso containing sample TIM/STR/XA files.",
    )
    args = parser.parse_args()

    out_dir = Path(args.output_dir).resolve()
    demo_exe = Path(args.demo_exe).resolve() if args.demo_exe else None
    if demo_exe is not None and not demo_exe.exists():
        raise FileNotFoundError(f"Demo EXE not found: {demo_exe}")
    if args.with_assets and demo_exe is None:
        raise ValueError("--with-assets requires --demo-exe.")

    artifacts = write_outputs(out_dir, demo_exe, args.with_assets)
    print(f"Wrote {len(artifacts)} fixture(s) to {out_dir}")
    for item in artifacts:
        print(f" - {item['file']}: {item['description']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
