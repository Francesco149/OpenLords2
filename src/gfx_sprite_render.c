/*
 * OpenLords2 — higher-level sprite renderers (implementation).
 *
 * See gfx_sprite_render.h for the per-function map and the
 * sibling-of-dispatcher relationship to gfx_dispatch.c.  Each
 * wrapper translates a (sprite_index, x, y) call into the same
 * staging-then-clip-then-dispatch sequence the dispatcher uses,
 * with extra bookkeeping retail does at the wrapper level rather
 * than the dispatcher level:
 *
 *   - DAT_005c9288 ("last sprite index") is set to `sprite_index`
 *     BEFORE staging the descriptor — the dispatcher itself does
 *     not write this global.
 *   - The descriptor's src_offset is gated against
 *     [1, 0x4baf1) — out-of-range descriptors latch error code 4
 *     and fire the log hook.  The dispatcher omits this check.
 *   - The clip rect is always (0, canvas_w, 0, canvas_h) — there
 *     is no "alt clip" / "caller rect" gate at this level.  Callers
 *     wanting a sub-rect use gfx_dispatch_sprite_clipped directly.
 *
 * Two variants (truncate_bottom / truncate_top) tweak the staged
 * state after the descriptor read but before the clip setters:
 *
 *   - truncate_bottom: shrink height_short by N → blitter renders
 *     fewer rows; the missing rows are the bottom N of the natural
 *     extent.
 *   - truncate_top: shrink height_short AND advance src_offset by
 *     N*sprite_w AND shift dst_y down by N → blitter renders the
 *     remaining (h - N) rows landed at (x, y+N).  Visible pixels are
 *     in the same place; the source rows are different.
 */

#include "gfx_sprite_render.h"
#include "gfx_globals.h"

#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_sprite.h"
#include "gfx_paint.h"

/* ──────────────────────────────────────────────────────────────────
 * Module state
 * ────────────────────────────────────────────────────────────────── */

static gfx_sprite_render_log_fn  g_log_fn;         /* hook → 0x4afab9 */

/* The sentinel range for descriptor.src_offset — retail checks
 *   (src_offset < 0x4baf1)  i.e. src_offset <= 0x4baf0
 *   (src_offset >= 1)
 * Both conditions must hold for the descriptor to be accepted. */
#define GFX_SPRITE_RENDER_MAX_SRC_OFFSET 0x4baf0

/* Mirror of retail FUN_00408fb0 (the 27-byte error-code latch).
 * Trivially inlinable; we keep it as a function so the call site
 * structure of each wrapper still reads like the retail decomp. */
static void latch_error(uint8_t code)
{
    g_gfx_last_error = code;
}

