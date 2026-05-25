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

## 9. Delayed-action dispatcher's hard-capped 10-pass shift compaction

`FUN_004472ce`'s MP-relay reorder branch (game_mode == 2, queue_count
> 1) pulls qualifying records (types 0x21..0x23 from non-local players)
out of the main compaction queue and into a high-priority stash, leaving
`QUEUE_EMPTY` (0xffff) sentinels in the queue.  It then runs a hard-coded
**10 passes** of a single-slot left-shift loop to collapse the holes:

```c
for (local_14 = 0; local_14 < 10; local_14 = local_14 + 1) {
    for (local_c = 0; local_c < DAT_00515910; local_c = local_c + 1) {
        if ((&DAT_00515990)[local_c] == 0xffff) {
            for (local_10 = local_c; local_10 < DAT_00515910; local_10 = local_10 + 1) {
                (&DAT_00515990)[local_10] = *(undefined4 *)(&DAT_00515994 + local_10 * 4);
            }
        }
    }
}
```

Each pass shifts every slot from a 0xffff position rightward leftward by
one — one contiguous run of K 0xffff entries collapses by **one slot per
pass**, so K consecutive holes need K passes.  The 10-pass cap means that
if **11 or more** consecutive relay records exist, the resulting queue
still contains 0xffff entries in the middle, and the subsequent
"find-first-EMPTY + append hipri" loop appends the stash on top of the
first remaining 0xffff, clobbering whatever valid entries follow it.

In practice this never trips: a per-tick relay batch is at most a
handful of records, and the bound holds.  But the cap is deliberate (the
number is hard-coded, not derived from queue capacity), and shipping it
without a tighter argument suggests either the dev verified the bound
empirically or shipped a robustness margin and called it done.

The port preserves the 10-pass count and the inner-loop shape verbatim
in `src/engine_delayed_action.c`'s `engine_dispatch_delayed_actions`
step 4b.  The same function also has a related quirk: the inner shift
reads `queue[queue_count]` on its last iteration (one slot past the
queue tail).  At `queue_count < 100` this lands within the queue array
(and reads the 0xffff sentinel from the function-entry clear); at
`queue_count == 100` retail would read `DAT_00515b20`, an unrelated
adjacent global.  The diff harness pins `queue_count <= 99` in test
vectors, so the divergent read never fires.

> 📍 `src/engine_delayed_action.c`'s `engine_dispatch_delayed_actions`
> step 4b, Ghidra `FUN_004472ce @ 0x4472ce`.

---

## 10. The castle-load loop clobbers most of the footprint expansion's writes

`FUN_004aea70` (Castles.dat slot-load — `castles_read_scenario` in
our port) walks the 6400 records in order, copying each record's `+1`
(flags) and `+3` (anchor char) from disk into the live tile array.
For records flagged "preserved across reload" (`flags & 0x01`,
`flags & 0x02` clear), the loop also calls `FUN_004917dd(byte_off)` —
our `engine_tile_expand_footprint_at` — to lay down a 3×3 building
footprint: stamp glyphs into `+3` of all 9 cells, set `+2 |= 0x04`
on each, set `+2 |= 0x80` on the center, write `+1 |= 0x10` to four
diagonal neighbors of the center, write `+4 = 1` to two cardinal
neighbors, and write `+4 = 2 / +3 = door-char / +1 = 0` to one
"door" cell two rows past the anchor.

But the expansion happens INSIDE the per-record loop, and the loop
keeps advancing — overwriting `+1` and `+3` of every record at byte
offset > anchor's byte offset.  So almost every glyph and flag the
expansion just stamped gets immediately erased.  What survives:

- Anchor cell's `+1`/`+3`/`+2`/`+4` (the anchor IS the iter that just
  ran, no later iter touches it).
- ALL cells' `+2` and `+4` (the load loop only writes `+1` and `+3`).
- `path_scratch[]`, `stage_scratch[]`, `footprint_list[]` — separate
  globals, untouched by the load loop.

This means the renderable 3×3 of building glyphs is NOT visible from
the on-disk anchor + flags after a Castles.dat load.  Whatever the
renderer reads must either consult `+2` (the center marker + the 0x04
"inside footprint" bit, which DO survive) or run a second pass that
re-expands footprints after the load loop completes.  When we get
that far in the port the resolution to that puzzle will land in
`docs/findings/render-*.md`; for now the load path mirrors retail
verbatim, surviving observables and all.

