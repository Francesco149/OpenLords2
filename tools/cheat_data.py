#!/usr/bin/env python3
"""Extract the LoTR II economic-cheat dataset from live retail, per realm per
turn, for the slopstudio "AI cheats" overlay.

Sits on the probe daemon (via tools/ai_pov.py's client). Emits, for every realm:
  · treasury                (strat +0x118)
  · income DISPLAYED        (Σ county projected +0xc0 — the tax screen's "People pay")
  · income COLLECTED        (Σ county accrued  +0xbc — what the treasury actually gets;
                             zeroed on an income-suppression event, so it can differ)
  · upkeep / wage bill      (strat +0xfc — human value/4 ALWAYS; AI value/{3,5,10,10})
  · AI free-gold GRANT      (added to treasury EVERY turn; [pers][diff][level] table, ≤1800)
  · army strength + troops  (Σ county counts +0x16c × weights {2,16,8,13,9,13,22})

    tools/cheat_data.py grant-table          # print the decoded free-gold cheat table
    tools/cheat_data.py snapshot             # one-shot per-realm economy table (now)
    tools/cheat_data.py extract --turns 20 [--difficulty 3] --out runs/probe/cheat.csv

The grant + upkeep are difficulty-gated: at easy the AI is nerfed (pays MORE upkeep,
tiny grant); the cheat only bites at hard. --difficulty pokes s_slot_0053f23c before
extracting so you can film the real cheat off this (difficulty-0) seed; the run is
consistent from turn 1 (the one-time setup grant in the start cash stays difficulty-0).
"""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_pov as A   # daemon client + strat()/region_owner_map()/reads()/poke_i32()

# --- economy layout (grounded 2026-07-09) ---
REC_BASE, REC_STRIDE = 0x53f9b0, 0x300
R_INC_COLLECTED, R_INC_PROJECTED, R_COUNTS = 0xbc, 0xc0, 0x16c
S_CASH, S_WAGE, S_LEVEL, S_PERS, S_ACTIVE, S_HUMAN = 0x118, 0xfc, 0x29, 0x07, 0x04, 0x05
WEIGHTS = [2, 16, 8, 13, 9, 13, 22]
DIFFICULTY = 0x53f23c
GRANT_DC1E0, GRANT_DC230 = 0x4dc1e0, 0x4dc230   # retail tables; [pers 0..4][diff 0..3] i32
AI_UPKEEP_DIV = {0: 3, 1: 5, 2: 10, 3: 10}
HUMAN_UPKEEP_DIV = 4
NPLAYERS = 5


def rec(k, off):
    return REC_BASE + k * REC_STRIDE + off


def grant_table():
    """Read the retail free-gold tables live → {(level_ge3, pers, diff): grant}."""
    out = {}
    for base, ge3 in ((GRANT_DC1E0, True), (GRANT_DC230, False)):
        vals = A.reads([(f"e{i}", base + i * 4, "i32") for i in range(20)])
        for pers in range(5):
            for diff in range(4):
                out[(ge3, pers, diff)] = vals[f"e{pers * 4 + diff}"]
    return out


def grant_for(gt, pers, diff, level):
    return gt.get((level >= 3, pers, diff), 0)


def realm_snapshot(gt, diff):
    """Per active realm: treasury, displayed/collected income, upkeep, grant, army."""
    # Only REAL map counties (records that appear in the minimap region grid,
    # regions 1..14). Records 15.. are armies / uninitialised phantoms whose +5
    # byte can read as a bogus owner (e.g. the 0x05/0x07 fill pattern) — skip them.
    valid = set(A.load_region_grid().keys())
    owners = {k: o for k, o in A.region_owner_map().items() if k in valid}
    rows = {}
    for s in range(1, NPLAYERS + 1):
        v = A.reads([("cash", A.strat(s, S_CASH), "i32"),
                     ("wage", A.strat(s, S_WAGE), "i32"),
                     ("lvl", A.strat(s, S_LEVEL), "u8"),
                     ("pers", A.strat(s, S_PERS), "u8"),
                     ("human", A.strat(s, S_HUMAN), "u8"),
                     ("active", A.strat(s, S_ACTIVE), "u8")])
        if not v["active"]:
            continue
        disp = coll = strength = counties = 0
        troops = [0] * 7
        for k, o in owners.items():
            if o != s:
                continue
            counties += 1
            f = A.reads([("d", rec(k, R_INC_PROJECTED), "i32"),
                         ("c", rec(k, R_INC_COLLECTED), "i32")]
                        + [(f"t{i}", rec(k, R_COUNTS + i * 2), "i16") for i in range(7)])
            disp += f["d"] or 0
            coll += f["c"] or 0
            for i in range(7):
                troops[i] += f[f"t{i}"] or 0
            strength += sum((f[f"t{i}"] or 0) * WEIGHTS[i] for i in range(7))
        is_human = v["human"] == 1
        rows[s] = {
            "slot": s, "who": "human" if is_human else "ai",
            "pers": v["pers"], "level": v["lvl"], "treasury": v["cash"],
            "income_displayed": disp, "income_collected": coll,
            "upkeep": v["wage"],
            "upkeep_div": HUMAN_UPKEEP_DIV if is_human else AI_UPKEEP_DIV.get(diff, 10),
            "grant": 0 if is_human else grant_for(gt, v["pers"], diff, v["lvl"]),
            "counties": counties, "army_strength": strength,
            "troops": "|".join(map(str, troops)),
        }
    return rows


