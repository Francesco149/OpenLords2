/*
 * OpenLords2 — strategic-mode base-terrain renderer (pass 1).
 *
 * See engine_map_terrain.h for the per-function map and
 * docs/findings/map-render.md ("Pass 1") for the full RE.
 */

#include "engine_map_terrain.h"

#include "call_trace.h"
#include "gfx_glyph.h"
#include "gfx_paint.h"

#include <stddef.h>

/* Byte-exact unrolled iso-tile blitter tables, table-extracted from the
 * retail decompiles by tools/extract_blit_table.py.  View-mode 0 only. */
#include "gen/blit_table_452820.h"
#include "gen/blit_table_45337b.h"
#include "gen/blit_table_4538f6.h"

/* Drawer-B (tall-sprite overhang) strip-blitter tables.  Replayed with the
 * r2-corrected guard `iVar1 < col_y_accum` (Ghidra inverted it).  See
 * docs/findings/map-render.md "drawer B". */
#include "gen/drawb_table_453e71.h"

/* Drawer-B kind-3/kind-4/edge-phase siblings (branch-B flat strip, param_5=0).
 * Pyramid branch A is dead (param_4=col_y_accum >> count); the flat strip is
 * the live path.  See tools/extract_drawb_stripB.py + map-render.md "drawer B". */
#include "gen/drawb_stripb_455f04.h"   /* kind 2, phase 3 (left edge) */
#include "gen/drawb_stripb_4580a9.h"   /* kind 3, phase 0/1 (aligned) */
#include "gen/drawb_stripb_459258.h"   /* kind 4, phase 0/1 (aligned) */

/* ──────────────────────────────────────────────────────────────────
 * Bound state — retail DAT_* globals this cluster touches.
 * ────────────────────────────────────────────────────────────────── */

static const int32_t *g_cells;          /* DAT_0055cea0  */
static int            g_row_stride;     /* 0x104         */
static const uint8_t *g_desc;           /* 0x522f90      */
static const uint8_t *g_blob[5];        /* 568208/53e914/57d344/55409c/56898c */
static int            g_blob_len[5];

static int g_x0, g_y0;                   /* DAT_0052afdc / DAT_00553244 */
static int g_col_start, g_row_start;     /* DAT_005651b4 / DAT_005651b8 */
static int g_col_count, g_row_count;     /* DAT_0053e8ac / DAT_0056d67c */
static int g_x_step, g_y_step, g_x_half; /* DAT_00568220 / 0053f65c / 0567950 */

static int g_view_mode;                  /* DAT_0057cb18 */
static int g_render_mode;                /* DAT_0053f264 */
static int g_empty_redraw_gate;                   /* DAT_0053e9ac */

/* Walker state — retail DAT_0056d594 / 0056d598 / 0056957c / 00567954. */
static int g_cur_row;
static int g_cur_col;
static int g_col_y_accum;
static int g_current_cell;

/* The empty-cell / drawer sprite index (retail DAT_005c9288). */
static int g_sprite_index;

static int g_error_count;                /* _DAT_0058fea8 */

/* Stats. */
static int g_n_drawa, g_n_empty, g_n_drawb, g_n_drawb_skipped, g_n_oob;

/* Retail src_off validation upper bound (FUN_004063c1 line "< 0x4baf1"). */
#define TERRAIN_SRC_OFFSET_MAX  0x4baf1

/* ──────────────────────────────────────────────────────────────────
 * Bind / config
 * ────────────────────────────────────────────────────────────────── */

void engine_map_terrain_bind_tile_array(const int32_t *cells, int row_stride_bytes)
{
    g_cells = cells;
    g_row_stride = row_stride_bytes;
}

void engine_map_terrain_bind_descriptors(const uint8_t *descriptors)
{
    g_desc = descriptors;
}

void engine_map_terrain_bind_blobs(const uint8_t *b0, const uint8_t *b1,
                                   const uint8_t *b2, const uint8_t *b3,
                                   const uint8_t *b4)
{
    g_blob[0] = b0; g_blob[1] = b1; g_blob[2] = b2;
    g_blob[3] = b3; g_blob[4] = b4;
}

