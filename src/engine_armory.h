/*
 * OpenLords2 — the Armory family: the army-muster VIEW (screen 0x0a), the
 * army CREATE/CHANGE overlay (screen 0x17) and the weapon ASSIGN overlay
 * (screen 0x0d).
 *
 * The county-interior "armory" scene: a full-screen room (armoury.pl8 over
 * armoury.256) decorated with the player's HERALDRY-colored interior sheet
 * (arm_it_r<c>.pl8), the weapons the player owns hung on the racks, the
 * bottom muster tally (men available + a count per owned weapon type), the
 * three action labels ("Create" / "Change" / "Cancel"), and the continue
 * seal.  Reached from the map's bottom-right county-action tab strip (table
 * 0x4dc680 rec0 → FUN_0043ae30 param 1, owner-gated): that opens the
 * INTERACTIVE army create/change overlay (screen 0x17, FUN_00418653, drawn
 * OVER this backdrop); RMB-ing out of 0x17 lands on the bare 0x0a view.
 * Clicking a muster figure / tally readout on 0x0a (arm_grid.pl8 zones or
 * the 6 hotspot boxes) opens the per-weapon ASSIGN overlay (screen 0x0d,
 * FUN_004184c6/FUN_00418e2d) whose four gauntlet buttons move men between
 * the unassigned pool and the selected weapon; assigning men triggers the
 * troop-march animation (FUN_004aabd8 → FUN_00418fc5/FUN_004190db) across
 * the muster floor.  "Create"/"Cancel" (FUN_00435ae8 → FUN_00435b4d) commit
 * or abandon the army — the actual spawn is the ALREADY-PORTED
 * engine_combat_spawn_unit (FUN_004a9a9a) — and close to the map.
 *
 * The two wall torches are animated on the 0x0a and 0x0d screens: retail's
 * per-frame refresh (FUN_004ba26e cases '\n' / '\r') redraws them every tick
 * from armtorch.pl8 at the shared phase frame (DAT_005aea54, cycles 0..0xc).
 * Screen 0x17's per-frame arm repaints ONLY its button strip — the torches
 * freeze while the create/change overlay is up (retail behavior).
 *
 * Retail address map:
 *   FUN_00417ea7 @ 0x417ea7 (591 B) → engine_armory_render (the 0x0a scene;
 *                                     also loads arm_grid.pl8 + armtorch.pl8
 *                                     and captures the 4 march backdrop
 *                                     strips via FUN_004b3f0a)
 *   FUN_004180f6 @ 0x4180f6 (245 B) → the reduced 0x0d→0x0a re-render; the
 *                                     port reuses engine_armory_render (the
 *                                     extra reloads/captures are byte-inert)
 *   FUN_00419243 @ 0x419243 ( 41 B) → engine_armory_refresh (per-frame: the
 *                                     two animated torch flames)
 *   FUN_004aa90a @ 0x4aa90a (374 B) → engine_armory_muster_init (the on-entry
 *                                     muster-pool setup, run before the render)
 *   FUN_00418653 @ 0x418653 (2010 B)→ engine_armory_overlay_render (screen
 *                                     0x17: conscription slider + army info)
 *   FUN_00435ebc @ 0x435ebc (336 B) → engine_armory_conscript_update
 *   FUN_00435cef @ 0x435cef (456 B) → engine_armory_slider_click
 *   FUN_004184c6 @ 0x4184c6 (397 B) → engine_armory_assign_render (screen
 *                                     0x0d: the per-weapon assign overlay)
 *   FUN_00418e2d @ 0x418e2d (216 B) → engine_armory_assign_refresh
 *   FUN_004181eb @ 0x4181eb         → engine_armory_counts_render (the bottom
 *                                     muster tally; also 0x0d's per-frame)
 *   FUN_0043582a @ 0x43582a (134 B) → engine_armory_zone_at (arm_grid lookup)
 *   FUN_004358b0 @ 0x4358b0 ( 138 B)→ engine_armory_zone_select (figure click
 *                                     → weapon select + march trigger)
 *   FUN_004aabd8 @ 0x4aabd8 (303 B) → engine_armory_march_trigger
 *   FUN_00418fc5 @ 0x418fc5 (204 B) → engine_armory_march_restore
 *   FUN_004190db @ 0x4190db (360 B) → engine_armory_march_step
 *   FUN_00435c89 / FUN_00435cbf / FUN_0043593a / FUN_004359bc / FUN_00435a0c
 *   / FUN_00435a61 → the button actions (see engine_armory_*_action)
 *
 * The counts are pure reads of the per-player troop muster pool
 * (g_troop_pool, retail DAT_0053f6a0, 0x80 stride) that muster_init fills
 * from saved state.  Per-record field offsets:
 *   strategic (g_player_strategic_352, stride 0x160, key = local player)
 *     +0x00a u8  heraldry / interior color (0..5 → arm_it_r/y/k/p/b)
 *     +0x118 i32 gold
 *     +0x138 i32 men already under arms (the "N men in armies" readout)
 *     +0x13c + t*4  weapon[t] stock, t=1..6  (== treasury's +0x140 + i*4)
 *   county (g_per_army_768, stride 0x300, key = selected county 0x57c93c)
 *     +0x005 u8  owner player
 *     +0x00c s8  happiness
 *     +0x024 i32 population (conscriptable base)
 *     +0x1ad u8  existing army id (1-based into g_unit_table_20; 0 = none)
 *     +0x2f4 i32 conscription happiness-cost modifier
 *   unit table (g_unit_table_20, retail 0x568dc0, stride 0x14, 1-based)
 *     +0x005 u8  unit-noun index (grp 8 sub = idx*2 + 0x34)
 *     +0x008 i32 army size (men)
 *     +0x00c i32 change cost (Crowns)
 *   muster pool (g_troop_pool, stride 0x80, key = county OWNER)
 *     entry e at +e*0x10:  +0 count   +4 shown gate   +8 stock cap
 *                          +0xc count-at-assign-open (march trigger compare)
 *     entry 0 = unassigned men, 1..6 = weapons, 7 = total
 */

