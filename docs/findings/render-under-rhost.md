# Render under rhost — the gfx layer on retail's layout, the media shadow (2026-11-04)

FRONT ★3: make the port's render code verifiable against retail under rhost. Before: no render function had a
hook — the gfx modules kept retail's render globals in file-statics and drew into a canvas only the standalone
bound, so they could not run inside retail, and the shadow compared state but not pixels. After: the gfx layer runs
on retail's own globals and draws into retail's live back buffer; a render row's shadow compares every surface
pixel too; 64 rows (`tools/rhost/rh_hooks_gfx.c`) are verified over the corpus.

## Result

- **64 rows** in `tools/rhost/rh_hooks_gfx.c`: the gfx primitives (paint, glyph, sprite, RLE, clip / dispatch,
  line / rect / bevel), the sprite renderers and seal, the panels (incl. the chrome twins), text / labels /
  formatter, word-wrap, the caret, and three renderers above them (the menu-bar parchment strip FUN_00409e09, the
  menu-bar labels FUN_0040c5b0, the button strip FUN_0040cfd2).
- `rhost.py verify --target wine -j 3 --hooks '^(gfx_|hud_)'` (62 rows) + `--hooks '^(button_render_list|hud_menubar_labels)$'`
  over the whole corpus (51 scenarios): **52 reached, 0 divergences** — 2,710,762 calls compared on identical state
  AND identical pixels (RH_SH_MEDIA), 554,780,920 more run by sampled-out passes (RH_SH_SAMPLE, below); 12 never
  called. Shadow passes: 3 (nesting); pass 1 13 min, the hybrid 2 min.
- **Hybrid PASS**: the 62 rows replaced at once, the 2 at once, and then **all 64 together — and every row of every
  table at once (506: AI, combat, battle, events, economy, the battlefield partitions, rl, gfx)** — `corpus --check`
  PASS on all 51 scenarios: every blessed state hash and every presented frame's framebuffer hash.
- Found and fixed in the port: the RLE 3-run spill, the rect's line order (state), the darken table, the panels' /
  strip's shape, the measurers' shared cache, the label highlight's staging (§6). Two rhost tool bugs: the wine
  runner hang and the 8 KB hook-spec cut (§8).

## 1. The gfx state on retail's layout

`src/gfx_globals.h` / `.c` hold every retail global the gfx modules use (69 of them), one non-static definition
each, named `g_gfx_<what>` with the retail VA in the comment. The modules (gfx_paint / glyph / dispatch / sprite /
sprite_rle / line / text / text_label / wrap / sprite_render / panel / caret, engine_hud_menubar, engine_button) read and write
them directly. The standalone and the host tests link `gfx_globals.c`; rhost does not (build.sh never links
`*_globals.c`) — `rh_hooks_gfx.c` defines each name as an ABSOLUTE symbol at its VA (`A(name, 0xVA)`), so the linked
port code reads and writes retail's memory: no copies, no sync. A one-line check (header comments vs. aliases)
found 0 mismatches over the 69.

**Widths are retail's** (data_xrefs.tsv + the asm of every reader/writer): a wider port type would clobber the
neighbouring retail global once aliased.

| global | retail | port before |
|---|---|---|
| DAT_005c9258 glyph width | WORD (`movsx ecx, word_5C9258`) — DAT_005c925a next to it is the byte collapse flag | int |
| DAT_005aeb70 descriptor height | WORD (movsx) | int |
| DAT_005cdd20 / DAT_0058fe18 clip modes | BYTE | int |
| DAT_005aeb7c collapse-spaces, DAT_005c925a collapse-active, DAT_004eac50 screen | BYTE | int |
| DAT_0057d3bc color | DWORD (written whole, the blitters use `bl`) | uint8_t |

The descriptor reads are now sign-extended (`(int16_t)`), as retail's movsx.

**Not retail globals** — the port's own bindings, set once under rhost (`gfx_bind`): the default and tall font
blobs retail compares by address (`&DAT_005af8f0`, `&DAT_005b2fa0`), and the sprite renderers' / panels' error
log (retail FUN_004afab9(fmt, 0, n), called through `rh_port_retail_enter/leave`). The char tables are retail
globals passed as arguments: render DAT_004d71f0, measure DAT_004d71d0 (= +0x20 apart).

## 2. The canvas = retail's live back buffer

