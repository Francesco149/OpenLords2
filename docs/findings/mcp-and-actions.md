# MCP server + player-action map (drive/play retail Lords via an LLM)

2026-07-09. The probe daemon (`tools/probe_daemon.py`) already IS a play-API: state reads =
the board, `settax`/`setrations`/`endturn`/`callq` = moves. `tools/openlords2_mcp.py` wraps
it as an MCP server so a frontier LLM can PLAY (and audit) the game — the roadmap's "play
against an LLM instead of the built-in (cheating) AI" goal, shippable.

## MCP server (`tools/openlords2_mcp.py`)
Stdlib-only stdio JSON-RPC 2.0 (no `mcp` SDK dep — not in the nix env). Connects to the
running daemon via `runs/probe/daemon.json`. Tools return LLM-friendly text.

**Tools:** `game_state` · `list_realms` · `cheat_report` (audits the free-grant + upkeep
cheat vs you) · `county_report` (incl. displayed-vs-collected income lie) · `set_tax` ·
`set_rations` · `end_turn` (reports every realm's treasury delta) · `scout_realm` (switch
POV to a realm + screenshot — see the enemy economy) · `screenshot` · and advanced
`read_memory`/`poke_memory`/`call_function` (engine-thread callq).

**Test without a client (the "python path"):**
- `tools/openlords2_mcp.py --selftest` — scripted JSON-RPC (initialize/tools list/calls).
- `tools/openlords2_mcp.py --play N` — dogfoods the handlers as a play-loop (set_tax → end N
  turns → watch the AI's free gold compound). Boot the daemon first.

**Wiring into a client — DONE 2026-07-09.** Registered as a stdio MCP server `openlords2`
under the `/opt/src/slopstudio` project (`~/.claude.json`, local scope, next to `yutu`);
`claude mcp list` shows ✔ Connected. Restart the client to load its tools; the probe daemon
must be running or every tool returns "GAME NOT REACHABLE".

**GOTCHA — do NOT wire it as `nix develop -c python3 …`.** The dev-shell shellHook echoes its
banner ("openlords2 dev shell ready …") to **stdout**, which corrupts the newline-delimited
JSON-RPC stream (the client reads the banner as garbage and the handshake breaks). Invoke the
interpreter DIRECTLY instead — the MCP server and `ai_pov` are stdlib-only, so a bare python
needs none of the dev-shell env. Registered command:

    ~/.local/state/openlords2/mcp-python/bin/python3 \
        /opt/src/OpenLords2/tools/openlords2_mcp.py

`mcp-python` is a GC-root pinning the flake's python3 so `nix-collect-garbage` can't delete it:
`nix-store --realise <devshell-python-store-path> --add-root ~/.local/state/openlords2/mcp-python --indirect`.
The script resolves the daemon (`runs/probe/daemon.json`) from `__file__`, so cwd is irrelevant.
(Re-pin after a `nix flake update` for a newer interpreter — not needed for a stdlib script.)

**Roadmap:** add the military moves below as tools; a `new_game(config)` tool (difficulty /
player count / AI level) so a match is fully LLM-set-up; a turn-diff summarizer.

## Player-action map (UI anchor + the commit function to `callq`)
The reliable substrate is `callq <VA> [args]` (engine-thread command queue — no coord-hunt).
Clicks (game-space 640×480, need `click_eligible`+`force_active`+`suppress_edge_scroll`) are
the fallback. Full anchor table: `ui-action-anchors.md`.

| action | UI anchor (screen) | commit fn (callq) | notes |
|---|---|---|---|
| **end turn** | (558,469) | `FUN_0043ac23()` | self-dismisses scrolls; season ticks |
| **tax** | Tax label (515,218)→0x15; ↑(197,178)/↓(243,178) | daemon `settax` | "People pay N" = DISPLAYED income (the lie) |
| **rations** | Ration (600,218)→0x19; ↑(340,148)/↓(378,148) | `FUN_0043a5a9(county,level,mode)`; grain/meat = `FUN_0043a379` | daemon `setrations` |
| **conscript** (raise army) | icon (492,447)→0x17 | `FUN_00435ebc` = `engine_armory_conscript_update(county_rec,pct)` | pct→men+happiness; slider hit-test `FUN_00435cef` |
| **arm** (assign weapons) | icon (525,447)→0x0d | (armory family, `engine_armory.c`) | +1/−1/none/all per weapon |
| **send supplies** | icon (560,447)→0x18 | (shipment commit, ported) | From/To + grain/cattle |
| **castle** build/upgrade | icon (592,447)→0x1b | (castle subsystem) | — |
| **diplomacy gift** | icon (622,447)→0x0b→0x1a | (gift screen 0x1a mode 0, ported) | ±10 arrows, Yes/No |
| **labor split** | HUD ◄►(~490/620,258) | in-place | farm↔industry, no screen change |
| **army move / besiege** | map-tile click (needs minimap-recenter on home) | `engine_army_move` (`engine_army_move.c`; `FUN_00466893`=eval_dir) | the expansion move; drives combat |
| **market** buy/sell | (merchant only)→0x0c | (trade commit) | — |

## Gotchas for driving (see [[lotr2-retail-driving-poc]])
- In-game clicks need the daemon's `click_eligible` force + hidden window (real mouse
  overrides synthetic input on a shown window). Drive real-time, not turbo.
- Report/entry scrolls (screen 0x27) dismiss on RMB; event scrolls (screen 0, `var_extended_pause`≠0)
  need `autodismiss`. **Screen 0x1f (31) = end-of-scenario** (a realm won / you lost) — endturn
  stops advancing; the game is OVER (not a dismissable modal).
- On **hardest**, a cheating AI often WINS by ~turn 25 (its snowball → domination), ending the
  run before others reach level 3. To observe a long game, keep a passive human alive
  (`film_survive.py` re-asserts a home garrison + flat treasury each turn).
- **AI level `+0x29` is dynamic on retail** (grows with size; the port only ever writes 1 —
  no growth path ported). Poking it is stomped by the per-turn refresh; it reaches ≥3
  naturally when the AI is big → unlocks the strong grant table (≤1800/turn).