#ifndef OPENLORDS2_ENGINE_ARMORY_H
#define OPENLORDS2_ENGINE_ARMORY_H

#include <stdint.h>

#include "engine_button.h"
#include "engine_dirty_rect.h"
#include "engine_menu_input.h"
#include "localization.h"

/* ── Retail-layout globals (engine_armory_globals.c; the hybrid host aliases each at its VA) ── */
extern int32_t g_arm_pct;            /* DAT_0056d65c conscription % (persists) */
extern int32_t g_arm_conscripts;     /* DAT_00543fd8 men at the current % */
extern int32_t g_arm_happy_cost;     /* DAT_00565400 */
extern int32_t g_arm_mode;           /* DAT_00522f58 overlay button count (1 / 3) */
extern int32_t g_arm_merc;           /* DAT_0055446c hire-mercenaries toggle */
extern int32_t g_arm_weapon;         /* DAT_00553f20 assign-screen weapon (1..6) */
extern int32_t g_arm_march_active;   /* DAT_005679d0 */
extern int32_t g_arm_march_x;        /* DAT_0056d630 */
extern int32_t g_arm_march_y;        /* DAT_0056d62c */
extern int32_t g_arm_march_target;   /* DAT_00568228 */
extern int32_t g_arm_march_land;     /* DAT_005681f8 salute frame counter */
extern int32_t g_arm_march_sprite;   /* DAT_0052f008 */
extern int32_t g_arm_march_walk;     /* DAT_0057cb10 walk cycle 0..7 */
extern int32_t g_arm_glint;          /* DAT_0057c8e4 */
extern uint8_t *g_arm_interior;      /* DAT_0056d5b8 the interior sheet (arm_it_<c>.pl8) */
extern uint8_t *g_arm_torch;         /* DAT_005533c4 the torch sheet (armtorch.pl8) */
extern uint8_t *g_arm_troop;         /* DAT_0057a0c0 the marching troop sheet */
extern engine_button_rec_t g_arm_overlay_buttons[3];   /* DAT_004dd340 the overlay's confirm + merc Yes/No */

/* The hybrid host: the march strips live in the buffer DAT_004eabec points at and the click grid is retail's
 * &DAT_00542ce0 (the standalone keeps buffers of its own); the dirty rectangle the readouts mark. */
void engine_armory_bind_retail_buffers(uint8_t *grid);
void engine_armory_set_dirty_state(engine_dirty_rect_state_t *dirty);

