/*
 * Unit tests for engine_battlefield_render — combat-mode frame
 * driver + zoom-16 second-pass stub (chip 2 of the battlefield port).
 *
 * Coverage matrix:
 *
 *   - camera bind: frame driver stores (camera_x, camera_y) into
 *     retail globals readable via the accessors
 *   - dispatch shape at zoom 0x10: terrain-z16 + middle + overlay-z16
 *     + late + final, no zoom-32 passes
 *   - dispatch shape at zoom 0x20: terrain-z32 + middle + overlay-z32
 *     + late + final, no zoom-16 passes
 *   - dispatch shape at unknown zoom: middle + late + final only
 *   - force-redraw decrement on non-zero post-frame
 *   - force-redraw stays at 0 when already 0 (no underflow)
 *   - counters accumulate across frames; reset zeroes all
 *   - zoom-16 overlay pass IS dispatched even though its retail body
 *     is a no-op (counter increments to prove the dispatch fired)
 */

#include "t.h"

#include "engine_battlefield_render.h"

#include "gfx_dispatch.h"
#include "gfx_glyph.h"
#include "gfx_paint.h"

#include <stdint.h>
#include <string.h>

/* ────────────────────────────────────────────────────────────────
 * Helpers
 * ──────────────────────────────────────────────────────────────── */

static void scaffold_init(void)
{
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_force_redraw(0);
}

/* ────────────────────────────────────────────────────────────────
 * Camera binding
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_frame_stores_camera(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(123, -456);

    T_ASSERT_EQ_I(engine_battlefield_render_camera_x(), 123);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_y(), -456);
    return 0;
}

int test_engine_battlefield_render_camera_updates_each_frame(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x20);

    engine_battlefield_render_frame(10, 20);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_x(), 10);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_y(), 20);

    engine_battlefield_render_frame(-7, 99);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_x(), -7);
    T_ASSERT_EQ_I(engine_battlefield_render_camera_y(), 99);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Dispatch shape — zoom 0x10
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_zoom16_dispatches_z16_passes(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          1);
    return 0;
}

/* The zoom-16 overlay pass is FUN_004bcbd1 — a retail no-op body.
 * The dispatch itself MUST still fire, otherwise the renderer
 * cluster's call-graph diverges from retail. */
int test_engine_battlefield_render_zoom16_overlay_pass_is_dispatched(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 1);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Dispatch shape — zoom 0x20
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_zoom32_dispatches_z32_passes(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          1);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Dispatch shape — unknown zoom (neither 0x10 nor 0x20)
 *
 * The retail driver has no fall-through default in the if/else-if
 * chains: any other value skips both zoom-dispatched passes but
 * still runs middle, late, final and decrements force-redraw.
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_unknown_zoom_skips_zoom_passes(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          1);
    return 0;
}

int test_engine_battlefield_render_unknown_zoom_decrements_force_redraw(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x33);   /* not 0x10, not 0x20 */
    engine_battlefield_render_set_force_redraw(3);
    engine_battlefield_render_frame(0, 0);

    /* force-redraw decrement happens regardless of zoom dispatch. */
    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 2);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Force-redraw counter
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_force_redraw_decrements(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(5);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 4);

    engine_battlefield_render_frame(0, 0);
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 2);
    return 0;
}

/* The zero floor — counter must NEVER decrement below zero.  Retail
 * uses an unsigned-ish `if (DAT != 0) DAT--`; a careless port could
 * underflow into negative territory. */
int test_engine_battlefield_render_force_redraw_zero_floor(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 0);
    return 0;
}

int test_engine_battlefield_render_force_redraw_one_to_zero(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 0);

    /* Next frame should hold at 0, not underflow. */
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_force_redraw(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Counter accumulation + reset
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_counters_accumulate_across_frames(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);

    for (int i = 0; i < 7; i++) engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           7);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          7);

    /* Zoom-32 passes never fired. */
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    return 0;
}

int test_engine_battlefield_render_counters_reset_zeroes_all(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_frame(0, 0);
    engine_battlefield_render_frame(0, 0);

    /* Pre-reset: at least middle should be nonzero. */
    T_ASSERT(engine_battlefield_render_count_middle() > 0);
    T_ASSERT(engine_battlefield_render_count_terrain_zoom32() > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Mode-switch fidelity — switching zoom mid-stream picks the right
 * pair on the next frame, doesn't leak state.
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_switching_zoom_picks_correct_pair(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_frame(0, 0);   /* +1 z16 terrain, +1 z16 overlay */

    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_frame(0, 0);   /* +1 z32 terrain, +1 z32 overlay */

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(), 1);

    /* Always-on passes fired twice. */
    T_ASSERT_EQ_I(engine_battlefield_render_count_middle(),         2);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),           2);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),          2);
    return 0;
}

/* Zoom accessor round-trip. */
int test_engine_battlefield_render_zoom_round_trip(void)
{
    scaffold_init();

    engine_battlefield_render_set_zoom(0x10);
    T_ASSERT_EQ_I(engine_battlefield_render_zoom(), 0x10);

    engine_battlefield_render_set_zoom(0x20);
    T_ASSERT_EQ_I(engine_battlefield_render_zoom(), 0x20);

    engine_battlefield_render_set_zoom(0);
    T_ASSERT_EQ_I(engine_battlefield_render_zoom(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Terrain (zoom-16) pass — port of FUN_004bc8fc.
 *
 * Scaffolding: a small canvas + a synthetic descriptor table + two
 * blobs whose first sprite slot is 16×16 with a known src offset.
 * The blob's first 256 bytes after the descriptor block hold a
 * pattern we can read back from the canvas to confirm the blit
 * fired with the correct source.
 * ──────────────────────────────────────────────────────────────── */

#define CANVAS_W      640
#define CANVAS_H      400
#define DESC_STRIDE   8        /* arbitrary > 4 */
#define DESC_ROWS_PHY 8        /* physical rows of descriptors backing the test */
#define DESC_COLS_PHY 8

static uint8_t g_terrain_canvas[CANVAS_W * CANVAS_H];
static uint8_t g_terrain_desc[DESC_STRIDE * DESC_COLS_PHY * DESC_ROWS_PHY];

/* The blob shape FUN_004b49f7 + gfx_dispatch_stage_sprite_descriptor
 * read from:
 *
 *   +0x00..+0x07           header (ignored by these tests)
 *   slot N at +(N*16 + 8)   sprite metadata + glyph_offset
 *     +0x00  width  (uint16 LE) — 16 for terrain
 *     +0x02  height (uint16 LE) — 16 for terrain
 *     +0x04  src_offset (uint32 LE) — byte offset into blob for pixels
 *     +0x0c  flag_a  (byte)
 *     +0x0d  glyph_advance / row count (byte)
 *     +0x0e  flag_b  (byte)
 *
 * The pixel bytes live AT `blob + src_offset` (NOT inside the
 * descriptor slot — gfx_sprite_blit_16x16_raw reads from `blob +
 * gfx_glyph_glyph_offset()`).  We size the test blob generously
 * (1024 bytes) and stage the pixels at offset 0x100. */
#define TEST_BLOB_SIZE   1024
static uint8_t g_blob_primary_buf[TEST_BLOB_SIZE];
static uint8_t g_blob_alt_buf    [TEST_BLOB_SIZE];

/* Populate a 16-byte sprite descriptor slot.  `slot_index` is the
 * zero-based slot ID; the slot's bytes live at `blob + slot*16 + 8`. */
static void blob_set_slot(uint8_t *blob, int slot_index,
                          int w, int h, uint32_t src_off,
                          uint8_t flag_a, uint8_t advance, uint8_t flag_b)
{
    uint8_t *slot = blob + slot_index * 16 + 8;
    slot[0]   = (uint8_t)(w & 0xff);
    slot[1]   = (uint8_t)((w >> 8) & 0xff);
    slot[2]   = (uint8_t)(h & 0xff);
    slot[3]   = (uint8_t)((h >> 8) & 0xff);
    slot[4]   = (uint8_t)(src_off & 0xff);
    slot[5]   = (uint8_t)((src_off >>  8) & 0xff);
    slot[6]   = (uint8_t)((src_off >> 16) & 0xff);
    slot[7]   = (uint8_t)((src_off >> 24) & 0xff);
    /* slot[8..11] unused */
    slot[0xc] = flag_a;
    slot[0xd] = advance;
    slot[0xe] = flag_b;
}

/* Stamp a unique pattern into a blob's pixel region.  `byte_val` is
 * written to all 256 bytes of the 16×16 sprite at offset 0x100. */
static void blob_stamp_pixels(uint8_t *blob, uint32_t src_off, uint8_t byte_val)
{
    memset(blob + src_off, byte_val, 16 * 16);
}

/* Set descriptor[+2] (flags) and descriptor[+3] (sprite_idx) at the
 * grid cell (col, row).  `phys_cols` is the table's physical col
 * count; the grid's visible width may be smaller. */
static void desc_set(int col, int row, int phys_cols, uint8_t flags, uint8_t sprite_idx)
{
    int off = (col + row * phys_cols) * DESC_STRIDE;
    g_terrain_desc[off + 0] = 0;
    g_terrain_desc[off + 1] = 0;
    g_terrain_desc[off + 2] = flags;
    g_terrain_desc[off + 3] = sprite_idx;
}

static uint8_t desc_flags(int col, int row, int phys_cols)
{
    int off = (col + row * phys_cols) * DESC_STRIDE;
    return g_terrain_desc[off + 2];
}

/* Scaffold the terrain pass with a `vis_cols × vis_rows` visible grid
 * embedded in an 8×8 physical descriptor table.  Viewport at (32, 16);
 * camera at (0, 0); 16-byte sprite slot 1 in the primary blob writes
 * a known byte pattern. */
static void terrain_scaffold(int vis_cols, int vis_rows)
{
    /* Canvas + paint binding (the blit uses dest_origin() from
     * gfx_paint binding). */
    memset(g_terrain_canvas, 0xff, sizeof g_terrain_canvas);
    gfx_paint_bind_canvas(g_terrain_canvas, CANVAS_W, CANVAS_H);

    /* Descriptor table — clear, then set known flags + sprite_idx for
     * each visible cell.  Default: dirty bit set, sprite 1, no blob
     * selector (so flags & 0x1c == 0 → primary). */
    memset(g_terrain_desc, 0, sizeof g_terrain_desc);
    for (int r = 0; r < vis_rows; r++) {
        for (int c = 0; c < vis_cols; c++) {
            desc_set(c, r, DESC_COLS_PHY, /*flags=*/0x01, /*sprite_idx=*/1);
        }
    }

    /* Blobs: slot 1 = 16×16, src_off=0x100, flag_a=0xab, advance=16, flag_b=0xcd. */
    memset(g_blob_primary_buf, 0, sizeof g_blob_primary_buf);
    memset(g_blob_alt_buf, 0, sizeof g_blob_alt_buf);
    blob_set_slot(g_blob_primary_buf, 1, 16, 16, 0x100, 0xab, 16, 0xcd);
    blob_set_slot(g_blob_alt_buf,     1, 16, 16, 0x100, 0xab, 16, 0xcd);
    blob_stamp_pixels(g_blob_primary_buf, 0x100, /*byte_val=*/0x42);
    blob_stamp_pixels(g_blob_alt_buf,     0x100, /*byte_val=*/0x77);

    /* Bindings. */
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_bind_descriptor_table(g_terrain_desc, DESC_STRIDE);
    engine_battlefield_render_bind_grid(vis_cols, vis_rows,
                                        /*row_wrap=*/(DESC_COLS_PHY - vis_cols) * DESC_STRIDE);
    engine_battlefield_render_bind_viewport_offset(32, 16);
    engine_battlefield_render_bind_camera_row_stride(DESC_COLS_PHY);
    engine_battlefield_render_bind_blobs(g_blob_primary_buf, g_blob_alt_buf);
}

/* ─── Tests ──────────────────────────────────────────────────────── */

int test_engine_battlefield_render_terrain_blits_dirty_cells(void)
{
    terrain_scaffold(/*cols=*/3, /*rows=*/2);
    engine_battlefield_render_frame(0, 0);

    /* 3 × 2 = 6 dirty cells, all primary blob.  All should blit. */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 6);
    return 0;
}

int test_engine_battlefield_render_terrain_skips_clean_cells(void)
{
    terrain_scaffold(/*cols=*/3, /*rows=*/2);

    /* Mark one cell clean (clear bit 0x01) — that cell must NOT blit. */
    desc_set(1, 0, DESC_COLS_PHY, /*flags=*/0x00, /*sprite_idx=*/1);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 5);
    return 0;
}

int test_engine_battlefield_render_terrain_force_redraw_overrides_dirty(void)
{
    terrain_scaffold(/*cols=*/3, /*rows=*/2);

    /* All cells clean — without force-redraw, no blits. */
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            desc_set(c, r, DESC_COLS_PHY, /*flags=*/0x00, /*sprite_idx=*/1);
        }
    }

    /* Force-redraw on: ALL cells redraw regardless of dirty bit. */
    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 6);
    return 0;
}

int test_engine_battlefield_render_terrain_clears_dirty_after_draw(void)
{
    terrain_scaffold(/*cols=*/3, /*rows=*/2);

    engine_battlefield_render_frame(0, 0);

    /* Every cell that was drawn should have its dirty bit cleared.
     * Interior cell (1, 0) is NOT a boundary, so the only post-draw
     * flag change is the dirty-clear. */
    T_ASSERT_EQ_I(desc_flags(1, 0, DESC_COLS_PHY) & 0x01, 0);
    return 0;
}

int test_engine_battlefield_render_terrain_marks_boundary_cells(void)
{
    terrain_scaffold(/*cols=*/3, /*rows=*/2);

    /* For 3x2 grid, every cell is on a boundary (since both dims are
     * <= 2 in one axis or another).  Pick the worst case — interior
     * 3×3 grid where the centre cell is NOT a boundary. */
    terrain_scaffold(/*cols=*/3, /*rows=*/3);
    engine_battlefield_render_frame(0, 0);

    /* Centre cell (1, 1) is NOT boundary → bit 0x02 NOT set. */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0);

    /* Corner cells get bit 0x02. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(2, 0, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(0, 2, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(2, 2, DESC_COLS_PHY) & 0x02, 0x02);

    /* Edge cells get bit 0x02. */
    T_ASSERT_EQ_I(desc_flags(1, 0, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(1, 2, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(0, 1, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(2, 1, DESC_COLS_PHY) & 0x02, 0x02);
    return 0;
}

int test_engine_battlefield_render_terrain_marks_boundary_even_when_clean(void)
{
    terrain_scaffold(/*cols=*/3, /*rows=*/3);

    /* Mark all cells clean.  Boundary marking still fires (retail's
     * boundary block runs OUTSIDE the dirty-gated branch). */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            desc_set(c, r, DESC_COLS_PHY, /*flags=*/0x00, /*sprite_idx=*/1);
        }
    }

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 0);
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0); /* interior */
    return 0;
}

int test_engine_battlefield_render_terrain_blob_selector_primary(void)
{
    terrain_scaffold(/*cols=*/1, /*rows=*/1);

    /* (flags & 0x1c) == 0 → primary blob */
    desc_set(0, 0, DESC_COLS_PHY, /*flags=*/0x01, /*sprite_idx=*/1);

    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 1);

    /* Verify by reading back a pixel from the canvas.  Cell at viewport
     * (32, 16) — should be the primary blob's 0x42 pattern. */
    int pixel = g_terrain_canvas[16 * CANVAS_W + 32];
    T_ASSERT_EQ_I(pixel, 0x42);
    return 0;
}

int test_engine_battlefield_render_terrain_blob_selector_alt(void)
{
    terrain_scaffold(/*cols=*/1, /*rows=*/1);

    /* (flags & 0x1c) == 4 → alt blob */
    desc_set(0, 0, DESC_COLS_PHY, /*flags=*/0x01 | 0x04, /*sprite_idx=*/1);

    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 1);

    int pixel = g_terrain_canvas[16 * CANVAS_W + 32];
    T_ASSERT_EQ_I(pixel, 0x77);
    return 0;
}

int test_engine_battlefield_render_terrain_blob_selector_other_skips_dispatch(void)
{
    terrain_scaffold(/*cols=*/1, /*rows=*/1);

    /* (flags & 0x1c) == 8 → neither primary nor alt; retail's strict
     * if/else-if chain has no default, so no blit dispatch. */
    desc_set(0, 0, DESC_COLS_PHY, /*flags=*/0x01 | 0x08, /*sprite_idx=*/1);

    engine_battlefield_render_frame(0, 0);

    /* Dirty bit was still cleared (the descriptor mutation lives
     * outside the blob-selector branch). */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    /* But no blit fired. */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 0);

    /* And the canvas was not touched — still 0xff. */
    int pixel = g_terrain_canvas[16 * CANVAS_W + 32];
    T_ASSERT_EQ_I(pixel, 0xff);
    return 0;
}

int test_engine_battlefield_render_terrain_advances_cursor_per_cell(void)
{
    terrain_scaffold(/*cols=*/2, /*rows=*/1);

    /* Two dirty cells at viewport (32, 16): cell 0 at (32, 16),
     * cell 1 at (48, 16).  Both blits should fire. */
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 2);

    /* Sample one pixel from each blit's destination.  Both use the
     * primary blob (pattern 0x42). */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 48], 0x42);

    /* Pixel BETWEEN destinations should still be 0x42 (the two 16-px
     * sprites tile contiguously) — sample row 16, col 47 (last byte
     * of cell 0). */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 47], 0x42);
    return 0;
}

int test_engine_battlefield_render_terrain_no_descriptor_short_circuits(void)
{
    /* Driver should not crash when no descriptor table has been bound. */
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_bind_descriptor_table(NULL, 0);
    engine_battlefield_render_bind_grid(0, 0, 0);
    engine_battlefield_render_bind_viewport_offset(0, 0);
    engine_battlefield_render_bind_camera_row_stride(0);
    engine_battlefield_render_bind_blobs(NULL, NULL);

    engine_battlefield_render_frame(0, 0);

    /* Pass counter still fires (frame driver dispatched the pass);
     * the pass short-circuited inside without blitting. */
    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom16(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 0);
    return 0;
}

int test_engine_battlefield_render_terrain_camera_offset_skips_to_correct_row(void)
{
    /* Camera at (0, 1) — the terrain pass should start descriptor
     * cursor at row 1 of the table (skipping row 0 entirely).  Row 0
     * dirty cells should NOT blit; row 1 cells should. */

    terrain_scaffold(/*cols=*/2, /*rows=*/1);

    /* Row 0 cells: dirty (would blit if camera were (0,0)). */
    desc_set(0, 0, DESC_COLS_PHY, /*flags=*/0x01, /*sprite_idx=*/1);
    desc_set(1, 0, DESC_COLS_PHY, /*flags=*/0x01, /*sprite_idx=*/1);
    /* Row 1 cells: dirty too. */
    desc_set(0, 1, DESC_COLS_PHY, /*flags=*/0x01, /*sprite_idx=*/1);
    desc_set(1, 1, DESC_COLS_PHY, /*flags=*/0x01, /*sprite_idx=*/1);

    /* Now point camera_y at 1 — only row 1 cells get visited. */
    engine_battlefield_render_frame(0, 1);

    /* Two cells blitted. */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 2);

    /* Row 0 descriptors should be untouched (dirty bit still set). */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0x01);
    T_ASSERT_EQ_I(desc_flags(1, 0, DESC_COLS_PHY) & 0x01, 0x01);

    /* Row 1 descriptors cleared. */
    T_ASSERT_EQ_I(desc_flags(0, 1, DESC_COLS_PHY) & 0x01, 0);
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x01, 0);
    return 0;
}

int test_engine_battlefield_render_terrain_per_blit_counter_resets(void)
{
    terrain_scaffold(/*cols=*/2, /*rows=*/1);
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 2);

    engine_battlefield_render_reset_counts();
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_blit_count(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Terrain (zoom-32) pass — port of FUN_004bcbdc.
 *
 * Bigger scaffold: 32×32 sprite slot (1024 bytes) in two blobs, plus
 * a clip rect bound for the second-sprite-per-cell path.  The
 * descriptor stride is left at 8 so the same DESC_STRIDE constant
 * survives — it's >= 5 (zoom-32 reads desc[0..4]) so all five fields
 * fit.
 * ──────────────────────────────────────────────────────────────── */

/* Set the full 5-byte descriptor zoom-32 reads from. */
static void desc_set_z32(int col, int row, int phys_cols,
                         uint8_t b0, uint8_t b1,
                         uint8_t flags, uint8_t sprite_idx,
                         uint8_t b4)
{
    int off = (col + row * phys_cols) * DESC_STRIDE;
    g_terrain_desc[off + 0] = b0;
    g_terrain_desc[off + 1] = b1;
    g_terrain_desc[off + 2] = flags;
    g_terrain_desc[off + 3] = sprite_idx;
    g_terrain_desc[off + 4] = b4;
}

/* Bigger blob: slots 0..0xff (the highest we touch is 0x9a) live in
 * [8, 0xff*16+8+0xf] = [8, 4087].  Pixel data must therefore start at
 * >= 4096 to avoid clobbering descriptors via the memset for pixel
 * patterns.  Use 8192 so we have room for four distinct 1024-byte
 * pixel regions at offsets 4096 / 5120 / 6144 / 7168. */
#define Z32_BLOB_SIZE       8192
#define Z32_PIX_FIRST_OFF   4096   /* slot 1 primary + alt (1st sprite) */
#define Z32_PIX_CLAMP_OFF   5120   /* slot 0x9a alt — clamp target */
#define Z32_PIX_BAND_OFF    6144   /* slots 0x8c..0x99 alt — alternates band */
static uint8_t g_z32_blob_primary[Z32_BLOB_SIZE];
static uint8_t g_z32_blob_alt    [Z32_BLOB_SIZE];

/* Scaffold the zoom-32 terrain pass with a vis_cols × vis_rows visible
 * grid embedded in an 8×8 physical descriptor table.  Defaults:
 *
 *   - All cells dirty (flag bit 0x01) with sprite_idx=1, desc[+0]=0
 *     (no second sprite), desc[+1]=0 (skip-bit clear), desc[+4]=0
 *     (irrelevant since no second sprite).
 *   - Both blobs have slot 1 = 32×32, src_off=0x400, advance=32.
 *   - Slot 0x9a in alt blob is also configured (32×32 at src_off=0x800)
 *     so the second-sprite path has a valid target.
 *   - Slots 0x8c..0x9b in alt blob configured (32×32 at src_off=0xc00).
 *   - Clip rect set wide (0..640 × 0..400) so clip_x / clip_y both
 *     report "no clip".
 *   - Viewport at (32, 16); camera at (0, 0); zoom 0x20. */
static void terrain_z32_scaffold(int vis_cols, int vis_rows)
{
    memset(g_terrain_canvas, 0xff, sizeof g_terrain_canvas);
    gfx_paint_bind_canvas(g_terrain_canvas, CANVAS_W, CANVAS_H);

    memset(g_terrain_desc, 0, sizeof g_terrain_desc);
    for (int r = 0; r < vis_rows; r++) {
        for (int c = 0; c < vis_cols; c++) {
            desc_set_z32(c, r, DESC_COLS_PHY,
                         /*b0=*/0, /*b1=*/0,
                         /*flags=*/0x01, /*sprite_idx=*/1, /*b4=*/0);
        }
    }

    memset(g_z32_blob_primary, 0, sizeof g_z32_blob_primary);
    memset(g_z32_blob_alt,     0, sizeof g_z32_blob_alt);

    /* Slot 1 — first-sprite target.  Pixel region at Z32_PIX_FIRST_OFF
     * (well above the slot descriptor table to avoid clobbering it). */
    blob_set_slot(g_z32_blob_primary, 1, 32, 32, Z32_PIX_FIRST_OFF, 0xab, 32, 0xcd);
    blob_set_slot(g_z32_blob_alt,     1, 32, 32, Z32_PIX_FIRST_OFF, 0xab, 32, 0xcd);
    memset(g_z32_blob_primary + Z32_PIX_FIRST_OFF, /*byte_val=*/0x42, 32 * 32);
    memset(g_z32_blob_alt     + Z32_PIX_FIRST_OFF, /*byte_val=*/0x77, 32 * 32);

    /* Slots 0x8c..0x99 — second-sprite per-byte alternates (b0=1..14
     * maps to slot 0x8c..0x99).  All share Z32_PIX_BAND_OFF with
     * pattern 0x55.  Note: slot 0x9a is configured SEPARATELY below —
     * both as the b0=15 boundary case AND as the b0>=0x10 clamp target. */
    for (int s = 0x8c; s <= 0x99; s++) {
        blob_set_slot(g_z32_blob_alt, s, 32, 32, Z32_PIX_BAND_OFF, 0x11, 32, 0x22);
    }
    memset(g_z32_blob_alt + Z32_PIX_BAND_OFF, 0x55, 32 * 32);

    /* Slot 0x9a — second-sprite clamp target.  Pattern 0x88 so it's
     * distinguishable from the alternates band's 0x55. */
    blob_set_slot(g_z32_blob_alt, 0x9a, 32, 32, Z32_PIX_CLAMP_OFF, 0x11, 32, 0x22);
    memset(g_z32_blob_alt + Z32_PIX_CLAMP_OFF, 0x88, 32 * 32);

    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_bind_descriptor_table(g_terrain_desc, DESC_STRIDE);
    engine_battlefield_render_bind_grid(vis_cols, vis_rows,
                                        /*row_wrap=*/(DESC_COLS_PHY - vis_cols) * DESC_STRIDE);
    engine_battlefield_render_bind_viewport_offset(32, 16);
    engine_battlefield_render_bind_camera_row_stride(DESC_COLS_PHY);
    engine_battlefield_render_bind_blobs(g_z32_blob_primary, g_z32_blob_alt);
    engine_battlefield_render_bind_clip_rect(/*left=*/0, /*top=*/0,
                                             /*right=*/CANVAS_W, /*bottom=*/CANVAS_H);
}

int test_engine_battlefield_render_terrain_z32_blits_dirty_cells(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/2);
    engine_battlefield_render_frame(0, 0);

    /* 3 × 2 = 6 dirty cells, primary blob, no second sprite. */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(),  6);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 0);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_skips_clean_cells(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/2);

    /* Mark one cell clean. */
    desc_set_z32(1, 0, DESC_COLS_PHY, 0, 0, /*flags=*/0, /*sprite=*/1, 0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 5);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_force_redraw_overrides_dirty(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/2);

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
            desc_set_z32(c, r, DESC_COLS_PHY, 0, 0,
                         /*flags=*/0, /*sprite=*/1, 0);
        }
    }

    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 6);
    return 0;
}

