#!/usr/bin/env python3
"""Economy extract with a PASSIVE-CONTROL human that can't be conquered — so the
game runs long enough to film the AI's free-gold SNOWBALL (level 3 → 1800/turn),
which a normal passive human dies before reaching (~turn 22-33, stochastic).

Each turn we re-assert the human's home garrison + a flat treasury (the human
does NOTHING else — 0% tax, a stable observation platform), then snapshot every
realm's economy (cheat_data) and end the turn (with a couple of retries in case
an AI think-slice times out).  The human line stays flat/零 in every chart; the
keepalive only stops an AI from ending the run early.

    tools/film_survive.py --difficulty 3 --turns 45 --out runs/probe/film_hard_data.csv
"""
from __future__ import annotations
import argparse, csv, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_pov as A
import cheat_data as C

HUMAN_SLOT = 1
HUMAN_REGION = 13                      # this seed's human capital (brief)
KNIGHT_IDX = 5                         # troop col 5, weight 13
GARRISON = 6000                        # unconquerable vs any AI army + defender mult


def keepalive():
    # big home garrison (survive) + flat treasury (afford upkeep, stay passive)
    va_kn = C.rec(HUMAN_REGION, C.R_COUNTS + KNIGHT_IDX * 2)
    A.send({"cmd": "poke", "va": va_kn, "bytes": [GARRISON & 0xFF, (GARRISON >> 8) & 0xFF]})
    A.poke_i32(A.strat(HUMAN_SLOT, C.S_CASH), 1000)


def endturn_retry(tries=3):
    for _ in range(tries):
        r = A.send({"cmd": "tomap"}); A.sleep_ms(150)
        r = A.send({"cmd": "endturn", "timeout": 30.0}); A.sleep_ms(300)
        if r.get("advanced"):
            return r
        time.sleep(0.5)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--difficulty", type=int, default=3)
    ap.add_argument("--turns", type=int, default=45)
    ap.add_argument("--out", default="runs/probe/film_survive.csv")
    a = ap.parse_args()

    A.poke_i32(C.DIFFICULTY, a.difficulty)
    print(f"difficulty -> {A.read(C.DIFFICULTY,'i32')}")
    A.send({"cmd": "tomap"}); A.sleep_ms(300)
    gt = C.grant_table()
    out = Path(a.out); out.parent.mkdir(parents=True, exist_ok=True)
    n = 0
    with out.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=C.COLS); w.writeheader()
        for t in range(0, a.turns + 1):
            keepalive()
            if t > 0:
                r = endturn_retry()
                if not r.get("advanced"):
                    print(f"turn {t}: stalled after retries -> stop"); break
                keepalive()  # re-assert after the roll too
            diff = A.read(C.DIFFICULTY, "i32")
            year = A.read(0x553edc, "i32"); season = A.read(0x57c934, "i32")
            rows = C.realm_snapshot(gt, diff)
            for s in sorted(rows):
                w.writerow({k: {"turn": t, "year": year, "season": season, **rows[s]}.get(k, "")
                            for k in C.COLS})
                n += 1
            if t % 5 == 0:
                lvl = {s: rows[s]["level"] for s in sorted(rows) if rows[s]["who"] == "ai"}
                gr = {s: rows[s]["grant"] for s in sorted(rows) if rows[s]["who"] == "ai"}
                print(f"  turn {t:>2} (yr {year} s{season}): lvls {lvl} grants {gr}")
    print(f"wrote {n} rows -> {out}")


if __name__ == "__main__":
    main()
