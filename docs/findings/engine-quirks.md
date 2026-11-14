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

## 115. Two palette builders by desktop depth — fullscreen shows the red highlight, 8bpp/windowed blacks it

Every image-palette upload runs through the dispatcher `FUN_0042f1df`,
which picks one of **two** PALETTEENTRY builders based on `DAT_0053f228`
(set once at boot in `FUN_0042e5c0` from the *desktop* color depth):

- **`DAT_0053f228 == 0`** — desktop is 8bpp → **`FUN_0042f449`**, builds
  `DAT_00515d90`: 10..245 from the `.256` (peFlags 4 / `PC_NOCOLLAPSE`);
  0..9 and 246..255 keep their never-written BSS value (**black**),
  peFlags 0.  Also does a `SetSystemPaletteUse(NOSTATIC)`/`(STATIC)` dance.
- **`DAT_0053f228 != 0`** — desktop is "full color" (any modern 16/32bpp
  desktop) → **`FUN_0042f306`**, builds `DAT_005165b0`: **1..254 from the
  `.256`** (peFlags 4), idx 0 hard-coded black `(0,0,0)` and idx 255 hard-
  coded white `(255,255,255)`, both peFlags 0.

The menu's UI/text indices land where the two builders differ: the
uppercase-highlight color is **1** (`FUN_0041ea14` arms `DAT_0058fe2c`,
`FUN_00402637` sets foreground 1 for A..Z) and the selected/hovered-button
highlight is **0xf9** (`FUN_0041eaa3`).  gateway.256 carries idx 1 =
`(128,0,0)` dark red and idx 0xf9 = `(252,0,0)` bright red.

So the visible result depends on the desktop, *not* on fullscreen-vs-
windowed per se:

- On a **full-color desktop** (every modern machine), retail runs
  `FUN_0042f306`, the `.256` colors at 1 and 0xf9 survive, and the menu
  shows **dark-red title caps** and a **bright-red hover highlight**.
  This is what a real player sees.
- On a true **8bpp desktop**, `FUN_0042f449` blacks the reserved range and
  the highlight is invisible.

**Empirically confirmed (2026-05-29), both ways.**  A live
fullscreen-exclusive retail capture (`frida_capture.py --no-force-windowed
--no-gdi-seat`, palette dumped via GetEntries) read back **idx 0xf9 =
`(252,0,0)` peFlags 4**, idx 1 = `(128,0,0)`, idx 0xff = `(255,255,255)`;
and the red hover was **visually confirmed on screen**.  The matching
windowed capture (default `--force-windowed`) read idx 0xf9 = `(0,0,0)`.

> **Earlier mistake, corrected.**  This entry once claimed the blackout was
> retail-faithful, and a first "correction" then claimed fullscreen was *also*
> black (reasoning only from `FUN_0042f449` + a NULL-handle `GetPaletteEntries`
> seed, having missed `FUN_0042f306` entirely).  Both were wrong.  The blackout
> our windowed capture showed is a **self-inflicted artifact**: the
> `--force-windowed` harness hook sets `DAT_0053f228 = 0` inside
> `SetCooperativeLevel`, which fires *before* the menu's `gateway.256` upload,
> so `FUN_0042f1df` takes the 8bpp `FUN_0042f449` branch.  Real retail on the
> same box never does this — it stays on `FUN_0042f306` and renders red.

**Port:** `gfx_load_palette` now defaults to the `FUN_0042f306` build
(`g_palette_full_range = 1`) — the faithful path for the drop-in's
full-color-desktop target, so the red hover highlight + dark-red caps
render.  `gfx_set_palette_full_range(0)` selects the `FUN_0042f449`
blackout build for bit-identical regression against the *old* windowed
groundtruths (which were captured under `--force-windowed` and are
therefore black-reserved-range; new full-color groundtruths should be
captured with `--no-force-windowed`).

> 📍 `src/gfx.c` (`gfx_load_palette`, `gfx_set_palette_full_range`); Ghidra
> `FUN_0042f1df` @ `0x42f1df` (dispatcher) / `FUN_0042f306` @ `0x42f306`
> (full-color builder, `DAT_005165b0`) / `FUN_0042f449` @ `0x42f449` (8bpp
> builder, `DAT_00515d90`) / `FUN_0042f58c` (SetEntries) / `FUN_004b0ab5`
> (×4 working buffer) / `FUN_0041ea14` + `FUN_00402637` (color-1 caps).

---

## 116. The page-4 name box is no-shadow in the full render but SHADOWED at steady state

`FUN_0041ef57` (the page-4 "Choose your title and your shield" full
render) brackets the name box + shield row in no-shadow mode
(`DAT_005aea40 = 1` before `FUN_0041f321`, back to `0` after).  But the
per-tick title-update ladder also calls `FUN_0041f321` *standalone* every
frame — outside that bracket — so the name text is re-rendered with
`DAT_005aea40 == 0` (shadow ON): foreground glyph at `cursor_y`, plus the
drop-shadow passes at `cursor_y ± 1`.  Any captured retail frame is the
steady-state refresh, so its name text carries a shadow the one-shot full
render omits.

Found by diffing the drop-in page-4 render against retail: a 191-px
residual confined to the name-text rows, where retail had an extra dark
top row (the `cursor_y-1` shadow pass) the no-shadow port lacked.  The
fix mirrors retail's ladder: a page-4 refresh
(`engine_title_render_refresh_choose_title`) re-renders the name box with
shadow on, applied once after the full render for the parity capture.

> 📍 `src/engine_title_render.c` (`engine_title_render_refresh_choose_title`),
> `docs/findings/title-menu-screens.md` §"Choose your title…",
> Ghidra `FUN_0041ef57` / `FUN_0041f321` / `FUN_00402637`.

---

## 117. The chrome bevel renderer is the stone-panel renderer with a hardcoded sheet

`FUN_00409934` (bevel border) and `FUN_00409c93` (bevel interior) are
**byte-for-byte twins** of the stone-panel builders `FUN_0040946d` /
`FUN_004097c5` — identical 9-patch index logic, identical descriptor
staging, identical blit dispatch — differing only in that the sprite
sheet is hardcoded to the chrome blob `0x57d3d0` (panels.pl8) instead of
taken as a parameter, and the panel *mode* is the first arg rather than
the second.  `FUN_004093e0` (the raised button/tab frame) is then just
`border(mode 1) + inset interior`, and mode 1 is what adds +0xcc to every
border index to select the sheet's second (chrome) style block.

Consequence for the port: nothing new to write — `gfx_panel_draw_bevel`
calls the existing `gfx_panel_draw_border(sheet, 1, …)` +
`gfx_panel_fill_interior` with the chrome sheet.  Only the asset
(panels.pl8, distinct from the stone panels2.pl8) needed loading.  The
two builders' only other difference, the diagnostic-string addresses on
the bad-offset abort, is immaterial — the port elides the error latch.

> 📍 `src/gfx_panel.c` (`gfx_panel_draw_bevel`), `src/gfx.c`
> (`gfx_load_chrome_sheet`), Ghidra `0x409934` / `0x409c93` / `FUN_004093e0`
> vs `FUN_0040946d` / `FUN_004097c5`.

---

## 118. The page-7 coat-of-arms preview is a plain blit — the compositor is dead at param 0

The game-setup screen's top-right preview is drawn by `0x410c71(0, …)`,
which blits the 128×128 buffer `0x569590` and then calls the shield
compositor `0x410cbd` with **param 0**.  That compositor's entire
per-pixel remap loop is guarded by `if (param_1 != 0)`, so at param 0 it
does **nothing visible** — only an unconditional title/rank state
recompute (`0x451bba`, which writes lord-record fields read by the
compositor's *non-zero* paths) runs.  So the page-7 preview is exactly a
raw blit of whatever the scenario-load path rendered into `0x569590` (the
selected map thumbnail), and porting the 654-byte shield compositor is
unnecessary for this screen.

The render port therefore models the preview as a bound buffer
(`engine_title_render_set_preview`) and defers `0x410cbd` / `0x451bba`
until an in-game screen exercises the non-zero compositor paths (which
need the lord-record structures that nothing ported yet reads).

> 📍 `src/engine_title_render.c` (`engine_title_render_setup_country_list`),
> Ghidra `0x410c71` / `0x410cbd` / `0x451bba`, called from `FUN_0041f98b`.

---

## 119. The page-7 per-frame refresh is dirty-gated inside its sub-renderers, and one of its three calls is multiplayer-only

`FUN_004ba26e`'s page-7 arm (`DAT_005530f0 == 7`) re-runs three calls every
frame: `FUN_0041f98b()` (country list), `FUN_0041f86d(999)` (option tabs,
`999` ⇒ no tab is "active" so all keep their bevel), and `FUN_0041ff75(0)`.
Two non-obvious facts:

- **The gate lives inside the sub-renderer, not the caller.**  `FUN_0041f98b`
  is wrapped in `if (0 < DAT_0057c948) { DAT_0057c948 = 0; … }` and
  `FUN_0041f86d` in `if (0 < DAT_0055ce94) { … }`.  So the per-frame arm is
  cheap — it paints nothing unless an input action set the flag.  The
  full-scene root `FUN_0041f6c7` sets *both* flags to 1 before calling the
  same subs, so a full redraw always draws and ends with both back at 0.
  The drop-in's `setup_country_list` / `setup_tabs` render *unconditionally*
  (the full-scene path needs that), so the port **hoists the gate to the
  refresh caller** (`engine_title_render_refresh_setup`) and has the full
  render clear both flags at its tail — same observable behavior.

- **`FUN_0041ff75(0)` is the multiplayer chat-line ticker and a no-op in
  single-player.**  Its whole body is guarded by `if (DAT_00553030 != 0 …)`;
  page 7 is the single-player setup screen (`DAT_00553030 == 0`), so the
  call does nothing.  The drop-in elides it.  (The multiplayer setup screen
  is page 8, which shares this arm but adds `FUN_00420147`.)

A third subtlety: the country scroll/select actions (`FUN_004337b4` /
`FUN_00433905`) set the demand-redraw `DAT_0057d340 = 2` **and** both sub-
dirty flags.  In the drop-in `DAT_0057d340 = 2` triggers a full-scene redraw
next frame (which redraws the scrolled list and clears the flags), so the
dirty-gated refresh is the faithful per-frame path but the full redraw is
what's observed after an action.

> 📍 `src/engine_title_render.c` (`engine_title_render_refresh_setup`,
> `engine_title_render_country_scroll/select`), `src/engine_menu_input.c`
> (page-7 arm), Ghidra `FUN_004ba26e` / `FUN_0041f98b` / `FUN_0041f86d` /
> `FUN_0041ff75` / `FUN_004337b4` / `FUN_00433905`.

---

## 120. Retail's mouse input assumes a fullscreen window — three globals fight a windowed seat

Driving retail interactively in a *windowed* (non-fullscreen) window exposes
three input/timing assumptions baked in for its native 640×480-fullscreen
mode.  All three were found while building the `frida_capture.py
--interactive` seat (PROGRESS 2026-05-30):

- **App-active gate (`DAT_004e65a8`).**  The main loop `FUN_0040e9ab` runs its
  present + full-rate tick only while `DAT_004e65d0 == 0 && DAT_004e65a8 != 0`,
  else `Sleep(200)`.  `DAT_004e65a8` is set to 1 by the WndProc on
  `WM_ACTIVATEAPP` (0x1c) *only when the window is the foreground window* and
  not iconic.  A background window crawls at ~5 Hz.

- **Cursor cache (`DAT_0057cac4`).**  `FUN_004b191e` (the per-tick input read)
  calls `GetCursorPos` (screen coords) and, *iff* `DAT_0057cac4 != 0`,
  overwrites the WndProc's WM_MOUSEMOVE-cached cursor (`DAT_004eabbe/c0`,
  client coords) with those screen coords.  Correct only when client == screen
  (fullscreen at origin); in a window it feeds screen coords into a
  client-coord mapping.  It's set to 1 in in-game screens (`FUN_0043278b`),
  0 elsewhere.

- **Client rect (`DAT_004ea180..18c`) = fullscreen when `DAT_0053f228 != 0`.**
  The WndProc fills the cursor→game mapping rect from `GetClientRect` only when
  `DAT_0053f228 == 0`; otherwise (`!= 0`, retail's default) it uses
  `SetRect(0,0,GetSystemMetrics(0/1))` — the whole desktop.  `FUN_004b191e`
  then maps `game = cached_cursor * 640 / (rect width)`.  So in a 640×480
  window on a 1920-wide desktop, every click is scaled by 640/1920 (÷3) into
  the wrong game position, even though the on-screen image (GDI-seat
  StretchDIBits to the *actual* client) looks right.

The interactive seat neutralises all three by poking, every tick:
`DAT_004e65a8=1`, `DAT_004e65d0=0`, `DAT_0057cac4=0`, and
`DAT_004ea180..18c = (0,0, GetClientRect.w, GetClientRect.h)`.

> 📍 `tools/frida/openlords2-agent.js` (`tryForceAppActive`), Ghidra
> `FUN_0040e9ab` / `FUN_004b191e` / `FUN_004b29be` (WndProc),
> `DAT_004e65a8` / `DAT_0057cac4` / `DAT_0053f228` / `DAT_004ea180`.

---

## 121. The page-9 option dropdown redraws the whole page underneath every frame, and one tab grows *upward*

Clicking an option tab on the custom-game setup screen (page 7) opens a
little chrome dropdown of that option's values — page 9.  Rather than draw
the dropdown as a cheap overlay on the existing frame, `FUN_0041fdd6`
*re-renders the entire page it came from* first — `FUN_0041f6c7(tab)` redraws
the full page-7 setup screen (panel, roster, country list, all 12 tabs) — and
only then stamps the value list on top.  The redraw passes the edited tab as
the "active" arg so that one tab's value cap is suppressed (the dropdown
covers it); every other tab keeps its single-value cap.  It's a full-scene
redraw per frame just to show a 2–7 row popup.

Two further oddities in the same 415-byte function:

- **Tab 2 (the player-count option) grows upward.**  Every other tab's
  dropdown drops *down* from its anchor in `DAT_004d3158`, but tab 2 sits on
  the bottom row of the grid, so its panel would run off-screen.  The fix is
  a single special-case: `y += (count-1) * -0x10`, pinning the panel's
  *bottom* at the tab and letting it extend up.  Tab 2 is also the only tab
  whose value count is dynamic — clamped to `roster_count - 1` when the
  roster has fewer than 5 seats (you can't pick "5 players" in a 3-player
  game), where every other tab reads a fixed count from `DAT_004d5560`.

- **A precomputed column the renderer ignores.**  `DAT_004d3158` is a 12×3
  table `{x, y, rows}` where the third column is exactly `value_count + 2`
  (the panel height in tile-rows).  `FUN_0041fdd6` reads only `x`/`y` and
  recomputes the height as `DAT_00553fb4 + 2` from the live count — so the
  baked third column is dead data, presumably a leftover from when the count
  wasn't yet dynamic (see the tab-2 clamp above, which would desync it).

> 📍 `src/engine_title_render.c` (`engine_title_render_option_detail`),
> `src/engine_menu_input.c` (`menu_action_p7_tab`),
> `docs/findings/title-menu-screens.md` §"Page 9", Ghidra `FUN_0041fdd6` /
> `FUN_00433998`, `DAT_004d3158` / `DAT_004d5560` / `DAT_0053e8a4`.

---

## 122. The page-9 dropdown's value rows sit *inside* its own tab's hotspot box, so dispatch order decides which wins

The page-9 input arm (`FUN_004ba9c8`, `DAT_005530f0 == 9`) dispatches three
things per frame, and their geometry overlaps in a way that only works
because of the **order**:

1. the dropdown's own value rows (hover `FUN_00433ce4`, pick `FUN_00433a23`),
2. the page-7 option-tab grid (`DAT_004dce30`, 12 entries),
3. a catch-all "any remaining press closes the dropdown".

The catch is that a tab's hotspot box and the dropdown it opens **occupy the
same screen pixels**.  Tab 0's box is `(170,280)-(266,328)`; its dropdown
anchors at `(0xaa=170, 0x118=280)` with value rows at y `296–328` — fully
inside the tab box.  So a click at, say, `(218, 320)` is simultaneously "on
the tab" and "on value row 2".  Retail resolves it by running the hover→pick
check (step 1) **before** the tab dispatch (step 2): the value pick wins, and
the tab is never re-triggered.  Get the order wrong and clicking a value would
just re-open (or flicker) the dropdown instead of selecting.

A second consequence of step 3: while a dropdown is open, a press on *anything
else* — the country list, the Cancel/Start buttons, empty parchment — doesn't
activate that control.  It only **dismisses the dropdown** (back to the
bookmark page).  Page 9 never dispatches the page-7 button/country tables at
all; the next click does.  So the dropdown is strictly mode-modal: one click
to commit-or-cancel, then you're back on page 7.

The pick also has no hover-to-current-value step: when the dropdown first
opens (`FUN_00433998`) `DAT_00553114` is left at its stale page-7 value, so
the *first* live frame highlights nothing on the values unless the cursor
already happens to be over a row.  The highlight is purely cursor-driven from
then on — retail does **not** pre-select the current value.

> 📍 `src/engine_menu_input.c` (`engine_menu_title_input` page-9 arm,
> `menu_action_p9_pick`), `src/engine_title_render.c`
> (`engine_title_render_option_detail_hover` / `_pick`),
> `docs/findings/title-menu-screens.md` §"Page 9 input", Ghidra
> `FUN_004ba9c8` (`DAT_005530f0 == 9` arm) / `FUN_00433ce4` / `FUN_00433a23`.

---

## 123. The text editor's enable flag is reset to 0 every frame, so the WM_CHAR route is gated by the *render* arm, not the keyboard

The one global title text editor (`DAT_005cd550` buffer + state scalars) is
fed by `WM_CHAR`/`WM_KEYDOWN` in the WndProc (`FUN_004b29be`), but every
insert leaf (`FUN_00401d26`) first checks `DAT_005aeb78 != 0`.  That enable
flag is **not** owned by the keyboard path — it's owned by the per-frame UI
input dispatcher `FUN_004ba9c8`, which sets `DAT_005aeb78 = 0` at its very
top (line 9, gated on the input-suppress timer `DAT_0057a0cc < 1`) and then
each page arm that has a live text field raises it back to 1 (page 4 →
name box, page 3/8/0xb → save-name, page 5/6 → MP name).  Pages with no
field leave it 0.

So a keystroke that arrives while you're on, say, page 7 is filtered and
then **dropped** — `FUN_00401a20` runs, but the insert no-ops because the
last dispatch frame cleared the gate and page 7 never re-raised it.  The
editor only accepts input on the exact frame-after-frame a text-field page
is on screen.  This is why the editor can be a single global shared across
every screen: it's mode-gated by whichever render arm is running, not by
focus tracking.

Corollary for the drop-in: the gate must be reset at the *top of the menu
input dispatch* (before the page switch) and re-raised in the page arm —
not in the WndProc.  Putting the reset in the keyboard handler would mean a
key with no intervening frame keeps the stale enable.

> 📍 `src/engine_menu_input.c` (`engine_menu_title_input` head reset +
> page-4 arm tail), `src/engine_title_render.c`
> (`engine_title_render_title_editor_disable` /
> `engine_title_render_choose_title_name_tail`), `src/main.c` (WM_CHAR /
> WM_KEYDOWN route), Ghidra `FUN_004ba9c8` line 9 + page arms,
> `FUN_00401d26`, `FUN_004b29be`.

---

## 124. The page-4 name editor is capped at 16 chars but copied out at 31 — the cap lives in the *init*, not the field

`FUN_00432cc8` (page-2 → page-4) inits the editor with
`FUN_00402009(&DAT_0053f1e0, 0x10, 0xc0, 0)` — `max_length = 0x10` (16).
But the per-frame copy-out that streams the editor into the visible name
field is `FUN_0040210c(&DAT_0053f1e0, 0x1f)` — **31** chars.  The 31 is just
the destination's safety bound; the editor never grows past 16 because
`text_input_clamp_cursor` raises `clamp_state = 2` (which makes
`FUN_00401d26` refuse the insert) as soon as `cursor >= max_length`.

The trap for a re-implementation: if you seed the editor struct field-by-
field and forget `max_length`, it defaults to 0, and `cursor (0) >=
max_length (0)` clamps **every** insert off — the box looks dead even though
the WM_CHAR route is wired correctly.  The cap is carried by the init call,
not by any property of the name field itself.

> 📍 `src/main.c` (`seed_page4_state` → `text_input_init(.., 0x10, 0xc0, 0)`),
> `src/text_input.c` (`text_input_clamp_cursor` / `_insert_char`), Ghidra
> `FUN_00432cc8` / `FUN_00402009` / `FUN_0040210c`.

---

## 125. The button-record hit-test reuses the sprite `kind` field as the hitbox side length

