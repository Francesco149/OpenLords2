#!/usr/bin/env python3
"""
tools/make_owned_county_fixture.py — build a --save-ref fixture with a
PLAIN owned county (kind==1, owner==local_player) reachable at the
existing (260,250)/lParam 16384260 click coordinate every other trace in
tools/traces/ already uses.

Why this exists: the default pinned reference (--ol2_reference, a fresh
map-0 new-game save) has ZERO kind==1 territory records — nobody has built
a keep yet, so `engine_army_select_dispatch`'s "owned county" branch is
unreachable from it (session 2026-07-01 finding). The game dir's old_turn.
sav/test.sav backups (map 17) DO have one kind==1 record (county key 4,
placed at map cell col=30/row=46) but it's INTERNALLY INCONSISTENT + not
owned by the default local_player:
  - owner byte (TERR_REC[4]+0x00) is 2, not local_player (1).
  - g_overworld_map's own reciprocal stamp at that cell's +5 byte is 0
    (never got a place_entity-style write — g_per_territory_420 and
    g_overworld_map disagree about whether anything is there at all).
This script patches a COPY (never the source file) to fix both, plus
overrides the save-carried camera scroll so the county sits at screen-
diamond cell (4,15) — exactly what pixel (260,250) resolves to at ANY
scroll, per engine_camera_cursor_to_cell's math (see tools/traces/
plain_county_click.jsonl's header comment for the full derivation).

Usage:
    nix develop -c python3 tools/make_owned_county_fixture.py OUT_DIR

Then:
    nix develop -c python3 tools/trace_studio_v3/olv3_capture.py \\
        --trace plain_county_click --only port --no-view --save-ref OUT_DIR
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

TERR_OFF = 99400          # g_per_territory_420, sav_offset (entry 5)
TERR_STRIDE = 0x1a4
COUNTY_KEY = 4             # the one kind==1 record test.sav/old_turn.sav carry
COUNTY_COL = 30            # TERR_REC[COUNTY_KEY]+0x0a (mc)
COUNTY_ROW = 46            # TERR_REC[COUNTY_KEY]+0x0b (mr)
OVERWORLD_CELL_OFF = (COUNTY_ROW * 0x40 + COUNTY_COL) * 8   # entry 0, sav_offset 0
LOCAL_PLAYER_OFF = 262884  # s_slot_0057c8cc (entry 72)
SCROLL_COL_OFF = 262660    # s_slot_005651b4 (entry 16)
SCROLL_ROW_OFF = 262664    # s_slot_005651b8 (entry 17)
# scroll chosen so cell (4,15) -- what pixel (260,250) resolves to at ANY
# scroll -- lands on (COUNTY_COL, COUNTY_ROW)'s screen-diamond position
# (sr=1+mr+mc, sc=(0x40-mr+mc)/2 => sr=77, sc=24 for mc=30/mr=46).
FIXTURE_SCROLL_COL = 20
FIXTURE_SCROLL_ROW = 62


def _game_dir() -> Path:
    import os
    gd = os.environ.get("OPENLORDS2_GAME_DIR")
    if not gd:
        sys.exit("OPENLORDS2_GAME_DIR not set -- run inside `nix develop`")
    p = Path(gd)
    if not p.is_dir():
        # flake exports a WSL path already; accept a Windows path too.
        out = subprocess.run(["wslpath", "-u", gd], capture_output=True, text=True)
        p = Path(out.stdout.strip())
    if not p.is_dir():
        sys.exit(f"game dir not found: {gd!r}")
    return p


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir", type=Path, help="directory to write lastturn.sav + Castles.dat into")
    ap.add_argument("--source", default="test.sav",
                    help="save (in the game dir) to patch a copy of (default: test.sav)")
    args = ap.parse_args()

    gd = _game_dir()
    src_sav = gd / args.source
    if not src_sav.exists():
        sys.exit(f"source save not found: {src_sav}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    dst_sav = args.out_dir / "lastturn.sav"
    shutil.copyfile(src_sav, dst_sav)

    # Castles.dat: any structurally-valid file works for this narrow test
    # (the click-dispatch path this fixture exercises never reads it) --
    # reuse the pinned reference's copy if present, else the game dir's.
    ref_cas = gd / "_ol2_reference" / "Castles.dat"
    src_cas = ref_cas if ref_cas.exists() else gd / "Castles.dat"
    if src_cas.exists():
        shutil.copyfile(src_cas, args.out_dir / "Castles.dat")

    data = bytearray(dst_sav.read_bytes())

    rec_off = TERR_OFF + COUNTY_KEY * TERR_STRIDE
    kind = data[rec_off + 0x08]
    col = data[rec_off + 0x0a]
    row = data[rec_off + 0x0b]
    if kind != 1 or col != COUNTY_COL or row != COUNTY_ROW:
        sys.exit(f"{src_sav} no longer matches the expected fixture shape "
                 f"(TERR_REC[{COUNTY_KEY}] kind={kind} col={col} row={row}, "
                 f"expected kind=1 col={COUNTY_COL} row={COUNTY_ROW}) -- "
                 "re-derive the constants at the top of this script.")

    local_player = struct.unpack_from("<i", data, LOCAL_PLAYER_OFF)[0]
    owner_before = data[rec_off + 0x00]
    data[rec_off + 0x00] = local_player & 0xff       # TERR_REC owner -> local_player

    overworld_before = data[OVERWORLD_CELL_OFF + 5]
    data[OVERWORLD_CELL_OFF + 5] = COUNTY_KEY        # reciprocal g_overworld_map stamp

    scroll_before = struct.unpack_from("<ii", data, SCROLL_COL_OFF)
    struct.pack_into("<i", data, SCROLL_COL_OFF, FIXTURE_SCROLL_COL)
    struct.pack_into("<i", data, SCROLL_ROW_OFF, FIXTURE_SCROLL_ROW)

    dst_sav.write_bytes(data)

    print(f"wrote {dst_sav}")
    print(f"  local_player={local_player}")
    print(f"  TERR_REC[{COUNTY_KEY}] owner {owner_before} -> {local_player}")
    print(f"  g_overworld_map[{OVERWORLD_CELL_OFF}]+5 {overworld_before} -> {COUNTY_KEY}")
    print(f"  scroll {scroll_before} -> ({FIXTURE_SCROLL_COL}, {FIXTURE_SCROLL_ROW})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
