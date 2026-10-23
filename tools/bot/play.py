#!/usr/bin/env python3
"""play.py — the autoplay bot: Jev plays Lords of the Realm II through rhost to generate scenarios.

    python3 tools/bot/play.py --turns 20 --seed 7                    # one game from newgame-playnow (wine, local)
    python3 tools/bot/play.py --turns 40 --seed 3 --target win       # on the Windows box
    python3 tools/bot/play.py --turns 12 --seeds 1-8 -j 4            # a batch of games, 4 at a time
    python3 tools/bot/play.py --turns 30 --seed 72 --start custom:England --persona diplomat   # a custom game

Per turn: settle the map → read the state (l2state.snapshot) → ask Jev typed questions (policy.decide) →
sample a plan → apply it through the UI (per county: select via the minimap, tax, rations, industry
split, draft an army; per army: march on a target county's town) → End turn. A battle is auto-resolved or
fought on the battlefield (battle.py), as Jev chooses at the "take the field?" scroll.
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
from ui import UI, GameEnded, Stuck  # noqa: E402

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
        if p.get("castle"):
            done.append(("castle", cid, p["castle"], actions.build_castle(s, ui, p["castle"])))
        pop = next((c["population"] for c in st["counties"] if c["id"] == cid), 0)
        men = int(pop * p["draft"]) if pop * p["draft"] >= 20 else 0
        if men or p.get("hire"):
            a = actions.raise_army(s, ui, men, hire=bool(p.get("hire")))
            done.append(("army", cid, a and {"id": a["id"], "men": a["men"], "mercenaries": a.get("mercenaries")}))
    if plan.get("trade"):
        done.append(("trade", plan["trade"], actions.trade(s, ui, plan["trade"]["good"], plan["trade"]["qty"])))
    if plan.get("gift"):
        done.append(("gift", plan["gift"], actions.gift(s, ui, plan["gift"])))
    if plan.get("diplo"):
        d = plan["diplo"]
        done.append(("diplo", d, actions.diplomacy(s, ui, d["action"], d["realm"], crowns=d.get("crowns", 0), county=d.get("county"))))
    st = l2state.snapshot(s.read)
    for aid, order in plan["armies"].items():
        army = next((a for a in st["armies"] if a["id"] == aid and a["owner"] == st["me"]), None)
        if army is None or not order:
            continue
        if order["do"] == "leave":
            r = actions.leave_castle(s, ui, army, order["cell"])
        elif order["do"] == "disband":
            r = actions.disband(s, ui, army, order["cell"])
        else:
            if order["do"] == "combine":           # the other army may have moved this season
                other = next((a for a in l2state.snapshot(s.read)["armies"] if a["id"] == order.get("army")), None)
                if other is None:
                    continue
                order = dict(order, cell=other["pos"])
            r = actions.march(s, ui, army, order["cell"],
                              intent=order["do"] if order["do"] in ("besiege", "garrison", "combine") else None)
        done.append(("order", aid, order, r))
        ui.settle(max_frames=1200)
    return done


def summary(st):
    me = st["me"]
    r = next((x for x in st["realms"] if x["id"] == me), {})
    mine = [c for c in st["counties"] if c["owner"] == me]
    return {"season": st["date"].get("season_name"), "year": st["date"].get("year"), "crowns": r.get("crowns"),
            "owners": [c["owner"] for c in st["counties"]],
            "counties": len(mine), "population": sum(c.get("population", 0) for c in mine),
            "happiness": round(sum(c.get("happiness", 0) for c in mine) / max(1, len(mine))),
            "armies": sum(1 for a in st["armies"] if a["owner"] == me), "men": sum(a.get("men", 0) for a in st["armies"] if a["owner"] == me)}


def play(seed, turns, target="wine", temperature=1.0, name=None, log=print, start=START, persona=None, quick="off"):
    rng = random.Random(seed)
    persona = persona or rng.choice(sorted(policy.PERSONAS))
    name = name or f"bot-s{seed}-t{turns}"
    base, k = name, 1
    while os.path.exists(os.path.join(ROOT, "tests", "scenarios", name)):   # never overwrite a game: Jev's
        k += 1                                                               # answers vary run to run, so a
        name = f"{base}-{k}"                                                 # seed does not reproduce one
    t0 = time.time()
    decisions, rows = [], []
    stuck = ended = None
    asked = []

    def asker(kind, info):
        """retail asks (a popup with a choice, a confirm dialog, the siege board): Jev answers"""
        st_now = info.pop("state", None) or l2state.snapshot(s.read)
        try:
            ans, raw = policy.ask(kind, info, st_now, rng, temperature, session_id=name, persona=persona)
        except RuntimeError as e:
            log(f"[{name}] Jev failed on {kind} ({e}); default answer")
            ans, raw = None, None
        if ans is None:
            from ui import default_ask
            ans = default_ask(kind, info)
        asked.append({"kind": kind, "info": {k: v for k, v in info.items() if k != "state"}, "answer": ans, "jev": raw})
        return ans

    custom = start.startswith("custom:") if isinstance(start, str) else False
    with Session(name, target=target, trace=None if custom else start) as s:
        if custom:                           # a custom game set up in-session: the trace starts at boot
            meta = {}
            ui = UI(s, log=log, ask=asker)
            log(f"[{name}] custom game: {actions.custom_game(s, ui, start.split(':', 1)[1])}")
        else:
            meta = json.loads(next(l for l in open(start) if '"meta"' in l))["meta"]
            s.run(int(meta.get("frames", 1900)))
            ui = UI(s, log=log, ask=asker)
            ui.settle()
        orders = {}                          # army id → standing order (retail ends a march at the season's end)
        raw = None
        if quick == "jev":                   # v2: Jev chooses; v3 default "off": every battle offers the field
            try:
                quick, raw = policy.quick_battles(l2state.snapshot(s.read), rng, temperature, session_id=name, persona=persona)
            except RuntimeError as e:
                quick, raw = False, {"error": str(e)}
        else:
            quick = quick == "on"
        quick = actions.quick_battles(s, ui, quick) if quick else False
        decisions.append({"turn": -1, "frame": s.frame, "persona": persona, "quick_battles": quick, "jev": raw})
        log(f"[{name}] persona {persona}, quick battles {quick}")
        for t in range(turns):
            st = l2state.snapshot(s.read)
            sm = summary(st)
            rows.append(dict(turn=t, frame=s.frame, **sm))
            if sm["counties"] == 0:          # lost: End turn plays the defeat out; retail restarts the scenario
                log(f"[{name}] turn {t}: no counties left — playing out the defeat")
                try:
                    ui.end_turn()
                except GameEnded as e:
                    ended = str(e); log(f"[{name}] game over: {e}"); break
                except Stuck as e:
                    stuck = str(e); log(f"[{name}] defeat flow STUCK: {e}"); break
                continue
            live = {a["id"] for a in st["armies"] if a["owner"] == st["me"]}
            orders = {k: v for k, v in orders.items() if k in live}
            try:
                plan, raw = policy.decide(st, rng, temperature=temperature, session_id=name, persona=persona, orders=orders)
            except RuntimeError as e:
                log(f"[{name}] turn {t}: Jev failed ({e}); ending the turn without a plan")
                plan, raw = {"counties": {}, "armies": {}, "trade": None, "gift": 0}, {}
            for aid, order in plan["armies"].items():
                if order and order["do"] in ("claim", "attack", "besiege"):
                    orders[aid] = order
                else:
                    orders.pop(aid, None)
            ev0, ask0 = len(ui.events), len(asked)
            try:
                done = apply_plan(s, ui, st, plan, log)
                ui.end_turn()
            except GameEnded as e:
                ended = str(e)
                log(f"[{name}] turn {t}: game over: {e}")
                decisions.append({"turn": t, "frame": s.frame, "ended": ended})
                break
            except Stuck as e:
                stuck = str(e)
                s.shot(f"stuck_t{t}")
                log(f"[{name}] turn {t}: STUCK: {e}")
                decisions.append({"turn": t, "frame": s.frame, "stuck": stuck})
                break
            decisions.append({"turn": t, "frame": s.frame, "date": st["date"], "summary": sm, "plan": plan, "done": done,
                              "armies": [{k: a.get(k) for k in ("id", "owner", "pos", "county", "men", "strength", "dest",
                                                                "garrisoned", "besieging")} for a in st["armies"]],
                              "events": ui.events[ev0:], "asked": asked[ask0:], "jev": raw})
            log(f"[{name}] t{t:3d} {sm['season']:6s} {sm['year']} f{s.frame:6d} counties {sm['counties']} pop {sm['population']:5d} "
                f"happy {sm['happiness']:3d} crowns {sm['crowns']:6d} armies {sm['armies']}/{sm['men']} battles {ui.battles}+{ui.quick} "
                f"field {ui.fields} sieges {ui.sieges} "
                f"plan {json.dumps(plan)[:110]}")
    out = os.path.join(ROOT, "tests", "scenarios", name)
    os.makedirs(out, exist_ok=True)
    lines = [l for l in open(s.out_trace).read().splitlines() if '"meta"' not in l]
    hdr = [f"# {name}: autoplay (tools/bot/play.py), seed {seed}, {len(rows)} turns, persona {persona}, Jev {llm.DECISIONS_MODEL} T={temperature};",
           (f"# a custom game ({start.split(':', 1)[1]}, defaults) set up from the title." if custom else
            f"# starts from {os.path.basename(os.path.dirname(start))} (its events are the prefix).")
           + " Decisions + Jev answers: decisions.jsonl; per-turn state: turns.tsv." + (f" Ended STUCK: {stuck}" if stuck else "")
           + (f" Game over: {ended}" if ended else ""),
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
    kinds = {}
    for e in ui.events:
        k = e["what"] + (":" + e.get("text", "") if e["what"] == "confirm" else "")
        kinds[k] = kinds.get(k, 0) + 1
    log(f"[{name}] {len(rows)} turns in {time.time() - t0:.0f}s, battles {ui.battles} (field {ui.fields}) + quick {ui.quick}, "
        f"sieges {ui.sieges}, game overs {ui.game_overs}, "
        f"events {kinds}, unknown screens {sorted(hex(k) for k in ui.unknown)} "
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
    ap.add_argument("--start", default="newgame-playnow", help="scenario the game starts from (newgame-movies: movies on), "
                    "or custom:MAP (England, Scotland, France: a custom game with five nobles, set up from the title)")
    ap.add_argument("--prefix", default="bot3", help="scenario name prefix (bot3: autoplay v3 — battlefield, long games)")
    ap.add_argument("--persona", choices=sorted(policy.PERSONAS), help="default: sampled from the seed")
    ap.add_argument("--quick", choices=("off", "on", "jev"), default="off",
                    help="Options > 'Fight humans only?': off = every battle offers the field (v3), jev = Jev chooses (v2)")
    a = ap.parse_args()
    start = a.start if a.start.startswith("custom:") else os.path.join(ROOT, "tests", "scenarios", a.start, "trace.jsonl")
    tag = ("" if a.start == "newgame-playnow" else "m" if a.start == "newgame-movies" else
           "c" + a.start.split(":", 1)[1][:2].lower() if a.start.startswith("custom:") else "x")
    seeds = [a.seed]
    if a.seeds:
        lo, hi = (int(x) for x in a.seeds.split("-"))
        seeds = list(range(lo, hi + 1))
    name = lambda sd: f"{a.prefix}{tag}-s{sd}-t{a.turns}"
    if len(seeds) == 1:
        play(seeds[0], a.turns, a.target, a.temperature, name=name(seeds[0]), start=start, persona=a.persona, quick=a.quick)
        return
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for out in ex.map(lambda sd: play(sd, a.turns, a.target, a.temperature, name=name(sd), start=start, persona=a.persona,
                                          quick=a.quick), seeds):
            print("done", out)


if __name__ == "__main__":
    main()
