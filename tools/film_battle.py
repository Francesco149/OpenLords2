#!/usr/bin/env python3
"""
tools/film_battle.py — film a played-out LoTR2 tactical battle for VIDEO beat #2
("the tactical AI is exploitably dumb → why the devs rig the auto-calc").

WORKING RECIPE (2026-07-10, produced the first clean 28v28 clip — no crash):
  • NATURAL 28v28 (enter_battle 4v5, 800 HP/side): the compelling, STABLE fight.
  • THE CRASH FIX (root-caused 2026-07-10): arm the two INGAME GATES after start() via
    kd.arm_ingame() — game_state_phase(0x52af94)=3 + master_switch(0x553030)=2. Without
    them the tactical sim never ticks (units frozen, zero combat); driving live combat
    with them INCONSISTENT is what made retail's per-tick action-dispatcher deref unmapped
    pointers → the old "attack-orders / mass-setpos / fast-rout crash frida". With the
    gates armed, attack-orders, mass-setpos, in-tick `shots`, capture-while-frozen and the
    8-stack pile-on ALL run crash-free (verified tools/trigger_test.py, every trigger).
  • Drive with attack-orders (target=enemy) OR a move-order charge (target=0) — both stable.
  • Capture DURING THE RUN (screen 0x29) with the synchronized, lock-free
    post-render read (`shotq`). For unattended filming, run the daemon without
    `--present`; that flag is an interactive preview seat which continuously
    locks the DDraw surface and is not needed by `shotq`. Running frames read the correct
    BRIGHT palette (frozen-mid-combat frames read a dark palette cycle-state — a game
    palette quirk both capture paths see, avoided by capturing while running). No
    freeze, no `record` streamer (that combo crashed frida).
  • FOLLOW-CAM on OUR army's centroid (side 0) via micro_bot.center_on — the camera
    tracks our army the whole march across the field (a rectangular tile grid; poke
    DAT_0055ce70/DAT_005678a0). Following the *global* centroid frames the empty
    midpoint while the armies are still apart, so follow side 0.

Result: our 28-stack army marches the whole field (frontRow 70→4), crosses the
bridge chokepoint, and clashes — 120 bright frames → mp4, no crash.

  For an assertion-backed staged ranged proof, prefer `film_unit_behavior.py`.
  `--staged` remains a looser exploratory capture mode.

Usage (daemon up, retail on cutestation):
    nix develop -c python3 tools/film_battle.py --out runs/probe/beat2/march.mp4
    nix develop -c python3 tools/film_battle.py --staged --enemy mace --n 16
"""
from __future__ import annotations
import argparse, json, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
import enter_battle as eb
import micro_bot as mb
import kiting_demo as kd

V_SCREEN = 0x4eac50


class Film:
    def __init__(self, port=27099):
        self.d = eb.Daemon(port)
        self.b = mb.Battle(mb.Daemon(port))
        self.kd = kd.Demo(port)
        self.last_shot = None

    def screen(self):
        return self.d.rd(V_SCREEN, "u8")

    def follow_side(self, us, side=0, ahead_col=3, ahead_row=-2):
        """Keep the camera on `side`'s live-unit centroid, biased slightly toward
        the enemy so the leading edge (and the clash) stays framed."""
        live = [u for u in us if u["side"] == side and u["hp"] > 0]
        if live:
            self.b.center_on(sum(u["col"] for u in live) / len(live) + ahead_col,
                             sum(u["row"] for u in live) / len(live) + ahead_row)

    def shot(self, path, settle_ms=70, mode="stable", combat_only=False):
        """Capture during the run after forcing a complete terrain redraw.

        ``stable`` copies the software framebuffer on the engine thread immediately
        after a completed render. It prevents half-painted reads without using the
        DDraw Lock that can destabilize live combat. ``direct`` retains the old
        asynchronous, potentially torn read for diagnostics. ``sync`` retains the
        in-tick DDraw-lock path for parity diagnostics, not production footage.

        Forces a FULL terrain redraw first: the combat terrain pass (FUN_004bcbdc) only
        redraws ALL cells while the redraw counter DAT_0053e9ac != 0 — otherwise it goes
        incremental. The game's own scroll (FUN_00431f59) sets it on every move; panning
        by raw camera-poke bypasses that, so stale terrain tiles persist for frames (the
        'morphing'). Poking it high + settling a few ticks makes each capture a clean full
        frame."""
        self.d.poke(0x53e9ac, [4, 0, 0, 0])          # force full terrain redraw
        self.d.rpc({"cmd": "sleep", "ms": settle_ms})  # let a few ticks fully repaint
        cmd = {"stable": "shotq", "sync": "shots", "direct": "shotd"}[mode]
        reply = self.d.rpc({"cmd": cmd, "path": path})
        self.last_shot = reply
        if not reply.get("ok"):
            return False
        if combat_only:
            captured_screen = (reply.get("frame_composite") or {}).get("screen")
            if captured_screen not in (0x28, 0x29, 0x2A):
                try:
                    os.remove(path)
                except FileNotFoundError:
                    pass
                return False
        return True


