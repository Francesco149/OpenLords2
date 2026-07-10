#!/usr/bin/env python3
"""
tools/trigger_test.py — pin down WHICH capture/drive op actually crashes the harness
during live combat (2026-07-10). Runs ONE trigger to completion-or-crash on a freshly
staged fight (ingame gates set). Restart the daemon between triggers for a clean game.

Triggers:
  shotd   : async lock-free capture during run (the film_battle recipe) — baseline, expect OK
  shots   : in-tick synchronized capture (cmd 'shots') during run
  frozen  : capture while the sim is FROZEN (pause=-1)
  setpos  : mass per-unit setpos each round (the 'kite step-back' motion) + capture
  pileon  : 8 archer stacks (auto-calc instant-resolve threshold) + capture

Usage: nix develop -c python3 tools/trigger_test.py --trigger setpos [--rounds 25]
"""
import argparse, os, sys, time, socket, json
sys.path.insert(0, os.path.dirname(__file__))
import kiting_demo as kd

PORT = 27099
def rpc(req, t=20):
    s = socket.socket(); s.settimeout(t); s.connect(("127.0.0.1", PORT))
    s.sendall((json.dumps(req) + "\n").encode()); buf = b""
    while b"\n" not in buf:
        c = s.recv(65536)
        if not c: break
        buf += c
    return json.loads(buf.split(b"\n")[0].decode()) if buf else None
def alive():
    try:
        r = rpc({"cmd": "ping"}, 6); return bool(r and r.get("ok"))
    except Exception: return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trigger", required=True,
                    choices=["shotd", "shots", "frozen", "setpos", "pileon"])
    ap.add_argument("--enemy", default="mace")
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--enemy-n", type=int, default=24)
    ap.add_argument("--rounds", type=int, default=25)
    a = ap.parse_args()

    g = kd.Demo(PORT); d = g.d; b = g.b
    stacks = 8 if a.trigger == "pileon" else 5
    na = stacks * 4
    print(f"[{a.trigger}] staging {na} archers ({stacks} stacks) vs {a.enemy_n} {a.enemy}", flush=True)
    g.stage({kd.CLASS["archer"]: na}, {kd.CLASS[a.enemy]: a.enemy_n})
    us = g.units(); mine = [u for u in us if u["side"] == 0]
    if not mine:
        print("deploy failed — restart the daemon for a clean game"); return
    b.start(); time.sleep(0.4)
    d.poke(0x52af94, [3, 0, 0, 0]); d.poke(0x553030, [2, 0, 0, 0])   # ingame gates
    fire_row = 16
    for k, u in enumerate(mine):
        g.setpos(u["idx"], 48 + k, fire_row)
    print(f"staged; alive={alive()}", flush=True)

    fdir = f"runs/probe/trig_{a.trigger}"; os.makedirs(fdir, exist_ok=True)
    for rnd in range(a.rounds):
        us = g.units(); mine = [u for u in us if u["side"] == 0]; foe = [u for u in us if u["side"] == 1]
        ah = sum(u["hp"] for u in mine); fh = sum(u["hp"] for u in foe)
        sc = d.rpc({"cmd": "read", "va": 0x4eac50, "type": "u8"}).get("val")
        print(f"r{rnd:2d}: aHP={ah} eHP={fh} nfoe={len(foe)} scr=0x{sc:x}", flush=True)
        if not foe or sc < 0x28 or sc > 0x2a:
            print("battle ended"); break

        # order (attack the nearest foe)
        tf = kd.nearest({"col": 50, "row": fire_row}, foe)
        for fm in sorted({u["form"] for u in mine}):
            b.order(fm, tf["col"], tf["row"], tf["idx"])
        if not alive(): print(f"  !! CRASH on ORDER"); return

        # kite step-back: mass setpos our archers back one row toward spawn
        if a.trigger == "setpos":
            for k, u in enumerate(mine):
                g.setpos(u["idx"], 48 + k, min(fire_row + 1 + (rnd % 3), 70))
            if not alive(): print(f"  !! CRASH on MASS-SETPOS"); return

        # advance the sim
        d.poke(0x4eac50, [0x29]); d.poke(0x53f238, [0, 0, 0, 0])
        d.rpc({"cmd": "sleep", "ms": 230})
        if not alive(): print(f"  !! CRASH on RUN"); return

        # capture
        path = f"{fdir}/r{rnd:03d}.png"
        if a.trigger == "frozen":
            d.poke(0x53f238, [255, 255, 255, 255])   # freeze
            d.poke(0x53e9ac, [4, 0, 0, 0]); d.rpc({"cmd": "sleep", "ms": 40})
            r = rpc({"cmd": "shotd", "path": path})
            d.poke(0x53f238, [0, 0, 0, 0])            # unfreeze
        elif a.trigger == "shots":
            r = rpc({"cmd": "shots", "path": path})
        else:  # shotd / setpos / pileon
            d.poke(0x53e9ac, [4, 0, 0, 0]); d.rpc({"cmd": "sleep", "ms": 60})
            r = rpc({"cmd": "shotd", "path": path})
        if not alive():
            print(f"  !! CRASH on CAPTURE ({a.trigger}) resp={r}"); return
    print(f"=== [{a.trigger}] survived, NO CRASH ===", flush=True)

if __name__ == "__main__":
    main()