void engine_map_terrain_set_blob_lengths(int n0, int n1, int n2, int n3, int n4)
{
    g_blob_len[0] = n0; g_blob_len[1] = n1; g_blob_len[2] = n2;
    g_blob_len[3] = n3; g_blob_len[4] = n4;
}

void engine_map_terrain_bind_camera(int x0, int y0,
                                    int col_start, int row_start,
                                    int col_count, int row_count,
                                    int x_step, int y_step, int x_half_step)
{
    g_x0 = x0; g_y0 = y0;
    g_col_start = col_start; g_row_start = row_start;
    g_col_count = col_count; g_row_count = row_count;
    g_x_step = x_step; g_y_step = y_step; g_x_half = x_half_step;
}

void engine_map_terrain_set_view_mode(int v)   { g_view_mode = v; }
void engine_map_terrain_set_render_mode(int m)  { g_render_mode = m; }
void engine_map_terrain_set_empty_redraw_gate(int f){ g_empty_redraw_gate = f; }
int  engine_map_terrain_empty_redraw_gate(void)      { return g_empty_redraw_gate; }

int  engine_map_terrain_error_count(void)       { return g_error_count; }
void engine_map_terrain_set_error_count(int n)  { g_error_count = n; }

int  engine_map_terrain_drawa_count(void)       { return g_n_drawa; }
int  engine_map_terrain_empty_draw_count(void)         { return g_n_empty; }
int  engine_map_terrain_drawb_count(void)       { return g_n_drawb; }
int  engine_map_terrain_drawb_skipped(void)     { return g_n_drawb_skipped; }
int  engine_map_terrain_oob_copies(void)        { return g_n_oob; }
void engine_map_terrain_reset_stats(void)
{
    g_n_drawa = g_n_empty = g_n_drawb = g_n_drawb_skipped = g_n_oob = 0;
}

/* ──────────────────────────────────────────────────────────────────
 * Tile-cell fetch — *(int*)(DAT_0055cea0 + row*0x104 + col*4).
 * ────────────────────────────────────────────────────────────────── */

static int terrain_cell(int col, int row)
{
    const uint8_t *p = (const uint8_t *)g_cells + row * g_row_stride + col * 4;
    int32_t v;
    /* The bound array is little-endian raw bytes; read as int32. */
    v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    return v;
}

/* ──────────────────────────────────────────────────────────────────
 * Table replay — one block of (dest_off, src_off, size) copies.
 *
 * dest = canvas + cursor + entry.dest_off; src = blob + src_off +
 * entry.src_off.  Defensive bounds checks (count, skip) guard the
 * harness against malformed capture data; for the captured frame they
 * never trip (retail itself does no bounds check here).
 * ────────────────────────────────────────────────────────────────── */

static void blit_block(const blit_copy_t *tbl, int n,
                       const uint8_t *blob, int blob_len, int src_off)
{
    uint8_t *canvas = gfx_paint_canvas_pixels();
    int pitch = gfx_paint_canvas_width();
    int cw = pitch;
    int ch = gfx_paint_canvas_height();
    if (!canvas || !blob) return;
    long canvas_len = (long)cw * ch;
    long dest_base = (long)gfx_glyph_cursor_x()
                   + (long)gfx_glyph_cursor_y() * pitch;

    for (int i = 0; i < n; i++) {
        long s = (long)src_off + tbl[i].src_off;
        long d = dest_base + tbl[i].dest_off;
        int  sz = tbl[i].size;
        if (s < 0 || (blob_len > 0 && s + sz > blob_len)
            || d < 0 || d + sz > canvas_len) {
            g_n_oob++;
            continue;
        }
        for (int k = 0; k < sz; k++)
            canvas[d + k] = blob[s + k];
    }
}

