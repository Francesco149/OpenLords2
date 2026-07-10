#!/usr/bin/env python3
"""tools/film_strategic.py — CONTINUOUS-video capture of the strategic map across turns
(the strategic analogue of film_battle.py). The daemon is single-threaded, so we can't
capture concurrently with a blocking `endturn`; instead we replicate end-turn as a
click + capture-poll-dismiss loop on ONE socket, grabbing a frame every ~step-ms the
whole time. Output: a frame sequence + turnmap.json (turn -> first-frame-index) + mp4.

This upgrades the "one still held per turn" Layout-B footage to real motion (the end-turn
resolution, report scrolls popping, season/colour changes, the AI's map redrawing).

    # boot into a loaded game FIRST:
    nix develop -c python3 tools/probe_daemon.py \
        --segtrace tools/traces/new_game_to_map.jsonl --rng-seed --run-id clips
    nix develop -c python3 tools/film_strategic.py --pov 4 --turns 30 --survive \
        --out runs/probe/strat/ai4_pov.mp4

Notes: --pov N films realm N's POV (camera+HUD switched each turn). --survive keeps a
passive human alive so an AI can reach the level-3 grant (600->1800). --fps sets the
OUTPUT rate (frames are duplicated to hit it for a smooth 60fps at natural pace).
"""
from __future__ import annotations
import argparse, json, os, socket, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, os.path.dirname(__file__))
import ai_pov as A
import cheat_data as C

V_SCREEN, V_SEASON, V_YEAR = 0x4EAC50, 0x57C934, 0x553EDC
END_TURN_XY, SCROLL_XY = (558, 469), (300, 240)
HUMAN_SLOT, HUMAN_REGION, KNIGHT_IDX, GARRISON = 1, 13, 5, 6000


class Sock:
    """One persistent connection; the daemon is single-threaded so we stay serial."""
    def __init__(self, port):
        self.port = port
    def rpc(self, req, t=90):
        s = socket.socket(); s.settimeout(t); s.connect(("127.0.0.1", self.port))
        s.sendall((json.dumps(req) + "\n").encode()); b = b""
        while b"\n" not in b:
            c = s.recv(65536)
            if not c: break
            b += c
        s.close()
        return json.loads(b.split(b"\n")[0]) if b else None
    def rd(self, va, ty="i32"): return self.rpc({"cmd": "read", "va": va, "type": ty}).get("val")
    def click(self, x, y, btn="l"): self.rpc({"cmd": "click", "x": x, "y": y, "button": btn})
    def shot(self, path): return self.rpc({"cmd": "shotd", "path": path}, t=15)


def keepalive(sk):
    va_kn = C.rec(HUMAN_REGION, C.R_COUNTS + KNIGHT_IDX * 2)
    sk.rpc({"cmd": "poke", "va": va_kn, "bytes": [GARRISON & 0xFF, (GARRISON >> 8) & 0xFF]})
    sk.rpc({"cmd": "poke", "va": A.strat(HUMAN_SLOT, C.S_CASH),
            "bytes": list((1000).to_bytes(4, "little"))})


