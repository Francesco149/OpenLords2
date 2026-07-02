# Labor-allocation screen 0x0f — full port analysis (session 24)

> Complete decompile analysis + port plan for the labor-allocation detail
> screen (`g_sim_current_screen` = 0x0f, renderer `FUN_00412b33`).  All
> render helpers are ALREADY ported; every `.rdata` table is extracted
> below; every county-record field offset is computed.  The port is a
> mechanical 1:1 translation — the only remaining risk is pixel
> transcription, which the olv3 index-diff loop + a human scrub resolve.
> This doc exists so the next session is pure execution + verification
> (the analysis is the expensive part, captured while the decompile
> context was loaded — feedback_document_quirks).

## What the screen is

The per-task labor DETAIL panel.  The workforce icon-grid overlay
(screen 0x02, `FUN_00412143`) is already ported (engine_workforce_hud.c,
s17); clicking a task icon there opens THIS screen, which shows that one
task's allocation detail (worker count bar + per-task readouts) with the
worker +/- steppers.  `DAT_00553f54` (a byte, 1..9) selects which task:

| task | id | sub-renderer | what it shows |
|---|---|---|---|
| Grain farming    | 1 | FUN_00413590 (1440 B) | fields, yield, seed, rats |
| Cattle farming   | 2 | FUN_00413b30 (1475 B) | herd size, dairy, births |
| Field reclamation| 3 | FUN_004140f3 ( 301 B) | reclaim progress |
| Castle building  | 4 | FUN_00414220 (  31 B) → FUN_0041dedb | castle build |
| Iron mining      | 5 | FUN_00412e6b ( 746 B, shared) | ore stock/rate |
| Stone quarrying  | 6 | FUN_00412e6b (shared) | stone stock/rate |
| Wood cutting     | 7 | FUN_00412e6b (shared) | wood stock/rate |
| Blacksmith       | 8 | FUN_00413155 ( 977 B) | weapon production |
| (idle)           | 9 | — (main renderer only) | idle workers |

`FUN_00412b33` (824 B) is the dispatcher/common-chrome renderer; tasks
5-7 share `FUN_00412e6b`; task 9 (idle) has no sub-renderer.

## Helper → port-name map (ALL already ported)

| retail | port | header |
|---|---|---|
| FUN_004af502 | io cwd-revert | io_path.h |
| FUN_004b1de0 | (HUD panel setup) | engine_hud_panel.h |
| FUN_0045240a | engine_territory dirty-mark | engine_territory.c |
| FUN_004af8d9 | asset load | io.h / engine_pl8.h |
| FUN_00409397 | filled rect | engine_terrain_info.c (via gfx) |
| FUN_00403cf4 | (rect) | engine_tooltip.c |
| FUN_0040a682 | sprite blit | gfx_sprite_render.h |
| FUN_00402d37 | gfx_wrap_localized_line(loc, grp, sub, x, y, font, glyph, color) | gfx_wrap.h |
| FUN_00402c5e | gfx_wrap_localized_line_centered | gfx_wrap.h |
| FUN_00402f64 | gfx_text_label_render_dec(val, prefix, suffix, x, y, font, glyph, color) | gfx_text_label.h |
| FUN_00402e0c | gfx_text_label_render_labeled_signed(val, mode, label, suffix, x, y, font, glyph, cpos, cneg) | gfx_text_label.h |
| FUN_0041ab67 | number + loc-group-8 unit substring — PORT AS generic helper (below) | (new, in engine_labor.c) |
| FUN_0040d1bc | gfx_sprite_render (continue-seal / bar) | gfx_sprite_render.h |
| FUN_004b414a | gfx_sprite solid-color block fill | gfx_sprite.h |
| FUN_0040a21a | icon blit (misc sheet DAT_005530c8) | (used by county_panels delta label) |
| FUN_0040a3d0 | sprite-with-index blit | gfx |
| FUN_00403deb | gfx_line_rect_bevel_dark_br | gfx_line.h |
| FUN_00426216 / FUN_00427990 | sound cue (no-op, audio.h deferred) | — |
| FUN_00419c78 | engine_hud_menubar (blacksmith reuses for chrome) | engine_hud_menubar.h |

