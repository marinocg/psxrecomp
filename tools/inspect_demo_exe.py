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



def summarize_header(data: bytes):
    notes = []
    magic_ok = data[:8] == b'PS-X EXE'
    entry = u32(data, 0x10)
    load_addr = u32(data, 0x18)
    load_size = u32(data, 0x1C)
    stack = u32(data, 0x30)

    if not magic_ok:
        notes.append('ERROR: invalid PS-X EXE magic')
    if entry == 0 or load_addr == 0 or load_size == 0:
        notes.append('ERROR: zero entry/load/size field detected')
    if entry and entry < 0x80000000:
        notes.append('WARN: entry point is not in KSEG0')
    if load_addr and load_addr < 0x80000000:
        notes.append('WARN: load address is not in KSEG0')
    if stack == 0:
        notes.append('INFO: header stack is zero (startup code must initialize SP)')

    return notes


def analyze_disassembly(text: str):
    notes = []
    if '<deregister_tm_clones>:' in text:
        notes.append('INFO: GCC crt helper symbols detected (deregister/register_tm_clones).')

    marker = '<main>:'
    idx = text.find(marker)
    if idx != -1:
        tail = text[idx:idx + 1200]
        for line in tail.splitlines():
            if 'addiu	sp,sp,-' in line:
                try:
                    imm = int(line.split('addiu	sp,sp,-', 1)[1], 0)
                    notes.append(f'INFO: main stack frame allocates {imm} bytes.')
                    if imm > 4096:
                        notes.append('WARN: large main stack frame; consider static buffers.')
                except Exception:
                    pass
                break

    return notes


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

    out.append('== assessment ==')
    for note in summarize_header(data):
        out.append(note)
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
                disasm = subprocess.check_output([objdump, '-d', str(candidate)], text=True, errors='ignore', stderr=subprocess.STDOUT)
                out.append(disasm[:8000])
                for note in analyze_disassembly(disasm):
                    out.append(note)
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