Every retail blitter computes its destination as `Buf + cursor_x + cursor_y * xRight` (`mov edi, Buf; add edi,
dword_591524; mov eax, dword_591528; mov edx, xRight; mul edx; add edi, eax`). `Buf` = DAT_004ea1a8, written by
FUN_0042ec91: it creates the 640×480 offscreen composition surface DAT_004eafac (FUN_0042f148(xRight, yBottom, 1)),
Locks it, stores `lpSurface` in Buf, Unlocks, and keeps drawing through the cached pointer; FUN_0042eaa6 clears it.
xRight DAT_004eb274 = 0x280 and yBottom DAT_004eac4c = 0x1e0 are set once (FUN_004b2258). The port's canvas IS those
three globals (`g_gfx_canvas_pixels / _width / _height`), so under rhost it is that surface in rhost's media pool
(rh_ddraw.c, 0x6000_0000..) — nothing to bind.

## 3. The media shadow (pixel-level)

`rh_state.c` compared .data + the arena, not the media pool, so a render divergence surfaced only at the next
presented frame (hybrid), and a shadowed render ran retail and then the port on the same pixels (a non-idempotent
op — the darken remap — would have been applied twice).

- rh_media.c tags each live block with its surface's pitch (`rh_media_alloc_surface`, from rh_ddraw `surf_new`);
  `rh_media_surface_ranges` lists the live surface blocks (address order), `rh_media_surface_at` finds the block of
  an address.
- `rh_shadow_ex_f(…, RH_SH_MEDIA)` appends those ranges to the state ranges for the call: page-tracked like the
  state (write-protect, copy on first write — the default) or copied (`--shadow-full`); retail's pixels are kept,
  the port's compared. A differing surface dword is reported as its first differing pixel,
  `px(X,Y)@VA rRR/pPP`, and counted per pixel: `(N px differ)`.
- A hook row sets it in `hook_t.flags` (`RH_SH_MEDIA`); `--shadow-media` turns it on for every shadowed call.
- `--shadow-poison-px X,Y` flips bit 0 of canvas pixel (X,Y) after every media-compared port call. endturn-2,
  three render rows: **6126/6126 calls diverge** (paged), **1943/1943** (`--shadow-full`), every one reported as
  `px(100,100)@6005aa64 r../p.. (1 px differ)`; without the poison 0.

Found on its first run: the RLE 3-run spill (below) — `px(269,162)@6006460d r00/p80`.

## 4. Sampling the hot leaves (RH_SH_SAMPLE)

A shadowed call costs ~400–460 µs under wine (snap 65, retail 160, port 140, cmp 75 µs: the page faults dominate),
a glyph blit ~1 µs. endturn-2 alone makes 470k glyph dispatches, 133k 16×16 blits, 123k clipped pixels (3700
frames); the corpus is ~1.06 M frames → ~250 M render calls: every-call shadowing would be ~30 CPU-hours a pass.
Rows flagged `RH_SH_SAMPLE` (rh_hooks.c `rh_hook_sample`) shadow, per (row, caller's return address, screen id
DAT_004eac50), the first 64 calls and then every power-of-two-th; the rest run retail and count in `sampled`
(`hook-result … sampled=N`; rhost.py verify prints the column), not in `calls`. Every call site on every screen is
compared; the hybrid pass runs every call through the port and checks every presented frame. `--shadow-all`
disables it. A sampled-out outer call runs retail outside any shadow, so the rows nested in it get their own
chance — most leaves are reached in pass 1.

## 5. Rows

Depth = the shadow pass that reached the row; `calls` = calls shadowed (state + pixels compared); `sampled` = calls a
sampled row let run retail-only in the shadow passes (the hybrid ran every one through the port).

