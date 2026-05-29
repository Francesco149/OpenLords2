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

## 35. Two word-wrap renderers differ only by feature flags

`FUN_0040328e` (673 B, localized) and `FUN_0040352f` (458 B,
caller-supplied buffer) share the same outer loop structure — for
each line up to a hard cap of 99: zero an intermediate line buffer,
greedily append whole words until the running pixel width meets the
budget, arm `substitution_on` + `line_x_acc = 0` +
`collapse_active` and call `gfx_text_render_line`, then advance the
local cursor_y and global `line_y_acc` by a per-font line height.

`FUN_0040328e` adds four wrinkles on top:

- A localization-blob seek at the top (`DAT_005c9270 = blob_base +
  offset(loc_index)`) + the `param_2` sub-string skip + the
  leading-control-byte skip.
- A leading-space-skip flag (`bVar3`) armed at the start of every
  wrapped line and cleared after the first printable byte is
  copied.  Suppresses indent leak on soft wraps.
- A two-stage pagination knob (`param_6 <= local_18` after each
  emit swaps `cursor_x` to `param_7` and `line_width` to `param_8`).
- A `DAT_005aeb88 = 0` reset at the top (the max-line-width
  tracker).  `FUN_0040352f` does NOT reset it — see quirk #36.

`FUN_0040328e` uses a 2000-byte line buffer (`DAT_005c92a0`);
`FUN_0040352f` uses a 200-byte one (`DAT_005aea60`).  Source-level
the two could plausibly be one function with optional features;
MSVC 4.20 emitted them separately, and the dead-stripping linker
didn't fold them.

Our port factors the shared structure into shared helpers
(`arm_line`, `track_max_line_x`, `advance_y`, `seek_localized`)
inside `src/gfx_wrap.c`, but keeps the two entry points so each
matches the retail call shape.

> 📍 `src/gfx_wrap.c`'s `gfx_wrap_buffer` and `gfx_wrap_localized`,
> Ghidra `FUN_0040352f @ 0x40352f` and `FUN_0040328e @ 0x40328e`.

---

## 36. max_line_x output is reset only by the localized word-wrap