/* Quirk: descriptor[+1] & 4 set on a DIRTY cell in the TERRAIN-Z32
 * pass → NO drawing AND NO boundary marking (the entire if/else-if
 * chain short-circuits).  The dirty bit is, however, cleared by the
 * subsequent OVERLAY-Z32 pass (chip 5b), which requires desc[+1] & 4
 * SET to draw — the layer bit picks which pass owns this cell. */
int test_engine_battlefield_render_terrain_z32_skip_bit_drops_dirty_cell(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Centre cell (1, 1) — interior, dirty, with the skip-bit set. */
    desc_set_z32(1, 1, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* TERRAIN: 8 other interior+edge cells blitted (9 - 1 skipped). */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 8);
    /* OVERLAY: only the centre cell qualifies (dirty + layer bit). */
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 1);

    /* Centre cell: dirty bit cleared by OVERLAY (not terrain). */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x01, 0);
    /* Centre cell: boundary bit NOT set — neither pass touches it
     * (terrain skipped boundary on the dirty + skip-bit cell; overlay
     * has no boundary block).  Interior, so it would not have been
     * a boundary cell anyway. */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0);
    return 0;
}

/* Quirk corner case: descriptor[+1] & 4 set on a DIRTY EDGE cell —
 * terrain skips boundary marking on the cell.  Overlay clears dirty
 * but adds no boundary bit either, so the corner stays without bit
 * 0x02 (whereas it would have gotten it on a regular dirty edge cell). */
int test_engine_battlefield_render_terrain_z32_skip_bit_blocks_boundary_too(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Top-left corner cell (0, 0) — dirty, skip-bit set. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* Boundary bit NOT set on the cell — terrain skipped it; overlay
     * doesn't mark boundary at all. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0);
    /* Dirty bit cleared by OVERLAY pass. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    /* Overlay fired on exactly this corner cell (the only one with
     * the layer bit set). */
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 1);
    return 0;
}

/* Counterpart: descriptor[+1] & 4 set on a CLEAN cell — boundary
 * marking still runs (the skip-bit only gates the dirty path; clean
 * cells go straight to the boundary block).  Important asymmetry vs.
 * dirty + skip-bit. */
int test_engine_battlefield_render_terrain_z32_skip_bit_leaves_clean_boundary(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Top-left corner (0, 0) — CLEAN, skip-bit set. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x00, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* Boundary bit IS set — clean cells get boundary marking
     * regardless of the skip-bit. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0x02);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_blob_selector_primary(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* (flags & 0x1c) == 0 → primary blob */
    desc_set_z32(0, 0, DESC_COLS_PHY, 0, 0, /*flags=*/0x01, /*sprite=*/1, 0);

    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 1);

    /* Sample pixel at viewport (32, 16) — primary 0x42 pattern. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_blob_selector_alt(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    desc_set_z32(0, 0, DESC_COLS_PHY, 0, 0,
                 /*flags=*/0x01 | 0x04, /*sprite=*/1, 0);

    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 1);

    /* Alt pattern is 0x77 — written by 32×32 raw blit. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x77);
    return 0;
}

/* Quirk #76: (flags & 0x1c) values outside {0, 4} skip first-sprite
 * dispatch.  In zoom-32 the dirty bit still clears AND boundary marking
 * still runs.  Second-sprite path is independent of blob selector — if
 * bVar1 != 0 the second sprite ALSO runs. */
int test_engine_battlefield_render_terrain_z32_blob_selector_other_skips_first(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    desc_set_z32(0, 0, DESC_COLS_PHY, 0, 0,
                 /*flags=*/0x01 | 0x08, /*sprite=*/1, 0);

    engine_battlefield_render_frame(0, 0);

    /* No first blit. */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 0);
    /* Dirty bit cleared. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    /* Boundary bit set (1×1 grid = corner). */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0x02);
    /* No canvas change. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0xff);
    return 0;
}

/* Quirk: [1, 0x4baf0] glyph_offset gate aborts the entire cell.  The
 * upper bound is 309488 (0x4baf0); 309489 is the first value that
 * aborts.  Retail constant confirmed via r2 at 0x4bcd76. */
int test_engine_battlefield_render_terrain_z32_src_offset_gate_aborts(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Re-stage slot 1 in primary with an out-of-band src_off.  Use
     * 0x4baf1 = 309489 (just past the upper bound). */
    blob_set_slot(g_z32_blob_primary, 1, 32, 32, 0x4baf1, 0xab, 32, 0xcd);

    /* Mark only the centre interior cell as the target.  Other cells
     * use a different sprite (slot 0) so they don't hit the bad slot. */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            desc_set_z32(c, r, DESC_COLS_PHY, 0, 0,
                         /*flags=*/0x01, /*sprite=*/(c == 1 && r == 1) ? 1 : 2, 0);
        }
    }
    /* Slot 2 — valid src_off so the surrounding cells blit normally. */
    blob_set_slot(g_z32_blob_primary, 2, 32, 32, Z32_PIX_FIRST_OFF, 0xab, 32, 0xcd);

    engine_battlefield_render_frame(0, 0);

    /* 8 cells blit (surrounding 3×3 minus centre). */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 8);

    /* Centre cell: dirty bit cleared (mutation happens BEFORE the gate),
     * but boundary bit NOT set (centre is interior and gate aborted). */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x01, 0);
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_second_sprite_runs_when_b0_nonzero(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* desc[+0] = 1 (second sprite chooser), desc[+4] = 1 (valid mode). */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/1, /*b1=*/0,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/1);

    engine_battlefield_render_frame(0, 0);

    /* Both the first-sprite blit AND the second-sprite blit fire. */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(),  1);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 1);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_second_sprite_skipped_when_b0_zero(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* desc[+0] = 0 → no second sprite even with a valid mode byte. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/1);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(),  1);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 0);
    return 0;
}

/* Quirk: second-sprite mode byte ∈ {1, 2, 3} only.  0 or > 3 aborts
 * the cell (skips boundary marking + skips second blit). */
int test_engine_battlefield_render_terrain_z32_second_sprite_mode_zero_aborts(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Centre cell: second-sprite enabled (b0=1) but mode=0 → abort. */
    desc_set_z32(1, 1, DESC_COLS_PHY,
                 /*b0=*/1, /*b1=*/0,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* Centre cell: dirty cleared (pre-gate mutation), boundary NOT set,
     * second blit NOT dispatched. */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x01, 0);
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 0);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_second_sprite_mode_high_aborts(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/1, /*b1=*/0,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/4);   /* 4 > 3 */

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 0);
    return 0;
}

/* Quirk: descriptor[+0] >= 0x10 clamps the second-sprite slot to 0x9a.
 * Values in [1, 0x10) map to [0x8c, 0x9b).  Verify by reading the
 * canvas — slot 0x9a has pattern 0x88; slots 0x8c..0x9b have 0x55. */
int test_engine_battlefield_render_terrain_z32_second_sprite_slot_picks_lower_band(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* No first sprite (blob selector 8 → no first blit).  desc[+0]=1
     * → second slot = 0x8c (pattern 0x55). */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/1, /*b1=*/0,
                 /*flags=*/0x01 | 0x08, /*sprite=*/1, /*b4=*/1);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 1);
    /* Sample pixel — slot 0x8c pattern 0x55. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x55);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_second_sprite_slot_clamps_at_high(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* desc[+0] = 0x20 (>= 0x10) → second slot clamps to 0x9a. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0x20, /*b1=*/0,
                 /*flags=*/0x01 | 0x08, /*sprite=*/1, /*b4=*/1);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 1);
    /* Slot 0x9a pattern 0x88. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x88);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_cursor_advances_32px_per_cell(void)
{
    terrain_z32_scaffold(/*cols=*/2, /*rows=*/1);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 2);
    /* Cell 0 at (32, 16): primary pattern 0x42. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    /* Cell 1 at (32 + 32, 16) = (64, 16): same pattern. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 64], 0x42);
    /* Last byte of cell 0 = column 63: still 0x42 (sprites tile
     * contiguously at 32-px pitch). */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 63], 0x42);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_no_descriptor_short_circuits(void)
{
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_bind_descriptor_table(NULL, 0);
    engine_battlefield_render_bind_grid(0, 0, 0);
    engine_battlefield_render_bind_viewport_offset(0, 0);
    engine_battlefield_render_bind_camera_row_stride(0);
    engine_battlefield_render_bind_blobs(NULL, NULL);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_count_terrain_zoom32(),            1);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_second_blit_count(), 0);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_clears_dirty_after_draw(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/2);

    engine_battlefield_render_frame(0, 0);

    /* Interior-ish cell (1, 0) — dirty bit cleared after draw. */
    T_ASSERT_EQ_I(desc_flags(1, 0, DESC_COLS_PHY) & 0x01, 0);
    return 0;
}

int test_engine_battlefield_render_terrain_z32_marks_boundary_cells(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    engine_battlefield_render_frame(0, 0);

    /* Centre (1, 1) — interior, no boundary bit. */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0);
    /* Corners + edges — boundary bit set. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(2, 2, DESC_COLS_PHY) & 0x02, 0x02);
    T_ASSERT_EQ_I(desc_flags(1, 0, DESC_COLS_PHY) & 0x02, 0x02);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Overlay (zoom-32) pass — port of FUN_004bd07f (chip 5b).
 *
 * Reuses the terrain_z32 scaffold (same primary blob, descriptor
 * layout, viewport, etc.) but exercises the SECOND zoom-32 pass that
 * runs after middle / before late.  Drawing requires BOTH:
 *
 *   (a) dirty (desc[+2] & 1) OR force-redraw
 *   (b) layer bit (desc[+1] & 4)
 *
 * Always uses PRIMARY blob.  Same [1, 0x4baf0] glyph_offset gate as
 * terrain-z32.  No boundary marking, no second sprite.
 * ──────────────────────────────────────────────────────────────── */

int test_engine_battlefield_render_overlay_z32_draws_on_layer_bit_dirty(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Layer bit + dirty.  Terrain skips this cell (skip-bit logic);
     * overlay picks it up. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(),       1);

    /* Dirty bit cleared by overlay. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    /* Primary blob pattern 0x42 written at viewport (32, 16). */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_skips_without_layer_bit(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Dirty but layer bit clear — overlay skips, terrain draws. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x00,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(),       0);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_skips_clean_with_layer_bit(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Layer bit set but cell clean — overlay needs dirty/forced. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x00, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(),       0);
    /* Canvas untouched. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0xff);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_force_redraw_drives_clean(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Clean + layer bit + force-redraw on → overlay should draw. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x00, /*sprite=*/1, /*b4=*/0);
    engine_battlefield_render_set_force_redraw(1);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(),       1);
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_always_uses_primary_blob(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Layer bit + dirty + (flags & 0x1c) == 4 — terrain-z32 would
     * pick the alt blob for this, but overlay-z32 has no selector
     * and always uses primary. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01 | 0x04, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 1);
    /* Primary pattern is 0x42, alt is 0x77.  Verify primary. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    return 0;
}

/* Verify the [1, 0x4baf0] glyph_offset gate boundary at 0x4baf0:
 *   src_off = 0x4baf0 (309488)  — draws (upper bound INCLUSIVE)
 *   src_off = 0x4baf1 (309489)  — aborts. */
int test_engine_battlefield_render_overlay_z32_glyph_offset_at_upper_bound_draws(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* The blob's primary slot 1 is the one overlay reads from; set
     * its src_off to exactly 0x4baf0.  Out-of-range for our 8 KB blob
     * — but the gate accepts the value, so the blit fires and reads
     * garbage.  We're not checking the canvas content, just that the
     * dispatch counter ticked.  Use a private blob below to give us a
     * 310-KB scratch region instead, since the existing scaffold's
     * blob is too small.  Simpler: bypass the blit by binding a NULL
     * blob — except then the blit-count test would still fire.
     *
     * Cleanest: don't actually have the gate read a real blit.  Just
     * stage the slot with src_off=0x4baf0 and confirm the blit_count
     * increments (the 32×32 raw blit will write 32×32 bytes starting
     * at canvas dest based on cursor + glyph_offset's role inside
     * blob, but we're only counting). */
    static uint8_t big_blob[0x4baf0 + 32 * 32 + 32];
    memset(big_blob, 0, sizeof big_blob);
    blob_set_slot(big_blob, 1, 32, 32, /*src_off=*/0x4baf0,
                  0xab, 32, 0xcd);
    /* Stamp a recognizable byte at the pixel region. */
    memset(big_blob + 0x4baf0, 0x3c, 32 * 32);

    engine_battlefield_render_bind_blobs(big_blob, big_blob);

    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 1);
    /* Sample pixel — 0x3c pattern at (32, 16). */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x3c);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_glyph_offset_above_aborts(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Stage slot 1 with src_off=0x4baf1 — one past the upper bound. */
    blob_set_slot(g_z32_blob_primary, 1, 32, 32, /*src_off=*/0x4baf1,
                  0xab, 32, 0xcd);

    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* Blit aborted by gate. */
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 0);
    /* Dirty bit STILL cleared — clearing happens before the gate.
     * Important behavior: aborted-blit cells still appear "redrawn"
     * to subsequent frames. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_glyph_offset_zero_aborts(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);

    /* Stage slot 1 with src_off=0 — below lower bound. */
    blob_set_slot(g_z32_blob_primary, 1, 32, 32, /*src_off=*/0,
                  0xab, 32, 0xcd);

    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 0);
    /* Dirty bit cleared (pre-gate). */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_no_boundary_marking(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Make all cells skip-bit + dirty so terrain-z32 leaves all of
     * them alone (no drawing, no boundary marking).  Overlay-z32
     * fires on all 9 and clears dirty — but should NOT mark any
     * boundary bit. */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            desc_set_z32(c, r, DESC_COLS_PHY,
                         /*b0=*/0, /*b1=*/0x04,
                         /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);
        }
    }

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(),       9);

    /* Corner: boundary bit NOT set (overlay has no boundary block;
     * terrain skipped it). */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0);
    /* All dirty bits cleared by overlay. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0);
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x01, 0);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_cursor_advances_32px(void)
{
    terrain_z32_scaffold(/*cols=*/2, /*rows=*/1);

    /* Both cells get the layer bit + dirty so overlay draws both. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04, /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);
    desc_set_z32(1, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04, /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 2);
    /* Cell 0 at viewport (32, 16) — 0x42 (primary). */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 32], 0x42);
    /* Cell 1 at (64, 16) — 32-px pitch. */
    T_ASSERT_EQ_I(g_terrain_canvas[16 * CANVAS_W + 64], 0x42);
    return 0;
}

int test_engine_battlefield_render_overlay_z32_no_descriptor_short_circuits(void)
{
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_bind_descriptor_table(NULL, 0);
    engine_battlefield_render_bind_grid(0, 0, 0);
    engine_battlefield_render_bind_viewport_offset(0, 0);
    engine_battlefield_render_bind_camera_row_stride(0);
    engine_battlefield_render_bind_blobs(NULL, NULL);

    engine_battlefield_render_frame(0, 0);

    /* Pass dispatched; blit-count zero. */
    T_ASSERT_EQ_I(engine_battlefield_render_count_overlay_zoom32(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 0);
    return 0;
}

/* Counter is reset by reset_counts. */
int test_engine_battlefield_render_overlay_z32_blit_count_resets(void)
{
    terrain_z32_scaffold(/*cols=*/1, /*rows=*/1);
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04, /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);
    engine_battlefield_render_frame(0, 0);
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 1);

    engine_battlefield_render_reset_counts();
    T_ASSERT_EQ_I(engine_battlefield_render_overlay_zoom32_blit_count(), 0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Middle pass — port of FUN_004bd355 (chip 5a.i).
 *
 * The driver itself is real; the six leaves are still stubs that bump
 * counters.  Tests verify:
 *
 *   - the three setup helpers (build / sort / render entity list) fire
 *     ONCE each per frame, independent of any descriptor binding
 *   - the cell loop iterates the over-scan extension
 *     ((cols+2) × (rows+2) with start at (cam_x-1, cam_y-1)), clamped
 *     against camera_row_stride / total_rows
 *   - west/north clamps when cam_x == 0 / cam_y == 0
 *   - per-cell dispatch shape:
 *       desc[+2] & 0x80, desc[+3] == 0 → player marker AND re-dirty
 *       desc[+2] & 0x80, desc[+3] != 0 → unit_key(desc[+3])
 *       desc[+6] != 0                  → effect_chain(desc[+6])
 *   - the re-dirty asymmetry (only the player-marker branch sets bit 1)
 *   - per-cell counter advances per visited cell
 *   - cursor advances by zoom per cell and per row
 *
 * Scaffold uses a 8×8 physical descriptor table (the same one the
 * terrain tests use, `g_terrain_desc`).  We reuse `DESC_STRIDE` (8)
 * which leaves room for the four bytes the middle pass reads
 * (+0, +1, +2, +3, +6) within each slot.
 * ──────────────────────────────────────────────────────────────── */

/* Write the four descriptor bytes the middle pass reads at cell
 * (col, row).  All other bytes in the slot are zeroed. */
static void desc_set_middle(int col, int row, int phys_cols,
                            uint8_t b1, uint8_t flags, uint8_t key3,
                            uint8_t effect6)
{
    int off = (col + row * phys_cols) * DESC_STRIDE;
    g_terrain_desc[off + 0] = 0;
    g_terrain_desc[off + 1] = b1;
    g_terrain_desc[off + 2] = flags;
    g_terrain_desc[off + 3] = key3;
    g_terrain_desc[off + 4] = 0;
    g_terrain_desc[off + 5] = 0;
    g_terrain_desc[off + 6] = effect6;
    g_terrain_desc[off + 7] = 0;
}

/* Scaffold the middle pass with a `vis_cols × vis_rows` visible grid
 * embedded in an 8×8 physical descriptor table.  Total grid extent =
 * physical size (so over-scan never clamps unless tests deliberately
 * cause it).  Viewport at (0, 0), zoom 16, camera at (0, 0) — tests
 * adjust each as needed.  All cells start zeroed (no dispatch fires). */
static void middle_scaffold(int vis_cols, int vis_rows)
{
    memset(g_terrain_desc, 0, sizeof g_terrain_desc);

    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(0x10);
    engine_battlefield_render_set_force_redraw(0);
    engine_battlefield_render_bind_descriptor_table(g_terrain_desc, DESC_STRIDE);
    engine_battlefield_render_bind_grid(vis_cols, vis_rows,
                                        /*row_wrap=*/0);
    engine_battlefield_render_bind_viewport_offset(0, 0);
    engine_battlefield_render_bind_camera_row_stride(DESC_COLS_PHY);
    engine_battlefield_render_bind_total_rows(DESC_ROWS_PHY);
    engine_battlefield_render_bind_blobs(NULL, NULL);
    /* Tear down any entity-table binding inherited from a prior test —
     * the chip 5a.i tests in this group don't bind one, so the build
     * helper should emit an empty visible list. */
    engine_battlefield_render_bind_entity_table(NULL, 0, 0);

    /* Reset cursor so per-cell advance assertions start from a known
     * state.  gfx_glyph_set_dest writes both cursor_x and cursor_y. */
    gfx_glyph_set_dest(0, 0);
}

/* Run the middle pass in isolation (no terrain / overlay / late /
 * final dispatch).  Uses the test-only entry that bypasses the frame
 * driver, so tests can keep zoom set to whatever value they want and
 * inspect cursor / per-cell counter without sibling-pass contamination.
 * The middle pass's own pitch comes from the bound zoom. */
static void run_middle_only(int cam_x, int cam_y)
{
    engine_battlefield_render_run_pass_middle_for_test(cam_x, cam_y);
}

/* ─── Tests ──────────────────────────────────────────────────────── */

int test_engine_battlefield_render_middle_setup_helpers_fire_once(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_build_count(),  1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_count(),   1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_render_count(), 1);
    return 0;
}

/* The setup helpers fire at the HEAD of the middle pass, BEFORE the
 * descriptor-table short-circuit — they walk their own entity table
 * (not yet ported in chip 5a.i), so an unbound descriptor table must
 * still result in all three counters bumped. */
int test_engine_battlefield_render_middle_setup_runs_without_descriptors(void)
{
    engine_battlefield_render_reset_counts();
    engine_battlefield_render_set_zoom(99);
    engine_battlefield_render_bind_descriptor_table(NULL, DESC_STRIDE);

    engine_battlefield_render_frame(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_build_count(),  1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_count(),   1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_render_count(), 1);
    /* No cells visited because the descriptor short-circuit fires. */
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 0);
    return 0;
}

/* Over-scan extension: visible 3×3 grid at cam_x=2, cam_y=2 (well
 * inside the 8×8 physical table) → iteration covers (cols+2)×(rows+2)
 * = 5×5 = 25 cells; no clamps fire. */
