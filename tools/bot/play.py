#!/usr/bin/env python3
"""play.py — the autoplay bot: Jev plays Lords of the Realm II through rhost to generate scenarios.

    python3 tools/bot/play.py --turns 20 --seed 7                    # one game from newgame-playnow (wine, local)
    python3 tools/bot/play.py --turns 40 --seed 3 --target win       # on the Windows box
    python3 tools/bot/play.py --turns 12 --seeds 1-8 -j 4            # a batch of games, 4 at a time

Per turn: settle the map → read the state (l2state.snapshot) → ask Jev typed questions (policy.decide) →
sample a plan → apply it through the UI (per county: select via the minimap, tax, rations, industry
split, draft an army; per army: march on a target county's town) → End turn. Battles auto-resolve.
Every input lands in the session's trace, so a game is a scenario: tests/scenarios/bot-s<seed>-t<turns>/
{trace.jsonl, decisions.jsonl, turns.tsv} replays with no LLM and no network.
"""
import argparse
import concurrent.futures
import json
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "rhost"))
import actions  # noqa: E402
import l2state  # noqa: E402
import llm  # noqa: E402
import policy  # noqa: E402
from rhbot import Session  # noqa: E402
from ui import UI, Stuck  # noqa: E402

START = os.path.join(ROOT, "tests", "scenarios", "newgame-playnow", "trace.jsonl")
CB, CS = l2state.COUNTY_BASE, l2state.COUNTY_STRIDE


def select_county(s, ui, st, cid):
    if st.get("selected_county") == cid:
        return True
    c = next((x for x in st["counties"] if x["id"] == cid), None)
    if not c or not c.get("minimap_click"):
        return False
    s.click(*c["minimap_click"]); s.run(actions.GAP)
    ui.settle(max_frames=600)
    return s.u32(l2state.SEL_COUNTY) == cid


def set_industry(s, ui, cid, target, max_clicks=24):
    for _ in range(max_clicks):
        cur = s.read(CB + cid * CS + 0x08, 1)[0]
        cur = cur - 256 if cur > 127 else cur
        if abs(cur - target) <= 4:
            return cur
        ui.nudge_labor(1 if cur < target else -1)
    return cur


def apply_plan(s, ui, st, plan, log):
    done = []
    for cid, p in plan["counties"].items():
        if not select_county(s, ui, st, cid):
            done.append(("select-failed", cid)); continue
        tax = ui.set_tax(p["tax"], lambda: s.read(CB + cid * CS + 0xB9, 1)[0])
        rat = actions.set_rations(s, ui, p["ration"], lambda: s.read(CB + cid * CS + 0x15E, 1)[0])
        ind = set_industry(s, ui, cid, p["industry_pct"])
        done.append(("county", cid, {"tax": tax, "ration": rat, "industry_pct": ind}))
        pop = next((c["population"] for c in st["counties"] if c["id"] == cid), 0)
        if p["draft"] > 0 and pop * p["draft"] >= 20:
            a = actions.raise_army(s, ui, int(pop * p["draft"]))
            done.append(("army", cid, a and {"id": a["id"], "men": a["men"]}))
    if plan.get("trade"):
        done.append(("trade", plan["trade"], actions.trade(s, ui, plan["trade"]["good"], plan["trade"]["qty"])))
    if plan.get("gift"):
        done.append(("gift", plan["gift"], actions.gift(s, ui, plan["gift"])))
    st = l2state.snapshot(s.read)
    towns = {c["id"]: c.get("capital") for c in st["counties"]}
    for aid, target in plan["armies"].items():
        army = next((a for a in st["armies"] if a["id"] == aid and a["owner"] == st["me"]), None)
        if army is None or target is None or not towns.get(target):
            continue
        r = actions.march(s, ui, army, towns[target])
        done.append(("march", aid, target, r))
        ui.settle(max_frames=1200)
    return done


def summary(st):
    me = st["me"]
    r = next((x for x in st["realms"] if x["id"] == me), {})
    mine = [c for c in st["counties"] if c["owner"] == me]
    return {"season": st["date"].get("season_name"), "year": st["date"].get("year"), "crowns": r.get("crowns"),
            "counties": len(mine), "population": sum(c.get("population", 0) for c in mine),
            "happiness": round(sum(c.get("happiness", 0) for c in mine) / max(1, len(mine))),
            "armies": sum(1 for a in st["armies"] if a["owner"] == me), "men": sum(a.get("men", 0) for a in st["armies"] if a["owner"] == me)}