The wrap subsystem's `DAT_005aeb88` tracks the widest emitted line
across the most recent multi-line render — caller-readable; used by
upstream UI code (e.g. retail's scenario-hint center at `0x476e95`)
to compute centering offsets.

`FUN_0040328e` resets it to 0 at the top (`DAT_005aeb88 = 0;` is
the FIRST statement of the function).  `FUN_0040352f` does NOT —
it relies on the caller to seed the value externally.

Effect: a `FUN_0040352f` call's reported max-width is "max of THIS
render's lines AND whatever was already in the field".  Callers
that want a fresh reading must zero `DAT_005aeb88` themselves
before the call.  In retail this manifests as a few specific call
sites doing the reset explicitly; a typo or omission would have
yielded a stale-max bug.

Our port exposes the asymmetry verbatim: `gfx_wrap_localized`
resets, `gfx_wrap_buffer` does not.  `gfx_wrap_set_max_line_x` is
the explicit reset helper for the latter.

> 📍 `src/gfx_wrap.c`'s `gfx_wrap_buffer` (no reset) +
> `gfx_wrap_localized` (resets at top), Ghidra `FUN_0040352f` and
> `FUN_0040328e`.

---

## 37. Word-wrap line-height switch is by font-pointer identity

Both word-wrap functions advance the line cursor by 24 px when
`param_9 == &DAT_005b2fa0` and 16 px otherwise.  The discriminator
is **pointer equality with the address of a specific font blob's
BSS location**, not a property of the font itself.

This means the "tall font" is identified externally by who-owns-the-
pointer rather than by reading a height byte from the font header.
Adding a new tall font requires either re-using the `DAT_005b2fa0`
blob (overwriting it) or duplicating the comparison everywhere
line-height matters.

Our port exposes the discriminator as a registered marker pointer
(`gfx_wrap_set_tall_font_marker`) so tests can register any blob
address as "the tall one"; callers in src/ register the same blob
the runtime loader puts in the equivalent BSS slot.

> 📍 `src/gfx_wrap.c`'s `line_height_for`, Ghidra
> `FUN_0040328e` and `FUN_0040352f` (the
> `if (param_9 == &DAT_005b2fa0)` branch).

---

## 38. Bresenham line drawer mutates the abs(dx)/abs(dy) BSS slots

`FUN_00403a8f` is a Bresenham line drawer with an unusual variant:
the `DAT_005aeb40` and `DAT_005aeb44` slots (set up by `FUN_004037ee`
to hold abs(dx) and abs(dy)) are **decremented inside the inner
loop** — one slot per iteration, and BOTH when the err accumulator
crosses zero.

Loop body:

    paint(local_c, local_8, color);           // current pixel
    bres_err_update(steep_mode);              // err update using
                                              //   CURRENT abs_dx/dy
    DAT_005aeb40-- (or 44, dominant axis);    // decrement
    if (err > 0) {
        DAT_005aeb44-- (or 40, other axis);   // also decrement
        local_c += DAT_005aeb54 (x or y step);
    }

The accumulator update inside `FUN_00403949` reads the current
(mutated) abs_dx / abs_dy each call.  Reads:

  shallow: err < 0  → err += 2 * abs_dy
           err >= 0 → err += 2 * (abs_dy - abs_dx)
  steep:   err < 0  → err += 2 * abs_dx
           err >= 0 → err += 2 * (abs_dx - abs_dy)

This is mathematically valid but not the textbook form (textbook
Bresenham keeps abs_dx/abs_dy fixed and updates err with the same
constants every iteration).  The mutating form has a non-uniform
pixel distribution along shallow / steep diagonals — e.g. a line
from `(0,0)` to `(10,5)` plots 4 pixels at y=0, 3 at y=1, then 1
each at y=2..5.  Distinct from textbook Bresenham's symmetric
2-2-2-2-2-1 distribution, but endpoints land on the correct
coordinates and total pixel count = `max(|dx|,|dy|) + 1` is
preserved.

Why retail did this is unclear — possibly an MSVC 4.20 codegen
artifact from a hand-written algorithm, possibly an explicit choice
(the per-iteration `abs_dx - abs_dy` motif removes one constant
load).  Our port preserves the exact arithmetic verbatim.

> 📍 `src/gfx_line.c`'s `bres_err_update` + `gfx_line_draw`, Ghidra
> `FUN_00403a8f @ 0x403a8f` and `FUN_00403949 @ 0x403949`.

---

## 39. Bevel rects skip the right/bottom clamp when x/y was clamped

The 3 axis-rect bevel wrappers (`FUN_00403deb`, `FUN_00403ee4`,
`FUN_00403fdd`) all use the same bounds-clamp pattern at the top:

    if (param_1 < 1) {
        param_1 = 0;
    } else if (DAT_004eb274 <= param_3 + param_1) {
        param_3 = DAT_004eb274 - param_1;
    }

Note the `else if`.  The right-edge clamp ONLY fires when the
left-edge clamp DIDN'T.  So a rect with x=0 (well in-canvas) gets
its left edge clamped to 0 (no-op since x was already 0), but the
right-edge-fit-in-canvas check is **skipped entirely**.  Same
pattern for y / height.

By contrast, the solid rect (`FUN_00403cf4`) uses sequential `if`s:

    if (param_1 < 1)                              param_1 = 0;
    if (DAT_004eb274 <= param_3 + param_1)        param_3 = DAT_004eb274 - param_1;

Both clamps fire independently.

Behaviorally the bevel skip is harmless — the per-pixel painter
(`FUN_004039c7`) clips off-canvas writes, so the visible result is
the same.  But the line drawer iterates the full unclamped width,
wasting cycles painting pixels that then get dropped.  Looks like
an oversight that survived for 30 years because the slow path is
never hit at common rect sizes.

Our port preserves both clamp shapes as distinct helpers
(`clamp_solid` vs `clamp_bevel`) — the visible difference only
shows up when oversize rects are stress-tested.

> 📍 `src/gfx_line.c`'s `clamp_solid` + `clamp_bevel`, Ghidra
> `FUN_00403cf4`, `FUN_00403deb`, `FUN_00403ee4`, `FUN_00403fdd`.

---

## 40. Dotted line drawer is axis-only — diagonals silently draw nothing

`FUN_00403c49` is the dotted-line variant of the Bresenham line
drawer.  Its body is the same setup + axis-dispatch shape as
`FUN_00403a8f`, but trimmed:

  - Pure vertical (x1==x2)   → loop with step +2 on y
  - Pure horizontal (y1==y2) → loop with step +2 on x
  - Anything else            → fall through, draw NOTHING

There are no shallow or steep branches.  Diagonal calls reach the
end of the function without painting a single pixel.

The retail codebase never invokes this with diagonal endpoints (the
two callsites both pass axis-aligned coords for UI panel borders),
so this is "load-bearing only for the axis path".  A future caller
that didn't check would draw an invisible line and have no
diagnostic to point at.

Our port preserves the silent no-op verbatim — tests assert that
diagonal dotted-line calls produce zero pixels, so a future
regression that "fixes" the body to draw diagonals would
deliberately fail.

> 📍 `src/gfx_line.c`'s `gfx_line_draw_dotted`, Ghidra
> `FUN_00403c49 @ 0x403c49`.

---

## 41. Label-render `prefix` overwrites buffer[0] rather than prepending

The four label-render wrappers (`FUN_00402f64`, `FUN_00403015`,
`FUN_004030c6`, `FUN_00403190`) all take a `char prefix` parameter.
The wrapper body is:

    FUN_004022bd(value, 1, 0);          /* format digits at offset 1 */
    if (prefix != 0) {
        DAT_0058fda0 = prefix;          /* OVERWRITE byte 0 */
    }
    /* ...append suffix, render... */

The formatter is called with offset=1, so it writes the sign (if any)
at buffer[1] and digits at buffer[1..count].  Buffer[0] is left as 0
by the prior zero-fill.  The prefix then drops INTO buffer[0] — it
isn't a "prepended" character; it's the byte-0 sentinel slot.

So `prefix=' '` with value=42 yields the buffer `" 42\0"` (space at
[0], "42" at [1..2]); `prefix='@'` yields `"@42\0"`.  All retail
callers rely on this slot being either a separator (' ') for rank
lists or an icon-like sigil ('@') for stat readouts — there's no
"unprefixed" path that uses buffer[0] for a digit.  The width budget
implicitly includes the prefix slot.

A future caller passing `prefix=0` will get a 1-byte gap (NUL) at
the start of the buffer, which the renderer skips silently — but no
retail callsite does this.

> 📍 `src/gfx_text_label.c`'s `apply_prefix`, Ghidra `FUN_00402f64
> @ 0x402f64` / `FUN_00403015 @ 0x403015` / `FUN_004030c6 @ 0x4030c6`
> / `FUN_00403190 @ 0x403190`.

---

## 42. Integer formatters silently render zero digits for large values

The two digit formatters (`FUN_004022bd` decimal, `FUN_00402447` hex)
both pick a digit count by walking a magnitude cascade:

    if      (v <         10) count = 1;
    else if (v <        100) count = 2;
    ...
    else if (v < 1000000000) count = 9;
    else                     count = 0;       /* dec */

    if      (v < 0x10)       count = 1;
    ...
    else if (v < 0x10000000) count = 7;
    else                     count = 0;       /* hex */

The digit-write loop then iterates `count` times.  When `count == 0`,
the loop body never executes — the formatter writes the sign byte
(if any), then no digits.  Net effect: values ≥ 10^9 (decimal) or
≥ 0x10000000 (hex) render as just `""`, `"-"`, or `"+"` with no
number.

Almost certainly a "callers never pass values this large" assumption
— in 1996 a 30-bit player stat was implausible.  Modern callers that
land in this regime (e.g. an experimental mod with high resource
caps) would see the digits silently vanish with no diagnostic.

Our port keeps the silent-zero branch verbatim; tests assert that
`format_dec(10^9, ...)` produces an empty string and
`format_dec(-10^9, ...)` produces `"-"`.

> 📍 `src/gfx_text_label.c`'s `gfx_text_label_format_dec` /
> `gfx_text_label_format_hex`, Ghidra `FUN_004022bd @ 0x4022bd` /
> `FUN_00402447 @ 0x402447`.

---

## 43. Format buffer pre-zero scope is 100 bytes (not the full 8000)

Both integer formatters open with:

    for (i = 0; i < 100; i++) {
        DAT_0058fda0[i] = 0;
    }

The format buffer (`DAT_0058fda0`) is 8000 bytes wide.  Only the
first 100 bytes are zeroed before each format/render — bytes 100+
retain whatever a prior caller left there.

This is fine in practice because:
  - The longest formatted prefix+digits+suffix sequence in real
    callers is well under 100 bytes (typically ≤ 16 bytes).
  - The suffix-append walks until the first NUL, which always lies
    within the zeroed region.

But the buffer is **genuinely shared mutable state** across multiple
subsystems: the label-render path here, and the save-name copy paths
in `FUN_004afab9` and `FUN_004afeed` (not yet ported) which READ
bytes 0..99 of the same buffer.  Any future addition that wants to
stage longer strings in this buffer needs to extend the zero scope.

> 📍 `src/gfx_text_label.c`'s `format_zero_and_sign`, Ghidra
> `FUN_004022bd @ 0x4022bd` and `FUN_00402447 @ 0x402447`.

---

## 44. Signed-color label render skips entirely when both inputs are zero

`FUN_00403190` is the only label-render wrapper with an "early
return" gate:

    if ((param_1 != 0) || (param_2 != 0)) {
        /* ...format + render... */
    }
    /* fall through with no render */

`param_1` is the value to format; `param_2` is a sister value (no
captured callers; semantics inferred).  When BOTH are zero, the
wrapper returns without touching the format buffer or invoking the
renderer.

Inferred design: the wrapper renders a delta (e.g. "+5" or "-3"), and
`param_2` is a companion stat for the same UI row.  When both stats
are zero, the whole row is uninteresting and gets suppressed — a
common pattern in resource panels where empty deltas would clutter
the display.

No captured callsite exists in the decompiled corpus for this
function, so the semantics are unverified.  Our port honors the gate
verbatim; tests assert that `render_dec_signed(0, 0, ...)` leaves
the format buffer untouched (sentinel survives) and produces zero
pixels.

> 📍 `src/gfx_text_label.c`'s `gfx_text_label_render_dec_signed`,
> Ghidra `FUN_00403190 @ 0x403190`.

---

## 45. Centered label-render variants embed a NUL between digits and suffix

Two pairs of label-render wrappers share the same shape — format
digits, optional prefix, append suffix, render — but the centered
variants (`FUN_004030c6` and `FUN_00403190`) walk the buffer with a
do/while loop that advances ONE byte past the first NUL:

    /* centered variant — buggy off-by-one */
    do {
        prev = local_8;
        local_8++;
    } while (buf[prev] != 0);
    /* loop exits with local_8 = (first-NUL-index) + 1 */

The non-centered variants (`FUN_00402f64`, `FUN_00403015`) use the
standard for-loop pattern that stops AT the NUL:

    /* non-centered variant — correct */
    while (buf[local_8] != 0) {
        local_8++;
    }
    /* loop exits with local_8 = first-NUL-index */

The suffix is then appended starting at `buf[local_8]`.  In the
centered case, that's one byte PAST the formatter's terminating NUL
— leaving an embedded NUL between the digits and the suffix.  The
text renderer stops at the first NUL, so the suffix is **never
rendered**.

This is a real bug in retail — the centered render effectively only
shows the formatted number, never the suffix.  It survived for 30
years because every retail callsite passes either an empty suffix or
a single-char suffix (typically `" "`).  The "missing" trailing space
on a centered row is invisible to the eye.

Source-level the two implementations look identical; the divergence
appears to be MSVC 4.20 emitting different loop shapes for what was
probably copy-pasted code with a small textual difference (an extra
`++` operator, a different for-loop init, etc.).  R2 disassembly of
both functions confirms the divergence at the byte level — not a
Ghidra mis-decompilation.

Our port preserves both behaviors as distinct helpers
(`append_suffix` vs `append_suffix_off_by_one`); a regression test
witnesses the embedded NUL on the centered path.

> 📍 `src/gfx_text_label.c`'s `append_suffix` and
> `append_suffix_off_by_one`, Ghidra `FUN_00402f64 @ 0x402f64` /
> `FUN_00403015 @ 0x403015` (correct) vs `FUN_004030c6 @ 0x4030c6` /
> `FUN_00403190 @ 0x403190` (off-by-one).

---

## 46. Compound label+value render clobbers line_x_acc when both inputs are zero

The compound "label + signed value" renderer at `FUN_00402e0c` wraps
its two-half render in a save/reset/restore dance against the global
horizontal accumulator `DAT_005cd404` (`line_x_acc`):

    iVar1 = DAT_005cd404;       /* save */
    DAT_005cd404 = 0;            /* reset (BEFORE the gate) */
    if ((value != 0) || (mode != 0)) {
        /* render label (mutates DAT_005cd404 to label width) */
        /* render value at x + DAT_005cd404 */
        DAT_005cd404 = DAT_005cd404 + iVar1;   /* restore (inside gate) */
    }
    return;

The reset happens BEFORE the early-return check, but the restore lives
INSIDE the active branch.  When both `value` and `mode` are zero, the
function returns with `DAT_005cd404 = 0` — silently clobbering the
caller's running x accumulator.

The non-zero path leaves `DAT_005cd404` at
`(label_width + value_width) + saved`, which is the intended
"increment the caller's accumulator by my combined width" semantic.
The zero path was almost certainly meant to leave `DAT_005cd404`
untouched (i.e. be a transparent no-op) but the early reset and the
gated restore conspire to break that invariant.

Likely a bug.  In practice the gate condition is almost never hit at a
moment where the caller cares about preserving `line_x_acc` across an
all-zero compound render (the captured callsites mostly use the
function as a standalone stat row), but a caller threading
`line_x_acc` through a sequence of compound renders WOULD see their
running advance silently zeroed by an all-zero entry.

Our port preserves the divergence verbatim.  A test witnesses the
clobber by pre-loading `line_x_acc` and asserting it ends up zero
after an all-zero call.

> 📍 `src/gfx_text_label.c`'s `gfx_text_label_render_labeled_signed`,
> Ghidra `FUN_00402e0c @ 0x402e0c`.

---

## 47. Label color tracks value sign even when `mode == 2` neutralizes the value display

`FUN_00402e0c`'s mode parameter has a special value (`2`) that makes
the value half render with a neutral '@' prefix in `color_positive`,
regardless of the value's sign:

    if (mode == 2) {
        render_value(value, '@', color_positive);
    } else if (value < 0) {
        render_value(-value, '-', color_negative);
    } else { /* + or @ for zero, both color_positive */ }

But the label half's color is selected by the value's sign alone — the
mode does not enter the decision:

    if (value < 0) render_label(label, ..., color_negative);
    else           render_label(label, ..., color_positive);

So a call with `value < 0` AND `mode == 2` paints a label in
`color_negative` paired with a value in `color_positive`.  The label
visually says "negative" while the value displays neutrally as `@N`.

Worse: the formatter inside `gfx_text_label_render_dec` still emits
its own sign byte when given a negative value, so the assembled value
buffer is `"@-N"` (not `"@N"`) — the '@' overwrites buffer[0], but the
formatter-emitted '-' survives at buffer[1].  The visible glyph stream
is therefore `@-N` for negative-value-mode-2 — the '-' "leaks" through
even though the mode was apparently intended to suppress sign display.

Whether this is a bug depends on the caller's intent for mode==2.  No
captured callsite invokes the path with a negative value, so the
asymmetric color and the leaked '-' may both be latent — present in
the binary but never triggered.  We preserve them verbatim.

> 📍 `src/gfx_text_label.c`'s `gfx_text_label_render_labeled_signed`,
> Ghidra `FUN_00402e0c @ 0x402e0c` (mode-decision tree at `0x402e7c`
> onward) + `FUN_00402f64 @ 0x402f64` (formatter's sign-byte emission).

---

## 48. Sprite blitter's 4-byte partial unroll fires AT MOST ONCE per row

`FUN_004b43b1` (the unclipped sprite blit kernel) starts each row by
testing `(width & 3) != 0`.  If width is NOT a multiple of 4, the
entire row goes through a byte-at-a-time tail loop — no unrolling at
all.  If width IS a multiple of 4, an unrolled 4-byte block fires
ONCE and then falls into the same byte-at-a-time tail loop for the
remaining `width - 4` bytes:

    do {  /* per-row outer */
        if ((glyph_w & 3) != 0) {
            tail_count = glyph_w;
            goto tail;
        }
        /* one 4-byte unrolled chunk */
        if (src[0]) dst[0] = src[0];
        if (src[1]) dst[1] = src[1];
        if (src[2]) dst[2] = src[2];
        if (src[3]) dst[3] = src[3];
        src += 4; dst += 4;
        tail_count = glyph_w - 4;
        if (tail_count == 0 || glyph_w <= 3) goto end_of_row;
    tail:
        do { copy_one_byte(); } while (...);
    end_of_row:
        dst += dst_pad;
        rows--;
    } while (rows > 0);

A genuine Duff's device would loop the unrolled block until the
remaining count drops below the unroll factor.  This one runs the
unrolled block AT MOST ONCE per row.  For a 16-wide sprite the row
breaks down as `4 unrolled + 12 byte-at-a-time` — the unroll
amortizes nothing for any width above 4.

Almost certainly an MSVC 4.20 codegen artifact rather than a hand-
written optimization.  No callsite is sensitive to the loop shape
(byte-write order doesn't matter for the silhouette overlay
semantics), so the executed memory traffic differs from a uniformly-
unrolled implementation only in instruction count, not in pixels.

Preserved verbatim so cycle counts and instruction-cache behavior
match retail under emulation / instrumented runs.

> 📍 `src/gfx_sprite.c`'s `gfx_sprite_blit`,
> Ghidra `FUN_004b43b1 @ 0x4b43b1`.

---

## 49. Zero-width sprite still writes 4 bytes per row

The unrolled-prefix gate in `FUN_004b43b1` is `(glyph_w & 3) != 0`.
For `glyph_w == 0` this evaluates to `false` (the zero matches the
"multiple of 4" path), so the unrolled body fires unconditionally:

    if (src[0]) dst[0] = src[0];
    if (src[1]) dst[1] = src[1];
    if (src[2]) dst[2] = src[2];
    if (src[3]) dst[3] = src[3];

Four source bytes are read and written per row, regardless of the
"zero width" semantic intent.  The post-unroll gate then computes
`tail_count = -4` (wrapping unsigned) and the tail-loop conditional
short-circuits to `false` (`glyph_w > 3` is false for `glyph_w == 0`),
so the byte-at-a-time tail does NOT compound the over-write.

Latent in retail because the dispatcher (`FUN_00404442`) sets
`y_clip_mode == 5` (skip) before calling — but that gate only fires
when the CLIP rect knocks the sprite entirely off-canvas, not when
the descriptor itself encodes a zero width.  No retail sprite blob
contains a zero-width descriptor, so the path is never exercised.
Preserved as a witness test in `tests/test_gfx_sprite.c` documenting
the on-disk behavior.

> 📍 `src/gfx_sprite.c`'s `gfx_sprite_blit`,
> Ghidra `FUN_004b43b1 @ 0x4b43b1`.

---

## 50. Sprite clipped pair is byte-identical (101 B × 2)

`FUN_004b446d` and `FUN_004b44d2` are 101 bytes each and byte-for-
byte identical — verified with r2 `pxd 101 @ 0x4b446d` vs.
`pxd 101 @ 0x4b44d2` (every int matches across all 26 rows).  Same
pattern as the clipped glyph pair (`FUN_004b425c` / `FUN_004b42c5`),
documented earlier in this file.

The sprite dispatcher (`FUN_00404442`) selects between them by the
x_clip_mode tag:

    if (x_clip_mode == 1) call FUN_004b446d;  /* left-clip case */
    else if (x_clip_mode == 2) call FUN_004b44d2;  /* right-clip */

But since the bodies are identical, the selection has no observable
effect.  The port mirrors the call topology (separate
`gfx_sprite_blit_left_clipped` / `..._right_clipped` entry points)
so retail's call-graph stays recognizable, with both routing to a
shared `sprite_blit_clipped` static helper.

Most likely an MSVC inlining / template-duplication artifact: the
clip path was implemented as a single body, the compiler emitted
one copy for each of two distinct logical entry points (analogous
to the glyph case), and the linker did not deduplicate.

> 📍 `src/gfx_sprite.c`'s `sprite_blit_clipped`,
> Ghidra `FUN_004b446d @ 0x4b446d` + `FUN_004b44d2 @ 0x4b44d2`.

---

## 51. Sprite blitter writes source bytes verbatim — no color modulation

The glyph blitter (`FUN_004b41b7`) honors a `color_modulation` flag
(`DAT_005cd40c`) to choose between two write modes: silhouette fill
(write the bound `color`) or copy-byte (write the source byte
verbatim).  The sprite blitters have NO such gate — every non-zero
source byte is copied verbatim:

    if (*src != 0) *dst = *src;   /* sprite — always copy */

vs. the glyph blitter:

    if (*src != 0) *dst = (color_modulation == 1) ? *src : color;

The implication: sprites are stored pre-colored.  Their on-disk
bytes ARE the palette indices that land on the canvas, byte for
byte.  Glyphs, in contrast, are stored as silhouettes (any non-zero
byte means "lit") with the color chosen at render time.

Note that the GLYPH clipped variants (`FUN_004b425c` / `FUN_004b42c5`)
ALSO ignore color_modulation — they unconditionally write `color`.
The sprite clipped variants behave like their unclipped sibling:
they copy bytes verbatim.  So the two trios diverge on the clipped
write mode:

   glyph trio:   unclipped honors modulation; clipped always color-fills
   sprite trio:  all three always copy bytes

Not a quirk in the "bug-like" sense — just a load-bearing semantic
distinction worth documenting up front, since both trios share the
same staging state and the same dispatcher shape.

> 📍 `src/gfx_sprite.c` vs. `src/gfx_glyph.c`,
> Ghidra `FUN_004b43b1` / `FUN_004b446d` / `FUN_004b44d2`.

---

## 52. Sprite-render wrappers pre-write dst_stride_pad — dead code

All five sibling-of-dispatcher sprite-render wrappers (`FUN_0040a01d`,
`FUN_0040a21a`, `FUN_0040a682`, `FUN_0040a80e`, `FUN_0040a9b0`)
pre-write the dst_stride_pad global immediately before invoking the
horizontal-clip setter:

    _DAT_005cd400 = 0x280 - DAT_005c9258;   /* 640 - sprite_w */
    FUN_0040464c(0, DAT_004eb274);           /* clip_x — overwrites */

`FUN_0040464c` (the clip_x setter) unconditionally writes
`_DAT_005cd400 = DAT_004eb274 - DAT_00591514` at the bottom of its
body — so the pre-write is dead.  If the canvas-width global
`DAT_004eb274` happens to be 0x280 (the literal in the wrappers) AND
the sprite is unclipped (so clipped_w == sprite_w), the two values
agree by coincidence — but the wrapper's pre-write is not load-bearing
under any branch.

Almost certainly an MSVC inlining artifact from an earlier factoring
where the dst_pad was computed at the wrapper level before the clip
helper became reusable.  The 0x280 literal — distinct from the
`DAT_004eb274` global the clip setter reads — is the tell.

Skipped in our port; both source-level branches (port + retail) end
up with the same value in `g_dst_stride_pad` after clip_x runs.  Same
flavor as the label-render `prefix == 0` dead-write quirk: a
pre-store that the next call overwrites unconditionally.

> 📍 `src/gfx_sprite_render.c`,
> Ghidra `FUN_0040a01d` / `FUN_0040a21a` / `FUN_0040a682` /
> `FUN_0040a80e` / `FUN_0040a9b0` vs. `FUN_0040464c`.

---

## 53. Sprite-render descriptor validity gate rejects offset 0

Four of the five sprite-render wrappers gate the staged descriptor's
`src_offset` against the half-open range `[1, 0x4baf1)` (= [1, 310001)).
A descriptor with `src_offset == 0` is rejected as "too low":

    if (DAT_0058fe04 < 0x4baf1) {
      if (DAT_0058fe04 < 1) { /* error */ }
      else { /* dispatch */ }
    } else { /* error */ }

The "too high" sentinel of 310000 is plausible — the largest possible
sprite-blob size the engine allocates (compare to `FUN_004b25b0`).
But the "too low" floor of 1 is curious: zero is otherwise a valid
relative offset inside a blob (the dispatcher itself, `FUN_00404442`,
accepts it).  This wrapper layer specifically rejects it.

Effect: descriptors whose `src_offset` field happens to be zero are
treated as invalid even when the surrounding blob is well-formed.
Almost certainly a "no real sprite starts at offset 0" 1996 assumption
— in a well-formed PL8 file the 8-byte header pushes the first
descriptor's src_offset to at least 8.  Latent footgun: a stripped
or malformed sprite blob with zero offsets surfaces here as a flood
of error-code-4 latches, not silent zero-render.

Preserved verbatim in the port; tests cover both rejection paths.

> 📍 `src/gfx_sprite_render.c`,
> Ghidra `FUN_0040a21a` / `FUN_0040a682` / `FUN_0040a80e` /
> `FUN_0040a9b0`.

---

## 54. Raw-buffer sprite render leaks descriptor flags from prior call

`FUN_0040a01d` (the "render a raw caller-supplied buffer" wrapper)
stages width / height / src_offset / dst_x / dst_y from its own
parameters — but does NOT touch the descriptor-flag globals
(`DAT_00591568`, `DAT_0057d3c0`) or the per-glyph advance byte
(`DAT_005bb478`).  Whatever the previous descriptor-based render
left in those slots carries over into the next blit.

`DAT_005bb478` is harmless — `FUN_0040477b` (clip_y) overwrites it
with the post-clip visible row count before the blitter reads it.
The two flag bytes (`DAT_00591568` / `DAT_0057d3c0`, retail offsets
+0xc and +0xe in a descriptor) have NO current consumer in the
ported code, so the leak is invisible today.

Latent but worth a witness — once the flag readers land (whatever
they are), a `FUN_0040a01d` call interleaved between a sprite render
and a flag-reading caller will surface the wrong flag values.

> 📍 `src/gfx_sprite_render.c`,
> Ghidra `FUN_0040a01d`.

---

## 55. truncate_bottom subtraction is unconditional

`FUN_0040a80e` (sprite render with bottom-row trim) subtracts the
caller's `trim_rows` parameter from the staged `height_short`
unconditionally:

    DAT_005aeb70 = DAT_005aeb70 - param_4;

No clamp against zero, no clamp against the descriptor's natural
height.  A negative `trim_rows` makes the visible height GROW past
the descriptor's natural extent — the blitter would then read past
the end of the source pixel data into whatever follows it in the
blob.  A `trim_rows` larger than the natural height makes
`height_short` go negative, which clip_y treats as "off-canvas"
(skip mode) — silent no-render.

No captured callsite passes a negative or oversized value, and the
visible-height GROW path is gated downstream by clip_y's bounds
intersection anyway (a height past canvas_h gets clipped).  But the
unconditional subtract is the kind of "trust the caller" assumption
the 1996 codebase scatters everywhere.

Preserved verbatim; documented here so it shows up in audits.

> 📍 `src/gfx_sprite_render.c`,
> Ghidra `FUN_0040a80e`.

---

## 56. RLE sprite unclipped opcode 0x0F is folded into the default arm

`FUN_004b4537` (the unclipped RLE sprite blitter) has a 17-arm `switch`
covering opcodes 0x00..0x0E plus 0x10.  Opcode 0x0F has NO explicit
case label — it falls through to the `else` arm.  That arm copies 15
literal bytes and advances src by 16, dst by 15, pixel counter by -15.
This is the SAME behavior the explicit 0x0F arm would have, so for a
well-formed RLE stream the fold is invisible.

The fold becomes visible only for opcodes 0x11..0xFF (which retail
never emits): such an opcode value is also treated as a 15-byte
literal, so the row's pixel accounting drifts (consumed 15 pixels of
budget when the source actually encoded e.g. 17 data bytes).  No known
captured callsite emits a >0x10 opcode, but the misalignment is
reachable in principle.

This is almost certainly MSVC 4.20 codegen optimization — case 0x0F is
the *first* case where the byte-by-byte unrolled copy stops being a
unique pattern (cases 0x05, 0x09, 0x0D end with a single byte after a
4-byte block, so the compiler emits dedicated arms; case 0x0F ends with
a single byte after three 4-byte blocks AND a 2-byte word, which is
the same shape as the default-arm template so the compiler folded
them).

Preserved verbatim in the port — `gfx_sprite_rle_blit`'s `default:`
arm handles both 0x0F and any unknown opcode the same way retail does.

> 📍 `src/gfx_sprite_rle.c`,
> Ghidra `FUN_004b4537`.

---

## 57. RLE left-clip blitter pre-shifts dest pointer LEFT by `left_skip`

`FUN_004b4814` (the left-clipped RLE blitter) computes its per-row
dest origin as `canvas_origin + cursor - left_skip` — i.e. the dest
pointer is shifted LEFT by `left_skip` cells BEFORE the per-row RLE
walk.  The pixel-write gate is then `clip_counter < 1` where
`clip_counter` starts at `left_skip` and decrements by 1 (or by N for
a skip-opcode of N) at every advance — so writes are gated off until
`clip_counter` reaches zero, by which point the dest pointer has
walked forward by `left_skip` cells and lands at the visible canvas
column.

The first family handles left-clip differently: the wrapper pre-bakes
`+ left_skip` into the descriptor's src_offset, then the clipped
blitter starts reading from the post-skip source position with the
dest pointer set to the visible column directly.  The RLE family
can't pre-bake the source position (the byte offset for "skip N
pixels of decoded output" requires walking the RLE stream — variable-
sized opcodes), so it walks the stream from the natural row start
and uses the dest-pointer pre-shift to compensate.

Behaviorally identical to the first family for in-bounds reads, but
note that the pre-shifted dest pointer (`canvas + cursor - left_skip`)
can land BEFORE the canvas's left edge if `left_skip` > `cursor`.
The dispatcher only invokes this blitter when `cursor < canvas_left
< cursor + sprite_w`, so `cursor < canvas_left` implies `cursor -
left_skip = cursor - (canvas_left - cursor) < cursor < canvas_left` —
the pre-shifted pointer can dip below the canvas base, but writes are
gated off there.  Still a footgun: a sufficiently-permissive
out-of-frame-buffer `*dst` read (e.g. an OS guard page) would crash,
but retail's framebuffer is heap-allocated with no guard pages and
the dest pointer is only DEREFERENCED when the write gate fires (at
visible-column positions only).

> 📍 `src/gfx_sprite_rle.c`,
> Ghidra `FUN_004b4814`.

---

## 58. RLE right-clip blitter keeps walking source past visible budget

`FUN_004b48b8` (the right-clipped RLE blitter) tracks two row
counters: `pixels` (natural row width, drives the per-row loop) and
`visible_budget` (= clipped_w, gates writes).  Once `visible_budget`
reaches zero the writes stop, but the source pointer keeps walking
through the remainder of the row's RLE opcodes (because the next
row's RLE start needs to land at the natural row boundary).

In the first family the right-clip variant doesn't have this problem:
the source is densely packed at sprite_w bytes per row, so the row
boundary is at a known byte offset, and the blitter just iterates
`clipped_w` reads and adds `src_stride_pad = sprite_w - clipped_w`
to skip to the next row.  The RLE family can't do that — the byte
offset to the next row depends on the opcode mix, so the walk must
continue past the visible budget.

Behaviorally equivalent to first-family right-clip; preserved
verbatim.  Footgun: an opcode-skip of N decrements `visible_budget`
by N, which can drive it well below zero (e.g. a skip-32 at
visible_budget=4 drives it to -28).  The `> 0` gate still correctly
suppresses writes, so the negative value is harmless.

> 📍 `src/gfx_sprite_rle.c`,
> Ghidra `FUN_004b48b8`.

---

## 59. RLE wrapper does NOT pre-bake skip offsets into src_offset

`FUN_0040a3d0` (the RLE-family caller-blob render wrapper) is byte-
for-byte symmetric to `FUN_0040a21a` (the first-family wrapper)
EXCEPT for a missing two-line skip-baking sequence:

    DAT_0058fe04 = DAT_0058fe04 + DAT_005c9258 * DAT_0058feac;
    DAT_0058fe04 = DAT_0058fe04 + DAT_0058fd50;

These lines are present in `FUN_0040a21a` immediately after the clip
setters.  They bake `top_skip * sprite_w` and `left_skip` into the
descriptor's staged src_offset so the first-family blitters can read
densely-packed raw pixels starting at the first visible cell.

The RLE family CAN'T bake either skip into a byte offset (RLE
opcodes are variable-sized, so a "skip N decoded pixels" doesn't
correspond to a fixed byte advance).  Instead:
  - `top_skip` is consumed by the private `rle_skip_top_rows` helper
    (`FUN_004b495a`) which is called from the prologue of all three
    RLE blitters — it walks the RLE stream past the top rows by
    decoding opcodes without writing.
  - `left_skip` is consumed by the left-clipped blitter directly
    (via the dest pre-shift + write gate described in quirk #57).

Net effect: the two wrappers stage IDENTICAL state for the blitters
they target; the RLE wrappers' missing two lines are exactly the
arithmetic the RLE blitters do internally.  Preserved verbatim.

> 📍 `src/gfx_sprite_render.c` (`gfx_sprite_render_rle_from_blob`),
> Ghidra `FUN_0040a3d0` vs `FUN_0040a21a`.

---

## 60. Map-render uses BYTE offsets into per-tile descriptor table

The per-tile drawer (`FUN_00408438` @ 0x408438) reads two descriptor
fields by treating the LOADED CELL VALUE (`DAT_00567954`) as a byte
offset into the table at `0x522f90`:

    local_28 = (byte)(&DAT_00522f95)[DAT_00567954];   // head-of-list
    flag     = (&DAT_00522f92)[DAT_00567954] & 0x20;  // mode-1 gate

So a cell value of `0x10` reads `desc[0x10 + 5]` and `desc[0x10 + 2]`.
The cells in the 2D map array (`0x55cea0`) thus encode `tile_id *
stride` directly, not `tile_id` — the multiplication is baked in at
map-load time.

Effect: indexing math is "free" inside FUN_00408438, but a malformed
cell value can read FAR outside the per-tile region (the iterators
gate `< 0xfff0000` as the only sanity check).  Tile IDs near that
boundary read uninitialized memory.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> `docs/findings/map-render.md` §"Per-tile descriptor table",
> Ghidra `FUN_00408438` lines 18-19.

---

## 61. Map-render linked-list terminator is self-loop, not zero

The per-tile drawer iterates the linked list of territory records
using each record's next-link field (offset +0x04, short).  The
loop's continuation condition is:

    *(short *)(&DAT_0052f0b4 + local_28 * 0x1a4) == local_28

That is, the loop exits when the next-link points back at the
current record's index — a SELF-LOOP, not a null terminator.
Records linked through valid maps always have a non-zero next-link;
the LAST record's next-link is its own index.

Effect: an in-list record with `next == 0` does NOT terminate the
loop — it advances to slot 0 (the reserved sentinel), which the
loop continues to process.  Engine code must ensure last-record's
next-link is set to its own slot index.  We preserve verbatim.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> Ghidra `FUN_00408438` line 203.

---

## 62. Map-render invalid descriptor aborts ENTIRE linked-list walk

The per-tile drawer's two validation gates (`src_offset > 310000`
and `sprite_w < 1`) both `return;` from the function — NOT just the
current record's iteration:

    if (310000 < DAT_0058fe04) {
        _DAT_0058fea8 = _DAT_0058fea8 + 1;
        return;       // <- exits FUN_00408438 entirely
    }

So a single bad primary OR overlay descriptor in the middle of a
multi-record tile causes all SUBSEQUENT records in that tile's
linked list to be skipped — for that frame.  Two distinct error
counters' worth of bumps are possible per tile (one primary check,
one overlay check) but the second is unreachable past the early
return.

Effect: if one of a tile's army/decoration records has a corrupt
descriptor, the whole tile renders as just its earliest valid
records — undefined visual outcome for that frame, but no crash.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> Ghidra `FUN_00408438` lines 84-87 / 173-176.

---

## 63. Map-render overlay dirty-rect uses POST-clip cursor (1 of 2 marks)

The per-tile drawer fires TWO dirty-rect marks per overlay-bearing
record:

  1. PRE-clip 32-px mark, using the cursor right after the
     overlay-anchor computation (line 147 / 163 of FUN_00408438).
  2. POST-clip-and-dispatch 48-px mark, using whatever cursor the
     clip-x setter ended up writing back (line 199 of FUN_00408438).

If `gfx_dispatch_clip_x` shifts the cursor right (the left-edge
straddle branch), the 48-px mark fires at a DIFFERENT tile-coord
than the 32-px mark.  Both marks expand the running dirty rect, so
the union is correct, but the two marks visit two different tile-
coord cells — observable via call-counting against the dirty-rect
mark helpers.

Equivalent first-sprite (non-overlay) path fires exactly ONE dirty
mark (pre-clip) — quirk noted because the asymmetric mark count
between primary-only and primary-plus-overlay records is observable.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> Ghidra `FUN_00408438` lines 100-108 (32-px / 48-px / 64-px
> selector pre-clip) vs lines 199 (48-px post-clip).

---

## 64. Map-render dead 48-px dirty box in non-overlay path

The per-tile drawer's `local_24 == 2` branch dispatches to the 48-px
dirty-rect helper:

    if (local_24 == 3)      FUN_004524dc(iVar2, iVar1);   // 64-px
    else if (local_24 == 2) FUN_00452489(iVar2, iVar1);   // 48-px ← dead
    else                    FUN_00452436(iVar2, iVar1);   // 32-px

But the only writes to `local_24` in FUN_00408438 set it to 0 (default,
type 0) or 3 (decorated types 1/2/3/4).  Value 2 is never assigned.

So the 48-px mark for the primary sprite is UNREACHABLE.  The 48-px
helper IS reachable via the overlay path's POST-dispatch mark (line
199), but that's a different call site with a different cursor.

Preserved verbatim — branch likely a remnant of an earlier version
or a defensive guard never trimmed by the compiler.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> Ghidra `FUN_00408438` lines 100-108.

---

## 65. Overlay-pass phase0 row has NO left/right edge cells (asymmetric vs primary pass)

The strategic-mode OVERLAY pass at `FUN_00405eb5` (the phase-0 row
iterator) walks exactly `col_count` cells, no more.  Contrast with the
PRIMARY pass's phase-0 row iterator at `FUN_00405862` which walks
`col_count + 2` cells (one extra left-edge cell gated on
`col_start > 0`, plus one extra trailing cell gated on `cur_col < 0x40`).

The two iterators have nearly identical shapes — same camera, same
map array, same per-tile descriptor base — but the OVERLAY iterator
omits both edge-cell paths entirely.  The visible consequence: at the
left/right edges of the map, the territories layer extends one column
further than the units/cities overlay.  This is rarely noticed because
the camera typically positions away from the map edge.

Suspect motivation: edge cells in the primary pass exist to draw
half-visible decorative terrain (mountains, water).  The overlay pass
doesn't need them — armies and cities are always anchored to whole
tiles, never bleeding into half-tiles.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_row_phase0`),
> Ghidra `FUN_00405eb5` (247 B) vs `FUN_00405862` (333 B).

