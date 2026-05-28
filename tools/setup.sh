#!/usr/bin/env bash
# tools/setup.sh — one-shot bootstrap for a fresh clone.
#
#   1. Symlinks the user's GOG install of Lords of the Realm II into
#      vendor/original/.
#   2. SHA256s the key binaries for reproducibility / drift detection.
#   3. Prints next steps.
#
# Lords2.exe has no DRM packer (clean MSVC 4.20 PE — verified 2026-05-24),
# so there's no Steamless-equivalent unpack step. Ghidra runs directly
# against vendor/original/Lords2.exe.
#
# Idempotent. Re-running re-validates the symlink and re-checks SHA256s.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

GAME_DIR="${OPENLORDS2_GAME_DIR:-/mnt/c/GOG Games/Lords of the Realm II}"

VENDOR="$ROOT/vendor"
ORIGINAL="$VENDOR/original"

bold()  { printf "\033[1m%s\033[0m\n" "$*"; }
green() { printf "\033[32m%s\033[0m\n" "$*"; }
red()   { printf "\033[31m%s\033[0m\n" "$*" >&2; }
yellow(){ printf "\033[33m%s\033[0m\n" "$*"; }

# ─── pre-flight ────────────────────────────────────────────────────────────
bold "[1/3] Pre-flight"

if [[ ! -d "$GAME_DIR" ]]; then
    red "Game directory not found: $GAME_DIR"
    red "Set OPENLORDS2_GAME_DIR to the absolute path of your Lords II install."
    exit 1
fi
if [[ ! -f "$GAME_DIR/Lords2.exe" ]]; then
    red "Lords2.exe not found inside $GAME_DIR"
    red "Looked at: $GAME_DIR/Lords2.exe"
    exit 1
fi
green "  ✓ game dir:    $GAME_DIR"

# ─── symlink game files ────────────────────────────────────────────────────
bold "[2/3] Linking game files into vendor/original/"

mkdir -p "$VENDOR"
# Replace the symlink each run so a moved game install doesn't go stale.
if [[ -L "$ORIGINAL" || -e "$ORIGINAL" ]]; then
    rm -f "$ORIGINAL"
fi
ln -s "$GAME_DIR" "$ORIGINAL"
green "  ✓ vendor/original -> $GAME_DIR"

# ─── git hooks ─────────────────────────────────────────────────────────────
# Point git at the tracked hook dir so the co-author-trailer (commit-msg) and
# ledger-regen + test-gate (pre-commit) hooks run for everyone, with no
# per-clone install step.
git -C "$ROOT" config core.hooksPath tools/git-hooks
chmod +x "$ROOT"/tools/git-hooks/* "$ROOT"/tools/claude-hooks/*.sh 2>/dev/null || true
green "  ✓ git hooks:   core.hooksPath → tools/git-hooks"

# ─── hash key files ────────────────────────────────────────────────────────
bold "[3/3] Hashing key files"

# Important binaries — track these so we notice if the user upgraded /
# repatched the game. Lords II shipped on CD originally; the GOG release
# includes a patched 2.0.0.3 binary and the entire CD contents (movies,
# WAVs, .pl8 sprite sheets, etc.) installed locally.
KEY_FILES=(
    "Lords2.exe"
    "mapl2.exe"
    "Smackw32.dll"
    "Smackw2.dll"
    "L2.INI"
    "Sierra.inf"
    "lords2.inf"
    "LANGUAGE.INF"
    "L2MAP.INF"
    "Castles.dat"
    "C_hist.dat"
    "History.dat"
    "L2_maps.dat"
)

for f in "${KEY_FILES[@]}"; do
    p="$GAME_DIR/$f"
    if [[ -f "$p" ]]; then
        sha="$(sha256sum "$p" | awk '{print $1}')"
        sz="$(stat -c %s "$p" 2>/dev/null || stat -f %z "$p" 2>/dev/null || echo '?')"
        printf "  %-30s %s  %12s B\n" "$f" "${sha:0:16}…" "$sz"
    else
        yellow "  (missing) $f"
    fi
done

cat <<EOF

Inputs:
  vendor/original/  → $GAME_DIR

Next:
  • ./tools/ghidra-headless.sh         # batch decompile Lords2.exe
  • nix develop --command python3 tools/smoke-test.py --help

Reminder: every command needs the dev shell. Either run
  nix develop
once at the top of a session, or prefix individual commands with
  nix develop --command <cmd>
EOF
