/*
 * OpenLords2 — higher-level sprite renderers.
 *
 * Five retail leaves that sit at the top of the sprite-class blit
 * stack — engine callsites invoke these (NOT the dispatcher in
 * gfx_dispatch.c) when they want to render a sprite from a blob.
 * Each is a sibling-of-dispatcher: same staging shape, same clip
 * pipeline, same three-blitter route, BUT with extra bookkeeping
 * the dispatcher itself omits — a "current sprite index" write to
 * DAT_005c9288, a descriptor-validity gate, an error-log call, and
 * an error-code latch.  Two variants add per-call source-offset
 * adjustments (top-row skip / bottom-row truncate).
 *
 * Retail address map:
 *
 *   FUN_0040a01d @ 0x40a01d (261 B) → gfx_sprite_render_raw
 *                                     Render a caller-supplied raw
 *                                     buffer (no descriptor) at (x, y)
 *                                     with explicit width/height.
 *                                     Early-return on NULL buffer.  No
 *                                     descriptor validity check, no
 *                                     error log.
 *   FUN_0040a21a @ 0x40a21a (433 B) → gfx_sprite_render_from_blob
 *                                     Render sprite[index] from a
 *                                     caller-supplied blob.  Three
 *                                     error gates: NULL blob, descriptor
 *                                     src_offset < 1, descriptor
 *                                     src_offset > 0x4baf0.
 *   FUN_0040a682 @ 0x40a682 (391 B) → gfx_sprite_render
 *                                     Same shape but reads the blob
 *                                     from the module-private "current
 *                                     blob" pointer (mirrors retail
 *                                     DAT_004eabec).  Two error gates;
 *                                     no NULL-blob check (caller is
 *                                     responsible for binding a non-
 *                                     NULL blob).
 *   FUN_0040a80e @ 0x40a80e (413 B) → gfx_sprite_render_truncate_bottom
 *                                     Same as gfx_sprite_render but
 *                                     trims the bottom `trim_rows`
 *                                     rows off the visible portion
 *                                     (descriptor.height -= trim_rows).
 *   FUN_0040a9b0 @ 0x40a9b0 (439 B) → gfx_sprite_render_truncate_top
 *                                     Same as gfx_sprite_render but
 *                                     trims the top `skip_rows` rows
 *                                     off the visible portion (height
 *                                     shrinks, dst_y shifts down, source
 *                                     pointer advances).
 *
 * State globals (mirror retail DAT_* names):
 *
 *   current_blob       DAT_004eabec — pointer to the "currently active"
 *                                    sprite blob.  Retail allocates it
 *                                    via _malloc (FUN_004b25b0) and
 *                                    reads from it across many callers.
 *                                    Settable via the bind API below.
 *   last_error         DAT_005aeb68 — error-code byte latch.  Retail
 *                                    writes via FUN_00408fb0 (27 B, just
 *                                    `DAT_005aeb68 = param_1; return;`).
 *                                    All four bounds-checking wrappers
 *                                    here latch code 4 on a descriptor
 *                                    failure.
 *
 * Error log hook:
 *
 *   FUN_004afab9 @ 0x4afab9 (897 B) is the retail "format an error
 *   message and append it to the in-memory log buffer" function.  Its
 *   formatting depends on FUN_004b049b (timestamp), FUN_004afe3a (log
 *   write), the per-format-id string literals in rdata at 0x4d2314..,
 *   and a 4KB log-line buffer at &DAT_004eadb0 — none of which are
 *   ported yet.  We surface the call as a NULL-default callback hook
 *   `gfx_sprite_render_log_fn`.  Tests can install a hook that records
 *   each (fmt_addr, sprite_index_plus_one) tuple to verify the error
 *   path was taken without porting the formatter itself.
 *
 *   The `fmt_addr` argument is the retail RVA of the format string
 *   passed as FUN_004afab9's first arg — preserved as an opaque
 *   uint32_t (we won't dereference it; tests check addresses against
 *   the enum below).
 *
 * Quirks preserved verbatim — see engine-quirks.md for narratives:
 *
 *   - All five wrappers pre-set the dst_stride_pad global
 *     (`_DAT_005cd400 = 0x280 - DAT_005c9258`) BEFORE invoking the
 *     horizontal-clip setter — which unconditionally overwrites the
 *     same global at the end of its body.  The pre-set is dead code.
 *     Skipped in the port; the visible output is identical because
 *     clip_x always wins.  Same flavor as the centered-label
 *     buffer[0] pre-write quirk.
 *
 *   - The bounds-check sentinels are asymmetric.  `src_offset > 0x4baf0`
 *     (310000) is the "too big" reject; `src_offset < 1` is the "too
 *     small" reject.  Together they accept the open interval
 *     [1, 310001).  Zero IS rejected — meaning a descriptor with a
 *     literal zero src_offset is treated as invalid, even though zero
 *     is otherwise a valid relative offset inside a blob (the
 *     dispatcher accepts it).  Almost certainly a "no real sprite
 *     starts at offset 0" 1996 assumption — wherever sprite data
 *     lives in a real PL8 file, the 8-byte header pushes the first
 *     descriptor's src_offset to at least 8.  Latent footgun preserved.
 *
 *   - The raw-buffer wrapper (FUN_0040a01d) doesn't reset the
 *     descriptor-flag globals (DAT_00591568 / DAT_0057d3c0).  Whatever
 *     the previous caller staged there carries over into the next blit.
 *     Latent — those flags have no consumer yet.
 *
 *   - The truncate-bottom wrapper (FUN_0040a80e) subtracts `trim_rows`
 *     UNCONDITIONALLY from the staged height_short.  If `trim_rows` is
 *     negative, height_short grows past the descriptor's natural
 *     extent — the blitter would read past the end of the source data.
 *     Latent; preserved verbatim.
 */

