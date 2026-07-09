#!/usr/bin/env python3
"""OpenLords2 MCP server — let an LLM PLAY (and audit) retail Lords of the Realm II.

A stdlib-only Model-Context-Protocol server (stdio / newline-delimited JSON-RPC 2.0,
no SDK dependency) that sits on the running probe daemon (tools/probe_daemon.py) and
exposes the live retail game as MCP tools:

  observe   game_state · list_realms · county_report · cheat_report · screenshot · scout_realm
  act       set_tax · set_rations · end_turn
  advanced  read_memory · poke_memory · call_function   (engine-thread callq)

The daemon holds ONE persistent game; the MCP is a thin, LLM-friendly protocol skin over
it (state reads = the board, set_*/end_turn = moves).  This is the "play against an LLM"
substrate the roadmap calls for — wrap + release so people can play vs a frontier model
instead of the (cheating) built-in AI.

Boot the game first:  tools/probe_daemon.py --segtrace tools/traces/new_game_to_map.jsonl \
    --rng-seed --present &   then register THIS as an MCP server (stdio) and restart the client.

Self-test without an MCP client:
    tools/openlords2_mcp.py --selftest      # runs a scripted JSON-RPC session over the daemon
"""
from __future__ import annotations

import json
import socket
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTRL = ROOT / "runs" / "probe" / "daemon.json"

# economy layout (grounded 2026-07-09; mirrors cheat_data.py / ai_pov.py) --------
STRAT_BASE, STRAT_STRIDE = 0x57bf00, 0x160
S_ACTIVE, S_HUMAN, S_PERS, S_RANK, S_CASH, S_WAGE = 0x04, 0x05, 0x07, 0x2b, 0x118, 0xfc
REC_BASE, REC_STRIDE = 0x53f9b0, 0x300
R_OWNER, R_POP, R_TAX, R_INC_COLL, R_INC_PROJ, R_COUNTS = 0x05, 0x24, 0xb9, 0xbc, 0xc0, 0x16c
DIFFICULTY = 0x53f23c
GRANT_DC1E0, GRANT_DC230 = 0x4dc1e0, 0x4dc230
AI_UPKEEP_DIV = {0: 3, 1: 5, 2: 10, 3: 10}
HUMAN_UPKEEP_DIV = 4
WEIGHTS = [2, 16, 8, 13, 9, 13, 22]
TROOP_NAMES = ["peasants", "archers", "pikemen", "crossbows", "swordsmen", "knights", "cavalry"]
PERS_NAME = {0: "—", 1: "cautious", 2: "balanced", 3: "defensive", 4: "aggressive"}
SEASON = {1: "Spring", 2: "Summer", 3: "Autumn", 4: "Winter"}
YEAR_VA, SEASON_VA, CURTERR_VA, SCREEN_VA = 0x553edc, 0x57c934, 0x57c93c, 0x4eac50


def strat(slot, off): return STRAT_BASE + slot * STRAT_STRIDE + off
def rec(k, off): return REC_BASE + k * REC_STRIDE + off


# ── daemon client ────────────────────────────────────────────────────────────
class DaemonError(RuntimeError):
    pass


def _port():
    if CTRL.exists():
        return int(json.loads(CTRL.read_text())["port"])
    raise DaemonError("no daemon.json — is the probe daemon running?")