/* Dispatch the view-mode-0 blitter for a phase.
 *   phase 0 → FUN_00452820 block A+B (full)
 *   phase 1 → FUN_00452820 block A   (top half)   [bottom phantom row]
 *   phase 2 → FUN_00452820 block B   (bottom half) [top phantom row]
 *   phase 3 → FUN_0045337b block A+B (left-edge half-tile)
 *   phase 4 → FUN_004538f6 block A+B (right-edge half-tile)
 */
static void terrain_blit(const uint8_t *blob, int blob_len, int src_off, int phase)
{
    switch (phase) {
    case 3:
        blit_block(k_blit_45337b_a, k_blit_45337b_a_n, blob, blob_len, src_off);
        blit_block(k_blit_45337b_b, k_blit_45337b_b_n, blob, blob_len, src_off);
        break;
    case 4:
        blit_block(k_blit_4538f6_a, k_blit_4538f6_a_n, blob, blob_len, src_off);
        blit_block(k_blit_4538f6_b, k_blit_4538f6_b_n, blob, blob_len, src_off);
        break;
    default:
        if (phase != 2)
            blit_block(k_blit_452820_a, k_blit_452820_a_n, blob, blob_len, src_off);
        if (phase != 1)
            blit_block(k_blit_452820_b, k_blit_452820_b_n, blob, blob_len, src_off);
        break;
    }
}

/* Read the i32 src_off from a sprite directory entry: blob+idx*0x10+0xc. */
static int sprite_src_off(const uint8_t *blob, int idx)
{
    const uint8_t *e = blob + idx * 0x10 + 0xc;
    return (int)((uint32_t)e[0] | ((uint32_t)e[1] << 8)
               | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24));
}

/* ──────────────────────────────────────────────────────────────────
 * Terrain drawer A — port of FUN_004063c1 (base ground layer).
 * ────────────────────────────────────────────────────────────────── */

static void drawer_a(int phase)
{
    int cell = g_current_cell;
    int d2  = g_desc[cell + 2];
    int sel = d2 & 0x1c;
    int idx = g_desc[cell + 3];

    /* Render-mode mask (mode 1 hides un-flagged tiles → sprite 0/blob 0). */
    if (g_render_mode == 1 && (d2 & 0x20) == 0) {
        sel = 0;
        idx = 0;
    }

    int bi;
    switch (sel) {
    case 0x00: bi = 0; break;
    case 0x04: bi = 1; break;
    case 0x08: bi = 2; break;
    case 0x0c: bi = 3; break;
    case 0x10: bi = 4; break;
    default:   return;   /* retail returns without drawing */
    }
    const uint8_t *blob = g_blob[bi];
    if (!blob) return;

    /* First-paint dirty bit (desc+2 bit 0) — retail clears it + marks
     * the tile dirty.  Affects dirty-rect bookkeeping only, not pixels;
     * a single-frame render needs neither, so it is intentionally
     * elided (descriptors stay const). */

    int src_off = sprite_src_off(blob, idx);
    if (src_off >= TERRAIN_SRC_OFFSET_MAX || src_off < 0) {
        g_error_count++;
        return;
    }
    if (g_view_mode != 0) return;   /* only view-mode 0 family ported */

    terrain_blit(blob, g_blob_len[bi], src_off, phase);
    g_n_drawa++;
}

/* Drawer-B overhang strip-blitter replay (one blitter table).  Faithful to
 * the retail do/while with the r2-CORRECTED guard: draw outer row `iVar1`
 * only while `iVar1 < col_y_accum` (Ghidra decompiled this as the inverse
 * `col_y_accum <= iVar1`; the asm at 0x453ea4 is `cmp ebx,ecx; jle`, i.e.
 * draw when iVar1 < col_y).  Each table entry's src_off/dst_off already
 * bake in the per-row shear; depth_k gates the `iVar1 + k < count` window.
 *   src  = blob + src_off2  (= sprite src_off + 900, view-mode 0)
 *   dst  = canvas + cursor + (dst_off - iVar1*pitch)
 * Per-copy zero-skip transparency; out-of-canvas writes are clipped. */
