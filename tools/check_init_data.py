#!/usr/bin/env python3
"""Check initial RAM value at a specific address from embedded EXE data."""
import re
import sys

filepath = sys.argv[1]
target_addr = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x80163DC0
base_addr = 0x80150000

with open(filepath, 'r') as f:
    in_data = False
    data_bytes = []
    for line in f:
        if 'kRamInitData[] =' in line:
            in_data = True
            continue
        if in_data:
            if '};' in line:
                break
            # Parse decimal comma-separated values
            for val_str in line.strip().rstrip(',').split(','):
                val_str = val_str.strip()
                if val_str:
                    if val_str.startswith('0x'):
                        data_bytes.append(int(val_str, 16))
                    else:
                        try:
                            data_bytes.append(int(val_str))
                        except ValueError:
                            pass

offset = target_addr - base_addr
print(f"Total data bytes: {len(data_bytes)}")
if offset + 4 <= len(data_bytes):
    val = data_bytes[offset] | (data_bytes[offset+1]<<8) | (data_bytes[offset+2]<<16) | (data_bytes[offset+3]<<24)
    print(f"At offset 0x{offset:x} (addr 0x{target_addr:08x}): 0x{val:08x} ({val})")
    for i in range(-16, 48, 4):
        off = offset + i
        if 0 <= off + 3 < len(data_bytes):
            v = data_bytes[off] | (data_bytes[off+1]<<8) | (data_bytes[off+2]<<16) | (data_bytes[off+3]<<24)
            addr = base_addr + off
            marker = " <-- TARGET" if i == 0 else ""
            print(f"  [0x{addr:08x}] = 0x{v:08x}{marker}")
else:
    print(f"Offset 0x{offset:x} out of range (data size: {len(data_bytes)})")