def dsend(req, timeout=30.0):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(("127.0.0.1", _port()))
        s.sendall((json.dumps(req) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
        s.close()
    except (OSError, DaemonError) as e:
        raise DaemonError(f"daemon unreachable: {e}") from e
    return json.loads(buf.decode()) if buf else {"ok": False, "err": "no reply"}


def dread(va, t="i32"):
    return dsend({"cmd": "read", "va": va, "type": t}).get("val")


def dreads(specs):
    r = dsend({"cmd": "reads", "specs": [{"name": n, "va": v, "type": t} for n, v, t in specs]})
    return r.get("vals", {})


# ── game-domain reads ────────────────────────────────────────────────────────
def _region_grid():
    cache = ROOT / "runs" / "probe" / "region_grid.json"
    if cache.exists():
        return {int(k) for k in json.loads(cache.read_text())}
    return set(range(1, 15))  # fallback: real counties are regions 1..14


def _grant_table():
    out = {}
    for base, ge3 in ((GRANT_DC1E0, True), (GRANT_DC230, False)):
        vals = dreads([(f"e{i}", base + i * 4, "i32") for i in range(20)])
        for pers in range(5):
            for diff in range(4):
                out[(ge3, pers, diff)] = vals.get(f"e{pers * 4 + diff}", 0)
    return out


def _owners(valid):
    specs = [(f"r{k}", rec(k, R_OWNER), "u8") for k in range(1, 0x60)]
    v = dreads(specs)
    return {k: v.get(f"r{k}") for k in range(1, 0x60) if v.get(f"r{k}") and k in valid}


def realms():
    """Per-active-realm economy snapshot (the board state)."""
    diff = dread(DIFFICULTY, "i32") or 0
    valid = _region_grid()
    owners = _owners(valid)
    gt = _grant_table()
    out = []
    for s in range(1, 6):
        v = dreads([("cash", strat(s, S_CASH), "i32"), ("wage", strat(s, S_WAGE), "i32"),
                    ("lvl", strat(s, 0x29), "u8"), ("pers", strat(s, S_PERS), "u8"),
                    ("human", strat(s, S_HUMAN), "u8"), ("active", strat(s, S_ACTIVE), "u8"),
                    ("rank", strat(s, S_RANK), "u8")])
        if not v.get("active"):
            continue
        counties = strength = 0
        troops = [0] * 7
        for k, o in owners.items():
            if o != s:
                continue
            counties += 1
            f = dreads([(f"t{i}", rec(k, R_COUNTS + i * 2), "i16") for i in range(7)])
            for i in range(7):
                troops[i] += f.get(f"t{i}") or 0
            strength += sum((f.get(f"t{i}") or 0) * WEIGHTS[i] for i in range(7))
        human = v["human"] == 1
        lvl = v["lvl"] or 1
        out.append({
            "slot": s, "controller": "you" if human else "AI",
            "personality": PERS_NAME.get(v["pers"], v["pers"]) if not human else "you",
            "treasury": v["cash"], "upkeep_bill": v["wage"],
            "upkeep_divisor": HUMAN_UPKEEP_DIV if human else AI_UPKEEP_DIV.get(diff, 10),
            "free_grant_per_turn": 0 if human else gt.get((lvl >= 3, v["pers"], diff), 0),
            "ai_level": lvl, "counties": counties, "army_strength": strength,
            "troops": {TROOP_NAMES[i]: troops[i] for i in range(7) if troops[i]},
            "rank": v["rank"], "alive": True,
        })
    return diff, out


def game_state():
    v = dreads([("season", SEASON_VA, "i32"), ("year", YEAR_VA, "i32"),
                ("screen", SCREEN_VA, "i32"), ("curterr", CURTERR_VA, "i32"),
                ("diff", DIFFICULTY, "i32")])
    return {
        "season": SEASON.get(v.get("season"), v.get("season")),
        "year": v.get("year"), "screen": v.get("screen"),
        "on_map": v.get("screen") == 0, "selected_county": v.get("curterr"),
        "difficulty": v.get("diff"),
        "difficulty_name": {0: "easy", 1: "medium", 2: "hard", 3: "hardest"}.get(v.get("diff")),
    }


# ── MCP tool implementations (return LLM-friendly text) ───────────────────────
def _fmt_realms(diff, rs):
    lines = [f"Difficulty: {game_state()['difficulty_name']} (AI upkeep ÷{AI_UPKEEP_DIV.get(diff,10)}, "
             f"you always ÷{HUMAN_UPKEEP_DIV}).", ""]
    lines.append(f"{'slot':>4} {'who':>4} {'personality':>11} {'treasury':>9} "
                 f"{'free/turn':>9} {'upkeep÷':>7} {'army':>7} {'cnty':>4} {'rank':>4}")
    for r in rs:
        lines.append(f"{r['slot']:>4} {r['controller']:>4} {str(r['personality']):>11} "
                     f"{r['treasury']:>9} {r['free_grant_per_turn']:>9} "
                     f"{r['upkeep_divisor']:>7} {r['army_strength']:>7} {r['counties']:>4} {r['rank']:>4}")
    return "\n".join(lines)


def tool_game_state(_):
    gs = game_state()
    return (f"{gs['season']} {gs['year']} · difficulty={gs['difficulty_name']} · "
            f"on_map={gs['on_map']} (screen {gs['screen']}) · selected county={gs['selected_county']}")


def tool_list_realms(_):
    diff, rs = realms()
    return _fmt_realms(diff, rs)


def tool_cheat_report(_):
    """What the game does for the AI behind your back."""
    diff, rs = realms()
    you = next((r for r in rs if r["controller"] == "you"), None)
    ai = [r for r in rs if r["controller"] == "AI"]
    lines = [f"CHEAT AUDIT — difficulty {game_state()['difficulty_name']}:", ""]
    for r in ai:
        vs = HUMAN_UPKEEP_DIV / r["upkeep_divisor"]
        up = f"{vs:.1f}× cheaper" if vs < 1 else (f"{vs:.1f}× MORE (nerfed)" if vs > 1 else "same as you")
        lines.append(f"AI slot {r['slot']} ({r['personality']}, level {r['ai_level']}): "
                     f"+{r['free_grant_per_turn']} free crowns/turn, army upkeep {up}. treasury {r['treasury']}.")
    lines += ["", f"YOU: 0 free crowns/turn, always ÷{HUMAN_UPKEEP_DIV} upkeep. treasury {you['treasury'] if you else '?'}.",
              "", "The free grant scales with the AI's 'level' (grows with its size); at level 3 the "
              "strong table pays up to 1800/turn. At easy the AI is nerfed (pays MORE upkeep, ~0 grant)."]
    return "\n".join(lines)


def tool_county_report(_):
    gs = game_state()
    k = gs["selected_county"]
    if not k:
        return "No county selected (are you on the map?)."
    f = dreads([("pop", rec(k, R_POP), "i32"), ("tax", rec(k, R_TAX), "u8"),
                ("disp", rec(k, R_INC_PROJ), "i32"), ("coll", rec(k, R_INC_COLL), "i32")]
               + [(f"t{i}", rec(k, R_COUNTS + i * 2), "i16") for i in range(7)])
    troops = ", ".join(f"{TROOP_NAMES[i]}={f.get(f't{i}')}" for i in range(7) if f.get(f"t{i}"))
    return (f"County {k}: population {f.get('pop')}, tax {f.get('tax')}%. "
            f"income displayed {f.get('disp')} vs collected {f.get('coll')}"
            f"{'  ← the game is UNDER-collecting (income lie)' if (f.get('disp') or 0) > (f.get('coll') or 0) else ''}. "
            f"garrison: {troops or 'none'}.")


def tool_set_tax(args):
    pct = int(args["percent"])
    r = dsend({"cmd": "settax", "pct": pct})
    return f"set tax → {pct}%  ({'ok' if r.get('ok') else 'FAILED: ' + str(r.get('err'))})"


def tool_set_rations(args):
    lvl = int(args["level"])
    r = dsend({"cmd": "setrations", "level": lvl})
    return f"set rations → level {lvl}  ({'ok' if r.get('ok') else 'FAILED: ' + str(r.get('err'))})"


def tool_end_turn(_):
    before = {r["slot"]: r["treasury"] for r in realms()[1]}
    r = dsend({"cmd": "tomap"}); r = dsend({"cmd": "endturn", "timeout": 25.0})
    if not r.get("advanced"):
        return f"end_turn did NOT advance ({r.get('err','')}). The game may be over or waiting on a modal."
    time.sleep(0.3)
    gs = game_state(); diff, rs = realms()
    deltas = []
    for rr in rs:
        d = rr["treasury"] - before.get(rr["slot"], rr["treasury"])
        deltas.append(f"slot {rr['slot']} {rr['controller']}: {rr['treasury']} ({d:+d})")
    return f"Advanced to {gs['season']} {gs['year']}.\n" + "\n".join(deltas)


def tool_scout_realm(args):
    """Switch the camera/HUD to a realm's POV (see the enemy's map + treasury) and screenshot."""
    slot = int(args["slot"])
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import ai_pov as A
    grid = A.load_region_grid(); owners = A.region_owner_map()
    px, reg = A.owned_center_pixel(slot, grid, owners)
    if px is None:
        return f"realm slot {slot} owns no territory (eliminated?)."
    A.click(A.MM_SCREEN_X0 + px[0], A.MM_SCREEN_Y0 + px[1]); A.sleep_ms(400)
    A.poke_i32(A.VIEWER, slot)
    if reg:
        A.poke_i32(A.CURTERR, reg)
    path = args.get("path") or str(ROOT / "runs" / "probe" / f"scout_slot{slot}.png")
    A.force_present_shot(path)
    cash = dread(strat(slot, S_CASH), "i32")
    return f"POV → realm slot {slot} (treasury {cash}). screenshot: {path}"


def tool_screenshot(args):
    path = args.get("path") or str(ROOT / "runs" / "probe" / "mcp_shot.png")
    r = dsend({"cmd": "shot", "path": path})
    return f"screenshot: {path}" if r.get("ok") else f"screenshot FAILED: {r.get('err')}"


def tool_read_memory(args):
    return json.dumps(dsend({"cmd": "read", "va": int(args["va"], 0) if isinstance(args["va"], str) else args["va"],
                             "type": args.get("type", "i32")}))


def tool_poke_memory(args):
    va = int(args["va"], 0) if isinstance(args["va"], str) else args["va"]
    return json.dumps(dsend({"cmd": "poke", "va": va, "bytes": args["bytes"]}))


def tool_call_function(args):
    va = int(args["va"], 0) if isinstance(args["va"], str) else args["va"]
    return json.dumps(dsend({"cmd": "callq", "va": va, "args": args.get("args", [])}))


# ── tool registry (name → (schema, handler)) ─────────────────────────────────
TOOLS = [
    ("game_state", "Current turn: season, year, difficulty, whether you're on the map, selected county.",
     {"type": "object", "properties": {}}, tool_game_state),
    ("list_realms", "The board: every realm's treasury, free-grant/turn, upkeep divisor, army strength, counties, rank.",
     {"type": "object", "properties": {}}, tool_list_realms),
    ("cheat_report", "Audit what the game hands the AI behind your back (free gold + upkeep discount) vs you.",
     {"type": "object", "properties": {}}, tool_cheat_report),
    ("county_report", "Your selected county: population, tax %, displayed-vs-collected income, garrison.",
     {"type": "object", "properties": {}}, tool_county_report),
    ("set_tax", "Set your county's tax rate (0..100 %). Higher tax = more income but lower approval.",
     {"type": "object", "properties": {"percent": {"type": "integer"}}, "required": ["percent"]}, tool_set_tax),
    ("set_rations", "Set your county's food ration level (0=starve .. higher=well-fed, grows population/approval).",
     {"type": "object", "properties": {"level": {"type": "integer"}}, "required": ["level"]}, tool_set_rations),
    ("end_turn", "End your turn: run the season roll and report each realm's new treasury + delta.",
     {"type": "object", "properties": {}}, tool_end_turn),
    ("scout_realm", "Switch the camera + HUD to a realm's point of view and screenshot it (see the enemy economy).",
     {"type": "object", "properties": {"slot": {"type": "integer"}, "path": {"type": "string"}}, "required": ["slot"]},
     tool_scout_realm),
    ("screenshot", "Capture the current game screen to a PNG (view it to see the board).",
     {"type": "object", "properties": {"path": {"type": "string"}}}, tool_screenshot),
    ("read_memory", "ADVANCED: read a game memory address (va, type in u8/i16/i32).",
     {"type": "object", "properties": {"va": {"type": ["integer", "string"]}, "type": {"type": "string"}},
      "required": ["va"]}, tool_read_memory),
    ("poke_memory", "ADVANCED: write bytes to a game memory address.",
     {"type": "object", "properties": {"va": {"type": ["integer", "string"]}, "bytes": {"type": "array"}},
      "required": ["va", "bytes"]}, tool_poke_memory),
    ("call_function", "ADVANCED: call a game function on the engine thread (callq va args).",
     {"type": "object", "properties": {"va": {"type": ["integer", "string"]}, "args": {"type": "array"}},
      "required": ["va"]}, tool_call_function),
]
HANDLERS = {name: fn for name, _d, _s, fn in TOOLS}


# ── MCP JSON-RPC (stdio, newline-delimited) ──────────────────────────────────
def _log(msg):
    print(f"[openlords2-mcp] {msg}", file=sys.stderr, flush=True)


def handle(msg):
    mid = msg.get("id")
    method = msg.get("method")
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": mid, "result": {
            "protocolVersion": msg.get("params", {}).get("protocolVersion", "2024-11-05"),
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "openlords2", "version": "0.1.0"}}}
    if method in ("notifications/initialized", "initialized"):
        return None
    if method == "ping":
        return {"jsonrpc": "2.0", "id": mid, "result": {}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": mid, "result": {
            "tools": [{"name": n, "description": d, "inputSchema": s} for n, d, s, _ in TOOLS]}}
    if method == "tools/call":
        params = msg.get("params", {})
        name = params.get("name"); args = params.get("arguments", {}) or {}
        fn = HANDLERS.get(name)
        if not fn:
            return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"no tool {name}"}}
        try:
            text = fn(args)
            return {"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": text}]}}
        except DaemonError as e:
            return {"jsonrpc": "2.0", "id": mid, "result": {
                "content": [{"type": "text", "text": f"GAME NOT REACHABLE: {e}. Boot the probe daemon first."}],
                "isError": True}}
        except Exception as e:  # noqa: BLE001
            return {"jsonrpc": "2.0", "id": mid, "result": {
                "content": [{"type": "text", "text": f"tool error: {e!r}"}], "isError": True}}
    if mid is not None:
        return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"unknown method {method}"}}
    return None


