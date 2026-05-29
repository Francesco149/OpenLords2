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

### Preferred: `--skip-intro` (skips the movie chain outright)

`tools/frida_capture.py --skip-intro` makes the agent snap each boot-intro
Smacker movie to its last frame, so the engine runs its *own* intro→title
transitions in ~3 frames (~200 ms wall) instead of playing the chain. The menu
is reached essentially immediately — a tiny `--frame-capture-warmup-secs` (2–3)
suffices, with or without `--turbo`:

```sh
nix develop -c python3 tools/frida_capture.py \
    --remote cutestation.soy:27042 --run-id retail-mm \
    --turbo --skip-intro --frame-capture --frame-capture-warmup-secs 3 \
    --frame-capture-max 4 --duration-ms 15000
# every captured frame is BIT-IDENTICAL to runs/groundtruth/mainmenu-retail.bmp
# (verified 2026-05-29, both with and without --turbo).
```

**The intro chain.** WinMain (`FUN_0040e9ab`) line 7611 calls `FUN_004b3571(0)`,
which kicks off `intro.smk` (screen state 0x22) and returns — the movie then
plays during the main loop via the state-0x22 dispatch into `FUN_0042dbc7` (the
per-frame Smacker stepper). When a movie reaches its last frame, `FUN_0042dbc7`
closes the handle and calls `FUN_0042e060`, which (while engine state
`DAT_0052af94 == 1`) chains the next movie by filename: **intro.smk →
imptitle.smk → credits.smk**, each with return-state 0x1f. After `credits.smk`,
`FUN_0042e060` lands on screen state **0x1f** (the title menu) and sets the
deferred-screen cookie `DAT_0057d340`, so the next-frame `FUN_0040f1a0` deferred
dispatcher builds the menu via `FUN_0041e7e1`.

**The skip mechanism** (`installSkipIntroHook` in the agent): attach to
`FUN_0042dbc7` and, on each main-loop step (gated `DAT_0052af94 == 1` so only the
boot chain is affected), force the Smacker handle's current-frame
(`DAT_004d4d58 + 0x374`) to its last frame (`+0x0c` count − 1) so the stepper
takes its done→chain branch. The open-time `FUN_0042dbc7` call (nested inside
`FUN_0042da18`) is bracketed out via `g_smack_in_open`, because forcing it done
there would run `FUN_0042e060` before `FUN_0042d91b` has written the return-state
global `_DAT_005169b0`. All engine state transitions stay retail-authentic; only
the per-movie playback duration is cut.

The snap also force-returns `_SmackWait` (smackw32 Ordinal_32) to 0 ("ready")
during the boot chain — under silent-audio + non-turbo the decode thread never
reports ready, which otherwise left the engine stuck at `screen=0x22` with the
menu drawn but non-interactive. With the fix, skip-intro reaches the title menu
(`0x1f`) reliably in both turbo and non-turbo. See
[`title-menu-screens.md`](title-menu-screens.md) for the title page state
machine and the menu-navigation capture workflow built on top of this.

### Fallback: turbo fast-forward (no skip)

The frame-capture cadence is one shot per engine cumulative-second rollover
(`frame_capture_warmup_secs` skips the first N). Under `--turbo` cum-secs fly
(~43 cum-sec per real-sec), so a high warmup lands *after* the played intro:

```sh
nix develop -c python3 tools/frida_capture.py \
    --remote cutestation.soy:27042 --run-id retail-mm \
    --turbo --frame-capture --frame-capture-warmup-secs 1000 \
    --frame-capture-max 6 --duration-ms 30000
# tail frame = the main menu ("Lords of the Realm 2 / The siege is on":
# Single player / Multiple players / Lords of Magic? / Exit game)
```

`warmup 1000` (≈ cum-sec 1000, ~23 real-sec under turbo) reliably lands on the
menu — but `--skip-intro` above is strictly faster and is now the default choice
for menu/UI parity runs.

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
