# Engine quirks — a fun tour of *Lords of the Realm II*

A curated list of weird, charming, and occasionally inexplicable
things found in Impressions's engine while reverse-engineering it.
These aren't bugs (well, mostly) and they aren't load-bearing for
anyone using the game — they're just the kind of texture that makes
RE work feel less like archaeology and more like reading someone's
diary.

Each entry: what we found, where we found it, and (when it's
interesting) why.  Citations to engine functions are `FUN_XXXXXX`
Ghidra addresses from `docs/decompiled/by-address/` (older entries) or
IDA VAs (`0x4b29be`, `sub_4B29BE`: `/mnt/ephemeral/openlords2/decompiled-ida/`).

**Audit 2026-11-18:** every entry (#1..#241) re-checked against the IDA
pseudocode + disassembly: 91 confirmed as written, 141 corrected (facts,
roles, sizes, stale port pointers — each ends with a *Re-checked
2026-11-18* line saying what changed), 9 withdrawn (#3, #16, #34, #80,
#86, #98, #129, #143, #147 — Ghidra artefacts or misreads; the stub says
what retail actually does). Numbers are stable: a withdrawn entry keeps
its number, nothing is renumbered.

---

## 1. The windowed-mode bring-up path has been dead code since ~2001

`sub_42E5C0` (DDraw create + cooperative level + display mode) has
two arms gated on `DAT_0053f228` — the "fullscreen flag" (the
defaults `0x4ae310` set it to 1).  Before either arm runs, the flag is
auto-set from the desktop depth:

```c
hdc = GetDC(NULL);
caps = GetDeviceCaps(hdc, RASTERCAPS);
DAT_0053f22c = GetDeviceCaps(hdc, BITSPIXEL) * GetDeviceCaps(hdc, PLANES);
if (caps == -1) DAT_0053f22c = 16;
ReleaseDC(NULL, hdc);
if (DAT_0053f228 == 0) DAT_0053f228 = (DAT_0053f22c != 8);
```

i.e. "if windowed was asked for and the desktop isn't 8bpp, promote
to fullscreen anyway."  Lords II shipped in 1996 when 8bpp desktops
were normal; by the Windows 2000 / XP transition (~2001) virtually
every desktop in the wild was 16bpp or deeper.  So the windowed
branch — the `SetWindowLongA` style transform, the
`AdjustWindowRectEx` centring, `SetCooperativeLevel(hwnd, 8)`
(DDSCL_NORMAL; fullscreen passes 19 = EXCLUSIVE | FULLSCREEN |
ALLOWREBOOT), the windowed clipper allocation in `0x42ec91` — has
been effectively unreachable for ~25 years on retail.

The in-game toggle agrees: `sub_434B10` (the windowed/fullscreen
option) refuses to leave fullscreen on a non-8bpp desktop — with
`DAT_0053f228 == 1 && DAT_0053f22c != 8` it only posts event 260 and
returns; otherwise it flips the flag, tears down (`0x4b28cd(1)`) and
re-inits (`0x4b2922`).  `engine_tick` (`0x4b99c0`) posts event 298
once on the battlefield under the same condition.

The surface-restore escalator in the WndProc (`0x4b29be`,
`WM_ACTIVATEAPP`) is NOT windowed-specific: with the graphics up
(`DAT_004e65d0`) and the app active it calls `0x4b2730` (Restore on
the primary `DAT_004eabd0` and the back surface `DAT_004eafac`) and,
on failure, `0x4b2922` (full teardown `0x4b28cd(0)` + re-init
`0x4b284e`, palette reload).  That is the live fullscreen alt-tab
path.  Under windowed coop it would be reached far less often (no
exclusive-mode eviction), though a display-mode change can still
lose surfaces there.

We ported both arms anyway — the standalone build's windowed
branch is OpenLords2's default (`--fullscreen` opts into retail's
popup).  rhost's virtual GDI reports 32 bpp (`tools/rhost/rh_win.c`
`my_GetDeviceCaps`), so retail under rhost takes the fullscreen path,
as every GOG user's does.

> 📍 `src/rl_ui4.c` (`rl_42e5c0`, `rl_4b2730`), `src/rl_host.c`
> (`rl_4b29be` WM_ACTIVATEAPP arm, `rl_4b2922`), `src/rl_ui3.c`
> (`rl_434b10`), standalone `src/gfx.c` (`gfx_init`, `gfx_restore`) /
> `src/main.c`, `docs/findings/ddraw-dsound-bringup.md`
> §"FUN_0042e5c0", IDA `sub_42E5C0` / `0x4b2730` / `0x4b2922` /
> `0x4b29be` / `sub_434B10`.

*Re-checked 2026-11-18 against the IDA export: depth = BITSPIXEL × PLANES (RASTERCAPS −1 → 16); added the 0x434b10 toggle; the restore escalator is the live fullscreen path, not windowed-only; 📍 now names the rl_ ports.*

---

## 2. The .ini parser reads past its own buffer (and gets away with it)

`sub_40ECC4` (hardware defaults + `l2.ini` / `sierra.ini` parser)
sets the defaults (CD letter `'D'`, CDROM speed 2, 8000 KB, CPU
486 / 66 MHz, VideoSpeed 0, 13 sound channels), mallocs a fixed
20000-byte scratch buffer and reads `l2.ini` into it, then the
`sierra.ini` in the `WindowsDir=` it names, else `sierra.ini` in the
current directory.  Every key lookup is the same two-step:

```c
off = strutil_find_substr(buf, "CDROM", 20000);  // 0x401000
off += strutil_find_char(buf + off, '=', 20000); // 0x401085: index+1 of '=', 0 if none
if (off > 0) value = atoi(buf + off);
```

The bound passed to `find_char` is **the full 20000**, not the
remaining bytes after `off`, so a key with no `=` after it walks up
to `off` bytes past the end of the malloc.  `find_substr` itself
compares `strlen(key)` bytes at every offset `< 20000`, so it can
read `strlen(key) - 1` bytes past the end too, and the CPU= line's
dash scan (`for (i = 0; buf[off + i] != '-' && i < 20; i++)`) tests
the byte before the bound.  On a well-formed file the `=` follows
the key immediately and nothing leaves the file's bytes.

The sharper quirk is in `find_substr`: it returns `0` for both "not
found" *and* "found at offset 0".  A **missing key** therefore
gives `off = 0`, and `find_char` then finds the first `=` anywhere
in the file — the key takes the value of whatever line holds the
first `=` (`atoi` of it, or its first byte for `CDRomDrives`).
Retail only gets the defaults for a missing key when the file has
no `=` at all.

On the user's GOG install none of this runs: `L2.INI` is
`[Config] CDDrive=. WindowsDir=.`, so the parser tries `.\sierra.ini`
and then `sierra.ini`, neither exists, it logs
`Err:Could not read sierra.ini` and keeps the defaults.  (Same for
rhost's pristine game copies.)

`rl_40ecc4` (`src/rl_ui4.c`) ports retail's semantics exactly
(route b: `rl_401000` / `rl_401085` with the full 0x4e20 bound).  The
older standalone `src/config.c` still deviates on purpose — its
`seek_past_equals` clamps `find_char` to the remaining bytes and
disambiguates `find_substr`'s 0 with a `memcmp` — and is not what
rhost runs.

> 📍 `src/rl_ui4.c` (`rl_40ecc4`, `rl_401000`, `rl_401085`),
> standalone `src/config.c` (`seek_past_equals`), `tests/test_config.c`,
> `docs/findings/winmain-pre-init.md` §"String-helper family",
> IDA `sub_40ECC4` / `0x401000` / `0x401085`.

*Re-checked 2026-11-18 against the IDA export: a missing key reads the value after the file's first '=' (not "harmless garbage"); GOG has no sierra.ini so the parse never runs; rl_40ecc4 is the retail-semantics port, config.c's deviation is standalone-only.*

---

## 3. (withdrawn) The sim-rate LUT is read ~500 bytes past its allocated end

Claimed `0x4bb978` indexes an 11-row (0x6e0 B) duty table at `DAT_004e3260` with rows up to 14, reading 480 B past
its end.  Wrong: the table is **15 rows × 0xa0 = 0x960 B** (`0x4e3260..0x4e3bc0`, next object `dword_4E3BC0`); rows
0..14 hold 160 / 144 / … / 16 / 8 / 4 / 2 / 1 / 0 ones, a clean decay.  The hint clamps at 140 → row ≤ 14: every read
is in bounds.  SP's hint is 1 → row 0 only.  See #195's correction and `src/engine_rate.c` `g_engine_rate_lut`.

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

Retail's per-frame sub-step body (inside `engine_tick` `0x4b99c0`'s
variable-rate loop, once per step `0x4bb978` grants) reads:

```c
DAT_0057c998++;                                   // master tick
if (player[local] +4 flag) {                      // byte_57BF04[352 * DAT_0057c8cc]
    tick % 1000 == 0 → 0x43eda0(0xb);             // MP messages
    battlefield: tick % 130 → 0x43eda0(0xc), % 10 → (0xa), % 130 == 45 → link poll 0x440666
    else:        tick % 10 → 0x43eda0(0xa); turn vote 0x49b8b0
}
engine_dispatch_delayed_actions(DAT_0057c998);    // 0x4472ce
if (DAT_00553030 == 0) engine_rng_step();         // SP-only LFSR step 0x404a46
if (state 10 / 11) return;
map (DAT_0057c8fc == 0):        0x40490d, 0x49a010, engine_army_state_run 0x4650b0
battlefield (DAT_0057c8fc == 2): 0x478952, below screen 0x2b the unit / group passes, end check 0x477dfc
```

The natural assumption is that `0x4472ce` — 1046 bytes, called with
the master tick, walking a 400-record table (56 B
each, fields from `0x50d7bc`) and dispatching through `funcs_4476BB` — is the heart of the
simulation step.  But its whole body is gated on
`if (DAT_00553030 != 0)` (MP active): in single-player it returns at
once.  The records are MP delayed actions (each due at a tick,
committed in lock-step).  `0x43eda0` is not a scheduler either: it is
the MP message sender (builds a 260-byte packet of type `a1`, payload
size `byte_4D5B90[a1]`, handlers `funcs_43EE4E`), also gated on
`DAT_00553030`.

So in SP the tick-boundary messages and the delayed-action walk are
dead, but the sub-step is still where SP's simulation advances: the
master tick, the LFSR, and per step the strategic sim (`0x40490d`,
`0x49a010`, `0x4650b0`) on the map or the battle passes on the
battlefield.  SP acts on UI events the frame they fire; MP routes
the same actions through the delayed-action queue so peers commit
them at the same tick.

> 📍 `src/rl_host.c` (`rl_4b99c0`), `src/rl_ctl.c` (`rl_4472ce`),
> `src/engine_delayed_action.c` (`engine_dispatch_delayed_actions`),
> `docs/findings/main-loop-tick.md` §"sub-step body",
> `docs/findings/multiplayer-rhost.md`, IDA `0x4472ce` / `0x43eda0` /
> `0x4b99c0`.

*Re-checked 2026-11-18 against the IDA export: 0x43eda0 is the MP message sender, not a scheduled-events dispatcher; SP's sub-step does run the strategic / battle sim (0x40490d, 0x49a010, 0x4650b0 / 0x478952 …), so the "real SP sim happens elsewhere" claim is withdrawn; 📍 → rl_host.c rl_4b99c0.*

---

## 6. DDraw NORMAL Blt is invisible under DWM — and retail dodges this by accident

Modern Windows runs DWM compositing.  Each top-level window has a
"redirection bitmap" backing store; DWM composites the visible
desktop from those.  In our standalone build's windowed mode, DDraw
NORMAL-coop Blts to the primary returned `DD_OK`, the back buffer
held the correct pixels, but the window stayed black.

Observed empirically (standalone build): a windowed-mode
`gfx_capture_backbuffer` showed the rendered fill colour while
`gfx_capture_window` (GDI BitBlt of the same window's client area at
the same instant) showed uniform `(0,0,0)`.

Retail Lords2.exe never bites this because of quirk #1: `sub_42E5C0`
promotes to fullscreen whenever the desktop isn't 8bpp, so it runs
exclusive coop + `SetDisplayMode(640, 480, 8)` and DWM is not in the
loop.  The windowed branch — the standalone build's default — is
what exposes the issue.

Fix (standalone build): `present_via_gdi()` in `src/gfx.c` Locks the
8bpp back surface, builds a `BITMAPINFO` with the 256 palette entries
inline (RGBQUAD B,G,R order — DDraw's PALETTEENTRY hands us R,G,B and
the swap is the only non-obvious bit), and `StretchDIBits` to the
window's HDC.  GDI calls go through DWM, so the composited result is
visible.  Fullscreen `BltFast` needs no such seat.  (The legacy Frida
harness carried the same seat for retail: `presentBackToWindowViaGdi`
/ `installPrimaryBltGdiSeat` in `tools/frida/openlords2-agent.js` —
superseded; rhost's software DirectDraw layer has no DWM in the path.)

> 📍 `src/gfx.c` (`present_via_gdi`), legacy
> `tools/frida/openlords2-agent.js`, IDA `sub_42E5C0`.

*Re-checked 2026-11-18 against the IDA export: retail half confirmed (0x42e5c0 promotes to exclusive fullscreen); dropped the stale `project_dwm_ddraw_normal` memory pointer (no longer exists); Frida marked legacy.*

---

## 7. The save format has nine forward-compatibility slots that were never used

The 225-entry positional schema table at `0x4de960` (8-byte rows:
address, size) is the save-file layout: `save_write` (`0x4ade93`)
writes and `save_load` (`0x4adfe6`) reads the rows in order until a
zero size.  Its last nine rows (indices 216..224) all point to the
**same** 4-byte buffer at `DAT_004eb2ac`, a slot no code references
(0 direct xrefs; it sits just before the castle scratch
`DAT_004eb2b0`).  Save: emits those 4 bytes 9 times in a row.  Load:
reads 4 bytes into it, 9 times, overwriting itself and never using the
value.

```
schema entry 215 → DAT_0056d5cc, 4 B
schema entry 216 → DAT_004eb2ac, 4 B
...                                     ← same address ×9
schema entry 224 → DAT_004eb2ac, 4 B
```

The effect is 36 bytes at offset 266992..267027 of every .sav file —
zero in a fresh session (.data initial value 0), but a load leaves
the last of them in `DAT_004eb2ac`, so a later save writes that word
back ×9.  They are not at the end of the file: after the schema
rows both functions copy the 16 castle-damage blocks (16 × 0x3200 B,
`Castles.dat` ↔ the save) through `DAT_004eb2b0`.

Index 87 (`0x4eafb0`) is **not** a reserved slot: it is the screen
the save dialog was opened from — `0x433f49` stores `DAT_004eac50`
there before switching to screen 54, and `engine_map_post_load`
(`0x4976a1`) restores `DAT_004eac50` from it after a load.

The nine identical rows look like reserved slots — placeholders for
fields a later patch might add, readable by older engines that
would discard them.  The patch never used them.

Our port preserves the layout exactly — the 36 bytes have to be in
the right position for retail to load our saves.

> 📍 [`docs/findings/save-format.md`](save-format.md) §"Reserved/
> sentinel slots", IDA `save_write 0x4ade93` / `save_load 0x4adfe6`,
> `0x4de960` (the schema table), `0x433f49` / `0x4976a1` (index 87).

*Re-checked 2026-11-18 against the IDA export: rows 216..224 confirmed; index 87 is a live field (save dialog's return screen), not scratch; the 36 bytes are followed by the 16 × 0x3200 castle blocks, and a load can make them non-zero.*

---

## 8. Footprint-neighbor helper uses the orthogonal-axis bound for the wrong axis's bounds check

`engine_tile_set_footprint_neighbor_flags` (`0x4921e5`) marks the
neighbours of the centre cell of a 3×3 footprint on the battlefield
grid (`0x5440e0`, 80×80 cells × 8 B; row stride 0x280).  It sets the
centre's `+4 = 1`, ORs `0x10` into `+1` of the four diagonals (each
gated on both axes, correctly), then has two orientation branches —
one writing `+1 |= 0x10` to the two neighbours on one axis, one
writing `+4 = 1` to the two on the other.  The flag writes are
bounded correctly.  The stage writes are bounded on the **wrong
axis**:

```c
if (orient == 0 || orient == 4) {                  /* a3 */
    if (x < 79) cell(x+1, y)[+1] |= 0x10;   /* byte_5440E9 — gated on x<79 ✓ */
    if (x > 0)  cell(x-1, y)[+1] |= 0x10;   /* byte_5440D9 — gated on x>0  ✓ */
    if (x < 79) cell(x, y-1)[+4] = 1;       /* byte_543E64 — needs y>0,  gated x<79 ✗ */
    if (x > 0)  cell(x, y+1)[+4] = 1;       /* byte_544364 — needs y<79, gated x>0  ✗ */
} else {
    if (y > 0)  cell(x, y-1)[+1] |= 0x10;   /* ✓ */
    if (y < 79) cell(x, y+1)[+1] |= 0x10;   /* ✓ */
    if (y > 0)  cell(x+1, y)[+4] = 1;       /* byte_5440EC — needs x<79, gated y>0 ✗ */
    if (y < 79) cell(x-1, y)[+4] = 1;       /* byte_5440DC — needs x>0,  gated y<79 ✗ */
}
```

The stage writes reuse the flag writes' gates — a copy-paste.  At
`y == 0` (or `y == 79`, `x == 0`, `x == 79`) the ungated write lands a
row before / after the grid.

It stays dormant because of who calls it.  There are two callers,
both for a 3×3 siege unit (a siege tower) at the wall:
`0x491492` (a 3×3 unit in movement state 8 docking, called with the
unit's centre) and `0x4917dd` (castles.dat restore of a docked
tower, called with anchor + 1 on both axes, so x, y ≥ 1).  A docked
tower's centre is a wall-side cell inside the grid (castle walls sit
well away from the map edge), so the wrong-axis gates are not known
to ever let a write leave it.

The ports preserve this verbatim — `src/engine_tile.c`'s
`engine_tile_set_footprint_neighbor_flags` uses the same gates.

> 📍 `src/engine_tile.c`'s `engine_tile_set_footprint_neighbor_flags`,
> `src/engine_bf_move.c` (`bf_491492`), `src/rl_cov3.c` (`rl_4917dd`),
> IDA `0x4921e5`, callers `0x491492` / `0x4917dd`.

*Re-checked 2026-11-18 against the IDA export: the wrong-axis gates confirmed; callers corrected (siege-tower dock 0x491492 + castles.dat restore 0x4917dd, not scenario-anchored buildings at castle load); the "save round-trip" rationale dropped.*

---

## 9. Delayed-action dispatcher's hard-capped 10-pass shift compaction

`engine_dispatch_delayed_actions` (`0x4472ce`, MP only) first fills
its due-queue `DAT_00515990` (100 dwords, pre-filled with 0xffff)
with the index of every due record of the 400-record table, count in
`DAT_00515910`.  On the battlefield (`DAT_0057c8fc == 2`) with more
than one due record it then pulls records of types 0x21..0x23 from
non-local players into a stash (`DAT_0050d620`), leaving 0xffff holes,
and runs a hard-coded **10 passes** of a shift loop to close them:

```c
for (m = 0; m < 10; m++)
    for (n = 0; n < count; n++)
        if (queue[n] == 0xffff)
            for (ii = n; ii < count; ii++)
                queue[ii] = queue[ii + 1];     // DAT_00515994[ii]
```

then appends the stash at the first remaining 0xffff
(`for (jj = 0; jj < 100 && queue[jj] < 0xffff; jj++)`).

The 10 is generous, not tight.  Within one pass, after a shift at
`n` the scan moves on to `n+1`, which now holds the run's third
hole — so a run of K consecutive holes shrinks to ⌊K/2⌋ per pass
and closes in ⌊log2 K⌋ + 1 passes (K = 1 → 1, 2..3 → 2, 4..7 → 3,
… 256..511 → 9).  Scattered single holes all close in one pass.
With at most 399 holes (count ≤ 400) the queue is always fully
compacted by pass 9; the cap never leaves a hole for the stash to
land on.

The real edge is elsewhere: the collection loop never caps `count`
at 100, so more than 100 simultaneously-due records would write past
`DAT_00515990` (into `0x515b20..`), and the shift's last iteration
reads `queue[count]` — the 0xffff pre-fill for `count < 100`,
`DAT_00515b20` at `count == 100`.  A per-tick batch is a handful of
records in practice.

The ports preserve the 10-pass count and loop shape verbatim.

> 📍 `src/rl_ctl.c` (`rl_4472ce`), `src/engine_delayed_action.c`'s
> `engine_dispatch_delayed_actions` step 4b, IDA `0x4472ce`.

*Re-checked 2026-11-18 against the IDA export: the shift halves a hole run per pass, so 10 passes suffice for any run < 1024 (≤ 399 possible) — the "11+ consecutive relay records leave holes" claim was wrong; added the uncapped count > 100 overflow.*

---

## 10. The castle-load loop clobbers most of the footprint expansion's writes

`castles_read_scenario` (`0x4aea70`) restores a county's siege
damage: slot `n` of `Castles.dat` (0x3200 B = 6400 two-byte
records) is unpacked row-major into the battlefield grid's `+3`
(glyph) and `+1` (flags) bytes.  The file is written by
`castles_write_scenario` (`0x4ae968`, packing the same two bytes of
every cell) when a strategic siege ends with walls fallen or moats
filled; it is read at the next siege of that county (see
`docs/findings/coverage-round3.md`).  Per cell, with the new flags:
bit 1 → `+7 = 9`, `+4 = 0`, `+2 = (+2 & 0xe3) | 4`; else
bit 0 → `0x4917dd(offset)`.

Flag bit 0 marks the nine cells of a **docked siege tower**:
`0x491492` (a 3×3 unit docking) ORs `+1 |= 1` and stamps the glyphs
`DAT_004d9dd0[9 × orient ..]` into all nine.  On restore, `0x4917dd`
switches on the cell's glyph — only the tower's top-left glyph
(`'@'` / `'C'` / `'X'` / `'['`, orientation 0 / 2 / 4 / 6) proceeds,
the other eight return at once — and re-lays the tower: the nine
glyphs, `+2 = (+2 & 0xe3) | 4` (+ `0x80` on the centre),
`0x4921e5`'s neighbour flags, and a ramp/door cell (`+4 = 2` on the
footprint's facing edge, `+3` = 1 or 2 and `+1 = 0` one cell beyond
it — above for `'@'`, right for `'C'`, below for `'X'`, left for
`'['`).

The expansion runs INSIDE the per-record loop, and the loop keeps
going, so every footprint cell after the anchor in row-major order
gets its `+3` / `+1` overwritten by the file's bytes (rl-cov3 quirk
7).  That clobber is mostly a no-op: the file was packed from the
grid *after* the dock stamped those same glyphs and flags, so the
overwrite writes back what `0x4917dd` just wrote.  What only the
expansion provides survives: `+2` / `+4` of every cell (the loop
never writes them), the anchor's and earlier cells' bytes, and the
path / stage scratches and footprint list (`0x471c1f`, `0x471d30`,
`0x48ee46`).  A cell whose saved `+1` / `+3` differ from what
`0x4917dd` stamps keeps the file's values — the file wins.

