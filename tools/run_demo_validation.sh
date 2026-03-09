#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${PSXRECOMP_BIN:-$ROOT/build/psxrecomp}"
FALLBACK_INPUT_ROOT="${PSXRECOMP_INPUT_ROOT:-$ROOT/out/recompiled-demos-ubuntu-latest/inputs}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_ROOT="${PSXRECOMP_VALIDATION_OUT:-$ROOT/out/demo-validation-${STAMP}}"
SUMMARY="$OUT_ROOT/summary.csv"
RENDER_COMPARE="$OUT_ROOT/render_compare.csv"

render_demos=(ADVHELLO COP0TEST COP0LAB GPUTEST HELLOWLD MEMTEST CDBROWSE CDCRC CDXA GTELAB)
step_overrides=("CDBROWSE:200000000")

declare -A DEMO_ISOS=(
  [ADVHELLO]="$ROOT/examples/demos/advancedhello/ADVHELLO.iso"
  [COP0TEST]="$ROOT/examples/demos/cop0test/COP0TEST.iso"
  [COP0LAB]="$ROOT/examples/demos/cop0lab_auto/COP0LAB.iso"
  [GPUTEST]="$ROOT/examples/demos/gputest/GPUTEST.iso"
  [HELLOWLD]="$ROOT/examples/demos/helloworld/HELLOWLD.iso"
  [MEMTEST]="$ROOT/examples/demos/memorytest/MEMTEST.iso"
  [CDBROWSE]="$ROOT/examples/demos/cdbrowse/CDBROWSE.iso"
  [CDCRC]="$ROOT/examples/demos/cdcrc/CDCRC.iso"
  [CDXA]="$ROOT/examples/demos/cdxa/CDXA.iso"
  [GTELAB]="$ROOT/examples/demos/gtelab_auto/GTELAB.iso"
)

if [[ ! -x "$BIN" ]]; then
  echo "psxrecomp binary not found or not executable: $BIN" >&2
  exit 1
fi

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

printf 'demo,iso,recompile_ok,unsupported_ops,build_ok,resource_ok,render_ok,ppm_non_zero,gpu_commands,frame_count,run_rc,unsupported_jump,status,notes\n' > "$SUMMARY"

find_source_root() {
  local search_root="$1"
  local cmake_file=""
  cmake_file=$(find "$search_root" -type f -name CMakeLists.txt | head -n 1)
  if [[ -n "$cmake_file" ]]; then
    dirname "$cmake_file"
  fi
}

run_with_timeout() {
  local timeout_seconds="$1"
  shift
  python3 - "$timeout_seconds" "$@" <<'PY'
import subprocess
import sys

timeout_seconds = float(sys.argv[1])
command = sys.argv[2:]

try:
    completed = subprocess.run(command, check=False, timeout=timeout_seconds)
    raise SystemExit(completed.returncode)
except subprocess.TimeoutExpired:
    raise SystemExit(124)
PY
}

resolve_iso() {
  local demo="$1"
  local mapped="${DEMO_ISOS[$demo]:-}"
  if [[ -n "$mapped" && -f "$mapped" ]]; then
    printf '%s\n' "$mapped"
    return
  fi
  local fallback="$FALLBACK_INPUT_ROOT/$demo.iso"
  if [[ -f "$fallback" ]]; then
    printf '%s\n' "$fallback"
    return
  fi
  printf '\n'
}

