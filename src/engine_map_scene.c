/*
 * OpenLords2 — strategic-map BODY composition driver.  See engine_map_scene.h and
 * docs/findings/render-mapview.md.
 *
 * Port of FUN_004050c0 (+ the GOG pacer stub FUN_004cfb08 it detours through at 0x4050f0) on the shared
 * retail-layout globals (engine_mapview_globals.h): the 16 ms rate gate on the two animation counters, the eight
 * sub-phases, the county votes the terrain rows cast, the three passes, the redraw-gate decay, and the
 * current-county follow (view mode 0).
 */

#include <stdlib.h>

#include "engine_map_scene.h"

#include "engine_map_render.h"
#include "engine_map_terrain.h"
#include "engine_mapview_globals.h"
#include "engine_overlay_render.h"

/* ── the clock (retail `call ds:__imp_GetTickCount` ×2) ──────────────── */

static uint32_t g_now;                                  /* the standalone's injected now_ms */
static uint32_t default_clock(void) { return g_now; }
static engine_map_scene_clock_fn g_clock = default_clock;

void engine_map_scene_bind_clock(engine_map_scene_clock_fn fn) { g_clock = fn ? fn : default_clock; }

/* ── pieces of FUN_004050c0 ────────────────────────────────────────── */

/* 0x4050d3..0x4cfb43: the redraw phase, the pacer stub's 16 ms gate on the two counters. */
static void advance_counters(void)
{
    if (++g_mv_redraw_phase > 3)
        g_mv_redraw_phase = 0;
    if (g_mv_tick_now != 0) {
        g_mv_tick_prev = g_mv_tick_now;
        g_mv_tick_now = g_clock();
        if ((uint32_t)(g_mv_tick_now - g_mv_tick_prev) < 0x10)
            return;
    }
    ++g_mv_anim_378;
    g_mv_tick_now = g_clock();
    ++g_mv_anim_388;
}

/* 0x4050f6..0x4051ae: wrap the counters, chain the sub-phases (each the previous >> 1). */
void engine_map_scene_advance_animation(void)
{
    if (g_mv_anim_378 >= 128) g_mv_anim_378 = 0;
    g_mv_sub_378_1 = g_mv_anim_378 >> 1;
    g_mv_sub_378_2 = g_mv_sub_378_1 >> 1;
    g_mv_sub_378_3 = g_mv_sub_378_2 >> 1;
    g_mv_overlay_base = g_mv_sub_378_3 >> 1;
    if (g_mv_anim_388 >= 96) g_mv_anim_388 = 0;
    g_mv_sub_388_1 = g_mv_anim_388 >> 1;
    g_mv_sub_388_2 = g_mv_sub_388_1 >> 1;
    g_mv_sub_388_3 = g_mv_sub_388_2 >> 1;
    g_mv_arrow_base = g_mv_sub_388_3 >> 1;
}

/* 0x4051ae..: the votes, the passes, the gate decay, the county follow. */
static void body(void)
{
    int v4 = (int)s_slot_0057c93c;
    int v1 = 0;
    for (int i = 0; i < 33; ++i)
        g_mv_county_votes[i] = 0;
    g_mv_county_votes[s_slot_0057c93c] = 30;
    {
        /* DEV: OL2_SKIP_PASSES=<mask> drops passes for diagnosis (bit0 terrain, bit1 overlay, bit2 features). */
        static int skip = -1;
        if (skip < 0) { const char *sp = getenv("OL2_SKIP_PASSES"); skip = sp ? atoi(sp) : 0; }
        if (!(skip & 1)) engine_map_terrain_frame();     /* FUN_0040526e */
        if (!(skip & 2)) engine_overlay_render_frame();  /* FUN_00405602 */
        if (!(skip & 4)) engine_map_render_frame();      /* FUN_00405487 */
    }
    int gate = engine_map_terrain_empty_redraw_gate();  /* DAT_0053e9ac */
    if (gate != 0)
        engine_map_terrain_set_empty_redraw_gate(gate - 1);
    for (int j = 1; j < 32; ++j) {
        if (g_mv_county_votes[j] > v1) {
            v1 = g_mv_county_votes[j];
            v4 = j;
        }
    }
    if (v4 != (int)s_slot_0057c93c && g_mv_view_mode == 0)
        s_slot_0057c93c = (uint32_t)v4;
}

/* Port of FUN_004050c0. */
void engine_map_scene_tick_frame(void)
{
    g_mv_error_count = 0;
    advance_counters();
    engine_map_scene_advance_animation();
    body();
}

/* ── the standalone's entry points ─────────────────────────────────── */

void engine_map_scene_reset(void)
{
    g_mv_anim_378 = 0;
    g_mv_anim_388 = 0;
    g_mv_redraw_phase = 0;
    g_mv_tick_now = 0;
    g_mv_tick_prev = 0;
    engine_map_scene_advance_animation();
}

void engine_map_scene_set_anim_counters(int counter_378, int counter_388)
{
    g_mv_anim_378 = counter_378;
    g_mv_anim_388 = counter_388;
    /* derive without the wrap (the seats pin a phase; the next tick wraps) */
    g_mv_sub_378_1 = g_mv_anim_378 >> 1; g_mv_sub_378_2 = g_mv_sub_378_1 >> 1;
    g_mv_sub_378_3 = g_mv_sub_378_2 >> 1; g_mv_overlay_base = g_mv_sub_378_3 >> 1;
    g_mv_sub_388_1 = g_mv_anim_388 >> 1; g_mv_sub_388_2 = g_mv_sub_388_1 >> 1;
    g_mv_sub_388_3 = g_mv_sub_388_2 >> 1; g_mv_arrow_base = g_mv_sub_388_3 >> 1;
}

void engine_map_scene_render_body(void)
{
    engine_map_scene_advance_animation();
    body();
}

void engine_map_scene_advance_frame_counters(uint32_t now_ms)
{
    g_now = now_ms;
    advance_counters();
}

void engine_map_scene_tick(uint32_t now_ms)
{
    g_now = now_ms;
    engine_map_scene_tick_frame();
}

/* ── Accessors ──────────────────────────────────────────────────────── */

int engine_map_scene_overlay_base_index(void) { return g_mv_overlay_base; }
int engine_map_scene_arrow_marker_index(void)  { return g_mv_arrow_base; }
int engine_map_scene_anim_counter_378(void)     { return g_mv_anim_378; }
int engine_map_scene_anim_counter_388(void)     { return g_mv_anim_388; }
int engine_map_scene_redraw_phase(void)         { return g_mv_redraw_phase; }

int engine_map_scene_subphase_378(int shift)
{
    switch (shift) {
    case 1: return g_mv_sub_378_1;
    case 2: return g_mv_sub_378_2;
    case 3: return g_mv_sub_378_3;
    case 4: return g_mv_overlay_base;
    default: return 0;
    }
}
int engine_map_scene_subphase_388(int shift)
{
    switch (shift) {
    case 1: return g_mv_sub_388_1;
    case 2: return g_mv_sub_388_2;
    case 3: return g_mv_sub_388_3;
    case 4: return g_mv_arrow_base;
    default: return 0;
    }
}
