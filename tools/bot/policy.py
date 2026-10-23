"""policy.py — turn decisions for the autoplay bot, asked of Jev as typed questions.

Jev (TypeSafe's decision model, OpenRouter Decisions API — llm.decide) gets the game state as JSON and
typed questions, and returns a probability per option. The bot SAMPLES from those (temperature, seeded
RNG) instead of taking the argmax, so every game explores a different path through economy and war:
the goal is to exercise many situations for the scenario corpus, not to win. The Jev answers are logged
with every decision, so a game's choices can be audited and re-derived.
"""
import json
import random

import l2state
import llm

TAX_LEVELS = [0, 3, 5, 8, 10, 15, 20, 30]                         # percent
RATION_LEVELS = l2state.RATIONS                                   # none .. triple (index = retail value)
INDUSTRY_PCT = [5, 15, 25, 40, 60]                                # HUD farming ↔ industry split, % industry
ARMY_FRACTION = [0.0, 0.08, 0.15, 0.25, 0.40]                     # of a county's population, drafted now
TRADES = {"none": "no trade this season", "buy grain": "buy grain (food)", "buy cattle": "buy cattle (food, dairy)",
          "sell cattle": "sell some cattle for crowns", "sell grain": "sell some grain for crowns",
          "buy swords": "buy swords to arm troops", "buy bows": "buy bows", "buy maces": "buy maces",
          "buy armour": "buy armour", "buy wood": "buy timber", "buy stone": "buy stone", "buy iron": "buy iron"}
TRADE_QTY = [5, 20, 60]
GIFTS = [0, 50, 200]

BRIEF = ("Lords of the Realm II, medieval England from 1268. Each season (turn) a lord sets taxes and rations "
         "per county, splits peasants between farming (cattle, grain: food) and industry (wood, iron, stone, "
         "weapons), drafts armies from the population and marches them on neighbouring counties: neutral "
         "counties ('owner 0') are claimed by marching onto their town, but their own militia defends them; "
         "rival lords' counties must be conquered. Food keeps peasants alive and happy; taxes fill the "
         "treasury but cost happiness; drafting shrinks the workforce. Battles here are auto-resolved, and "
         "the defender has an advantage, so attack only with clearly stronger armies. Take varied, plausible decisions.")


def county_view(c):
    keep = ("id", "name", "owner", "population", "happiness", "health", "tax", "tax_income", "ration", "food",
            "industry_pct", "fields", "castle", "army_strength", "neighbors", "births", "deaths", "happiness_from")
    return {k: c[k] for k in keep if k in c}


def compact(st):
    """the snapshot reduced to what a decision needs"""
    me = st.get("me")
    realms = [{k: r.get(k) for k in ("id", "name", "human", "crowns", "counties", "population", "troops", "weapons",
                                     "iron", "stone", "wood", "relations") if k in r} for r in st.get("realms") or []]
    mine = [county_view(c) for c in st.get("counties") or [] if c.get("owner") == me]
    near = set()
    for c in mine:
        near |= set(c.get("neighbors") or [])
    others = [county_view(c) for c in st.get("counties") or [] if c.get("owner") != me]
    armies = [{k: a.get(k) for k in ("id", "owner", "county", "men", "troops", "strength", "moves_left", "dest")}
              for a in st.get("armies") or [] if a.get("kind", "army") in ("army", "rebels")]
    return {"brief": BRIEF, "date": st.get("date"), "me": me, "realms": realms, "my_counties": mine,
            "other_counties": others, "neighbour_ids": sorted(near), "armies": armies}


def sample(probs, rng, temperature):
    """probs: {key: p} → a key, sampled from p^(1/T)"""
    keys = sorted(probs, key=str)
    w = [max(1e-6, float(probs[k])) ** (1.0 / max(1e-3, temperature)) for k in keys]
    x = rng.random() * sum(w)
    for k, wk in zip(keys, w):
        x -= wk
        if x <= 0:
            return k
    return keys[-1]


