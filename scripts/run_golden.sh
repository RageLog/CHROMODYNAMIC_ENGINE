#!/usr/bin/env bash
# =============================================================================
# CHROMODYNAMIC — run_golden.sh
# Phase 11 Track B Part 1 — POSIX driver, parity with run_golden.ps1.
#
# Two modes:
#   capture   re-records every wired sample's golden reference PNG into
#             tests/golden/<sample>.png.
#   compare   diffs every wired sample against its committed golden;
#             non-zero exit on any mismatch above tolerance.
#
# Usage:
#   ./scripts/run_golden.sh [--mode compare|capture] [--build-dir DIR]
#                           [--config Debug|Release] [--tolerance N]
#                           [--golden-dir DIR] [--diff-dir DIR]
#
# Exit codes:
#   0  every sample passed (compare) or wrote (capture)
#   1  script-level error
#   2  at least one sample's compare exceeded the tolerance
# =============================================================================
set -u

MODE="compare"
BUILD_DIR="build/ninja-base"
CONFIG="Debug"
TOLERANCE="8"
TIMEOUT_SEC="15"
GOLDEN_DIR="tests/golden"
DIFF_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode) MODE="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --tolerance) TOLERANCE="$2"; shift 2 ;;
        --timeout) TIMEOUT_SEC="$2"; shift 2 ;;
        --golden-dir) GOLDEN_DIR="$2"; shift 2 ;;
        --diff-dir) DIFF_DIR="$2"; shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

if [[ "$MODE" != "capture" && "$MODE" != "compare" ]]; then
    echo "--mode must be 'capture' or 'compare'" >&2
    exit 1
fi

# Wired samples — keep in sync with run_golden.ps1 + each sample's
# main.cpp include of GoldenCapture.hpp.
SAMPLES=(
    hello_triangle
    hello_cube
    hello_anim
    hello_pbr
    hello_skybox
)

# Resolve sample directory. Multi-config (Ninja MC) uses bin/<Config>;
# single-config drops them in bin/.
SAMPLE_DIR=""
if [[ -d "$BUILD_DIR/bin/$CONFIG" ]]; then
    SAMPLE_DIR="$BUILD_DIR/bin/$CONFIG"
elif [[ -d "$BUILD_DIR/bin" ]]; then
    SAMPLE_DIR="$BUILD_DIR/bin"
else
    echo "Sample binary directory not found. Tried: $BUILD_DIR/bin/$CONFIG, $BUILD_DIR/bin" >&2
    exit 1
fi

if [[ ! -d "$GOLDEN_DIR" ]]; then
    if [[ "$MODE" == "compare" ]]; then
        echo "Golden reference directory missing: $GOLDEN_DIR. Run --mode capture first." >&2
        exit 1
    fi
    mkdir -p "$GOLDEN_DIR"
fi
if [[ -n "$DIFF_DIR" && ! -d "$DIFF_DIR" ]]; then
    mkdir -p "$DIFF_DIR"
fi

PASS=0
FAIL=0
TOTAL=0
FAILED_LIST=()

for name in "${SAMPLES[@]}"; do
    exe="$SAMPLE_DIR/$name"
    # Some Linux runs end up with no extension; Windows builds keep .exe.
    if [[ ! -x "$exe" && -x "$exe.exe" ]]; then
        exe="$exe.exe"
    fi
    if [[ ! -x "$exe" ]]; then
        echo "Sample binary missing: $exe" >&2
        exit 1
    fi
    TOTAL=$((TOTAL+1))
    golden="$GOLDEN_DIR/$name.png"

    args=("--golden-tolerance" "$TOLERANCE")
    if [[ "$MODE" == "capture" ]]; then
        args=("--golden-capture" "$golden" "${args[@]}")
    else
        if [[ ! -f "$golden" ]]; then
            printf "  [SKIP] %-22s no reference at %s\n" "$name" "$golden"
            FAIL=$((FAIL+1))
            FAILED_LIST+=("$name (no reference)")
            continue
        fi
        args=("--golden-compare" "$golden" "${args[@]}")
        if [[ -n "$DIFF_DIR" ]]; then
            args+=("--golden-diff-out" "$DIFF_DIR/$name.diff.png")
        fi
    fi

    printf "  [%-22s] %s ... " "$name" "$MODE"
    out=$(timeout "$TIMEOUT_SEC" "$exe" "${args[@]}" 2>&1)
    rc=$?
    line=$(echo "$out" | grep '\[golden\]' | head -1 || true)
    if [[ $rc -eq 0 ]]; then
        echo "OK"
        [[ -n "$line" ]] && echo "    $line"
        PASS=$((PASS+1))
    elif [[ $rc -eq 2 && "$MODE" == "compare" ]]; then
        echo "MISMATCH"
        [[ -n "$line" ]] && echo "    $line"
        FAIL=$((FAIL+1))
        FAILED_LIST+=("$name (mismatch)")
    else
        echo "FAIL rc=$rc"
        echo "$out" | tail -3 | sed 's/^/    /'
        FAIL=$((FAIL+1))
        FAILED_LIST+=("$name (rc=$rc)")
    fi
done

echo ""
echo "=== golden $MODE summary ============================================"
echo "  build dir   : $BUILD_DIR"
echo "  golden dir  : $GOLDEN_DIR"
echo "  tolerance   : $TOLERANCE / 255 per channel"
echo "  result      : $PASS/$TOTAL passed, $FAIL failed"
echo "===================================================================="

if [[ $FAIL -gt 0 ]]; then
    echo "Failed:"
    for f in "${FAILED_LIST[@]}"; do
        echo "  - $f"
    done
    exit 2
fi
exit 0
