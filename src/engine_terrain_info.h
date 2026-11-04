/*
 * OpenLords2 — terrain-info ("Here Be Dragons") open orchestration.
 *
 * Port of retail's terrain-info opener
 *   FUN_0043caf4 @ 0x43caf4 (354 B)
 * the action the in-game input dispatcher (FUN_0042ff10) fires when the
 * cursor is over a map tile and the right mouse button is released
 * (the dispatcher's `terrain_info_open` hook — engine_ingame_input.h).
 * It re-resolves the hovered cell, runs the terrain-attribute compute
 * (FUN_0046d5fe → engine_map_query_terrain_info), publishes the cell
 * col/row, stores the county under the cursor, dismisses any open event
 * scroll, optionally selects the army on the tile, and renders the
 * screen-4 terrain-info panel.
 *
 * SHAPE.  Like the dispatcher, this is glue over a dozen retail globals
 * whose values flow between sub-calls, so it is modeled as a pure core:
 * a read-only INPUT snapshot (the scalars + the army-record plane), a
 * MUTABLE state struct (the published cell geometry + the compute
 * outputs the later branches read + the screen), and a HOOKS struct of
 * the sub-calls (resolve-hover / terrain-compute / scroll-dismiss /
 * view-mode-2 jump / army-select / map-render / panel-render / tail).
 * The resolve-hover + terrain-compute hooks WRITE their results back into
 * the state (st->tile_index, st->county_id, st->head_link) — exactly the
 * globals retail's later branches read — so the orchestration (the
 * army-hover branch condition, the guard branches) is unit-testable with
 * recording hooks.  A NULL hook is a no-op.  engine.c binds the live
 * globals + the real ported sub-functions.
 *
 * See docs/plans/ingame-input-handler.md (terrain-info, piece 1).
 */

#ifndef OPENLORDS2_ENGINE_TERRAIN_INFO_H
#define OPENLORDS2_ENGINE_TERRAIN_INFO_H

#include <stdint.h>

/* ── Opener input snapshot (read-only within an open) ─────────────────── */
typedef struct engine_terrain_info_open_in {
    int game_mode;          /* DAT_0057c8fc — campaign (0) gate; else no-op   */
    int tile_stride;        /* DAT_0057d338 = 8 (bytes per descriptor cell)   */
    int map_width;          /* DAT_0052afd4 = 0x40 (cells per row)            */
    int view_mode;          /* DAT_0057cb18 — strategic view mode (2 = alt)   */
    int scroll_open;        /* DAT_0053e910 — a modal event scroll is up      */
    /* Army "active" plane: retail reads
     *   DAT_0053f0dc = *(int*)(&DAT_0053fa20 + head_link * 0x300)
     * to decide the army-hover branch.  Base = army record array + 0x70,
     * stride 0x300 bytes.  Read as a little-endian int32 at
     * head_link*0x300; NULL ⇒ treated as 0 (no army). */
    const uint8_t *army_active_plane;   /* &DAT_0053fa20 */
} engine_terrain_info_open_in_t;

/* ── Mutable state the opener + its hooks read/write ──────────────────────
 * Initialise `screen` (the dispatcher leaves it 4), `cur_army` from the
 * live globals before an open; read back `screen` + the published geometry
 * after.  The resolve-hover hook writes `tile_index`; the terrain-compute
 * hook writes `county_id` + `head_link` (the values retail leaves in
 * DAT_0052afbc / DAT_005681e0 for the branches below). */
