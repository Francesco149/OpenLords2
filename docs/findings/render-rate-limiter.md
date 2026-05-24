# Render rate-limiter + dirty-rect tracker

The retail engine doesn't blit back→primary on every Win32-message-pump
iteration.  It batches presents on a **50-unit accumulator** filled by
the same `timeGetTime`-delta source that drives the FPS counter, and
tracks dirty bounds independently so partial-redraw subsystems can
cull work.  Three small functions cover the whole mechanism.

All addresses are preferred-base (ImageBase `0x00400000`).

## Function map

| Address       | Size | Role                                                  |
|---------------|------|-------------------------------------------------------|
| `FUN_00452160`| 422 B| **The present.**  Rate-gated back→primary Blt/BltFast |
| `FUN_00452306`| 224 B| `dirty_rect_union(l,t,r,b)` — accumulate dirty bounds |
| `FUN_0045240a`|  33 B| `mark_all_dirty()` — wrapper for `(0,0,639,479)`       |
| `FUN_004b27bf`| 143 B| FPS counter + delta-time source (called by `00452160`) |

`FUN_004b27bf` is already documented in
[`main-loop-tick.md`](main-loop-tick.md) §"FPS counter"; covered here
only as the accumulator's input.

## State globals

```
DAT_004eabe8    int     accumulator                     (units: ms?)
DAT_005651ac    bool    sticky "force present next tick" flag
DAT_0050d09c    int     dirty surface count             (0 = no work)
DAT_0050a4c4    int     dirty rect: min_x  (resets to width  - 1)
DAT_0050a4bc    int     dirty rect: min_y  (resets to height - 1)
DAT_0050a4c8    int     dirty rect: max_x  (resets to 0)
DAT_0050a4c0    int     dirty rect: max_y  (resets to 0)
DAT_004eabd0    IDirectDrawSurface*  primary
DAT_004eafac    IDirectDrawSurface*  back
DAT_004ea180    RECT    "visible client rect, screen-space"
                        (Blt dest in windowed; BltFast src in fullscreen)
DAT_0053f228    bool    fullscreen flag (1 = exclusive, 0 = windowed)
DAT_004eb274    int     render width  = 640
DAT_004eac4c    int     render height = 480
DAT_00516a4c    bool    Smacker actively playing       (gate suppress)
DAT_004df28c    bool    Smacker has open handle        (gate suppress)
DAT_0056d650    bool    "alternate composition active"  (cutscene-ish)
DAT_00553008    int     forced-redraw-during-suppression counter (0..100)
```

## `FUN_00452160(int force)` — the rate-gated present

```c
void FUN_00452160(int force) {
    if (FUN_0042e26f() && DAT_004df28c != 0) return;   // Smacker owns the screen
    DAT_004eabe8 += FUN_004b27bf();                    // accumulate ms since last call
                                                       // (side-effect: ticks FPS counter)
    if (DAT_004eabe8 < 0) DAT_004eabe8 = 0;
    if (force         != 0) DAT_004eabe8 = 50;         // pin past threshold
    if (DAT_005651ac  != 0) DAT_004eabe8 = 50;         // sticky-force latch

    if (DAT_004eabe8 > 49 &&
        (DAT_0056d650 == 0 || DAT_00553008 < 1)) {     // not suppressed
        DAT_004eabe8  = 0;
        DAT_005651ac  = 0;
        if (DAT_0050d09c > 0) {                        // something dirty
            if (DAT_0053f228 == 0) {                   // WINDOWED
                primary->Blt(&DAT_004ea180,             // dest rect (screen-space)
                             back,
                             NULL,                      // src = whole back
                             DDBLT_WAIT, NULL);
            } else {                                    // FULLSCREEN
                primary->BltFast(0, 0,
                                 back,
                                 &DAT_004ea180,         // src rect (here = 0,0,640,480)
                                 DDBLTFAST_WAIT);
            }
            DAT_0050d09c--;
            if (DAT_0050d09c < 1) {                     // last dirty consumed
                DAT_0050d09c = 0;
                DAT_0050a4c4 = DAT_004eb274 - 1;        // sentinel: min_x = 639
                DAT_0050a4bc = DAT_004eac4c - 1;        // sentinel: min_y = 479
                DAT_0050a4c8 = 0;                       // sentinel: max_x = 0
                DAT_0050a4c0 = 0;                       // sentinel: max_y = 0
            }
        }
    }
}
```

### Rate-gate semantics

