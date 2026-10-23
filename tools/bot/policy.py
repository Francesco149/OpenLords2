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

# v2 narrows the economy to what keeps a county alive (v1's full ranges starved most games before turn 10;
# its scenarios keep those paths): the point of v2 is the war
TAX_LEVELS = [0, 3, 5, 8, 10]                                     # percent
RATION_LEVELS = l2state.RATIONS                                   # none .. triple (index = retail value)
RATION_CHOICES = [2, 3, 4]                                        # half, normal, double (double: only with grain
                                                                  # in store — v2's double rations ate the herd: the
                                                                  # extra is meat, slaughtered cattle, and the dairy
                                                                  # that fed the county went with them)
INDUSTRY_PCT = [5, 15, 25, 40]                                    # HUD farming ↔ industry split, % industry
ARMY_FRACTION = [0.0, 0.05, 0.10, 0.15, 0.25]                     # of a county's population, drafted now
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
         "rival lords' counties are taken by marching on their town when no garrison holds their castle, and a "
         "garrisoned castle must be besieged: march onto the castle itself, build siege engines (each takes "
         "seasons; none = assault at once) and assault it. Mercenary bands sometimes offer their service in a "
         "county: they cost crowns, not peasants, and are far stronger than drafted peasants (strength per man: "
         "peasant 2, pikeman 9, swordsman/archer 13, knight 22). An army in a castle defends it. "
         "Food keeps peasants alive and happy; taxes fill the treasury but cost happiness; drafting more than a "
         "tenth of a county at once starves it of workers and makes it unhappy, and unhappy counties revolt. "
         "A battle can be auto-calculated (which favours the defender) or fought on the battlefield under the "
         "lord's own command. Take varied, plausible decisions.")

# a persona per game (sampled from the seed) steers Jev toward different, still plausible campaigns
PERSONAS = {
    "warlord": "You are a warlord: hire every mercenary band you can afford, raise a strong army early and make "
               "war on the rival lord: besiege his castle and take his counties.",
    "opportunist": "You are an opportunist: claim the weak neutral counties first, then strike the rival lord "
                   "where he is weakest - an ungarrisoned county, or his castle once your army clearly outnumbers it.",
    "castellan": "You are a castellan: keep an army garrisoned in your castle while the economy grows, hire "
                 "mercenaries when offered, then sally out and besiege the rival lord's castle in the later seasons.",
    "diplomat": "You are a diplomat: court the other lords with gifts, keep your peasants fed and happy, grow "
                "strong but not overbearing, and welcome an alliance with a lord who offers one.",
    "steward": "You are a careful steward who means to rule for many years: keep every county fed (never let the "
               "herd be eaten), taxes low, a small army garrisoned in the castle, and claim a neighbouring county "
               "only when your army clearly outnumbers its defenders. Fight your battles in person.",
}
# a persona's appetite for war: Jev's probability of a claim / attack / siege order is multiplied by this before
# sampling, and an attack is offered from this fraction of the estimated defence up (v2: 1/2 for everyone — its
# armies rarely marched; v3 wants the battles)
AGGRESSION = {"warlord": (3.0, 1 / 3), "opportunist": (2.0, 1 / 3), "castellan": (1.0, 1 / 2), "diplomat": (0.7, 1 / 2),
              "steward": (1.5, 1 / 2)}
STICKY = 0.85                                       # chance a standing claim / attack / siege order is kept unasked
# per-persona economy: the steward's choices are narrowed to what keeps a county alive for 40 seasons (bot v3)
ECONOMY = {"steward": {"tax": [0, 3, 5], "ration": [3], "industry": [15, 25], "draft": [0.0, 0.05, 0.10], "castle": False}}
DRAFT_TEXT = {0.0: "nobody", 0.05: "a token force (5%)", 0.10: "a small army (10%)", 0.15: "a real army (15%)",
              0.25: "a large army (25%)"}