The screen button strips (`FUN_0040cfd2` renderer / `FUN_0040da1e` hit-test,
e.g. the page-3 file list's gauntlet + scroll buttons) store one 24-byte
record per button.  Field +0x06 is the *sprite kind* — a sheet+blitter
selector: `0x18` reads a 24×24 `system2.pl8` sprite, `0x20` a 32×32 one.  The
hit-test then computes the clickable box as `[x, x + kind)` square — i.e. it
uses the **same byte** as both the sprite selector *and* the pixel side
length.  It works only because the two system-sheet kinds happen to equal
their own sprite dimensions (0x18 = 24, 0x20 = 32).  A new kind whose numeric
value didn't match its sprite size would silently get the wrong hitbox.

> 📍 `src/engine_menu_input.c` (`button_rec_contains`), `src/engine_button.h`
> (the `kind` field), Ghidra `FUN_0040da1e` @ 0x40da1e.

## 126. The scroll-arrow auto-repeat is driven by a 0x30-byte sparse "gate" table that thins toward fire-every-frame

Holding a type-4 button (the file-list scroll arrows) doesn't fire on a fixed
interval.  `FUN_0040da1e` keeps a per-button repeat counter (the record's
+0x0e byte) that increments once per input frame while held, and fires the
action only when `DAT_004d2748[counter]` is non-zero.  Counters < 8 never fire
(the initial hold delay); from 8 the table is sparse — `1,0,0,0,0,0,1,0,…` —
so it fires every ~6th frame, then every ~4th, every ~2nd, and once the
counter saturates at `0x2f` it fires *every* frame.  The effect is a scroll
that starts slow and accelerates the longer you hold, with the curve baked
into a hand-tuned lookup table rather than computed.  The frame cadence itself
is gated by `DAT_004ea128` = `FUN_004b20ed()`, a `timeGetTime` delta that
returns 0 (no counter advance) if < 30 ms elapsed since the last poll.

> 📍 `src/engine_menu_input.c` (`k_button_repeat_gate`, the type-4 arm of
> `engine_menu_button_dispatch`), Ghidra `FUN_0040da1e` / `FUN_004b20ed`,
> table dumped from `0x4d2748`.

## 127. Save filenames are still DOS 8.3 — the page-3 editor caps at 8 chars even though it streams to a 64-byte field

The page-3 "Loading a game." filename editor is seeded with
`FUN_00402009(&DAT_004ea130, 8, 0xa0, 1)` — `max_length = 8`.  The per-frame
copy-out streams it with width `0x40` (`FUN_0040210c(&DAT_004ea130, 0x40)`),
but the editor refuses inserts past 8 chars (quirk #124's clamp).  Eight is the
DOS 8.3 base name: the engine still writes `*.sav`-style files and won't let
you name a save longer than the filesystem of 1996 allowed.  Typing "war" over
the seed "lastturn" yields "wartturn" — overwrite-from-cursor, then truncated
at 8.  (Mode 1, vs page 4's mode 0, changes the allowed-character filter, not
the case — the captured frame stays lowercase.)

> 📍 `src/main.c` (`seed_page3_state` → `text_input_init(.., 8, 0xa0, 1)`),
> Ghidra `FUN_00402009(&DAT_004ea130, 8, 0xa0, 1)` (5 call sites).

---

## 128. The new-game calendar is seeded as six redundant scalars — year and season each stored alongside their own ±1 neighbours and a verbatim copy

The new-game initialiser (`FUN_00497ced`) doesn't store the start date as
two numbers (year, season).  It writes **six** save scalars laid out as two
parallel "triple + copy" groups, derived in the asm from a single base via
`inc`/`dec` rather than as independent constants:

```
year   base 0x4f3 (1267)   prev 0x4f2 (entry 21)   next 0x4f4 (entry 22)   copy 0x4f3 (entry 28)
season base 3              prev 2    (entry 24)    next 4    (entry 25)    copy 3    (entry 29)
```

The bases live in save entries 20 (year) and 23 (season); entries 21–29 are
the contiguous block that holds their neighbours and copies.  The pre-computed
prev/next mirrors are why the strategic-map header can render adjacent
years/seasons without arithmetic at draw time — entry 20 is read straight into
the year glyph by the map arm `FUN_0040f5fd` → `FUN_0041a900`.

The season-next write (`entry 25 = base+1`) is followed in retail by a wrap
clamp `if (next > 4) next = 1` — the 1..4 seasonal cycle — but it is **dead**
at the seeded value 4 (`4 > 4` is false), so the decompiler folds the block
away ("Removing unreachable block").  The clamp only bites once a turn
actually advances the season past winter; at new-game it never fires.

> 📍 `src/game_init.c` (`engine_game_init_seed_scalars`),
> Ghidra `FUN_00497ced` @ 0x497d2f–0x497d97, r2 `pdf @ 0x497ced`.

---

## 129. The army-coord probes index a 3-byte table that Ghidra renders as a 0x300 stride

The new-game / AI-setup army-neighbourhood probes `FUN_00428007` and
`FUN_00428078` look up an army's map coordinate before scanning the cells
around it.  Ghidra decompiles the lookup as

```c
FUN_0046cfbd((uint)(byte)(&DAT_0053fa1c)[param_1 * 0x300],   // col
             (uint)(byte)(&DAT_0053fa1d)[param_1 * 0x300],   // row
             radius);
```

i.e. a stride of **0x300** — which lines up suspiciously well with the
768-byte per-army record stride of the block that table lives in (save
entry 4, `g_per_army_768`).  It's a lie.  The actual instruction is

```
mov cl, byte [eax + eax*2 + 0x53fa1c]   ; eax = param_1  →  base + param_1*3
```

`eax + eax*2` is `param_1 * 3`.  The coordinate table is a **packed
3-byte-stride** array — `{col, row, ?}` per army id — sitting at offset
0x6c of entry 4, *not* one field sampled once per 768-B record.  The
decompiler appears to have mis-folded the `lea`-style `base + idx*3`
into a scaled array index and picked the wrong scale.  This is a
standing reminder that Ghidra's array-stride rendering is not byte-truth;
`r2 pdf` is.

A corollary: save entry 4 is therefore **not** a flat 17×768 block — it
carries internal sub-tables (this coord array among them).  Our
`engine_game_init_clear_tables` still clears it correctly (17×0x300 ==
the whole 13056-byte block), but the "17 per-army records" framing is a
clear-shape convenience, not a faithful layout.

> 📍 `src/game_init.c` (`engine_game_init_army_near_rawflag_bit0` /
> `_only_bit1`), Ghidra `FUN_00428007`/`FUN_00428078`,
> r2 `pdf @ 0x428007` (`@ 0x428035` / `0x428045` show the `eax*2`).

---

## 130. The box-scan loop counters double as the engine's nearest-cell result globals — a port trap for FUN_00427ed0  ✅ RESOLVED

The strategic-map box scans `FUN_0046cfbd` / `FUN_0046d130` (and the
ring-distance finder `FUN_0046733c` over them) use the globals
`DAT_00569570` (inner/col) and `DAT_00569578` (outer/row) as their loop
counters — the very same globals `engine_path` fills with its
nearest-cell result.  On the return-1 path the counters happen to hold
the **matching cell's (col, row)**:

```
0x46d0ae  mov [0x569570], eax   ; col = clamped start, then inc per cell
0x46d083  mov [0x569578], eax   ; row = clamped start, then inc per row
0x46d110  mov eax, 1            ; match → return, counters frozen at the hit
```

Our port of all three predicates deliberately uses **locals**, because
their three direct callers (`0x46733c` / `0x428007` / `0x428078`) read
only the 0/1 (or ring-distance) return value — never the globals.  That
is faithful *for those callers*.

But `FUN_00427ed0` (a direct `0x497ced` new-game callee) calls the army
probes and then immediately does

```c
if (FUN_00428007(id) || FUN_00428078(id))
    FUN_0046e1b0(3, DAT_00569570, DAT_00569578, 6);   // place something AT the found cell
```

— reading the globals the probe's box scan left behind.  When
`FUN_00427ed0` is ported, the box scan **must** expose the matched
(col, row) (an out-param, or by actually writing the globals), or the
placement will silently target the wrong cell.  Flagged here and in
`engine_map_query.c` so the trap is seen before, not after.

**Resolution (FUN_00427ed0 chip).**  Took the out-param route: `box_scan`
and the two box predicates gained `int *out_col, int *out_row`, written
on the return-1 path and passed through `army_near` up to the public army
probes (`engine_game_init_army_near_rawflag_bit0/_only_bit1`).
`engine_game_init_place_army_starts` reads them with the same `||`
short-circuit retail uses, so the cell handed to the placer
(`engine_territory_place_entity`) is the one the winning probe matched —
no global clobber, no clash with `engine_path`'s nearest-cell result.
Callers that ignore the position (the ring finder, region_clear) pass
NULL.  A host test (`game_init_army_probe_reports_matched_cell`) pins the
reported cell; the two `place_army_starts` tests confirm the marker lands
at the matched cell.

> 📍 `src/engine_map_query.c` (`box_scan` + predicates, out-params),
> `src/game_init.c` (`engine_game_init_place_army_starts`),
> `src/engine_territory.c` (`engine_territory_place_entity` = FUN_0046e1b0),
> Ghidra `FUN_0046cfbd` @ 0x46d083/0x46d0ae, `FUN_00427ed0` @ 0x427f0a.

---

## 131. The all-cells grid clearer is unrolled 8 cells wide and steers itself with the nearest-cell result globals

`FUN_0046c5b8` zeroes one byte of every overworld cell (4096 cells, 8 B
each), and a one-line C loop would do it — but retail hand-unrolls the
sweep **8 cells per inner iteration** (eight `mov byte [...], 0` at cell
offsets +0, +8, … +0x38) and runs the two enclosing loops on
`DAT_00569578` (0..0x40) and `DAT_00569570` (0..8), advancing a byte
cursor `DAT_0053e8b8 += 0x40` each step.

The charming part: `DAT_00569570` / `DAT_00569578` are the *same two
globals* `engine_path` uses to report its nearest-found cell (quirk
#130).  A grid-wipe has no "result," so here they're pure loop counters —
the function just grabbed two convenient zero-page dwords.  After it
returns they hold (8, 0x40), meaningless leftovers.  Like the box scans,
our port (`engine_territory_clear_cell_field`) uses locals and leaves
engine_path's result globals alone; the byte cursor `DAT_0053e8b8` and
the sibling `DAT_0053e974` (in `FUN_0046df51`) are likewise write-only
scratch reset at entry, never read across calls, so they collapse to the
loop variable.  r2 (`pdf @ 0x46c5b8`) was needed to see the stores are
byte-sized and the parameter is the low-byte field offset — Ghidra's
unrolled render hid both.

> 📍 `src/engine_territory.c` (`engine_territory_clear_cell_field`,
> `engine_territory_clear_cell_render_flags`),
> Ghidra `FUN_0046c5b8` @ 0x46c5b8, `FUN_0046df51` @ 0x46df51.

---

## 132. The per-key cell roster's overflow guard allows a key (17) the destination table can't hold — a latent 1-int OOB defused only by the producers' tighter gate

`FUN_0046da4b` rebuilds a per-key roster (retail `DAT_0053ea00` = save
entry 12, `g_misc_1360`) that groups active overworld cells by their +7
"group-key" byte: row *k* lists up to 20 cell byte-offsets whose key ==
*k*.  The destination is exactly **1360 bytes = 17 rows × 20 × 4** — rows
0..16.  But the scan's accept guard is `key != 0 && key < 0x12`, i.e. it
admits keys **1..17**, and a key of 17 indexes `roster[17*20 + c]` =
i32 index 340.. = the first int *past* `g_misc_1360`.

The clear half is narrower still: it zeroes rows **1..16 only**
(`for (k = 1; k < 0x11; …)`), so even row 0 and the would-be row 17 are
left untouched at entry.  The write never actually lands out of bounds in
practice because the +7 field is produced clamped to **1..16** — every
other reader gates it `< 0x11` (see `engine_strat_cost.h`, "+7 a
level/count, gated `< 0x11`").  So `< 0x12` here is just one-too-loose;
17 is unreachable.  The port preserves the `< 0x12` guard verbatim (a
faithful drop-in keeps retail's latent bug) and documents it rather than
silently clamping; the host tests stay within keys 1..16, and a 340-i32
roster buffer + ASan would catch any regression that fed a 17 through.

Second, smaller blind spot in the same function: the "free column"
sentinel is the value **0**, which is also cell (0,0)'s own byte-offset.
A feature at cell (0,0) would store 0 — indistinguishable from "empty" —
so it could never register distinctly and a later same-key cell would
overwrite its slot.  Harmless because cell (0,0) is map-border terrain,
never an active feature.

The **per-army adjacency builder `FUN_00467ca6`** shares this exact loose
guard against a 17-row block: it accepts a cell's owning-army key with
`key != 0 && key <= 0x11` (1..17) and indexes the per-army record block
(retail `DAT_0053f9b0` = save entry 4, **17 rows × 0x300 B**) at
`base + key*0x300`, so a centre cell with key 17 would write its
neighbour-count (`+0x5a`) and list (`+0x5c`) one 0x300-row *past* the
block.  Same defuse — the +7 producers clamp to 1..16, so a key-17 *centre*
never occurs — and the same verbatim preservation (`engine_territory_build_army_adjacency`).
Note a neighbour key of 17 is harmless: it is only stored as a list *byte*,
never used to index the block; the OOB needs key 17 as the swept centre.

The **per-army feature-seeding pass `FUN_00467a36`** is a third instance of
the same loose guard against the **same** 17-row per-army block: it accepts a
cell's owning-army key with `key != 0 && key < 0x12` (1..17) and bumps that
army's active-cell tally at `base + key*0x300 + 0x205`, so a key-17 centre
would write its tally one 0x300-row past the block.  Identical defuse (the +7
producers clamp to 1..16) and verbatim preservation
(`engine_territory_seed_army_features`).

> 📍 `src/engine_territory.c` (`engine_territory_rebuild_cell_roster`,
> `engine_territory_build_army_adjacency`, `engine_territory_seed_army_features`),
> `docs/findings/game-start.md` §"FUN_004676e0", Ghidra `FUN_0046da4b`
> @ 0x46da4b, `FUN_00467ca6` @ 0x467ca6, `FUN_00467a36` @ 0x467a36.

---

## 133. The overworld neighbour predicate's bounds check is load-bearing, not defensive — and the strategic map is a doubled-row hex grid

`FUN_0046de94` (the per-tile eligibility predicate behind the new-game
feature-spread) opens with a four-way signed bounds ladder —
`-1 < col && -1 < row && col < 0x41 && row < 0x81` — and returns 0
(ineligible) for anything off-grid.  That guard looks like ordinary
defensive coding, but it is **functional**: its caller `FUN_0046de05`
adds a neighbour offset to (col,row) *before* the call, and for tiles on
the map border those offsets land off the edge (e.g. col `-1`, row `-2`).
Retail relies on the predicate quietly reporting "ineligible" there, so a
port that drops the bounds check as redundant (the grid read would still
be in-bounds for most coordinates) would silently corrupt feature
generation along the map edges — exactly the cells hardest to eyeball.

The reason the offsets reach off-grid at all is the second half of the
quirk: the strategic map is a **doubled-row hex grid**, so `FUN_0046de05`
picks one of *two* 8-entry `{dx,dy}` offset tables by **row parity**
(`row & 1`) — retail `DAT_004d6c28` (even) / `DAT_004d6be8` (odd).  The
two sets are genuinely different (even includes `(1,-1)(1,1)`; odd
includes `(-1,1)(-1,-1)`), the hallmark of offset-coordinate hex
neighbours.  Both tables carry a `(0,±2)` pair — the doubled-row vertical
step.  Any future strategic-map adjacency code (pathfinding, fog, feature
spread) must consult the parity-correct table, not a single 8-neighbour
set.

> 📍 `src/engine_territory.c` (`engine_territory_tile_eligible` /
> `engine_territory_compute_neighbour_mask`),
> `docs/findings/game-start.md` §"FUN_004676e0", Ghidra `FUN_0046de94`
> @ 0x46de94 / `FUN_0046de05` @ 0x46de05.

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

## 134. The capital-region orchestrator stashes-then-restores a cursor across a pass that never touches it — a vestigial round-trip

`FUN_00468d4f` (the per-army capital-region setup, the last callee of the
overworld-grid setup sequence) seats two scratch globals — `DAT_0057c8d0`
(col) / `DAT_0056846c` (row) — from the capital finder `FUN_00467fd1`,
then does this dance around the cell-collect pass `FUN_00468c41`:

```
army[+0x6c] = DAT_0057c8d0;  army[+0x6d] = DAT_0056846c;   // save
FUN_00468c41(key);                                          // collect
DAT_0057c8d0 = army[+0x6c];   DAT_0056846c = army[+0x6d];   // restore
```

But `FUN_00468c41` never writes either cursor global — it touches only
the army cell-list and per-cell bytes.  So the restore reloads exactly
the values just saved: a **no-op round-trip**.  It reads like defensive
programming against a clobber that can't happen (perhaps `FUN_00468c41`
*used* to use the cursor, or the author wasn't sure).  The save half is
*not* dead, though — `+0x6c`/`+0x6d` are real record fields the strategic
AI reads later, so the port keeps the stash write and drops only the
identity read-back.

A second charm in the same function: the capital cursor it preserves is
the capital finder's **4th** matching cell, while the sibling secondary
finder `FUN_00468121` seats the cursor on its **1st** match — the two
finders disagree on which cell "the cursor" means, and the orchestrator
faithfully records both (`+0x6c/6d` vs `+0x74/75`).  The capital cursor's
only live consumer is the port placer `FUN_0046902a`, which uses it as a
Chebyshev distance origin — so a coastal port is sited nearest the 4th
castle-candidate cell, not the capital itself.

> 📍 `src/engine_territory.c` (`engine_territory_setup_capitals`),
> Ghidra `FUN_00468d4f` @ 0x468d4f.

## 135. The per-army terrain census zeroes its five counters with a daisy-chain of copies, not five stores

`FUN_00469b8d` (the new-game per-realm terrain census) starts by clearing
its five tally bytes in the realm record.  A normal compiler would emit five
`mov byte [addr], 0`.  Retail instead writes a **chain of copies**:

```
army[+0x200] = 0;            // DAT_0053fbb0
army[+0x201] = army[+0x200]; // DAT_0053fbb1 = fbb0
army[+0x1ff] = army[+0x201]; // DAT_0053fbaf = fbb1
army[+0x204] = army[+0x1ff]; // DAT_0053fbb4 = fbaf
army[+0x203] = army[+0x204]; // DAT_0053fbb3 = fbb4
```

Each field is set from the *previous* field, which is only 0 because the
head of the chain was just literal-zeroed.  The result is identical to five
zero-stores — it's a no-difference idiom (the C source was almost certainly
`a = b = c = d = e = 0;` with the fields enumerated in a scrambled order, and
the compiler kept the right-to-left assignment evaluation literally).  The
scramble also quietly reveals the field *grouping*: the five touched offsets
are `0x1ff,0x200,0x201,0x203,0x204` — note `0x202` is skipped, confirming it
belongs to a different field, not this census run.  The port just zeroes all
five directly.

> 📍 `src/engine_territory.c` (`engine_territory_census_army_terrain`),
> Ghidra `FUN_00469b8d` @ 0x469b8d.

## 136. The population allocator reads a 999999 cap as "fill nothing," not "no limit"

`FUN_0044f6e7` (the new-game per-realm population / job-slot allocator)
distributes each population pool into capped buckets with a greedy `while
(counter < cap)` fill plus a round-robin that also tests `counter < cap`.
Before any of that it normalises every cap field:

```
cap = army[+0xcc + 0xc*i];
if (cap == 999999) cap = 0;
```

`999999` is the engine's "unlimited" sentinel elsewhere, so the natural
reading is "this bucket has no ceiling."  Here it does the **opposite**: a
sentinel cap becomes `0`, and `counter < 0` is never true, so the bucket
accepts nothing and its target share spills into the +0x124 leftover.  A
bucket that wants to be unbounded must instead carry a large concrete cap.

The likely intent is that `999999` marks a bucket that is *absent/disabled*
for this realm (the same value an uninitialised cap field would never
legitimately hold as a real ceiling), so mapping it to a no-fill `0` is how
the allocator skips it.  Either way the port preserves the literal compare —
mapping `999999 → 0` and leaving every other value as the cap — because the
greedy and round-robin passes both depend on it to leave sentinel buckets
empty.  The same realm record's phase-2 buckets are *also* gated to `0` by
their per-building "present" flags (+0x297/+0x2af/+0x2c7/+0x2df and the
settlement flag +0x1c3 ∧ +0x1b0); a 999999 cap is the parallel mechanism for
the ungated path.

> 📍 `src/engine_territory.c` (`engine_territory_allocate_realm_workforce`),
> Ghidra `FUN_0044f6e7` @ 0x44f6e7.

## 137. The strategic-map recenter clamps the scroll even when it refused to move it

`FUN_0043278b` (recenter-on-tile) scans the terrain-index grid for a tile
value and, on a hit, sets the scroll position to centre on it — but **only
when the view-mode global `DAT_0057cb18` is 0** (the normal map view).  The
clamp that follows, however, is called **unconditionally**, outside that
gate:

```c
if (DAT_0057cb18 == 0) {
    DAT_005651b4 = col - 4;
    DAT_005651b8 = (row & 0xfffe) - 0xc;
}
FUN_00429b1d();          /* clamp DAT_005651b4/b8 in place — always */
DAT_0053e9ac = 1;        /* below-status redraw    */
DAT_0057cac4 = 1;        /* cursor-cache enable    */
return 2;
```

So calling the recenter while in a non-normal view mode still (a) re-clamps
whatever scroll position was already there — which can *move* it if a prior
state left it out of range — and (b) latches the two redraw flags and
returns the same "found" code 2.  The hit is reported and the screen is
flagged dirty regardless of whether the view actually scrolled.  The port
mirrors this: the clamp + flag latches live after the view-mode `if`, not
inside it.

Two incidental notes from the same pair of functions:

- The centring offsets are asymmetric — column is `−4`, row is
  `(row & ~1) − 12` — and the row is floored to even first.  That `& 0xfffe`
  is the doubled-row hex grid showing through (quirk #133): a logical map
  row spans two grid rows, so the camera anchors on the even one.

- The caller `FUN_00432746` stages the capital's cell offset through the
  global `DAT_0053f0dc` purely as a write-then-read temporary
  (`DAT_0053f0dc = record[+0x70]; if (DAT_0053f0dc != 0) recenter(DAT_0053f0dc)`).
  All three call sites that drive the recenter do the identical
  write-then-read; nothing ever reads `DAT_0053f0dc` across a call boundary,
  so it is scratch, not state — the port uses a local.

> 📍 `src/engine_territory.c` (`engine_territory_recenter_on_tile` /
> `engine_territory_recenter_on_capital`), Ghidra `FUN_0043278b` @ 0x43278b
> and `FUN_00432746` @ 0x432746.  First producer of the scroll save globals
> (entries 16/17) — see the camera-mirror note in `src/engine.c`.

## 138. One per-army byte (+0x1a7) is shared between the production-level fallback and the cell-retype flag

The production-level solver `FUN_0044cfe1` (`engine_army_economy_production_output`)
writes a per-army boolean at record **+0x1a7**: it sets the byte to 1 when the
army cannot afford its rate-scaled output and falls back to a flat production
level, and clears it (to 0) both on entry and when nothing fits at all.

That same byte is what `FUN_00469d21`
(`engine_territory_retype_army_band`, `ARMY_RETYPE_FLAG = 0x1a7`) reads to decide
whether a band-retype should collapse every match after the first to type 2.  So
a flag the *economy* sets as "this realm fell back on production this turn" is
later consumed by the *terrain/cell-retype* pass — two unrelated-sounding
subsystems sharing one byte of the 0x300 per-army record.  Whether this is
deliberate signalling or just a reused scratch slot is unclear from the two
functions alone, but the coupling is real: both modules name the same offset, so
a faithful port must treat +0x1a7 as live cross-subsystem state, not a private
scratch byte of either.

> 📍 `src/engine_army_economy.c` (`ARMY_PROD_FALLBACK`, `engine_army_economy_production_output`)
> writes it; `src/engine_territory.c` (`ARMY_RETYPE_FLAG`, `engine_territory_retype_army_band`)
> reads it.  Ghidra `FUN_0044cfe1` @ 0x44cfe1 and `FUN_00469d21` @ 0x469d21.

## 139. The provisioning decision's level clamp reads one entry past its supply-cost table

The per-army provisioning decision `FUN_0044df5f`
(`engine_army_economy_provision`) prices a ration "supply level" L by indexing a
table of `(divisor, mult)` pairs at `DAT_004d6738`, stride 8.  The real ladder
is six levels, 0..5:

```
L0 (1,0)  L1 (4,1)  L2 (2,1)  L3 (1,1)  L4 (1,2)  L5 (1,3)
```

— monotonically rising cost `mult·ceil(feed/divisor)`: 0, ~feed/4, ~feed/2,
feed, 2·feed, 3·feed.  But the two level-ratchet loops clamp the index with
`if (6 < level) level = 6`, i.e. they permit **level 6**, one past the table.
At offset `0x4d6738 + 6·8 = 0x4d6768` sits not a 7th supply tier but the first
entry of the *adjacent* reciprocal-percentage table (`100, 50, 33, 25, 20, …` =
⌊100/n⌋).  So "level 6" silently means `(divisor, mult) = (100, 50)` → cost
`ceil(feed/100)·50 ≈ feed/2`, which is *cheaper* than levels 3–5 and breaks the
otherwise-monotone ladder.

Whether the original authors intended a 7-level ladder and got lucky with
adjacent data, or the clamp bound is simply off by one, the observable behavior
is a real over-read with a real (if odd) cost.  The port reproduces it exactly:
`k_supply_table` is seven entries, the 7th hard-coded to `{100, 50}`, so the
clamped index range `[0,6]` matches retail's memory reads bit-for-bit.

> 📍 `src/engine_army_economy.c` (`k_supply_table`, `supply_cost`,
> `clamp_supply_level`).  Ghidra `FUN_0044df5f` @ 0x44df5f; table at
> `0x4d6738`, percentage table it bleeds into at `0x4d6768`.

## 140. A yield-bonus helper guards a field with a branch whose two arms are identical

The production-yield helper `FUN_0044d303` (a static in the production-level
planner, `engine_army_economy.c`) reads one record field, `+0x208`, halves it,
and uses it as a percentage bonus.  But Ghidra decompiles the read as:

```c
if (*(int *)(&DAT_0053fbb8 + param_1 * 0x300) < 1) {
    local_c = *(int *)(&DAT_0053fbb8 + param_1 * 0x300);
} else {
    local_c = *(int *)(&DAT_0053fbb8 + param_1 * 0x300);
}
```

— a `field < 1 ? field : field`, where both arms load the *same* field.  The
compare sets flags that are never consumed; the two stores are byte-identical.
It's almost certainly a `MAX(field, field)`-style macro or a clamp idiom that
collapsed once the second operand turned out to be the same expression — the
sort of thing a `#define LO(x) ((x) < 1 ? (x) : (x))` template emits when
instantiated with a degenerate bound.  The net effect is a plain
`local_c = field`, so the port reads the field directly and notes the vestigial
branch in a comment rather than reproducing a no-op `if`.

> 📍 `src/engine_army_economy.c` (`prod_yield_bonus`).  Ghidra `FUN_0044d303`
> @ 0x44d303; field `DAT_0053fbb8` = per-army row `+0x208`.

## 141. The secondary-settlement reserve pass computes a ratio it immediately throws away

The secondary-settlement reserve pass `FUN_00450e46`
(`engine_army_economy_plan_secondary`) opens its gated body with:

```c
FUN_00404dc1(*(int *)(&DAT_0053fb7c + param_1 * 0x300),   /* value  +0x1cc */
             *(int *)(&DAT_0053fb88 + param_1 * 0x300));  /* cap    +0x1d8 */
iVar1 = FUN_00450fb4(param_1);
```

The first call is `percent_div(pair0_value, pair0_capacity)` — the fill
percentage of reserve pair 0 — but its return value is never assigned, stored,
or branched on.  The very next line recomputes a *different* headroom figure
(`FUN_00450fb4`, over pairs 1 and 2) and uses that instead.

`FUN_00404dc1` is a pure function (it reads no globals and guards its divisor
against zero), so the dead call has no observable effect whatsoever — it is
genuinely vestigial, most likely a pair-0 headroom check that an edit later
replaced with the pair-1/2 version at `0x450fb4` without deleting the original.
The port drops it and notes it here.  (Pair 0's value *is* still used further
down — as the surplus figure and the turns-counter dividend — just not as a
fill ratio.)

> 📍 `src/engine_army_economy.c` (`engine_army_economy_plan_secondary`).  Ghidra
> `FUN_00450e46` @ 0x450e46; dead call to `FUN_00404dc1` @ 0x404dc1.

## 142. The supply-distribution pass finds the same fullest cell twice, and returns a value nobody reads

The storage/supply distribution `FUN_0044c278`
(`engine_army_economy_distribute_stock`) opens by calling `FUN_0044c5fc`
(`seed_fullest_cell`), which writes the argmax-fill developed-cell slot into the
record's `+0x210`, defaulting to **99** when the realm has no developed cell.
The `99` is then used as the gate: `if (*(+0x210) < 99)`.  Inside that gate the
pass immediately calls `FUN_0044c53b` (`pick_fullest_cell`), which does the
**exact same argmax scan** over the same cells and the same `+0x90` fills, the
only difference being its no-developed-cell default of **0** instead of 99.  But
the gate has already proven a developed cell exists, so that default can never
bite — the second scan provably reproduces the first.  The two functions are
otherwise byte-for-byte the same loop; they exist as a separate gate-default vs
body-default pair purely so the `99` sentinel can do double duty as "no cells".
The port keeps both calls (the recompute is free and faithful).

On top of that, `FUN_0044c278` builds an `int` return value through three
branches — `key*0x100`, the fullest cell's headroom, or the turns quotient —
and its only caller `FUN_004485a5` discards it (the call is a bare statement).
So the function's entire return path is dead; only its record-field side effects
(`+0xe0/+0xe4/+0x20c/+0x210/+0x214`) matter.  Ported verbatim regardless.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_distribute_stock` /
> `_seed_fullest_cell` / `_pick_fullest_cell`).  Ghidra `FUN_0044c278` @
> 0x44c278, `FUN_0044c5fc` @ 0x44c5fc, `FUN_0044c53b` @ 0x44c53b; caller
> `FUN_004485a5` @ 0x4485a5.

## 143. One eight-slot record array, two unrelated lives — and a field that gates its own producer

The per-realm economy plumbing reuses the same eight 0xc-byte records at record
`+0xc4` (retail `DAT_0053fa74`) for two completely different jobs in two
different game phases.  At new-game setup, `FUN_0044f6e7`
(`engine_territory_allocate_realm_workforce`, the WF_COUNTER array) fills each record as a
*job-slot bucket*: `+0x00` the assigned worker count, `+0x08` that bucket's
population cap.  Per turn, the resource planner `FUN_0044f318`
(`engine_army_economy_plan_resource`) walks the *same* records by `rec_idx`
(4..7, one per resource slot) and reads `+0x00` as the **production level** to
feed the supply-target solver — then overwrites `+0x08` (the bucket cap) with
either the sweep's best level (resource slot 2) or a flat `100000` sentinel
(every other slot).  Nothing reconciles the two readings; they simply never run
in the same phase, so the cap field leads a double life as "max workers" and
"best production level / unbounded sentinel".

The aliasing has a second twist: `FUN_0044f15b`
(`engine_army_economy_sum_supply_pools`), the leaf that totals a realm group's
race-weighted supply pool, gates each army on `*(int *)(record + 0x118) > 0` —
and `+0x118` is exactly job-slot record **7**'s `+0x00` count (`0xc4 + 7*0xc`),
i.e. the slot-2 production level that `FUN_0044f318(…,2,7,…)` itself writes.  So
the pool-summation reads the very field its sibling planner produces, one record
index hard-coded into an offset that looks unrelated until you do the
arithmetic.  The race weights themselves (`DAT_004d8990`/`94`) are a baked
six-entry `.data` table — one `(poolA, poolB)` pair per nationality —
`{(6,10),(4,4),(3,10),(6,3),(13,0),(4,18)}`.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_plan_resource` /
> `_sum_supply_pools`).  Ghidra `FUN_0044f318` @ 0x44f318, `FUN_0044f15b` @
> 0x44f15b, `FUN_0044f6e7` @ 0x44f6e7 (ported as `engine_territory`'s
> WF_COUNTER); race table `DAT_004d8990`/`DAT_004d8994` @ 0x4d8990.

## 144. Three new-game preset tables share one block of bytes, and a realm's resource defaults overwrite the economy it just planned

The per-realm/country setup pass `FUN_0049bd99`
(`engine_game_init_setup_realms`) draws its starting values from three baked
`.data` tables — the per-army economy seed `DAT_004dc0d0` (stride `0x14`), the
per-player resource pool `DAT_004dc070` (stride `0x18`), and the per-player
troop counts `DAT_004dc110` (stride `0x1c`).  They look like three separate
arrays, but they are one contiguous 0x110-byte region (`0x4dc070..0x4dc180`)
read three ways: the bases are `+0x00`, `+0x60` and `+0xa0` into the same
bytes, and because the strides do not tile, a high enough index into one table
walks straight into another's rows.  Retail never trips this — each index comes
from the bounded config-screen selection (a handful of rows each) — but the
overlap is real, so the port bakes the single backing region and exposes the
three views by base-offset + stride rather than as three independent arrays.

Two ordering oddities inside the per-player init:

- The economic passes run, *then* the defaults clobber them.  For each active
  player the function calls `allocate_realm_workforce` → `provision` →
  `plan_realm` (twice), all of which write the realm's resource fields — and
  only *after* that does it slam `+0x120`/`+0x128`/`+0x130` to a flat `0x32`
  and reload `+0x140..` from the resource preset.  So whatever the planner
  computed for those three pool fields at setup is immediately discarded; the
  first real values come from the next turn's planner.

- A four-iteration loop that ignores its own counter.  Retail clears player
  record `+0x80` inside `for (i = 0; i < 4; i++)` but the body never indexes
  `i`, so it writes the same dword four times — a redundant zero, folded to a
  single write in the port.  (The neighbouring `+0x140` loop *does* index `i`,
  so the two are not the same pattern misread.)

The pass is also the **producer** of the per-player strategic block
(`g_player_strategic_352`, retail `0x57bf00`) whose `+0x120` sub-view the
per-turn economy planner later reads as its resource-demand pool, and of the
runtime-only troop muster pool (`DAT_0053f6a0`, 6 × 0x80, not persisted) that
the recruitment UI and the deferred battlefield spawn (`0x4a9a9a`) consume.

> 📍 `src/game_init.c` (`engine_game_init_setup_realms`).  Ghidra
> `FUN_0049bd99` @ 0x49bd99; preset tables `DAT_004dc070`/`DAT_004dc0d0`/
> `DAT_004dc110` @ 0x4dc070; r2 `pdf @ 0x49bd99` confirms the byte/dword field
> widths, the `+0x80` no-index loop, and the post-plan `0x32` overwrite.

## 145. The emigration pass's source-list fill has no break — the first feeder claims all 16 slots

The per-realm emigration pass `FUN_0044a6ba`
(`engine_realm_migrate_population`) moves population from a realm toward its
most-popular neighbour, and bookkeeps the flow both ways: each donor stamps its
chosen destination in its own record (+0x58), and each destination keeps a
16-slot list of the donor realms feeding it (+0x48..+0x57).  The list is filled
by walking it for an empty slot:

```c
for (i = 0; i < 0x10; i++)
    if (dest[0x48 + i] == 0)
        dest[0x48 + i] = (byte)donor_key;   /* <-- no break */
```

There is no `break` after the write (r2 @0x44a91e — the slot-set falls straight
through to the loop increment, not to the loop exit).  So the *first* realm to
feed a given destination does not occupy one slot — it occupies **all sixteen**,
because every slot it sees is still zero.  Every later donor to the same
destination then finds the list completely full and is never recorded, even
though its contribution *is* still added to the destination's inflow total
(+0x40).  The third pass, which scans the list to find the destination's single
largest contributor (recording it in +0x44 / +0x59), therefore only ever sees
the first feeder — so +0x44/+0x59 report the *first* donor, not necessarily the
*largest*.  The inflow total stays correct; only the "top contributor"
attribution is affected.

Retail tolerates this because the attribution is cosmetic (it surfaces in an
event/notification, not in the population maths), and most destinations have a
single dominant feeder anyway.  The port keeps the loop break-less, byte-exact;
a host test (`engine_realm_migrate_source_list_no_break`) pins the behaviour
with two donors whose order would flip the answer if a break were added.

> 📍 `src/engine_realm.c` (`engine_realm_migrate_population`).  Ghidra
> `FUN_0044a6ba` @ 0x44a6ba; r2 `pdf @ 0x44a6ba` confirms the missing break in
> the +0x48 fill loop and the pass-3 max-contributor scan over the same list.

## 146. Random events aren't independently random per realm — one RNG draw seeds a deterministic walk through a fixed, weighted deck

The per-realm event-onset pass `FUN_00448819`
(`engine_realm_roll_events`) decides which "random" event, if any, befalls each
realm this turn.  The mechanism is not a per-realm dice roll.  A single cursor
is seeded **once** at entry from the cached 7-bit RNG byte (`DAT_0058fd60 * 2`,
range 0..254) and then walked **one slot forward per realm** through a fixed
256-entry deck (const table `DAT_004d6108`):

```c
cursor = rng_7bit * 2;
for (realm = 1; realm <= count; realm++) {
    cursor++; if (cursor > 0xff) cursor = 0;
    if (gates_open && DECK[cursor] != 0) roll DECK[cursor] for this realm;
}
```

Two consequences fall straight out of this:

1. **Adjacent realms are correlated, not independent.**  Realm *r* reads deck
   slot `(2·rng + r) mod 256`.  The entire turn's event assignment is fully
   determined by one 7-bit number; reordering the realm list would reshuffle
   who gets what.  Only one RNG value is consumed for the whole pass, no matter
   how many realms exist.

2. **The deck is sparse and weighted by duplication.**  Of the 256 slots only
   **26** are non-zero (all at the same +7/+15 column within each 16-byte row),
   so most cursor positions roll nothing — events are rare by construction.
   The non-zero slots are not distinct: id `0x8b` appears twice (deck[55],
   deck[87]) and `0x8a` twice (deck[39], deck[159]), so those events are
   twice as likely to land as the singletons.  The deck *is* the probability
   table; there is no separate weighting logic.

The gate that lets any of this fire at all is the era counter
`DAT_00553edc > 0x4f4` (plus the owner's per-player events-enabled flag), so
events only begin well into a game.  The drop-in reproduces the deck byte-exact
and the single-draw walk; a host test
(`engine_events_scratch_and_gates`) pins the cursor arithmetic by landing a
chosen realm on a chosen deck slot via the seed.

> 📍 `src/engine_realm_events.c` (`engine_realm_roll_events` + `EVENT_DECK`).
> Ghidra `FUN_00448819` @ 0x448819; r2 `pdf @ 0x448819` confirms the
> `mov eax,[0x58fd60]; add eax,eax` cursor seed and the per-realm `inc`/`cmp
> 0x100` walk; deck dumped via `p8 512 @ 0x4d6108`.

## 147. In spring and winter a realm can collapse without the player ever being told

The per-realm mood/standing machine `FUN_00449889`
(`engine_realm_update_mood`) recomputes each realm's "standing" band (army
`+0x21b`, 1=collapsed .. 5=peak) from a mood accumulator, then runs side
effects: a band-1 collapse posts notification code `0x8f` to the local player
and stamps an unrest marker (overworld cell type `0x18`); a band-5 peak posts
`0x90` and stamps type `0x17`.  But in event-modes 1 and 4 (two of the four
seasons) the pass first **demotes** the two best bands toward zero — band 1
always becomes band 0, and band 2 becomes 0 when the accumulator still exceeds
74 — and *then* checks the band for the notify/marker step:

```c
band = classify(acc);
if (mode == 4 || mode == 1) {        /* demotion runs FIRST */
    if (band == 1) band = 0;
    else if (band == 2 && acc > 74) band = 0;
}
clear_old_markers(realm);
if (band == 1) { notify(0x8f); mark(0x18); }   /* reads the DEMOTED band */
if (band == 5) { notify(0x90); mark(0x17); }
```

Because the marker/notify checks read the post-demotion band, a realm that
genuinely hit band 1 in mode 1 or 4 is already band 0 by the time the `== 1`
check runs, so it gets **neither the collapse notification nor the unrest
marker** — only peaks (band 5, never demoted) still fire in those modes.  The
collapse marker/notice is effectively a mode-2/3-only event for the bottom
band.  Whether this is intentional (collapses are "absorbed" silently in those
seasons) or an ordering oversight is unknown, but the drop-in reproduces it
verbatim (`test_engine_realm_mood_mode1_demotion` pins the band-0 outcome; the
notify/marker tests run in mode 3 to keep band 1 alive).

A smaller companion quirk lives in the helper `FUN_00449d6e`
(`realm_mood_delta`), which maps a realm's disposition byte (`+0x21e`) to a
mood adjustment under the same event-mode.  Its mode-2 arm has a **dead
duplicate**: it tests `disp == 4` twice in a row, so the first arm returns −12
and the second (which would return −24) is unreachable; mode 2 also never
handles `disp == 3` (falls through to 0).  Ghidra renders both `disp == 4`
comparisons faithfully; the port keeps the first and drops the dead second arm
with a comment.

> 📍 `src/engine_realm.c` (`engine_realm_update_mood` + static helpers
> `realm_mood_delta` / `realm_clear_marker_cells` / `realm_mark_next_cell`).
> Ghidra `FUN_00449889` @ 0x449889 (helpers 0x449d6e / 0x46942c / 0x469a9c);
> r2 `pdf @ 0x449889` confirms the per-mode base delta (`BASE − rng_b/8`), the
> demote-before-check ordering, and the duplicate `cmp …, 4` in 0x449d6e.

## 148. A realm's army count is reset every turn — but the i32 beside it just keeps growing

The per-realm army recount pass `FUN_00451061`
(`engine_realm_recount_armies`) rebuilds each realm's count of placed armies
(territory records whose kind byte `+0x08` is 3).  It does this in two passes:
pass 1 zeroes the per-realm scratch, pass 2 walks all 150 territory records and
bumps the owning realm's counters for each army.  The catch is that the two
counters it bumps are reset asymmetrically:

```c
for (realm = 1; realm <= count; realm++) {
    army[realm].count_b   /* +0x1a4, byte */ = 0;   /* reset */
    army[realm].flag      /* +0x15c        */ = 0;
    army[realm].last_idx  /* +0x1a5        */ = 0;
    /* NOTE: +0x1a0 (i32) is NOT cleared here */
}
for (t = 1; t < 0x97; t++)
    if (terr[t].kind == 3) {
        realm = terr[t].owner;              /* +0x10 */
        army[realm].count_b += 1;           /* +0x1a4 — live count   */
        army[realm].count_i32 += 1;         /* +0x1a0 — accumulates! */
        army[realm].last_idx = t;           /* +0x1a5                */
    }
```

So after every turn the byte at `+0x1a4` holds the realm's *current* army count
(reset-then-recounted), while the i32 at `+0x1a0` holds a *running total* — it
is only ever incremented, never cleared by this pass, so it climbs by the army
count each turn without bound.  Two adjacent fields, fed from the same loop,
with opposite lifetimes.  Whether `+0x1a0` is a deliberate lifetime accumulator
cleared elsewhere (new-game init) or a latent non-reset is unknown; the drop-in
reproduces the asymmetry verbatim
(`test_engine_realm_recount_armies_i32_accumulates` pins the byte at the live
count and the i32 climbing across two recounts).

> 📍 `src/engine_realm.c` (`engine_realm_recount_armies`).  Ghidra
> `FUN_00451061` @ 0x451061; the kind-3 = placed-army mapping comes from
> `engine_territory_place_entity`'s army-start caller (kind=3, type=6).

## 149. The tax-approval pass has six policy rows for a seven-value policy — the seventh reads off the end into the next table

The per-realm tax-policy → approval-modifier pass `FUN_0044bd9f`
(`engine_realm_apply_tax_approval`) turns a realm's tax-rate policy (`+0x15d`,
clamped 0..6 by its writer at 0x36435) into the approval modifier the
roll-forward (`engine_realm_advance_rating`) later folds into the main rating.
The core is a 2D lookup: a signed adjustment indexed by `[policy] × [band]`,
added to the realm's tax-approval value.

```c
adj = tbl_0x4d64a8[policy * 5 + band];   /* policy 0..6, band 0..4 */
```

The table at `0x4d64a8` has **six** rows (one per policy 0..5), `5 × i32`
each — 0x78 bytes, ending at `0x4d6520`.  But the policy is clamped to **0..6**,
not 0..5.  Policy 6 indexes `6 * 5 = 30` entries in, which lands exactly at
`0x4d6520` — the start of the *band-threshold table* that immediately follows in
`.rdata` (and which this very pass also reads, via
`engine_math_lookup_threshold`, to classify the result).  So for a 100%-tax
realm the "adjustment" isn't a happiness penalty at all; it's the threshold
table's `(cutoff, band)` pairs reinterpreted as adjustments:

| policy 6, band | reads | value |
|---|---|---|
| 0 | cutoff[0] | 10 |
| 1 | band[0]   | 0  |
| 2 | cutoff[1] | 35 |
| 3 | band[1]   | 1  |
| 4 | cutoff[2] | 65 |

Adding +10/+35/+65 to *approval* for the highest tax bracket is plainly not the
intent — the row is simply missing and the read falls through into the next
table.  Because the two tables are read across this boundary, the drop-in bakes
them as one contiguous `k_tax_approval_tbl[40]` so the aliasing reproduces
exactly (`test_engine_realm_tax_policy6_aliasing` pins the +35 read for policy 6
band 2).  This is the same "one block of bytes, two tables" hazard as quirk
#144, but here the overlap is read accidentally rather than by design.

A smaller sibling oddity lives in the classification step.  The threshold table
is `(10,0) (35,1) (65,2) (90,3) (100,4)` with fallback band 3, and
`engine_math_lookup_threshold` returns the first band whose cutoff is *strictly
greater* than the value.  The pass clamps tax-approval to a maximum of 100 — but
`100 < 100` is false, so a realm pinned at exactly 100 falls through to the
**fallback band 3**, one band *below* a realm sitting at 99 (which gets band 4).
Maxing out tax-approval quietly costs you a band
(`test_engine_realm_tax_clamp_high_and_100_quirk`).

> 📍 `src/engine_realm.c` (`engine_realm_apply_tax_approval`, `k_tax_approval_tbl`).
> Ghidra `FUN_0044bd9f` @ 0x44bd9f; tables r2-dumped from 0x4d64a8 (2D), 0x4d6520
> (thresholds) and 0x4d6548 (band modifiers).

## 150. The development-spread pass finds the best cell twice in a row, throwing the first answer away

The per-realm territory-development spread pass `FUN_0044bf7a`
(`engine_realm_spread_development`) distributes each realm's surplus pool across
its developable overworld cells, starting from the cell with the highest
current development value.  Finding that cell is delegated to a tiny helper that
scans the realm's 20-slot roster, skips empty cells and cells whose terrain type
is already ≥ 0x19, and records the slot with the maximum `+0x90` value into
`+0x210`.  Retail ships the helper as **two** near-identical copies that differ
only in what they leave in `+0x210` when *no* cell qualifies: `FUN_0044c5fc`
writes the sentinel 99, `FUN_0044c53b` writes 0.

The distributor `FUN_0044c093` opens by calling **both**, back to back:

```c
realm_dev_best_cell(realm, 99);            /* FUN_0044c5fc → +0x210 */
int pool = army[realm].pool;               /* +0xdc — unrelated read */
int slot = realm_dev_best_cell(realm, 0);  /* FUN_0044c53b → +0x210 (overwrites) */
```

The only thing `FUN_0044c5fc` does is write `+0x210`, and `FUN_0044c53b`
overwrites `+0x210` before it is ever read — so the first call is pure dead
work.  The two are not interchangeable in general (their no-cell defaults differ,
and `FUN_0044c278` genuinely relies on the 99 sentinel to detect "no developable
cell"), but *here* the sentinel result is discarded.  The drop-in keeps the
redundant call so the write sequence to `+0x210` matches retail exactly.

Most likely the distributor was cloned from the projector `FUN_0044c278` — which
legitimately calls the default-99 variant first (to set the sentinel) and the
default-0 variant later (to pick a start cell) — and the now-pointless first call
came along for the ride.

> 📍 `src/engine_realm.c` (`realm_dev_spread_pool`, `realm_dev_best_cell`).
> Ghidra `FUN_0044c093` @ 0x44c093; the two argmax variants are `FUN_0044c5fc`
> @ 0x44c5fc (default 99) and `FUN_0044c53b` @ 0x44c53b (default 0).

## 151. A realm's displayed tax income and its actually-collected tax income are two different numbers — and only one of them can be suppressed

Two near-identical passes compute a realm's tax income from the same inputs but
write to two different per-realm fields:

  - `FUN_0044b59b` (`engine_realm_accrue_income`) — the per-turn **accrual**
    pass.  Writes the collected income to `+0xbc` and credits it to the owner's
    treasury.
  - `FUN_0044b80b` (`engine_realm_recompute_realm_income`) — the live
    **projection** recompute, run both from the per-turn re-plan pass
    `FUN_00448545` and directly from the management UI / strategic events
    whenever a tax setting changes (six call sites).  Writes to `+0xc0` and
    credits no treasury; instead it refreshes the per-player projected-income
    total `+0x15c` (via `FUN_0044ba35`).

The band→rate→`percent_of(pop,rate)`→`percent_of(·,taxsetting)` arithmetic is
byte-for-byte identical between the two — with one deliberate omission.  The
accrual pass gates on the per-realm turn-scratch byte `+0x1a8` (set by events
such as `0x13b`), forcing the rate (and therefore the collected income `+0xbc`)
to zero that turn:

```c
/* accrual (FUN_0044b59b) */            /* projection (FUN_0044b80b) */
if (army[r].scratch_1a8 == 0)           /* NO +0x1a8 check at all —      */
    rate = rate_for_band(band);         /*   rate is always computed     */
else
    rate = 0;
```

The projection pass has no such check.  So when an event suppresses a realm's
income for the turn, the treasury is correctly credited **0**, but the figure
the recompute leaves in `+0xc0` / the player total `+0x15c` is the *full*
un-suppressed amount.  The two numbers diverge precisely on the turns where a
strike/blockade-style event bites — the displayed projection keeps promising
income the realm will not actually collect.

A third sibling difference: the projection pass writes its rate-adjustment to
`+0x0f` and a tax-level tally byte to `+0x16` (looked up from a `.data` table at
`0x4d63d8`), whereas the accrual pass writes its adjustment to the *adjacent*
`+0x0e`.  The `0x4d63d8` table is sized for exactly the tax setting's live range
(0..50, the cap its incrementer enforces): indices 0..19 are zero and 20..50
ramp `-1 … -15`, and entry 51 is the last zero before the *tax-approval* table
(`0x4d64a8`, quirk #149) begins — so a tax setting above 50 would read straight
into that neighbouring table, the same off-the-end hazard as #149 / #139, kept
latent only by the 0..50 clamp.

> 📍 `src/engine_realm.c` (`engine_realm_recompute_realm_income`,
> `realm_refresh_player_income_totals`, `k_taxsetting_tally_modifier`).
> Ghidra `FUN_0044b80b` @ 0x44b80b, `FUN_0044ba35` @ 0x44ba35; tally table
> r2-dumped from 0x4d63d8.  Field semantics (+0xc0 displayed vs +0xbc collected)
> provisional but the suppression-gate asymmetry is exact.  Verified byte-exact
> vs retail (`tools/diff_test.py --functions realm_income`, 24/24).

## 152. The AI pays up to a third of the human's unit upkeep — and the discount is hardcoded to the difficulty

The per-turn unit-wage computation `FUN_004ad52b` (`engine_unit_recompute_wage`)
derives each standing unit's wage from its value (`+0x168`) — but the divisor
depends on **who owns it**:

```c
if (strategic[owner] + 0x05 == 0)            /* AI-controlled owner */
    wage = value / {3, 5, 10, 10}[difficulty];   /* s_slot_0053f23c */
else                                         /* human owner */
    wage = value / 4;                            /* signed, toward zero */
```

A human always pays `value/4`.  An AI pays `value/3` on the easiest setting
(difficulty 0) — *more* than the human — but `value/10` on the two hardest
settings, i.e. **2.5× cheaper upkeep than the human for the identical unit**.
The fourth difficulty slot aliases the third (both `/10`), so the table is
really three distinct rates for four difficulties.  Because the wage is summed
per player (`FUN_004ad495`) and compared against the owner's resource account to
decide whether units must be disbanded, this single divisor is a quiet but
large AI economic handicap: at high difficulty the AI can sustain ~2.5× the
army the human can on the same income.

There is no corresponding human discount anywhere — the `value/4` arm is
unconditional once the owner's `+0x05` flag is set.  The same `+0x05` byte is
the "events-enabled" gate read by the random-event pass (quirk #146), so a
single per-player flag doubles as "is a real (human/active) player" across
multiple subsystems.

> 📍 `src/engine_realm_upkeep.c` (`engine_unit_recompute_wage`,
> `engine_unit_total_wage`).  Ghidra/r2 `FUN_004ad52b` @ 0x4ad52b,
> `FUN_004ad495` @ 0x4ad495.  Verified byte-exact vs retail
> (`tools/diff_test.py --functions upkeep`, 64/64).

## 153. The winter-season production adjustment scales the wrong number — the cap, not the yield it just computed

The seasonal production pass `FUN_0044c8ae` computes a season's output, then
applies a model-type "fudge" to it before storing.  For three of the four
seasons the fudge scales the value it just computed.  But the **season-4
(winter)** branch scales a *different* field:

```c
out = yield_phase4(key, ref, cap);   /* cap = record +0x244 */
record[+0x248] = out;
/* model-type adjustment — note it reads +0x244 (the CAP), not `out`: */
if (type == 5) record[+0x248] = cap / 4;     /* not out/4 */
if (type == 4) record[+0x248] = cap / 2;     /* not out/2 */
if (type == 0) record[+0x248] = cap / 2;
if (type == 2) record[+0x248] = cap * 3 / 2;
```

The other three seasons (`type`-gated by the same byte `+0x21b`) adjust the
freshly-computed output in place.  Winter alone, whenever the realm's model
type is one of {0,2,4,5}, throws away the `yield_phase4` result entirely and
substitutes a fraction of the **input cap** — so the army's stored winter
output bears no relation to what the yield evaluator returned, and the
`+0x24c` delta field (stored output − raw yield) records the full difference.
Only a model type *not* in {0,2,4,5} (e.g. type 1/3/6) leaves the genuine
yield in place for winter.

Whether this is a deliberate "winter caps production at a fraction of capacity"
rule or a copy-paste slip from the season-1 branch (which legitimately reads
its own `+0x240` output) is impossible to tell from the binary — but it is
load-bearing: it runs every winter turn for every army and is reproduced
exactly by the port.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_season_update`).
> Ghidra/r2 `FUN_0044c8ae` @ 0x44c8ae.  Verified byte-exact vs retail
> (`tools/diff_test.py --functions season_economy`, 128/128 across seeds).

## 154. The "resource demand pool" is not its own table — it's accounts inside each player's strategic record, and resource production is booked as cashflow income

The resource-delivery pass `FUN_0044e852` and its leaves (`FUN_0044ea92`
quota-apply, `FUN_0044edc2` cell-stamp) read and write a per-group "demand
pool" addressed as `DAT_0057c020 + group * 0x160`.  That looks like a standalone
6×0x160 runtime table.  It is not:

```
g_player_strategic_352 base = 0x57bf00   (0x160 stride, players 0..5)
"resource pool" base        = 0x57c020 = 0x57bf00 + 0x120
```

`0x57c020` is **field +0x120 of player 0's strategic record** — so
`pool[group]` is just `strategic[group] + 0x120`.  The "pool" fields are
economic accounts living in the same record the leaderboard/standings code
already owns:

| pool offset | strat offset | meaning |
|------------:|-------------:|---------|
| +0x00 / +0x04 | +0x120 / +0x124 | commodity-B demand (this turn / prior turn) |
| +0x08 / +0x0c | +0x128 / +0x12c | commodity-C demand |
| +0x10 / +0x14 | +0x130 / +0x134 | commodity-A demand |
| +0x20 + race·4 | **+0x140 + race·4** | slot-2 per-race accumulator |

That last row is the punchline: the slot-2 per-race accumulator at pool +0x20
is the **same memory** as the strategic record's six cashflow category
sub-accounts at +0x140..+0x154 — the very fields `FUN_004487a9`
(`engine_ranking_recompute_player_total`) sums into the +0x138 cashflow total.
So when a realm delivers slot-2 production, `FUN_0044ea92` credits the
race-indexed cashflow category, debits the A/B commodity columns by the race
weights, and then calls `recompute_player_total` to fold the new income into
the player's running total — production and the economy/standings ledger are
literally the same bytes.

Consequence for the diff harness: there is **no separate global to inject**.
Binding the port's resource pool to `g_player_strategic_352 + 0x120` and
injecting/reading-back the strategic block alone covers every pool write *and*
the cashflow resync.  A reader who treated `0x57c020` as an independent table
would either miss the strategic-record aliasing or double-allocate it.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_resource_pass` +
> `..apply_resource_quota` / `..stamp_resource_cell`).  Ghidra/r2
> `FUN_0044e852`/`FUN_0044ea92`/`FUN_0044edc2`.  Verified byte-exact vs retail
> (`tools/diff_test.py --functions resource_pass`, 24/24 across seeds 0+7).

## 155. When a realm's population crashes below 1, the engine records a *negative* death count

`FUN_00449ef3` (`engine_realm_advance_population`), the per-realm population
turn pass, advances the head-count at `+0x24` by `births (+0x30) − deaths
(+0x34)`.  When that net change drives the head-count below 1, retail runs a
crash-floor block:

```c
*pop += *births - *deaths;
if (*pop < 1) {
    *births = 0;
    *deaths = *pop;     // <-- stores the (≤ 0) leftover head-count into deaths
    *pop    = 0;
}
```

So a realm that ends the turn at, say, −1 has its **deaths field set to −1**,
not to the number of people who actually died.  The births field is zeroed and
the head-count floored to 0, but the deaths field is overwritten with the
signed remainder.  Whatever per-turn report reads `+0x34` for a depopulated
realm shows a negative death toll — almost certainly a bug nobody noticed,
because a realm that hits zero population is usually about to be removed anyway.

The port reproduces this verbatim (`*deaths = *pop` before the floor), and the
retail diff (`tools/diff_test.py --functions population`) exercises the branch
with a 2-head realm in region 0 (the +2-deaths terrain) at zero standing — both
sides land on `deaths == -1`.

> 📍 `src/engine_realm.c` (`engine_realm_advance_population`).  Ghidra/r2
> `FUN_00449ef3`.  Verified byte-exact vs retail (`tools/diff_test.py
> --functions population`, 200/200).

## 156. The active 8bpp palette is screen-state-owned — a menu→map transition must reinstall the strategic palette or the map presents under the menu's colours

The game runs a single 256-colour DirectDraw palette at a time, and **the
title-menu page backgrounds swap it.**  `gfx_init` installs the base game
palette `base01.256` (the one the strategic map renders under — the seasonal
variation lives in the sprite *banks*, not the palette).  But each title page's
background load (`gateway.256` / `custom.256` / `skirmish.256`) installs its own
palette over it.

So when the **live load path** arrives at the strategic map from the menu, the
active palette is the last menu page's (`gateway.256`), and the map + sand HUD
parchment present as **magenta / rainbow speckle** — the correct 8bpp indices
mapped through the wrong colour table.  (Palette swaps don't touch the index
data, so a raw-index dump / `--strat-load` byte-exact validation is blind to
this — the `--strat-load` seat never shows the menu, so `base01.256` is still
active there, which is why its frame looked right.)

**The transition into a screen owns its palette.**  The drop-in's menu→map load
(`drop_in_load`) reinstalls `base01.256` after the map setup; any future screen
that arrives from a differently-palettized one will need the same.  Symptom to
watch for: a structurally-correct frame that is uniformly mis-coloured (often
magenta, since unused/parchment indices land on magenta in foreign palettes) —
suspect the palette before the render.

> 📍 `src/main.c` (`drop_in_load` → `gfx_load_palette("base01.256")`); the
> menu page palettes are in `gfx.c` (`gfx_title_background_load`).  Found
> live 2026-06-02 when the wired menu→load path rendered the map magenta.

---

## 157. A loaded save's camera scroll survives because the load path skips FUN_00498270's default-scroll write

The strategic-map scroll origin (`DAT_005651b4` col / `DAT_005651b8` row) is a
**saved** field (save-schema entries 16/17), so a `.sav` carries the camera the
player left the map at — `lastturn.sav` is at col 31 / row 78, not the
new-game default 20/74.

It survives a load because the two map-setup entry points differ:

- **New game** runs `FUN_00498270`, which writes the **default** scroll
  (`DAT_005651b4 = 0x14`, `DAT_005651b8 = 0x4a`) before building the map.
- **Load** (`FUN_004adfe6` save_load → `FUN_004976a1` map setup) does **NOT**
  call `FUN_00498270` — `FUN_004976a1` is `build_cells` (4298c1) + `set_camera`
  (451fcc) + autotile/roster/banks, with no default-scroll write — so the
  scroll `save_load` just restored into `DAT_005651b4/b8` is what the camera
  (`FUN_00451fcc` derives the viewport from it) and the renderer read.

**Port gotcha (fixed 2026-06-02):** the drop-in's `engine_map_post_load`
reused `engine_map_init_strategic` (the `FUN_00498270` port) for its
build_cells + set_camera convenience, which *also* reset the scroll to 20/74 —
so a loaded map rendered at the default view, not the saved one (a glaring
terrain mismatch vs retail once the trace-replay diff put them side by side).
Fix: restore `DAT_005651b4/b8` from the save globals (`s_slot_005651b4/b8`)
after `engine_map_init_strategic`, via `engine_map_init_set_scroll`.  Note the
port keeps the scroll in a private `g_col_start/g_row_start` (not the shared
`s_slot_*` save globals), so the save restore does not reach it automatically —
the explicit copy is required.

## 158. There is no "warmup region→owner remap" — descriptor +7 is just plane-5 of the map file, and a cross-map capture invented a transformation that never happens

For ~two sessions the render notes carried a "remaining gap": loading
`lastturn.sav` produced a descriptor table whose per-cell **+7** field held
*region ids* `{1..0x0e}` (sea `0x20`), but a captured "settled" frame
(`runs/capture/strat-terrain1`) had **+7** ∈ `{0,1,2,3,4}` with sea grown
84→497.  The theory was a post-load **turn/warmup pass** that remaps
region→owner and recomputes terrain — a "spatial ownership fill" nobody could
locate in the decompile.

It does not exist.  The descriptor planes are read **verbatim** from the map
file.  `FUN_00467770` (the new-game plane-unpack) does:
`FUN_004af8d9(L2_maps.dat, buf, 0x80c1, map_id*0x80c1)` then, per 64×64 cell,
copies six 0x1000-byte planes — plane 0/1/2/3 → desc **+1/+2/+3/+4**, plane 5 →
desc **+7** (plane 4 drives feature dispatch; `DAT_0056d5dc` territory-count =
`max(plane5 < 0x11)`).  A `.sav` preserves these planes, so the load path
reproduces them.

So **+7 is the map's baked per-cell region id**, and its histogram is a map
fingerprint.  Extracting plane-5 for all 80 maps in `L2_maps.dat` (stride
`0x80c1`, plane 5 at `+0x5000`):

- **map 0** → sea 84 / neutral 2337 / regions 1675 / 16 distinct — matches
  `lastturn.sav` and *every* load capture (immediate, +30, +150, +600,
  clean+32s) **byte-for-byte** (plane5 == +7 = 4096/4096; plane3 == +4 = 100%).
- **map 17** → sea 497 / neutral 2367 / regions 1232 / **6 distinct** — matches
  `strat-terrain1` byte-for-byte (plane5 == +7 = 4096/4096; plane3 == +4 =
  100%).  Cross-pairing map 0 ↔ strat-terrain1 scores 4–42%.

`strat-terrain1` was simply captured on a **different map** (map 17, a
4-region sea-heavy scenario), not a warmed-up `lastturn.sav`.  Its "owners
`{1,2,3,4}`" are map 17's native region ids; the "sea growth" is map 17's
coastline.  The port's immediate-load render of map 0 is byte-exact and is
exactly what retail shows on load — idling 600 frames / 32 s never changes +7
(confirmed across all load captures).

**Lesson:** a descriptor field whose values look "transformed" across two
captures may just be two different inputs.  Fingerprint the *source asset*
(here: per-map plane-5 of `L2_maps.dat`) before hypothesising an engine pass —
it's a free local check that needs no frida and would have killed this phantom
on day one.  See `docs/findings/map-render.md` "L2_maps.dat plane layout".

## 159. Coalitions are adjacency-connected components per owner — built by a two-phase fixpoint that joins everyone it can before seeding a single new group

`FUN_0044b042` (the producer half of the turn-driver's coalition pass
`FUN_0044ae3c`) partitions every active army (per-army owner byte +0x05 != 0)
into "coalition groups" held in the runtime table at `0x568240` (17 groups ×
`0x1c`: +0x00 i32 strength, +0x04 owner key, +0x05.. 20 member army ids).  Two
behaviours are worth pinning down because they are easy to get subtly wrong when
porting:

**(a) The round structure is join-all-then-maybe-seed-one, not seed-then-grow.**
Each round of the bounded fixpoint (`local_c < 100`, so ≤ 99 rounds) does two
sweeps over armies `1..DAT_0056d5dc`:

1. For every ungrouped army, try to fold it into an *existing* same-owner group
   it is adjacent to (`FUN_0044b391`); count already-grouped armies into
   `local_1c`; set `bVar1` if anything joined.
2. **Only if `bVar1` is still false** (the whole first sweep joined nobody),
   seed exactly one fresh group from the first ungrouped army (`FUN_0044b302`).

So a round never both grows existing groups *and* creates a new one — creation
is the fallback for a stalled round.  The loop ends when the grouped count
reaches the active count.

**(b) Adjacency is read DIRECTED, from the joining army's list.**  An army `a`
joins a group iff `a`'s own +0x5c adjacency list (16 bytes, 0-terminated,
`FUN_00467e2c`) names *some current member* of a same-owner group — the
member's list is never consulted.  In practice the +0x5c neighbour lists are
symmetric so this rarely matters, but a hand-built asymmetric list would group
differently depending on which army seeds the group first.  The seeding order is
strictly ascending army index, so the partition is deterministic for a given
state.

The strength field (+0x00) is a plain sum of member populations (+0x24) computed
in a final pass, and the active-group count is mirrored to `_DAT_00553ef4`.
Ported as `engine_realm_build_coalitions` (engine_realm.c); the consumer half
`FUN_0044ae51` (eliminate every group but each owner's strongest — it reaches
the combat-init chokepoint `FUN_004ac3c6` and posts popups) is deferred.

## 160. The unrest pass warns a human realm exactly once, and the warning latch can leave a low-approval realm permanently calm

`FUN_0044aa41` escalates each realm's unrest band (+0x20) every turn, but the
human-player branch is gated by a one-shot warning latch (+0x21) that produces
two non-obvious behaviours.

**(a) One warning per "spell", cleared only by a real recovery.**  The first
turn a human realm's approval (+0x0c) dips below 30 it posts a single warning
(event 0x92) and sets the latch — and does *nothing else* that turn (no unrest
change).  The latch is cleared only when approval climbs back above 29.  So the
player is warned once as the realm slips, not every turn.

**(b) The escalation threshold (25) is below the warning threshold (30), so a
realm parked in [25, 29] is calm forever after its one warning.**  Once latched,
the pass escalates unrest only while approval < 25; at approval ≥ 25 it sets
unrest back to 0.  A realm hovering at 25–29 approval is below the warning line
(so it never re-warns — the latch stays set) yet at/above the escalation line
(so unrest is held at 0).  It sits in a quiet limbo: visibly unhappy, never
revolting, never re-flagged.  Only a drop below 25 starts the band climb
(0x96→0x99 notifications) toward a rebellion.

AI realms use neither the latch nor the 0x92 warning — they drift purely on
approval (≥41 calm, 11–40 ease, <1 climb).  Also note the decompile's
`approval < 1 → unrest = 4` arm in the human branch is dead code: it sits under
the `approval < 0x19` (< 25) test in the `else` of `approval < 0x19`, which can
never be entered, so the port elides it.  Ported as
`engine_realm_escalate_unrest` (engine_realm.c); the rebellion trigger
`FUN_004ac185` is a deferred bound seam (it reaches combat-init).

## 161. The player-aggregate recompute averages the per-realm "standing" byte in 8-bit arithmetic, so a populous player's average can wrap

`FUN_0049d1e0` (ported as `engine_ranking_recompute_player_aggregate`,
engine_ranking.c) recomputes one player's standings inputs from the realms it
owns: army count (+0x29), total/avg/previous troop strength (+0x10/+0x14/+0x18),
an averaged rating (+0x0c) and standing (+0x58), realm-share percent (+0x60),
and territory count/value (+0x2c/+0x54).

Two of those averages accumulate into **byte** fields before the divide, and the
binary does the running sum in 8-bit registers:

- The standing accumulator at +0x58 is a `byte += byte` (AL) add, so once the
  per-realm standing values sum past 255 it **wraps mod 256** — and the average
  is then taken of the *wrapped* value.  A player with two realms at standing
  200 sums to 400 → byte 144 → averaged /2 = 72, not 200.  This is not a sign
  bug to "fix": the resulting save byte is what retail writes, so the port keeps
  the 8-bit add (`p[0x58] = (uint8_t)(p[0x58] + r[0x0b])`).  The rating
  accumulator, by contrast, is a real `int` (sign-extended `(char)` adds), so it
  does not wrap in practice.

- All four divides are guarded on the army count: a zero-owned player gets every
  average set to 0 rather than dividing by zero.  Player 0 (the neutral/unowned
  slot) is special-cased even earlier — it only clears the four head fields
  (+0x29/+0x18/+0x10/+0x0c/+0x58) and returns before any averaging or the
  territory loop, so its +0x14/+0x60/+0x2c/+0x54 keep whatever they held.

The previous-turn strength snapshot (+0x18 ← old +0x10) is taken *unconditionally*
at the top, before the player==0 / zero-owned gates, so even an empty slot rolls
its strength history forward.  Validated 192/192 byte-exact vs retail
(`player_aggregate` diff target, 64 @ seed0 + 128 @ seed7).

## 162. Player elimination shares the victory message's rotating counter, advances it even when no message is shown, and only opens the game-over screen in multiplayer

`FUN_0049b42b` (ported as `engine_ranking_check_elimination`, engine_ranking.c)
is the exact structural mirror of the victory branch inside the standings
recompute (quirk-free `engine_ranking_recompute` Phase G): same per-player
rotating counter at +0x159, same `(rot − 4) + opp_type·4` payload, just the
defeat codes 0xe0/0xc2 in place of victory's 0xe1/0xc3.

Three non-obvious behaviours:

**(a) Liveness is weighted 3:1.**  A player is "alive" iff +0x04 (which doubles
as the active-slot flag the recompute reads) is nonzero, and it is rebuilt each
call as `3·(owned armies) + 1·(owned settled territories, kind +0x08 == 1)`.  So
a single army outweighs two territories for survival purposes; only a player
with *no* armies and *no* settled territory is eliminated.

**(b) The rotating counter advances on every elimination, even silent ones.**
The `+0x159 += 1` (wrap 0..3) sits *outside* the notification if/else, so it
ticks whenever the player is eliminated — including the third case where nobody
is notified at all (a non-local human player: not the local slot, so no 0xe0,
and +0x05 has-data != 0, so no 0xc2).  The counter and the local human therefore
drift out of step with the messages actually shown.

**(c) The game-over screen is multiplayer-only.**  The local-defeat screen
(`FUN_0043eda0(0x24, 0)`, a bound NULL seam in the port) fires only when the
eliminated player is the local slot *and* `DAT_00553030` (g_sim_mp_active,
threaded as `mp_active`) is nonzero.  In single-player a local elimination posts
the 0xe0 notification but does **not** trigger this screen here — that path is
handled elsewhere.

Validated 192/192 byte-exact vs retail (`elimination` diff target, 64 @ seed0 +
128 @ seed7); because the readback is the strategic block after the internal
re-rank, the diff also re-validates `engine_ranking_recompute` byte-for-byte.

## 163. The post-battle condition recompute returns garbage (the caller ignores it), and the survivor percent is written before being clamped

`FUN_004a80db` (ported as `engine_combat_recompute_condition`, engine_combat.c)
recomputes a battle survivor's supply condition from 3 groups (count +0x182 /
out% +0x184 / stock +0x186) against the rate table `DAT_004de440 = {200,200,400}`,
storing the shortfall averaged over the head-count (+0x168, rounded up) into the
condition byte +0x19c.

Two oddities:

**(a) The return value is dead.**  The function is declared `int` but its two
call sites (both in the aftermath `FUN_004ab383`) invoke it as a statement.  The
returned value is `shortfall / head` — except when there is a nonzero remainder,
in which case it is *overwritten* with `rec * 0x54` (the byte-stride of an
unrelated record table) right before returning.  Pure garbage; the port returns
`void`.

**(b) out% is written, then conditionally re-written.**  For each group the
percent `stock*100/(rate*count)` is stored into +0x184 first; only then is it
compared against 100 and, if it reached or exceeded 100, overwritten with exactly
100.  The intermediate write is invisible (same field), but it means an
over-supplied group lands on a flat 100, never its true ratio.

Validated 64/64 byte-exact vs retail (`combat_condition` diff target, seed 7).

## 164. The combat-aftermath diplomacy hit double-counts the loser via the broadcast penalty, and its war-declaration escalation only ever fires against a human

`FUN_004a1ee1` (ported as `engine_diplomacy_battle_adjust`, engine_diplomacy.c)
is the post-battle relations adjustment — AI-only (it no-ops unless the actor
+0x04 is set and +0x05 is clear).

**(a) The actor's own relation toward the loser is decremented twice on a
betrayal.**  When the loser was the AI's vassal, the function calls
`engine_diplomacy_broadcast_penalty(loser, 15)`, which loops *every* other realm
— including the actor itself — and subtracts 15 from its relation toward the
loser.  Then the main hit subtracts the (now +15-bumped) delta a second time.  So
the actor takes a −15 broadcast hit *plus* the −(amount+15) direct hit in one
call.

**(b) The escalating war-declaration sequence is gated on a HUMAN opponent.**
The 0xbd → 0xbe → 0xbf escalation (driven by the per-pair counter +0x87) only
runs when the bottomed-out (-30) opponent's +0x05 is nonzero — i.e. a human.  Two
AIs that hate each other never walk this ladder; they just sit pinned at −30.

**(c) The notification variant is sampled before the mood counter advances.**
Each notification's payload is `(mood-cycle − 4) + personality·4` read from the
current +0x159, which is only bumped (wrap 0..3) *after* the post — the same
rotating-counter pattern as quirks #162/#147, here shared across all four
diplomacy codes (0xb6/0xbd/0xbe/0xbf).

Validated 64/64 byte-exact vs retail (`diplomacy_adjust` diff target, seed 7),
which also covers the two callees `engine_diplomacy_clear_treaty` (4a1a54) and
`engine_diplomacy_broadcast_penalty` (4a24d1).

## 165. The combat-init SCC resolves into the UI-selected territory's region, clears quick-resolve on every dispatch, and lets a winner immediately lose a too-distant conquest

The strategic-combat chokepoint is a 5-function mutually-recursive cluster
(`FUN_004ac3c6` demolish → `FUN_00437535` transfer → `FUN_004a7158` dispatch →
`FUN_004ab383` aftermath → `FUN_004a72fe` ownership-transfer → back to demolish),
ported as one unit into `engine_combat.c`.  Four non-obvious behaviours:

1. **The aftermath transfers the region named by the UI selection, not the
   marching army.**  `FUN_004a7158` seeds the combat region scalar
   `DAT_0056822c` from `(&DAT_0052f0c0)[DAT_00553f8c * 0x1a4]` — the `+0x10`
   region key of the *currently selected* territory (`DAT_00553f8c`).  On the
   auto-resolve path `FUN_004a7158` returns 0, so the `DAT_0056822c = key`
   correction in `FUN_00437535` is skipped, and the aftermath's
   `FUN_004a72fe(winner_owner, DAT_0056822c)` hands over *that* region.  The
   transferred region is therefore coupled to the UI cursor, not to the
   battle's own location — a port that "fixes" this to use the battle region
   diverges from retail.

2. **`FUN_004a7158` clears `DAT_0053e8fc` (quick-resolve) to 0 unconditionally
   at entry**, before both the resolver and the aftermath run.  So the
   difficulty multiplier (quirk in `combat_resolve`) and the aftermath's
   `DAT_0053e8fc != 0` quick-paths *never* fire when combat is reached through
   the turn driver — those are a separate "resolve all battles" UI entry.  The
   dispatch path always takes the `recompute_condition` (FUN_004a80db) arm.

3. **The two aftermath win-branches are asymmetric.**  Attacker-win runs the
   ownership transfer (`FUN_004a72fe`) and bumps `+0x153` by 8 then, if the
   winner is ungarrisoned, resets it to `+0x154 − 1`; defender-win does *no*
   ownership transfer and bumps `+0x153` by only 7 (ungarrisoned).  The
   quick-path field clears also differ between the branches.  They are written
   out explicitly rather than factored, to preserve the byte sequence.

4. **A winner can immediately lose a too-distant conquest — and "already
   conquered" really means "already owns a realm".**  `FUN_004a72fe` first calls
   the standings aggregate `FUN_0049d1e0`, which *overwrites* the player's
   `+0x29` byte with the number of realms it currently owns.  The very next
   can-hold test reads that freshly-recomputed `+0x29`: if it is non-zero (the
   player already holds ≥1 realm) *and* the player owns no neighbour of the
   region (`FUN_00467eb0` == 0), the just-won region is demolished
   (`FUN_004ac3c6`, recursing the cycle) instead of held — modelling supply
   lines, you cannot annex a region you cannot reach.  So `+0x29` is not a
   persistent "times conquered" counter here; it is the live realm-ownership
   count the aggregate just wrote.

The auto-resolve path is pure save-state logic and is the target of the retail
diff; the interactive path (`DAT_004eac50` 0x12/0x13 screen-state, the
s080/ff_lose/ff_batl WAV cues, the battlefield render handoff) is bound through
NULL-default seams (`engine_combat_bind_wav_fn` / `_click_fn` / `_clock_fn`) and
needs human A/V verification.

The **`combat_ownership` diff target** (FUN_004a72fe, can-hold path: region
unowned, new owner holds no realms, local ≠ new owner) is **32/32 byte-exact**
(seed 7) across the army + strat + territory blocks — covering the up-front
aggregate, the region reassign, the popularity decay, and the conquest-counter
bump.  Constraining the new owner to own no realms is what keeps it off the
non-diffable demolish branch (point 4 above).

## 166. Coalition consolidation walks a hardcoded 5 player slots (not the dynamic realm count), gates on the +0x04 slot byte, and breaks strength ties toward the later group

`FUN_0044ae51` (the consumer half of the turn-driver's coalition pass
`FUN_0044ae3c`, which first rebuilds the group table via `FUN_0044b042`) keeps
each player's strongest coalition group and demolishes every member army of all
the player's weaker groups (`FUN_004ac3c6`), so a player whose armies split into
disconnected clusters loses all but the largest cluster each turn.  Three
details are worth pinning:

1. **The player loop is hardcoded `1..5`** (`cmp 6` / `jge`), *not* the dynamic
   realm count `s_slot_0056d5dc` that every other per-realm turn pass iterates.
   Slots beyond 5 are never consolidated by this pass.
2. **The in-game gate reads strategic `+0x04`** (`0x57bf04 + slot*0x160`), the
   slot-active byte — a *different* byte from the `+0x05` has-data byte that the
   event-queue filter (`engine_event_queue_post`) consults for the same slot.
3. **The strongest-group search compares with `jg`**, i.e. it updates the
   winner on `max <= strength`, so equal-strength groups resolve to the *later*
   group in the table.  (`best_group` is never explicitly initialised, but a
   live player always has at least one matching group, so it is always set
   before the demolish pass reads it.)

The notification is posted only when the affected player is the local POV
(`s_slot_0057c8cc`) *and* they held more than one group: code `0x7f` (with the
lost army id in `flag6`) when exactly one army was demolished, else code `0x80`.
Demolish's economy re-plan hits the price-scalar wall (same as the other AI
passes), so the pass is host-test verified — demolish's unconditional `+0x05`
owner-zeroing is the observable hook the tests assert on.

## 167. The player-resign routine recounts a player's surviving realms/units AFTER it has already destroyed them, so its "resigned with assets" notification is dead code

`FUN_0049cf20` (the per-player removal body run by the multiplayer resignation
sweep `FUN_0049b2a3`) does four passes in this order: (1) demolish every realm
the player owns (`FUN_004ac3c6`, army records 1..16); (2) disband every live
placed unit they own (`FUN_004aa039`, which `memset`s the occupant record);
(3) **recount** the player's realms (1..`s_slot_0056d5dc`, ×3 each) and units
(same predicate as pass 2) into a survivor tally; (4) post a notification keyed
on that tally.

Because passes 1–2 run *before* the recount over the *same* tables, the tally
is always 0:

* the realm array `g_per_army_768` holds only 17 records ("16 used + slot 0"),
  so `s_slot_0056d5dc` ≤ 16 and the 1..16 demolish loop fully covers everything
  the recount walks — every owned realm's owner byte is already zeroed;
* the unit recount uses the identical `+0x08 == 1 && owner == player` predicate
  the disband loop used, and disband `memset`s each matched record.

So the survivor count is invariably 0, and the branch that would post code
`0x102` ("player resigned", with surviving assets) is unreachable — a removed
player is *always* reported eliminated via code `0xe0`.  The recount and the
`0x102` arm are ported verbatim but are effectively dead.

Two smaller notes on the same routine: the explicit `+0x04`/`+0x0a` clears
immediately precede a full `memset` of the 0x160-byte strategic record (so they
are redundant), and the interactive on-screen cleanup (`FUN_0047fe0b`, a
0x50-entry stride-0x1b0 UI table) only runs when the game-mode byte
`s_slot_0057c8fc == 2` — deferred to a NULL-default seam in the port.


## 168. The army-stack merge inherits the absorbed unit's mercenary contract, then frees that contract's pool slot out from under the survivor

`FUN_004aa181` (the army-stack merge run when a unit stacks onto an occupied
territory, reached via the garrison-placement path `FUN_004a79a3`) folds
occupant `src` into `dst`.  When `src` is the one carrying a mercenary
contingent (the merge only runs when *at most one* side has one), it copies
src's contract triple — class `+0x195`, head-count `+0x196`, **pool slot
`+0x197`** — onto `dst`, then unconditionally disbands `src`
(`FUN_004aa039`).

The disband routes through `FUN_0046f0a9` → `FUN_004ac6fe`
(`engine_unit_clear_contract`), which frees `g_unit_table_20[src.+0x197]` —
*src's* pool slot.  But the merge never cleared src's `+0x197` after copying it,
so src still names the slot at disband time.  The result: `dst` ends the merge
holding a mercenary contract (`+0x195/+0x196/+0x197` all set) whose backing
g_unit_table_20 entry has just been zeroed.  The survivor's merc slot index is
dangling.

Ported verbatim (the merge copies the triple, the disband frees the slot) and
pinned by `test_engine_combat_merge_inherits_src_merc`, which seeds the pool
slot and asserts it reads 0 after the merge.

Verification note: `FUN_004aa181` is **host-unit-tested only**, not retail-diff
gated.  Its disband tail reaches `engine_realm_collect_territory_income`
(`FUN_004ad6c0`), which unconditionally runs the per-realm production planners
(`FUN_0044df5f`/`FUN_0044d374`/`FUN_0044dd4d`) for all 16 realms — the same
price-scalar-dependent path the `upkeep` diff target had to carve out (the diff
lib's fixed prices vs retail's live ones diverge, and the planners can divide by
zero on un-set-up menu-state realm records).  The merge's own writes land in
`g_per_territory_420`, whose field offsets are already retail-validated by the
`combat_resolve` / `combat_condition` / `combat_halve` diff targets sharing that
record layout; the five host tests pin every summed field, the linkage folds,
and this quirk.

## 169. The settlement-spawn cluster has no audio — and a race-0 (independent) garrison spawns with owner 6, which the income pass reads one slot past the strategic array

The per-turn settlement / construction-completion pass `FUN_004508de` and its
spawn subtree (`FUN_004a551b` find-slot → `FUN_004a9a9a` spawn → `FUN_004a79a3`
place-garrison, plus the four pure leaves `FUN_004a9f18` provision-debit /
`FUN_004a9f72` roster-load pick / `FUN_004a9eb1` cashflow-debit / `FUN_004ac7f3`
muster-attach) were long deferred as "spawn + SFX" pending audible verification.
**On decompiling the whole chain there is no WAV / sound call anywhere in it** —
the only side effects beyond record mutation are two render-dirty signals
(`FUN_0046e067` draw-box + `FUN_0045240a` full-screen, fired only when the spawn
belongs to the viewing player; and `DAT_005530d0` view-dirty in place-garrison).
The cluster is pure logic + render-dirty flags, so it is host-testable and
retail-diffable without an A/V gate.  (Audio for *new-game* troop spawns lives
elsewhere; this turn-pass spawn path is silent.)

The second half is a faithful out-of-bounds read.  `FUN_004a9a9a` sets a fresh
occupant's owner byte (`+0x00`) to **6** when its `owner`/race argument is 0
(an independent / neutral settlement).  The pass then calls
`engine_realm_collect_territory_income` (`FUN_004ad6c0`), whose pass-2 reads
`STRAT_REC(terr_owner)[0x81]` for every counted territory — i.e.
`g_player_strategic_352[6 * 0x160 + 0x81]`.  The strategic array is only six
slots (`2112 = 6 × 0x160`, save entry 3), so slot 6 is one past the end.  Retail
reads whatever global the linker placed next (harmless in its flat address
space); under ASan the host build flags it.  This is not introduced by the port
— `collect_territory_income` already had the read — but the race-0 spawn is what
feeds an owner-6 occupant into it.  Consequently the owner-0 spawn path is **not
host-unit-tested** (it would need a 7-slot strategic array); the owner≠0 spawn,
the four leaves, and the place-garrison cap/role logic are pinned by the eight
host tests in `test_engine_combat.c` / `test_engine_realm.c`, and the owner-6
field assignment is trivially correct by inspection.

`FUN_004a9a9a` also re-runs the per-realm economy trio
(`FUN_0044f6e7`/`FUN_0044df5f`/`FUN_004485a5`) **twice, back to back, verbatim**
— ported as a 2-iteration loop, not deduplicated.

Verification: `engine_combat_place_garrison` (`FUN_004a79a3`) is **retail-diff
gated** — new diff target `place_garrison`, **96/96 byte-exact** (48 seed0 + 48
seed7).  It dodges the economy-re-plan wall the same way `combat_ownership`
does: only the occupant block (`g_per_territory_420`) is read back, and the
place branch's `collect_territory_income` tail writes only `g_per_army_768`, so
the readback stays a pure save-state diff.  The diff covers the within-cap place
branch (role 5 + seat coords + cell offset + `+0x153 += 5` + `+0x151` / `+0x198`)
and the over-cap reject branch (role 2), including the pending-garrison
head-count summing in the cap check.  `engine_combat_spawn_unit` and the four
leaves are host-unit-tested (the spawn's economy tail + the race-0 `strat[6]`
read keep them off the diffable path); their occupant-field writes share the
already-diffed `g_per_territory_420` layout.

## 170. The year-rollover CD-audio probe's path-template `memcpy` overruns the `"c:.\wav"` literal into the adjacent pooled string literals — and every flag write stores the same value, so the file checks are dead

The turn-advance engine `FUN_00448440` calls `FUN_004aef7e` once per year on the
winter→spring rollover (`season == 4` branch) to probe the CD for its full audio
set.  Two faithful oddities, both reproduced verbatim in
`io_path_probe_cd_audio` (`src/io_path.c`):

**(a) string-pool overrun.**  The function builds the probe path with
`FUN_00401136(0x4df3ac, 0x5cd490, 0x1e)` — a 30-byte (`0x1e`) bounded copy.  But
the source literal at `0x4df3ac` is only `"c:.\wav\0"` (8 bytes).  The linker
packed the next pooled string literals immediately after it, so the 30-byte copy
drags in 22 bytes of *adjacent string-table residue*: 8 pad bytes, then
`"swor_p2.wav\0"` (the first WAV name, at `0x4df3bc`), then `"pu"` (the head of
`"pumkin.wav"` at `0x4df3c8`).  The chdir/open calls that follow stop at the
`\0` at offset 7, so the trailing 22 bytes are functionally inert — but they are
the byte-exact contents of `DAT_005cd490` after the copy, so the port hardcodes
the full 30-byte template (`CD_PATH_TEMPLATE`) rather than just `"c:.\wav"`.
Byte 0 is then overwritten with the CD drive letter (`DAT_0053f200`,
`g_config.cd_drive`), giving e.g. `"D:.\wav"`.

**(b) the availability gate is always 1.**  `FUN_004aef7e`'s only persistent
output besides the path buffer is the single-player / CD-audio availability gate
`DAT_005c9a74` (the gate `engine_menu_input` reads to route "Single player" to
page 2 vs the deferred profile page).  Retail stores **1** in four places: the
unconditional store at entry, the redundant `if (DAT_0053f684 == 1)` branch, and
both `filelength >= 151 MB` ("full audio CD present") branches.  Every store is
the same value, so the elaborate `swor_p2.wav` / `pumkin.wav` / `pumkin2.wav`
open + size-check sequence **can never change the gate** — it is dead with
respect to the observable state.  The port collapses it to one
`engine_menu_input_set_sp_available(1)` at entry plus a faithful (but effect-free)
transcription of the file probe.  On the flat drop-in / host layout the first
chdir into the (absent) CD wav dir fails and the probe is skipped entirely,
leaving gate=1 and the path buffer built — the faithful retail behaviour when no
CD audio is present.

Verification: `io_path_probe_cd_audio` is pinned by two host tests
(`test_io_path.c`): one asserts the gate forced to 1 and the full 40-byte buffer
byte-exact (drive letter + `"c:.\wav"` body + the pooled `swor_p2.wav`/`pu`
residue + zero tail), the other the drive-letter patch.  No live retail-diff
target: the output is fully determined by static `rdata` (the 30-byte template
read directly from the image) and the constant gate, so a Frida diff would only
re-confirm static bytes while running real `chdir`/file-IO in the retail process.

## 171. The very first turn-driver run of every new game takes the year-rollover branch, so the CD-audio probe fires on turn 1

The new-game initialiser `FUN_00497ced` seeds the calendar (in
`engine_game_init_seed_scalars`) to turn 0 with the season triple
`{prev 2, cur 3, next 4}` (`s_slot_00554470`/`0057c934`/`0057c92c`), then calls
the turn-advance engine `FUN_00448440` once to tick the fresh world to turn 1.
But the calendar block's year-rollover predicate (quirk #170's caller) is "the
just-consumed season-next was 4" — and the seed *is* 4.  So the first advance
**always** rotates the year: rollover counter `s_slot_0055cd5c` 0→1, year triple
1267→1268, and `io_path_probe_cd_audio` (`FUN_004aef7e`) runs.  In other words
the seasonal CD-audio re-probe is not a once-a-year-from-now event — it fires on
literally the first turn of a brand-new game (season then reads cur=4 / winter,
next wraps 5→1).  The seed `next == 4` is deliberate: it makes turn 1 land on the
winter→spring boundary so year 1268 begins immediately.

Verification: the host test `game_init_turn_driver_advances_to_turn_1`
(`test_game_init.c`) binds the assembled driver into the new-game initialiser's
step-17 seam and runs a full new game over the real L2_maps world, asserting the
exact post-state — turn 1, season `{prev 3, cur 4, next 1}`, rollover counter 1,
year 1268 — which is only reachable through the rollover branch.

## 172 — strategic cattle/livestock render in the OVERLAY pass, AFTER terrain (z-order bug surfaced by --newgame)

The `--newgame 0` first frame shows the player-capital's livestock (a fenced
field of black-and-white cows) rendering **over the top of a snow-capped
mountain** that sits in front of (below, in iso) the field — the cows paint on
the mountain peak instead of being occluded by it.

