/*
 * Building footprint expansion — pure-C port of retail FUN_004917dd
 * plus its four helpers.  See engine_tile.h for the API doc and the
 * retail address map.
 *
 * The 9-glyph-per-orientation footprint table is at retail VA
 * 0x4d9dd0, identified by the disassembler as `s__ABHIJPQR_004d9dd0`.
 * Layout (extracted via PE off-line; orients 1/3/5 are zero-filled):
 *
 *   orient 0 (anchor '@'):  "@ABHIJPQR"   (door up)
 *   orient 2 (anchor 'C'):  "CDEKLMSTU"   (door right)
 *   orient 4 (anchor 'X'):  "XYZ`abhij"   (door down)
 *   orient 6 (anchor '['):  "[\\]cdeklm"  (door left)
 *
 * Index `local_14 + local_1c * 9` where local_14 ∈ 0..8 (row-major
 * across the 3x3) and local_1c ∈ {0, 2, 4, 6}.
 */

#include "engine_tile.h"

#include <stddef.h>
#include <string.h>

/* The 9-char footprint glyph table.  4 orientations × 9 glyphs each.
 * Orient row index = anchor_to_orient(char).  Within a row, indices
 * 0..8 are written across the 3x3 in row-major order (top-left first,
 * then right, then second row, etc).  The orient-0 row's index 0 is
 * the anchor char itself — overwriting tiles[offset+3] with '@', which
 * is the no-op identity for that cell. */
static const uint8_t k_footprint_glyphs[4][9] = {
    /* orient 0, anchor '@' */ { '@', 'A', 'B', 'H', 'I', 'J', 'P', 'Q', 'R' },
    /* orient 2, anchor 'C' */ { 'C', 'D', 'E', 'K', 'L', 'M', 'S', 'T', 'U' },
    /* orient 4, anchor 'X' */ { 'X', 'Y', 'Z', '`', 'a', 'b', 'h', 'i', 'j' },
    /* orient 6, anchor '[' */ { '[', '\\',']', 'c', 'd', 'e', 'k', 'l', 'm' },
};

/* Map anchor char → row index in k_footprint_glyphs.  Returns -1 if
 * the char is not one of the four recognized anchors. */
static int anchor_to_row(uint8_t c)
{
    switch (c) {
    case '@': return 0;
    case 'C': return 1;
    case 'X': return 2;
    case '[': return 3;
    default:  return -1;
    }
}

/* Retail uses orient values 0/2/4/6 as both the door direction and the
 * stride-9 multiplier into the glyph table.  Convert table-row → retail
 * orient value for the neighbor-flag helper. */
static int row_to_orient(int row)
{
    return row * 2;
}

void engine_tile_set_footprint_neighbor_flags(uint8_t *tiles,
                                              int center_col,
                                              int center_row,
                                              int orient)
{
    /* Mirror of FUN_004921e5.  param_1 = col, param_2 = row,
     * param_3 = orient.  iVar1 in retail = byte offset of the center
     * cell — recomputed here. */
    int center_off = (center_row * ENGINE_TILE_GRID_W + center_col)
                     * ENGINE_TILE_CELL_BYTES;

    /* Center cell +4 = 1 (build stage initialized to 1). */
    tiles[center_off + 4] = 1;

    /* Corner-neighbor +1 |= 0x10 (mark as "footprint cell").  Each
     * test gates the existence of the diagonal neighbor. */
    if (center_col < 79 && center_row > 0) {
        /* upper-right: dy=-1, dx=+1, N=+1.  Byte address = center +
         * (-640) + 8 + 1 = center - 631.  Retail spelling: DAT_00543e69. */
        tiles[center_off - 631] |= 0x10;
    }
    if (center_col < 79 && center_row < 79) {
        /* lower-right: dy=+1, dx=+1, N=+1.  Center + 649 = DAT_00544369. */
        tiles[center_off + 649] |= 0x10;
    }
    if (center_col > 0 && center_row < 79) {
        /* lower-left: dy=+1, dx=-1, N=+1.  Center + 633 = DAT_00544359. */
        tiles[center_off + 633] |= 0x10;
    }
    if (center_col > 0 && center_row > 0) {
        /* upper-left: dy=-1, dx=-1, N=+1.  Center - 647 = DAT_00543e59. */
        tiles[center_off - 647] |= 0x10;
    }

    if (orient == 0 || orient == 4) {
        /* Vertical-axis building (door up or down).  Horizontal
         * neighbors get +1 flag + +4 stage. */
        if (center_col < 79) {
            tiles[center_off + 9] |= 0x10;     /* right.+1 — DAT_005440e9 */
        }
        if (center_col > 0) {
            tiles[center_off - 7] |= 0x10;     /* left.+1  — DAT_005440d9 */
        }
        /* QUIRK: the two writes below gate on COL bounds but target the
         * UP / DOWN neighbors' +4 bytes.  This is geometrically wrong
         * (the existence of up/down neighbors depends on ROW bounds),
         * but preserved verbatim — see engine-quirks.md.  Callers
         * ensure the row is also in [0, 79] so the write stays
         * in-array. */
        if (center_col < 79) {
            tiles[center_off - 636] = 1;       /* up.+4   — DAT_00543e64 */
        }
        if (center_col > 0) {
            tiles[center_off + 644] = 1;       /* down.+4 — DAT_00544364 */
        }
    } else {
        /* Horizontal-axis building (door left or right).  Vertical
         * neighbors get +1 flag + +4 stage. */
        if (center_row > 0) {
            tiles[center_off - 639] |= 0x10;   /* up.+1   — DAT_00543e61 */
        }
        if (center_row < 79) {
            tiles[center_off + 641] |= 0x10;   /* down.+1 — DAT_00544361 */
        }
        /* QUIRK (mirror of above): gates on ROW bounds but targets
         * RIGHT / LEFT neighbors' +4 bytes. */
        if (center_row > 0) {
            tiles[center_off + 12] = 1;        /* right.+4 — DAT_005440ec */
        }
        if (center_row < 79) {
            tiles[center_off - 4] = 1;         /* left.+4  — DAT_005440dc */
        }
    }
}