#ifndef OPENLORDS2_GFX_SPRITE_RENDER_H
#define OPENLORDS2_GFX_SPRITE_RENDER_H

#include <stdint.h>

/* Format-string opaque IDs — mirror the retail rdata addresses of the
 * format-string literals passed to FUN_004afab9.  Tests inspect the
 * value passed to the log hook against one of these. */
enum {
    GFX_SPRITE_RENDER_ERR_FROM_BLOB_NULL                = 0x4d2314,
    GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_HIGH      = 0x4d2330,
    GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_LOW       = 0x4d234c,
    GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_HIGH         = 0x4d2418,
    GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_LOW          = 0x4d2434,
    GFX_SPRITE_RENDER_ERR_TRUNCATE_BOTTOM_INDEX_TOO_HIGH= 0x4d2450,
    GFX_SPRITE_RENDER_ERR_TRUNCATE_BOTTOM_INDEX_TOO_LOW = 0x4d2470,
    GFX_SPRITE_RENDER_ERR_TRUNCATE_TOP_INDEX_TOO_HIGH   = 0x4d2490,
    GFX_SPRITE_RENDER_ERR_TRUNCATE_TOP_INDEX_TOO_LOW    = 0x4d24b0
};

/* Hook fired when a descriptor-validation gate trips.  Default is a
 * no-op.  Tests install one that records (fmt_addr, sprite_index + 1)
 * tuples — `sprite_index_plus_one` is the literal value retail passes
 * as FUN_004afab9's third arg (param_2 + 1), so log messages render
 * 1-based indices. */
typedef void (*gfx_sprite_render_log_fn)(uint32_t fmt_addr,
                                         int      sprite_index_plus_one);
void gfx_sprite_render_set_log(gfx_sprite_render_log_fn fn);

/* Read / clear the error-code byte latch (DAT_005aeb68).  Retail
 * FUN_00408fb0 stores `param_1` here; readers consult this to learn
 * whether the last render attempted but failed.  All four bounds-
 * checking wrappers store code 4 on failure; success leaves the latch
 * untouched (so a sequence "render-OK, render-OK, render-fail" leaves
 * 4 in the latch; sequence "render-OK, render-fail, render-OK" also
 * leaves 4 — the latch is sticky until explicitly cleared). */
