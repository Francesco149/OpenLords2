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

/* Quirk: descriptor[+1] & 4 set on a DIRTY cell → NO drawing AND NO
 * boundary marking (the entire if/else-if chain short-circuits). */
int test_engine_battlefield_render_terrain_z32_skip_bit_drops_dirty_cell(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Centre cell (1, 1) — interior, dirty, with the skip-bit set. */
    desc_set_z32(1, 1, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* 8 other interior+edge cells blitted (9 - 1 skipped). */
    T_ASSERT_EQ_I(engine_battlefield_render_terrain_zoom32_first_blit_count(), 8);

    /* Centre cell: dirty bit STILL set (drawing path never ran). */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x01, 0x01);

    /* Centre cell: boundary bit NOT set (skip-bit short-circuited
     * boundary marking too — but centre cell is interior so this is
     * trivially true; the relevant check is the corner case below). */
    T_ASSERT_EQ_I(desc_flags(1, 1, DESC_COLS_PHY) & 0x02, 0);
    return 0;
}

/* Quirk corner case: descriptor[+1] & 4 set on a DIRTY EDGE cell —
 * boundary marking should still be skipped (the skip-bit short-
 * circuits BOTH drawing AND boundary marking, unlike on a clean cell). */
int test_engine_battlefield_render_terrain_z32_skip_bit_blocks_boundary_too(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Top-left corner cell (0, 0) — dirty, skip-bit set. */
    desc_set_z32(0, 0, DESC_COLS_PHY,
                 /*b0=*/0, /*b1=*/0x04,
                 /*flags=*/0x01, /*sprite=*/1, /*b4=*/0);

    engine_battlefield_render_frame(0, 0);

    /* Boundary bit NOT set on the cell — skip-bit blocked it. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x02, 0);
    /* Dirty bit still set. */
    T_ASSERT_EQ_I(desc_flags(0, 0, DESC_COLS_PHY) & 0x01, 0x01);
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

/* Quirk: [1, 310000] glyph_offset gate aborts the entire cell. */
int test_engine_battlefield_render_terrain_z32_src_offset_gate_aborts(void)
{
    terrain_z32_scaffold(/*cols=*/3, /*rows=*/3);

    /* Re-stage slot 1 in primary with an out-of-band src_off.  Use
     * 310001 (just past the upper bound). */
    blob_set_slot(g_z32_blob_primary, 1, 32, 32, 310001, 0xab, 32, 0xcd);

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