---

## 66. Overlay debug text fires only on middle cells of phase1 row

The debug-overlay text helper `FUN_00408c50` is called between cells in
the row iterators, but its placement is asymmetric:

  - `FUN_00405eb5` (phase-0 row): debug text fires on EVERY non-empty
    cell — straightforwardly inside the column-loop body.
  - `FUN_00405fac` (phase-1 row): debug text fires ONLY on the middle
    cells (the inner column-loop body).  The first cell (phase 1) and
    last cell (phase 2) are dispatched as standalone fragments
    BEFORE/AFTER the loop, and neither fragment calls the debug helper.

This is an artifact of how the phase-1 row is unrolled.  The first
and last cells use a different `phase` argument to the per-tile
drawers, so the loop body proper only handles uniform phase-0 middle
cells.  The debug-text call lives inside that loop body and inherits
its scope.

Observable only when dev flags `DAT_005bb4a4` or `DAT_005bb4a5` are
set — production builds have both at 0.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_row_phase1`),
> Ghidra `FUN_00405fac` (497 B).

---

## 67. Overlay city drawer's territory-cap test is asymmetric for negative sizes

The city drawer at `FUN_004081a6` computes a sprite-size selector
from a per-cell size table:

    local_c = (short *)0x500c30[cell/8] - 1;
    if (max_hi - max_lo < local_c)  local_c = 0;
    DAT_005c9288 = local_c + 0x38;

The cap test fires only when the computed size EXCEEDS the territory's
allowed band.  For negative `local_c` (table value < 1), the cap test
does NOT fire — the negative size passes through unchecked, producing
a sprite index BELOW `0x38` (e.g., `0x37` when table value is 0).

This is a likely source of latent bugs.  Negative sprite indices read
out-of-bounds descriptors in the overlay blob.  Map files that produce
a 0 value in the per-cell table at any cell hosting a city would
trigger this — but in practice the per-cell table seems to never hold
0 at city-bearing cells (no error path observed in retail traces).

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_city`),
> Ghidra `FUN_004081a6` lines 18-22.

