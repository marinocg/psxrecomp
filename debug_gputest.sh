#!/bin/bash
set -e
cd /psxrecomp
export PSXRECOMP_MAX_STEPS=2000000
export PSXRECOMP_TRACE_MMIO=1

ISO="/psxrecomp/out/recompiled-demos-windows-latest/inputs/GPUTEST.iso"
OUTDIR="/tmp/gtr"
mkdir -p "$OUTDIR"
./build/psxrecomp -o "$OUTDIR" "$ISO" 2>/dev/null

cd "$OUTDIR/GPUTEST/GPUTEST/_0x80010000_BF93A65C"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release 2>/dev/null >/dev/null
cmake --build build 2>/dev/null >/dev/null

echo "=== DMA register writes ==="
./build/GPUTEST__0x80010000_BF93A65C 2>&1 | grep -E "1f8010[0-9a-f]" | head -30

echo "=== All MMIO writes (first 20) ==="
./build/GPUTEST__0x80010000_BF93A65C 2>&1 | grep "\[mmio\] write" | head -20