typedef struct engine_terrain_info_open_state {
    int screen;             /* DAT_004eac50  (in 4 / out; → 0 on the edges)  */
    int pending_flag;       /* DAT_0052afa4  (in/out; cleared on its branch) */
    int tile_index;         /* DAT_00568974  (set by resolve_hover)          */
    int copy_tile;          /* _DAT_0057a0d4 (out; = tile_index)             */
    int cell_index;         /* DAT_0053f064  (out; tile_index / tile_stride) */
    int col;                /* _DAT_0053e98c (out; cell_index % map_width)   */
    int row;                /* _DAT_0053e99c (out; cell_index / map_width)   */
    int compute_tile;       /* DAT_0056795c  (out; = tile_index — compute in)*/
    int county_id;          /* DAT_0052afbc  (set by terrain_compute)        */
    int head_link;          /* DAT_005681e0  (set by terrain_compute)        */
    int county_store;       /* DAT_00553e68  (out; = county_id iff != 0)     */
    int state_5681cc;       /* _DAT_005681cc (out; set to 3 on the open path)*/
    int cur_army;           /* DAT_0057c93c  (in/out; army-hover branch)     */
    int army_ptr;           /* DAT_0053f0dc  (out; army_active_plane[head])  */
    int scene_flag;         /* DAT_004eb260  (out; set 1 on the army branch) */
    int army_selected;      /* 1 iff army_select fired (testing aid)         */
} engine_terrain_info_open_state_t;

/* ── Sub-call hooks ───────────────────────────────────────────────────────
 * All receive the input snapshot + the mutable state.  `resolve_hover`
 * (FUN_00429ba4) must set st->tile_index; `terrain_compute` (FUN_0046d5fe)
 * must set st->county_id + st->head_link (and, live, publish the render
 * globals).  `army_select` (FUN_0043278b) reads st->army_ptr.  NULL = no-op.
 */
typedef void (*engine_terrain_info_hook_fn)(const engine_terrain_info_open_in_t *in,
                                            engine_terrain_info_open_state_t *st);

typedef struct engine_terrain_info_hooks {
    engine_terrain_info_hook_fn resolve_hover;   /* FUN_00429ba4(1)  → tile_index */
    engine_terrain_info_hook_fn terrain_compute; /* FUN_0046d5fe → county_id/head */
    engine_terrain_info_hook_fn dismiss_scroll;  /* FUN_00476768 (scroll up)      */
    engine_terrain_info_hook_fn mode2_jump;      /* 0x4350a1 (view-mode 2)    */
    engine_terrain_info_hook_fn army_select;     /* FUN_0043278b(army_ptr)        */
    engine_terrain_info_hook_fn map_render;      /* FUN_004050c0                  */
    engine_terrain_info_hook_fn panel_render;    /* FUN_0041b032 (the panel)      */
    engine_terrain_info_hook_fn tail;            /* 0x4b37bc                  */
} engine_terrain_info_hooks_t;

/* Run the terrain-info open (retail FUN_0043caf4).  Reads `in`, mutates
 * `*st`, fires hooks in retail's order.  Pass NULL for `hooks` to run the
 * pure skeleton (every sub-call inert).  Void, like retail. */
void engine_terrain_info_open(const engine_terrain_info_open_in_t *in,
                              engine_terrain_info_open_state_t *st,
                              const engine_terrain_info_hooks_t *hooks);

/* ════════════════════════════════════════════════════════════════════════
 * Screen-4 terrain-info PANEL RENDER — port of FUN_0041befe (778 B, the
 * simple-terrain arm of FUN_0041b032) + FUN_0041c208 (1934 B, the
 * string/sprite classifier + draws).  The complex county/army panel
 * 0x41b19d and the three army-roster sub-renderers (0x41cb3a /
 * 0x41d299 / 0x41da2f, each fired only for armies / special
 * features) are DEFERRED — guarded behind the same flag/type branches
 * retail uses, so the common terrain tiles ("Here Be Dragons" / named
 * terrain) render and the deferred arms are visibly stubbed.
 * ════════════════════════════════════════════════════════════════════════ */

/* The terrain-attribute bundle the classifier reads — exactly the globals
 * the terrain-info compute (engine_map_query_terrain_info) leaves behind,
 * plus the few extra strategic-state globals FUN_0041c208 reads. */