---

## 68. Overlay city drawer clears bit 0x40 of descriptor[+2] post-dispatch

After dispatching, `FUN_004081a6` clears bit `0x40` of the per-tile
descriptor's flag byte (`& 0xbf`).  This is mid-frame mutation of
descriptor table state.

The bit is re-set elsewhere — likely by save-load, by city-state
changes, or by a frame-edge "mark all visible" pass.  Whatever the
re-arm path is, this clear is load-bearing: without it, every cell
with a city would re-dispatch every frame, and the bit would never
fall to 0.

Tests must re-arm the bit between frames to verify multi-frame
dispatch behavior.  See
`test_engine_overlay_render_error_count_persists_across_frames`.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_city`),
> Ghidra `FUN_004081a6` line 13.

---

## 69. Overlay pre-clear strip is view-mode-1-only

The driver `FUN_00405602` pre-clears a 4-pixel-wide vertical strip
at column 0, rows `0x18..0x1bc`, by calling `FUN_004b4056` (4-byte
write) in a 420-iteration loop at the very top of the function.  But
this clear is GATED on `DAT_0057cb18 == 1` — only view-mode 1 (zoom-
mid) triggers it.  View-modes 0 and 2 leave the strip undisturbed.

The strip's position corresponds to the leftmost column of the visible
map area in zoom-mid layout.  Suspected purpose: cleaning up stale
pixels from view-mode-transition artifacts that only manifest at this
zoom level.  Could also be a workaround for a clip-rect off-by-one
that only matters when the half-step is exactly 16 pixels (the zoom-
mid setting).

Quirk-worth because the same driver function changes behavior based
on a global mode — typical "load-bearing if" pattern.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_frame` →
> `pre_clear_left_strip`), Ghidra `FUN_00405602` lines 10-14.

---

## 70. Army drawer 0x40 path dispatches stale sprite for unhandled low-nibble

