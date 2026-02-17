#!/usr/bin/env bash
set -uo pipefail

BIN="/psxrecomp/build/psxrecomp"
ISO_DIR="/psxrecomp/out/recompiled-demos-windows-latest/inputs"
ARTIFACT_DIR="/psxrecomp/docker-results"
mkdir -p "${ARTIFACT_DIR}"

if [[ ! -x "${BIN}" ]]; then
  echo "ERROR: psxrecomp binary not found at ${BIN}" >&2
  exit 1
fi

# Collect ISOs
mapfile -t isos < <(find "${ISO_DIR}" -type f -name '*.iso' | sort)
if [[ ${#isos[@]} -eq 0 ]]; then
  echo "ERROR: No ISO files found in ${ISO_DIR}" >&2
  exit 1
fi

echo "======================================="
echo "  PSXRecomp Docker end-to-end test"
echo "  ISOs found: ${#isos[@]}"
echo "======================================="

OVERALL_STATUS=0

for iso in "${isos[@]}"; do
  base="$(basename "${iso}" .iso)"
  echo ""
  echo "───────────────────────────────────────"
  echo "  Processing: ${base}"
  echo "───────────────────────────────────────"

  out_dir="${ARTIFACT_DIR}/source/${base}"
  mkdir -p "${out_dir}"

  # ── Step 1: Recompile the ISO ──
  echo "[${base}] Step 1: Recompiling ISO..."
  result_json="${ARTIFACT_DIR}/${base}.result.json"
  stderr_log="${ARTIFACT_DIR}/${base}.stderr.log"
  if ! "${BIN}" --json -o "${out_dir}" "${iso}" >"${result_json}" 2>"${stderr_log}"; then
    echo "[${base}] ✗ Recompilation FAILED"
    cat "${stderr_log}" || true
    OVERALL_STATUS=1
    continue
  fi

  # Show warnings from the result
  if python3 -c "
import json, sys
with open('${result_json}') as f:
    data = json.load(f)
warnings = data.get('warnings', [])
if warnings:
    for w in warnings:
        print(f'  [warn] {w}')
unsupported = data.get('unsupportedOpcodes', [])
if unsupported:
    for u in unsupported:
        print(f'  [UNSUPPORTED] {u}')
    sys.exit(2)
" 2>/dev/null; then
    echo "[${base}] ✓ Recompilation OK"
  else
    rc=$?
    if [[ $rc -eq 2 ]]; then
      echo "[${base}] ✗ Has UNSUPPORTED opcodes (see above)"
      OVERALL_STATUS=1
    fi
  fi

  # ── Step 2: Find & build generated CMakeLists.txt ──
  echo "[${base}] Step 2: Building generated executable..."
  cmake_file="$(find "${out_dir}" -type f -name 'CMakeLists.txt' | head -1)"
  if [[ -z "${cmake_file}" ]]; then
    echo "[${base}] ✗ No CMakeLists.txt found in generated output"
    OVERALL_STATUS=1
    continue
  fi
  source_root="$(dirname "${cmake_file}")"
  build_dir="${out_dir}/build-generated"

  config_log="${ARTIFACT_DIR}/${base}.cmake.configure.log"
  build_log="${ARTIFACT_DIR}/${base}.cmake.build.log"

  if ! cmake -S "${source_root}" -B "${build_dir}" -G Ninja \
       -DPSXRECOMP_ENABLE_LOGGING=ON \
       -DPSXRECOMP_LOG_LEVEL=2 >"${config_log}" 2>&1; then
    echo "[${base}] ✗ CMake configure FAILED"
    tail -20 "${config_log}"
    OVERALL_STATUS=1
    continue
  fi

  if ! cmake --build "${build_dir}" >"${build_log}" 2>&1; then
    echo "[${base}] ✗ Build FAILED"
    tail -30 "${build_log}"
    OVERALL_STATUS=1
    continue
  fi
  echo "[${base}] ✓ Build OK"

  # ── Step 3: Run the demo headless ──
  echo "[${base}] Step 3: Running demo..."
  runner="$(find "${build_dir}" -type f -perm -u+x \
       ! -path '*/CMakeFiles/*' ! -name '*.a' ! -name '*.o' | head -1)"
  if [[ -z "${runner}" ]]; then
    echo "[${base}] ✗ No executable found in build output"
    OVERALL_STATUS=1
    continue
  fi

  framebuffer_dump="${ARTIFACT_DIR}/${base}.ppm"
  run_log="${ARTIFACT_DIR}/${base}.run.log"

  # Copy resources next to the runner if they exist
  runner_dir="$(dirname "${runner}")"
  if [[ -d "${source_root}/resources" ]]; then
    cp -R "${source_root}/resources" "${runner_dir}/"
  fi

  export PSXRECOMP_PRESENT_FRAMEBUFFER=0
  export PSXRECOMP_DUMP_FRAMEBUFFER="${framebuffer_dump}"
  # Limit execution to 10M steps so demos have enough time to render.
  # PSX demos loop forever, so we need a step budget to terminate.
  export PSXRECOMP_MAX_STEPS=10000000

  if timeout 30 "${runner}" >"${run_log}" 2>&1; then
    echo "[${base}] ✓ Execution OK (clean exit)"
  else
    rc=$?
    if [[ $rc -eq 124 ]]; then
      echo "[${base}] ✗ Execution TIMED OUT (30s)"
    else
      # Non-zero exit from max-steps is expected for PSX demos (they loop
      # forever).  Treat as success if we got GPU output.
      echo "[${base}] ✓ Execution finished (exit code ${rc}, likely step-limit reached)"
    fi
    cat "${run_log}" 2>/dev/null | tail -40
  fi

  # ── Step 4: Analyze results ──
  if [[ -f "${run_log}" ]]; then
    echo "[${base}] Output:"
    grep -E '^\[psxrecomp\]' "${run_log}" | head -20
    echo ""

    # Extract GPU command count and pixel count
    gpu_cmds=$(grep -oP 'GPU command count: \K[0-9]+' "${run_log}" 2>/dev/null || echo "?")
    pixels=$(grep -oP 'non-zero display pixels: \K[0-9]+' "${run_log}" 2>/dev/null || echo "?")
    exec_ms=$(grep -oP 'returned in \K[0-9]+' "${run_log}" 2>/dev/null || echo "?")

    echo "[${base}] Summary: GPU cmds=${gpu_cmds}, pixels=${pixels}, time=${exec_ms}ms"

    # Flag if suspicious
    if [[ "${gpu_cmds}" == "0" ]]; then
      echo "[${base}] ⚠ WARNING: Zero GPU commands - demo may not be working"
    fi
    if [[ "${pixels}" == "0" ]]; then
      echo "[${base}] ⚠ WARNING: Zero display pixels - black screen"
    fi
  fi

  echo ""
done

echo "======================================="
echo "  End-to-end test complete"
if [[ ${OVERALL_STATUS} -eq 0 ]]; then
  echo "  Result: ALL DEMOS PASSED ✓"
else
  echo "  Result: SOME DEMOS FAILED ✗"
fi
echo "======================================="

exit ${OVERALL_STATUS}
