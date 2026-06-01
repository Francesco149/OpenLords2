/*
 * OpenLords2 — strategic-map BODY composition driver.
 *
 * Ports the map-body draw chain that screen-dispatch arm 0
 * (FUN_0040f5fd, the strategic-map renderer) calls to paint the
 * overworld, sitting one level above the three already-ported tile
 * clusters:
 *
 *   FUN_004050c0 (53 B)  → engine_map_scene_tick   (anim-phase counter + rate limiter)
 *   FUN_004cfb08 (64 B)  → (folded into the tick)  (GetTickCount 16 ms gate)
 *   FUN_004050f6 (376 B) → engine_map_scene_render_body
 *       ├─ FUN_0040526e  → engine_map_terrain_frame   (pass 1 base terrain)
 *       ├─ FUN_00405602  → engine_overlay_render_frame (pass 2 overlay)
 *       └─ FUN_00405487  → engine_map_render_frame     (pass 3 features)
 *
 * KEY RE FINDING (2026-06-01).  FUN_004050f6 derives EIGHT animation
 * sub-phases from two free-running frame counters and writes them to
 * the globals the clusters read:
 *
 *   DAT_0057d378 (wraps at 0x80)      DAT_0057d388 (wraps at 0x60)
 *     >>1 → DAT_0057d3a8                >>1 → DAT_0057d3a4
 *     >>2 → DAT_0057d394                >>2 → DAT_0057d3a0
 *     >>3 → DAT_0057d39c                >>3 → DAT_0057d398
 *     >>4 → DAT_0057d390  *             >>4 → _DAT_0057d38c  *
 *
 * The two starred sub-phases are the ones the ported clusters consume:
 *   DAT_0057d390  = the "overlay base index" read by BOTH
 *                   engine_overlay_render and engine_map_render, and
 *   _DAT_0057d38c = the "arrow-marker base index" read by
 *                   engine_overlay_render.
 * i.e. those two bind parameters were never static configuration —
 * they are animation phases (counter>>4, cycling 0..7).  Earlier the
 * scene harness fed them the captured value (0); this module computes
 * them from the counter so the harness can sweep animation phases.
 * The other six sub-phases are written for fidelity but no ported code
 * reads them yet.
 *
 * DEFERRED (next-frame-only, no effect on the rendered frame): the
 * column-height accumulator DAT_0052f020[0..0x20] and the resulting
 * camera-follow-row update DAT_0057c93c.  Those are filled by the
 * pass-1 row iterators FUN_00405ae9 / FUN_00405c2f, which the ported
 * pass-1 collapses, so the accumulator stays empty and the follow-row
 * computation is inert.  It is computed AFTER the clusters draw and
 * only steers the NEXT frame's camera, so omitting it does not change
 * this frame's pixels.  See docs/findings/map-render.md.
 *
 * This module performs NO binding — exactly like FUN_004050f6, which
 * assumes the clusters' tile arrays / blobs / camera are already bound
 * (by the season loader + camera setup in retail, or by the caller /
 * harness in the port).
 */

#ifndef OPENLORDS2_ENGINE_MAP_SCENE_H
#define OPENLORDS2_ENGINE_MAP_SCENE_H

#include <stdint.h>

/* Reset the animation state to retail's initial values (both frame
 * counters 0, anim-tick phase 0, rate-limiter "first call"). */
void engine_map_scene_reset(void);

/* Set the two free-running frame counters directly (retail DAT_0057d378,
 * DAT_0057d388).  For deterministic parity captures — the scene harness
 * pins them to the captured frame's phase. */
void engine_map_scene_set_anim_counters(int counter_378, int counter_388);

/* FUN_004050c0 + FUN_004cfb08 (counter half, render-free): advance the
 * 0..3 redraw-phase counter (DAT_0057d380) and, when at least 0x10 ms have
 * elapsed since the last advance, bump both frame counters.  `now_ms` is the
 * GetTickCount() value (injected for determinism). */
void engine_map_scene_advance_frame_counters(uint32_t now_ms);

/* FUN_004050c0 + FUN_004cfb08: advance the frame counters then render the
 * body.  Native per-frame entry.  Deterministic captures use
 * set_anim_counters + render_body instead. */
void engine_map_scene_tick(uint32_t now_ms);

/* FUN_004050f6's animation prologue: wrap the frame counters (at 0x80 /
 * 0x60), re-derive the eight sub-phases, and push the two consumed ones
 * into the clusters.  Exposed as a test seam (and reused by render_body).
 * Does NOT touch the canvas. */
void engine_map_scene_advance_animation(void);

/* FUN_004050f6: advance the animation, then draw the three clusters in
 * retail order (pass 1 terrain → pass 2 overlay → pass 3 features) and
 * decrement the sky gate.  Assumes the clusters are already bound. */
void engine_map_scene_render_body(void);

/* Accessors for the derived sub-phases (testing / introspection). */
int engine_map_scene_overlay_base_index(void);   /* DAT_0057d390  = c378>>4 */
int engine_map_scene_arrow_marker_index(void);    /* _DAT_0057d38c = c388>>4 */
int engine_map_scene_anim_counter_378(void);
int engine_map_scene_anim_counter_388(void);
int engine_map_scene_redraw_phase(void);          /* DAT_0057d380, 0..3 */
int engine_map_scene_subphase_378(int shift);      /* shift ∈ {1,2,3,4} */
int engine_map_scene_subphase_388(int shift);      /* shift ∈ {1,2,3,4} */

#endif /* OPENLORDS2_ENGINE_MAP_SCENE_H */
