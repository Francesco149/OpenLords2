#!/usr/bin/env python3
"""
tools/kiting_demo.py — a MINIMAL synthetic battle that isolates the kiting mechanic
for the LotR2 video: a few archer stacks vs a few melee stacks, positioned close, the
melee driven forward while the archers fire from range and step back to keep distance.

Shows the two verified facts cleanly (BATTLE-MASTERY.md §1/§6):
  * archers (class 5) out-range every melee — range 15 hex vs melee's adjacency-only,
  * the enemy takes damage the whole approach while the archers take ZERO,
without the visual clutter of a full 28-v-28 fight.

Staging uses the scratch-rec class-word override (DAT_0052f21c + class*2 + rec*0x1a4)
between the muster (FUN_0042b7f7) and the deploy (FUN_004778a0). Small counts only —
big single-class musters overflow the 0x50 unit table and crash the deploy.

Usage (daemon must be up, retail on cutestation):
    nix develop -c python3 tools/kiting_demo.py [--enemy mace|peasant] [--shot DIR]

STATUS (2026-07-09): mechanic PROVEN live. Staging 16 archers vs 16 melee deploys
4 stacks/side (no crash); archers positioned at row 21, enemy at spawn row 8 (~13
apart, inside archer range 15); ordering archers to attack fires from range — enemy
HP drops (16→12) while archers stay at full 16 (ZERO return damage). vs peasants the
enemy routs in ~4 rounds (the rout-cascade finding — a fine "archers break them
untouched" clip); vs mace it lasts ~17 rounds (tougher morale). TWO footage caveats:
(1) the battle ends (screen→0x2e) if you poke/order the DEFENDER — leave the enemy at
spawn; (2) the combat-mode screenshot path grabs the wrong/stale (strategic) DDraw
surface, so --shot frames are currently unreliable for the battlefield — the numeric
proof is solid; polished footage needs the combat-capture surface fixed (separate
tooling gap) or owner-captured in a real game.
"""
from __future__ import annotations
import argparse, time, sys, os
sys.path.insert(0, os.path.dirname(__file__))
import enter_battle as eb
import micro_bot as mb

BASE, STRIDE, CLS = 0x52f0b0, 0x1a4, 0x52f21c
UT, UST = 0x5544ac, 0x1b0
GRID = 0x5440e5   # battlefield cell-occupancy grid: grid[(row*0x50+col)*8] = unit idx (all.c:55983)
CLASS = {"peasant": 0, "crossbow": 1, "mace": 2, "swordsman": 3, "pike": 4, "archer": 5, "knight": 6}
# INGAME GATES — the tactical per-tick action-dispatcher (FUN_004472ce, movement + combat
# commits) BAILS unless master_switch(0x553030)!=0 AND game_state_phase(0x52af94)==3. A bare
# menu-spawn deploys+renders units but leaves these unset → the sim NEVER ticks (units frozen,
# zero combat). Worse, driving live combat with these INCONSISTENT vs the real ingame state is
# what made retail's dispatcher deref unmapped pointers → the "capture crashes" (agent line 733).
# Setting them to a consistent ingame state (phase=3, master=2) both unfreezes combat AND makes
# capture-during-combat stable across every mode. (root-caused 2026-07-10)
V_GAME_PHASE  = 0x52af94   # DAT_0052af94 — game_state_phase (must be 3 ingame)
V_MASTER_SW   = 0x553030   # DAT_00553030 — master_switch / mp_active (0 = dispatcher bails)

def rec(i): return BASE + i * STRIDE
def clsword(r, c): return CLS + r * STRIDE + c * 2

def arm_ingame(d):
    """Assert the two ingame gates so the tactical sim ticks AND capture stays stable.
    Cheap; safe to re-call each run window. `d` is an eb.Daemon / mb.Daemon."""
    d.poke(V_GAME_PHASE, [3, 0, 0, 0])
    d.poke(V_MASTER_SW, [2, 0, 0, 0])

