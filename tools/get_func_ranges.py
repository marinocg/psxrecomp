#!/usr/bin/env python3
"""Extract function PC ranges from a generated runner.cpp."""
import re
import sys

runner = sys.argv[1] if len(sys.argv) > 1 else ''
lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x15d800
hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x15e000

with open(runner) as f:
    text = f.read()

matches = re.findall(r'\{(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(func_[^}]+)\}', text)
for start, end, name in matches:
    sv = int(start, 16)
    if lo <= sv <= hi:
        print(f'{start}\t{end}\t{name.strip()}')