def decide(st, rng, temperature=1.0, session_id=None):
    """→ (plan, raw answers). plan = {"counties": {id: {tax, ration, industry_pct, draft}}, "armies": {id: target county|None}}"""
    state = compact(st)
    me = st.get("me")
    plan = {"counties": {}, "armies": {}, "trade": None, "gift": 0}
    raw = {}
    for c in state["my_counties"]:
        cid, name = c["id"], c.get("name", c["id"])
        q = {
            "tax": {"type": "score", "instructions": f"What tax rate should {name} pay this season?",
                    "criteria": [f"{t}%" for t in TAX_LEVELS]},
            "ration": {"type": "score", "instructions": f"What food ration should {name}'s peasants get this season?",
                       "criteria": RATION_LEVELS},
            "industry": {"type": "score", "instructions": f"What share of {name}'s workforce should work in industry rather than farming?",
                         "criteria": [f"{p}% industry" for p in INDUSTRY_PCT]},
            "draft": {"type": "score", "instructions": f"How much of {name}'s population should be drafted into a new army now?",
                      "criteria": ["nobody", "a token force (8%)", "a small army (15%)", "a real army (25%)", "a large army (40%)"]},
        }
        castle = c.get("castle") or {}
        if not castle.get("building") and castle.get("level", 0) < 5:
            q["castle"] = {"type": "noul", "instructions": f"Should {name} start building a stronger castle "
                           f"(it has a {castle.get('name', 'none')}; building needs wood, stone and builders)?",
                           "criteria": {"true": "yes, start the next castle design now", "false": "no, not this season"}}
        a = llm.decide(dict(state, deciding_for=name), q, session_id=session_id)
        raw[cid] = a
        pick = {k: int(sample(a[k]["probabilities"], rng, temperature)) for k in q if k != "castle"}
        plan["counties"][cid] = {"tax": TAX_LEVELS[pick["tax"]], "ration": pick["ration"],
                                 "industry_pct": INDUSTRY_PCT[pick["industry"]], "draft": ARMY_FRACTION[pick["draft"]]}
        if "castle" in q:
            p_yes = float(a["castle"].get("noul", 0)) ** (1.0 / max(1e-3, temperature))
            p_no = (1.0 - float(a["castle"].get("noul", 0))) ** (1.0 / max(1e-3, temperature))
            if rng.random() * (p_yes + p_no) < p_yes:
                plan["counties"][cid]["castle"] = (castle.get("level", 0) or 0) + 1
    if state["my_counties"]:
        q = {"trade": {"type": "choice", "instructions": "Should the lord trade with the merchant this season, and what?", "criteria": TRADES},
             "qty": {"type": "score", "instructions": "How much should be traded?", "criteria": [f"{n} units" for n in TRADE_QTY]},
             "gift": {"type": "score", "instructions": "Should the lord send a gift of crowns to a rival lord to improve relations?",
                      "criteria": ["no gift", "a small gift (50 crowns)", "a generous gift (200 crowns)"]}}
        a = llm.decide(state, q, session_id=session_id)
        raw["realm"] = a
        t = sample(a["trade"]["probabilities"], rng, temperature)
        n = TRADE_QTY[int(sample(a["qty"]["probabilities"], rng, temperature))]
        plan["trade"] = None if t == "none" else {"good": t.split()[1], "qty": n if t.startswith("buy") else -n}
        plan["gift"] = GIFTS[int(sample(a["gift"]["probabilities"], rng, temperature))]
    targets = {str(cid): f"march on {c.get('name', cid)} (owner {c.get('owner')}, population {c.get('population')})"
               for c in state["other_counties"] for cid in [c["id"]] if cid in state["neighbour_ids"]}
    for army in [x for x in state["armies"] if x["owner"] == me]:
        opts = dict(targets, hold="stay and guard the realm this season")
        a = llm.decide(dict(state, deciding_for_army=army),
                       {"target": {"type": "choice", "instructions": f"Where should army {army['id']} ({army['men']} men, "
                                   f"strength {army['strength']}) march this season?", "criteria": opts}}, session_id=session_id)
        raw[f"army{army['id']}"] = a
        t = sample(a["target"]["probabilities"], rng, temperature)
        plan["armies"][army["id"]] = None if t == "hold" else int(t)
    return plan, raw