void engine_tile_refresh_path_scratch(const uint8_t *tiles,
                                      uint16_t *path_scratch)
{
    /* Mirror of FUN_00471c1f.  Two nested loops over the 80×80 grid;
     * per cell, the +1 flags decide passability, then the outer-ring
     * cells get force-overwritten to 999. */
    int dest_idx = 0;
    int tile_byte = 0;
    for (int row = 0; row < ENGINE_TILE_GRID_H; row++) {
        for (int col = 0; col < ENGINE_TILE_GRID_W; col++) {
            uint8_t flags = tiles[tile_byte + 1];
            uint16_t v;
            if ((flags & 0x10) == 0) {
                v = (flags & 0x40) ? 999 : 0;
            } else {
                v = 999;
            }
            /* Outer-ring override.  Retail does this AFTER the flag
             * test, so it always wins. */
            if (row < 1 || row > 0x4e || col < 1 || col > 0x4e) {
                v = 999;
            }
            path_scratch[dest_idx] = v;
            tile_byte += ENGINE_TILE_CELL_BYTES;
            dest_idx++;
        }
    }
}

void engine_tile_refresh_stage_scratch(const uint8_t *tiles,
                                       uint8_t *stage_scratch)
{
    /* Mirror of FUN_00471d30.  Copy tile +4 of every cell into the
     * 6400-byte scratch.  Loop layout matches retail (cell-stride 8
     * advanced manually). */
    int dest_idx = 0;
    int tile_byte = 0;
    for (int row = 0; row < ENGINE_TILE_GRID_H; row++) {
        for (int col = 0; col < ENGINE_TILE_GRID_W; col++) {
            stage_scratch[dest_idx] = tiles[tile_byte + 4];
            tile_byte += ENGINE_TILE_CELL_BYTES;
            dest_idx++;
        }
    }
}

int engine_tile_push_footprint_record(int32_t footprint_list[][2],
                                      int32_t *last_footprint_tile,
                                      int32_t tile_byte_offset)
{
    /* Mirror of FUN_0048ee46.  Walk the 19-entry list; if a slot's
     * first int is 0, write tile_byte_offset there and return the
     * slot index.  If all slots are full (loop exited with i == 19),
     * write last_footprint_tile and return -1. */
    for (int i = 0; i < ENGINE_TILE_FOOTPRINT_LIST_LEN; i++) {
        if (footprint_list[i][0] == 0) {
            footprint_list[i][0] = tile_byte_offset;
            return i;
        }
    }
    *last_footprint_tile = tile_byte_offset;
    return -1;
}

/* Per-orientation "door" neighbor writes.  Each orient writes 3 bytes
 * to (up to two) neighboring cells of the top-left anchor:
 *
 *   orient 0 (door up):    (col+1, row).+4 = 2; (col+1, row-1).+3 = 1; (col+1, row-1).+1 = 0
 *   orient 2 (door right): (col+2, row+1).+4 = 2; (col+3, row+1).+3 = 2; (col+3, row+1).+1 = 0
 *   orient 4 (door down):  (col+1, row+2).+4 = 2; (col+1, row+3).+3 = 1; (col+1, row+3).+1 = 0
 *   orient 6 (door left):  (col, row+1).+4 = 2;  (col-1, row+1).+3 = 2; (col-1, row+1).+1 = 0
 *
 * Encoded as byte offsets from the top-left's first byte (anchor_off).
 * Values + offsets transcribed from FUN_004917dd's tail switch. */