struct engine_armory_ctx;
/* FUN_00418426 — the owned-weapon wall racks; FUN_0041ac3e — the loc-group-8 unit noun (sub / sub+1). */
void engine_armory_racks(const struct engine_armory_ctx *ctx);
void engine_armory_unit_noun(const struct engine_armory_ctx *ctx, int value, int unit_sub, int x, int y,
                             const uint8_t *font, uint8_t color);

/* ── Render context ────────────────────────────────────────────────────
 * Built by engine.c's render seam from the save-schema externs + gfx
 * loaders (same NULL-guard convention as engine_treasury / engine_rankings:
 * a NULL pointer skips the draw that needs it).  The heraldry-colored
 * interior sheet is loaded by the render itself (indexed off the strategic
 * block), so it is NOT carried here. */
/* The open render's retail-only steps (the hybrid host binds retail's; NULL — the standalone — skips them and loads
 * the palette in one step at the end). */
typedef struct engine_armory_retail {
    void (*palette_prep)(void);                  /* 0x4b11ce (open arg 1) */
    void (*teardown_alloc)(void);                /* 0x4996c8: the map teardown + the sheets' allocation */
    void (*input_tick)(int arg);                 /* FUN_00452160(1) */
    void (*palette_apply)(const uint8_t *pal);   /* FUN_004b0ab5 */
    uint8_t *palette_mirror;                     /* &DAT_004ea8a0 */
    const char *interior_names;                  /* &DAT_004d2c88: the arm_it_<c>.pl8 names, 16 bytes each */
} engine_armory_retail_t;

typedef struct engine_armory_ctx {
    const uint8_t *strategic_block;    /* g_player_strategic_352 (0x57bf00) */
    const uint8_t *troop_pool;         /* g_troop_pool (retail 0x53f6a0, stride 0x80) */
    int local_player;                  /* s_slot_0057c8cc (== the county owner) */

    const uint8_t *county_rec;         /* g_per_army_768 + county*0x300 (0x53f9b0) */
    int county;                        /* s_slot_0057c93c — selected county   */
    int map_id;                        /* s_slot_0053f034 — county-name page  */
    const uint8_t *unit_table;         /* g_unit_table_20 (retail 0x568dc0)   */

    const uint8_t *system_sheet;       /* system2.pl8 (seal + slider chrome)  */
    const uint8_t *chrome;             /* parchment chrome (gfx_panel_draw)   */
    const uint8_t *menu_font;          /* fntl2_14 (DAT_005af8f0)             */
    const uint8_t *title_font;         /* fntl2_22 (DAT_005b2fa0)             */
    const uint8_t *glyph_table;
    const unsigned char *glyph_metrics;
    localization_state_t *loc;
    int open_arg;                      /* FUN_00417ea7's argument (1: the palette prep) */
    const engine_armory_retail_t *retail;   /* NULL: the standalone */
} engine_armory_ctx_t;

/* ── FUN_004aa90a — the on-entry muster-pool setup ──────────────────────
 * Zeroes the county owner's muster row, seeds the weapon stock gates (+4/+8)
 * from the owner's strategic weapon stocks (rec +0x13c + t*4, t=1..6), and
 * writes `population` (the men conscriptable at the current % — the
 * conscript count on entry from the tab / the 0x17 confirm) into the
 * men/total entries.  Also resets the selected weapon + the march (retail
 * tail: DAT_00553f20 = 0, DAT_005679d0 = 0, _DAT_0057c8e4 = 0). */
void engine_armory_muster_init(int county_owner, int population,
                               const uint8_t *owner_strat_rec);

/* ── FUN_00417ea7 — the OPEN render of the Armory view (screen 0x0a) ─────
 * A FULL-SCREEN scene: loads armoury.pl8 into the back buffer + the player's
 * arm_it_r<c>.pl8 interior sheet + arm_grid.pl8 (the muster-figure click
 * zones) + armtorch.pl8, draws the owned-weapon racks, captures the four
 * troop-march backdrop strips (FUN_004b3f0a ×4), draws the muster counts,
 * the three action labels and the seal, then applies armoury.256 as the
 * active palette last (so it lands on the settled frame — the screen-0
 * close arm restores base01.256, same as the merchant/rankings scenes). */
void engine_armory_render(const engine_armory_ctx_t *ctx);

/* ── FUN_004181eb — the bottom muster tally (also 0x0d's per-frame) ──────*/
void engine_armory_counts_render(const engine_armory_ctx_t *ctx);

