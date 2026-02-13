#!/usr/bin/env python3
import argparse
from pathlib import Path

SECTOR = 2048


def read_root_entries(iso: Path):
    data = iso.read_bytes()
    pvd_off = 16 * SECTOR
    if data[pvd_off + 0] != 1 or data[pvd_off + 1:pvd_off + 6] != b'CD001':
        raise RuntimeError(f'{iso}: invalid ISO9660 PVD')

    root = data[pvd_off + 156:pvd_off + 156 + 34]
    extent = int.from_bytes(root[2:6], 'little')
    size = int.from_bytes(root[10:14], 'little')
    dir_off = extent * SECTOR
    dir_data = data[dir_off:dir_off + size]

    names = []
    i = 0
    while i < len(dir_data):
        length = dir_data[i]
        if length == 0:
            i = ((i // SECTOR) + 1) * SECTOR
            continue
        rec = dir_data[i:i + length]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        if name == b'\x00' or name == b'\x01':
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