| row | retail | port | depth | calls | sampled | scenarios |
|---|---|---|---:|---:|---:|---:|
| `gfx_paint_pixel` | FUN_004b3f54 | `gfx_paint_pixel` | 1 | 19389 | 85234794 | 49/51 |
| `gfx_paint_2x1` | FUN_004b3f7a | `gfx_paint_2x1` | 1 | 6 |  | 1/51 |
| `gfx_paint_2x1_under` | FUN_004b3fa3 | `gfx_paint_2x1_under` | - | 0 |  | 0/51 |
| `gfx_paint_pixel_clipped` | FUN_004039c7 | `gfx_paint_pixel_clipped` | 1 | 22215 | 85254183 | 49/51 |
| `gfx_paint_2x1_clipped` | FUN_00403a2c | `gfx_paint_2x1_clipped` | - | 0 |  | 0/51 |
| `gfx_paint_darken_rect` | FUN_004b13ed | `gfx_paint_darken_rect` | 1 | 9 |  | 9/51 |
| `gfx_glyph_blit` | FUN_004b41b7 | `gfx_glyph_blit` | 1 | 22829 | 65859742 | 51/51 |
| `gfx_glyph_blit_left` | FUN_004b425c | `gfx_glyph_blit_left_clipped` | - | 0 |  | 0/51 |
| `gfx_glyph_blit_right` | FUN_004b42c5 | `gfx_glyph_blit_right_clipped` | - | 0 |  | 0/51 |
| `gfx_sprite_blit` | FUN_004b43b1 | `gfx_sprite_blit` | 1 | 32611 | 1012441 | 49/51 |
| `gfx_sprite_blit_left` | FUN_004b446d | `gfx_sprite_blit_left_clipped` | 1 | 2267 | 4596 | 29/51 |
| `gfx_sprite_blit_right` | FUN_004b44d2 | `gfx_sprite_blit_right_clipped` | 1 | 3436 | 23951 | 45/51 |
| `gfx_sprite_fill_block` | FUN_004b414a | `gfx_sprite_fill_block` | 1 | 227868 |  | 47/51 |
| `gfx_sprite_opaque` | FUN_004b432e | `gfx_sprite_blit_opaque` | 2 | 121533 |  | 49/51 |
| `gfx_sprite_raw16` | FUN_004b49f7 | `gfx_sprite_blit_16x16_raw` | 2 | 36462 | 70292767 | 49/51 |
| `gfx_sprite_raw24` | FUN_004b4cb8 | `gfx_sprite_blit_24x24_raw` | 1 | 12459 | 365931 | 45/51 |
| `gfx_sprite_raw32` | FUN_004b5333 | `gfx_sprite_blit_32x32_raw` | 1 | 17140 | 4794626 | 45/51 |
| `gfx_sprite_raw2` | FUN_004b49b7 | `gfx_sprite_blit_2x2_raw` | 1 | 7683 | 18834147 | 13/51 |
| `gfx_rle_blit` | FUN_004b4537 | `gfx_sprite_rle_blit` | 1 | 34714 | 5689197 | 49/51 |
| `gfx_rle_blit_left` | FUN_004b4814 | `gfx_sprite_rle_blit_left_clipped` | 1 | 6753 | 48292 | 33/51 |
| `gfx_rle_blit_right` | FUN_004b48b8 | `gfx_sprite_rle_blit_right_clipped` | 1 | 10730 | 274063 | 33/51 |
| `gfx_clip_x` | FUN_0040464c | `gfx_dispatch_clip_x` | 1 | 90931 | 73025935 | 51/51 |
| `gfx_clip_y` | FUN_0040477b | `gfx_dispatch_clip_y` | 1 | 90931 | 73025935 | 51/51 |
| `gfx_dispatch_glyph` | FUN_00402a14 | `gfx_dispatch_glyph` | 1 | 80439 | 65882571 | 51/51 |
| `gfx_dispatch_sprite` | FUN_00404442 | `gfx_dispatch_sprite` | - | 0 |  | 0/51 |
| `gfx_dispatch_sprite_clipped` | FUN_00404547 | `gfx_dispatch_sprite_clipped` | - | 0 |  | 0/51 |
| `gfx_line_draw` | FUN_00403a8f | `gfx_line_draw` | 1 | 5842 | 47846 | 44/51 |
| `gfx_line_dotted` | FUN_00403c49 | `gfx_line_draw_dotted` | - | 0 |  | 0/51 |
| `gfx_line_rect` | FUN_00403cf4 | `gfx_line_rect` | 1 | 229088 |  | 47/51 |
| `gfx_line_fill_rect` | FUN_0040437d | `gfx_line_fill_rect` | 1 | 62528 |  | 49/51 |
| `gfx_bevel_dark_br` | FUN_00403deb | `gfx_line_rect_bevel_dark_br` | 1 | 91823 |  | 46/51 |
| `gfx_bevel_palette_b` | FUN_00403ee4 | `gfx_line_rect_bevel_palette_b` | 1 | 199589 |  | 51/51 |
| `gfx_bevel_dark_tl` | FUN_00403fdd | `gfx_line_rect_bevel_dark_tl` | 1 | 71983 |  | 49/51 |
| `gfx_render_raw` | FUN_0040a01d | `gfx_sprite_render_raw` | 1 | 34784 |  | 49/51 |
| `gfx_render_from_blob` | FUN_0040a21a | `gfx_sprite_render_from_blob` | 1 | 101757 | 207737 | 49/51 |
| `gfx_render_rle_from_blob` | FUN_0040a3d0 | `gfx_sprite_render_rle_from_blob` | - | 0 |  | 0/51 |
| `gfx_render_opaque_from_blob` | FUN_0040a567 | `gfx_sprite_render_opaque_from_blob` | 1 | 121533 |  | 49/51 |
| `gfx_seal_draw` | FUN_0040d1bc | `gfx_sprite_seal_draw` | 1 | 163663 |  | 49/51 |
| `gfx_render` | FUN_0040a682 | `gfx_sprite_render` | 1 | 6475 | 114561 | 46/51 |
| `gfx_render_trunc_bottom` | FUN_0040a80e | `gfx_sprite_render_truncate_bottom` | 1 | 2 |  | 1/51 |
| `gfx_render_trunc_top` | FUN_0040a9b0 | `gfx_sprite_render_truncate_top` | - | 0 |  | 0/51 |
| `gfx_panel_border` | FUN_0040946d | `gfx_panel_draw_border` | 2 | 253 |  | 51/51 |
| `gfx_panel_fill` | FUN_004097c5 | `gfx_panel_fill_interior` | 2 | 253 |  | 51/51 |
| `gfx_panel_draw` | FUN_00409346 | `gfx_panel_draw` | 1 | 253 |  | 51/51 |
| `gfx_panel_border_chrome` | FUN_00409934 | `gfx_panel_draw_border_chrome` | 1 | 5557 |  | 46/51 |
| `gfx_panel_fill_chrome` | FUN_00409c93 | `gfx_panel_fill_interior_chrome` | 1 | 94322 |  | 46/51 |
| `gfx_panel_bevel` | FUN_004093e0 | `gfx_panel_draw_bevel` | 1 | 165415 |  | 49/51 |
| `gfx_text_line` | FUN_00402637 | `gfx_text_render_line` | 1 | 68221 | 2103372 | 51/51 |
| `gfx_text_line_centered` | FUN_004025d7 | `gfx_text_render_line_centered` | 1 | 13159 | 248027 | 51/51 |
| `gfx_fmt_dec` | FUN_004022bd | `gfx_text_label_format_dec` | 1 | 13028 | 310752 | 51/51 |
| `gfx_fmt_hex` | FUN_00402447 | `gfx_text_label_format_hex` | - | 0 |  | 0/51 |
| `gfx_label_dec` | FUN_00402f64 | `gfx_text_label_render_dec` | 1 | 68585 | 306638 | 49/51 |
| `gfx_label_hex` | FUN_00403015 | `gfx_text_label_render_hex` | - | 0 |  | 0/51 |
| `gfx_label_dec_centered` | FUN_004030c6 | `gfx_text_label_render_dec_centered` | 1 | 15957 | 16955 | 49/51 |
| `gfx_label_dec_signed` | FUN_00403190 | `gfx_text_label_render_dec_signed` | - | 0 |  | 0/51 |
| `gfx_label_labeled_signed` | FUN_00402e0c | `gfx_text_label_render_labeled_signed` | 1 | 18172 | 12573 | 49/51 |
| `gfx_wrap_line` | FUN_00402d37 | `gfx_wrap_localized_line` | 1 | 120156 | 730548 | 49/51 |
| `gfx_wrap_line_centered` | FUN_00402c5e | `gfx_wrap_localized_line_centered` | 1 | 91636 | 243332 | 51/51 |
| `gfx_wrap_buffer` | FUN_0040352f | `gfx_wrap_buffer` | 1 | 133 |  | 4/51 |
| `gfx_wrap_localized` | FUN_0040328e | `gfx_wrap_localized` | 1 | 44946 | 590299 | 49/51 |
| `gfx_caret` | FUN_0040acce | `gfx_caret_render` | 1 | 3488 | 10778 | 49/51 |
| `hud_menubar_tile_strip` | FUN_00409e09 | `engine_hud_menubar_tile_strip` | 1 | 24376 |  | 49/51 |
| `button_render_list` | FUN_0040cfd2 | `engine_button_render_list` | 1 | 26568 | 208955 | 45/51 |
| `hud_menubar_labels` | FUN_0040c5b0 | `engine_hud_menubar_labels` | 1 | 6812 | 5376 | 49/51 |