def set_pov(sk, slot, grid, owners):
    """Switch camera+HUD to realm `slot` (film its territory)."""
    px, reg = A.owned_center_pixel(slot, grid, owners)
    if px is not None:
        sk.click(A.MM_SCREEN_X0 + px[0], A.MM_SCREEN_Y0 + px[1])
        sk.rpc({"cmd": "poke", "va": A.VIEWER, "bytes": list(int(slot).to_bytes(4, "little"))})
        if reg:
            sk.rpc({"cmd": "poke", "va": A.CURTERR, "bytes": list(int(reg).to_bytes(4, "little"))})
    sk.rpc({"cmd": "poke", "va": A.REDRAW, "bytes": [2, 0, 0, 0]})   # force full strat redraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pov", type=int, default=4)
    ap.add_argument("--turns", type=int, default=30)
    ap.add_argument("--survive", action="store_true")
    ap.add_argument("--difficulty", type=int, default=3)
    ap.add_argument("--step-ms", type=int, default=70, help="capture cadence during resolution")
    ap.add_argument("--settle-frames", type=int, default=8, help="frames of the settled map per turn")
    ap.add_argument("--turn-timeout", type=float, default=25.0)
    ap.add_argument("--out", default="runs/probe/strat/pov.mp4")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--port", type=int, default=27099)
    a = ap.parse_args()

    sk = Sock(a.port)
    sk.rpc({"cmd": "poke", "va": A.DIFFICULTY, "bytes": list(int(a.difficulty).to_bytes(4, "little"))})
    # get to the free-roam map
    for _ in range(6):
        if sk.rd(V_SCREEN, "u8") == 0: break
        sk.click(*SCROLL_XY, "r"); time.sleep(0.3)

    grid = A.load_region_grid(); owners = A.region_owner_map()
    fdir = Path(a.out).with_suffix(""); fdir = Path(str(fdir) + "_frames")
    fdir.mkdir(parents=True, exist_ok=True)
    for x in fdir.glob("*.png"): x.unlink()

    n = 0
    turnmap = []          # (turn_index, first_frame_index)
    def grab():
        nonlocal n
        if sk.shot(str(fdir / f"f{n:05d}.png")): n += 1

    for t in range(a.turns):
        set_pov(sk, a.pov, grid, owners)
        turnmap.append([t, n])
        for _ in range(a.settle_frames):        # hold on the settled POV map
            grab(); time.sleep(a.step_ms / 1000)
        if a.survive:
            keepalive(sk)
        # End-Turn only fires for the HUMAN local-player, and only on the free-roam map
        # (screen 0). set_pov left viewer=AI on the county panel — restore + RMB out.
        sk.rpc({"cmd": "poke", "va": A.VIEWER, "bytes": list(int(HUMAN_SLOT).to_bytes(4, "little"))})
        for _ in range(4):
            if sk.rd(V_SCREEN, "u8") == 0: break
            sk.click(*SCROLL_XY, "r"); grab(); time.sleep(0.12)
        # end-turn, interleaving capture with the click/poll/dismiss
        s0, y0 = sk.rd(V_SEASON), sk.rd(V_YEAR)
        sk.click(*END_TURN_XY, "l"); last_click = time.time()
        deadline = time.time() + a.turn_timeout
        advanced = False
        while time.time() < deadline:
            scr = sk.rd(V_SCREEN, "u8")
            if scr == 0x27:                      # report/event scroll — capture it THEN dismiss
                grab(); sk.click(*SCROLL_XY, "r")
            elif scr == 0 and time.time() - last_click > 4.0:
                sk.click(*END_TURN_XY, "l"); last_click = time.time()
            grab()
            s1, y1 = sk.rd(V_SEASON), sk.rd(V_YEAR)
            if s1 != s0 or y1 != y0:
                advanced = True
                for _ in range(6):               # trailing new-turn scrolls
                    if sk.rd(V_SCREEN, "u8") != 0x27: break
                    grab(); sk.click(*SCROLL_XY, "r"); time.sleep(0.1)
                break
            time.sleep(a.step_ms / 1000)
        cash = [sk.rd(A.strat(i, C.S_CASH)) for i in range(1, 5)]
        print(f"turn {t:2d}: adv={advanced} yr={sk.rd(V_YEAR)} s={sk.rd(V_SEASON)} cash(P1-4)={cash} frames={n}", flush=True)
        if not advanced:
            print("  stalled — stopping."); break

    (fdir / "turnmap.json").write_text(json.dumps(turnmap))
    print(f"captured {n} frames over {len(turnmap)} turns → {fdir}")
    if n:
        subprocess.run(["ffmpeg", "-y", "-framerate", str(a.fps), "-i", str(fdir / "f%05d.png"),
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20",
                        "-movflags", "+faststart", a.out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"assembled → {a.out}")


if __name__ == "__main__":
    main()