COLS = ["turn", "year", "season", "slot", "who", "pers", "level", "treasury",
        "income_displayed", "income_collected", "upkeep", "upkeep_div", "grant",
        "counties", "army_strength", "troops"]


def _print_table(rows, diff):
    print(f"difficulty={diff}  (AI upkeep value/{AI_UPKEEP_DIV.get(diff,10)}, "
          f"human value/{HUMAN_UPKEEP_DIV}; grant per-turn shown)")
    print(f"{'slot':>4} {'who':>5} {'treas':>7} {'disp':>6} {'coll':>6} "
          f"{'upkeep':>6} {'grant':>6} {'army':>6} {'cnty':>4}")
    for s in sorted(rows):
        r = rows[s]
        print(f"{r['slot']:>4} {r['who']:>5} {r['treasury']:>7} "
              f"{r['income_displayed']:>6} {r['income_collected']:>6} "
              f"{r['upkeep']:>6} {r['grant']:>6} {r['army_strength']:>6} {r['counties']:>4}")


def cmd_grant_table(a):
    gt = grant_table()
    for ge3, label in ((False, "DC230  AI level < 3 (weaker AIs)"),
                       (True, "DC1E0  AI level >= 3 (stronger AIs)")):
        print(f"\n{label} — free crowns/turn [personality][difficulty]:")
        print(f"{'pers':>5} {'easy(0)':>8} {'(1)':>6} {'(2)':>6} {'hard(3)':>8}")
        for pers in range(1, 5):
            g = [gt[(ge3, pers, d)] for d in range(4)]
            print(f"{pers:>5} {g[0]:>8} {g[1]:>6} {g[2]:>6} {g[3]:>8}")
    print("\nmax = 1800/turn (personality 4, AI level >= 3, hardest). Humans get 0.")
    return 0


def cmd_snapshot(a):
    diff = A.read(DIFFICULTY, "i32")
    _print_table(realm_snapshot(grant_table(), diff), diff)
    return 0


def cmd_extract(a):
    if a.difficulty is not None:
        A.poke_i32(DIFFICULTY, a.difficulty)
        print(f"poked difficulty -> {A.read(DIFFICULTY,'i32')}")
    A.send({"cmd": "tomap"}); A.send({"cmd": "sleep", "ms": 300})
    gt = grant_table()
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    n_rows = 0
    with out.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=COLS)
        w.writeheader()
        for t in range(0, a.turns + 1):
            if t > 0:
                r = A.send({"cmd": "endturn", "timeout": 25.0})
                A.send({"cmd": "sleep", "ms": 300})
                if not r.get("advanced"):
                    print(f"turn {t}: endturn did not advance ({r.get('err','')})",
                          file=sys.stderr)
                    break
            diff = A.read(DIFFICULTY, "i32")
            year = A.read(0x553edc, "i32"); season = A.read(0x57c934, "i32")
            rows = realm_snapshot(gt, diff)
            for s in sorted(rows):
                rec_row = {"turn": t, "year": year, "season": season, **rows[s]}
                w.writerow({k: rec_row.get(k, "") for k in COLS})
                n_rows += 1
            if t % 5 == 0 or t == a.turns:
                cash = {s: rows[s]["treasury"] for s in sorted(rows)}
                print(f"  turn {t:>2} (yr {year} s{season}): treasuries {cash}")
    print(f"wrote {n_rows} rows → {out}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("grant-table", help="print the decoded free-gold cheat table").set_defaults(func=cmd_grant_table)
    sub.add_parser("snapshot", help="one-shot per-realm economy table").set_defaults(func=cmd_snapshot)
    p = sub.add_parser("extract", help="advance N turns → per-turn per-realm CSV")
    p.add_argument("--turns", type=int, default=20)
    p.add_argument("--difficulty", type=int, default=None, help="poke s_0053f23c first (0..3)")
    p.add_argument("--out", default="runs/probe/cheat_data.csv")
    p.set_defaults(func=cmd_extract)
    a = ap.parse_args()
    sys.exit(a.func(a))


if __name__ == "__main__":
    main()