/* ── FUN_00419243 — the per-frame torch-flame refresh (0x0a / 0x0d) ──────
 * Redraws the two wall torches from armtorch.pl8 (loaded by the render) over
 * the retained backdrop: sprite `torch_frame` at (0x9a,0x76) and sprite
 * `torch_frame + 0xd` at (0x19a,0x76).  `torch_frame` is the shared phase
 * counter (retail DAT_005aea54 / g_phase_counter_13, 0..0xc).  A no-op until
 * the render has loaded the torch sheet. */
void engine_armory_refresh(int torch_frame);

/* ═══ Screen 0x17 — the army create/change overlay ══════════════════════ */

/* ── FUN_00435ebc — conscription % → men + happiness cost ────────────────
 * conscripts = pct% of county population (+0x24); happiness cost =
 * k_conscript_happy[pct] (+ the county's +0x2f4 modifier when pct != 0),
 * clamped 0..100; while the county's happiness (+0xc) would drop below 1
 * the EFFECTIVE pct steps down (the slider global itself is NOT written
 * back — retail keeps the knob where the user put it).  A county at
 * happiness < 1 conscripts nothing. */
void engine_armory_conscript_update(const uint8_t *county_rec, int pct);

/* ── FUN_00435cef — the conscription slider hit-test ─────────────────────
 * Box x [0x80,0x176), y [ybase,ybase+0x30) where ybase = 0x90 with an
 * existing army else 0xb0.  Left third (x<0xc4): −1 on a fresh press /
 * repeat; track (x<0x129): pct = x−0xc4 while LMB HELD (drag); right third:
 * +1.  Clamps 0..100, recomputes the conscripts, returns 1 (the caller
 * requests demand=2).  Returns 0 on no hit. */
int engine_armory_slider_click(const uint8_t *county_rec,
                               int cursor_x, int cursor_y,
                               int lmb_pressed, int lmb_repeat, int lmb_held);

/* ── FUN_00418653 — the create/change overlay render (screen 0x17) ───────
 * Drawn OVER the retained 0x0a scene (the demand-1 path re-renders the full
 * scene first; demand-2 repaints just the overlay).  Create mode (no county
 * army): a 14-row parchment at y 0x90 with the conscription slider, the
 * population/conscript readouts, the weapon stocks, the happiness cost and
 * the info text.  Change mode (army at +0x1ad): a 17-row parchment at y 0x70
 * adding the army's size/name/cost rows and — when affordable — the
 * gold readout + the mercenary Yes/No toggle.  Sets the router button count
 * (engine_armory_overlay_mode): 1 = confirm only, 3 = + the Yes/No pair.
 * Self-locks the canvas. */
void engine_armory_overlay_render(const engine_armory_ctx_t *ctx);

/* The overlay button strip (retail DAT_004dd340, 3 recs): rec0 = the
 * confirm seal (FUN_00435cbf → back to 0x0a with the muster re-seeded),
 * rec1/rec2 = the mercenary Yes/No toggle (FUN_00435c89).  Painted with
 * y-offset 0x10 in change mode (no offset in create mode).  Count =
 * engine_armory_overlay_mode() (DAT_00522f58, set by the render). */
engine_button_rec_t *engine_armory_overlay_buttons(void);
int  engine_armory_overlay_mode(void);       /* DAT_00522f58 (1 or 3)      */
int  engine_armory_conscripts(void);         /* DAT_00543fd8               */
int  engine_armory_pct(void);                /* DAT_0056d65c               */
void engine_armory_set_merc(int on);         /* DAT_0055446c (FUN_00435c89)*/
int  engine_armory_merc(void);
int  engine_armory_happy_cost(void);         /* DAT_00565400               */
int  engine_armory_weapon(void);             /* DAT_00553f20               */

/* ═══ Screen 0x0d — the weapon assign overlay ═══════════════════════════ */

/* ── FUN_0043582a head — the arm_grid.pl8 muster-figure zone lookup ──────
 * grid[(x>>3) + (y>>3)*0x50] (round-toward-zero on negatives); returns the
 * weapon id 1..6 or 0.  The grid is loaded by engine_armory_render. */
int engine_armory_zone_at(int cursor_x, int cursor_y);

