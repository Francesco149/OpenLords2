#!/usr/bin/env python3
"""
tools/make_enemy_target_fixture.py — build a --save-ref fixture that
organically exercises the LAST unexercised confirm-dialog siblings of
engine_army_move_confirm's click resolver (`sibling_983b` via both its
claim and attack classifications, and `sibling_960f` via direct), closing
the [[project_event_popup_arms]] session-20 "2 siblings remain" gap.

The session-20 assumption that these would be "harder to stage since they
need a genuine enemy-owned record" turned out WRONG on inspection: test.sav
already carries everything naturally.

  - ARMY_REC[2] is a real record with owner==2 != local_player(1), and
    the map's uniform baseline occupant stamp (+7 byte == 2 at every
    neighborhood cell, session-20 finding) already points at it. So the
    "enemy army under the cursor" condition (`ARMY_REC(CELL[+7])[5] !=
    armed_owner`) holds at ANY baseline cell once county A is owned by
    the local player — no fabricated record needed at all.
  - (33,47), 3 tiles SE of the keep, is naturally t=0x15/f=0x20 (an
    enemy FIELD tile): the attack classification (bit 0x20 + terrain in
    [2,0x17)) fires with ZERO patches -> confirm msg 10 "Destroy field?".
  - (31,46), the tile session 20 patched FRIENDLY to reach garrison, is
    naturally t=0x15/f=0x80: left enemy-stamped it classifies DIRECT ->
    `sibling_960f`, confirm msg 8 "Besiege castle?".  ARMY_REC[2]'s
    link(+0x1bc)==4 -> TERR_REC[4](+0x19a)==0 -> the silent-execute
    branch (army marches; no event post).
  - claim is the only classification with no naturally-flagged cell in
    range: TWO bytes patched — (32,46) flags(+1) 0x00 -> 0x10 AND type(+0)
    0x00 -> 0x10 -> confirm msg 4 "Slaughter villagers?" (bit 0x10 ==
    village raid, not "claim"; the localization table renames the whole
    family: 0x20 == field, 0x80 == castle/besiege).  The type byte is
    load-bearing: engine_strat_cost_rebuild's ladder maps flag-0x10 cells
    to cost 0 (IMPASSABLE -> hover classifier disarms, cost<2) unless
    type==0x10, which yields cost 100 -> the >99 neighbour-fallback ->
    passable.  Found empirically: a flags-only patch left the click
    falling through to the plain-move path.

Everything else is the standard make_owned_county_fixture.py patch:
county A (TERR_REC key 4, the one kind==1 record, keep at mc=30/mr=46)
owner -> local_player, reciprocal overworld stamp, scroll pinned (20,62).

Screen coordinates (same projection + round-trip rigor as sessions 18-20;
sr=1+mr+mc, sc=(0x40-mr+mc)//2, view-mode-0 camera, pixel = cell origin
+(50,16)):
  keep    (30,46) -> screen(4,15) -> pixel(260,250) lParam 16384260
  claim   (32,46) -> screen(5,17) -> pixel(320,280) lParam 18350400
  attack  (33,47) -> screen(5,19) -> pixel(320,310) lParam 20316480
  besiege (31,46) -> screen(4,16) -> pixel(290,265) lParam 17367330

Usage:
    nix develop -c python3 tools/make_enemy_target_fixture.py OUT_DIR

Then (one fixture serves all three traces):
    nix develop -c python3 tools/trace_studio_v3/olv3_capture.py \\
        --trace slaughter_county_click --only port --no-view --save-ref OUT_DIR
    ... same for attack_field_click / besiege_county_click.
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
ENEMY_ARMY_KEY = 2         # real owner==2 record the baseline stamps point at

CLAIM_COL = 32             # empty/unflagged cell; gets flag bit 0x10
CLAIM_ROW = 46
ATTACK_COL = 33            # naturally t=0x15/f=0x20 (enemy field) — untouched
ATTACK_ROW = 47
BESIEGE_COL = 31           # naturally t=0x15/f=0x80 — untouched
BESIEGE_ROW = 46

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

    local_player = struct.unpack_from("<i", data, LOCAL_PLAYER_OFF)[0]

    # The natural preconditions this fixture RELIES on (fails loudly if the
    # source save drifts rather than silently testing something else):
    enemy_off = ARMY_OFF + ENEMY_ARMY_KEY * ARMY_STRIDE
    if data[enemy_off + 5] == (local_player & 0xff):
        sys.exit(f"ARMY_REC[{ENEMY_ARMY_KEY}] is no longer enemy-owned "
                 f"(owner={data[enemy_off + 5]} == local_player) -- re-derive.")
    for name, c, r, want_type_min, want_flags in (
            ("attack", ATTACK_COL, ATTACK_ROW, 2, 0x20),
            ("besiege", BESIEGE_COL, BESIEGE_ROW, 0x15, 0x80)):
        o = _overworld_cell_off(c, r)
        if data[o + 1] != want_flags or data[o + 0] < want_type_min \
                or data[o + 5] != 0 or data[o + 7] != ENEMY_ARMY_KEY:
            sys.exit(f"{name} cell ({c},{r}) no longer has the expected natural "
                     f"shape (type={data[o]:#x} flags={data[o + 1]:#x} "
                     f"head_terr={data[o + 5]} occ={data[o + 7]}) -- re-derive.")
    c_off = _overworld_cell_off(CLAIM_COL, CLAIM_ROW)
    if data[c_off + 1] != 0 or data[c_off + 5] != 0 or data[c_off + 7] != ENEMY_ARMY_KEY:
        sys.exit(f"claim cell ({CLAIM_COL},{CLAIM_ROW}) no longer empty/unflagged "
                 f"(flags={data[c_off + 1]:#x} head_terr={data[c_off + 5]} "
                 f"occ={data[c_off + 7]}) -- re-derive.")

    # County A: owner -> local_player + reciprocal overworld stamp (the
    # standard make_owned_county_fixture.py patch).
    owner_a_before = data[rec_a_off + 0x00]
    data[rec_a_off + 0x00] = local_player & 0xff

    # Movement spent this turn (+0x153) 5 -> 0: full headroom (15).  The
    # natural field cluster's cheapest cell floods at cost 14 from the
    # keep, over the source save's 15-5=10 headroom -- the hover
    # classifier's final gate zeroes ALL classifications for cost >
    # headroom (found empirically; attack armed then got zeroed).
    spent_before = data[rec_a_off + 0x153]
    data[rec_a_off + 0x153] = 0
    a_off = _overworld_cell_off(COUNTY_A_COL, COUNTY_A_ROW)
    overworld_a_before = data[a_off + 5]
    data[a_off + 5] = COUNTY_A_KEY

    # Claim target: village flag bit 0x10 + terrain type 0x10 (the only
    # type engine_strat_cost_rebuild keeps passable for flag-0x10 cells).
    data[c_off + 1] = 0x10
    data[c_off + 0] = 0x10

    scroll_before = struct.unpack_from("<ii", data, SCROLL_COL_OFF)
    struct.pack_into("<i", data, SCROLL_COL_OFF, FIXTURE_SCROLL_COL)
    struct.pack_into("<i", data, SCROLL_ROW_OFF, FIXTURE_SCROLL_ROW)

    dst_sav.write_bytes(data)

    print(f"wrote {dst_sav}")
    print(f"  local_player={local_player}")
    print(f"  TERR_REC[{COUNTY_A_KEY}] owner {owner_a_before} -> {local_player}, "
          f"movement-spent(+0x153) {spent_before} -> 0")
    print(f"  g_overworld_map[{a_off}]+5 {overworld_a_before} -> {COUNTY_A_KEY}")
    print(f"  claim cell ({CLAIM_COL},{CLAIM_ROW}) flags 0x00 -> 0x10, type 0x00 -> 0x10")
    print(f"  attack ({ATTACK_COL},{ATTACK_ROW}) + besiege ({BESIEGE_COL},{BESIEGE_ROW}): natural, untouched")
    print(f"  scroll {scroll_before} -> ({FIXTURE_SCROLL_COL}, {FIXTURE_SCROLL_ROW})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
