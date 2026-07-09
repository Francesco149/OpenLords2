#!/usr/bin/env python3
"""
tools/micro_bot.py — a tactical MICRO bot for the LoTR2 interactive battlefield.

Drives a live, paused interactive battle (entered via enter_battle.py) by issuing
formation move/attack orders straight into the engine and stepping the sim in short
bursts.  The point (video beat #2, Path B): a *skilled* commander beats an even AI
army through concentration/micro — which is exactly why the game rigs the auto-calc
shortcut (the defender-only 1.6x..4x multiplier).  So we MEASURE: same starting
armies, our micro vs the AI's line-advance, and compare survivors to an auto-resolve.

STATUS (2026-07-09): world model, order primitives, and run/pause/start gates are all
RE'd + verified live. Orders queue CORRECTLY end to end: FUN_00479e90 (formation) and
FUN_0047a814 (per unit) set the unit walking and populate the move-animation queue
DAT_0057a108 with the right start/target pixels (verified: a clean straight-line walk
entry). BUT the queue never advances — on a COLD enter_battle boot the per-tick unit-sim
dispatch (state handlers via jump table PTR_FUN_004d9170 -> the animator FUN_00492c8b
@0x492c8b) does not run. So units don't move and an unmanaged 28-v-28 doesn't resolve.
Combat flags are all correct (quick-resolve 0x53e8fc==0, interactive 0x53e91c==0, role
0x5533d0==1, battle-active 0x568964==1) — NOT a quick-resolve misconfig; the master
battle-sim dispatch is gated OFF for the callq-stitched entry (likely no enemy battle-AI
controller / a "both sides ready" latch the organic Fight path sets). CAVEATS proven
this session: (1) capture "no frame" in combat = a stale/post-crash daemon -> reboot
fixes it; (2) drive within ONE process — across separate invocations the deploy screen
auto-starts and the fight auto-resolves. NEXT: breakpoint FUN_00492c8b during a REAL
organic battle to capture the live caller + gating flag, then poke it under this fixture.

Reverse-engineered driving model (docs/findings/combat-battlefield-input.md):
  * World model — unit record base 0x5544ac, stride 0x1b0:
        +0x00 type   +0x02 side(0/1)   +0x14c formation-id   +0x174 HP/strength
        +0x1a4 cur-col   +0x1a6 cur-row   +0x1a8/+0x1aa target-col/row
    Live units are indices 1..0x50 with type!=0.  Our side (attacker) = side 0
    (formations 1..6); enemy = side 1 (formations 7..12).
  * Order — FUN_00479e90(formation, col, row, target_unit_or_0, 0, 0) queues a
    move (target 0) or attack-move (target = an enemy unit index) for every unit
    in that formation.  This is exactly what the RMB handler (FUN_0043c634) calls.
  * Run/pause gate — the sim advances only while screen(0x4eac50)==0x29 AND
    pause(0x53f238)==0.  Set pause=-1 (or screen 0x28) to FREEZE.  Orders can be
    queued while frozen; they execute on the next unpaused burst.

Usage:
    python tools/micro_bot.py fight --strategy focus   # enter + fight to the end
    python tools/micro_bot.py fight --strategy charge   # baseline line-advance
    python tools/micro_bot.py world                     # dump the live world model
"""
from __future__ import annotations
import argparse, json, socket, time, sys
import enter_battle as eb

# ── VAs ──────────────────────────────────────────────────────────────────────
UT, STRIDE = 0x5544ac, 0x1b0     # battlefield unit table (indices 1..0x50)
O_TYPE, O_SIDE, O_FORM = 0x00, 0x02, 0x14c
O_COL, O_ROW, O_TCOL, O_TROW, O_HP = 0x1a4, 0x1a6, 0x1a8, 0x1aa, 0x174
V_SCREEN = 0x4eac50              # 0x28 deploy / 0x29 fight
V_PAUSE  = 0x53f238              # 0 = running, -1 = frozen
V_BFREDRAW = 0x53e9ac
F_ORDER  = 0x479e90             # FUN_00479e90(form, col, row, target, 0, 0)  formation order
F_MOVE1  = 0x47a814             # FUN_0047a814(unit, col, row)  per-unit move-to-hex
F_HGLASS = 0x43b9a1             # FUN_0043b9a1()  hourglass: toggle DAT_0053f238 (start/pause)
GRID_MAX = 0x50