HELP_FROM = {1: 750, 2: 800, 3: 900, 4: 1000}       # an ally's troop strength needed to help, by personality (0x4d8a88)
ALLY_ACTS = {"none": "nothing this season", "gift": "send our ally a gift of 100 crowns",
             "compliment": "send our ally a compliment", "help": "plead with our ally for help in one of our counties",
             "attack": "ask our ally to attack a rival lord's county"}
BATTLE_TACTICS = {"attack": "take the field and lead an all-out attack on the nearest enemy",
                  "retreat": "take the field and attack, but retreat if the battle goes badly",
                  "autocalc": "take the field for a skirmish, then let the rest be calculated",
                  "mopup": "take the field and order every unit to charge",
                  "auto": "do not take the field: let the battle be calculated"}


def county_view(c):
    keep = ("id", "name", "owner", "population", "happiness", "health", "tax", "tax_income", "ration", "food",
            "industry_pct", "fields", "castle", "army_strength", "neighbors", "births", "deaths", "happiness_from",
            "merc_offer")
    return {k: c[k] for k in keep if k in c}


def compact(st, persona=None):
    """the snapshot reduced to what a decision needs"""
    me = st.get("me")
    realms = [{k: r.get(k) for k in ("id", "name", "human", "crowns", "counties", "population", "troops", "weapons",
                                     "iron", "stone", "wood", "relations") if k in r} for r in st.get("realms") or []]
    mine = [county_view(c) for c in st.get("counties") or [] if c.get("owner") == me]
    near = set()
    for c in mine:
        near |= set(c.get("neighbors") or [])
    others = [{k: c[k] for k in ("id", "name", "owner", "population", "castle", "neighbors", "army_strength") if k in c}
              for c in st.get("counties") or [] if c.get("owner") != me]
    armies = [{k: a.get(k) for k in ("id", "owner", "county", "men", "troops", "strength", "moves_left", "dest",
                                     "garrisoned", "besieging", "mercenaries") if a.get(k) not in (None, False, 0)}
              for a in st.get("armies") or [] if a.get("kind", "army") in ("army", "rebels")]
    mercs = [m for m in st.get("mercenaries") or [] if not m.get("hired")]
    return {"brief": BRIEF + (" " + PERSONAS[persona] if persona in PERSONAS else ""), "date": st.get("date"),
            "me": me, "realms": realms, "my_counties": mine, "other_counties": others,
            "neighbour_ids": sorted(near), "armies": armies, "mercenary_bands": mercs}


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