> 📍 `src/rl_cov3.c` (`rl_4aea70`, `rl_4917dd` → `src/engine_tile.c`
> `engine_tile_expand_footprint_at`), `docs/findings/rl-cov3.md`
> quirk 7, `docs/findings/coverage-round3.md` (siege-castles),
> IDA `0x4aea70` / `0x4917dd` / `0x491492` / `0x4ae968`.
> (`src/save.c`'s `castles_read_scenario` is not linked in rhost.)

*Re-checked 2026-11-18 against the IDA export: Castles.dat is persisted siege damage and bit-0 cells are a docked siege tower, not scenario buildings; the clobber writes back the same bytes the dock saved, so the "3×3 glyphs not visible after load" conclusion is withdrawn; 📍 → rl_cov3.c.*

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

`engine_category_buckets_refresh_digest` (`0x4291b3`) refreshes a
6-byte digest (`DAT_00569518`), one entry per 16-byte bucket at
`DAT_00567970`: it takes the first bucket value not already in the
digest (`0x42925e`), probing at every-other-byte stride:

```c
v1 = bucket[0];  v0 = 0;  tries = 0;
while (digest_contains(v1)) {
    if (++tries > 5) break;
    v0 += 2;
    v1 = bucket[v0];
    if (bucket[v0] == 0) v0 = 1;      // flip to odd parity
}
digest[i] = v1;
```

The probe sequence is bucket[0], [2], [4], … — until a probe reads a
zero byte, at which point `v0 = 1` flips the walk to odd indices:
the next probe reads bucket[3], then [5], [7].  The zero only drives
a flip when 0 is itself already in the digest; otherwise the loop
ends and the digest takes the 0.

At most six bytes are probed.  Without a flip they are slots 0, 2,
4, 6, 8, 10; with one the walk can reach odd slots 3..9.  Slots 1,
11, 12, 13, 14 and 15 are never read on any path; odd slots only
after a flip.

The map-load caller populates buckets densely from the top, so the
zero-flip rarely fires; hand-made unit-test inputs show it.
Preserved verbatim with its own test case.

> 📍 `src/engine_category_buckets.c`'s
> `engine_category_buckets_refresh_digest`,
> `tests/test_engine_category_buckets.c`'s
> `engine_category_buckets_refresh_zero_value_flips_to_odd_parity`,
> IDA `0x4291b3` / `0x42925e`.

*Re-checked 2026-11-18 against the IDA export: mechanics confirmed; the unreachable-slot list corrected (≤ 6 probes: slots 1, 11..15 never read; no odd slot without a flip), and the flip needs 0 already in the digest.*

---

## 15. Text-input clear() sets length=1 even on a zeroed buffer

`text_input_clear` (`0x4020ae`) zeroes the 2000-byte text buffer
`DAT_005cd550` and the cursor / flags (`DAT_005bb4a8`,
`DAT_0057d3b0`, `DAT_0058fd54`), then sets `DAT_0058fe94`
(`length`) to **1**, not 0.  A later `text_input_recompute_length`
(`0x401f49`) would make it 0.

Why 1 is not clear from the editor logic.  The clamp helper
(`0x401984`, #17) flags `cursor >= length + 1`; with the cursor at 0
neither length 0 nor length 1 trips it, so the "1" does not avoid a
spurious clamp report.  What it does change: with length 1 the cursor
may sit at 1 (one past the terminator) before the clamp fires at 2,
and the insert branch's shift (`0x401333(buf + cursor, buf + length
+ 1)`) spans one more byte.  Most `text_input_init`
(`0x402009`) calls pass a one-space literal (`" "` at
`0x4d56d0..0x4d56f4`, `0x4d5790`), which also yields length 1 — so
"length 1 over a blank field" is the editor's usual starting state.

`text_input_init` sets length to the copied payload length (0 for an
empty payload — possible for the name buffers `DAT_0053f1e0` /
`DAT_004ea130`; the save-dialog caller `0x433f49` follows init with
a recompute anyway).

> 📍 `src/text_input.c`'s `text_input_clear`,
> `tests/test_text_input.c`'s `text_input_clear_sets_length_one_sentinel`,
> IDA `0x4020ae` / `0x402009` / `0x401984`.

*Re-checked 2026-11-18 against the IDA export: the length=1 store confirmed; the "avoids a spurious clamp report" rationale is wrong (cursor 0 never clamps at length 0 either); noted the " " init literals.*

---

## 16. (withdrawn) Text-input init's register-reuse off-by-minus-one indexing

Claimed `0x402009` shares one register between its clear and copy loops and writes `(&buf[1])[i]` with `i = -1`, an
off-by-one "compensated" in the address formula.  That is Ghidra's rendering.  The asm keeps `i` in a stack local
(`[ebp-4]`) and does `inc [ebp-4]; mov byte_5CD550[ecx], al` — plain `buf[++i] = *p++` after the clear loop leaves
`i = -1`: the source reused its loop variable, nothing more.  (The copy has no 2000-byte bound.)

---

## 17. Text-input cursor clamps past-end to length+1, not length

`text_input_clamp_cursor` (`0x401984`) resets `DAT_005cd408`
(clamp_state) to 0, refreshes the text width (`0x401f8f`, which sets
the too-wide flag `DAT_0057d3b0`), then:

```c
if (length + 1 <= cursor) { cursor = length + 1; clamp_state = 1; }
if (cursor >= max_len)    { cursor = max_len;    clamp_state = 2; }   // MaxCharCount
if (DAT_0057d3b0)          clamp_state = 2;                          // text too wide
if (cursor < 0)            cursor = 0;
```

So `cursor == length` (on the terminator) is unflagged; `cursor ==
length + 1` stays put but **is** flagged (state 1); anything beyond
is pulled back to `length + 1` with state 1.  State 2 means "no
room": the cursor reached `max_len`, or the text is wider than the
field's pixel budget (`DAT_0058fd68`).  `text_input_insert_char`
(`0x401d26`, #18) refuses to write in state 2.

Why `length + 1` rather than `length` is not visible in the clamp
itself; with `text_input_clear`'s length = 1 (#15) it lets the
cursor sit one past the terminator of an empty field.

> 📍 `src/text_input.c`'s `text_input_clamp_cursor`,
> `tests/test_text_input.c`'s
> `text_input_clamp_cursor_past_length_bumps_to_length_plus_one`,
> IDA `0x401984`.

*Re-checked 2026-11-18 against the IDA export: cursor == length+1 is flagged (state 1), not a no-op; state 2 also comes from the too-wide flag DAT_0057d3b0; the caret-rendering rationale was unverified and is dropped.*

---

## 18. Text-input "overwrite" mode skips the length update

`text_input_insert_char` (`0x401d26`) is gated twice before either
branch runs: the editor must be enabled (`DAT_005aeb78`), and the
clamp helper (`0x401984`, #17) must not report state 2 — so the cursor
is below `max_len` and the text fits the field's pixel budget.  Then
it branches on `DAT_005c9280` (the insert / overwrite toggle):

```c
if (DAT_005aeb78 && (clamp_cursor(), clamp_state != 2)) {
    if (DAT_005c9280 == 0) {                       // overwrite
        buf[cursor++] = c;
    } else if (length < max_len) {                 // insert
        shift_right(buf + cursor, buf + length + 1);   // 0x401333
        buf[cursor++] = c;
    }
}
```

Neither branch updates `length` (`DAT_0058fe94`).  In overwrite mode
a char typed at the terminator (cursor == length) or one past it
(cursor == length + 1, allowed by the clamp) replaces the NUL / the
byte after it, and `length` stays stale until the next
`recompute_length` (`0x401f49`).  The insert branch bounds on
`length < max_len`; overwrite relies on the clamp's `cursor <
max_len` instead.

> 📍 `src/text_input.c`'s `text_input_insert_char`,
> `tests/test_text_input.c`'s `text_input_insert_char_at_end_extends_in_overwrite`,
> IDA `0x401d26`.

*Re-checked 2026-11-18 against the IDA export: overwrite is not unbounded — the clamp's state-2 gate (cursor < max_len, text fits) and DAT_005aeb78 guard both branches; the stale-length quirk stands.*

---

## 19. Text-input probe special-cases the decimal point past column 8

`text_input_probe_at_cursor` (`0x401e49`) writes `DAT_00591520`
from the character under the cursor: 1 if it is the terminator, 0
otherwise — except that with `clamp_state == 1` and a `'.'` under the
cursor it writes 1 below column 8 and **2** from column 8 on:

```c
DAT_00591520 = (buf[cursor] == '\0');
if (DAT_005cd408 == 1 && buf[cursor] == '.')
    DAT_00591520 = (cursor >= 8) ? 2 : 1;
```

Nothing uses it.  `0x401e49` has no call site in the binary (no
`call`, no stored pointer) and `DAT_00591520` is write-only (its four
xrefs are this function's stores).  It is dead code — the meaning of
the 8 and the 2 is lost.  (State 1 puts the cursor one past the
terminator, so the `'.'` test reads a stale byte there.)

> 📍 `src/text_input.c`'s `text_input_probe_at_cursor`,
> `tests/test_text_input.c`'s `text_input_probe_at_cursor_decimal_ge_eight_is_two`,
> IDA `0x401e49`.

*Re-checked 2026-11-18 against the IDA export: 0x401e49 is uncalled and DAT_00591520 never read — dead; the "number-entry warning indicator" explanation was invented and is withdrawn.*

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

The localized-text blob lives at `0x591580` (120000 B; L2.eng is
100016 B).  The seek (`0x4018d7`), read (`0x4017bf`) and
`gfx_wrap_localized` (`0x40328e`) paths check only the **section
start** — `blob + localization_offset(index)` — once, against the
address `0x5aea40 = blob + 120000`:

```c
p = blob + localization_offset(index);
if (p <= (char *)&DAT_005aea40) {            // asm: cmp; jnb — inclusive
    while (n > 0) { if (*p == 0 && p[-1] ...) n--; p++; }   // unbounded
    while (*p < 0x20) p++;                                  // unbounded
    ...
}
```

So the start may sit exactly at `blob + 120000` (one past the blob),
and nothing bounds the sub-string walk or the control-byte skip that
follow: a large `n`, or a run of bytes < 0x20, walks off the blob.
`blob + 120000` is not padding — it is `DAT_005aea40` itself, a
live flag (the tooltip `0x476e95` sets it to 1 around its
measure-and-draw `gfx_wrap_localized` calls, and
`gfx_text_render_line` reads it to skip the shadow passes).  The seek
path stores its pointer in `DAT_005c9270` before the check, so a
failed check still leaves the global pointing at the section start.

The write path (`0x4016f3`, #21) has no check at all — walk,
control-skip and copy all unguarded — but it has no caller.

In retail this is masked by the data: section offsets come from the
blob's own table and L2.eng ends ~20 KB short of the 120000-byte
buffer (zero-filled .bss), so walks stop on real strings.  Our port
mirrors the inclusive start check (`walker_in_bounds`) and, like
retail, does not bound the walk.

> 📍 `src/localization.c`'s `walker_in_bounds` and
> `localization_write`,
> `tests/test_localization.c`'s
> `localization_seek_skip_out_of_bounds_suppresses_walk`,
> IDA `0x4018d7`, `0x4017bf`, `0x40328e`, `0x4016f3`.

*Re-checked 2026-11-18 against the IDA export: the bound is a one-time check of the section start (inclusive), not a gate on the walk; blob+120000 is the live flag DAT_005aea40; added 0x40328e's copy of the check.*

---

## 23. Word-width measurer treats leading spaces as word-internal

`font_metrics_word_width` (`0x4036f9`, the per-word pixel-width
measurer behind both word-wrap renderers) splits characters into
"leading" and "internal" with a `saw_printable` flag:

```c
if (c == ' ') {
  if (saw_printable) return total;     /* internal space ends word, not consumed */
  total += 4;                          /* leading space contributes */
}
```

Leading spaces are MEASURED INTO the word's width — a flat **+4 px
each, whatever the font** (`a2` is ignored here; `0x4015b9` also
returns 4 for a space) — but the first space after a printable ends
the word without being counted in `DAT_005c9a7c` (chars consumed).
The next call starts on that space, so whitespace belongs to the
**following** word: the caller copies `DAT_005c9a7c` bytes, a word
plus the spaces before it.  `gfx_wrap_localized` (`0x40328e`) then
drops leading spaces at the start of each wrapped line (#35); the
buffer variant `0x40352f` keeps them.

> 📍 `src/font_metrics.c`'s `font_metrics_word_width`,
> `tests/test_font_metrics.c`'s
> `font_metrics_word_width_leading_spaces_count_as_four` and
> `font_metrics_word_width_terminates_at_internal_space`,
> IDA `0x4036f9`.

*Re-checked 2026-11-18 against the IDA export: whitespace attaches to the following word (the entry said the preceding one); the 4 px is font-independent.*

---

## 24. `$` is a soft word boundary in the word-wrap stream

Same function (`0x4036f9`), parallel branch:

```c
if (c == '$') {
  if (saw_printable) return total;     /* internal '$' ends word, not consumed */
  /* leading '$': no width, counted in chars_consumed */
}
```

A `'$'` after the first printable ends the word; the next word then
starts on it, where it is "leading": consumed with zero width.  So
`'$'` is a zero-width break point for the wrap.

It is not a macro sigil.  The text renderer `gfx_text_render_line`
(`0x402637`) substitutes on `'#'` (`0x402c16`, #33), not `'$'`, and
L2.eng holds no `$P` / `$1`-style strings (four `$` bytes in all).
In the renderer `'$'` is an ordinary character with no glyph
(`DAT_004d71f0[4] == 0`), drawn as a 4-px gap — so a `'$'` measures
0 px but renders 4 px wide, and a wrapped line containing one can
overrun its budget by 4 px.  Why the measurer singles out `'$'` is
not known.

> 📍 `src/font_metrics.c`'s `font_metrics_word_width`,
> `tests/test_font_metrics.c`'s
> `font_metrics_word_width_terminates_at_internal_dollar` and
> `font_metrics_word_width_leading_dollar_is_skipped`,
> IDA `0x4036f9`, `0x402637`.

*Re-checked 2026-11-18 against the IDA export: the measurer behaviour is confirmed; the "$P / $1 macro" explanation was wrong ('#' is the substitution sigil, L2.eng has no $ macros); added the 0-vs-4 px measure/render mismatch.*

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

The bounds-checked 2-byte horizontal brush `gfx_paint_2x1_clipped`
(`0x403a2c`) tests `(x, y)` against `(xRight, yBottom)` and then calls
the unchecked painter `gfx_paint_2x1` (`0x4b3f7a`), which writes
`Buf[x + y*xRight]` *and* `Buf[x + y*xRight + 1]`; the `x+1` pixel is
never checked.

```c
if (x >= 0 && x < xRight && y >= 0 && y < yBottom)
    gfx_paint_2x1(x, y, color);   /* writes [x] and [x+1] */
```

With `x == xRight - 1` the right pixel lands on the first pixel of
row `y+1` (`Buf` is the locked back surface, addressed with pitch =
`xRight`); at the bottom-right corner it writes one byte past the
surface.

In retail this is unreachable: `0x403a2c`'s only caller is
`0x4040d6` (a diamond outline drawer stepping x by 2), and `0x4040d6`
itself has no caller (no `call`, no stored pointer).  We preserve
it verbatim — the
`gfx_paint_2x1_clipped_bottom_right_corner_wraps_into_guard` test
pins the stomp.

> 📍 `src/gfx_paint.c`'s `gfx_paint_2x1_clipped`,
> `tests/test_gfx_paint.c`'s
> `gfx_paint_2x1_clipped_right_edge_wraps_to_next_row` and
> `_bottom_right_corner_wraps_into_guard`,
> IDA `0x403a2c` (wrapper), `0x4b3f7a` (unchecked painter),
> `0x4040d6` (sole, dead caller).

*Re-checked 2026-11-18 against the IDA export: the unchecked x+1 confirmed; the callers are not the line drawers 0x403c49 / 0x40437d — the only caller 0x4040d6 is itself uncalled, so the quirk is dead code.*

---

## 27. Glyph blitter clipped duplicates are byte-identical

The text-glyph dispatcher (`gfx_dispatch_glyph`, `0x402a14`) selects
between three leaf blitters by the x-clip mode `DAT_005cdd20` set by
`0x40464c`:

  - `0` (unclipped)  → `0x4b41b7` (165 B)
  - `1` (left-clip)  → `0x4b425c` (105 B)
  - `2` (right-clip) → `0x4b42c5` (105 B)

(mode 5 = invisible, no call.)  The two clipped functions are
**byte-for-byte identical** modulo addresses: same prologue, same
globals, same loop:

```
0x4b425c: push ebp; mov ebp,esp; push ebx; push esi; push edi
          mov esi,[ebp+8]; add esi,[Offset]           ; src = font + glyph offset
          mov edi,[Buf]; add edi,[0x591524]           ; dst = surface + x
          mov eax,[0x591528]; mov edx,[xRight]; mul edx; add edi,eax
          mov edx,[0x5bb478]; mov ebx,[0x57d3bc]      ; rows, colour
row:      mov ecx,[0x591514]                          ; visible width
px:       mov al,[esi]; cmp al,0; jz +2; mov [edi],bl ; paint non-zero texels
          inc esi; inc edi; dec ecx; jg px
          add esi,[0x59150c]; add edi,[0x5cd400]; dec edx; jg row
```

These are hand-written assembly (the `0x4b3xxx` blitter module:
register-only loops, `dec / jg`), not compiler output, so no
compiler de-duplication question arises — the author simply wrote
the left and right cases as two copies.  The clip offsets
(`DAT_0058fd50` into the source, the x shift) are applied by the
dispatcher / `0x40464c` beforehand, which is why one body serves both.

One behavioural difference vs the unclipped blitter: `0x4b41b7`
tests `DAT_005cd40c == 1` and, if set, copies each texel's own
colour; both clipped copies always paint the solid colour
`DAT_0057d3bc`.  A multicolour glyph clipped at a side edge draws
single-colour.

Our port exposes both clipped entry points
(`gfx_glyph_blit_left_clipped` / `_right_clipped`) sharing a static
helper.

> 📍 `src/gfx_glyph.c`'s `gfx_glyph_blit_left_clipped` /
> `_right_clipped` / shared `glyph_blit_clipped`,
> `tests/test_gfx_glyph.c`'s
> `gfx_glyph_blit_right_clipped_byte_identical_behavior`,
> IDA `0x4b41b7`, `0x4b425c`, `0x4b42c5`.

*Re-checked 2026-11-18 against the IDA export: identity confirmed; the MSVC de-dup / COMDAT rationale is wrong (hand-written asm); added the clipped copies' missing DAT_005cd40c texel-colour path.*

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

Each 16-byte glyph descriptor in a Lords II font blob (`font + 8 +
16 × (glyph − 1)`, read by `gfx_dispatch_glyph` `0x402a14`) carries:

  - **`short` +0** — width (`word_5C9258`; the return value is width + 1).
  - **`short` +2** — height in rows (`word_5AEB70`).  The y-clip setter
    `0x40477b` clips it against the clip rect and computes the top skip
    `DAT_0058feac`.
  - **`dword` +4** — the texel offset.
  - **`byte` +0xd** — a per-glyph **vertical offset**.  The dispatcher
    stores it in `DAT_005bb478` and adds it to the draw y
    `DAT_00591528`.

`DAT_005bb478` then gets OVERWRITTEN by the y-clip setter with the
visible (post-clip) row count — the value the blitter actually uses
as its row counter.  One global, two unrelated uses, ordered so the
offset is applied before the clip needs the slot:

```c
DAT_005bb478 = desc[+0xd];           // step 1: per-glyph y offset
DAT_00591528 += DAT_005bb478;        // step 2: draw y += offset
... (default-font accent shave, #30) ...
clip_x(…); clip_y(…);                // step 3: OVERWRITES DAT_005bb478 with visible rows
blit(font);                          // step 4: blitter reads the row count
```

The offset is not a line advance: `gfx_text_render_line` (`0x402637`,
the only caller) re-sets `DAT_00591528` to the line's y (± 1 for
the shadow passes) before every dispatch, so it never accumulates.
It is the glyph's top bearing — how far below the line top its
first stored row is drawn (presumably so short glyphs need no blank
top rows in the blob).

> 📍 `src/gfx_dispatch.c`'s `gfx_dispatch_glyph` (its comment still
> calls +0xd an "advance"), IDA `0x402a14`, `0x40477b`, `0x402637`.

*Re-checked 2026-11-18 against the IDA export: +0xd is a per-glyph y offset (top bearing), not a cursor/line advance — render_line resets the y before each glyph; dropped the dead docs/decompiled pointer.*

---

## 30. Glyph dispatcher hard-codes per-character kerning tweaks for one font

`gfx_dispatch_glyph` (`0x402a14`) has three character-range checks
that each subtract one from the draw y — but ONLY when the font
passed in is the default font blob at `0x5af8f0`:

```c
if (font == &unk_5AF8F0) {
    if (a2 >= 0x61 && a2 <= 0x6d) y--;
    if (a2 >= 0x73 && a2 <= 0x77) y--;
    if (a2 >= 0x80 && a2 <= 0x84) y--;
}
```

`a2` is not the character: the only caller, `gfx_text_render_line`
(`0x402637`), passes `c − 0x20` (the index into `DAT_004d71f0`, #32).
The ranges are therefore characters **0x81..0x8d, 0x93..0x97 and
0xa0..0xa4** — in code page 437 exactly the accented lowercase
letters: ü é â ä à å ç ê ë è ï î ì, ô ö ò û ù, á í ó ú ñ (not the
unaccented æ at 0x91).  Smaller y is higher on the screen, so these
glyphs are drawn **one pixel higher** than their descriptor's offset
(#29) says — room for the accent, in the default font only.

  - **The tweak lives in the dispatcher, not the font**: keyed on the
    font pointer, so the tall font `0x5b2fa0` and any other blob draw
    the same glyph indices unshifted.
  - **It applies after `y += offset`** and only to the glyph pass it is
    in; render_line re-sets y before each pass, so nothing accumulates.

We expose the trigger font as a runtime registration
(`gfx_dispatch_set_default_font`); the port tests the same raw
`a2` ranges.

> 📍 `src/gfx_dispatch.c`'s `gfx_dispatch_glyph` default-font branch,
> IDA `0x402a14` (`cmp` against `0x5af8f0`), caller `0x402637`.

*Re-checked 2026-11-18 against the IDA export: the ranges are glyph indices (char − 0x20) → CP437 accented lowercase 0x81..0x8d / 0x93..0x97 / 0xa0..0xa4, not 'a'..'m' / 's'..'w'; y−1 draws them one pixel higher, not lower.*

---

## 31. Alt-clip rect mixes runtime globals with hard-coded constants

`gfx_dispatch_glyph` (`0x402a14`) clips each glyph against one of
two rects.  Normally `(0, 0)..(xRight, yBottom)`.  When
`DAT_005aeb84 != 0` it uses an alt rect from four sources:

  - **Left**: `DAT_0052afdc` (runtime global; 0 or 4 by map zoom, set in
    `engine_map_init_set_camera` `0x451fcc`).
  - **Right**: 478 (`0x1de`, immediate).
  - **Top**: 24 (`0x18`, immediate).
  - **Bottom**: `DAT_00553254` (runtime global, same setter).

The globals are the map view's extents, so the alt rect would clip
text to the map viewport.

It never fires in retail: every store to `DAT_005aeb84` writes 0
(eight `mov dword_5AEB84, 0` in `0x402637` and the wrap / label
renderers `0x402c5e` .. `0x40328e`; the image holds nine references:
those eight stores and the dispatcher's one read).  The alt-clip
branch is dead code.  Our port keeps it (`set_alt_clip`, the two
constants as `#define`s) for shape parity.

> 📍 `src/gfx_dispatch.c`'s `GFX_DISPATCH_ALT_CLIP_TOP/RIGHT` +
> `gfx_dispatch_glyph` alt-clip branch, IDA `0x402a14` (`0x402b0e`
> read of `DAT_005aeb84`), `0x451fcc`.

*Re-checked 2026-11-18 against the IDA export: DAT_005aeb84 is only ever written 0, so the alt-clip branch is dead (the entry said overlay panels used it); the globals are map-view extents.*

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

`gfx_text_render_line` (`0x402637`) has a `'#'` macro: when the
substitution flag `DAT_005bb4c4` is 1 (the wrap renderers arm it per
line), a `'#'` in the source is replaced by bytes from the buffer
`DAT_005bb4d0` via `substitution_next` (`0x402c16`); with the flag
off `'#'` renders as a space.  The top-of-loop `'#'` test only
starts the stream; from then on the BOTTOM-of-loop advance fetches
from the stream purely because its cursor `DAT_005bb4b4` is non-zero.
The source pointer stays parked on the `'#'` meanwhile.

When the stream reaches its NUL, `0x402c16` returns `' '` and resets
the cursor to 0.  That space is rendered (4 px), and the zero cursor
re-arms the source read: the next advance is `*++src`, the char
after the `'#'`.

So `"text#more"` with substitution `"AB"` renders as
**`"textAB more"`** — the stream's terminator becomes a visible
space.  Every `'#'` restarts the stream from its first byte (an empty
substitution renders the `'#'` as one space).  Neither the flag nor
the cursor survive the call (both reset at the end).  The wrap
measurer (`0x4036f9`) sees `'#'` as a zero-width glyph-less char, so
substituted text is not measured.

> 📍 `src/gfx_text.c`'s `gfx_text_render_line` (substitution stream
> branch) + `substitution_next`, IDA `0x402637` (`0x4026c6` the `'#'`
> test, the bottom-of-loop `DAT_005bb4b4` test), `0x402c16`.

*Re-checked 2026-11-18 against the IDA export: the stream's NUL yields a rendered space ("textAB more", not "textABmore"); each '#' restarts the stream; corrected the cited addresses.*

---

## 34. (withdrawn) Renderer's uppercase-highlight does a per-iteration color reset

Claimed `0x402637` resets the glyph colour every iteration through a source-level comma expression in a short-circuit
gate.  That is Ghidra's rendering; the asm at `0x4026ff` is a plain block: `if (DAT_0058fe2c) { colour = a5; if ('A' <=
c && c <= 'Z') colour = 1; }`.  Nothing quirky: with `DAT_0058fe2c` set (by the screens at `0x40f5fd`, `0x41608b`,
`0x41635f`, `0x417960`) uppercase letters draw in palette index 1, everything else in the caller's colour.

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
(the max of `gfx_text_render_line`'s `DAT_005cd404` across lines).

`gfx_wrap_localized` (`0x40328e`) resets it to 0 as its first
statement.  `gfx_wrap_buffer` (`0x40352f`) does NOT, so after it the
field holds "max of this render's lines AND whatever was there
before".  No other code writes it — there are no compensating resets
at call sites.

The asymmetry is unobservable in retail: the field's only reader is
the tooltip `engine_tooltip_tick` (`0x476e95`), which reads it right
after its own `gfx_wrap_localized` call (to size the tooltip box:
`12 - (176 - DAT_005aeb88) / 16` tiles).  Nothing reads it after a
`gfx_wrap_buffer`.

Our port keeps the asymmetry: `gfx_wrap_localized` resets,
`gfx_wrap_buffer` does not; `gfx_wrap_set_max_line_x` is a test
helper.

> 📍 `src/gfx_wrap.c`'s `gfx_wrap_buffer` (no reset) +
> `gfx_wrap_localized` (resets at top), IDA `0x40352f`, `0x40328e`,
> reader `0x476e95`.

*Re-checked 2026-11-18 against the IDA export: no call site resets DAT_005aeb88 (the entry claimed some do); its only reader is the tooltip 0x476e95 right after a localized wrap, so the missing reset is never observed.*

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

`gfx_line_draw` (`0x403a8f`) is a Bresenham line drawer with an
unusual variant: `DAT_005aeb40` / `DAT_005aeb44`, set by `bres_setup`
(`0x4037ee`) to abs(dx) / abs(dy), are **decremented inside the
loop** — the dominant axis's every step, the other's on each minor
step.  (Pure vertical / horizontal lines take separate plain loops.)

Shallow loop body (steep mirrors it):

    paint(x, y, color);                  // 0x4039c7, clipped
    bres_err_update(0);                  // 0x403949, reads CURRENT slots
    DAT_005aeb40--;
    if (err >= 1) { DAT_005aeb44--; y += DAT_005aeb58; }
    x++;

with `err = 2·dy − dx` initially and

  shallow: err < 0  → err += 2 * abs_dy
           err >= 0 → err += 2 * (abs_dy - abs_dx)
  steep:   err < 0  → err += 2 * abs_dx
           err >= 0 → err += 2 * (abs_dx - abs_dy)

Textbook Bresenham keeps the deltas fixed.  With the shrinking
deltas the distribution is lopsided — `(0,0)→(10,5)` plots 4 pixels
at y=0, 3 at y=1, then 1 each at y=2..5 — and the far endpoint is
often **missed**: the pixel count is always `max(|dx|,|dy|) + 1`
and the endpoint `bres_setup` starts from (lower x when shallow,
lower y when steep) is exact; the other often is not: `(0,0)→(2,1)` ends at `(2,0)`,
`(0,0)→(1,2)` at `(0,2)`, `(0,0)→(10,3)` at `(10,2)`; 154 of the 400
lines with |dx|, |dy| in 1..20 end off their target.  The common
callers draw axis-aligned rects (`0x403cf4` …), which take the plain
loops and are exact.

Why retail did this is unclear.  Our port preserves the exact
arithmetic (rhost row `gfx_line_draw`, 0x403a8f).

> 📍 `src/gfx_line.c`'s `bres_err_update` + `gfx_line_draw`,
> `tools/rhost/rh_hooks_gfx.c` (row `gfx_line_draw`), IDA `0x403a8f`,
> `0x403949`, `0x4037ee`.

*Re-checked 2026-11-18 against the IDA export: the mutation and the (0,0)→(10,5) distribution confirmed; the "endpoints land on the correct coordinates" claim is false (simulated from the IDA body: 154/400 small diagonals miss the end).*

---

## 39. Bevel rects skip the right/bottom clamp when x/y was clamped

The 3 axis-rect bevel wrappers (`0x403deb`, `0x403ee4`, `0x403fdd`)
all use the same bounds-clamp pattern at the top:

    if (x > 0) {
        if (x + w >= xRight) w = xRight - x;
    } else {
        x = 0;
    }

(same for y / h against `yBottom`).  The right-edge clamp ONLY runs
when the left edge was positive.  A rect starting at `x <= 0` is
moved to x = 0 with its width unchanged and no right-edge fit.

The solid rect `gfx_line_rect` (`0x403cf4`) uses sequential `if`s:

    if (x <= 0) x = 0;
    if (x + w >= xRight) w = xRight - x;

Both clamps fire independently.

The difference is visible, not just wasted work: for a rect with
`x <= 0` and `x + w > xRight` (or the y analogue), the solid rect
draws its right (bottom) edge at `xRight − 1`, while the bevel's
right edge lands at `x + w − 1 >= xRight` and is clipped away
entirely by the per-pixel painter (`0x4039c7`) — the bevel loses its
right (bottom) edge.  Ordinary on-screen panels never hit it.

Our port preserves both clamp shapes as distinct helpers
(`clamp_solid` vs `clamp_bevel`).

> 📍 `src/gfx_line.c`'s `clamp_solid` + `clamp_bevel`, IDA
> `0x403cf4`, `0x403deb`, `0x403ee4`, `0x403fdd`.

*Re-checked 2026-11-18 against the IDA export: clamp shapes confirmed; "visible result is the same" is wrong — an oversize bevel starting at x ≤ 0 loses its right/bottom edge.*

---

## 40. Dotted line drawer is axis-only — diagonals silently draw nothing

`gfx_line_draw_dotted` (`0x403c49`) is the dotted variant of the
line drawer.  Its body is the same `bres_setup` + axis dispatch as
`0x403a8f`, trimmed:

  - Pure vertical (x1==x2)   → loop with step +2 on y
  - Pure horizontal (y1==y2) → loop with step +2 on x
  - Anything else            → fall through, draw NOTHING

There are no shallow or steep branches.  Diagonal calls return
without painting a pixel.

It does not matter in retail: `0x403c49` has no caller at all (no
`call`, no stored pointer in the image) — the whole function is dead
code.

Our port preserves the silent no-op verbatim — tests assert that
diagonal dotted-line calls produce zero pixels.

> 📍 `src/gfx_line.c`'s `gfx_line_draw_dotted`, IDA `0x403c49`.

*Re-checked 2026-11-18 against the IDA export: axis-only body confirmed; the function is uncalled (the entry claimed two UI-border call sites).*

---

## 41. Label-render `prefix` overwrites buffer[0] rather than prepending

The four label-render wrappers (`0x402f64` render_dec, `0x403015`
render_hex, `0x4030c6` render_dec_centered, `0x403190`
render_dec_signed) all take a `char prefix` parameter.  The wrapper
body is:

    gfx_text_label_format_dec(value, 1, 0);   /* 0x4022bd: digits at offset 1 */
    if (prefix != 0)
        byte_58FDA0[0] = prefix;              /* OVERWRITE byte 0 */
    /* ...append suffix, render... */

The formatter is called with offset=1, so it writes the sign (if any)
at buffer[1] and digits after it.  Buffer[0] is left as 0 by the
formatter's zero-fill.  The prefix then drops INTO buffer[0] — it
isn't a "prepended" character; it's the byte-0 slot.

So `prefix=' '` with value=42 yields `" 42\0"`; `prefix='@'` yields
`"@42\0"`.  Retail passes ' ' (most callers), '@' (stat readouts),
'(' (three callers: `0x4224e7` twice, `0x425487`) and '+' / '-'
(`0x402e0c`, the labeled-signed renderer — see #47).  Every literal
prefix in the IDA export is non-zero; the width budget implicitly
includes the prefix slot.

A caller passing `prefix=0` would NOT get a harmless leading gap: the
suffix-append walk starts at the first NUL, which is buffer[0] itself.
In the non-centered wrappers the suffix is copied over the digits from
[0] and the string renders as the suffix alone; in the centered ones
(#45's off-by-one) the suffix lands at [1] and buffer[0] = NUL renders
nothing.  Either way the number vanishes.  No retail callsite does
this.

> 📍 `src/gfx_text_label.c`'s `apply_prefix`; rhost rows
> `gfx_label_dec` / `gfx_label_hex` / `gfx_label_dec_centered` /
> `gfx_label_dec_signed` in `tools/rhost/rh_hooks_gfx.c`; IDA
> `0x402f64` / `0x403015` / `0x4030c6` / `0x403190`.

*Re-checked 2026-11-18 against the IDA export: prefixes also '(' / '+' / '-', and prefix 0 drops the number instead of leaving a skipped gap.*

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

## 43. Format buffer pre-zero scope is 100 bytes — the whole retail buffer

Both integer formatters (`0x4022bd` dec, `0x402447` hex) open with:

    for (i = 0; i < 100; ++i)
        byte_58FDA0[i] = 0;

In retail that is the ENTIRE buffer: `byte_58FDA0` is 100 bytes, and
the next global, `Offset` at `0x58fe04` (= `0x58fda0 + 100`), is the
sprite-descriptor src-offset dword staged by every sprite wrapper
(`0x404442`, `0x40a21a`, ...).  The port's 8000-byte
`g_gfx_format_buffer` (`GFX_TEXT_LABEL_BUFFER_LEN`) is a standalone
safety margin, not retail's size (see also
`docs/findings/render-under-rhost.md`).

Consequences:
  - Every format/render starts from a fully zeroed retail buffer.
  - A prefix+digits+suffix string longer than ~99 bytes would run
    past the buffer into `Offset` (and the globals after it) in retail;
    real callers stay well under 16 bytes.

Other readers of the buffer: `0x4afab9` (the status.txt logger,
`rl_4afab9` in `src/rl_io.c`) copies at most 100 bytes of it after
formatting its line number, and `0x4afeed` (a date/time formatter whose
only caller `0x4b03ab` has no callers) copies at most 10 / 4 bytes.

> 📍 `src/gfx_text_label.c`'s `format_zero_and_sign`,
> `src/gfx_globals.h` (`GFX_TEXT_LABEL_BUFFER_LEN`), IDA
> `0x4022bd` / `0x402447`.

*Re-checked 2026-11-18 against the IDA export: retail's buffer is 100 bytes (`Offset` at 0x58fe04 follows), not 8000; 0x4afab9 / 0x4afeed roles corrected.*

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

The unrolled-prefix gate in `0x4b43b1` (`gfx_sprite_blit`) is
`(word_5C9258 & 3) != 0`.  For a width of 0 this is `false` (zero
matches the "multiple of 4" path), so the unrolled body fires
unconditionally:

    if (src[0]) dst[0] = src[0];
    if (src[1]) dst[1] = src[1];
    if (src[2]) dst[2] = src[2];
    if (src[3]) dst[3] = src[3];

Four source bytes are read and up to four written per row.  The
post-unroll test is `v6 = v4 <= 4; v4 -= 4; if (!v6) break;` — for
width 0 `v6` is true, so the row ends there and the byte-at-a-time
tail does NOT compound the over-write.

Unreachable in retail, though: every caller of the blitter (all 13 —
`0x404442`, `0x404547`, `0x408438`, `0x40946d`, `0x409934`, the
`0x40a0xx` wrappers, `0x4bcbdc`, `0x4be070`, `0x4be33f`) runs
`gfx_dispatch_clip_x` (`0x40464c`) first, which sets x mode 5 when
`word_5C9258 <= 0`, and `gfx_dispatch_clip_y` (`0x40477b`) copies that
5 into `byte_58FE18`; the callers then skip the blit
(`byte_58FE18 != 5`).  So a zero-width descriptor never reaches
`0x4b43b1`.  The behaviour is kept as a witness test in
`tests/test_gfx_sprite.c`.

> 📍 `src/gfx_sprite.c`'s `gfx_sprite_blit`,
> IDA `0x4b43b1`, `0x40464c`, `0x40477b`.

*Re-checked 2026-11-18 against the IDA export: the clip setters DO skip zero-width sprites (x mode 5 for width <= 0, propagated by clip_y), so the path is unreachable, not merely unused by the data.*

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

`0x40a01d` (`gfx_sprite_render_raw`, "render a raw caller-supplied
buffer") stages width / height / `Offset = 0` / dst_x / dst_y from its
own parameters — but does NOT touch the descriptor-flag globals
(`byte_591568`, `byte_57D3C0`, descriptor +0xc / +0xe) or the row
count `dword_5BB478` (+0xd).  Whatever the previous descriptor-based
render left in those slots carries over.

None of it is observable in retail:
  - `dword_5BB478` is overwritten by clip_y (`0x40477b`) with the
    post-clip visible row count before the blitter reads it.
  - `byte_57D3C0` has no reader anywhere in the binary (data_xrefs:
    writes only).
  - `byte_591568`'s only readers, `0x406673` (drawer_b) and
    `0x406bba`, stage it from their own descriptor (+12) before
    reading it, so a stale value from `0x40a01d` is never consumed.

A harmless leak — worth knowing only if a future port reads these
globals without restaging them.

> 📍 `src/gfx_sprite_render.c`, IDA `0x40a01d`; readers `0x406673`,
> `0x406bba`.

*Re-checked 2026-11-18 against the IDA export: the flag globals' only readers restage them first (0x57d3c0 is never read), so the leak is unobservable in retail.*

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

## 61. Map-render linked-list terminator is self-loop (or zero)

The per-tile drawer (`0x408438`) iterates the linked list of feature
records (`g_per_territory_420`, base `0x52f0b0`, stride `0x1a4`) using
each record's next-link field (+0x04, signed short,
`word_52F0B4[210 * idx]`).  The end-of-record test is:

    v1 = word_52F0B4[210 * v5];
    if (v1 == v5) return;      /* next points at itself → done */
    v5 = (__int16)v1;

and the loop top is `if (v5 == 0) return;`.  So the walk ends on
EITHER a self-loop (the last record's next-link is its own index) or
a 0 link (slot 0 is the reserved "none" record — the tile head byte
`byte_522F95[cell]` is 0 for an empty tile by the same rule).  A
record with `next == 0` does NOT advance into slot 0; slot 0 is never
drawn.

The self-loop convention is the notable part: a list whose last record
points at a DIFFERENT live record (e.g. a stale link after a record
moved tiles) keeps walking into that record's list, and a 2-cycle
loops forever.  The port matches (`while (v5 != 0)` + the
`next == v5` exit).

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> IDA `0x408438` (asm loop tail at the `word_52F0B4` compare).

*Re-checked 2026-11-18 against the IDA export: a 0 next-link terminates too (loop top `if (v5 == 0) return`); the "slot 0 gets processed" claim was wrong.*

---

## 62. Map-render invalid descriptor aborts ENTIRE linked-list walk

The per-tile drawer's (`0x408438`) two validation gates
(`Offset > 310000` and `word_5C9258 <= 0`), checked once for the
record's primary sprite and once for its overlay sprite, all
`return` from the function — NOT just the current record's iteration:

    if (Offset > 310000) {
        ++dword_58FEA8;
        return;       // <- exits 0x408438 entirely
    }

So a single bad primary OR overlay descriptor in the middle of a
multi-record tile causes all SUBSEQUENT records in that tile's list to
be skipped for that frame.  The same counter `dword_58FEA8` takes at
most one bump per call.

Worse, the OVERLAY-gate return skips the cursor restore.  By then the
drawer has saved the cursor to `dword_591508 / 591504` and moved
`dword_591524 / 591528` to the overlay anchor (`v13 + 18 / 24 / 4`,
`v12 - 21 / 18 / 4`); returning there leaves the cursor displaced.  The
row iterators (`0x405862` etc.) only add the cell pitch
(`591524 += 568220`) and reset x at the next row start, while y is
never reset (`591528 += 53F65C` per row) — so the rest of the row is
drawn shifted and every later row of the frame is shifted vertically.
The primary-gate return happens before the cursor is touched, so it
only drops the tile's remaining records.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> IDA `0x408438`; its callers `0x405487` (frame driver) / `0x405862` / `0x4059af` (row iterators).

*Re-checked 2026-11-18 against the IDA export: added the unrestored cursor on the overlay-gate return (one shared error counter, not two).*

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

The per-tile drawer's (`0x408438`) primary-sprite dirty mark selects
by the local kind `v6`:

    if (v6 == 3)      engine_dirty_rect_mark_tile_64(x, y);  // 0x40880e
    else if (v6 == 2) engine_dirty_rect_mark_tile_48(x, y);  // 0x408837 ← dead
    else              engine_dirty_rect_mark_tile_32(x, y);  // 0x408856

But the only writes to `v6` in `0x408438` set it to 0 (default) or 3
(record types 1/2/3/4).  Value 2 is never assigned, so the 48-px mark
for the primary sprite is UNREACHABLE (Hex-Rays drops the arm from its
pseudocode; it is present in the asm).  The 48-px helper IS reached
via the overlay path's post-dispatch mark (`0x408bed`), a different
call site with a different cursor (#63).

A remnant of an earlier version or a defensive guard never trimmed by
the compiler.  The port omits the dead arm
(`mark(kind == 3 ? 64 : 32)`) — behaviourally identical.

> 📍 `src/engine_map_render.c` (`engine_map_render_tile`),
> IDA `0x408438` (asm `0x4087f0..0x408856`).

*Re-checked 2026-11-18 against the IDA export: the dead arm confirmed in the asm; the port no longer keeps it.*

---

## 65. Overlay-pass phase0 row has NO left/right edge cells (asymmetric vs primary pass)

The strategic-mode OVERLAY pass's phase-0 row iterator (`0x405eb5`)
walks exactly `dword_53E8AC` (col_count) cells.  The PRIMARY pass's
phase-0 row iterator (`0x405862`) walks up to `col_count + 2`: one
extra cell at `col_start - 1` (gated on `dword_5651B4 > 0`) drawn at
`x = left - pitch`, plus one at `col_start + col_count` (gated on
`dword_56D598 < 64`).

The two iterators have nearly identical shapes — same camera, same
map array, same per-tile descriptor base — but the OVERLAY iterator
omits both edge-cell paths entirely.  The edge cells are the columns
just outside the VIEWPORT on either side; the `> 0` / `< 64` gates are
only map-bound guards.  So on every frame the primary layer draws the
off-view neighbour columns (whose sprites are wider than the pitch and
bleed into view) while the overlay layer (armies, path markers) does
not — an army standing one column outside the view is not drawn even
where its sprite would overlap the visible edge.

Suspect motivation: the primary pass's wide terrain/feature sprites
need the neighbours to fill the edge; the overlay sprites were judged
narrow enough not to.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_row_phase0`)
> vs `src/engine_map_render.c` (the phase-0 row), IDA `0x405eb5`
> (247 B) vs `0x405862` (333 B).

*Re-checked 2026-11-18 against the IDA export: the edge cells sit at the viewport edges every frame, not at the map edges.*

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

## 67. Overlay path-marker drawer's step-budget cap is asymmetric for non-positive costs

`0x4081a6` (ported as `engine_overlay_render_tile_city` — the name is
historical) draws the planned-move PATH markers: it runs on cells whose
descriptor +2 bit 0x40 is set, and that bit is set only by
`hover_mark_path_overlay` (`0x4a91ba`, one cell per path step of
player `DAT_0057c8cc`'s planned route) and cleared map-wide by
`0x46c6ba`.  It picks the marker sprite from the path-cost field
`word_500C30` (the local player's i16 64×64 grid, filled by
`engine_path_field_fill` `0x46f700`):

    v5 = word_500C30[cell / 8] - 1;
    if (rec[0x154] - rec[0x153] < v5)     /* rec = record DAT_0057c8c4 */
        v5 = 0;
    dword_5C9288 = flagged ? 78 : v5 + 56;

`rec` is the `g_per_territory_420` record of `DAT_0057c8c4`; +0x154 is
the step budget and +0x153 the path-progress accumulator (both signed
bytes, `movsx`; see `combat-resolution.md`).  A step whose cost exceeds
the remaining budget collapses to sprite 56.

The cap fires only on the high side.  A cost <= 0 (v5 < 0) passes
through unchecked, giving a sprite index below 56 (0x37 for cost 0) —
a different sprite of the same overlay sheet, not an out-of-bounds
read unless the cost is <= -55.  Whether a marked path cell can hold
cost 0 (e.g. the route's start cell) is not established; no error has
been observed.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_city`),
> IDA `0x4081a6` (cap at `0x40821d..0x408265`), `0x4a91ba`, `0x46c6ba`.

*Re-checked 2026-11-18 against the IDA export: 0x4081a6 is the path-marker drawer (bit 0x40 = planned path, word_500C30 = path-cost field, cap = selected record's step budget), not a city / size drawer; a negative index stays inside the sheet for costs > -55.*

---

## 68. Overlay path-marker drawer clears bit 0x40 of descriptor[+2] first thing

The very first instruction group of `0x4081a6` (`0x4081af..0x4081c4`)
clears bit `0x40` of the per-tile descriptor's +2 flag byte
(`byte_522F92[cell] &= 0xbf`) — BEFORE staging, the offset gate and the
dispatch, so the bit is consumed even when the sprite is rejected
(`++dword_58FEA8`) or clipped away.  This is mid-frame mutation of
descriptor table state.

The bit is the planned-path marker (#67): `hover_mark_path_overlay`
(`0x4a91ba`) sets it on every step cell of the planned route, and
`engine_territory_clear_cell_path_overlay` (`0x46c6ba`) clears it
map-wide.  So a marker cell is drawn once per arming; without a re-arm
from `0x4a91ba` the marker is not redrawn on the next frame.

Tests must re-arm the bit between frames to verify multi-frame
dispatch behavior.  See
`test_engine_overlay_render_error_count_persists_across_frames`.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_city`),
> IDA `0x4081a6`, `0x4a91ba`, `0x46c6ba`.

*Re-checked 2026-11-18 against the IDA export: the clear precedes the dispatch (also on error paths); the re-arm is 0x4a91ba, not save-load / city changes.*

---

## 69. Overlay pre-clear strip is view-mode-1-only

The driver `0x405602` (`engine_overlay_render_frame`) pre-clears a
4-pixel-wide vertical strip at column 0, rows `24..443`, by calling
`0x4b4056` (four bytes of colour 0 through `Buf` / `xRight`) in a
420-iteration loop at the very top of the function.  But this clear is
GATED on `dword_57CB18 == 1` — only view-mode 1 (zoom-mid) triggers
it.  View-modes 0 and 2 leave the strip undisturbed.

The strip's position corresponds to the leftmost column of the visible
map area in zoom-mid layout.  Suspected purpose: cleaning up stale
pixels from view-mode-transition artifacts that only manifest at this
zoom level.  Could also be a workaround for a clip-rect off-by-one
that only matters when the half-step is exactly 16 pixels (the zoom-
mid setting).

Quirk-worth because the same driver function changes behavior based
on a global mode — typical "load-bearing if" pattern.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_frame`, the
> loop calling `engine_overlay_render_fill4` — port of `0x4b4056`),
> IDA `0x405602`.

*Re-checked 2026-11-18 against the IDA export: behaviour confirmed; 📍 updated (no `pre_clear_left_strip` helper any more).*

---

## 70. Army drawer 0x40 path dispatches stale sprite for unhandled low-nibble

The army drawer's (`0x4071a0`) `descriptor[+1] & 0x40` branch
dispatches sub-branches keyed by `descriptor[+4] & 0x0f`:

  - low-nibble == 0 → fortress sprite (army record `+0x07`,
    `byte_53F9B7[768 * army]`; returns if it is 0), dirty kind 48
  - low-nibble == 2 → march-out sprite (fixed 129 = 0x81; returns if
    army `+0x1ad` is 0), no dirty mark
  - **low-nibble ∈ {1, 3, 4, …, 15}** → **no sprite index set, no
    offsets set, BUT control still reaches the dispatch tail
    (LABEL_118).**

The tail stages from the overlay sheet using `dword_5C9288`, which in
the unhandled-nibble case still holds whatever the last staging left —
the previous army / path-marker sprite of this frame, or the 999 /
overlay index the primary pass's `0x408438` leaves behind.  The cursor
offset is (0, 0) (minus `dword_567950` in x for a phase-1 cell), and
`v22` stays 0, so no dirty-rect mark is made for the draw.  If the
stale index stages an offset outside `(0, 70000]` the tail logs
`aErrTopItBadDat*` and latches `byte_5AEB68`.

Visually: a stale sprite drawn at the cell's cursor when a tile has
`descriptor[+1] & 0x40` set and `descriptor[+4] & 0x0f` outside
{0, 2}, without a dirty mark to erase it next frame.

The port reproduces it by leaving the global `g_gfx_last_glyph_index`
untouched on that path and falling into the same tail.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_army`,
> the block comment cites this quirk), IDA `0x4071a0` (the 0x40 branch,
> `if (v8 != 0) { if (v8 == 2) … }` with no else).