`FUN_0041ab67(value, unit_sub, x, y, font, color)` decompiled (215 B):
saves `DAT_005cd404` (line-x acc), zeroes it, renders `value` via
render_dec with '@' prefix + empty suffix (`&DAT_004d41f4` = ""), then
loc-group-8 substring `unit_sub` (if |value|==1) else `unit_sub+1`
(plural) at `x + line_x_acc`, then restores the accumulator.  Identical
shape to county_panels' inline `county_crowns_label` (group 8, sub 0/1)
— extract a shared `labor_num_units(ctx, value, unit_sub, x, y, font,
color)` and OPTIONALLY refactor county_crowns_label onto it.

## `.rdata` tables (extracted via r2, bin.relocs.apply=true)

Per-task arrays, indexed `[task*4]` (i32):
```
DAT_004d2950 sound/anim cue (FUN_00426216 arg): t1..t8 = 7,4,6,0,10,8,9,0
DAT_004d2974 task icon index (FUN_00402d37 0x4a): t1..t8 = 0,0,0,2,3,5,6,7 (t9→0x10)
DAT_004d29a0 worker-bar unit sub / bar base:      t1..t8 = 13,13,9,11,9,9,9,9
```
Iron/stone/wood (FUN_00412e6b) uses per-task (local_8, local_c):
t5→(1,0xc) t6→(3,0xe) t7→(0,0x10) t8→(2, DAT_004d29c8[weapon]).
Blacksmith (FUN_00413155) tables:
```
DAT_004d29c8 weapon → unit sub:  0x18,0x16,0x1a,0x12,0x14,0x1c,...
DAT_004d29e0 weapon → hearth y-offset (i32): 0x88,0x8e,0x88,0x8e,0x77,0x7d,0x7c,0xb6,...
DAT_004d8990 weapon rate/cost table (stride 8): +0=6,10,4,4,3,10,6,3,0xd,0,4,0x12 ; +4(0x4d8994)=0x190,0x28,0x320,0x50,...
```
String constants: `DAT_004d41f4` = "" (empty); `DAT_004d3e58`
"iconvill.pl8", `DAT_004d3e68` "fire.wav", `DAT_004d3e74` "%",
`DAT_004d3e84` "hearth.pl8"; the `DAT_004d3ea4..DAT_004d3edc` block is a
run of single-space " " suffix/label constants (the real row labels come
from the FUN_00402d37 group-0x4d/0x4c calls that precede each value).

Loc groups used: **0x4a** (task names, primary), **0x4c**, **0x4d**
(the per-task readout labels), **8** (unit singular/plural).  Dump via
the gameplay-and-screens.md recipe.

## County-record field offsets (base g_per_army_768 = DAT_0053f9b0, stride 0x300, index s_slot_0057c93c)

Computed as `VA − 0x53f9b0`.  Access as `cty_i32(rec, off)` /
`cty_u8(rec, off)` (same accessors as engine_county_panels.c).
```
main FUN_00412b33 per-task worker triple @ +0xC4: (&DAT_0053fa74)[(task*3-3)*4]=allocated,
  +0xC8 wanted, +0xCC available  (colour: red 0xf9 if allocated<wanted, 0xfc if >; else 0x3f)
grain  0x224 fields  0x204 (yield/0x1d sprite)  0x278 seed  +0x1aa i16 seed-kind(0x87/0x8b)
       0x24c bushels(+/-)  0x230 herd?  0x248/0x2fc dairy  0x178 tax? 0x22c
cattle 0x250 herd  0x25c age(10/20/30)  0x274 dairy  +0x1aa i16 kind(0x88/0x89/0x8c/0x8d)
       0x270 births  0x268/0x26c in/out  0x258 net  0x17c ale?  0x258
reclaim 0x204 progress(@ suffix)  +0x204 state==1  0x214 active
castle FUN_0041dedb(county,-0x20,0x40,0) — read its decompile @ 0x41dedb
iron/stone/wood 0x2a8+local_8*0x18 stock, 0x294+local_8*0x18 label; +0x284/0x28c/0x280/0x288 rates
blacksmith 0x290 weapon-sel  0x2d8 rate  0x2c4 count  0x118 gold? +0x1c4 sound
```
(Offsets above are a first pass from the decompile — RE-VERIFY each
against the FUN_ line while porting; they are literal `DAT_0053fXXX −
0x53f9b0` and easy to get one nibble wrong.  The safe port keeps them as
raw offsets, no need to name the field.)