static void log_error(uint32_t fmt_addr, int sprite_index_plus_one)
{
    if (g_log_fn != 0) {
        g_log_fn(fmt_addr, sprite_index_plus_one);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Module API
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_set_log(gfx_sprite_render_log_fn fn) { g_log_fn = fn; }
void gfx_sprite_render_log_error(uint32_t fmt_addr, int n) { log_error(fmt_addr, n); }
uint8_t gfx_sprite_render_last_error(void) { return g_gfx_last_error; }
void gfx_sprite_render_clear_error(void)   { g_gfx_last_error = 0; }
void gfx_sprite_render_set_blob(const uint8_t *blob) { g_gfx_current_blob = blob; }
const uint8_t *gfx_sprite_render_blob(void) { return g_gfx_current_blob; }

/* ──────────────────────────────────────────────────────────────────
 * Shared post-descriptor pipeline
 *
 * After a descriptor is staged (or pseudo-staged in the raw-buffer
 * case), the wrappers all run the same sequence:
 *   - cursor_x/y := dst_x/dst_y
 *   - clip_x(0, canvas_w)
 *   - clip_y(0, canvas_h)
 *   - apply skip + dispatch
 *
 * Note the dead pre-write of dst_stride_pad in retail (see header
 * doc + quirk).  Not mirrored here — clip_x overwrites it
 * unconditionally.
 * ────────────────────────────────────────────────────────────────── */

static void canvas_clip_and_dispatch(const uint8_t *blob)
{
    gfx_dispatch_clip_x(0, gfx_paint_canvas_width());
    gfx_dispatch_clip_y(0, gfx_paint_canvas_height());
    gfx_dispatch_apply_skip_and_dispatch_sprite(blob);
}

/* RLE counterpart — runs the same canvas-clip pair but routes through
 * the no-skip-bake dispatch helper.  The RLE blitters consume the
 * staged top_skip / left_skip themselves rather than reading from a
 * pre-baked src_offset. */
static void canvas_clip_and_dispatch_rle(const uint8_t *blob)
{
    gfx_dispatch_clip_x(0, gfx_paint_canvas_width());
    gfx_dispatch_clip_y(0, gfx_paint_canvas_height());
    gfx_dispatch_dispatch_sprite_rle(blob);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a01d — raw-buffer render
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_raw(const uint8_t *buffer,
                           int x, int y,
                           int16_t w, int16_t h)
{
    if (buffer == 0) return;          /* retail `if (param_1 != 0)` gate */

    gfx_dispatch_set_last_glyph_index(0);

    /* Stage width/height/src_offset directly — no descriptor read.
     * The set_glyph 3-tuple (offset, w, row_count) writes the row-
     * count placeholder via the same setter; retail leaves
     * DAT_005bb478 unchanged (it gets overwritten by clip_y).  Use 0
     * for the placeholder; clip_y will overwrite. */
    gfx_glyph_set_glyph(/*src_offset=*/0, /*w=*/w, /*h=*/0);
    gfx_dispatch_set_glyph_height_short(h);

    gfx_glyph_set_dest(x, y);

    canvas_clip_and_dispatch(buffer);
}

/* ──────────────────────────────────────────────────────────────────
 * Descriptor-validity gate, shared by the four blob-based wrappers.
 *
 * Returns 1 if the staged descriptor is OK (caller proceeds with the
 * clip + dispatch), 0 if rejected (caller returns immediately with
 * the error latch + log hook already fired).
 *
 * `fmt_addr_low` and `fmt_addr_high` are the per-wrapper error
 * message addresses to pass to the log hook for each gate.  The
 * sprite_index_plus_one is `sprite_index + 1` — mirrors what retail
 * sends as 0x4afab9's third arg.
 * ────────────────────────────────────────────────────────────────── */

static int descriptor_offset_in_range(int sprite_index_plus_one,
                                      uint32_t fmt_addr_low,
                                      uint32_t fmt_addr_high)
{
    int src_off = gfx_glyph_glyph_offset();
    if (src_off > GFX_SPRITE_RENDER_MAX_SRC_OFFSET) {
        log_error(fmt_addr_high, sprite_index_plus_one);
        latch_error(4);
        return 0;
    }
    if (src_off < 1) {
        log_error(fmt_addr_low, sprite_index_plus_one);
        latch_error(4);
        return 0;
    }
    return 1;
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a21a — caller-blob render
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_from_blob(const uint8_t *blob,
                                 int sprite_index,
                                 int x, int y)
{
    if (blob == 0) {
        log_error(GFX_SPRITE_RENDER_ERR_FROM_BLOB_NULL, sprite_index + 1);
        latch_error(4);
        return;
    }

    gfx_dispatch_set_last_glyph_index(sprite_index);
    gfx_dispatch_stage_sprite_descriptor(blob, sprite_index);

    if (!descriptor_offset_in_range(sprite_index + 1,
                                    GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_LOW,
                                    GFX_SPRITE_RENDER_ERR_FROM_BLOB_INDEX_TOO_HIGH)) {
        return;
    }

    gfx_glyph_set_dest(x, y);
    canvas_clip_and_dispatch(blob);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a3d0 — caller-blob RLE render
 *
 * Sibling of FUN_0040a21a — same staging + validation shape, but does
 * NOT pre-bake top_skip/left_skip into src_offset.  The RLE blitters
 * handle both intrinsically.
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_rle_from_blob(const uint8_t *blob,
                                     int sprite_index,
                                     int x, int y)
{
    if (blob == 0) {
        log_error(GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_NULL, sprite_index + 1);
        latch_error(4);
        return;
    }

    gfx_dispatch_set_last_glyph_index(sprite_index);
    gfx_dispatch_stage_sprite_descriptor(blob, sprite_index);

    if (!descriptor_offset_in_range(sprite_index + 1,
                                    GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_INDEX_TOO_LOW,
                                    GFX_SPRITE_RENDER_ERR_RLE_FROM_BLOB_INDEX_TOO_HIGH)) {
        return;
    }

    gfx_glyph_set_dest(x, y);
    canvas_clip_and_dispatch_rle(blob);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a567 — caller-blob OPAQUE render
 *
 * Sibling of FUN_0040a21a (gfx_sprite_render_from_blob) but routes to
 * the opaque, unclipped blit (gfx_sprite_blit_opaque / FUN_004b432e)
 * for solid sprite frames such as the title-screen name-entry box.
 * Two differences from the transparent wrapper:
 *
 *   - No canvas-clip pair (FUN_0040464c/FUN_0040477b).  Retail writes
 *     the dest stride pad directly: `_DAT_005cd400 = 0x280 - width`
 *     (640 minus the sprite width; 0x280 is a hardcoded literal, NOT
 *     the dynamic canvas pitch — same fixed-640 quirk as the raw
 *     blits, engine-quirks #75).
 *   - Dispatches straight to the opaque blit; no x_clip_mode branch.
 *
 * Same three descriptor gates (NULL blob, src_offset < 1, src_offset
 * > 0x4baf0) with the opaque wrapper's own error-format addresses.
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_opaque_from_blob(const uint8_t *blob,
                                        int sprite_index,
                                        int x, int y)
{
    if (blob == 0) {
        log_error(GFX_SPRITE_RENDER_ERR_OPAQUE_FROM_BLOB_NULL, sprite_index + 1);
        latch_error(4);
        return;
    }

    gfx_dispatch_set_last_glyph_index(sprite_index);
    gfx_dispatch_stage_sprite_descriptor(blob, sprite_index);

    if (!descriptor_offset_in_range(sprite_index + 1,
                                    GFX_SPRITE_RENDER_ERR_OPAQUE_FROM_BLOB_INDEX_TOO_LOW,
                                    GFX_SPRITE_RENDER_ERR_OPAQUE_FROM_BLOB_INDEX_TOO_HIGH)) {
        return;
    }

    gfx_glyph_set_dest(x, y);
    gfx_glyph_set_dst_stride_pad(0x280 - gfx_glyph_glyph_w());
    gfx_sprite_blit_opaque(blob);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040d1bc — continue/dismiss seal draw
 * ────────────────────────────────────────────────────────────────── */

/* Seal-position globals (gfx_globals.h).  Retail parks the most recent seal draw's
 * coordinates here; the event-scroll renderer copies them into its
 * "committed" pair at the end of every popup frame and the dismiss
 * hit-test reads them to bound the click region. */

int gfx_sprite_seal_x(void)    { return g_gfx_seal_x; }
int gfx_sprite_seal_y(void)    { return g_gfx_seal_y; }
int gfx_sprite_seal_mode(void) { return g_gfx_seal_mode; }

void gfx_sprite_seal_draw(const uint8_t *system_sheet,
                          int x, int y, int mode)
{
    g_gfx_seal_x    = x;
    g_gfx_seal_y    = y;
    g_gfx_seal_mode = mode;

    /* Sprite select: mode 0 = the wax-seal "continue" stamp (0x33);
     * any other mode = the small variant (0x10). */
    int idx = (mode == 0) ? 0x33 : 0x10;
    gfx_dispatch_set_last_glyph_index(idx);          /* DAT_005c9288 */

    gfx_glyph_set_dest(x, y);                        /* DAT_00591524/8 */

    /* Drop-in guard: retail's system sheet is always resident.  When
     * the blob isn't loaded, keep the position/index writes (state-
     * faithful) and skip the descriptor read + blit. */
    if (system_sheet != 0) {
        gfx_dispatch_stage_sprite_descriptor(system_sheet, idx);
        gfx_sprite_blit_24x24_raw(system_sheet);     /* FUN_004b4cb8 */
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a682 — global-blob render
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render(int sprite_index, int x, int y)
{
    gfx_dispatch_set_last_glyph_index(sprite_index);
    gfx_dispatch_stage_sprite_descriptor(g_gfx_current_blob, sprite_index);

    if (!descriptor_offset_in_range(sprite_index + 1,
                                    GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_LOW,
                                    GFX_SPRITE_RENDER_ERR_GLOBAL_INDEX_TOO_HIGH)) {
        return;
    }

    gfx_glyph_set_dest(x, y);
    canvas_clip_and_dispatch(g_gfx_current_blob);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a80e — render with bottom-row trim
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_truncate_bottom(int sprite_index,
                                       int x, int y,
                                       int16_t trim_rows)
{
    gfx_dispatch_set_last_glyph_index(sprite_index);
    gfx_dispatch_stage_sprite_descriptor(g_gfx_current_blob, sprite_index);

    if (!descriptor_offset_in_range(sprite_index + 1,
                                    GFX_SPRITE_RENDER_ERR_TRUNCATE_BOTTOM_INDEX_TOO_LOW,
                                    GFX_SPRITE_RENDER_ERR_TRUNCATE_BOTTOM_INDEX_TOO_HIGH)) {
        return;
    }

    /* Shrink the staged height_short by `trim_rows`.  Mirrors retail:
     *   DAT_005aeb70 = DAT_005aeb70 - param_4;
     * Subtraction is unconditional — negative trim_rows is a latent
     * footgun (see header doc + quirk). */
    int h_short = gfx_dispatch_glyph_height_short() - (int)trim_rows;
    gfx_dispatch_set_glyph_height_short(h_short);

    gfx_glyph_set_dest(x, y);
    canvas_clip_and_dispatch(g_gfx_current_blob);
}

/* ──────────────────────────────────────────────────────────────────
 * Port of FUN_0040a9b0 — render with top-row skip
 * ────────────────────────────────────────────────────────────────── */

void gfx_sprite_render_truncate_top(int sprite_index,
                                    int x, int y,
                                    int skip_rows)
{
    gfx_dispatch_set_last_glyph_index(sprite_index);
    gfx_dispatch_stage_sprite_descriptor(g_gfx_current_blob, sprite_index);

    if (!descriptor_offset_in_range(sprite_index + 1,
                                    GFX_SPRITE_RENDER_ERR_TRUNCATE_TOP_INDEX_TOO_LOW,
                                    GFX_SPRITE_RENDER_ERR_TRUNCATE_TOP_INDEX_TOO_HIGH)) {
        return;
    }

    /* Three tweaks before the clip:
     *   - dst_y += skip_rows  (shift dest down so visible rows land
     *                          at the absolute y the caller wanted)
     *   - height_short -= skip_rows  (shrink visible extent)
     *   - src_offset += sprite_w * skip_rows  (advance past the
     *                                          skipped top rows)
     */
    int sprite_w = gfx_glyph_glyph_w();
    int src_off  = gfx_glyph_glyph_offset();

    int h_short = gfx_dispatch_glyph_height_short() - skip_rows;
    gfx_dispatch_set_glyph_height_short(h_short);

    int new_src_off = src_off + sprite_w * skip_rows;
    int row_count   = gfx_glyph_glyph_h();
    gfx_glyph_set_glyph(new_src_off, sprite_w, row_count);

    gfx_glyph_set_dest(x, y + skip_rows);
    canvas_clip_and_dispatch(g_gfx_current_blob);
}