static void drawb_strip(const drawb_copy_t *tbl, int n,
                        const uint8_t *blob, int blob_len,
                        int src_off2, int count, int col_y)
{
    uint8_t *canvas = gfx_paint_canvas_pixels();
    int pitch = gfx_paint_canvas_width();
    long canvas_len = (long)pitch * gfx_paint_canvas_height();
    if (!canvas || !blob) return;
    long base = (long)gfx_glyph_cursor_x() + (long)gfx_glyph_cursor_y() * pitch;

    for (int row = 0; row < count; row++) {
        if (row >= col_y) break;            /* corrected guard */
        long src_row = (long)src_off2 + (long)row * 0x3a;
        long dst_row = base - (long)row * pitch;
        for (int i = 0; i < n; i++) {
            if (row + tbl[i].depth_k >= count) continue;
            long s = src_row + tbl[i].src_off;
            long d = dst_row + tbl[i].dst_off;
            if (s < 0 || (blob_len > 0 && s >= blob_len)) { g_n_oob++; continue; }
            if (d < 0 || d >= canvas_len) { g_n_oob++; continue; }
            uint8_t v = blob[s];
            if (v != 0) canvas[d] = v;       /* zero-skip transparency */
        }
    }
}

/* Flat branch-B strip replay for the kind-3/4 + edge-phase siblings of
 * 453e71 (FUN_00455f04 / 004580a9 / 00459258).  Each draws a flat single-row
 * strip per row with the dst pointer decremented one scanline FIRST (so row r
 * → base - (r+1)*pitch) and the src base advanced `stride` per row; the table
 * already folds in init_off + pcVar4 aliasing.  No depth gating — the diamond
 * shape comes from the dst_off spread + zero-skip transparency.  Same dead-
 * pyramid reasoning as drawb_strip: the loop runs `count` rows while row<col_y
 * (always, since col_y ≈ 105-300 ≫ count). */
static void drawb_strip_flat(const drawb_flat_t *tbl, int n, int stride,
                             const uint8_t *blob, int blob_len,
                             int src_off2, int count, int col_y)
{
    uint8_t *canvas = gfx_paint_canvas_pixels();
    int pitch = gfx_paint_canvas_width();
    long canvas_len = (long)pitch * gfx_paint_canvas_height();
    if (!canvas || !blob) return;
    long base = (long)gfx_glyph_cursor_x() + (long)gfx_glyph_cursor_y() * pitch;

    for (int row = 0; row < count; row++) {
        if (row >= col_y) break;            /* corrected guard (never bites) */
        long src_row = (long)src_off2 + (long)row * stride;
        long dst_row = base - (long)(row + 1) * pitch;
        for (int i = 0; i < n; i++) {
            long s = src_row + tbl[i].src_idx;
            long d = dst_row + tbl[i].dst_off;
            if (s < 0 || (blob_len > 0 && s >= blob_len)) { g_n_oob++; continue; }
            if (d < 0 || d >= canvas_len) { g_n_oob++; continue; }
            uint8_t v = blob[s];
            if (v != 0) canvas[d] = v;       /* zero-skip transparency */
        }
    }
}

/* Read drawer-B sprite-directory fields (retail FUN_00406673 staging):
 * src_off @ entry+0xc, kind @ entry+0x14, count @ entry+0x15. */
static void drawb_dir(const uint8_t *blob, int idx,
                      int *src_off, int *kind, int *count)
{
    const uint8_t *e = blob + idx * 0x10;
    *src_off = (int)((uint32_t)e[0xc] | ((uint32_t)e[0xd] << 8)
                   | ((uint32_t)e[0xe] << 16) | ((uint32_t)e[0xf] << 24));
    *kind  = e[0x14];
    *count = e[0x15];
}

/* Terrain drawer B — port of FUN_00406673 (tall-sprite overhang layer).
 * Draws the part of mountains / improvements / livestock that rises above
 * the base diamond and bleeds into neighbouring cells.  View-mode 0 only.
 *
 * Currently ported blitter: kind 2 phase 0/1 (FUN_00453e71).  Kinds 3/4 and
 * the phase-3/4 edge variants (455f04/456fd8/4580a9/459258) are counted in
 * g_n_drawb_skipped pending their tables. */