static void write_door_neighbor(uint8_t *tiles, int anchor_off, int row_in_table)
{
    switch (row_in_table) {
    case 0: /* orient 0, anchor '@' */
        tiles[anchor_off + 12]   = 2;   /* DAT_005440ec — (col+1, row).+4 */
        tiles[anchor_off - 629]  = 1;   /* DAT_00543e6b — (col+1, row-1).+3 */
        tiles[anchor_off - 631]  = 0;   /* DAT_00543e69 — (col+1, row-1).+1 */
        break;
    case 1: /* orient 2, anchor 'C' */
        tiles[anchor_off + 660]  = 2;   /* DAT_00544374 — (col+2, row+1).+4 */
        tiles[anchor_off + 667]  = 2;   /* DAT_0054437b — (col+3, row+1).+3 */
        tiles[anchor_off + 665]  = 0;   /* DAT_00544379 — (col+3, row+1).+1 */
        break;
    case 2: /* orient 4, anchor 'X' */
        tiles[anchor_off + 1292] = 2;   /* DAT_005445ec — (col+1, row+2).+4 */
        tiles[anchor_off + 1931] = 1;   /* DAT_0054486b — (col+1, row+3).+3 */
        tiles[anchor_off + 1929] = 0;   /* DAT_00544869 — (col+1, row+3).+1 */
        break;
    case 3: /* orient 6, anchor '[' */
        tiles[anchor_off + 644]  = 2;   /* DAT_00544364 — (col, row+1).+4 */
        tiles[anchor_off + 635]  = 2;   /* DAT_0054435b — (col-1, row+1).+3 */
        tiles[anchor_off + 633]  = 0;   /* DAT_00544359 — (col-1, row+1).+1 */
        break;
    default:
        break;
    }
}

int engine_tile_expand_footprint_at(int tile_byte_offset,
                                    uint8_t *tiles,
                                    uint16_t *path_scratch,
                                    uint8_t *stage_scratch,
                                    int32_t footprint_list[][2],
                                    int32_t *last_footprint_tile)
{
    int anchor_off = tile_byte_offset;
    int row_in_table = anchor_to_row(tiles[anchor_off + 3]);
    if (row_in_table < 0) {
        return 0;
    }

    /* Decompose anchor byte offset into (col, row).  Retail uses an
     * arithmetic right shift sequence; we use straightforward unsigned
     * division since byte_offset is always nonneg in valid inputs. */
    int cell_idx = anchor_off / ENGINE_TILE_CELL_BYTES;
    int anchor_col = cell_idx % ENGINE_TILE_GRID_W;
    int anchor_row = cell_idx / ENGINE_TILE_GRID_W;

    /* Stage 2 — neighbor flags around the CENTER of the 3x3
     * (anchor_col+1, anchor_row+1). */
    engine_tile_set_footprint_neighbor_flags(tiles,
                                             anchor_col + 1,
                                             anchor_row + 1,
                                             row_to_orient(row_in_table));

    /* Stage 3 — walk the 3x3 starting at the anchor cell, row-major.
     * For each cell:
     *   - +2 &= 0xe3 (clear bits 2,3,4)
     *   - +2 |= 0x04 (set bit 2)
     *   - +3 = glyph[orient][i]
     *   - center cell additionally: +2 |= 0x80
     */
    int glyph_idx = 0;
    for (int dy = 0; dy <= 2; dy++) {
        for (int dx = 0; dx <= 2; dx++) {
            int cell_off = ((anchor_row + dy) * ENGINE_TILE_GRID_W
                            + (anchor_col + dx))
                           * ENGINE_TILE_CELL_BYTES;
            tiles[cell_off + 2] = (uint8_t)(tiles[cell_off + 2] & 0xe3);
            tiles[cell_off + 2] = (uint8_t)(tiles[cell_off + 2] | 0x04);
            tiles[cell_off + 3] = k_footprint_glyphs[row_in_table][glyph_idx];
            glyph_idx++;
            if (dy == 1 && dx == 1) {
                tiles[cell_off + 2] = (uint8_t)(tiles[cell_off + 2] | 0x80);
            }
        }
    }

    /* Stage 4 — per-orientation door-neighbor triplet write. */
    write_door_neighbor(tiles, anchor_off, row_in_table);

    /* Stage 5 — refresh the two whole-grid scratches. */
    engine_tile_refresh_path_scratch(tiles, path_scratch);
    engine_tile_refresh_stage_scratch(tiles, stage_scratch);

    /* Stage 6 — insert into recent-footprint list + record last tile. */
    engine_tile_push_footprint_record(footprint_list,
                                      last_footprint_tile,
                                      anchor_off);

    return 1;
}

int engine_tile_expand_footprint(engine_tile_footprint_state_t *state)
{
    return engine_tile_expand_footprint_at(state->tile_byte_offset,
                                           state->tiles,
                                           state->path_scratch,
                                           state->stage_scratch,
                                           state->footprint_list,
                                           &state->last_footprint_tile);
}
