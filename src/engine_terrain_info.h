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
    engine_terrain_info_hook_fn mode2_jump;      /* FUN_004350a1 (view-mode 2)    */
    engine_terrain_info_hook_fn army_select;     /* FUN_0043278b(army_ptr)        */
    engine_terrain_info_hook_fn map_render;      /* FUN_004050c0                  */
    engine_terrain_info_hook_fn panel_render;    /* FUN_0041b032 (the panel)      */
    engine_terrain_info_hook_fn tail;            /* FUN_004b37bc                  */
} engine_terrain_info_hooks_t;

/* Run the terrain-info open (retail FUN_0043caf4).  Reads `in`, mutates
 * `*st`, fires hooks in retail's order.  Pass NULL for `hooks` to run the
 * pure skeleton (every sub-call inert).  Void, like retail. */
void engine_terrain_info_open(const engine_terrain_info_open_in_t *in,
                              engine_terrain_info_open_state_t *st,
                              const engine_terrain_info_hooks_t *hooks);

#endif /* OPENLORDS2_ENGINE_TERRAIN_INFO_H */
