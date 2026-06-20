/*
 * OpenLords2 — terrain-info ("Here Be Dragons") open orchestration.
 *
 * Port of FUN_0043caf4 @ 0x43caf4 (354 B).  Branch shape is verbatim from
 * the decompile (docs/decompiled/by-address/43caf4.c).  See the header for
 * the design + the retail global ↔ field mapping.
 */

#include <stddef.h>

#include "engine_terrain_info.h"

#include "call_trace.h"

/* NULL-safe hook caller. */
static void call_hook(engine_terrain_info_hook_fn fn,
                      const engine_terrain_info_open_in_t *in,
                      engine_terrain_info_open_state_t *st)
{
    if (fn) {
        fn(in, st);
    }
}

/* Army "active" field: *(int*)(army_active_plane + head_link*0x300),
 * little-endian, NULL plane ⇒ 0.  (Retail reads &DAT_0053fa20 directly.) */
static int army_active_field(const uint8_t *plane, int head_link)
{
    if (plane == NULL) {
        return 0;
    }
    const uint8_t *p = plane + (size_t)head_link * 0x300;
    return (int)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

void engine_terrain_info_open(const engine_terrain_info_open_in_t *in,
                              engine_terrain_info_open_state_t *st,
                              const engine_terrain_info_hooks_t *hooks)
{
    CALL_TRACE_ENTER(0x0043caf4);
    static const engine_terrain_info_hooks_t empty_hooks = {0};
    if (!hooks) {
        hooks = &empty_hooks;
    }

    /* `if (DAT_0057c8fc == 0)` — terrain-info only opens in campaign mode. */
    if (in->game_mode != 0) {
        return;
    }

    /* FUN_00429ba4(1): re-resolve the hovered cell → st->tile_index. */
    call_hook(hooks->resolve_hover, in, st);

    /* Publish the hovered cell's geometry (the globals the panel + other
     * systems read).  Division guarded against a 0 stride/width — retail
     * never sees those (tile_stride is 8, map_width 0x40 once the map is
     * loaded), but the resolver/skeleton can run before that. */
    st->copy_tile    = st->tile_index;                                /* _DAT_0057a0d4 */
    st->cell_index   = in->tile_stride ? st->tile_index / in->tile_stride : 0; /* DAT_0053f064 */
    st->col          = in->map_width ? st->cell_index % in->map_width : 0;     /* _DAT_0053e98c */
    st->row          = in->map_width ? st->cell_index / in->map_width : 0;     /* _DAT_0053e99c */
    st->compute_tile = st->tile_index;                                /* DAT_0056795c */

    /* `if (DAT_0052afa4 == 0)` — the guard.  Nonzero ⇒ snap back to the
     * strategic map, clear the guard, no panel. */
    if (st->pending_flag != 0) {
        st->screen       = 0;           /* DAT_004eac50 = 0  */
        st->pending_flag = 0;           /* DAT_0052afa4 = 0  */
        return;
    }

    /* `if (DAT_0057cb18 == 2)` — alt view-mode: snap to the map + run the
     * mode-2 jump instead of opening the panel. */
    if (in->view_mode == 2) {
        st->screen = 0;                 /* DAT_004eac50 = 0  */
        call_hook(hooks->mode2_jump, in, st);
        return;
    }

    /* FUN_0046d5fe: terrain-attribute compute → st->county_id, st->head_link
     * (and, live, the render globals DAT_005440b0 / DAT_0057cb2c / …). */
    call_hook(hooks->terrain_compute, in, st);

    if (st->county_id != 0) {
        st->county_store = st->county_id;   /* DAT_00553e68 = DAT_0052afbc */
    }
    if (in->scroll_open != 0) {
        call_hook(hooks->dismiss_scroll, in, st);   /* FUN_00476768 */
    }
    st->state_5681cc = 3;                   /* _DAT_005681cc = 3 */

    /* Army-hover branch: the tile carries an army (head_link != 0) that is
     * not already selected (!= cur_army) AND no county is under the cursor.
     * Select that army + re-render the map. */
    if (st->head_link != 0 && st->head_link != st->cur_army && st->county_id == 0) {
        st->army_ptr = army_active_field(in->army_active_plane, st->head_link); /* DAT_0053f0dc */
        if (st->army_ptr != 0) {
            st->cur_army      = st->head_link;      /* DAT_0057c93c = DAT_005681e0 */
            call_hook(hooks->army_select, in, st);  /* FUN_0043278b(DAT_0053f0dc) */
            st->army_selected = 1;
        }
        st->scene_flag = 1;                         /* DAT_004eb260 = 1 */
        call_hook(hooks->map_render, in, st);       /* FUN_004050c0 */
    }

    call_hook(hooks->panel_render, in, st);         /* FUN_0041b032 */
    call_hook(hooks->tail, in, st);                 /* FUN_004b37bc */
}
