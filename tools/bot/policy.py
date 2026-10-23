"""policy.py — turn decisions for the autoplay bot, asked of Jev as typed questions.

Jev returns a probability per option; the bot SAMPLES from those (with a temperature) instead of taking
the argmax, so every game explores a different path through the economy, war and diplomacy — the point
is to exercise many situations for the scenario corpus, not to win. Sampling uses the game's own
seeded RNG stream (`random.Random(seed)`), so a (seed, Jev answers) pair reproduces the same choices;
the Jev answers themselves are logged with every decision.
"""
import json
import math
import random

import llm

TAX_LEVELS = [0, 1, 3, 5, 8, 10, 15, 20, 30]                    # percent
RATION_LEVELS = ["none", "quarter", "half", "normal", "double", "triple"]
LABOR_BIAS = ["all farming", "mostly farming", "balanced", "mostly industry", "all industry"]
ARMY_FRACTION = [0.0, 0.05, 0.10, 0.20, 0.35]                    # of a county's population

BRIEF = ("You are the lord of a realm in Lords of the Realm II (medieval England, 1268+). Each season (turn) you "
         "set taxes and rations per county, split peasants between farming (grain, cattle) and industry (wood, "
         "stone, iron, weapons), raise armies from the population, and march them on neighbouring counties. Food "
         "keeps the population alive and happy; taxes fill the treasury (crowns) but cost happiness; armies need "
         "weapons and food. Play to grow and conquer, but take varied, plausible decisions.")


def compact(st):
    """the snapshot reduced to what a decision needs (Jev reads JSON state)"""
    me = st.get("me")
    out = {"brief": BRIEF, "date": st.get("date"), "me": me}
    realms = st.get("realms") or []
    out["realms"] = [{k: r.get(k) for k in ("id", "name", "crowns", "counties", "human", "active", "rank") if k in r} for r in realms]
    counties = st.get("counties") or []
    mine = [c for c in counties if c.get("owner") == me]
    out["my_counties"] = mine
    near = set()
    for c in mine:
        near |= set(c.get("neighbors") or [])
    out["neighbours"] = [{k: c.get(k) for k in ("id", "name", "owner", "population", "army_strength", "castle") if k in c}
                         for c in counties if c.get("id") in near and c.get("owner") != me]
    out["my_armies"] = [a for a in (st.get("armies") or []) if a.get("owner") == me]
    return out


def sample(probs, rng, temperature):
    """probs: {key: p} → a key, sampled from p^(1/T)"""
    keys = list(probs)
    w = [max(1e-6, float(probs[k])) ** (1.0 / max(1e-3, temperature)) for k in keys]
    tot = sum(w)
    x = rng.random() * tot
    for k, wk in zip(keys, w):
        x -= wk
        if x <= 0:
            return k
    return keys[-1]


def decide(st, rng, temperature=1.0, session_id=None):
    """→ (plan, raw answers). plan = {"counties": {id: {...}}, "attack": target|None}"""
    state = compact(st)
    me = st.get("me")
    plan = {"counties": {}, "attack": None}
    raw = {}
    for c in state["my_counties"]:
        cid = c.get("id")
        q = {
            "tax": {"type": "score", "instructions": f"Tax rate for county {c.get('name', cid)} this season?",
                    "criteria": [f"{t}%" for t in TAX_LEVELS]},
            "rations": {"type": "score", "instructions": f"Ration level for county {c.get('name', cid)} this season?",
                        "criteria": RATION_LEVELS},
            "labor": {"type": "score", "instructions": f"How should {c.get('name', cid)}'s peasants be split between farming and industry?",
                      "criteria": LABOR_BIAS},
            "army": {"type": "score", "instructions": f"How much of {c.get('name', cid)}'s population should be drafted into an army now?",
                     "criteria": ["none", "a token force (5%)", "a small army (10%)", "a real army (20%)", "a large army (35%)"]},
        }
        a = llm.decide(dict(state, deciding_for=c), q, session_id=session_id)
        raw[cid] = a
        pick = {k: int(sample(a[k]["probabilities"], rng, temperature)) for k in q}
        plan["counties"][cid] = {"tax": TAX_LEVELS[pick["tax"]], "rations": RATION_LEVELS[pick["rations"]],
                                 "labor": pick["labor"], "army_fraction": ARMY_FRACTION[pick["army"]]}
    targets = {str(n["id"]): f"attack {n.get('name', n['id'])} (owner {n.get('owner')}, population {n.get('population')})"
               for n in state["neighbours"]}
    if targets and state["my_armies"]:
        targets["none"] = "do not attack anyone this season"
        a = llm.decide(state, {"attack": {"type": "choice", "instructions": "Which neighbouring county should your armies attack this season?",
                                          "criteria": targets}}, session_id=session_id)
        raw["attack"] = a
        t = sample(a["attack"]["probabilities"], rng, temperature)
        plan["attack"] = None if t == "none" else int(t)
    return plan, raw


if __name__ == "__main__":
    demo = {"date": {"season": "Winter", "year": 1268}, "me": 1,
            "realms": [{"id": 1, "name": "You", "crowns": 5000, "human": True}, {"id": 2, "name": "Yellow", "crowns": 4000}],
            "counties": [{"id": 3, "name": "Raw Sienna", "owner": 1, "population": 435, "happiness": 72, "tax": 0,
                          "rations": "normal", "grain": 101, "cattle": 10, "neighbors": [4]},
                         {"id": 4, "name": "Neighbour", "owner": 2, "population": 400}],
            "armies": []}
    plan, raw = decide(demo, random.Random(1))
    print(json.dumps(plan, indent=1))
    print(llm.USAGE)