class Daemon:
    def __init__(self, port=27099): self.port = port
    def rpc(self, req):
        s = socket.socket(); s.connect(("127.0.0.1", self.port))
        s.sendall((json.dumps(req) + "\n").encode()); buf = b""
        while b"\n" not in buf:
            c = s.recv(65536)
            if not c: break
            buf += c
        return json.loads(buf.split(b"\n")[0].decode())
    def reads(self, specs): return self.rpc({"cmd": "reads", "specs": specs}).get("vals", {})
    def rd(self, va, ty="i32"): return self.rpc({"cmd": "read", "va": va, "type": ty}).get("val")
    def poke(self, va, b): self.rpc({"cmd": "poke", "va": va, "bytes": b if isinstance(b, list) else [b]})
    def callq(self, va, *a): return self.rpc({"cmd": "callq", "va": va, "args": list(a)}).get("ret")
    def sleep(self, ms): self.rpc({"cmd": "sleep", "ms": ms})


class Battle:
    """Live view + control of the interactive battlefield."""
    def __init__(self, d: Daemon): self.d = d

    # — world model —
    def units(self):
        """Return [{idx, side, form, col, row, hp, typ}] for every live unit."""
        specs = []
        for i in range(1, GRID_MAX + 1):
            b = UT + i * STRIDE
            specs += [{"name": f"t{i}", "va": b + O_TYPE, "type": "u8"},
                      {"name": f"s{i}", "va": b + O_SIDE, "type": "u8"},
                      {"name": f"f{i}", "va": b + O_FORM, "type": "u8"},
                      {"name": f"c{i}", "va": b + O_COL, "type": "u8"},
                      {"name": f"r{i}", "va": b + O_ROW, "type": "u8"},
                      {"name": f"h{i}", "va": b + O_HP, "type": "u8"}]
        v = self.d.reads(specs)
        out = []
        for i in range(1, GRID_MAX + 1):
            if v.get(f"t{i}", 0) != 0:
                out.append(dict(idx=i, typ=v[f"t{i}"], side=v[f"s{i}"], form=v[f"f{i}"],
                                col=v[f"c{i}"], row=v[f"r{i}"], hp=v[f"h{i}"]))
        return out

    @staticmethod
    def summarize(units):
        sides = {}
        for u in units:
            s = sides.setdefault(u["side"], dict(units=0, hp=0, forms=set()))
            s["units"] += 1; s["hp"] += u["hp"]; s["forms"].add(u["form"])
        return sides

    # — control —
    def freeze(self): self.d.poke(V_PAUSE, [255, 255, 255, 255])
    def screen(self): return self.d.rd(V_SCREEN, "u8")
    def order(self, form, col, row, target=0):
        return self.d.callq(F_ORDER, form, col, row, target, 0, 0)
    def move_unit(self, unit, col, row):
        # per-unit move: sets action->walking + populates the move-anim queue (verified).
        return self.d.callq(F_MOVE1, unit, col, row)
    def start(self):
        # authentic deploy->fight start: inject an RMB on the field (sets 0x29 + pause 0).
        self.d.rpc({"cmd": "click", "x": 320, "y": 240, "button": "r"})

    def step(self, ms):
        """Run the sim for ~ms, then freeze again."""
        self.d.poke(V_SCREEN, [0x29])
        self.d.poke(V_PAUSE, [0, 0, 0, 0])
        self.d.sleep(ms)
        self.freeze()
        self.d.poke(V_BFREDRAW, [8, 0, 0, 0])

    def over(self, units=None):
        """Battle over if a side has no HP left, or we've left the combat screens."""
        sc = self.screen()
        if sc < 0x28 or sc > 0x2a:
            return True, f"left combat screens (screen=0x{sc:x})"
        u = units if units is not None else self.units()
        sides = self.summarize(u)
        for side in (0, 1):
            if sides.get(side, {}).get("hp", 0) == 0:
                return True, f"side {side} wiped"
        return False, ""


# ── commanders ───────────────────────────────────────────────────────────────
def my_forms(units, side=0):
    return sorted({u["form"] for u in units if u["side"] == side})

