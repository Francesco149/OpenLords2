# Engine quirks — a fun tour of *Lords of the Realm II*

A curated list of weird, charming, and occasionally inexplicable
things found in Impressions's engine while reverse-engineering it.
These aren't bugs (well, mostly) and they aren't load-bearing for
anyone using the game — they're just the kind of texture that makes
RE work feel less like archaeology and more like reading someone's
diary.

Each entry: what we found, where we found it, and (when it's
interesting) why.  Citations to engine functions are `FUN_XXXXXX`
Ghidra addresses from `docs/decompiled/by-address/`.

---

## 1. The windowed-mode bring-up path has been dead code since ~2001

`FUN_0042e5c0` (DDraw create + cooperative-level + display-mode) has
two arms gated on `DAT_0053f228` — the "fullscreen flag".  Before
either arm runs, the flag is auto-set:

```c
desktop_bpp = GetDeviceCaps(hdc, BITSPIXEL);
if (DAT_0053f228 == 0 && desktop_bpp != 8) DAT_0053f228 = 1;
```

i.e. "if user asked for windowed and the desktop isn't 8bpp, promote
to fullscreen anyway."  Lords II shipped in 1996 when 8bpp desktops
were normal; by the Windows 2000 / XP transition (~2001) virtually
every desktop in the wild was 16bpp or 24bpp.  So the entire
windowed branch — the `SetWindowLong` style transform, the
`AdjustWindowRectEx` centring, the `SetCooperativeLevel(NORMAL)`
call, the windowed-mode clipper allocation in `FUN_0042ec91` — has
been effectively unreachable for ~25 years on retail.

The dead-code stack runs deeper than that.  WndProc's
`WM_ACTIVATEAPP` (`FUN_004b29be`) has a surface-restore escalator
that calls `FUN_004b2730` (`IDirectDrawSurface::Restore`) on
focus-regain-with-lost-surfaces, and on failure escalates to
`FUN_004b2922` (full re-init).  But surfaces only get
`DDERR_SURFACELOST` under exclusive coop — under NORMAL coop the
primary is a wrapper for the desktop surface, GDI/DWM owns it, and
it never gets evicted.  So the *windowed-mode* surface-restore
escalator is doubly unreachable: needs the windowed branch (already
dead) AND a surface-lost event (impossible in windowed coop).

We ported both anyway — both compile, both are wired, and the
windowed branch is OpenLords2's default.  Twenty-five years after
the developers stopped exercising this code, it has its first real
user.

> 📍 `src/gfx.c` (`gfx_init`, `gfx_restore`), `src/main.c`
> (`WM_ACTIVATEAPP` arm), `docs/findings/ddraw-dsound-bringup.md`
> §"FUN_0042e5c0", Ghidra `FUN_0042e5c0` / `FUN_004b2730` /
> `FUN_004b29be`.

---

## 2. The .ini parser reads past its own buffer (and gets away with it)

`FUN_0040ecc4` (`l2.ini` + `sierra.ini` parser) allocates a fixed
20000-byte scratch buffer and reads a few KB of `.ini` text into
the start of it.  Every key lookup is the same two-step:

```c
off = FUN_00401000(scratch, "CDROM", 20000);   // find substring
eq  = FUN_00401085(scratch + off, '=', 20000); // find '=' AFTER it
off = off + eq;
if (off > 0) value = atoi(scratch + off);
```

The bound passed to `find_char` is **the full 20000**, not the
remaining bytes after `off`.  So once `find_substr` returns a
non-zero offset, the subsequent `find_char` is searching a window
of `20000 - off + 20000` bytes — i.e. it walks up to `off` bytes
past the end of the malloc.

That's a heap-OOB read in textbook terms.  Modern ASan would
scream.  Retail gets away with it because:

1. `malloc(20000)` on the Win32 CRT typically lands inside a
   ≥4 KiB heap segment with surrounding bookkeeping that's
   readable — the OOB doesn't touch unmapped memory.
2. Whatever garbage bytes the scan happens to traverse, they end
   up either (a) not matching `'='`, in which case the search
   gives up and returns 0 (harmless); or (b) matching `'='` by
   coincidence, in which case `atoi` reads more garbage starting
   there.  `atoi` of a random non-digit byte returns 0, of an
   ASCII digit byte returns 1–9, both of which are valid-looking
   small ints that fit the config schema.
3. The parser is structurally robust to "wrong but small int"
   — the misparses get overwritten by later keys that are
   actually present in the file, or they land in fields where
   `0` is a reasonable fallback (`VideoSpeed=0` is the default).

The same mass-bound shows up in `FUN_00401000`'s substring search
itself and in the CPU= line's 20-byte dash scan
(`for (dash = 0; dash < 0x14 && buf[off + dash] != '-'; dash++)`).
All three can technically walk past the buffer; all three are
de-facto safe by the same robustness argument.