def decide(st, rng, temperature=1.0, session_id=None, persona=None, orders=None):
    """→ (plan, raw answers). plan = {"counties": {id: {tax, ration, industry_pct, draft, hire?}},
    "armies": {id: order|None}}; an order = {"do": claim|attack|besiege|garrison|leave, "county", "cell"}.
    `orders`: the standing order per army (re-issued each season: retail stops a march at the season's end)."""
    state = compact(st, persona)
    orders = orders or {}
    me = st.get("me")
    plan = {"counties": {}, "armies": {}, "trade": None, "gift": 0}
    raw = {}
    eco = ECONOMY.get(persona, {})
    for c in state["my_counties"]:
        cid, name = c["id"], c.get("name", c["id"])
        food = c.get("food") or {}
        pop = c.get("population", 0)
        keep = sustainable_ration(c)
        rations = [r for r in RATION_CHOICES if r <= max(2, keep) and (r < 4 or food.get("grain", 0) >= pop)]
        if persona == "steward":                        # exactly what the herd's dairy feeds: no slaughter
            rations = [keep]
        draft = eco.get("draft", ARMY_FRACTION)
        if persona == "steward" and sum(x.get("men", 0) for x in st["armies"] if x["owner"] == me) >= 0.08 * pop:
            draft = [0.0]                               # a small standing army only (v3 s201: 409 men mutinied unpaid)
        opts = {"tax": eco.get("tax", TAX_LEVELS), "ration": rations or [2],
                "industry": eco.get("industry", INDUSTRY_PCT), "draft": draft}
        q = {
            "tax": {"type": "score", "instructions": f"What tax rate should {name} pay this season?",
                    "criteria": [f"{t}%" for t in opts["tax"]]},
            "ration": {"type": "score", "instructions": f"What food ration should {name}'s peasants get this season?",
                       "criteria": [RATION_LEVELS[r] for r in opts["ration"]]},
            "industry": {"type": "score", "instructions": f"What share of {name}'s workforce should work in industry rather than farming?",
                         "criteria": [f"{p}% industry" for p in opts["industry"]]},
            "draft": {"type": "score", "instructions": f"How much of {name}'s population should be drafted into a new army now?",
                      "criteria": [DRAFT_TEXT.get(f, f"{f:.0%}") for f in opts["draft"]]},
        }
        for k in [k for k, v in opts.items() if len(v) == 1]:
            del q[k]                                    # nothing to choose
        castle = c.get("castle") or {}
        if eco.get("castle", True) and not castle.get("building") and castle.get("level", 0) < 5:
            q["castle"] = {"type": "noul", "instructions": f"Should {name} start building a stronger castle "
                           f"(it has a {castle.get('name', 'none')}; building needs wood, stone and builders)?",
                           "criteria": {"true": "yes, start the next castle design now", "false": "no, not this season"}}
        band = next((m for m in state["mercenary_bands"] if m["band"] == c.get("merc_offer")), None)
        if band:
            q["hire"] = {"type": "noul", "instructions": f"{band['men']} {band['nation']} {band['troops']} offer their "
                         f"service in {name}: {band['cost']} crowns to hire, about {band['wages_approx']} crowns wages a season. Hire them?",
                         "criteria": {"true": "yes, hire the mercenaries", "false": "no"}}
        a = llm.decide(dict(state, deciding_for=name), q, session_id=session_id)
        raw[cid] = a
        pick = {k: int(sample(a[k]["probabilities"], rng, temperature)) if k in q else 0 for k in opts}
        plan["counties"][cid] = {"tax": opts["tax"][pick["tax"]], "ration": opts["ration"][pick["ration"]],
                                 "industry_pct": opts["industry"][pick["industry"]], "draft": opts["draft"][pick["draft"]]}
        if "castle" in q:
            p_yes = float(a["castle"].get("noul", 0)) ** (1.0 / max(1e-3, temperature))
            p_no = (1.0 - float(a["castle"].get("noul", 0))) ** (1.0 / max(1e-3, temperature))
            if rng.random() * (p_yes + p_no) < p_yes:
                plan["counties"][cid]["castle"] = (castle.get("level", 0) or 0) + 1
        crowns = next((r.get("crowns", 0) for r in state["realms"] if r["id"] == me), 0)
        if band and crowns >= band["cost"] and noul(a["hire"], rng, temperature):
            plan["counties"][cid]["hire"] = band["band"]
    ally = next((r for r in st["realms"] if r["id"] == me), {}).get("ally") or 0
    ally_r = next((r for r in st["realms"] if r["id"] == ally and ally != me), None)
    if state["my_counties"] and ally_r:
        att = (ally_r.get("relations") or {}).get(me, {}).get("attitude")
        rivals = [c for c in st["counties"] if c["owner"] not in (0, me, ally)]
        crit = dict(ALLY_ACTS)
        # an ally answers a plea only when its army is strong enough (+0x14 from 750/800/900/1000 by personality)
        # and its attitude toward us is 10 or more — anything else is refused (183/186) and sours it (FUN_004a3551 /
        # FUN_004a38db); a lord who asks anyway learns nothing, so the pleas are offered only then
        ready = (ally_r.get("troop_strength") or 0) >= HELP_FROM.get(ally_r.get("personality"), 1000) and (att or 0) >= 10
        if not rivals or not ready:
            crit.pop("attack")
        if not ready:
            crit.pop("help")
        q = {"ally": {"type": "choice", "instructions": f"{ally_r.get('name')} is our ally (army strength "
                      f"{ally_r.get('troop_strength')}, attitude toward us {att} on a -30..30 scale). What should we "
                      f"send to or ask of our ally this season?", "criteria": crit}}
        a = llm.decide(state, q, session_id=session_id)
        raw["ally"] = a
        act = sample(a["ally"]["probabilities"], rng, temperature)
        if act != "none":
            mine = [c for c in st["counties"] if c["owner"] == me]
            threat = {c["id"]: sum(1 for x in st["armies"] if x["owner"] not in (me, ally) and x["county"] == c["id"]) for c in mine}
            county = (max(mine, key=lambda c: threat[c["id"]])["id"] if act == "help" else
                      rng.choice(rivals)["id"] if act == "attack" else None)
            plan["diplo"] = {"action": act, "realm": ally, "county": county, "crowns": 100 if act == "gift" else 0}
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
    for army in [x for x in st["armies"] if x["owner"] == me and x.get("kind", "army") == "army"]:
        if army.get("besieging"):
            continue                                   # stays at the siege (retail runs the assault)
        appetite, floor = AGGRESSION.get(persona, (1.0, 0.5))
        standing = orders.get(army["id"])
        opts = army_options(st, army, standing, floor=floor)
        if len(opts) == 1:                              # only "hold": nothing to ask
            plan["armies"][army["id"]] = None
            continue
        key = standing and f"{standing['do']}{standing['county']}"
        if key in opts and rng.random() < STICKY:       # a march under way goes on (v2 re-asked Jev every season
            plan["armies"][army["id"]] = opts[key][1]   # and its attacks rarely landed: retail ends a march at
            raw[f"army{army['id']}"] = {"standing": key}  # every season's end)
            continue
        av = {k: a for k, a in army.items() if k in ("id", "men", "troops", "strength", "county", "garrisoned", "mercenaries")}
        a = llm.decide(dict(state, deciding_for_army=av),
                       {"target": {"type": "choice", "instructions": f"What should army {army['id']} ({army['men']} men, "
                                   f"strength {army.get('strength')}, in county {army['county']}) do this season?",
                                   "criteria": {k: v[0] for k, v in opts.items()}}}, session_id=session_id)
        raw[f"army{army['id']}"] = a
        # a march under way is resampled colder: retail ends every march at the season's end, so a siege
        # five seasons away needs Jev's choice to hold five times in a row
        probs = {k: p * (appetite if k.startswith(("claim", "attack", "besiege")) else 1.0)
                 for k, p in a["target"]["probabilities"].items()}
        t = sample(probs, rng, temperature * (0.35 if orders.get(army["id"]) else 1.0))
        plan["armies"][army["id"]] = opts.get(t, (None, None))[1]
    return plan, raw