The 50-unit threshold against a `timeGetTime`-delta accumulator
produces ~20 Hz cap when the engine has work to do.  Every call adds
the wall-clock delta since the last present (sourced via the
FPS counter's tick) — once the accumulator crosses 50 ms, present
once and reset.  `force=1` and the sticky `005651ac` latch override
this — used by screen-state transitions to guarantee an immediate
visible update.

### The Smacker bypass

The first guard suppresses the present entirely while a Smacker movie
is playing.  Smacker's draw path (`FUN_0042e3ba` in WM_PAINT) writes
directly to the window DC via `PatBlt` and `_SmackToBuffer*`/`_SmackBufferBlit*`,
so the back-buffer pipeline is dormant.

### The cutscene suppression + forced-redraw insurance

The second gate `(DAT_0056d650 == 0 || DAT_00553008 < 1)` permits the
present **unless** an alternate composition path has claimed the
screen (`0056d650 != 0`) AND the insurance counter (`00553008`) hasn't
yet expired.  `FUN_004247f5` decrements `00553008` once per call and
re-loads it to 100 when it reaches 0 — so even during alternate
composition, a normal back→primary blit is forced once every 100
ticks as a redraw insurance.

Both flags are zero in the steady-state main menu / map loop, so the
gate simplifies to "accumulator ≥ 50" most of the time.

## `FUN_00452306(int l, int t, int r, int b)` — `dirty_rect_union`

```c
void FUN_00452306(int l, int t, int r, int b) {
    if (DAT_0050d09c < 1) DAT_0050d09c = 1;             // ≥ 1 dirty surface
    if (l < DAT_0050a4c4) DAT_0050a4c4 = l;             // expand min_x
    if (DAT_0050a4c8 < r) DAT_0050a4c8 = r;             // expand max_x
    if (t < DAT_0050a4bc) DAT_0050a4bc = t;             // expand min_y
    if (DAT_0050a4c0 < b) DAT_0050a4c0 = b;             // expand max_y
    if (DAT_0050a4c4 < 0)     DAT_0050a4c4 = 0;
    if (DAT_0050a4bc < 0)     DAT_0050a4bc = 0;
    if (0x27f < DAT_0050a4c8) DAT_0050a4c8 = 0x27f;     // clamp x to 639
    if (0x27f < DAT_0050a4c0) DAT_0050a4c0 = 0x1df;     // clamp y to 479
}
```

The final clamp on `max_y` reads as a retail typo: the **threshold**
is `0x27f = 639` (a width value) but the **clamped result** is
`0x1df = 479` (the right height value).  In practice the inputs are
already bounded by caller intent (the only "all of screen" call is
`(0,0,639,479)`), so the typo never bites.

### Sentinel encoding

The "no dirty" state sets `min = (width-1, height-1), max = (0, 0)` —
an empty rect where `min > max`.  Any union with a non-empty rect
immediately flips both bounds correctly because the `<`/`>` tests
always favour the incoming value over the sentinel.

### What consumes the dirty bounds

**Not `FUN_00452160` itself** — the present uses `DAT_004ea180` as the
Blt rect, not the dirty bounds.  The bounds are read by other
rendering subsystems that perform incremental composition (sprite
blit helpers, UI redraws, map scrolling) and want to cull work based
on what has changed.  We don't have any of those wired yet, so the
bookkeeping is currently write-only.

## `FUN_0045240a()` — `mark_all_dirty`

```c
void FUN_0045240a(void) { FUN_00452306(0, 0, 0x27f, 0x1df); }
```

Marks the full 640×480 screen dirty.  Called from ~50 sites — every
screen transition, every major UI redraw, every Restore-after-focus.
Idiomatic "the next present must paint everything".

## Call-pattern catalogue

The 50-odd callsites of `FUN_00452160` split cleanly:

- **Forced present (`FUN_00452160(1)`)** — used immediately after
  screen-state transitions and palette loads.  Pins the accumulator
  past the threshold so the present happens *this* iteration.
- **Rate-limited present (`FUN_00452160(0)`)** — exactly two
  callsites, both inside `FUN_004b99c0`'s render-phase tail.  The
  per-frame "normal" cadence in the steady-state loop.

`FUN_0045240a()` precedes most `FUN_00452160(1)` calls — i.e.
"transition just happened, mark all dirty, force present".  The two
helpers are decoupled (`mark_all_dirty()` doesn't present and
`present(force=1)` doesn't auto-mark) but the pairing is the dominant
pattern.

## Porting notes for `src/gfx.c`

- The dirty-rect bookkeeping is portable as-is even before any
  consumer is ported — it's static-state arithmetic.
- The rate gate replaces our current "Blt every Win32 iteration"
  pattern.  Without a sim-driver, an idle window will still get
  20 Hz repaints (force flag from screen-state transitions clamps
  through unaffected).
- The cutscene gate (`0056d650` / `00553008`) is dead in our drop-in
  until we port the Smacker / video-load path, so the gate
  simplifies to the threshold check.
- The Smacker guard (`0042e26f`) similarly returns false in our
  drop-in (no Smacker state).  Either stub it returning 0, or carry
  the `DAT_00516a4c` mirror as an explicit `g_smacker_playing` flag
  that future Smacker code can set.  Latter is cleaner.
- `DAT_004ea180` (visible client rect) is computed live in our
  windowed Blt via `GetClientRect`+`ClientToScreen` — keep that, no
  reason to mirror the global.

## What this slice does NOT solve

- **Variable sim sub-steps** — that's `FUN_004bb978` in
  [`main-loop-tick.md`](main-loop-tick.md), independent of presents.
- **Surface-lost guard** — `FUN_0040e8bb`, already ported via
  `gfx_restore` + the WM_ACTIVATEAPP wire-up.
- **Smacker playback** — separate TU, deferred.

> 📍 Sources: `docs/decompiled/by-address/{452160,452306,45240a,
> 4b27bf}.c`, `docs/decompiled/all.c` (callsite scan), Ghidra
> `FUN_00452160` / `FUN_00452306` / `FUN_0045240a` / `FUN_004b27bf`.