class Demo:
    def __init__(self, port=27099):
        self.d = eb.Daemon(port)
        self.b = mb.Battle(mb.Daemon(port))
    def rd(self, va, ty="i16"): return self.d.rpc({"cmd": "read", "va": va, "type": ty}).get("val")
    def pi16(self, va, v): self.d.poke(va, [v & 0xff, (v >> 8) & 0xff])
    def pi32(self, va, v): self.d.poke(va, [v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff])

    def stage(self, atk_comp, def_comp):
        d = self.d
        d.poke(rec(4) + 1, [1]); d.poke(rec(5) + 1, [0])
        d.pi32(eb.V_VIEWER, 1); d.pi32(eb.V_QUICKOPT, 1)
        d.pi32(eb.V_ATK, 4); d.pi32(eb.V_DEF, 5)
        d.callq(eb.F_CLEAR, 4); d.callq(eb.F_CLEAR, 5)
        d.pi32(eb.V_ROLE, 1)
        d.callq(eb.F_MUSTER, 4, 5)
        d.callq(eb.F_SUBINIT); d.callq(eb.F_ENTER)
        for r, comp in ((1, atk_comp), (2, def_comp)):
            for c in range(11): self.pi16(clsword(r, c), comp.get(c, 0))
            self.pi32(rec(r) + 0x168, sum(comp.values()))
        d.callq(eb.F_DEPLOY)
        # consistent ingame state: unfreezes the combat sim AND stops the crashes.
        d.pi32(V_GAME_PHASE, 3); d.pi32(V_MASTER_SW, 2)
        d.poke(eb.V_SCREEN, [0x28]); time.sleep(0.3)

    def units(self):
        specs = []
        for i in range(1, 0x51):
            base = UT + i * UST
            specs += [{"name": f"cl{i}", "va": base - 0x1a, "type": "u8"},
                      {"name": f"sd{i}", "va": base + 0x02, "type": "u8"},
                      {"name": f"c{i}", "va": base - 0xC, "type": "i16"},
                      {"name": f"r{i}", "va": base - 0xA, "type": "i16"},
                      {"name": f"fm{i}", "va": base + 0x14c, "type": "u8"},
                      {"name": f"h{i}", "va": base + 0x174, "type": "u8"}]
        v = self.d.rpc({"cmd": "reads", "specs": specs}).get("vals", {})
        out = []
        for i in range(1, 0x51):
            if v.get(f"h{i}", 0) > 0:
                out.append(dict(idx=i, cl=v[f"cl{i}"], side=v[f"sd{i}"], col=v[f"c{i}"],
                                row=v[f"r{i}"], form=v[f"fm{i}"], hp=v[f"h{i}"]))
        return out

    def setpos(self, idx, col, row, grid=False):
        base = UT + idx * UST
        if grid:
            # clear the OLD occupancy cell (grid[old]==idx → 0) before moving. Melee needs
            # this: the adjacency scanner reads grid @0x5440e5 indexed by the unit's cell-idx
            # field (base-0x10) = (row*0x50+col)*8. Plain setpos moves col/row but leaves the
            # cell-idx + grid at the deploy cell → the unit is melee-invisible / mis-placed.
            old_ci = self.rd(base - 0x10, "i32") or 0
            if 0 <= old_ci < 0x50 * 0x50 * 8 and (self.rd(GRID + old_ci, "u8") == idx):
                self.d.poke(GRID + old_ci, [0])
        for off in (-0xC, -0x8):  # cur-col, dest-col
            self.d.poke(base + off, [col & 0xff, (col >> 8) & 0xff])
        for off in (-0xA, -0x6):  # cur-row, dest-row
            self.d.poke(base + off, [row & 0xff, (row >> 8) & 0xff])
        self.pi16(base + 0x184, (col << 5) & 0xffff)
        self.pi16(base + 0x186, (row << 5) & 0xffff)
        if grid:
            ci = (row * 0x50 + col) * 8            # register the NEW cell in the occupancy grid
            self.pi32(base - 0x10, ci)             # cell-index field the scanner reads
            self.d.poke(GRID + ci, [idx & 0xff])   # grid[cell] = unit idx

    def run(self, ms):
        self.d.poke(eb.V_SCREEN, [0x29]); self.d.poke(0x53f238, [0, 0, 0, 0])
        self.d.rpc({"cmd": "sleep", "ms": ms})
    def freeze(self): self.d.poke(0x53f238, [255, 255, 255, 255])
    def screen(self): return self.rd(0x4eac50, "u8")
    def shot(self, path):
        self.d.poke(0x53e9ac, [8, 0, 0, 0])
        return self.d.rpc({"cmd": "shot", "path": path})