/* ── FUN_004358b0 — muster-figure / tally-readout click ──────────────────
 * Gated on the weapon's stock cap (pool +8 > 0).  Commits the march check
 * for the PREVIOUSLY selected weapon (engine_armory_march_trigger), selects
 * `zone`, snapshots its count into +0xc (the next march-trigger compare).
 * Returns 1 when the select fired (the caller flips to screen 0x0d and runs
 * the assign render), 0 when the weapon has no stock. */
int engine_armory_zone_select(int zone, const engine_armory_ctx_t *ctx);

/* ── FUN_004184c6 — the assign overlay OPEN render (screen 0x0d) ─────────
 * Loads the selected weapon's demo sheet (arm_cros/mace/swor/pike/bow/mail
 * .pl8), draws the parchment strip at (0x60,4), the continue seal (mode 0)
 * at (0x1e4,0x58), the demo-figure frame 0 and the first refresh.  Drawn
 * over the retained 0x0a scene — retail calls it DIRECTLY from the click
 * (no demand-redraw case exists for 0x0d in FUN_0040f1a0).  Self-locks. */
void engine_armory_assign_render(const engine_armory_ctx_t *ctx);

/* ── FUN_00418e2d — the assign overlay per-frame refresh ─────────────────
 * The animated weapon-demo figure (frame = g_phase_counter_24), the armed
 * count + unit noun, and the "N more available" readout. */
void engine_armory_assign_refresh(const engine_armory_ctx_t *ctx,
                                  int demo_frame);

/* The assign button strip (retail DAT_004dd8c8, 4 recs, origin (0x60,4)):
 * +1 / −1 / none / all (FUN_0043593a / FUN_004359bc / FUN_00435a0c /
 * FUN_00435a61) — all type-4 hold-to-repeat gauntlets. */
engine_button_rec_t *engine_armory_assign_buttons(void);

/* The four assign actions (mutate the owner's muster pool row). */
void engine_armory_assign_plus(uint8_t *pool_row);    /* FUN_0043593a */
void engine_armory_assign_minus(uint8_t *pool_row);   /* FUN_004359bc */
void engine_armory_assign_none(uint8_t *pool_row);    /* FUN_00435a0c */
void engine_armory_assign_all(uint8_t *pool_row);     /* FUN_00435a61 */

/* The 0x0a/0x0d view hotspot table (retail DAT_004dc938, 9 recs walked by
 * FUN_0040e3ee): recs 0..5 = the six tally readout boxes (param = weapon),
 * recs 6..8 = the Create / Change / Cancel labels (param = 1/2/3).  Screen
 * 0x0a walks all 9; screen 0x0d walks only the first 7 (retail count).
 * Actions are engine.c-owned (they navigate) — the table carries params
 * only; the caller dispatches. */
const engine_menu_hotspot_t *engine_armory_view_hotspots(void);

/* ═══ The troop-march animation (armoury floor) ═════════════════════════ */

/* ── FUN_004aabd8 — arm the march when the previous weapon GAINED men ────
 * No-op unless prev_weapon > 0, its count rose past the +0xc snapshot and
 * no march is running.  Seeds the march state (start x = −0x50, y = 0xd8,
 * target from the per-weapon .rdata table 0x4de6f0) and loads the troop
 * sprite sheet trp_<w>_<c>.pl8 (heraldry color c, fallback trp_xb_b.pl8). */
void engine_armory_march_trigger(int prev_weapon,
                                 const engine_armory_ctx_t *ctx);

/* ── FUN_00418fc5 — restore the backdrop strip under the marching troop ──
 * Picks the captured strip segment (x 0 / 0xa0 / 0x140 / 0x1e0) the troop
 * is in and writes it back to the canvas.  No-op unless a march is live. */
void engine_armory_march_restore(void);

/* ── FUN_004190db — advance + draw the marching troop ────────────────────
 * Walk frames 0..7 toward the target, salute frames 8..0xc at the target,
 * then walk-out frames (+0xd) until x ≥ 0x280 despawns the march.  Movement
 * advances only on the 20 ms phase tick; the walk/salute cycle steps on the
 * 80 ms tick (retail DAT_0058fcb0 / DAT_00591510).  Draws every call. */
void engine_armory_march_step(int tick_20ms, int tick_80ms);

int engine_armory_march_active(void);        /* DAT_005679d0 */

#endif /* OPENLORDS2_ENGINE_ARMORY_H */