run_demo() {
  local demo="$1"
  local iso
  iso="$(resolve_iso "$demo")"
  local demo_root="$OUT_ROOT/$demo"
  local gen_dir="$demo_root/gen"
  local build_dir="$demo_root/build"
  local log_dir="$demo_root/logs"
  local screen_dir="$demo_root/screens"
  local source_root=""
  local runner=""
  local recompile_ok=0 unsupported_ops=0 build_ok=0 resource_ok=0 render_ok=0
  local ppm_non_zero="" gpu_commands="" frame_count="" run_rc="" unsupported_jump=0
  local status="FAIL" notes=""
  local max_steps=20000000

  for override in "${step_overrides[@]}"; do
    if [[ "$override" == "$demo:"* ]]; then
      max_steps="${override#*:}"
    fi
  done

  mkdir -p "$gen_dir" "$build_dir" "$log_dir" "$screen_dir"

  if [[ -z "$iso" ]]; then
    notes="missing_iso"
    printf '%s,%s,%d,%d,%d,%d,%d,%s,%s,%s,%s,%d,%s,%s\n' \
      "$demo" "$iso" "$recompile_ok" "$unsupported_ops" "$build_ok" "$resource_ok" "$render_ok" \
      "$ppm_non_zero" "$gpu_commands" "$frame_count" "$run_rc" "$unsupported_jump" "$status" "$notes" >> "$SUMMARY"
    return
  fi

  if "$BIN" --json -o "$gen_dir" "$iso" > "$log_dir/recompile.result.json" 2> "$log_dir/recompile.stderr.log"; then
    recompile_ok=1
  fi

  unsupported_ops=$(python3 - <<'PY' "$log_dir/recompile.result.json"
import json
import sys
from pathlib import Path
p = Path(sys.argv[1])
if not p.exists() or not p.read_text().strip():
    print(0)
    raise SystemExit
warnings = json.loads(p.read_text()).get('warnings', []) or []
print(sum(1 for w in warnings if 'Unsupported opcode:' in w))
PY
)

  source_root="$(find_source_root "$gen_dir")"
  if [[ -n "$source_root" ]]; then
    if [[ -f "$source_root/resources/index/catalog.json" ]] || [[ -f "$source_root/resources/index/recomp_inputs.json" ]]; then
      resource_ok=1
    fi
  fi

  if [[ "$recompile_ok" -eq 1 && -n "$source_root" ]]; then
    if cmake -S "$source_root" -B "$build_dir" -G Ninja \
         -DPSXRECOMP_ENABLE_LOGGING=ON \
         -DPSXRECOMP_LOG_LEVEL=2 > "$log_dir/cmake.configure.log" 2>&1 && \
       cmake --build "$build_dir" > "$log_dir/cmake.build.log" 2>&1; then
      build_ok=1
    else
      notes=${notes:+$notes"|"}build_failed
    fi
  else
    [[ -z "$source_root" ]] && notes=${notes:+$notes"|"}missing_cmakelists
    [[ "$recompile_ok" -ne 1 ]] && notes=${notes:+$notes"|"}recompile_failed
  fi

  if [[ "$build_ok" -eq 1 ]]; then
    runner=$(find "$build_dir" -type f -perm -u+x ! -path '*/CMakeFiles/*' ! -name '*.a' ! -name '*.o' | head -n 1)
    if [[ -n "$runner" ]]; then
      if [[ -d "$source_root/resources" ]]; then
        cp -R "$source_root/resources" "$(dirname "$runner")/"
      fi
      set +e
      export PSXRECOMP_PRESENT_FRAMEBUFFER=0
      export PSXRECOMP_MAX_STEPS="$max_steps"
      export PSXRECOMP_DUMP_FRAMEBUFFER="$screen_dir/$demo.ppm"
      run_with_timeout 30 "$runner" > "$log_dir/run.log" 2>&1
      run_rc=$?
      unset PSXRECOMP_PRESENT_FRAMEBUFFER
      unset PSXRECOMP_MAX_STEPS
      unset PSXRECOMP_DUMP_FRAMEBUFFER
      set -e
      unsupported_jump=$(python3 - <<'PY' "$log_dir/run.log"
from pathlib import Path
import sys
p = Path(sys.argv[1])
print(1 if p.exists() and 'Unsupported JR/JUMP target' in p.read_text() else 0)
PY
)
      if [[ -f "$screen_dir/$demo.ppm" ]]; then
        readarray -t metrics < <(python3 - <<'PY' "$screen_dir/$demo.ppm" "$log_dir/run.log"
import re
import sys
from pathlib import Path
ppm_path, log_path = map(Path, sys.argv[1:3])
body = ppm_path.read_bytes().split(b'\n', 3)[3]
non_zero = sum(1 for i in range(0, len(body), 3) if body[i:i+3] != b'\x00\x00\x00')
log = log_path.read_text() if log_path.exists() else ''
gpu = re.findall(r'GPU command count: (\d+)', log)
frame = re.findall(r'Frame count: (\d+)', log)
print(non_zero)
print(gpu[-1] if gpu else '')
print(frame[-1] if frame else '')
PY
)
        ppm_non_zero="${metrics[0]}"
        gpu_commands="${metrics[1]}"
        frame_count="${metrics[2]}"
        if [[ "$ppm_non_zero" != "0" && "$unsupported_jump" -eq 0 ]]; then
          render_ok=1
        else
          [[ "$ppm_non_zero" == "0" ]] && notes=${notes:+$notes"|"}black_framebuffer
          [[ "$unsupported_jump" -ne 0 ]] && notes=${notes:+$notes"|"}unsupported_jump
        fi
      else
        notes=${notes:+$notes"|"}runner_no_framebuffer
      fi
    else
      notes=${notes:+$notes"|"}missing_runner
    fi
  fi

  if [[ "$recompile_ok" -eq 1 && "$build_ok" -eq 1 && "$resource_ok" -eq 1 && "$render_ok" -eq 1 ]]; then
    status="PASS"
  fi

  printf '%s,%s,%d,%d,%d,%d,%d,%s,%s,%s,%s,%d,%s,%s\n' \
    "$demo" "$iso" "$recompile_ok" "$unsupported_ops" "$build_ok" "$resource_ok" "$render_ok" \
    "$ppm_non_zero" "$gpu_commands" "$frame_count" "$run_rc" "$unsupported_jump" "$status" "$notes" >> "$SUMMARY"
}

for demo in "${render_demos[@]}"; do
  run_demo "$demo"
done

python3 - <<'PY' "$SUMMARY" "$RENDER_COMPARE"
import csv
import sys
summary_path, compare_path = sys.argv[1:3]
with open(summary_path, newline='') as f:
    rows = list(csv.DictReader(f))
with open(compare_path, 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['demo', 'status', 'iso', 'ppm_non_zero', 'gpu_commands', 'frame_count'])
    for row in rows:
        writer.writerow([
            row['demo'],
            row['status'],
            row['iso'],
            row['ppm_non_zero'],
            row['gpu_commands'],
            row['frame_count'],
        ])
PY

echo "Wrote demo validation artifacts to: $OUT_ROOT"
