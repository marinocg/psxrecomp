#!/usr/bin/env python3
import argparse
import struct
import subprocess
from subprocess import CalledProcessError
from pathlib import Path


def u32(data, off):
    return struct.unpack_from('<I', data, off)[0]




def elf_candidates(path: Path):
    candidates = [path]

    # Same basename next to EXE (rare, but cheap check).
    same_dir_elf = path.with_suffix('.elf')
    if same_dir_elf.exists():
        candidates.append(same_dir_elf)

    # Common repo layout: demo root has EXE, build/<name>.elf contains link artifact.
    build_elf = path.parent / 'build' / f'{path.stem}.elf'
    if build_elf.exists():
        candidates.append(build_elf)

    return candidates

def inspect(path: Path, out_dir: Path):
    data = path.read_bytes()
    out = []
    out.append(f'file: {path}')
    out.append(f'size: {len(data)} bytes')
    out.append(f'magic: {data[:8]!r}')
    out.append(f'pc0(entry): 0x{u32(data, 0x10):08X}')
    out.append(f't_addr(load): 0x{u32(data, 0x18):08X}')
    out.append(f't_size: {u32(data, 0x1C)}')
    out.append(f's_addr: 0x{u32(data, 0x30):08X}')
    out.append(f's_size: {u32(data, 0x34)}')
    out.append(f'sp: 0x{u32(data, 0x30):08X}')
    out.append('')

    objdump = 'mipsel-none-elf-objdump'
    if subprocess.run(['bash', '-lc', f'command -v {objdump} >/dev/null']).returncode == 0:
        out.append('== objdump diagnostics ==')
        candidates = elf_candidates(path)

        ran = False
        for candidate in candidates:
            try:
                out.append(f'-- candidate: {candidate}')
                out.append(subprocess.check_output([objdump, '-f', str(candidate)], text=True, errors='ignore', stderr=subprocess.STDOUT))
                out.append(subprocess.check_output([objdump, '-d', str(candidate)], text=True, errors='ignore', stderr=subprocess.STDOUT)[:8000])
                ran = True
                break
            except CalledProcessError as exc:
                out.append(f'objdump failed for {candidate}: exit={exc.returncode}')
                if exc.output:
                    out.append(exc.output)
        if not ran:
            out.append('objdump unavailable for provided artifact format (this is non-fatal).')
    else:
        out.append('objdump not found in PATH')

    dest = out_dir / f'{path.stem}.inspect.txt'
    dest.write_text('\n'.join(out))
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('exe', nargs='+')
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for item in args.exe:
        written.append(inspect(Path(item), out_dir))
    for p in written:
        print(p)


if __name__ == '__main__':
    main()
