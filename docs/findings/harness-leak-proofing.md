# Harness leak-proofing — never orphan an (elevated) retail process

> Landed 2026-05-29 after two `Lords2.exe` leaked and couldn't be killed.

## The failure

`tools/frida_capture.py` spawns retail through the **UAC-elevated**
frida-server, so the spawned `Lords2.exe` runs **elevated**. Non-admin WSL
(`kill`, `taskkill.exe`) cannot touch it — only the frida device can.

The teardown did fragile synchronous frida RPCs **before** the kill:

```python
stats = script.exports_sync.get_gdi_seat_stats()   # ← un-timeout'd; HUNG
script.unload(); session.detach()
device.kill(pid)                                    # ← never reached
```

When `get_gdi_seat_stats()` wedged, the kill never ran: the python hung *and*
the elevated retail leaked, unkillable from WSL. Two accumulated before it was
noticed. (Reaping them required `device.kill` through the elevated frida-server
— `tools/kill_retail.py`.)

## The fix — defence in depth

Each layer covers a different death mode; all validated 2026-05-29.

| Layer | Covers | Mechanism |
|-------|--------|-----------|
| **Bounded teardown** | the actual bug | every fragile frida call wrapped in `_call_with_timeout`; **kill happens before** unload/detach, and a hung diagnostic can't block it |
| **Kill via device** | elevated target | `device.kill(pid)`, with an enumerate-by-name sweep retry |
| **Pre-run reap** | leftovers from a prior crash | `_reap_by_name` at capture start kills stale `Lords2.exe` before spawning |
| **Controller watchdog thread** | main thread wedged | force `device.kill(pid)` + `os._exit(124)` if teardown exceeds `duration + WATCHDOG_GRACE_S` |
| **In-target heartbeat watchdog** | controller alive but silent (session still up) | agent `setInterval` calls `ExitProcess` if `heartbeat()` RPCs stop for `HEARTBEAT_TIMEOUT_MS`. **Only works while the frida session is alive** — frida tears the agent down the instant the controller disconnects, so this does NOT cover controller death (verified empirically) |
| **Detached reaper** (`tools/reaper.py`) | controller SIGKILL / OOM / crash | spawned `start_new_session=True` (survives controller death); watches a heartbeat file; if it goes stale (>15s) → `device.kill` + name sweep → exit. If the file is **deleted** (clean teardown) → exit without killing |
| **`tools/kill_retail.py`** | anything else / manual | one-command reap of all `Lords2.exe` via the device |

The reaper is the key addition: it's an ordinary non-elevated WSL python (itself
trivially killable) that always terminates (clean signal, staleness, or
max-life), so it can't become a leak of its own, yet it outlives a hard-killed
controller and reaps the elevated target the controller no longer can.

## Validation

- **Normal run**: retail killed, reaper self-exits on heartbeat-file deletion,
  pidfile + heartbeat file cleaned. ✓
- **Controller SIGKILL mid-run**: detached reaper detected staleness at +15.1s,
  `killed spawned pid` + swept by name, retail gone, reaper exited. ✓
- **In-target watchdog alone**: did NOT save a SIGKILLed-controller run (agent
  log dead-stops at disconnect) — which is *why* the detached reaper exists. ✓

## Residual gap

If **frida-server itself** dies while a retail is running, nothing can kill that
retail via frida until the server returns — then the next run's pre-reap (or
`kill_retail.py`) collects it. Acceptable: frida-server is persistent infra.

## Operational notes

- Always pass `--remote cutestation.soy:27042` (WSL2 blocks the 127.0.0.1
  default — [[reference_frida_remote]]).
- After any suspected leak: `nix develop -c python3 tools/kill_retail.py`.
- Constants: `WATCHDOG_GRACE_S`, `TEARDOWN_CALL_TIMEOUT_S`,
  `HEARTBEAT_INTERVAL_S`, `HEARTBEAT_TIMEOUT_MS` in `frida_capture.py`;
  reaper `--stale-s` / `--max-life-s`.