typedef struct engine_terrain_info_attr {
    int flags;          /* DAT_005440b0 — post-masked terrain flag byte    */
    int terrain_type;   /* DAT_0057cb2c — origin cell terrain type         */
    int head_link;      /* DAT_005681e0 — army/territory record index      */
    int owner;          /* DAT_005681d0 — record's owner realm             */
    int player_id;      /* DAT_0057c8cc — local/current player realm       */
    int derived_bit;    /* DAT_005651bc — compute's derived bit            */
    int f25c;           /* DAT_0053f25c — detail toggle (special arm)      */
    /* g_per_army_768 (DAT_0053f9b0, 0x300 stride): the classifier reads
     * byte/int fields at army_block[head_link*0x300 + off].  NULL ⇒ 0. */
    const uint8_t *army_block;
    /* &DAT_004d2ec8 special-feature 4-int table (stride 0x10), indexed by
     * terrain_type; read only on the flags&0x20 arm.  NULL ⇒ that arm
     * yields zeroes (a deferred special feature). */
    const int32_t *terr_table;
} engine_terrain_info_attr_t;

/* FUN_0041befe classification result. */
typedef struct engine_terrain_info_panel_class {
    int row;            /* DAT_00553d2c — panel row count / string row     */
    int off;            /* DAT_005651c8 — vertical offset (0 or 2)         */
} engine_terrain_info_panel_class_t;

/* FUN_0041c208 classification result (string/sprite indices + the
 * army-alert arg the draw driver fires via army_move_alert). */
typedef struct engine_terrain_info_text_class {
    int idx20;          /* local_20 — title string sub-index               */
    int idx1c;          /* local_1c — body string sub-index                */
    int idx18;          /* local_18 — army stat slot (0..3)                */
    int idx8;           /* local_8  — terrain icon sprite index            */
    int idxc;           /* local_c  — secondary title / body-mode selector */
    int alert_arg;      /* 0x426216(n) arg, 0 = none                   */
} engine_terrain_info_text_class_t;

/* Pure classifiers — port the index logic of FUN_0041befe / FUN_0041c208
 * with no draws.  Unit-testable against the decompile branch table. */
engine_terrain_info_panel_class_t
engine_terrain_info_classify_panel(const engine_terrain_info_attr_t *a);

engine_terrain_info_text_class_t
engine_terrain_info_classify_text(const engine_terrain_info_attr_t *a);

/* Render-context assets (the fonts / glyph maps / loc / sprite sheets the
 * panel draws from — same set the event-scroll renderer plumbs). */
typedef struct engine_terrain_info_render_ctx {
    const uint8_t           *chrome;       /* panels.pl8 (parchment box)    */
    const uint8_t           *system_sheet; /* system.pl8 (the corner seal)  */
    const uint8_t           *title_font;   /* fntl2_22 (DAT_005b2fa0)        */
    const uint8_t           *menu_font;    /* fntl2_14 (DAT_005af8f0)        */
    const uint8_t           *glyph_table;  /* gfx_glyph_render_map()         */
    const unsigned char     *glyph_metrics;/* gfx_glyph_measure_map()        */
    const uint8_t           *icon_sheet;   /* icon_tmp.pl8 (terrain icon)    */
    const uint8_t           *sel_sheet;    /* DAT_0055ce5c selection blob    */
    int                      map_id;       /* DAT_0053f034 — county name idx */
    void                    *loc;          /* localization_state_t *         */
} engine_terrain_info_render_ctx_t;

/* Render the screen-4 simple-terrain panel (FUN_0041befe → FUN_0041c208).
 * Draws to the bound gfx canvas.  `ctx->loc` is a localization_state_t*
 * (void* to keep this header free of the localization include). */
void engine_terrain_info_render(const engine_terrain_info_attr_t *a,
                                const engine_terrain_info_render_ctx_t *ctx);

#endif /* OPENLORDS2_ENGINE_TERRAIN_INFO_H */