Pass-skip diagnosis (`OL2_SKIP_PASSES` mask in `engine_map_scene.c`,
bit0=terrain/bit1=overlay/bit2=features) localised it cleanly:

- terrain-only (mask 6): the fenced field-plot **base** and the **mountain**
  (peak and all) are present — both are pass-1 TERRAIN (`FUN_0040526e`); the
  **cows are absent**.
- +overlay (mask 4, features off): the **cows return** (and still cover the
  mountain).  → the cows are drawn by the pass-2 OVERLAY cluster
  (`engine_overlay_render` / `FUN_00405602`), which runs AFTER the entire
  pass-1 terrain.  So a pass-2 cow always paints over a pass-1 mountain
  regardless of iso depth.

This is a different cow system from the map-17 decorative-herd cows that an
earlier note recorded as terrain-walk-drawn (`blob2 idx84/104`,
[[project_strategic_map_render_frontier]]); the capital-livestock cows are an
overlay-pass entity.

OPEN (not yet resolved — needs retail ground truth at a cow-behind-mountain
frame): whether retail's `FUN_00405602` draws these capital-livestock cows at
all (if so it would have the same over-mountain ordering — i.e. this would be
*faithful*, not a port bug), or whether the mountain's tall overhang is meant to
be re-drawn in a later pass to occlude them.  Frame parity for the truth capture
is currently blocked by a separate issue: a retail LOAD of the port's
`--newgame` save lands on a DIFFERENT map region than the port renders for the
same stored scroll (entries 16/17) — the new-game render camera and retail's
load camera project the same scroll differently.  Captured under run ids
`newgame-truth` (custom-game retail), `newgame-clean2` (retail load of the port
save).