DAIRY_PER_COW = 5                                   # fed by dairy = 5 x cattle [live: 505/101, 380/76, 325/65]
RATION_SHARE = {1: 0.25, 2: 0.5, 3: 1.0, 4: 2.0, 5: 3.0}   # food a head per ration level (normal = 1) [live: fed = pop]


def sustainable_ration(c):
    """the highest ration (1 quarter .. 3 normal) the herd's dairy covers: retail makes up any shortfall with meat,
    i.e. slaughtered cattle, and with the meat slider at 100 % a growing county eats its herd and starves (v2's
    bot2cen-s72: 101 cattle → 0 in eight seasons; v3 s201 at normal rations: 65 → 4 once the population passed the
    dairy)"""
    pop = c.get("population", 0) or 0
    food = c.get("food") or {}
    supply = DAIRY_PER_COW * (food.get("cattle", 0) or 0)
    best = 1
    for r in (2, 3):
        if pop * RATION_SHARE[r] <= supply:
            best = r
    return best


def noul(ans, rng, temperature):
    """sample a noul answer {"noul": p_yes}"""
    p = min(1.0, max(0.0, float(ans.get("noul", 0))))
    w_yes, w_no = max(1e-6, p) ** (1.0 / max(1e-3, temperature)), max(1e-6, 1 - p) ** (1.0 / max(1e-3, temperature))
    return rng.random() * (w_yes + w_no) < w_yes


MUSTER_PCT = {0: 25, 1: 40, 2: 50, 3: 60}           # contest_region FUN_004a6c68: militia % of a neutral county by difficulty


