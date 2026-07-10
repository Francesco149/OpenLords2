#!/usr/bin/env python3
"""
tools/crash_repro.py — ISOLATE the capture+combat crash (2026-07-10).

Prior finding: CAPTURE + ATTACK-ORDER melee crashes frida after the first shotd+order,
while MOVE-order + capture survives 120 rounds and attack-order WITHOUT capture survives
20 rounds (crash_probe.py). This script drives a staged archers-vs-mace fight and pings
aliveness after EVERY sub-step (order / redraw-poke / palette-getentries / framebuffer
copy) so we learn the EXACT faulting op, not just "it crashed".

It exercises the capture in stages by toggling agent knobs (if present):
  --nopal   : ask the agent to skip the COM GetEntries (tests the palette-COM-race theory)
Usage: nix develop -c python3 tools/crash_repro.py [--enemy mace] [--n 20] [--stacks 5]
"""
import argparse, os, sys, time, socket, json
sys.path.insert(0, os.path.dirname(__file__))
import kiting_demo as kd

PORT = 27099

def rpc(req, timeout=20):
    s = socket.socket(); s.settimeout(timeout); s.connect(("127.0.0.1", PORT))
    s.sendall((json.dumps(req) + "\n").encode()); buf = b""
    while b"\n" not in buf:
        c = s.recv(65536)
        if not c: break
        buf += c
    if not buf: return None
    return json.loads(buf.split(b"\n")[0].decode())

def alive():
    try:
        r = rpc({"cmd": "ping"}, timeout=6)
        return bool(r and r.get("ok"))
    except Exception as e:
        return False

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--enemy", default="mace")
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--enemy-n", type=int, default=0, help="0 = same as --n")
    ap.add_argument("--stacks-fire-row", type=int, default=18)
    ap.add_argument("--rounds", type=int, default=15)
    ap.add_argument("--nopal", action="store_true", help="skip palette GetEntries in shotd")
    ap.add_argument("--no-capture", action="store_true", help="attack-order only, no shotd")
    a = ap.parse_args()

    print(f"alive at start: {alive()}", flush=True)
    g = kd.Demo(PORT); d = g.d; b = g.b
    print("staging...", flush=True)
    en = a.enemy_n or a.n
    g.stage({kd.CLASS["archer"]: a.n}, {kd.CLASS[a.enemy]: en})
    us = g.units()
    mine = [u for u in us if u["side"] == 0]; foe = [u for u in us if u["side"] == 1]
    print(f"deployed {len(mine)} archer-stacks vs {len(foe)} {a.enemy}; alive={alive()}", flush=True)
    if not mine:
        print("deploy failed"); return
    b.start(); time.sleep(0.4)
    # INGAME GATES: the tactical action-dispatcher (movement/combat commits) bails
    # unless master_switch(0x553030)!=0 AND game_state_phase(0x52af94)==3. A fresh
    # menu-spawn leaves these unset → units deploy+render but never act. (2026-07-10)
    d.poke(0x52af94, [3, 0, 0, 0]); d.poke(0x553030, [2, 0, 0, 0])
    for k, u in enumerate(mine):
        g.setpos(u["idx"], 51 + k, a.stacks_fire_row)
    print(f"firing line set (gates on); alive={alive()}", flush=True)

    fdir = "runs/probe/crashrepro"; os.makedirs(fdir, exist_ok=True)
    for rnd in range(a.rounds):
        us = g.units()
        mine = [u for u in us if u["side"] == 0]; foe = [u for u in us if u["side"] == 1]
        myhp = sum(u["hp"] for u in mine); fhp = sum(u["hp"] for u in foe)
        sc = d.rpc({"cmd": "read", "va": 0x4eac50, "type": "u8"}).get("val")
        print(f"r{rnd:2d}: archerHP={myhp} enemyHP={fhp} nfoe={len(foe)} scr=0x{sc:x}", flush=True)
        if not foe:
            print("enemy gone"); break

        # ISSUE ATTACK ORDER (callq → engine-thread queue)
        tf = kd.nearest({"col": 55, "row": a.stacks_fire_row}, foe)
        for fm in sorted({u["form"] for u in mine}):
            b.order(fm, tf["col"], tf["row"], tf["idx"])
            if not alive():
                print(f"  !! CRASH on ORDER form={fm}", flush=True); return
        print(f"  order ok; alive={alive()}", flush=True)

        # RUN the sim a bit so melee/fire is actually happening during capture
        d.poke(0x4eac50, [0x29]); d.poke(0x53f238, [0, 0, 0, 0])
        d.rpc({"cmd": "sleep", "ms": 230})
        if not alive():
            print(f"  !! CRASH on RUN", flush=True); return
        print(f"  run ok; alive={alive()}", flush=True)

        if a.no_capture:
            continue

        # CAPTURE — broken into steps via agent knobs to localize the fault.
        d.poke(0x53e9ac, [4, 0, 0, 0])   # force full terrain redraw
        if not alive():
            print(f"  !! CRASH on REDRAW-POKE", flush=True); return
        d.rpc({"cmd": "sleep", "ms": 60})
        req = {"cmd": "shotd", "path": f"{fdir}/r{rnd:03d}.png"}
        if a.nopal:
            req["nopal"] = True
        r = rpc(req, timeout=20)
        if not alive():
            print(f"  !! CRASH on SHOTD (nopal={a.nopal}) resp={r}", flush=True); return
        print(f"  shotd ok ({r.get('ok') if r else r}); alive={alive()}", flush=True)

    print("=== survived all rounds, no crash ===", flush=True)

if __name__ == "__main__":
    main()