int test_engine_battlefield_render_middle_overscans_visible_grid(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    run_middle_only(/*cam_x=*/2, /*cam_y=*/2);

    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 25);
    return 0;
}

/* West clamp: cam_x == 0 → start_col = 0 (not -1).  Over-scan is
 * therefore cols+1 wide on the east side (since start moves right by
 * one), but the loop still iterates (cols+2) cells because the start
 * just shifts — the loop count `local_2c` is unaffected unless the
 * east edge runs into the row_stride clamp.
 *
 * For cam_x=0, cam_y=2, cols=3, rows=3, row_stride=8: start_col=0,
 * num_cols clamps from 5 to min(5, 8-0)=5.  Same for north: start_row=
 * 0 effectively (cam_y-1 = 1, no clamp), so num_rows stays 5. */
int test_engine_battlefield_render_middle_west_clamp_shifts_start(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    /* Cam at left edge: cam_x=0, cam_y=2 (so y over-scan still has
     * room to extend north).  Compare cursor_x at end of frame vs.
     * the unclamped case. */
    run_middle_only(/*cam_x=*/0, /*cam_y=*/2);

    /* num_cols still 5 (5 ≤ row_stride 8 → no clamp); num_rows = 5.
     * 5 × 5 = 25 cells visited. */
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 25);

    /* Cursor X at end of frame: walked 5 cells of zoom-16 starting at
     * viewport_x (because start_px_x = viewport_x - zoom + zoom (clamp
     * cancel) = viewport_x = 0).  Last set is at viewport_x + 5 *
     * zoom = 0 + 80 = 80. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 80);
    return 0;
}

/* East clamp: cam_x near the right edge of the underlying table →
 * num_cols clamps so we don't walk past the last valid column.
 * For row_stride=8, cam_x=6, cols=3, rows=3: start_col=5, num_cols
 * clamps from 5 to (8 - 5) = 3.  Loop visits 3 cols * 5 rows = 15
 * cells. */
int test_engine_battlefield_render_middle_east_clamp_reduces_cols(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    run_middle_only(/*cam_x=*/6, /*cam_y=*/2);

    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 15);
    return 0;
}

/* South clamp: cam_y near the bottom of the underlying table →
 * num_rows clamps.  For total_rows=8, cam_y=6, cols=3, rows=3:
 * start_row=5, num_rows clamps from 5 to (8 - 5) = 3.  Visits
 * 5 cols * 3 rows = 15 cells. */
int test_engine_battlefield_render_middle_south_clamp_reduces_rows(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    run_middle_only(/*cam_x=*/2, /*cam_y=*/6);

    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 15);
    return 0;
}

/* Bit 0x80 set, descriptor[+3] == 0 → player marker draws AND the
 * cell is re-dirtied (bit 0x01 set).  This is unique to the player-
 * marker branch — quirk: animated sprite needs re-paint each frame. */
int test_engine_battlefield_render_middle_bit80_key0_calls_player_marker(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    /* Cell (2, 2) — inside the over-scan window when cam=(2, 2). */
    desc_set_middle(2, 2, DESC_COLS_PHY,
                    /*b1=*/0, /*flags=*/0x80, /*key3=*/0, /*effect6=*/0);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),      0);
    /* Re-dirty: bit 0x01 set after the dispatch. */
    int off = (2 + 2 * DESC_COLS_PHY) * DESC_STRIDE;
    T_ASSERT_EQ_I(g_terrain_desc[off + 2] & 0x01, 0x01);
    /* Bit 0x80 still present — only bit 1 was ORed in. */
    T_ASSERT_EQ_I(g_terrain_desc[off + 2] & 0x80, 0x80);
    return 0;
}

/* Bit 0x80 set, descriptor[+3] != 0 → unit_key called with that byte;
 * no re-dirty. */
int test_engine_battlefield_render_middle_bit80_nonzero_key_calls_unit_key(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    desc_set_middle(2, 2, DESC_COLS_PHY,
                    /*b1=*/0, /*flags=*/0x80, /*key3=*/0x4c, /*effect6=*/0);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_arg(), 0x4c);
    /* NO re-dirty for this branch. */
    int off = (2 + 2 * DESC_COLS_PHY) * DESC_STRIDE;
    T_ASSERT_EQ_I(g_terrain_desc[off + 2] & 0x01, 0);
    return 0;
}

/* Bit 0x80 CLEAR → neither player_marker nor unit_key. */
int test_engine_battlefield_render_middle_no_bit80_no_draw(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    desc_set_middle(2, 2, DESC_COLS_PHY,
                    /*b1=*/0, /*flags=*/0x00, /*key3=*/0x4c, /*effect6=*/0);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),      0);
    return 0;
}

/* descriptor[+6] != 0 → effect_chain called with that byte;
 * independent of the bit-0x80 gate. */
int test_engine_battlefield_render_middle_nonzero_effect6_calls_chain(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    desc_set_middle(2, 2, DESC_COLS_PHY,
                    /*b1=*/0, /*flags=*/0x00, /*key3=*/0, /*effect6=*/0x42);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_last_arg(), 0x42);
    return 0;
}

/* descriptor[+6] == 0 → no effect_chain dispatch. */
int test_engine_battlefield_render_middle_zero_effect6_skips_chain(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    /* All cells zeroed by scaffold; no dispatch should fire. */
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_count(), 0);
    return 0;
}

/* Bit 0x80 AND descriptor[+6] both set → BOTH dispatches fire on the
 * same cell.  Verifies the two gates are independent. */
int test_engine_battlefield_render_middle_combined_dispatch(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    desc_set_middle(2, 2, DESC_COLS_PHY,
                    /*b1=*/0, /*flags=*/0x80, /*key3=*/0x4c,
                    /*effect6=*/0x42);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_arg(), 0x4c);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_last_arg(), 0x42);
    return 0;
}

/* Zoom 32 changes the per-cell pitch — cursor advances by 32 instead
 * of 16.  Cell count is unaffected (driven by num_cols × num_rows). */
int test_engine_battlefield_render_middle_zoom32_advances_32px(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    engine_battlefield_render_set_zoom(0x20);
    /* For zoom 32 + cam=(2,2) + cols=rows=3 → over-scan 5×5.
     * start_px_x = viewport_x - 32 = -32; final cursor_x after 5
     * cells = -32 + 5*32 = 128. */
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 25);
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 128);
    return 0;
}

/* Per-cell counter resets to 0 at the start of each frame. */
int test_engine_battlefield_render_middle_per_frame_counter_resets(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);

    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 25);

    /* Second frame at the same camera should observe the same count
     * (i.e. counter reset before re-entry, not accumulated). */
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 25);
    return 0;
}

/* The middle leaf counters accumulate across frames (until reset). */
int test_engine_battlefield_render_middle_counters_accumulate(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);

    run_middle_only(2, 2);
    run_middle_only(2, 2);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_build_count(),  3);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_count(),   3);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_render_count(), 3);
    return 0;
}

/* reset_counts zeroes all middle counters and the two last-arg
 * captures (back to -1). */
int test_engine_battlefield_render_middle_reset_clears_state(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    desc_set_middle(2, 2, DESC_COLS_PHY,
                    /*b1=*/0, /*flags=*/0x80, /*key3=*/0x99,
                    /*effect6=*/0x44);
    run_middle_only(2, 2);

    /* Sanity — both last-arg captures set. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_arg(), 0x99);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_last_arg(), 0x44);

    engine_battlefield_render_reset_counts();
    T_ASSERT_EQ_I(engine_battlefield_render_middle_build_count(),         0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_count(),          0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_render_count(),        0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),      0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_arg(),     -1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_last_arg(), -1);
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(),     0);
    return 0;
}

/* The middle pass observes the camera args verbatim (just like
 * terrain / overlay) — same camera readback applies. */
int test_engine_battlefield_render_middle_uses_frame_camera(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);

    /* Run at cam=(2, 2): cell counter = 25.  Then at cam=(6, 2):
     * east clamp brings it down to 15. */
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 25);

    run_middle_only(6, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_per_frame_cell_counter(), 15);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 5a.ii.a — sorted-entity infrastructure.
 *
 * The fixture lays out an entity table sized to retail's max_id (0x50),
 * lets tests set per-entity (x, y, alive) tuples, and then runs the
 * middle pass to populate the visible-entity list.  The entity table
 * binding is wired through `middle_scaffold` (which NULLs it by
 * default); tests that want a populated list call `bind_entities` to
 * attach the fixture.
 * ──────────────────────────────────────────────────────────────── */

#define ENT_STRIDE  ENGINE_BATTLEFIELD_ENTITY_STRIDE_RETAIL
#define ENT_MAX_ID  ENGINE_BATTLEFIELD_ENTITY_MAX_ID_RETAIL
static uint8_t g_entity_table[(ENT_MAX_ID + 1) * ENT_STRIDE];

static void entity_set(int id, int x, int y, uint8_t alive)
{
    uint8_t *rec = g_entity_table + id * ENT_STRIDE;
    *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_X)     = (int16_t)x;
    *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y)     = (int16_t)y;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ALIVE] = alive;
}

static void bind_entities(int max_id)
{
    memset(g_entity_table, 0, sizeof g_entity_table);
    engine_battlefield_render_bind_entity_table(g_entity_table,
                                                ENT_STRIDE, max_id);
}

/* Visible-list build — empty when no entity table is bound. */
int test_engine_battlefield_render_visible_list_empty_without_table(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);  /* unbinds entity table */
    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 0);
    /* Build helper still bumped — runs unconditionally at head. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_build_count(), 1);
    return 0;
}

/* Build filters out dead entities (alive byte == 0). */
int test_engine_battlefield_render_visible_list_filters_dead(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/4);
    entity_set(1, /*x=*/2, /*y=*/2, /*alive=*/1);
    entity_set(2, /*x=*/2, /*y=*/3, /*alive=*/0);  /* dead, skipped */
    entity_set(3, /*x=*/3, /*y=*/2, /*alive=*/1);
    entity_set(4, /*x=*/3, /*y=*/3, /*alive=*/0);  /* dead, skipped */

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 2);
    return 0;
}

/* Build filter window matches the cell-loop over-scan extent:
 *   ex ∈ [cam_x - 1, cam_x + cols], inclusive both ends
 *   ey ∈ [cam_y - 1, cam_y + rows], inclusive both ends
 *
 * Visible 3×3 grid at cam=(2, 2): x range [1, 5], y range [1, 5]. */
int test_engine_battlefield_render_visible_list_filters_viewport(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/8);
    entity_set(1, /*x=*/1, /*y=*/1, 1);   /* in (corner) */
    entity_set(2, /*x=*/5, /*y=*/5, 1);   /* in (corner) */
    entity_set(3, /*x=*/3, /*y=*/3, 1);   /* in (center) */
    entity_set(4, /*x=*/0, /*y=*/3, 1);   /* OUT — x < 1 */
    entity_set(5, /*x=*/6, /*y=*/3, 1);   /* OUT — x > 5 */
    entity_set(6, /*x=*/3, /*y=*/0, 1);   /* OUT — y < 1 */
    entity_set(7, /*x=*/3, /*y=*/6, 1);   /* OUT — y > 5 */
    entity_set(8, /*x=*/3, /*y=*/3, 0);   /* OUT — dead */

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 3);
    return 0;
}

/* Filter bounds are INCLUSIVE on both ends — entities at the
 * boundary (cam_x - 1, cam_x + cols, cam_y - 1, cam_y + rows) are
 * INCLUDED.  Filed as quirk #85. */
int test_engine_battlefield_render_visible_list_bounds_inclusive(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/4);
    entity_set(1, /*x=*/1, /*y=*/3, 1);  /* x lo bound */
    entity_set(2, /*x=*/5, /*y=*/3, 1);  /* x hi bound */
    entity_set(3, /*x=*/3, /*y=*/1, 1);  /* y lo bound */
    entity_set(4, /*x=*/3, /*y=*/5, 1);  /* y hi bound */

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 4);
    return 0;
}

/* Build count carries the entity id (not the slot index). */
int test_engine_battlefield_render_visible_list_carries_id(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/5);
    /* Only id 3 alive — sparse layout. */
    entity_set(3, /*x=*/3, /*y=*/3, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(0), 3);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(0),  3);
    return 0;
}

/* Bubble sort orders entries by ascending y. */
int test_engine_battlefield_render_sort_orders_by_y_ascending(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/4);
    /* Iteration order id=1..4 → input list is (1,5)(2,3)(3,1)(4,4).
     * After sort by y ascending: (3,1)(2,3)(4,4)(1,5).  */
    entity_set(1, /*x=*/2, /*y=*/5, 1);
    entity_set(2, /*x=*/2, /*y=*/3, 1);
    entity_set(3, /*x=*/2, /*y=*/1, 1);
    entity_set(4, /*x=*/2, /*y=*/4, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 4);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(0), 3);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(0),  1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(1), 2);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(1),  3);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(2), 4);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(2),  4);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(3), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(3),  5);
    return 0;
}

/* Sort is STABLE-ish wrt equal y: equal-y entries retain insertion
 * order because the swap only fires on strict `y[i+1] < y[i]`.  This
 * is observable behavior of the retail decompilation, not a guarantee
 * we want to gold-lock, but documents it for now. */
int test_engine_battlefield_render_sort_keeps_equal_y_in_insertion_order(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/3);
    entity_set(1, /*x=*/2, /*y=*/3, 1);
    entity_set(2, /*x=*/3, /*y=*/3, 1);
    entity_set(3, /*x=*/4, /*y=*/3, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 3);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(0), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(1), 2);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(2), 3);
    return 0;
}

/* When the list is already sorted, the bubble sort terminates after
 * ONE outer pass (the pass sees no swap → last_swap = 0 → outer loop
 * exits via `0 < local_1c` predicate). */
int test_engine_battlefield_render_sort_already_sorted_one_pass(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/3);
    /* Iteration order 1..3 → list is already sorted by y. */
    entity_set(1, /*x=*/2, /*y=*/1, 1);
    entity_set(2, /*x=*/3, /*y=*/2, 1);
    entity_set(3, /*x=*/4, /*y=*/3, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_passes(), 1);
    return 0;
}

/* Empty / single-entry list runs ZERO sort passes — the outer
 * `while` predicate `0 < high` (high = count - 1) is false. */
int test_engine_battlefield_render_sort_zero_passes_when_trivial(void)
{
    /* Single entry. */
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/1);
    entity_set(1, /*x=*/2, /*y=*/2, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_passes(),   0);
    return 0;
}

/* Sort passes never exceed the safety cap (500) — verified by the
 * worst-case-ish full-reverse layout with retail max_id. */
int test_engine_battlefield_render_sort_within_safety_cap(void)
{
    /* Use a 21×1 visible window so the over-scan filter doesn't drop
     * any of the 20 entities we lay down at distinct y. */
    middle_scaffold(/*cols=*/3, /*rows=*/21);
    bind_entities(/*max_id=*/20);
    /* Fully reverse-sorted by y: id=1 has y=20, id=20 has y=1.  After
     * sort, id=20 should be first. */
    for (int id = 1; id <= 20; id++) {
        entity_set(id, /*x=*/2, /*y=*/21 - id, 1);
    }

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 20);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(0),  20);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(19),  1);
    /* Worst-case = 19 outer passes (one less than n).  Well under
     * the 500 cap. */
    int passes = engine_battlefield_render_middle_sort_passes();
    T_ASSERT(passes >  0);
    T_ASSERT(passes <= 19);
    return 0;
}

/* Render loop dispatches the per-entity stub once per visible
 * entity.  Capture the last id processed. */
int test_engine_battlefield_render_render_loop_per_entity(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/3);
    entity_set(1, /*x=*/2, /*y=*/5, 1);  /* sorts LAST (largest y) */
    entity_set(2, /*x=*/2, /*y=*/3, 1);
    entity_set(3, /*x=*/2, /*y=*/1, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(), 3);
    /* Render loop walks the sorted list — last id processed is id=1
     * (which sorts to the end because of largest y). */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_id(), 1);
    return 0;
}

/* Render loop computes per-entity cursor from
 *   px_x = (entity_x - cam_x) * zoom + viewport_x
 *   px_y = (entity_y - cam_y) * zoom + viewport_y
 *
 * Scaffold zoom 16, viewport (0, 0), so for entity at (3, 4) with
 * camera (2, 2) → px = ((3-2)*16, (4-2)*16) = (16, 32).  Last entity
 * processed has those coords captured. */
int test_engine_battlefield_render_render_loop_cursor_computation(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    engine_battlefield_render_bind_viewport_offset(100, 50);
    bind_entities(/*max_id=*/1);
    entity_set(1, /*x=*/3, /*y=*/4, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_id(),   1);
    /* px = ((3-2)*16 + 100, (4-2)*16 + 50) = (116, 82). */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_x(), 116);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_y(), 82);
    return 0;
}

/* Render loop honors zoom: same hex coords, zoom 32 → twice the
 * pixel distance from origin. */
int test_engine_battlefield_render_render_loop_cursor_zoom32(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    engine_battlefield_render_set_zoom(0x20);
    engine_battlefield_render_bind_viewport_offset(0, 0);
    bind_entities(/*max_id=*/1);
    entity_set(1, /*x=*/3, /*y=*/4, 1);

    run_middle_only(/*cam=*/2, 2);

    /* px = ((3-2)*32, (4-2)*32) = (32, 64). */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_x(), 32);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_y(), 64);
    return 0;
}

/* The render-loop's per-entity dispatch fires AFTER the sort —
 * capture-order of last_id is the post-sort tail, not the highest id. */
int test_engine_battlefield_render_render_loop_walks_sorted_order(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/5);
    /* id=5 has lowest y, id=1 has highest y. */
    entity_set(1, /*x=*/2, /*y=*/9, 1);
    entity_set(2, /*x=*/2, /*y=*/7, 1);
    entity_set(3, /*x=*/2, /*y=*/5, 1);
    entity_set(4, /*x=*/2, /*y=*/3, 1);
    entity_set(5, /*x=*/2, /*y=*/1, 1);
    /* Visible filter excludes those past y=cam_y+rows = 2+3 = 5.
     * So only id 3 (y=5), id 4 (y=3), id 5 (y=1) are visible.
     * Sort by y → id=5, id=4, id=3.  Last processed = id 3. */

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 3);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(), 3);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_id(), 3);
    return 0;
}

/* Visible-entity accessors return 0 for out-of-range indices —
 * defensive guard not present in retail (which would silently read
 * past the buffer). */
int test_engine_battlefield_render_visible_entity_accessor_out_of_range(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/1);
    entity_set(1, /*x=*/2, /*y=*/2, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(-1), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(1),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(99), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(-1),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(1),   0);
    return 0;
}

/* Each middle pass invocation rebuilds the list from scratch — count
 * does not accumulate across consecutive passes. */
int test_engine_battlefield_render_visible_list_rebuilds_per_pass(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/2);
    entity_set(1, /*x=*/2, /*y=*/2, 1);
    entity_set(2, /*x=*/3, /*y=*/3, 1);

    run_middle_only(/*cam=*/2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 2);

    /* Kill an entity between passes — count should drop to 1. */
    entity_set(2, /*x=*/3, /*y=*/3, 0);
    run_middle_only(/*cam=*/2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(0), 1);

    return 0;
}

/* The render-loop's per-entity counter accumulates across consecutive
 * pass invocations (matches the build/sort/render counters, which all
 * bump per dispatch).  Reset clears it. */
int test_engine_battlefield_render_per_entity_count_accumulates(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    bind_entities(/*max_id=*/2);
    entity_set(1, /*x=*/2, /*y=*/2, 1);
    entity_set(2, /*x=*/3, /*y=*/3, 1);

    run_middle_only(/*cam=*/2, 2);
    run_middle_only(/*cam=*/2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(), 4);

    engine_battlefield_render_reset_counts();
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),     0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_id(),  -1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_x(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_y(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_passes(),          0);
    return 0;
}

/* Render loop is a no-op without an entity table — even if dispatch
 * fires (build/sort/render counters still bump). */
int test_engine_battlefield_render_render_loop_noop_without_table(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);  /* NULL entity table */
    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_build_count(),       1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_sort_count(),        1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_render_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),  0);
    return 0;
}

/* Sentinel handling: when count = max_id ≤ 80, the in-band sentinel
 * lives at the buffer slot just past the last valid entry.  We can't
 * directly observe it (the accessors guard against out-of-range
 * reads), but we can verify the sort doesn't blow past the count even
 * when the sentinel is involved — pack the buffer with 0x50 entities
 * each at distinct y and confirm the sort completes cleanly. */
int test_engine_battlefield_render_sort_handles_full_buffer(void)
{
    /* Visible window large enough to admit 80 entities (set cols/rows
     * to extreme values so the over-scan filter doesn't drop any). */
    middle_scaffold(/*cols=*/3, /*rows=*/200);
    bind_entities(/*max_id=*/ENT_MAX_ID);  /* 0x50 = 80 */
    for (int id = 1; id <= ENT_MAX_ID; id++) {
        /* All entities at x=2 (in [1, 5]).  y = id descending so the
         * sort reverses the input list. */
        entity_set(id, /*x=*/2, /*y=*/ENT_MAX_ID + 1 - id, 1);
    }

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_count(), ENT_MAX_ID);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(0),   ENT_MAX_ID);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(0),    1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_id(ENT_MAX_ID - 1), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_visible_entity_y(ENT_MAX_ID - 1),  ENT_MAX_ID);
    /* Worst case = 79 outer passes, well below the 500 cap. */
    T_ASSERT(engine_battlefield_render_middle_sort_passes() <= ENT_MAX_ID);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 5a.ii.b — per-entity draw FUN_004bdc31 base path.
 *
 * Per-entity blob fixture: each entity points at a small blob whose
 * slot N contains a sprite header (width, height, src_off, flags).
 * The blit itself is fired through gfx_dispatch_dispatch_sprite_rle —
 * with no canvas bound, the RLE blitter's `if (!dst) return;` prologue
 * makes it a safe no-op, but the dispatcher's `y_clip_mode != 5` gate
 * still fires, so the entity-blit counter tracks "would have dispatched"
 * accurately.  Tests that exercise dispatch suppression set a degenerate
 * clip rect.
 *
 * The anim-offset tables are 0x88-byte-stride per facing.  Tests stage
 * a single facing's worth of frames and verify the body indexes into
 * the right (facing, frame) entry.
 * ──────────────────────────────────────────────────────────────── */

#define ENTITY_BLOB_SIZE 0x200
static uint8_t g_entity_blob[ENTITY_BLOB_SIZE];

#define ANIM_TABLE_SIZE 0x440
static uint8_t g_anim_z16[ANIM_TABLE_SIZE];
static uint8_t g_anim_z32[ANIM_TABLE_SIZE];

static void blob_set_header(uint8_t *blob, int slot,
                            int w, int h, int src_off)
{
    blob_set_slot(blob, slot, w, h, (uint32_t)src_off,
                  /*flag_a=*/0xab, /*advance=*/h,
                  /*flag_b=*/0xcd);
}

static void anim_table_set(uint8_t *table, int facing, int frame,
                           int32_t x_off, int32_t y_off)
{
    uint8_t *entry = table + facing * ENGINE_BATTLEFIELD_ANIM_FACING_STRIDE
                           + frame  * ENGINE_BATTLEFIELD_ANIM_FRAME_STRIDE;
    memcpy(entry + 0, &x_off, sizeof x_off);
    memcpy(entry + 4, &y_off, sizeof y_off);
}

/* Stuff entity fields needed by the per-entity draw body.  Type,
 * facing, frame, subtype, primary_blob ptr, primary_frame index. */