def centroid(units, side, form=None):
    pts = [(u["col"], u["row"]) for u in units
           if u["side"] == side and (form is None or u["form"] == form)]
    if not pts: return None
    return (round(sum(p[0] for p in pts) / len(pts)),
            round(sum(p[1] for p in pts) / len(pts)))

def enemy_forms_by_strength(units, enemy=1):
    """Enemy formations, weakest total-HP first (focus-fire target order)."""
    agg = {}
    for u in units:
        if u["side"] == enemy:
            a = agg.setdefault(u["form"], dict(hp=0, col=0, row=0, n=0, rep=u["idx"]))
            a["hp"] += u["hp"]; a["col"] += u["col"]; a["row"] += u["row"]; a["n"] += 1
    for f, a in agg.items():
        a["col"] = round(a["col"] / a["n"]); a["row"] = round(a["row"] / a["n"])
    return sorted(agg.items(), key=lambda kv: kv[1]["hp"])


def commander_charge(b: Battle, units):
    """Baseline: every one of our formations advances on the enemy centroid."""
    ec = centroid(units, 1)
    if not ec: return
    for f in my_forms(units, 0):
        b.order(f, ec[0], ec[1], 0)

def commander_focus(b: Battle, units):
    """Concentration: ALL our formations pile onto the single weakest enemy
    formation (attack-move onto a representative enemy unit)."""
    tgts = enemy_forms_by_strength(units, 1)
    if not tgts: return
    _, a = tgts[0]
    for f in my_forms(units, 0):
        b.order(f, a["col"], a["row"], a["rep"])

COMMANDERS = {"charge": commander_charge, "focus": commander_focus}


def fight(d: Daemon, strategy="focus", burst_ms=350, max_bursts=60, verbose=True,
          enter=None):
    b = Battle(d)
    if enter is not None:                     # (attacker, defender): enter fresh + freeze
        atk, dfn = enter
        eb.enter(eb.Daemon(d.port), atk, dfn)  # lands paused at 0x28
    b.freeze()
    cmd = COMMANDERS[strategy]
    log = []
    for k in range(max_bursts):
        units = b.units()
        done, why = b.over(units)
        sides = Battle.summarize(units)
        s0 = sides.get(0, {}); s1 = sides.get(1, {})
        rec = dict(burst=k, us_hp=s0.get("hp", 0), us_n=s0.get("units", 0),
                   en_hp=s1.get("hp", 0), en_n=s1.get("units", 0))
        log.append(rec)
        if verbose:
            print(f"burst {k:2d}: us hp={rec['us_hp']:4d} n={rec['us_n']:2d} | "
                  f"enemy hp={rec['en_hp']:4d} n={rec['en_n']:2d}")
        if done:
            if verbose: print(f"-> BATTLE OVER: {why}")
            break
        cmd(b, units)          # (re)issue orders based on the current world
        b.step(burst_ms)       # advance the sim
    b.freeze()
    final = Battle.summarize(b.units())
    return dict(strategy=strategy, log=log, final=_ser(final))


def _ser(sides):
    return {str(s): dict(units=v["units"], hp=v["hp"], forms=sorted(v["forms"]))
            for s, v in sides.items()}


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    pf = sub.add_parser("fight", help="run the micro commander to the end of the battle")
    pf.add_argument("--strategy", choices=list(COMMANDERS), default="focus")
    pf.add_argument("--burst-ms", type=int, default=350)
    pf.add_argument("--max-bursts", type=int, default=60)
    pf.add_argument("--enter", metavar="ATK:DEF", default=None,
                    help="enter a fresh battle first, e.g. 4:5 (attacker:defender)")
    sub.add_parser("world", help="dump the live world model")
    ap.add_argument("--port", type=int, default=27099)
    args = ap.parse_args()
    d = Daemon(args.port)
    if args.cmd == "world":
        b = Battle(d); us = b.units()
        for u in us:
            print(f"  #{u['idx']:2d} side{u['side']} form{u['form']:2d} type{u['typ']} "
                  f"@({u['col']:2d},{u['row']:2d}) hp={u['hp']}")
        print("summary:", _ser(Battle.summarize(us)))
    elif args.cmd == "fight":
        enter = tuple(int(x) for x in args.enter.split(":")) if args.enter else None
        res = fight(d, args.strategy, args.burst_ms, args.max_bursts, enter=enter)
        print("\nFINAL:", json.dumps(res["final"]))