def militia_strength(pop, difficulty=0):
    """the militia a neutral county musters (FUN_004a50ae mode 2: archers / pikemen / macemen by size band)"""
    n = pop * MUSTER_PCT.get(difficulty, 25) // 100
    if pop < 40:
        return 0
    bands = [(480, (150, 100, 50)), (360, (100, 70, 0)), (240, (80, 40, 0)), (120, (60, 0, 0)), (0, (0, 0, 0))]
    arch, pike, mace = next(b for lo, b in bands if n >= lo)
    return arch * 13 + pike * 9 + mace * 8 + max(0, n - arch - pike - mace) * 2


def army_options(st, army, standing=None, floor=0.5):
    """the moves open to one army → {key: (description, order)}. Hopeless attacks (under `floor` of the defence,
    which also gets the defender's bonus) are not offered: v1's armies marched to their deaths"""
    me = st["me"]
    owner = {c["id"]: c["owner"] for c in st["counties"]}
    names = {r["id"]: r.get("name") for r in st["realms"]}
    mine = [c for c in st["counties"] if c["owner"] == me]
    near = {army["county"]} | {n for c in mine for n in c.get("neighbors") or []}
    near |= set(next((c.get("neighbors") or [] for c in st["counties"] if c["id"] == army["county"]), []))
    by_id = {a["id"]: a for a in st["armies"]}
    strength = army.get("strength") or 0
    opts = {"hold": ("stay where it is this season", None)}
    if army.get("garrisoned"):
        home = next((c for c in st["counties"] if c["castle"].get("garrison") == army["id"]), None)
        if home:
            opts["leave"] = (f"leave the castle of {home['name']} (to march next season)",
                             {"do": "leave", "county": home["id"], "cell": home["castle"]["cell"]})
            opts["disband"] = (f"disband the garrison of {home['name']}: its men go back to the fields (no more wages, "
                               f"no defence)", {"do": "disband", "county": home["id"], "cell": home["castle"]["cell"]})
        return opts
    for o in st["armies"]:                     # combine with another of our field armies nearby
        if (o["owner"] == me and o["id"] != army["id"] and o.get("kind", "army") == "army" and not o.get("garrisoned")
                and not o.get("besieging") and o["county"] in near and not (o.get("mercenaries") and army.get("mercenaries"))):
            opts[f"join{o['id']}"] = (f"join army {o['id']} ({o.get('men')} men, strength {o.get('strength')}) into one stronger army",
                                      {"do": "combine", "county": o["county"], "cell": o["pos"], "army": o["id"]})
    for c in st["counties"]:
        cid, o = c["id"], c["owner"]
        if o == me:
            if c["id"] == army["county"] and c["castle"].get("level") and not c["castle"].get("garrison"):
                opts[f"garrison{cid}"] = (f"garrison our castle in {c['name']} to defend it",
                                          {"do": "garrison", "county": cid, "cell": c["castle"]["cell"]})
            continue
        if cid not in near:
            continue
        if o == 0:
            est = militia_strength(c.get("population") or 0, st.get("difficulty") or 0)
            if strength >= est * floor:
                opts[f"claim{cid}"] = (f"march on the neutral county {c['name']} to claim it (its militia: strength "
                                       f"about {est})", {"do": "claim", "county": cid, "cell": c["capital"]})
            continue
        lord = names.get(o, f"lord {o}")
        g = c["castle"].get("garrison")
        if c["castle"].get("level") and g:
            gs = (by_id.get(g) or {}).get("strength") or 0
            if strength >= gs * floor:
                opts[f"besiege{cid}"] = (f"besiege {lord}'s {c['castle'].get('name')} in {c['name']} (garrison "
                                         f"{(by_id.get(g) or {}).get('men', '?')} men, strength {gs})",
                                         {"do": "besiege", "county": cid, "cell": c["castle"]["cell"]})
        else:
            est = sum(a.get("strength") or 0 for a in st["armies"] if a["owner"] == o and a["county"] == cid) or \
                (c.get("population") or 0) * 4 // 5
            if strength >= est * floor:
                opts[f"attack{cid}"] = (f"attack {lord}'s county {c['name']} at its town (no castle garrison; "
                                        f"defence about {est})", {"do": "attack", "county": cid, "cell": c["capital"]})
    if standing and standing.get("do") in ("claim", "attack", "besiege") and owner.get(standing["county"]) != me:
        key = f"{standing['do']}{standing['county']}"
        if key in opts:
            opts[key] = (opts[key][0] + " (continue the march already under way)", opts[key][1])
    return opts


