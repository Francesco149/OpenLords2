/*
 * OpenLords2 — strategic-map BODY composition driver.
 * See engine_map_scene.h.
 *
 * Faithful port of FUN_004050c0 / FUN_004cfb08 / FUN_004050f6.
 */

#include <stdlib.h>

#include "engine_map_scene.h"

#include "engine_map_render.h"
#include "engine_map_terrain.h"
#include "engine_overlay_render.h"

/* ── Animation state (retail globals) ───────────────────────────────── */

static int g_counter_378;       /* DAT_0057d378 — wraps at 0x80 */
static int g_counter_388;       /* DAT_0057d388 — wraps at 0x60 */
static int g_redraw_phase_380;  /* DAT_0057d380 — 0..3 (FUN_004050c0) */

/* Rate-limiter timestamps (_DAT_005ceffc current / _DAT_005ceff8 previous).
 * "first call" is g_tick_valid == 0, mirroring retail's _DAT_005ceffc == 0. */
static uint32_t g_last_tick;
static int      g_tick_valid;

/* Derived sub-phases (FUN_004050f6).  Only [3] (>>4) of each set is read by
 * ported clusters; the rest are kept for fidelity / future consumers. */
static int g_sub_378[5];        /* index = shift; [1..4] used, [0] unused */
static int g_sub_388[5];

/* ── Helpers ────────────────────────────────────────────────────────── */

/* Re-derive the eight sub-phases from the two frame counters and publish the
 * two consumed ones to the clusters.  FUN_004050f6 lines:
 *   DAT_0057d3a8 = c378>>1; DAT_0057d394 = c378>>2;
 *   DAT_0057d39c = c378>>3; DAT_0057d390 = c378>>4;   (overlay base)
 *   DAT_0057d3a4 = c388>>1; DAT_0057d3a0 = c388>>2;
 *   DAT_0057d398 = c388>>3; _DAT_0057d38c = c388>>4;  (arrow marker) */
static void derive_subphases(void)
{
    g_sub_378[1] = g_counter_378 >> 1;
    g_sub_378[2] = g_counter_378 >> 2;
    g_sub_378[3] = g_counter_378 >> 3;
    g_sub_378[4] = g_counter_378 >> 4;   /* DAT_0057d390  — overlay base */

    g_sub_388[1] = g_counter_388 >> 1;
    g_sub_388[2] = g_counter_388 >> 2;
    g_sub_388[3] = g_counter_388 >> 3;
    g_sub_388[4] = g_counter_388 >> 4;   /* _DAT_0057d38c — arrow marker */

    /* DAT_0057d390 is read by BOTH the overlay cluster and the feature
     * walker; _DAT_0057d38c by the overlay cluster only. */
    engine_overlay_render_set_overlay_base_index(g_sub_378[4]);
    engine_map_render_set_overlay_base_index(g_sub_378[4]);
    engine_overlay_render_set_arrow_marker_base_index(g_sub_388[4]);
}

/* ── Public API ─────────────────────────────────────────────────────── */

void engine_map_scene_reset(void)
{
    g_counter_378 = 0;
    g_counter_388 = 0;
    g_redraw_phase_380 = 0;
    g_last_tick = 0;
    g_tick_valid = 0;
    derive_subphases();
}

void engine_map_scene_set_anim_counters(int counter_378, int counter_388)
{
    g_counter_378 = counter_378;
    g_counter_388 = counter_388;
    derive_subphases();
}

void engine_map_scene_advance_animation(void)
{
    /* FUN_004050f6 wraps the counters BEFORE deriving the sub-phases. */
    if (g_counter_378 > 0x7f) g_counter_378 = 0;
    if (g_counter_388 > 0x5f) g_counter_388 = 0;
    derive_subphases();
}

void engine_map_scene_render_body(void)
{
    engine_map_scene_advance_animation();

    /* The column-height accumulator (DAT_0052f020) + camera-follow-row
     * (DAT_0057c93c) setup that brackets the cluster calls is DEFERRED:
     * the pass-1 row iterators that fill the accumulator are collapsed in
     * the ported pass-1, so the follow-row result is inert and only steers
     * the next frame's camera.  See the header / map-render.md. */

    /* Three clusters, retail draw order. */
    {
        /* DEV: OL2_SKIP_PASSES=<mask> drops passes for diagnosis
         * (bit0=terrain, bit1=overlay, bit2=features). */
        const char *sp = getenv("OL2_SKIP_PASSES");
        int skip = sp ? atoi(sp) : 0;
        if (!(skip & 1)) engine_map_terrain_frame();    /* FUN_0040526e — pass 1 base terrain */
        if (!(skip & 2)) engine_overlay_render_frame(); /* FUN_00405602 — pass 2 overlay       */
        if (!(skip & 4)) engine_map_render_frame();     /* FUN_00405487 — pass 3 features      */
    }

    /* if (DAT_0053e9ac != 0) DAT_0053e9ac--;  (next-frame empty-redraw-gate decay) */
    {
        int gate = engine_map_terrain_empty_redraw_gate();
        if (gate != 0) engine_map_terrain_set_empty_redraw_gate(gate - 1);
    }
}

void engine_map_scene_advance_frame_counters(uint32_t now_ms)
{
    /* FUN_004050c0: bump the 0..3 redraw phase. */
    g_redraw_phase_380++;
    if (g_redraw_phase_380 > 3) g_redraw_phase_380 = 0;

    /* FUN_004cfb08: 16 ms rate gate on the frame counters. */
    if (g_tick_valid) {
        uint32_t prev = g_last_tick;
        g_last_tick = now_ms;
        if ((uint32_t)(now_ms - prev) < 0x10) return;
    }
    g_counter_378++;
    g_last_tick = now_ms;
    g_tick_valid = 1;
    g_counter_388++;
}

void engine_map_scene_tick(uint32_t now_ms)
{
    engine_map_scene_advance_frame_counters(now_ms);
    engine_map_scene_render_body();
}

/* ── Accessors ──────────────────────────────────────────────────────── */

int engine_map_scene_overlay_base_index(void) { return g_sub_378[4]; }
int engine_map_scene_arrow_marker_index(void)  { return g_sub_388[4]; }
int engine_map_scene_anim_counter_378(void)     { return g_counter_378; }
int engine_map_scene_anim_counter_388(void)     { return g_counter_388; }
int engine_map_scene_redraw_phase(void)         { return g_redraw_phase_380; }

int engine_map_scene_subphase_378(int shift)
{
    if (shift < 1 || shift > 4) return 0;
    return g_sub_378[shift];
}
int engine_map_scene_subphase_388(int shift)
{
    if (shift < 1 || shift > 4) return 0;
    return g_sub_388[shift];
}