## Navigation + input (screen 0x0f open/close, from 42ff10.c)

OPEN (from the screen-0/0x02 input block, 42ff10.c:96-102): when
`FUN_0043a123()` (a hit-test, UNPORTED — read @ 0x43a123) returns
non-zero → `g_sim_current_screen = 0x0f; FUN_00476768(); DAT_005533f4 =
0; FUN_00412b33()`.  `FUN_00476768` is ported (engine_ingame_input.h).

IN-SCREEN (42ff10.c:114-128): `FUN_0040e7e4()` (generic click, ported as
cp_generic_click) OR `DAT_004e6900` (RMB) → close: next screen =
(`DAT_005533f4`==0 ? 0x02 : 0), then `DAT_005533f4=0; FUN_00427d19();
DAT_0057d340=2; FUN_0041438c()`.  `FUN_0041438c` (446 B) is a border
repaint (ported? check — it's the 0x1da..0x1df row fills).

The worker +/- steppers (move labor between tasks) are the per-frame
widget arm (FUN_004ba26e) + UI router (FUN_004ba9c8) for screen 0x0f,
same split as the tax/rations panels — find the 0x0f button table there.
`screen0f_commit` applies the labor reallocation.

## Port plan (next session, WITH the render-parity loop)

0. **KNOWN TRICKY BIT (verify first):** the main renderer FUN_00412b33's
   "sprite-state globals" are actually the GLYPH-RENDERER state, and it
   pokes them as scratch before FUN_00409397 (the icon-cell bevel rect):
     DAT_005c9258 = g_glyph_w            (gfx_glyph.c static)
     DAT_005aeb70 = glyph_height_short   (gfx_dispatch.c static)
     DAT_00591524 = glyph cursor_x       (gfx_glyph.c static)
     DAT_00591528 = glyph cursor_y       (gfx_glyph.c static)
   Retail does `g_glyph_w=0x19; glyph_h=k_task_bar[task]; cursor=(0x30,
   0x60); FUN_00409397(0x30,0x60,g_glyph_w,glyph_h)` — i.e. it reuses the
   glyph w/h globals to pass the icon-cell size, and seeds the glyph
   cursor (which the redraw-dirty FUN_00452628 later reads — quirk #216).
   These are gfx_glyph/gfx_dispatch STATICS: porting needs
   setter accessors (add gfx_glyph_set_cursor / _set_glyph_dims etc. if
   absent) AND care that seeding them doesn't corrupt the following text
   draws.  Verify this block's pixels FIRST with the index-diff — it's
   the only non-mechanical part of the whole screen.
1. engine_labor.h — ctx struct (mirror engine_county_panels_ctx_t:
   county_block, fonts DAT_005af8f0/005b2fa0, glyph table, loc, sheets
   iconvill/hearth/smithy) + function map + this field table.
2. engine_labor.c — generic `labor_num_units` (FUN_0041ab67), then
   FUN_00412b33 main + the 6 sub-renderers + FUN_0041dedb, 1:1.  Keep
   county fields as raw offsets.
3. Wire the screen-0x0f render arm in engine.c's render seam (composite
   strategic map first, like the county panels do, then the panel).
4. Port FUN_0043a123 (open hit-test) + the 0x0f input case + close.
5. Author `tools/traces/labor_click.jsonl`: load → open workforce grid
   (0x02) → click a task → capture labor panel per task → step workers
   → close.  olv3 capture, index-diff each task panel, iterate to
   byte-exact, then human scrub for sign-off.

Deferred like county_panels: the WAV cues (FUN_00426216/FUN_00427990 —
audio.h no-op) and any MP arm.