def serve_stdio():
    _log("ready (stdio); connecting tools to probe daemon on demand")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        resp = handle(msg)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


def selftest():
    """Drive a scripted JSON-RPC session (no MCP client needed) against the live daemon."""
    script = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2024-11-05"}},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "game_state", "arguments": {}}},
        {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "list_realms", "arguments": {}}},
        {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "cheat_report", "arguments": {}}},
        {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": "county_report", "arguments": {}}},
    ]
    for req in script:
        resp = handle(req)
        print(f"\n>>> {req.get('method')} {req.get('params',{}).get('name','')}")
        if resp and "result" in resp:
            res = resp["result"]
            if "content" in res:
                print(res["content"][0]["text"])
            elif "tools" in res:
                print("tools:", ", ".join(t["name"] for t in res["tools"]))
            else:
                print(json.dumps(res)[:200])
        elif resp:
            print(resp)


def play(turns=8):
    """Dogfood the MCP tool HANDLERS as a play-loop (the 'python path' the owner
    asked for — proves an agent could drive + audit the game through these tools,
    ahead of wiring the stdio MCP into a client).  Sets a tax policy, ends N turns,
    and watches the AI's free-gold cheat compound."""
    print("=== MCP play-loop (driving retail via the tool handlers) ===")
    print("\n[cheat_report BEFORE]\n" + tool_cheat_report({}))
    print("\n[set_tax 40] " + tool_set_tax({"percent": 40}))
    for i in range(turns):
        print(f"\n[end_turn {i+1}/{turns}] " + tool_end_turn({}).splitlines()[0])
    print("\n[list_realms AFTER]\n" + tool_list_realms({}))
    print("\n[cheat_report AFTER]\n" + tool_cheat_report({}))


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    elif "--play" in sys.argv:
        n = 8
        for i, a in enumerate(sys.argv):
            if a == "--play" and i + 1 < len(sys.argv) and sys.argv[i + 1].isdigit():
                n = int(sys.argv[i + 1])
        play(n)
    else:
        serve_stdio()