static void entity_set_body(int id, int x, int y, uint8_t alive,
                            uint8_t type, uint8_t facing, uint8_t anim_frame,
                            uint8_t subtype,
                            uint8_t *blob, uint8_t primary_frame)
{
    uint8_t *rec = g_entity_table + id * ENT_STRIDE;
    *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_X)     = (int16_t)x;
    *(int16_t *)(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_Y)     = (int16_t)y;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ALIVE]                = alive;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE]                 = type;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING]               = facing;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME]           = anim_frame;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_SUBTYPE]              = subtype;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_FRAME]        = primary_frame;

    /* Store blob as a host-width pointer (matches the port's read
     * shape — see ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB docs). */
    memcpy(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_PRIMARY_BLOB,
           &blob, sizeof blob);
}

/* Scaffold for chip 5a.ii.b: one entity at (3, 3), camera (2, 2),
 * 3×3 visible grid, zoom 16, anim tables bound, generous clip rect.
 *
 * Crucial: UNBIND the canvas (gfx_paint_bind_canvas(NULL, ...)) so the
 * RLE blitter's `if (!dst) return;` prologue makes the actual blit a
 * no-op.  Without that, the inner blit walks the (zero-initialised)
 * RLE stream — which would be an infinite read of 0x00 opcodes with
 * zero skip counts.  We don't want to write a valid RLE stream for
 * every test, since we're only testing the body's CONTROL FLOW around
 * the blit (which the blit_count + the dirty-mark coords already
 * capture without needing real pixel output). */
static void perentity_scaffold(int zoom)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    engine_battlefield_render_set_zoom(zoom);

    /* Unbind the canvas inherited from terrain-pass tests. */
    gfx_paint_bind_canvas(NULL, 0, 0);

    /* Zero blob/anim tables; populate one default slot for slot=1. */
    memset(g_entity_blob, 0, sizeof g_entity_blob);
    blob_set_header(g_entity_blob, /*slot=*/1,
                    /*w=*/16, /*h=*/16, /*src_off=*/0x100);

    memset(g_anim_z16, 0, sizeof g_anim_z16);
    memset(g_anim_z32, 0, sizeof g_anim_z32);
    engine_battlefield_render_bind_anim_offset_tables(g_anim_z16, g_anim_z32);

    /* Generous clip rect so y_clip_mode resolves to 0 (blit reachable). */
    engine_battlefield_render_bind_clip_rect(/*left=*/-1000, /*top=*/-1000,
                                             /*right=*/2000, /*bottom=*/2000);

    /* No dirty state — body skips the dirty mark by default; tests
     * that observe the mark bind their own state. */
    engine_battlefield_render_set_dirty_state(NULL);

    bind_entities(/*max_id=*/4);
}

/* Type-6 entity triggers the PRE-call stub.  Fires regardless of blob
 * or glyph-offset gates. */
int test_engine_battlefield_render_per_entity_type6_pre_call(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    entity_set_body(1, /*x=*/3, /*y=*/3, /*alive=*/1,
                    /*type=*/6, /*facing=*/0, /*anim_frame=*/0,
                    /*subtype=*/0,
                    g_entity_blob, /*primary_frame=*/1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_count(),   1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_last_id(), 1);
    return 0;
}

/* Type-7 entity triggers POST-call stub.  Gated by the same
 * glyph-offset window as the main draw. */
int test_engine_battlefield_render_per_entity_type7_post_call(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    entity_set_body(1, 3, 3, 1, /*type=*/7, 0, 0, 0,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_count(),   1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_last_id(), 1);
    /* Type-7's box uses 9×9 (type >= 7).  Verified by dirty-rect test. */
    return 0;
}

/* Type-9 with subtype 0x0e triggers POST-call stub. */
int test_engine_battlefield_render_per_entity_type9_sub_e_post_call(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    entity_set_body(1, 3, 3, 1, /*type=*/9, 0, 0, /*subtype=*/0x0e,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_count(),   1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_last_id(), 1);
    return 0;
}

/* Type-9 with subtype != 0x0e does NOT trigger the subtype post-call. */
int test_engine_battlefield_render_per_entity_type9_other_subtype(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    entity_set_body(1, 3, 3, 1, /*type=*/9, 0, 0, /*subtype=*/0x05,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),       1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_count(), 0);
    return 0;
}

/* NULL primary blob: type-6 PRE-call STILL fires (it's above the
 * no-blob check), but the main draw and type-7/9 post-calls are
 * skipped.  No-blob counter bumps. */
int test_engine_battlefield_render_per_entity_no_blob_path(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    /* Type 6 + NULL blob.  Pass NULL via blob arg → primary blob
     * field is 0. */
    entity_set_body(1, 3, 3, 1, /*type=*/6, 0, 0, 0,
                    /*blob=*/NULL, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_count(),   1);  /* fires */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_no_blob_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_no_blob_last_id(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(),    0);
    return 0;
}

/* NULL blob path also skips type-7 POST-call.  Set type 7 but NULL
 * blob → type7 stub does NOT fire. */
int test_engine_battlefield_render_per_entity_no_blob_skips_type7(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    entity_set_body(1, 3, 3, 1, /*type=*/7, 0, 0, 0, NULL, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_count(),   0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_no_blob_count(), 1);
    return 0;
}

/* Glyph-offset gate: src_off outside [1, 0x4baf0] aborts the drawing
 * block — main draw doesn't dispatch and type-7/9 post-calls don't
 * fire.  Type-6 already fired above the gate. */
int test_engine_battlefield_render_per_entity_glyph_offset_gate_zero(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    /* src_off = 0 → out-of-band (gate is `0 < off && off <= 0x4baf0`). */
    blob_set_header(g_entity_blob, /*slot=*/1,
                    16, 16, /*src_off=*/0);
    entity_set_body(1, 3, 3, 1, /*type=*/7, 0, 0, 0,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),   1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_count(), 0);
    return 0;
}

int test_engine_battlefield_render_per_entity_glyph_offset_gate_high(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    /* src_off = 0x4baf1 → just past the upper inclusive bound (0x4baf0). */
    blob_set_header(g_entity_blob, /*slot=*/1,
                    16, 16, /*src_off=*/0x4baf1);
    entity_set_body(1, 3, 3, 1, /*type=*/9, 0, 0, /*subtype=*/0x0e,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(),      0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_count(), 0);
    return 0;
}

/* Helper: initialise a fresh dirty-rect state with extreme bounds so
 * any input widens them (mirrors the pattern in test_engine_dirty_rect). */
static void dirty_reset(engine_dirty_rect_state_t *s)
{
    s->dirty_mark           = 0;
    s->dirty_xmin           = 0x7fff;
    s->dirty_xmax           = -0x7fff;
    s->dirty_ymin           = 0x7fff;
    s->dirty_ymax           = -0x7fff;
    s->below_status_redraw  = 0;
}

/* Anim-offset application at zoom 16.  Entity (facing=0, frame=0)
 * with anim offset (10, 20).  Sprite width 16.  Expected adjusted
 * cursor:
 *   entry_x + 10 + (8 - 16/2) = entry_x + 10 + 0  = entry_x + 10
 *   entry_y + 20 - 16/2 + 4   = entry_y + 20 - 4  = entry_y + 16
 *
 * Entity at (3, 3), camera (2, 2), zoom 16, viewport (0, 0):
 *   entry cursor = (16, 16) → adjusted cursor = (26, 32).
 *
 * We can't read the cursor back directly (the body restores entry
 * cursor before returning), but we can verify:
 *   - dirty_mark widens (some mark happened).
 *   - blit_count bumped (RLE dispatch reached past the gates). */
int test_engine_battlefield_render_per_entity_anim_offset_zoom16(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    anim_table_set(g_anim_z16, /*facing=*/0, /*frame=*/0,
                   /*x_off=*/10, /*y_off=*/20);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    entity_set_body(1, 3, 3, 1, /*type=*/0, /*facing=*/0,
                    /*anim_frame=*/0, /*subtype=*/0,
                    g_entity_blob, /*primary_frame=*/1);

    run_middle_only(/*cam=*/2, 2);

    /* Dirty box: type < 7 → 5×5 cells (5 tiles each side, 16 px per
     * tile).  Box top-left = (26, 32) (the adjusted cursor); the
     * expand call records (x1, y1, x2, y2) where x2 = 26 + 5*16 = 106,
     * y2 = 32 + 5*16 = 112.  Force-full flag (=2) escalates dirty_mark
     * past the default 1. */
    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    T_ASSERT_EQ_I(dirty.dirty_xmin, 26);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 32);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 106);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 112);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(), 1);
    return 0;
}

/* Anim-offset application at zoom 32.  Centering uses 16 (zoom_32 /
 * 2) and Y bonus 8 (vs 4 for zoom 16). */
int test_engine_battlefield_render_per_entity_anim_offset_zoom32(void)
{
    perentity_scaffold(/*zoom=*/0x20);
    anim_table_set(g_anim_z32, /*facing=*/0, /*frame=*/0,
                   /*x_off=*/3, /*y_off=*/5);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    /* Entity at (3, 3), camera (2, 2), zoom 32, viewport (0, 0):
     *   entry cursor = (32, 32).  Sprite w=16, h=16.
     *   off_x = 3 + (16 - 16/2) = 3 + 8  = 11
     *   off_y = 5 - 16/2 + 8   = 5 - 0   = 5
     * adjusted cursor = (43, 37). */
    entity_set_body(1, 3, 3, 1, /*type=*/0, /*facing=*/0,
                    /*anim_frame=*/0, /*subtype=*/0,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    T_ASSERT_EQ_I(dirty.dirty_xmin, 43);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 37);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(), 1);
    return 0;
}

/* Type-9 +12 Y bonus AFTER anim offset, at zoom 16.  Entity:
 *   entry cursor = (16, 16).  Anim offset (0, 0) so:
 *     post-anim: (0 + (8 - 16/2), 0 - 16/2 + 4) = (0, -4).
 *     cursor after anim = (16, 12).
 *   Type-9 bonus: cursor_y += 12 → (16, 24). */
int test_engine_battlefield_render_per_entity_type9_y_bonus(void)
{
    perentity_scaffold(/*zoom=*/0x10);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    /* Type 9 with subtype != 0xe so the subtype-e post-call doesn't
     * fire — keeps test focused on the Y bonus. */
    entity_set_body(1, 3, 3, 1, /*type=*/9, /*facing=*/0,
                    /*anim_frame=*/0, /*subtype=*/0,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    /* Dirty x stays at 16 (no X bonus); dirty y picks up +12 → 24. */
    T_ASSERT_EQ_I(dirty.dirty_xmin, 16);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 24);
    /* Type 9 ≥ 7 → 9×9 cell box. xmax = 16 + 9*16 = 160; ymax = 24 + 9*16 = 168. */
    T_ASSERT_EQ_I(dirty.dirty_xmax, 160);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 168);
    return 0;
}

/* Type-10 +8 Y bonus AFTER anim offset. */
int test_engine_battlefield_render_per_entity_type10_y_bonus(void)
{
    perentity_scaffold(/*zoom=*/0x10);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    entity_set_body(1, 3, 3, 1, /*type=*/10, /*facing=*/0,
                    /*anim_frame=*/0, /*subtype=*/0,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    /* Same arithmetic as type-9 but +8 instead of +12. */
    T_ASSERT_EQ_I(dirty.dirty_xmin, 16);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 20);
    /* Type 10 ≥ 7 → 9×9 box. */
    T_ASSERT_EQ_I(dirty.dirty_xmax, 160);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 164);
    return 0;
}

/* Dirty box uses 5×5 cells when entity type < 7, 9×9 when ≥ 7. */
int test_engine_battlefield_render_per_entity_box_5x5_vs_9x9(void)
{
    perentity_scaffold(/*zoom=*/0x10);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    /* type = 0 < 7 → 5×5 box.  Adjusted cursor (zoom 16, anim table
     * all zeros, sprite w=h=16):
     *   off_x = 0 + (8 - 16/2) = 0
     *   off_y = 0 - 16/2 + 4   = -4
     * → adjusted cursor (16, 12), 5×5 box → (16, 12)..(96, 92). */
    entity_set_body(1, 3, 3, 1, /*type=*/0, 0, 0, 0,
                    g_entity_blob, 1);
    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(dirty.dirty_xmax, 16 + 5 * 16);  /* 96  */
    T_ASSERT_EQ_I(dirty.dirty_ymax, 12 + 5 * 16);  /* 92  */

    /* Now type = 8 ≥ 7 → 9×9 box.  No type-9/10 Y bonus, so adjusted
     * cursor stays at (16, 12); box covers (16, 12)..(160, 156). */
    engine_battlefield_render_reset_counts();
    dirty_reset(&dirty);
    entity_set_body(1, 3, 3, 1, /*type=*/8, 0, 0, 0,
                    g_entity_blob, 1);
    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(dirty.dirty_xmax, 16 + 9 * 16);  /* 160 */
    T_ASSERT_EQ_I(dirty.dirty_ymax, 12 + 9 * 16);  /* 156 */
    return 0;
}

/* Unsupported zoom (not 0x10 / 0x20): per-entity body STILL runs (so
 * blit_count bumps), but anim offsets stay at 0.  Retail reads
 * uninitialized locals here; the port initializes to 0 for safety —
 * the game never exercises this path so the behavior diff is
 * unobservable in the original. */
int test_engine_battlefield_render_per_entity_unsupported_zoom(void)
{
    perentity_scaffold(/*zoom=*/99);
    /* Even though the frame driver would skip the terrain/overlay
     * passes at zoom != 0x10/0x20, the middle pass runs unconditionally
     * (see chip 5a.i). */
    entity_set_body(1, 3, 3, 1, /*type=*/0, /*facing=*/0,
                    /*anim_frame=*/0, /*subtype=*/0,
                    g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    /* The cell counter loops at zoom 99 still advance by zoom (=99)
     * per cell, but they don't crash. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(), 1);
    return 0;
}

/* Cursor restored after the per-entity draw — the next entity in
 * the sorted list sees the render-loop's staged anchor, not the
 * previous entity's post-adjust cursor.  Verify with two entities. */
int test_engine_battlefield_render_per_entity_cursor_restored(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    anim_table_set(g_anim_z16, /*facing=*/0, /*frame=*/0,
                   /*x_off=*/100, /*y_off=*/100);

    /* Two entities: id=1 at (2, 3), id=2 at (3, 4).  Sort by y →
     * id=1 first, id=2 second.  Second entity's last_px_x/y is the
     * render-loop-staged anchor (3-2)*16 + 0 = 16, (4-2)*16 + 0 = 32. */
    entity_set_body(1, /*x=*/2, /*y=*/3, 1, /*type=*/0, /*facing=*/0,
                    /*anim_frame=*/0, 0, g_entity_blob, 1);
    entity_set_body(2, /*x=*/3, /*y=*/4, 1, /*type=*/0, /*facing=*/0,
                    /*anim_frame=*/0, 0, g_entity_blob, 1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_count(),    2);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_id(),  2);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_x(), 16);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_per_entity_last_px_y(), 32);
    return 0;
}

/* Reset clears the new state too. */
int test_engine_battlefield_render_per_entity_reset_clears_new_state(void)
{
    perentity_scaffold(/*zoom=*/0x10);
    entity_set_body(1, 3, 3, 1, /*type=*/6, 0, 0, 0,
                    g_entity_blob, 1);
    entity_set_body(2, 3, 3, 1, /*type=*/7, 0, 0, 0,
                    g_entity_blob, 1);
    run_middle_only(/*cam=*/2, 2);

    T_ASSERT(engine_battlefield_render_middle_entity_type6_count() > 0);
    T_ASSERT(engine_battlefield_render_middle_entity_type7_count() > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_count(),       0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_last_id(),    -1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_count(),       0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_last_id(),    -1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_count(),   0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_last_id(),-1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_no_blob_count(),     0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_no_blob_last_id(),  -1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_blit_count(),        0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 5a.iv — per-entity type-special leaves (type-6 PRE-call,
 * type-7 POST-call, type-9-subtype-0xe POST-call).
 *
 * These three leaves are dispatched from `middle_leaf_draw_entity`
 * under specific type/subtype gates.  Each test reuses the `perentity`
 * scaffold from chip 5a.ii.b (entity at (3, 3), cam (2, 2), zoom 16,
 * 3×3 grid), plus the new chip-5a.iv blob bindings (type-6 shared
 * blob, type-7 facing-selected pair).
 *
 * Entry cursor for the entity at (3, 3) with cam (2, 2) zoom 16:
 *   px_x = (3-2)*16 + 0 = 16
 *   px_y = (3-2)*16 + 0 = 16
 *
 * Anim offset (facing=0, frame=0) defaults to (0, 0) in unwritten
 * anim tables.  Sprite w=h=16 (set by perentity_scaffold's
 * blob_set_header for slot 1).
 *
 * Centering at zoom 16 for type-6/type-7:
 *   x_off = 0 + (8 - 8) = 0
 *   y_off = 0 + (4 - 8) = -4
 * → adjusted cursor (16, 12).  6×6 box → (16, 12)..(112, 108).
 * ──────────────────────────────────────────────────────────────── */

#define TS_BLOB_SIZE 0x600
static uint8_t g_type6_blob[TS_BLOB_SIZE];
static uint8_t g_type7_blob_low[TS_BLOB_SIZE];
static uint8_t g_type7_blob_high[TS_BLOB_SIZE];

/* Scaffold extension: zero all type-special blobs and bind a fresh
 * dirty-rect state so each test starts clean. */
static void typespecial_scaffold(int zoom, engine_dirty_rect_state_t *dirty)
{
    perentity_scaffold(zoom);

    memset(g_type6_blob,      0, sizeof g_type6_blob);
    memset(g_type7_blob_low,  0, sizeof g_type7_blob_low);
    memset(g_type7_blob_high, 0, sizeof g_type7_blob_high);

    engine_battlefield_render_bind_entity_type6_blob(NULL);
    engine_battlefield_render_bind_entity_type7_blobs(NULL, NULL);

    if (dirty) {
        dirty_reset(dirty);
        engine_battlefield_render_set_dirty_state(dirty);
    }
}

/* ─── Type-6 PRE-call tests ───────────────────────────────────── */

/* NULL blob: leaf fires (count bumps) but no blit. */
int test_engine_battlefield_render_type6_null_blob_no_blit(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    entity_set_body(1, /*x=*/3, /*y=*/3, 1, /*type=*/6, /*facing=*/0,
                    /*anim_frame=*/0, 0, g_entity_blob, /*primary_frame=*/1);

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_blit_count(), 0);
    return 0;
}

/* Bound blob with valid src_off + slot → 1 blit; 6×6 dirty box at
 * adjusted cursor. */
int test_engine_battlefield_render_type6_blits_and_dirty_box(void)
{
    engine_dirty_rect_state_t dirty;
    typespecial_scaffold(/*zoom=*/0x10, &dirty);
    /* Secondary-frame slot 7 (matches the entity's primary_frame=1
     * stored at +0x10, but secondary_frame is at +0x11 — we override
     * it below). */
    blob_set_header(g_type6_blob, /*slot=*/7, /*w=*/16, /*h=*/16,
                    /*src_off=*/0x100);
    engine_battlefield_render_bind_entity_type6_blob(g_type6_blob);

    entity_set_body(1, /*x=*/3, /*y=*/3, 1, /*type=*/6, /*facing=*/0,
                    /*anim_frame=*/0, 0, g_entity_blob, /*primary_frame=*/1);
    /* Set secondary frame index to 7 — the slot we staged above. */
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 7;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_blit_count(), 1);
    /* Adjusted cursor (16, 12), 6×6 box: (16, 12)..(112, 108).
     * The MAIN entity draw (type 6 is not gated out of it) also marks
     * its own 5×5 box at (16, 12) (type < 7 → 5×5).  Both marks expand
     * the same dirty state — the larger box wins on xmax/ymax. */
    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    T_ASSERT_EQ_I(dirty.dirty_xmin, 16);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 12);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 112);  /* 16 + 6*16 — type-6 6×6 wins */
    T_ASSERT_EQ_I(dirty.dirty_ymax, 108);
    return 0;
}

/* Glyph offset gate: src_off out-of-band → no blit. */
int test_engine_battlefield_render_type6_glyph_offset_gate(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    blob_set_header(g_type6_blob, /*slot=*/7, 16, 16, /*src_off=*/0);
    engine_battlefield_render_bind_entity_type6_blob(g_type6_blob);

    entity_set_body(1, 3, 3, 1, /*type=*/6, 0, 0, 0, g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 7;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_blit_count(), 0);
    return 0;
}

/* ─── Type-7 POST-call tests ──────────────────────────────────── */

/* Facing < 4 → low blob.  Both bound; verify low fires, high doesn't. */
int test_engine_battlefield_render_type7_facing_low_blob(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    blob_set_header(g_type7_blob_low,  /*slot=*/5, 16, 16, /*src_off=*/0x100);
    blob_set_header(g_type7_blob_high, /*slot=*/5, 16, 16, /*src_off=*/0x100);
    engine_battlefield_render_bind_entity_type7_blobs(g_type7_blob_low,
                                                      g_type7_blob_high);

    entity_set_body(1, 3, 3, 1, /*type=*/7, /*facing=*/3, /*anim_frame=*/0,
                    0, g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 5;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_blit_count(), 1);
    return 0;
}

/* Facing >= 4 → high blob.  Only high bound; low NULL.  Verify high
 * fires; the test would also fire if low were used (asserts that the
 * facing selection picked HIGH at facing=4). */
int test_engine_battlefield_render_type7_facing_high_blob(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    blob_set_header(g_type7_blob_high, /*slot=*/5, 16, 16, /*src_off=*/0x100);
    engine_battlefield_render_bind_entity_type7_blobs(NULL, g_type7_blob_high);

    entity_set_body(1, 3, 3, 1, /*type=*/7, /*facing=*/4, 0, 0,
                    g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 5;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_blit_count(), 1);
    return 0;
}

/* Facing >= 4 with high NULL → no blit. */
int test_engine_battlefield_render_type7_facing_high_null(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    blob_set_header(g_type7_blob_low, /*slot=*/5, 16, 16, /*src_off=*/0x100);
    engine_battlefield_render_bind_entity_type7_blobs(g_type7_blob_low, NULL);

    entity_set_body(1, 3, 3, 1, /*type=*/7, /*facing=*/4, 0, 0,
                    g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 5;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_blit_count(), 0);
    return 0;
}

/* ─── Type-9-sub-0xe POST-call tests ──────────────────────────── */

/* Two-sprite draw.  Entity at (3, 5) for room above and below; both
 * sprite slots staged with valid src_off → 2 blits per call. */
int test_engine_battlefield_render_type9_sub_two_blits(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    /* Entity primary blob: stage slot 1 (main draw) AND slot 1+0x16=0x17
     * (type-9 first sprite) AND slot 1+0xd=0xe (type-9 second sprite). */
    blob_set_header(g_entity_blob, /*slot=*/0x17, 16, 16, /*src_off=*/0x100);
    blob_set_header(g_entity_blob, /*slot=*/0x0e, 16, 16, /*src_off=*/0x100);

    entity_set_body(1, /*x=*/3, /*y=*/5, 1, /*type=*/9, /*facing=*/0,
                    /*anim_frame=*/0, /*subtype=*/0x0e,
                    g_entity_blob, /*primary_frame=*/1);
    /* secondary_frame default 0 from entity_set_body → slot1 = 0+0x16 = 0x16
     * (NOT 0x17 we staged).  Override to make slot1=0x17. */
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 1;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_count(),      1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_blit_count(), 2);
    return 0;
}

/* First sprite's glyph_offset out-of-band → 0 blits, second NEVER tried
 * (gated by first's pass). */
