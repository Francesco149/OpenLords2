#!/usr/bin/env python3
"""Combined FILM pass for the LoTR II "AI cheats" video: drive N turns at a set
difficulty, and per turn log the economy dataset (cheat_data) AND capture the
enemy AI's point-of-view frame (ai_pov).  One deterministic pass → a CSV and a
frame sequence that are guaranteed in sync (same seed, same run).

    tools/film.py --difficulty 3 --turns 40 --pov 4 \
        --out-csv runs/probe/film_hard.csv --frames runs/probe/frames_hard \
        --frame-every 3

Capture cadence: `--frame-every K` grabs the POV realm's strategic map every K
turns (K=0 → never; establishing shots only).  Turn 0 also grabs the human POV
and the tax screen for establishing/contrast.  Restores the human POV before
each end-turn so the sim advances from a consistent view.

Sits on the running probe daemon (boot it first).  Stops cleanly when end-turn
stops advancing (game over / a modal), writing whatever it captured.
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_pov as A
import cheat_data as C


def open_tax_and_shot(path: str):
    """Open the Tax panel on the selected county and screenshot it (the
    displayed-vs-collected 'People pay N' shot).  Tax label anchor 515,218 →
    screen 0x15; RMB backs out.  Best-effort."""
    A.send({"cmd": "tomap"}); A.sleep_ms(200)
    A.click(515, 218); A.sleep_ms(500)          # Tax label → tax screen
    A.send({"cmd": "shot", "path": path}); A.sleep_ms(150)
    A.click(320, 240, "r"); A.sleep_ms(300)     # RMB out of the panel


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--difficulty", type=int, default=3)
    ap.add_argument("--turns", type=int, default=40)
    ap.add_argument("--pov", type=int, default=4, help="AI slot to film (POV)")
    ap.add_argument("--out-csv", default="runs/probe/film.csv")
    ap.add_argument("--frames", default="runs/probe/frames")
    ap.add_argument("--frame-every", type=int, default=3, help="capture POV frame every K turns (0=never)")
    a = ap.parse_args()

    fdir = Path(a.frames); fdir.mkdir(parents=True, exist_ok=True)
    outc = Path(a.out_csv); outc.parent.mkdir(parents=True, exist_ok=True)

    A.poke_i32(C.DIFFICULTY, a.difficulty)
    print(f"difficulty -> {A.read(C.DIFFICULTY,'i32')}")
    A.send({"cmd": "tomap"}); A.sleep_ms(300)
    gt = C.grant_table()

    # establishing shots (turn 0)
    A.send({"cmd": "shot", "path": str(fdir / "human_t00.png")}); A.sleep_ms(150)
    open_tax_and_shot(str(fdir / "taxscreen_t00.png"))

    n = 0
    with outc.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=C.COLS); w.writeheader()
        for t in range(0, a.turns + 1):
            if t > 0:
                r = A.send({"cmd": "endturn", "timeout": 25.0})
                A.sleep_ms(300)
                if not r.get("advanced"):
                    print(f"turn {t}: endturn did not advance -> stop ({r.get('err','')})")
                    break
            diff = A.read(C.DIFFICULTY, "i32")
            year = A.read(0x553edc, "i32"); season = A.read(0x57c934, "i32")
            rows = C.realm_snapshot(gt, diff)
            for s in sorted(rows):
                w.writerow({k: {"turn": t, "year": year, "season": season, **rows[s]}.get(k, "")
                            for k in C.COLS})
                n += 1
            # POV frame of the enemy economy
            if a.frame_every and (t % a.frame_every == 0):
                _pov_shot(a.pov, str(fdir / f"ai{a.pov}_t{t:02d}.png"))
                A._restore()  # back to human before next end-turn
            if t % 5 == 0:
                cash = {s: rows[s]["treasury"] for s in sorted(rows)}
                lvl = {s: rows[s]["level"] for s in sorted(rows) if rows[s]["who"] == "ai"}
                print(f"  turn {t:>2} (yr {year} s{season}): treasuries {cash} lvls {lvl}")
    print(f"wrote {n} rows -> {outc}; frames -> {fdir}")


def _pov_shot(slot: int, path: str):
    """Switch camera+HUD to `slot` and capture the enemy's strategic map."""
    grid = A.load_region_grid(); owners = A.region_owner_map()
    px, reg = A.owned_center_pixel(slot, grid, owners)
    if px is None:
        # realm eliminated / owns nothing — grab whatever is on screen
        A.force_present_shot(path); return
    A.click(A.MM_SCREEN_X0 + px[0], A.MM_SCREEN_Y0 + px[1]); A.sleep_ms(400)
    A.poke_i32(A.VIEWER, slot)
    if reg:
        A.poke_i32(A.CURTERR, reg)
    A.force_present_shot(path)


if __name__ == "__main__":
    main()
