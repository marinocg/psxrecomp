#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <psxrecomp_binary> <work_dir> [demo_exe]" >&2
  exit 1
fi

PSXRECOMP_BIN="$1"
WORK_DIR="$2"
DEMO_EXE="${3:-}"
FIXTURE_DIR="$WORK_DIR/fixtures"
OUT_DIR="$WORK_DIR/out"
mkdir -p "$WORK_DIR"

cmd=(python3 tools/iso_fixture_generator.py --output-dir "$FIXTURE_DIR")
if [[ -n "$DEMO_EXE" ]]; then
  cmd+=(--demo-exe "$DEMO_EXE" --with-assets)
fi
"${cmd[@]}"

# bad fixture should fail
set +e
"$PSXRECOMP_BIN" --json -o "$OUT_DIR" "$FIXTURE_DIR/bad_invalid_pvd.iso" > "$WORK_DIR/bad.json"
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "Expected bad fixture to fail, but command succeeded." >&2
  exit 2
fi

# good fixture should succeed
"$PSXRECOMP_BIN" --json -o "$OUT_DIR" "$FIXTURE_DIR/good_minimal.iso" > "$WORK_DIR/good.json"

if [[ -n "$DEMO_EXE" ]]; then
  "$PSXRECOMP_BIN" --json -o "$OUT_DIR" "$FIXTURE_DIR/good_demo_with_assets.iso" > "$WORK_DIR/good_rich.json"
fi

bundle_dir=$(WORK_JSON="$WORK_DIR/good.json" python3 - <<'PY'
import json, os
from pathlib import Path
j=json.loads(Path(os.environ['WORK_JSON']).read_text())
if not j.get('success'):
    raise SystemExit(3)
print(Path(j['artifacts']['build']).parent)
PY
)

cmake -S "$bundle_dir" -B "$bundle_dir/build" -G Ninja
cmake --build "$bundle_dir/build" -j4

if [[ -n "$DEMO_EXE" ]]; then
  WORK_JSON="$WORK_DIR/good_rich.json" python3 - <<'PY'
import json, os
from pathlib import Path
j=json.loads(Path(os.environ['WORK_JSON']).read_text())
if not j.get('success'):
    raise SystemExit('rich fixture failed to recompile')
print('Rich fixture selected executable:', j['selection']['selectedPath'])
print('Rich fixture candidates:', len(j.get('exeCandidates', [])))
PY
fi

echo "Fixture validation succeeded."