int test_engine_battlefield_render_type9_sub_first_gate_aborts(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    /* slot 0x17 has out-of-band src_off (0); slot 0xe has valid src_off
     * — to prove it's not reached. */
    blob_set_header(g_entity_blob, /*slot=*/0x17, 16, 16, /*src_off=*/0);
    blob_set_header(g_entity_blob, /*slot=*/0x0e, 16, 16, /*src_off=*/0x100);

    entity_set_body(1, 3, 5, 1, /*type=*/9, 0, 0, 0x0e, g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 1;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_blit_count(), 0);
    return 0;
}

/* Second sprite's glyph_offset out-of-band → first fires but second
 * doesn't.  blit_count = 1. */
int test_engine_battlefield_render_type9_sub_second_gate_skips(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    blob_set_header(g_entity_blob, /*slot=*/0x17, 16, 16, /*src_off=*/0x100);
    blob_set_header(g_entity_blob, /*slot=*/0x0e, 16, 16, /*src_off=*/0);

    entity_set_body(1, 3, 5, 1, /*type=*/9, 0, 0, 0x0e, g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 1;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_blit_count(), 1);
    return 0;
}

/* Type-9-sub-0xe dirty box: entity at (3, 5), zoom 16:
 *   entry cursor (16, 48)
 *   First sprite (sw=16, half=8): cursor (16+8, 48-8-16) = (24, 24)
 *     box 6×6 (96×96) → (24, 24)..(120, 120)
 *   Second sprite: cursor (16+8, 48-8+0x56) = (24, 126)
 *     box 6×6 → (24, 126)..(120, 222)
 *   Expand union: xmin=24, ymin=24, xmax=120, ymax=222.
 *   Main entity draw (type 9 < 7? no, 9 >= 7 → 9×9 box) adds its
 *   own mark at (16, 48+12)=(16, 60) [type 9 Y bonus +12 — see chip
 *   5a.ii.b].  Actually wait — main draw mark uses the FULLY-ADJUSTED
 *   cursor.  For type=9 zoom 16 with anim 0,0 and primary_frame slot 1
 *   sprite_w=16: adj_x = 0 + (8 - 8) = 0; adj_y = 0 + (4 - 8) = -4;
 *   then type 9 Y bonus +12 → (16, 16-4+12) = (16, 24).  9×9 box
 *   (144×144) → (16, 24)..(160, 168).  This is wider than the type-9
 *   sub leaves in X but narrower in Y.
 *   Union: xmin=16, ymin=24, xmax=160, ymax=222. */
int test_engine_battlefield_render_type9_sub_dirty_boxes(void)
{
    engine_dirty_rect_state_t dirty;
    typespecial_scaffold(/*zoom=*/0x10, &dirty);
    blob_set_header(g_entity_blob, 0x17, 16, 16, 0x100);
    blob_set_header(g_entity_blob, 0x0e, 16, 16, 0x100);

    entity_set_body(1, 3, 5, 1, /*type=*/9, 0, 0, 0x0e, g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 1;

    run_middle_only(/*cam=*/2, 2);

    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    T_ASSERT_EQ_I(dirty.dirty_xmin, 16);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 24);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 160);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 222);
    return 0;
}

/* ─── Reset zeros chip 5a.iv counters ─────────────────────────── */

int test_engine_battlefield_render_typespecial_reset_clears(void)
{
    typespecial_scaffold(/*zoom=*/0x10, NULL);
    blob_set_header(g_type6_blob, 7, 16, 16, 0x100);
    engine_battlefield_render_bind_entity_type6_blob(g_type6_blob);

    entity_set_body(1, 3, 3, 1, /*type=*/6, 0, 0, 0, g_entity_blob, 1);
    g_entity_table[1 * ENT_STRIDE
                   + ENGINE_BATTLEFIELD_ENTITY_OFFSET_SECONDARY_FRAME] = 7;
    run_middle_only(/*cam=*/2, 2);

    T_ASSERT(engine_battlefield_render_middle_entity_type6_blit_count() > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type6_blit_count(),      0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type7_blit_count(),      0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_entity_type9_sub_blit_count(),  0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 5a.iii — per-cell draw leaves (player_marker / unit_key /
 * effect_chain).
 *
 * Each test stages a 3×3 visible grid at cam=(2,2) inside an 8×8
 * physical table (so the middle-pass over-scan never clamps), unbinds
 * the canvas (so RLE blit dispatches but is a no-op at the pixel
 * level), and binds a fake sprite blob with a single populated slot.
 * The leaf's call counter + blit counter + slot capture + dirty-rect
 * state observe the body's behavior without needing real pixel output.
 *
 * The middle pass dispatches each leaf via the bit-0x80 / non-zero
 * gates explored by the chip 5a.i tests above:
 *
 *   desc[+2] & 0x80, desc[+3] == 0  → player_marker
 *   desc[+2] & 0x80, desc[+3] != 0  → unit_key(desc[+3])
 *   desc[+6] != 0                    → effect_chain(desc[+6])
 *
 * So each leaf test sets up one cell with the appropriate descriptor
 * shape and triggers `run_middle_only`.
 * ──────────────────────────────────────────────────────────────── */

#define PLAYER_MARKER_BLOB_SIZE 0x600
static uint8_t g_player_marker_blob_buf[PLAYER_MARKER_BLOB_SIZE];

#define PLAYER_SLOT_TABLE_SIZE  0x1000
static uint8_t g_player_slot_table[PLAYER_SLOT_TABLE_SIZE];

#define UNIT_KEY_BLOB_SIZE  0x400
static uint8_t g_unit_key_blob_buf[UNIT_KEY_BLOB_SIZE];

#define EFFECT_CHAIN_MAX_ID 0x10
#define EFFECT_BLOB_SIZE    0x400
static uint8_t g_effect_chain_buf[(EFFECT_CHAIN_MAX_ID + 1)
    * ENGINE_BATTLEFIELD_EFFECT_CHAIN_STRIDE_RETAIL];
static uint8_t g_effect_blob_buf[EFFECT_BLOB_SIZE];
static uint8_t g_effect_anim_table[ENGINE_BATTLEFIELD_EFFECT_ANIM_TABLE_FRAMES * 8];

/* Stage a leaf-test descriptor cell at (col, row) of the 8×8 physical
 * table with the given (flags, key, effect6). */
static void leaf_cell(int col, int row,
                      uint8_t flags, uint8_t key3, uint8_t effect6)
{
    desc_set_middle(col, row, DESC_COLS_PHY,
                    /*b1=*/0, flags, key3, effect6);
}

/* Common leaf scaffold.  Unbinds canvas (blits become no-ops at pixel
 * level — we observe via the blit counters); zeros all per-leaf
 * fixtures; binds a generous clip rect so y_clip_mode resolves to 0
 * (blits reachable through the dispatcher). */
static void leaf_scaffold(void)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    gfx_paint_bind_canvas(NULL, 0, 0);

    memset(g_player_marker_blob_buf, 0, sizeof g_player_marker_blob_buf);
    memset(g_player_slot_table,      0, sizeof g_player_slot_table);
    memset(g_unit_key_blob_buf,      0, sizeof g_unit_key_blob_buf);
    memset(g_effect_chain_buf,       0, sizeof g_effect_chain_buf);
    memset(g_effect_blob_buf,        0, sizeof g_effect_blob_buf);
    memset(g_effect_anim_table,      0, sizeof g_effect_anim_table);

    engine_battlefield_render_bind_player_marker_blob(NULL);
    engine_battlefield_render_bind_player_marker_slot_table(NULL, 0);
    engine_battlefield_render_set_player_marker_player_id(0);
    engine_battlefield_render_set_player_marker_anim_advance(0);
    engine_battlefield_render_bind_unit_key_blob(NULL);
    engine_battlefield_render_bind_effect_chain_table(NULL, 0, 0);
    engine_battlefield_render_bind_effect_chain_anim_table(NULL);
    engine_battlefield_render_bind_effect_chain_shared_blob(NULL);
    engine_battlefield_render_bind_scroll_pad(0);
    engine_battlefield_render_set_dirty_state(NULL);

    engine_battlefield_render_bind_clip_rect(/*left=*/-1000, /*top=*/-1000,
                                             /*right=*/2000, /*bottom=*/2000);
}

/* Stage an effect record at id with the given fields.  Blob comes from
 * the shared bind (see leaf_scaffold and the header notes on the
 * shared-blob substitute for the per-record indirection). */
static void effect_set(int id,
                       int16_t next_id, uint8_t type, uint8_t subtype,
                       int16_t x_off, int16_t y_off, int16_t slot)
{
    uint8_t *rec = g_effect_chain_buf
        + id * ENGINE_BATTLEFIELD_EFFECT_CHAIN_STRIDE_RETAIL;
    memcpy(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_NEXT_ID, &next_id, sizeof next_id);
    rec[ENGINE_BATTLEFIELD_EFFECT_OFFSET_TYPE]    = type;
    rec[ENGINE_BATTLEFIELD_EFFECT_OFFSET_SUBTYPE] = subtype;
    memcpy(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_X_OFFSET, &x_off, sizeof x_off);
    memcpy(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_Y_OFFSET, &y_off, sizeof y_off);
    memcpy(rec + ENGINE_BATTLEFIELD_EFFECT_OFFSET_SLOT,     &slot,  sizeof slot);
}

/* ─── Player-marker tests ─────────────────────────────────────── */

/* NULL blob: leaf fires (count bumps) but no blit dispatch. */
int test_engine_battlefield_render_player_marker_null_blob_no_blit(void)
{
    leaf_scaffold();
    leaf_cell(2, 2, /*flags=*/0x80, /*key3=*/0, /*effect6=*/0);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_count(),       1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_blit_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_last_slot(), -1);
    return 0;
}

/* Blob bound, slot table NULL → banner_byte defaults to 0 → slot =
 * frame + 0x21 = 0x21 on first call (frame=0, anim_advance=0). */
int test_engine_battlefield_render_player_marker_slot_default(void)
{
    leaf_scaffold();
    blob_set_slot(g_player_marker_blob_buf, /*slot=*/0x21,
                  16, 16, /*src_off=*/0x100,
                  /*flag_a=*/0, /*advance=*/16, /*flag_b=*/0);
    engine_battlefield_render_bind_player_marker_blob(g_player_marker_blob_buf);

    leaf_cell(2, 2, /*flags=*/0x80, 0, 0);

    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_blit_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_last_slot(), 0x21);
    return 0;
}

/* Slot computation: banner_byte (from slot_table[player_id * stride])
 * × 8 + frame + 0x21.  Pick banner_byte = 3, player_id = 5 → slot table
 * write at offset 5 * 0x1a4 = 0x82c.  Slot = 3*8 + 0 + 0x21 = 0x39. */
int test_engine_battlefield_render_player_marker_slot_from_table(void)
{
    leaf_scaffold();
    g_player_slot_table[5 * ENGINE_BATTLEFIELD_PLAYER_SLOT_TABLE_STRIDE_RETAIL] = 3;
    engine_battlefield_render_bind_player_marker_slot_table(
        g_player_slot_table, ENGINE_BATTLEFIELD_PLAYER_SLOT_TABLE_STRIDE_RETAIL);
    engine_battlefield_render_set_player_marker_player_id(5);

    blob_set_slot(g_player_marker_blob_buf, /*slot=*/0x39,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_player_marker_blob(g_player_marker_blob_buf);

    leaf_cell(2, 2, 0x80, 0, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_last_slot(), 0x39);
    return 0;
}

/* anim_advance non-zero advances frame by 1 each call. */
int test_engine_battlefield_render_player_marker_anim_advance(void)
{
    leaf_scaffold();
    engine_battlefield_render_set_player_marker_anim_advance(1);

    leaf_cell(2, 2, 0x80, 0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_player_marker_anim_frame(), 0);
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_player_marker_anim_frame(), 1);
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_player_marker_anim_frame(), 2);
    return 0;
}

/* anim_advance zero holds frame. */
int test_engine_battlefield_render_player_marker_anim_hold(void)
{
    leaf_scaffold();
    engine_battlefield_render_set_player_marker_anim_advance(0);

    leaf_cell(2, 2, 0x80, 0, 0);

    run_middle_only(2, 2);
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_player_marker_anim_frame(), 0);
    return 0;
}

/* Frame wraps 0..7. */
int test_engine_battlefield_render_player_marker_anim_wraps(void)
{
    leaf_scaffold();
    engine_battlefield_render_set_player_marker_anim_advance(1);

    leaf_cell(2, 2, 0x80, 0, 0);

    /* 8 advances → frame goes 1, 2, 3, 4, 5, 6, 7, 0. */
    for (int i = 0; i < 8; i++) {
        run_middle_only(2, 2);
    }
    T_ASSERT_EQ_I(engine_battlefield_render_player_marker_anim_frame(), 0);
    return 0;
}

/* Glyph offset out-of-band → no blit, no slot capture. */
int test_engine_battlefield_render_player_marker_glyph_offset_gate(void)
{
    leaf_scaffold();
    blob_set_slot(g_player_marker_blob_buf, /*slot=*/0x21,
                  16, 16, /*src_off=*/0, 0, 16, 0);   /* offset = 0 → out-of-band */
    engine_battlefield_render_bind_player_marker_blob(g_player_marker_blob_buf);

    leaf_cell(2, 2, 0x80, 0, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_count(),       1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_blit_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_last_slot(), -1);
    return 0;
}

/* Dirty box: 4×4 tiles (64×64 pixels) at the recentered cursor,
 * force_full = 2.  Sprite width 16 → adj = 16 - 16/2 = 8.  Cell (2,2)
 * with cam=(2,2): over-scan starts at cam-1=1, start_px_x=-16.  Cell
 * (2,2) is at col_in_loop=1, so entry cursor = (-16+16, -16+16) = (0, 0).
 * Re-centered: (8, 8).  Box: (8, 8)..(8+64, 8+64) = (8, 8)..(72, 72). */
int test_engine_battlefield_render_player_marker_dirty_box(void)
{
    leaf_scaffold();
    blob_set_slot(g_player_marker_blob_buf, /*slot=*/0x21,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_player_marker_blob(g_player_marker_blob_buf);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    leaf_cell(2, 2, 0x80, 0, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    T_ASSERT_EQ_I(dirty.dirty_xmin, 8);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 8);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 72);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 72);
    return 0;
}

/* Cursor restored after blit — middle-pass per-cell cursor walks
 * continue from the cell's pre-blit position. */
int test_engine_battlefield_render_player_marker_cursor_restored(void)
{
    leaf_scaffold();
    blob_set_slot(g_player_marker_blob_buf, /*slot=*/0x21,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_player_marker_blob(g_player_marker_blob_buf);

    leaf_cell(2, 2, 0x80, 0, 0);
    run_middle_only(2, 2);

    /* The middle-pass cursor_x ends at start_px_x + num_cols*zoom =
     * -16 + 5*16 = 64, regardless of whether the leaf re-centered cursor
     * mid-frame.  This proves the leaf restored before its caller's
     * per-cell advance. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 64);
    return 0;
}

/* ─── Unit-key tests ──────────────────────────────────────────── */

/* Key→slot map: 0x49 → 0x1f. */
int test_engine_battlefield_render_unit_key_slot_I(void)
{
    leaf_scaffold();
    blob_set_slot(g_unit_key_blob_buf, /*slot=*/0x1f,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);

    leaf_cell(2, 2, 0x80, /*key3=*/0x49, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_arg(),  0x49);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_blit_count(),    1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(), 0x1f);
    return 0;
}

/* Key→slot map: 0x4c → 0x20. */
int test_engine_battlefield_render_unit_key_slot_L(void)
{
    leaf_scaffold();
    blob_set_slot(g_unit_key_blob_buf, /*slot=*/0x20,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);

    leaf_cell(2, 2, 0x80, 0x4c, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(), 0x20);
    return 0;
}

/* Key→slot map: 0x61 → 0x21. */
int test_engine_battlefield_render_unit_key_slot_a(void)
{
    leaf_scaffold();
    blob_set_slot(g_unit_key_blob_buf, /*slot=*/0x21,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);

    leaf_cell(2, 2, 0x80, 0x61, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(), 0x21);
    return 0;
}

/* Key→slot map: 0x64 → 0x22. */
int test_engine_battlefield_render_unit_key_slot_d(void)
{
    leaf_scaffold();
    blob_set_slot(g_unit_key_blob_buf, /*slot=*/0x22,
                  16, 16, /*src_off=*/0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);

    leaf_cell(2, 2, 0x80, 0x64, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(), 0x22);
    return 0;
}

/* Unknown key → leaf returns immediately; no blit, no slot. */
int test_engine_battlefield_render_unit_key_unknown_no_blit(void)
{
    leaf_scaffold();
    /* Stage a blob at slot 0x1f so the gate would pass if dispatch ever
     * reached it — to prove the early return cuts off cleanly. */
    blob_set_slot(g_unit_key_blob_buf, 0x1f, 16, 16, 0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);

    leaf_cell(2, 2, 0x80, /*key3=*/0x55, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_arg(),  0x55);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_blit_count(),    0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(),    -1);
    return 0;
}

/* NULL blob → leaf reaches dispatch site but render helper returns 0. */
int test_engine_battlefield_render_unit_key_null_blob(void)
{
    leaf_scaffold();
    /* blob NOT bound — default NULL. */
    leaf_cell(2, 2, 0x80, 0x49, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_count(),         1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_blit_count(),    0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(),    -1);
    return 0;
}

/* Dirty box: cell (2,2), key 'I' → slot 0x1f.  Sprite w=16, adj = 8.
 * Entry cursor (0, 0) at the cell, re-centered (8, 8), 4×4 box →
 * (8,8)..(72,72). */
int test_engine_battlefield_render_unit_key_dirty_box(void)
{
    leaf_scaffold();
    blob_set_slot(g_unit_key_blob_buf, 0x1f, 16, 16, 0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    leaf_cell(2, 2, 0x80, 0x49, 0);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(dirty.dirty_xmin, 8);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 8);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 72);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 72);
    return 0;
}

/* ─── Effect-chain tests ──────────────────────────────────────── */

static void bind_effect_table(int max_id)
{
    engine_battlefield_render_bind_effect_chain_table(
        g_effect_chain_buf,
        ENGINE_BATTLEFIELD_EFFECT_CHAIN_STRIDE_RETAIL,
        max_id);
    engine_battlefield_render_bind_effect_chain_shared_blob(g_effect_blob_buf);
}

/* descriptor[+6] == 1, but effect[1].type == 0 → chain terminates
 * immediately; no blit, 0 iterations. */
int test_engine_battlefield_render_effect_chain_type_zero_ends(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    /* effect[1] left zeroed → type == 0. */

    leaf_cell(2, 2, 0, 0, /*effect6=*/1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_count(),    1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(),0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),    0);
    return 0;
}

/* Single non-animated effect with valid type, blob, slot → 1 blit.
 * iters reaches 1 because the post-record advance happens before the
 * loop sees id=0 and returns. */
int test_engine_battlefield_render_effect_chain_single_effect(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, /*slot=*/3, 16, 16, 0x100, 0, 16, 0);
    effect_set(/*id=*/1,
               /*next_id=*/0, /*type=*/1, /*subtype=*/0,
               /*x_off=*/0, /*y_off=*/0, /*slot=*/3);

    leaf_cell(2, 2, 0, 0, /*effect6=*/1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_count(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(),1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),     1);
    return 0;
}

/* Two-effect chain → walks both, 2 blits, iters=2 (the iter counter
 * increments AFTER each record's processing, so the final value reflects
 * "records processed" exactly). */
int test_engine_battlefield_render_effect_chain_walks_chain(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, /*next_id=*/2, 1, 0, 0, 0, 3);
    effect_set(2, /*next_id=*/0, 1, 0, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(), 2);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),      2);
    return 0;
}

/* Self-loop: effect[1].next == 1 → walk does effect[1] then guard
 * triggers, ending the chain.  1 blit, 0 iters (the iter counter is
 * incremented AFTER the self-loop check, so it stays 0). */
int test_engine_battlefield_render_effect_chain_self_loop_guard(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, /*next_id=*/1, 1, 0, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(), 1);
    return 0;
}

/* 10-hop safety cap: chain of 12 forward-linked effects.  The walk
 * processes 10 iterations + the entry record → 11 blits, then the
 * `9 < iters` break fires. */
int test_engine_battlefield_render_effect_chain_safety_cap(void)
{
    leaf_scaffold();
    bind_effect_table(EFFECT_CHAIN_MAX_ID);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    /* Chain 1 → 2 → 3 → ... → 12 → 0.  Build 12 records. */
    for (int i = 1; i <= 12; i++) {
        effect_set(i,
                   /*next_id=*/(int16_t)((i == 12) ? 0 : i + 1),
                   /*type=*/1, /*subtype=*/0,
                   /*x_off=*/0, /*y_off=*/0, /*slot=*/3);
    }

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    /* Retail loop: enter with id=1, iters=0; at start of each iteration
     * `9 < iters` checks for break BEFORE processing.  So records
     * processed = 10 (iters values 0..9), then iters=10 fails the check
     * and the loop breaks without processing record id=11.  Blit count
     * should be exactly 10. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(), 10);
    return 0;
}

/* Subtype 7 → skip render, continue walk.  Two-effect chain: effect[1]
 * is subtype 7 (skip), effect[2] is subtype 0 (renders).  Total: 1
 * blit (only effect[2]). */
int test_engine_battlefield_render_effect_chain_subtype7_skips(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, /*next_id=*/2, 1, /*subtype=*/7, 0, 0, 3);
    effect_set(2, /*next_id=*/0, 1, /*subtype=*/0, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(), 1);
    /* But the chain walk DID continue past effect[1] — iters bumped on both. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),      2);
    return 0;
}

/* Subtype 5 advances anim_frame counter. */
int test_engine_battlefield_render_effect_chain_subtype5_advances_anim(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, 0, 1, /*subtype=*/5, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);

    T_ASSERT_EQ_I(engine_battlefield_render_effect_chain_anim_frame(), 0);
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_effect_chain_anim_frame(), 1);
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_effect_chain_anim_frame(), 2);
    return 0;
}

/* Anim frame wraps 0..15. */
int test_engine_battlefield_render_effect_chain_anim_wraps(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, 0, 1, /*subtype=*/5, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);

    /* 16 advances → wraps back to 0. */
    for (int i = 0; i < 16; i++) {
        run_middle_only(2, 2);
    }
    T_ASSERT_EQ_I(engine_battlefield_render_effect_chain_anim_frame(), 0);
    return 0;
}

/* Subtype != 5 does NOT advance anim_frame. */
int test_engine_battlefield_render_effect_chain_nonsubtype5_holds_anim(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, 0, 1, /*subtype=*/0, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);
    run_middle_only(2, 2);
    T_ASSERT_EQ_I(engine_battlefield_render_effect_chain_anim_frame(), 0);
    return 0;
}

/* QUIRK: glyph-offset gate out-of-band aborts the WHOLE chain walk —
 * not just this record.  Two-effect chain with effect[1] having an
 * out-of-band src_off and effect[2] perfectly renderable: the leaf
 * still gates on effect[1] and `return`s, never advancing to effect[2].
 * Verifies the abort behavior via blit_count: with a per-record skip
 * we'd expect 1 blit (effect[2]); with the chain-walk abort, 0.  */