Layered on top is a related quirk in `FUN_00401000` itself: it
returns `0` for both "not found" *and* "found at offset 0".
Callers (`FUN_0040ecc4`) work around this with the convention
that real keys never sit at offset 0 (`.ini` files start with
`[Section]` headers or comments).  When that convention holds, the
ambiguity is invisible.  When it doesn't — say, a missing key
combined with any other `=` in the file — the parser silently
parses the wrong value.  Real sierra.ini files have all the
expected keys, so this never bites in retail's actual workload.

We caught both quirks the moment the parser ran under ASan in
`make -C tests run`.  Our port (`src/config.c`) clamps
`find_char` to the remaining bytes and disambiguates
`find_substr`'s 0-return with an explicit `memcmp(buf, key, klen)`
— both flagged in the comment block of `seek_past_equals` as
deliberate retail deviations.  The on-disk behaviour is identical
on well-formed `.ini` inputs; it's the malformed / partial / weird
ones that now do the safer thing instead of the lucky thing.

> 📍 `src/config.c` (`seek_past_equals`), `tests/test_config.c`,
> `docs/findings/winmain-pre-init.md` §"String-helper family",
> Ghidra `FUN_0040ecc4` / `FUN_00401000` / `FUN_00401085`.

---

## 3. The sim-rate LUT is read ~500 bytes past its allocated end

`FUN_004bb978` (sim sub-step rate controller) consults a flat byte
table at `DAT_004e3260` to decide whether to allow a sim step in the
current "0.1× hint × phase column" bucket.  The lookup is:

```c
if ((&DAT_004e3260)[DAT_004d2164 + ((int)local_14 / 10) * 0xa0] == '\0') {
    local_10 = 0;
}
```

`DAT_004d2164` (the phase counter) is `0..0x9f`.  `local_14` is the
clamped scenario hint, `0..0x8c` (140).  So `local_14 / 10` ranges
`0..14`, and the index is up to `0x9f + 14*0xa0 = 0x8df`.

The table is documented (in the binary's only debug-style symbol
density nearby) as having 11 rows × `0xa0` bytes = `0x6e0` bytes
(1760 B).  The actual max index reached is `0x8df` — **480 bytes
past the documented end**.  Same shape as the .ini parser's OOB:
adjacent .data is readable, the bytes that land there happen to be
benign (zero or harmless), the engine never knows.

This is reachable in retail when the scenario hint exceeds 110 ms
per sim sub-step.  `FUN_0043de70`'s rate selector produces values up
to `0x8c` for "Renaissance" scenarios, so the OOB rows 11..14 are
exercised on the actual shipped maps — they just happen to read
into adjacent .data that's all zero (or non-zero but coincidentally
"step allowed", which is the safer-of-the-two failure mode).

Our port (`src/engine_rate.c`) takes the LUT as a pointer, so we
have to extract the real bytes from retail's `.data` and decide
whether to allocate 0x6e0 or 0x8e0 (or beyond).  Today we pass
`NULL` (treats as all-zeros) — the LUT path is never reached
because `engine_scenario_rate_hint()` returns 0 until the engine
reaches ingame state, which our drop-in can't do yet.  The
extraction lands when the consumer subsystems land.

The test suite uses a privately-allocated LUT to exercise the
function past this boundary (`tests/test_engine_rate.c`
`test_engine_rate_hint_clamped_to_0x8c`).

> 📍 `src/engine_rate.c` (`engine_compute_sim_sub_steps`),
> `tests/test_engine_rate.c`, `docs/findings/main-loop-tick.md`
> §"FUN_004bb978", Ghidra `FUN_004bb978`.

---

## 4. The 16-frame anim counter has a defensive clamp for negative values

`FUN_00451f1a` (the pre-tick subfunction that drives the 16-frame
sprite-strip animation cycle) increments `DAT_0053e9a4` mod 16 each
time the 80 ms phase tick fires:

```c
if (DAT_00591510 != 0) {
    DAT_0053e9a4 = DAT_0053e9a4 + 1;
    if (0xf < DAT_0053e9a4) DAT_0053e9a4 = 0;
    if (DAT_0053e9a4 < 0)   DAT_0053e9a4 = 0;
}
```

The second clamp is unreachable.  The counter lives in .bss
(zero-initialised), the only writer is this function, and the only
operations are `+1` followed by the upward clamp that resets to 0
before the value could ever turn negative.  Every consumer
(`FUN_004071a0` is the only one) reads it inside an
`if (0xf < counter) return;` early-out, so even an out-of-range
value couldn't cause an OOB sprite-index lookup downstream — but
the producer guards anyway.

