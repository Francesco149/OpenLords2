#!/usr/bin/env python3
"""camera_director.py — record a LoTR II AI-play run with an ACTION-FOLLOWING camera.

Each turn, snapshot every county's owner + garrison, diff vs last turn to find the
most interesting change, and pan the camera there (the game's own minimap-recentre):

  ownership flip  = conquest / battle aftermath   (TOP priority — AI battles resolve
                    HEADLESS, so the county flipping IS the visible event)
  garrison surge  = an army was raised / marched in
  garrison drop   = an army marched out / was lost

Records continuously to h264 (agent frame-stream, decoupled from driving) and stops
on the domination win (screen 0x1f).  In turbo the result is a real fast-motion
timelapse that stays ON the action instead of a fixed camera.

    camera_director.py --turns 30 --out runs/probe/ai_play.mp4 --fps 30 --hold 1.2

Boot the probe daemon first (--present, usually --turbo).
"""
from __future__ import annotations
import argparse, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_pov as A
import cheat_data as C

SCREEN = 0x004eac50
END_SCENARIO = 0x1f
HUMAN_SLOT, HUMAN_REGION, KNIGHT_IDX, GARRISON = 1, 13, 5, 6000


def rd(va):
    return A.send({"cmd": "read", "va": va, "type": "i32"}).get("val")


def region_centroid_px(region, grid):
    pts = grid.get(region)
    if not pts:
        return None
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    return min(pts, key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2)


def garrison(region):
    f = A.reads([(f"t{i}", C.rec(region, C.R_COUNTS + i * 2), "i16") for i in range(7)])
    return sum(max(0, f.get(f"t{i}") or 0) for i in range(7))


def snapshot(grid, owners):
    """{region: (owner_slot, garrison_headcount)} for every real county."""
    return {r: (owners.get(r, 0), garrison(r)) for r in grid}


def pick_target(prev, cur, surge):
    """The single most interesting county change this turn, or None.
    (priority, magnitude) ordered; conquest > army-in > army-out."""
    best = None
    for r, (co, cg) in cur.items():
        po, pg = prev.get(r, (0, 0))
        if co and po != co:
            cand = (3, 1, r, f"county {r} CONQUERED (realm {po or 'neutral'}→{co})")
        elif cg - pg >= surge:
            cand = (2, cg - pg, r, f"county {r} army +{cg - pg}")
        elif pg - cg >= surge:
            cand = (1, pg - cg, r, f"county {r} army -{pg - cg}")
        else:
            continue
        if best is None or cand[:2] > best[:2]:
            best = cand
    return best


def keepalive():
    A.send({"cmd": "poke", "va": C.rec(HUMAN_REGION, C.R_COUNTS + KNIGHT_IDX * 2),
            "bytes": [GARRISON & 0xFF, (GARRISON >> 8) & 0xFF]})
    A.poke_i32(A.strat(HUMAN_SLOT, C.S_CASH), 1000)


def pan_to(region, grid, owners):
    """Recentre the isometric camera on `region` (the game's minimap transform),
    view it as its owner (owned info card), force a redraw, and move the cursor off
    the minimap so the 'Kingdom view' hover tooltip clears."""
    px = region_centroid_px(region, grid)
    if px is None:
        return False
    A.click(A.MM_SCREEN_X0 + px[0], A.MM_SCREEN_Y0 + px[1])
    A.sleep_ms(250)
    owner = owners.get(region)
    if owner:
        A.poke_i32(A.VIEWER, owner)
    A.poke_i32(A.CURTERR, region)
    A.send({"cmd": "move", "x": 320, "y": 240})       # cursor off minimap → clear tooltip
    A.poke_i32(A.REDRAW, 2)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--turns", type=int, default=30)
    ap.add_argument("--out", default="runs/probe/ai_play.mp4")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--hold", type=float, default=1.2, help="seconds to dwell on each action")
    ap.add_argument("--difficulty", type=int, default=3)
    ap.add_argument("--surge", type=int, default=40, help="troop delta that counts as an army event")
    ap.add_argument("--no-survive", action="store_true", help="don't keep the human alive")
    a = ap.parse_args()

    A.poke_i32(C.DIFFICULTY, a.difficulty)
    grid = A.load_region_grid()
    prev = snapshot(grid, A.region_owner_map())
    print("difficulty", rd(C.DIFFICULTY), "counties", len(grid))
    print("rec start:", A.send({"cmd": "record", "action": "start",
                                "path": a.out, "fps": a.fps}))
    panned = 0
    try:
        for t in range(a.turns):
            A.poke_i32(A.VIEWER, HUMAN_SLOT)               # human POV to End-Turn
            if not a.no_survive:
                keepalive()
            A.send({"cmd": "tomap"})
            r = A.send({"cmd": "endturn", "timeout": 18.0})
            if not r.get("advanced") or rd(SCREEN) == END_SCENARIO:
                print(f"turn {t + 1}: game over / stalled -> stop")
                break
            owners = A.region_owner_map()
            cur = snapshot(grid, owners)
            tgt = pick_target(prev, cur, a.surge)
            prev = cur
            if tgt:
                _, _, reg, reason = tgt
                pan_to(reg, grid, owners)
                panned += 1
                print(f"turn {t + 1}: PAN -> {reason}")
            else:
                A.poke_i32(A.REDRAW, 2)
                print(f"turn {t + 1}: (no notable action)")
            time.sleep(a.hold)
    finally:
        print("rec stop:", A.send({"cmd": "record", "action": "stop"}))
        print(f"panned to action on {panned} turns")


if __name__ == "__main__":
    main()
