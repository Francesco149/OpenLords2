#!/usr/bin/env python3
"""
tools/make_swap_county_fixture.py — build a --save-ref fixture with TWO
owned counties reachable within the same 8x30 viewport, to organically
exercise engine_army_move_confirm's `g_classify_swap` branch (armed
county A, second click lands on a DIFFERENT owned county B) — the
session-2026-07-01(18) "next candidate": a real click exercising one of
FUN_004a9252's confirm-dialog siblings for the first time (session 18
itself only reached the no-classification fallback, re-targeting the
SAME county it had just armed).

Builds on tools/make_owned_county_fixture.py's approach (patch a COPY of
test.sav, which carries the one real kind==1 record — county key 4, a
keep at map cell col=30/row=46 — owned by player 2 with its reciprocal
g_overworld_map stamp still 0). This script ALSO fabricates a second
owned county at key 5 (confirmed fully unused/zeroed in test.sav): a
byte-for-byte copy of key 4's record (already self-consistent — real
game data, not hand-invented field values) relocated 2 map cells east
(col=32, same row=46). engine_army_move_confirm.c's g_classify_swap gate
only reads the TARGET record's owner (+0x00) and kind (+0x08) plus the
clicked cell's reciprocal head_terr stamp (g_overworld_map[cell]+5) — it
never reads the record's own col/row — so relocating by reusing every
other byte verbatim is safe; col/row are patched too anyway for
cosmetic/render fidelity (city drawer, camera-center-on-select).

Map cell (32,46) was chosen because it's naturally EMPTY (head_terr==0)
and UNFLAGGED (g_overworld_map descriptor flags byte == 0, confirmed via
direct save inspection) in test.sav, so it can't accidentally satisfy
any of the OTHER hover-classify branches (merge/claim/attack all require
non-zero flag bits at the target cell; garrison/direct require flag bit
0x80) — isolating the click to the swap path alone. It also sits only 2
plain-terrain (cost-3) tiles from the county-4 keep, comfortably under
that county's presence-accumulator headroom (+0x154 - +0x153 = 15 - 5 =
10) that gates whether the hover classifier arms ANY outcome at all.

Screen coordinates for both keep tiles were derived (and round-trip
verified against a faithful Python port of engine_camera_cursor_to_cell
+ engine_map_init_build_cells' rotation-0 projection) at the SAME fixed
scroll (col=20, row=62) tools/make_owned_county_fixture.py already uses
— so both counties are visible/clickable without any camera scroll
between the two clicks:
  county 4 keep (mc=30,mr=46) -> screen cell (4,15)  -> pixel (260,250)
  county 5 keep (mc=32,mr=46) -> screen cell (5,17)  -> pixel (320,280)
(pixel = the diamond cell's rendered top-left reference corner, offset
by the same (+50,+16) interior bias tools/traces/plain_county_click.jsonl's
own (260,250) resolves to relative to cell (4,15)'s own reference corner
-- reused rather than re-derived, since the diamond geometry is
translation-invariant and one real click already proved that bias lands
solidly inside a cell, not on a boundary.)

Usage:
    nix develop -c python3 tools/make_swap_county_fixture.py OUT_DIR

Then:
    nix develop -c python3 tools/trace_studio_v3/olv3_capture.py \\
        --trace swap_county_click --only port --no-view --save-ref OUT_DIR
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

COUNTY_A_KEY = 4           # the one real kind==1 record test.sav carries
COUNTY_A_COL = 30
COUNTY_A_ROW = 46

COUNTY_B_KEY = 5           # confirmed fully-zeroed/unused in test.sav
COUNTY_B_COL = 32          # 2 tiles east of county A -- empty, unflagged
COUNTY_B_ROW = 46

LOCAL_PLAYER_OFF = 262884  # s_slot_0057c8cc (entry 72)
SCROLL_COL_OFF = 262660    # s_slot_005651b4 (entry 16)
SCROLL_ROW_OFF = 262664    # s_slot_005651b8 (entry 17)
FIXTURE_SCROLL_COL = 20
FIXTURE_SCROLL_ROW = 62


def _overworld_cell_off(col: int, row: int) -> int:
    return (row * 0x40 + col) * 8   # entry 0, sav_offset 0, stride 8


def _game_dir() -> Path:
    import os
    gd = os.environ.get("OPENLORDS2_GAME_DIR")
    if not gd:
        sys.exit("OPENLORDS2_GAME_DIR not set -- run inside `nix develop`")
    p = Path(gd)
    if not p.is_dir():
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

    ref_cas = gd / "_ol2_reference" / "Castles.dat"
    src_cas = ref_cas if ref_cas.exists() else gd / "Castles.dat"
    if src_cas.exists():
        shutil.copyfile(src_cas, args.out_dir / "Castles.dat")

    data = bytearray(dst_sav.read_bytes())

    rec_a_off = TERR_OFF + COUNTY_A_KEY * TERR_STRIDE
    kind = data[rec_a_off + 0x08]
    col = data[rec_a_off + 0x0a]
    row = data[rec_a_off + 0x0b]
    if kind != 1 or col != COUNTY_A_COL or row != COUNTY_A_ROW:
        sys.exit(f"{src_sav} no longer matches the expected fixture shape "
                 f"(TERR_REC[{COUNTY_A_KEY}] kind={kind} col={col} row={row}, "
                 f"expected kind=1 col={COUNTY_A_COL} row={COUNTY_A_ROW}) -- "
                 "re-derive the constants at the top of this script.")

    rec_b_off = TERR_OFF + COUNTY_B_KEY * TERR_STRIDE
    if any(data[rec_b_off:rec_b_off + TERR_STRIDE]):
        sys.exit(f"{src_sav}'s TERR_REC[{COUNTY_B_KEY}] is no longer all-zero "
                 "-- pick a different unused key and re-derive.")

    b_off = _overworld_cell_off(COUNTY_B_COL, COUNTY_B_ROW)
    if data[b_off + 5] != 0 or data[b_off + 1] != 0:
        sys.exit(f"map cell (col={COUNTY_B_COL},row={COUNTY_B_ROW}) is no longer "
                 "empty/unflagged -- pick a different cell and re-derive.")

    local_player = struct.unpack_from("<i", data, LOCAL_PLAYER_OFF)[0]

    # County A: owner -> local_player + reciprocal overworld stamp (same
    # patch tools/make_owned_county_fixture.py applies).
    owner_a_before = data[rec_a_off + 0x00]
    data[rec_a_off + 0x00] = local_player & 0xff
    a_off = _overworld_cell_off(COUNTY_A_COL, COUNTY_A_ROW)
    overworld_a_before = data[a_off + 5]
    data[a_off + 5] = COUNTY_A_KEY

    # County B: byte-for-byte copy of county A's (now-patched) record --
    # already self-consistent real game data -- relocated cosmetically to
    # (COUNTY_B_COL, COUNTY_B_ROW), plus its own reciprocal stamp.
    data[rec_b_off:rec_b_off + TERR_STRIDE] = data[rec_a_off:rec_a_off + TERR_STRIDE]
    data[rec_b_off + 0x0a] = COUNTY_B_COL
    data[rec_b_off + 0x0b] = COUNTY_B_ROW
    data[b_off + 5] = COUNTY_B_KEY

    scroll_before = struct.unpack_from("<ii", data, SCROLL_COL_OFF)
    struct.pack_into("<i", data, SCROLL_COL_OFF, FIXTURE_SCROLL_COL)
    struct.pack_into("<i", data, SCROLL_ROW_OFF, FIXTURE_SCROLL_ROW)

    dst_sav.write_bytes(data)

    print(f"wrote {dst_sav}")
    print(f"  local_player={local_player}")
    print(f"  TERR_REC[{COUNTY_A_KEY}] owner {owner_a_before} -> {local_player}")
    print(f"  g_overworld_map[{a_off}]+5 {overworld_a_before} -> {COUNTY_A_KEY}")
    print(f"  TERR_REC[{COUNTY_B_KEY}] <- copy of TERR_REC[{COUNTY_A_KEY}], "
         f"col/row -> ({COUNTY_B_COL},{COUNTY_B_ROW})")
    print(f"  g_overworld_map[{b_off}]+5 0 -> {COUNTY_B_KEY}")
    print(f"  scroll {scroll_before} -> ({FIXTURE_SCROLL_COL}, {FIXTURE_SCROLL_ROW})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