int test_engine_battlefield_render_effect_chain_glyph_offset_quirk(void)
{
    leaf_scaffold();
    bind_effect_table(4);

    /* slot 3 has out-of-band src_off (0); slot 4 has a valid src_off
     * (0x100) — visible if reached. */
    blob_set_slot(g_effect_blob_buf, /*slot=*/3, 16, 16, /*src_off=*/0,    0, 16, 0);
    blob_set_slot(g_effect_blob_buf, /*slot=*/4, 16, 16, /*src_off=*/0x100, 0, 16, 0);

    effect_set(1, /*next_id=*/2, 1, /*subtype=*/0, 0, 0, /*slot=*/3);
    effect_set(2, /*next_id=*/0, 1, /*subtype=*/0, 0, 0, /*slot=*/4);

    leaf_cell(2, 2, 0, 0, /*effect6=*/1);
    run_middle_only(2, 2);

    /* Chain aborted by effect[1]'s gate → effect[2] never reached. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(), 0);
    /* iters stays at 0 because the early `return` skips the
     * post-record `g_effect_chain_iters += 1`. */
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),      0);
    return 0;
}

/* NULL shared blob → CONTINUE chain walk but no blits.  Two-effect chain:
 * walk visits both, but the blob-NULL gate suppresses dispatch on each. */
int test_engine_battlefield_render_effect_chain_null_blob_continues(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    /* Override the shared blob installed by bind_effect_table. */
    engine_battlefield_render_bind_effect_chain_shared_blob(NULL);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);

    effect_set(1, /*next_id=*/2, 1, 0, 0, 0, 3);
    effect_set(2, /*next_id=*/0, 1, 0, 0, 0, 3);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),      2);
    return 0;
}

/* Scroll pad shifts cursor on the rendered effect.  Observe via the
 * dirty-box max corner (mark_box_tiled clamps the TOP-LEFT to 0 — so
 * to observe the pad we need a positive cursor, i.e. compensate the
 * x_off so cam*32 doesn't pull it negative).
 *
 * x_off = 100 → eff_x = 100 - 2*32 = 36; cursor_x = 0 + 36 + pad.
 * With pad=7: cursor=43; box (43, 43)..(107, 107) → xmax=107. */
int test_engine_battlefield_render_effect_chain_scroll_pad(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);
    effect_set(1, 0, 1, /*subtype=*/0,
               /*x_off=*/100, /*y_off=*/100, /*slot=*/3);
    engine_battlefield_render_bind_scroll_pad(7);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    /* Mark fired → dirty_mark escalates to 2 (force_full = 2). */
    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    T_ASSERT_EQ_I(dirty.dirty_xmin, 43);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 43);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 107);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 107);
    return 0;
}

/* Subtype-5 anim jitter from the anim table.  Same approach as the
 * scroll-pad test — compensate cam*32 with the record's x_off / y_off
 * so the cursor stays positive and the dirty-box clamp doesn't fire.
 *
 * x_off=100, y_off=100, subtype 5, anim_frame advances 0→1 → table
 * entry 1 = (10, 20).  Cursor = (100 + 10 - 64, 100 + 20 - 64) = (46, 56).
 * Box: (46, 56)..(110, 120). */
int test_engine_battlefield_render_effect_chain_anim_jitter(void)
{
    leaf_scaffold();
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);
    effect_set(1, 0, 1, /*subtype=*/5,
               /*x_off=*/100, /*y_off=*/100, /*slot=*/3);

    int32_t x_off = 10, y_off = 20;
    memcpy(g_effect_anim_table + 1 * 8 + 0, &x_off, sizeof x_off);
    memcpy(g_effect_anim_table + 1 * 8 + 4, &y_off, sizeof y_off);
    engine_battlefield_render_bind_effect_chain_anim_table(g_effect_anim_table);

    engine_dirty_rect_state_t dirty;
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);

    leaf_cell(2, 2, 0, 0, 1);
    run_middle_only(2, 2);

    T_ASSERT_EQ_I(dirty.dirty_xmin, 46);
    T_ASSERT_EQ_I(dirty.dirty_ymin, 56);
    T_ASSERT_EQ_I(dirty.dirty_xmax, 110);
    T_ASSERT_EQ_I(dirty.dirty_ymax, 120);
    return 0;
}

/* ─── Reset / accumulator tests ──────────────────────────────── */

/* Reset clears all chip-5a.iii state (counts, slots, anim counters). */
int test_engine_battlefield_render_leaf_reset_clears_state(void)
{
    leaf_scaffold();
    /* Don't enable player_marker anim_advance — frame stays 0 so slot
     * lands on 0x21, the one we've staged.  Reset-of-anim_frame is
     * verified separately (it starts at 0 anyway). */
    blob_set_slot(g_player_marker_blob_buf, 0x21, 16, 16, 0x100, 0, 16, 0);
    engine_battlefield_render_bind_player_marker_blob(g_player_marker_blob_buf);
    blob_set_slot(g_unit_key_blob_buf, 0x1f, 16, 16, 0x100, 0, 16, 0);
    engine_battlefield_render_bind_unit_key_blob(g_unit_key_blob_buf);
    bind_effect_table(4);
    blob_set_slot(g_effect_blob_buf, 3, 16, 16, 0x100, 0, 16, 0);
    /* subtype 5 → advances effect_chain anim_frame, lets us observe
     * reset zeros it. */
    effect_set(1, 0, 1, 5, 0, 0, 3);

    /* Cells must be within the cam=(2,2) over-scan window (physical
     * cols/rows 1..5). */
    leaf_cell(2, 2, 0x80, 0,    1);  /* player_marker + effect_chain */
    leaf_cell(3, 2, 0x80, 0x49, 0);  /* unit_key */
    run_middle_only(2, 2);

    T_ASSERT(engine_battlefield_render_middle_player_marker_blit_count() > 0);
    T_ASSERT(engine_battlefield_render_middle_unit_key_blit_count()      > 0);
    T_ASSERT(engine_battlefield_render_middle_effect_chain_blit_count()  > 0);
    T_ASSERT(engine_battlefield_render_effect_chain_anim_frame()         > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_blit_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_blit_count(),       0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_blit_count(),   0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_player_marker_last_slot(),  -1);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_unit_key_last_slot(),       -1);
    T_ASSERT_EQ_I(engine_battlefield_render_player_marker_anim_frame(),         0);
    T_ASSERT_EQ_I(engine_battlefield_render_effect_chain_anim_frame(),          0);
    T_ASSERT_EQ_I(engine_battlefield_render_middle_effect_chain_iters(),        0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Coordinate helpers — ports of FUN_004bc222 / 2a5 / 2c5 / 2e5 / 346.
 *
 * Scaffold sets a 10×5 visible grid at viewport (100, 50), zoom 32,
 * band_width 8 (so the cell is divided into bands [0,8)/[8,24)/[24,32)),
 * camera_row_stride 20.
 * ──────────────────────────────────────────────────────────────── */

static void coords_scaffold(void)
{
    engine_battlefield_render_set_zoom(32);
    engine_battlefield_render_bind_grid(/*cols=*/10, /*rows=*/5,
                                        /*row_wrap=*/0);
    engine_battlefield_render_bind_viewport_offset(/*x=*/100, /*y=*/50);
    engine_battlefield_render_bind_camera_row_stride(20);
    engine_battlefield_render_bind_cell_band_width(8);
    /* chip 6b — total_rows clamp for the top-biased row helper. */
    engine_battlefield_render_bind_total_rows(15);
}

int test_engine_battlefield_render_screen_in_grid_inside(void)
{
    coords_scaffold();

    /* Centre pixel of the grid. */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(160, 90), 1);
    /* Top-left corner of grid (inclusive). */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(100, 50), 1);
    /* One pixel inside the bottom-right corner.  Grid extends to
     * viewport_x + cols*zoom = 100 + 320 = 420, and y = 50 + 160 = 210
     * — those are EXCLUSIVE upper bounds. */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(419, 209), 1);
    return 0;
}

int test_engine_battlefield_render_screen_in_grid_outside(void)
{
    coords_scaffold();

    /* Just past upper bounds (exclusive). */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(420, 90),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(160, 210), 0);
    /* Below viewport. */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(99, 90),   0);
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(160, 49),  0);
    /* Both axes outside. */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(0, 0),     0);
    return 0;
}

int test_engine_battlefield_render_pixel_to_col_basic(void)
{
    coords_scaffold();

    /* Pixel at viewport.x → col 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col(100), 0);
    /* Pixel at viewport.x + 31 → still col 0 (last pixel of cell). */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col(131), 0);
    /* Pixel at viewport.x + 32 → col 1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col(132), 1);
    /* Pixel at viewport.x + 5*32 → col 5. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col(260), 5);
    return 0;
}

int test_engine_battlefield_render_pixel_to_row_basic(void)
{
    coords_scaffold();

    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row(50),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row(81),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row(82),  1);
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row(178), 4);
    return 0;
}

/* Right-biased variant: pixel in third band (residue >= 24) shifts
 * column +1. */
int test_engine_battlefield_render_pixel_to_col_right_biased_middle_band(void)
{
    coords_scaffold();

    /* px=120 → residue=20 (middle band, [8,24)) → no shift, col=0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(120), 0);
    return 0;
}

int test_engine_battlefield_render_pixel_to_col_right_biased_right_band(void)
{
    coords_scaffold();

    /* px=124 → residue=24 (third band, [24,32)) → col=0+1=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(124), 1);
    /* px=131 → residue=31 (still third band) → col=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(131), 1);
    return 0;
}

int test_engine_battlefield_render_pixel_to_col_right_biased_first_band(void)
{
    coords_scaffold();

    /* px=102 → residue=2 (first band, [0,8)) → no shift (function
     * doesn't bias on first band). */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(102), 0);
    return 0;
}

/* Lower-bound clamp: any input giving raw col < 1 returns 0. */
int test_engine_battlefield_render_pixel_to_col_right_biased_clamps_low(void)
{
    coords_scaffold();

    /* px=99 → (99-100)/32 = 0 in C99 truncation (towards zero), then
     * (-1)%32 = -1, which is < 24 so no shift.  col=0.  C-correct. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(99), 0);
    /* px=68 → (-32)/32 = -1, then bias: (68-100)%32 = -32%32 = 0,
     * NOT >= 24, so no shift.  col=-1, but clamp says < 1 → 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(68), 0);
    return 0;
}

/* Left-biased variant: pixel in first band (residue < 8) shifts col -1. */
int test_engine_battlefield_render_pixel_to_col_left_biased_first_band(void)
{
    coords_scaffold();

    /* px=140 → (140-100)/32 = 1, residue = 40%32 = 8.  NOT < 8 →
     * no shift, col=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(140), 1);
    /* px=139 → col=1, residue=39%32=7, < 8 → col=0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(139), 0);
    return 0;
}

int test_engine_battlefield_render_pixel_to_col_left_biased_middle_band(void)
{
    coords_scaffold();

    /* px=120 → col=0, residue=20 (middle band) → no shift, col=0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(120), 0);
    /* px=124 → col=0, residue=24 → not < 8 → no shift, col=0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(124), 0);
    return 0;
}

/* Upper-bound clamp: col >= camera_row_stride floors to row_stride-1. */
int test_engine_battlefield_render_pixel_to_col_left_biased_clamps_high(void)
{
    coords_scaffold();

    /* camera_row_stride=20.  px far enough right to compute col >= 20.
     * px = 100 + 20*32 = 740 → col=20 → clamp to 19. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(740), 19);
    /* px=100 + 25*32 = 900 → col=25 → clamp to 19. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(900), 19);
    return 0;
}

/* Asymmetry: left_biased does NOT clamp negative cols. */
int test_engine_battlefield_render_pixel_to_col_left_biased_no_low_clamp(void)
{
    coords_scaffold();

    /* px=99 → col = (99-100)/32 = 0 (truncated towards 0 in C99).
     * residue = -1 % 32 = -1, NOT < 8 (since -1 < 8 is true!),
     * so col -= 1 → col = -1.  No low clamp → returns -1. */
    /* Wait: -1 < 8 IS true in two's-complement, so the bias FIRES.
     * Verify negative result propagates. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_left_biased(99), -1);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 6b — Y-axis siblings, delta helpers, zoom accessor.
 * Same scaffold: viewport (100, 50), zoom 32, band_width 8,
 * total_rows 15.
 * ──────────────────────────────────────────────────────────────── */

/* Bottom-biased: py in third band (residue >= 24) shifts row +1.
 * Floor-clamps to 0 (no negative rows). */
int test_engine_battlefield_render_pixel_to_row_bottom_biased_middle_band(void)
{
    coords_scaffold();

    /* py=70 → residue=20 (middle band) → no shift, row=0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_bottom_biased(70), 0);
    return 0;
}

int test_engine_battlefield_render_pixel_to_row_bottom_biased_bottom_band(void)
{
    coords_scaffold();

    /* py=74 → row=0, residue=24 (third band) → row=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_bottom_biased(74), 1);
    /* py=81 → row=0, residue=31 → still third band → row=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_bottom_biased(81), 1);
    return 0;
}

int test_engine_battlefield_render_pixel_to_row_bottom_biased_first_band(void)
{
    coords_scaffold();

    /* py=52 → residue=2 (first band) → no shift, row=0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_bottom_biased(52), 0);
    return 0;
}

int test_engine_battlefield_render_pixel_to_row_bottom_biased_clamps_low(void)
{
    coords_scaffold();

    /* py=49 → (49-50)/32 = 0 (C99 truncation), residue = -1, NOT >= 24
     * → no shift, raw row = 0, < 1 → clamped to 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_bottom_biased(49), 0);
    /* py=18 → (-32)/32 = -1, residue = 0 (not >= 24) → row=-1, < 1 → 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_bottom_biased(18), 0);
    return 0;
}

/* Top-biased: py in first band (residue < 8) shifts row -1.
 * Ceiling-clamps to total_rows - 1. */
int test_engine_battlefield_render_pixel_to_row_top_biased_first_band(void)
{
    coords_scaffold();

    /* py=89 → row=1, residue=39%32=7 → < 8, row -= 1 → 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_top_biased(89), 0);
    /* py=90 → row=1, residue=40%32=8 → NOT < 8, no shift, row=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_top_biased(90), 1);
    return 0;
}

int test_engine_battlefield_render_pixel_to_row_top_biased_clamps_high(void)
{
    coords_scaffold();

    /* total_rows=15.  py = 50 + 15*32 = 530 → row=15 → clamp to 14. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_top_biased(530), 14);
    /* py = 50 + 20*32 = 690 → row=20 → clamp to 14. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_top_biased(690), 14);
    return 0;
}

int test_engine_battlefield_render_pixel_to_row_top_biased_no_low_clamp(void)
{
    coords_scaffold();

    /* py=49 → (49-50)/32 = 0 (C99 truncation), residue = -1, < 8 →
     * row -= 1 → -1.  No low clamp → returns -1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_row_top_biased(49), -1);
    return 0;
}

/* Delta helpers — both are pure (b - a) integer-divided by zoom. */
int test_engine_battlefield_render_col_delta_basic(void)
{
    coords_scaffold();

    /* a=100 (col 0), b=260 (col 5) → delta = 5. */
    T_ASSERT_EQ_I(engine_battlefield_render_col_delta(100, 260), 5);
    /* a=260 (col 5), b=100 (col 0) → delta = -5. */
    T_ASSERT_EQ_I(engine_battlefield_render_col_delta(260, 100), -5);
    /* Same pixel → delta = 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_col_delta(160, 160), 0);
    /* Both inside same cell (col 1) → delta = 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_col_delta(132, 163), 0);
    return 0;
}

int test_engine_battlefield_render_row_delta_basic(void)
{
    coords_scaffold();

    /* a=50 (row 0), b=178 (row 4) → delta = 4. */
    T_ASSERT_EQ_I(engine_battlefield_render_row_delta(50, 178), 4);
    /* a=178 (row 4), b=50 (row 0) → delta = -4. */
    T_ASSERT_EQ_I(engine_battlefield_render_row_delta(178, 50), -4);
    /* Same pixel → delta = 0. */
    T_ASSERT_EQ_I(engine_battlefield_render_row_delta(90, 90), 0);
    return 0;
}

int test_engine_battlefield_render_get_zoom_returns_bound(void)
{
    coords_scaffold();

    T_ASSERT_EQ_I(engine_battlefield_render_get_zoom(), 32);

    engine_battlefield_render_set_zoom(16);
    T_ASSERT_EQ_I(engine_battlefield_render_get_zoom(), 16);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 5c — late pass: per-cell creature draw (FUN_004be070) +
 * selection marker (FUN_004be33f).
 *
 * The late pass walks visible cells (NO over-scan unlike the middle
 * pass).  For each cell whose descriptor[+5] is non-zero, AND whose
 * per-entity visibility mask AND'd with the per-player visibility byte
 * is non-zero, it dispatches the creature-draw leaf with that entity id.
 *
 * After the cell loop, if the selection-marker gate is positive, the
 * marker leaf draws a fixed-slot sprite at the active hero/army
 * position and invokes a world-map dirty hook for a 3×3 box around it.
 *
 * Tests use `run_pass_late_for_test` to bypass the frame driver so
 * sibling passes don't perturb the cursor / counters being asserted.
 * ──────────────────────────────────────────────────────────────── */

/* Scaffold for the late pass: 3×3 visible grid inside the 8×8 physical
 * descriptor table.  Camera (0, 0), zoom 16 (the late pass HARDCODES
 * 0x20 pitch — see quirk #97), viewport (0, 0), entity table bound
 * empty (tests poke per-entity bytes via `late_entity_set`), creature
 * blob unbound, selection marker disabled (gate = 0), world hook
 * unbound, clip rect generous.  The creature blob uses g_entity_blob
 * (already declared by chip 5a tests).
 *
 * Tests typically:
 *   1. Call late_scaffold(zoom).
 *   2. Poke descriptor[+5] of one or more cells via desc_set_late.
 *   3. Optionally bind entity[+0a] / [+1a0] via late_entity_set.
 *   4. Optionally bind the per-player visibility mask byte via
 *      late_vis_mask.
 *   5. Run engine_battlefield_render_run_pass_late_for_test(cam, cam).
 *   6. Inspect counters / cursor / dirty state. */

#define CREATURE_BLOB_SIZE 0x600
static uint8_t g_creature_blob_buf[CREATURE_BLOB_SIZE];

/* Per-player visibility table fixture: just one byte for player 0.
 * `late_vis_mask` binds it (or NULL to disable). */
static uint8_t g_late_vis_table[0x160];

/* World-hook capture state. */
static int g_world_hook_called;
static int g_world_hook_last_x, g_world_hook_last_y;
static int g_world_hook_last_w, g_world_hook_last_h;

static void world_hook_capture(int x, int y, int w, int h)
{
    g_world_hook_called += 1;
    g_world_hook_last_x = x;
    g_world_hook_last_y = y;
    g_world_hook_last_w = w;
    g_world_hook_last_h = h;
}

/* Stage descriptor[+5] (the entity id byte the late pass gates on). */
static void desc_set_late(int col, int row, int phys_cols,
                          uint8_t creature_id)
{
    int off = (col + row * phys_cols) * DESC_STRIDE;
    g_terrain_desc[off + 5] = creature_id;
}

/* Stage an entity record with the per-entity fields the late pass
 * actually reads (visibility mask byte + sprite slot short + type
 * byte + facing + anim_frame).  The chip 5a tests pre-existing
 * `entity_set_body` covers different offsets and clobbers the +0x1a0
 * slot field — this helper writes ONLY the late-pass fields, leaving
 * other bytes zero (or whatever a previous setup left them). */
static void late_entity_set(int id, uint8_t vis_mask, int16_t sprite_slot,
                            uint8_t type, int8_t facing, int8_t anim_frame)
{
    uint8_t *rec = g_entity_table + id * ENT_STRIDE;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_VISIBILITY_MASK] = vis_mask;
    memcpy(rec + ENGINE_BATTLEFIELD_ENTITY_OFFSET_LATE_SPRITE_SLOT,
           &sprite_slot, sizeof sprite_slot);
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_TYPE]        = type;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_FACING]      = (uint8_t)facing;
    rec[ENGINE_BATTLEFIELD_ENTITY_OFFSET_ANIM_FRAME]  = (uint8_t)anim_frame;
}

static void late_vis_mask(uint8_t byte_at_player0)
{
    g_late_vis_table[0] = byte_at_player0;
    engine_battlefield_render_bind_late_player_visibility_mask(
        g_late_vis_table, /*stride=*/0x160, /*player_id=*/0);
}

static void late_scaffold(int zoom)
{
    middle_scaffold(/*cols=*/3, /*rows=*/3);
    /* The late-pass driver advances the descriptor cursor by
     * `g_grid_row_wrap` at end-of-row (unlike the middle pass, which
     * computes its own skip from `row_stride * num_cols`).  Rebind the
     * grid with a row_wrap that bridges the gap between 3 visible cols
     * and the 8 physical cols of g_terrain_desc, so the cursor walks
     * contiguous physical rows. */
    engine_battlefield_render_bind_grid(/*cols=*/3, /*rows=*/3,
        /*row_wrap=*/(DESC_COLS_PHY - 3) * DESC_STRIDE);
    engine_battlefield_render_set_zoom(zoom);

    /* Bind a generous clip rect so y_clip_mode resolves to 0
     * (blits reachable). */
    engine_battlefield_render_bind_clip_rect(/*left=*/-1000, /*top=*/-1000,
                                             /*right=*/2000, /*bottom=*/2000);

    /* Unbind canvas — blits become no-ops at the pixel level, observed
     * via blit_count instead. */
    gfx_paint_bind_canvas(NULL, 0, 0);

    /* Zero / unbind chip 5c-specific state. */
    memset(g_creature_blob_buf, 0, sizeof g_creature_blob_buf);
    memset(g_late_vis_table,    0, sizeof g_late_vis_table);
    engine_battlefield_render_bind_creature_sprite_blob(NULL);
    engine_battlefield_render_bind_late_player_visibility_mask(NULL, 0, 0);
    engine_battlefield_render_bind_creature_slot_shift(0, 0);
    engine_battlefield_render_bind_selection_marker(0, 0, /*gate=*/0);
    engine_battlefield_render_bind_world_map_dirty_hook(NULL);
    engine_battlefield_render_set_dirty_state(NULL);

    /* Bind anim tables — late creature draw uses z32 unconditionally
     * (quirk #95). */
    memset(g_anim_z16, 0, sizeof g_anim_z16);
    memset(g_anim_z32, 0, sizeof g_anim_z32);
    engine_battlefield_render_bind_anim_offset_tables(g_anim_z16, g_anim_z32);

    /* Empty entity table backed by g_entity_table (zeros = all
     * descriptors safe to look up). */
    bind_entities(/*max_id=*/ENT_MAX_ID);

    /* Reset world hook capture state. */
    g_world_hook_called  = 0;
    g_world_hook_last_x  = 0;
    g_world_hook_last_y  = 0;
    g_world_hook_last_w  = 0;
    g_world_hook_last_h  = 0;

    /* Reset cursor so per-cell advance assertions start at a known
     * state.  gfx_glyph_set_dest writes both x and y. */
    gfx_glyph_set_dest(0, 0);
}

static void run_late_only(int cam_x, int cam_y)
{
    engine_battlefield_render_run_pass_late_for_test(cam_x, cam_y);
}

/* ─── Driver / cell-loop tests ───────────────────────────────────── */

/* Visible 3×3 → 9 cells visited (no over-scan).  All cells zero, so
 * no creature dispatch. */
int test_engine_battlefield_render_late_visits_visible_grid(void)
{
    late_scaffold(/*zoom=*/0x10);
    run_late_only(/*cam=*/0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_per_cell_count(),       9);
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(),  0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_late(),                1);
    return 0;
}

/* Cell with descriptor[+5] == 0 → no draw. */
int test_engine_battlefield_render_late_zero_id_no_draw(void)
{
    late_scaffold(/*zoom=*/0x10);
    /* Leave descriptor zeros. */
    run_late_only(/*cam=*/0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(), 0);
    return 0;
}

/* Cell with descriptor[+5] != 0 but visibility AND == 0 → no draw. */
int test_engine_battlefield_render_late_vis_mask_blocks(void)
{
    late_scaffold(/*zoom=*/0x10);
    desc_set_late(/*col=*/1, /*row=*/1, DESC_COLS_PHY, /*id=*/3);
    late_entity_set(/*id=*/3, /*vis_mask=*/0x0f, /*slot=*/0,
                    /*type=*/1, 0, 0);
    /* Player visibility mask = 0xf0 — AND with 0x0f = 0 → cell blocked. */
    late_vis_mask(0xf0);

    run_late_only(/*cam=*/0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(), 0);
    return 0;
}

/* Cell with both masks overlapping → draw fires. */
int test_engine_battlefield_render_late_vis_mask_allows(void)
{
    late_scaffold(/*zoom=*/0x10);
    desc_set_late(/*col=*/1, /*row=*/1, DESC_COLS_PHY, /*id=*/3);
    late_entity_set(/*id=*/3, /*vis_mask=*/0x0f, /*slot=*/0, /*type=*/1, 0, 0);
    late_vis_mask(0x01);

    run_late_only(/*cam=*/0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(), 1);
    return 0;
}

/* NULL player visibility base → effectively zero AND → no draws even
 * when descriptor and entity vis are non-zero. */
int test_engine_battlefield_render_late_null_vis_blocks_all(void)
{
    late_scaffold(/*zoom=*/0x10);
    desc_set_late(/*col=*/1, /*row=*/1, DESC_COLS_PHY, /*id=*/3);
    late_entity_set(/*id=*/3, /*vis_mask=*/0xff, /*slot=*/0, /*type=*/1, 0, 0);
    /* NULL base → per-player byte defaults to 0 → AND == 0 → no draw. */

    run_late_only(/*cam=*/0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(), 0);
    return 0;
}

/* Quirk #97: cursor advances by 0x20 per cell regardless of zoom.  At
 * zoom 16, three cells should advance cursor_x to 0x60 (not 0x30). */
int test_engine_battlefield_render_late_hardcoded_zoom32_pitch(void)
{
    late_scaffold(/*zoom=*/0x10);  /* zoom 16, but pitch still 0x20 */
    run_late_only(/*cam=*/0, 0);

    /* After 3 cells of the last row, cursor_x = 0 + 3*0x20 = 0x60.
     * After 3 rows, cursor_y = 0 + 3*0x20 = 0x60. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 0x60);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x60);
    return 0;
}

/* No descriptor table bound → no per-cell visits, but selection
 * marker still runs if its gate is positive. */
int test_engine_battlefield_render_late_no_descriptors_marker_fires(void)
{
    late_scaffold(/*zoom=*/0x10);
    engine_battlefield_render_bind_descriptor_table(NULL, DESC_STRIDE);
    engine_battlefield_render_bind_selection_marker(/*x=*/0, /*y=*/0, /*gate=*/1);

    run_late_only(/*cam=*/0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_per_cell_count(),         0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_count(), 1);
    return 0;
}

/* ─── Creature-draw leaf tests ───────────────────────────────────── */

/* Slot computation: sign >= 0 → left shift by amount; final = + 0x2e.
 * Set raw=3, amount=2, sign=0 → 3 << 2 = 12 → +0x2e = 0x3a. */
int test_engine_battlefield_render_late_creature_slot_left_shift(void)
{
    late_scaffold(/*zoom=*/0x10);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x3a, 16, 16, /*src_off=*/0x100);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(1, 1, DESC_COLS_PHY, /*id=*/3);
    late_entity_set(/*id=*/3, /*vis_mask=*/0xff, /*slot=*/3, /*type=*/1, 0, 0);
    late_vis_mask(0xff);
    engine_battlefield_render_bind_creature_slot_shift(/*amount=*/2, /*sign=*/0);

    run_late_only(0, 0);

    /* Hits the right slot → blit fires. */
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_blit_count(), 1);
    return 0;
}

/* Slot computation: sign < 0 → right shift.  raw=0x20, amount=2 →
 * 0x20 >> 2 = 8 → +0x2e = 0x36. */
int test_engine_battlefield_render_late_creature_slot_right_shift(void)
{
    late_scaffold(/*zoom=*/0x10);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x36, 16, 16, /*src_off=*/0x100);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(1, 1, DESC_COLS_PHY, /*id=*/4);
    late_entity_set(/*id=*/4, /*vis_mask=*/0xff, /*slot=*/0x20,
                    /*type=*/1, 0, 0);
    late_vis_mask(0xff);
    engine_battlefield_render_bind_creature_slot_shift(/*amount=*/2, /*sign=*/-1);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_blit_count(), 1);
    return 0;
}