The army drawer's `bit 0x40` branch dispatches sub-branches keyed by
`descriptor[+4] & 0x0f`:

  - low-nibble == 0 → fortress sprite (uses army record's +0x07 byte)
  - low-nibble == 2 → march-out sprite (fixed 0x81)
  - **low-nibble ∈ {1, 3, 4, …, 15}** → **no sprite_idx set, no
    offsets set, BUT the dispatch tail still runs.**

The dispatch tail reads `DAT_005c9288` (the last-glyph-index global)
to stage from the overlay blob.  In the unhandled-nibble case, that
global retains whatever value it held from the most recent staging —
typically a city sprite from the city drawer earlier in the same
frame, or another army sprite from the previous tile.

Result: the cell at (cur_col, cur_row) gets a STALE sprite drawn at
its cursor position (offset 0,0 because dx/dy weren't set either).
Visually surfaces as garbled territory markers when a tile has both
`descriptor[+1] & 0x40` set AND `descriptor[+4] & 0x0f` outside
{0, 2}.

The port mirrors this by pulling `gfx_dispatch_last_glyph_index()`
into the local sprite_idx when no branch supplied one.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_army`,
> stale-sprite fall-through comment), Ghidra `FUN_004071a0` lines
> 265-294 (the `bit 0x40 → else if low_nibble == 0 / 2` chain has no
> default — control falls through to the unconditional dispatch tail).

---

## 71. Army drawer src_off gate is TIGHTER than city / primary pass

Three different overlay-blob src_off validation gates exist for the
same physical blob (`DAT_0055ce5c`):

  - **Primary pass `engine_map_render_tile`**: `src_off ≤ 310000`
    (= `0x4baf0`).
  - **City drawer `engine_overlay_render_tile_city`**: same `≤ 310000`.
  - **Army label sub-renderer `army_render_label` (FUN_00407f82)**:
    same `< 0x4baf1` ⇒ `≤ 310000`.
  - **Army drawer `engine_overlay_render_tile_army` main dispatch
    tail**: `src_off < 0x11171` (= `≤ 70000`) — over 4× tighter.

The army drawer's tighter gate effectively partitions the overlay
blob into a "first 70 KB" region usable by army sprites and a
"first 310 KB" region usable by cities + the army-label sprite
(0x82).  Sprites at offsets in (70000, 310000] are valid for the
city / label paths but flagged as errors by the army drawer.

Practical implication: the overlay blob's first 70 KB must contain
ALL the unit-category sprites (tick+0x28 family, fortress family,
arrow markers, owner-color territory sprites) — the level-edit /
PL8-pack layout MUST honor this boundary.

> 📍 `src/engine_overlay_render.c` (`OVERLAY_ARMY_SRC_OFFSET_MAX` =
> 70000 vs `OVERLAY_SRC_OFFSET_MAX` = 310000),
> Ghidra `FUN_004071a0` line 310 (`< 0x11171`)
> vs `FUN_004081a6` line 41 (`< 0x4baf1`).

---

## 72. End-frame animation byte committed even when dispatch is suppressed

The army drawer's "end-frame" branches (categories 3, 6, 9, 12 and
the 0x10 path with `first == 0x13`) write a fixed end-frame value
to `descriptor[+3]` BEFORE checking the tier byte / tick-cycle gates
that decide whether to dispatch:

```c
descriptors[cell + 3] = END_FRAME_BAND_X;
if (army[tier_off] < 3)             return;   // no dispatch
if (g_animation_tick > 0xf)         return;   // no dispatch
sprite_idx = tick + 0x28;
// → fall through to dispatch
```

A failed gate leaves the descriptor in its end-frame state (0x2e /
0x09 / 0x13 / 0x1d) — meaning the animation "completes" visually
even if the sprite for that frame was never drawn.  On the NEXT
frame the cell will be in a different category band (because the
descriptor[+0] category byte determines which band) — but the
descriptor[+3] frame byte reflects a transition the player never
saw rendered.

This is observable as units that appear to "skip" the last frame of
their animation when the army-record's tier byte hasn't ramped up
to the dispatch threshold yet.

> 📍 `src/engine_overlay_render.c` (`army_end_frame_band`),
> Ghidra `FUN_004071a0` lines 60-71 (cat 3),
> 98-109 (cat 6), 136-147 (cat 9), 174-185 (cat 12).

---

## 73. Arrow-marker sub-branch silently filters 0x13 via byte underflow

The army drawer's `bit 0x20` (no 0x40, no 0x10) directional-arrow
branch is range-gated to `first_byte ∈ [0xf, 0x16]`, then split at
0x13 into low (0x10..0x12) and high (0x14..0x16) sub-branches.  The
high sub-branch's inner gate is:

```c
if (2 < (byte)(bVar2 - 0x14)) return;
```

When `bVar2 == 0x13`, `bVar2 - 0x14` underflows in the byte cast to
`0xff` — well over 2 — so the gate trips and the function returns.
Net effect: 0x13 is FILTERED by the upper sub-branch via byte
arithmetic underflow, not by an explicit equality check.

The port replicates this faithfully via a signed `delta < 0 || delta
> 2` test, matching the same outcome without relying on
implementation-defined unsigned wrap.

Practical implication: the 0x13 value of `first_byte` is reserved by
the OTHER `bit 0x10` branch (animation-end path) — having both arms
silently filter it keeps the descriptor reach unambiguous.

> 📍 `src/engine_overlay_render.c`
> (`engine_overlay_render_tile_army`, 0x20 high sub-branch),
> Ghidra `FUN_004071a0` lines 240-246.

---

## 74. Animation-tier flag is a GLOBAL toggle, not a per-army timer

The four animation-rate "tier" flags (`DAT_0058fd08`, `DAT_0057d3c8`,
`DAT_0057d3ac`, `DAT_00591510`) are SINGLE GLOBAL booleans, not
per-army or per-tile state.  The army drawer's step categories
(0, 2, 5, 8, 11) compute `distance = army[cur] - army[prev]` per
tile, then pick a tier based on `distance` bin — but the tier flag
itself is a frame-level toggle that bumps EVERY army in that
distance bin uniformly.

When the global tier flag is non-zero, every army record matching
that distance bin advances its frame by 1 this iteration.  When the
flag is zero, none of them advance.  The "distance" computation
exists only to BIN armies into rate tiers; the actual time-base is
shared.

The four tiers map to the 640/320/160/80-ms phase ticks (in that
order) — small distance gets the slow 640 ms cadence, large
distance gets the fast 80 ms cadence:

  distance <  10   → tier 0 → `DAT_0058fd08` (640 ms — slowest)
  distance < 0x19  → tier 1 → `DAT_0057d3c8` (320 ms)
  distance < 0x32  → tier 2 → `DAT_0057d3ac` (160 ms)
  distance ≥ 0x32  → tier 3 → `DAT_00591510` ( 80 ms — fastest)

So slow-moving armies (small cur-prev gap) cycle their sprite
sequence more slowly than fast-moving ones — visually correct
gait: a marching column at walking pace flickers its frames less
often than a unit at full speed.

The four "tier" globals are produced by `FUN_004bbc80` (the
9-flag animation-phase tick generator, ported as
`engine_compute_phase_ticks` in `src/engine_phase.c`).  The
Win32-coupled wrapper `engine_tick_phase_step` writes the four
outputs straight through to
`engine_overlay_render_set_animation_rate_flags` each main-loop
iteration — so once the wider main loop runs the army drawer in
production, no extra wiring is needed.  (Originally filed as
"the driver lives elsewhere" before we realized the phase-tick
ports were the same generator.)

> 📍 `src/engine_overlay_render.c`
> (`engine_overlay_render_set_animation_rate_flags`,
> `pick_rate_tier`), Ghidra `FUN_004071a0` lines 41-54
> (and parallels at 80-91, 119-130, 156-167).  Driver:
> `src/engine_phase.c` (`engine_compute_phase_ticks`),
> `src/engine.c` (`engine_tick_phase_step`),
> Ghidra `FUN_004bbc80` @ 0x4bbc80.

---

## 75. 16×16 raw sprite blit has a HARDCODED 640-byte dest row stride

`FUN_004b49f7` (ported as `gfx_sprite_blit_16x16_raw`) computes its
destination row stride from compile-time dword indices, NOT from the
runtime canvas pitch.  Each row writes via `puVar2[0xa0 * row + col]`
where 0xa0 dwords = 640 bytes — the standard 640×480 back-buffer
pitch.  In contrast, the destination *origin* is computed from
`DAT_004ea1a8 + DAT_00591524 + DAT_00591528 * DAT_004eb274` — which
DOES read the dynamic pitch global.

So the origin respects whatever pitch is bound, but the per-row
stride is locked.  If the bound canvas pitch is anything other than
640, rows past the first land at offsets that DO NOT line up with the
canvas's actual scanline structure — pixels written by the blit
would overwrite arbitrary buffer locations.

The function works correctly in retail because retail never binds a
non-640 canvas at this primitive's call sites (battlefield terrain
renderer and the 5 UI helpers at 0x40920..0x4097a).  The mismatch is
a latent footgun, preserved verbatim in the port (16 rows × 16 byte
memcpy with literal `dst += 640` between rows).  Tests use a 640-
wide canvas to match retail's deployment.

> 📍 `src/gfx_sprite.c` (`gfx_sprite_blit_16x16_raw`),
> Ghidra `FUN_004b49f7` @ 0x4b49f7 (705 B).

---

## 76. Battlefield terrain blob selector is strict `if/else-if` — non-{0,4} silently skips dispatch

`FUN_004bc8fc` (zoom-16 terrain pass, ported as `pass_terrain_zoom16`
inside `engine_battlefield_render`) picks a sprite blob by masking the
per-cell flags byte with `0x1c` (= bits 2..4):

```c
bVar1 = bVar1 & 0x1c;
if (bVar1 == 0) {           /* primary blob → FUN_004b49f7(DAT_004e5d78) */
    ... stage descriptor + dispatch ...
}
else if (bVar1 == 4) {      /* alt blob → FUN_004b49f7(DAT_004e6560)   */
    ... stage descriptor + dispatch ...
}
/* else: silently fall through; NO blit dispatch */
```

There is NO `else` clause.  Bits `0x08`, `0x0c`, `0x10`, `0x14`, `0x18`,
`0x1c` and any combination of bits 2..4 outside the literal values
`0` and `4` will silently skip the blit while STILL:

  - clearing the dirty bit (descriptor[+2] &= 0xfe),
  - reading the sprite-index byte (descriptor[+3]),
  - running boundary marking (descriptor[+2] |= 0x02 on edge cells).

Result: when a cell's flag byte lands in the "no blob" band, the cell
appears to be "redrawn" from the descriptor's perspective (dirty
clear + boundary mark) but the screen pixels never change.  Stale
content from the previous frame persists.

This works in retail because the level data only ever produces flag
values whose `& 0x1c` is `0` or `4`.  Anything else is a data-corruption
or scribble-on-descriptor symptom — the strict chain is effectively a
silent assertion.  The port preserves the strict shape (no default
`else`) so the same data corruption produces the same symptom.

The zoom-32 sibling (`FUN_004bcbdc`) DOES have the same shape —
confirmed by chip 4 (the zoom-32 port).  Same `if (== 0) { primary }
else if (== 4) { alt }` with no default; non-{0,4} selectors skip the
FIRST sprite but the second-sprite path is independent and runs
regardless.

> 📍 `src/engine_battlefield_render.c` (`pass_terrain_zoom16`,
> `pass_terrain_zoom32`),
> `docs/findings/battlefield-render.md` §"Storage layout",
> Ghidra `FUN_004bc8fc` @ 0x4bc8fc, `FUN_004bcbdc` @ 0x4bcbdc.

---

## 77. Battlefield zoom-32 cell ABORT skips boundary marking too

`FUN_004bcbdc` (zoom-32 terrain pass, ported as `pass_terrain_zoom32`
inside `engine_battlefield_render`) has THREE distinct cell-disposal
paths — and only ONE of them runs the boundary-marking step.  This
differs structurally from the zoom-16 sibling (`FUN_004bc8fc`), where
boundary marking ran for EVERY cell regardless of dirty/clean.

The three paths (mapped to retail's two if/else-if blocks):

1. **Clean cell** (`force_redraw == 0 && (flags & 1) == 0`) — go
   straight to the boundary block, then advance.  Boundary marking
   runs.

2. **Dirty (or forced) cell with skip-bit clear** (`(flags & 1) != 0`
   OR `force_redraw != 0`, AND `(descriptor[+1] & 4) == 0`) — clear
   dirty bit, run first-sprite + second-sprite dispatch, THEN go to
   the boundary block, then advance.  Boundary marking runs UNLESS
   one of the abort gates fires (see below).

3. **Dirty + skip-bit set** (`(descriptor[+1] & 4) != 0`) — go
   directly to the advance, SKIPPING the boundary block entirely.
   Boundary marking does NOT run.

Within path 2, three abort gates each `goto LAB_004bcc53`, jumping
straight to the advance and SKIPPING boundary marking:

  - First-sprite primary path: `if (310000 < DAT_0058fe04 ||
    DAT_0058fe04 < 1) goto LAB_004bcc53;` — out-of-band src offset
    aborts.
  - First-sprite alt path: same gate.
  - Second-sprite path: `if (bVar2 == 0 || 3 < bVar2) goto
    LAB_004bcc53;` — second-sprite mode byte (descriptor[+4]) outside
    {1, 2, 3} aborts.

The result: a cell whose first-sprite descriptor has a corrupted
src_offset, OR whose second-sprite mode byte is out of band, will
NEITHER be drawn NOR get its boundary bit set.  The dirty bit IS
cleared (that happens BEFORE the gate), so on the next frame the
cell stays clean — meaning the corruption silently propagates as
unredrawn pixels, with no boundary-cell re-paint trick to recover it
on scroll.

```c
/* retail FUN_004bcbdc shape:                                       */
if ((force_redraw == 0) && ((flags & 1) == 0)) {
    /* CLEAN cell                                                   */
LAB_004bcfa3:
    /* boundary marking                                             */
    if (col == 0) flags |= 2; else if (row == 0) flags |= 2; ...
}
else if ((desc[+1] & 4) == 0) {
    /* DIRTY + skip-bit clear                                       */
    desc[+2] &= 0xfe;
    /* ... first sprite ... */
    if (off > 310000 || off < 1) goto LAB_004bcc53;  /* abort       */
    /* ... second sprite ... */
    if (mode == 0 || mode > 3) goto LAB_004bcc53;    /* abort       */
    goto LAB_004bcfa3;                               /* run boundary*/
}
/* DIRTY + skip-bit set: fall through (no boundary, no draw)        */
LAB_004bcc53:
    /* per-cell advance */
```

The port mirrors the three-path structure via a local `do_boundary`
flag rather than goto, but the behavior is byte-equivalent.

> 📍 `src/engine_battlefield_render.c` (`pass_terrain_zoom32`),
> Ghidra `FUN_004bcbdc` @ 0x4bcbdc.

---

## 78. Battlefield zoom-32 second-sprite index has a clamp at slot 0x9a

`FUN_004bcbdc`'s second-sprite-per-cell path picks its sprite slot
from `descriptor[+0]` (the byte read into `bVar1` in retail) with a
strict clamp:

```c
if (bVar1 < 0x10) {
    DAT_005c9288 = bVar1 + 0x8b;   /* slot in [0x8c, 0x9b)         */
}
else {
    DAT_005c9288 = 0x9a;           /* clamped sentinel              */
}
```

So `descriptor[+0]` values in `[1, 0x10)` (i.e. 1..15) map to slots
`[0x8c, 0x9b)` (140..154) — fifteen per-byte alternates.  Value 0x0f
(15) lands on slot 0x9a (154), the SAME slot the `>= 0x10` clamp
defaults to.  Values 0x10..0xff all also collapse to slot 0x9a.

This means slot 0x9a is the "default overlay" sprite that the engine
falls back to whenever the per-cell selector byte is either at its
upper-band boundary or above it.  The fifteen-slot alternate band
(0x8c..0x99) is exclusively per-byte selection; slot 0x9b and above
in that range is unused by this dispatch path.

Asymmetry vs. a more obvious `(bVar1 - 1) & 0xf` mask: this is a
*saturating clamp*, not a wrap.  A descriptor with a corrupted high
value never indexes outside the slot table; it just always renders
the same default sprite.  Effectively a silent failsafe — the engine
keeps rendering *something* rather than dereferencing past the slot
table.

> 📍 `src/engine_battlefield_render.c` (`pass_terrain_zoom32`),
> Ghidra `FUN_004bcbdc` @ 0x4bcbdc, lines 72-77 of decompilation.

---

## 79. Battlefield zoom-32 layer bit `descriptor[+1] & 4` is the SAME bit used by two opposing passes

The battlefield zoom-32 cluster reads `descriptor[+1] & 0x04` in BOTH
the terrain pass (`FUN_004bcbdc`) AND the overlay pass (`FUN_004bd07f`)
to decide whether to draw — but with opposite polarity:

  - Terrain-z32: SKIPS the cell when the bit is set (EARLY-SKIP gate).
  - Overlay-z32: REQUIRES the bit to draw (layer gate).

Net effect: per cell, at most ONE of the two zoom-32 passes runs the
drawing block in a given frame, picked by the bit.  Cells flagged
with the bit are "feature overlays" that replace what would have been
terrain in that cell.  Cells without the bit get terrain.  Neither
pass touches the dirty bit unless it actually draws; cells that are
clean AND have a clear layer bit get neither drawing nor a dirty
clear from either pass.

This explains the apparent asymmetry of terrain-z32's quirk #77 (cell
abort skips boundary marking on a dirty+skip-bit cell): the cell ISN'T
abandoned — overlay-z32 picks it up next.

> 📍 `src/engine_battlefield_render.c` (`pass_terrain_zoom32`,
> `pass_overlay_zoom32`), Ghidra `FUN_004bcbdc` (line 376 of
> decomp) and `FUN_004bd07f` (line 16 of decomp).

---

## 80. Battlefield zoom-32 glyph_offset gate constant is 0x4baf0, NOT 310000

Ghidra's decomp shows the `[1, 310000]` glyph_offset gate in
`FUN_004bcbdc` as a decimal literal, but the underlying retail
constant is `0x4baf0` = 309488.  Confirmed via r2 at 0x4bcd76:

```
0x4bcd76      cmp dword [0x58fe04], 0x4baf0
0x4bcd80      jle 0x4bcd8b           ; offset <= 0x4baf0 → draw
0x4bcd86      jmp 0x4bcc53           ; abort
```

The same constant appears in the overlay pass (`FUN_004bd07f` @
0x4bd1ef) and presumably elsewhere — it's likely the size of the
largest blob the engine accepts (~302 KB).

OpenLords2's chip-4 port initially used 310000 verbatim from the
decomp — a 512-value off-by-one that would have accepted bad glyph
offsets in `[309489, 310000]`.  Fixed in this commit while landing
chip 5b.  Lesson: when Ghidra shows a "round" decimal literal as a
cmp immediate, verify against the disassembly — Ghidra normalizes
hex to decimal aggressively and the rounding direction is not
documented.

> 📍 `src/engine_battlefield_render.c` (chips 4 + 5b), retail
> Ghidra `FUN_004bcbdc` + `FUN_004bd07f`, r2 disasm at 0x4bcd76 /
> 0x4bd1ef.

---

## 81. Battlefield zoom-32 overlay pass clears dirty bit even when blit aborts

`FUN_004bd07f` clears `descriptor[+2] & 1` BEFORE checking the
`[1, 0x4baf0]` glyph_offset gate.  An out-of-band glyph offset
therefore leaves the cell with:

  - Dirty bit CLEARED (cell appears "redrawn" to subsequent frames).
  - No pixels written to the canvas.
  - No boundary mark (this pass has none).

So a corrupted blob slot for a layer-bit cell silently disappears:
the cell shows whatever stale pixels were under it, and no recovery
mechanism re-marks it dirty.  Unlike terrain-z32's analogous gate
(quirk #77), which also skips boundary marking — overlay-z32 doesn't
mark boundaries at all so this is a structural rather than
configurable property.

The terrain-z32 sibling (`FUN_004bcbdc`) also clears dirty before
the gate; the difference is that terrain has a boundary-marking
block that gets skipped on abort, whereas overlay has none.

> 📍 `src/engine_battlefield_render.c` (`pass_overlay_zoom32`),
> Ghidra `FUN_004bd07f` (lines 17-29 of decomp).

---

## 82. Battlefield left-biased screen→hex clamp is asymmetric (high only)

The five-helper coordinate family at the head of the battlefield
cluster includes two "neighbor-biased" variants of the basic screen→hex
column converter:

  - `FUN_004bc2e5` — right-biased (residue ≥ band*3 → col+=1).
    Clamps low: `col < 1 → col = 0`.
  - `FUN_004bc346` — left-biased  (residue < band  → col-=1).
    Clamps high: `col >= row_stride → col = row_stride - 1`.

Neither variant clamps both sides.  In particular, the left-biased
function can return NEGATIVE column indices when the input pixel is
near the left edge of the grid AND falls in the first sub-band — a
double-shift that's not blocked by any low clamp.  Callers must handle
out-of-range columns themselves; the helpers are not safe to use as a
"clip into grid" primitive.

Real-world effect on input handling: a click 7 pixels left of the
viewport but within the visible cell row evaluates to column -1, which
will index past the descriptor table on the LOW side if used as-is.
The right-biased variant has the OPPOSITE asymmetry — it clamps low
but not high, so out-of-band column indices on the high side propagate
freely.  Lesson: don't substitute either variant for the other or
combine clamps; they're carefully tuned to their respective callers
(unsurveyed).

> 📍 `src/engine_battlefield_render.c` (`pixel_to_col_left_biased`,
> `pixel_to_col_right_biased`), Ghidra `FUN_004bc346` + `FUN_004bc2e5`.

---

## 83. Battlefield middle pass iterates an OVER-SCAN extension of the visible grid

The middle pass (`FUN_004bd355`, chip 5a.i) does NOT iterate the visible
grid like its terrain / overlay siblings.  Instead it walks an extension
that starts ONE cell west / ONE cell north of the camera and ends up to
TWO cells past the south-east corner — a `(cols + 2) × (rows + 2)`
window when no clamp fires, reduced near grid edges by clamps against
the underlying grid extent (`camera_row_stride` for cols, `total_rows`
— a new global `DAT_004e6570` — for rows).

```c
local_10 = DAT_004e657c - 1;            // start_col = cam_x - 1
local_c  = DAT_004e6558 - DAT_004e656c; // start_px_x = viewport_x - zoom
if (local_10 < 0) { local_10 = 0; local_c += DAT_004e656c; }  // west cancel
local_18 = DAT_004e6580 - 1;            // start_row = cam_y - 1
local_20 = DAT_004e6554 - DAT_004e656c; // start_px_y = viewport_y - zoom
if (local_18 < 0) { local_18 = 0; local_20 += DAT_004e656c; }  // north cancel
local_2c = DAT_004e5d5c + 2;            // num_cols = grid_cols + 2
if (DAT_004e6578 <= local_2c + local_10) local_2c = DAT_004e6578 - local_10;
local_28 = DAT_004e5d4c + 2;            // num_rows = grid_rows + 2
if (DAT_004e6570 <= local_28 + local_18) local_28 = DAT_004e6570 - local_18;
```

Why the asymmetry (+1 west / +2 east, +1 north / +2 south)?  The
middle pass renders Y-sorted entities (units/effects) plus per-cell
extras whose sprites can be **larger than one hex cell**.  An entity
anchored at a hex column adjacent to the visible viewport on the WEST
side can still have its right edge visible inside the viewport; the
middle pass needs to iterate that off-screen cell to draw the
overlapping portion.  Similarly on the east / south, but those need
+2 cells of slack rather than +1 because the entities are drawn at
cell-origin-PLUS-offset positions that can poke up to ~32 px past the
anchor, which can span TWO cells at zoom 16.  Terrain and overlay
passes don't have this problem because their sprites are exactly one
cell, so they iterate only the visible cells.

A separate consequence: the west / north start cells can have
NEGATIVE pixel coordinates relative to the viewport's top-left
(`start_px_x = viewport_x - zoom`).  Per-cell extras that draw at
those coords rely on the clip rect to suppress the off-screen
portions — quirk on its own that gates a future port (chip 5a.iii).

The `DAT_004e6570` global is otherwise unbound by terrain / overlay,
so it's a NEW binding introduced in chip 5a.i
(`engine_battlefield_render_bind_total_rows`).

> 📍 `src/engine_battlefield_render.c` (`pass_middle`),
> `docs/findings/battlefield-render.md` §"What's next",
> Ghidra `FUN_004bd355` lines for the local_10/c/18/20/2c/28 setup.

---

## 84. Battlefield middle pass re-dirties cell only on player-marker branch

The middle pass's per-cell dispatch on `descriptor[+2] & 0x80` has
TWO branches that differ in ONE crucial side-effect:

```c
if (descriptor[+2] & 0x80) {
    if (descriptor[+3] == 0) {
        FUN_004bd574();                       // player marker
        descriptor[+2] = descriptor[+2] | 1;  // RE-DIRTY (!)
    } else {
        FUN_004bd759(descriptor[+3]);          // unit-key sprite
        /* no re-dirty */
    }
}
```

Only the **player-marker branch** sets bit 0x01 (re-dirty) on the
flag byte.  The unit-key branch does NOT.

Why: the player marker is an **animated sprite** that cycles through
8 frames driven by an internal counter (`DAT_004e5b18`, advanced once
per call to `FUN_004bd574`).  Re-dirtying the cell ensures the terrain
pass picks it up next frame and re-paints the cell, which causes the
middle pass to re-render the marker with the next animation frame.
Without the re-dirty the cell would only re-render when something else
marked it dirty, and the animation would freeze.

The unit-key sprite is static per frame (no animation counter), so it
doesn't need the re-paint loop.  The unit-key branch is also called
with a SPECIFIC byte from `descriptor[+3]` — only four values (0x49,
0x4c, 0x61, 0x64) are valid; anything else is silently dropped by the
leaf (separate quirk to file when chip 5a.iii lands).

Caveat: the re-dirty bit is ORed into the flag byte READ AFTER the
leaf returns (retail: `*(byte *)(...) = *(byte *)(...) | 1`).  If the
leaf mutates the same byte (e.g. clearing a different bit), the
re-read picks up that mutation.  For chip 5a.i the leaf is a stub
that doesn't mutate, but the verbatim port preserves the re-read
semantics for chip 5a.iii.

> 📍 `src/engine_battlefield_render.c` (`pass_middle`),
> `docs/findings/battlefield-render.md` §"What's next",
> Ghidra `FUN_004bd355` lines 50-56.

---

## 85. Battlefield visible-entity build filter window is INCLUSIVE on both ends

The visible-entity build helper (`FUN_004bd938`, chip 5a.ii.a) walks
the entity table for ids 1..0x50 and includes each alive entity whose
hex coords fall in the window:

```c
ex ∈ [cam_x - 1, cam_x + cols]    (inclusive both ends)
ey ∈ [cam_y - 1, cam_y + rows]    (inclusive both ends)
```

The retail source confirms inclusive bounds on the RIGHT/BOTTOM end:

```c
if (((((&DAT_005544ac)[id*0x1b0] != 0) &&
     (iVar1 <= *(short *)(&DAT_005544a0 + id*0x1b0))) &&
    (*(short *)(&DAT_005544a0 + id*0x1b0) <= iVar2)) &&  // <= (NOT <)
   ((iVar3 <= *(short *)(&DAT_005544a2 + id*0x1b0) &&
    (*(short *)(&DAT_005544a2 + id*0x1b0) <= iVar4)))) { // <= (NOT <)
```

Both `<=` checks are inclusive, so an entity at the EXACT north / south
or east / west bound coord is part of the visible list.  The terrain /
overlay passes' cell loop has the same inclusive shape on its
over-scan extent (`(cam_x - 1) .. (cam_x - 1) + (cols + 2) - 1 =
cam_x + cols`), so an entity at `cam_x + cols` is reachable by the
cell loop as well — the two ranges match exactly.

What this means in practice: an entity anchored exactly one cell
past the visible viewport's east edge IS included in the visible
list AND iterated by the cell loop.  This is intentional — those
entities can have sprite pixels reaching back into the visible
viewport.

The build helper does NOT clamp the filter window against the grid
extent the way the cell loop does (clamping against `camera_row_stride`
for cols, `total_rows` for rows).  An entity outside the grid extent
can't exist in practice (the simulator places entities on cells), so
the missing clamp is observably equivalent.  Keeping the un-clamped
form lets the build helper short-circuit a few comparisons per id.

> 📍 `src/engine_battlefield_render.c` (`middle_leaf_build_entity_list`),
> `docs/findings/battlefield-render.md` §5a.ii.a,
> Ghidra `FUN_004bd938` lines 13-19.

---

## 86. Battlefield visible-entity sort uses an in-band sentinel as right-edge max

The bubble sort (`FUN_004bda92`, chip 5a.ii.a) walks pairs `(i, i+1)`
for `i = 0 .. high - 1`.  On the final pass with `high = count - 1`,
the inner loop reads `y[count]` — which is the IN-BAND SENTINEL that
the build helper writes at index `count` (id=0, y=9999):

```c
/* End of FUN_004bd938 */
(&DAT_004e5d82)[DAT_004e5d38 * 2] = 9999;   // y sentinel
(&DAT_004e5d80)[DAT_004e5d38 * 2] = 0;      // id sentinel
```

The sentinel value 9999 is chosen to exceed any realistic y coord
(the battlefield grid is on the order of tens of cells tall), so the
comparison `y[count] < y[count-1]` always fires false and no swap
happens at the sentinel boundary.  Net effect: the sort behaves
identically with or without the sentinel — its purpose is to keep the
read at `y[count]` defined for the loop bound `i < high`.

Subtle property: the sort uses STRICT `<` (`y[i+1] < y[i]`) for the
swap condition, so equal-y entries retain their input order — the
sort is effectively stable for ties.  The render loop walks the sorted
list in that order, which matters when two entities share a y coord
(typically one cell stacked on another) — the rendering order is
determined by entity id ascending (since the build helper walks ids
1..0x50 in order).

The 500-iter outer safety cap on the bubble sort is purely defensive:
with `max_id = 0x50 = 80` the worst case is 79 outer passes, well
under the cap.

> 📍 `src/engine_battlefield_render.c` (`middle_leaf_build_entity_list`,
> `middle_leaf_sort_entity_list`),
> `docs/findings/battlefield-render.md` §5a.ii.a,
> Ghidra `FUN_004bd938` lines 28-29; `FUN_004bda92` lines 10-21.

---

## 87. Battlefield per-entity draw type-6 PRE-call fires even when blob is NULL

The per-entity draw `FUN_004bdc31` (chip 5a.ii.b) has a strict
top-to-bottom order:

```c
if (entity[+0x12] == 6) FUN_004be4df(id);    // pre-call (TYPE-6)

blob = entity[+0x00];
if (blob == 0) {
    FUN_004afab9(...);                       // log "no blob"
    return;                                   // <— skips type-7/9 too
}
... main draw, post-calls ...
```

The type-6 PRE-call is **outside** the no-blob gate.  An entity of
type 6 with a NULL primary sprite blob still gets `FUN_004be4df`
called — only the main draw + type-7 + type-9-subtype-0xe post-calls
are skipped.

Why this matters: `FUN_004be4df` is the SECONDARY draw (likely a
shadow / ground decal for type-6 units).  It uses a DIFFERENT blob
field (`entity[+0x04]`, the secondary blob), so even when the primary
blob is missing the secondary can still render.  Treating the
PRE-call as part of the main-draw gate would silently lose shadows
for entities whose primary asset failed to load.

The type-7 / type-9-subtype-0xe POST-calls are different — they sit
**inside** the glyph-offset gate AND inside the no-blob else branch,
so an out-of-band glyph offset OR a missing primary blob suppresses
all three.  The asymmetry between PRE-call (loose gate) and POST-calls
(strict gates) is consistent: secondary draws survive primary
failures; type-specific overlays only fire when the primary draw
succeeded.

> 📍 `src/engine_battlefield_render.c` (`middle_leaf_draw_entity`),
> `docs/findings/battlefield-render.md` §5a.ii.b,
> Ghidra `FUN_004bdc31` lines 11-14.

---

## 88. Battlefield per-entity draw reads uninitialised stack at unsupported zoom

`FUN_004bdc31` (chip 5a.ii.b) selects the per-entity animation-offset
table based on zoom:

```c
if (DAT_004e656c == 0x20) {
    local_1c = ...DAT_004e4030[facing*0x88 + frame*8]...;
    local_24 = ...DAT_004e4034[facing*0x88 + frame*8]...;
} else if (DAT_004e656c == 0x10) {
    local_1c = ...DAT_004e3bf0[facing*0x88 + frame*8]...;
    local_24 = ...DAT_004e3bf4[facing*0x88 + frame*8]...;
}
DAT_00591524 = DAT_00591524 + local_1c;   // <— uses local_1c
DAT_00591528 = DAT_00591528 + local_24;   // <— uses local_24
```

There is NO else arm — if `DAT_004e656c` is neither `0x10` nor `0x20`,
both `local_1c` and `local_24` are READ uninitialised on the stack.
The 1996 MSVC codegen leaves them as whatever the calling convention
junk happens to be, and the cursor is then shifted by garbage.

Why this is unreachable in retail: the frame driver's terrain /
overlay pass dispatch ALSO has no else arm on the same zoom check —
unsupported zooms skip both passes.  The game never sets zoom to
anything but `0x10` or `0x20`.  But the middle pass dispatches
unconditionally on zoom, so this gate is technically alive.

The port initialises both offsets to 0 in the missing-else arm.
Deterministic behaviour at unsupported zoom; observationally
equivalent in retail because the unsupported-zoom path doesn't
exist.

> 📍 `src/engine_battlefield_render.c` (`middle_leaf_draw_entity` — the
> `g_zoom == 0x20 / 0x10` chain), `docs/findings/battlefield-render.md`
> §5a.ii.b, Ghidra `FUN_004bdc31` lines 21-39.

---

## 89. Battlefield per-entity dirty-rect box size threshold uses entity type, not sprite size

`FUN_004bdc31` (chip 5a.ii.b) marks a dirty box AFTER applying the
anim-offset + type-9/type-10 Y bonus but BEFORE dispatching the blit.
Box size is picked by entity TYPE, not sprite dimensions:

```c
if ((byte)(&DAT_00554492)[id*0x1b0] < 7) {
    FUN_004527a6(cursor_x, cursor_y, 5, 5, 2);   // 5×5 tile box
} else {
    FUN_004527a6(cursor_x, cursor_y, 9, 9, 2);   // 9×9 tile box
}
```

The 5×5 box covers 80×80 pixels (5 tiles × 16 px); the 9×9 box
covers 144×144 pixels.  Types 0..6 (units?) get 5×5; types ≥ 7
(heroes / siege engines / larger entities) get 9×9.

A few subtle properties:

- The box size is hardcoded.  Sprites of varying actual size all
  trigger the same dirty extent — over-marking is preferred over
  under-marking (the blit might write outside the 5×5 box and still
  be safely re-painted next frame because the box is conservative).
- The escalation flag is `2` (forces full-repaint behaviour), not
  the default `1`.  An entity's per-frame mark always escalates
  `dirty_mark` to 2 — even if the rest of the frame only marked at
  level 1.
- The box is anchored at the FULLY-ADJUSTED cursor (anim offset +
  type-9/10 Y bonus applied), NOT at the entity's hex-aligned
  anchor.  Off-by-one bugs in anim tables would shift the box
  accordingly.

> 📍 `src/engine_battlefield_render.c` (`middle_leaf_draw_entity`),
> `docs/findings/battlefield-render.md` §5a.ii.b,
> Ghidra `FUN_004bdc31` lines 47-52.

---

## 90. Battlefield effect-chain glyph_offset gate aborts the WHOLE chain walk

`FUN_004beed4` (chip 5a.iii) walks a linked list of visual effect
records anchored to a battlefield cell.  Per record, it gates the
sprite blit on the `[1, 0x4baf0]` glyph_offset window — same gate that
guards the per-cell terrain/overlay/per-entity paths.  But here the
gate's failure path is a `return;` rather than a `continue;` — the
whole function exits, abandoning any unwalked records remaining in the
chain:

```c
if ((310000 < DAT_0058fe04) || (DAT_0058fe04 < 1)) {
    return;       // exits FUN_004beed4 entirely — even if next-id is non-zero
}
```

(The decompiler renders the constant as 310000; r2 confirms it's
actually 0x4baf0 = 309488, same as everywhere else.)

Net effect: a single mis-loaded effect record at position N silently
prevents the rest of the chain from rendering this frame.  The
`current_id` global isn't even updated to walk past the bad record
— so on the NEXT frame, if `current_id` still points at the bad
record, the chain restart hits the same gate and the same chain-
abandonment.  Effects "behind" a bad record stay invisible until the
bad record's metadata clears.

The chain-abort also skips the saved-cursor restore epilogue
(`DAT_00591524 / DAT_00591528 = saved`), but since the cursor isn't
written before the gate check (cursor write sits AFTER the gate), this
particular missing-restore is observably a no-op.

> 📍 `src/engine_battlefield_render.c` (`middle_leaf_draw_effect_chain`),
> `docs/findings/battlefield-render.md` §5a.iii,
> Ghidra `FUN_004beed4` lines 50-51.

---

## 91. Battlefield player_marker / unit_key re-center cursor by sprite WIDTH on both axes

`FUN_004bd574` (player marker) and `FUN_004bd759` (unit key) each
re-center the cursor by `(16 - sprite_w/2)` BEFORE drawing.  Both
axes use the SAME `sprite_w` term — the Y axis re-center does NOT use
`sprite_h`:

```c
DAT_00591524 = DAT_00591524 + (0x10 - ((int)DAT_005c9258 >> 1));   // cursor_x
DAT_00591528 = DAT_00591528 + (0x10 - ((int)DAT_005c9258 >> 1));   // cursor_y — same term
```

For 16×16 sprites (the actual marker / unit-key shape) this is
incidentally correct (sprite_w == sprite_h), but it's a copy-paste bug
that would yield mis-centered draws if either leaf is ever pointed at
a non-square sprite.  Since both leaves only ever dispatch from a
fixed slot range (0x21..0x28 for the player marker animation, 0x1f..0x22
for the four unit-key letters), and the asset designer made all those
slots square, the bug stays unobservable in retail.

The per-entity draw (`FUN_004bdc31`, chip 5a.ii.b) does NOT share this
bug — it has separate `(16 - sprite_w/2)` and `(- sprite_w/2 + 8)`
expressions for X and Y, derived from the anim-offset table.

> 📍 `src/engine_battlefield_render.c`
> (`player_marker_or_unit_key_render`),
> `docs/findings/battlefield-render.md` §5a.iii,
> Ghidra `FUN_004bd574` lines 30-31, `FUN_004bd759` lines 35-36.

---

## 92. Battlefield effect-chain dirty-box branch is identical in both arms

`FUN_004beed4` (chip 5a.iii) gates dirty-box marking on the per-record
subtype:

```c
if ((&DAT_0057a109)[DAT_00568994 * 0x4c] == '\x05') {
    FUN_004527a6(DAT_00591524, DAT_00591528, 4, 4, 2);   // subtype 5
}
else {
    FUN_004527a6(DAT_00591524, DAT_00591528, 4, 4, 2);   // anything else
}
```

Both arms call the SAME mark-box-tiled helper with the SAME arguments
(4×4 tile box, force_full=2).  The conditional is dead code — likely
a leftover from an earlier version where subtype-5's box size or
force_full differed, then later collapsed without removing the
branch.  The port folds the conditional away; behavior is identical.

Three plausible histories for the leftover:

1. Early version had a smaller box for non-animated effects (e.g. 3×3
   for static, 4×4 for animated) — later equalized.
2. Early version had different force_full escalation (1 vs 2) — later
   standardized on 2.
3. Pure cargo-cult: the dev assumed subtype 5 might need a special
   box later and pre-stubbed the branch.

Not behaviorally observable; documented because future RE work might
otherwise read the branch as meaningful.

> 📍 `src/engine_battlefield_render.c`
> (`middle_leaf_draw_effect_chain`),
> `docs/findings/battlefield-render.md` §5a.iii,
> Ghidra `FUN_004beed4` lines 57-62.

---

## 93. Battlefield type-9-subtype-0xe leaf reads z32 anim table even at zoom 16

`FUN_004beab9` (chip 5a.iv, type-9-sub-0xe POST-call) reads its
animation offset from `DAT_004e4030` (the zoom-32 table) UNCONDITIONALLY:

```c
DAT_00591524 = DAT_00591524 +
               *(int *)(&DAT_004e4030 + ...);   // z32 table, no zoom check
DAT_00591528 = DAT_00591528 +
               *(int *)(&DAT_004e4034 + ...);
```

The sister leaves `FUN_004be4df` (type-6) and `FUN_004be7be` (type-7)
both have proper `if (DAT_004e656c == 0x20) {...z32...} else if (...
== 0x10) {...z16...}` branches.  Only this leaf skips the check.

Likely two-part explanation:
1. Subtype 0x0e (the only subtype that triggers this leaf) only ever
   occurs at zoom 32 in the actual game data, so the missing zoom
   check is never exercised.
2. The z16 table sits adjacent in memory at `DAT_004e3bf0`, with
   identical 0x88-byte-per-facing layout — accidentally indexing
   into z32 from a z16-intended context would corrupt the offset
   by reading "the wrong facing's z32 entry" rather than
   segfaulting.

The port preserves the unconditional z32 read verbatim.  Tests
verify the behavior by running the leaf at zoom 16 and observing
that z32-table values (not z16) feed the cursor.

> 📍 `src/engine_battlefield_render.c`
> (`entity_type9_subtype_e_post_call`),
> `docs/findings/battlefield-render.md` §5a.iv,
> Ghidra `FUN_004beab9` lines 23-30 (first sprite),
> lines 57-62 (second sprite).

---

## 94. Battlefield type-9-sub-0xe second sprite anchored at SAVED entry cursor

`FUN_004beab9` draws TWO sprites per call.  Between the two, the leaf
RESETS the cursor to the saved entry cursor (`iVar2` / `iVar3`
captured at function entry) rather than continuing from the first
sprite's post-adjustment cursor:

```c
// First sprite ends with cursor at (entry_x + anim_x + ..., entry_y + ...)
// ... blit ...

// Second sprite restarts from SAVED entry cursor:
DAT_00591524 = iVar2 + *(int *)(&DAT_004e4030 + ...);   // iVar2 = saved entry_x
DAT_00591528 = iVar3 + *(int *)(&DAT_004e4034 + ...);   // iVar3 = saved entry_y
```

If retail had continued from the first sprite's adjusted cursor, the
second sprite would have inherited the first's anim offset twice over,
plus the first's `(0x10 - half_w)` X bias and `(- half_w - 0x10)` Y
bias.  By restarting from entry, both sprites share the same
"anchor + anim_offset + half-cell centering" base, just with
different Y biases:

- First sprite Y bias:  `- 0x10` (shifts UP 16 pixels)
- Second sprite Y bias: `+ 0x56` (shifts DOWN 86 pixels)

Total Y separation between the two sprites: 102 pixels.  This matches
what you'd expect for a two-tier siege-engine-style entity: top piece
(catapult arm?) above, bottom piece (catapult base?) below the
anchor.

The port mirrors this by passing the saved `entry_y` (not the post-
first-sprite cursor) into the second sprite's cursor computation.

> 📍 `src/engine_battlefield_render.c`
> (`entity_type9_subtype_e_post_call`),
> `docs/findings/battlefield-render.md` §5a.iv,
> Ghidra `FUN_004beab9` lines 57-58 (`DAT_00591524 = iVar2 + ...`).

---

## 95. Battlefield late-pass creature draw reads z32 anim table at zoom 16 too

The chip-5c per-cell creature draw leaf (`FUN_004be070`) indexes into
the z32 anim-offset table (`DAT_004e4030`) UNCONDITIONALLY — no
`if (zoom == 0x20)` gate at all:

```c
iVar4 = *(int *)(&DAT_004e4030 +
                 (char)(&DAT_00554498)[param_1 * 0x1b0] * 0x88 +
                 (char)(&DAT_005544b2)[param_1 * 0x1b0] * 8);
```

The middle-pass per-entity draw (chip 5a.ii.b) gates this lookup by
zoom (z16 table at zoom 0x10, z32 at zoom 0x20).  The late pass does
not.  Whether this matters in practice depends on the late pass ever
running at zoom 16 — which it does (the frame driver dispatches it
unconditionally regardless of zoom — see quirk #97).  At zoom 16 the
leaf still indexes z32, so the per-cell creature draw inherits z32
anim offsets even though every other zoom-16 caller uses z16.

> 📍 `src/engine_battlefield_render.c` (`late_leaf_draw_creature`),
> `docs/findings/battlefield-render.md` §5c,
> Ghidra `FUN_004be070` lines 14-16.

---

## 96. Battlefield late-pass creature draw lopsided cursor restore on gate fail

`FUN_004be070` saves the entry cursor at the top (`iVar2 / iVar3`),
applies a type-conditional offset, then dispatches the sprite stage +
gate.  On gate PASS, the leaf restores cursor to entry; on gate FAIL,
cursor stays at the POST-OFFSET value:

```c
iVar4 = DAT_00591524;   // re-read post-offset cursor_x
iVar1 = DAT_00591528;   // re-read post-offset cursor_y
if (gate passes) {
    ... clip + stage ...
    iVar4 = iVar2;     // OVERWRITE with entry x
    iVar1 = iVar3;     // OVERWRITE with entry y
    if (...) blit;
}
DAT_00591528 = iVar1;   // restore: entry on PASS, offset on FAIL
DAT_00591524 = iVar4;
```

The late-pass driver resets cursor_x at the start of each ROW (so the
damage stays localised within a row) but NEVER resets cursor_y within
the frame.  A gate-fail in row N propagates the offset Y into every
subsequent row's per-cell advance.  In practice every valid creature
record passes the gate, so this is defensive territory.  Filed because
it's a subtle "leaf cursor restore asymmetry" — the kind of thing a
test discovers by accident and a reader can't reconstruct from the
surrounding code.

> 📍 `src/engine_battlefield_render.c` (`late_leaf_draw_creature`),
> `docs/findings/battlefield-render.md` §5c,
> Ghidra `FUN_004be070` lines 49-70.

---

## 97. Battlefield late-pass cell pitch is HARDCODED 0x20 regardless of zoom

`FUN_004bd233`'s per-cell cursor advance is a hardcoded `+ 0x20`:

```c
for (local_10 = 0; local_10 < DAT_004e5d5c; ++local_10) {
    ...
    DAT_004e6588 = DAT_004e6588 + DAT_004e5d50;
    DAT_00591524 = DAT_00591524 + 0x20;       // HARDCODED 32px pitch
}
DAT_004e6588 = DAT_004e6588 + DAT_004e5d70;
DAT_00591528 = DAT_00591528 + 0x20;           // HARDCODED 32px pitch
```

The middle-pass driver uses `g_zoom` for its per-cell advance (16 at
zoom 0x10, 32 at zoom 0x20); the late pass uses a literal `0x20`.
Net effect: at zoom 16, the late pass walks the descriptor cursor
correctly (by `g_desc_stride`), but the SCREEN cursor advances at
zoom-32 spacing over a zoom-16 grid — creature sprites land at half-
density (every other cell's worth of pixel space).

The late pass is effectively a zoom-32-only feature even though it's
dispatched unconditionally.  Probable cause: combat zoom 16 is the
"strategic overview" view where creature draws aren't needed (or
where the per-cell entity index byte at descriptor[+5] is always 0),
so the misalignment never manifests visually.  Filed because the
zoom-coupling assumption is implicit (no zoom branch in the driver)
and easy to miss.

The selection-marker leaf (`FUN_004be33f`) uses the same `0x20`
literal for its cursor formula, inheriting the same quirk for the
hero/army marker position.

> 📍 `src/engine_battlefield_render.c` (`pass_late`),
> `docs/findings/battlefield-render.md` §5c,
> Ghidra `FUN_004bd233` lines 21, 24.

---

## 98. Battlefield final-pass three-way bucket has unreachable branches

The chip-5d final pass digests each visible creature into a one-byte
signed-char sum (port of retail `FUN_00440089(0x19e, ...)`), then
three-way buckets the result:

```c
if (sum < 0xc9) {
    if (sum < 0x65) { value = sum;        pen = 0x3f; }
    else            { value = sum - 100;  pen = 0xf9; }
} else            { value = sum - 200;  pen = 0x20; }
```

Then filters: `if (value != 999 && value != 0) render(...)`.

A signed-char sum has range `[-128, 127]`.  So:

  - bucket-low: `sum < 101`              — fires for sums in `[-128, 100]`.
  - bucket-mid: `101 <= sum < 201`       — UNREACHABLE for signed-char.
  - bucket-high: `sum >= 201`            — UNREACHABLE for signed-char.

Both bucket-mid and bucket-high are dead code with a signed-char
sum.  The filter `value == 999` is also unreachable (max value is
`127 - 0 = 127`).

Likely cause: an older revision used a wider sum type (16-bit?
32-bit?) where the bucket ranges and 999 filter were meaningful.
When the sum was narrowed to signed-char, the bucket boundaries
weren't pruned.

Filed because the port preserves the (dead) branches verbatim — a
reader naively running through the bucket logic would assume all
three pens can fire in practice.  The pen-color expectation in
tests should only target bucket-low.

> 📍 `src/engine_battlefield_render.c` (`pass_final`),
> `docs/findings/battlefield-render.md` §5d,
> Ghidra `FUN_004bf1f9` lines 22-34.

---

## 99. Battlefield final-pass cell pitch IS zoom-aware (sibling of quirk #97)

`FUN_004bf1f9` computes its per-cell pixel position inline from
column/row:

```c
FUN_00402f64(value, ' ', &DAT_004e4520,
             local_1c * DAT_004e656c + DAT_004e6558 + 1,  /* zoom-aware */
             local_c  * DAT_004e656c + DAT_004e6554 + 1,
             &DAT_005af8f0, pen);
```

The `DAT_004e656c` (zoom) multiplier on `local_1c` (col) and
`local_c` (row) means the final pass walks the visible grid at the
ACTUAL zoom-level cell pitch, NOT the hardcoded `0x20` the late
pass uses (quirk #97).

Quirk-worth as the **counter-example** to #97: a sibling pass that
walks the same descriptor table, in the same cluster, with the same
gates — but does NOT inherit #97's pitch hardcoding.  Reinforces that
#97 is a localized bug (or zoom-32-only design decision) inside
`FUN_004bd233 / FUN_004be33f`, not a cluster-wide convention.

Tests verify the final pass renders labels at correct positions at
both zoom 16 and zoom 32 — neither misaligns the way the late pass
would at zoom 16.

> 📍 `src/engine_battlefield_render.c` (`pass_final`),
> `docs/findings/battlefield-render.md` §5d,
> Ghidra `FUN_004bf1f9` lines 36-37.

---

## 100. Mini-map rolling-scan wrap formula leaves a one-row guard band

`FUN_004bc1d1` bumps `DAT_004e5d74` (next-scan-row cursor) by `param_1`
each call, then wraps when:

```c
if (DAT_004e6570 - param_1 < DAT_004e5d74) {
    DAT_004e5d74 = 0;
}
```

That is, the wrap fires when `scan_pos > total_rows - rows`, NOT when
`scan_pos >= total_rows`.  Net effect: a `param_1`-row strip render
never spills past the bottom — because the snap-to-zero happens
BEFORE the render call, the last strip rendered at the bottom of a
cycle is always the FIRST strip of the next cycle, never a partial
half-strip.

Strict reading: at `total_rows = 80`, `param_1 = 4`, the cursor reaches
`{0, 4, 8, ..., 76}` and then on the next call (`72 + 4 = 76 + 4 = 80
> 76`) it wraps to 0.  The "row 80" position would have been a valid
zero-indexed off-grid row anyway, but the conservative formula avoids
the off-by-one risk if `total_rows` ever ends up not-divisible by the
typical scroll step.

The same hardcoded `4` is used from the main-loop tick (`FUN_004b99c0`
line 47), and `0x50` (=80, full sweep) from the many full-redraw call
sites — so both paths benefit from the guard.

> 📍 `src/engine_battlefield_render.c`
> (`engine_battlefield_render_minimap_scroll`),
> `docs/findings/battlefield-render.md` §7,
> Ghidra `FUN_004bc1d1` lines 6-12.

---

## 101. Mini-map cell silently drops on unmapped flag values

`FUN_004bc51a`'s terrain-cell path uses `flags & 0x1c` to select
between the primary and alt mini-map sprite blobs:

```c
if ((bVar1 & 0x1c) == 0) FUN_004b49b7(DAT_004e6584);  // primary
else if ((bVar1 & 0x1c) == 4) FUN_004b49b7(DAT_004e6568);  // alt
// else: nothing
```

The mask spans 3 bits (0x1c = 0b00011100), so it can take values 0,
4, 8, 0xc, 0x10, 0x14, 0x18, 0x1c — but only `0` and `4` map to a
blob.  Cells whose flag bits land on any of the other 6 values are
silently dropped (no blit, no error, no fallback to default).  In
practice the engine should only ever set `0` or `4` here — the other
combinations are probably "we never get there", but the code leaves
no safety net.

The boundary-cell branch (`flags & 0x02`) IS taken first, so a cell
with both `0x02` set AND `0x08` set will paint the default descriptor
(bit 2 cleared after) rather than being dropped.  Only pure-terrain
cells (no boundary bit, no entity) fall into the silent-drop case.

> 📍 `src/engine_battlefield_render.c` (`minimap_render_strip`),
> `docs/findings/battlefield-render.md` §7,
> Ghidra `FUN_004bc51a` lines 34-56.

---

## 102. Mini-map first-col-ingame override fires BEFORE the blob-selector

For the first cell of each mini-map row (`local_24 == 0`), the strip
render checks the engine state gate:

```c
if ((local_24 == 0) && (DAT_0052af94 == 3)) {
    // Default descriptor slot 0 — overrides the normal path
    ...
} else {
    // Normal blob-selector path (flags & 0x1c)
    ...
}
```

When the engine is in-game (state == 3), the leftmost column of the
mini-map is ALWAYS painted from the default descriptor's slot 0,
regardless of the cell's flags or sprite_idx.  This produces a
consistent left-edge marker — almost certainly the world-map's
left-edge boundary indicator.

When NOT in-game (menu, save dialog, etc., state != 3), the first
column falls through to the normal blob-selector path, which means
the mini-map's left edge can look different across screens.  This
is probably deliberate but worth flagging because tests can't
observe it without setting the engine state gate.

> 📍 `src/engine_battlefield_render.c` (`minimap_render_strip`),
> `docs/findings/battlefield-render.md` §7,
> Ghidra `FUN_004bc51a` lines 24-33.

---

## 103. Camera-anchor clamp uses asymmetric bounds (0x41 vs 0x42)

`FUN_0047ed34` clamps the strategic / battlefield camera anchor
`DAT_0055ce70` / `DAT_005678a0` to `[0, 0x41]` (x) and `[0, 0x42]`
(y) — one cell taller than wide.  Both bounds are hardcoded literals
in the binary; nothing scalar is fed in.

```c
if (DAT_0055ce70 < 0)    DAT_0055ce70 = 0;
if (DAT_005678a0 < 0)    DAT_005678a0 = 0;
if (0x41 < DAT_0055ce70) DAT_0055ce70 = 0x41;
if (0x42 < DAT_005678a0) DAT_005678a0 = 0x42;
```

Strategic-map dimensions are 64 × 128 (0x40 × 0x80) — the wider
sibling clamp `FUN_00429b1d` derives its upper bounds from those
constants minus the per-frame visible-cells count.  This clamp's
constants are NOT derived from anything that would naturally produce
0x41 / 0x42:

  - 0x41 = 0x40 + 1 — one past max map x.
  - 0x42 = 0x40 + 2 — two past?  Or 0x80 / 2 + 2?  No obvious algebra.

Likely the anchor coordinate space is a centered camera with a 16-cell
viewport: valid centers would be 7..0x40-8 = 7..56 on the strategic
map (or some battlefield-specific range).  But the implementation
does not actually compute that — it just clamps to hardcoded 0x41 /
0x42.  The asymmetry is probably a single-character typo in the
source ("hand-tuned" by an artist who tweaked one value and forgot
the other).  Preserved for parity.

The clamp's test is `0x41 < x` (strict less-than), so 0x41 itself is
INCLUSIVE.

> 📍 `src/engine_camera.c` (`engine_camera_clamp_anchor`),
> `docs/findings/main-loop-tick.md` §"render-phase tail",
> Ghidra `FUN_0047ed34` lines 62798-62816.

---

## 104. Scroll clamp's lower-then-upper order can push 0 negative

`FUN_00429b1d` clamps the strategic-map scroll position with a lower
clamp followed by an upper clamp:

```c
if (DAT_005651b4 < 0) DAT_005651b4 = 0;                       // lower
...
if (0x41 - DAT_0053e8ac <= DAT_005651b4)                      // upper
    DAT_005651b4 = 0x40 - DAT_0053e8ac;
```

If the visible-column count `DAT_0053e8ac` exceeds 0x40 (the map
width), the upper-bound expression `0x40 - col_count` is negative.
The upper clamp's test condition `0x41 - col_count <= 0` is true for
any non-negative col, so even col == 0 fires the upper clamp and
ends up at a negative value.  The lower clamp has already fired
once this call — it does not re-fire — so the negative value is
what remains.

Never observed in practice (real visible-column counts max around
0x10), but the boundary is in the binary and the test pins it.
Documenting because it's a non-obvious consequence of the
lower-then-upper test order.

> 📍 `src/engine_camera.c` (`engine_camera_clamp_scroll`),
> `docs/findings/main-loop-tick.md` §"render-phase tail",
> Ghidra `FUN_00429b1d` lines 17006-17024.

---

## 105. Player roster persists 6 bytes but the runtime uses 12

The player roster at retail `0x568db0` is a table of 6 interleaved
`{army-id, seat}` byte pairs (12 B), indexed by seat 0..5.  The producer
`FUN_0049bbe8` writes pair `seat` and the clear `FUN_0049bc1d` zeroes all
six pairs — so the live structure is unambiguously 12 bytes.

But the save schema (entry 140, retail `DAT_004de960[140]`) records this
slot with **size 6** — it persists only the leading three pairs.  Loading
a save therefore restores pairs 0..2 and leaves pairs 3..5 at whatever
they held (zero after a fresh `player_roster_clear`, or stale from a
prior scenario).  This is retail behaviour, not a port artifact: the
retail schema entry is genuinely 6 bytes wide.

In the port this forces the roster storage to be *larger* than its
persisted slot.  `g_player_roster[12]` is hand-owned in `save.h`/`save.c`
(not emitted as a generated scalar global), and `gen_save_schema.py`
points schema row 140 at its first 6 bytes — so the `.sav` byte-stream is
byte-identical to retail while the runtime gets all six pairs.

> 📍 `src/engine_roster.c`, `src/save.c` (`g_player_roster`),
> `tools/gen_save_schema.py` (NAMED_BIG_BLOCKS entry 140),
> Ghidra `FUN_0049bbe8` / `FUN_0049bc1d`.

---

## 106. Roster compaction cannot remove its last slot (non-convergent)

`FUN_0049bc5f` compacts the roster by dropping any pair whose seat index
exceeds the active-player count, shifting later pairs down:

```c
for (i = 1; i < 6; i++)
    if (active < pair[i].seat) {
        for (j = i; j < 5; j++) pair[j] = pair[j + 1];   /* shift down */
        changed = 1;
    }
```

The shift copies `pair[j+1]` into `pair[j]` for `j = i..4`, so when the
offending entry is at index 5 the inner loop (`j = 5; j < 5`) runs zero
times — nothing moves, `pair[5]` is untouched — yet `changed` is still
set.  The outer pass loop reruns while `changed` is true, re-detects the
same index-5 entry every pass, and only stops when it hits its hard
**6-pass cap**.  The out-of-range entry survives as a trailing duplicate.

This is harmless in practice: the consumer (`FUN_0049bd99`, deferred)
reads only the front `active` entries, so a stale tail is never seen.
But the surviving bytes are part of the persisted slot, so the port
reproduces the cap-bounded non-convergence verbatim rather than looping
to a clean fixpoint.  The `active=0` case is the extreme: every front
pair collapses to a copy of the stale `pair[5]` over the six passes.

> 📍 `src/engine_roster.c` (`player_roster_compact`),
> test `roster_compact_non_convergent_tail_survives`,
> Ghidra `FUN_0049bc5f` lines 74394+.

---

## 107. NPC schedule reads the global localization walker mid-routine

The NPC worker-schedule initialiser (`FUN_0049caaa`) hands each AI
player a job, then copies that job's localized 16-byte name out of
L2.ENG by reading the **global walker pointer** `DAT_005c9270`
*directly* — right after seeking it — rather than via a self-contained
read accessor:

```c
FUN_004018d7(7, work_location);              // seek walker into section 7
FUN_00401136(DAT_005c9270, &aux[+4], 0x10);  // copy 16 B FROM the walker
```

This is the one caller that motivated surfacing the walker offset in
the localization port (see `localization.h` §"Walker pointer").  Our
port runs the seek with a *local* `localization_state_t` over an
injected blob and copies from `blob + walker_offset`, deliberately not
modelling the cross-subsystem global walker (other functions, e.g.
`0x42ab82`, repurpose `DAT_005c9270` for unrelated regions).

**Drop-in divergence:** retail copies unconditionally — L2.ENG is
always resident, loaded by WinMain before any strategic setup.  Our
port skips the copy when no blob has been wired
(`engine_npc_schedule_set_localization(NULL, …)`), so the routine is
safe to run before localization load.  Once a strategic caller lands,
the bring-up wires `config_localization_data()` and the copy fires.

> 📍 `src/engine_npc_schedule.c` (`engine_npc_schedule_init`),
> test `npc_copies_localized_name`, Ghidra `FUN_0049caaa` line 74720.

---

## 108. NPC schedule indexes fixed-size tables with no bounds check

`FUN_0049caaa` has two unchecked indexings that the port reproduces
verbatim, both safe only because the live data stays in range:

- The fixed-job pre-reservation marks `job_taken[aux[+0x25]]` into an
  8-byte stack array; a corrupt fixed-job id ≥ 8 would smash the stack
  (retail has the same latent overflow).
- The finalize pass reads the per-job work-position table by **every**
  seat's `[+0x0a]` job id — including owned or limit-skipped seats
  whose job byte was never assigned this run (it stays whatever it
  was, typically 0 → position `(4,2)`).  A job id past the table tail
  would read adjacent `.rdata`.

Valid play keeps job ids in `0..5`, so neither fires; the host tests
stay in range to avoid tripping ASan on the same reads retail makes
blind.

> 📍 `src/engine_npc_schedule.c` (`engine_npc_schedule_init`),
> Ghidra `FUN_0049caaa` lines 74681 (pre-mark), 74780+ (finalize).

---

## 109. Seat-assign uses 999 as an in-band "keep the existing id" sentinel

`FUN_0049bae9` (`player_seat_assign`) always writes the seat's
occupancy flag and 31-byte name, but stamps the aux id field only when
`player_id != 999`.  The single-player reload path (`FUN_004412cc`'s
second call, `FUN_0044121a`) passes literal `999` to re-seat the local
player by name **without** disturbing an id that was already set up
elsewhere; the join paths pass the real id read from the packet.

So `999` is a magic value carried through the same `int` argument as
real ids — there is no separate "set id?" flag.  Any future caller that
legitimately needed id 999 would silently no-op the write; in practice
ids are small (army/color indices), so the collision never bites.

The 31-byte name copy (`strutil_bounded_copy`, no NUL terminator) also
assumes the source is a full ≥31-byte field — retail always hands it
the 0x53f1e0 config buffer or a fixed packet slot.  Passing a short C
string reads past it (the host tests use 32-byte zero-padded buffers
to stay clean under ASan).

> 📍 `src/engine_roster.c` (`player_seat_assign`),
> Ghidra `FUN_0049bae9`; callers `FUN_004410bd` / `FUN_0044121a` /
> `FUN_004412cc`.

---

## 110. Turn-save backup is a 3-deep generational rename chain

Every turn-save goes through `FUN_0049a453` (`save_rotate_and_write`),
which keeps three generations on disk under fixed sentinel names:

```
lastturn.*   ← the turn being written
old_turn.*   ← previous turn
safeturn.*   ← two turns ago (oldest)
```

The rotation is three unchecked file ops — `remove(safeturn)`,
`rename(old_turn → safeturn)`, `rename(lastturn → old_turn)` — then the
new `lastturn` is written.  Nothing checks a return value: on the first
save of a game the two `rename`s simply fail (no source file) and the
chain fills in over the next two turns.  It's a poor-man's ring buffer
built entirely out of `rename`.

Two twists worth noting:

- **The suffix encodes the session mode.**  Single-player uses `.sav`;
  multiplayer splits into `.sva` / `.svb` selected by `DAT_00553248`
  (`g_sim_mp_variant`).  So an MP game keeps an entirely separate backup
  chain from an SP game in the same directory — they never collide.
- **Rotation can be held off.**  `DAT_00553260` (`g_sim_save_rotate_hold`)
  is a small countdown bumped to 2 on the MP turn-advance path
  (`FUN_0049b973`) and decremented elsewhere; while it's ≥ 1 the chain is
  *not* rotated — `lastturn` is just overwritten in place, preserving the
  older two generations across a burst of saves.  It's always 0 in SP, so
  SP rotates on every save.

The filenames are bare relative paths; retail's `save_write`
(`FUN_004ade93`) `chdir`s to the save base dir first (`FUN_004af502`)
before opening them.

> 📍 `src/save.c` (`save_rotate_and_write`),
> Ghidra `FUN_0049a453`; filename templates @ `0x4dc290`–`0x4dc310`;
> hold counter set by `FUN_0049b973`.

---

## 111. Two readers of the player roster disagree on how many slots there are

The end-of-turn readiness check (`FUN_0049b762`) walks player slots
`1..5` — its loop is `for (i = 1; i < 6; ...)`.  The sibling min-tick
walker (`FUN_0043e330`), reading the *same* 2112-byte roster array
(`0x57bf00`, 6 records × 352 B), walks `1..6` — `for (i = 1; i < 7;
...)`.  Slot 6 sits at byte offset `6 * 0x160 = 2112`, exactly one
record *past* the end of the array, so `FUN_0043e330` reads four bytes
of whatever `.data` follows on every active-player count.

In retail this is benign-by-layout: the bytes after the array happen to
be a zeroed/inactive region, so the `has_data && active && !disconnected`
gate fails for the phantom slot 6 and it never contributes.  Our port
of `FUN_0043e330` (the rate-controller min-tick walker in
`engine_rate.c`) collapses the strided layout into 7-entry arrays,
sidestepping the OOB read — but it's a reminder that the engine's notion
of "how many player slots" is not consistent across functions.
`FUN_0049b762`'s 1..5 stays safely in bounds either way.

> 📍 `src/engine_turn.c` (`engine_turn_end_check`), Ghidra
> `FUN_0049b762` (1..5) vs `FUN_0043e330` (1..6); roster array
> `0x57bf00`.

---

## 112. Turn-sync timer is armed 10 seconds in the past so it fires immediately

When the MP turn-sync handshake arms (`FUN_0049b762` state 0→1), it
seeds the periodic-broadcast baseline with `timeGetTime() - 10000`
rather than the current time.  The downstream gate (`FUN_0049b8b0`)
fires its broadcast when `9999 < now - baseline`.  By back-dating the
baseline a full 10 s, the engine guarantees the very next poll sees
`~10000 < now - baseline` true and broadcasts the turn-sync message
immediately, instead of waiting a real 10 s after the handshake opens.
It's a one-line way to say "fire on the next tick, then settle into the
10 s cadence" without a separate first-time flag.

> 📍 `src/engine_turn.c` (`engine_turn_end_check`, `new_sync_baseline_ms
> = now_ms - 10000`), Ghidra `FUN_0049b762` @ `0x49b762`; gate in
> `FUN_0049b8b0`.

---

## 113. Main-menu button 3's label lives at localization sub-string 0x2f, not 4

The main-menu renderer (`FUN_0041eaa3`, ported as
`engine_title_render_buttons`) draws four buttons whose labels are all
sub-strings of localization-blob entry `0xb`.  Three of the four use the
obvious contiguous indices — button 1 = sub-string 2, button 2 = 3,
button 4 = 4 — but **button 3 = sub-string 0x2f (47)**, jumping 43 slots
ahead in the same entry.

Almost certainly the label text was added late: rather than renumber the
existing strings (other screens also index into entry `0xb`), the "new"
third option's text was appended near the end of the section and
addressed by its tail index.  The 42 intervening sub-strings (5..46) are
unrelated entries the seek simply walks past.  Faithful ports must keep
the literal `0x2f` skip count — a "tidy" 2/3/4/5 sequence would render
the wrong string for button 3.

> 📍 `src/engine_title_render.c` (`engine_title_render_buttons`, the
> `label_skip` table), Ghidra `FUN_0041eaa3` @ `0x41eaa3`
> (`FUN_00402c5e(0xb, 0x2f, …)` for the third button).

---

## 114. `DAT_004eac50` (screen-state byte) has two independent port mirrors

Retail's `DAT_004eac50` is a single byte: the current screen-state ID,
the selector for the `FUN_0040f1a0` render-dispatch switch.  In the port
it is mirrored **twice, independently**: `g_sim_current_screen` (engine.c,
the dispatch selector) and `g_language_tag` (gfx_text.c, which renders
title-screen text in a taller line box — 0x36 vs 0x10 — when the value is
`0x1f`/`0x1c`; the "language tag" name is a misnomer carried over from an
early labelling).

Both are `0` today because no producer transitions the screen, so they
cannot yet desync.  But the boot→title transition (`FUN_0042b780`, sets
`0x1f`) will write the screen-state byte, and at that point **both mirrors
must be updated together** or the menu will dispatch the title arm while
`gfx_text` still measures text with the short box.  The faithful fix is a
single screen-state setter that fans out to both — to be introduced when
that transition lands (the first real producer), so it can be tested
against observable behaviour rather than added as dead infrastructure.

> 📍 `src/engine.c` (`g_sim_current_screen`), `src/gfx_text.c`
> (`g_language_tag`, line ~163), Ghidra `FUN_0040f1a0` @ `0x40f1a0`
> (dispatch) / `FUN_00402637` (taller-box special-case).

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