It's hard to say whether the author had a specific failure mode in
mind (maybe an earlier signed-int variant where wraparound could
underflow?) or whether it's just a "belt and braces" habit.  Either
way, the binary carries the extra two-instruction check on every
pre-tick iteration — a few wasted cycles in the hot path for a
condition that physically can't occur.

We port it faithfully (`src/engine_anim_cycle.c`) — same byte
shape, same dead clamp.  The unit tests don't even attempt to
reach the clamp because there's no input that gets there.

> 📍 `src/engine_anim_cycle.c` (`engine_compute_anim_cycle`),
> `tests/test_engine_anim_cycle.c`,
> `docs/findings/main-loop-tick.md` §"pre-tick block",
> Ghidra `FUN_00451f1a`.

---

## 5. The "sim tick" function is an MP-only no-op

Retail's per-frame sub-step body (inside `FUN_004b99c0`'s
variable-rate loop) reads, on the surface, like the canonical "this
is where the simulation advances":

```c
DAT_0057c998 = DAT_0057c998 + 1;                  // master tick
... 1000/0x82/10-tick scheduled events ...
FUN_004472ce(DAT_0057c998);                       // "advance sim"
if (DAT_00553030 == 0) FUN_00404a46();            // SP-only step
```

The natural assumption is that `FUN_004472ce` — the function whose
parameter is the master tick counter, in the slot labelled "advance
sim by 1 tick" — is the heart of the simulation step.  It's 1046
bytes; it visibly walks a 400-element table; it dispatches actions
through a function pointer table.

But the entire function is gated on `if (DAT_00553030 != 0)` (i.e.
MP active).  In single-player it returns immediately.  Same with
`FUN_0043eda0` (637 B, the "scheduled events" dispatcher called on
1000/0x82/10 tick boundaries) — also entirely MP-gated.

So in SP mode, the sub-step body's *actual* per-step work collapses
to: bump the master tick counter, advance the LFSR (`FUN_00404a46`
— ported as `engine_rng_step`), check the savepump state machine
breaks, and run the screen-state-specific extras (which themselves
hand off to per-screen subsystems).  The real per-frame sim work in
SP happens elsewhere — in the pre-tick block (anim phase, frame
delta) and inside the screen-dispatch ladder's per-screen handlers.

The shape suggests an architecture where SP and MP share the same
top-level main-loop driver, but the "deterministic action queue"
machinery (commit actions at the synchronised tick they were
scheduled for) is MP-only because SP doesn't need to synchronise
across peers — it can act on UI events the same frame they fire.
`DAT_0057c998` (the master tick) still increments in SP because the
LUT-driven rate gate depends on it, and because the savefile
captures it, but no `FUN_004472ce` walk consumes it.

We discovered this while surveying the sub-step body for porting.
Our skeleton calls `engine_tick_rng_step` in the SP-only branch and
leaves the MP dispatcher as a comment-only TODO — observably
correct in SP because retail does the same thing.

> 📍 `src/engine.c` (`engine_tick` sub-step body),
> `docs/findings/main-loop-tick.md` §"sub-step body",
> Ghidra `FUN_004472ce` (1046 B, MP-gated body),
> `FUN_0043eda0` (637 B, MP-gated body),
> `FUN_004b99c0` lines 88749..88803.

---

## 6. DDraw NORMAL Blt is invisible under DWM — and retail dodges this by accident

Modern Windows runs DWM compositing.  Each top-level window has a
"redirection bitmap" backing store; DWM composites the visible
desktop from those.  DDraw NORMAL-mode Blts go straight to the
*physical* primary surface and **bypass the redirection bitmap
entirely**.  The Blt returns `DD_OK`, the back buffer holds the
correct pixels, but DWM re-composites the desktop from the unchanged
backing store and the window stays black.

