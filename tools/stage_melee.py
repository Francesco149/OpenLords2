#!/usr/bin/env python3
"""
tools/stage_melee.py — stage a CLEAN, controllable melee lock between two chosen classes
and log the attrition curve (+ optional footage). The melee unlock (2026-07-10): the
adjacency scanner reads the cell-occupancy grid @0x5440e5 indexed by each unit's cell-index
field (base-0x10). Plain setpos moved col/row but left those at the DEPLOY cell → units were
melee-invisible. `kiting_demo.setpos(..., grid=True)` now registers the grid, so two lines
placed adjacent lock and fight on command — no long bridge-march needed.

Verifies the melee unit-behaviour findings live:
  #1 knight-is-a-trap  : classA=swordsman classB=knight → knight loses the grind (swings slower)
  #8 attrition-by-move-period, #6 initiator asymmetry : read the per-unit timers below.

Fields (base 0x5544ac, stride 0x1b0): +0x158 swing-timer, +0x159 lock-state, +0x15b intake,
+0x15e speed-tier, +0x174 troops, +0x148 target, +0x05 action(4=melee).

Usage: nix develop -c python3 tools/stage_melee.py --a swordsman --b knight --n 20 [--out mp4]
"""
import argparse, os, sys, time, socket, json, subprocess
sys.path.insert(0, os.path.dirname(__file__))
import kiting_demo as kd, micro_bot as mb

PORT = 27099
UT, UST = 0x5544ac, 0x1b0
def rpc(req, t=20):
    s = socket.socket(); s.settimeout(t); s.connect(("127.0.0.1", PORT))
    s.sendall((json.dumps(req) + "\n").encode()); buf = b""
    while b"\n" not in buf:
        c = s.recv(65536)
        if not c: break
        buf += c
    return json.loads(buf.split(b"\n")[0].decode()) if buf else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", default="swordsman"); ap.add_argument("--b", default="knight")
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--rounds", type=int, default=30)
    ap.add_argument("--row", type=int, default=40, help="contact row (on land, mid-lane)")
    ap.add_argument("--out", default=None, help="mp4 out (enables follow-cam capture)")
    ap.add_argument("--both", action="store_true", help="order BOTH sides to attack (default: A attacks)")
    a = ap.parse_args()

    g = kd.Demo(PORT); d = g.d; b = g.b
    g.stage({kd.CLASS[a.a]: a.n}, {kd.CLASS[a.b]: a.n})
    us = g.units(); A = [u for u in us if u["side"] == 0]; B = [u for u in us if u["side"] == 1]
    if not A or not B:
        print("deploy failed — restart daemon"); return
    b.start(); time.sleep(0.4); kd.arm_ingame(d)
    # interleave two adjacent lines on land: A at row, B at row-1, shared columns
    cols = list(range(46, 46 + max(len(A), len(B))))
    for k, u in enumerate(A): g.setpos(u["idx"], cols[k % len(cols)], a.row, grid=True)
    for k, u in enumerate(B): g.setpos(u["idx"], cols[k % len(cols)], a.row - 1, grid=True)

    fdir = None
    if a.out:
        fdir = a.out.rsplit(".", 1)[0] + "_frames"; os.makedirs(fdir, exist_ok=True)
        for x in os.listdir(fdir):
            if x.endswith(".png"): os.remove(os.path.join(fdir, x))
    n = 0
    print(f"MELEE {a.a}(A,side0) vs {a.b}(B,side1), {a.n} troops each, contact row {a.row}")
    curve = []
    for rnd in range(a.rounds):
        us = g.units(); A = [u for u in us if u["side"] == 0]; B = [u for u in us if u["side"] == 1]
        ah = sum(u["hp"] for u in A); bh = sum(u["hp"] for u in B)
        # sample a locked pair's swing timers
        la = next((u for u in A if g.rd(UT + u["idx"] * UST + 0x159, "u8") in (1, 2)), A[0] if A else None)
        lb = next((u for u in B if g.rd(UT + u["idx"] * UST + 0x159, "u8") in (1, 2)), B[0] if B else None)
        sw_a = g.rd(UT + la["idx"] * UST + 0x158, "u8") if la else 0
        sw_b = g.rd(UT + lb["idx"] * UST + 0x158, "u8") if lb else 0
        curve.append((rnd, ah, bh))
        sc = g.rd(0x4eac50, "u8")
        if sc is None: sc = 0x29
        sw_a = sw_a or 0; sw_b = sw_b or 0
        print(f"r{rnd:2d}: {a.a}HP={ah:3d} {a.b}HP={bh:3d} | swingtimer {a.a}={sw_a} {a.b}={sw_b} scr=0x{sc:x}")
        if not A or not B or sc < 0x28 or sc > 0x2a: print("ended"); break
        # (re)issue attack orders to keep the lock fed
        tb = kd.nearest({"col": 50, "row": a.row - 1}, B)
        for fm in sorted({u["form"] for u in A}): b.order(fm, tb["col"], tb["row"], tb["idx"])
        if a.both:
            ta = kd.nearest({"col": 50, "row": a.row}, A)
            for fm in sorted({u["form"] for u in B}): b.order(fm, ta["col"], ta["row"], ta["idx"])
        if fdir:
            live = [u for u in us if u["hp"] > 0]
            if live:
                b.center_on(sum(u["col"] for u in live)/len(live), sum(u["row"] for u in live)/len(live))
            d.poke(0x53e9ac, [4, 0, 0, 0]); d.rpc({"cmd": "sleep", "ms": 60})
            if rpc({"cmd": "shotd", "path": f"{fdir}/f{n:03d}.png"}): n += 1
        d.poke(0x4eac50, [0x29]); d.poke(0x53f238, [0, 0, 0, 0]); d.rpc({"cmd": "sleep", "ms": 300})

    a0 = curve[0]; aL = curve[-1]
    print(f"\nRESULT: {a.a} {a0[1]}→{aL[1]} (lost {a0[1]-aL[1]}) | {a.b} {a0[2]}→{aL[2]} (lost {a0[2]-aL[2]})")
    if fdir and n:
        subprocess.run(["ffmpeg", "-y", "-framerate", "10", "-i", f"{fdir}/f%03d.png",
                        "-vf", "crop=478:446:0:16", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                        "-crf", "18", a.out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"assembled {n} frames → {a.out}")

if __name__ == "__main__":
    main()
