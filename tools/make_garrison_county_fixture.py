#!/usr/bin/env python3
"""
tools/make_garrison_county_fixture.py — build a --save-ref fixture that
organically exercises engine_army_move_confirm's `g_classify_garrison`
branch (-> `sibling_9372`, the LIVE producer of the 0x11 event popup) via
a real click, continuing session 19's [[project_event_popup_arms]] work
(swap branch closed via tools/make_swap_county_fixture.py; this is the
"most valuable next" sibling flagged there, since sibling_9372 is the
actual 0x11 popup producer, not just another confirm-dialog leaf).

Reuses county A (key 4, the one real kind==1 record in test.sav, keep at
map cell col=30/row=46) as the armed county, same as the swap fixture.
The garrison branch doesn't need a second COUNTY though -- it needs a
target CELL with descriptor flag byte (+1) bit 0x80 set (the same
"keep/garrisonable" flag county A's own keep tile carries) whose
occupant-army stamp (+7) points at a FRIENDLY (locally-owned) army
record. Map cell (31,46) -- one tile east of the keep -- already
naturally carries type=0x15/flags=0x80 in test.sav (same terrain
category as the keep itself; confirmed via direct save inspection) and
is otherwise unclaimed (head_terr==0), so the ONLY byte patched there is
its occupant stamp (+7), redirected from the map's baseline default (2,
confirmed uniformly present across this whole neighborhood -- not a real
placed army) to a fabricated friendly record at ARMY_REC key 6.

ARMY_REC key 6 (confirmed unused/all-zero-but-one-padding-byte in
test.sav) is a byte-for-byte copy of ARMY_REC key 4 (owner=1, a real
populated record -- same "reuse real, self-consistent data" approach the
swap fixture used for TERR_REC), with ONE field patched: keep level
(+0x1c0) forced from 1 to 6. `engine_combat_garrison_cap(6)` reads
`k_garrison_cap[6] == 0` (src/engine_combat.c's baked table), which
makes `sibling_9372` take the `cap < 1` branch and post event 0x11b --
deliberately chosen over the "cap < county A's headcount" (0xa6, THE
0x11 popup itself) branch, which host tests already cover end-to-end
(session 14) and which this specific table has no reachable keep level
for anyway (headcount is 47; the table's only entries below that are 0,
which routes to 0x11b first). 0x11b has never been organically OR
host-tested before this.

Screen coordinates derived the same way as the swap fixture (a faithful
Python port of engine_camera_cursor_to_cell + engine_map_init_
build_cells' rotation-0 projection, round-trip verified) at the same
fixed scroll (20,62):
  county 4 keep (mc=30,mr=46)  -> screen cell (4,15) -> pixel (260,250)
  garrison target (mc=31,mr=46) -> screen cell (4,16) -> pixel (290,265)
(NOTE the projection's sc uses C's truncating `sc2/2` division, not exact
division -- adjacent map cells can share sc while sr, which increments
every map-column step, keeps each (sr,sc) pair unique. An earlier
one-tile-off version of this derivation script asserted sc2 must be even
and would have raised on this exact cell; re-derive with floor division,
not exact division, if extending this further.)

Usage:
    nix develop -c python3 tools/make_garrison_county_fixture.py OUT_DIR

Then:
    nix develop -c python3 tools/trace_studio_v3/olv3_capture.py \\
        --trace garrison_county_click --only port --no-view --save-ref OUT_DIR
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
COUNTY_A_KEY = 4
COUNTY_A_COL = 30
COUNTY_A_ROW = 46

ARMY_OFF = 86344           # g_per_army_768, sav_offset (entry 4)
ARMY_STRIDE = 0x300
ARMY_SRC_KEY = 4            # real, populated, owner==local_player record
ARMY_DST_KEY = 6            # confirmed fully-unused (1 padding byte) in test.sav
ARMY_KEEPLVL_OFF = 0x1c0    # engine_army_ai.c ARMY_KEEP_LEVEL

GARRISON_COL = 31           # 1 tile east of the keep -- naturally type=0x15/
GARRISON_ROW = 46           # flags=0x80 already, per direct save inspection

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

    g_off = _overworld_cell_off(GARRISON_COL, GARRISON_ROW)
    g_type, g_flags = data[g_off + 0], data[g_off + 1]
    if g_flags & 0x80 == 0 or g_type <= 0x14 or data[g_off + 5] != 0:
        sys.exit(f"map cell (col={GARRISON_COL},row={GARRISON_ROW}) no longer has the "
                 f"expected shape (type={g_type} flags={g_flags:#x} head_terr="
                 f"{data[g_off + 5]}, expected type>0x14 flags&0x80!=0 head_terr==0) "
                 "-- re-derive the constants at the top of this script.")

    army_src_off = ARMY_OFF + ARMY_SRC_KEY * ARMY_STRIDE
    army_dst_off = ARMY_OFF + ARMY_DST_KEY * ARMY_STRIDE
    # Confirmed via direct inspection: every unused army slot in test.sav
    # carries exactly one nonzero padding byte, owner(+5) and value(+0x168)
    # both 0 -- check those two fields specifically rather than requiring
    # a strictly all-zero record.
    if data[army_dst_off + 5] != 0 or struct.unpack_from("<i", data, army_dst_off + 0x168)[0] != 0:
        sys.exit(f"{src_sav}'s ARMY_REC[{ARMY_DST_KEY}] is no longer unused "
                 "(owner or value nonzero) -- pick a different unused key and re-derive.")

    local_player = struct.unpack_from("<i", data, LOCAL_PLAYER_OFF)[0]

    # County A: owner -> local_player + reciprocal overworld stamp (same
    # patch tools/make_owned_county_fixture.py applies).
    owner_a_before = data[rec_a_off + 0x00]
    data[rec_a_off + 0x00] = local_player & 0xff
    a_off = _overworld_cell_off(COUNTY_A_COL, COUNTY_A_ROW)
    overworld_a_before = data[a_off + 5]
    data[a_off + 5] = COUNTY_A_KEY

    # Friendly garrison-target army: byte-for-byte copy of ARMY_REC[4]
    # (already owner==local_player, self-consistent real data), keep
    # level forced to 6 so engine_combat_garrison_cap(6) == 0.
    data[army_dst_off:army_dst_off + ARMY_STRIDE] = data[army_src_off:army_src_off + ARMY_STRIDE]
    keeplvl_before = data[army_dst_off + ARMY_KEEPLVL_OFF]
    data[army_dst_off + ARMY_KEEPLVL_OFF] = 6

    # Redirect the garrison-target cell's occupant stamp at the new
    # friendly army (leaves type/flags/head_terr untouched -- already
    # exactly what the classify gate needs).
    head_army_before = data[g_off + 7]
    data[g_off + 7] = ARMY_DST_KEY

    scroll_before = struct.unpack_from("<ii", data, SCROLL_COL_OFF)
    struct.pack_into("<i", data, SCROLL_COL_OFF, FIXTURE_SCROLL_COL)
    struct.pack_into("<i", data, SCROLL_ROW_OFF, FIXTURE_SCROLL_ROW)

    dst_sav.write_bytes(data)

    print(f"wrote {dst_sav}")
    print(f"  local_player={local_player}")
    print(f"  TERR_REC[{COUNTY_A_KEY}] owner {owner_a_before} -> {local_player}")
    print(f"  g_overworld_map[{a_off}]+5 {overworld_a_before} -> {COUNTY_A_KEY}")
    print(f"  ARMY_REC[{ARMY_DST_KEY}] <- copy of ARMY_REC[{ARMY_SRC_KEY}], "
         f"keeplvl(+0x1c0) {keeplvl_before} -> 6")
    print(f"  g_overworld_map[{g_off}]+7 {head_army_before} -> {ARMY_DST_KEY}")
    print(f"  scroll {scroll_before} -> ({FIXTURE_SCROLL_COL}, {FIXTURE_SCROLL_ROW})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