static void drawer_b(int phase)
{
    if (phase == 2) return;                  /* dispatcher skips phase 2 */

    int cell = g_current_cell;
    int d2  = g_desc[cell + 2];
    int sel = d2 & 0x1c;
    int idx = g_desc[cell + 3];
    if (g_render_mode == 1 && (d2 & 0x20) == 0) { sel = 0; idx = 0; }

    int bi;
    switch (sel) {
    case 0x00: bi = 0; break; case 0x04: bi = 1; break; case 0x08: bi = 2; break;
    case 0x0c: bi = 3; break; case 0x10: bi = 4; break;
    default: return;
    }
    const uint8_t *blob = g_blob[bi];
    if (!blob) return;
    if (g_view_mode != 0) return;

    int src_off, kind, count;
    drawb_dir(blob, idx, &src_off, &kind, &count);
    if (src_off < 0 || src_off >= TERRAIN_SRC_OFFSET_MAX) { g_error_count++; return; }
    if (count <= 0 || count >= 0xc9) return;

    int src_off2 = src_off + 900;            /* view-mode-0 overhang offset */

    /* Dispatch mirrors retail FUN_00406673 (view-mode 0).  Phase 0/1 = aligned
     * (param_5=0); phase 3 = left edge, phase 4 = right edge.  Ported variants:
     *   kind 2 phase 0/1 → 453e71 (pyramid replay), phase 3 → 455f04
     *   kind 3 phase 0/1 → 4580a9 (param_5=0)
     *   kind 4 phase 0/1 → 459258 (param_5=0)
     * Edge-phase kind-3/4 (param_5=2) + kind-2 phase-4 (456fd8) are not yet
     * ported — counted as skipped (don't appear in the strat-terrain1 frame). */
    int aligned = (phase == 0 || phase == 1);
    if (kind == 2 && aligned) {
        drawb_strip(k_drawb_453e71, k_drawb_453e71_n, blob, g_blob_len[bi],
                    src_off2, count, g_col_y_accum);
        g_n_drawb++;
    } else if (kind == 2 && phase == 3) {
        drawb_strip_flat(k_drawb_stripb_455f04, k_drawb_stripb_455f04_n,
                         k_drawb_stripb_455f04_stride, blob, g_blob_len[bi],
                         src_off2, count, g_col_y_accum);
        g_n_drawb++;
    } else if (kind == 3 && aligned) {
        drawb_strip_flat(k_drawb_stripb_4580a9, k_drawb_stripb_4580a9_n,
                         k_drawb_stripb_4580a9_stride, blob, g_blob_len[bi],
                         src_off2, count, g_col_y_accum);
        g_n_drawb++;
    } else if (kind == 4 && aligned) {
        drawb_strip_flat(k_drawb_stripb_459258, k_drawb_stripb_459258_n,
                         k_drawb_stripb_459258_stride, blob, g_blob_len[bi],
                         src_off2, count, g_col_y_accum);
        g_n_drawb++;
    } else {
        g_n_drawb_skipped++;   /* edge-phase kind 3/4 / kind-2 phase-4 — TODO */
    }
}

/* Empty-cell drawer — port of FUN_0042a7f1.  Draws blob 0 at the
 * empty-marker sprite index (view-mode-0 blitter FUN_00452820).  At the
 * map's TOP edge these off-map cells paint the sky band (hence the
 * historical "sky" naming), but the same path also fills INTERIOR empty
 * "border" cells with a base-grass sprite — not sky. */
static void empty_cell_draw(int phase)
{
    const uint8_t *blob = g_blob[0];
    if (!blob) return;
    int src_off = sprite_src_off(blob, g_sprite_index);
    if (src_off >= TERRAIN_SRC_OFFSET_MAX || src_off < 0) {
        g_error_count++;
        return;
    }
    if (g_view_mode != 0) return;
    terrain_blit(blob, g_blob_len[0], src_off, phase);
    g_n_empty++;
}