The diff harness for `FUN_004917dd` (port's `engine_tile_expand_
footprint`) runs it in isolation — outside the load loop — so the
full expansion is visible to the diff vector.  The clobber is a
load-path quirk, not a function-of-FUN_004917dd quirk.

> 📍 `src/save.c`'s `castles_read_scenario`,
> `src/engine_tile.c`'s `engine_tile_expand_footprint_at`,
> `tests/test_save.c`'s `test_castles_read_fixup_flag_bit0` (which
> only asserts on the surviving observables), Ghidra
> `FUN_004aea70 @ 0x4aea70` and `FUN_004917dd @ 0x4917dd`.

---

## 11. Dirty-rect y-max clamp checks against the wrong axis's bound

`FUN_00452306` (port: `engine_dirty_rect_expand`) is the running
"what pixels need re-rendering" bookkeeper.  After widening the
dirty box to include the new (x1,y1)..(x2,y2), it clamps each
extreme back to the 640×480 screen bounds:

```c
if (DAT_0050a4c4 < 0)          DAT_0050a4c4 = 0;      // xmin → 0
if (DAT_0050a4bc < 0)          DAT_0050a4bc = 0;      // ymin → 0
if (0x27f < DAT_0050a4c8)      DAT_0050a4c8 = 0x27f;  // xmax → 639
if (0x27f < DAT_0050a4c0)      DAT_0050a4c0 = 0x1df;  // ymax clamp — BUG
```

The last line's **condition** uses `0x27f` (639, the X bound) as the
threshold even though the **assignment** uses `0x1df` (479, the Y
bound).  So any dirty_ymax in the range [0x1e0, 0x27f] (480..639)
slips past unclamped — those rows would be 1..160 pixels below the
visible screen.

In practice this hardly fires.  Callers that compute y2 from in-bound
inputs land at y2 ≤ 0x1df anyway; the wrappers we ported all pass y2
≤ 0x1df explicitly.  The only paths that COULD produce an off-screen
ymax are callers that pass `tile_row * 16 + (some height)` for tile
rows past row 29 — and the engine uses an 80×80 sub-grid where rows
go to 79, so e.g. tile_row=29 + height=0x60 = 0x1d0 + 0x60 = 0x230
which the buggy condition misses.  Whether that ever happens in
practice is an open question worth checking when the renderer is
ported, but for now we preserve the bug verbatim — fixing it could
mask a hypothetical caller that has come to depend on the slack.

> 📍 `src/engine_dirty_rect.c`'s `engine_dirty_rect_expand` (last
> clamp), `tests/test_engine_dirty_rect.c`'s
> `engine_dirty_rect_quirk_ymax_threshold_uses_x_bound`, Ghidra
> `FUN_00452306 @ 0x452306` line 38295 in the decompile.

---

## 12. Event-queue slot-zero only zeros one field at the loop-supplied index

`FUN_00472b40` (port: `engine_event_queue_slot_zero`) clears the seven
mutable fields of one record in the 50-slot UI-event ring at
`&DAT_00568480`.  Retail's body looks like:

```c
*(int *)(&DAT_00568488 + param_1     * 0x18) = 0;  // slot[param_1]+8
*(int *)(&DAT_0056848c + DAT_00553ec4 * 0x18) = 0;  // slot[read_idx]+12
*(int *)(&DAT_00568494 + DAT_00553ec4 * 0x18) = 0;  // slot[read_idx]+20
(&DAT_00568492)[DAT_00553ec4 * 0x18] = 0;           // slot[read_idx]+18
(&DAT_00568493)[DAT_00553ec4 * 0x18] = 0;           // slot[read_idx]+19
(&DAT_00568491)[DAT_00553ec4 * 0x18] = 0;           // slot[read_idx]+17
*(int *)(&DAT_00568484 + DAT_00553ec4 * 0x18) = 0;  // slot[read_idx]+4
```

The first write honors the caller-supplied `param_1`; the other six
target slot `DAT_00553ec4` (the consumer's *read* index), ignoring
`param_1` entirely.  r2 confirms the codegen literally: an
`8d 04 40` (`lea eax, [eax+eax*2]`) emerges once from `param_1` and
six times from a fresh load of `[0x553ec4]`.

This is observationally benign at runtime — the consumer
(`FUN_00472e46`) is the only non-init caller and it passes
`param_1 = DAT_00553ec4`.  But the *init* path (`FUN_00472ae0`) loops
`local_8 = 0..0x31` while `DAT_00553ec4` is held at 0 throughout, so
the 50-slot sweep only zeros every slot's `code` (+8) field — every
other field on slots 1..49 retains its pre-init bytes.

At cold boot retail's BSS is loader-zeroed and the divergence is
invisible.  But a runtime re-init from a non-zero state (e.g.
in-game→menu transitions that call `FUN_00472ae0` afresh) leaves stale
audience / target / payload bytes in slots 1..49, which never get
overwritten until a subsequent `engine_event_queue_post` lands on
that slot.  Whether any caller observes the residual bytes depends on
the consumer order — slots are drained sequentially from `read_idx`
forward, and the live-check is on `+8` (the cleared field), so by the
time a slot is considered drain-eligible its `+8` is whatever the most
recent producer wrote, not the stale init bytes.

Preserved verbatim — same byte sequence as retail.

> 📍 `src/engine_event_queue.c`'s `engine_event_queue_slot_zero`,
> `tests/test_engine_event_queue.c`'s
> `engine_event_queue_init_quirk_slots_1_to_49_keep_non_code`, Ghidra
> `FUN_00472b40 @ 0x472b40`.

---

## 13. Nearest-matching picker can't see a hit at linear index 0

`FUN_0047265d` (the expanding-square scanner for "find the cheapest
tile whose terrain stage matches the start cell's") initializes its
"best-cost cell index" tracker as:

```c
local_24 = 0;       // best index
sVar2 = 900;        // best cost
```

and after the box scan, checks:

```c
if (local_24 != 0) {
    DAT_00569570 = local_24 % 0x50;
    DAT_00569578 = local_24 / 0x50;
    return 1;
}
```

The "found something" sentinel and the "no match" sentinel are both
the integer 0.  If the matching cell happens to live at linear index
0 (col=0, row=0 — the top-left corner of the 80x80 grid), the
assignment to `local_24` is indistinguishable from "still no match,"
so the function expands to the next radius and either finds a
different cell or eventually returns 0.

In practice this is invisible: the outer-ring cells are always
walls in retail maps (cost 999, skipped before the match check),
so a match at idx 0 never happens.  But during unit testing with
hand-crafted cost maps the quirk is observable — the picker silently
misses corner-0 matches.  Preserved verbatim; the host test suite
documents the behavior with its own dedicated test case.

> 📍 `src/engine_path.c`'s `engine_path_find_nearest_matching`,
> `tests/test_engine_path.c`'s
> `engine_path_nearest_quirk13_topleft_invisible`, Ghidra
> `FUN_0047265d @ 0x47265d`.

---

## 14. Category-bucket digest probe flips to odd parity on zero

`FUN_004291b3` (the per-category-bucket "first non-duplicate" digest
refresher) walks each bucket's entries at every-other-byte stride
(slots 0, 2, 4, 6, ...) when looking for a value not already in the
digest:

```c
local_10 = (uint)(byte)(&DAT_00567970)[local_8 * 0x10];   // bucket[0]
while ((iVar1 = FUN_0042925e(local_10), iVar1 != 0 &&
       (local_c = local_c + 1, local_c < 6))) {
  local_10 = (uint)*(byte *)(local_14 + 0x567972 + local_8 * 0x10);
  local_14 = local_14 + 2;
  if (local_10 == 0) {
    local_14 = 1;     // <-- flip to odd-index parity
  }
}
```

The probe sequence is bucket[2], bucket[4], bucket[6], ... — until
a probe hits a zero byte, at which point `local_14 = 1` flips the
walk to odd indices: the NEXT probe reads bucket[1 + 2] = bucket[3],
then bucket[5], bucket[7], etc.

In practice this matters only when buckets are sparsely populated
with zeros mixed in.  The map-load caller populates buckets densely
from the top, so the zero-flip almost never fires.  But when crafting
hermetic unit-test inputs it's observable — preserved verbatim and
documented with its own test case
(`engine_category_buckets_refresh_zero_value_flips_to_odd_parity`).

The wider every-other-byte scheme also implies the digest never
samples slots 7, 9, 11, 13, 15 in the no-flip case — those are
unreachable from the initial offset=0 trajectory.  That's a
documentable but separate limitation; we don't have evidence it
affects production runs.

> 📍 `src/engine_category_buckets.c`'s
> `engine_category_buckets_refresh_digest`,
> `tests/test_engine_category_buckets.c`'s
> `engine_category_buckets_refresh_zero_value_flips_to_odd_parity`,
> Ghidra `FUN_004291b3 @ 0x4291b3`.

---

## 15. Text-input clear() sets length=1 even on a zeroed buffer

`FUN_004020ae` (the editor "clear") memsets the 2000-byte text buffer
to zero, then sets `DAT_0058fe94` (`length`) to **1**, not 0.  A
subsequent recompute (`FUN_00401f49`) would set length=0 — so the
"1" is a deliberate sentinel between operations.

In the editor's state machine, valid cursor positions are
`0..length+1`, so `length=1` after clear means "cursor may sit at
position 0, with the terminator visible at position 1".  Without the
+1, an empty buffer would treat position 0 as past-the-end and the
clamp helper would bump cursor to length+1=1 anyway — but with
clamp_state=1.  The "1" sentinel avoids the spurious clamp report.

Init (`FUN_00402009`) doesn't apply the same sentinel — it sets
length to whatever was copied (0 for empty payload).  Inconsistency,
but the editor's renderer / cursor logic don't read length=0 in
practice because init is always called with a non-empty payload.

> 📍 `src/text_input.c`'s `text_input_clear`,
> `tests/test_text_input.c`'s `text_input_clear_sets_length_one_sentinel`,
> Ghidra `FUN_004020ae @ 0x4020ae`.

---

## 16. Text-input init's register-reuse off-by-minus-one indexing

`FUN_00402009` (the editor "init with payload") uses a single
register for both the clearing loop and the payload-copy loop:

```c
for (local_8 = 1999; -1 < local_8; local_8 = local_8 + -1) {
  (&DAT_005cd550)[local_8] = 0;       // clears buf[0..1999]
}
// local_8 == -1 on exit
for (; *param_1 != '\0'; param_1 = param_1 + 1) {
  (&DAT_005cd551)[local_8] = *param_1;  // (&buf[1])[-1] == &buf[0]
  local_8 = local_8 + 1;
}
DAT_0058fe94 = local_8 + 1;            // length = payload_len
```

It looks like a buffer-overflow bug at first glance — `(&buf[1])[i]`
with `i=-1` is `buf[0]`, which is technically a one-past-the-start
read into the *previous* allocation.  But buf is a static `.data`
global at `0x5cd550`, and `&buf[1] + (-1)` arithmetically resolves
to `0x5cd550` — exactly buf[0].  No out-of-bounds access.

The compiler picked this shape because both loops can share `local_8`
in EDX without a reload between them.  The off-by-one in the address
formula compensates for the off-by-one in the register's exit value.

> 📍 `src/text_input.c`'s `text_input_init`,
> `tests/test_text_input.c`'s `text_input_init_empty_payload_yields_length_zero`,
> Ghidra `FUN_00402009 @ 0x402009`.

---

## 17. Text-input cursor clamps past-end to length+1, not length

`FUN_00401984` (the editor's clamp helper) treats `length+1` — not
`length` — as the past-end cursor position:

```c
if (DAT_0058fe94 + 1 <= DAT_005bb4a8) {       // length+1 <= cursor
  DAT_005bb4a8 = DAT_0058fe94 + 1;            // cursor = length+1
  DAT_005cd408 = 1;                           // clamp_state = 1
}
```

So `cursor == length` is fine (sits on the terminator); `cursor ==
length+1` is also fine (no-op); anything beyond is clamped to
length+1 with `clamp_state := 1`.  Then a second clamp step pins
cursor to `max_length` and sets `clamp_state := 2`.

The "+1" matters because the renderer draws an insertion caret
*after* the terminator when the cursor is at `length+1`, indicating
"empty position at end ready to receive input".  Distinct visual
from `cursor == length` (caret on the terminator, indicating "next
char will overwrite the end").

> 📍 `src/text_input.c`'s `text_input_clamp_cursor`,
> `tests/test_text_input.c`'s
> `text_input_clamp_cursor_past_length_bumps_to_length_plus_one`,
> Ghidra `FUN_00401984 @ 0x401984`.

---

## 18. Text-input "overwrite" mode skips max_length and length update

`FUN_00401d26` (insert_char) has two branches depending on `DAT_005c9280`
(the insert/overwrite toggle):

```c
if (DAT_005c9280 == 0) {                         // overwrite branch
  (&DAT_005cd550)[DAT_005bb4a8] = param_1;       // buf[cursor] = c
  DAT_005bb4a8 = DAT_005bb4a8 + 1;               // cursor++
}
else if (DAT_0058fe94 < DAT_005cd4f4) {          // insert branch
  FUN_00401333(buf + cursor, buf + 1 + length);  // shift_right
  buf[cursor] = c;
  cursor++;
}
```

The overwrite branch is unconditional — no `length < max_length`
guard, no length update.  If the caller types past the end, the new
char overwrites the terminator and `length` stays stale until the
next `recompute_length` call.  The clamp helper handles the cursor
position fine afterward (it pins to `max_length` if needed) but
`length` lags behind reality.

The insert branch is properly bounded by `length < max_length` and
uses `shift_right` to make room — but it also doesn't update
`length`.  Both branches rely on the caller to call recompute when
they care about the value.

> 📍 `src/text_input.c`'s `text_input_insert_char`,
> `tests/test_text_input.c`'s `text_input_insert_char_at_end_extends_in_overwrite`,
> Ghidra `FUN_00401d26 @ 0x401d26`.

---

## 19. Text-input probe special-cases the decimal point past column 8

`FUN_00401e49` (probe_at_cursor) writes `_DAT_00591520` based on the
character under the cursor.  Normally it's 1 if the cursor sits on
the terminator, 0 otherwise.  But when `clamp_state == 1` AND the
character is `'.'` AND the cursor index is `>= 8`, the probe returns
**2** instead of 1:

```c
_DAT_00591520 = (uint)(buf[cursor] == '\0');
if (DAT_005cd408 == 1 && buf[cursor] == '.') {
  _DAT_00591520 = (cursor < 8) ? 1 : 2;
}
```

The "8" is the column threshold that retail's number-entry fields
treat as "too far right for a normal decimal point" — past column 8
the field has run out of room for the fractional part and the
renderer paints a warning indicator instead of a normal caret.
`clamp_state == 1` (cursor was at past-end) plus a decimal point at
that position is the engine's way of saying "user typed a '.' but
there's no room for digits after it."

> 📍 `src/text_input.c`'s `text_input_probe_at_cursor`,
> `tests/test_text_input.c`'s `text_input_probe_at_cursor_decimal_ge_eight_is_two`,
> Ghidra `FUN_00401e49 @ 0x401e49`.

---

## 20. Localization's two boundary detectors disagree on NUL-after-NUL

The localization-blob walker advances past sub-strings by detecting
a NUL byte preceded by a "boundary-eligible" byte.  But the cluster
has TWO definitions of "eligible," depending on which entry point
is in play:

```c
/* FUN_004018d7 (seek-only): */
if (*p == 0 && (p[-1] > 0x1f || p[-1] == 0)) { n--; }

/* FUN_004017bf (read) and FUN_004016f3 (write): */
if (*p == 0 && p[-1] > 0x1f)                  { n--; }
```

The seek-only path accepts NUL-after-NUL as a valid boundary;
the read and write paths do not.  In practice this means a blob
containing an empty sub-string (two consecutive NULs) yields a
different boundary count between a `seek N` and a `read N` for
the same N — the read will undershoot by one and copy from the
wrong location.

Either this is a deliberate optimization (callers that seek but
never read might want a faster O(n) advance through empty
sub-strings) or it's a hand-rolled divergence between two
near-clones of the same routine.  Given the rest of the cluster's
asymmetries (see quirks #21 and #22), the latter is more likely.

> 📍 `src/localization.c`'s `localization_seek_skip` vs.
> `localization_read`,
> `tests/test_localization.c`'s
> `localization_seek_skip_accepts_nul_after_nul_boundary` and
> `localization_read_rejects_nul_after_nul_boundary`,
> Ghidra `FUN_004018d7 @ 0x4018d7`, `FUN_004017bf @ 0x4017bf`.

---

## 21. Localization's write path has a second, asymmetric offset table

`FUN_004017bf` (read) and `FUN_004018d7` (seek) decode section
offsets out of a primary table at blob offset 8: 4-byte stride,
**24-bit little-endian**, base anchor is blob[0].

`FUN_004016f3` (write) decodes section offsets out of a different
table at blob offset 0x1c: 4-byte stride too, but **16-bit
big-endian**, with the offset bytes at slot positions +2 and +3,
and the base anchor is blob+0x1c (not blob[0]):

```c
ptr = blob + 0x1c                                 /* base */
    + ((byte*)(blob + 0x1e))[i * 4] * 0x100       /* hi (BE!) */
    + ((byte*)(blob + 0x1f))[i * 4];              /* lo */
```

The write function has **zero retail callsites** — it's dead code
that almost certainly never executed in the shipped binary.
Theories:

1. It's a legacy editor-build path for writing back into a
   localized blob (separate map-editor functionality dropped from
   the game build).
2. It's a hand-rolled clone of `FUN_004017bf` that was written
   against a different blob layout and never updated.

Either way, the read and write halves of the cluster do NOT
operate on the same offset table.  Faithful port preserves the
asymmetry and notes it loudly.

> 📍 `src/localization.c`'s `localization_write` vs.
> `localization_offset`,
> `tests/test_localization.c`'s
> `localization_write_uses_secondary_16bit_be_table`,
> Ghidra `FUN_004016f3 @ 0x4016f3`, `FUN_004017bf @ 0x4017bf`.

---

## 22. Localization bounds checks are off-by-one or absent

The seek and read paths gate their walk on
`walker < (byte *)((int)&DAT_005aea40 + 1)`, where `DAT_005aea40`
is exactly `blob_base + 120000`.  So the check permits the walker
to land ONE BYTE past the end of the blob (offset 120000 inclusive
is treated as in-bounds, even though 120000 is the first
out-of-allocation byte).  The control-byte-skip loop after the
walk has no bounds check at all and will read forward past the
end if every byte happens to be < 0x20.

The write path has **no bounds check whatsoever** — it does the
walk + control-skip + memcpy entirely unguarded.  A caller passing
an offset that walks the pointer past the buffer end will corrupt
adjacent .bss.

In retail this is masked by two things: (a) the actual l2_eng.dat
file is much smaller than 120000 bytes for the English release
(so the buffer is trailing zeros, not garbage), and (b) the write
function is never called.  But the off-by-one in the seek/read
path is observable — a section at the very end of the blob whose
"section" is just the NUL terminator can advance the walker to
offset 120000, where the control-skip loop reads `blob[120000]`
(out of allocation).  Our port mirrors the off-by-one but uses a
size_t comparison instead of pointer arithmetic.

> 📍 `src/localization.c`'s `walker_in_bounds` and
> `localization_write`,
> `tests/test_localization.c`'s
> `localization_seek_skip_out_of_bounds_suppresses_walk`,
> Ghidra `FUN_004018d7 @ 0x4018d7`, `FUN_004017bf @ 0x4017bf`,
> `FUN_004016f3 @ 0x4016f3`.

---

## 23. Word-width measurer treats leading spaces as word-internal

`FUN_004036f9` (the per-word pixel-width measurer used by the
word-wrap renderer) categorizes characters into "leading" and
"internal" via a `saw_printable` flag:

```c
if (c == ' ') {
  if (saw_printable) return total;     /* internal space ends word */
  total += 4;                          /* leading space contributes */
}
```

Leading spaces are MEASURED INTO the word's width (each contributes
+4 pixels), but the FIRST internal space terminates the word.  This
implements the conventional word-wrap rule that whitespace "belongs
to" the preceding word for line-break purposes — the caller copies
out `chars_consumed` bytes after the measurement, picking up the
exact bytes that fit on the current line.

> 📍 `src/font_metrics.c`'s `font_metrics_word_width`,
> `tests/test_font_metrics.c`'s
> `font_metrics_word_width_leading_spaces_count_as_four` and
> `font_metrics_word_width_terminates_at_internal_space`,
> Ghidra `FUN_004036f9 @ 0x4036f9`.

---

## 24. `$` is a soft word boundary in the word-wrap stream

Same function, parallel branch:

```c
if (c == '$') {
  if (saw_printable) return total;     /* internal '$' ends word */
  /* leading '$' falls through — no width, but counted in
   * chars_consumed by the unconditional bump at loop end */
}
```

A '$' after the first printable character terminates the word with
no contribution.  But a '$' that appears BEFORE the first printable
is silently consumed — no width, no boundary, still counted in
`chars_consumed`.

The engine uses '$' as a sigil for runtime macro expansion ("$P"
for player name, "$1" for the first arg, etc. — visible in the
l2_eng.dat strings).  Treating it as a soft word boundary lets the
word-wrap layer break between fixed text and a macro that will be
substituted later.  The leading-skip behavior accommodates strings
that start with a macro: the wrap state machine consumes the sigil
prefix and continues into the macro body to determine wrap width.

> 📍 `src/font_metrics.c`'s `font_metrics_word_width`,
> `tests/test_font_metrics.c`'s
> `font_metrics_word_width_terminates_at_internal_dollar` and
> `font_metrics_word_width_leading_dollar_is_skipped`,
> Ghidra `FUN_004036f9 @ 0x4036f9`.

---

## 25. Word-wrap iteration cap is 1999 (not 2000)

The word-width measurer caps its loop at 1999 iterations:

```c
local_14 = local_14 + 1;
if (1999 < local_14) return local_10;     /* exits at local_14 == 2000 */
```

The text-input editor buffer is exactly 2000 bytes (see
`text_input_state_t.buf`).  The 1999-cap leaves one byte of head-
room — almost certainly the NUL terminator at `buf[length]`.  In
practice the measurer would hit the NUL break first for any real
buffer; the cap exists to catch a buffer that lost its NUL (e.g.
via the overwrite-mode quirk #18) and avoids an unbounded read
into adjacent memory.

> 📍 `src/font_metrics.c`'s `font_metrics_word_width`,
> `tests/test_font_metrics.c`'s
> `font_metrics_word_width_iteration_cap_at_1999`,
> Ghidra `FUN_004036f9 @ 0x4036f9`.

---

## 26. Clipped 2x1 painter checks only the left pixel

The bounds-checked variant of the 2-byte horizontal brush
(`FUN_00403a2c`) tests `(x, y)` against `(width, height)` and then
calls the unchecked 2x1 painter (`FUN_004b3f7a`).  The unchecked
painter writes `pixels[x + y*width]` *and* `pixels[x + y*width + 1]`,
but the wrapper never separately checks the `x+1` pixel.

```c
if (-1 < x && x < width && -1 < y && y < height) {
    FUN_004b3f7a(x, y, color);   /* writes [x] and [x+1] */
}
```

Consequence: when called with `x == width - 1`, the left pixel
writes the last column of row `y` and the right pixel writes one
column past the row end.  In a contiguous canvas (pitch == width,
which is retail's setup at 640×480×8 with no row padding), that
"one past the end" lands on the FIRST pixel of row `y+1`.

The corner case (`x = width-1`, `y = height-1`) writes one byte
PAST the back-buffer entirely — into whatever heap-adjacent memory
DDraw's `lpSurface` was followed by.  Retail callers (the line
drawers in `FUN_00403c49` / `FUN_0040437d`) iterate with a step of
2 and never call the wrapper at exactly the right edge, so the
quirk is dormant in practice.  We preserve it verbatim — the
`gfx_paint_2x1_clipped_bottom_right_corner_wraps_into_guard` test
pins the behavior down by checking that the byte one past the
canvas does in fact get stomped.

> 📍 `src/gfx_paint.c`'s `gfx_paint_2x1_clipped`,
> `tests/test_gfx_paint.c`'s
> `gfx_paint_2x1_clipped_right_edge_wraps_to_next_row` and
> `_bottom_right_corner_wraps_into_guard`,
> Ghidra `FUN_00403a2c @ 0x403a2c` (wrapper),
> `FUN_004b3f7a @ 0x4b3f7a` (unchecked painter).

---

## 27. Glyph blitter clipped duplicates are byte-identical

The text-glyph dispatcher (`FUN_00402a14`) selects between three
leaf blitters by clip mode:

  - `DAT_005cdd20 == 0` (unclipped)  → `FUN_004b41b7` (165 B)
  - `DAT_005cdd20 == 1` (left-clip)  → `FUN_004b425c` (105 B)
  - `DAT_005cdd20 == 2` (right-clip) → `FUN_004b42c5` (105 B)

The two clipped functions are **byte-for-byte identical**.  Every
instruction matches — same prologue, same DAT_* references, same
loop structure, same epilogue.  r2 confirms (105 B each, identical
disassembly modulo address):

```
0x004b425c: push ebp; mov ebp,esp; push ebx; push esi; push edi
0x004b4262: mov esi,[ebp+8]; add esi,[0x58fe04]   ; src = font + glyph_offset
0x004b426b: mov edi,[0x4ea1a8]; add edi,[0x591524]; dst = canvas + cursor_x
0x004b4277: mov eax,[0x591528]; mov edx,[0x4eb274]; mul edx; add edi,eax  ; + cursor_y*W
...
```

The retail source almost certainly had two distinct functions —
`blit_clipped_left()` and `blit_clipped_right()` — that reduced to
the same code after the column-stride / source-stride math was
unified.  MSVC 4.20's inliner/de-duplicator did not catch the
overlap (or the build settings disabled COMDAT folding), so both
copies shipped.  The dispatcher selects "the right one" but they're
behaviorally indistinguishable.

Our port exposes both as named entry points
(`gfx_glyph_blit_left_clipped` / `_right_clipped`) sharing a static
helper.  Preserves the retail call topology and gives the eventual
port of `FUN_00402a14` a 1:1 mapping for both clip cases, without
duplicating the body.

> 📍 `src/gfx_glyph.c`'s `gfx_glyph_blit_left_clipped` /
> `_right_clipped` / shared `glyph_blit_clipped`,
> `tests/test_gfx_glyph.c`'s
> `gfx_glyph_blit_right_clipped_byte_identical_behavior`,
> Ghidra `FUN_004b425c @ 0x4b425c`, `FUN_004b42c5 @ 0x4b42c5`.

---

## 28. Glyph blitter inner loop runs once even at zero counter

The retail inner-loop shape (all three glyph blitters, Ghidra-
decompiled):

```c
do {
    /* body */
    iVar3 = iVar4 + -1;
    bVar1 = 0 < iVar4;
    iVar4 = iVar3;
} while (iVar3 != 0 && bVar1);
```

Translates to: post-decrement the counter, then loop while
*post-decrement value is non-zero* AND *pre-decrement value was
strictly positive*.  Because the body sits inside a `do { ... }
while (...)` it ALWAYS executes at least once — including when the
counter starts at 0 or negative.

  - With `glyph_w == 0`: inner body runs once → one pixel write per
    row.  Outer loop still runs `glyph_h` times → `glyph_h` rogue
    writes at successive dest positions.
  - With `glyph_h == 0`: outer body runs once → one full row of
    `glyph_w` pixel writes.
  - With both zero: one pixel write at the cursor.

In practice the upstream dispatcher (`FUN_00402a14` via
`FUN_0040464c` / `FUN_0040477b`) filters zero-width and zero-height
glyphs by setting `DAT_005cdd20` / `DAT_0058fe18` to `'\x05'` and
short-circuiting the call.  The leaf functions themselves do NOT
guard.  Faithful port preserves the unguarded behavior; the
upstream filter is the only thing keeping it dormant in retail.

> 📍 `src/gfx_glyph.c`'s `gfx_glyph_blit`,
> `tests/test_gfx_glyph.c`'s
> `gfx_glyph_blit_zero_width_writes_one_pixel_per_row` and
> `_zero_height_writes_one_row`,
> Ghidra `FUN_004b41b7 @ 0x4b41b7`.

---

## 29. Glyph descriptor has two height fields, used at different stages

Each 16-byte glyph descriptor in a Lords II font blob carries TWO
"height-shaped" fields, read by different consumers:

  - **`short` at descriptor +2** — the "natural" glyph extent in rows.
    The Y-clip setter (`FUN_0040477b`) uses this to decide whether the
    glyph fits inside the clip rect and to compute the top/bottom skip.
  - **`byte` at descriptor +0xd** — a per-glyph cursor_y advance.  The
    dispatcher reads it into `DAT_005bb478` and immediately *adds it to
    cursor_y*, as if it were a line advance.

What makes this interesting: `DAT_005bb478` then gets OVERWRITTEN by
the Y-clip setter with the visible (post-clip) row count — that's the
value the blitter actually reads as its outer-loop bound.  So one
global has two unrelated uses, ordered carefully by the dispatcher to
avoid clobbering before the cursor update:

```c
DAT_005bb478 = byte_at_descriptor_+0xd;          // step 1: stage advance
DAT_00591528 += DAT_005bb478;                    // step 2: cursor_y += advance
... (default-font tweak shaves) ...
FUN_0040477b(...);                               // step 3: clip; OVERWRITES
                                                 //         DAT_005bb478
                                                 //         with row count
FUN_004b41b7(font);                              // step 4: blitter reads the
                                                 //         post-clip value
```

The natural reading is that the `short` at +2 is the *glyph height for
clip math* and the `byte` at +0xd is the *cursor advance for the next
glyph*.  Almost always they should be equal (a glyph occupies as many
rows as it advances the cursor), but the engine treats them as
independent — handy for descenders, accent marks above the baseline,
or zero-height combining marks that consume zero rows but still
advance the line.

> 📍 `src/gfx_dispatch.c`'s `gfx_dispatch_glyph`,
> `docs/decompiled/by-address/402a14.c`, Ghidra `FUN_00402a14` lines
> reading `psVar2[1]` (height short) vs `psVar2[+0xd]` (advance byte).

---

## 30. Glyph dispatcher hard-codes per-character kerning tweaks for one font

`FUN_00402a14` has three independent character-range checks that each
decrement `cursor_y` by one pixel — but ONLY when the font passed in
matches a specific global (`DAT_005af8f0`, one of the Charset blobs):

```c
if (font_base == &DAT_005af8f0) {
    if (0x60 < c && c < 0x6e) cursor_y -= 1;   // 'a'..'m'
    if (0x72 < c && c < 0x78) cursor_y -= 1;   // 's'..'w'
    if (0x7f < c && c < 0x85) cursor_y -= 1;   // 0x80..0x84
}
```

A few observations:

  - **The tweaks are baked into the dispatcher, not the font.**  Two
    callers using the same font data with different `font_base`
    pointers would get different kerning.  Almost certainly a way to
    reuse the same font blob with and without the tweak (one pointer
    is the "tweaked" alias, the other is the raw).
  - **The ranges look like a manual kerning pass.**  Lowercase
    `'a'..'m'` covers most of the high-x-height lowercase letters
    (a, c, e, m, n, o…); `'s'..'w'` picks up the rest.  The third
    range is in the extended Latin block (0x80..0x84) — almost
    certainly accented variants of the same lowercase letters.  A
    type-designer would say these glyphs sit one pixel "tall" in the
    font blob and need to be drawn one pixel "lower" to align.
  - **The decrement happens AFTER `cursor_y += advance`.**  So the
    effective draw position is `(prev_cursor_y + advance) - 1` — a
    one-pixel shave per tweaked character.  Glyphs are positioned
    vertically per-character.

We expose the trigger font as a runtime registration
(`gfx_dispatch_set_default_font`) so the test harness can pick which
font gets the tweak.

> 📍 `src/gfx_dispatch.c`'s `gfx_dispatch_glyph` default-font branch,
> `docs/decompiled/by-address/402a14.c`, Ghidra `FUN_00402a14` at
> `0x402a97` (`cmp dword [arg_8h], 0x5af8f0`).

---

## 31. Alt-clip rect mixes runtime globals with hard-coded constants

The glyph dispatcher's alt-clip branch — used when an overlay panel
(scenario list, hint text) wants to confine glyphs to a sub-rectangle
— sources its clip bounds from FOUR places:

  - **Left**: `DAT_0052afdc` (runtime global).
  - **Right**: `0x1de` (hard-coded immediate in the dispatcher body).
  - **Top**: `0x18` (hard-coded immediate).
  - **Bottom**: `DAT_00553254` (runtime global).

The two constants (478, 24) are pixel-perfect for the panel-overlay
layout in 640×480 mode — they correspond to specific UI element
edges baked into the dispatcher's body rather than configured
externally.  Our port preserves the asymmetry: `set_alt_clip(enabled,
left, bottom)` takes runtime values for the two axes that retail
loads from globals, and the constants are encoded as `#define`s
inside `src/gfx_dispatch.c`.

This means OpenLords2 can't render text in a panel of a different
size without re-compiling — the same constraint retail had.  Not a
bug; just a design choice from 1996 where "the overlay is always 24
pixels from the top and 478 pixels wide" was a fixed assumption.

> 📍 `src/gfx_dispatch.c`'s `GFX_DISPATCH_ALT_CLIP_TOP/RIGHT` +
> `gfx_dispatch_glyph` alt-clip branch, Ghidra `FUN_00402a14` at
> `0x402b1b` / `0x402b34` (push immediates 0x1de / 0x18).

---

## 32. Char→glyph table is one buffer viewed from two addresses

The renderer at `FUN_00402637` and the font-metrics measurer at
`FUN_004015b9` / `FUN_00401f8f` look up character glyphs through what
appear to be two distinct tables: `DAT_004d71f0` (the renderer's) and
`DAT_004d71d0` (font_metrics').  In fact those symbols are exactly
0x20 bytes apart, and the renderer indexes its table at
`raw_char - 0x20` while font_metrics indexes at `raw_char`.  The two
reads land on the same byte.

The trick: lift the 32 control-byte slots out of the renderer's
indexed range entirely.  The renderer subtracts 0x20 from each
character before indexing, so the table it consults has its origin at
ASCII 0x20.  Font_metrics doesn't bother with the subtraction (its
char-width function is called from places that may pass control
bytes), so it consults the same data at its "true" address.  The two
DAT_* symbols are just two views of one allocation.

In our port we don't share addresses — `gfx_text` and `font_metrics`
each take a `char_glyph_table` parameter.  Callers can either pass the
same backing array twice with the renderer's pointer offset by 0x20
(matching retail's memory layout) or pass two semantically-equivalent
tables.  Tests use the first approach via an `overlapped_table_t`
helper that materializes the memory layout retail relied on.

> 📍 `src/gfx_text.c` (table indexed at `glyph_idx = ch - 0x20`) and
> `src/font_metrics.c` (table indexed at raw `c`), Ghidra
> `FUN_00402637` at `0x402683` (`subus`/`movzx` on `local_c - 0x20`).

---

## 33. Renderer's substitution stream is one-shot but loop-sticky

The text renderer at `FUN_00402637` has a '#' macro: when the
substitution flag (`DAT_005bb4c4`) is set and a '#' appears in the
source, the renderer pulls bytes from a fixed substitution buffer
(`DAT_005bb4d0`) instead.  But the stickiness is unusual.  The
top-of-loop check `if (local_c == 0x23)` only triggers the FIRST
substitution; from that point on, the BOTTOM-of-loop advance switches
to "fetch from stream" purely because the stream cursor
(`DAT_005bb4b4`) is non-zero.  Subsequent chars come from the stream
without needing more '#'s in the source.

When the stream's NUL terminator is hit, `FUN_00402c16` returns ' '
and resets the cursor to 0.  THIS is what re-arms the source-buffer
read: the bottom-of-loop advance checks `DAT_005bb4b4 == 0`, and once
the cursor wraps to 0, the next iteration reads from the source
buffer again.  The substituted '#' has been "consumed" by then.

So `"text#more"` with substitution "AB" loaded renders as
`"textABmore"` — exactly one source `#` triggers TWO substituted
characters because once the stream is engaged, it stays engaged
until exhausted.

> 📍 `src/gfx_text.c`'s `gfx_text_render_line` (substitution stream
> branch) + `substitution_next`, Ghidra `FUN_00402637` lines around
> `0x402702` / `0x402a05` (top-of-loop '#' branch vs bottom-of-loop
> stream advance).

---

## 34. Renderer's uppercase-highlight does a per-iteration color reset

When `DAT_0058fe2c` (the uppercase-highlight flag) is non-zero, the
text renderer at `FUN_00402637` reassigns the per-glyph color
(`local_8 = param_5`) on EVERY loop iteration — then, if the current
character is uppercase A-Z, overrides it to 1.  When the flag is
zero, the renderer skips both the reset AND the override, leaving
`local_8` at whatever value it had from the prior iteration (which
starts at `param_5` from the function preamble).

This means the behavior with the flag off is "use the caller's color
throughout" — the reset-and-override is dead-equivalent to just
"override per-char" when the flag is on.  The reset is a defensive
write that costs an iteration store but ensures the previous char's
override doesn't bleed forward.

Implemented as a comma-expression in retail's source:
`(DAT_0058fe2c != 0) && (local_8 = param_5, 0x40 < local_c) && ...`
where the side-effect lives inside the short-circuit gate.  Cute
1996-era pattern.

> 📍 `src/gfx_text.c`'s `gfx_text_render_line` (the
> `g_uppercase_highlight` block), Ghidra `FUN_00402637` at `0x4026d4`
> (comma-expression with `mov dword [local_8], esi`).

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