*Re-checked 2026-11-18 against the IDA export: confirmed; added the missing dirty mark / phase-1 x shift, port description updated (no gfx_dispatch_last_glyph_index() pull).*

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

The army drawer's (`0x4071a0`) "end-frame" branches — descriptor[+0]
categories 3, 6, 9 and 12 — write a fixed end-frame value to
`descriptor[+3]` (`byte_522F93[cell]`) BEFORE checking the army tier
byte / tick gates that decide whether to dispatch:

```c
byte_522F93[cell] = END_FRAME;               /* 46 / 9 / 19 / 29 */
if (army[tier] < 3 || dword_53E9A4 >= 16)    /* tier: +0x2ae / +0x2de / +0x2c6 / +0x296 */
    return;                                   /* no dispatch */
dword_5C9288 = dword_53E9A4 + 40;
/* → dispatch tail (LABEL_118), dirty kind 32 */
```

A failed gate leaves the descriptor in its end-frame state (0x2e /
0x09 / 0x13 / 0x1d) even though the tick+40 sprite for that frame was
never drawn.  The `descriptor[+1] & 0x10` branch (category 19 = 0x13)
uses the same `dword_53E9A4 + 40` sprite and tick gate but writes no
+3 byte.

The +3 byte is the frame the walk-cycle categories advance (#74), so
the committed value is what the next frame's walk starts from
regardless of whether the end frame was shown — units can appear to
"skip" the last frame while the army's tier byte is below 3.

> 📍 `src/engine_overlay_render.c` (`engine_overlay_render_tile_army`,
> the `k_band` table's `end_cat` / `end_frame` / `tier` columns),
> IDA `0x4071a0` (the `v15 == 3 / 6 / 9 / 12` arms).

*Re-checked 2026-11-18 against the IDA export: the 0x10 path writes no +3 byte; port helper `army_end_frame_band` replaced by the `k_band` table.*

---

## 73. Arrow-marker sub-branches silently filter 0x0F and 0x13 via byte underflow

The army drawer's (`0x4071a0`) `bit 0x20` (no 0x40, no 0x10)
directional-arrow branch is range-gated to `first_byte ∈ [0xf, 0x16]`,
then split at 0x13 into a high and a low sub-branch, each with an
unsigned-byte inner gate:

```c
if (v15 >= 0x13) { v17 = v15 - 20; if (v17 > 2u) return; … 6*v17 + 85 }
else             { v16 = v15 - 16; if (v16 > 2u) return; … 6*v16 + 103 }
```

For `v15 == 0x13` the high gate's `0x13 - 0x14` wraps to `0xff`; for
`v15 == 0x0f` the low gate's `0x0f - 0x10` wraps the same way.  Both
return.  Net effect: of the gated range only 0x10..0x12 and 0x14..0x16
draw — the two boundary values are FILTERED by byte underflow, not by
an explicit equality check.

The port uses the same `uint8_t` wrap (`(uint8_t)(v15 - 20) > 2`).

Practical note: 0x13 is the category the `bit 0x10` branch draws (#72);
the arrow branch is selected by descriptor[+1] bits, not by the
category, so the filter only matters for a cell with bit 0x20 and
category 0x0f / 0x13.

> 📍 `src/engine_overlay_render.c`
> (`engine_overlay_render_tile_army`, the 0x20 branch),
> IDA `0x4071a0`.

*Re-checked 2026-11-18 against the IDA export: 0x0F is filtered by the low arm's underflow too; the port uses the byte wrap, not a signed delta test.*

---

## 74. Animation-tier flag is a GLOBAL toggle, not a per-army timer

The four animation-rate "tier" flags (`dword_58FD08`, `dword_57D3C8`,
`dword_57D3AC`, `dword_591510`) are SINGLE GLOBAL booleans, not
per-army or per-tile state.  The army drawer's (`0x4071a0`) walk-cycle
categories (descriptor[+0] 0, 2, 5, 8, 11 — each band's values other
than its skip category 1 / 4 / 7 / 10 and end category 3 / 6 / 9 / 12)
compute
`distance = army[cur] - army[prev]` for the cell's army record
(`+0x2b8 - +0x2bc`, `+0x2e8 - +0x2ec`, `+0x2d0 - +0x2d4`,
`+0x2a0 - +0x2a4` per band), then pick a tier by `distance` bin — but
the tier flag itself is a frame-level toggle shared by every army in
that bin.

When the selected flag is non-zero, the cell's frame byte
(descriptor[+3]) advances by 1 (wrapping per band); when it is zero,
nothing advances.  The "distance" computation exists only to BIN
armies into rate tiers; the actual time-base is shared.

The four tiers map to the 640/320/160/80-ms phase ticks:

  distance <  10   → `dword_58FD08` (640 ms — slowest)
  distance < 0x19  → `dword_57D3C8` (320 ms)
  distance < 0x32  → `dword_57D3AC` (160 ms)
  distance ≥ 0x32  → `dword_591510` ( 80 ms — fastest)

So slow-moving armies (small cur-prev gap) cycle their sprite
sequence more slowly than fast-moving ones.

The four flags are produced by `0x4bbc80` (`engine_compute_phase_ticks`,
the 9-flag phase-tick generator: a base tick every >= 20 ms of
`timeGetTime`, `dword_591510` every 4 base ticks, `57D3AC` every 8,
`57D3C8` every 16, `58FD08` every 32), all cleared at its entry.  The
port's `engine_tick_phase_step` (`src/engine.c`) forwards them to
`engine_overlay_render_set_animation_rate_flags`.

> 📍 `src/engine_overlay_render.c` (`rate_gate`, `walk`,
> `engine_overlay_render_set_animation_rate_flags`),
> IDA `0x4071a0` (the four walk arms); driver `src/engine_phase.c`
> (`engine_compute_phase_ticks`), `src/engine.c`
> (`engine_tick_phase_step`), IDA `0x4bbc80`.

*Re-checked 2026-11-18 against the IDA export: behaviour confirmed; 📍 updated (`pick_rate_tier` is now `rate_gate` / `walk`), tick periods stated from 0x4bbc80.*

---

## 75. 16×16 raw sprite blit has a HARDCODED 640-byte dest row stride

`0x4b49f7` (ported as `gfx_sprite_blit_16x16_raw`) computes its
destination row stride from compile-time dword indices, NOT from the
runtime canvas pitch.  Each row writes `v2[160 * row + 0..3]` where
160 dwords = 640 bytes — the standard 640×480 back-buffer pitch.  In
contrast, the destination *origin* is
`Buf + dword_591524 + xRight * dword_591528` — which DOES read the
dynamic pitch global `xRight` (`0x4eb274`).

So the origin respects whatever pitch is bound, but the per-row
stride is locked.  If the bound pitch were anything other than 640,
rows past the first would not line up with the canvas's scanlines.
The 32×32 sibling `0x4b5333` (`gfx_sprite_blit_32x32_raw`) has the
same fixed stride.

The function works correctly in retail because `xRight` is 640 at all
its call sites: the zoom-16 battlefield terrain pass `0x4bc8fc` and
the UI helpers `0x40914b`, `0x40927a`, `0x4097c5`, `0x409c93`,
`0x40cfd2`.  The mismatch is a latent footgun, preserved verbatim in
the port (16 rows × 16-byte `memcpy` with literal `dst += 640`).
Tests use a 640-wide canvas to match retail's deployment.

> 📍 `src/gfx_sprite.c` (`gfx_sprite_blit_16x16_raw`,
> `gfx_sprite_blit_32x32_raw`), IDA `0x4b49f7` (705 B), `0x4b5333`.

*Re-checked 2026-11-18 against the IDA export: caller list corrected (0x40914b / 0x40927a / 0x4097c5 / 0x409c93 / 0x40cfd2 / 0x4bc8fc); 0x4b5333 shares the quirk.*

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

The port mirrors the three-path structure via a local `boundary`
int (1 = mark, 0 / -1 = the gates that jump to LABEL_4) rather than
goto, but the behavior is byte-equivalent.

Note the second-sprite path itself has no `Offset` gate: once the mode
byte passes, the `desc[+0] + 0x8b` / 0x9a sprite (#78) is staged and
dispatched through clip_x / clip_y unchecked.

> 📍 `src/engine_battlefield_render.c` (`pass_terrain_zoom32`),
> IDA `0x4bcbdc` (advance `loc_4BCC53`).

*Re-checked 2026-11-18 against the IDA export: the three paths and abort gates confirmed; the port's flag is `boundary`, not `do_boundary`.*

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
the terrain pass (`0x4bcbdc`) AND the overlay pass (`0x4bd07f`) to
decide whether to draw — with opposite polarity:

  - Terrain-z32: SKIPS a dirty / forced cell when the bit is set
    (straight to the advance: no dirty clear, no draw, no boundary).
  - Overlay-z32: REQUIRES the bit (and dirty / forced) to draw.

Net effect: per dirty cell, exactly one of the two zoom-32 passes runs
its drawing block in a given frame, picked by the bit.  Both passes
draw the cell's descriptor[+3] sprite from the primary sheet
(`dword_4E5D78`) as a 32×32 raw blit; terrain may instead pick the alt
sheet by the +2 selector and adds a second sprite.

Order matters: `engine_battlefield_render_frame` (`0x4bc142`) runs
terrain → `pass_middle` (the units) → overlay, so bit-4 cells are
drawn AFTER the units — a foreground layer that occludes units
standing on them, not just "a feature instead of terrain".

Dirty-bit handling: each pass clears bit 0 of +2 as soon as it
commits to the cell, BEFORE its `Offset` gate — the terrain pass also
when the +2 selector is outside {0, 4} and nothing is drawn (#76),
the overlay pass also when the blit is rejected (#81).  A dirty bit-4
cell gets no boundary mark (+2 |= 2) that frame — the overlay pass has
no boundary step; a CLEAN bit-4 cell is still marked by the terrain
pass's clean path.

This explains the skip-bit path of terrain-z32's quirk #77 (path 3):
that cell ISN'T abandoned — overlay-z32 picks it up later the same
frame.

> 📍 `src/engine_battlefield_render.c` (`pass_terrain_zoom32`,
> `pass_overlay_zoom32`), IDA `0x4bcbdc`, `0x4bd07f`, `0x4bc142`.

*Re-checked 2026-11-18 against the IDA export: both passes can clear the dirty bit without drawing (the old "only when it draws" was wrong); the overlay pass runs after pass_middle, over the units.*

---

## 80. (withdrawn) Battlefield zoom-32 glyph_offset gate constant is 0x4baf0, NOT 310000

Claimed retail's gate constant `0x4baf0` is 309488, so the decompiler's
`310000` was a 512-value off-by-one.  Wrong: `0x4baf0` = 310000
exactly (`0x40000 + 0xbaf0` = 262144 + 47856).  The asm at `0x4bcd76`
(`cmp Offset, 4BAF0h; jle`) and `0x4bd1ef` is the plain
`0 < Offset <= 310000` gate used by every sprite wrapper; the port's
`310000` / `0x4baf0` spellings are the same value and the "fix" changed
nothing.

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
column converter (band = `DAT_004e5d3c` = zoom/4, set by
`engine_battlefield_render_bind_hex_grid_context` 0x4bc020):

  - `0x4bc2e5` — right-biased (residue ≥ band*3 → col+=1).
    Clamps low only: `col <= 0 → 0`.
  - `0x4bc346` — left-biased  (residue < band  → col-=1).
    Clamps high only: `col >= DAT_004e6578 (row stride) → row_stride - 1`.

Neither variant clamps both sides.  The left-biased function returns
-1 for a pixel just left of the viewport (C truncation: `(px-vx)/zoom`
= 0, residue negative < band → col-1); the right-biased one lets a
high column through unclamped.  The row pair `0x4bc3ac` (bottom-biased,
low clamp) / `0x4bc40d` (top-biased, high clamp against `DAT_004e6570`)
has the same shape.

The asymmetry is tuned to the only caller, `0x479b58` (the battlefield
rubber-band selection, called from 0x43c247): after sorting the box
corners it takes the LEFT edge through the right-biased helper and the
RIGHT edge through the left-biased one (and the rows likewise), so each
edge rounds inward and is clamped only on the side it can overrun.  A
box entirely off one side yields `first > last` and the cell loop
(`for j = first; j <= last`) runs zero times — the missing clamp is
never used as an index.  Don't substitute either variant for the other
or treat them as a generic "clip into grid" primitive.

> 📍 `src/engine_battlefield_render.c`
> (`engine_battlefield_render_pixel_to_col_left_biased`,
> `engine_battlefield_render_pixel_to_col_right_biased`, `…_row_top_biased`,
> `…_row_bottom_biased`), IDA `0x4bc346` / `0x4bc2e5` / `0x4bc3ac` / `0x4bc40d`,
> caller `sub_479B58`.

*Re-checked 2026-11-18 against the IDA export: caller surveyed (0x479b58 box select, sole caller — the asymmetry is the inward rounding of a box's edges), row pair added, port names updated.*

---

## 83. Battlefield middle pass iterates an OVER-SCAN extension of the visible grid

The middle pass (`0x4bd355`) does NOT iterate the visible grid like
its terrain / overlay siblings.  After building / sorting / drawing the
entity list (0x4bd938 / 0x4bda92 / 0x4bdb95) it walks a cell window
that starts ONE cell west / ONE cell north of the camera and spans
`(cols + 2) × (rows + 2)` cells, clamped against the grid extent
(`DAT_004e6578` row stride for cols, `DAT_004e6570` total rows):

```c
c0 = DAT_004e657c - 1;  x0 = DAT_004e6558 - DAT_004e656c;   // cam_x - 1, viewport_x - zoom
if (c0 < 0) { c0 = 0; x0 = DAT_004e6558; }                 // west edge: start at col 0
r0 = DAT_004e6580 - 1;  y0 = DAT_004e6554 - DAT_004e656c;
if (r0 < 0) { r0 = 0; y0 = DAT_004e6554; }
cols = DAT_004e5d5c + 2;
if (c0 + DAT_004e5d5c + 2 >= DAT_004e6578) cols = DAT_004e6578 - c0;
rows = DAT_004e5d4c + 2;
if (r0 + DAT_004e5d4c + 2 >= DAT_004e6570) rows = DAT_004e6570 - r0;
```

Unclamped, the window is symmetric: cells `cam-1 .. cam+cols` — one
cell of slack on each side (visible cells are `cam .. cam+cols-1`).
Only at the west / north edge, where the start clamps to 0 but the
count stays `cols + 2`, does it reach TWO cells past the east / south
edge.  The per-cell leaves (player marker, unit key, effect chain) draw
sprites that can overhang a neighbouring cell, so the one-cell rim
lets a marker anchored just off-screen still paint its visible part;
terrain / overlay sprites are exactly one cell and iterate only the
visible cells.  The cursor advances by `DAT_004e656c` (zoom) per cell,
unlike the late pass (quirk #97).

The west / north start cells have NEGATIVE pixel coordinates relative
to the viewport's top-left (`x0 = viewport_x - zoom`); the leaves rely
on the clip rect (`0x40464c` / `0x40477b` against `DAT_004e6564`..) to
suppress the off-screen part.

> 📍 `src/engine_battlefield_render.c` (`pass_middle`, binder
> `engine_battlefield_render_bind_total_rows`), `docs/findings/battlefield-render.md`,
> IDA `0x4bd355`.

*Re-checked 2026-11-18 against the IDA export: the over-scan is +1 on every side (not +1 west / +2 east); +2 east/south only when the start clamps at the west/north edge; speculative "+2 because sprites poke 32 px" reasoning removed.*

---

## 84. Battlefield middle pass re-dirties cell only on player-marker branch

The middle pass's (`0x4bd355`) per-cell dispatch on
`descriptor[+2] & 0x80` has TWO branches that differ in ONE side-effect:

```c
if ((int8_t)descriptor[+2] < 0) {
    if (descriptor[+3] != 0) {
        0x4bd759(descriptor[+3]);             // unit-key letter
        /* no re-dirty */
    } else {
        0x4bd574();                           // player marker
        descriptor[+2] |= 1;                  // RE-DIRTY (!)
    }
}
```

Only the **player-marker branch** sets bit 0x01 (re-dirty).  The
unit-key branch does NOT.

Why: the player marker is an **animated sprite** — frame
`DAT_004e5b18` (kept in 0..7) of the marker player's slot group
(`DAT_0052f0b2[DAT_00568218 * 0x1a4] * 8 + 33 + frame`).  The frame
counter advances only when the animation gate `DAT_00591510` is set
(the 80 ms tick), once per marker drawn — it is one shared counter, so
two markers in view each step it.  Re-dirtying the cell makes the
terrain pass repaint it next frame (erasing the previous marker frame)
before the middle pass draws the next one; without it the cell would
only repaint when something else dirtied it.

The unit-key sprite is static.  `0x4bd759` accepts only four key bytes
— `'I' 'L' 'a' 'd'` (0x49 0x4c 0x61 0x64) → frames 31..34 of
`DAT_00553250`; any other byte returns before drawing (switch default).

The OR is a read-modify-write of the flag byte after the leaf returns;
neither leaf writes the descriptor, so it is equivalent to setting the
bit before the call.

> 📍 `src/engine_battlefield_render.c` (`pass_middle`,
> `engine_battlefield_render_player_marker`, `engine_battlefield_render_unit_key`),
> IDA `0x4bd355` / `0x4bd574` / `0x4bd759`.

*Re-checked 2026-11-18 against the IDA export: the frame advance is gated by DAT_00591510 (not every call), marker slot formula added, unit-key filter confirmed, stale "chip 5a.i stub" note dropped.*

---

## 85. Battlefield visible-entity build filter window is INCLUSIVE on both ends

The visible-entity build helper (`0x4bd938`) walks entity ids 1..80
(records `0x554480`, 0x1b0 each) and includes each live one (`+0x2c`
byte, `0x5544ac`, non-zero) whose hex coords (`+0x20` / `+0x22`) fall in

```c
ex ∈ [cam_x - 1, cam_x + cols]    (inclusive both ends)
ey ∈ [cam_y - 1, cam_y + rows]    (inclusive both ends)
```

```c
if (byte_5544AC[432*i] != 0
    && word_5544A0[216*i] >= cam_x - 1 && word_5544A0[216*i] <= cam_x + cols
    && word_5544A2[216*i] >= cam_y - 1 && word_5544A2[216*i] <= cam_y + rows)
```

Both upper checks are `<=`, so an entity exactly one cell past the
visible east / south edge is in the list.  This matches the middle
pass's own over-scan cell window (quirk #83: `cam-1 .. cam+cols`
unclamped) — not the terrain / overlay passes, which walk only the
visible cells.  Those edge entities can have sprite pixels reaching
back into the viewport.

The build helper does NOT clamp the window against the grid extent the
way the middle pass's cell loop does; an entity outside the 80×80 grid
can't exist (the simulator places entities on cells), so the missing
clamp is not observable.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_build_list`),
> `docs/findings/battlefield-render.md` §5a.ii.a, IDA `0x4bd938`.

*Re-checked 2026-11-18 against the IDA export: the matching over-scan is the middle pass's (#83), not terrain/overlay's; port function renamed.*

---

## 86. (withdrawn) Battlefield visible-entity sort uses an in-band sentinel as right-edge max

Claimed the bubble sort `0x4bda92` reads the `(id 0, y 9999)` sentinel the build helper `0x4bd938` writes at
index `count`.  It does not: the inner loop is `for (i = 0; i < high; i++)` comparing `y[i+1] < y[i]` with
`high = count - 1` (asm 0x4bdaee..0x4bdb12: `cmp var_C, i; jle`), so the last index read is `count - 1`; no
function reads the sentinel (data_xrefs: only 0x4bd938 touches index `count`).  True and kept: the swap is strict
`<` (`jge` skip at 0x4bdb12), so equal-y entities keep id order; the 500-pass cap never binds (≤ 79 passes).

---

## 87. Battlefield per-entity draw type-6 PRE-call fires even when blob is NULL

The per-entity draw `0x4bdc31` has a strict top-to-bottom order:

```c
if (entity[+0x12] == 6) 0x4be4df(id);       // pre-call (TYPE-6)

blob = entity[+0x00];
if (blob == 0) {
    0x4afab9(aErrTileMapShow, 0, type + 1);  // log "no blob"
    return;                                   // <— skips type-7/9 too
}
... stage frame entity[+0x10]; if (0 < Offset <= 310000) { main draw, post-calls }
```

The type-6 PRE-call is **outside** the no-blob gate.  An entity of
type 6 with a NULL primary sprite blob still gets `0x4be4df` called —
only the main draw + type-7 + type-9-subtype-0xe post-calls are
skipped.

`0x4be4df` is the SECONDARY draw: it uses a DIFFERENT sheet field
(`entity[+0x04]`, frame `entity[+0x11]`, a 6×6 dirty mark), so even
when the primary blob is missing the secondary can still render.
Treating the PRE-call as part of the main-draw gate would silently lose
it for entities whose primary asset failed to load.

The type-7 (`0x4be7be`) / type-9-subtype-0xe (`0x4beab9`) POST-calls
sit **inside** both the no-blob branch AND the glyph-offset gate, after
the cursor restore — so an out-of-band offset OR a missing primary blob
suppresses all three.  Secondary draws survive primary failures;
type-specific overlays only fire when the primary draw happened.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_entity`,
> `engine_battlefield_render_entity_type6`), `docs/findings/battlefield-render.md`
> §5a.ii.b, IDA `0x4bdc31`.

*Re-checked 2026-11-18 against the IDA export: confirmed; port function names updated.*

---

## 88. Battlefield per-entity draw reads uninitialised stack at unsupported zoom

`0x4bdc31` selects the per-entity centering + animation offset by zoom:

```c
if (DAT_004e656c == 0x20) {
    dx = 16 - (w >> 1) + DAT_004e4030[34*facing + 2*frame];
    dy = 16 - (w >> 1) + DAT_004e4034[34*facing + 2*frame] - 8;
} else if (DAT_004e656c == 0x10) {
    dx =  8 - (w >> 1) + DAT_004e3bf0[...];
    dy =  8 - (w >> 1) + DAT_004e3bf4[...] - 4;
}
DAT_00591524 += dx;   // <— uninitialised at any other zoom
DAT_00591528 += dy;
```

There is NO else arm — at any other zoom both locals are read
uninitialised (stack residue).  The type-6 / type-7 leaves
(`0x4be4df`, `0x4be7be`) have the same shape.

Unreachable in retail, more strongly than "the game only uses 16 or
32": the zoom global `DAT_004e656c` has one writer,
`engine_battlefield_render_bind_hex_grid_context` (0x4bc020), and both
of its callers pass the literal `0x20` (`push 20h` at 0x498d2b /
0x498f47, before the calls at 0x498d4c / 0x498f68).  So the battlefield
always runs at zoom 32 and even the zoom-16 arm is dead.

The port's `entity_offset` initialises both offsets to 0 in the
missing-else arm — unobservable, since the arm is never taken.

> 📍 `src/engine_battlefield_render.c` (`entity_offset`,
> `engine_battlefield_render_entity`), `docs/findings/battlefield-render.md`
> §5a.ii.b, IDA `0x4bdc31` / `0x4bc020`.

*Re-checked 2026-11-18 against the IDA export: zoom is always 0x20 (sole writer 0x4bc020, both callers push 0x20), so the zoom-16 arm is dead too; the "middle pass keeps the gate alive" remark dropped; port names updated.*

---

## 89. Battlefield per-entity dirty-rect box size threshold uses entity type, not sprite size

`0x4bdc31` marks a dirty box AFTER applying the anim offset + the
type-9 (+12) / type-10 (+8) Y bonus but BEFORE dispatching the blit.
Box size is picked by entity TYPE (`entity[+0x12]`), not sprite
dimensions:

```c
if ((uint8_t)entity[+0x12] < 7)
    engine_dirty_rect_mark_box_tiled(cursor_x, cursor_y, 5, 5, 2);   // 0x4527a6
else
    engine_dirty_rect_mark_box_tiled(cursor_x, cursor_y, 9, 9, 2);
```

`0x4527a6` marks `x .. x + 16*w`, `y .. y + 16*h` (negative origins
clamped to 0), so 5×5 covers 80×80 pixels and 9×9 covers 144×144.
Types 0..6 get 5×5; types ≥ 7 get 9×9.

- The box size is hardcoded; over-marking is preferred to sprite-exact
  marking.
- The last argument `2` sets the escalation global `DAT_0050d09c = 2`
  (`if (a5 > 1)`), so any drawn entity escalates the frame's mark to 2.
- The box is anchored at the FULLY-ADJUSTED cursor (anim offset +
  type-9/10 bonus), NOT at the entity's cell origin.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_entity`),
> `docs/findings/battlefield-render.md` §5a.ii.b, IDA `0x4bdc31` / `0x4527a6`.

*Re-checked 2026-11-18 against the IDA export: confirmed (tile = 16 px, escalation global DAT_0050d09c); port function name updated.*

---

## 90. Battlefield effect-chain glyph_offset gate aborts the WHOLE chain walk

`0x4beed4` walks a linked list of visual-effect records (`0x57a100`,
0x4c each; `+4` next id) anchored to a battlefield cell (head =
`descriptor[+6]`), at most 10 links; it stops on a dead record
(`+8 == 0`) or a self-link.  Per record it stages frame `+0x12` of the
record's sheet (`+0`) and gates the blit on `0 < Offset <= 0x4baf0`
(= 310000 — the same gate as every other battlefield path).  Here the
gate's failure path is a `return` rather than a `continue`:

```c
if (Offset > 310000 || Offset <= 0)
    return;       // asm 0x4bf06d..0x4bf08f → 0x4bf1f4: skips the restore at 0x4bf1e4
```

Effects:

- A single bad record at position N silently drops the rest of the
  chain this frame.  The walk restarts from the cell's head next frame,
  so it keeps being dropped until the bad record changes.
- The epilogue that restores the saved cursor (`DAT_00591524 /
  DAT_00591528 = saved`, 0x4bf1e4) is skipped.  This is NOT a no-op:
  every earlier drawn link SETS the cursor absolutely
  (`scroll_pad + x + viewport`), so when link N ≥ 2 fails, the cursor
  is left at link N-1's position.  The middle pass (`0x4bd355`) then
  keeps advancing that cursor by `zoom` for the rest of the row, and
  `cursor_y` (reset only at the pass start) stays wrong for every later
  row — later markers / unit keys / chains in the frame draw displaced.
  Only when the FIRST link fails (or kind 7 links precede it) is the
  cursor still the caller's.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_effect_chain`,
> its QUIRK comment), `docs/findings/battlefield-render.md` §5a.iii, IDA `0x4beed4`.

*Re-checked 2026-11-18 against the IDA export: 0x4baf0 is 310000 (not 309488); the skipped cursor restore is observable (an earlier link's position leaks into the middle pass); port name updated.*

---

## 91. Battlefield player_marker / unit_key re-center cursor by sprite WIDTH on both axes

`0x4bd574` (player marker) and `0x4bd759` (unit key) each re-center
the cursor by `(16 - sprite_w/2)` BEFORE drawing.  Both axes use the
SAME `sprite_w` (`DAT_005c9258`) term — the Y re-center does NOT use
`sprite_h` (`DAT_005aeb70`):

```c
DAT_00591524 += 16 - (DAT_005c9258 >> 1);   // cursor_x
DAT_00591528 += 16 - (DAT_005c9258 >> 1);   // cursor_y — same term
```

For square sprites this is harmless; a non-square sprite would be
mis-centred vertically.  The slots: unit keys are frames 31..34
(0x1f..0x22) of `DAT_00553250`; the marker is frame
`DAT_0052f0b2[player * 0x1a4] * 8 + 33 + (0..7)` of `DAT_00566518` — the
range depends on the marker player's slot byte (0x21..0x28 for slot
1).  Whether every one of those frames is square has not been checked
against the sheets.

This is the cluster's convention, not a one-off: the per-entity draw
(`0x4bdc31`) and its type-6 / type-7 / type-9-0xe leaves (`0x4be4df`,
`0x4be7be`, `0x4beab9`) also centre BOTH axes with
`16 - (DAT_005c9258 >> 1)` (plus an anim offset, and `- 8` on Y for the
entity draw).

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_player_marker`,
> `engine_battlefield_render_unit_key`, `entity_offset`),
> `docs/findings/battlefield-render.md` §5a.iii, IDA `0x4bd574` / `0x4bd759` / `0x4bdc31`.

*Re-checked 2026-11-18 against the IDA export: the per-entity draw shares the width-on-both-axes centering (the entry said it did not); marker slot range depends on the player's slot byte; port names updated.*

---

## 92. Battlefield effect-chain dirty-box branch is identical in both arms

`0x4beed4` gates its dirty-box marking on the record's kind
(`+9`, `0x57a109`):

```
004bf0fc  cmp  ecx, 5
004bf0ff  jnz  loc_4BF124
004bf105  push 2 / push 4 / push 4 / push y / push x
004bf117  call engine_dirty_rect_mark_box_tiled      ; kind 5
004bf11f  jmp  loc_4BF13E
004bf124  push 2 / push 4 / push 4 / push y / push x
004bf136  call engine_dirty_rect_mark_box_tiled      ; anything else
```

Both arms call `0x4527a6` with the SAME arguments (4×4 tiles,
escalation 2).  The conditional is dead code — a leftover from an
earlier version where kind 5's box differed.  IDA's pseudocode already
folds the two arms into one call (so it is only visible in the asm);
the port folds it too.  Kind 5's real difference is elsewhere: it adds
the 16-step jitter `DAT_004e4470[DAT_004e5b1c]` to the position.

Not behaviorally observable; documented because a reader of the asm
might otherwise read the branch as meaningful.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_effect_chain`),
> `docs/findings/battlefield-render.md` §5a.iii, IDA asm `0x4bf0fc`..`0x4bf13b`.

*Re-checked 2026-11-18 against the IDA export: confirmed in the asm (pseudocode folds it); speculative histories dropped; port name updated.*

---

## 93. Battlefield type-9-subtype-0xe leaf reads z32 anim table even at zoom 16

`0x4beab9` (the type-9 / subtype-0x0e POST-call of `0x4bdc31`) reads
its animation offset from `DAT_004e4030` / `DAT_004e4034` (the zoom-32
table) UNCONDITIONALLY, for both of its sprites, and centres with the
zoom-32 constant 16:

```c
DAT_00591524 += DAT_004e4030[34*facing + 2*frame];   // z32 table, no zoom check
DAT_00591528 += DAT_004e4034[34*facing + 2*frame];
DAT_00591524 += 16 - (w >> 1);
```

The sister leaves `0x4be4df` (type 6) and `0x4be7be` (type 7) — and the
entity draw `0x4bdc31` itself — branch on `DAT_004e656c == 0x20 /
0x10` and pick `DAT_004e4030` or the zoom-16 table `DAT_004e3bf0`.

The missing check is never exercised: the battlefield zoom
`DAT_004e656c` is written only by `0x4bc020`, whose two callers pass
`0x20` (0x498d2b / 0x498f47), so every battlefield frame is zoom 32 and
the z32 table is the right one.  The port keeps the unconditional z32
read verbatim.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_entity_type9e`),
> `docs/findings/battlefield-render.md` §5a.iv, IDA `0x4beab9`.

*Re-checked 2026-11-18 against the IDA export: zoom is always 0x20 (sole writer 0x4bc020), so the zoom-16 case cannot occur; adjacency speculation dropped; port name updated.*

---

## 94. Battlefield type-9-sub-0xe second sprite anchored at SAVED entry cursor

`0x4beab9` draws TWO sprites per call (frames `entity[+0x11] + 22` and
`+ 13` of the entity's sheet `entity[+0]`).  Between the two, the leaf
RESETS the cursor to the cursor saved at entry rather than continuing
from the first sprite's adjusted cursor:

```c
// first sprite:  cursor = entry + anim + (16 - w1/2, 16 - w1/2) + (0, 12 - 44)
// ... 6×6 mark, clip, RLE blit ...
DAT_00591524 = saved_x;  DAT_00591528 = saved_y;
// second sprite: cursor = entry + anim + (16 - w2/2, 16 - w2/2) + (0, 12 + 58)
```

Both sprites share the same "anchor + anim offset + width centering"
base, with different Y biases: `16 + 12 - 44 = -16` above the anchor
for the first, `16 + 12 + 58 = +86` below for the second — 102 px apart
(plus `(w1 - w2)/2` if the two frames differ in width).  A two-tier
sprite (top piece / bottom piece).

Two more properties of the shape:
- The second sprite is staged and drawn only INSIDE the first sprite's
  `0 < Offset <= 310000` gate — a bad first frame suppresses both.
- The cursor is restored after each sprite, but not when the SECOND
  frame fails its gate (it is still the saved value then, so no leak).

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_entity_type9e`),
> `docs/findings/battlefield-render.md` §5a.iv, IDA `0x4beab9`.

*Re-checked 2026-11-18 against the IDA export: confirmed; nested gate and frame indices added; port name updated.*

---

## 95. Battlefield late-pass creature draw reads z32 anim table at zoom 16 too

The late-pass per-cell creature leaf (`0x4be070`) indexes the zoom-32
anim-offset table (`DAT_004e4030` / `DAT_004e4034`) UNCONDITIONALLY —
no `if (zoom == 0x20)` gate:

```c
dx = DAT_004e4030[34 * entity[+0x18] + 2 * entity[+0x32]];
dy = DAT_004e4034[34 * entity[+0x18] + 2 * entity[+0x32]];
```

The middle-pass per-entity draw (`0x4bdc31`) gates the same lookup by
zoom.  Like the late pass's hardcoded 0x20 cell pitch (quirk #97), this
is a zoom-32-only assumption — and it is never violated: the
battlefield zoom `DAT_004e656c` is written only by `0x4bc020`, whose
two callers pass `0x20`.  At the only zoom retail uses, the z32 table
is correct.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_creature`),
> `docs/findings/battlefield-render.md` §5c, IDA `0x4be070`.

*Re-checked 2026-11-18 against the IDA export: the "at zoom 16 it inherits z32 offsets" scenario cannot happen (zoom is always 0x20); port name updated.*

---

## 96. Battlefield late-pass creature draw lopsided cursor restore on gate fail

`0x4be070` saves the entry cursor, applies a type-conditional offset
(anim offset + 8 on X; −12 on Y for types < 7 and type 10, −32 for
other types ≥ 7, +16 back for type 9), stages frame
`46 + (entity[+0x1a0] shifted by DAT_00566514)` of `DAT_005530c8`, then
gates on `0 < Offset <= 310000`.  On gate PASS it draws and restores the
entry cursor; on gate FAIL it returns with the cursor still at the
POST-OFFSET value:

```c
saved_x = DAT_00591524; saved_y = DAT_00591528;
DAT_00591524 += dx + 8; DAT_00591528 += dy - 12 /* or -32 / -16 */;
if (0 < Offset && Offset <= 310000) {
    ... clip + blit ...
    DAT_00591524 = saved_x; DAT_00591528 = saved_y;
}
/* fail: no restore */
```

The late-pass driver (`0x4bd233`) resets `cursor_x` at the start of
each ROW but only ADVANCES `cursor_y` (`+= 0x20`) — so a gate fail in
row N shifts the Y of every creature drawn in later rows of the frame
(and X for the rest of row N).  The selection marker (`0x4be33f`) sets
its cursor absolutely and is unaffected.  Valid creature frames always
pass the gate, so this is defensive territory.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_creature`,
> `pass_late`), `docs/findings/battlefield-render.md` §5c, IDA `0x4be070` / `0x4bd233`.

*Re-checked 2026-11-18 against the IDA export: confirmed; offsets/frame spelled out; port name updated.*

---

## 97. Battlefield late-pass cell pitch is HARDCODED 0x20 regardless of zoom

`0x4bd233`'s per-cell cursor advance is a hardcoded `+ 0x20`:

```c
for (col = 0; col < DAT_004e5d5c; ++col) {
    ...
    DAT_004e6588 += DAT_004e5d50;
    DAT_00591524 += 32;            // HARDCODED 32 px pitch
}
DAT_004e6588 += DAT_004e5d70;
DAT_00591528 += 32;                // per row
```

The middle pass (`0x4bd355`) advances by `DAT_004e656c` (zoom); the
late pass, the zoom-32 terrain / overlay passes, the selection marker
`0x4be33f` (`32 * (col - cam_x) + 2`) and the creature leaf's z32 table
(quirk #95) all assume 32.  The frame driver `0x4bc142` calls the late
pass with no zoom branch.

This never misaligns: the battlefield zoom `DAT_004e656c` is written
only by `engine_battlefield_render_bind_hex_grid_context` (0x4bc020),
and both callers pass `0x20` (0x498d2b / 0x498f47) — retail's
battlefield is always zoom 32, and the whole zoom-16 side of the
cluster (0x4bc8fc, 0x4bcbd1, the z16 anim table) is dead code.  The
hardcoded pitch is a latent assumption, not a visible bug.

> 📍 `src/engine_battlefield_render.c` (`pass_late`,
> `engine_battlefield_render_selection_marker`), `docs/findings/battlefield-render.md`
> §5c, IDA `0x4bd233` / `0x4be33f` / `0x4bc020`; `tests/probes/zoom16.probe` (the dead zoom-16 rows).

*Re-checked 2026-11-18 against the IDA export: zoom is always 0x20, so the "half-density at zoom 16" effect and the "zoom 16 = strategic overview" speculation are moot.*

---

## 98. (withdrawn) Battlefield final-pass three-way bucket has unreachable branches

Claimed the byte sum `0x440089(0x19e, entity + 0x12)` is a signed char, so the `> 100` / `> 200` buckets of
`0x4bf1f9` are dead.  Wrong: `0x440089` returns `mov al, [sum]` with EAX's upper bytes already 0 (its loop exit
leaves `eax = [arg_0] = 0`, 0x44009d / 0x4400cd), and `0x4bf1f9` stores the full EAX (0x4bf2f6) — the value is the
unsigned 0..255 byte sum.  All three buckets are reachable: ≤ 100 → pen 0x3f, 101..200 → `-100`, pen 0xf9,
201..255 → `-200`, pen 0x20; only the `!= 999` filter is dead.  (The pass runs only with the debug flag `DAT_005bb4a5`.)

---

## 99. Battlefield final-pass cell pitch IS zoom-aware (sibling of quirk #97)

`0x4bf1f9` (the debug-label pass, active only while `DAT_005bb4a5 != 0`)
computes its per-cell label position inline from column/row:

```c
gfx_text_label_render_dec(value, ' ', &DAT_004e4520,
    DAT_004e6558 + DAT_004e656c * col + 1,   /* zoom-aware */
    DAT_004e6554 + DAT_004e656c * row + 1,
    &DAT_005af8f0, pen);
```

Unlike the late pass's literal `0x20` (quirk #97), this pass multiplies
by the zoom global.  The contrast is only stylistic: the zoom
`DAT_004e656c` is always 0x20 in retail (sole writer 0x4bc020, both
callers pass 0x20), so both passes place their output identically.

> 📍 `src/engine_battlefield_render.c` (`pass_final`),
> `docs/findings/battlefield-render.md` §5d, IDA `0x4bf1f9`.

*Re-checked 2026-11-18 against the IDA export: zoom is always 0x20, so #97 is not a "localized bug" this pass avoids; debug gate DAT_005bb4a5 added.*

---

## 100. Mini-map rolling-scan cursor is bumped BEFORE the strip it renders

`0x4bc1d1` advances the next-scan-row cursor `DAT_004e5d74` by
`param_1`, wraps it, and only then renders (when the minimap mode
`DAT_004e5d58 == 2`):

```c
DAT_004e5d74 += param_1;
if (DAT_004e6570 - param_1 < DAT_004e5d74) DAT_004e5d74 = 0;
if (DAT_004e5d58 == 2) 0x4bc51a(DAT_004e5d74, param_1);   // rows [pos, pos + param_1)
```

The wrap test `pos > total_rows - rows` is exact, not a guard band:
with the battlefield's `total_rows = 80` (bind 0x4bc020, both callers
pass 80) and the tick's step 4, the rendered strips start at
`4, 8, …, 76`, then `80 > 76` wraps to `0` — every row of the 80 is
covered once per 20 calls and no strip spills past row 79.  A
`total_rows` not divisible by the step would leave its last
`total_rows mod step` rows unscanned; retail never has one.

The pre-increment is the visible quirk: the strip at row 0 is drawn
only on a wrap, never on the first call after a reset.  The full
redraw `param_1 = 80` always lands on 0 (`pos + 80 > 0`) and renders
all rows.  The caller is `engine_tick` (`0x4b99c0`) on the battle
screens 0x28..0x2a: `80` while the force-redraw counter `DAT_0053e9ac`
is set, else `4`.  The cursor advances even when the mode gate skips
the render.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_minimap_scroll`),
> `docs/findings/battlefield-render.md` §7, IDA `0x4bc1d1` / `0x4b99c0`.

*Re-checked 2026-11-18 against the IDA export: there is no guard band (the wrap is exact for 80 rows / step 4); title and walk-through corrected; mode gate and caller added.*

---

## 101. Mini-map cell silently drops on unmapped flag values

`0x4bc51a` (the minimap strip, 2×2 px per cell) visits a cell only when
forced (`DAT_0053e9ac`), boundary-marked (`flags & 2`) or dirty
(`flags & 1`), then dispatches in this order:

1. an entity on the cell (`descriptor[+5] != 0`) → the owner's colour
   frame (`DAT_0057bf0a[owner * 0x160]`, owner 6 → frame 6) of
   `DAT_004e5d40`, clearing bit 0x02;
2. else boundary bit 0x02 → frame 0 of `DAT_004e5d40`, clearing 0x02;
3. else (not the in-game first column, quirk #102) the terrain selector:

```c
if ((flags & 0x1c) == 0)      blit 2×2 from DAT_004e6584;  // primary
else if ((flags & 0x1c) == 4) blit 2×2 from DAT_004e6568;  // alt
// else: nothing
```

The mask spans 3 bits, so it can take 0, 4, 8, … 0x1c — only `0` and
`4` map to a blob.  A plain terrain cell (no entity, no boundary bit)
with any other value is silently skipped: no blit, no error, no
default.  Retail's level data only produces 0 / 4 here (the battlefield
terrain passes 0x4bc8fc / 0x4bcbdc use the same strict pair, quirk #76).

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_minimap_strip`,
> `minimap_cell`), `docs/findings/battlefield-render.md` §7, IDA `0x4bc51a`.

