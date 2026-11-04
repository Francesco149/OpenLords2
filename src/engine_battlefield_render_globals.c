/*
 * OpenLords2 — the battlefield renderer's retail globals (engine_battlefield_render.h declares them).
 *
 * Own translation unit: the hybrid host (tools/rhost) leaves this file out and aliases each symbol at its retail
 * VA instead (rh_hooks_screens.c), so the linked port reads and writes retail's memory.  The renderer's own block
 * (DAT_004e5b18..DAT_004e6588, DAT_00568994) is referenced by no other retail function; the rest are other
 * subsystems' globals the renderer reads (the battlefield sprite sheets, the selection marker, …) — in the
 * standalone the binders set them.  Pointers are retail dwords (i686).
 */
#include "engine_battlefield_render.h"

/* the frame: camera, zoom, the descriptor walk */
int32_t        g_bfr_camera_x;          /* DAT_004e657c */
int32_t        g_bfr_camera_y;          /* DAT_004e6580 */
int32_t        g_bfr_zoom;              /* DAT_004e656c the cell pitch: 0x10 / 0x20 */
uint8_t       *g_bfr_desc_base;         /* DAT_004e6574 the cell-descriptor table */
int32_t        g_bfr_desc_stride;       /* DAT_004e5d50 bytes per descriptor */
int32_t        g_bfr_desc_cursor;       /* DAT_004e6588 the walk's byte offset */
int32_t        g_bfr_grid_cols;         /* DAT_004e5d5c visible columns */
int32_t        g_bfr_grid_rows;         /* DAT_004e5d4c visible rows */
int32_t        g_bfr_row_wrap;          /* DAT_004e5d70 bytes from a visible row's end to the next one's start */
int32_t        g_bfr_viewport_x;        /* DAT_004e6558 */
int32_t        g_bfr_viewport_y;        /* DAT_004e6554 */
int32_t        g_bfr_row_stride;        /* DAT_004e6578 grid columns (cells per row) */
int32_t        g_bfr_total_rows;        /* DAT_004e6570 grid rows */
int32_t        g_bfr_band_width;        /* DAT_004e5d3c zoom / 4 */
int32_t        g_bfr_scroll_pad;        /* DAT_004e5d44 zoom / 2 */
int32_t        g_bfr_zoom_sq;           /* DAT_004e6550 zoom * zoom */
int32_t        g_bfr_cell_counter;      /* DAT_004e5d64 */
const uint8_t *g_bfr_blob_primary;      /* DAT_004e5d78 */
const uint8_t *g_bfr_blob_alt;          /* DAT_004e6560 */
int32_t        g_bfr_clip_left;         /* DAT_004e6564 */
int32_t        g_bfr_clip_right;        /* DAT_004e5d54 */
int32_t        g_bfr_clip_top;          /* DAT_004e5d48 */
int32_t        g_bfr_clip_bottom;       /* DAT_004e5d6c */
/* the middle pass: the y-sorted visible list ((id, y) word pairs + the sentinel), the leaves' animation state */
int16_t        g_bfr_visible[BFR_VISIBLE_CAP * 2];   /* DAT_004e5d80 */
int32_t        g_bfr_visible_count;     /* DAT_004e5d38 */
int32_t        g_bfr_sort_passes;       /* DAT_004e655c */
int32_t        g_bfr_marker_frame;      /* DAT_004e5b18 */
int32_t        g_bfr_effect_frame;      /* DAT_004e5b1c */
int32_t        g_bfr_effect_id;         /* DAT_00568994 */
/* the minimap */
const uint8_t *g_bfr_mm_blob_primary;   /* DAT_004e6584 */
const uint8_t *g_bfr_mm_blob_alt;       /* DAT_004e6568 */
const uint8_t *g_bfr_mm_default;        /* DAT_004e5d40 */
int32_t        g_bfr_mm_origin_x;       /* DAT_004e5d68 */
int32_t        g_bfr_mm_y_offset;       /* DAT_004e5d60 */
int32_t        g_bfr_mm_mode;           /* DAT_004e5d58 */
int32_t        g_bfr_mm_scan_pos;       /* DAT_004e5d74 */
/* other subsystems' globals the renderer reads */
const uint8_t *g_bfr_marker_blob;       /* DAT_00566518 */
int32_t        g_bfr_marker_player;     /* DAT_00568218 */
int32_t        g_bfr_marker_advance;    /* DAT_00591510 */
const uint8_t *g_bfr_unit_key_blob;     /* DAT_00553250 */
const uint8_t *g_bfr_type7_blob_low;    /* DAT_00553f6c */
const uint8_t *g_bfr_type7_blob_high;   /* DAT_00522f78 */
const uint8_t *g_bfr_creature_blob;     /* DAT_005530c8 */
int32_t        g_bfr_late_player;       /* DAT_0057c8cc */
int32_t        g_bfr_slot_shift;        /* DAT_00566514 (its low byte is the shift count) */
int32_t        g_bfr_slot_shift_sign;   /* DAT_0056651c */
int32_t        g_bfr_sel_x;             /* DAT_00553f14 */
int32_t        g_bfr_sel_y;             /* DAT_00553f18 */
int32_t        g_bfr_sel_gate;          /* DAT_00553f84 */
uint8_t        g_bfr_debug_label;       /* DAT_005bb4a5 */
int32_t        g_bfr_engine_state;      /* DAT_0052af94 */