**RESOLVED (2026-06-02) — it WAS a symptom of #173, not a render-order bug.**
The cow-over-mountain came from the broken new-game *state*: the player capital
was being placed on the wrong territory (the strategic slots were never set up,
so the camera recentred on a stale slot).  Once the config-screen player setup
was seeded (#173), the capital recentres on the human's real capital (army
`0x0d`), the livestock pasture renders in the correct region, and the cows are
no longer over a mountain (user-confirmed).  The overlay-after-terrain z-order
observation above is faithful retail behaviour — the bug was *where* the pasture
was, not *when* it drew.

## 173 — the new-game player roster is config-screen output, not init output; +0x07 is the NPC work-location (NOT an opponent-type), and the AI count gate is `DAT_0053f268`

The `--newgame` drop-in seat seeds map + economy presets but the **config screen**
(`FUN_00499dc3` finalise + the screen-entry default population, both unported)
also sets up the *players* before `FUN_00497ced` runs.  Skipping that left the
strategic slots empty, so a saved new game read to retail as an all-AI /
auto-advancing turn state and **retail spammed turns on load**
(project_new_game_render bug #1).  Pinning the fix surfaced three non-obvious
facts (frida ground truth: `newgame-preinit1` at the config screen,
`newgame-statedump1`/`newgame-cfgdump1` at the first MAP_FREEROAM):

1. **The player→army roster is rebuilt from the MAP, not the config screen.**
   `engine_territory_load_strat_map` (in `setup_overworld`) calls
   `player_roster_clear` + `player_roster_add(plane5, plane4)` over the map's
   owner tiles, so seeding `g_player_roster` in the seat is overwritten.  Seed
   only the player *types*; the roster (army ids + seats) comes from the map and
   is then permuted by the seat-shuffle `0x497e65`.

2. **player_strategic `+0x07` is the NPC scheduler's work-location, not an AI
   "opponent-type".**  `engine_npc_schedule_init` (`0x49caaa`, init step 4)
   CLEARS `+0x07` for every slot, then assigns a work-location to the first
   `s_slot_0053f268` *unowned* (has_data==0) slots.  Phase 2 / the inactive-purge
   (`0x49b35e`) later read that same `+0x07` as the active-or-empty test.  So an
   AI slot is "active" iff the scheduler gave it a work-location.

3. **The AI count is `DAT_0053f268`, computed by the unported `FUN_00499dc3`**
   as `(DAT_0053f288 + 2) - DAT_00553f98` = total_players - humans.  The seat
   never set it → it stayed 0 → the scheduler assigned NO work-locations → every
   AI slot read as empty and got purged, leaving a 1-player (human-only) world.

The fix (`engine_game_init_set_player_config`): seed per-slot `+0x05` has_data /
`+0x0a` job-slot / `+0xee` player-bit, the local player `DAT_0057c8cc`, the AI
count `DAT_0053f268`, and the human's aux job reservation
(`g_player_aux_44[+0x25/+0x27]`, retail's SP `player_seat_assign`) so the
scheduler schedules the AI players *around* the human's fixed job — without it
the AI work-locations shift and diverge.  Also corrected the map-0 presets to
the captured retail values (economy 1 / resource 2 / gold 1000 / keep 3, NOT the
scenario-table values assumed earlier).  Result: post-init `player_strategic`
matches retail 2108/2112 bytes (the 4 residual = AI `+0x130` strategy field,
shared root with ~90 `per_army` AI-economy bytes — a separate turn-driver/AI
byte-completeness frontier), `cur_player`/roster/armies/work-locations exact, and
a retail LOAD of the resulting save sits stable (no turn spam — `year`/`season`
set once and hold; run `newgame-loadverify1`).

## 174 — `place_army_starts` reads the per-army capital cursor at `record[id]+0x6c` (stride `0x300`), populated by the ported `setup_capitals` — there is NO unported coord-table populator (RESOLVED)

**This quirk's original analysis was WRONG on two counts** — corrected and
resolved here (commit landing this update).  The entry-78 / entry-5-capital
cascade predicted by new-game-save-parity #1 IS real, but it was gated by two
*port* bugs, not a missing populator.

**Mistake 1 — the coord-table stride is `0x300`, not `3`.**  The probe
`army_near` (`FUN_00428007`/`428078`) is:

```
mov  eax, [id]          ; eax = id
shl  eax, 8             ; eax = id << 8        ← the original read MISSED this
mov  cl,  byte [eax + eax*2 + 0x53fa1c]
```

`3 * (id<<8) = id*0x300`, so the effective address is `id*0x300 + 0x53fa1c =
g_per_army_768[id*0x300 + 0x6c]` — i.e. **per-army record `id`'s capital cursor
at `+0x6c/+0x6d`, stride `0x300`** (the universal per-army-record size).  Ghidra's
original `(&DAT_0053fa1c)[id*0x300]` rendering was CORRECT; the port author
"corrected" it to packed `id*3` after reading the r2 `[eax+eax*2+...]` form
without accounting for the preceding `shl eax,8`.  The packed-stride-3 table,
the "distinct sub-table", and the hunt for an unported computed-base populator
were all artifacts of this misread.  **The populator is the already-ported
`setup_capitals` (`FUN_00468d4f`)**, which writes exactly `record[key]+0x6c/+0x6d`
(stride `0x300`) for `key=1..count` — the same bytes the probe reads.

With the stride misread, the port read `g_per_army_768[0x6c + id*3]` (all
clustered in record 0's body) = `(0,0)` for every digest id → 0 placed.  Fix:
`ARMY_COORD_STRIDE` `3` → `0x300` in game_init.c.  After it, `army_near` reads
the real cursors `(20,4)/(48,10)/(4,11)/(31,10)/(24,13)/(60,24)` (diag-verified).

**Mistake 2 — `engine_map_query`'s cell grid was never bound (see #175).**  Even
with correct coords, the radius-1..3 rawflag probe (`box_scan`) returned 0 for
all six because `engine_map_query`'s `g_cells` pointer had **no `src/` caller** —
only tests bound it.  At step 8 it was NULL, so every scan early-returned 0.
Binding it to `g_overworld_map` in `engine_game_init` (and the load path) lets
the probe find each capital's adjacent feature.

**Result (both fixes):** all 6 armies place; new-game save entry **78** (count) =
6 and entry **5** (per-territory capital records, ~119 B) drop out of the
`save_diff` vs `lastturn.sav`; entry 4 shrinks 41→23 B; total 251→101 B; 1871/1871
host tests green.  See docs/findings/new-game-save-parity.md #1.

## 175 — `engine_map_query`'s cell grid (`g_cells`) had no `src/` binder — the army-proximity probes were silent no-ops in every live build

`engine_map_query_bind_cell_descriptors` (the only setter for the query module's
`g_cells`) was called **only by tests** — no `src/` file ever bound it.  In retail
there is ONE shared cell-grid global that every consumer reads; the port split the
grid into per-module bound pointers (`engine_territory`, `engine_map_terrain`,
`engine_army_economy`, …) and each is bound at init — except `engine_map_query`.

Consequence: every `box_scan`/`region_clear`/ring-probe in `engine_map_query`
(`FUN_0046cfbd`, `46d130`, `46733c`, `46d2a3`) early-returned 0 on the `g_cells ==
NULL` guard.  Two live consumers were affected:

- **`place_army_starts`** (init step 8) — placed 0 armies (see #174).
- **the combat path** (`engine_combat.c` army-near probes) — would have read a
  NULL grid in-game.

Fix: bind it to the same `g_overworld_map` the other modules use, next to
`engine_territory_bind_cells`, in BOTH the new-game (`engine_game_init`) and load
(`main.c` post-load) paths.  Lesson: a module-local bound pointer with no
production binder is invisible to unit tests (which bind their own scratch grid) —
grep every `*_bind_*` setter for a non-test caller when splitting a shared global.

## 176 — the strategic scroll origin (`DAT_005651b4/b8`) is ONE retail global but TWO port storages; the new-game capital recenter was written to the save mirror, not the render camera

Retail has a single scroll-origin storage, `DAT_005651b4` (col) / `DAT_005651b8`
(row), read by **both** the strategic renderer (as the terrain-walk start) **and**
the save serialiser (entries 16/17).  Three writers touch it: `FUN_00498270`
(map setup, default **20/74**), the recenter-on-capital chain (`FUN_00432746` →
`FUN_0043278b`, which sets it to **col-4 / evenrow-12** = the capital-centred
**24/2** on map 0), and load (the save value survives because retail's load never
re-runs `FUN_00498270`).

The port **split that one global into two storages**:
- the renderer reads `engine_map_init`'s `g_col_start`/`g_row_start` statics;
- the save reads the `s_slot_005651b4`/`s_slot_005651b8` schema mirrors;
- the recenter (`engine_territory_recenter_on_capital`) writes **only the
  `s_slot` mirror**.

So the recenter moved the *save's* camera but not the *rendered* camera.  The
`--newgame` seat originally "fixed" the resulting save↔render divergence by
clobbering the `s_slot` mirror back to the render statics' default (20/74) — which
picked the **wrong winner**: the first new-game frame drew at the map default
instead of the capital, and the save carried 20/74 instead of retail's 24/2.

Fix: bridge the *other* direction — after the new-game init, push the recenter'd
`s_slot` scroll into the render camera via `engine_map_init_set_scroll(...)`,
exactly as the LOAD path (`engine_map_post_load`) already does after
`engine_map_init_strategic` resets to the default.  Now both the render and the
save reflect 24/2.  (Verified: a `DIAG` log confirmed the recenter produces
col=24/row=2 pre-clobber; save entries 16/17 then match `lastturn.sav` byte-exact
and the rendered frame recentres on the capital — user-confirmed via feed.)

Same family as #175 (a split shared-grid global): when retail has ONE global and
the port shards it per-consumer, every writer must fan out to all shards, or the
one consumer reading a stale shard silently diverges.  Grep: a `*_set_scroll` /
`*_bind_*` that writes one shard but not the sibling is the smell.

## 177 — scenario / "Play Now!" config-apply (`FUN_00499e5d`): same config globals as the custom apply, but from a table; the five realm presets are NOT saved (only the config scalars are)

The scenario/"Play Now!" start (`FUN_00499e5d`, ported as
`engine_game_init_apply_scenario_config`) is the sibling of the page-7 custom
apply (`FUN_00499dc3`).  Two non-obvious facts:

**(1) It writes the SAME config globals as the custom apply, just sourced from a
table.**  Where `499dc3` reads the 12 page-7 option tabs, `499e5d` copies a row
of a fixed 0x20-stride preset table — `0x4d8e18` (skirmish/deathmatch) or
`0x4d8f58` (campaign), selected by the difficulty picker's campaign flag
`DAT_0053f640` + index `DAT_0053f258` (range 0..7) — into the identical globals
(map id `0x53f034`, density `0x53f23c`, gold `0x53f278`, keep `0x53f280`, economy
`0x53f27c`, resource `0x53f270`, troop `0x53f274`, AI count `0x53f268`).  It then
forces standings off / render-mode 0 / morale-gate off / zero turn budget.  The
two tables carry DIFFERENT map ranges (skirmish: maps 17/12/2/5/0/3/7/4 — incl.
map 0, the England default at row 4; campaign: 52/53/45/44/41/42).  `499e5d`
ALSO runs the map-setup tail inline (`0x46a037` minimap grids / `0x467770`
plane-unpack / `0x4298c1` build-cells) that `499dc3` leaves to the surrounding
flow — the port reproduces that tail once in `newgame_build_world_and_start`
(shared by both apply paths), so it is NOT duplicated in the scenario apply.

**(2) The five realm presets are transient, NOT saved — only the config scalars
round-trip.**  `gold/keep/economy/resource/troop` (`0x53f278/280/27c/270/274`)
are recomputed each new game and have no save-schema entry; only the config
SCALARS do (map `123`, scen-idx `198`, camp-flag `212`, AI `173`, density `13`,
the mode flags `170/171/172`, budget `169`).  So a retail LOAD of a port-written
scenario save shows the saved scalars byte-exact (verified: `playnow-loadverify1`
— retail loads `port_playnow.sav`, dumps scen_idx=4 / density=2 / ai=4 / map=0 /
standings=render=budget=0, all matching the port) but the preset globals read
back as retail's transient MENU values (gold 2500 / keep 1), NOT the scenario
row's (gold 5000 / keep 4).  That is correct: the presets' EFFECT is already
baked into the saved per-army/per-realm records (player gold 5000, keep-4 stamps
— visible in the `port_newgame.sav` vs `port_playnow.sav` save diff), so the
loaded world is faithful even though the input globals aren't persisted.  Retail
loads the scenario world STABLE (single `NEW_GAME` + `MAP_FREEROAM` anchor, no
turn-spam), confirming it is a valid retail game state.

## 178 — the new-game seeder `seat_shuffle` (FUN_00497e65) stirs the GLOBAL double-LFSR; the port's local copy left the turn driver reading a stale RNG (scalar-wall again, same family as #175/#176)

Retail's new-game randomiser `FUN_00497e65` (`engine_game_init_seat_shuffle`)
draws from the **global** double-LFSR `DAT_0058fe28/0058fe08` by calling the
shared stir `FUN_00404a46` (3 advances per loop iteration — once in each of its
three phases).  It is the **only** consumer of that global on the new-game init
path before the turn driver (verified: of the seven `FUN_00404a46` callers, only
`497e65` and the turn driver `448440` are reachable from `497ced`; the value
table `42847c` does NOT stir).  So after seat_shuffle the global has advanced by
`active + 96` steps from its InitInstance seed.

The port ported seat_shuffle to thread a **local** `engine_rng_in_t` seeded to
the same InitInstance constants (`0x54657687/0x72641663`).  Its *output* — the
roster shuffle + the two 48-entry tables — matched retail exactly (local starts
at the same seed), so seat_shuffle never showed up in the save diff.  **But the
global side effect was dropped:** `DAT_0058fe28/08` stayed at the seed.  The turn
driver (game_init step 17, `engine_turn_advance`) then reads that global, stirs
it once (`engine_tick_rng_step`), and feeds `rng_7bit_a/b` into every RNG-driven
turn-1 pass.  Starting from the un-advanced seed silently perturbed:

- **`engine_realm_update_mood` (FUN_00449889)** — the focus realm
  `sel = rng_a & 0xf` (save entry 79, `DAT_00554020`) and the per-realm mood
  deltas, hence which realms land in band 5 and get a type-**0x17** standing
  marker cell (`realm_mark_next_cell`).  That marker is save entry 0 — which an
  earlier draft mis-filed as an *autotile build residual*.  It is not: the
  autotiler leaves those cells untouched (per-pass instrumentation of
  `setup_overworld` confirmed); they are turn-1 standing markers.
- the **AI economy passes** (save entry 4, per-army economy).

Fix (same shape as #176): collapse the scalar-wall split.  `game_init` now loads
the global (`engine_rng_global_get`), threads it through seat_shuffle, and writes
the advanced state back (`engine_rng_global_set`); `engine_tick_rng_step` /
`_set` mirror the live state into the save home `s_slot_0058fe28/08` (entries
18/19).  Result: the whole-save diff vs `lastturn.sav` dropped 75 B → 34 B,
entries 0/4/18/19/79 all byte-exact, leaving only entry 3 (the AI strategy
block) on the AI-brain frontier.

**Smell (recurring across #175/#176/#178):** a ported seeder that takes a *local*
RNG/state when retail consumes a *global* one — the output matches but a later
global reader diverges.  Grep for a seeder threading a private `engine_rng_in_t`
seeded to an InitInstance constant: if retail's original calls `FUN_00404a46`
(or any shared-global mutator) rather than a local stepper, the global must be
threaded through, not copied.

## 179 — the AI resource-demand pool `DAT_0057c020` is the SAME memory as the player-strategic commodity pools `+0x120/+0x128/+0x130` (overlapping global; scalar-wall again, family #175/#176/#178)

Retail's per-group resource-demand pool `DAT_0057c020` (stride `0x160`, the table
the per-turn resource-delivery pass `FUN_0044e852` accumulates production into)
does **not** have its own storage — it physically lives **inside** the
player-strategic block `g_player_strategic_352` (`0x57bf00`, stride `0x160`) at
offset `+0x120`: `0x57c020 = 0x57bf00 + 0x120`.  So the commodity pools the AI
brain reads through the strategic record — `+0x120` (commodity 6 / B), `+0x128`
(7 / C), `+0x130` (8 / A) — **are** the resource accumulators the economy pass
writes through `DAT_0057c020`.  One memory, two bases/views:

```
strategic[player] + 0x120   ==  DAT_0057c020 + player*0x160 + 0x00   (RESPOOL_B_FLAG0)
strategic[player] + 0x128   ==  DAT_0057c020 + player*0x160 + 0x08   (RESPOOL_C_FLAG0)
strategic[player] + 0x130   ==  DAT_0057c020 + player*0x160 + 0x10   (RESPOOL_A_FLAG0)
```

(The `group` the economy pass indexes by IS the owning player index for owned
realms, so the overlap is meaningful — `apply_resource_quota`'s
`respool_field(group, RESPOOL_A_FLAG0) += amount` lands on that owner's strategic
`+0x130`.)

**The port split them.**  `g_player_strategic_352` is a real buffer, but
`engine_army_economy_bind_resource_pool` was only ever called from tests — the
live `g_resource_pool` stayed **NULL**, so `respool_field`/`respool_demand`
returned NULL/0 and the entire turn-1 resource-delivery accumulation was a silent
no-op.  Symptom: new-game save entry 3, strategic `+0x130` for AI players 2–5,
stuck at the config default `0x32` where retail had `0x74/0x74/0xb6/0xd8` (the
accumulated commodity-A production).  `+0x120`/`+0x128` happened to match only
because turn-1 produced 0 demand into B/C.

**Fix:** bind the pool to alias the strategic block in `engine_game_init`, next
to the other economy binders:
`engine_army_economy_bind_resource_pool(g_player_strategic_352 + 0x120)` — exactly
the address (`0x57c020`) retail's pool sits at.  Entry 3 `+0x130` → byte-exact;
the full `0x57bf00:2112` block matches a point-matched retail dump with 0 B diff.

**Smell (the now-canonical scalar-wall family #175/#176/#178/#179):** a ported
table whose retail base is a `DAT_` *inside* another already-bound structure's
address range.  When two retail globals' VAs differ by less than the structure's
size (here `0x57c020 − 0x57bf00 = 0x120 < 0x160` stride), they are very likely
**the same storage viewed twice** — bind the port's second view to alias the
first, never to a fresh buffer.  Grep heuristic: a `bind_*` setter only ever
called from tests (never from `game_init`/`main`) is a NULL-in-production wall.

## 180 — a blocked army `engine_army_move_classify_step` step into an impassable foreign-region cell does NOT stall: it scorch-decays the cell and bulldozes through

`FUN_0046634d` post-processes an outcome of 8 (impassable) by running the
blocked branch (`FUN_0046673c`) and then **rewriting the code to 1 (open) and
committing the step anyway** — the stack ends the tick standing in the cell it
"could not enter".  The blocked branch is what makes this consistent: when the
obstacle belongs to a foreign region and the tile is real land
(`engine_army_move_terrain_passable`, terrain 2..0x16), it drains a census-
proportional slice of that region's stockpile (`decay_region_cell` /
`FUN_00469e5b`) and flattens the tile to terrain 0
(`engine_territory_set_cell_type`).  So by the time `commit_step` relinks the
stack, the cell IS passable.  This is the strategic-map "raze your way in"
mechanic, not a bug.

Two secondary gotchas in the same function:

- **The path-progress accumulator (+0x153) is bumped twice on an impassable
  step**: once inside the blocked branch (`+= 3`) and again in the classifier's
  common tail (`+= 3`, since the rough flag was just cleared) — net +6, vs +3
  for a plain open step.  Not a double-count bug; retail does exactly this.
- **The retry budget reads the PREVIOUS step's rough flag** (+0x14d): a stack
  that stepped onto rough terrain last tick gets budget 0 (wraps its retry
  counter immediately, ramping the stall accumulator faster) instead of the
  normal budget 3.  The flag is the single sticky `M_ROUGH` byte that
  `commit_step` writes from the entered cell and the classifier both reads (for
  the budget) and rewrites (from the new step outcome).

## 181 — the movement executor `FUN_00465d28` reads an UNINITIALISED waypoint on its disarmed first pass (harmless because the disarmed classifier ignores the direction)

`engine_army_move_run` only assigns its `wp_col`/`wp_row` locals (retail stack
slots `var_ch`/`var_10h`) inside the **step-eval-armed** branch (+0x14b bit0).
But the common tail after that branch unconditionally evaluates a direction from
them (`engine_path_direction_8way`, `FUN_0046f434`) and feeds it to the
classifier.  On the very first executor pass for an army the flag is still
disarmed, so retail reads whatever those stack slots last held — genuine
read-before-write UB.

It's benign in practice for two reasons:

1. The **disarmed** classifier path (`engine_army_move_classify_step` with
   +0x14b bit0 = 0) runs only the stall/retry hysteresis and **never reads its
   direction argument** — so the garbage direction is computed and discarded.
2. The only other use of that garbage is the `dir == 8` (already-on-cell) early
   return guarding the classifier call.  `FUN_0046f434` returns 8 only when the
   "from" and "to" coordinates are equal, which for a real stack position
   requires the garbage waypoint to exactly equal the stack's own (col,row) — it
   essentially never does, and a stack can't legitimately sit at the (0,0) grid
   corner where the zero-init equivalent would trip.

The port zero-initialises `wp_col`/`wp_row` for a defined first pass; this is
observationally identical to retail except in the impossible (0,0)-corner case,
and the disarmed classifier discards the value regardless.  Same "faithfully
reproduce retail's control flow, tighten only the genuinely-unreachable UB"
posture as elsewhere.

## 182 — siege-engine construction (`FUN_004a8507`) splits the besieger's head-count as the per-turn build budget; assault launches the tick all ordered engines hit 100%

The strategic turn driver's phase-2 callee `FUN_004a84ba`
(`engine_army_state_scan_siege_built`) scans the 150 per-territory occupant
records and, for each **besieging** stack (live, state 1, `+0x199` siege link
set), advances its siege-engine construction one tick via `FUN_004a8507`
(`engine_army_state_siege_build_step`).  The build state is three i16 slots
`{ count, pct, value }` at `+0x182` (stride 6) — `count` engines of that type
ordered, `value` build progress, `pct` = `value` as a percent of the target.

Mechanics worth recording:

- **The build budget is the army's head-count** (`+0x168`, the same field the
  march/settle handlers read as total troops).  Each tick that whole budget is
  poured into the unfinished slots: first split **evenly** across the active
  slots (`head / active`), then a second pass tops up any slot the even share
  left short, in slot order, until the budget is spent.  So a bigger besieging
  army literally builds its siege engines faster.

- **Per-engine targets are baked** — `k_siege_build_target` (retail
  `DAT_004de440`) = `{ 200, 200, 400 }`.  A slot's full build value is
  `target[slot] * count`, i.e. ordering two of engine-type 0 needs 400 build,
  not 200.

- **The completion signal is an aggregate remaining-work byte** (`+0x19c`):
  `ceil(sum_of_unfinished_gaps / head)`.  `FUN_004a8507` returns 1 the tick that
  byte reaches 0 — every ordered engine at 100% — and the scanner then parks its
  persistent cursor (`s_slot_0057cad4`) on that record so the driver can launch
  the assault on it (`FUN_004a8aab`, the A/V assault trigger — sounds + combat
  render, not yet ported).  A non-zero remaining byte (or any failed gate)
  returns 0 and the cursor steps to the next record.

- **The "pct < 100" gate is read pre-recompute in every pass.**  All three
  passes test the *old* `pct` to decide whether a slot is active; only the third
  pass rewrites it (`engine_math_percent_div`, clamped to 100).  A slot whose
  `pct` was already 100 from a prior turn is skipped entirely — never
  re-evaluated — which is why a fully-built slot stops contributing to the
  even-split denominator.

- **The cursor is resumable, not reset here.**  `FUN_004a84ba` only ever
  *increments* `s_slot_0057cad4`; the reset to 1 lives in the phase-2 entry init
  `FUN_004a82b9` (unported).  The driver calls the scan once per 100 sub-ticks,
  so the cursor walks the record block at one assault-check per ~100 ticks and is
  re-armed to 1 each time phase 2 is re-entered.

The remaining-byte ceiling is inlined in retail (`div` + `mod != 0 ? +1`) rather
than routed through the ported `engine_math_div_ceil` (`FUN_00404e03`); the port
mirrors the inline form to stay byte-faithful.

## 183 — the mercenary-offer rotation (`FUN_004aca2b`) publishes each offer for exactly one turn; the cursor double-steps on publish so consecutive offers never repeat

A phase-7 (turn-rollover) callee of the strategic turn driver `0x49a010`.  Each
free merc-pool slot (`g_unit_table_20`, stride 0x14, back-ref `+0x00` == 0)
carries a rotation cursor (`+0x04`), a countdown (`+0x06`) and a reload period
(`+0x07`).  Every turn the offer byte (`+0x03`) is **cleared first**, the
countdown decremented and the cursor stepped (wrapping at the realm count
`s_slot_0056d5dc`); only on the turn the countdown reaches ≤ 0 is the cursor
**republished** into `+0x03`, the countdown reloaded from the period, and the
cursor stepped *again*.

Two consequences worth recording:

- **An offer is visible for a single turn.**  `+0x03` is non-zero only on the
  exact turn a slot's countdown expires; every other turn it reads 0.  The
  reverse map built in the second loop (`g_per_army_768[realm*0x300 + 0x1ad]`,
  the per-realm mustered-unit scratch `A_F1AD`) is therefore mostly zero and
  lights up one realm per expiring slot.

- **The publish path steps the cursor twice** (once in the common per-turn step,
  once inside the expiry block) and does *not* re-clamp after the second step.
  So the cursor can transiently sit at `realms + 1`; the next turn's ordinary
  step+clamp folds it back to 1.  This is deliberate — it guarantees the value
  published next period differs from the one just published (no back-to-back
  identical offers to the same realm).

The countdown decrement is a raw byte `dec` followed by a *signed* `< 1` test,
so a slot already at 0 wraps to 0xff and reads as `-1` → immediately "expired".
The port mirrors the unsigned-decrement / signed-compare pair exactly.  In-use
slots (back-ref `!= 0`) are skipped wholesale and never match as an offer holder
even if their stale `+0x03` happens to line up with a realm.

## 184 — lord/vassal treaty reconciliation (`FUN_004a1847`) is order-dependent: a dead or contested lord only severs the bond when it has the lower player index

The phase-7 reconciler snapshots every player's declared lord (+0x81) into a
local array *before* the pass, then validates players 1..5 in ascending order.
Two consequences fall out of "ascending order + pre-pass snapshot":

- **The "settled" (inactive/severed) flag is only reliable for lower indices.**
  A player's settled flag is set during *its own* iteration, so when player `i`
  inspects its lord `p`, `settled[p]` reflects reality only if `p < i`.  A bond
  onto a *dead higher-indexed* lord therefore is NOT severed by the dead-check —
  it falls through to the mutual branch and the bond gets imposed onto the
  corpse.  The game avoids visible breakage because the inactive record is inert
  elsewhere, but the asymmetry is real and faithfully reproduced.

- **A one-sided declaration onto a live, *unbonded* player imposes the bond.**
  Severance fires only when the lord is settled OR already bonded to a *third*
  player (`lord[p] != 0 && lord[p] != i`).  If the target simply has no lord
  (`lord[p] == 0`), the else-branch pins `target.lord = i` and sets the matrix
  both ways — vassalizing the target.  Because the lord array is a pre-pass
  snapshot, the freshly-imposed `target.lord` is not seen when the target's own
  (later) iteration runs (it still reads the snapshot 0), so the imposed bond is
  not re-processed and survives the pass.

The port mirrors the snapshot + ascending-order exactly (the matrix wipe, the
settled/lord local arrays, the lower-index-only reliability).  Same family as
the other "scalar snapshot vs live store" ordering quirks.

## 185 — raider target assignment (`FUN_004ac5ba`) recomputes the destination from the SHARED realm cursor, not the stack's stored target — so a kept target can point at the wrong capital

The phase-5 raider planner gives each settling stack a target realm via a single
global round-robin cursor (`DAT_004e59dc`, `g_raider_target_cursor`).  The
reassignment block (entered when the planner is in force-mode 2 *or* the stack
has no target yet) steps the cursor, skips the stack's own region, and writes the
result into the per-record target byte (`+0x151`).

The bug-shaped detail: the *destination* write that follows reads the capital
through the **cursor** (`g_per_army_768[cursor*0x300 + 0x6c/0x6d]`), not through
the per-record target byte.  These agree on the turn a target is freshly
assigned (cursor == target), but a stack that re-enters with a target already set
(mode != 2) **skips the reassignment** yet still recomputes its destination from
whatever realm the cursor currently points at — which, mid-sweep, is the target
of the most recently *reassigned* stack, not this stack's own.  So a stack can
keep target realm N in `+0x151` while its destination coords point at realm M's
capital.

In practice the sweep `FUN_004ac499` re-floods and re-traces from the (possibly
mismatched) destination every pass, so the stack simply drifts toward whichever
capital the cursor last named; the stored target is essentially advisory.  The
port reproduces the cursor-sourced destination verbatim rather than "fixing" it
to read the target byte — same faithful-to-retail posture as the other
snapshot/shared-cursor quirks (#183/#184).

## 186 — phase-3 march planner (`FUN_00429418`) floods conservatively first, then permissively only on a degenerate route

The phase-3 turn-driver callee routes every idle (state 4) stack toward its
target realm's (+0x167) capital.  It floods the path-field cost map with
`blocked_mode = 1` (costly-but-passable cells clamped to cost 100 — heavily
penalised), traces to the capital, and commits the route only when the trace
lands in **under 150 waypoints**.  150 is the path-field's hard trace cap
(`engine_path_field` writes at most 0x96 waypoints), so a path of exactly 150
is the engine's "didn't actually reach / had to give up" sentinel, not a real
route.  On that sentinel it re-floods with `blocked_mode = 0` (no cost clamp,
costly terrain freely traversable) and traces once more, committing whatever
that yields.

Net effect: stacks prefer routes that avoid expensive terrain, and fall back to
"go through the swamp/forest" only when the cheap-terrain route can't be found
within the waypoint budget.  The waypoint-count gate reads slot 0's count
(`DAT_0053f070[0]`, the value later copied into the record's +0x1c waypoint
counter) — the same slot the two floods/traces use.  The phase-6 planner
(`FUN_004280e9`) shares this exact dual-flood tail.

## 187 — phase-6 settle planner (`FUN_004280e9`) indexes the 6-row category-bucket table by per-region (i-1), sliding past row 5 into BSS slack

The phase-6 turn-driver callee, when an idle (state 3) stack is flagged
"arrived" (+0x150), scans up to 16 entries of a category-bucket row for a
neighbour realm to settle toward.  The bucket table is `DAT_00567970`, which the
rest of the engine treats as `buckets[6][16]` — a fixed 6-category × 16-entry
list (built by FUN_004290d0/FUN_00429153, read by FUN_0042925e; the save schema
persists exactly its 96 bytes, entry 138).

But phase-6 indexes it as `buckets[(i-1)*0x10 + cursor]` where **i is the
per-territory record index 1..150**, not a category 1..6.  For records 1..6 this
reads a real bucket row; for i >= 7 it reads off the end of the 96-byte table.
The next BSS global (`DAT_00567d10`, save entry 149) does not start until
+0x3a0 (928 B), so rows 6..57 are uninitialised BSS — they read as zero, the
loop finds no in-range realm, and the stack is left unchanged.  Only a state-3 +
arrived record at i >= 58 would reach `DAT_00567d10`'s live data; that the
engine never corrupts the save in practice means such records do not occur at
high indices (state 3 + arrived is effectively a low-index / player-row event).

The port reproduces the exact `(i-1)*0x10 + cursor` arithmetic.  To read the
same zeros retail reads for rows 6..57 without an out-of-bounds access, the
in-memory backing of `s_slot_00567970` is widened to 150*16 B (the save still
serialises only the first 96).  Behaviour matches retail for every index a
legitimate game state can produce; the only divergence (port reads zero vs
retail reading `DAT_00567d10`) is confined to indices retail itself could not
survive.

## 188 — phase-1 AI grant (`FUN_0049d638`): higher AI levels get the BIGGER cash table but NO resource topups, and one ration ladder is dead code

The phase-1 per-player AI init (`FUN_0049d638` → `engine_army_ai_set_rations_and_grant`,
strategic FSM `FUN_0049a581` dispatch step 3) does two passes: a per-realm
approval→ration-setting (+0xb9) ladder, then a difficulty cash/resource grant.
Three counterintuitive facts:

1. **The grant scales inversely to what the table sizes suggest.**  The cash
   grant is read from one of two adjacent const tables keyed by the player's AI
   level (+0x29): level `< 3` draws `DAT_004dc230` (small — max 600), level
   `>= 3` draws `DAT_004dc1e0` (large — max 1800).  So the *higher* AI levels
   receive the *bigger* treasury injection.  But the per-realm resource topups
   (population +0x24 & its +0x30 mirror, stock +0x250, provisions +0x224) run the
   *opposite* way: level 1-2 get the big step set (diff×{20,5,40}), level 3-4 the
   small set (diff×{10,2,20}), and **level ≥ 5 get the cash grant but every
   per-realm step stays 0**.  Net: top-level AI is funded by cash alone, low-level
   AI is padded with both cash and raw resources.  Both tables are indexed
   `base + difficulty(s_slot_0053f23c)*4 + personality*0x10`; row 0 (personality
   0) is unused.

2. **The ration ladder's selector-0 branch is dead code in retail.**  Pass 1
   picks one of three approval ladders by the int at +4 of the AI's personality
   row (retail `DAT_004d8a5c`-based read).  The four shipped personalities have
   selector values {2, 2, 2, 1} — none select 0, so the harshest ladder
   (`<30→0, <50→2, <65→4, <80→10, else→15`) never fires for any retail AI.  The
   port keeps the branch faithfully; only a hypothetical 5th personality (or an
   edited tuning table) would reach it.

3. **Pass 1 runs for player 0 too.**  Called with `player == 0` it sets every
   *independent* realm's ration via a distinct fixed 8-step ladder; pass 2 then
   falls out immediately (strat slot 0 is inactive).  So the same function both
   manages AI economies and reposts independent-realm rations each turn.

All three tables are baked verbatim from the image (`g_ai_cash_grant` 0xa0 B;
the personality block was already baked as `g_ai_personality`).  Host tests
exercise both rations ladders that fire, the player-0 ladder, and all three grant
regimes (level 2 / 4 / 5) plus the active/eliminated/level-0 gating.

## 189 — independent realms run a SEPARATE AI dispatch table and self-fund provisions from their own gold pool

The strategic independent-realm block (`DAT_00569584 == 1`) drives owner-0
realms through `FUN_0049d638(0)` (rations, quirk #188) and then
`FUN_0049dfc6(0)` → `engine_army_ai_run_independent`.  The latter is a parallel
of the owned-realm driver `FUN_0049dd01` (`engine_army_ai_run_player`) but with
two differences:

1. **A different dispatch table.**  Owned realms latch a personality dispatch
   code (+0x1fe) of 0/1/9 → balanced / compact / expansionist
   (`FUN_004a4052/42e3/440f`).  Independent realms read the *same* +0x1fe field
   but route 0/1 → `FUN_004a3c67` / `FUN_004a3ed3`
   (`engine_army_ai_dispatch_independent_balanced` / `_compact`).  Code 9 and
   any other value are no-ops for independent realms.

2. **No market — self-funding instead.**  Both independent dispatchers skip the
   `engine_army_ai_reconcile_resources` market step (it is player-gated and an
   independent realm has no player).  When provisions (+0x224) run below 100 the
   balanced variant credits the realm's *own* gold pool (+0x1f4, the field
   `engine_realm_accrue_income` accrues an unowned realm's income into) by 100
   and then runs a self-funded buy ladder (400/200/100/50, each re-checking the
   `< 100` gate); the compact variant runs the buy ladder without the gold
   credit.  Their cell-reshape math also keys off the census-bucket *sum*
   (type-1 + mid + high) rather than the live `feature_cell_total`, and both
   stamp the strategy tag (+0x008) to 0 (owned dispatchers stamp 0x32 / 0x14 /
   0x28).  `engine_army_ai_run_independent` clears a per-realm review flag
   (+0x1b0) before each realm and develops empty cells on the same
   population-gated ladder as `run_player`, sized off the census sum.

## 190 — the per-player strategic +5 byte is "human-controlled", not "eliminated"; the AI brain runs iff +4 != 0 && +5 == 0, and diplomacy proposals target +5 != 0 slots

The strategic-block byte at `(&DAT_0057bf05)[p * 0x160]` has accumulated three
contradictory names across modules: `engine_army_ai.c` calls it
`STRAT_ELIMINATED`, `engine_event_queue.c` calls it "slot is active", and
`engine_diplomacy.c` calls it the "AI gate (0 = AI-controlled)".  The ground
truth, pinned by the AI FSM dispatcher `FUN_0049a581`, is the diplomacy reading:
**+5 != 0 means a human owns the slot.**  The dispatcher runs a slot's brain
only when `(&DAT_0057bf04)[p*0x160] != 0` (in game) **and**
`(&DAT_0057bf05)[p*0x160] == 0` (AI-controlled) — so a human slot (in game, +5
set) is skipped, and the difficulty cash grant (`FUN_0049d638`, quirk #188),
gated on `+5 != 0 → bail`, lands only on AI players.  The `STRAT_ELIMINATED`
label is a misnomer; the byte is the human/AI discriminator.

The diplomacy-proposal brain step `FUN_004a13a6`
(`engine_army_ai_propose_diplomacy`, brain step 0xd) consumes this directly: it
posts treaty/war proposals through the event-queue producer to the **human**
slots (+4 != 0 && +5 != 0), which is exactly why these route through the UI
notification ring rather than mutating diplomacy state inline.  Its own shape:

1. **Gate on rank.**  Runs only when the player's finishing rank
   (`+0x2b`, `engine_ranking` F_RANK) is `< 2` — i.e. the current leader
   (rank 1) or an unranked slot (rank 0).
2. **Alternate two proposal modes** via the `+0xea` phase flag, each fired once
   the per-tick cooldown `+0xe9` reaches 8 (incremented only after the mode's
   own gauge gate passes):
   - **phase 0 (alliance):** once the diplomacy-readiness gauge `+0x60` tops
     `0x28`, broadcast code `0xc1` to every other in-game human slot, then flip
     the phase to 1.
   - **phase 1 (war):** once `+0x60` tops `0x1c`, send code `0xc0` to the
     **last-ranked (weakest)** player `s_slot_00522d90` — provided that player
     is human and is not already this AI's lord (`+0x81`) — then flip back to 0.
   The proposal variant selector reuses the shared notification rotation:
   `(mood-cycle[+0x159] − 4) + personality[+0x07]·4`, identical to
   `engine_diplomacy.c`'s `diplo_notify`, advancing `+0x159` (wrap 0..3) per
   post.

Brain step 8 (`FUN_0049f96c`, `engine_army_ai_step_noop`) is an empty function
— a live slot in the dispatch ladder with no shipped body.

## 191 — AI brain step 0xc (`FUN_0049e77d`) reads its per-realm build orders from columns BAKED INTO the personality table, cycling 10 rotation codes over only 6 distinct columns

The per-turn build-order planner `FUN_0049e77d`
(`engine_army_ai_run_build_orders`, brain step 0xc) stamps each owned realm's
build tag (`+0x290`) from the AI's personality row — but the "build columns" are
not a separate table.  Retail reads `(&DAT_004d8aa8)[(personality·3−3)·0x50]`,
and `DAT_004d8aa8 = 0x4d8a58 + 0x50` is offset **0x50 inside the personality
table** (`g_ai_personality`, base `0x4d8a58`, stride `0xf0 = 3·0x50`).  So the
build tags live at personality-row offsets `+0x50/+0x54/+0x58/+0x5c/+0x60/+0x64`
— six bytes interleaved with the economy targets the same row carries at
`+0x78`+.  The port reads them straight out of the already-imaged
`g_ai_personality` image rather than baking a second table.

The per-player rotation counter `+0x6c` (`STRAT_BUILD_ROTATE`) advances 0..9 and
wraps at 10, but maps onto only the **six** columns: codes 0..3 and 4..7 both
select columns 0..3 (offsets 0x50..0x5c), and only 8/9 reach the last two
(0x60/0x64).  Net effect: the first four build orders are each twice as likely
per cycle as the last two.

The build **cost** table `DAT_004d8990` (consumed by the affordability leaf
`FUN_0049ed13` → `engine_army_ai_mark_build_affordable`) is exactly **six**
8-byte entries (`{cost_vs_pool130, cost_vs_pool120}`, tags 0..5) — the next const
table begins at `0x4d89c0`, +0x30 — matching the six possible build tags.  A
realm is flagged affordable (`+0x2c5`) iff the player's `+0x130` and `+0x120`
pools (the overlapping strategic commodity pools of #179) both cover the tag's
two costs.

Step 0xc's second pass lights the four stride-0x18 settlement workforce gates
(`+0x297`, `engine_territory`'s `WF_GATE_C6`+) for present (`+0x295`), unlocked
(`+0x296 == 0`) buckets, then — when a secondary settlement exists (`+0x1c3`) —
applies a per-bucket filter that drops buckets 1/2 outright and gates bucket 0 on
build-draw-hi (`+0x1d4 > 0`) and bucket 3 on build-draw-lo (`+0x1d0 > 0`).  The
third pass then runs the workforce allocator (`FUN_0044f6e7`) and per-realm
economy orchestrator (`FUN_004485a5`) over the now-enabled gates.

## 192 — the AI deploy-cell finder (`FUN_004a689d`) can never select grid cell (0,0); it reuses the byte offset 0 as its "no match" sentinel

`FUN_004a689d` (`engine_combat_find_deploy_cell`) scans the 0x40×0x40 overworld
grid for the cell owned by a region (`+0x07`), marked deployable (`+0x01` bit
0x20), with passable terrain (`+0x00` in `[3, 0x16]`), nearest by Manhattan
distance to the army's seat — tracking the winner as its **byte offset** into
`g_overworld_map` (cell index × 8).  The "no candidate yet" sentinel is offset
`0`, which is also the legitimate offset of grid cell `(0,0)`.  So a match at the
grid origin is indistinguishable from no match, and the function falls through to
its capital-cell fallback (region `+0x6c`/`+0x6d`) instead.  Cell (0,0) is the
top-left corner — effectively never a real deployment target — so the collision
is benign, but it is a true retail behaviour the port reproduces.

Both callers (brain step 0xa `FUN_004a0015` and `FUN_004a58f4`) read the chosen
col/row out of engine_path's find-nearest scratch `DAT_00569570/78` immediately
after the call and copy them into the army's destination fields; nothing reads
the scratch in between, so the port returns the pair via out-params rather than
maintaining those globals (the same dodge `engine_combat_find_capital_defender`
uses for the loop-counter reuse, quirk #130).  The distance itself is retail's
`FUN_00404ead` (`engine_path_manhattan_distance`); only its `|dx|+|dy|` return is
consumed here (the dx/dy side-effect globals go unread), so it is inlined.

## 193 — AI brain step 0xa (attack launch) raises its force from the RICHEST realm but marches it at an enemy realm near that muster point, throttled by a personality-column cooldown

The AI offensive (`FUN_004a0015`, `engine_army_ai_launch_attack`, dispatch step
0xa) is a multi-stage pipeline assembled from a 9-function chain:

1. **Throttle.**  A per-player cooldown (`+0x15a`) ticks down each turn; the step
   does nothing else until it hits 0.
2. **Pick the foe.**  `most_hostile_player` (`FUN_004a0309`) returns a pinned war
   target (`+0xeb`) or the active slot this player's relation (`+0x84`) is most
   negative toward, requiring < −10.
3. **Pick the target realm.**  `select_target` (`FUN_004a03f2`) scans the foe's
   realms (forced-owner mode) for the best one near the muster realm — scored by
   chebyshev distance + a defended-keep penalty − stock/provision wealth bonuses
   — and stores it in `+0x4b`.
4. **Gate on the muster realm.**  The force is raised from the player's **richest**
   realm (`+0xe5`, chosen earlier by `pick_muster_realms` `FUN_004a0aaa`), but
   only if that realm is prosperous: provision `+0x24` ≥ 400 **and** approval
   `+0xc` ≥ 25.
5. **Raise + order the march.**  `raise_region_unit` (`FUN_004a5003`) musters a
   ~50-head field unit; the new occupant is stamped with order code 7 (`+0x1a`),
   pointed at the deploy cell `find_deploy_cell` (`FUN_004a689d`) picks inside the
   target realm (`+0x16`/`+0x17`), tagged with the target realm (`+0x151`) and its
   placed flag cleared (`+0x150`).
6. **Reload the cooldown** from the personality row at `+0x74` (6/10/5/10 by
   personality) — the **same per-personality table** whose `+0x50..+0x64` columns
   hold the build-order tags of quirk #191.  So the personality block is not just
   economy tuning: it encodes build orders *and* attack cadence as interleaved
   columns.

The split in step 4 is the notable design quirk: the AI does **not** attack from
the realm nearest the enemy — it always musters from its wealthiest realm and
accepts whatever march distance that implies, which is exactly why
`select_target` scores candidates from the muster realm's capital rather than a
frontier.

(Caveat for future porters: the Ghidra decompile of `pick_muster_realms`
`FUN_004a0aaa` transposes its two score variables — the r2 ground truth has the
stockpile-below-provision realm feeding the `+0xe6` score, not `+0xe5`.)

## 194 — tools gotcha: `gen_save_schema.py` is STALE vs the committed schema headers (clobbers entry 138's widened storage)

Running `tools/gen_save_schema.py` regenerates `save_schema_table.h` /
`save_schema_globals.{h,c}` from its CSV — but the committed headers carry a
LATER hand-adjustment the generator does not know about: entry 138's storage
was widened from its saved 96 bytes to `s_slot_00567970[150*16]`
(`buckets[6][16]`; the save still persists exactly 96 bytes — quirk above).
Re-running the generator silently narrows it back to `[96]`, which compiles
fine and then corrupts the category-bucket plane at runtime. Until the
generator/CSV is taught the widened storage, treat the three schema files as
hand-maintained: do NOT re-run the generator blind; if you do, restore with
`git checkout -- src/save_schema_*.{h,c} src/save_schema_table.h`.

## 195 — the sub-step duty-cycle LUT (`DAT_004e3260`) is 11 rows but MP hints ≥ 110 index rows 11..14 — an as-shipped OOB read into the next .data object

> **Correction (2026-11-07, data catalog):** the table is **15 rows** (0x960 B, 0x4e3260..0x4e3bc0): rows 10..14 hold
> 8 / 4 / 2 / 1 / 0 ones, the text starts at row 15. Hint ≤ 140 → row ≤ 14 never leaves the table: no OOB read. The
> port images the bytes verbatim, so behaviour was never affected. (`docs/data/catalog.tsv` k_sim_duty_lut.)

The rate controller `FUN_004bb978` gates each sim sub-step on
`DAT_004e3260[phase + (hint/10)*0xa0]` after clamping the scenario hint to
0x8c (140) — so the row index reaches 14, while the shipped duty table is
11 rows (0x6e0 bytes).  The table is a clean LINEAR-DECAY duty mask:
**row 0 = all 160 ones** (full duty — step every iteration), row 1 = 144,
row 2 = 128, … row 9 = 16, then 8/4/2/1/0 for rows 10..14.  Rows 11..14 read
PAST the table into the adjacent .data (the "ERR:…reloading data" ASCII +
i32 bytes), which is mostly ZERO with a few non-zero — so a high MP hint
degrades toward "skip almost every iteration", not the reverse.  SP pins the
hint at 1 → hint/10 == 0 → **row 0, full duty**: SP steps every iteration that
clears the slider gate (rate is gate/freeze-limited, NOT LUT-limited).

**Port-bug correction (2026-06-10 session 5, commit e179d8a):** the prior
`g_engine_rate_lut` image had a BOGUS row 0 (`1,1,0,…` ~2/3 duty, wrong file
offset) that capped the port at ~0.67 sub-steps/tick vs retail's ~0.95 — the
entire end-turn rate offset.  Re-imaged verbatim from VA 0x4e3260; live retail
`--watch` (hint=1, phase +1/frame, sub-step 0.95/frame) confirms the all-ones
row 0.  The port now images the full 0xe60 bytes verbatim so the OOB rows
behave byte-identically.

Related find, same chase (the grid_load_probe load-flip bisect): retail's
game-speed slider default is 90 (`FUN_004ae310` seeds `_DAT_0053f230 =
0x5a`), and the slider is NOT a save-schema slot — a boot→load session
plays at 90 regardless of the loaded save.  At 90 the slider gate needs
12 ms wall-clock between sub-steps, which is precisely why the turbo'd
retail seat runs exactly ONE sub-step at the load flip (elapsed-since-boot
huge) and zero during idle (sub-ms frames).  A port default of 100 (2 ms
gate) sub-stepped nearly every frame and would have drifted the LFSR
arbitrarily far from retail.

## 196 — an open event scroll IS the sim freeze: the consumer parks the event code in `DAT_0053e910`, the rate controller's extended-pause input

The in-game event-scroll consumer (`FUN_00472e46`, render phase) dequeues
the next pending ring record and stores its CODE in `DAT_0053e910`
(`g_sim_extended_pause`).  That same global is the rate controller
`FUN_004bb978`'s "extended pause" gate — non-zero returns 0 sub-steps in
SP.  So "a scroll is open" and "the sim is frozen" are one mechanism with
no separate modal flag; dismissing the scroll (0x476768) zeroes the code
and the sim resumes.  In SP the consumer pins its cooldown at ≥ 1 while a
scroll is open, so scrolls never time out — modal until input.

This is what froze retail for the whole grid_load_probe idle window after
exactly ONE sub-step: the load-flip tick posts the "Non 256 colour
display" warning (event 0x12a, posted by the per-tick body's head when
`DAT_0053f228 == 1 && DAT_0053f22c != 8`, one-shot `DAT_005533d4`), the
same tick's render dequeues it, and every later tick is extended-paused.
Deterministic — not a wall-clock artifact.

Related: the .sav CARRIES the live double-LFSR state (slots 18/19 of the
save schema are `DAT_0058fe28/08`!) — a load restores the saved RNG
stream and the flip tick advances it by exactly the one per-sub-step SP
stir.  The earlier "menu+load consumes ~102 pulls" reading of the
canonical+102/+103 numbers was wrong: the save itself stored +102.
Port-side corollary: any engine-side WORKING MIRROR of a schema slot
(the RNG mirrors, the event-queue ring struct) must re-seed from the
schema storage after save_load, or the first ingame mutation advances
from boot state (two such bugs fixed in the load route this session).

## 197 — `DAT_004eac50` (the current-SCREEN byte) is the text shadow-pair discriminator; an early port pass misread it as a "language tag"

`FUN_00402637` (the per-line text renderer) picks its two drop-shadow
colors from the CURRENT SCREEN id: screens 0x1c / 0x1f (title class) get
the gray pair (0x36, 0x2c), every other screen the parchment pair
(0x10, 0x1f).  The boot-era port named the gfx_text mirror
"language_tag" and set it ONCE to 0x1f at title bring-up — so every
in-game string (the event scroll's whole text block) rendered with the
title's gray shadows: ~8.1k px of the load-route popup diff, invisible
until the trace-studio per-label compare put truth|port|diff side by
side.  Renamed `gfx_text_set_screen_byte`; `engine_set_current_screen`
forwards every write so the two mirrors of `DAT_004eac50` cannot skew.
Port lesson: a retail global with TWO port-side mirrors (engine screen
byte + gfx_text discriminator) is a standing desync hazard — route all
writes through one setter.

## 198 — `DAT_005bb540` is a SWAPPED sheet slot: system2.pl8 on the title flow, system.pl8 ingame, re-ensured EVERY in-game tick

`FUN_00499a1c(which)` loads `(0x4dbbd0 + which*0x10)` → "system2.pl8"
(0) or "system.pl8" (1) into the single 0xdd18-byte slot at
`DAT_005bb540`, with an asymmetric latch (`DAT_004dbbf0`): a which==1
request when 1 is already resident is a no-op, a which==0 request
ALWAYS reloads.  The per-tick body's second line is
`if (DAT_0052af94 == 3) FUN_00499a1c(1);` — the in-game sheet is
re-ensured every tick (free after the first).  Consequences:
- The in-game UI sprites (the event-scroll seal 0x33, cursors, in-game
  buttons) index the system.pl8 image; title chrome (gauntlet buttons,
  scroll arrows) indexes system2.pl8 — same indices, different art.
  The port served system2 unconditionally and the popup seal drew as a
  gray box of wrong pixels (descriptor 0x33 points at different data
  in each file).
- The latch is written BEFORE the read: a failed system.pl8 read still
  flips it to 1, so every later per-tick ensure no-ops on a garbage
  blob.  Latent in retail (the file always exists); mirrored verbatim.

## 199 — `DAT_005aeb68` is BOTH the sprite-render error latch and the main-loop run-state: error code 4 == the deferred-shutdown value

`FUN_00408fb0` (the 27-byte error latch the sprite-render descriptor
gates call with code 4) stores into `DAT_005aeb68` — the SAME global
WinMain's loop polls for deferred shutdown (== 4) and the scroll
dismiss writes 3 into on game-end events (0xef/0xee/0x9f/0x9e arm of
0x476768).  So a corrupt sprite descriptor doesn't just log: it
REQUESTS APPLICATION SHUTDOWN (latch 4 == shutdown 4).  The port keeps
them as two storages today (gfx_sprite_render's g_last_error vs the
engine's shutdown latch) — safe while nothing corrupts descriptors, but
the unification (one global, both meanings) is required before the
game-end flow ports.  Run-state value map so far: 1 = quit (menu Exit),
3 = game-end handoff, 4 = deferred shutdown / sprite error.

## 200 — the open scroll re-renders EVERY frame and beats its one-shots off exact consumer-cooldown values (frame-locked, not wall-clock)

While `DAT_0053e910` != 0 the consumer calls the renderer
(`FUN_0047309e`) every render frame; the scroll surface is repainted
whole each time (then tile-marked via `FUN_004527a6(..., 2)`).  The
per-event one-shots fire when the decrementing cooldown
(`DAT_0057c954`: 2000 on open, −1/frame, pinned ≥ 1 in SP) crosses
EXACT values: 2000 = open frame (capture-fanfare cue, message wavs),
0x7c6 (1990) / 0x776 (1910) / 0x708 (1800) = camera-jump / speak beats
~10/90/200 frames in (per type).  All frame-count-locked: at retail's
~14 fps the speak lands ~6 s in; in turbo it lands almost immediately;
the beats NEVER fire on MP timed scrolls that dismiss before the
threshold.  The voice ranges in the 0x776 speak (`FUN_004b35c1`) are
codes 100..0xa9 (S1xx table), 0xaa..0xc5 (ktxxx per-payload variants),
200..0x11c (S2xx) — 198/199 and ≥ 0x11d (incl. the 256c warning 0x12a)
fall through silently.

## 201 — the last load-route map residual is COSMETIC: reload re-rolls the +3 terrain sprite-variant from an UNSAVED session LFSR, so block 0 can never round-trip

The grid_load_probe state ribbon collapses to a SINGLE differing save
slot — block 0 `g_overworld_map` (0x522f90), and within it only
sub-ranges 2/3/4 (records 512..1279 at the true 8-byte cell stride; the
"2 bytes / 128×128" theory in save-format.md is WRONG — the grid is
4096 cells × 8 B = 64×64).  A 3-way FNV diff (.sav vs port vs retail,
all per-range) pins it precisely: ranges 0/1/5..15 are .sav == port ==
retail; ranges 2/3/4 are **.sav == port ≠ retail**.  The port loads the
block verbatim and KEEPS the saved value; retail MUTATES those records
during load.

By elimination, the only load-tail (`FUN_004976a1`) writer that can
touch block 0 is `FUN_0046dbf5` (the autotile classify), and it writes
**only the +3 byte** — the terrain sprite variant:
`(&DAT_00522f93)[cell] = DAT_00568200 + DAT_0057c8a4`.  The siblings
don't qualify: `FUN_004298c1` (build_cells) and `FUN_0046dd34`
(randomise specials) write the tile_cells int-view 0x55cea0, not the
grid; `FUN_0046da4b(0)` evicts roster overflow but the raw .sav grid
has ZERO overflow (max 16 feature cells/key vs the 20 cap — simulated);
`FUN_0049ca32` writes player records, not the map.  So the residual is
+3-ONLY = a cosmetic per-tile sprite frame (which of 8 jittered art
variants), never gameplay.

Why it diverges: the variant comes from `FUN_00404b2c`, a 31-step LFSR
over `DAT_00591564` (`DAT_005c9a84 = state & 0x7f`).  **`DAT_00591564`
is NOT a save-schema slot** — it's runtime-only, seeded once at
InitInstance (0x34518632) and advanced by every puller since boot
(several 0x47xxxx menu/preload sites, plus classify itself).  On
RELOAD retail re-runs classify with `DAT_00591564` at its
boot+menu+load-advanced state, so the isolated-feature cells (the arm
where `DAT_0057c8a4 == 0x16` pulls the LFSR) get FRESH variants that
differ from the ones the .sav stored.  The port re-runs the same
classify but reseeds a LOCAL single-RNG to the fixed 0x34518632 —
reproducing the .sav's ORIGINAL new-game variants byte-exact (which is
why block 0 == .sav), but not retail's reload re-roll.

Consequence: this byte band can NEVER round-trip even in retail —
load→save→load→save re-rolls the variants every time, because the RNG
that drives them isn't persisted.  The port's fixed-seed choice is
arguably MORE faithful to the saved file than retail's re-roll.  The
downstream pixel cost is 273 px (0.089 %), the handful of re-rolled
variants that happen to be on-screen under the load camera; most of
records 512..1279 (rows ~8..19) sit outside the viewport.  Treated as
an accepted cosmetic divergence, NOT a bug.  To bit-match retail
under a FIXED seat one could capture retail's live `DAT_00591564` at
load-classify time and seed the port's classify with it — deterministic
per-seat but fragile (any seat/RNG-pull change shifts it) and
cosmetic-only, so deferred.

## 202 — the AI pathfinder's move-cost grid base (0x4f3ffe) is the cost cells (0x4f4080) MINUS 0x41 int16; it must be bound, or the first AI army to move NULL-derefs

`engine_path_field` (the cost-field flood/trace, retail FUN_0046f700/4701ac) reads
its move-cost input at `g_terrain[cell + dir]` for the nine direction offsets
`{0,1,2,0x40,0x41,0x42,0x80,0x81,0x82}` — a 3×3 window with row-stride 0x40, where
**`COST_DIAG == +0x41` is the cell ITSELF**.  So the grid's NW-sub-table base
(retail 0x4f3ffe) is the per-cell cost grid (retail `DAT_004f4080` =
`g_strat_move_cost`) **minus 0x41 int16**, and the grid needs a **0x41-entry skirt
on each side** so edge cells' off-map neighbours read cost 0 (impassable).  The
port had `g_strat_move_cost` as a bare `[4096]` array and never bound it — binding
the pathfinder to it directly is doubly wrong (off by 0x41 AND runs off the
unpadded end).  Fix: `g_strat_move_cost` is now a `+0x41` view into a padded
backing; `engine_strat_cost_pathfield_base()` returns the 0x4f3ffe base, bound in
`engine_game_init_bind_runtime_tables`.

## 203 — terrain drawer-B kind-2 **phase-4** (right-edge overhang, FUN_00456fd8) was unported → the "blacksmith forge" top dropped on right-column tiles

`engine_map_terrain` `drawer_b` (FUN_00406673) is the tall-sprite OVERHANG layer:
the part of a tile sprite that rises ABOVE the iso base diamond (mountain peaks,
building tops, the blacksmith anvil + orange forge fire).  It dispatches by sprite
`kind` (dir entry +0x14) × walk `phase`.  The port had ported kind-2 phase-0/1
(453e71) + phase-3 left-edge (455f04), kind-3/4 aligned — but **kind-2 phase-4
(the RIGHT-edge sibling, FUN_00456fd8)** fell into the `g_n_drawb_skipped++` TODO.
Phase 4 is the TRAILING cell of every `row_shifted` (FUN_00405c2f) — i.e. the
right column of the visible map — so any overhang sprite on a right-edge tile lost
its top.  In `grid_load_dense` that was the settlement tile at cell 4680 (screen
450,264): drawer_a drew the base diamond (cattle pen, lower roof), but the anvil +
forge overhang above the diamond never blit → the tile read as plain grass on top.
**This is NOT an anim-phase diff** (the figure never appears at any animation
counter — verified by sweeping the overlay anim base 0..7 across a 50-frame
capture; the overhang is terrain pass-1, not the overlay/army/feature passes).
Mis-attributed twice before settling: it is NOT the cat-7 army unit at that cell
(`tile_army` cat 7 → return, faithful to retail FUN_004071a0 `0x40786f`), NOT a
feature-pass head-link (cell +5 == 0 in save AND retail render dump), NOT the
overlay city/army pass.  Fix: extracted `456fd8`'s branch-B strip table
(stride 0x3a, 28 zero-skip copies, init_off 0 — same shape as 455f04) via
`tools/extract_drawb_stripB.py 456fd8` → `src/gen/drawb_stripb_456fd8.h`, added the
`kind == 2 && phase == 4` dispatch case in `drawer_b`.  Verified: grid_load_dense
blacksmith box 157→17 px, whole-frame mean parity 0.116%→0.058% (the same right-edge
overhang recurs on cells 3640/7280 across all frames).  Residual 17 px = the
forge-fire anim phase (8 px, port renders anim frame 0) + a pre-existing 9-px
left-edge terrain clip diff (unrelated, `oldport == port`).  Still skipped: kind-3/4
**edge** phases (param_5=2) — one off-blacksmith cell (4160) in this frame.

The gap stayed latent until the brain dispatcher was completed: the ONLY on-engine
caller of the pathfinder is the AI brain (steps 6/9/0xb deployment), and in a
turn-1 headless SP world steps 9/0xb find no field armies to move
(reached-but-input-empty, like step 1).  **Brain step 7's keep-garrison muster
(commit fd50752) creates the first AI army that actually moves**, so it was the
first code to invoke the pathfinder on-engine — and it crashed (access violation in
`engine_path_field_fill`) until the grid was bound (commit f6e77d5).  Corollary:
that crash is positive proof step 7 is live + active on the real new-game world.

## 204 — the multi-line event scroll (types 5–9) lays its objectives out through a SHARED y-accumulator that the wrap renderer advances per WRAPPED line — not a fixed per-block stride

`engine_popup_scroll_render`'s types-5..9 arm (retail `FUN_0047309e` final else, the
in-game welcome/objectives + combat hints 0xc8..0xda) stacks N word-wrapped body
sub-strings.  Each block's height is DATA-dependent — a long objective wraps to >1
line — so the panel height and the next block's Y can't be pre-computed.  Retail
threads this through a **shared module global `DAT_005cd4f8`** (= the gfx_text
`line_y_acc`, `g_line_y_acc` in the port): the wrap renderer `FUN_0040328e`
(`gfx_wrap_localized`) bumps it by the per-line height (`0x18` tall font / `0x10`
else) for **every wrapped line it emits** (its lines 71–78, alongside the local
cursor), and the caller seeds it + adds a flat `0x10` gap between blocks.  So the
caller runs TWO passes: a MEASURE pass (seed `0x20`, render each block at a throwaway
y that the bevel overpaints, purely to accumulate the wrapped height → panel height)
then a RENDER pass (re-seed `0x40`, draw each block at `popup_y + acc`).  A port that
used a fixed per-block stride (`row += 0x10` or `0x20`) instead of the shared
accumulator **collapsed a wrapping objective onto the next one** ("them multiply"
overprinting "Make weapons…") AND mis-sized the panel — because with a fixed stride
`body_h` no longer reflects the real wrap, which also flips the `body_h < 0x61`
y-bump.  Fix (commit pending): drive the arm off `gfx_text_{,set_}line_y_acc`
exactly like retail.  Verified on `endturn_seal` t030 (welcome objectives 0xc8,
type 7): objectives-frame diff 43%→14.8%, and the objectives panel proper
(x[16,464) y[64,232)) is now **0-px byte-exact** vs retail.  The 14.8% residual is
the SEPARATE stale-256-warning block (y[272,383], below the panel) — the dismiss
doesn't repaint the map there (the "256-warning dismiss-tick" follow-up).

## 205 — the in-game dispatcher's view-mode-2 LMB path BARE-RETURNS, skipping the tail edge latch (the one asymmetric exit)

`FUN_0042ff10` (`engine_ingame_input_dispatch`) ends every screen-case branch with
`goto LAB_00431f25` — the shared tail that runs the falling-edge latch on
`DAT_0057cac4` and clears `DAT_004eabb4 = 0`.  Every path reaches it EXCEPT one: the
strategic-screen view-mode-2 map-LMB jump (`DAT_005533b0 && DAT_004eafb4 &&
DAT_0057cb18 == 2`) does `DAT_004eac50 = 0; FUN_004350a1(); return;` — a bare
`return`, NOT a goto.  So on that one frame the tail latch does not run and
`DAT_004eabb4` is left at its entry value (not cleared).  The port mirrors this
(`st->returned_early = 1; return;` before the tail).  Almost certainly an oversight
in the original (every sibling path gotos), but it is in the binary, so it is
preserved for parity; pinned by `test_ingame_map_viewmode2_jump_bare_returns`.

## 206 — terrain-info compute (FUN_0046d5fe) reads the army record with the ORIGINAL head-link, THEN applies the 0x20→0 reset (statement order is load-bearing)

`engine_map_query_terrain_info` (FUN_0046d5fe) reads the hovered cell's head-link
byte (+7) into `DAT_005681e0`, immediately reads the army record
`(&DAT_0053f9b5)[DAT_005681e0 * 0x300]` (= record[head_link] byte +5) into
`DAT_005681d0`, and only AFTER that does `if (DAT_005681e0 == 0x20) DAT_005681e0 = 0`.
So the army-record fetch uses the UN-reset head-link (a head-link of 0x20 fetches
record 0x20, not record 0), while every later consumer of `DAT_005681e0` sees the
reset-to-0 value.  A port that hoisted the reset above the army read would fetch the
wrong record.  (0x20 is the "no real army / sentinel" head-link; the reset funnels
later logic to record 0.)  Pinned by `test_map_query_terrain_head_link_reset`.

## 207 — the terrain-info parchment box (FUN_00409397) is gfx_panel_draw with the chrome sheet baked in (no new blit code)

`FUN_0041befe` opens the screen-4 panel by tiling a parchment box via
`FUN_00409397(8, y, 0x1c, rows)` → `FUN_00409934(0,…)` (border) +
`FUN_00409c93(…)` (interior fill).  Those two are NOT new functions: they are
byte-for-byte twins of the already-ported `gfx_panel_draw_border` (FUN_0040946d)
and `gfx_panel_fill_interior` (FUN_004097c5) with the chrome sprite-sheet
`0x57d3d0` (panels.pl8) hard-baked as the asset instead of taken as a parameter,
and the mode parameter folded in (FUN_00409934 param_1 = the 0/1/2 border style).
So `FUN_00409397(x,y,c,r)` ≡ `gfx_panel_draw(gfx_load_chrome_sheet(), x, y, c, r)`
— the same composition `gfx_panel_draw` (FUN_00409346) already does.  The port
calls `gfx_panel_draw` directly; ~1.2 KB of blit code (00409934/9c93) needs no
separate port.  (The clip-mode dispatch DAT_005cdd20/DAT_0058fe18 in 00409934 maps
to gfx_panel.c's `blit_border_tile` clip_x_mode/clip_y_mode 1:1.)

## 208 — FUN_00426216 inside the terrain-info render is a DirectSound SFX-cue seam, not a render/state mutation (a no-op under audio-off)

`FUN_0041c208`'s army-stats arm (`flags&0x80 && terrain_type<0xd`) calls
`FUN_00426216(10/8/9)`.  It is easy to read as part of the stat layout, but the
decompile shows it indexes a COM-object table `&DAT_00522afc + arg*4` and invokes
two vtable slots (+0x48 then +0x30) — an IDirectSoundBuffer status query / restore
(the army-info UI sound cue).  It is the same `army_move_alert` seam the port maps
FUN_00426216 to (it dispatches to the unbound `g_alert_fn`).  Because drop-in audio
is off by default (feedback_mute_and_skip_intro), it is a no-op, so the port records
the cue arg in the classifier (`alert_arg`) for fidelity but the draw driver elides
the call — matching the project-wide deferred-SFX convention (cf. the event-scroll
renderer's TODO(sfx) sites).  Not load-bearing for the panel pixels.

## 209 — the strategic map needs the EVERY-FRAME composite (FUN_004050c0, tick line 42), not just the demand-redraw render (FUN_0040f1a0, line 36)

Retail's in-game tick (FUN_004b99c0) composites the strategic map in TWO places:
the demand-redraw seam (line 36, `if (DAT_0057d340) FUN_0040f1a0(redraw)` — the
FULL screen incl. HUD, on a redraw request) AND, unconditionally every frame,
line 42:

```
if (DAT_0056d650 == 0 && DAT_0057c8fc == 0 &&
    (DAT_004eac50 == 0 || DAT_004eac50 == 0x10)) FUN_004050c0();
```

FUN_004050c0 (53 B) bumps the 0..3 redraw phase + calls FUN_004cfb08 (the 16 ms-
gated anim-counter advance + the 3-cluster map-scene render).  The port had ONLY
the demand-redraw seam, so the map was static between redraws and the anim
counters were pinned at (0,0).  Consequences, all one root cause:

* the boot 256-colour warning (a capture-host artifact) survived as stale
  framebuffer BEHIND the welcome/report scroll on screen 0x27 — there was no
  every-frame screen-0 composite to repaint the clean map before the 0x27 flip
  (the welcome objectives render byte-exact; the map behind them was stale).
  endturn_seal t030 index-diff was 14.82% (all of it the warning text below the
  scroll); with the every-frame composite it is 0.08% (just the cattle/castle
  anim speck).  THIS is what "port the 0x27 report renderer's map-behind pass"
  reduced to — not a 0x27 dispatch arm (FUN_0040f1a0 has NO 0x27 case; the
  report scroll renders via the popup pump FUN_00472e46, and the map behind it
  is simply whatever the last screen-0 composite left).
* the frozen cattle/castle/water animation (strat-map-render-gaps #1/#3): the
  counters now free-run in live gameplay.
* a season/bank change would never show live (the map kept its load-time
  terrain) — the every-frame composite re-reads the season banks each frame, so
  a bank reload shows on the next frame.

Drop-in deviation: the port composes the FULL strat frame each in-game frame
(engine_screen_strat_render) rather than retail's incremental map-scene-only
(FUN_004050c0 leaves the HUD untouched).  The port already memset-clears +
fully redraws each strategic frame, so a full repaint is simpler, has no
map/HUD-overlap risk, and is equally correct; cost is a per-frame HUD redraw
(trivial).  A LIVE/PINNED anim-mode flag (engine_screen_strat_set_anim_live)
keeps the single-shot seats (--strat-map/--strat-load/--ti-cell) + the
capture-parity harness at anim frame 0 for byte-exactness; only the in-game
tick (g_engine_state==3) sets LIVE.

Turbo-capture caveat: the 16 ms anim gate (FUN_004cfb08, now_ms delta) barely
advances under --turbo on BOTH engines, so cattle/castle stay near frame 0 on
both sides — which is exactly why grid_load_dense parity stays clean (0.055%
mean).  The animation is a real-time-gameplay effect; a season change (a STATE
change, not anim-gated) shows immediately on the next frame regardless.

## 210 — the "+0x1c0/+0x1c1 dual-use byte" scare was a false alarm: it's one concept (keep level) read by two formulas, not two meanings

Session 11 flagged a porting risk: `engine_realm.c`'s income functions
(`engine_realm_accrue_income` / `engine_realm_recompute_realm_income`) call
`g_per_army_768` row field `+0x1c0` the realm's tax "standing band", while
`engine_territory.c`'s `engine_territory_stamp_secondary_keep` doc comment
calls the SAME offset a "banner-level"/settlement tier — different enough
wording to worry it was a genuine retail dual-use byte (one memory cell,
two unrelated meanings depending on which code reads it), which would make
porting the event-popup type-0x12 render arm (which also reads it) a guess
without resolving which meaning applied there.

An r2 byte-search for every direct reference to the row's `+0x1c0`/`+0x1c1`
addresses (`/x` on the literal displacement, no full-binary `aa`) turned up
the WRITER: `fcn.00436d02` (a "start a keep construction" handler, called
from the build-menu click chain around 0x436a88-0x436d02) takes `(key,
new_level)` and:

```
if (a[+0x1c0] != 0)            // realm already had SOME keep level
    a[+0x1c1] = a[+0x1c0];     // snapshot the OLD level before overwriting
a[+0x1c0] = new_level + 1;     // bump to the NEW target level
a[+0x1c3] = 1;                 // construction-kind = "new" (vs 2 = "upgrade")
...
engine_realm_recompute_realm_income(key);   // called immediately after
```

So `+0x1c0` is the realm's current/target keep level, full stop — bumped the
INSTANT construction starts, not when it completes.  `+0x1c1` is a snapshot
of what that level was right before this construction began (skipped, i.e.
left at 0, when there was no previous keep at all).  The income functions'
"+0x1c3-gated cap using +0x1c1" is just: *while* a construction is in
progress, income is computed off the OLD (still-functional) level instead of
the new target you haven't finished building yet — the identical "keep
level" concept, just consumed as a tax-rate-by-level lookup key in one
formula and a population/garrison-by-level lookup key (`k_keep_tbl`,
`engine_combat.c`) in another.  `engine_combat_settlement_find_slot`
(already-ported `FUN_004a551b`) computes the EXACT same
`k_keep_tbl[target] − k_keep_tbl[old]` delta for a different purpose
(sizing the new garrison), confirming the reading independently.

The real tell that should have killed the "dual-use" worry immediately: the
popup's `g_popup_flag6` (the realm key it reads `+0x1c0/+0x1c1` from) is the
SAME `key` the posting code (`engine_realm_complete_settlements`) was just
iterating when it posted the event — i.e. the popup is reading the row the
producer function itself just finished interpreting as a keep level.  When a
field looks ambiguous across files, check whether the suspicious READER and
a known-good WRITER/producer share the same call chain before assuming a
genuine aliasing — most "dual-use" scares are a vocabulary mismatch between
two porting sessions' comments, not a byte doing two jobs.  See
`project_event_popup_arms` memory for the resulting type-0x12 port
(commit history same day) — visually verified via `--force-popup`: a
brand-new keep shows "50 troops assigned as a garrison" (`k_keep_tbl[1]`),
an upgrade shows "50 extra troops sent to castle"
(`k_keep_tbl[4]-k_keep_tbl[2]`), confirming `k_keep_tbl` is a garrison
troop-count table, not population or gold as its old name guesses suggested.

## 211 — screen 0x10/0x1e's per-frame refresh was nested inside the demand-redraw gate, freezing the handoff cooldown under idle-mouse play

`engine_tick_title_render_step` (engine.c) is the port's stand-in for
retail's two DISTINCT per-frame render actions inside `FUN_004b99c0`: the
demand-redraw seam (`FUN_0040f1a0`, a full repaint only when a state
handler explicitly asked for one) and the unconditional per-frame UI
refresh (`FUN_004ba26e`, which runs EVERY tick regardless — the mechanism
that keeps a hover highlight tracking the mouse smoothly). The title
screen's own code already modeled this split correctly (a `full` vs
per-frame-buttons branch). The screen-0x10 (`engine_tick_army_move_hover_
step`) and screen-0x1e (`engine_tick_confirm_dialog_render`) arms — both
explicitly commented as "`FUN_004ba26e`'s arm" — were nevertheless nested
INSIDE the `if (g_demand_redraw != 0)` block alongside the screen-0/4
demand-gated arms (a plain copy-paste-shape mistake, not a deliberate
choice).

Consequence: `engine_tick_army_move_hover_step` calls `engine_army_move_
confirm_hover_tick`, which decrements the screen-0x10 handoff cooldown
(`DAT_00553ecc`, starts at 0x28=40) that gates the SECOND click of the
army-move two-click flow (`in->lmb_pressed != 0 && in->handoff_cooldown
< 1` in `engine_ingame_input.c`). With the hover step demand-gated, once
nothing else in a given session happened to also request a redraw, the
cooldown froze above 0 forever and the second click could never resolve
— a real soft-lock on the just-landed (session 14) army-move feature
under ordinary idle-mouse play. `--force-popup`-style verification never
exercises the live per-frame tick loop across idle frames, so this was
invisible to every prior verification pass on this feature. Caught only
by scripting a fully organic two-click test and watching it silently fail
to reach the resolver on the second click. Fixed by hoisting both arms
out of the demand-redraw gate to run unconditionally, matching retail's
`FUN_004ba26e` semantics; the screen-0/4/other arms stayed gated (those
really do correspond to retail's separate `FUN_0040f1a0` demand seam).

## 212 — `strat_draw_menu_bar`'s `engine_set_current_screen(0)` was a screen-0-only-render-era assumption that broke once screen 0x10 shared the same composite

`engine_screen_strat.c`'s `strat_draw_menu_bar` (part of `engine_screen_
strat_render`, retail `FUN_00419c78`) called `engine_set_current_screen(0)`
unconditionally at its top. Added in commit 8c2eb01 (2026-06-02) when this
whole render path was ONLY ever invoked for the screen-id-0 dispatch arm —
retail's actual `FUN_00419c78` (checked via the decompile,
`docs/decompiled/by-address/419c78.c`) only READS `DAT_004eac50` for its
own internal gating and never writes it, so this call was never a
faithful-port requirement; it was a defensive "make sure the screen mirror
(`gfx_text`'s shadow-color-pair selector) reads 0" leftover from a period
when 0 was the only value this path could ever see.

Commit d7cc930 (2026-06-19, "every-frame strategic-map composite") wired
the SAME `engine_screen_strat_render` call to ALSO fire every frame while
`g_sim_current_screen == 0x10` (the army-move handoff screen) — at which
point this call started stomping the screen back to 0 on literally the
FIRST render tick after `engine_army_select_open_handoff` armed it,
independent of finding #211 above (fixing #211 alone still left this bug:
the hover-tick would finally run, but the SAME tick's map-composite render
would immediately reset the screen it just correctly set). The army-move
handoff screen was unreachable in real play from the moment commit d7cc930
landed, for over two weeks of sessions, because nothing had yet tried a
fully organic (non-`--force-popup`) two-click test through the live tick
loop.

Confirmed safe to delete outright (not just special-case for screen 0x10):
every current caller of this render path already sets the screen itself
before rendering — `bringup_strat_map` (its own inline duplicate menu-bar
code, main.c), `engine_map_post_load` (the real load path, main.c), and
the live tick's own input-dispatch write-back all independently establish
`g_sim_current_screen` before this function ever runs. Zero behavior change
for every screen this path already served correctly; only screen 0x10
(and any future non-zero screen sharing this composite) benefits.

Both #211 and #212 were found by the SAME session's fixture (`tools/make_
owned_county_fixture.py` + `tools/traces/plain_county_click.jsonl`) — see
`project_event_popup_arms` memory for the full click-chain writeup.

## 213 — harness: bare `{"auto_dismiss":N}` segtrace ops fired at DIFFERENT frames on port vs retail (capture-scheduling asymmetry), racing the first-time hint — the real cause of session 20's retracted "Army Movement hint gap"

Not an engine quirk — a trace-HARNESS asymmetry that masqueraded as an
engine divergence across two sessions.

Both segtrace drivers drain the op list in order, EXCEPT captures:

- the **port** (`src/input_trace.c`) keeps `{capture:N}` ops in the
  sequential queue — an op after a capture waits until that capture's
  frame passes;
- the **retail agent** (`openlords2-agent.js`) pre-arms all captures
  out-of-band into `g_capture_at_frames` at wait-resolution — captures
  never block the queue.

So a bare `{"auto_dismiss":0}` placed after `{"capture":1000}` fired at
~t1000 on the port but ~t910 on retail (right after the preceding click's
t:908 input op posted).  The first-time "Army Movement" hint (code 0xd2,
runtime-only `g_hint_shown`, fires ~1-2 frames after the screen-0x10 arm
on BOTH engines with ep=0xd2 set — confirmed via `--watch
ep=0x53e910:u32 scr=0x4eac50:u8 hint_d2=0x4f036a:u8`) then landed inside
the port's still-on window but outside retail's: the port dismissed it,
retail's stayed open eating every later click.  Session 20 saw exactly
this, suspected a port hint gap, retracted the claim, and left it
unresolved; the sandbox-state theory recorded there was wrong — it was
this scheduling race all along.

Fix (session 21): the op now takes an optional anchor-relative gate —
`{"t":N,"auto_dismiss":M}` — honoured identically by all four parsers
(src/input_trace.c, openlords2-agent.js, frida_capture.py `_load_segtrace`,
trace_studio_v3/trace_ops.py).  The bare form keeps the old
immediate-drain semantics for existing traces.  Any trace that must order
an auto_dismiss toggle against t-stamped activity MUST use the timed form.

## 214 — retail dispatches in-game input at the tick END (4b99c0 line 192), AFTER the renders and the tooltip tick — the port's input-first order makes tooltip backdrop restores go stale on click-flips

Discovered session 22 via the county-panels trace (user-flagged: the
happiness panel's "Last season 65♥" persisted as a rectangle, plus
UI-scrap patches over terrain, on the port's tax_open frame only).

Retail's per-tick order in FUN_004b99c0:

```
line  36  FUN_0040f1a0(DAT_0057d340)   demand-redraw seam
line  42  FUN_004050c0()               every-frame map composite
line  99  FUN_004ba26e()               per-frame UI refresh (widgets)
line 112  FUN_00476e95()               tooltip tick
line 192  FUN_0042ff10()               ★ INPUT DISPATCH — near the END
```

(The ingame-input plan doc's "called before the render phase" claim was
wrong.)  Consequence: a click that flips the screen sets the redraw at
the tick END and the repaint lands NEXT tick — so retail's tooltip
clear (mouse activity → FUN_004770b8 → FUN_004b3ec0 backdrop restore)
always writes onto the SAME frame the backdrop was captured from: a
correct box-erase, never visible.

The port dispatches input FIRST (engine_tick_ingame_input_step early in
the tick), so a click-flip repaints the frame BEFORE the tooltip tick
runs; the clear then restored a backdrop captured on the PREVIOUS
screen onto the fresh frame — stamping old-panel pixels (the 65♥
rectangle) over the new panel.

Port compensation (kept local, same pattern as the widget-repaint
note): `g_canvas_repainted_this_tick` — raised by every full-frame
paint arm (screen-0 / screen-4 / county-panel demand arms + the
every-frame composite), reset at the tick top.  The tooltip treats a
raised flag as "backdrop invalid": the clear SKIPS the restore (the
repaint already erased the box) and a tip shown through a repaint
RE-CAPTURES before re-rendering.  Visible pixels match retail.  If the
port's input dispatch is ever moved to retail's tick position, this
compensation becomes inert (the flag is never set when the clear runs)
and can be removed.

---

## 215 — the cursor→game mapping is a TRUNCATED percent compose, so a harness that inverts the ideal ratio lands every injected click ~1% short

`FUN_004b191e` maps the cached client-space cursor to game coords not as
`game = cached * game_extent / viewport_extent` but as two truncating
integer helpers composed (`FUN_00404dc1` then `FUN_00404d6b`):

```
scale = (game_extent * 100) / viewport_extent     ← C division, truncates
game  = (cached * scale) / 100                    ← truncates again
```

On the headless capture seat the viewport rect `DAT_004ea180..18c` holds
the DESKTOP metrics (quirk #120's `DAT_0053f228 != 0` branch — 1920×1080
here), so `scale_x = 64000/1920 = 33` — truncated from 33.33.  The
effective mapping is `game = client * 33/100`, NOT `client/3`: a whole
percent smaller than the true ratio.  Consequences:

- An injector that pre-scales authored game coords by the *ideal* ratio
  (`cx = round(gx * vp/gw)`, the agent's historical form) gets undone by
  the truncated divide ~1% short: authored x=599 resolved to 593, y=226
  to 223 — session 22's "(6,2-3) px short" finding.  (That session's
  writeup guessed the cause was a ~646×486 client rect; the real killer
  is the percent truncation — the rect is the full desktop.)
- Whole bands of game coords become UNREACHABLE by any client coord
  (at scale 33, game x ∈ {634..639} has no preimage inside the client),
  but `PostMessage` lParams may lie PAST the window edge, so an exact
  inverse can still hit them.
- The exact inverse of the quantized form is `cx = ceil(gx*100/scale)`
  (smallest client coord mapping back to `gx`; exact whenever
  scale ≤ 100).  `rescaleMouseLParamToClient` now does this.

Verified empirically (the way the s22 flag demanded): a watch on
retail's own cursor mirrors `DAT_004e65fc/f4` through the
county_panels_click trace — all 10 authored coordinates reproduce
EXACTLY post-fix.  Notably the fix changes no pixels on that trace:
every hit-box is ≥24 px and the grid-button tooltips anchor to the
CLASSIFIED HOTSPOT, not the raw cursor (the s22 "tooltip x-offset" flag
was quirk #214's stale backdrop, already fixed).  The fix matters for
anything genuinely cursor-derived: slider drags, tight hit-boxes,
cursor-anchored tooltips.

> 📍 `tools/frida/openlords2-agent.js` (`rescaleMouseLParamToClient`),
> `src/engine_input.c` (`percent_ratio`/`apply_percent`), Ghidra
> `FUN_004b191e` / `FUN_00404dc1` / `FUN_00404d6b`.

## 216 — the post-load state-slot "divergences" (map slot 0 / strategic slot 3 / AI-cursor slot 84) are load-flip bookkeeping, not a sim divergence — map geography is byte-identical

When the v3 state oracle was repaired (quirk-adjacent, s23) it surfaced
slots 0/3/84 hashing apart on every county-panel column.  s23 feared a
real sim divergence and hypothesised popup-dismiss timing.  Session-24
byte-granular diagnostics (`lane8` per-cell-field + `f2bits` per-bit,
added to `state_dump.c` + the agent) settle it:

- The overworld map (`g_overworld_map`, 8 B/cell) matches bit-for-bit
  on **terrain (+0)** and **region (+7)** — the actual geography is
  identical.  The ONLY persistent map diff is field **+2 bit 0x01**,
  the per-cell **redraw-request/dirty flag**: `FUN_004063c1` does
  `if (cell[2] & 1) { cell[2] &= 0xfe; FUN_00452628(x>>4, y>>4); }` —
  set means "repaint this tile", cleared once repainted.  The two
  engines flag different cells dirty because the port repaints clean/
  full while retail is dirty-rect-gated (the same root as the known
  "panel-switch scraps", gameplay-and-screens.md).  It is invisible in
  the composited frame — the pixel render is already verified
  byte-exact.
- A separate TRANSIENT layer (slot 4 `g_per_army_768`, map field +5
  army head-links, part of slot 3) self-heals by the first panel open:
  a fine sub-tick settle-phase offset right after load.  This — not
  the persistent core — was the timing-sensitive part.
- slot 3 residue = RNG-scratch strategic fields; slot 84 = the AI
  round-robin cursor start offset (port 2 / retail 1, stable — both
  cycle all realms, so benign unless a turn-order divergence appears).
  All downstream of the load-flip's KNOWN +1 LFSR pull (slots 18/19) +
  popup flag (74).

Takeaway: nothing writes the map during idle browsing, so a stable
divergent band was baked in at LOAD (before any dismiss) — which the
popup-timing story can't explain.  No fix needed; severity cosmetic.

> 📍 `src/state_dump.c` (`lane8`/`f2bits`), `tools/frida/openlords2-agent.js`
> (`stateDumpOnce`), `tools/traces/slot_triage_probe.jsonl`, Ghidra
> `FUN_00467770` (plane unpack) / `FUN_004063c1` (dirty-bit render).

## 217 — the trade grid (0x0c) computes its figures ONLY on the open render; the per-frame refresh redraws content+arrows with NO compute, so a purchase in progress survives the redraws its own arrows drive

`FUN_00428daf` (the trade-figure compute) unconditionally zeroes the running
buy/sell net (`DAT_00554170`), total (`DAT_00553f64`), and ale delta
(`DAT_0053e8c0`) at its top.  Retail runs it exactly once per grid open — from
`FUN_0043530e` → `FUN_00416308` (the OPEN render) — and then never again while
the grid is up: the per-tick refresh is `FUN_004ba26e`'s 0x0c arm, which draws
`FUN_0041635f` (content) + `FUN_0040cfd2(0x30,0x50,…)` (the arrow strip) but
does NOT call the compute.  So the arrows (`FUN_00435339` etc.) mutate the net,
and every subsequent frame repaints the new figure without wiping it.

The trap for a drop-in that folds the render into a single demand-redraw arm:
if that arm calls the compute, then clicking an arrow (which requests a
redraw) recomputes → net snaps back to 0 → the panel is frozen at "Buying 0 /
Total 0" no matter how many times you click.  The port splits it exactly like
retail: `engine_trade_render` (open, computes once behind a one-shot guard) +
`engine_trade_refresh` (the per-frame content+arrows, no compute), the latter
run from the UNCONDITIONAL per-frame render seam (same tier as the county
0x15/0x19 arrow refresh), and the arrow actions request no redraw at all.

Corollary — the merchant zone hit-test `FUN_004357a6` (screen 8) also runs
every frame in the router and sets the hovered good (`DAT_0056d8ac =
mercgrid[cursor]`) UNCONDITIONALLY, opening the grid only on an LMB press over
a rug good; porting it as click-only leaves the good stale after a trade.

> 📍 `src/engine_trade.c` (`engine_trade_render` / `engine_trade_refresh` /
> the arrow arms), `src/engine.c` (`ti_ui_router` trade arm, the per-frame
> `engine_tick_trade_render(0)` seam, `ti_merchant_zone` hover), Ghidra
> `FUN_00416308` / `FUN_00428daf` / `FUN_004ba26e` (0x0c) / `FUN_004357a6`.

## 218 — the Court/Treasury (0x09) opens over the MAP from the strategic bottom-right tab strip, NOT the merchant; and its .rdata "button table" 0x4dc680 is a short[] HOTSPOT table, not a sprite-button record table

Two scouting facts recorded before this port were WRONG, and both fall out of
one mis-read.  `FUN_00416925` (the Court render) loads no backdrop and no
palette — `FUN_0040f1a0` case 9 is a bare `FUN_00416925()` — so it draws over
whatever is retained.  The early guess (from assuming the Court is a
county-interior screen) was that it reuses `merchant.pl8` / `merchant.256`.

It doesn't.  The Court is reached from the strategic map's **bottom-right
county-action button strip** — retail hotspot table **`0x4dc680`**, rendered +
click-dispatched at origin `(0x1de,0x1ae)` by `FUN_00432967 → FUN_0040e3ee` —
whose rec1 (`FUN_0043ae30` param 2) flips to screen 9.  So the Court draws over
the retained **strategic map** (`base01.256`), leaving the whole right HUD
panel visible; the right ~200 px of the frame is untouched map/HUD.  Proven
against retail: driven to the real screen 9 via the map tab, the ported panel
is **0/107548 px** diff over its interior (the whole parchment, every
text/icon/value row).

The second mis-read: `0x4dc680` is NOT the `0x18`-byte `engine_button_rec_t`
layout (x/y/sprite/kind/action/…).  It is the **short[] HOTSPOT** layout —
`short[0..3] = x0,y0,x1,y1` (the box), `+0x8` action, `+0xf` type, `+0x10`
param → `DAT_0059154c`.  Extracting it as sprite-buttons makes rec1 look like
`sprite=0x41 kind=0x1d`; read as a hotspot it is the box `[0x22,0]-[0x41,0x1d]`
= screen `(512..543, 430..459)`, which is exactly where a click lands the
Court.  (The genuinely-merchant tab table `DAT_004dd808`, by contrast, is the
`0x18`-byte layout, and its first two "tabs" are no-op stubs — a separate dead
end that also fed the wrong model.)

Consequence for a drop-in: seed the Court seat over the strat frame with NO
merchant/palette load (the port's `engine_dev_force_treasury_render` just
paints over the retained map), and wire the organic open by un-narrowing the
already-ported End-Turn strip hit-test (`engine_endturn_hit_test` = the
`0x4dc680` walker) to also fire the Court tab.  The seal's real exit is Rankings
`0x20` (`FUN_004351c4`), not the map — deferred until 0x20 renders; RMB closes
to the map in the meantime (retail's own close arm, `42ff10` @ `0x430757`).

> 📍 `src/engine_treasury.c` (`FUN_00416925`), `src/engine.c`
> (`engine_tick_treasury_render`, the screen-9 demand + per-frame seams,
> `k_treasury_tab` in `engine_endturn_hit_test`, `engine_dev_force_treasury_render`),
> `src/engine_ingame_input.c` (screen-9 close), Ghidra `FUN_00416925` /
> `FUN_0040f1a0` case 9 / `FUN_0043ae30` / `FUN_00432967` / table `0x4dc680`.

## 219 — a type-5 (press-latch) router seal FIRES from pass 1 of the button dispatch, ~0x14 frames after the click, on a frame where the dispatch RETURNS 0 — so a follow-up transition must key off the action flag, NOT the dispatch's `hit`

The router button dispatcher `FUN_0040da1e` (port: `engine_menu_button_dispatch`)
runs two passes.  For a **type-5** button (the "continue" seal on the Court 0x09,
Rankings 0x20, trade-confirm 0x0c, …) the two passes split responsibility across
DIFFERENT frames:

- **press frame** — pass 2 hit-tests the cursor, sees the new LMB press, arms the
  long press-flash (`pressed = 0x14`, `state = 1`), and returns `i+1` ("hit").
  It does NOT run the action yet.
- **~0x14 frames later** — pass 1 (which decrements every armed button's
  `pressed` timer every call, cursor-independent) reaches 0 and THEN invokes the
  action callback (`if (r->type == 5) dispatch(...)`).  But pass 2 that same frame
  finds no new press, so the dispatch RETURNS **0**.

The trap: a `ti_ui_router` arm that wires the seal's consequence as

```c
int hit = engine_menu_button_dispatch(…, seal_action);
if (hit && g_flag) { …transition… }        // BUG: hit==0 on the firing frame
```

never fires the transition — on the only frame the callback sets `g_flag`, the
dispatch return is 0.  (The count-down decrements once per dispatch CALL, so the
router arm must also be reached every tick, which it is — `FUN_0042ff10` runs
`FUN_004ba9c8` every tick.)  Correct shape: reset the flag before the dispatch,
then act on the FLAG alone (ungated by `hit`), consuming the click when it fires:

```c
g_flag = 0;
int hit = engine_menu_button_dispatch(…, seal_action);   // sets g_flag in pass 1
if (g_flag) { …transition…; return 1; }
return hit ? 1 : 0;
```

Found porting the Court-seal → Rankings 0x20 nav (s30): the seal painted its
press-flash but the screen never advanced until the gate was moved off `hit`.
(The trade-grid confirm seal `0x0c` uses the same `hit && flag` shape and
happens to work only because its committed state is what the trace re-reads, not
an immediate screen flip — worth revisiting if a trade close ever looks sticky.)

> 📍 `src/engine.c` (`ti_ui_router` screen-9 arm, `treasury_button_action`),
> `src/engine_menu_input.c` (`engine_menu_button_dispatch` passes 1/2), Ghidra
> `FUN_0040da1e` / `FUN_004ba9c8` (screen-9 arm) / `FUN_004351c4`.

## 220 — the armory tab opens 0x17 (army create/change), NOT the bare 0x0a view; and the 0x0a "view" is NOT static — it has a per-frame torch refresh

Two traps when porting the county armory (screen family 0x0a/0x17/0x0d):

**(a) The map's county-action strip (table 0x4dc680) rec0 opens screen 0x17,
not 0x0a.**  `FUN_00432967` → `FUN_0040e3ee(0x1de,0x1ae,&DAT_004dc680,6)` walks
6 records (0x18 B each); rec0 = box [0,0]-[0x21,0x1d], type 1, param 1 →
`FUN_0043ae30` param 1 → screen **0x17** (the interactive army create/change
overlay, `FUN_00418653`, drawn OVER the armory backdrop `FUN_00417ea7`), owner-
gated.  The bare **0x0a** VIEW is reached only by RMB-ing out of 0x17
(42ff10.c screen-0x17 arm: RMB → 0x0a + `FUN_004aa90a`).  (rec1 → 9 Court,
rec2 param 3 → 0x18 send-supplies, rec5 = End-Turn `FUN_0043ac23`.)  So an
organic 0x0a olv3 must drive tab→0x17→RMB→0x0a.

**(b) `FUN_00417ea7` (the 0x0a render) draws NO torches — but the view still
shows lit torches, because the PER-FRAME refresh does.**  `FUN_004ba26e`
case '\n' = `FUN_00418fc5` (sweep restore) + `FUN_00419243` (two flames) +
`FUN_004190db` (sweep).  On the bare view `DAT_005679d0` is 0 (zeroed by
`FUN_004aa90a`), so the sweep + its restore no-op; only `FUN_00419243` draws —
two flames from armtorch.pl8 (`DAT_005533c4`) at (0x9a,0x76) and (0x19a,0x76),
sprite = the shared phase frame `DAT_005aea54` (0..0xc) and +0xd for the right
torch.  Chasing the "static scene" model leaves the torches dark; they belong
to the per-frame seam, not the flip render.  The muster COUNTS come from the
per-player troop pool `DAT_0053f6a0` (0x80 stride), which `FUN_004aa90a` seeds
on entry from the strategic weapon stocks (rec +0x13c + t*4) + the conscript
population (`DAT_00543fd8`, 0 at the pct=0 first entry) — the render only reads
it back.

> 📍 `src/engine_armory.c` (`engine_armory_render` / `engine_armory_refresh` /
> `engine_armory_muster_init`), `src/engine.c` (screen-0x0a demand + per-frame
> arms, `engine_endturn_hit_test` for the strip), Ghidra `FUN_00417ea7` /
> `FUN_00419243` / `FUN_004aa90a` / `FUN_0043ae30` / `FUN_004ba26e` case '\n'.

## 221 — armory 0x0d has NO demand-redraw case: it renders DIRECTLY from the click; the troop march fires on weapon SWITCH, not on assign; and the whole family's residual diff is anim-PHASE, not pixels

Port traps from making the armory family (0x17 create/change + 0x0d assign +
the troop march) organic (s32):

**(a) Screen 0x0d is invisible to the demand-redraw seam.**  `FUN_0040f1a0`
has NO `'\r'` case — the assign overlay is painted DIRECTLY by the zone click
(`FUN_004358b0` tail-calls `FUN_004184c6`) and the 0x0d→0x0a close re-renders
the view directly (`FUN_004180f6`, a reduced `FUN_00417ea7` with no muster
re-seed / no reloads / no strip re-capture — byte-equivalent to the full
render, which the port reuses).  Everything else on 0x0d is carried by the
per-frame arm (`FUN_004ba26e` '\r' = sweep-restore + torches + tally
`FUN_004181eb` + `FUN_00418e2d` readouts/demo-figure + sweep + the 4 gauntlets
over `DAT_004dd8c8` at origin (0x60,4)).  A port that routes 0x0d through
demand-redraw paints nothing (the generic stub) — render from the click, like
retail.

**(b) The troop-march trigger lives in the NEXT zone click, not in assign or
close.**  `FUN_004aabd8(county, prev_weapon)` runs at the top of
`FUN_004358b0` — i.e. when you click ANOTHER muster figure — and fires only
when the previous weapon's count rose past its `+0xc` snapshot (taken when
that weapon was selected).  Assigning men and RMB-ing straight out never
marches; switch weapons and the marcher walks in (frames 0..7), salutes at
the weapon's rack x (`0x4de6f0` table; frames 8..0xc via `DAT_005681f8`/3+8),
then walks OUT (+0xd frames) until x ≥ 0x280.  The restore strips it walks
over are captured by the 0x0a OPEN render (`FUN_004b3f0a` ×4: y 0xd8, 0x9e
rows, segments x 0/0xa0/0x140/0x1e0 × 0x3c/0x3c/0x3c/0x28 dwords — buffer
offsets are DWORD offsets, 0x2508 apart).  Movement gates on the 20 ms phase
tick; the walk/salute cycle on the 80 ms tick.

**(c) The tally/zone clicks are stock-gated and SILENTLY consumed.**
`FUN_004358b0` returns after the `pool[zone*0x10+8] > 0` check — clicking a
zero-stock weapon's figure/readout does nothing (but the router still ate the
click).  lastturn.sav's Lancashire holds stock only in weapons 3/4/5 (swords/
pikes/bows) — a trace clicking tally boxes 1/2 exercises only the gate (it
holds identically on both engines).  Keying split worth knowing:
`FUN_004aa90a` (muster seed) + `FUN_004aabd8` (march) key the pool row by the
county OWNER byte (+5); `FUN_004358b0` / the assign gauntlets /
`FUN_00418e2d` key by the LOCAL player (`DAT_0057c8cc`) — identical
organically because the tab entry is owner-gated.

**(d) After the port, the family's ONLY residual frame diff is animation
PHASE.**  The armory_family olv3 (tab → 0x17 → slider ×2 → RMB → 0x0a →
assign w4 +2 → switch w5 (march) → RMB → Create → map): the three 0x17
frames are idxdiff **0**; every other frame's diff sits exactly inside the
torch flames (`DAT_005aea54`), the 0x0d demo figure (`DAT_005aea48`), the
marcher's x (phase-tick count), and the map flag-wave/cow frames.  Root
cause: the shared phase counters are pure functions of engine-tick count
since boot, and the two engines spend a DIFFERENT number of ticks inside the
load stretch — the capture anchor cancels wall-time, not tick-phase.  Same
cosmetic family as quirk #216; state parity is unaffected (the Create commit
lands pop 435→353 / happiness 72→63 on BOTH engines, army flag in the same
cells).

> 📍 `src/engine_armory.c/.h`, `src/engine.c` (armory router arms /
> `armory_zone_action` / `armory_commit` / per-frame 0x17/0x0d arms),
> `src/engine_ingame_input.c` (0x0a/0x17/0x0d close arms),
> `tools/traces/armory_family.jsonl`, Ghidra `FUN_004358b0` / `FUN_004aabd8` /
> `FUN_004184c6` / `FUN_00418fc5` / `FUN_004190db` / `FUN_00435b4d`.

## 222 — the anim-phase counters are tick-count-since-boot, so port↔retail anim frames NEVER align across the load stretch without a pin — and the pin must run POST-clock-advance on both engines

The shared phase-cadence counters (`_DAT_004e5b10` last-base-timestamp +
`DAT_005aeb2c` base 0..3 + the derived `DAT_0058feb4/b0/b8` and
`DAT_005aea54/44/48/4c`) drive every ambient animation: torch flames
(0..0xc), the armory demo figures (0..0x17), flag waves, the troop-march
move/walk cadence (20 ms / 80 ms flags).  They are pure functions of the
ENGINE-TICK COUNT since boot — and the two engines spend a different number
of ticks inside boot/menu/load, so at anchor+offset captures every ambient
animation samples a different frame even when all pixel-producing state is
byte-identical.  The capture anchor cancels wall-time, not tick-phase.

**Fix = `--phase-pin`** (on by default in olv3 captures), the anim sibling of
`--rng-seed`: each pre-ingame tick, zero all cadence counters and re-base the
20 ms base-tick timestamp to the current virtual clock; stop at state 3.
Both engines then advance the cadence in lockstep relative to the ingame
flip.  Three traps found calibrating it against the armory troop march
(a 20 ms-granular +4 px/step animation — the most phase-sensitive probe yet):

1. **The pin must run AFTER the tick's virtual-clock advance.**  The agent
   pins inside the engine_tick entry hook (post `g_virtual_now_ms` bump); a
   port pin in the pre-tick loop body wrote the PRE-advance clock into
   `last_base`, leaving its 20 ms accumulator one 17 ms step ahead — the
   port's first 20 ms tick fired one tick early, a permanent one-move
   (+4 px) march lead.  The port pin now runs at the engine_tick head.
2. **Retail's per-frame UI refresh runs BEFORE its input dispatch**
   (FUN_004ba26e at tick line 99, FUN_0042ff10 at line 192) — a click-armed
   animation first steps on the NEXT tick's refresh.  The port dispatches
   input before its render seam, so a same-tick step must be skipped
   (engine_armory.c's arm-tick skip latch) or the animation leads by one
   move permanently.
3. **The 20 ms base tick is an ACCUMULATOR, not a last=now timer** — at the
   17 ms turbo step it fires ~6 of every 7 ticks (not every other), which is
   why a one-tick skew shows up as a CONSTANT pixel offset instead of noise.

With the pin + skip latch the armory_family olv3 lands 9 of 11 frames at
index-diff 0 — including a mid-march frame — with only ~10 px of
restore-boundary dust on the other two.  (The segtrace seat also now posts
its WM ops one frame late, buffered, matching the agent's tick-entry
PostMessage latency — measured inert here, kept for seat fidelity.)

> 📍 `src/engine.c` (`engine_phase_pin_reset` + the tick-head pin),
> `src/main.c` (`--phase-pin`, the buffered segtrace post),
> `tools/frida/openlords2-agent.js` (`pinPhaseCounters`),
> `tools/trace_studio_v3/olv3_capture.py` (default-on), Ghidra tick lines
> 99/192, `src/engine_phase.c` for the counter map.

## 223 — Send Supplies (0x18) doesn't transfer goods; it SPAWNS A CARAVAN through the AI's own dispatcher — and its staging array IS the AI's, so the two paths are byte-identical

Porting the Send Supplies screen (0x18, FUN_0041ad5d) surfaced four traps, all
now handled in `engine_sendsupplies.c` + `engine.c`:

1. **The "Dispatch shipment?" seal does NOT move stock between counties** —
   FUN_0043b04c → FUN_0043b145 → **FUN_004292af spawns a type-4 supply CARAVAN**
   (an army-movement entity, via FUN_0046e1b0) at a settle cell near the source,
   loaded with the staged goods and bound for the target; the grain/cattle are
   debited from the source county immediately and the caravan walks them over.
   The AI already ships caravans through the *same* FUN_004292af
   (`engine_army_ai_dispatch_caravan`), so the port's commit just calls it — no
   new executor, exactly as the trade grid reuses `engine_army_ai_execute_trade`.
   FUN_0043b145's tail is a 1×provision + 2×(workforce/grade/plan) recompute of
   the SOURCE county only (all already-ported economy helpers).

2. **The staging array is shared with the AI, and the good index skips sheep** —
   the have/to-send figures live in retail's `DAT_005678c0` (stride 0x18 = six
   int32 per player), which is the port's `g_caravan_order[player]` (was
   file-static in engine_army_ai.c; now exposed via
   `engine_army_ai_caravan_order`).  Layout: [0/1] grain have/to-send, [2/3]
   sheep (NO UI arm — "Sheep" is loc sub 4 but never rendered), [4/5] cattle
   have/to-send.  The good index (retail DAT_0059154c, the button's `param`) is
   **0 or 2** (never 1); have = ord[good*2], to-send = ord[good*2+1] — which is
   why the arrow handlers stride by `idx*8` bytes and land on grain/cattle only.
   The **player** seed (FUN_0043ae30 param 3) stages ord[0]/ord[4] (have) with
   ord[1]/ord[5]=0; the **AI** seed stages ord[1]/ord[5] (to-send) directly —
   different slots, same dispatcher, because the AI ships everything at once
   while the player stages via the up/down arrows.

3. **Both bottom seals carry the SAME action (0x43b04c); the param picks
   send-vs-cancel** — DAT_004dd538 rec0 (sprite 0x1d) = param 1 = Send, rec1
   (sprite 0x1f) = param 0 = Cancel.  Both are type-5 press-latch seals, so —
   like the Court→Rankings seal (quirk #219) — the transition must be driven off
   the action flag, not the dispatch's `hit` return (the latch fires from pass 1
   where the dispatch returns 0).  Send with target==source pops a "can't send to
   self" scroll (FUN_0040e6f2) — deferred; the port stays on the screen.  The 2
   "send-all / return-all" boxes over the goods rows are a SEPARATE FUN_0040e3ee
   hotspot table (DAT_004dc7e8 at origin 0x40,0x30 → FUN_0043b32a).

4. **The wrapped instruction line needs the glyph-WIDTH map, not the render
   map** — FUN_0040328e ("Click on a county to supply.") measures wrap points
   with a distinct metrics table.  Passing the render glyph map for both args
   wrapped "to" onto the wrong line (port broke before "to", retail after) — a
   746→212 px panel diff that vanished once `gfx_glyph_measure_map()` was passed
   as `char_glyph_table_metrics`.  Single-line draws (gfx_wrap_localized_line /
   FUN_00402d37) take no metrics arg, so only the multi-line wrap tripped.

**Verified byte-exact** (tools/traces/sendsupplies.jsonl, olv3 both engines: load
→ Send Supplies tab → cattle-up arrow → RMB back).  The panel — title, county
thumbnail (terrain raw-blit + political plot), "Supply from: Lancashire" /
"To: Lancashire", the Grain/Cattle have/to-send table, "Dispatch shipment?" — is
pixel-identical; the cattle-up arrow moves 10 head byte-exact (have 101→91,
to-send 0→10).  The only residual diff is OUTSIDE the panel: the right-edge map
anim and the **top HUD player-shield strip**, which retail repaints/animates and
the port renders static (same frozen-anim-counter family as the cow/castle/flags,
quirk #216/#222 — NOT a Send-Supplies pixel).

> 📍 `src/engine_sendsupplies.c/.h`, `src/engine.c` (strip tab entry, router 0x18
> arm, demand + per-frame arms, ctx builder, `ti_sendsupplies_target`),
> `src/engine_army_ai.c` (`engine_army_ai_caravan_order` +
> `engine_army_ai_dispatch_caravan` exposed), `src/engine_ingame_input.c` (0x18
> close case).  Retail FUN_0041ad5d / FUN_0041aea2 / FUN_0043ae30 / FUN_0043b04c
> / FUN_0043b145 / FUN_004292af / FUN_0043b1ca-b32a / FUN_0043b412.

## 224 — Noble Faces (0x0b) is the 5th county-action tab (param 5, NOT owner-gated), draws over the retained map with NO recomposite, and is byte-exact only with BOTH the demand render AND the per-frame button strip

Porting the Noble Faces / diplomacy partner-select screen (0x0b, FUN_00416cf3 +
the per-noble cell FUN_004171ee) — the diplomacy roster reached from the map's
county-action strip.  Five facts pinned it down, all handled in
`engine_noble_faces.c` + `engine.c`:

1. **It's rec4 of the SAME 0x4dc680 strip that opens Court/Armory/Send-Supplies**
   — the strip is 6 records (5 action tabs + End-Turn), each 0x18 B:
   {x0,y0,x1,y1 s16; action fn @0x08; type @0x0f; DAT_0059154c param @0x10}.  The
   tabs dispatch through FUN_0043ae30(DAT_0059154c): param 1 → Armory 0x17, 2 →
   Court 0x09, 3 → Send-Supplies 0x18, 4 → **Castle 0x1b** (still unported), 5 →
   **Diplomacy 0x0b**.  Diplomacy's box is [0x82,0]-[0xa1,0x1d] at origin
   (0x1de,0x1ae) → screen (608..639,430..459).  UNLIKE the armory/send-supplies
   tabs (owner-gated, post a 0x70 scroll on a foreign county), FUN_0043611b is a
   **bare screen set** — no gate; it just resets DAT_0057a0f8 and opens 0x0b.

2. **Draws OVER the retained map, NO recomposite** — FUN_0040f1a0 case 0x0b is a
   bare `FUN_00416cf3()`, and FUN_00416cf3 (unlike Send-Supplies' FUN_0041ad5d)
   does NOT call FUN_004050c0.  So the demand arm must NOT run the strat composite
   first (that would be the send-supplies pattern) — it locks the canvas and
   paints the panel straight over the last map frame, exactly like the Court.
   The entry still sets DAT_0057d340=2, but the "2" is just the request level;
   the render is a retained-frame overlay either way.

3. **Byte-exact needs BOTH tiers.**  The demand render (FUN_00416cf3) paints the
   panel bevel + seal + the face grid + the partner name + the action-label TEXT
   (grp 0x48, at x=0xe0).  A SEPARATE per-frame refresh (FUN_004ba26e case 0x0b)
   draws the action-button SPRITE strip (FUN_0040cfd2 over DAT_004dd940 — 6 recs,
   x=0x190, sprite 0x40, system sheet, type 5, count = the relationship-state
   button count DAT_00553ee8).  The labels (x=0xe0) and the sprites (x=0x190) are
   at different x — two halves of each row — so dropping the per-frame tier loses
   the right-hand button sprites.  Mirror the send-supplies split: the demand
   frame runs render THEN refresh; every later tick runs refresh only.

4. **The face + status icons use the GLOBAL sprite blob (faces.pl8); only the
   mood icon uses an explicit blob.**  FUN_00416cf3 loads faces.pl8 into the
   shared DAT_004eabec (100000 B) and binds it as the global blob, then each cell
   draws the personality portrait (FUN_0040a682, index personality*3-3, or the
   generic 0xc for a non-AI slot) and the war/treaty/alliance status icons
   (0xd/0xe/0xf) from that global blob; the mood icon alone comes from the
   explicit misc_cty sheet (FUN_0040a21a, sprite mood+0x55).  The per-face mood
   byte (+0x0a) is lazily normalized IN PLACE (0→1, clamp >5→5) — the render
   mutates the strategic block, so its ctx pointer is non-const.

5. **Relationship state DAT_00553f38 (save entry 142) + partner DAT_00553fbc
   (143) are save slots; the default partner is picked by the already-ported
   FUN_004a1e6c (player_first_active_nonlocal).**  The relation-state (0 no-lord /
   1 partner-is-my-lord / 2 other-lord / 3 allied) selects which action set +
   count is drawn; the per-noble relation METER (AI slots only) is a 61-segment
   vertical bar filled top-down to the +0x84 relation value, one colour by
   overall relation (red <-10, yellow <11, else green).

**Verified byte-exact** (tools/traces/diplomacy.jsonl, olv3 both engines: load →
Diplomacy tab → RMB back).  `diplomacy_open` + `diplomacy_settled` are BOTH
index-diff 0 / RGB-diff 0 vs retail (the constant palette diff is only the
non-rendered RGBA pad byte; R/G/B match exactly).  The port shows all four living
opponents (Knight/Baron/Bishop/Countess) with faces, mood/relation meters, shield
+ status icons, the centered partner name "The Knight", and the state-0 action
set (gift/compliment/insult/alliance) with the arrow sprites — pixel-identical to
retail.  The action-button clicks (→ the 0x1a diplomacy-action screens,
FUN_00436141…FUN_0043631e) are the widget-router's 0x0b arm — DEFERRED; they
render but don't navigate (a button click at x=0x190 misses the seal test at
x=0x1a8, so it neither acts nor closes).  Residual diff is only OUTSIDE the
panel: `map_before` catches retail's Game-Objectives hint scroll still open
(trace timing, quirk #222 tail), `map_resettled` is the load-flip cosmetic
(quirk #216).

> 📍 `src/engine_noble_faces.c/.h`, `src/engine.c` (diplomacy tab entry +
> hit-test wire, demand + per-frame arms, ctx builder, `--force-diplomacy`
> seat), `src/engine_ingame_input.c` (0x0b close case), `src/main.c` (seat
> flag).  Retail FUN_00416cf3 / FUN_004171ee / FUN_0043611b / FUN_0043ae30
> param 5 / FUN_004ba26e case 0x0b / FUN_0040cfd2 over DAT_004dd940 /
> FUN_004a1e6c.

## 225 — the diplomacy-action GIFT screen (0x1a mode 0) commits by ENQUEUING into the AI's own diplomacy inbox (not an instant effect) + transfers the gold immediately; and its panel text is byte-exact ONLY with the illuminated-drop-cap flag (DAT_0058fe2c) that colours every leading A..Z red

Porting the first diplomacy-action sub-screen — the GIFT panel (screen 0x1a, the
button-0 target of the s34 Noble Faces roster).  0x1a is a MODE dispatcher: the
roster's action buttons (DAT_004dd940 → FUN_00436141…FUN_0043631e) set
DAT_005651cc (0..6) and screen 0x1a, and the render dispatch FUN_0041789b
switches on it — mode 0 = gift (FUN_00417960), 1..4 = message screens
(FUN_00417aef, compliment/insult/…), 5..6 = ask-ally-to-attack (FUN_00417cef).
Only mode 0 is self-contained; the others need the runtime message buffer
(0x53f1e0, all-zero statically) / a target list (0x5530b0) and are DEFERRED.
Three facts pinned the gift port:

1. **The confirm is a PRODUCER, not an instant effect — the same shape as trade
   (quirk #217) and Send-Supplies (quirk #223).**  FUN_00436408's Yes seal calls
   FUN_004a2621(local, partner, mode, gift_amt, target), which appends
   {sender,+1 type,+2 target,+4 i32 value} to `partner`'s five-slot inbox at
   s_slot_0053f0f0 (save entry 146: partner*0x28 + slot*8) — the SAME grid the AI
   brain's step-1 processor drains (engine_army_ai.c).  So the player issues a
   diplomacy action exactly as the AI does; the AI reacts at end-turn.  A gift
   ALSO moves the gold immediately inside FUN_004a2621 (local +0x118 −= amt,
   partner += amt, clamped to the giver's cash), while the inbox keeps the
   original (unclamped) value.  Ported as `engine_diplomacy_queue_action`.  (The
   +0x89 compliment counter bump is mode-1 only; +0x90 is the pending scratch the
   AI resets.)  The Yes seal is a type-5 latch → drive the transition off the
   action flag, not `hit` (quirk #219); the No seal returns to the roster (0x0b),
   NOT the map.

2. **The illuminated drop-cap.**  FUN_00417960 brackets its whole text block with
   DAT_0058fe2c = 1 / 0 — the uppercase-highlight flag: every leading A..Z of the
   diplomacy strings renders in the red highlight colour (glyph index 1), not the
   caller's 0x3f.  The port already had the feature (gfx_text.c line 220,
   `gfx_text_set_uppercase_highlight`) but the gift render omitted the enable — an
   olv3 diff caught it (the panel's capitals rendered black; ~700 px, dead-on the
   drop-caps).  Restoring the bracket → panel interior 0 diff.  A trap because the
   OTHER panels (treasury/rankings, grp 0x46/0x23) do NOT set it, so their text is
   all one colour; the diplomacy strings (grp 0x48) are the first to use it.

3. **Unlike the roster, the gift screen RECOMPOSITES the map.**  The 0x0b→0x1a
   entry (FUN_00436141) calls FUN_004050c0, and the gift panel is smaller + lower
   than the roster's, so the fresh map is EXPOSED where the roster panel had
   covered it.  That exposes the load-flip map-anim divergence (quirk #216 — the
   port renders the top-left AI army-banner / cursor-highlight tiles at a
   different phase than retail) that the roster's retained-map overlay hid (0x0b
   was 0 diff there).  So the gift panel is byte-exact but the frame carries a
   ~3.7k-px map residual OUTSIDE the panel — the same family every
   map-recompositing screen shows (Send-Supplies 212 px, etc.), just more exposed.

**Verified byte-exact** (tools/traces/diplomacy_gift.jsonl, olv3 both engines:
load → Diplomacy tab → GIFT button → up-arrow → Yes seal).  The gift PANEL
interior is 0 diff on gift_open/gift_settled/gift_amount; "Gift of 10 Crowns"
after the arrow matches; and — the conclusive interaction proof — the HUD "990
Crowns" readout after confirm is byte-exact (0 diff in the whole HUD band), so the
10-crown transfer landed identically on both engines.  All residual diff is the
quirk-#216 map layer outside the panel.  Modes 1..6 (message / ally-attack) and
the portrait partner-select (FUN_004369bd) are DEFERRED.

> 📍 `src/engine_diplomacy_action.c/.h` (the 0x1a render + gift arrow + confirm),
> `src/engine_diplomacy.c/.h` (`engine_diplomacy_queue_action` = FUN_004a2621),
> `src/engine.c` (0x0b button router + 0x1a router/demand/per-frame arms, ctx
> builder), `src/engine_ingame_input.c` (0x1a close case).  Retail FUN_0041789b /
> FUN_00417960 / FUN_00436372 / FUN_00436408 / FUN_004a2621 / FUN_0040cfd2 over
> DAT_004dd9d0.

## 226 — retail's scratch globals are live state: the distance helpers' |dx|/|dy| (DAT_00591560 / DAT_0058fea0) and the shared cell cursor (DAT_00569570 / DAT_00569578) must be written where retail writes them

Found by the in-process shadow (tools/rhost, `rh_hooks_ai.c`), which compares EVERY
state dword after retail and after the port run on identical pre-state.  Two retail
"scratch register" pairs that the port had deliberately dropped (quirk #130's
out-param convention, and "only the distance is consumed, so it is inlined"):

* **FUN_00404f4c / FUN_00404ead** (chebyshev / manhattan) store |x1−x2| in
  DAT_00591560 and |y1−y2| in DAT_0058fea0 on every call.  The AI target scorers
  (FUN_004a0649 / FUN_004a03f2) and the cell finders (FUN_004a65a3 / FUN_004a6735 /
  FUN_004a689d) inlined the distance → `ai_select_target`, `ai_run_campaign`,
  `ai_run_garrison` diverged on exactly those two dwords.
* **DAT_00569570 / DAT_00569578** is ONE cursor pair shared by ~55 retail functions:
  the box scans (FUN_0046cfbd / FUN_0046d130) and FUN_0046d2a3 use it as their loop
  counters (a hit leaves the cell, a miss the scan's end position), the AI cell
  finders and FUN_004a689d leave their result in it, and FUN_004a65a3's caller
  (garrison step, FUN_0049f12f) reads it back.  The port had four private copies
  (box-scan locals + out-params, `s_ai_find_col/row`, `s_settle_col/row`, combat
  out-params).

Port: `engine_path_dist_chebyshev/_manhattan` + `g_path_dist_dx/dy` (engine_path.c)
and `g_cell_cursor_col/row` (engine_map_query.c), written exactly where retail
writes them; the AI finders and the deploy-cell finder use them (out-params kept).
Nothing in retail reads these values without re-writing them first, so no gameplay
changed — but the shadow cannot tell dead state from live state, and neither should
the port.  The other ~45 retail writers of the cursor pair (territory / render loops)
still count in locals; each needs the same treatment when it is shadow-verified
(done so far: the box scans, FUN_0046d2a3, FUN_0046e067 `mark_draw_box`, the AI and
deploy-cell finders).

The cost-field pathfinder (FUN_0046f700 fill / FUN_004701ac trace, AI brain step 0xb)
is the same story at a larger scale: retail keeps its source cell (DAT_00504020 /
DAT_00504024 / DAT_004f4070), its ring-queue head/tail (DAT_00500c20 / DAT_004f2768)
and its trace cursor (DAT_00507230 / DAT_00507234) in globals; `engine_path_field`
now counts in `g_pf_*` the same way, and its fields / queues / waypoint scratch are
bindable (`engine_path_field_bind_storage`) so the verifier points them at retail's
0x500c30 / 0x4f0380 / 0x4f9820 / 0x503020 / 0x53f070 / 0x553550.

Same for **DAT_00553f8c**: the combat occupied-cell register (`g_combat_active_terr`)
that the AI's step-0xb nearest-army scanners (FUN_00467f2e family) use as their loop
variable and result.  The AI kept a private `s_nearest_occ`; it now counts in the
shared global (the shadow of `ai_run_per_tick` / `ai_run_movement` showed retail
leaving 5/6 there in 30 of 33 scenarios).

## 227 — the spawned occupant's roster slot (FUN_004a9f72) is keyed by the OWNER, not the realm

FUN_004a9a9a (`engine_combat_spawn_unit(owner, army, …)`) stamps
`occupant +0x14f = FUN_004a9f72(a1)` — `a1` is the owner player, whose 24-byte
roster-load array (strategic +0x2d) gets the least-loaded entry bumped by 2.  The
port passed `army` (the realm key), so it bumped the load of player #realm (when
realm ≤ 5) or none at all, and stamped a slot index from the wrong array.  The
rhost shadow of AI brain step 7 (`ai_run_garrison`, FUN_0049f93d → FUN_0049f12f →
muster → spawn) showed it as player 2's +0x2d (retail) vs player 4's (port) at f1752
of endturn-2.  Fixed: `spawn_pick_roster_slot(owner)`.

> 📍 `src/engine_combat.c` (spawn_unit, find_deploy_cell), `src/engine_path.c/.h`,
> `src/engine_map_query.c/.h`, `src/engine_army_ai.c` (target scorers, cell finders),
> `src/engine_territory.c` (mark_draw_box), `src/engine_path_field.c/.h`;
> evidence: `tools/rhost/rh_hooks_ai.c` rows ai_run_garrison / ai_select_target /
> ai_run_campaign / combat_spawn_unit / ai_run_movement / ai_run_per_tick.

## 228 — top-band unrest never rebelled in the port: the escalation pass reached FUN_004ac185 only through a seam nothing bound

FUN_0044aa41 (`engine_realm_escalate_unrest`) raises a realm's unrest band and, at
the top band (>3), calls FUN_004ac185 — the rebellion spawn (a type-6 rebel occupant
with 30% of the realm's capacity; the realm's economy is demolished).  The port
reached it through `engine_realm_bind_rebellion_fn`, written when FUN_004ac185 was
unported; FUN_004ac185 was later ported as `engine_combat_spawn_rebellion(region,
evq, mp_active)` (a different signature) and the seam was never bound — so in the
port a starving realm sat at band 4 forever.  rhost replace (`realm_escalate_unrest`)
failed 24 of 33 corpus scenarios (the bot games, where realms starve); the shadow
showed retail allocating a record slot (DAT_0052afe0 9 vs 8) and stamping the rebel
occupant.  Fixed: the pass calls `engine_combat_spawn_rebellion` directly and takes
`mp_active` (forwarded to the demolish path); the seam is gone.

> 📍 `src/engine_realm.c/.h` (escalate_unrest), `src/engine_turn.c` (caller);
> evidence: `tools/rhost/rh_hooks_ai.c` row realm_escalate_unrest.

## 229 — the army-move executor reads its waypoint from stack garbage on a disarmed pass: an army standing on the cell the PREVIOUS army just stepped into "arrives" and stops

FUN_00465d28 (`engine_army_move_run`, the per-tick movement-step executor) keeps the
current waypoint in two locals, `[ebp-0xc]` col / `[ebp-0x10]` row, and assigns them
only on a pass where the step-eval is armed (record +0x14b bit0).  On a disarmed pass
it still computes `FUN_0046f434(col, row, wp_col, wp_row)` from whatever those stack
slots hold, and a result of 8 ("standing on the waypoint") ends the walk: +0x14c = 0,
+0x150 = 1 — the army stops.  The slots hold the residue of the previous call at the
same depth, in practice the previous `move_run` — usually for ANOTHER army — so an army
standing on the cell that army just entered stops for the tick (rhost `--trap 466116`,
bot-s15-t20 f16450: army 2 at (0x25,0x23) reads (0x25,0x23) left by the previous army's
step, arrives; the port's zero-initialized locals let it step east).

The port had zero-initialized them ("a real stack is never at (0,0)").  Now the
waypoint is explicit state, `g_army_move_wp_col / _row`, kept across calls; the hybrid
host loads it from retail's stack residue before each call and stores the port's final
values back where retail's frame would have left them (rh_port.h STACK RESIDUE).  The
shadow compares them with the residue retail's run leaves: 135k calls, 0 divergences.

Found because the shadow itself perturbed it: a C wrapper frame at the call site
overwrites the residue retail reads, so a shadowed run took different decisions than a
pure one with 0 divergences reported (14 of 33 scenarios ended off the blessed state).
The wrappers now run retail AT the caller's stack position and the machinery 256 KB
below (rh_port.h WRAPPERS).

> 📍 `src/engine_army_move.c/.h` (engine_army_move_run, g_army_move_wp_col/_row),
> `src/engine_combat_globals.c`; evidence: `tools/rhost/rh_hooks_combat.c` row move_run.

## 230 — an overrun of an idle hostile stack hits the VICTIM's relation toward the mover, not the other way round

FUN_004658c1 (the step evaluator's occupied-cell resolver), target in state 4 with a
hostile owner the mover may engage: `FUN_004a1ee1(owner(target), owner(mover), 5)` —
the same (victim, aggressor) order as the battle aftermath's `(loser, winner, 0x14)`.
The port passed (mover, target): the overrunning player's opinion of its victim dropped
instead of the victim's opinion of the overrunner.  Fixed (read from the disassembly at
0x465c63..0x465c97; the corpus never overruns an idle stack, so no shadow call yet).
Same pass: FUN_00468ae2 (special-tile plunder) takes the aggressor from the army-state
cursor DAT_005533e8, not its argument — identical from its only caller, now exact.

> 📍 `src/engine_army_move.c` (engine_army_move_eval_occupied, engine_army_move_special_tile).

## 231 — the battle-setup outputs are SAVE slots, and the battle screens are the engine's own screen byte

FUN_004a6a30 (battle sizing) writes the local player's combat role DAT_005533d0, the
two battle sizes DAT_00568934 / 38 and owners DAT_005681ec / d8 — save-schema entries
65 and 94..97; FUN_00422c12 writes the muster scratch DAT_00568420 (entry 193, 56 B:
attacker 7 × i32, defender at +0x1c); the quick-anim path writes the combatants to
DAT_00553c70 / DAT_00553d20 (entries 180 / 181).  The port wrote private copies
(`g_combat_size_a`, `g_combat_muster_a`, ...), so a save taken after a battle lost them
and anything reading the slots saw stale values.  The dispatch / contest / siege paths
also set the screen byte DAT_004eac50 (0x12 battlefield, 0x13 quick-anim, 0x1d siege),
the demand-redraw mode DAT_0057d340 and (aftermath) DAT_00553fc8 — engine.c's screen,
redraw mode and engine_turn.c's g_sim_turn_progress — through file-static mirrors that
nothing read, so the executor's "battle screen up → abort the walk" check never fired.
The two battle clocks are distinct: the battlefield stamps DAT_0057a0d0, the quick-anim
DAT_005689a0.  Fixed: the slots / engine setters / g_sim_turn_progress are written
directly; the remaining battle / movement runtime globals live in
`src/engine_combat_globals.c` (rhost binds them to retail's VAs).

> 📍 `src/engine_combat.c/.h`, `src/engine_combat_globals.c`, `src/engine_army_move.c/.h`;
> evidence: `tools/rhost/rh_hooks_combat.c` rows combat_size_battle / combat_contest_region /
> combat_dispatch / combat_aftermath.

## 232 — the army-state handlers call the move executor from two stack depths: an idle stack reads the previous marcher's outcome code / waypoint column as its waypoint

FUN_004650b0 (the per-tick army-state driver) dispatches through PTR_FUN_004d6a50 to
four handlers that all end in the movement executor FUN_00465d28 — whose waypoint
locals `[ebp-0xc]` / `[ebp-0x10]` are stack residue on a disarmed pass (quirk #229).
The state-1/2 handlers (FUN_0046521f / FUN_00465486) reserve one local (`sub esp,4`),
the state-3/4 handlers (FUN_00465622 / FUN_00465761) none, so the executor's frame sits
4 bytes higher under an idle handler.  Relative to the driver's entry esp, the
executor's `[ebp-4]` direction / `[ebp-8]` outcome code / `[ebp-0xc]` wp col /
`[ebp-0x10]` wp row are at −0x34/−0x38/−0x3c/−0x40 for states 1/2 and
−0x30/−0x34/−0x38/−0x3c for states 3/4: an idle stack after a marching one takes the
marcher's outcome code as its waypoint column and the marcher's waypoint column as
its row (and vice versa one slot down).  `[ebp-4]` is written twice — the step
direction AND, on the budget-spent / turn-commit exit, the direction toward the
final destination.

Port: the executor keeps all four locals as explicit state (`g_army_move_dir /
_code / _wp_col / _wp_row`, assigned where retail assigns them); the driver keeps the
residue window `g_army_state_res[5]` (driver esp −0x30..−0x40) and moves the
executor's locals through it at the handler's depth.  Not modelled: what the other
handler callees' frames (enter / settle region, territory income, unit remove) leave
in those slots.  Evidence (rhost, wine, 33 scenarios): `state_run` shadow 241,671
calls, 0 divergences, replace corpus --check PASS; with the idle handlers moved to
the deep slots the shadow diverges in 5 scenarios (bot-s12-t20 f16358: record 6
stops — +0x14c 0, facing not bumped — where retail keeps stalling); `move_run`
shadow now compares all four residue slots: 135,034 calls, 0 divergences (it caught
the second `[ebp-4]` write).

> 📍 `src/engine_army_state.c/.h` (run_move, g_army_state_res), `src/engine_army_move.c/.h`
> (g_army_move_dir / _code), `src/engine_combat_globals.c`; evidence:
> `tools/rhost/rh_hooks_state.c` row state_run, `tools/rhost/rh_hooks_combat.c` row move_run.

## 233 — the raid planner's target is the path-trace goal +0x16/+0x17, not +0x116/+0x117

FUN_004ac5ba (raid target) writes the target realm's capital to `byte_52F0C6 /
byte_52F0C7` — record +0x16/+0x17, the path-trace goal the march planners also use —
and FUN_004ac499 traces to the same pair.  The port used +0x116/+0x117 for both
(self-consistent, so the route was right, but it left +0x16/+0x17 stale and
scribbled two bytes inside the record's waypoint area).  Fixed (read from the
disassembly, 0x4ac6bd / 0x4ac6e1); rhost shadow of `state_raid_assign_target` after
the fix: 16 calls / 2 scenarios, 0 divergences.

> 📍 `src/engine_army_state.c` (A_DEST_COL / A_DEST_ROW); `tests/test_engine_army_state.c`.

## 234 — the coalition pass builds its group table in retail's .data (0x568240, count DAT_00553ef4): a private copy is a divergence

FUN_0044ae3c (the season pass's coalition step) = FUN_0044b042 (build the
adjacency-connected groups of each owner's counties: 17 × 0x1c-byte records at
0x568240 — strength, owner key, 20 member ids — and the active-group count
DAT_00553ef4) + FUN_0044ae51 (demolish all but each player's strongest group).
The port kept the table and the count as file-statics in engine_realm.c, so retail's
copy went stale: the rhost shadow of `realm_coalition_pass` diverged on every call
(523 / 32 scenarios: 0x553ef4 r2/p0, 0x568240.. strengths and keys).  Only the
coalition functions read the table and it is rebuilt every call, so no gameplay
changed — but like #226 it is retail state the shadow (and a save-compatible port)
cannot tell from live state.  Now `g_coalition_tbl` / `g_coalition_active_count` in
`src/engine_realm_globals.c` (rhost aliases them to 0x568240 / 0x553ef4): 512 calls /
31 scenarios, 0 divergences.

> 📍 `src/engine_realm.c/.h`, `src/engine_realm_globals.c`, `tests/Makefile`; evidence:
> `tools/rhost/rh_hooks_events.c` row realm_coalition_pass.

## 235 — two more #226 scratch writers: the capital-defender box scan counts in the cell cursor, the AI's nearest-army scanners go through the chebyshev helper

Found by the rhost shadow on the bot v2 games (bot2-s61-t28 / bot2-s64-t28, the first corpus games that march on
OWNED counties):

* **FUN_0046d42c** (`engine_combat_find_capital_defender`) scans the 4×4 box at the capital with the shared cell
  cursor DAT_00569570 (col) / DAT_00569578 (row) as its loop counters and leaves them at (cap_col+2, cap_row+2);
  the port counted in locals (the old #130 reasoning).  `combat_capital_defender` diverged on both dwords (2 calls).
* **FUN_004673b8 / FUN_00467532** (the AI order handlers' nearest enemy / engageable army scanners) call the
  chebyshev helper FUN_00404f4c, which leaves |dx| / |dy| in DAT_00591560 / DAT_0058fea0; the port inlined the
  distance.  `ai_run_per_tick` / `ai_run_movement` diverged on those dwords at f21264 / f21968 of bot2-s61-t28
  (order handlers advance / lord-move / intercept → FUN_004a57ac → FUN_004a5667).

Fixed: the box scan counts in `g_cell_cursor_col/row`, the scanners call `engine_path_dist_chebyshev`.  No gameplay
change (nothing reads these without rewriting them first).  `rhost.py verify --only bot2` (3 scenarios): 0
divergences, hybrid PASS.  Still inlined / unhooked: the battlefield's distance callers (FUN_0047a814, FUN_00489a62,
FUN_0048b5fc … FUN_00497437, FUN_004843bc, FUN_00484bf9, FUN_0048ef45 …) and the castle-border placer FUN_0046902a.

> 📍 `src/engine_combat.c` (engine_combat_find_capital_defender), `src/engine_army_ai.c`
> (nearest_enemy_army / nearest_engageable_army); evidence: rows combat_capital_defender, ai_run_movement.

## 236 — the economy screens' scratch is retail .data too: the caravan staging order and the trade grid's figures

Batch 4 (`docs/findings/hybrid-batch4.md`), like #234: retail keeps the per-player goods-shipment order in
DAT_005678c0 (stride 0x18; the AI's reinforce pass FUN_0049f431 stages it, the caravan executor FUN_004292af and the
Send Supplies screen read it) and the trade grid's figures in nine scattered globals (DAT_0053e9e4 held, DAT_0057c8c0
offer, DAT_0055cd70 max, DAT_0057a0fc affordable, DAT_00554170 net, DAT_00553f64 total, DAT_0053e8c0 ale delta,
DAT_00568948 category, DAT_0057c994 error).  The port kept them as `engine_army_ai.c` / `engine_trade.c` statics —
a hooked FUN_004292af would have shipped the port's private (empty) order.  Now `src/engine_economy_globals.c`
(rhost aliases them), and the trade model is `src/engine_trade_model.c` (linkable without the gfx / io closure).
The diplomacy-action mode DAT_005651cc / latch DAT_0052afa4 and the Send Supplies target DAT_00553c78 were statics
too; they moved with them (plus the castle tab's tier DAT_0056d898, which the port had not modelled).

Read from the disassembly while moving them (the corpus reaches none of these edges):

* FUN_00428daf has no case for goods 3 / 5 (sheep, wool): DAT_0053e9e4 keeps its previous value and the negate flips
  it; the port zeroed it.
* "buy all" FUN_004355db / "sell all" FUN_00435541 compute the total with no sign branch (afford × max / −(offer ×
  held)); the port's shared tail picked the price by the net's sign (differs only for a negative treasury / stock).
* all four arrows play `s068_01.wav` (up / buy all) or `s068_02.wav` (down / sell all) after DAT_005533f0 = 1 when
  the net crosses 0 → positive; the port was silent (now `engine_trade_bind_wav_fn`, rhost binds FUN_00427990).
* FUN_004a2621 debits the giver, then credits the partner, in memory — a self-gift nets 0; the port read both
  treasuries first (+value).  Unreachable from the UI (the partner is never the local player).

> 📍 `src/engine_economy_globals.c`, `src/engine_trade_model.c`, `src/engine_army_ai.c` (g_caravan_order),
> `src/engine_diplomacy.c` (engine_diplomacy_queue_action); evidence: `tools/rhost/rh_hooks_economy.c`.

## 237 — the cell sweeps' "write-only" counter DAT_0053e974 is state: the sweeps leave it at 0x1000 / 8385

Found by the shadow on bot v3's long custom games (`bot3cen-s210-t44`, frame 63100):
`combat_contest_region` diverged on one dword, `0053e974 r00001000/p000020c1`. Retail's cell sweeps reset
DAT_0053e974 and bump it once per visited cell — FUN_0046df51 (clear render flags) and FUN_0046dfd5 (highlight
key cells) over the 64×64 grid (→ 0x1000), FUN_0046dbf5 / FUN_0046dd34 / FUN_0046797d over the 129×65 tile grid
(→ 8385; FUN_0046797d also indexes with it), and the strategic-map loader FUN_00467770. Nothing reads it across
calls, so the port had modelled it as a local (#130's out-param habit, retired by #226) — but it is .data, the
shadow compares it, and a hybrid run's state hash includes it.

Not yet written: the feature picker's scratch DAT_0057c8a4 and the variant register DAT_00568200 (FUN_0046dbf5 /
FUN_0046dd34) — new-game map setup only, which no hook reaches; write them when those functions are hooked.

> 📍 `src/engine_territory_globals.c` (`g_cell_sweep_counter`, aliased at 0x53e974 by `tools/rhost/rh_hooks_ai.c`),
> `src/engine_territory.c`, `src/engine_map_init.c`.

## 238 — the march executor's waypoint residue is the AI step's frame imprint: saved registers, not the last waypoint

Extends #229 / #232.  The army-state driver FUN_004650b0 has one caller, the frame driver FUN_004b99c0 (0x4b9c0f),
which calls the AI step FUN_0049a010 at the same esp right before it.  0x49a010's callees' frames rewrite the
driver's residue window (driver e0 −0x30..−0x40) on nearly every pass: measured in pure retail (bot-s16-t20, 13,965
passes, an observer row that runs retail under `rh_call_at` and logs the 16 dwords below e0), the three waypoint
slots −0x38 / −0x3c / −0x40 hold large values in 13,947 passes — the frame loop's ebx / esi / edi saved by
0x49a010's callees' prologues, their return addresses, frame pointers; the small exceptions: the siege scan
0x4a84ba → 0x4a8507 leaves 0x96 at −0x38 (18 passes).  So an executor on a disarmed pass mostly walks toward a
"waypoint" of saved registers (direction 3, south-east: `engine_path_direction_8way` compares only, any large
positive value behaves alike), and a waypoint survives only within a driver pass (#232).  The values themselves are
host-dependent (rhost's frame loop registers; real Windows has others): only their class (exact small / large) is
behaviour.

The port's frames under the port's frame driver leave other values there: bot-s16-t20 f20034 army 5 read waypoint
row 0x27 (army 3's) where retail read 0x1400000 — facing 5 vs 4 (+0x1b), +0x149 0x0b vs 0x09, then at f21114 a click
opened screen 0x10 in retail, not in the port (the standalone and the full hybrid both diverged).

Port: `engine_army_state_res_ai_step` sets `g_army_state_res` to the imprint (retail's dominant values); the frame
driver `rl_4b99c0` calls it after `rl_49a010` and then the driver port directly (`engine_army_state_run`, was an
`engine_retail_call` seam); the `rl_49a010` row writes the imprint below its e0 in replace mode (rh_res_set).  The
driver's march handler also leaves FUN_004ad6c0's locals after a region change (as `rl_46521f` did).  rhost note: a
retail call through `rh_call_at` (shadow, the observer) runs with the hook machinery's ebx / esi / edi, so saved-register
residue differs from a pure run's (0x58fbf7b4 … instead of 0x13ff84c …); the class is the same here.

Evidence (wine): standalone 64/64 equal to pure retail (was 63); full hybrid (every row replaced) bot-s16-t20 PASS;
`corpus --check --hook replace:state_run,replace:rl_49a010` 64/64 PASS.

> 📍 `src/engine_army_state.c/.h` (engine_army_state_res_ai_step, handler_march), `src/rl_host.c` (rl_4b99c0),
> `tools/rhost/rh_hooks_rl_turn.c` (row rl_49a010), `tools/rhost/rh_hooks_state.c` (row state_run).

## 239 — the battlefield siege orders' sprite-frame residue is a frame pointer: its value comes from the host's stack

The class 7..10 sequencers (0x488793 / 0x488436 / 0x48895e) store an unset local as a unit's sprite frame (+0x10) for a
unit of another class; after a 1-argument call that local is the order handler's saved ebp (engine_bf_action.c
§the siege orders' stack residue).  That ebp is a fixed depth below the frame driver 0x4b99c0's entry esp, and in a
pure rhost run that esp is 0x58fffdcc: rhost's pinned main stack, retail's start → WinMain → 0x40e8bb → 0x40e890
chain, and rhost's frame_wrap frame (it calls 0x4b99c0).  So the sprite frame is the low byte of a host stack address
(0x80 under rhost; real Windows gives another).  Under the port's WinMain (the full hybrid, the standalone) the real
esp is the port's: field-orders f9601 units 15 / 16 got 0xa0 where retail has 0x80 (0x555de0 / 0x555f90, the only
two differing dwords at the end).

Port: rl_40e890 marks its frame (`rl_frame_port_guard`); rhost's rl_4b99c0 row then gives the residue models retail's
e0 (`RH_RETAIL_E0_4B99C0`), else the real one, logging once if a retail-guard run ever sees another (rhost's
frame_wrap or stack changed).  Evidence (wine): full hybrid 64/64 PASS (field-orders failed before); Windows:
field-orders / bot-s16-t20 hybrid PASS.

> 📍 `src/rl_ui4.c` (rl_40e890), `src/rl_main.c/.h` (rl_frame_port_guard), `tools/rhost/rh_hooks_rl_host.c`
> (e0_4b99c0_model), `src/engine_bf_unit.c` / `src/engine_bf_action.c` (the model).

**Update 2026-11-14:** the constant (`RH_RETAIL_E0_4B99C0` = 0x58fffdcc) went stale: frame_wrap's frame grew by 8
(rhost edits), pure retail entered 0x4b99c0 at 0x58fffdd4 (the warning fired), and field-orders / ai-sally failed the
hybrid and the standalone again — a dead unit's sprite frame byte (0x488793 run on a slot 0x48f1dd(0) just cleared)
0x80 / 0x8c vs retail's 0x88. The model now takes retail's chain constant — esp before retail's call at 0x40e896,
`RH_RETAIL_CFA_FW` = 0x58fffe18 (retail code only) — minus frame_wrap's own frame, measured every frame
(`rh_fw_frame`: `__builtin_dwarf_cfa()` − esp at the frame call). The port's WinMain / 0x40e8bb / 0x40e890 are now
retail's frames instruction for instruction (naked asm) and the WinMain row enters with the stack untouched, so a
hybrid whose battlefield is retail stores the same stack pointers too. Evidence (wine): full hybrid 78/78 PASS,
standalone 77/78 = pure retail (hotkeys: gap 0x43f7e3), snap-check PASS.

## 240 — the MP message panel's liege check reads a render_tile return address: never 3 on the map

FUN_00475e07 (popup panel 12, the MP messages between humans) starts with `cmp [ebp-8], 3` → if the local realm has a
liege (+0x81) the popup is dismissed (0x476768) — before any store to that local.  The call chain is fixed:
engine_tick 0x4b99c0 → the popup step 0x472e46 (0x4b9ef4) → 0x47309e → 0x475e07 (0x474c5f), so the slot is engine_tick's
body esp − 92 (rhost: 0x58fffd60; entry esp 0x58fffd6c).  Measured in pure retail (mp-trio-p3 f56019 / f56020,
`--watch-write 58fffd60`): its last writer before the read is the map scene 0x4b9ce4 → 0x4050c0 → render_frame 0x405487
→ row_phase0 / row_phase1 (0x4059af), whose `call engine_map_render_tile` pushes its return address on that very slot;
the frame's last one is 0x405ac5 (phase1's last tile, 0x405ac0).  So on the map screens the check sees a code address and
the liege dismiss never fires.  After the read 0x475e07 stores its kind there (0x475e9a), then 0x47309e's common tail
pushes DAT_00553064 (the box y, 0x475b23) as the dirty mark's 4th argument onto it.

The port had modelled the read as "the previous call's kind": under the standalone the slot is the port's native stack
and kept the 3 of an earlier kind-3 popup → mp-trio-p3 f56021 dismissed an open type-12 popup (0x53e910 0x100 → 0, the
cue buffer released, full redraw) that retail keeps.  (The 2026-11-14 note in multiplayer-rhost.md watched 0x58fffd98,
which is 0x47309e's own local, not this slot.)

Port: `rl_475e07_res_in` = the port's model of the slot: rl_4b99c0 sets 0x405ac5 after the map scene, rl_47309e's tail
sets the box y; rl_47309e's case 12 calls the port `rl_475e07()` with it (was an `engine_retail_call` seam).  The row's
adapter (entered from retail code) still reads the real dword (rh_res_get(12)).  Not modelled: screens without the map
scene (the slot then holds the previous popup's box y, never 3 either), render_frame's tail-row render_tile local
([ebp-0x14] of a call from render_frame's own depth lands on the slot).  Evidence (wine): standalone `--only '^mp-'`
24/25 = pure retail (mp-trio-p3 fixed; was 23/25).

> 📍 `src/rl_mpnet.c/.h` (rl_475e07, rl_475e07_res_in), `src/rl_turn.c` (rl_47309e), `src/rl_host.c` (rl_4b99c0).