Confirmed empirically: a windowed-mode `gfx_capture_backbuffer`
shows the rendered fill colour while `gfx_capture_window` (GDI BitBlt
of the same window's client area at the same instant) shows uniform
`(0,0,0)`.

Retail Lords2.exe never bites this because of quirk #1 above: its
`FUN_0042e5c0` auto-fullscreen-promotes any time the desktop isn't
8bpp.  Exclusive coop + `SetDisplayMode` put DDraw outside DWM
compositing, so retail's Blts hit the real front-buffer and DWM
isn't in the loop at all.  The windowed branch — which we default
to — is what exposes the issue.

Fix in two places:

```
src/gfx.c          present_via_gdi()   (drop-in's windowed present)
tools/frida/...    presentBackToWindowViaGdi() + Blt-vtable patch
                   (retail under the harness — visual verification)
```

Both Lock the 8bpp DDraw back surface, build a `BITMAPINFO` with
the 256 palette entries inline (RGBQUAD B,G,R order — DDraw's
PALETTEENTRY hands us R,G,B and the swap is the only non-obvious
bit), and `StretchDIBits` to the window's HDC.  GDI calls DO go
through DWM's intercept, so the redirection bitmap updates and the
composited result is visible.

Fullscreen `BltFast` still works (no DWM mediation in exclusive
coop) — so the GDI seat is a windowed-mode-only insertion.

> 📍 `src/gfx.c` (`present_via_gdi`),
> `tools/frida/openlords2-agent.js` (`presentBackToWindowViaGdi`,
> `installPrimaryBltGdiSeat`),
> the `project_dwm_ddraw_normal` auto-memory entry for the original
> empirical diagnosis, Ghidra `FUN_0042e5c0`.

---

## 7. The save format has nine forward-compatibility slots that were never used

The 225-entry positional schema table at `0x4de960` (the entire save-
file layout) has nine trailing entries (indices 216..224) that all
point to the **same** 4-byte buffer at `DAT_004eb2ac` — a scratch
region.  Save: emits the contents of that scratch 9 times in a row.
Load: reads 4 bytes from the file into that scratch, 9 times,
overwriting itself each iteration and discarding the result.

```
schema entry 216 → DAT_004eb2ac, 4 B
schema entry 217 → DAT_004eb2ac, 4 B   ← same address
...
schema entry 224 → DAT_004eb2ac, 4 B   ← same address
```

The functional effect is 36 zero bytes at offset 266992 of every
.sav file, ignored on load.  Index 87 (`0x4eafb0`) is similar.

These are forward-compatibility reserved slots — placeholders the
1996 devs added "in case we need to extend the save format."  The
plan was presumably: a future patch would change one or more of
those pointers from `0x4eb2ac` to a real global, automatically
giving any save written by that patch a place to stash the new
field while still being readable by the old engine (which would
discard it harmlessly).

The patch never came.  Lords II shipped, got a couple of bugfix
releases, and the engine architecture froze.  The 36 reserved bytes
sit at the end of every save file ever written by the game,
faithfully scribbling zeros and reading them back.

Our drop-in preserves the layout exactly — the 36 bytes have to be
in the right position for retail to load our saves.

> 📍 [`docs/findings/save-format.md`](save-format.md) §"Reserved/
> sentinel slots", Ghidra `0x4ade93` / `0x4adfe6` (the I/O loops),
> `0x4de960` (the schema table).

---

## 8. Footprint-neighbor helper uses the orthogonal-axis bound for the wrong axis's bounds check

`FUN_004921e5` (the helper that flags the neighbors of a building
footprint center cell at castle-load time) has two write blocks per
orientation branch — one writing `+1` flag bits to the axis the
building "faces", and one writing `+4` build-stage bytes to the
perpendicular axis.  The flag writes are bounded correctly.  The
stage writes are bounded on the **wrong axis**:

```c
if ((param_3 == 0) || (param_3 == 4)) {           /* vertical orientation */
    if (param_1 < 0x4f) (&DAT_005440e9)[iVar1] |= 0x10;   /* right.+1 — gated on x<79 ✓ */
    if (0 < param_1)    (&DAT_005440d9)[iVar1] |= 0x10;   /* left.+1  — gated on x>0  ✓ */
    if (param_1 < 0x4f) (&DAT_00543e64)[iVar1] = 1;       /* up.+4    — gated on x<79 ✗ */
    if (0 < param_1)    (&DAT_00544364)[iVar1] = 1;       /* down.+4  — gated on x>0  ✗ */
}
```

The `up.+4` write only depends on the row being > 0; gating it on
`col < 79` is geometric nonsense.  The else branch (horizontal
orientation) is the mirror image of the same bug — vertical neighbor
flag writes are gated on row, but the right/left neighbor `+4`
writes are also gated on row.

The bug is *load-bearing by accident*: every caller of `FUN_004921e5`
(via `FUN_004917dd`) feeds it a center cell from a 3x3 footprint
whose top-left was placed by the scenario format, and scenario tooling
constrains anchors to interior coords where both axes are in
`[1, 78]`.  So the gates never fire on a value that would put the
write OOB, even when the gate value is geometrically the wrong axis.

The port preserves this verbatim — `src/engine_tile.c`'s
`engine_tile_set_footprint_neighbor_flags` uses the same axis on the
same write — because a fix would break byte-identical round-trips on
saves that depend on the side-effect surface staying constant.

> 📍 `src/engine_tile.c`'s `engine_tile_set_footprint_neighbor_flags`,
> Ghidra `FUN_004921e5 @ 0x4921e5`.

---

<!--
Template for new entries:

## N. Short title

One-paragraph what.  One-paragraph why or how we found it.  Optional
short code snippet.

> 📍 `src/<file>.c`, `docs/findings/<subsystem>.md` §"<section>",
> Ghidra `FUN_XXXXXX`.

---
-->
