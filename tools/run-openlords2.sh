#!/usr/bin/env bash
# tools/run-openlords2.sh — canonical OpenLords2 dev-loop recipe.
#
# Does, in one step, what the dev recipe spelled out by hand:
#   1. Build src/ and tools/launcher/.
#   2. Copy openlords2-debug.exe to /tmp/openlords2-<pid>.exe so each
#      run gets a fresh PE-image mapping (dodges WSL's image-by-path
#      reuse).
#   3. Exec the launcher to supervise it.  Launcher guarantees the
#      child dies if anything kills us — Job Object with
#      KILL_ON_JOB_CLOSE.
#   4. Clean up the /tmp/openlords2-*.exe after.
#
# Run inside `nix develop`.  Defaults match a typical smoke run:
# 200 frames, 8 s timeout.  Override the timeout via
# $OPENLORDS2_TIMEOUT_MS, the frame count via $OPENLORDS2_FRAMES, or
# pass extra child args on the command line.
#
# Examples:
#   tools/run-openlords2.sh
#   tools/run-openlords2.sh --frames 500
#   OPENLORDS2_TIMEOUT_MS=30000 tools/run-openlords2.sh
#   tools/run-openlords2.sh -- --no-cd       # pass-through args
#
# Borrowed wholesale from OpenMare's tools/run-openmare.sh.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if ! command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "error: mingw32 toolchain not on PATH — are you inside \`nix develop\`?" >&2
    exit 125
fi

TIMEOUT_MS=${OPENLORDS2_TIMEOUT_MS:-8000}
FRAMES=${OPENLORDS2_FRAMES:-200}

# All args are pass-through to the child by default.  An optional leading
# `--` is accepted (and stripped) for the symmetry-loving.
if [[ "${1:-}" == "--" ]]; then shift; fi
extra=("$@")

# If the child argv already specifies --frames, skip our default frame
# cap so the user's choice wins.
user_set_frames=0
for a in "${extra[@]}"; do
    if [[ "$a" == --frames || "$a" == "--frames="* ]]; then user_set_frames=1; fi
done

echo "[run-openlords2] building src/ + tools/launcher/"
make -s -C src
make -s -C tools/launcher

stamp=$$
tmp_exe="/tmp/openlords2-${stamp}.exe"
cleanup() { rm -f /tmp/openlords2-*.exe; }
trap cleanup EXIT

cp build/openlords2-debug.exe "$tmp_exe"

child_args=()
if [[ "$FRAMES" -gt 0 && "$user_set_frames" -eq 0 ]]; then
    child_args+=(--frames "$FRAMES")
fi
child_args+=("${extra[@]}")

if [[ "$user_set_frames" -eq 1 ]]; then
    echo "[run-openlords2] launcher → $tmp_exe (timeout=${TIMEOUT_MS}ms, frames=user)"
else
    echo "[run-openlords2] launcher → $tmp_exe (timeout=${TIMEOUT_MS}ms, frames=${FRAMES})"
fi

exec ./build/openlords2-launcher.exe \
    --timeout-ms "$TIMEOUT_MS" \
    -- \
    "$tmp_exe" "${child_args[@]}"
