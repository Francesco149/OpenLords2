# Hybrid pilot — the AI realm turn under rhost (2026-10-23)

Audit §3 asked for a pilot on one already-ported subsystem before deciding hybrid (rhost + port = a drop-in
that runs port code and falls back to retail for the rest) vs standalone (the port keeps its own layout,
rhost is only its oracle). Subsystem: the AI realm turn — the per-tick AI brain FSM + movement, the economy
model, the realm passes (`src/engine_army_ai.c`, `engine_army_economy.c`, `engine_realm.c` and callees).
Commits `5e32afd`..`6d925ae`; wrappers `tools/rhost/rh_hooks_ai.c`.

## Result

- **89 functions hooked, 88 verified**: shadow (retail and port on identical pre-state, every state dword
  compared) and replace (the port alone; `corpus --check` bit-exact on all 33 scenarios), 0 divergences.
  `ai_advance_tier` (0x428c42) is never called by the corpus.
- **Two real gameplay bugs** that the earlier "BYTE-EXACT organic" claims missed, found by the shadow and
  fixed: a spawned unit's roster slot keyed by the realm instead of the owner (quirk #227), and rebellions
  never firing in the port: `escalate_unrest` reached `spawn_rebellion` only through a seam nothing bound
  (quirk #228). Also: retail's scratch globals are live state (quirk #226: the distance helpers' |dx|/|dy|,
  the shared cell cursor DAT_00569570/78) and the nearest-army scanners count in DAT_00553f8c (`8b25090`).
- **Cost per function: ~3 lines** — a table row and a small adapter. The port's 219 save-block globals are
  bound as ABSOLUTE symbols at their retail VAs (one asm block), so linked port modules read and write
  retail's own memory: no copies, no sync. What still needs sync are module file-statics that mirror retail
  globals (`ai_sync_in` / `ai_sync_out`).

## Blockers met (and what was done)

| blocker | status |
|---|---|
| scratch globals the port dropped (convention #130: out-params instead of retail's shared result globals) | fixed per function (#226); retire #130 — write retail's globals where retail writes them |
| file-static mirrors of retail globals, some without setters | synced around every call; better: extern globals named by VA, aliased like the save block |
| link closure: the AI pulls in 27 modules; cut at game_init / engine / main / engine_turn / engine_army_move, bridged in `rh_hooks_ai.c` | works; each batch extends the same closure (the aliases are global to `rhost.exe`) |
| shadow cost: ~2.6 ms a call (the whole ~5 MB state copied four times); hot leaves tripped the watchdog | `rh_state.c` page-tracked shadow (write-protect + copy on first write) |
| `verify` with every hook active never reaches a hook nested inside another shadowed call (it runs retail there): 47 of 89 showed 0 calls | `rhost.py verify` runs in passes, one nesting level each, then a hybrid pass (all hooks replaced, `corpus --check`) |
| parallel agents rebuilding / restaging `rhost.exe` mid-batch (SizeOfImage pinning raced) | `build.sh` flock + link-aside-and-rename |

## Recommendation (pilot agent): hybrid

The binding is cheap once the save block is aliased, the shadow finds bugs the Frida-era claims missed, and
replace mode already runs 89 port functions inside retail bit-exactly over the corpus. Standalone keeps the
port's own data layout and would need a second set of adapters for every batch.

**Decision (owner, 2026-10-23): hybrid** — easier to verify; once coverage is full or close, the standalone
port happens automatically.
