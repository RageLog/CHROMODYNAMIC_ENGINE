#!/usr/bin/env bash
# =============================================================================
# CHROMODYNAMIC — run_all_samples.sh
# Smoke-tests every hello_* executable in headless mode with a watchdog.
#
# Usage:
#   ./scripts/run_all_samples.sh                              # default Debug
#   BUILD_DIR=build/ci-gcc CONFIG=Debug ./scripts/run_all_samples.sh
#   TIMEOUT_SEC=8 FRAMES=5 ./scripts/run_all_samples.sh
#
# Exit codes:
#   0 — every sample exited 0 within the watchdog
#   1 — at least one sample failed (non-zero exit, crash, or timeout)
#   2 — script-level error (no build dir, no samples found)
# =============================================================================
set -u

BUILD_DIR="${BUILD_DIR:-build/ninja-base}"
CONFIG="${CONFIG:-Debug}"
TIMEOUT_SEC="${TIMEOUT_SEC:-10}"
FRAMES="${FRAMES:-3}"
LOG_DIR="${LOG_DIR:-${BUILD_DIR}/smoke-logs}"

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

# Multi-config (Ninja MC) uses bin/<Config>/, single-config uses bin/.
if [ -d "${BUILD_DIR}/bin/${CONFIG}" ]; then
    sample_dir="${BUILD_DIR}/bin/${CONFIG}"
elif [ -d "${BUILD_DIR}/bin" ]; then
    sample_dir="${BUILD_DIR}/bin"
else
    echo "error: sample directory not found (tried ${BUILD_DIR}/bin/${CONFIG} and ${BUILD_DIR}/bin)" >&2
    exit 2
fi

mkdir -p "$LOG_DIR"

# Per-sample extra timeout for slow-on-first-run cases.
extra_timeout() {
    case "$1" in
        hello_textured_cooked|hello_textured_cooked.exe) echo 15 ;;
        hello_hot_reload|hello_hot_reload.exe)            echo 15 ;;
        *)                                                 echo "$TIMEOUT_SEC" ;;
    esac
}

shopt -s nullglob
samples=( "$sample_dir"/hello_*.exe "$sample_dir"/hello_* )
# Dedupe in case both .exe and no-ext glob matched the same logical sample.
declare -A seen
filtered=()
for s in "${samples[@]}"; do
    base="$(basename "${s%.exe}")"
    if [ -z "${seen[$base]:-}" ]; then
        filtered+=( "$s" )
        seen[$base]=1
    fi
done
samples=( "${filtered[@]}" )

if [ ${#samples[@]} -eq 0 ]; then
    echo "error: no hello_* binaries in $sample_dir" >&2
    exit 2
fi

pass=0
fail=0
total=0
declare -a failed_names
start_wall=$SECONDS

for exe in "${samples[@]}"; do
    name="$(basename "$exe")"
    log="$LOG_DIR/${name%.exe}.log"
    tmo="$(extra_timeout "$name")"
    total=$((total + 1))

    printf '  [%-32s] ... ' "$name"
    sw_start=$(date +%s.%N)

    # `timeout` on Linux/macOS sends SIGTERM at the limit; -k 2s sends SIGKILL
    # 2 seconds later if the process doesn't comply. Output captured to log.
    if timeout --preserve-status -k 2s "$tmo" "$exe" --headless "$FRAMES" >"$log" 2>"$log.err"; then
        code=0
    else
        code=$?
    fi
    sw_end=$(date +%s.%N)
    secs=$(awk "BEGIN { printf \"%.2f\", $sw_end - $sw_start }")

    if [ "$code" -eq 0 ]; then
        printf 'OK (%ss)\n' "$secs"
        pass=$((pass + 1))
    elif [ "$code" -eq 124 ] || [ "$code" -eq 137 ]; then
        # 124 = SIGTERM at deadline; 137 = SIGKILL after -k window.
        printf 'TIMEOUT (%ss)\n' "$tmo"
        failed_names+=( "$name|TIMEOUT|$code" )
        fail=$((fail + 1))
    else
        printf 'FAIL exit=%d\n' "$code"
        failed_names+=( "$name|FAIL|$code" )
        fail=$((fail + 1))
    fi
done

wall=$((SECONDS - start_wall))

echo
echo '=== smoke-test summary ============================================'
printf '  build dir : %s\n' "$BUILD_DIR"
printf '  binaries  : %s\n' "$sample_dir"
printf '  log dir   : %s\n' "$LOG_DIR"
printf '  frames    : %s\n' "$FRAMES"
printf '  timeout   : %ss (per sample)\n' "$TIMEOUT_SEC"
printf '  wall time : %ss\n' "$wall"
printf '  result    : %d/%d passed, %d failed\n' "$pass" "$total" "$fail"
echo '==================================================================='

if [ "$fail" -gt 0 ]; then
    echo
    echo 'Failed samples:'
    for f in "${failed_names[@]}"; do
        IFS='|' read -r n s c <<<"$f"
        printf '  - %-32s %-8s exit=%s  log=%s\n' "$n" "$s" "$c" "$LOG_DIR/${n%.exe}.log"
    done
    exit 1
fi

exit 0