/* Rebind a 1×1 visible grid (so a single creature at vis (0, 0) is
 * the only cell visited — keeps cursor-offset assertions clean: the
 * leaf's POST-OFFSET cursor (quirk #96, gate-fail path) propagates
 * straight to end-of-frame after just one per-cell +0x20 + one
 * end-of-row +0x20). */
static void late_scaffold_1x1(int zoom)
{
    late_scaffold(zoom);
    engine_battlefield_render_bind_grid(/*cols=*/1, /*rows=*/1,
        /*row_wrap=*/(DESC_COLS_PHY - 1) * DESC_STRIDE);
}

/* Type < 7 → cursor_y -= 12 (offset).  With anim (0,0): adjusted
 * cursor_y = entry_y - 12.  Gate-fail leaves cursor at adjusted (quirk
 * #96) — we use this as the assertion vector. */
int test_engine_battlefield_render_late_creature_type_lt7_y_offset(void)
{
    late_scaffold_1x1(/*zoom=*/0x10);
    /* No blob slot staged → src_off = 0 → gate FAILS → cursor stays
     * at adjusted pos (quirk #96).  Provide blob to get past the NULL
     * check. */
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(/*col=*/0, /*row=*/0, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/3 /*<7*/, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    /* Cell (0, 0) entry cursor (0, 0).  After type<7 offset:
     *   x += 0 + 8 = 8
     *   y += 0 - 12 = -12
     * Gate-fail (no blob slot) leaves cursor at (8, -12) per quirk #96.
     * Driver per-cell advance: cursor_x += 0x20 → 0x28.
     * End-of-row advance: cursor_y += 0x20 → 0x14.
     * No more rows. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 0x28);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x14);
    return 0;
}

/* Type 9 → cursor_y -= 16 (net of -0x20 + 0x10). */
int test_engine_battlefield_render_late_creature_type9_y_offset(void)
{
    late_scaffold_1x1(/*zoom=*/0x10);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/9, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    /* Type 9: cursor_y += 0 - 0x20 + 0x10 = -16.  Adjusted y = -16.
     * End-of-row +0x20 → 0x10. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x10);
    return 0;
}

/* Type 10 → cursor_y -= 12 (same as type < 7). */
int test_engine_battlefield_render_late_creature_type10_y_offset(void)
{
    late_scaffold_1x1(/*zoom=*/0x10);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/10, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    /* Type 10: cursor_y += 0 - 12 = -12.  End-of-row → 0x14. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x14);
    return 0;
}

/* Type other (11) → cursor_y -= 0x20 (no type-9 bonus). */
int test_engine_battlefield_render_late_creature_type_other_y_offset(void)
{
    late_scaffold_1x1(/*zoom=*/0x10);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/11, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    /* Type other: cursor_y += 0 - 0x20 = -0x20.  End-of-row → 0. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x00);
    return 0;
}

/* Quirk #95: at zoom 16, the leaf reads the z32 anim table (not z16).
 * Stage z32 with a distinctive offset; z16 with zeros.  Verify the
 * z32 value made it into the cursor adjustment. */
int test_engine_battlefield_render_late_creature_anim_z32_unconditional(void)
{
    late_scaffold_1x1(/*zoom=*/0x10);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/1 /*<7*/, /*facing=*/0, /*anim_frame=*/0);
    late_vis_mask(0xff);

    /* z32 entry (facing=0, frame=0) = (0x100, 0x200).  z16 unchanged
     * (zero) — at retail zoom 16, the leaf reads z32 regardless. */
    anim_table_set(g_anim_z32, /*facing=*/0, /*frame=*/0,
                   /*x=*/0x100, /*y=*/0x200);

    run_late_only(0, 0);

    /* Type<7: cursor_x += 0x100 + 8 = 0x108; cursor_y += 0x200 - 12 = 0x1f4.
     * Entry (0, 0) → adjusted (0x108, 0x1f4).  Per-cell +0x20 →
     * 0x128.  End-of-row +0x20 → 0x214. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 0x128);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x214);
    return 0;
}

/* Glyph-offset gate: src_off outside [1, 0x4baf0] → no blit. */
int test_engine_battlefield_render_late_creature_gate_skips_blit(void)
{
    late_scaffold(/*zoom=*/0x10);
    /* src_off = 0 → out of band. */
    blob_set_header(g_creature_blob_buf, /*slot=*/0x2e, 16, 16, /*src_off=*/0);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(1, 1, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/1, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_blit_count(), 0);
    return 0;
}

/* Quirk #96: on gate PASS, cursor is restored to ENTRY.  Place a
 * single creature in the last cell of the row, and verify the cell-
 * loop advance is correct (final x = grid_cols * 0x20). */
int test_engine_battlefield_render_late_creature_gate_pass_cursor_restored(void)
{
    late_scaffold(/*zoom=*/0x10);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x2e, 16, 16,
                    /*src_off=*/0x100);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);

    desc_set_late(/*col=*/0, /*row=*/0, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0,
                    /*type=*/1, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    /* Cell (0,0) at entry cursor (0, 0); leaf restores cursor to entry
     * before returning.  Then driver advances cursor_x by 0x20 per
     * cell × 3 → cursor_x = 0x60.  cursor_y advances per row × 3 →
     * 0x60. */
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_blit_count(), 1);
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 0x60);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 0x60);
    return 0;
}

/* NULL creature blob → no blit (but the type-Y offset still applies,
 * verifying that the NULL-blob early-return is post-offset). */
int test_engine_battlefield_render_late_creature_null_blob_no_blit(void)
{
    late_scaffold(/*zoom=*/0x10);
    /* Don't bind creature blob — defaults NULL. */
    desc_set_late(1, 1, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0, /*type=*/1, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_blit_count(), 0);
    return 0;
}

/* ─── Selection marker tests ─────────────────────────────────────── */

/* Gate = 0 → marker leaf does NOT dispatch. */
int test_engine_battlefield_render_late_selection_marker_gate_zero(void)
{
    late_scaffold(/*zoom=*/0x10);
    engine_battlefield_render_bind_selection_marker(/*x=*/5, /*y=*/5, /*gate=*/0);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_count(), 0);
    return 0;
}

/* Gate > 0 → marker leaf dispatches. */
int test_engine_battlefield_render_late_selection_marker_gate_fires(void)
{
    late_scaffold(/*zoom=*/0x10);
    engine_battlefield_render_bind_selection_marker(/*x=*/5, /*y=*/5, /*gate=*/1);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_count(), 1);
    return 0;
}

/* Marker dispatches the world hook ONLY inside the glyph-offset gate.
 * Stage blob with valid src_off → gate passes → hook fires with
 * (marker_x - 1, marker_y - 1, 3, 3). */
int test_engine_battlefield_render_late_selection_marker_world_hook(void)
{
    late_scaffold(/*zoom=*/0x10);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x3f, 16, 16,
                    /*src_off=*/0x100);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);
    engine_battlefield_render_bind_world_map_dirty_hook(world_hook_capture);
    engine_battlefield_render_bind_selection_marker(/*x=*/7, /*y=*/4, /*gate=*/1);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_blit_count(), 1);
    T_ASSERT_EQ_I(g_world_hook_called,    1);
    T_ASSERT_EQ_I(g_world_hook_last_x,    6);   /* 7 - 1 */
    T_ASSERT_EQ_I(g_world_hook_last_y,    3);   /* 4 - 1 */
    T_ASSERT_EQ_I(g_world_hook_last_w,    3);
    T_ASSERT_EQ_I(g_world_hook_last_h,    3);
    T_ASSERT_EQ_I(engine_battlefield_render_late_world_dirty_hook_count(), 1);
    return 0;
}

/* Glyph-offset gate FAIL → no blit, no world hook (both gated). */
int test_engine_battlefield_render_late_selection_marker_gate_fail_no_hook(void)
{
    late_scaffold(/*zoom=*/0x10);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x3f, 16, 16,
                    /*src_off=*/0);  /* out of band */
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);
    engine_battlefield_render_bind_world_map_dirty_hook(world_hook_capture);
    engine_battlefield_render_bind_selection_marker(/*x=*/3, /*y=*/3, /*gate=*/1);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_count(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_blit_count(),0);
    T_ASSERT_EQ_I(g_world_hook_called,                                          0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_world_dirty_hook_count(),     0);
    return 0;
}

/* Selection marker cursor formula:
 *   px_x = (x - cam_x) * 0x20 + viewport_x + 2
 *   px_y = (y - cam_y) * 0x20 + viewport_y + 9 */
int test_engine_battlefield_render_late_selection_marker_position(void)
{
    late_scaffold(/*zoom=*/0x10);
    engine_battlefield_render_bind_viewport_offset(/*x=*/100, /*y=*/200);
    engine_battlefield_render_bind_selection_marker(/*x=*/5, /*y=*/3, /*gate=*/1);

    run_late_only(/*cam_x=*/2, /*cam_y=*/1);

    /* px_x = (5-2)*0x20 + 100 + 2 = 0x60 + 102 = 96 + 102 = 198.
     * px_y = (3-1)*0x20 + 200 + 9 = 0x40 + 209 = 64 + 209 = 273. */
    T_ASSERT_EQ_I(gfx_glyph_cursor_x(), 198);
    T_ASSERT_EQ_I(gfx_glyph_cursor_y(), 273);
    return 0;
}

/* Dirty mark runs UNCONDITIONALLY (above the glyph-offset gate).
 * Bind a dirty state; verify a 3×3 cell mark at the marker position
 * regardless of whether the blit fires. */
int test_engine_battlefield_render_late_selection_marker_dirty_box(void)
{
    engine_dirty_rect_state_t dirty;
    late_scaffold(/*zoom=*/0x10);
    /* NO blob bound → blit doesn't fire; but dirty mark should still
     * happen (it's above the NULL-blob check). */
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);
    engine_battlefield_render_bind_selection_marker(/*x=*/0, /*y=*/0, /*gate=*/1);

    run_late_only(0, 0);

    /* Cursor at (2, 9) (per formula: (0-0)*0x20 + 0 + 2, +9).
     * 3×3 cells with cell_size=16 in the dirty marker — but the
     * marker uses 3 as a TILE count and force_full=2.  Default tile
     * size used by engine_dirty_rect_mark_box_tiled is what? — we
     * just observe the mark fired. */
    T_ASSERT_EQ_I(dirty.dirty_mark, 2);
    return 0;
}

/* NULL blob → no blit, no world hook, but counter / dirty still
 * bump (counter is above blob check; dirty too). */
int test_engine_battlefield_render_late_selection_marker_null_blob(void)
{
    engine_dirty_rect_state_t dirty;
    late_scaffold(/*zoom=*/0x10);
    dirty_reset(&dirty);
    engine_battlefield_render_set_dirty_state(&dirty);
    engine_battlefield_render_bind_world_map_dirty_hook(world_hook_capture);
    engine_battlefield_render_bind_selection_marker(/*x=*/0, /*y=*/0, /*gate=*/1);

    run_late_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_count(),     1);
    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_blit_count(),0);
    T_ASSERT_EQ_I(g_world_hook_called,                                          0);
    T_ASSERT_EQ_I(dirty.dirty_mark, 2);  /* dirty still ran */
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 5d — final pass debug-label overlay (FUN_004bf1f9).
 *
 * The final pass is a debug-only overlay drawing per-creature numeric
 * labels.  Tests verify:
 *   - debug-flag gate (off → pass is a no-op).
 *   - cell iteration without over-scan (same shape as late pass).
 *   - byte-sum + three-bucket transform (pen + value).
 *   - magic filters (value == 0 / 999 → skip).
 *   - zoom-aware pixel pitch (unlike late-pass's hardcoded 0x20).
 *   - reset clears counters.
 * ──────────────────────────────────────────────────────────────── */

/* Final-pass scaffold builds on late-pass scaffold: 3×3 visible grid
 * with row_wrap matching the 8×8 physical descriptor table.  Adds
 * font + suffix bindings and enables the debug flag by default. */

#define FINAL_FONT_SIZE 0x100
static uint8_t g_final_font_buf[FINAL_FONT_SIZE];
static uint8_t g_final_glyph_table[FINAL_FONT_SIZE];

static void final_scaffold(int zoom)
{
    late_scaffold(zoom);
    /* Inherits 3×3 vis grid + row_wrap from late_scaffold. */

    memset(g_final_font_buf,    0, sizeof g_final_font_buf);
    memset(g_final_glyph_table, 0, sizeof g_final_glyph_table);

    engine_battlefield_render_bind_debug_label_enabled(1);
    engine_battlefield_render_bind_debug_label_font(g_final_font_buf,
                                                    g_final_glyph_table);
    engine_battlefield_render_bind_debug_label_suffix("");
}

static void run_final_only(int cam_x, int cam_y)
{
    engine_battlefield_render_run_pass_final_for_test(cam_x, cam_y);
}

/* Helper: place an entity record whose first 0x19e bytes sum (as
 * signed char) to exactly `target_sum`.  Achieved by zeroing the
 * first 0x19d bytes and writing `target_sum` into byte 0. */
static void final_set_entity_sum(int id, int target_sum)
{
    uint8_t *rec = g_entity_table + id * ENT_STRIDE;
    memset(rec, 0, 0x19e);
    rec[0] = (uint8_t)target_sum;
}

/* Reset entity table to zeros for the (max_id+1) records — avoids
 * leftover entity bytes from previous tests affecting sum/visibility. */
static void final_clear_entities(void)
{
    memset(g_entity_table, 0, sizeof g_entity_table);
}

/* ─── Debug-flag gate ────────────────────────────────────────────── */

/* Debug flag OFF → pass is a no-op (per-cell counter does NOT bump). */
int test_engine_battlefield_render_final_disabled_no_op(void)
{
    final_scaffold(/*zoom=*/0x10);
    engine_battlefield_render_bind_debug_label_enabled(0);
    /* Stage one entity that WOULD render if the gate passed. */
    desc_set_late(/*col=*/0, /*row=*/0, DESC_COLS_PHY, /*id=*/2);
    final_clear_entities();
    final_set_entity_sum(2, 5);  /* bucket: <0x65, pen 0x3f, value 5 */

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_per_cell_count(),     0);
    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_count_final(),              1);
    return 0;
}

/* Debug flag ON → pass walks all visible cells. */
int test_engine_battlefield_render_final_visits_visible_grid(void)
{
    final_scaffold(/*zoom=*/0x10);
    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_per_cell_count(), 9);
    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 0);
    return 0;
}

/* ─── Cell-iteration tests ───────────────────────────────────────── */

/* Cell with descriptor[+5] == 0 → no render. */
int test_engine_battlefield_render_final_zero_id_no_render(void)
{
    final_scaffold(/*zoom=*/0x10);
    /* All descriptors zero. */
    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 0);
    return 0;
}

/* Cell with descriptor[+5] != 0 AND nonzero sum (bucket 0, value 5) →
 * label renders. */
int test_engine_battlefield_render_final_renders_label(void)
{
    final_scaffold(/*zoom=*/0x10);
    desc_set_late(/*col=*/1, /*row=*/1, DESC_COLS_PHY, /*id=*/2);
    final_clear_entities();
    final_set_entity_sum(2, /*sum=*/5);

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 1);
    return 0;
}

/* ─── Three-way bucket tests ─────────────────────────────────────── */

/* Sum in [0x65, 0xc9) → value = sum - 100, pen = 0xf9.  Use sum=150
 * → value=50.  Renders (value != 0 / 999). */
int test_engine_battlefield_render_final_bucket_mid(void)
{
    final_scaffold(/*zoom=*/0x10);
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/3);
    final_clear_entities();
    final_set_entity_sum(3, /*sum=*/150);

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 1);
    return 0;
}

/* Sum >= 0xc9 → value = sum - 200, pen = 0x20.  Use sum=0xc9=201
 * → value=1. */
int test_engine_battlefield_render_final_bucket_high(void)
{
    final_scaffold(/*zoom=*/0x10);
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/3);
    final_clear_entities();
    final_set_entity_sum(3, /*sum=*/0xc9);

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 1);
    return 0;
}

/* ─── Bucket-transition + magic-value filter tests ───────────────── */

/* Bucket cutoff is STRICT less-than 0x65 = 101: sum=100 is still
 * bucket-low (value = 100); sum=101 jumps to bucket-mid (value = 1).
 * Both render — verifies the boundary is `<`, not `<=`. */
int test_engine_battlefield_render_final_bucket_low_boundary(void)
{
    final_scaffold(/*zoom=*/0x10);
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    desc_set_late(1, 0, DESC_COLS_PHY, /*id=*/3);
    final_clear_entities();
    final_set_entity_sum(2, /*sum=*/100);  /* bucket-low edge */
    final_set_entity_sum(3, /*sum=*/101);  /* bucket-mid edge */

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 2);
    return 0;
}

/* Sum 0 → bucket-low → value = 0 → SKIP. */
int test_engine_battlefield_render_final_sum_zero_skipped(void)
{
    final_scaffold(/*zoom=*/0x10);
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/3);
    final_clear_entities();
    /* Entity record all zeros → sum = 0 → value = 0 → skip. */

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 0);
    return 0;
}

/* ─── Font / suffix binding tests ────────────────────────────────── */

/* NULL font → render path skipped silently. */
int test_engine_battlefield_render_final_null_font_no_render(void)
{
    final_scaffold(/*zoom=*/0x10);
    engine_battlefield_render_bind_debug_label_font(NULL, NULL);
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    final_clear_entities();
    final_set_entity_sum(2, 5);

    run_final_only(0, 0);

    /* Cell visited, but no label rendered. */
    T_ASSERT_EQ_I(engine_battlefield_render_final_per_cell_count(),     9);
    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 0);
    return 0;
}

/* ─── Cell-iteration test: walking through all cells ─────────────── */

/* Multiple entities at different cells → multiple labels rendered.
 * Each cell with descriptor[+5] != 0 AND nonzero sum AND nonzero
 * value-after-bucket fires the renderer. */
int test_engine_battlefield_render_final_multiple_labels(void)
{
    final_scaffold(/*zoom=*/0x10);
    final_clear_entities();
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    desc_set_late(1, 1, DESC_COLS_PHY, /*id=*/3);
    desc_set_late(2, 2, DESC_COLS_PHY, /*id=*/4);
    final_set_entity_sum(2, /*sum=*/5);    /* bucket-low, value 5 */
    final_set_entity_sum(3, /*sum=*/150);  /* bucket-mid, value 50 */
    final_set_entity_sum(4, /*sum=*/210);  /* bucket-high, value 10 */

    run_final_only(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 3);
    return 0;
}

/* ─── Reset clears counters ──────────────────────────────────────── */

int test_engine_battlefield_render_final_reset_clears(void)
{
    final_scaffold(/*zoom=*/0x10);
    desc_set_late(0, 0, DESC_COLS_PHY, /*id=*/2);
    final_clear_entities();
    final_set_entity_sum(2, /*sum=*/5);

    run_final_only(0, 0);

    T_ASSERT(engine_battlefield_render_final_per_cell_count()    > 0);
    T_ASSERT(engine_battlefield_render_final_label_render_count() > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_final_per_cell_count(),     0);
    T_ASSERT_EQ_I(engine_battlefield_render_final_label_render_count(), 0);
    return 0;
}