/* Per-cell dispatch shared by the iterators.  `gated` mirrors the
 * inner-iterator empty-redraw gate (empty cells draw only when the gate
 * flag is set OR the sprite index exceeds 6); phantom rows pass
 * gated=0 (always draw). */
static void do_cell(int col, int row, int phase, int gated)
{
    int cv = terrain_cell(col, row);
    g_current_cell = cv;
    if ((uint32_t)cv < ENGINE_MAP_TERRAIN_CELL_EMPTY) {
        drawer_a(phase);
        drawer_b(phase);
    } else {
        g_sprite_index = (g_render_mode == 1)
                       ? 0 : (cv - (int)ENGINE_MAP_TERRAIN_CELL_EMPTY);
        if (!gated || g_empty_redraw_gate != 0 || g_sprite_index > 6)
            empty_cell_draw(phase);
    }
}

/* ──────────────────────────────────────────────────────────────────
 * Row iterators.
 * ────────────────────────────────────────────────────────────────── */

/* FUN_00405ae9 — aligned phase-0 row: col_count cells from col_start,
 * cursor starting at x0, advancing x_step each. */
static void row_aligned(void)
{
    CALL_TRACE_ENTER(0x00405ae9);
    gfx_glyph_set_cursor_x(g_x0);
    g_cur_col = g_col_start;
    for (int i = 0; i < g_col_count; i++) {
        do_cell(g_cur_col, g_cur_row, 0, /*gated=*/1);
        g_cur_col++;
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_x_step);
    }
    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_y_step);
    g_cur_row++;
    g_col_y_accum += g_y_step;
}

/* FUN_00405c2f — half-shifted row: first cell phase 3, then col_count-1
 * cells phase 0, then one trailing cell phase 4.  cursor += x_half_step
 * after the first cell. */
static void row_shifted(void)
{
    CALL_TRACE_ENTER(0x00405c2f);
    gfx_glyph_set_cursor_x(g_x0);

    do_cell(g_col_start, g_cur_row, 3, /*gated=*/1);
    g_cur_col = g_col_start + 1;
    gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_x_half);

    for (int i = 0; i < g_col_count - 1; i++) {
        do_cell(g_cur_col, g_cur_row, 0, /*gated=*/1);
        g_cur_col++;
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_x_step);
    }

    do_cell(g_cur_col, g_cur_row, 4, /*gated=*/1);
    g_cur_col++;

    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_y_step);
    g_cur_row++;
    g_col_y_accum += g_y_step;
}

/* ──────────────────────────────────────────────────────────────────
 * Frame driver — port of FUN_0040526e.
 * ────────────────────────────────────────────────────────────────── */

void engine_map_terrain_frame(void)
{
    CALL_TRACE_ENTER(0x0040526e);

    gfx_glyph_set_cursor_y(g_y0);
    gfx_glyph_set_cursor_x(g_x0);
    g_cur_row = g_row_start;
    g_col_y_accum = 0;
    g_cur_col = g_col_start;

    /* Top phantom row — phase 2, col_count cells, no edge cells. */
    for (int i = 0; i < g_col_count; i++) {
        do_cell(g_cur_col, g_cur_row, 2, /*gated=*/0);
        g_cur_col++;
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_x_step);
    }
    gfx_glyph_set_cursor_y(gfx_glyph_cursor_y() + g_y_step);
    g_cur_row++;

    row_shifted();

    int pair_count = (g_row_count - 2) / 2;
    for (int r = 0; r < pair_count; r++) {
        row_aligned();
        row_shifted();
    }

    /* Bottom phantom row — phase 1, col_count cells, no edge cells. */
    gfx_glyph_set_cursor_x(g_x0);
    g_cur_col = g_col_start;
    for (int i = 0; i < g_col_count; i++) {
        do_cell(g_cur_col, g_cur_row, 1, /*gated=*/0);
        g_cur_col++;
        gfx_glyph_set_cursor_x(gfx_glyph_cursor_x() + g_x_step);
    }
}
