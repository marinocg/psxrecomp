#!/usr/bin/env python3
import argparse
from pathlib import Path

LOGICAL_SECTOR = 2048


def _looks_like_pvd(buf: bytes) -> bool:
    return len(buf) >= 6 and buf[0] == 1 and buf[1:6] == b'CD001'


def _read_logical_sector(data: bytes, lba: int, sector_size: int, payload_offset: int) -> bytes:
    start = lba * sector_size + payload_offset
    end = start + LOGICAL_SECTOR
    if end > len(data):
        raise RuntimeError('ISO appears truncated while reading logical sector')
    return data[start:end]


def _detect_layout(data: bytes):
    # Try common layouts:
    # - plain ISO9660 image: 2048-byte sectors
    # - raw Mode1: 2352-byte sectors with 16-byte header before payload
    # - raw Mode2/XA: 2352-byte sectors with 24-byte header/subheader before payload
    candidates = [
        (2048, 0),
        (2352, 16),
        (2352, 24),
    ]
    for sector_size, payload_offset in candidates:
        try:
            pvd = _read_logical_sector(data, 16, sector_size, payload_offset)
        except RuntimeError:
            continue
        if _looks_like_pvd(pvd):
            return sector_size, payload_offset
    raise RuntimeError('invalid ISO9660 PVD (checked 2048 and raw 2352 layouts)')


def _read_logical_span(data: bytes, extent_lba: int, size_bytes: int, sector_size: int, payload_offset: int) -> bytes:
    out = bytearray()
    remaining = size_bytes
    lba = extent_lba
    while remaining > 0:
        chunk = _read_logical_sector(data, lba, sector_size, payload_offset)
        take = min(remaining, LOGICAL_SECTOR)
        out.extend(chunk[:take])
        remaining -= take
        lba += 1
    return bytes(out)


def read_root_entries(iso: Path):
    data = iso.read_bytes()
    sector_size, payload_offset = _detect_layout(data)

    pvd = _read_logical_sector(data, 16, sector_size, payload_offset)
    root = pvd[156:156 + 34]
    extent = int.from_bytes(root[2:6], 'little')
    size = int.from_bytes(root[10:14], 'little')
    dir_data = _read_logical_span(data, extent, size, sector_size, payload_offset)

    names = []
    i = 0
    while i < len(dir_data):
        length = dir_data[i]
        if length == 0:
            i = ((i // LOGICAL_SECTOR) + 1) * LOGICAL_SECTOR
            continue
        rec = dir_data[i:i + length]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        if name in (b'\x00', b'\x01'):
            i += length
            continue
        names.append(name.decode('ascii', errors='ignore'))
        i += length
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('iso', nargs='+')
    args = ap.parse_args()

    for item in args.iso:
        iso = Path(item)
        names = read_root_entries(iso)
        print(f'ISO: {iso}')
        for n in names:
            print(f'  {n}')

        upper = {n.upper() for n in names}
        if 'SYSTEM.CNF;1' not in upper:
            raise SystemExit(f'{iso}: missing SYSTEM.CNF;1 in root')
        exe_entries = [n for n in upper if n.endswith('.EXE;1')]
        if not exe_entries:
            raise SystemExit(f'{iso}: missing *.EXE;1 in root')


if __name__ == '__main__':
    main()
