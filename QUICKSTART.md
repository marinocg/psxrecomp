Get a demo running end-to-end:

PSXRECOMP_MAX_STEPS is set to a high value to allow the demo to run to completion; adjust as needed for testing or if the demo hangs. The framebuffer will be dumped as a PPM file for post-mortem analysis, and a 320x240 crop of the display area will also be saved as a PNG for easier viewing. Logs from each step of the process (recompilation, CMake configuration/build, and runtime execution) will be saved in the output directory for debugging.

Put demos in `out/recompiled-demos-ubuntu-latest/inputs`

```bash
make build

export ISONAME="COP0TEST"
export HOST_OUT="$PWD/out/manual-validation/$ISONAME"

RUNNAME="manual-validation/$ISONAME"
ISO="$PWD/out/recompiled-demos-ubuntu-latest/inputs/$ISONAME.iso"
HOST_OUT="$PWD/out/$RUNNAME"
GEN_ROOT="$PWD/out/$RUNNAME/gen"
BUILD_ROOT="$PWD/out/$RUNNAME/build"
rm -rf "$GEN_ROOT" "$BUILD_ROOT" "$HOST_OUT"
mkdir -p "$HOST_OUT"
$PWD/build/psxrecomp --json -o "$GEN_ROOT" "$ISO" > "$HOST_OUT/recompile.result.json" 2> "$HOST_OUT/recompile.stderr.log"
SRC_DIR=$(find "$GEN_ROOT" -type f -name CMakeLists.txt | head -1 | xargs dirname)
cmake -S "$SRC_DIR" -B "$BUILD_ROOT" -G Ninja -DPSXRECOMP_ENABLE_LOGGING=ON -DPSXRECOMP_LOG_LEVEL=4 > "$HOST_OUT/cmake.configure.log" 2>&1
cmake --build "$BUILD_ROOT" > "$HOST_OUT/cmake.build.log" 2>&1
RUNNER=$(find "$BUILD_ROOT" -type f -perm -u+x ! -path "*/CMakeFiles/*" ! -name "*.a" ! -name "*.o" | head -1)
if [ -d "$SRC_DIR/resources" ]; then cp -R "$SRC_DIR/resources" "$(dirname "$RUNNER")/"; fi
PSXRECOMP_PRESENT_FRAMEBUFFER=0 PSXRECOMP_MAX_STEPS=500000000 PSXRECOMP_LOG_LEVEL=info PSXRECOMP_RENDER_DEBUG_OVERLAY=1 PSXRECOMP_DUMP_FRAMEBUFFER="$HOST_OUT/${ISONAME}_framebuffer.ppm" "$RUNNER" > "$HOST_OUT/run.log" 2>&1 || true

python3 - <<'PY'
import pathlib, struct, zlib, os
# get isoname from env
isoname=os.getenv('ISONAME', 'unknown')
base=pathlib.Path(f'out/manual-validation/{isoname}')
ppm=base/f'{isoname}_framebuffer.ppm'
data=ppm.read_bytes(); m,wh,mx,body=data.split(b'\n',3)
w,h=map(int,wh.split()); stride=w*3

def write_png(path,width,height,rgb):
    raw=b"".join(b'\x00'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
    def ch(t,p): return struct.pack('>I',len(p))+t+p+struct.pack('>I',zlib.crc32(t+p)&0xffffffff)
    out=b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,9))+ch(b'IEND',b"")
    path.write_bytes(out)

write_png(base/f'{isoname}_framebuffer.png',w,h,body)
cw,ch=320,240
crop=bytearray()
for y in range(ch): crop.extend(body[y*stride:y*stride+cw*3])
write_png(base/f'{isoname}_display_320x240.png',cw,ch,bytes(crop))

bg=tuple(body[:3])
print('bg',bg)
for x in range(8,217,8):
    non=0
    for yy in range(16,24):
        off=(yy*w+x)*3
        for xx in range(8):
            if tuple(body[off+xx*3:off+xx*3+3])!=bg:
                non+=1
    print(x,non)
PY
```