def ask(kind, info, st, rng, temperature=1.0, session_id=None, persona=None):
    """retail asks the player (ui.UI.ask): popups with a choice, confirm dialogs, siege preparations"""
    state = compact(st, persona)
    if kind == "popup":
        who = next((r.get("name") for r in st["realms"] if r["id"] == info.get("from")), "a rival lord")
        if info.get("type") == 17:                 # the castle cannot barrack the whole army
            text = f"{info.get('title')} {info.get('text')}"
        else:                                      # 11 an alliance offer, 10 an ally asks for gold
            text = f"{who} writes ({info.get('title')}): {info.get('text')}"
        q = {"yes": {"type": "noul", "instructions": f"{text} Answer yes?",
                     "criteria": {"true": "yes", "false": "no"}}}
        a = llm.decide(state, q, session_id=session_id)
        return noul(a["yes"], rng, temperature), a
    if kind == "confirm":
        q = {"yes": {"type": "noul", "instructions": f"Retail asks: \"{info.get('text')}?\" Answer yes?",
                     "criteria": {"true": "yes", "false": "no"}}}
        a = llm.decide(state, q, session_id=session_id)
        return noul(a["yes"], rng, temperature), a
    if kind == "siege":
        c = next((x for x in st["counties"] if x["id"] == info.get("county")), {})
        q = {"engines": {"type": "score", "instructions": f"Your army besieges the {c.get('castle', {}).get('name')} of "
                         f"{c.get('name')}. How many siege engines should it build before the assault (each takes "
                         f"time; the defenders may sally or be reinforced meanwhile)?",
                         "criteria": ["none: assault at once", "one engine", "two engines", "three engines"]},
             "kind": {"type": "choice", "instructions": "Which kind of engine, if any?",
                      "criteria": {"catapults": "catapults (attack from a distance)", "rams": "battering rams (break the gate)",
                                   "towers": "siege towers (go over the wall)"}},
             "proceed": {"type": "noul", "instructions": "Go on with the siege (rather than lift it and withdraw)?",
                         "criteria": {"true": "go on with the siege", "false": "lift the siege"}}}
        a = llm.decide(state, q, session_id=session_id)
        n = int(sample(a["engines"]["probabilities"], rng, temperature))
        k = sample(a["kind"]["probabilities"], rng, temperature)
        return {"catapults": 0, "towers": 0, "rams": 0, k: n, "proceed": noul(a["proceed"], rng, temperature)}, a
    if kind == "battle":
        o, th = info.get("ours") or {}, info.get("theirs") or {}
        what = ("the assault on a castle" if info.get("siege") and info.get("we_attack") else "the defence of our castle"
                if info.get("siege") else "our attack" if info.get("we_attack") else "an enemy attack on us")
        q = {"field": {"type": "choice", "instructions": f"A battle is to be fought ({what}): our {o.get('men')} men "
                       f"(strength {o.get('strength')}) against {th.get('men')} men (strength {th.get('strength')}). "
                       f"Take the field and command the troops in person, or let the battle be calculated?",
                       "criteria": BATTLE_TACTICS}}
        a = llm.decide(state, q, session_id=session_id)
        return sample(a["field"]["probabilities"], rng, temperature), a
    return None, None


def quick_battles(st, rng, temperature=1.0, session_id=None, persona=None):
    """the game-start Options choice: battles against the computer as quick animations ("Fight humans only?")"""
    q = {"quick": {"type": "noul", "instructions": "Game options: should battles against computer lords be "
                   "resolved as a quick animation instead of offering to take the field each time?",
                   "criteria": {"true": "yes, quick battles", "false": "no, keep asking"}}}
    a = llm.decide(compact(st, persona), q, session_id=session_id)
    return noul(a["quick"], rng, temperature), a