def nearest(u, others):
    return min(others, key=lambda o: max(abs(o["col"] - u["col"]), abs(o["row"] - u["row"])), default=None)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--enemy", default="mace", choices=list(CLASS))
    ap.add_argument("--n", type=int, default=16, help="troops per side (small!)")
    ap.add_argument("--shot", default=None, help="dir to drop battlefield PNGs")
    ap.add_argument("--port", type=int, default=27099)
    ap.add_argument("--rounds", type=int, default=16)
    a = ap.parse_args()
    g = Demo(a.port)
    print(f"staging {a.n} archers vs {a.n} {a.enemy}...")
    g.stage({CLASS["archer"]: a.n}, {CLASS[a.enemy]: a.n})
    us = g.units()
    mine = [u for u in us if u["side"] == 0]
    foe = [u for u in us if u["side"] == 1]
    print(f"deployed {len(mine)} archer stacks vs {len(foe)} {a.enemy} stacks")
    if not mine or not foe:
        print("deploy failed"); return
    g.b.start(); time.sleep(0.2)
    # firing line: archers at row 21 (enemy deployed at ~row 8 → ~13 apart, inside range 15).
    # Leave the enemy at its spawn — poking/ordering the DEFENDER trips the battle-end check.
    FIRE_ROW = 21
    for k, u in enumerate(mine): g.setpos(u["idx"], 49 + k, FIRE_ROW)
    g.d.poke(0x53e9ac, [8, 0, 0, 0])
    if a.shot: os.makedirs(a.shot, exist_ok=True)
    endflags = ["0x553fb0", "0x553070", "0x55302c"]
    for rnd in range(a.rounds):
        us = g.units()
        mine = [u for u in us if u["side"] == 0]; foe = [u for u in us if u["side"] == 1]
        myhp = sum(u["hp"] for u in mine); fhp = sum(u["hp"] for u in foe)
        minrow = min((u["row"] for u in mine), default=0)
        foemax = max((u["row"] for u in foe), default=0)
        sc = g.screen()
        print(f"round {rnd:2d}: archerHP={myhp:2d} enemyHP={fhp:2d} "
              f"archers@row{minrow} enemy@row{foemax} scr=0x{sc:x}")
        if a.shot and sc in (0x28, 0x29, 0x2a):
            g.shot(os.path.join(a.shot, f"kite_{rnd:02d}.png"))
        if not foe or fhp == 0: print("-> enemy destroyed; archers untouched"); break
        if not mine or sc < 0x28 or sc > 0x2a: print(f"-> ended (scr=0x{sc:x})"); break
        # fire: order archers to attack nearest foe (in range → fires in place)
        for f in sorted({u["form"] for u in mine}):
            tf = nearest({"col": 50, "row": FIRE_ROW}, foe)
            if tf: g.b.order(f, tf["col"], tf["row"], tf["idx"])
        g.run(800)
    g.freeze()
    us = g.units()
    print("FINAL: archers", sum(u["hp"] for u in us if u["side"] == 0),
          "| enemy", sum(u["hp"] for u in us if u["side"] == 1))

if __name__ == "__main__":
    main()