def play(seed, turns, target="wine", temperature=1.0, name=None, log=print, start=START):
    rng = random.Random(seed)
    name = name or f"bot-s{seed}-t{turns}"
    base, k = name, 1
    while os.path.exists(os.path.join(ROOT, "tests", "scenarios", name)):   # never overwrite a game: Jev's
        k += 1                                                               # answers vary run to run, so a
        name = f"{base}-{k}"                                                 # seed does not reproduce one
    t0 = time.time()
    decisions, rows = [], []
    stuck = None
    with Session(name, target=target, trace=start) as s:
        meta = json.loads(next(l for l in open(start) if '"meta"' in l))["meta"]
        s.run(int(meta.get("frames", 1900)))
        ui = UI(s, log=log)
        ui.settle()
        for t in range(turns):
            st = l2state.snapshot(s.read)
            sm = summary(st)
            rows.append(dict(turn=t, frame=s.frame, **sm))
            if sm["counties"] == 0:          # lost: End turn plays the defeat out; retail restarts the scenario
                log(f"[{name}] turn {t}: no counties left — playing out the defeat")
                try:
                    ui.end_turn()
                except Stuck as e:
                    stuck = str(e); log(f"[{name}] defeat flow STUCK: {e}"); break
                continue
            try:
                plan, raw = policy.decide(st, rng, temperature=temperature, session_id=name)
            except RuntimeError as e:
                log(f"[{name}] turn {t}: Jev failed ({e}); ending the turn without a plan")
                plan, raw = {"counties": {}, "armies": {}, "trade": None, "gift": 0}, {}
            try:
                done = apply_plan(s, ui, st, plan, log)
                ui.end_turn()
            except Stuck as e:
                stuck = str(e)
                s.shot(f"stuck_t{t}")
                log(f"[{name}] turn {t}: STUCK: {e}")
                decisions.append({"turn": t, "frame": s.frame, "stuck": stuck})
                break
            decisions.append({"turn": t, "frame": s.frame, "date": st["date"], "summary": sm, "plan": plan, "done": done, "jev": raw})
            log(f"[{name}] t{t:3d} {sm['season']:6s} {sm['year']} f{s.frame:6d} counties {sm['counties']} pop {sm['population']:5d} "
                f"happy {sm['happiness']:3d} crowns {sm['crowns']:6d} armies {sm['armies']}/{sm['men']} battles {ui.battles} "
                f"plan {json.dumps(plan)[:110]}")
    out = os.path.join(ROOT, "tests", "scenarios", name)
    os.makedirs(out, exist_ok=True)
    lines = [l for l in open(s.out_trace).read().splitlines() if '"meta"' not in l]
    hdr = [f"# {name}: autoplay (tools/bot/play.py), seed {seed}, {len(rows)} turns, Jev {llm.DECISIONS_MODEL} T={temperature};",
           f"# starts from {os.path.basename(os.path.dirname(start))} (its events are the prefix). Decisions + Jev answers: decisions.jsonl;"
           " per-turn state: turns.tsv." + (f" Ended STUCK: {stuck}" if stuck else ""),
           json.dumps({"meta": dict({"step_ms": 17, "vbase": 1000000, "frames": int(s.result["frame"])},
                                    **({"movies": True} if meta.get("movies") else {}))})]
    open(os.path.join(out, "trace.jsonl"), "w").write("\n".join(hdr + lines) + "\n")
    with open(os.path.join(out, "decisions.jsonl"), "w") as f:
        for d in decisions:
            f.write(json.dumps(d) + "\n")
    if rows:
        keys = list(rows[0])
        with open(os.path.join(out, "turns.tsv"), "w") as f:
            f.write("\t".join(keys) + "\n")
            for r in rows:
                f.write("\t".join(str(r.get(k, "")) for k in keys) + "\n")
    log(f"[{name}] {len(rows)} turns in {time.time() - t0:.0f}s, battles {ui.battles}, game overs {ui.game_overs}, unknown screens {sorted(hex(k) for k in ui.unknown)} "
        f"→ {os.path.relpath(out, ROOT)}  (Jev: {llm.USAGE})")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--turns", type=int, default=12)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--seeds", help="A-B: a batch of games")
    ap.add_argument("-j", "--jobs", type=int, default=4)
    ap.add_argument("--target", default="wine", choices=("wine", "win"))
    ap.add_argument("--temperature", type=float, default=1.0)
    ap.add_argument("--start", default="newgame-playnow", help="scenario the game starts from (newgame-movies: movies on)")
    a = ap.parse_args()
    start = os.path.join(ROOT, "tests", "scenarios", a.start, "trace.jsonl")
    tag = "" if a.start == "newgame-playnow" else "m" if a.start == "newgame-movies" else "x"
    seeds = [a.seed]
    if a.seeds:
        lo, hi = (int(x) for x in a.seeds.split("-"))
        seeds = list(range(lo, hi + 1))
    name = lambda sd: f"bot{tag}-s{sd}-t{a.turns}"
    if len(seeds) == 1:
        play(seeds[0], a.turns, a.target, a.temperature, name=name(seeds[0]), start=start)
        return
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for out in ex.map(lambda sd: play(sd, a.turns, a.target, a.temperature, name=name(sd), start=start), seeds):
            print("done", out)


if __name__ == "__main__":
    main()