def hp(us, side):
    return sum(u["hp"] for u in us if u["side"] == side)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--staged", action="store_true", help="exploratory pure-archer kite; default = natural 28v28")
    ap.add_argument("--enemy", default="mace", choices=list(kd.CLASS), help="staged enemy class")
    ap.add_argument("--n", type=int, default=16, help="staged troops/side (small!)")
    ap.add_argument("--follow", default=None, choices=["us", "enemy"],
                    help="which army to keep framed (staged default: enemy under fire)")
    ap.add_argument("--enemy-advance", action="store_true",
                    help="staged: stage the enemy FAR and march it into the archer fire (long dynamic kite)")
    ap.add_argument("--follow-lead", type=float, default=0.35,
                    help="follow the LEADING edge: fraction of the followed army (nearest the enemy) to frame (0 = whole-army centroid). Keeps the first clash on screen.")
    ap.add_argument("--attacker", type=int, default=4)
    ap.add_argument("--defender", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=140)
    ap.add_argument("--sleep-ms", type=int, default=140, help="sim-advance per captured frame (smaller = denser/longer footage)")
    ap.add_argument("--capture-settle-ms", type=int, default=70,
                    help="full-redraw settling time before each synchronized capture")
    ap.add_argument("--recharge", type=int, default=6, help="re-issue the move-charge every N rounds")
    ap.add_argument("--cam-deadzone", type=float, default=5.0, help="tiles the action can drift before the cam eases (bigger = steadier)")
    ap.add_argument("--cam-lag", type=float, default=0.15, help="follow easing 0..1 (smaller = laggier/smoother)")
    ap.add_argument("--crop", default="", help="ffmpeg crop w:h:x:y; default '' = FULL game screen (raw footage stays uncropped — crop later in the edit)")
    ap.add_argument("--budget-s", type=float, default=150, help="wall-clock cap")
    ap.add_argument("--target-secs", type=float, default=None, help="stretch/compress the clip to ~N seconds (needs enough distinct frames — use a MARCH)")
    ap.add_argument("--enemy-n", type=int, default=0, help="staged: enemy troop count (0 = same as --n; bigger = a longer melt to film)")
    ap.add_argument("--dedup", action="store_true", help="drop near-duplicate frames (timelapse static fights)")
    ap.add_argument("--out", default="runs/probe/beat2/march.mp4")
    ap.add_argument("--fps", type=int, default=15)
    ap.add_argument("--capture-mode", choices=["stable", "direct", "sync"], default="stable",
                    help="stable = synchronized lock-free post-render read (default); direct = old async read; sync = diagnostic DDraw-lock read")
    ap.add_argument("--telemetry", default=None,
                    help="frame-aligned JSONL sidecar (default: <out stem>.telemetry.jsonl)")
    ap.add_argument("--no-telemetry", action="store_true",
                    help="disable the default frame-aligned unit/target/group-state sidecar")
    ap.add_argument("--port", type=int, default=27099)
    a = ap.parse_args()

    f = Film(a.port)
    fdir = a.out.rsplit(".", 1)[0] + "_frames"
    telemetry_path = None if a.no_telemetry else (a.telemetry or a.out.rsplit(".", 1)[0] + ".telemetry.jsonl")
    telemetry = open(telemetry_path, "w") if telemetry_path else None
    os.makedirs(fdir, exist_ok=True)
    for x in os.listdir(fdir):
        if x.endswith(".png"):
            os.remove(os.path.join(fdir, x))

    if a.staged:
        print(f"[staged/EXPERIMENTAL] {a.n} archers vs {a.n} {a.enemy}...")
        f.kd.stage({kd.CLASS["archer"]: a.n}, {kd.CLASS[a.enemy]: (a.enemy_n or a.n)})
        us = f.kd.units(); mine = [u for u in us if u["side"] == 0]
        if not mine:
            print("deploy failed (try smaller --n)"); return
        f.b.start(); time.sleep(0.4)
        kd.arm_ingame(f.d)                             # unfreeze combat + crash-proof capture
        # grid-register archers on a firing line; a MOTION kite wants the enemy far so it
        # marches DOWN into the fire (--enemy-advance) — long dynamic approach, archers untouched.
        fire_row = 58 if a.enemy_advance else 20
        for k, u in enumerate(mine):
            f.kd.setpos(u["idx"], 45 + k, fire_row, grid=a.enemy_advance)
        def order():
            us = f.b.units()
            foe = [u for u in us if u["side"] == 1]
            for frm in sorted({u["form"] for u in us if u["side"] == 0}):
                tf = kd.nearest({"col": 50, "row": fire_row}, foe)
                if tf:
                    f.b.order(frm, tf["col"], tf["row"], tf["idx"])   # archers fire (stable w/ gates)
            if a.enemy_advance and foe:                                # drive the enemy INTO the fire
                ac = (sum(u["col"] for u in mine) // len(mine), fire_row)
                for frm in sorted({u["form"] for u in foe}):
                    f.b.order(frm, ac[0], ac[1], 0)                    # move-order toward the archers
        follow_side = {"us": 0, "enemy": 1}.get(a.follow, 1)  # default: frame the enemy under fire
    else:
        eb.enter(f.d, a.attacker, a.defender); time.sleep(0.3)
        f.b.start(); time.sleep(0.4)                    # 0x29 → bright palette
        kd.arm_ingame(f.d)                              # unfreeze combat + crash-proof capture
        mb.commander_charge(f.b, f.b.units())           # move-orders toward the enemy centroid
        def order():
            mb.commander_charge(f.b, f.b.units())
        follow_side = {"us": 0, "enemy": 1}.get(a.follow, 0)

    n = ended = 0
    t0 = time.time()
    cam = mb.SmartCam(f.b, deadzone=a.cam_deadzone, lag=a.cam_lag)   # damped follow (no ±1-tile snap)
    for i in range(a.rounds):
        if time.time() - t0 > a.budget_s:
            print("wall budget reached"); break
        us = f.b.units(); sc = f.screen()
        s0, s1 = hp(us, 0), hp(us, 1)
        front = min((u["row"] for u in us if u["side"] == 0 and u["hp"] > 0), default=99)
        cam.follow(us, side=follow_side, ahead=(0.0, 0.0),    # ease toward the action each frame
                   lead=a.follow_lead, toward_side=1 - follow_side)   # follow the LEADING edge (first clash)
        shot_name = f"f{n:05d}.png"
        if f.shot(os.path.join(fdir, shot_name), settle_ms=a.capture_settle_ms,
                  mode=a.capture_mode, combat_only=True):
            if telemetry:
                snap = f.b.telemetry(us)
                snap.update(schema="openlords2.battle-telemetry.v2",
                            frame=n, round=i, elapsed_s=round(time.time() - t0, 4),
                            image=shot_name, capture_mode=a.capture_mode,
                            hp_by_side={str(side): hp(us, side) for side in (0, 1)})
                telemetry.write(json.dumps(snap, separators=(",", ":")) + "\n")
                telemetry.flush()
            n += 1
        if i % 5 == 0:
            print(f"i{i:3d}: us={s0:4d} en={s1:4d} frontRow={front:2d} scr=0x{sc:x} f={n}")
        if sc < 0x28 or sc > 0x2a:
            ended += 1
            if ended > 6:
                print(f"battle ended (scr=0x{sc:x})"); break
        elif i % a.recharge == a.recharge - 1:
            order()
        f.d.rpc({"cmd": "sleep", "ms": a.sleep_ms})

    if telemetry:
        telemetry.close()
        print(f"telemetry → {telemetry_path} ({n} frame-aligned records)")

    if n:
        # target-duration: pick the assembly fps so the clip runs ~target_secs (needs enough
        # distinct frames — a MARCH supplies them; a static fight will look choppy, use --dedup).
        fps = a.fps
        if a.target_secs:
            fps = max(1.0, n / a.target_secs)
        chain = []
        if a.crop:
            chain.append(f"crop={a.crop}")
        if a.dedup:                    # drop only NEAR-STATIC frames; frac low so a single unit
            # dying (a small changed region) still counts as motion and is KEPT — the default
            # frac=0.33 wrongly collapses a slow gradual melt into a few frames.
            chain += ["mpdecimate=hi=768:lo=320:frac=0.008", "setpts=N/(%g*TB)" % fps]
        vf = ["-vf", ",".join(chain)] if chain else []
        subprocess.run(
            ["ffmpeg", "-y", "-framerate", f"{fps:.3f}", "-i", os.path.join(fdir, "f%05d.png"),
             *vf, "-r", f"{max(fps,12):.3f}", "-c:v", "libx264", "-pix_fmt", "yuv420p",
             "-crf", "18", "-movflags", "+faststart", a.out],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"assembled {n} frames → {a.out} (~{n/fps:.1f}s @ {fps:.1f}fps"
              + (", dedup" if a.dedup else "") + (f", cropped {a.crop}" if a.crop else "") + ")")


if __name__ == "__main__":
    main()