The 12 rows at 0 calls are never reached by the corpus (in the hybrid either); the 2×1 painters, the clipped glyph
blits and the clipped sprite dispatcher were read against the asm instead (match).

## 6. What the shadow found (fixed in the port)

| # | function | divergence | cause |
|---|---|---|---|
| 1 | FUN_004b4537 RLE blit | shadow: `px(269,162)@6006460d r00/p80` (90/90 sampled calls, endturn-2) | a 3-pixel literal run is copied as two WORDS (`mov ax,[esi+1]; mov [edi],ax; mov ax,[esi+3]; mov [edi+2],ax`): 4 bytes, dst advances 3 — dst[3] gets the NEXT opcode byte. A following skip run leaves a 0 pixel there; a 3-run that ends the row leaves the next row's opcode one pixel past the row. 3 is the only length without an exact store sequence (5 / 7 / … end in a byte store). |
| 2 | FUN_00403cf4 rect | shadow: `005aeb48 r000001e1/p00000152 005aeb5c …` (1510/1510) | the port drew top→right→bottom→left (the bevel order); retail draws top, bottom, left, right — the last line leaves its endpoints in the Bresenham registers DAT_005aeb40..60 (state). |
| 3 | FUN_004b13ed darken | (read) | retail builds the remap in its own table DAT_004eac60 (shared with FUN_004b1310 / FUN_004b14ff); the port used a local. |
| 4 | FUN_0040946d / FUN_004097c5 panels | (read) | retail saves the glyph cursor in DAT_00591508 / DAT_00591504 and restores it on exit (not on the error return), writes the last-glyph index DAT_005c9288 and the flag bytes per tile, logs + latches error 1 on a bad offset; the port did none of it. The chrome twins FUN_00409934 / FUN_00409c93 (sheet DAT_0057d3d0 baked in, own messages) are now separate port functions; FUN_004093e0 composes them. |
| 5 | FUN_004025d7 centered text, FUN_0040352f / FUN_0040328e wrap, FUN_0040acce caret | (read) | the measurers (FUN_004014f0 / FUN_004036f9 / FUN_004015b9) cache into SHARED globals — DAT_005c9288 (the dispatcher's last glyph), DAT_0058fd88 (its descriptor offset), DAT_005c9a7c (bytes consumed) — the port measured on a zeroed local state. Now each measurement runs on the globals. |
| 6 | FUN_00409e09 menu-bar strip | (read) | same shape as the panels (cursor save / restore, flags, log + latch); the port called it "inert". |
| 7 | FUN_0040c5b0 menu-bar labels | (read) | retail raises the redraw latches DAT_004eb260 / DAT_004e65f8, measures the hovered label on the shared cache, stages ONLY the highlight's width / height (the port's `gfx_glyph_set_glyph(0, w, 0)` also zeroed the glyph offset DAT_0058fe04 and row count DAT_005bb478), and tails with the FUN_004527a6 mark the port elided. Now an optional dirty state (NULL in the standalone). |
| 8 | FUN_0040cfd2 button strip | (read) | the flag bytes were left unstaged; a PRESSED button marks through FUN_00452775 (unported; now a seam, retail's under rhost), not FUN_004527a6. |

Rows 3–8 were read in the asm while writing the adapters and fixed before their first shadow: each then ran at 0
divergences.

## 7. Quirks (retail ground truth) — for engine-quirks.md

- RLE 3-run spill (#1 above): FUN_004b4537 writes one byte past every 3-pixel literal run.
- The panel / strip passes clobber DAT_00591508 / DAT_00591504 (a cursor save slot shared with ~8 other renderers:
  FUN_004071a0, 00407f82, 004081a6, 00408438, 00408c50) and do not restore the cursor on the error return.
- FUN_004b432e (the opaque blit) takes its blob in EAX (`mov esi,[ebp+8]; mov esi,eax`); all three call sites
  load EAX with the value they push. The row's retail side goes through a thunk that sets EAX.
- The formatter buffer DAT_0058fda0 is 100 bytes in retail (Offset DAT_0058fe04 follows); the port's 8000 is a
  standalone safety margin. A suffix longer than ~90 bytes would run into the glyph offset in retail.
- The line rect FUN_00403cf4 and the bevels end on different edges (right vs left): the Bresenham registers after a
  rect are the right edge's, after a bevel the left edge's.

## 8. rhost core changes (shared, backward-compatible)

| file | change |
|---|---|
| `rh_media.c` / `rhost.h` | live blocks carry a tag (a surface's pitch); `rh_media_alloc_surface`, `rh_media_surface_ranges`, `rh_media_surface_at` |
| `rh_ddraw.c` | `surf_new` allocates its pixels with `rh_media_alloc_surface` (one line) |
| `rh_state.c` / `rhost.h` | `rh_shadow_ex_f(…, flags)`: `RH_SH_MEDIA` adds the surface ranges (paged and `--shadow-full`), per-pixel reporting / counting; `rh_shadow_ex` = flags 0, unchanged; `--shadow-poison-px` |
| `rh_hooks.h` / `rh_hooks.c` | `hook_t` gains `flags` and `sampled` (trailing fields: every positional row initializer still compiles); `rh_hook_sample`; `hook-result … sampled=N`; `T(RH_HOOKS_GFX)` |
| `rh_hooks_ai.c` / `rh_port.h` | `rh_port_run` passes the row's flags, skips the shadow for a sampled-out call (retail runs, as inside a shadow), copies TEN argument slots (FUN_00403190 has 10) |
| `rhost.c` / `rhost.h` | options `--shadow-media`, `--shadow-all`, `--shadow-poison-px X,Y`; `L2_CANVAS` / `L2_PITCH` |
| `rh_hooks.c` | the hook-spec buffer 8 KB → 128 KB, fatal if longer: every row replaced at once is 11.3 KB (506 rows) — the 8 KB buffer silently cut the tail off (the all-rows hybrid on master's 442 rows was already past it) |
| `rhost.py` | verify prints / records `sampled` (master's `--hooks` kept: a subset writes no evidence); **the wine runner writes wine's output to a file, not a pipe** (below) |
| `build.sh` | links the gfx closure (gfx_line / text / text_label / wrap / sprite_render / panel / caret, font_metrics, localization, engine_hud_menubar, engine_button) |

**The wine-runner hang (fixed).** `run_batch_wine` captured wine's stdout/stderr through pipes. A wine launch that
has to start the prefix's wineserver hands it its stdio; the wineserver (shared by every wine job in the prefix,
kept alive by the other agents' runs) then holds the pipes' write ends forever, `communicate()` never sees EOF, and
that worker thread blocks: the first verify of this batch ran pass 1 on one worker of three and then hung after the
last scenario (`/proc/<wineserver>/fd/2 -> pipe:[…]` = the verify's read end). Wine's output now goes to
`<run>/wine.out` directly; `subprocess.run` waits for `start.exe`, which waits for the program.

## 9. What blocks the next render ports

- **The screen renderers keep file-static mirrors and stubs.** `engine_title_render.c` (27 ports) holds ~30 statics
  mirroring retail globals (DAT_00553114, DAT_0053e90c, …) plus panel / background / page stubs;
  `engine_map_render.c` / `engine_overlay_render.c` keep camera and binding state in statics and bind blobs through
  setters; `engine_tooltip.c` compensates the port's frame order (quirk #214). Each needs the same move as here —
  globals in a `*_globals.c` aliased at their VAs, seams to retail for what is unported — before a row means
  anything. The gfx layer below them is now verified, so a divergence there is the renderer's own.
- **Dirty-rect bookkeeping the ports elided** ("the shim tracks dirty regions"): FUN_004527a6 marks are retail state.
  The button strip and the menu-bar labels now take an optional `engine_dirty_rect_state_t` (NULL in the standalone);
  the other renderers need the same.
- **Never-reached rows** (no corpus path): FUN_004b3fa3 / 00403a2c (2×1 painters), FUN_004b425c / 004b42c5 (clipped
  glyphs: no text in the corpus straddles a clip edge), FUN_00404442 / 00404547 (sprite dispatchers — no E8 call
  site: entry-patched), FUN_00403c49 (dotted line), FUN_0040a3d0 / 0040a9b0 (RLE-from-blob, top-trim render),
  FUN_00402447 / 00403015 (hex), FUN_00403190 (signed centered label). They need a screen that uses them
  (human-traces) or a poke probe.
- **Hot-leaf cost.** Sampling keeps a verify pass at ~30 min on wine (-j 3); `--shadow-all` on one scenario is the
  check for a suspected rare-path divergence.
- The five standalone call sites that render retail's FUN_00409c93 (engine_trade / sendsupplies / county_panels /
  armory ×2) called `gfx_panel_fill_interior` (FUN_004097c5: same pixels, the other error message); they now call
  `gfx_panel_fill_interior_chrome`.