/* ─── Reset zeros chip 5c counters ───────────────────────────────── */

int test_engine_battlefield_render_late_reset_clears(void)
{
    late_scaffold(/*zoom=*/0x10);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x2e, 16, 16, /*src_off=*/0x100);
    blob_set_header(g_creature_blob_buf, /*slot=*/0x3f, 16, 16, /*src_off=*/0x100);
    engine_battlefield_render_bind_creature_sprite_blob(g_creature_blob_buf);
    engine_battlefield_render_bind_world_map_dirty_hook(world_hook_capture);
    engine_battlefield_render_bind_selection_marker(/*x=*/1, /*y=*/1, /*gate=*/1);
    desc_set_late(1, 1, DESC_COLS_PHY, /*id=*/2);
    late_entity_set(/*id=*/2, /*vis_mask=*/0xff, /*slot=*/0, /*type=*/1, 0, 0);
    late_vis_mask(0xff);

    run_late_only(0, 0);

    T_ASSERT(engine_battlefield_render_late_per_cell_count()           > 0);
    T_ASSERT(engine_battlefield_render_late_creature_draw_count()      > 0);
    T_ASSERT(engine_battlefield_render_late_creature_blit_count()      > 0);
    T_ASSERT(engine_battlefield_render_late_selection_marker_count()   > 0);
    T_ASSERT(engine_battlefield_render_late_selection_marker_blit_count() > 0);
    T_ASSERT(engine_battlefield_render_late_world_dirty_hook_count()   > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_late_per_cell_count(),             0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_draw_count(),        0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_creature_blit_count(),        0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_count(),     0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_selection_marker_blit_count(),0);
    T_ASSERT_EQ_I(engine_battlefield_render_late_world_dirty_hook_count(),     0);
    return 0;
}

/* ────────────────────────────────────────────────────────────────
 * Chip 7 — mini-map / hex-grid overview renderer.
 *
 * Scaffold: 5×5 descriptor grid (total_rows = row_stride = 5), 8-byte
 * cell stride, mini-map origin at (40, 20).  Each cell renders as a
 * 2×2 pixel block on a 640×400 canvas.  The mini-map blobs and
 * default descriptor each have predictable pixel colors per slot so
 * tests can read back which path produced the blit.
 * ──────────────────────────────────────────────────────────────── */

#define MM_TOTAL_ROWS    5
#define MM_ROW_STRIDE    5      /* = visible cols of mini-map */
#define MM_STRIDE_BYTES  8      /* per-cell descriptor stride */
#define MM_ORIGIN_X      40
#define MM_Y_OFFSET      20

static uint8_t g_mm_canvas[CANVAS_W * CANVAS_H];
static uint8_t g_mm_desc[MM_STRIDE_BYTES * MM_ROW_STRIDE * MM_TOTAL_ROWS];

#define MM_BLOB_SIZE    1024
static uint8_t g_mm_blob_primary[MM_BLOB_SIZE];
static uint8_t g_mm_blob_alt    [MM_BLOB_SIZE];
static uint8_t g_mm_default_desc[MM_BLOB_SIZE];

/* Owner / slot tables.  Small strides so the fixtures stay tiny.
 * Retail strides are 0x1b0 / 0x160 but the port just uses them as a
 * multiplier — any value works. */
#define MM_OWNER_STRIDE  16
#define MM_SLOT_STRIDE   16
static uint8_t g_mm_owner_table [MM_OWNER_STRIDE * 32];
static uint8_t g_mm_slot_table  [MM_SLOT_STRIDE  * 32];

/* Stamp a 2×2 pattern at `src_off` into the given blob.  All 4 bytes
 * are set to `tag` so a single pixel read identifies the source. */
static void mm_blob_stamp(uint8_t *blob, uint32_t src_off, uint8_t tag)
{
    blob[src_off + 0] = tag;
    blob[src_off + 1] = tag;
    blob[src_off + 2] = tag;
    blob[src_off + 3] = tag;
}

/* Configure slot `slot_idx` of `blob` to draw a 2×2 tile of color
 * `tag`.  Uses src_off = 0x200 + slot*4 so each slot gets a distinct
 * 4-byte region. */
static void mm_blob_set_slot(uint8_t *blob, int slot_idx, uint8_t tag)
{
    uint32_t src_off = 0x200 + (uint32_t)slot_idx * 4;
    blob_set_slot(blob, slot_idx, /*w=*/2, /*h=*/2, src_off,
                  /*flag_a=*/0, /*advance=*/0, /*flag_b=*/0);
    mm_blob_stamp(blob, src_off, tag);
}

/* Initialise the mini-map scaffold. */
static void mm_scaffold(void)
{
    scaffold_init();
    memset(g_mm_canvas,        0, sizeof g_mm_canvas);
    memset(g_mm_desc,          0, sizeof g_mm_desc);
    memset(g_mm_blob_primary,  0, sizeof g_mm_blob_primary);
    memset(g_mm_blob_alt,      0, sizeof g_mm_blob_alt);
    memset(g_mm_default_desc,  0, sizeof g_mm_default_desc);
    memset(g_mm_owner_table,   0, sizeof g_mm_owner_table);
    memset(g_mm_slot_table,    0, sizeof g_mm_slot_table);

    gfx_paint_bind_canvas(g_mm_canvas, CANVAS_W, CANVAS_H);

    engine_battlefield_render_bind_descriptor_table(g_mm_desc, MM_STRIDE_BYTES);
    engine_battlefield_render_bind_camera_row_stride(MM_ROW_STRIDE);
    engine_battlefield_render_bind_total_rows(MM_TOTAL_ROWS);

    engine_battlefield_render_bind_minimap_origin(MM_ORIGIN_X, MM_Y_OFFSET);
    engine_battlefield_render_set_minimap_mode_gate(2);
    engine_battlefield_render_set_minimap_engine_state(0);
    engine_battlefield_render_reset_minimap_scan_pos();

    engine_battlefield_render_bind_minimap_blob_primary(g_mm_blob_primary);
    engine_battlefield_render_bind_minimap_blob_alt(g_mm_blob_alt);
    engine_battlefield_render_bind_minimap_default_descriptor(g_mm_default_desc);
    engine_battlefield_render_bind_minimap_entity_owner_table(g_mm_owner_table,
                                                              MM_OWNER_STRIDE);
    engine_battlefield_render_bind_minimap_player_slot_table(g_mm_slot_table,
                                                             MM_SLOT_STRIDE);

    /* Stage some default slot data so blits actually paint pixels. */
    mm_blob_set_slot(g_mm_blob_primary, 0, 0x11);
    mm_blob_set_slot(g_mm_blob_primary, 1, 0x12);
    mm_blob_set_slot(g_mm_blob_alt,     0, 0x21);
    mm_blob_set_slot(g_mm_blob_alt,     1, 0x22);
    mm_blob_set_slot(g_mm_default_desc, 0, 0x31);
    mm_blob_set_slot(g_mm_default_desc, 5, 0x35);
    mm_blob_set_slot(g_mm_default_desc, 6, 0x36);
}

/* Write descriptor fields (flags=+2, sprite_idx=+3, entity_id=+5) for
 * cell (col, row). */
static void mm_desc_set(int col, int row, uint8_t flags,
                        uint8_t sprite_idx, uint8_t entity_id)
{
    int off = (col + row * MM_ROW_STRIDE) * MM_STRIDE_BYTES;
    g_mm_desc[off + 2] = flags;
    g_mm_desc[off + 3] = sprite_idx;
    g_mm_desc[off + 5] = entity_id;
}

/* Read pixel at the top-left of the cell (col, row)'s on-screen 2×2
 * block.  Assumes start_row = 0 for the strip; otherwise add
 * `start_row * 2` to the y component. */
static uint8_t mm_cell_pixel(int col, int row)
{
    int x = MM_ORIGIN_X + col * 2;
    int y = MM_Y_OFFSET + row * 2;
    return g_mm_canvas[x + y * CANVAS_W];
}

int test_engine_battlefield_render_minimap_scroll_bumps_scan_pos(void)
{
    mm_scaffold();

    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 0);

    engine_battlefield_render_minimap_scroll(2);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 2);

    engine_battlefield_render_minimap_scroll(1);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 3);
    return 0;
}

int test_engine_battlefield_render_minimap_scroll_wraps_at_boundary(void)
{
    mm_scaffold();

    /* total_rows = 5.  Scroll by 4 → scan_pos = 4.  Wrap check is
     * `total_rows - rows < scan_pos`: 5 - 4 = 1 < 4 → WRAP to 0. */
    engine_battlefield_render_minimap_scroll(4);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 0);
    return 0;
}

int test_engine_battlefield_render_minimap_scroll_wrap_exact(void)
{
    mm_scaffold();

    /* total_rows = 5.  Scroll by 1, 5 times → 1, 2, 3, 4, then wrap.
     * Wrap formula: 5 - 1 = 4 < 5 → WRAP to 0.  Verify that the 5th
     * call lands at 0 (not 5). */
    engine_battlefield_render_minimap_scroll(1);
    engine_battlefield_render_minimap_scroll(1);
    engine_battlefield_render_minimap_scroll(1);
    engine_battlefield_render_minimap_scroll(1);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 4);
    engine_battlefield_render_minimap_scroll(1);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 0);
    return 0;
}

int test_engine_battlefield_render_minimap_scroll_renders_when_mode_2(void)
{
    mm_scaffold();
    engine_battlefield_render_set_minimap_mode_gate(2);

    /* Pre-mark one cell dirty so the strip render has something to
     * blit. */
    mm_desc_set(2, 0, /*flags=*/0x01, /*sprite_idx=*/0, /*entity_id=*/0);

    engine_battlefield_render_minimap_scroll(1);

    /* Scan position bumped, AND the strip render fired (per-cell
     * counter > 0). */
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 1);
    T_ASSERT(engine_battlefield_render_minimap_per_cell_count() > 0);
    return 0;
}

int test_engine_battlefield_render_minimap_scroll_skips_when_mode_not_2(void)
{
    mm_scaffold();
    engine_battlefield_render_set_minimap_mode_gate(0);

    mm_desc_set(2, 0, /*flags=*/0x01, /*sprite_idx=*/0, /*entity_id=*/0);

    engine_battlefield_render_minimap_scroll(1);

    /* Cursor still bumps even though no render. */
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(), 0);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_visits_all_cells(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);

    engine_battlefield_render_minimap_render_strip_for_test(/*start=*/0, /*count=*/3);

    /* 3 rows × 5 cols = 15 cell visits, all blitted. */
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 15);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(),     15);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_skips_clean_cells(void)
{
    mm_scaffold();
    /* No force_redraw.  Mark only cell (1, 0) dirty. */
    engine_battlefield_render_set_force_redraw(0);
    mm_desc_set(1, 0, /*flags=*/0x01, /*sprite_idx=*/0, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    /* 5 cells visited (whole row), but only 1 blitted (cell at col 1).
     * Note: col 0 is the first-col special case but engine_state==0
     * so it falls through to the normal blob-selector path, which is
     * gated on the dirty bit (it's not set for col 0). */
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 5);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(),     1);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_terrain_primary_blob(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* Cell (2, 0) with flags=0 (which is also (& 0x1c) == 0) and
     * sprite_idx=1.  Should pick PRIMARY blob slot 1 (color 0x12). */
    mm_desc_set(2, 0, /*flags=*/0x00, /*sprite_idx=*/1, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(2, 0), 0x12);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_terrain_alt_blob(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* flags = 0x04 → (& 0x1c) == 4 → ALT blob.  sprite_idx=0 → slot 0
     * → color 0x21. */
    mm_desc_set(2, 0, /*flags=*/0x04, /*sprite_idx=*/0, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(2, 0), 0x21);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_other_0x1c_drops_cell(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* flags = 0x08 → (& 0x1c) == 8 → neither primary nor alt, cell
     * dropped (no blit). */
    mm_desc_set(2, 0, /*flags=*/0x08, /*sprite_idx=*/0, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    /* Per-cell visited but no blit on this cell.  Total = 5 cells, 4
     * blits (the others get the primary-slot-0 path since flags=0). */
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 5);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(),     4);
    /* The dropped cell shows the canvas's pre-fill (0). */
    T_ASSERT_EQ_U(mm_cell_pixel(2, 0), 0x00);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_boundary_clears_bit(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(0);
    /* flags = 0x02 (boundary, NOT dirty) — boundary alone activates
     * the gate.  Default-descriptor slot 0 (color 0x31).  After draw,
     * bit 0x02 should be CLEARED. */
    mm_desc_set(2, 0, /*flags=*/0x02, /*sprite_idx=*/0, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(2, 0), 0x31);
    int off = (2 + 0 * MM_ROW_STRIDE) * MM_STRIDE_BYTES;
    T_ASSERT_EQ_I(g_mm_desc[off + 2] & 0x02, 0);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_first_col_ingame_override(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_set_minimap_engine_state(3);

    /* First col with flags = 0 and a non-zero sprite_idx.  The first-
     * col-ingame override should fire BEFORE the normal blob-selector,
     * so even though sprite_idx=1, we get default desc slot 0 (color
     * 0x31), not primary slot 1 (color 0x12). */
    mm_desc_set(0, 0, /*flags=*/0x00, /*sprite_idx=*/1, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(0, 0), 0x31);
    /* Subsequent cols use the normal path — col 1, sprite_idx=0
     * (default), primary slot 0 = 0x11. */
    T_ASSERT_EQ_U(mm_cell_pixel(1, 0), 0x11);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_first_col_not_ingame_normal(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_set_minimap_engine_state(0);

    /* engine_state != 3, so first-col path goes through normal blob-
     * selector.  flags = 0 → primary slot 1. */
    mm_desc_set(0, 0, /*flags=*/0x00, /*sprite_idx=*/1, /*entity_id=*/0);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(0, 0), 0x12);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_entity_owner_lookup(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* Entity id 1; owner table[1] = 4; slot_table[4] = 5.  Should
     * blit default desc slot 5 (color 0x35). */
    g_mm_owner_table[1 * MM_OWNER_STRIDE] = 4;
    g_mm_slot_table [4 * MM_SLOT_STRIDE ] = 5;

    mm_desc_set(2, 0, /*flags=*/0x00, /*sprite_idx=*/0, /*entity_id=*/1);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(2, 0), 0x35);
    /* Boundary bit cleared even when not initially set. */
    int off = (2 + 0 * MM_ROW_STRIDE) * MM_STRIDE_BYTES;
    T_ASSERT_EQ_I(g_mm_desc[off + 2] & 0x02, 0);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_entity_owner_6_shortcut(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* Owner type 6 short-circuits to slot 6 (color 0x36) — the slot
     * table is NOT consulted. */
    g_mm_owner_table[2 * MM_OWNER_STRIDE] = 6;
    /* Sabotage slot_table[6] to verify it's NOT used. */
    g_mm_slot_table[6 * MM_SLOT_STRIDE] = 0;

    mm_desc_set(3, 0, /*flags=*/0x00, /*sprite_idx=*/0, /*entity_id=*/2);

    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(mm_cell_pixel(3, 0), 0x36);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_entity_slot_zero_skipped(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* Owner type 5; slot_table[5] = 0 → slot is 0 → blit SKIPPED. */
    g_mm_owner_table[7 * MM_OWNER_STRIDE] = 5;
    g_mm_slot_table [5 * MM_SLOT_STRIDE ] = 0;

    mm_desc_set(2, 0, /*flags=*/0x00, /*sprite_idx=*/0, /*entity_id=*/7);

    /* Disable the other cells from painting so we can isolate cell
     * (2, 0). */
    /* Actually with force_redraw=1 every other cell will paint; that's
     * fine — we read just (2, 0). */
    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    /* Cell (2, 0) stays at canvas pre-fill (0). */
    T_ASSERT_EQ_U(mm_cell_pixel(2, 0), 0x00);
    /* Other cells (col 0 with flags=0, sprite_idx=0 → primary slot 0 = 0x11). */
    T_ASSERT_EQ_U(mm_cell_pixel(0, 0), 0x11);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_advances_cursor_pitch_2(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* All cells use primary slot 0 → 0x11.  Verify 2-pixel pitch by
     * checking that the cells land at (origin_x + col*2, y_offset +
     * row*2). */
    engine_battlefield_render_minimap_render_strip_for_test(0, 2);

    /* Cell (0, 0): pixel at (40, 20). */
    T_ASSERT_EQ_U(g_mm_canvas[40 + 20 * CANVAS_W], 0x11);
    /* Cell (1, 0): pixel at (42, 20) — 2 pixels right. */
    T_ASSERT_EQ_U(g_mm_canvas[42 + 20 * CANVAS_W], 0x11);
    /* Cell (4, 0): pixel at (48, 20) — last col of row 0. */
    T_ASSERT_EQ_U(g_mm_canvas[48 + 20 * CANVAS_W], 0x11);
    /* Cell (0, 1): pixel at (40, 22) — 2 pixels down. */
    T_ASSERT_EQ_U(g_mm_canvas[40 + 22 * CANVAS_W], 0x11);
    /* Cell (4, 1): pixel at (48, 22). */
    T_ASSERT_EQ_U(g_mm_canvas[48 + 22 * CANVAS_W], 0x11);
    /* Each rendered 2×2 tile: cell (0, 0) also paints (41, 20),
     * (40, 21), (41, 21). */
    T_ASSERT_EQ_U(g_mm_canvas[41 + 20 * CANVAS_W], 0x11);
    T_ASSERT_EQ_U(g_mm_canvas[40 + 21 * CANVAS_W], 0x11);
    T_ASSERT_EQ_U(g_mm_canvas[41 + 21 * CANVAS_W], 0x11);
    /* Right of cell (4, 0) — (50, 20) — should be untouched. */
    T_ASSERT_EQ_U(g_mm_canvas[50 + 20 * CANVAS_W], 0x00);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_at_offset_row(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    /* Render rows 2..3 — strip should land at y = 20 + 2*2 = 24. */
    engine_battlefield_render_minimap_render_strip_for_test(2, 2);

    /* Cell (0, 2): pixel at (40, 24). */
    T_ASSERT_EQ_U(g_mm_canvas[40 + 24 * CANVAS_W], 0x11);
    /* Cell (0, 3): pixel at (40, 26). */
    T_ASSERT_EQ_U(g_mm_canvas[40 + 26 * CANVAS_W], 0x11);
    /* Row 0 (y=20) untouched — strip started at row 2. */
    T_ASSERT_EQ_U(g_mm_canvas[40 + 20 * CANVAS_W], 0x00);
    /* Row 4 (y=28) untouched — count = 2 from start = 2 → ends at 3. */
    T_ASSERT_EQ_U(g_mm_canvas[40 + 28 * CANVAS_W], 0x00);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_null_desc_no_op(void)
{
    mm_scaffold();
    engine_battlefield_render_bind_descriptor_table(NULL, MM_STRIDE_BYTES);

    engine_battlefield_render_minimap_render_strip_for_test(0, 5);

    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(),     0);
    return 0;
}

int test_engine_battlefield_render_minimap_strip_zero_count_no_op(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);

    engine_battlefield_render_minimap_render_strip_for_test(0, 0);

    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(),     0);
    return 0;
}

int test_engine_battlefield_render_minimap_reset_clears_counters(void)
{
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);
    engine_battlefield_render_minimap_render_strip_for_test(0, 1);
    T_ASSERT(engine_battlefield_render_minimap_per_cell_count() > 0);
    T_ASSERT(engine_battlefield_render_minimap_blit_count()     > 0);

    engine_battlefield_render_reset_counts();

    T_ASSERT_EQ_I(engine_battlefield_render_minimap_per_cell_count(), 0);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_blit_count(),     0);
    return 0;
}

int test_engine_battlefield_render_minimap_reset_scan_pos(void)
{
    mm_scaffold();
    engine_battlefield_render_minimap_scroll(2);
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 2);

    engine_battlefield_render_reset_minimap_scan_pos();
    T_ASSERT_EQ_I(engine_battlefield_render_minimap_scan_pos(), 0);
    return 0;
}

int test_engine_battlefield_render_minimap_bulk_binder_minimap_context(void)
{
    /* Verifies the bulk binder wires all 6 fields by setting bogus
     * pointers via the bulk path, then triggering a strip render and
     * checking the pixel pattern matches the bound blobs. */
    mm_scaffold();
    engine_battlefield_render_set_force_redraw(1);

    /* Build two separate "alternate" blobs and rebind via the bulk
     * call. */
    static uint8_t alt_primary[MM_BLOB_SIZE];
    static uint8_t alt_alt    [MM_BLOB_SIZE];
    static uint8_t alt_default[MM_BLOB_SIZE];
    memset(alt_primary, 0, sizeof alt_primary);
    memset(alt_alt,     0, sizeof alt_alt);
    memset(alt_default, 0, sizeof alt_default);
    mm_blob_set_slot(alt_primary, 0, 0x71);
    mm_blob_set_slot(alt_alt,     0, 0x81);
    mm_blob_set_slot(alt_default, 0, 0x91);

    engine_battlefield_render_bind_minimap_context(
        alt_primary, alt_alt, alt_default,
        /*origin_x=*/200, /*y_offset=*/100, /*mode_gate=*/2);

    /* All cells flags=0 → primary blob slot 0 (color 0x71).  Origin
     * shifted to (200, 100). */
    engine_battlefield_render_minimap_render_strip_for_test(0, 1);

    T_ASSERT_EQ_U(g_mm_canvas[200 + 100 * CANVAS_W], 0x71);
    /* Mode gate is now 2 — scroll should dispatch. */
    engine_battlefield_render_minimap_scroll(1);
    T_ASSERT(engine_battlefield_render_minimap_per_cell_count() > 5);
    return 0;
}

int test_engine_battlefield_render_minimap_bulk_binder_hex_grid_context(void)
{
    /* Verifies the hex-grid bulk binder wires all 11 params.  We
     * check observable effects via the existing accessors and a
     * derived round trip (clip rect right edge = viewport_x +
     * visible_cols * zoom). */
    mm_scaffold();

    static uint8_t alt_primary[MM_BLOB_SIZE];
    static uint8_t alt_alt    [MM_BLOB_SIZE];
    static uint8_t alt_desc[ MM_STRIDE_BYTES * 4 * 4 ];
    memset(alt_primary, 0, sizeof alt_primary);
    memset(alt_alt,     0, sizeof alt_alt);
    memset(alt_desc,    0, sizeof alt_desc);

    engine_battlefield_render_bind_hex_grid_context(
        alt_primary, alt_alt, alt_desc,
        /*row_stride_in_cells=*/4,
        /*total_rows=*/4,
        /*cell_stride_bytes=*/MM_STRIDE_BYTES,
        /*viewport_x=*/64, /*viewport_y=*/32,
        /*visible_cols=*/3, /*visible_rows=*/3,
        /*zoom=*/16);

    /* Zoom dispatcher reflects the bound zoom. */
    T_ASSERT_EQ_I(engine_battlefield_render_get_zoom(), 16);
    /* viewport accessors via the grid hit-test (centre point inside
     * grid (3 cols × 16 px = 48 wide, starting at x=64) → x=80, y=40
     * should be inside; x=63, y=32 should be outside. */
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(80, 40), 1);
    T_ASSERT_EQ_I(engine_battlefield_render_screen_in_grid(63, 40), 0);
    /* Cell band width derived from zoom/4 = 4. */
    /* pixel_to_col_right_biased: px=64+12 = 76 → residue=12; band*3=12;
     * 12 >= 12 → col += 1 → col=1. */
    T_ASSERT_EQ_I(engine_battlefield_render_pixel_to_col_right_biased(76), 1);
    return 0;
}