uint8_t gfx_sprite_render_last_error(void);
void    gfx_sprite_render_clear_error(void);

/* Bind / consult the module-private "current sprite blob" pointer.
 * The global-blob variants (gfx_sprite_render, _truncate_bottom,
 * _truncate_top) read this pointer; the caller-blob variant
 * (gfx_sprite_render_from_blob) takes the blob as a parameter.
 * Pass NULL to unbind.  Mirrors retail DAT_004eabec — a heap pointer
 * allocated by FUN_004b25b0 and freed by FUN_004b25fc. */
void gfx_sprite_render_set_blob(const uint8_t *blob);
const uint8_t *gfx_sprite_render_blob(void);

/* Port of FUN_0040a01d.  Render a raw caller-supplied buffer at
 * (x, y) with the given width × height.  Source pixels are densely
 * packed at `w` bytes per row starting at `buffer[0]`.  No descriptor
 * read; no validity check.  If `buffer` is NULL the call is a no-op
 * (matches retail's early-out `if (param_1 != 0)` gate).
 *
 * Sets the last-glyph-index global to 0 (sprite_index is implicit).
 * Stages width/height into the dispatcher staging slots, clears the
 * source offset to 0, runs canvas-clip and dispatches to one of the
 * three sprite blitters. */
void gfx_sprite_render_raw(const uint8_t *buffer,
                           int x, int y,
                           int16_t w, int16_t h);

/* Port of FUN_0040a21a.  Render sprite[index] from a caller-supplied
 * blob at (x, y).
 *
 * Error gates (in order):
 *   1. blob == NULL:                         GFX_SPRITE_RENDER_ERR_FROM_BLOB_NULL
 *   2. descriptor.src_offset > 0x4baf0:      GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_HIGH
 *   3. descriptor.src_offset < 1:            GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_LOW
 *
 * Each gate latches error code 4 and fires the log hook with the
 * matching fmt_addr.  Gate 1 also skips the descriptor stage; gates
 * 2 and 3 fire AFTER the descriptor has been staged (so the staging
 * side-effects are visible to subsequent calls). */
void gfx_sprite_render_from_blob(const uint8_t *blob,
                                 int sprite_index,
                                 int x, int y);

/* Port of FUN_0040a682.  Render sprite[index] from the bound global
 * blob at (x, y).  No NULL check on the blob (retail dereferences
 * DAT_004eabec unconditionally).  Two error gates:
 *
 *   1. descriptor.src_offset > 0x4baf0:  GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_HIGH
 *   2. descriptor.src_offset < 1:        GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_LOW
 */
void gfx_sprite_render(int sprite_index, int x, int y);

/* Port of FUN_0040a80e.  Render sprite[index] from the bound global
 * blob at (x, y), trimming `trim_rows` rows off the BOTTOM of the
 * visible portion.  Effective visible height = descriptor.height -
 * trim_rows.  If `trim_rows` is 0 the call is identical to
 * gfx_sprite_render.
 *
 * Same error gates as gfx_sprite_render — but using the
 * GFX_SPRITE_RENDER_ERR_TRUNCATE_BOTTOM_* fmt_addrs. */
void gfx_sprite_render_truncate_bottom(int sprite_index,
                                       int x, int y,
                                       int16_t trim_rows);

/* Port of FUN_0040a9b0.  Render sprite[index] from the bound global
 * blob at (x, y), skipping `skip_rows` source rows from the TOP.
 * Net effect: a "blit the bottom (height - skip_rows) rows anchored
 * at (x, y + skip_rows)" — visible pixels land at the same absolute
 * position regardless of skip_rows (the dst_y shift compensates for
 * the source skip).
 *
 * Same error gates — using GFX_SPRITE_RENDER_ERR_TRUNCATE_TOP_*. */
void gfx_sprite_render_truncate_top(int sprite_index,
                                    int x, int y,
                                    int skip_rows);

#endif /* OPENLORDS2_GFX_SPRITE_RENDER_H */