*Re-checked 2026-11-18 against the IDA export: confirmed; full dispatch order spelled out (entity branch first); port name updated.*

---

## 102. Mini-map first-col-ingame override replaces only the terrain selector

In `0x4bc51a`, the terrain-selector branch (quirk #101) is reached only
after the entity and boundary branches, and is itself guarded:

```c
if (entity) { owner colour }
else if (flags & 2) { frame 0 of DAT_004e5d40 }
else if (col != 0 || DAT_0052af94 != 3) { flags & 0x1c selector }
else { frame 0 of DAT_004e5d40 }        // col 0 while engine state == 3
```

So with the engine state `DAT_0052af94 == 3` (in game), the minimap's
column 0 — world column 0 of the 80-column battlefield, not a screen
column — paints frame 0 (the same frame the boundary branch uses)
instead of its terrain, giving a consistent left-edge strip.  An entity
on column 0 still shows its owner colour.  With any other engine state
column 0 falls through to the normal selector.

The minimap only renders on the battle screens (0x28..0x2a, from
`engine_tick` 0x4b99c0), so "state != 3" there is not a menu; which
battle path runs with state != 3 (e.g. a battle launched from the
title's skirmish setup) is unverified — a `--watch-write 0x52af94` over
a skirmish battle would settle whether the override ever turns off.

> 📍 `src/engine_battlefield_render.c` (`engine_battlefield_render_minimap_strip`),
> `docs/findings/battlefield-render.md` §7, IDA `0x4bc51a`.

*Re-checked 2026-11-18 against the IDA export: the override sits after the entity and boundary branches (it does not pre-empt them); "menu / save dialog" reading removed; port name updated.*

---

## 103. Camera-anchor clamp uses asymmetric bounds (0x41 vs 0x42)

`0x47ed34` clamps the **battlefield** camera anchor `DAT_0055ce70`
(col) / `DAT_005678a0` (row) — the pair `engine_tick` passes to the
battlefield frame driver `0x4bc142` — to `[0, 0x41]` × `[0, 0x42]`:

```c
if (DAT_0055ce70 < 0)  DAT_0055ce70 = 0;
if (DAT_005678a0 < 0)  DAT_005678a0 = 0;
if (DAT_0055ce70 > 65) DAT_0055ce70 = 65;
if (DAT_005678a0 > 66) DAT_005678a0 = 66;
```

The literals are not a typo: they are the battlefield's grid minus its
visible window.  The grid bind `0x4bc020` (called at 0x498d4c /
0x498f68) sets an 80 × 80 cell grid with a 15-column × 14-row viewport,
and `80 - 15 = 65 = 0x41`, `80 - 14 = 66 = 0x42`.  So the clamp keeps
the 15 × 14 view inside the grid; the asymmetry is the viewport's
shape, hardcoded rather than derived from the bound globals.  (The
strategic-map scroll `0x429b1d`, quirk #104, is a different clamp on
different globals.)

Both bounds are inclusive (`> 65` clamps, so 65 itself stays).

> 📍 `src/engine_camera.c` (`engine_camera_clamp_anchor`; called from
> `src/rl_glue.c` / `src/rl_turn.c`), IDA `0x47ed34`, grid bind `0x4bc020`.

*Re-checked 2026-11-18 against the IDA export: 0x41 / 0x42 = 80 − 15 / 80 − 14 (the battlefield grid and viewport), not a typo; the anchor is the battlefield camera, not the strategic one.*

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

`0x49bc5f` counts the active realms (slots 1..5 with `+5` or `+7` set in
`0x57bf00`, 0x160 each), then compacts the roster `0x568db0` by
dropping any pair (from index 1) whose seat byte exceeds that count,
shifting later pairs down:

```c
for (pass = 0; changed && pass < 6; pass++)      // v1 = v2++; if (v1 >= 6) break
    for (i = 1; i < 6; i++)
        if (active < pair[i].seat) {
            for (j = i; j < 5; j++) pair[j] = pair[j + 1];   /* shift down */
            changed = 1;
        }
```

The shift copies `pair[j+1]` into `pair[j]` for `j = i..4`, so when the
offending entry is at index 5 the inner loop runs zero times — nothing
moves, `pair[5]` is untouched — yet `changed` is still set.  The pass
loop reruns until its hard **6-pass cap**.  The out-of-range entry
survives as a trailing duplicate.  With `active = 0`, pairs 1..4
collapse to copies of the stale `pair[5]`; `pair[0]` is never examined.

Harmless in practice: the consumer, `0x49bd99` (new-game realm setup,
ported as `rl_49bd99`), reads `pair[j]` only for active realms `j` and
the local player's `pair[DAT_0057c8cc]`.  Note `pair[5]` (bytes 10..11)
is outside the 6-byte saved slot (quirk #105), so the surviving tail is
not persisted; pairs 1..2 are.  The port reproduces the cap-bounded
non-convergence verbatim.

> 📍 `src/engine_roster.c` (`player_roster_compact`),
> test `roster_compact_non_convergent_tail_survives`, IDA `0x49bc5f`;
> consumer `rl_49bd99` (`src/rl_turn.c`).

*Re-checked 2026-11-18 against the IDA export: confirmed; consumer 0x49bd99 is ported (not deferred); the trailing pair[5] lies outside the 6-byte persisted slot; pair[0] is never examined.*

---

## 107. NPC schedule reads the global localization walker mid-routine

The AI-realm seating routine `0x49caaa` (`engine_npc_schedule_init` /
`rl_49caaa`) gives each non-human realm within the AI count
`DAT_0053f268` a free slot (`+0x0a`) and the first free personality
(`+0x07`) from its slot's row of `0x4dc17c` (MP or `DAT_0055302c == 1`)
/ `0x4dc180` (by the map `DAT_0053f034 & 3`), then copies that
personality's localized 16-byte name into the realm's aux record
(`0x553d50 + 44*j + 4`) by reading the **global walker pointer**
`DAT_005c9270` right after seeking it:

```c
localization_seek_skip(7, realm[+0x07]);                 // 0x4018d7: sets DAT_005c9270
strutil_bounded_copy(DAT_005c9270, &aux[+4], 16);         // 0x401136: 16 B, no NUL
```

This is retail's ordinary localization pattern, not a one-off: 13
functions read `DAT_005c9270` directly after a seek (data_xrefs), and
others reuse it for unrelated regions.  Retail copies unconditionally
— L2.ENG is always resident before setup runs.

**Port:** the retail-layout port `rl_49caaa` (`src/rl_turn.c`) seeks and
copies through the global walker exactly like retail.  The older pure
port `engine_npc_schedule_init` (`src/engine_npc_schedule.c`, still
called from `game_init.c`) runs the seek on a *local*
`localization_state_t` over an injected blob and skips the copy when
no blob is wired (`engine_npc_schedule_set_localization(NULL, …)`) — a
standalone-only divergence.

> 📍 `src/rl_turn.c` (`rl_49caaa`), `src/engine_npc_schedule.c`
> (`engine_npc_schedule_init`), test `npc_copies_localized_name`, IDA `0x49caaa`.

*Re-checked 2026-11-18 against the IDA export: the name is the personality's (+0x07), copied into aux +4; the direct walker read is the common pattern (13 readers); the rl_ port matches retail, only the legacy pure port skips the copy.*

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

`0x49bae9` (`player_seat_assign(id, seat, name)`) always sets the
seat's in-play flags (`0x57bf05[seat*0x160] = 1`, aux `+0x27` = 1) and
copies the 31-byte name into aux `+4`, stamps the aux id field
(`0x553d50[seat*11]`) only when `id != 999`, and in single-player
(`DAT_00553030 == 0`) copies the realm's slot `+0x0a` into aux `+0x25`.

The only caller passing `999` is the page-4 ("Choose your title and
your shield") button action `0x433155`, which re-seats the local player
`DAT_0057c8cc` with the edited name `0x53f1e0` **without** disturbing an
id set up elsewhere.  The other callers pass real ids: `0x432b05`
(page 1, id 0), `0x4410bd` / `0x44121a` / `0x4412cc` (MP join / seat
paths, ids from the packet or `DAT_004e2a28`), `0x4978ad` (id 0, seat 1).

So `999` is a magic value carried through the same `int` argument as
real ids; a caller that legitimately needed id 999 would silently
no-op the write.  Ids are small, so the collision never bites.

The 31-byte name copy (`0x401136`, a plain byte loop, no NUL
terminator) assumes the source is a full ≥31-byte field — retail always
hands it the `0x53f1e0` buffer or a fixed packet slot.  Passing a short
C string reads past it (the host tests use 32-byte zero-padded buffers).

> 📍 `src/engine_roster.c` (`player_seat_assign`; wrapped in `src/rl_turn.c`),
> IDA `0x49bae9`; callers `0x433155` (999), `0x432b05`, `0x4410bd`, `0x44121a`,
> `0x4412cc`, `0x4978ad`.

*Re-checked 2026-11-18 against the IDA export: the 999 caller is the page-4 button action 0x433155, not 0x44121a / 0x4412cc (they pass DAT_004e2a28); the SP aux +0x25 write added.*

---

## 110. Turn-save backup is a 3-deep generational rename chain

Every turn-save goes through `0x49a453` (`save_rotate_and_write`),
which keeps three generations on disk under fixed names:

```
lastturn.*   ← the turn being written
old_turn.*   ← previous turn
safeturn.*   ← two turns ago (oldest)
```

The rotation is three unchecked CRT calls — `remove(safeturn)`,
`rename(old_turn → safeturn)`, `rename(lastturn → old_turn)` — then
`save_write(lastturn)` (`0x4ade93`).  Nothing checks a return value: on
the first save of a game the renames fail (no source file) and the
chain fills in over the next two turns.

Two twists:

- **The suffix encodes the session mode.**  Single-player uses `.sav`;
  multiplayer (`DAT_00553030 != 0`) uses `.sva` when `DAT_00553248 != 0`,
  else `.svb`.  An MP game keeps a separate chain from an SP game in the
  same directory.
- **Rotation can be held off.**  The rotation runs only while
  `DAT_00553260 <= 0`.  `0x49b973` — the MP turn-sync resolution —
  on its resync branch (`param == 0`) RELOADS `old_turn` / `lastturn`
  (`.sva` / `.svb`) and then sets `DAT_00553260 = 2`; on its normal
  branch it decrements the counter.  `0x43f01d` zeroes it.  So after a
  resync reload the next saves overwrite `lastturn` in place, keeping
  the older generations the reload came from.  It is always 0 in SP.

Filename templates (13-byte copies) at `0x4dc290`–`0x4dc31c`.  The names
are bare relative paths: `save_write` `chdir`s to the save base dir
(`0x4af502`) before opening, but the remove / renames run before that,
in whatever the current directory already is.

> 📍 `src/save.c` (`save_rotate_and_write`), IDA `0x49a453`; hold counter
> `DAT_00553260` set / decremented by `0x49b973`, zeroed by `0x43f01d`.

*Re-checked 2026-11-18 against the IDA export: the hold is set to 2 by 0x49b973's resync-reload branch and decremented by its normal branch (not "bumped on turn advance, decremented elsewhere"); 0x43f01d reset added.*

---

## 111. Two readers of the player roster disagree on how many slots there are

The end-of-turn readiness check (`0x49b762`) walks realm slots `1..5`
(`for (i = 1; i < 6; ...)`).  The MP min-tick walker (`0x43e330`) and
its sibling `0x43e433` walk `1..6` (`for (i = 1; i <= 6; ...)`) over the
*same* realm array (`0x57bf00`, 6 records × 352 B = 2112 B, the saved
block).  Slot 6 sits at `0x57bf00 + 6*0x160 = 0x57c740`, one record past
the end, so `0x43e330`'s gate reads `0x57c745` (`+5`) and `0x57c744`
(`+4`), plus the aux byte `0x553d76 + 44*6 = 0x553e7e` (past the
6-record aux table) and `DAT_0053e8d0[6]` (in bounds — that table has
10 entries).

Benign by layout: no instruction references `0x57c740..0x57c74f`
directly (data_xrefs), so the bytes keep their zero BSS value and the
`+5 && +4 && !aux+0x26` gate fails for the phantom slot — it never
counts toward `DAT_0053f068` nor the minimum.  (A runtime confirmation
would be a `--watch-write 0x57c744` over an MP scenario.)

Ports: `rl_43e330` / `rl_43e433` (`src/rl_mpmsg.c`) reproduce the
slot-6 read on retail's layout; the older `src/engine_rate.c` model
collapses the layout into 7-entry arrays.  `0x49b762`'s 1..5 stays in
bounds either way.

> 📍 `src/rl_turn.c` (`rl_49b762`), `src/engine_turn.c` (`engine_turn_end_check`),
> `src/rl_mpmsg.c` (`rl_43e330`, `rl_43e433`), IDA `0x49b762` (1..5) vs
> `0x43e330` / `0x43e433` (1..6); realm array `0x57bf00`.

*Re-checked 2026-11-18 against the IDA export: the phantom read is +4/+5 of 0x57c740 plus aux 0x553e7e (not "four bytes"); 0x43e433 walks 1..6 too; rl_43e330 reproduces it.*

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

Retail's `DAT_004eac50` is a single byte: the current screen id, the
selector for the `0x40f1a0` render-dispatch switch and hundreds of
other references.  One of them is the text renderer `0x402637`, which picks its
drop-shadow colour pair by it: `(0x10, 0x1f)` normally, `(0x36, 0x2c)`
when the byte is `0x1f` or `0x1c` (the title-class screens).

In the port the byte is mirrored **twice**: `g_sim_current_screen`
(`src/engine.c`, the standalone dispatch selector) and
`g_gfx_screen_byte` (`src/gfx_globals.c`, read by `gfx_text.c` for the
shadow pair, by `engine_hud_panel.c` and `engine_title_render.c`).
`engine_set_current_screen` (`src/engine.c`) writes both, so the
standalone paths stay in step.  But the retail-layout ports write
`g_gfx_screen_byte` directly (e.g. `src/rl_mpmsg3.c`: `= 31`, `= 46`),
bypassing `g_sim_current_screen`, so the two mirrors can skew
whenever an rl_ port changes the screen.
The faithful end state is one variable.

> 📍 `src/engine.c` (`g_sim_current_screen`, `engine_set_current_screen`),
> `src/gfx_globals.c` (`g_gfx_screen_byte`), `src/gfx_text.c` (shadow pair),
> IDA `0x40f1a0` (dispatch) / `0x402637` (shadow-pair special case).

*Re-checked 2026-11-18 against the IDA export: 0x402637 switches the shadow colour pair (16,31)→(54,44), not a taller line box; `g_language_tag` is gone (now `g_gfx_screen_byte`), the fan-out setter exists, and rl_ ports write the gfx mirror directly.*

---

## 115. Two palette builders by desktop depth — fullscreen shows the red highlight, 8bpp/windowed blacks it

Every image-palette upload runs through the dispatcher `0x42f1df`,
which picks one of **two** PALETTEENTRY builders on `DAT_0053f228` —
the **Full screen** option (rl-ui3.md):

- default 1 in the options defaults (`0x4ae310`);
- at boot `0x42e5c0` promotes a 0 to 1 whenever the desktop is not
  8 bpp (`DAT_0053f228 = DAT_0053f22c != 8`) and, when it is 1, seeds
  `DAT_00515d90` with `GetPaletteEntries(NULL, 0, 256, …)`;
- the Full-screen menu action `0x434b10` can flip it only on an 8 bpp
  desktop / ≤ 640 px wide screen (otherwise it just posts event 0x104).

So on any modern 16/32 bpp desktop retail is full screen and stays so.

- **`DAT_0053f228 == 0`** (windowed, 8 bpp desktop) → **`0x42f449`**,
  builds `DAT_00515d90`: 10..245 from the `.256` (peFlags 4 /
  `PC_NOCOLLAPSE`); 0..9 and 246..255 are copied the other way — FROM
  `DAT_00515d90` into the working RGB buffer `0x4ea1b0` — so they keep
  whatever that array held (zero BSS = **black** unless seeded), peFlags 0.
  The dispatcher also does a `SetSystemPaletteUse(NOSTATIC)/(STATIC)`
  dance first when its argument is non-zero.
- **`DAT_0053f228 != 0`** (full screen) → **`0x42f306`**, builds
  `DAT_005165b0`: **1..254 from the `.256`** (peFlags 4), idx 0 from
  `0x4d50f0` (black) and idx 255 from `0x4d4e2e + 765` (white), peFlags 0.

The menu's UI/text indices land where the two builders differ: the
uppercase-highlight colour is **1** (`0x41ea14` arms `DAT_0058fe2c`,
`0x402637` sets foreground 1 for A..Z) and the selected/hovered-button
highlight is **0xf9** (`0x41eaa3`).  gateway.256 carries idx 1 =
`(128,0,0)` dark red and idx 0xf9 = `(252,0,0)` bright red.

- Full screen (every modern machine): `0x42f306`, the `.256` colours
  at 1 and 0xf9 survive — **dark-red title caps** and a **bright-red
  hover highlight**.  This is what a real player sees, and what rhost
  reproduces (it reports a 32 bpp desktop, so retail takes this path).
- Windowed on a true 8 bpp desktop: `0x42f449` blacks the reserved
  range and the highlight is invisible.

**Empirically confirmed (2026-05-29), both ways** with the legacy Frida
capture: fullscreen read idx 0xf9 = `(252,0,0)` peFlags 4, idx 1 =
`(128,0,0)`, idx 0xff = `(255,255,255)`, and the red hover was visible;
the `--force-windowed` capture read idx 0xf9 = `(0,0,0)` because that
harness hook set `DAT_0053f228 = 0` before the menu's gateway.256
upload — a self-inflicted artifact.  (An earlier version of this entry
called the blackout retail-faithful; wrong.)

**Port:** `gfx_load_palette` defaults to the `0x42f306` build
(`g_palette_full_range = 1`); `gfx_set_palette_full_range(0)` selects
the `0x42f449` blackout build for regression against the old
`--force-windowed` groundtruths.

> 📍 `src/gfx.c` (`gfx_load_palette`, `gfx_set_palette_full_range`);
> IDA `0x42f1df` (dispatcher) / `0x42f306` (full-screen builder, `DAT_005165b0`) /
> `0x42f449` (windowed builder, `DAT_00515d90`) / `0x42e5c0` (boot promote + seed) /
> `0x4ae310` (default 1) / `0x434b10` (Full-screen action); `docs/findings/rl-ui3.md`.

*Re-checked 2026-11-18 against the IDA export: DAT_0053f228 is the Full-screen option (default 1, promoted at boot on non-8bpp, toggled by 0x434b10), not a once-at-boot depth flag; 0x42f449 copies the reserved entries from DAT_00515d90 into the image buffer.*

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

The game-setup screen's top-right preview is drawn by
`0x410c71(0, 496, 9)` (from the country list `0x41f98b`), which blits
the 128×128 strategic-map image `0x569590` at `(x - 2, y + 3)` and then
calls the minimap plotter `0x410cbd` (`engine_minimap_plot`) with
**param 0**.  That plotter's entire per-pixel loop — recolouring the
county pixels (map bytes 10..13 of `0x569590`, county id from
the 128×128 grid `0x52aff0`) by owner / band — is guarded by
`if (param_1 != 0)`, so at param 0 it draws **nothing**.  Only its
unconditional first call runs: `0x451bba`
(`engine_minimap_compute_bands`), which recomputes per-county band
bytes (`0x53f9b1` / `0x53f9b2` of the 768-byte county records) that the
plotter's non-zero modes read.  So the page-7 preview is exactly a raw
blit of whatever the scenario-load path rendered into `0x569590`.

The render port models the preview as a bound buffer
(`engine_title_render_set_preview`, or the hybrid's `0x410c71` hook)
and defers `0x410cbd` / `0x451bba`; the other callers of `0x410c71`
(`0x417cef`, `0x475e07`) and the direct plotter callers (`0x410a5d`,
`0x410aa9`) are where the non-zero modes run.

> 📍 `src/engine_title_render.c` (`engine_title_render_setup_country_list`),
> IDA `0x410c71` / `0x410cbd` (engine_minimap_plot) / `0x451bba`
> (engine_minimap_compute_bands), called from `0x41f98b`.

*Re-checked 2026-11-18 against the IDA export: 0x410cbd is the strategic minimap plotter (county colours), not a shield compositor, and 0x451bba recomputes county bands, not lord title/rank; the core "param 0 = plain blit" holds.*

---

## 119. The page-7 per-frame refresh is dirty-gated inside its sub-renderers, and one of its three calls is multiplayer-only

`0x4ba26e`'s page-7 arm (`DAT_005530f0 == 7`) re-runs three calls every
frame: `0x41f98b()` (country list), `0x41f86d(999)` (option tabs,
`999` ⇒ no tab is "active" so all keep their bevel), and `0x41ff75(0)`.
Page 8 (the multiplayer setup) is its own case with the same three
calls plus `0x420147(0)`.  Two non-obvious facts:

- **The gate lives inside the sub-renderer, not the caller.**  `0x41f98b`
  is wrapped in `if (DAT_0057c948 > 0) { DAT_0057c948 = 0; … }` and
  `0x41f86d` in `if (DAT_0055ce94 > 0) { DAT_0055ce94 = 0; … }`.  So the
  per-frame arm paints nothing unless an action set a flag.  The
  full-scene root `0x41f6c7` sets *both* flags to 1 before calling the
  same subs, so a full redraw always draws and ends with both at 0.
  The port does the same: `engine_title_render_setup_country_list` /
  `engine_title_render_setup_tabs` gate on and clear
  `g_tr_country_dirty` / `g_tr_tabs_dirty` themselves, and
  `engine_title_render_setup_screen` sets both first.

- **`0x41ff75(0)` is the multiplayer chat-line ticker and a no-op in
  single-player.**  Its body is guarded by
  `if (DAT_00553030 != 0 && DAT_00553fc4 > 0)`; page 7 is the
  single-player setup screen (`DAT_00553030 == 0`), so it does nothing.
  The drop-in elides it.

A third subtlety: the country scroll/select actions (`0x4337b4` /
`0x433905`) set the demand-redraw `DAT_0057d340 = 2` **and** both
sub-dirty flags.  In the drop-in `DAT_0057d340 = 2` triggers a
full-scene redraw next frame, so the dirty-gated refresh is the
faithful per-frame path but the full redraw is what's observed after
an action.

> 📍 `src/engine_title_render.c` (`engine_title_render_refresh_setup`,
> `engine_title_render_setup_country_list`, `engine_title_render_setup_tabs`,
> `engine_title_render_country_scroll/select`), `src/engine_menu_input.c`
> (page-7 arm), IDA `0x4ba26e` / `0x41f98b` / `0x41f86d` / `0x41ff75` /
> `0x41f6c7` / `0x4337b4` / `0x433905`.

*Re-checked 2026-11-18 against the IDA export: the port now gates inside the sub-renderers like retail (no hoisted gate; refresh_setup's doc comment still says otherwise); page 8 is its own case; 0x41ff75's guard includes DAT_00553fc4.*

---

## 120. Retail's mouse input assumes a fullscreen window — three globals fight a windowed seat

Driving retail interactively in a *windowed* window while the Full
screen option `DAT_0053f228` is 1 (quirk #115: always so on a non-8bpp
desktop) exposes three input/timing assumptions baked in for 640×480
fullscreen.  All three were found building the legacy
`frida_capture.py --interactive` seat (PROGRESS 2026-05-30):

- **App-active gate (`DAT_004e65a8`).**  The main loop `0x40e9ab` runs
  its present + full-rate tick only while `DAT_004e65d0 == 0 &&
  DAT_004e65a8 != 0`, else `Sleep(200)`.  The WndProc `0x4b29be` sets
  `DAT_004e65a8` on `WM_ACTIVATEAPP` (0x1c) to `wParam && foreground &&
  !IsIconic`, and also to 1 on `WM_MOVE` / `WM_SIZE` when not iconic.  A
  background window crawls at ~5 Hz.

- **Cursor override (`DAT_0057cac4`).**  The per-tick input read
  `0x4b191e` calls `GetCursorPos` (screen coords) and, *iff*
  `DAT_0057cac4 != 0`, overwrites the WndProc's WM_MOUSEMOVE cursor
  (`DAT_004eabbe/c0`, client coords) with them.  `DAT_0057cac4` is a
  "camera just moved" latch, not an in-game flag: the edge-scroll step
  `0x431f59` and the recentre `0x43278b` set it to 1, the in-game input
  dispatcher `0x42ff10` copies it to `DAT_0052afa0` and clears it.
  Correct only when client == screen (fullscreen at the origin).

- **Mapping rect (`DAT_004ea180..18c`).**  On WM_MOVE / WM_SIZE the
  WndProc sets it to `(0, 0, GetSystemMetrics(0), GetSystemMetrics(1))`
  when `DAT_0053f228 != 0`, else to the client rect converted to screen
  coords (`GetClientRect` + `ClientToScreen`).  `0x4b191e` maps
  `game = cursor * 640 / (rect width)` (width only — the rect's origin
  is never subtracted).  So in a 640×480 window on a 1920-wide desktop
  with the option at 1, every position is scaled by 640/1920 into the
  wrong game position, even though the image looks right.

The legacy interactive seat neutralised all three by poking, every
tick: `DAT_004e65a8=1`, `DAT_004e65d0=0`, `DAT_0057cac4=0`, and
`DAT_004ea180..18c = (0,0, client w, client h)`.  rhost does not need
this: its software window layer presents the 640×480
display itself and trace input is recorded in 640×480 coordinates.

> 📍 `tools/frida/openlords2-agent.js` (`tryForceAppActive`, legacy), IDA
> `0x40e9ab` / `0x4b191e` / `0x4b29be` (WndProc) / `0x431f59` / `0x43278b` /
> `0x42ff10`; `DAT_004e65a8` / `DAT_0057cac4` / `DAT_0053f228` / `DAT_004ea180`.

*Re-checked 2026-11-18 against the IDA export: DAT_0057cac4 is set by the camera edge-scroll / recentre and cleared by 0x42ff10 (not "1 on in-game screens"); WM_MOVE/WM_SIZE also raise DAT_004e65a8; the windowed rect is in screen coords and only its width is used.*

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

The one global text editor (`DAT_005cd550` buffer + state scalars) is
fed by `WM_CHAR`/`WM_KEYDOWN` in the WndProc (`0x4b29be`), but every
edit leaf — insert `0x401d26`, delete `0x401dc8` — first checks
`DAT_005aeb78 != 0`.  That enable flag is **not** owned by the keyboard
path — it's owned by the per-frame UI input dispatcher `0x4ba9c8`, which
clears it at its very top (`if (DAT_0057a0cc <= 0) DAT_005aeb78 = 0;`) and
then each arm that has a live text field raises it back to 1 and streams the
editor out with `0x401f49` + `0x40210c`:

| screen `DAT_004eac50` / page `DAT_005530f0` | field |
|---|---|
| 0x1f page 3 | load filename `DAT_004ea130` (64) |
| 0x1f page 4 | player name `DAT_0053f1e0` (31) |
| 0x1f pages 8 / 0xb (MP setup) | MP game name `DAT_00553e80` (64, lower-cased by `0x401246`) |
| 0x35 / 0x36 (in-game load / save) | filename `DAT_004ea130` (64) |
| 0x1a (letter, `DAT_005651cc` 1..4) | `DAT_0053f1e0 + 200·n + 16` (199) |

Pages 5 / 6 do **not** raise it (no field there).  The only other writer is
the in-game chat-entry tick `0x420316`: while its countdown `DAT_0057a0cc` is
> 0 it raises the flag itself and streams into `DAT_00553e80` — which is also
why the dispatcher's reset is skipped while that countdown runs.

So a keystroke that arrives while you're on, say, page 7 is filtered and
then **dropped** — `0x401a20` runs, but the insert no-ops because the
last dispatch frame cleared the gate and page 7 never re-raised it.  The
editor only accepts input on frames where a text-field arm ran.  This is why
the editor can be a single global shared across every screen: it's
mode-gated by whichever input arm is running, not by focus tracking.

Corollary for the drop-in: the gate must be reset at the *top of the menu
input dispatch* (before the page switch, gated on `DAT_0057a0cc <= 0`) and
re-raised in the arm — not in the WndProc.  Putting the reset in the keyboard
handler would mean a key with no intervening frame keeps the stale enable.
The retail-layout port `rl_4ba9c8` does exactly this; the older
`engine_menu_title_input` port resets unconditionally (it ignores the
`DAT_0057a0cc` gate) and covers only the title pages.

> 📍 `src/rl_host.c` (`rl_4ba9c8`, `editor_out`), `src/engine_menu_input.c`
> (`engine_menu_title_input` head reset + page arms), `src/engine_title_render.c`
> (`engine_title_render_title_editor_disable` /
> `engine_title_render_choose_title_name_tail`), `src/main.c` (WM_CHAR /
> WM_KEYDOWN route); IDA `0x4ba9c8`, `0x420316`, `0x401d26`, `0x401dc8`,
> `0x4b29be`.

*Re-checked 2026-11-18 against the IDA export: the reset is gated on `DAT_0057a0cc <= 0`; raising arms are pages 3/4/8/0xb, screens 0x35/0x36/0x1a and the chat tick 0x420316 (pages 5/6 never raise it, page 8/0xb is the MP name not a save name); added the rl_4ba9c8 pointer.*

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

The screen button strips (`0x40cfd2` renderer / `0x40da1e` hit-test,
e.g. the page-3 file list's gauntlet + scroll buttons) store one 24-byte
record per button.  The i16 at +0x06 is the *sprite kind* — a sheet+blitter
selector in `0x40cfd2`: kinds `>= 24` read the `system2.pl8` sheet
(`0x5bb540`), smaller kinds the setup sheet (`DAT_005530c8`); `16` blits
16×16, `17` and `24` blit 24×24, `32` blits 32×32.  The hit-test then
computes the clickable box as `[x, x + kind) × [y, y + kind)` — i.e. it uses
the **same field** as both the sprite selector *and* the pixel side length.
It works only because every kind the shipped tables use equals its own
sprite size: a scan of all 32 strip tables passed to `0x40da1e` /
`0x40cfd2` (`0x4dd310` .. `0x4de000`) finds only kinds 16, 24 and 32.  The
renderer's kind 17 is the counter-example already built in: it would draw a
24×24 sprite under a 17×17 hitbox — no table uses it.

> 📍 `src/engine_menu_input.c` (`button_rec_contains`), `src/engine_button.h`
> (the `kind` field); IDA `0x40da1e` (`v8 = a3[3]`), `0x40cfd2` (the
> `v8 >= 24` sheet pick + `case 16/17/24/32` blitter switch).

*Re-checked 2026-11-18 against the IDA export: three kinds in use (16 on the setup sheet, 24 / 32 on system2), not two; the renderer also accepts kind 17 (24×24 sprite, 17-px hitbox), unused by any table.*

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
`0x402009(&DAT_004ea130, 8, 0xa0, 1)` — `max_length = 8`.  The per-frame
copy-out streams it with width `0x40` (`0x40210c(&DAT_004ea130, 0x40)`),
but the editor refuses inserts past 8 chars (quirk #124's clamp).  Eight is the
DOS 8.3 base name: the engine still writes `*.sav`-style files and won't let
you name a save longer than the filesystem of 1996 allowed.  Typing "war" over
the seed "lastturn" yields "wartturn" — overwrite-from-cursor, then truncated
at 8.  Mode 1 (vs page 4's mode 0) changes the key filter `0x401a20` in two
ways: it rejects `, . ? !` (accepted only in mode 0), and it **folds A–Z to
lower case** (`strutil_to_lower_char` `0x4011b0`) — so a save name is
always lower-case whatever the shift state.

> 📍 `src/main.c` (`seed_page3_state` → `text_input_init(.., 8, 0xa0, 1)`),
> `src/text_input.c` (`init_mode_param` filter); IDA
> `0x402009(&DAT_004ea130, 8, 0xa0, 1)` (5 call sites), `0x401a20`.

*Re-checked 2026-11-18 against the IDA export: mode 1 does change the case (upper → lower in 0x401a20) as well as dropping four punctuation keys; the old "not the case" remark was wrong.*

---

## 128. The new-game calendar is seeded as six redundant scalars — year and season each stored alongside their own ±1 neighbours and a verbatim copy

The new-game initialiser (`0x497ced`) doesn't store the start date as
two numbers (year, season).  It writes **six** save scalars laid out as two
parallel "triple + copy" groups, derived in the asm from a single base via
`inc`/`dec` rather than as independent constants (0x497d2f–0x497d97):

```
year   cur  DAT_00553edc 0x4f3 (1267)  prev DAT_0057c900 0x4f2  next DAT_00553e74 0x4f4  copy DAT_00553228 0x4f3
season cur  DAT_0057c934 3             prev DAT_00554470 2      next DAT_0057c92c 4      copy DAT_0057c990 3
```

(save entries 20/21/22/28 and 23/24/25/29.)  The triples are a **rolling
window**, not a render convenience: the turn advance `0x448440` shifts them
every turn — `prev ← cur`, `cur ← next++`, `next` wraps `> 4 → 1`; when the new
`cur` season is 4 the year triple shifts the same way (`0x57c900 ← 0x553edc ←
0x553e74++`).  The previous season is a live input (`0x44bf04` passes
`DAT_00554470` to the provisioning decision `0x44df5f`); the previous year
`DAT_0057c900` is write-only.  The "copies" are the **start date**, never
advanced: `DAT_00553228` feeds `county_year_number` (`0x41a900`, from
`0x4110b1` / `0x4116fb`) and the elapsed-years thresholds (`start + 6 / 12 / 32`
vs the current year) in `0x475b41`; `DAT_0057c990` is write-only.

The season-next write is followed in `0x497ced` by a wrap clamp
`if (next > 4) next = 1` (0x497d97) that is **dead** at the seeded value 4.
But the seed never survives to the first turn: `0x497ced` itself calls
`engine_turn_advance` (`0x448440`) further down, so a new game starts at
season 4, year 1268 (prev season 3, next season 5 → wrapped to 1 by
`0x448440`'s own clamp, turn counter `DAT_00553240` = 1).

> 📍 `src/rl_turn.c` (`rl_497ced`), `src/rl_ctl.c` (`rl_448440`'s calendar
> shift), `src/game_init.c` (`engine_game_init_seed_scalars`); IDA `0x497ced`
> @ 0x497d2f–0x497d9e, `0x448440`.

*Re-checked 2026-11-18 against the IDA export: the ±1 neighbours are a rolling prev/cur/next window shifted by 0x448440 (not a draw-time convenience); the "copies" are the never-advanced start date (DAT_00553228 read by 0x41a900 / 0x475b41); 0x497ced's own turn advance means play starts at season 4 / 1268.*

---

## 129. (withdrawn) The army-coord probes index a 3-byte table that Ghidra renders as a 0x300 stride

Claimed `0x428007` / `0x428078` read a packed stride-3 `{col,row,?}` table at `0x53fa1c` and that Ghidra's
`[id*0x300]` was a mis-rendered `lea`.  Wrong: the asm is `mov eax,[id]; shl eax,8; mov cl,[eax+eax*2+0x53fa1c]`
(0x428030 / 0x428035) = `id*0x300 + 0x53fa1c` — the earlier reading skipped the `shl`.  Retail reads the per-county
record's capital cursor at `+0x6c/+0x6d` (stride 0x300, written by `setup_capitals` 0x468d4f); the port already
uses stride 0x300 (`ARMY_COORD_STRIDE`, commit f550b09; see #174).

---

## 130. The box-scan loop counters double as the engine's shared cell cursor — a port trap for FUN_00427ed0  ✅ RESOLVED

The strategic-map box scans `0x46cfbd` / `0x46d130` (and the ring-distance
finder `0x46733c` over them) use the globals `DAT_00569570` (inner/col) and
`DAT_00569578` (outer/row) as their loop counters — the shared cell cursor
that ~90 retail functions (path field, grid sweeps, the AI scanners) count in
(see #226).  On the return-1 path the counters hold the **matching cell's
(col, row)**; on a miss they hold the scan's end position:

```
0x46d083  mov [0x569578], eax   ; row = clamped start, then inc per row
0x46d0ae  mov [0x569570], eax   ; col = clamped start, then inc per cell
          return 1              ; match → counters frozen at the hit
```

Three callers read the cursor right after an army probe (`0x428007` /
`0x428078`):

```c
/* 0x427ed0 place_army_starts */
if (0x428007(id) || 0x428078(id)) 0x46e1b0(3, DAT_00569570, DAT_00569578, 6);
/* 0x4292af AI caravan dispatch */
if (0x428007(r) || 0x428078(r))   0x46e1b0(4, DAT_00569570, DAT_00569578, player);
/* 0x4280e9 settle marches */
if (0x428007(r)) { stack[+0x14] = stack[+0x16] = DAT_00569570;
                  stack[+0x15] = stack[+0x17] = DAT_00569578; }
```

— so the box scan **must** expose the matched (col, row), or the placement
silently targets the wrong cell.

**Resolution.**  `box_scan` counts in `g_cell_cursor_col/row` (the port's
binding of `DAT_00569570/78`, #226) exactly as retail does, *and* reports the
hit through `int *out_col, int *out_row` out-params threaded through
`army_near` up to the public army probes
(`engine_game_init_army_near_rawflag_bit0/_only_bit1`) and
`engine_army_state_find_settle_spot`.
`engine_game_init_place_army_starts` reads them with the same `||`
short-circuit retail uses.  Callers that ignore the position (the ring finder,
region_clear) pass NULL.  A host test (`game_init_army_probe_reports_matched_cell`)
pins the reported cell; the two `place_army_starts` tests confirm the marker
lands at the matched cell.

> 📍 `src/engine_map_query.c` (`box_scan` + predicates), `src/game_init.c`
> (`army_near`, `engine_game_init_place_army_starts`), `src/engine_army_state.c`
> (`engine_army_state_find_settle_spot`), `src/engine_army_ai.c` (the 0x4292af
> caravan caller), `src/engine_territory.c` (`engine_territory_place_entity` =
> 0x46e1b0); IDA `0x46cfbd`, `0x427ed0`, `0x4280e9`, `0x4292af`.

*Re-checked 2026-11-18 against the IDA export: the port no longer uses locals (box_scan counts in g_cell_cursor_col/row, #226); 0x4280e9 and 0x4292af also read the cursor after the probe, not only 0x427ed0; the globals are the shared cell cursor, not engine_path's private result.*

---

## 131. The all-cells grid clearer is unrolled 8 cells wide and steers itself with the shared cell-cursor globals

`0x46c5b8` zeroes one byte (the parameter's low byte = the field offset) of
every overworld cell (4096 cells, 8 B each), and a one-line C loop would do
it — but retail hand-unrolls the sweep **8 cells per inner iteration** (eight
`mov byte [...], 0` at cell offsets +0, +8, … +0x38) and runs the two enclosing
loops on `DAT_00569578` (0..0x40) and `DAT_00569570` (0..8), advancing a byte
cursor `DAT_0053e8b8 += 0x40` each step.  Its sibling `0x46c6ba` (clear bit
0x40 of every cell's +2) is the same unrolled sweep.

`DAT_00569570` / `DAT_00569578` are the engine's shared cell cursor (quirk
#130 / #226) — the same two globals the box scans leave their hit in.  A
grid-wipe has no "result", but after it returns they hold (8, 0x40) and
`DAT_0053e8b8` holds 0x8000 — and per #226 that leftover is live state (the
cursor is read by ~90 functions, `DAT_0053e8b8` is the cursor of a dozen
sweeps 0x46a1a6..0x46ca84).  So the retail-layout ports keep them:
`rl_46c5b8` / `rl_46c6ba` count in the cursor and `DAT_0053e8b8` exactly as
retail does, and `rl_46df51` counts its cells in `DAT_0053e974`.  The older
`engine_territory_clear_cell_field` / `_clear_cell_render_flags` ports still
use locals and leave the cursor alone.

> 📍 `src/rl_terr.c` (`rl_46c5b8`, `rl_46c6ba`, `rl_46df51`),
> `src/engine_territory.c` (`engine_territory_clear_cell_field`,
> `engine_territory_clear_cell_render_flags`); IDA `0x46c5b8`, `0x46c6ba`,
> `0x46df51`.

*Re-checked 2026-11-18 against the IDA export: the cursor / byte-cursor leftovers are live state (#226) and the rl_ ports write them; "write-only scratch, collapses to a local" no longer describes the verified port.*

---

## 132. The per-key cell roster's overflow guard allows a key (17) the destination table can't hold — a latent 1-int OOB defused only by the shipped map data

`0x46da4b` rebuilds a per-key roster (retail `DAT_0053ea00` = save
entry 12, `g_misc_1360`) that groups active overworld cells (+1 bit 0x20) by
their +7 "group-key" byte: row *k* lists up to 20 cell byte-offsets whose key
== *k*.  The destination is exactly **1360 bytes = 17 rows × 20 × 4** — rows
0..16.  But the scan's accept guard is `key != 0 && key <= 0x11`, i.e. it
admits keys **1..17**, and a key of 17 indexes `roster[17*20 + c]` =
i32 index 340.. = the first int *past* `g_misc_1360`.  (A cell with no free
column left is reset instead: +0 = 0, +3 = 6, +1 = 0, +2 &= 0xe3.)

The clear half is narrower still: it zeroes rows **1..16 only**
(`for (k = 1; k < 0x11; …)`), so row 0 and the would-be row 17 are
left untouched at entry.  The write never lands out of bounds in practice,
but not because anything clamps +7: the map loader `0x467770` copies it
verbatim from plane 5 of `L2_maps.dat` (#158) — it only *counts* keys `<= 0x10`
for `DAT_0056d5dc`.  The defuse is the data: across all 80 maps plane 5 holds
only 0..16 and 0x20 (sea), never 17.  (Of the other +7 readers only
`0x46ff43` gates it, `< 0x11`.)  A custom map with a 17 would reach the OOB.
The port preserves the `<= 0x11` guard verbatim (a faithful drop-in keeps
retail's latent bug); the host tests stay within keys 1..16.

Second, smaller blind spot in the same function: the "free column"
sentinel is the value **0**, which is also cell (0,0)'s own byte-offset.
A feature at cell (0,0) would store 0 — indistinguishable from "empty" —
so it could never register distinctly and a later same-key cell would
overwrite its slot.  Harmless because cell (0,0) is map-border terrain,
never an active feature.

The **per-county adjacency builder `0x467ca6`** shares this exact loose
guard against a 17-row block: it accepts a cell's key with
`key != 0 && key <= 0x11` (1..17) and indexes the per-county record block
(retail `DAT_0053f9b0` = save entry 4, **17 rows × 0x300 B**) at
`base + key*0x300`, so a centre cell with key 17 would write its
neighbour-count (`+0x5a`) and list (`+0x5c`) one 0x300-row *past* the
block.  Same defuse (no key 17 in the shipped maps) and the same verbatim
preservation (`engine_territory_build_army_adjacency`).
Note a neighbour key of 17 is harmless: it is only stored as a list *byte*,
never used to index the block; the OOB needs key 17 as the swept centre.

The **feature-seeding pass `0x467a36`** is a third instance of the same
loose guard against the **same** 17-row block: it accepts `key != 0 &&
key <= 17` and bumps that county's active-cell tally at
`base + key*0x300 + 0x205`.  Identical defuse and verbatim preservation
(`engine_territory_seed_army_features`).

> 📍 `src/rl_terr.c` (`rl_46da4b`, `rl_467ca6`, `rl_467a36`, `rl_467770`),
> `src/engine_territory.c` (`engine_territory_rebuild_cell_roster`,
> `engine_territory_build_army_adjacency`, `engine_territory_seed_army_features`),
> `docs/findings/game-start.md` §"FUN_004676e0"; IDA `0x46da4b`, `0x467ca6`,
> `0x467a36`, `0x467770`.

*Re-checked 2026-11-18 against the IDA export: +7 is not clamped by its producer (0x467770 stores plane 5 raw); the defuse is that no shipped map has key 17 (plane-5 values 0..16 and 0x20 over all 80 maps); only 0x46ff43 gates +7 `< 0x11`; added the rl_ pointers.*

---

## 133. The overworld neighbour predicate's bounds check is load-bearing, not defensive — and the strategic map is a staggered-row (iso diamond) grid

`0x46de94` (the per-tile predicate behind the terrain auto-tiler
`0x46dbf5`) opens with a four-way signed bounds ladder —
`row >= 0 && col >= 0 && row < 0x81 && col < 0x41` — and returns 0 for
anything off-grid; otherwise it reads the 129 × 65 i32 terrain-index render
grid `DAT_0055cea0[65*row + col]` and returns 1 iff the tile is a real cell
(value `< 0xfff0000`, a byte-offset into the cell grid) whose +1 flags lack
bit 0x4, or the plain special sentinel `0xfff0006`.  That guard looks like
ordinary defensive coding, but it is **functional**: its caller `0x46de05`
adds a neighbour offset to (col,row) *before* the call, and for tiles on
the grid border those offsets land off the edge (e.g. col `-1`, row `-2`).
Retail relies on the predicate quietly reporting "ineligible" there; a port
that drops the check would read a neighbouring row's tile (`65*row - 1` is
still inside the array) and pick the wrong edge variant along the map border.

The auto-tiler (`0x46dbf5`) runs this for every feature tile (+1 bit 0x4, or
a non-`0xfff0006` special): `0x46de05` fills the 8-entry mask `DAT_00543fe0`,
`0x46c2de` (`engine_territory_pick_feature`) maps it through the 48-entry
pattern table `0x4d7310` to the tile variant.

The reason the offsets reach off-grid at all is the second half of the
quirk: the render grid is a **staggered-row isometric (diamond) grid** — 65
columns × 129 half-rows — so `0x46de05` picks one of *two* 8-entry
`{dx,dy}` tables by **row parity** (`row & 1`): `0x4d6c28` (even) /
`0x4d6be8` (odd).  Both carry E/W `(±1,0)` and N/S `(0,±2)` (the
doubled-row vertical step); the four diagonals `(dx,±1)` take dx = 0 / +1 on
even rows and −1 / 0 on odd rows — the hallmark of offset (staggered)
coordinates.  It is an 8-neighbourhood, not a hex grid.  Any strategic-map
adjacency code over this grid must consult the parity-correct table.

> 📍 `src/rl_terr.c` (`rl_46de94`, `rl_46de05`, `rl_46dbf5`),
> `src/engine_territory.c` (`engine_territory_tile_eligible` /
> `engine_territory_compute_neighbour_mask`),
> `docs/findings/game-start.md` §"FUN_004676e0"; IDA `0x46de94`, `0x46de05`,
> `0x46dbf5`, tables `0x4d6c28` / `0x4d6be8`.

*Re-checked 2026-11-18 against the IDA export: the caller is the auto-tiler 0x46dbf5 (not a feature-spread); the grid is the 129×65 render grid DAT_0055cea0; the geometry is a staggered iso 8-neighbourhood, not a hex grid; added the rl_ pointers.*

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

`0x44f6e7` (`engine_territory_allocate_realm_workforce`, the per-county
population / job-slot allocator — called from 23 sites: new-game setup
`0x49bd99`, the turn passes and every management-UI change) distributes the
population into capped buckets with a greedy `while (counter < cap)` fill plus
a round-robin that also tests `counter < cap`.  Before any of that it
normalises every cap field (the +8 of the nine 0xc-byte bucket records at
`+0xc4`):

```
cap = rec[+0xcc + 0xc*i];
if (cap == 999999) cap = 0;
```

So a `999999` cap becomes `0`, and `counter < 0` is never true: the bucket
accepts nothing and its share spills into the leftover.  The caps are the
planners' per-bucket targets (#143), and 999999 is not an "unlimited" value
anywhere in retail: its only producer is `engine_army_economy_select_candidate`
(`0x44dd4d`), whose best-index variable starts at 999999 and is stored into
`+0xd8` unchanged when no candidate was evaluated (population `+0x24 <= 0`).
Retail's real "no ceiling" is a large concrete cap — `0x44f318` writes
`100000` into the non-slot-2 resource buckets.  So `999999 → 0` reads "no
candidate → staff nothing", and the compare on the seven other cap fields is
defensive (only `+0xd8` can carry the value).  The port preserves the literal
compare on all eight fields.  The same county's phase-2 buckets are *also*
gated to `0` by their per-building "present" flags
(+0x297/+0x2af/+0x2c7/+0x2df and +0x1c3 ∧ +0x1b0).

> 📍 `src/engine_territory.c` (`engine_territory_allocate_realm_workforce`),
> `src/engine_army_economy.c` (`engine_army_economy_select_candidate`); IDA
> `0x44f6e7`, `0x44dd4d` (`v11 = 999999` → `+0xd8`), `0x44f318` (`100000`).

*Re-checked 2026-11-18 against the IDA export: 999999 is not an "unlimited" sentinel anywhere — its only producer is 0x44dd4d's no-candidate best index (→ +0xd8); retail's no-ceiling cap is 100000 (0x44f318); the allocator runs from 23 call sites, not only at new game.*

## 137. The strategic-map recenter clamps the scroll even when it refused to move it

`0x43278b` (recenter-on-tile) scans the 129 × 65 terrain-index grid
`DAT_0055cea0` for a tile value and, on a hit, sets the scroll position to
centre on it — but **only when the view-mode global `DAT_0057cb18` is 0** (the
normal map view).  The clamp that follows, however, is called
**unconditionally**, outside that gate:

```c
if (DAT_0057cb18 == 0) {
    DAT_005651b4 = col - 4;
    DAT_005651b8 = (row & 0xfffe) - 0xc;
}
0x429b1d();              /* clamp DAT_005651b4/b8 in place — always */
DAT_0057cac4 = 1;        /* cursor-cache enable    */
DAT_0053e9ac = 1;        /* below-status redraw    */
return 2;
```

So calling the recenter while in a non-normal view mode still (a) re-clamps
whatever scroll position was already there — which can *move* it if a prior
state left it out of range — and (b) latches the two redraw flags and
returns the same "found" code 2.  The hit is reported and the screen is
flagged dirty regardless of whether the view actually scrolled.  Both ports
mirror this: the clamp + flag latches live after the view-mode `if`.

Two incidental notes from the same pair of functions:

- The centring offsets are asymmetric — column is `−4`, row is
  `(row & ~1) − 12` — and the row is floored to even first.  That `& 0xfffe`
  is the staggered-row grid showing through (quirk #133): a logical map
  row spans two grid rows, so the camera anchors on the even one.

- The caller `0x432746` stages the county's capital tile (`+0x70`) through the
  global `DAT_0053f0dc` (`DAT_0053f0dc = rec[+0x70]; if (!= 0) recenter(it);`)
  and then selects the county (`DAT_0057c93c = c`).  The other two stagers
  (`0x43253a`, `0x43caf4`) do the same write-then-read; nothing reads
  `DAT_0053f0dc` across a call, but it is a written global, so the
  retail-layout port `rl_432746` writes it (`g_rl_53f0dc`); the older
  `engine_territory_recenter_on_capital` uses a local.

> 📍 `src/rl_ctl.c` (`rl_43278b`, `rl_432746`), `src/engine_territory.c`
> (`engine_territory_recenter_on_tile` / `engine_territory_recenter_on_capital`);
> IDA `0x43278b`, `0x432746`.  First producer of the scroll save globals
> (entries 16/17) — see #157 / #176.

*Re-checked 2026-11-18 against the IDA export: the grid is the staggered iso grid (not hex, #133); 0x432746 also sets DAT_0057c93c; the verified rl_432746 writes DAT_0053f0dc rather than using a local; added the rl_ pointers.*

## 138. One per-county byte (+0x1a7) is shared between the production-level fallback and the cell-retype flag

The production-level solver `0x44cfe1` (`engine_army_economy_production_output`)
writes a per-county boolean at record **+0x1a7** (`DAT_0053fb57`): with both
inputs positive it clears the byte, tries a rate-scaled output
(`level × +0x201`) that fits the budget, and on failure sets it to 1 and falls
back to a flat level; if even that fails it clears it again.  (The two early
`<= 0` returns leave the byte as it was.)

The byte is read in two places, both inside the seasonal production pass
`0x44c8ae` (`engine_army_economy_apply_season_pass`), which itself calls
`0x44cfe1` in its season-1 arm:

- `0x44c8ae` directly: flag set → `+0x202` and `+0x206` = 1, else both copy
  `+0x201`;
- `0x469d21` (`engine_territory_retype_army_band`, `ARMY_RETYPE_FLAG = 0x1a7`),
  called by `0x44c8ae` with `a4 == 2`: flag set → every match after the first
  is collapsed to type 2.  (Its other caller `0x44d913` passes `a4 == 19`, so
  the flag never matters there.)

So it is not two unrelated subsystems sharing a scratch byte: it is the season
pass signalling to its own retype step that the county fell back to the flat
production level, so only its first field keeps the retyped cell.  It is
still cross-function state (written in one turn-pass leaf, read by two
others, saved with the record), so a faithful port must treat +0x1a7 as live
state, not a private scratch byte of either module.

> 📍 `src/engine_army_economy.c` (`ARMY_PROD_FALLBACK`,
> `engine_army_economy_production_output`, the season pass) writes / reads it;
> `src/engine_territory.c` (`ARMY_RETYPE_FLAG`, `engine_territory_retype_army_band`)
> and `src/rl_terr.c` (`rl_469d21`) read it.  IDA `0x44cfe1`, `0x44c8ae`,
> `0x469d21`.

*Re-checked 2026-11-18 against the IDA export: a third reader (0x44c8ae itself, → +0x202/+0x206) was missing; both readers sit in the season pass that calls the producer, so the coupling is intra-economy signalling; the early returns leave the byte stale.*

## 139. The provisioning decision's level clamp allows one entry past its supply-cost table — but no path ever reaches it

The per-county provisioning decision `0x44df5f`
(`engine_army_economy_provision`) prices a ration level L (`+0x15d`) by
indexing a table of `(divisor, mult)` pairs at `0x4d6738`, stride 8.  The real
ladder is six levels, 0..5:

```
L0 (1,0)  L1 (4,1)  L2 (2,1)  L3 (1,1)  L4 (1,2)  L5 (1,3)
```

— monotonically rising cost `mult·ceil(feed/divisor)`: 0, ~feed/4, ~feed/2,
feed, 2·feed, 3·feed.  The two level-ratchet loops clamp the index with
`if (level > 6) level = 6`, i.e. the bound permits **level 6**, one past the
table: at `0x4d6738 + 6·8 = 0x4d6768` sits the first entry of the adjacent
`⌊100/n⌋` table (`100, 50, 33, 25, 20`, read by `0x4502ca` / `0x450639`), so
"level 6" would price as `(100, 50)` — cheaper than levels 3–5.

That read never happens.  L starts at the requested ration `+0x15e` + 1 and
the first loop decrements **before** each evaluation, so its first read is at
`+0x15e`; the second loop only decrements.  `+0x15e` is 0..5 by every writer:
the UI steppers `0x43a23f` (`< 5 → ++`) / `0x43a307` (`> 0 → --`), the AI
`0x4a4782` (0..5), new-game `0x451150` (3), and the MP mirror `0x444280` (a
peer's value).  So L ∈ 0..5 throughout and both `> 6` clamps are dead code.
The port's seven-entry `k_supply_table` (7th = `{100, 50}`) is harmless
belt-and-braces.  The same level byte `+0x15d` is the row index of the
ration→approval table (#149), whose would-be row 6 is likewise unreachable.

> 📍 `src/engine_army_economy.c` (`k_supply_table`, `supply_cost`,
> `clamp_supply_level`); IDA `0x44df5f` (L = `+0x15e` + 1, pre-decrement
> loops, clamps), writers of `+0x15e` `0x43a23f` / `0x43a307` / `0x4a4782` /
> `0x451150` / `0x444280`; tables `0x4d6738`, `0x4d6768`.

*Re-checked 2026-11-18 against the IDA export: the over-read is unreachable — L starts at +0x15e + 1 and is decremented before the first read, and +0x15e is 0..5 by every writer, so the `> 6` clamps never fire.*

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
and all three callers (`0x4485a5` plan_realm, `0x44bf7a` the development
spread, `0x451150` the new-game baseline) discard it (bare statements).
So the function's entire return path is dead; only its record-field side effects
(`+0xe0/+0xe4/+0x20c/+0x210/+0x214`) matter.  Ported verbatim regardless.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_distribute_stock` /
> `_seed_fullest_cell` / `_pick_fullest_cell`).  IDA `0x44c278`,
> `0x44c5fc`, `0x44c53b` (the pair is also called back to back by
> `0x44c093`, #150); callers `0x4485a5`, `0x44bf7a`, `0x451150`.

*Re-checked 2026-11-18 against the IDA export: 0x44c278 has three callers (0x4485a5, 0x44bf7a, 0x451150), all discarding the return — not one; the argmax keeps cells with type >= 0x19.*

## 143. (withdrawn) One eight-slot record array, two unrelated lives — and a field that gates its own producer

Claimed the `+0xc4` bucket records (`0xc` B) live two unrelated lives (setup job buckets vs per-turn production levels),
that `0x44f318` writes `+0x118`, and that `0x4d8990` is a per-nationality table.  None holds: there are nine records
(`0x44f6e7` zeroes `+0xc4 + 0xc·i`, i < 9) with one meaning throughout — `+0` workers assigned (written by the allocator
`0x44f6e7`, 23 call sites, every turn and UI change), `+8` the planners' target (`0x44d374` / `0x44dd4d` / `0x44f318`,
`100000` = no ceiling, #136) that the allocator fills up to.  `0x44f318` only *reads* `+0` and writes `+4` / `+8`;
`0x44f15b` gates on `+0x118` (record 7's workers, set by `0x44f6e7`) ∧ `+0x2c7`.  `0x4d8990` `{(6,10),(4,4),(3,10),
(6,3),(13,0),(4,18)}` is indexed by `+0x290`, which also selects the player's six-slot stock `+0x140 + k·4` (read by the armoury overlay
`0x418653` and the muster `0x4a50ae`, #154) — a per-weapon material cost, not a race weight.

## 144. Three new-game preset tables share one block of bytes

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

An oddity inside the per-player init: a four-iteration loop that ignores its
own counter.  Retail clears player record `+0x80` inside
`for (k = 0; k < 4; k++)` but the body never indexes `k`, so it writes the
same dword four times — a redundant zero, folded to a single write in the
port.  (The neighbouring `+0x140` loop *does* index its counter, so the two
are not the same pattern misread.)

The per-player commodity stocks `+0x120` / `+0x128` / `+0x130` are seeded to a
flat `0x32` and the six-slot stock `+0x140..` from the `0x4dc070` preset
*after* the county's `allocate_realm_workforce` → `provision` → `plan_realm`
calls (twice) — but nothing is clobbered: none of those passes (nor any
callee) writes the player's `+0x120..+0x134`; their writers are the resource
pass `0x44ea92`, trade `0x4284ce`, the AI `0x436d02` and events
`0x4494fe` / `0x4497ad` / `0x450ccd`.  The order is harmless.

The pass is also the **producer** of the per-player strategic block
(`g_player_strategic_352`, retail `0x57bf00`) whose `+0x120..` commodity
stocks the per-turn resource pass `0x44e852` later reads and writes (#154), and of the
runtime-only troop muster pool (`DAT_0053f6a0`, 6 × 0x80, not persisted) that
the recruitment UI and the deferred battlefield spawn (`0x4a9a9a`) consume.

> 📍 `src/game_init.c` (`engine_game_init_setup_realms`).  IDA
> `0x49bd99`; preset tables `DAT_004dc070`/`DAT_004dc0d0`/
> `DAT_004dc110` @ 0x4dc070; the `+0x80` no-index loop is
> `for (k…) dword_57BF80[88*j] = 0`.

*Re-checked 2026-11-18 against the IDA export: the "defaults overwrite the economy it just planned" oddity is withdrawn — no callee of 0x44f6e7 / 0x44df5f / 0x4485a5 writes the player's +0x120..+0x134, so the 0x32 seeding clobbers nothing; title shortened accordingly.*

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

The gate that lets any of this fire at all is the year
`DAT_00553edc > 0x4f4` (1268) **and** the county's owner being a **human**
player (`(&DAT_0057bf05)[owner·0x160] != 0`, the human/AI byte of #190) —
AI-owned counties never get a random event, though they still advance the
cursor.  A new game's first turn is already 1268 (#128), so events begin with
the first year rollover (into 1269).  The drop-in reproduces the deck byte-exact
and the single-draw walk; a host test
(`engine_events_scratch_and_gates`) pins the cursor arithmetic by landing a
chosen realm on a chosen deck slot via the seed.

> 📍 `src/engine_realm_events.c` (`engine_realm_roll_events` + `EVENT_DECK`),
> `src/rl_link_logic.c` (`rl_448819`).  IDA `0x448819` (`v1 = 2 * DAT_0058fd60`,
> per-realm `++v1 >= 256 → 0`, gate `byte_57BF05[352·owner] && DAT_00553edc > 1268`);
> deck = 256 u16 @ `0x4d6108`.

*Re-checked 2026-11-18 against the IDA export: the per-player gate is +5 = human-controlled (#190), not an "events-enabled" flag, so only human-owned counties roll events; 0x4f4 = 1268 means events start at the first year rollover, not "well into a game".*

## 147. (withdrawn) In spring and winter a realm can collapse without the player ever being told

Claimed `0x449889`'s band 1 is a "collapse" that seasons 1 / 4 hide.  The byte `+0x21b` is the county's **weather**
(L2.eng: 0 Frost, 1 Drought, 2 Sunny, 3 Cloudy, 4 Storms, 5 Flooding — matching `0x44c8ae`'s crop factors, e.g. season 4:
Frost ½, Sunny ×1½, Storms ½, Flooding ¼); the accumulator `+0x21d` gets a seasonal drift (`8 / 24 / 12 / −12` − rng/8) and classifies
`>= 95` → Drought (notice 0x8f, baked-field cell 0x18), `< 5` → Flooding (0x90, flooded-field 0x17).  In seasons 1 and
4 a Drought, or a Sunny with accumulator `>= 75`, becomes Frost (0) — so no drought notice is correct, nothing is
silently lost.  Still true: the demotion runs before the notice check, and `0x449d6e`'s season-2 arm has a dead
duplicate `cmp ecx, 4` (0x449e04 / 0x449e2b; the second, −24, is unreachable).

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

## 149. The ration→approval pass has six rows for a 0..6-clamped level — the seventh would read into the next table, but is never reached; a maxed approval of exactly 100 drops a band

The per-county pass `0x44bd9f` (`engine_realm_apply_tax_approval`; despite
the name its row index is the **ration** level) turns the county's effective
ration level `+0x15d` — the level the provisioning decision `0x44df5f` settles
on (#139) — into an adjustment of the approval value `+0x0b`:

```c
approval += tbl_0x4d64a8[level * 5 + band];   /* band = +0x09 */
```

The table at `0x4d64a8` has **six** rows (levels 0..5: none … triple
rations), `5 × i32` each — 0x78 bytes, ending at `0x4d6520`:

```
L0 −8 −10 −13 −16 −20   L1 −4 −6 −9 −12 −15   L2 −2 −4 −6 −8 −12
L3  8   4   2   1  −1   L4 12  8  4   2   0   L5 20 12  6   3   1
```

`0x44df5f` clamps the level with `> 6 → 6`, so on paper level 6 would index
`0x4d6520` — the band-threshold table this pass also reads — and add its
`(cutoff, band)` pairs (`10, 0, 35, 1, 65`) as adjustments.  But as #139
shows, the level is always 0..5 (it starts at the requested ration `+0x15e`
∈ 0..5 and is only decremented), so the row-6 read never happens.  The port
bakes the two tables as one contiguous `k_tax_approval_tbl[40]`
(`test_engine_realm_tax_policy6_aliasing` pins the hypothetical row-6 read);
harmless, since no input reaches it.

The real quirk is in the classification step.  After clamping approval to
0..100 (and forcing 50 for an empty AI county), the band is
`engine_math_lookup_threshold(approval, 0x4d6520, 5, 3)` over
`(10,0) (35,1) (65,2) (90,3) (100,4)`, which returns the first band whose
cutoff is *strictly greater* than the value, else the fallback 3.  `100 < 100`
is false, so a county pinned at exactly 100 falls through to **band 3**, one
band *below* a county at 99 (band 4).  Maxing out approval quietly costs a
band (`test_engine_realm_tax_clamp_high_and_100_quirk`).  The band then picks
the modifier `+0x10` from `0x4d6548` (`−10, −5, 0, 1, 2`).

> 📍 `src/engine_realm.c` (`engine_realm_apply_tax_approval`, `k_tax_approval_tbl`).
> IDA `0x44bd9f`, `0x404e4b` (`engine_math_lookup_threshold`), `0x44df5f`
> (the level's producer); tables `0x4d64a8`, `0x4d6520`, `0x4d6548`.

*Re-checked 2026-11-18 against the IDA export: the row index +0x15d is 0x44df5f's ration level (not a tax policy; 0x436435 does not write it) and is always 0..5, so the row-6 over-read is unreachable; the exactly-100 → band 3 fallthrough is real.*

## 150. The development-spread pass finds the best cell twice in a row, throwing the first answer away

The per-realm territory-development spread pass `FUN_0044bf7a`
(`engine_realm_spread_development`) distributes each realm's surplus pool across
its developable overworld cells, starting from the cell with the highest
current development value.  Finding that cell is delegated to a tiny helper that
scans the realm's 20-slot roster, skips empty slots and cells whose terrain type
is *below* 0x19 (only developed cells, type ≥ 0x19, count), and records the slot
with the maximum `+0x90` value into `+0x210`.  Retail ships the helper as **two** near-identical copies that differ
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

*Re-checked 2026-11-18 against the IDA export: the helpers keep cells with type >= 0x19 (the old text had the filter inverted); the rest holds (0x44c093 calls 0x44c5fc then 0x44c53b).*

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
ramp `-1 … -15`, and entry 51 is a spare zero before the ration→approval table
(`0x4d64a8`, quirk #149) begins — so a tax setting of 52+ would read into that
neighbouring table, kept latent only by the `< 0x32` increment guards
(`0x43aa83` / `0x43ab25`) and the AI writers `0x49d638` /
`0x49f431` (values ≤ 35).

> 📍 `src/engine_realm.c` (`engine_realm_recompute_realm_income`,
> `realm_refresh_player_income_totals`, `k_taxsetting_tally_modifier`).
> Ghidra `FUN_0044b80b` @ 0x44b80b, `FUN_0044ba35` @ 0x44ba35; tally table
> r2-dumped from 0x4d63d8.  Field semantics (+0xc0 displayed vs +0xbc collected)
> provisional but the suppression-gate asymmetry is exact.  Verified byte-exact
> vs retail (`tools/diff_test.py --functions realm_income`, 24/24).  The suppression
> gate is `+0x1a8` = `DAT_0053fb58`, set by events and cleared each turn by
> `0x448819`; IDA `0x44b59b` / `0x44b80b`.

*Re-checked 2026-11-18 against the IDA export: gate and field offsets confirmed; the tally table has 52 entries (index 51 is in-table), so the hazard starts at 52; the neighbour is the ration→approval table (#149).*

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

## 153. The season-4 production adjustment scales the wrong number — the standing crop, not the yield it just computed

The seasonal production pass `0x44c8ae` computes a season's output, then
applies the county's **weather** (`+0x21b`: 0 Frost, 1 Drought, 2 Sunny,
3 Cloudy, 4 Storms, 5 Flooding — see the withdrawn #147) to it before storing.
For seasons 1–3 the factor scales the value just computed: season 1 scales
the `0x44cfe1` output `+0x240` (Flooding ¼, Frost ½, Storms ½), seasons 2/3
scale the freshly grown crop `+0x244` in place (Flooding ½, Drought ½,
Sunny ×1½).  But the **season-4** branch (the harvest: its output is added to
the store `+0x1d4`) scales a *different* field:

```c
out = yield_phase4(key, workers, crop);   /* crop = record +0x244 */
record[+0x248] = out;
/* weather adjustment — note it reads +0x244 (the CROP), not `out`: */
if (w == 5) record[+0x248] = crop / 4;       /* Flooding, not out/4 */
if (w == 4) record[+0x248] = crop / 2;       /* Storms */
if (w == 0) record[+0x248] = crop / 2;       /* Frost */
if (w == 2) record[+0x248] = crop * 3 / 2;   /* Sunny */
```

Whenever the weather is Frost, Sunny, Storms or Flooding, season 4 throws away
the `yield_phase4` result (which accounts for the workers available to bring
the crop in) and substitutes a fraction of the **standing crop** — so the
stored harvest bears no relation to what the yield evaluator returned, and the
`+0x24c` delta field (stored − raw yield) records the full difference.  Only
Drought or Cloudy leave the genuine yield in place.

Whether this is a deliberate rule or a copy-paste slip from the season-2/3
branch (which legitimately scales its own `+0x244`) is impossible to tell from
the binary — but it is load-bearing: it runs every season-4 turn for every
county and is reproduced exactly by the port.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_season_update`).
> IDA `0x44c8ae` (`case 4:` arm).  Legacy evidence: `tools/diff_test.py
> --functions season_economy`, 128/128 across seeds (pre-rhost).

*Re-checked 2026-11-18 against the IDA export: the gating byte +0x21b is the weather (L2.eng names, 0..5), not a "model type"; +0x244 is the standing crop grown in seasons 1–3, not a cap; only Drought / Cloudy keep the yield; the "type 6" case does not exist.*

## 154. The "resource demand pool" is not its own table — it's accounts inside each player's strategic record, and smithy output is booked straight into the player's six-slot stock

The resource-delivery pass `0x44e852` and its leaves (`0x44ea92`
quota-apply, `0x44edc2` cell-stamp) read and write a per-player "pool"
addressed as `DAT_0057c020 + owner * 0x160`.  That looks like a standalone
6×0x160 runtime table.  It is not:

```
g_player_strategic_352 base = 0x57bf00   (0x160 stride, players 0..5)
"resource pool" base        = 0x57c020 = 0x57bf00 + 0x120
```

`0x57c020` is **field +0x120 of player 0's strategic record** — so
`pool[owner]` is just `strategic[owner] + 0x120` (also #179).  What
`0x44ea92` books, by resource slot `a2`, for the county's owner:

| slot | strat offset | effect |
|---:|---:|---|
| 1 | +0x120 | `+= delivered` |
| 3 | +0x128 | `+= delivered` |
| 0 | +0x130 | `+= delivered` |
| 2 | +0x140 + k·4 | `+= delivered`, where k = the county's `+0x290`; then `+0x130 -= delivered·cost_a[k]`, `+0x120 -= delivered·cost_b[k]` (table `0x4d8990`) and `0x4487a9` re-sums `+0x140..+0x154` into `+0x138` |

`+0x120` / `+0x128` / `+0x130` are commodity stocks (seeded to 50 per player
at new game by `0x49bd99`, #144; also traded by `0x4284ce`).  `+0x140..+0x154`
is a six-slot stock seeded from the `0x4dc070` preset and read by the armoury
overlay `0x418653`, the AI disband / garrison passes and the defender muster
`0x4a50ae` — the player's weapon stock, with `+0x138` its total; slot 2 is the
smithy turning the two commodities into weapon k at `0x4d8990`'s per-weapon
cost.  (The `0x4487a9` port name `engine_ranking_recompute_player_total` is
historical: it only sums the six slots.)

Consequence for any harness: there is **no separate global to inject**.
Binding the port's pool to `g_player_strategic_352 + 0x120` and
injecting / reading back the strategic block alone covers every pool write
*and* the `+0x138` resync.  A reader who treated `0x57c020` as an independent
table would either miss the strategic-record aliasing or double-allocate it.

> 📍 `src/engine_army_economy.c` (`engine_army_economy_resource_pass` +
> `..apply_resource_quota` / `..stamp_resource_cell`).  IDA `0x44e852` /
> `0x44ea92` / `0x44edc2` / `0x4487a9`.  Legacy evidence: `tools/diff_test.py
> --functions resource_pass`, 24/24 across seeds 0+7 (pre-rhost).

*Re-checked 2026-11-18 against the IDA export: the aliasing holds; the field meanings were wrong — +0x120/+0x128/+0x130 are commodity stocks credited per slot, not demand columns; +0x140.. is the six-slot weapon stock indexed by the county's +0x290 (not a race), and +0x138 its total, not a cashflow ledger.*

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

Retail shows where the reinstall lives: the base palette is read once at
start-up into the buffer `0x5691f0` (`0x499859`, the 13-file system-graphics
load, entry 0 = `base01.256`), the title pages `0x41e1dd` / `0x41e61d` load
`gateway.256` / `skirmish.256` / `custom.256` over the active palette, and the
strategic-screen render `0x40f5fd` ends every full draw (`a1 < 3`) with
`0x4b0ab5(&0x5691f0)` — expand the 6-bit entries ×4, zero entry 0, push to
DirectDraw.  The load path reaches it via `0x4976a1` → `0x40f5fd(1)`.

**The transition into a screen owns its palette.**  The drop-in's menu→map load
(`drop_in_load`) reinstalls `base01.256` after the map setup; any future screen
that arrives from a differently-palettized one will need the same.  Symptom to
watch for: a structurally-correct frame that is uniformly mis-coloured (often
magenta, since unused/parchment indices land on magenta in foreign palettes) —
suspect the palette before the render.

> 📍 `src/rl_ui4.c` (`rl_40f5fd`), `src/rl_host.c` (`rl_4b0ab5`),
> `src/main.c` (`drop_in_load` → `gfx_load_palette("base01.256")`); the
> menu page palettes are in `gfx.c` (`gfx_title_background_load`); IDA
> `0x40f5fd`, `0x4b0ab5`, `0x499859`.  Found live 2026-06-02 when the wired
> menu→load path rendered the map magenta.

*Re-checked 2026-11-18 against the IDA export: added retail's mechanism — 0x40f5fd re-pushes the base01 palette buffer 0x5691f0 through 0x4b0ab5 on every full strategic draw — and the rl_ pointers.*

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
Ported as `engine_realm_build_coalitions` (engine_realm.c, wrapped by
`rl_44b042`).  The consumer half `0x44ae51` — keep each player's strongest
group (ties: the later one) and demolish every county of its other groups
through `0x4ac3c6`, telling the local player (event 127 / 128) — is ported too:
`engine_realm_coalition_pass` (`0x44ae3c`) and `rl_44ae51`.  (The "armies"
here are the 0x300-stride per-county records; the rl_ ports call them counties.)

> 📍 `src/engine_realm.c` (`engine_realm_build_coalitions`,
> `engine_realm_coalition_pass`), `src/rl_ctl.c` (`rl_44b042`, `rl_44ae51`);
> IDA `0x44b042`, `0x44b391` (join), `0x44b302` (seed), `0x467e2c`
> (directed adjacency), `0x44ae51`.

*Re-checked 2026-11-18 against the IDA export: round structure, directed adjacency and the 99-round bound confirmed; the consumer 0x44ae51 is no longer deferred (commit a7159da).*

## 160. The unrest pass warns a human realm exactly once, and the warning latch can leave a low-approval realm permanently calm

`FUN_0044aa41` escalates each realm's unrest band (+0x20) every turn, but the
human-player branch is gated by a one-shot warning latch (+0x21) that produces
two non-obvious behaviours.

**(a) One warning per "spell", cleared only by a real recovery.**  The first
turn a human realm's approval (+0x0c) dips below 30 it posts a single warning
(event 0x92) and sets the latch — and does *nothing else* that turn (no unrest
change).  The latch is cleared only when approval climbs back to 30 or more.  So
the player is warned once as the realm slips, not every turn.

**(b) The escalation threshold (25) is below the warning threshold (30), so a
realm parked in [25, 29] is calm forever after its one warning.**  Once latched,
the pass escalates unrest only while approval < 25; at approval ≥ 25 it sets
unrest back to 0.  A realm hovering at 25–29 approval is below the warning line
(so it never re-warns — the latch stays set) yet at/above the escalation line
(so unrest is held at 0).  It sits in a quiet limbo: visibly unhappy, never
revolting, never re-flagged.  Only a drop below 25 starts the band climb
(0x96→0x99 notifications) toward a rebellion.

**(c) The rebellion is attempted only on the turn unrest RISES to 4.**  Both
branches reach the shared tail (0x44adf3: `unrest >= 4 && FUN_004ac185(realm)` →
unrest = 0) only when the new level is above the turn's starting level.  Unrest
is capped at 4 and climbs by 1, so the rebellion spawn is tried exactly once, on
the turn the band first hits 4; if `FUN_004ac185` returns 0 the realm sits at 4
with no retry until the band falls and climbs back.

AI realms use neither the latch nor the 0x92 warning — they drift purely on
approval (≥41 calm, 11–40 ease, <1 climb).  The human branch's
`approval <= 0 → unrest = 4` arm (0x44abcf..0x44abeb) is dead code: it is the
`else` of the `approval < 25` test nested inside the `approval < 25` arm, so it
can never be entered, and the port elides it.  Ported as
`engine_realm_escalate_unrest` (src/engine_realm.c); the rebellion trigger
`FUN_004ac185` is now ported and wired (`engine_combat_spawn_rebellion`,
src/engine_combat.c — it used to be an unbound seam, quirk #228).  rhost hook
`realm_escalate_unrest` 0x44aa41 (tools/rhost/rh_hooks_ai.c).

*Re-checked 2026-11-18 against the IDA export: 0x4ac185 is no longer a deferred seam; added (c) — the rebellion tail runs only on a rising turn; latch clears at ≥ 30.*

## 161. The player-aggregate recompute averages the per-realm "standing" byte in 8-bit arithmetic, so a populous player's average can wrap

`FUN_0049d1e0` (ported as `engine_ranking_recompute_player_aggregate`,
src/engine_ranking.c) recomputes one player's standings inputs from what it
owns: the owned-COUNTY count (+0x29 — the port's legacy "army count": the
counties are the `g_per_army_768` records, see game-state-map.md "Naming trap"),
total/avg/previous county strength (+0x10/+0x14/+0x18, from county +0x24), an
averaged rating (+0x0c, from county +0x0c approval) and standing (+0x58, from
county +0x0b), county-share percent (+0x60 = `FUN_00404dc1(+0x29, county count)`),
and the count of the player's live army pieces (+0x2c, map pieces with kind
+0x08 == 1) and their summed men (+0x54, piece +0x168).

Two of those averages accumulate into **byte** fields before the divide, and the
binary does the running sum in 8-bit registers:

- The standing accumulator at +0x58 is a `byte += byte` (AL) add, so once the
  per-county standing values sum past 255 it **wraps mod 256** — and the average
  is then taken of the *wrapped* value.  A player with two counties at standing
  200 sums to 400 → byte 144 → averaged /2 = 72, not 200.  This is not a sign
  bug to "fix": the resulting save byte is what retail writes, so the port keeps
  the 8-bit add (`p[0x58] = (uint8_t)(p[0x58] + r[0x0b])`).  The rating
  accumulator, by contrast, is a real `int` (sign-extended `(char)` adds), so it
  does not wrap in practice.

- All four divides are guarded on the county count: a zero-owned player gets
  every average set to 0 rather than dividing by zero.  Player 0 (the
  neutral/unowned slot) is special-cased even earlier — after the head stores
  (+0x29/+0x10/+0x0c/+0x58 cleared, +0x18 ← old +0x10) it returns before any
  averaging or the piece loop, so its +0x14/+0x60/+0x2c/+0x54 keep whatever
  they held.

The previous-turn strength snapshot (+0x18 ← old +0x10) is taken *unconditionally*
at the top, before the player==0 / zero-owned gates, so even an empty slot rolls
its strength history forward.  rhost hook 0x49d1e0 in tools/rhost/rh_hooks_ai.c
(the earlier Frida `player_aggregate` 192/192 diff predates the rhost method).

*Re-checked 2026-11-18 against the IDA export: field roles corrected (+0x29 = owned counties, +0x2c/+0x54 = army pieces / their men, not territories); player-0 head stores listed exactly; verification pointer → rhost.*

## 162. Player elimination shares the victory message's rotating counter, advances it even when no message is shown, and only opens the game-over screen in multiplayer

`FUN_0049b42b` (ported as `engine_ranking_check_elimination`, src/engine_ranking.c)
is the exact structural mirror of the victory branch inside the standings
recompute (quirk-free `engine_ranking_recompute` Phase G): same per-player
rotating counter at +0x159, same `(rot − 4) + opp_type·4` payload (on the 0xc2
post), just the defeat codes 0xe0/0xc2 in place of victory's 0xe1/0xc3.  It acts
only on a player 1..5 whose +0x04 is still non-zero on entry.

Three non-obvious behaviours:

**(a) Liveness is weighted 3:1 — counties over armies.**  A player is "alive"
iff +0x04 (which doubles as the active-slot flag the recompute reads) is
nonzero, and it is rebuilt each call as `3·(owned counties, +0x05 of the
0x53f9b0 records) + 1·(owned army pieces, map-piece kind +0x08 == 1)`.  So one
county outweighs two field armies; only a player with *no* county and *no*
army piece is eliminated.  (The port's names are swapped: its "armies" are the
county records, its "territories" the map pieces — game-state-map.md.)

**(b) The rotating counter advances on every elimination, even silent ones.**
The `+0x159 += 1` (wrap 0..3) sits *outside* the notification if/else, so it
ticks whenever the player is eliminated — including the third case where nobody
is notified at all (a non-local human player: not the local slot, so no 0xe0,
and +0x05 != 0, so no 0xc2).  The counter and the local human therefore drift
out of step with the messages actually shown.

**(c) The game-over screen is multiplayer-only.**  The local-defeat screen
(`FUN_0043eda0(0x24, 0)`) fires only when the eliminated player is the local
slot *and* `DAT_00553030` (g_sim_mp_active, threaded as `mp_active`) is
nonzero.  In single-player a local elimination posts the 0xe0 notification but
does **not** trigger this screen here — that path is handled elsewhere.  In the
port it is the `s_game_over_fn` hook, bound to `seam_game_over` in
src/rl_link_logic.c.

rhost hook 0x49b42b in tools/rhost/rh_hooks_combat.c (the earlier Frida
`elimination` 192/192 diff predates the rhost method).

*Re-checked 2026-11-18 against the IDA export: liveness weights restated (3 per county, 1 per army piece); game-over hook is bound now (rl_link_logic.c), not a NULL seam.*

## 163. The post-battle condition recompute returns garbage (the caller ignores it), and the survivor percent is written before being clamped

`FUN_004a80db` (ported as `engine_combat_recompute_condition`, src/engine_combat.c)
recomputes an army piece's **siege-engine build state**, not a supply condition:
for each of the 3 engine slots (count +0x182 / percent built +0x184 / build
progress +0x186, i16 triples) against the per-engine work table
`DAT_004de440 = {200,200,400}` (`k_siege_engine_work`, docs/data/catalog.tsv), it
stores the unfinished work averaged over the piece's men (+0x168, rounded up)
into +0x19c — the "seasons until the assault" byte.  It is the same arithmetic as
the per-turn build step `FUN_004a8507` (quirk #182) without the build pour.

Two oddities:

**(a) The return value is dead.**  The function is declared `int`, but none of
its callers uses the result: `FUN_00437fd7`, `FUN_0043b7c4`, `FUN_004aa181`
(merge) and `FUN_004ab383` (aftermath, twice) call it as a statement, and
`FUN_004a7eb5` tail-returns it to `FUN_004a7e0a`, which discards it.  The
returned value is `work / men` — except when there is a nonzero remainder, in
which case it is *overwritten* with `rec * 0x54` (0x4a8287..: the record index ×
the 0x1a4/5 stride factor the address arithmetic left in eax) right before
returning.  Pure garbage; the port returns `void`.

**(b) The percent is written, then conditionally re-written.**  For each slot
the percent `progress*100/(work*count)` is stored into +0x184 first; only then is
it compared against 100 and, if it reached or exceeded 100, overwritten with
exactly 100.  The intermediate write is invisible (same field), but it means an
over-built slot lands on a flat 100, never its true ratio.  A slot with count ≤ 0
gets percent and progress zeroed.

rhost hook 0x4a80db in tools/rhost/rh_hooks_combat.c (the earlier Frida
`combat_condition` 64/64 diff predates the rhost method).

*Re-checked 2026-11-18 against the IDA export: the fields are the siege-engine build slots (+0x19c = seasons to assault), not supply groups; the callers are five functions (0x437fd7, 0x43b7c4, 0x4a7eb5, 0x4aa181, 0x4ab383), not "two in the aftermath".*

## 164. The combat-aftermath diplomacy hit double-counts the loser via the broadcast penalty, and its war-declaration escalation only ever fires against a human

`FUN_004a1ee1` (ported as `engine_diplomacy_battle_adjust`, src/engine_diplomacy.c)
is the post-battle relations adjustment of player `a1` toward `a2` — AI-only (it
no-ops unless `a1` +0x04 is set and +0x05 is clear; both 1..5 and distinct).

**(a) The actor's own relation toward the other side is decremented twice on a
betrayal.**  When `a2`'s alliance partner (+0x81, game-state-map.md) is `a1` —
i.e. the AI was attacked by / fought its own ally — the function clears the
treaty (`FUN_004a1a54`), flags the pair (+0x88, and +0xeb if unset) unless the
personality (+0x07) is 4, posts 0xb6, and calls
`engine_diplomacy_broadcast_penalty(a2, 15)` (`FUN_004a24d1`), which loops *every*
other player 1..5 — including `a1` itself — and subtracts 15 (floor −30) from its
relation (+0x84 + a2·0x10) toward `a2`.  Then the main hit subtracts the (now
+15-bumped) amount a second time.  So the actor takes a −15 broadcast hit *plus*
the −(amount+15) direct hit in one call.

**(b) The escalating war-declaration sequence is gated on a HUMAN opponent.**
The 0xbd → 0xbe → 0xbf escalation (driven by the per-pair counter +0x87) only
runs when the pair flag +0x88 is clear, the relation sits at the −30 floor, and
`a2`'s +0x05 is nonzero — i.e. a human.  Two AIs that hate each other never walk
this ladder; they just sit pinned at −30.

**(c) The notification variant is sampled before the mood counter advances.**
Each notification's payload is `(mood-cycle − 4) + personality·4` read from the
current +0x159, which is only bumped (wrap 0..3) *after* the post — the same
rotating-counter pattern as quirks #162/#147, here shared across all four
diplomacy codes (0xb6/0xbd/0xbe/0xbf).

rhost hook 0x4a1ee1 in tools/rhost/rh_hooks_combat.c (the earlier Frida
`diplomacy_adjust` 64/64 diff predates the rhost method).

*Re-checked 2026-11-18 against the IDA export: +0x81 is the alliance partner, not a lord/vassal link; the +0x88 gate on (b) added.*

## 165. The combat-init SCC resolves into the region of the last-classified cell's occupant, clears quick-resolve on every dispatch, and lets a winner immediately lose a too-distant conquest

The strategic-combat chokepoint is a 5-function mutually-recursive cluster
(`FUN_004ac3c6` demolish → `FUN_00437535` transfer → `FUN_004a7158` dispatch →
`FUN_004ab383` aftermath → `FUN_004a72fe` ownership-transfer → back to demolish),
ported into src/engine_combat.c.  Four non-obvious behaviours:

1. **The county handed over is the +0x10 county of `DAT_00553f8c`, the occupant
   the movement classifier last met — not necessarily this battle's.**
   `FUN_004a7158` seeds the combat county scalar `DAT_0056822c` from
   `(&DAT_0052f0c0)[DAT_00553f8c * 0x1a4]`, piece +0x10 of piece
   `DAT_00553f8c`.  That global is not a UI selection: it is the occupant of the
   cell being stepped into, written by the move evaluator `FUN_00466893` (cleared)
   / `FUN_00466c3c` (cell +5) and by the AI nearest-army scans
   `FUN_004673b8` / `FUN_00467532`.  On the normal march path
   (`FUN_004658c1` → `FUN_004a7158(mover, DAT_00553f8c)`) it IS the defender, so
   the county is the defender's.  On the transfer path (`FUN_00437535` →
   `FUN_004a7158(piece, piece+0x19a)`, reached from demolish) it is whatever the
   last classification left; only a non-zero dispatch return (the interactive
   battle) overwrites `DAT_0056822c = county`, so on auto-resolve the aftermath's
   `FUN_004a72fe(winner_owner, DAT_0056822c)` hands over that stale county.  The
   port keeps the same global (`g_combat_active_terr`, src/engine_combat.c /
   engine_army_move.c).

2. **`FUN_004a7158` clears `DAT_0053e8fc` (quick-resolve) to 0 unconditionally
   at entry**, before both the resolver and the aftermath run.  So the
   difficulty multiplier (quirk in `combat_resolve`) and the aftermath's
   `DAT_0053e8fc != 0` quick-paths *never* fire when combat is reached through
   dispatch — those belong to the aftermath's other callers.  The dispatch path
   always takes the `recompute_condition` (`FUN_004a80db`) arm.

3. **The two aftermath win-branches are asymmetric.**  Attacker-win may run the
   ownership transfer (`FUN_004a72fe`, only when the loser had +0x198 garrison or
   +0x167 set) and adds 8 to the winner's moves-used +0x153, then, if the winner
   is ungarrisoned (+0x01 == 0), resets it to `+0x154 − 1`; defender-win does *no*
   ownership transfer and adds 7 (ungarrisoned only).  The quick-path field
   clears also differ between the branches.  They are written out explicitly
   rather than factored, to preserve the byte sequence.

4. **A winner can immediately lose a too-distant conquest — and "already
   conquered" really means "already owns a county".**  `FUN_004a72fe` first calls
   the standings aggregate `FUN_0049d1e0`, which *overwrites* the player's
   `+0x29` byte with the number of counties it currently owns.  The very next
   can-hold test reads that freshly-recomputed `+0x29`: if it is non-zero *and*
   the player owns no neighbour of the county (`FUN_00467eb0` == 0), the
   just-won county is demolished (`FUN_004ac3c6`, recursing the cycle; event 0x81
   to the local player) instead of held — you cannot annex a county you cannot
   reach.  So `+0x29` is not a persistent "times conquered" counter here; it is
   the live ownership count the aggregate just wrote.

The interactive path (`DAT_004eac50` 0x12/0x13 screen state, the
s080/ff_lose/ff_batl WAV cues, the battlefield render handoff) goes through the
seams `engine_combat_bind_wav_fn` / `_click_fn` / `_clock_fn`, now bound in
src/rl_link_logic.c.  All five functions are rhost hooks
(tools/rhost/rh_hooks_combat.c); the earlier Frida `combat_ownership` 32/32 diff
predates the rhost method.

*Re-checked 2026-11-18 against the IDA export: DAT_00553f8c is the movement classifier's met occupant (0x466893 / 0x466c3c), not the UI-selected territory; transfer is conditional on the loser's +0x198/+0x167; +0x153 is moves used; seams are bound.*

## 166. Coalition consolidation walks player slots 1..5, gates on the +0x04 slot byte, and breaks strength ties toward the later group

`FUN_0044ae51` (the consumer half of the turn-driver's coalition pass
`FUN_0044ae3c`, which first rebuilds the group table via `FUN_0044b042`) keeps
each player's strongest coalition group (17 groups × 0x1c at 0x568240: strength
i32 +0x00, owner +0x04, up to 20 member counties +0x05..) and demolishes every
member county of all the player's weaker groups (`FUN_004ac3c6`), so a player
whose counties split into disconnected clusters loses all but the strongest
cluster each turn.  Three details are worth pinning:

1. **The player loop is `1..5`** (`cmp 6` / `jge` at 0x44ae69) — the five
   player slots of the 6-slot strategic block, like every per-player pass.  (An
   earlier note contrasted it with the county count `s_slot_0056d5dc`; that is a
   different axis — counties, not players — so nothing is skipped.)
2. **The in-game gate reads strategic `+0x04`** (`0x57bf04 + slot*0x160`), the
   slot-active byte — a *different* byte from the `+0x05` human byte that the
   event-queue filter (`engine_event_queue_post`) consults for the same slot.
3. **The strongest-group search compares with `jg`** (0x44af02), i.e. it
   updates the winner on `max <= strength`, so equal-strength groups resolve to
   the *later* group in the table.  (`best_group` is never explicitly
   initialised, but a live player always has at least one matching group, so it
   is always set before the demolish pass reads it.)

The notification is posted only when the affected player is the local POV
(`s_slot_0057c8cc`) *and* they held more than one group: code `0x7f` (with the
lost county id in `flag6`) when exactly one county was demolished, else code
`0x80`.  Ported as `engine_realm_consolidate_coalitions` (src/engine_realm.c) and
the retail-layout `rl_44ae51` (src/rl_ctl.c); rhost hook 0x44ae51 in
tools/rhost/rh_hooks_rl_ctl.c (no longer host-test-only).

*Re-checked 2026-11-18 against the IDA export: members are counties (demolish_realm), the 1..5 loop is the player range (not a missed county range); verification is now rhost.*

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

`FUN_004aa181` (the army-stack merge, `engine_combat_merge_stack`) folds piece
`src` into `dst`.  It is reached from the march evaluator `FUN_004658c1` (an
army stepping onto a same-owner kind-1/2 piece) and from the garrison-placement
path `FUN_004a79a3`; it is a no-op when the combined men exceed 1500, and posts
0xa7 and refuses when both sides carry a mercenary contingent.  When `src` is the
one carrying the contingent, it copies src's contract triple — class `+0x195`,
head-count `+0x196`, **band index `+0x197`** — onto `dst`, sums the men / troop
classes / siege slots, then unconditionally disbands `src` (`FUN_004aa039`).

The disband routes through `FUN_0046f0a9` → `FUN_004ac6fe`
(`engine_unit_clear_contract`), which subtracts src's +0x196 from *src's* men and
zeroes the hired-by back-reference `word_568DC0[src.+0x197 * 10]` — freeing
*src's* mercenary band.  But the merge never cleared src's `+0x197` after copying
it, so src still names the band at disband time.  The result: `dst` ends the
merge holding a mercenary contract (`+0x195/+0x196/+0x197` all set, and the merc
men counted in its +0x168) whose band (`0x568dc0 + b*20`) now reads as free
(hired-by 0) and can be offered / hired again.  The survivor's band index is
dangling.

Ported verbatim (the merge copies the triple, the disband frees the band) and
pinned by `test_engine_combat_merge_inherits_src_merc`
(tests/test_engine_combat.c).  The merge is now also an rhost hook
(`combat_merge_stack` 0x4aa181, tools/rhost/rh_hooks_combat.c), which supersedes
the old "host-unit-tested only, the price-scalar wall keeps it off the retail
diff" note.

*Re-checked 2026-11-18 against the IDA export: added the main caller 0x4658c1 and the 1500-men / both-merc gates; only the band's hired-by word is cleared; verification is rhost now.*

## 169. The settlement-spawn cluster has no audio — and a race-0 (independent) garrison spawns with owner 6, which the income pass reads one slot past the strategic array

The per-turn settlement / construction-completion pass `FUN_004508de` and its
spawn subtree (`FUN_004a551b` find-slot → `FUN_004a9a9a` spawn → `FUN_004a79a3`
place-garrison, plus the four pure leaves `FUN_004a9f18` provision-debit /
`FUN_004a9f72` roster-load pick / `FUN_004a9eb1` cashflow-debit / `FUN_004ac7f3`
muster-attach) were long deferred as "spawn + SFX" pending audible verification.
**There is no WAV / sound call anywhere in the chain** (no `FUN_00427990` among
the callees in functions_ida.tsv) — the only side effects beyond record mutation
are two render-dirty signals (`FUN_0046e067` draw-box + `FUN_0045240a`
full-screen, fired only when the spawn belongs to the viewing player; and
`DAT_005530d0` view-dirty in place-garrison).  The cluster is pure logic +
render-dirty flags.  (Audio for *new-game* troop spawns lives elsewhere; this
turn-pass spawn path is silent.)

The second half is a faithful out-of-bounds read.  `FUN_004a9a9a` sets a fresh
piece's owner byte (`+0x00`) to **6** when its `owner` argument is 0 (a neutral
county; 6 is the owner id the merchants also use — game-state-map.md).  The
spawn then calls `engine_realm_collect_territory_income` (`FUN_004ad6c0`), whose
pass 2 reads the owner's alliance partner `STRAT_REC(piece_owner)[0x81]` for every
kind-1/2 piece standing in a county it does not own — i.e.
`g_player_strategic_352[6 * 0x160 + 0x81]` = 0x57c7c1.  The strategic array is
only six slots (`2112 = 6 × 0x160`, save entry 3), so slot 6 is one past the
end.  Retail reads whatever the linker placed there (harmless in its flat address
space); under ASan the host build flags it.  This is not introduced by the port —
`collect_territory_income` already had the read — but the race-0 spawn is what
feeds an owner-6 piece into it.  The owner-0 spawn path is therefore not
host-unit-tested (it would need a 7-slot strategic array).

`FUN_004a9a9a` also re-runs the per-county economy trio
(`FUN_0044f6e7`/`FUN_0044df5f`/`FUN_004485a5`) **twice, back to back, verbatim**
— ported as a 2-iteration loop, not deduplicated.

Verification: `FUN_004a9a9a` (tools/rhost/rh_hooks_ai.c) and `FUN_004a551b` /
`FUN_004a79a3` (tools/rhost/rh_hooks_combat.c) are rhost hooks now; the old
Frida `place_garrison` 96/96 diff and the "spawn is host-unit-tested only" note
predate the rhost method.

*Re-checked 2026-11-18 against the IDA export: owner-6 read is at 0x4ad6c0 pass 2 (only for pieces in a foreign county); verification paragraph replaced (rhost hooks exist for the spawn chain).*

## 170. The year-rollover CD-audio probe's path-template `memcpy` overruns the `"c:.\wav"` literal into the adjacent pooled string literals — and every flag write stores the same value, so the file checks are dead

The turn-advance engine `FUN_00448440` calls `FUN_004aef7e` once per year, on the
autumn→winter rollover (the branch taken when the new current season
`DAT_0057c934` is 4 = Winter; seasons run 1 Spring .. 4 Winter and the year
rolls entering Winter — game-state-map.md) to probe the CD for its full audio
set.  Two faithful oddities, both reproduced verbatim in
`io_path_probe_cd_audio` (`src/io_path.c`) and in the retail-layout
`rl_4aef7e` (src/rl_host.c, rhost hook 0x4aef7e):

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
page 2 vs the deferred profile page).  Retail stores **1** in four places (four `mov dword_5C9A74, 1` in
0x4aef7e): the unconditional store at entry, the redundant
`if (DAT_0053f684 == 1)` branch, and both `filelength >= 151000000` ("full audio
CD present") branches.  Every store is
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

*Re-checked 2026-11-18 against the IDA export: the probe runs on the autumn→winter rollover (season 4 = Winter), not winter→spring; template bytes at 0x4df3ac and the four gate stores confirmed; rl_4aef7e pointer added.*

## 171. The very first turn-driver run of every new game takes the year-rollover branch, so the CD-audio probe fires on turn 1

The new-game initialiser `FUN_00497ced` seeds the calendar (inline in
0x497ced; port `engine_game_init_seed_scalars`, src/game_init.c) to turn 0 with the season triple
`{prev 2, cur 3, next 4}` (`s_slot_00554470`/`0057c934`/`0057c92c`), then calls
the turn-advance engine `FUN_00448440` once to tick the fresh world to turn 1.
But the calendar block's year-rollover predicate (quirk #170's caller) is "the
just-consumed season-next was 4" — and the seed *is* 4.  So the first advance
**always** rotates the year: rollover counter `s_slot_0055cd5c` 0→1, year triple
1267→1268, and `io_path_probe_cd_audio` (`FUN_004aef7e`) runs.  In other words
the seasonal CD-audio re-probe is not a once-a-year-from-now event — it fires on
literally the first turn of a brand-new game (season then reads cur=4 / Winter,
next wraps 5→1).  The seed (cur 3 = Autumn, next 4 = Winter) makes turn 1 the
autumn→winter boundary, so the game opens in Winter 1268 — the year rolls on
entering Winter (game-state-map.md: Winter 1268 → Spring 1268 → …).

Verification: the host test `game_init_turn_driver_advances_to_turn_1`
(`test_game_init.c`) binds the assembled driver into the new-game initialiser's
step-17 seam and runs a full new game over the real L2_maps world, asserting the
exact post-state — turn 1, season `{prev 3, cur 4, next 1}`, rollover counter 1,
year 1268 — which is only reachable through the rollover branch.

*Re-checked 2026-11-18 against the IDA export: the boundary is autumn→winter (season 4 = Winter), not winter→spring; seeds 1266/1267/1268, season {2,3,4}, turn 0 confirmed in 0x497ced.*

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

## 173 — the new-game player roster is config-screen output, not init output; +0x07 is the AI lord's identity / personality (assigned only to the first `DAT_0053f268` AI slots), and the AI count gate is `DAT_0053f268`

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

2. **player_strategic `+0x07` is the AI lord's identity (personality 1..4) —
   the "NPC work-location" name the port gave it is wrong.**  `FUN_0049caaa`
   (`engine_npc_schedule_init`, init step 4) CLEARS `+0x07` for every slot 1..5,
   then, for the first `s_slot_0053f268` non-human (`+0x05 == 0`) slots, first
   gives the slot a free colour/seat index `+0x0a` (1..5, skipping the humans'),
   then picks `+0x07` from the 4-entry candidate row `DAT_004dc17c[+0x0a*4]` (MP /
   `DAT_0055302c == 1`) or `DAT_004dc180[(map&3)*20 − 4 + +0x0a*4]` (SP), first one
   not yet taken, and loads that lord's name via `localization_seek_skip(7, +0x07)`
   into the slot's name (`0x553d54 + slot*0x2c`).  AI slots beyond the count get
   `+0x04 = 0`.  Everything downstream reads `+0x07` as the personality index:
   `DAT_004d8a58[(+0x07)*0xf0 − 0xf0]` rows (quirks #188/#191/#193), the
   `+0x07 != 4` test in `FUN_004a1ee1` (#164), the notification payload
   `(rot − 4) + +0x07·4` (#162/#164/#190).  The inactive-purge (`0x49b35e`) wipes a
   slot unless it is active and (human or `+0x07 != 0`) — so an AI slot survives
   iff it was given a lord.

3. **The AI count is `DAT_0053f268`, computed by the unported `FUN_00499dc3`**
   as `(DAT_0053f288 + 2) - DAT_00553f98` = total_players - humans.  The seat
   never set it → it stayed 0 → `FUN_0049caaa` assigned no lord (`+0x07`) → every
   AI slot read as empty and got purged, leaving a 1-player (human-only) world.

The fix (`engine_game_init_set_player_config`): seed per-slot `+0x05` has_data /
`+0x0a` seat (colour) / `+0xee` player-bit, the local player `DAT_0057c8cc`, the AI
count `DAT_0053f268`, and the human's aux seat reservation
(`g_player_aux_44[+0x25/+0x27]`, retail's SP `player_seat_assign`) so the
assignment hands the AI players the seats / lords *around* the human's — without
it the AI `+0x0a` / `+0x07` picks shift and diverge.  Also corrected the map-0 presets to
the captured retail values (economy 1 / resource 2 / gold 1000 / keep 3, NOT the
scenario-table values assumed earlier).  Result: post-init `player_strategic`
matches retail 2108/2112 bytes (the 4 residual = AI `+0x130` strategy field,
shared root with ~90 `per_army` AI-economy bytes — a separate turn-driver/AI
byte-completeness frontier), `cur_player`/roster/armies/AI lords (+0x07) exact, and
a retail LOAD of the resulting save sits stable (no turn spam — `year`/`season`
set once and hold; run `newgame-loadverify1`).

*Re-checked 2026-11-18 against the IDA export (0x49caaa, 0x49b35e, 0x499dc3): +0x07 is the AI lord / personality id picked from DAT_004dc17c / DAT_004dc180 and named from text group 7, not a work-location; points 1 and 3 stand.*

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

**Role correction (2026-11-18).**  The pieces `place_army_starts` (`FUN_00427ed0`)
places are the **merchants**, not armies: for each of the ≤ 6 bucket rows it takes
the row's digest county `DAT_00569518[i]`, probes around that county's capital
cursor (+0x6c/+0x6d, the stride-0x300 read above), and calls
`FUN_0046e1b0(3, col, row, 6)` — map-piece kind 3, owner 6 (merchant;
game-state-map.md) — stamping +0x10 / +0x167 = the county, +0x166 markup 100,
+0x1ff = row i; `DAT_005530b4` (save entry 78) is the merchant count.  It is the
only kind-3 placement in the binary.  Read "armies" above as merchants.

*Re-checked 2026-11-18 against the IDA export (0x427ed0, 0x428007, 0x468d4f): the stride-0x300 fix stands; the placed pieces are kind-3 merchants (owner 6), not armies.*

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
shared stir `FUN_00404a46` (one stir per iteration of its three loops: the
`active`-seat roster shuffle, the 48-entry permutation `DAT_0057cae0`, and the
48-entry value table `DAT_00553080`).  It is the **only** consumer of that global on the new-game init
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

*Re-checked 2026-11-18 against the IDA export (0x497e65, 0x404a46): one stir per iteration across three loops (active + 48 + 48), not three per iteration; the active + 96 total stands.*

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
mechanic, not a bug.  (If the mover is human, the county owner's attitude toward
it also drops 10 via `FUN_004a1ee1`.)  Outside that case — own county, or a tile
outside terrain 2..0x16, or a kind-3/4 piece — the blocked branch razes nothing,
yet the code is still rewritten to 1 and the step committed.

Two secondary gotchas in the same function:

- **The moves-used counter (+0x153; moves left = 15 − it, game-state-map.md) is
  bumped twice on an impassable step**: once inside the blocked branch (`+= 3`,
  kind ≠ 3/4) and again in the classifier's common tail (`+= 3`, since the rough
  flag was just cleared) — net 6 moves, vs 3 for a plain open step and 1 for a
  rough one.  Not a double-count bug; retail does exactly this.
- **The retry budget reads the PREVIOUS step's rough flag** (+0x14d): a stack
  that stepped onto rough terrain last tick gets budget 0 (wraps its retry
  counter immediately, ramping the stall accumulator faster) instead of the
  normal budget 3.  The flag is the single sticky `M_ROUGH` byte that
  `commit_step` writes from the entered cell and the classifier both reads (for
  the budget) and rewrites (from the new step outcome).

*Re-checked 2026-11-18 against the IDA export (0x46634d, 0x46673c, 0x469e5b → set_cell_type(cell,0,0)): +0x153 is moves used, not path progress; the raze only fires for a foreign county on land terrain, the commit always.*

## 181 — the movement executor `FUN_00465d28` reads an UNINITIALISED waypoint on its disarmed first pass (NOT harmless — see #229)

`engine_army_move_run` only assigns its waypoint locals (retail stack slots
`[ebp-0xc]` col / `[ebp-0x10]` row) inside the **step-eval-armed** branch
(+0x14b bit0).  But the common tail after that branch unconditionally evaluates a
direction from them (`engine_path_direction_8way`, `FUN_0046f434`) and, unless it
is 8, feeds it to the classifier.  On a pass where the flag is disarmed, retail
reads whatever those stack slots last held.

What the garbage does:

1. The **disarmed** classifier path (`engine_army_move_classify_step` with
   +0x14b bit0 = 0) runs only the stall/retry hysteresis and **never reads its
   direction argument** — so on that path the direction is discarded.
2. But the `dir == 8` ("standing on the waypoint") exit before the classifier
   call is live: it sets +0x14c = 0, +0x150 = 1 and the army stops.  The slots
   hold the previous call's residue at the same stack depth — in practice the
   previous `move_run`, usually for ANOTHER army — so an army standing on the
   cell that army just entered stops for the tick.  The earlier reading here
   ("the garbage essentially never equals the army's own cell; the port
   zero-initialises it, observationally identical") was wrong: rhost caught it
   (`--trap 466116`, bot-s15-t20 f16450), quirk #229.

The port no longer zero-initialises: the waypoint is explicit state
`g_army_move_wp_col` / `_row` (src/engine_army_move.c), kept across calls and, in
the hybrid host, loaded from / stored back to retail's stack residue.

*Re-checked 2026-11-18 against the IDA export (0x465d28) and #229: the dir == 8 exit makes the stale waypoint observable; the port's zero-init was replaced.*

## 182 — siege-engine construction (`FUN_004a8507`) splits the besieger's head-count as the per-turn build budget; assault launches the tick all ordered engines hit 100%

The strategic turn driver's phase-2 callee `FUN_004a84ba`
(`engine_army_state_scan_siege_built`) scans the 150 map-piece records
(from the cursor up) and, for each **besieging** army (owner set, kind +0x08 == 1,
`+0x199` siege link set, men > 0), advances its siege-engine construction one tick via `FUN_004a8507`
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

- **The completion signal is an aggregate remaining-work byte** (`+0x19c`, the
  "seasons until the assault" figure the panel shows):
  `ceil(sum_of_unfinished_gaps / head)`.  `FUN_004a8507` returns 1 the tick that
  byte reaches 0 — every ordered engine at 100% — and the scanner then parks its
  persistent cursor (`s_slot_0057cad4`) on that record so the driver can launch
  the assault on it (`FUN_004a8aab`, the assault trigger — sounds + combat
  render; now ported as `rl_4a8aab`, rhost hook in tools/rhost/rh_hooks_battle.c).  A non-zero remaining byte (or any failed gate)
  returns 0 and the cursor steps to the next record.

- **The "pct < 100" gate is read pre-recompute in every pass.**  All three
  passes test the *old* `pct` to decide whether a slot is active; only the third
  pass rewrites it (`engine_math_percent_div`, clamped to 100).  A slot whose
  `pct` was already 100 from a prior turn is skipped entirely — never
  re-evaluated — which is why a fully-built slot stops contributing to the
  even-split denominator.

- **The cursor is resumable, not reset here.**  `FUN_004a84ba` only ever
  *increments* `s_slot_0057cad4`; the reset to 1 lives in the phase-2 entry init
  `FUN_004a82b9` (run by the turn driver `0x49a010` at phase tick
  `DAT_0053f658 == 1`; it also drops siege links whose besieged county no longer
  points back, and is ported — src/rl_turn.c / src/engine_army_state.c).  The
  driver calls the scan when `DAT_0053f658 % 100 == 2`: a hit (return 1) launches
  `FUN_004a8aab(cursor)` and phase 2 continues, so the next scan, 100 ticks
  later, resumes AT that same record (the cursor is not stepped past a hit); a full walk with no hit (return 0) ends
  phase 2 (`engine_turn_advance_phase`).

The remaining-byte ceiling is inlined in retail (`div` + `mod != 0 ? +1`) rather
than routed through the ported `engine_math_div_ceil` (`FUN_00404e03`); the port
mirrors the inline form to stay byte-faithful.

*Re-checked 2026-11-18 against the IDA export (0x4a84ba, 0x4a8507, 0x4a82b9, 0x49a010 phase 2): 0x4a82b9 and 0x4a8aab are ported now; scan cadence is phase tick % 100 == 2 and a no-hit walk ends phase 2.*

## 183 — the mercenary-offer rotation (`FUN_004aca2b`) publishes each offer for exactly one turn; the cursor double-steps on publish (no repeat guarantee)

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
  step+clamp folds it back to 1.  It does not guarantee a different county next
  time: whether the next publish repeats depends on the period +0x07 and the
  county count n (the unclamped `n + 1` state also folds to 1, losing a step).
  Simulating the loop: n = 4 with period 3, or n = 6 with period 5, publishes
  the SAME county every period; n = 5 with period 5 settles on county 5 after
  four publishes.

The countdown decrement is a raw byte `dec` followed by a *signed* `< 1` test,
so a slot already at 0 wraps to 0xff and reads as `-1` → immediately "expired".
The port mirrors the unsigned-decrement / signed-compare pair exactly.  In-use
slots (back-ref `!= 0`) are skipped wholesale and never match as an offer holder
even if their stale `+0x03` happens to line up with a realm.

*Re-checked 2026-11-18 against the IDA export (0x4aca2b): mechanics confirmed; the "consecutive offers never repeat" guarantee does not hold for every period / county count.*

## 184 — alliance reconciliation (`FUN_004a1847`) is order-dependent: a dead or contested partner only severs the bond when it has the lower player index

The phase-7 reconciler (`engine_diplomacy_reconcile_treaties`) rebuilds the
"allied with" matrix (+0x85 + q·0x10, wiped first) from each player's declared
alliance partner (+0x81 — game-state-map.md; the earlier "lord/vassal" reading
was wrong: the bond it writes is mutual).  It snapshots every player's +0x81 into
a local array *before* the pass, then validates players 1..5 in ascending order.
Two consequences fall out of "ascending order + pre-pass snapshot":

- **The "settled" (inactive/severed) flag is only reliable for lower indices.**
  A player's settled flag is set during *its own* iteration (inactive: +0x04 ==
  0; or severed), so when player `i` inspects its partner `p`, `settled[p]`
  reflects reality only if `p < i`.  A bond onto a *dead higher-indexed* partner
  is therefore NOT severed by the dead-check — it falls through to the mutual
  branch and the alliance is re-imposed onto the corpse (both +0x81 written, both
  matrix cells set).  The game avoids visible breakage because the inactive
  record is inert elsewhere, but the asymmetry is real and faithfully reproduced.

- **A one-sided declaration onto a live, *unallied* player imposes the bond.**
  Severance fires only when the partner is settled OR already declared toward a
  *third* player (`snap[p] != 0 && snap[p] != i`).  If the target simply has no
  partner (`snap[p] == 0`), the else-branch pins `target.+0x81 = i` and sets the
  matrix both ways — a mutual alliance.  Because the partner array is a pre-pass
  snapshot (only the severed branch updates it), the freshly-imposed
  `target.+0x81` is not seen when the target's own (later) iteration runs (it
  still reads the snapshot 0), so the imposed bond is not re-processed and
  survives the pass.

The port mirrors the snapshot + ascending-order exactly (the matrix wipe, the
settled/partner local arrays, the lower-index-only reliability); retail-layout
`rl_4a1847` (src/rl_turn.c), rhost hook in tools/rhost/rh_hooks_rl_turn.c.  Same
family as the other "scalar snapshot vs live store" ordering quirks.

*Re-checked 2026-11-18 against the IDA export (0x4a1847): mechanics confirmed; +0x81 is the alliance partner and the bond is mutual, not lord/vassal.*

## 185 — raider target assignment (`FUN_004ac5ba`) recomputes the destination from the SHARED realm cursor, not the stack's stored target — so a kept target can point at the wrong capital

The phase-5 raider planner `FUN_004ac499` walks the rebel bands (map pieces of
kind +0x08 == 2) and gives each a target county via a single global round-robin
cursor (`DAT_004e59dc`, `g_raider_target_cursor`).  The reassignment block
(entered when the current season `DAT_0057c934` is 2 = Summer *or* the band has
no target yet) steps the cursor, skips the band's own county (+0x10), and writes
the result into the per-record target byte (`+0x151`).  So every Summer all rebel
bands are re-targeted; the port's comment calling this "force-mode 2" misnames the
season global (the code reads `s_slot_0057c934` correctly,
src/engine_army_state.c).

The bug-shaped detail: the *destination* write that follows reads the capital
through the **cursor** (`g_per_army_768[cursor*0x300 + 0x6c/0x6d]`), not through
the per-record target byte.  These agree on the turn a target is freshly
assigned (cursor == target), but a band that re-enters with a target already set
(outside Summer) **skips the reassignment** yet still recomputes its destination from
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

*Re-checked 2026-11-18 against the IDA export (0x4ac5ba, 0x4ac499): the re-target gate is season == 2 (Summer), not a planner force-mode; the pieces are kind-2 rebels.*

## 186 — phase-3 supply-cart planner (`FUN_00429418`) floods conservatively first, then permissively only on a degenerate route

The phase-3 turn-driver callee (`engine_army_state_plan_capital_marches`) routes
every **supply cart** — map piece of kind +0x08 == 4 (game-state-map.md; not an
"idle state-4 stack") — toward the capital (+0x6c/+0x6d) of its destination
county (+0x167), writing the goal into +0x14/+0x15 and +0x16/+0x17, +0x151, and
clearing +0x150 / +0x14e.  It floods the path-field cost map with
`blocked_mode = 1` (every cell cost > 1 clamped to 100 — heavily penalised, still
passable), traces to the capital, and commits the route (+0x14c = 1) only when
the trace succeeds in **under 150 waypoints** (`DAT_0053f070 < 0x96`).  150 is the
path-field's hard trace cap (`engine_path_field` writes at most 0x96 waypoints),
so a path of 150 is the engine's "had to give up" sentinel, not a real route.  On
that sentinel it re-floods with `blocked_mode = 0` (the cells' real costs) and
traces once more, committing whatever that yields.  A first trace that fails
outright (returns 0) gets no retry — the clamp never blocks a cell, so mode 0
could not reach it either.

Net effect: carts prefer routes that avoid expensive terrain, and fall back to
the true-cost route only when the clamped route cannot be traced within the
waypoint budget.  The waypoint-count gate reads slot 0's count
(`DAT_0053f070[0]`, the value later copied into the record's +0x1c waypoint
counter) — the same slot the two floods/traces use.  The phase-6 merchant planner
(`FUN_004280e9`, #187) shares this exact dual-flood tail.

*Re-checked 2026-11-18 against the IDA export (0x429418, 0x46f700): the pieces are kind-4 supply carts; mode 0 uses the real costs (not "free"); no fallback after a failed first trace.*

## 187 — phase-6 merchant planner (`FUN_004280e9`) picks the route row by the merchant's PIECE SLOT (i-1), not by the route index it carries

The phase-6 turn-driver callee (`engine_army_state_plan_settle_marches`) handles
the **merchants** — map pieces of kind +0x08 == 3 (game-state-map.md; not "idle
state-3 stacks").  When a merchant is flagged arrived (+0x150), it scans up to 16
entries of a route row for its next county, starting at its cursor (low byte of
+0x164, wrapping at 16), skipping entries above the county count, then aims at
a feature cell near that county's capital (`FUN_00428007`) and runs the #186
dual-flood route.  The route table is `DAT_00567970`, `buckets[6][16]`: cleared
6×16 by `FUN_004290d0`, filled by `FUN_00429153(county, row)` from the map's
plane-4 bytes while `FUN_00467770` loads the map, digested by `FUN_004291b3`;
the save persists exactly its 96 bytes (entry 138).

Phase 6 indexes it as `buckets[(i-1)*0x10 + cursor]` with **i the piece slot
1..150**.  That works because the merchants are created only by
`FUN_00427ed0` (the port's `place_army_starts`, #174) at new-game step 8, one per
non-empty route row, into the lowest free slots of an empty piece table — so the
merchants hold slots 1..k (k ≤ 6) and `i-1` stays inside rows 0..5.  The quirk is
the mismatch: `FUN_00427ed0` records each merchant's own row in +0x1ff and
breaks at the first empty row but SKIPS a row whose capital has no adjacent
feature, so after a skip the next merchant sits in a lower slot than its row and
phase 6 walks the neighbouring row's route (the +0x1ff it carries is never read
here).

Rows ≥ 6 are not reached by retail in practice.  If they were, they are not
"zero BSS slack": `DAT_005679d0` (a 0/1 flag written by 0x4190db / 0x43ae30 /
0x4aa90a / 0x4aabd8) sits at +0x60 = row 6 offset 0, `DAT_00567d00..0c` at
+0x390.. (row 57) and `DAT_00567d10` (save entry 149) at +0x3a0.  The standalone
port backs `s_slot_00567970` with 150*16 zero bytes (the widened storage of #194),
so it would read 0 there; under rhost the symbol aliases retail's 0x567970
(tools/rhost/rh_hooks_ai.c `A(s_slot_00567970,0x567970)`) and reads the same
bytes retail does.

*Re-checked 2026-11-18 against the IDA export (0x4280e9, 0x427ed0, 0x4290d0, 0x429153, 0x467770, data_xrefs): the pieces are kind-3 merchants placed in slots 1..6 at new game, so i-1 is a merchant→row map, not a slide into BSS; the memory past row 5 holds live globals (0x5679d0, 0x567d00..), not 928 B of zero slack.*

## 188 — phase-1 AI grant (`FUN_0049d638`): bigger empires get the BIGGER cash table but NO resource topups, and one ration ladder is dead code

The per-player AI step 3 (`FUN_0049d638` → `engine_army_ai_set_rations_and_grant`,
strategic FSM `FUN_0049a581` dispatch step 3) does two passes: a per-county
approval→ration-setting (+0xb9) ladder, then a cash/resource grant.  Three
counterintuitive facts:

1. **The grant is keyed by how many counties the AI owns — the cash rises with
   size, the resource topups fall.**  The selector is the player's +0x29, which is
   the owned-county count (recomputed by `FUN_0049d1e0`, bumped on conquest by
   `FUN_004a72fe`, seeded 1 by `FUN_0049caaa`) — not an "AI level".  The cash
   grant (added to +0x118) reads `DAT_004dc230` (max 600) when the AI owns ≤ 2
   counties and `DAT_004dc1e0` (max 1800) when it owns ≥ 3.  The per-county
   resource topups run the opposite way: ≤ 2 counties get the big step set
   (`s_slot_0053f23c`×{20, 5, 40}), 3–4 counties the small set (×{10, 2, 20}), and
   **≥ 5 counties get the cash grant but every per-county step stays 0**.  The
   steps go to population +0x24 (and its +0x30 mirror) when > 20, stock +0x250
   when > 10, provisions +0x224 when > 50.  Net: a large AI empire is funded by
   cash alone, a small one is padded with both cash and raw resources (catch-up).
   Both tables are dword-indexed `base + s_slot_0053f23c*4 + personality*0x10`
   (personality = +0x07, quirk #173); row 0 (personality 0) is all zero.  The
   grant only runs for an active (+0x04), AI (+0x05 == 0) player owning ≥ 1 county.

2. **The ration ladder's selector-0 branch is dead code in retail.**  Pass 1
   picks one of three approval ladders by the int at +4 of the AI's personality
   row (`DAT_004d8a5c[(+0x07)*0x3c − 0x3c]`).  The four shipped personalities have
   selector values {2, 2, 2, 1} — none select 0, so the harshest ladder
   (`<30→0, <50→2, <65→4, <80→10, else→15`) never fires for any retail AI.  The
   port keeps the branch faithfully; only a hypothetical 5th personality (or an
   edited tuning table) would reach it.

3. **Pass 1 runs for player 0 too.**  Called with `player == 0` it sets every
   *independent* county's ration via a distinct fixed 8-step ladder
   (<20→0 … ≥90→12); pass 2 then falls out immediately (strat slot 0 is
   inactive).  So the same function both manages AI economies and reposts
   independent-county rations each turn.

All three tables are baked verbatim from the image (`g_ai_cash_grant` 0xa0 B;
the personality block was already baked as `g_ai_personality`).  The port's
comments (src/engine_army_ai.c, "AI level < 3 / >= 3") carry the old misreading;
the code reads the same +0x29 byte.  rhost hook 0x49d638 in
tools/rhost/rh_hooks_ai.c.

*Re-checked 2026-11-18 against the IDA export (0x49d638, 0x49d1e0, 0x4a72fe, data_xrefs 0x57bf29): the selector +0x29 is the owned-county count, not an AI level; topup gates and table values (0x4dc1e0 / 0x4dc230, selectors {2,2,2,1}) confirmed.*

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
   the counter `+0xe9` reaches 8 (incremented only after the mode's own gate
   passes).  The gate value `+0x60` is the player's **share of the counties in
   percent** (`FUN_00404dc1(+0x29 owned counties, county count)`, written only by
   `FUN_0049d1e0` and `FUN_0049bd99`) — not a "diplomacy-readiness gauge":
   - **phase 0:** once the AI holds ≥ 40 % of the counties (`+0x60 >= 0x28`),
     post code `0xc1` to every other in-game human slot, then flip the phase to 1.
   - **phase 1:** once it holds ≥ 28 % (`+0x60 >= 0x1c`), send code `0xc0` to the
     **last-ranked (weakest)** player `s_slot_00522d90` — provided that player
     is human and is not this AI's alliance partner (`+0x81`) — then flip back
     to 0.
   The proposal variant selector reuses the shared notification rotation:
   `(mood-cycle[+0x159] − 4) + personality[+0x07]·4`, identical to
   `engine_diplomacy.c`'s `diplo_notify`, advancing `+0x159` (wrap 0..3) per
   post (per recipient in phase 0).

Brain step 8 (`FUN_0049f96c`, `engine_army_ai_step_noop`) is an empty function
— a live slot in the dispatch ladder with no shipped body.

*Re-checked 2026-11-18 against the IDA export (0x49a581, 0x4a13a6, 0x49d1e0, data_xrefs 0x57bf60): +0x60 is the county-share percent (thresholds 40 % / 28 %), +0x81 the alliance partner; the +5 = human reading stands.*

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

## 193 — AI brain step 0xa (attack launch) raises its force from its best-scored MUSTER county but marches it at an enemy county near that muster point, throttled by a personality-column cooldown

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
4. **Gate on the muster county.**  The force is raised from the player's muster
   county (`+0xe5`, chosen earlier by `pick_muster_realms` `FUN_004a0aaa`: the max
   of `2·(population/500)`, −1 below approval 40 / +1 above 80, +1 when stock
   > 4× population, −2 when population exceeds stock; ties to the later county,
   a county needs a score ≥ 0),
   but only if that county is populous and content: population `+0x24` ≥ 400
   **and** approval `+0xc` ≥ 25.
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
the county nearest the enemy — it always musters from its best-scored (populous,
well-stocked) county and accepts whatever march distance that implies, which is exactly why
`select_target` scores candidates from the muster realm's capital rather than a
frontier.

(Caveat for future porters: the Ghidra decompile of `pick_muster_realms`
`FUN_004a0aaa` transposes its two score variables — the IDA export agrees with r2: the
population-above-stock bonus (+2) feeds the `+0xe6` score, not `+0xe5`.)

*Re-checked 2026-11-18 against the IDA export (0x4a0015, 0x4a0aaa; cooldown bytes 6/10/5/10 at 0x4d8acc + p·0xf0): the +0x24 gate is population, not provisions; the muster county is a score, not "richest".*

## 194 — tools gotcha: `gen_save_schema.py` is STALE vs the committed schema headers (clobbers entry 138's widened storage and entry 10's renamed buffer)

Running `tools/gen_save_schema.py` regenerates `save_schema_table.h` /
`save_schema_globals.{h,c}` from `docs/findings/save-schema-table.csv` — but the
committed files carry LATER hand edits the generator does not know about:

- entry 138's storage was widened from its saved 96 bytes to
  `s_slot_00567970[150 * 16]` (the save still persists exactly 96 bytes — quirk
  #187); the generator emits `[96]` again, which compiles and silently changes
  what the standalone merchant planner reads past row 5;
- entry 10 (retail 0x0056d8c0, 51200 B) now points at `g_realm_history`; the
  generator still emits `g_castles_shadow`, which no longer links.

Until the generator/CSV is taught both, treat the three schema files as
hand-maintained: do NOT re-run the generator blind; if you do, restore with
`git checkout -- src/save_schema_*.{h,c} src/save_schema_table.h`.

*Re-checked 2026-11-18 by running a copy of the generator against the CSV in a scratch tree: still emits `s_slot_00567970[96]`, and also reverts entry 10 to g_castles_shadow.*

## 195 — the sub-step duty-cycle LUT (`DAT_004e3260`) is 15 rows of linear decay; hints up to 140 stay inside it (no OOB read)

The rate controller `FUN_004bb978` gates each sim sub-step on
`DAT_004e3260[phase + (hint/10)*0xa0]` (phase `DAT_004d2164` cycles 0..159) after
clamping the scenario hint to 0x8c (140), so the row index reaches 14.  The table
is **15 rows** (0x960 B, 0x4e3260..0x4e3bc0; `k_sim_duty_lut` in
docs/data/catalog.tsv), a clean LINEAR-DECAY duty mask: **row 0 = all 160 ones**
(full duty — step every iteration), row 1 = 144, row 2 = 128, … row 9 = 16, then
8 / 4 / 2 / 1 / 0 for rows 10..14.  The "ERR:…reloading data" text starts at row
15, which no hint reaches — the earlier "11 rows, MP hints ≥ 110 read rows 11..14
out of bounds" reading was wrong.  A high hint simply degrades toward "skip almost
every iteration" (row 14 never steps).  A negative hint is treated as 0 (row 0)
and allows 2 sub-steps when `DAT_0057c998 >= 100`.  SP pins the hint at 1 →
hint/10 == 0 → **row 0, full duty**: SP steps every iteration that clears the
slider gate (rate is gate/freeze-limited, NOT LUT-limited).

**Port-bug correction (2026-06-10 session 5, commit e179d8a):** the prior
`g_engine_rate_lut` image had a BOGUS row 0 (`1,1,0,…` ~2/3 duty, wrong file
offset) that capped the port at ~0.67 sub-steps/tick vs retail's ~0.95 — the
entire end-turn rate offset.  Re-imaged verbatim from VA 0x4e3260; live retail
`--watch` (hint=1, phase +1/frame, sub-step 0.95/frame) confirms the all-ones
row 0.

Related find, same chase (the grid_load_probe load-flip bisect): retail's
game-speed slider default is 90 (`FUN_004ae310` seeds `DAT_0053f230 = 90`), and
the slider is NOT a save-schema slot — a boot→load session plays at 90 regardless
of the loaded save.  The gate is `v = (100 − slider)/10`; `v >= 10` never steps,
and otherwise a sub-step needs `10·v + 2` ms since the last one — 12 ms at 90 —
except `v == 0` in MP, which steps unconditionally.  That is why the turbo'd
retail seat runs exactly ONE sub-step at the load flip (elapsed-since-boot huge)
and zero during idle (sub-ms frames).  A port default of 100 (2 ms gate)
sub-stepped nearly every frame and would have drifted the LFSR arbitrarily far
from retail.

*Re-checked 2026-11-18 against the IDA export (0x4bb978, 0x4ae310) and the image bytes at 0x4e3260 (rows 0..14 = 160,144,…,16,8,4,2,1,0 ones; text at row 15): the table is 15 rows and the index never leaves it; title and body rewritten around the 2026-11-07 catalog correction.*

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

## 199 — `DAT_005aeb68` is BOTH the render error latch and the main-loop run-state: code 1 quits, code 4 (sprite error) triggers a sheet reload

`FUN_00408fb0` (the 27-byte latch setter, `byte_5AEB68 = code`) is called by the
sprite / frame drawers (0x40946d, 0x4097c5, 0x409934, 0x409c93, 0x409e09,
0x40a127, 0x40a21a, 0x40a3d0, 0x40a567, 0x40a682, 0x40a80e, 0x40a9b0) with two
codes, and the byte is the same global the main loop polls.  Run-state value map
(IDA export):

- **1 = quit.**  WinMain `0x40e9ab` (and the message pump `0x40ec2c`) post
  `WM_CLOSE` when it reads 1 and clear it.  Writers: the menu Exit / Escape paths
  and file-IO failures, and `FUN_00408fb0(1)` from the generic-frame drawers on a
  bad frame offset ("ERR: gen frame bad", offset > 310000 or ≤ 0) — so a corrupt
  generic frame DOES request application shutdown.
- **3 = game-end handoff.**  The scroll dismiss `0x476768` writes 3 on events
  0xef/0xee/0x9f/0x9e; the tick guard `0x40e8bb` turns 3 into 1 (→ quit).
- **4 = sprite error → reload, not shutdown.**  `FUN_00408fb0(4)` comes from the
  sprite writers on a bad descriptor ("ERR: write sprite…").  `engine_tick`
  (`0x4b99c0`, tail) consumes it: `== 4` → clear to 0, log "ERR: sprite error"
  and, unless screen `DAT_004eac50 == 34`, reload the sheet set (in game:
  `FUN_004987b7` + `FUN_00480f8b` in tactical mode, else `FUN_004984dc`;
  out of game: `gfx_load_misc_sheet`).  The earlier "latch 4 == deferred shutdown"
  reading was wrong.

Port: the latch is `g_gfx_last_error` (src/gfx_globals.c; set by
src/gfx_sprite_render.c, src/engine_overlay_render.c), aliased to retail
0x5aeb68 under rhost (tools/rhost/rh_hooks_gfx.c); the standalone build still
keeps a separate run-state (`g_shutdown_request`, src/engine.c).  Unifying them
(one byte, all three meanings) is required before the standalone game-end and
sprite-error recovery paths can match retail.

*Re-checked 2026-11-18 against the IDA export (0x408fb0 callers, 0x40e9ab, 0x40ec2c, 0x40e8bb, 0x4b99c0, 0x476768): code 4 is consumed by engine_tick as a sheet reload; 1 is the shutdown value (also written for a bad generic frame).*

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
differ from the ones the .sav stored.  The legacy port
(`main.c` `engine_map_post_load`) re-ran the same classify but reseeded a
LOCAL single-RNG to the fixed 0x34518632 — reproducing the .sav's ORIGINAL
new-game variants byte-exact (which is why block 0 == .sav), but not
retail's reload re-roll.

Consequence: this byte band can NEVER round-trip even in retail —
load→save→load→save re-rolls the variants every time, because the RNG
that drives them isn't persisted.  The legacy port's fixed-seed
choice was arguably MORE faithful to the saved file than retail's
re-roll, and it cost 273 px (0.089 %) on that seat.  The retail-layout
port no longer diverges: `rl_4976a1` (src/rl_turn.c, the load tail) calls
`rl_46dbf5` / `rl_46dd34` (src/rl_terr.c), which step the shared LFSR
`rl_404b2c` on retail's `DAT_00591564` (`g_bf_591564`, seeded 0x34518632
by `rl_4b2258`, src/rl_main.c) and write the feature / variant registers
`DAT_0057c8a4` / `DAT_00568200` — the same re-roll retail does, from the
same state.  Only the legacy `main.c` load path still uses the local
fixed seed.

*Re-checked 2026-11-18 against the IDA export: classify 0x46dbf5 (+3 = DAT_0057c8a4 + DAT_00568200, LFSR 0x404b2c only when DAT_0057c8a4 == 22), seed 0x34518632 at 0x4b2258 confirmed; the port paragraph now describes rl_4976a1 → rl_46dbf5 on retail's LFSR (the fixed-seed divergence is legacy main.c only).*

## 202 — the AI pathfinder's move-cost grid base (0x4f3ffe) is the cost cells (0x4f4080) MINUS 0x41 int16; it must be bound, or the first AI army to move NULL-derefs

`engine_path_field` (the cost-field flood/trace, retail FUN_0046f700/4701ac) reads
its move-cost input at `g_terrain[cell + dir]` for the nine direction offsets
`{0,1,2,0x40,0x41,0x42,0x80,0x81,0x82}` — a 3×3 window with row-stride 0x40, where
**`COST_DIAG == +0x41` is the cell ITSELF**.  So the grid's NW-sub-table base
(retail 0x4f3ffe) is the per-cell cost grid (retail `DAT_004f4080` =
`g_strat_move_cost`) **minus 0x41 int16**, and the grid needs a **0x41-entry skirt
on each side** (fill 0x46f700 reads `word_4F3FFE` / `4F4000` / `4F4002` /
`4F407E` / `4F4080` / `4F4082` / `4F40FE` / `4F4100` / `4F4102` [cell]: the cost
of the neighbour it is about to enqueue).  In retail that skirt is NOT dedicated
zero storage: 0x4f3ffe..0x4f407f is ordinary .data and holds `DAT_004f4070` — the
fill's own source-cell index (`a2 + (a3 << 6)`), so row-0 cells at columns 55..58
would read that index as a north / diagonal neighbour's cost.  A cell is enqueued only when its
own cost is non-zero, so this matters only on a map whose row-0 cells there are
passable (edge cells are normally sea, cost 0 — not checked on every map); the
legacy padded zero skirt (`src/engine_strat_cost.c`) differs only then, and under
rhost / the data image the pathfinder runs on retail's own bytes.  The
port had `g_strat_move_cost` as a bare `[4096]` array and never bound it — binding
the pathfinder to it directly is doubly wrong (off by 0x41 AND runs off the
unpadded end).  Fix: `g_strat_move_cost` is now a `+0x41` view into a padded
backing; `engine_strat_cost_pathfield_base()` returns the 0x4f3ffe base, bound in
`engine_game_init_bind_runtime_tables`.

The gap stayed latent until the brain dispatcher was completed: at the time the
port's ONLY on-engine caller of the pathfinder was the AI brain (steps 6/9/0xb
deployment; retail has eight callers of 0x46f700 — 0x4280e9, 0x429418, 0x43723a,
0x437fd7, 0x467196, 0x4a5667, 0x4a98fe, 0x4ac499), and in a turn-1 headless SP world
steps 9/0xb find no field armies to move (reached-but-input-empty, like step 1).
**Brain step 7's keep-garrison muster (commit fd50752) creates the first AI army
that actually moves**, so it was the first code to invoke the pathfinder on-engine —
and it crashed (access violation in `engine_path_field_fill`) until the grid was
bound (commit f6e77d5).  Corollary: that crash is positive proof step 7 is live +
active on the real new-game world.

> 📍 `src/engine_strat_cost.c` (`g_strat_cost_grid`, `engine_strat_cost_pathfield_base`),
> `src/game_init.c` (`engine_path_field_bind_terrain`), `src/engine_path_field.c`.

*Re-checked 2026-11-18 against the IDA export: base 0x4f3ffe = 0x4f4080 − 0x41 int16 confirmed (0x46fe2e `movsx eax, word_4F3FFE[eax*2]`); added that retail's lower skirt holds DAT_004f4070 (not zeros); moved here the "latent until step 7" paragraph that had landed at the end of #203; retail caller list.*

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
overlay city/army pass.  Fix (then): extracted `456fd8`'s branch-B strip table
(stride 0x3a, 28 zero-skip copies, init_off 0 — same shape as 455f04) via
`tools/extract_drawb_stripB.py 456fd8` → `src/gen/drawb_stripb_456fd8.h`, added the
`kind == 2 && phase == 4` dispatch case in `drawer_b`.  (Retail: `drawer_b` 0x406673
param `a1` is the phase, `byte_591568` the kind; phase 4 calls `sub_456FD8` for kind 2,
then re-tests the kind global for kind 3 → 0x4580a9.)  Verified: grid_load_dense
blacksmith box 157→17 px, whole-frame mean parity 0.116%→0.058% (the same right-edge
overhang recurs on cells 3640/7280 across all frames).  Residual 17 px = the
forge-fire anim phase (8 px, port renders anim frame 0) + a pre-existing 9-px
left-edge terrain clip diff (unrelated, `oldport == port`).  Still skipped then: kind-3/4
**edge** phases (param_5=2) — one off-blacksmith cell (4160) in this frame.

Now: every overhang strip is ported — `src/engine_map_terrain.c` `drawer_b` dispatches
all kinds × phases × the three views to the instruction-for-instruction blitters
`src/gen/mapblit_<va>.h` (`tools/gen_mapblit.py`; 456fd8 → `engine_map_terrain_strip_456fd8`,
wrapped as `rl_456fd8` in `src/rl_link_mapview.c`); `g_n_drawb_skipped` is gone
(`engine_map_terrain_drawb_skipped()` returns 0) and `drawb_stripb_*.h` is no longer
included.

*Re-checked 2026-11-18 against the IDA export: drawer_b 0x406673 → sub_456FD8 on phase 4 / kind 2 confirmed; pointers moved to the generated mapblit headers; the trailing "latent until step 7" paragraph belonged to #202 and moved there.*

## 204 — the multi-line event scroll (types 5–9) lays its objectives out through a SHARED y-accumulator that the wrap renderer advances per WRAPPED line — not a fixed per-block stride

`engine_popup_scroll_render`'s types-5..9 arm (retail `FUN_0047309e` final else, the
in-game welcome/objectives + combat hints 0xc8..0xda) stacks N word-wrapped body
sub-strings.  Each block's height is DATA-dependent — a long objective wraps to >1
line — so the panel height and the next block's Y can't be pre-computed.  Retail
threads this through a **shared module global `DAT_005cd4f8`** (= the gfx_text
`line_y_acc`, `g_gfx_line_y_acc` in the port): the wrap renderer `FUN_0040328e`
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
y-bump.  Fix: drive the arm off `gfx_text_{,set_}line_y_acc`
exactly like retail (now also `rl_47309e`, src/rl_turn.c, on retail's own
`DAT_005cd4f8`).  Verified on `endturn_seal` t030 (welcome objectives 0xc8,
type 7): objectives-frame diff 43%→14.8%, and the objectives panel proper
(x[16,464) y[64,232)) is now **0-px byte-exact** vs retail.  The 14.8% residual is
the SEPARATE stale-256-warning block (y[272,383], below the panel) — the dismiss
doesn't repaint the map there (the "256-warning dismiss-tick" follow-up).

*Re-checked 2026-11-18 against the IDA export: 0x40328e bumps DAT_005cd4f8 by 24 / 16 per wrapped line; 0x47309e's final arm seeds 32 (measure, +16 a block, `<= 96` bump) then 64 (render) — confirmed; port names updated (g_gfx_line_y_acc, rl_47309e).*

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

## 208 — FUN_00426216 inside the terrain-info render is a DirectSound SFX cue (restart effect buffer k), not a render mutation (a no-op with sound off)

`FUN_0041c208`'s army-stats arm (`flags&0x80 && terrain_type<0xd`) calls
`FUN_00426216(10/8/9)`.  It is easy to read as part of the stat layout, but it
indexes the effect-buffer table `DAT_00522afc[arg]` (IDirectSoundBuffer pointers)
and calls two vtable slots: +0x48 = `Stop`, then +0x30 = `Play(0, 0, 0)` — it
restarts effect `arg` from the top (the army-info UI sound cue), storing each
HRESULT in `DAT_00522b58` and returning `Play == DS_OK`.  It returns 0 with no call
when DirectSound is absent (`ppDS == 0`), the buffer slot is empty, or the effects
switch `DAT_0053f214` is off — so with sound off it touches nothing; with sound on
its only state write is `DAT_00522b58`.  Not load-bearing for the panel pixels.
The same function is retail's `army_move_alert` (callers 0x40d6ad / 0x40d7b8 /
0x40da1e / 0x412b33 / 0x41c208 / 0x438b02 / 0x43a997 / 0x46673c / 0x46873f /
0x468ae2: button presses, blocked army steps, plunder).

Port: `rl_426216` (src/rl_io.c) is the port; the terrain-info classifier still
records the cue (`alert_arg`, src/engine_terrain_info.c) and its env seam
`seam_ti_alert` (src/rl_link_mapview.c) calls `rl_426216`, so the cue is no longer
elided; the legacy `engine_army_move.c` `g_alert_fn` seam is bound the same way
(`seam_alert`, src/rl_link_logic.c).

*Re-checked 2026-11-18 against the IDA export: 0x426216's vtable slots are Stop (+0x48) and Play (+0x30), not a status query / restore; gates ppDS / buffer / DAT_0053f214, HRESULT in DAT_00522b58; port now rl_426216, called through the terrain-info seam.*

## 209 — the strategic map needs the EVERY-FRAME composite (FUN_004050c0, tick line 42), not just the demand-redraw render (FUN_0040f1a0, line 36)

Retail's in-game tick (FUN_004b99c0) composites the strategic map in TWO places:
the demand-redraw seam (line 36, `if (DAT_0057d340) FUN_0040f1a0(redraw)` — the
FULL screen incl. HUD, on a redraw request) AND, unconditionally every frame,
line 42:

```
if (DAT_0056d650 == 0 && DAT_0057c8fc == 0 &&
    (DAT_004eac50 == 0 || DAT_004eac50 == 0x10)) FUN_004050c0();
```

FUN_004050c0 (`engine_map_scene_tick`, 493 B in IDA) bumps the 0..3 redraw phase
`DAT_0057d380`, advances the anim counters `DAT_0057d378` (mod 128) / `DAT_0057d388`
(mod 96) — gated by the GOG 2.0.0.3 patch's jmp-detour 0x4050f0 → stub
0x4cfb08..0x4cfb48: advance only when ≥ 0x10 ms (`GetTickCount`) passed since the
PREVIOUS call (`DAT_005ceffc` is re-stamped every call, `DAT_005ceff8` keeps the
prior stamp) — then renders the 3-cluster map scene (0x40526e terrain, 0x405602
overlay, 0x405487 map) and re-picks the dominant county `DAT_0057c93c`.  (Ghidra
showed the 53-byte head and the stub as separate functions.)  The port had ONLY
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

Drop-in deviation (legacy own-layout tick, engine.c): the port composed the FULL strat frame each in-game frame
(engine_screen_strat_render) rather than retail's incremental map-scene-only
(FUN_004050c0 leaves the HUD untouched).  The port already memset-clears +
fully redraws each strategic frame, so a full repaint is simpler, has no
map/HUD-overlap risk, and is equally correct; cost is a per-frame HUD redraw
(trivial).  A LIVE/PINNED anim-mode flag (engine_screen_strat_set_anim_live)
keeps the single-shot seats (--strat-map/--strat-load/--ti-cell) + the
capture-parity harness at anim frame 0 for byte-exactness; only the in-game
tick (g_engine_state==3) sets LIVE.

The retail-layout frame driver `rl_4b99c0` (src/rl_host.c) has no such deviation:
it calls `rl_40f1a0` (demand redraw) and then `rl_4050c0` under retail's exact
condition, map-scene only, in retail's order.

Turbo-capture caveat (legacy Frida captures): the 16 ms anim gate (stub 0x4cfb08, delta since the previous call) barely
advances under --turbo on BOTH engines, so cattle/castle stay near frame 0 on
both sides — which is exactly why grid_load_dense parity stays clean (0.055%
mean).  The animation is a real-time-gameplay effect; a season change (a STATE
change, not anim-gated) shows immediately on the next frame regardless.

*Re-checked 2026-11-18 against the IDA export: 0x4b99c0's `56D650 == 0 && 57C8FC == 0 && (4EAC50 == 0 || 4EAC50 == 0x10)` → 0x4050c0 confirmed; 0x4050c0's size / patch-stub gate corrected; the full-frame deviation is the legacy engine.c tick, rl_4b99c0 follows retail.*

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

Legacy port compensation (engine.c's own-layout tick; kept local, same pattern as the widget-repaint
note): `g_canvas_repainted_this_tick` — raised by every full-frame
paint arm (screen-0 / screen-4 / county-panel demand arms + the
every-frame composite), reset at the tick top.  The tooltip treats a
raised flag as "backdrop invalid": the clear SKIPS the restore (the
repaint already erased the box) and a tip shown through a repaint
RE-CAPTURES before re-rendering.  Visible pixels match retail.  If the
port's input dispatch is ever moved to retail's tick position, this
compensation becomes inert (the flag is never set when the clear runs)
and can be removed.

The retail-layout frame driver `rl_4b99c0` (src/rl_host.c) calls `rl_40f1a0` →
`rl_4050c0` → `rl_4ba26e` → `rl_476e95` → `rl_42ff10` in retail's order, so on that
path the compensation is moot; `g_canvas_repainted_this_tick` lives only in the
legacy engine.c tick.  (The "line N" numbers above are Ghidra's 4b99c0.c lines; in
the IDA export the same calls are `engine_screen_dispatch`, `engine_map_scene_tick`,
`engine_diplomacy_action_refresh` (= 0x4ba26e, misnamed there), `engine_tooltip_tick`,
`engine_ingame_input_dispatch`.)

*Re-checked 2026-11-18 against the IDA export: 0x4b99c0 order 0x40f1a0 → 0x4050c0 → 0x4ba26e → 0x476e95 → 0x42ff10 confirmed; port note updated (rl_4b99c0 dispatches input at retail's position).*

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

## 217 — the trade grid (0x0c) computes its figures only in its render 0x416308 (open + demand redraw); the per-frame refresh redraws content+arrows with NO compute, so a purchase in progress survives the redraws its own arrows drive

`FUN_00428daf` (the trade-figure compute) unconditionally zeroes the running
buy/sell net (`DAT_00554170`), total (`DAT_00553f64`), and ale delta
(`DAT_0053e8c0`) at its top.  Its only caller is the grid render
`FUN_00416308`, which runs on the open (`FUN_0043530e` → `FUN_00416308`) AND on
every demand redraw while the grid is up (`FUN_0040f1a0` case 0x0c → `FUN_00416308`,
whenever something sets `DAT_0057d340`) — a demand redraw mid-trade DOES zero the
net.  The arrows never request one (no `DAT_0057d340` write in `FUN_00435339` etc.),
and the per-tick refresh is `FUN_004ba26e`'s 0x0c arm, which draws
`FUN_0041635f` (content) + `FUN_0040cfd2(0x30,0x50,…)` (the arrow strip) but
does NOT call the compute.  So the arrows (`FUN_00435339` etc.) mutate the net,
and every subsequent frame repaints the new figure without wiping it.

The trap for a drop-in that folds the render into a single demand-redraw arm:
if that arm calls the compute, then clicking an arrow (which requests a
redraw) recomputes → net snaps back to 0 → the panel is frozen at "Buying 0 /
Total 0" no matter how many times you click.  The legacy port split it as
`engine_trade_render` (open, computes once behind a one-shot guard) +
`engine_trade_refresh` (the per-frame content+arrows, no compute), the latter
run from the UNCONDITIONAL per-frame render seam (same tier as the county
0x15/0x19 arrow refresh), and the arrow actions request no redraw at all.  The one-shot guard was
narrower than retail (a demand redraw on 0x0c did not recompute); the
retail-layout entry `rl_416308` (src/rl_link_screens.c) sets
`g_trade_needs_compute = 1` before every render, so it recomputes on the open and
on every demand redraw, as retail does.

Corollary — the merchant zone hit-test `FUN_004357a6` (screen 8) also runs
every frame in the router and sets the hovered good (`DAT_0056d8ac =
mercgrid[cursor]`) UNCONDITIONALLY, opening the grid only on an LMB press over
a rug good; porting it as click-only leaves the good stale after a trade.

> 📍 `src/engine_trade.c` (`engine_trade_render` / `engine_trade_refresh` /
> the arrow arms), `src/engine.c` (`ti_ui_router` trade arm, the per-frame
> `engine_tick_trade_render(0)` seam, `ti_merchant_zone` hover), Ghidra
> `FUN_00416308` / `FUN_00428daf` / `FUN_004ba26e` (0x0c) / `FUN_004357a6`.

*Re-checked 2026-11-18 against the IDA export: 0x428daf zeroes DAT_00554170 / DAT_00553f64 / DAT_0053e8c0 and has the one caller 0x416308, whose callers are 0x43530e AND 0x40f1a0 case 12 — "only once per open" corrected; 0x4ba26e case 12 = 0x41635f + 0x40cfd2(0x30, 0x50, 0x4dd838, …) with no compute, and 0x4357a6's unconditional DAT_0056d8ac write, confirmed.*

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
`0x20` (`FUN_004351c4`), not the map — deferred then until 0x20 rendered; RMB
closes to the map (retail's own close arm, `42ff10` @ `0x430757`).  Now ported on
retail's layout: `rl_4351c4` (src/rl_ctl.c), the screen-9 per-frame strip
(`FUN_004ba26e` case 9 = `FUN_0040cfd2` over `DAT_004dd928`) in `rl_4ba26e`
(src/rl_host.c).

> 📍 `src/engine_treasury.c` (`FUN_00416925`), `src/engine.c`
> (`engine_tick_treasury_render`, the screen-9 demand + per-frame seams,
> `k_treasury_tab` in `engine_endturn_hit_test`, `engine_dev_force_treasury_render`),
> `src/engine_ingame_input.c` (screen-9 close), Ghidra `FUN_00416925` /
> `FUN_0040f1a0` case 9 / `FUN_0043ae30` / `FUN_00432967` / table `0x4dc680`.

*Re-checked 2026-11-18 against the IDA export: 0x432967 = 0x40e3ee(0x1de, 0x1ae, 0x4dc680, 6); 0x43ae30 case 2 → screen 9; 0x40f1a0 case 9 = bare 0x416925 (no file / palette load) — confirmed; port status updated (Rankings exit ported).*

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
`timeGetTime` sequence the ticks sample — with a fixed virtual-clock step, of the
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
3. **(corrected) The 20 ms base tick is a last=now timer, not an accumulator** —
   `FUN_004bbc80`: `d = timeGetTime() − DAT_004e5b10; if (d >= 0x14 || d < 0)
   { DAT_004e5b10 = now; ++DAT_005aeb2c; DAT_0058fcb0 = 1; }` (0x4bbce5..0x4bbd10).
   The base re-stamps to NOW, so the remainder is dropped: at a 17 ms step it
   fires every other tick (34 ms), not ~6 of 7.  The measured CONSTANT pixel
   offset from a one-tick skew stands; the original "accumulator" explanation
   of it was wrong.

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

Under rhost the question is gone: retail and the port run on the same virtual
clock in one process, so the counters agree without a pin; `--phase-pin` is a legacy
Frida / olv3 capture device.  The retail-layout port of the counter step is
`rl_4bbc80` (src/rl_host.c).

*Re-checked 2026-11-18 against the IDA export: 0x4bbc80's base tick re-stamps DAT_004e5b10 = now (`d >= 0x14 || d < 0`) — trap 3's "accumulator / 6 of 7" claim was wrong and is corrected; counter list (DAT_005aeb2c, DAT_0058feb4/b0/b8, DAT_005aea54 /13, 44 /16, 48 /24, 4c /32) confirmed.*

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
   where the dispatch returns 0).  Send with target==source opens a confirm dialog
   (`FUN_0040e6f2(14, 160, 160, FUN_0043b101)` — dialog 14, not a scroll) whose
   callback `FUN_0043b101` returns to the map on param 1 and stays on 0x18 otherwise;
   in MP (`DAT_00553030`) Send posts message 47 (`FUN_0043eda0(47, 0)`) instead of
   committing locally.  (Deferred in the legacy port; `rl_43b04c` (src/rl_ctl.c) /
   `rl_43b101` (src/rl_ui4.c) now port both.)  The 2
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

*Re-checked 2026-11-18 against the IDA export: 0x43b04c → 0x43b145 → 0x4292af (→ 0x46e1b0) and 0x43b145's provision + 2 × (workforce / grade / plan) tail confirmed; the self-send arm is confirm dialog 14 with callback 0x43b101 (was "a scroll"), plus the MP message-47 arm; port status updated.*

## 224 — Noble Faces (0x0b) is the 5th county-action tab (param 5, NOT owner-gated), draws over the retained map with NO recomposite, and is byte-exact only with BOTH the demand render AND the per-frame button strip

Porting the Noble Faces / diplomacy partner-select screen (0x0b, FUN_00416cf3 +
the per-noble cell FUN_004171ee) — the diplomacy roster reached from the map's
county-action strip.  Five facts pinned it down, all handled in
`engine_noble_faces.c` + `engine.c`:

1. **It's rec4 of the SAME 0x4dc680 strip that opens Court/Armory/Send-Supplies**
   — the strip is 6 records (5 action tabs + End-Turn), each 0x18 B:
   {x0,y0,x1,y1 s16; action fn @0x08; type @0x0f; DAT_0059154c param @0x10}.  The
   tabs dispatch through FUN_0043ae30(DAT_0059154c): param 1 → Armory 0x17, 2 →
   Court 0x09, 3 → Send-Supplies 0x18, 4 → **Castle 0x1b** (`FUN_00436a88`; unported then, now `rl_436a88` in src/rl_ctl.c), 5 →
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
FUN_00436141…FUN_0043631e) are the widget-router's 0x0b arm — DEFERRED at the
time (since ported on retail's layout: `rl_436141` (src/rl_ctl.c) … `rl_43618b` / `rl_43631e` (src/rl_glue.c), the 0x0b
strip in `rl_4ba26e` src/rl_host.c); in that legacy port they
rendered but didn't navigate (a button click at x=0x190 misses the seal test at
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

*Re-checked 2026-11-18 against the IDA export: 0x43ae30 case 4 → 0x436a88, case 5 → 0x43611b (= screen 0x0b, DAT_0057a0f8 = 0, DAT_0057d340 = 2, no owner gate); 0x40f1a0 case 11 = bare 0x416cf3 (not a caller of 0x4050c0); 0x4ba26e case 11 = 0x40cfd2(0, 0, 0x4dd940, DAT_00553ee8) — confirmed; port status updated.*

## 225 — the diplomacy-action GIFT screen (0x1a mode 0) commits by ENQUEUING into the AI's own diplomacy inbox (not an instant effect) + transfers the gold immediately; and its panel text is byte-exact ONLY with the illuminated-drop-cap flag (DAT_0058fe2c) that colours every leading A..Z red

Porting the first diplomacy-action sub-screen — the GIFT panel (screen 0x1a, the
button-0 target of the s34 Noble Faces roster).  0x1a is a MODE dispatcher: the
roster's action buttons (DAT_004dd940 → FUN_00436141…FUN_0043631e) set
DAT_005651cc (0..6) and screen 0x1a, and the render dispatch FUN_0041789b
switches on it — mode 0 = gift (FUN_00417960), 1..4 = message screens
(FUN_00417aef, compliment/insult/…), 5..6 = ask-ally-to-attack (FUN_00417cef).
Only mode 0 is self-contained; the others need the runtime message buffer
(0x53f1e0, all-zero statically) / a target list (0x5530b0) and were DEFERRED in the
legacy port (now `rl_41789b` → `rl_417960` / `rl_417aef` / `rl_417cef`, src/rl_ui4.c /
src/rl_render.c).
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
   original (unclamped) value.  (Precisely: the transfer runs for ANY action with a
   non-zero value, and only once a free inbox slot was found — a partner whose five
   slots are all taken gets neither the message nor the gold.)  Ported as `engine_diplomacy_queue_action`.  (The
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
the portrait partner-select (FUN_004369bd) were DEFERRED then; both are ported now
(`rl_417aef` / `rl_417cef`, src/rl_render.c; `rl_4369bd`, src/rl_glue.c).

> 📍 `src/engine_diplomacy_action.c/.h` (the 0x1a render + gift arrow + confirm),
> `src/engine_diplomacy.c/.h` (`engine_diplomacy_queue_action` = FUN_004a2621),
> `src/engine.c` (0x0b button router + 0x1a router/demand/per-frame arms, ctx
> builder), `src/engine_ingame_input.c` (0x1a close case).  Retail FUN_0041789b /
> FUN_00417960 / FUN_00436372 / FUN_00436408 / FUN_004a2621 / FUN_0040cfd2 over
> DAT_004dd9d0.

*Re-checked 2026-11-18 against the IDA export: 0x41789b mode switch (0 → 0x417960, 1..4 → 0x417aef(0..3), 5..6 → 0x417cef(0..1)), 0x417960's DAT_0058fe2c = 1 / 0 bracket and 0x4a2621's inbox append (+0x89 bump for type 1, +0x90 = 1) confirmed; added that the gold moves only after a free slot is found (any action with a value); port status updated.*

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
* **DAT_00569570 / DAT_00569578** is ONE cursor pair shared by 78 retail functions (45 of them write it):
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
the port.  The other retail writers of the cursor pair (territory / render loops) counted
in locals at the time; each needed the same treatment when shadow-verified (done
then: the box scans, FUN_0046d2a3, FUN_0046e067 `mark_draw_box`, the AI and
deploy-cell finders; later #235 and the battlefield ports in `src/engine_bf_*.c`).
The retail-layout `rl_*` ports run on retail's data image, so they write
0x569570 / 0x569578 (and 0x591560 / 0x58fea0 through `rl_404f4c` / `rl_404ead`)
by construction (e.g. `rl_46902a`, src/rl_terr.c).

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

*Re-checked 2026-11-18 against the IDA export: 0x404f4c / 0x404ead store |dx| / |dy| in DAT_00591560 / DAT_0058fea0 on every call — confirmed; the cursor pair's reach corrected from "~55 functions" to 78 referencing / 45 writing (data_xrefs.tsv); port progress note updated.*

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
divergences, hybrid PASS.  Still inlined / unhooked then: the battlefield's distance callers (FUN_0047a814, FUN_00489a62,
FUN_0048b5fc … FUN_00497437, FUN_004843bc, FUN_00484bf9, FUN_0048ef45 …) and FUN_0046902a — which is the capital's
PORT (harbour) placer `engine_territory_place_capital_port` (the water-adjacent plain cell nearest the capital, by
chebyshev from the cell cursor), not a castle-border placer.  Both are done since: the battlefield ports
(`src/engine_bf_*.c`) use `g_path_dist_dx / _dy`, and `rl_46902a` (src/rl_terr.c) calls `rl_404f4c` with the cursor
pair.

> 📍 `src/engine_combat.c` (engine_combat_find_capital_defender), `src/engine_army_ai.c`
> (nearest_enemy_army / nearest_engageable_army); evidence: rows combat_capital_defender, ai_run_movement.

*Re-checked 2026-11-18 against the IDA export: 0x46d42c's 4×4 box (cap−2 .. cap+1) leaves the cursor at (cap_col+2, cap_row+2); 0x4673b8 / 0x467532 call 0x404f4c — confirmed; 0x46902a's role corrected (capital port placer) and the "still inlined" list marked done.*

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

Not yet written then: the feature picker's scratch DAT_0057c8a4 and the variant register DAT_00568200 (FUN_0046c2de /
FUN_0046dbf5 / FUN_0046dd34).  These run not only in new-game map setup but also on every load (the load tail
FUN_004976a1 calls 0x46dbf5 / 0x46dd34, #201).  Written now: `rl_46c2de` / `rl_46dbf5` / `rl_46dd34` (src/rl_terr.c)
store `g_bf_57c8a4` / `g_bf_568200` and count in `g_cell_sweep_counter`.

> 📍 `src/engine_territory_globals.c` (`g_cell_sweep_counter`, aliased at 0x53e974 by `tools/rhost/rh_hooks_ai.c`),
> `src/engine_territory.c`, `src/engine_map_init.c`.

*Re-checked 2026-11-18 against the IDA export: DAT_0053e974's six referencing functions (0x467770, 0x46797d, 0x46dbf5, 0x46dd34, 0x46df51, 0x46dfd5) confirmed; "new-game only / not yet written" corrected (load tail 0x4976a1; rl_terr.c writes both registers).*

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
the liege dismiss never fires.  After the read 0x475e07 stores its kind there (0x475e97, `DAT_0053e910 − 0xf5`), then 0x47309e's common tail
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

*Re-checked 2026-11-18 against the IDA export: 0x475e10 `cmp [ebp-8], 3` → liege test (player +0x81) → dismiss before any store, 0x474c5f / 0x4b9ef4 / 0x405ac0 / 0x475b23 confirmed; the kind store is at 0x475e97 (0x475e9a is the next instruction, `DAT_00553060 = 0x10`).*

## 241 — the delayed-action handlers share one frame slot: 0x4479e9's uninitialized local is the pass's carry

The dispatcher 0x4472ce (engine_tick 0x4b9bb5) calls every due handler through `call DAT_004d5c98[type]` at the same
depth, so the dword at the handler's e0 − 8 ([ebp-4], rhost 0x58fffd88) carries from one handler to the next within a
pass.  Type 1 (0x4479e9, message 30) compares it with the local player BEFORE storing its realm (+4) there: equal →
DAT_0055403c = 2.  What each handler leaves (prologues of all 50, `all_ida.asm`): types 1 / 43 their local = the
record's realm +4 (43: e0 − 8 / 0xc / 0x10 = to / amount / from), 33 / 34 their local = the player +4 (written first),
23 its loop counter (option 2 only: n + 1 over DAT_0056d5dc; other options leave it), the 45 others `push ebx` =
0x4472ce's EBX (a register value of the host's: 0x14a000 / 1 under rhost).  At a pass's start the slot holds the tick
head's leftover: 8 (0x43ef5c, inside the every-10th-tick message 10 send 0x43eda0 at 0x4b9b77) or a stack address
(0x4bb8c1 / 0x43de71, the sim sub-steps) — never a realm index.  Measured (wine, the 39 MP sides, `--trap 4479e9`,
`--watch-write 58fffd88@10248` on mp-trio-p1): 495 type-1 calls; at entry 238 × 8, 9 × 0x58fffdb4, 1 × 6, 247 × the
previous handler's realm (message 30 files a realm's pair: the second of the local realm's pair sets DAT_0055403c).

Port: the dispatcher's model `s_da_res` (rl_ctl.c `da_run`): 8 at the start of a pass, then per handler as above
(EBX → 0xffffffff, no realm).  rl_4472ce calls rl_4479e9 / rl_448339 directly with it (were rl_va_call misses → the
entry thunk → the row's rh_res_get).  Not modelled: an EBX that equals the local player (never seen) and a pass start
whose tick head left a realm index (never seen).  Evidence (wine): verify `--only '^mp-'` on rl_4472ce + the five slot
handlers: 6/6 reached, 0 divergences (4472ce 647,437 calls shadowed at depth 1 = the model against retail's real slot),
hybrid PASS; standalone `--only '^mp-'` 39/39 = pure retail with 0 rl_va misses (was 29 sides via the thunk).

> 📍 `src/rl_ctl.c` (da_run, s_da_res), `src/rl_mpmsg2.c` (rl_4479e9_res, rl_448339_res), `src/rl_mpmsg3.c` (rl_447e0b_res).

## 242 — retail faults at its first paint unless its window starts in the foreground

0x4b2258 creates the main window visible (`CreateWindowExA` style 0x90080000, no ShowWindow) and calls `UpdateWindow`
before any DirectDraw object exists (0x42e5c0 comes later; 0x4b2258 itself zeroes the primary / back pointers
`DAT_004eabd0` / `DAT_004eafac` afterwards). The WndProc 0x4b29be:
- `WM_ACTIVATEAPP` sets the active flag `DAT_004e65a8 = wParam && GetForegroundWindow() == hWnd && !IsIconic(hWnd)`;
- `WM_PAINT` (no movie open) does BeginPaint / EndPaint and, **if `DAT_004e65a8 == 0`**, 0x451f70: mark the whole
  screen dirty (0x45240a) and 0x452160(1) — which Blts through `DAT_004eabd0`'s vtable (`BltFast` +0x1c in full-screen).

So when the new window is not the foreground window — the process was started by one that may not take the foreground
(a background launcher, a scheduled task, another app holding the focus lock) — WM_ACTIVATEAPP leaves the flag 0 (or is
never sent), the first WM_PAINT calls 0x452160 with `DAT_004eabd0 == 0`, and the game dies on a NULL vtable read before
its first frame. Launched normally (Explorer / GOG Galaxy: the foreground process starts it) the window is activated
inside CreateWindowExA and the flag is 1 by the paint.

Found 2026-11-18 by the product build (`openlords2.exe`, the port on real DirectDraw, started from rhost's job runner on
Windows 11): AV in `rl_452160` ← `rl_451f70` ← `rl_4b29be` at frame 0. The port keeps retail's behaviour; the product's
host layer (`tools/rhost/rh_real.c`, `real_CreateWindowExA`) makes the window foreground before CreateWindowExA returns
(input attached to the foreground thread, `SetForegroundWindow`), and its compat-layer parent grants the child
`AllowSetForegroundWindow`.

> 📍 IDA `0x4b2258` (window bring-up), `0x4b29be` (WndProc: WM_ACTIVATEAPP / WM_PAINT arms), `0x451f70`, `0x452160`;
> `src/rl_ctl.c` (`rl_452160`), `tools/rhost/rh_real.c`.

