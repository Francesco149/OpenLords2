# Main-menu render parity — capture, reach, compare

> 2026-05-29. First render-parity target (the start of Phase I / the
> chase-parity-per-scene plan). Capture pipeline + comparison harness landed
> and validated; the port-side menu render is the open work.

## Status

- **Retail frame capture: works** (was already built — agent `captureFrameOnce`
  Locks the DDraw back surface, reads palette + 8-bpp pixels, `_write_paletted_bmp`
  → `frames/frame_NNNNN.bmp`). The roadmap Phase 2 checkbox was stale.
- **Reaching the menu: turbo blows through the intro.** The "King is Dead"
  intro is engine-time-paced (NOT real-time video) — under `--turbo` it
  fast-forwards. At default real-time it's 2min+ (still in the intro at 73s).
- **Comparison harness: `tools/compare_frames.py`** — side-by-side + red-tint
  diff + stats.
- **Baseline established:** port scaffold renders a **solid red back-surface
  fill**; retail renders the full menu. 100% pixel diff. Getting the port to
  render the menu is the parity work.

## Reaching the retail main menu

The frame-capture cadence is one shot per engine cumulative-second rollover
(`frame_capture_warmup_secs` skips the first N). Under `--turbo` cum-secs fly
(~43 cum-sec per real-sec), so use a high warmup to land *after* the intro:

```sh
nix develop -c python3 tools/frida_capture.py \
    --remote cutestation.soy:27042 --run-id retail-mm \
    --turbo --frame-capture --frame-capture-warmup-secs 1000 \
    --frame-capture-max 6 --duration-ms 30000
# tail frame = the main menu ("Lords of the Realm 2 / The siege is on":
# Single player / Multiple players / Lords of Magic? / Exit game)
```

`warmup 1000` (≈ cum-sec 1000, ~23 real-sec under turbo) reliably lands on the
menu. Tune if the intro length changes.

> **Known limitation:** retail capture is cum-sec-cadenced, not frame/tick-
> cadenced, so it doesn't frame-align with the port's `--capture-frames`. Fine
> for a static screen (menu); a tick-precise retail capture trigger is future
> work if we need per-frame alignment on animated scenes.

## Comparison harness

```sh
nix develop -c python3 tools/compare_frames.py \
    --a <port_frame>.bmp   --a-label port \
    --b <retail_frame>.bmp --b-label retail \
    --out runs/compare/<name>
# → runs/compare/<name>/sidebyside.png + diff.png; JSON stats on stdout;
#   exit 0 if identical, 1 if differing.
```

Ground-truth retail frames live under `runs/groundtruth/` (gitignored — retail
screenshots are a redistribution risk, same policy as openrecet's goldens).

## The parity gap (what to port next)

The port currently fills the back surface red and never renders the menu — its
`engine_tick` is a scaffold with no scene dispatch / menu render. To chase
parity:

1. Capture retail's **call-trace** at a menu frame
   (`frida_capture --call-trace --call-trace-vas @hookvas.json` under turbo,
   frames around the menu cum-sec) → the set of engine functions retail runs to
   draw the menu.
2. Those (minus what the port already calls) are the menu render/scene-dispatch
   chips to port — the title/menu state machine + its draw calls.
3. Re-capture the port menu, `compare_frames.py` against
   `runs/groundtruth/mainmenu-retail.bmp`, iterate to `identical: true`.

The red fill is itself a clue: find where retail's menu-scene sets up its
background blit + button sprites + the "Lords of the Realm 2" text, and port
that chain.
