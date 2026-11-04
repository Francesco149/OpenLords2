/* rl_ui2.c — a retail-layout port, partition 'ui2' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * What the UI tours reach besides the skirmish: the labour model (the workforce panel's box select and the worker
 * moves, the county's labour / production shares), the kingdom overview, the custom-game / skirmish setup actions,
 * the confirm-dialog answers, the field panel, the rankings / help pages, a few frame-driver steps and the MP session
 * open. Findings: docs/findings/rl-ui2.md. */
#include "rl.h"
#include "rl_ui2_globals.h"
#include "rl_io_globals.h"
#include "rl_render_globals.h"
#include "rl_misc_globals.h"
#include "engine_bf_input_globals.h"
#include "engine.h"
#include "engine_combat.h"
#include "engine_territory.h"
#include "engine_dirty_rect.h"
#include "engine_army_economy.h"
#include "engine_map_query.h"
#include "engine_math.h"
#include "engine_diplomacy_action.h"
#include "engine_camera.h"
#include "engine_mapview_globals.h"
#include "engine_overlay_render.h"
#include "strutil.h"
#include "gfx_globals.h"
#include "gfx_line.h"
#include "gfx_panel.h"
#include "gfx_sprite.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_wrap.h"
#include "gfx_paint.h"
#include "localization.h"
#include "rl_io_plat.h"

#include <string.h>

/* rl_glue_globals.h's (it conflicts with rl_io / rl_render_globals.h in one unit: declared here as there) */
extern int32_t g_rl_5533b0;   /* DAT_005533b0 */
extern int32_t g_rl_568974;   /* DAT_00568974 */
extern int32_t g_rl_56d894;   /* DAT_0056d894 */
extern int32_t g_rl_5cd41c;   /* DAT_005cd41c */
extern int32_t g_rl_57d3c4;   /* DAT_0057d3c4 */
extern uint8_t g_rl_591548;   /* byte_591548 */
extern int32_t g_rl_512f88;   /* DAT_00512f88 */

/* ---------------------------------------------------------------- retail's blobs and tables, by address */
#define FONT       ((const uint8_t *)g_bf_5af8f0)        /* &DAT_005af8f0: the default 16 px font */
#define FONT_TALL  ((const uint8_t *)g_rl_5b2fa0)        /* &DAT_005b2fa0: the tall 24 px font */
#define CHROME     ((const uint8_t *)g_rl_57d3d0)        /* &DAT_0057d3d0: the chrome sheet */
#define SYSTEM     ((const uint8_t *)g_rl_5bb540)        /* &DAT_005bb540: system2.pl8 (the seal) */
#define GLYPHS     ((const uint8_t *)g_rl_4d71d0 + 0x20) /* DAT_004d71f0: the renderers' char -> glyph table */
#define STR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))
#define PTR(v)     ((const uint8_t *)(uintptr_t)(uint32_t)(v))   /* a retail pointer held in a dword global */

/* ---------------------------------------------------------------- the gfx layer's state structs (as rl_render.c) */
static localization_state_t s_loc;
static localization_state_t *loc_in(void)
{
    s_loc.blob = g_rl_591580;
    s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    return &s_loc;
}
static void loc_out(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
}
static void wrap_line(int idx, int skip, int x, int y, const uint8_t *font, int color)   /* 0x402d37 */
{
    gfx_wrap_localized_line(loc_in(), idx, skip, x, y, font, GLYPHS, (uint8_t)color);
    loc_out();
}
static void text_line(const uint8_t *buf, int x, int y, const uint8_t *font, int color)   /* 0x402637 */
{
    gfx_text_render_line(buf, x, y, font, GLYPHS, (uint8_t)color);
}
static void label(int v, int pad, const char *suffix, int x, int y, const uint8_t *font, int color)   /* 0x402f64 */
{
    gfx_text_label_render_dec(v, (char)pad, suffix, x, y, font, GLYPHS, (uint8_t)color);
}
static void label_ls(int v, int mode, const uint8_t *lab, const char *suffix, int x, int y, const uint8_t *font,
                     int cpos, int cneg)   /* 0x402e0c */
{
    gfx_text_label_render_labeled_signed(v, mode, lab, suffix, x, y, font, GLYPHS, (uint8_t)cpos, (uint8_t)cneg);
}
static void mark_box(int x, int y, int w, int h, int force)   /* 0x4527a6 */
{
    engine_dirty_rect_mark_box_tiled(engine_territory_dirty_state(), x, y, w, h, force);
}
static void mark_full(void) { engine_territory_mark_full_screen_dirty(); }   /* 0x45240a */

/* ---------------------------------------------------------------- records and retail's globals */
/* the county records (save.h g_per_army_768: retail 0x53f9b0, 17 × 768 B) */
#define CO8(c, va)   AT(uint8_t, g_per_army_768, 0x53f9b0u, (va) + 768 * (c))
#define COS8(c, va)  AT(int8_t,  g_per_army_768, 0x53f9b0u, (va) + 768 * (c))
#define CO32(c, va)  AT(int32_t, g_per_army_768, 0x53f9b0u, (va) + 768 * (c))
#define DIRTY      engine_territory_dirty_state()        /* 0x4527a6's six globals + DAT_0053e9ac (a mirror: rh_hooks_ai.c) */
#define LOCAL      ((int)s_slot_0057c8cc)                /* DAT_0057c8cc: the local player */
#define COUNTY     ((int)s_slot_0057c93c)                /* DAT_0057c93c: the selected county */
#define K59154C    AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)   /* DAT_0059154c: the router's parameter */
#define SEL        g_rl_53e9a0[0]                        /* byte_53E9A0: the box-selected category + 1 (10: idle) */
#define DROP       g_rl_53e9ec[0]                        /* byte_53E9EC: the drop target category + 1 */
#define MOVE_N     g_rl_57c988[0]                        /* DAT_0057c988: the workers to move */

static int32_t add32(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }

/* ---------------------------------------------------------------- seams (callees outside the partition) */
/* ported, not on retail's layout / not linked in rhost — TODO(rl-link) */
static int row_sprite(int c, int cat)   /* TODO(rl-link): engine_workforce_row_sprite_index 0x4517ca (engine_workforce_hud.c) */
{ return (int)engine_retail_call(0x4517cau, 2, (uint32_t)c, (uint32_t)cat); }
static int cell_hit(int x, int y)   /* TODO(rl-link): 0x4398f5 (engine_labor.c / engine_labor_region_grid.h) */
{ return (int)engine_retail_call(0x4398f5u, 2, (uint32_t)x, (uint32_t)y); }
static void plan_owner(int owner)   /* TODO(rl-link): 0x448648 (engine_army_select.c's static leaf) */
{ engine_retail_call(0x448648u, 1, (uint32_t)owner); }
static int workforce_hud_render(int a)   /* TODO(rl-link): engine_workforce_hud_render 0x412143 */
{ return (int)engine_retail_call(0x412143u, 1, (uint32_t)a); }
static void set_camera(int mode) { engine_retail_call(0x451fccu, 1, (uint32_t)mode); }   /* TODO(rl-link): engine_map_init_set_camera 0x451fcc */
static int  load_season(void) { return (int)engine_retail_call(0x4984dcu, 0); }   /* TODO(rl-link): engine_pl8.c's season loader 0x4984dc */
static int  present(int full) { return (int)engine_retail_call(0x452160u, 1, (uint32_t)full); }   /* TODO(rl-link): gfx.c's present 0x452160 */
static int  confirm_open(int kind, int x, int y, uint32_t cb)   /* TODO(rl-link): engine_confirm_dialog_open 0x40e6f2 (retail keeps the callback VA) */
{ return (int)engine_retail_call(0x40e6f2u, 4, (uint32_t)kind, (uint32_t)x, (uint32_t)y, cb); }
static int  evq_post(int a, int b, int code, int d, int e, int f, int g, int h)   /* TODO(rl-link): engine_event_queue_post 0x472bc5 (posts into a port struct) */
{
    return (int)engine_retail_call(0x472bc5u, 8, (uint32_t)a, (uint32_t)b, (uint32_t)code, (uint32_t)d, (uint32_t)e,
                                   (uint32_t)f, (uint32_t)g, (uint32_t)h);
}
static int  map_options_redraw(void) { return (int)engine_retail_call(0x476a5du, 0); }   /* TODO(rl-link): 0x476a5d (engine.c) */
static void text_input_open(const void *init, int n, int x, int y)   /* TODO(rl-link): text_input_init 0x402009 */
{ engine_retail_call(0x402009u, 4, (uint32_t)(uintptr_t)init, (uint32_t)n, (uint32_t)x, (uint32_t)y); }
static int  text_recompute_length(void) { return (int)engine_retail_call(0x401f49u, 0); }   /* TODO(rl-link): text_input_recompute_length 0x401f49 */
static int  smithy_panel(void) { return (int)engine_retail_call(0x413155u, 0); }   /* TODO(rl-link): labor_panel_smithy 0x413155 (engine_labor.c) */
static int  title_filelist(int page) { return (int)engine_retail_call(0x4148e4u, 1, (uint32_t)page); }   /* TODO(rl-link): engine_title_render_filelist 0x4148e4 */
/* unported — TODO(rl-extern) */
static int  mp_quit_session(void) { return (int)engine_retail_call(0x4b74ceu, 0); }   /* TODO(rl-extern): 0x4b74ce */

/* 0x46d5fe on retail's globals: the pure engine_map_query_terrain_info over the cell grid (DAT_00522f90), the county
 * records and the cursor's cell offset DAT_0056795c; its outputs stored where retail stores them */
static void terrain_info(void)
{
    engine_map_terrain_in_t in = { g_overworld_map, g_per_army_768, g_rl_56795c };
    engine_map_terrain_out_t o;
    engine_map_query_terrain_info(&in, &o);
    g_rl_5440b0 = o.flags;
    g_rl_5681e0[0] = o.head_link;
    g_rl_5681d0 = o.army_byte5;
    g_rl_57cb2c = o.terrain_type;
    g_rl_52afbc = o.county_id;
    g_rl_553f78 = o.cell3;
    g_rl_5651bc = o.derived_bit;
}

/* ================================================================ the labour model */

/* Port of FUN_0043958a (674 B) — the workforce panel's box select (screen 5, the drag from DAT_005530fc / 005530f4 to
 * the cursor DAT_004e65fc / 004e65f4): clear the 25 picked-cell flags DAT_00553040, then for each of the 8 category
 * boxes (origins DAT_004d85a8 pairs + DAT_0057c908, 152 x 100) the box overlaps, each of its 25 cells
 * (DAT_005540a0[25 j + k] 2 / 3+: a worker) whose point (DAT_004d85e8 pairs) lies in the drag box is picked (1 for
 * kind 2, else 2) and counted in DAT_0057c988; the category + 1 goes to byte_53E9A0 — cells of a second category
 * clear it and return 0. */
int rl_43958a(void)
{
    for (int i = 0; i < 25; i++)
        g_rl_553040[i] = 0;
    SEL = 0;
    MOVE_N = 0;
    int x0, x1, y0, y1;
    if (g_rl_5530fc >= g_bf_4e65fc) { x0 = g_bf_4e65fc; x1 = g_rl_5530fc; } else { x0 = g_rl_5530fc; x1 = g_bf_4e65fc; }
    if (g_bf_4e65f4 <= g_rl_5530f4) { y0 = g_bf_4e65f4; y1 = g_rl_5530f4; } else { y0 = g_rl_5530f4; y1 = g_bf_4e65f4; }
    for (int j = 0; j < 8; j++) {
        int bx = AT(int32_t, g_rl_4d85a8, 0x4d85a8u, 0x4d85a8u + 8 * j) + 64;
        int by = AT(int32_t, g_rl_4d85a8, 0x4d85a8u, 0x4d85acu + 8 * j) + g_rl_57c908 - 20;
        if (bx + 88 < x0 || bx > x1 || by + 80 < y0 || by > y1)
            continue;
        int ox = AT(int32_t, g_rl_4d85a8, 0x4d85a8u, 0x4d85a8u + 8 * j) + 64;
        int oy = AT(int32_t, g_rl_4d85a8, 0x4d85a8u, 0x4d85acu + 8 * j) + g_rl_57c908;
        for (int k = 0; k < 25; k++) {
            uint8_t v = g_rl_5540a0[25 * j + k];
            if (v == 0 || v == 1)
                continue;
            int px = ox + AT(uint8_t, g_rl_4d85a8, 0x4d85a8u, 0x4d85e8u + 2 * k) + 8;
            int py = oy + AT(uint8_t, g_rl_4d85a8, 0x4d85a8u, 0x4d85e9u + 2 * k) - 15;
            if (px < x0 || px > x1 || py < y0 || py > y1)
                continue;
            if (SEL != 0 && SEL != j + 1) {
                SEL = 0;
                return 0;
            }
            SEL = (uint8_t)(j + 1);
            g_rl_553040[k] = v == 2 ? 1 : 2;
            MOVE_N++;
        }
    }
    return 1;
}

/* Port of FUN_004399b0 (418 B) — drop the box-selected workers (a left press, DAT_004e65d8, over the panel x 64..511,
 * y DAT_0057c908 .. +375): the cell under the cursor (0x4398f5) is the target category + 1 (byte_53E9EC; 0: none →
 * 0). The count DAT_0057c988 is scaled by the county's +0xb8 (workers per figure) and capped at the source row's
 * workers (+0xc4 + 12 row); the same category deselects (byte_53E9A0 = 0, the panel redrawn 0x4120e0), else MP sends
 * message 37, else the move (0x439b52). Returns 1 when the press was taken. */
int rl_4399b0(void)
{
    if (g_bf_4e65d8 == 0)
        return 0;
    if (g_bf_4e65fc < 64)
        return 0;
    if (g_bf_4e65fc >= 512)
        return 0;
    if (g_rl_57c908 > g_bf_4e65f4)
        return 0;
    if (g_rl_57c908 + 376 <= g_bf_4e65f4)
        return 0;
    DROP = (uint8_t)cell_hit(g_bf_4e65fc, g_bf_4e65f4);
    if (DROP == 0)
        return 0;
    int row = row_sprite(COUNTY, SEL - 1);
    MOVE_N = (int32_t)((uint32_t)CO8(COUNTY, 0x53fa68u) * (uint32_t)MOVE_N);
    if (CO32(COUNTY, 0x53fa74u + 12 * row) < MOVE_N)
        MOVE_N = CO32(COUNTY, 0x53fa74u + 12 * row);
    if (DROP == SEL) {
        SEL = 0;
        rl_4120e0();
    } else if (g_sim_mp_active != 0) {
        rl_43eda0(37, 0);
    } else {
        rl_439b52(COUNTY, SEL - 1, DROP - 1, MOVE_N);
    }
    return 1;
}

/* Port of FUN_00439b52 (368 B) — move `n` workers of county `c` from category `from` to `to` (their rows by
 * 0x4517ca): the rows' counts (+0xc4 + 12 row), the target's side effects (0x439cc2), then twice the economy
 * re-plan — provisioning (0x44df5f, DAT_0057c934), the realm plan (0x4485a5, DAT_0057c92c), the owner's resource
 * plan (0x448648) — with the labour share (0x44ff4a) and the production shares (0x450000) between. For the selected
 * county owned locally: deselect, the workforce HUD (0x412143(0)), focus redraw DAT_005530d0, and the route
 * overlay's redraw while it is up (DAT_0057a0c4 > 0 → DAT_00567d0c). Returns retail's EAX (c; << 8 for the selected
 * county; the HUD's). */
int rl_439b52(int c, int from, int to, int n)
{
    int owner = CO8(c, 0x53f9b5u);
    int rt = row_sprite(c, to);
    int rf = row_sprite(c, from);
    CO32(c, 0x53fa74u + 12 * rt) = add32(CO32(c, 0x53fa74u + 12 * rt), n);
    CO32(c, 0x53fa74u + 12 * rf) = add32(CO32(c, 0x53fa74u + 12 * rf), -n);
    rl_439cc2(c, rf, rt);
    engine_army_economy_provision(c, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    plan_owner(owner);
    rl_44ff4a(c);
    rl_450000(c);
    engine_army_economy_provision(c, (int)s_slot_0057c934);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    plan_owner(owner);
    int r = c;
    if (COUNTY == c) {
        r = COUNTY << 8;
        if (CO8(COUNTY, 0x53f9b5u) == LOCAL) {
            SEL = 0;
            r = workforce_hud_render(0);
            g_combat_flag_530d0 = 1;
            if ((int32_t)s_slot_0057a0c4 > 0)
                g_combat_flag_567d0c = 1;
        }
    }
    return r;
}

/* Port of FUN_00439cc2 (302 B) — a worker move's target side effects: a resource row (6 / 5 / 4 / 7 → slot 0 / 3 /
 * 1 / 2) whose resource exists (+0x295 / +0x2dd / +0x2ad / +0x2c5) marks the slot worked (+0x297 + 24 slot) and
 * stamps its cell (0x44edc2); row 3 marks +0x1b0. Returns 3 × the source row (retail's EAX). */
int rl_439cc2(int c, int from, int to)
{
    int slot = 999;
    if (to == 6 && COS8(c, 0x53fc45u) != 0)
        slot = 0;
    else if (to == 5 && COS8(c, 0x53fc8du) != 0)
        slot = 3;
    else if (to == 4 && COS8(c, 0x53fc5du) != 0)
        slot = 1;
    else if (to == 7 && COS8(c, 0x53fc75u) != 0)
        slot = 2;
    if (slot != 999) {
        CO8(c, 0x53fc47u + 24 * slot) = 1;
        engine_army_economy_stamp_resource_cell(c, slot);
    }
    if (to == 3)
        CO8(c, 0x53fb60u) = 1;
    return 3 * from;
}

/* Port of FUN_00439f6a (441 B) — balance one category `cat` of county `c` (its row by 0x4517ca): with `fill` and the
 * row short of its target (+0xc8 > 0: target − workers), take min(shortfall, idle +0x124) from the idle (category 6
 * → cat; 0 idle → 0); else a row over its cap (+0xcc < 99999: workers − cap) sends the excess to the idle
 * (cat → 6); else 0. byte_53E9A0 / byte_53E9EC take the source / target rows (10: idle), DAT_0057c988 the count,
 * then the move (0x439b52). Returns 1 when it moved. */
int rl_439f6a(int c, int cat, int fill)
{
    int row = row_sprite(c, cat);
    int want = CO32(c, 0x53fa78u + 12 * row) > 0 ? CO32(c, 0x53fa78u + 12 * row) - CO32(c, 0x53fa74u + 12 * row) : 0;
    int over = CO32(c, 0x53fa7cu + 12 * row) < 99999 ? CO32(c, 0x53fa74u + 12 * row) - CO32(c, 0x53fa7cu + 12 * row) : 0;
    int n, from, to;
    if (want > 0 && fill != 0) {
        if (CO32(c, 0x53fad4u) == 0)
            return 0;
        n = CO32(c, 0x53fad4u) < want ? CO32(c, 0x53fad4u) : want;
        SEL = 10;
        DROP = (uint8_t)row;
        to = cat;
        from = 6;
    } else {
        if (over <= 0)
            return 0;
        n = over;
        SEL = (uint8_t)row;
        DROP = 10;
        to = 6;
        from = cat;
    }
    MOVE_N = n;
    rl_439b52(c, from, to, MOVE_N);
    return 1;
}

/* Port of FUN_00439edb (143 B) — rebalance county `c`: one category (fill), or for 6 every category 0..9 twice
 * (first the excess to the idle, then the shortfalls filled). Returns the last 0x439f6a's. */
int rl_439edb(int c, int cat)
{
    if (cat != 6)
        return rl_439f6a(c, cat, 1);
    int r = 0;
    for (int i = 0; i < 10; i++)
        r = rl_439f6a(c, i, 0);
    for (int j = 0; j < 10; j++)
        r = rl_439f6a(c, j, 1);
    return r;
}

/* Port of FUN_0044ff4a (182 B) — county `c`'s free labour share (+8, a byte): the population +0x24 less the farm /
 * industry / building workers (+0xc4 / +0xd0 / +0xdc) and half the idle (+0x124), as a percentage of the population
 * (0x404dc1). Returns the percentage. */
int rl_44ff4a(int c)
{
    int busy = add32(add32(CO32(c, 0x53fa74u), CO32(c, 0x53fa80u)), CO32(c, 0x53fa8cu));
    int idle = CO32(c, 0x53fad4u);
    int v = add32(add32(CO32(c, 0x53f9d4u), -busy), -idle) + idle / 2;
    int r = engine_math_percent_div(v, CO32(c, 0x53f9d4u));
    CO8(c, 0x53f9b8u) = (uint8_t)r;
    return r;
}

/* the largest of `n` dwords of county c from field `va0` + 4 first (retail's argmax: the first strict maximum over
 * the start value `start` at index `first`) */
static int argmax(int c, int lo, int hi, int first, int start)
{
    int best = first, mx = start;
    for (int i = lo; i <= hi; i++) {
        int v = CO32(c, 0x53fae0u + 4 * i);
        if (v > mx) {
            mx = v;
            best = i;
        }
    }
    return best;
}

/* Port of FUN_00450000 (714 B) — county `c`'s production shares: the three worker groups (+0xc4 / +0xd0 / +0xdc)
 * as percentages of their sum into +0x130..+0x138, the rounding remainder to the largest (argmax from index 1);
 * the five resource rows (+0x10c / +0x100 / +0xf4 / +0x118 / +0xe8) into +0x148 / +0x144 / +0x140 / +0x14c / +0x13c,
 * the remainder to the largest of +0x13c..+0x14c (argmax from index 6). Returns the second remainder. */
int rl_450000(int c)
{
    int a = CO32(c, 0x53fa74u), b = CO32(c, 0x53fa80u), d = CO32(c, 0x53fa8cu);
    int sum = add32(add32(d, b), a);
    a = engine_math_percent_div(a, sum);
    b = engine_math_percent_div(b, sum);
    d = engine_math_percent_div(d, sum);
    int rem = 100 - a - b - d;
    CO32(c, 0x53fae0u) = a;
    CO32(c, 0x53fae4u) = b;
    CO32(c, 0x53fae8u) = d;
    int k = argmax(c, 0, 2, 1, b);
    CO32(c, 0x53fae0u + 4 * k) = add32(CO32(c, 0x53fae0u + 4 * k), rem);
    int r0 = CO32(c, 0x53fabcu), r1 = CO32(c, 0x53fab0u), r2 = CO32(c, 0x53faa4u), r3 = CO32(c, 0x53fac8u),
        r4 = CO32(c, 0x53fa98u);
    int tot = add32(add32(add32(add32(r4, r3), r2), r1), r0);
    r0 = engine_math_percent_div(r0, tot);
    r1 = engine_math_percent_div(r1, tot);
    r2 = engine_math_percent_div(r2, tot);
    r3 = engine_math_percent_div(r3, tot);
    r4 = engine_math_percent_div(r4, tot);
    int rem2 = 100 - r0 - r1 - r2 - r3 - r4;
    CO32(c, 0x53faf8u) = r0;
    CO32(c, 0x53faf4u) = r1;
    CO32(c, 0x53faf0u) = r2;
    CO32(c, 0x53fafcu) = r3;
    CO32(c, 0x53faecu) = r4;
    k = argmax(c, 3, 7, 6, r0);
    CO32(c, 0x53fae0u + 4 * k) = add32(CO32(c, 0x53fae0u + 4 * k), rem2);
    return rem2;
}

/* Port of FUN_00450639 (677 B) — switch worker group `g` (0..2: +0x130 + 4 g) of county `c` on (`on` 1: it takes
 * DAT_004d6768[groups already on] / div percent, the others scaled to the rest 0x404d6b) or off (0: its share is
 * spread back over the others 0x404dc1); no-op when it already is. The rounding remainder goes to the largest
 * (argmax from index 1). Returns the remainder (else 768 c: retail's EAX). */
int rl_450639(int c, int g, int on, int div)
{
    if (CO32(c, 0x53fae0u + 4 * g) != 0 && on == 1)
        return 768 * c;
    if (CO32(c, 0x53fae0u + 4 * g) == 0 && on == 0)
        return 768 * c;
    int a = CO32(c, 0x53fae0u), b = CO32(c, 0x53fae4u), d = CO32(c, 0x53fae8u);
    int n = a != 0;
    if (b != 0)
        n++;
    if (d != 0)
        n++;
    if (on == 1) {
        int share = g_rl_4d6768[n] / div;
        CO32(c, 0x53fae0u) = engine_math_percent_of(100 - share, a);
        CO32(c, 0x53fae4u) = engine_math_percent_of(100 - share, b);
        CO32(c, 0x53fae8u) = engine_math_percent_of(100 - share, d);
        CO32(c, 0x53fae0u + 4 * g) = share;
    } else if (on == 0) {
        int old = CO32(c, 0x53fae0u + 4 * g);
        CO32(c, 0x53fae0u + 4 * g) = 0;
        CO32(c, 0x53fae0u) = engine_math_percent_div(a, 100 - old);
        CO32(c, 0x53fae4u) = engine_math_percent_div(b, 100 - old);
        CO32(c, 0x53fae8u) = engine_math_percent_div(d, 100 - old);
    }
    int rem = 100 - CO32(c, 0x53fae0u) - CO32(c, 0x53fae4u) - CO32(c, 0x53fae8u);
    int k = argmax(c, 0, 2, 1, CO32(c, 0x53fae4u));
    CO32(c, 0x53fae0u + 4 * k) = add32(CO32(c, 0x53fae0u + 4 * k), rem);
    return rem;
}

/* Port of FUN_00438bec (255 B) — a field's use changed: county `c`'s cell `cell` gets type `type` (0x46d7f4), the
 * realms' census (0x469b51), the second worker group on / off by the county's +0x204 (0x450639(c, 2, on, 3)), then
 * twice the workforce (0x44f6e7), the feature grades (0x44d913) and the realm plan (0x4485a5, DAT_0057c92c); focus
 * redraw DAT_005530d0 for the selected county owned locally. */
int rl_438bec(int c, int cell, int type)
{
    engine_territory_set_cell_type(cell, type, 0);
    engine_territory_census_all_realms();
    if (CO8(c, 0x53fbb4u) != 0)
        rl_450639(c, 2, 1, 3);
    else
        rl_450639(c, 2, 0, 3);
    engine_territory_allocate_realm_workforce(c);
    engine_territory_grade_feature_cells(c);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    engine_territory_allocate_realm_workforce(c);
    engine_territory_grade_feature_cells(c);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    int r = c << 8;
    if (CO8(c, 0x53f9b5u) == LOCAL) {
        r = c;
        if (COUNTY == c)
            g_combat_flag_530d0 = 1;
    }
    return r;
}

/* Port of FUN_00438b02 (234 B) — the Farmland panel's field icons (router action; DAT_0059154c = the new use: 19
 * grain → cue 4, 2 → 7, 0 / 1 / 25 → 6 (0x426216)): the terrain under the cursor (0x46d5fe), the use applied to the
 * cell's county (0x438bec(DAT_005681e0, DAT_0056795c, use)), MP message 34, back to the map (screen 0) with the
 * map area redrawn (0x45270a), DAT_0052afa4 = 0, focus redraw. */
int rl_438b02(void)
{
    terrain_info();
    if (K59154C == 19)
        rl_426216(4);
    else if (K59154C == 2)
        rl_426216(7);
    else if (K59154C == 1 || K59154C == 0 || K59154C == 25)
        rl_426216(6);
    rl_438bec(g_rl_5681e0[0], g_rl_56795c, K59154C);
    if (g_sim_mp_active != 0)
        rl_43eda0(34, 0);
    engine_set_current_screen(0);
    engine_dirty_rect_mark_below_status_bar(DIRTY);
    g_diplo_action_taken = 0;
    g_combat_flag_530d0 = 1;
    return 0;
}

/* Port of FUN_00439541 (73 B) — the end of a workforce-panel drag (no press repeat, DAT_004e65cc == 0): redraw the
 * panel (0x4120e0); 2 when a category is box-selected (byte_53E9A0), else 1; 0 while the press repeats. */
int rl_439541(void)
{
    if (g_bf_4e65cc != 0)
        return 0;
    rl_4120e0();
    return SEL != 0 ? 2 : 1;
}

/* ================================================================ the kingdom overview (minimap view mode 2) */

/* Port of FUN_0041424a (225 B) — in view mode 1 (DAT_0057cb18) clear the map's left strip (x 0..3, y 24..479) and the
 * two bottom rows (y 472 / 473, x 0..475 in 4-pixel steps, then 2 pixels at 476) to colour 0. */
int rl_41424a(void)
{
    if ((int)s_slot_0057cb18 != 1)
        return 0;
    int i;
    for (i = 24; i < 480; i++)
        engine_overlay_render_fill4(0, i, 0);
    for (i = 0; i < 476; i += 4)
        engine_overlay_render_fill4(i, 472, 0);
    gfx_paint_2x1(i, 472, 0);
    for (i = 0; i < 476; i += 4)
        engine_overlay_render_fill4(i, 473, 0);
    gfx_paint_2x1(i, 473, 0);
    return 0;
}

/* Port of FUN_0046d421 (11 B) — an empty function. */
int rl_46d421(void) { return 0; }

/* Port of FUN_00434fd5 (133 B) — open the kingdom overview: keep the scroll (DAT_005651b4 / 005651b8 →
 * DAT_00543fbc / 00543fc8), centre it (14, 12) in camera mode 2 (0x451fcc), clamp (0x429b1d), redraw the map area
 * (0x45270a) and the strip (0x41424a), full redraw, the season sheets (0x4984dc), the cue s033_02.wav (0x427990). */
int rl_434fd5(void)
{
    g_rl_543fbc = (int32_t)s_slot_005651b4;
    g_rl_543fc8 = (int32_t)s_slot_005651b8;
    s_slot_005651b4 = 14;
    s_slot_005651b8 = 12;
    set_camera(2);
    engine_camera_clamp_scroll((int *)&s_slot_005651b4, (int *)&s_slot_005651b8, g_mv_col_count, g_mv_row_count);
    engine_dirty_rect_mark_below_status_bar(DIRTY);
    rl_41424a();
    rl_46d421();
    DIRTY->below_status_redraw = 1;
    engine_set_demand_redraw(2);
    load_season();
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)g_rl_4d5754, 1, 0);
}

/* Port of FUN_0043505a (71 B) — close the kingdom overview: the kept scroll back, camera mode 0 (0x451fcc), clamp,
 * redraw the map area, the season sheets (0x4984dc). */
int rl_43505a(void)
{
    s_slot_005651b4 = (uint32_t)g_rl_543fbc;
    s_slot_005651b8 = (uint32_t)g_rl_543fc8;
    set_camera(0);
    engine_camera_clamp_scroll((int *)&s_slot_005651b4, (int *)&s_slot_005651b8, g_mv_col_count, g_mv_row_count);
    engine_dirty_rect_mark_below_status_bar(DIRTY);
    rl_46d421();
    DIRTY->below_status_redraw = 1;
    return load_season();
}

/* Port of FUN_00434fa1 (52 B) — the magnifier: view mode DAT_0057cb18 0 → open the overview (0x434fd5), 2 → close it
 * (0x43505a). */
int rl_434fa1(void)
{
    if ((int)s_slot_0057cb18 == 0)
        return rl_434fd5();
    if ((int)s_slot_0057cb18 == 2)
        return rl_43505a();
    return 0;
}

/* Port of FUN_004350a1 (232 B) — a county click on the overview (DAT_005533b0 set): find the clicked county
 * DAT_00568974 in the 129 x 65 county map DAT_0055cea0 (row-major, the first match; none: col 65, row 129) and centre
 * the map there (scroll col − 4, (row & ~1) − 12) in camera mode 0, clamp, redraw, the season sheets; DAT_00567d0c = 0,
 * DAT_0052afa4 = 1. */
int rl_4350a1(void)
{
    if (g_rl_5533b0 == 0)
        return 0;
    int i, j = 0;
    for (i = 0; i < 129; i++) {
        for (j = 0; j < 65; j++)
            if (g_rl_55cea0[65 * i + j] == g_rl_568974)
                goto found;
    }
found:
    s_slot_005651b4 = (uint32_t)(j - 4);
    s_slot_005651b8 = (uint32_t)((i & 0xfffe) - 12);
    set_camera(0);
    engine_camera_clamp_scroll((int *)&s_slot_005651b4, (int *)&s_slot_005651b8, g_mv_col_count, g_mv_row_count);
    engine_dirty_rect_mark_below_status_bar(DIRTY);
    rl_46d421();
    g_combat_flag_567d0c = 0;
    DIRTY->below_status_redraw = 1;
    engine_set_demand_redraw(2);
    int r = load_season();
    g_diplo_action_taken = 1;
    return r;
}

/* Port of FUN_0043ab76 (173 B) — the minimap's overlay buttons (router action, DAT_0059154c = 1..3, 4 the magnifier):
 * with no overlay up (DAT_0057a0c4 == 0) the magnifier goes back to the map (screen 0, redraw) and toggles the
 * overview (0x434fa1); another button raises its overlay (DAT_0057a0c4, DAT_0053e920 = 1, its start time
 * DAT_0056d5e0 = timeGetTime, combat redraw). With one up, the magnifier drops it. */
int rl_43ab76(void)
{
    if (s_slot_0057a0c4 != 0) {
        if (K59154C == 4) {
            s_slot_0057a0c4 = 0;
            s_slot_0053e920 = 0;
            engine_set_demand_redraw(2);
            g_combat_flag_567d0c = 1;
        }
    } else if (K59154C == 4) {
        engine_set_current_screen(0);
        DIRTY->below_status_redraw = 1;
        engine_set_demand_redraw(2);
        rl_434fa1();
    } else {
        s_slot_0057a0c4 = (uint32_t)K59154C;
        s_slot_0053e920 = 1;
        s_slot_0056d5e0 = rl_plat_timeGetTime();
        g_combat_flag_567d0c = 1;
    }
    return 0;
}

/* ================================================================ help / rankings pages, small router actions */

/* Port of FUN_0041543f (171 B) — the About box (screen 37): menu redraw latches, a 22x9 chrome panel at (96, 224),
 * group 59's title (tall) and two lines, the seal, full redraw, present (0x452160(1)). */
int rl_41543f(void)
{
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    gfx_panel_draw_bevel(CHROME, 96, 224, 22, 9);
    wrap_line(59, 0, 128, 244, FONT_TALL, 63);
    wrap_line(59, 2, 128, 272, FONT, 63);
    wrap_line(59, 1, 128, 328, FONT, 63);
    gfx_sprite_seal_draw(SYSTEM, 404, 326, 0);
    mark_full();
    return present(1);
}

/* Port of FUN_004154ea (340 B) — Help Options (screen 49): DAT_0052afd0 = 3 (rows), a 22x11 chrome panel at (96, 128),
 * group 45's four lines, the seal, the two rows' on / off (group 18) from DAT_0053f24c (tips) / DAT_0053f250 (tool
 * tips); full redraw, menu redraw. */
int rl_4154ea(void)
{
    g_rl_52afd0 = 3;
    gfx_panel_draw_bevel(CHROME, 96, 128, 22, 11);
    wrap_line(45, 3, 128, 256, FONT, 63);
    gfx_sprite_seal_draw(SYSTEM, 404, 262, 0);
    wrap_line(45, 0, 128, 148, FONT_TALL, 63);
    wrap_line(45, 1, 128, 192, FONT, 63);
    wrap_line(45, 2, 128, 224, FONT, 63);
    wrap_line(18, g_rl_53f24c != 0 ? 0 : 1, 288, 192, FONT, 63);
    wrap_line(18, g_rl_53f250 != 0 ? 0 : 1, 288, 224, FONT, 63);
    mark_full();
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* Port of FUN_00434d33 (23 B) — Help > Help Options: screen 49, its panel (0x4154ea). */
int rl_434d33(void)
{
    engine_set_current_screen(49);
    return rl_4154ea();
}

/* Port of FUN_00434d60 (23 B) — Help > About: screen 37, its box (0x41543f). */
int rl_434d60(void)
{
    engine_set_current_screen(37);
    return rl_41543f();
}

/* Port of FUN_00434787 (64 B) — Help Options' tips row: toggle DAT_0053f24c, the panel refresh (0x476a5d), redraw. */
int rl_434787(void)
{
    g_rl_53f24c = g_rl_53f24c == 1 ? 0 : 1;
    int r = map_options_redraw();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_004347c7 (69 B) — Help Options' tool-tips row: toggle DAT_0053f250, drop the pending tool tip
 * (DAT_004ea830 = 0), redraw. */
int rl_4347c7(void)
{
    g_rl_53f250 = g_rl_53f250 == 1 ? 0 : 1;
    g_rl_4ea830 = 0;
    engine_set_demand_redraw(2);
    return 0;
}

/* the four How-do-I pages: post help event 292..295 for the local player (0x472bc5), back to the screen
 * DAT_0056d894 (its low byte), DAT_0052afa4 = 1 */
static int how_do_i(int code)
{
    evq_post(0, LOCAL, code, 0, 19, 0, 0, 0);
    engine_set_current_screen((uint8_t)g_rl_56d894);
    g_diplo_action_taken = 1;
    return (uint8_t)g_rl_56d894;
}
/* Port of FUN_0043484a (62 B) — How do I…: the first page (help event 292). */
int rl_43484a(void) { return how_do_i(292); }
/* Port of FUN_00434888 (62 B) — How do I…: help event 293. */
int rl_434888(void) { return how_do_i(293); }
/* Port of FUN_004348c6 (62 B) — How do I…: help event 294. */
int rl_4348c6(void) { return how_do_i(294); }
/* Port of FUN_00434904 (62 B) — How do I…: help event 295. */
int rl_434904(void) { return how_do_i(295); }

/* ================================================================ the confirm dialogs, the frame driver's keys */

/* Port of FUN_00401c5b (21 B) — DAT_005cd41c = 100 (the WndProc's key 13 arm). */
int rl_401c5b(void)
{
    g_rl_5cd41c = 100;
    return 0;
}

/* Port of FUN_00433dbd (46 B) — File > New Game: drop the pending modes (0x4360f2), clear the MP chat state
 * (0x43f24b), the yes / no dialog (kind 1 at (160, 160)) with the answer callback 0x433deb. */
int rl_433dbd(void)
{
    rl_4360f2();
    rl_43f24b();
    return confirm_open(1, 160, 160, 0x433debu);
}

/* Port of FUN_00433deb (168 B) — the New Game dialog's answer: on yes (byte_59151C == 1) MP sends message 25; a
 * skirmish (DAT_0057a0f0) goes back to its setup (DAT_0053f238 = 0, screen 46 kept in DAT_0057c90c, 0x42c64f,
 * sub-state 0, engine state 2, redraw); else the title (byte_5C9274 = 1, byte_5AEB8C = 0, 0x4b11ce). Then the screen
 * byte = DAT_0057c90c's low byte (returned). */
int rl_433deb(void)
{
    if (g_bf_59151c[0] == 1) {
        if (g_sim_mp_active != 0) {
            rl_43eda0(25, 0);
        } else if (s_slot_0057a0f0 != 0) {
            g_bf_53f238 = 0;
            engine_set_current_screen(46);
            g_bf_57c90c = engine_current_screen();
            rl_42c64f();
            s_slot_0057c8fc = 0;
            g_engine_state = 2;
            engine_set_demand_redraw(2);
        } else {
            g_rl_5c9274 = 1;
            g_rl_5aeb8c = 0;
            rl_4b11ce();
        }
    }
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    return (uint8_t)g_bf_57c90c;
}

/* Port of FUN_004343f8 (36 B) — File > Quit / Esc: the yes / no dialog (kind 0 at (160, 160)) with the answer
 * callback 0x43441c. */
int rl_4343f8(void)
{
    return confirm_open(0, 160, 160, 0x43441cu);
}

/* Port of FUN_0043441c (158 B) — the Quit dialog's answer: on yes in an MP session (DAT_00568464) leave it (0x4b74ce)
 * → screen 31, DAT_005530f0 = 1; else up to 3 tries (DAT_0053f644) to show the exit picture (screen 69, redraw,
 * lom.256 present 0x4af767 — missing: error latch 1); a 4th → error latch 3. On no the screen byte = DAT_0057c90c. */
int rl_43441c(void)
{
    if (g_bf_59151c[0] != 1) {
        engine_set_current_screen((uint8_t)g_bf_57c90c);
        return (uint8_t)g_bf_57c90c;
    }
    if (g_rl_568464 != 0) {
        int r = mp_quit_session();
        engine_set_current_screen(31);
        g_rl_5530f0 = 1;
        return (uint8_t)r;
    }
    if (g_rl_53f644 >= 3) {
        g_gfx_last_error = 3;
        return 1;
    }
    g_rl_53f644++;
    engine_set_current_screen(69);
    engine_set_demand_redraw(2);
    int v = rl_4af767((int)(uintptr_t)g_rl_4d5740);
    if (v == 0)
        g_gfx_last_error = 1;
    return v;
}

/* Port of FUN_0043600c (230 B) — the chat key (MP only, not on screens 10 / 23 / 13): an open chat line
 * (DAT_0057a0cc) closes (DAT_005cd41c = 0, message 6, redraw); on the map (screen 0) or 41 a new one opens (the
 * editor at (470, 0), 64 chars, 0x402009 / 0x401f49; DAT_0057a0cc = 5000 ms, redraw). */
int rl_43600c(void)
{
    if (g_sim_mp_active == 0)
        return 0;
    int s = engine_current_screen();
    if (s == 10 || s == 23 || s == 13)
        return s;
    if (g_rl_57a0cc != 0) {
        g_rl_57a0cc = 0;
        g_rl_5cd41c = 0;
        int r = rl_43eda0(6, 0);
        engine_set_demand_redraw(2);
        return r;
    }
    if (engine_current_screen() == 0 || engine_current_screen() == 41) {
        text_input_open(g_rl_4d5790, 64, 470, 0);
        int r = text_recompute_length();
        g_rl_57a0cc = 5000;
        engine_set_demand_redraw(2);
        return r;
    }
    return engine_current_screen();
}

/* ================================================================ the armoury / smithy */

/* Port of FUN_0043a997 (155 B) — set county `c`'s weapon in production (+0x290 = kind, a byte), re-plan its smithy
 * resource (0x44f318(c, 2, 7, 15, 4)), the workforce (0x44f6e7), the realm plan (0x4485a5, DAT_0057c92c) and the
 * owner's plan (0x448648); focus redraw; for the local player the smithy panel (0x413155) and cue 8 (0x426216).
 * Returns the cue's (else the owner). */
int rl_43a997(int c, int kind)
{
    CO8(c, 0x53fc40u) = (uint8_t)kind;
    int owner = CO8(c, 0x53f9b5u);
    engine_army_economy_plan_resource(c, 2, 7, 15, 4);
    engine_territory_allocate_realm_workforce(c);
    engine_army_economy_plan_realm(c, (int)s_slot_0057c92c);
    plan_owner(owner);
    g_combat_flag_530d0 = 1;
    if (LOCAL == owner) {
        smithy_panel();
        return rl_426216(8);
    }
    return owner;
}

/* Port of FUN_0043a950 (71 B) — the smithy's weapon icons (router action, DAT_0059154c = the weapon): kept in
 * DAT_005530b8; MP sends message 33, else the selected county's weapon is set (0x43a997). */
int rl_43a950(void)
{
    g_rl_5530b8[0] = K59154C;
    if (g_sim_mp_active != 0)
        return rl_43eda0(33, 0);
    return rl_43a997(COUNTY, g_rl_5530b8[0]);
}

/* ================================================================ screenshots, the save box, small screen draws */

/* Port of FUN_00408f1a (149 B) — write the screen as an IFF ILBM `name` (_O_BINARY | _O_CREAT | _O_TRUNC | _O_WRONLY,
 * 0600): the 48-byte FORM / BMHD / CMAP header, the palette DAT_004ea1b0 (0x300), the BODY header (8) and the
 * 640x480 back buffer. Returns _close's (or -1 when the open failed). */
int rl_408f1a(int name)
{
    int fd = rl_plat_open((const char *)(uintptr_t)(uint32_t)name, 0x8301, 0x180);
    if (fd == -1)
        return fd;
    rl_plat_write(fd, g_rl_4d20a0, 0x30u);
    rl_plat_write(fd, g_rl_4ea1b0, 0x300u);
    rl_plat_write(fd, &AT(uint8_t, g_rl_4d20a0, 0x4d20a0u, 0x4d20d0u), 8u);
    rl_plat_write(fd, g_gfx_canvas_pixels, 0x4b000u);
    return rl_plat_close(fd);
}

/* Port of FUN_00408ea4 (118 B) — Alt+1..4: the screenshot shot1..4.lbm (0x408f1a). */
int rl_408ea4(int k)
{
    if (k == 1) return rl_408f1a((int)(uintptr_t)&AT(uint8_t, g_rl_4d211c, 0x4d211cu, 0x4d211cu));
    if (k == 2) return rl_408f1a((int)(uintptr_t)&AT(uint8_t, g_rl_4d211c, 0x4d211cu, 0x4d2128u));
    if (k == 3) return rl_408f1a((int)(uintptr_t)&AT(uint8_t, g_rl_4d211c, 0x4d211cu, 0x4d2134u));
    if (k == 4) return rl_408f1a((int)(uintptr_t)&AT(uint8_t, g_rl_4d211c, 0x4d211cu, 0x4d2140u));
    return 0;
}

/* Port of FUN_00413526 (106 B) — the saving animation (DAT_00591510 set): the next of 11 frames (DAT_004e59bc) of the
 * current blob DAT_004eabec at (88, 157), its 8x8 tiles marked. */
int rl_413526(void)
{
    if (g_rl_591510 == 0)
        return 0;
    if (++g_rl_4e59bc >= 11)
        g_rl_4e59bc = 0;
    gfx_sprite_render_rle_from_blob(PTR(g_bf_4eabec), g_rl_4e59bc, 88, 157);
    mark_box(88, 157, 8, 8, 1);
    return 0;
}

/* Port of FUN_00414e06 (354 B) — the save / load box's file list: a 21x11 chrome interior at (126, 174) (0x409c93),
 * its tiles marked, the 352x165 frame at (120, 172); up to 10 names (65-byte rows DAT_004e8790) from the top
 * DAT_004ea1a0 below DAT_004eb25c at (128, 176 + 16 n), the row `sel` highlighted (a 18x16 block of 63 behind it,
 * colour 102), shadows off (DAT_005aea40) meanwhile. */
int rl_414e06(int sel)
{
    g_gfx_no_shadow = 1;
    int x = 128, y = 176;
    gfx_panel_fill_interior_chrome(CHROME, x - 2, y - 2, 21, 11);
    mark_box(x - 4, y - 4, 22, 12, 1);
    gfx_line_rect(120, 172, 352, 165, 63);
    for (int v = g_rl_4ea1a0, i = 0; g_rl_4eb25c > v; v++, i++) {
        if (v == sel) {
            g_gfx_glyph_w = 18;
            g_gfx_glyph_height_short = 16;
            gfx_sprite_fill_block(x - 2, y - 1, 63);
            text_line(&g_rl_4e8790[65 * v], x, y, FONT, 102);
        } else {
            text_line(&g_rl_4e8790[65 * v], x, y, FONT, 63);
        }
        y += 16;
        if (i >= 9)
            break;
    }
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_00434174 (383 B) — a click on the save / load box's list (x 128..479, y 176..335): the row under the
 * cursor (DAT_004e65dc = top + (y − 176) / 16) when the button is down (byte_4EAFB4) and the row exists; a name other
 * than the current DAT_0053f5fc is copied there (64 B) and the skirmish map loaded: MP — 0x42d4aa, 0x42d857, message
 * 93; else DAT_0053e91c = 3 (the user map), 0x42d4aa, 0x42b9c4, 0x42ba40, 0x42bf46, DAT_005530f0 = DAT_00553e5c;
 * redraw. Returns 0 when not taken (else the last call's EAX). The argument is unused. */
int rl_434174(int unused)
{
    (void)unused;
    if (g_bf_4e65fc < 128 || g_bf_4e65fc >= 480)
        return 0;
    if (g_bf_4e65f4 < 176 || g_bf_4e65f4 >= 336)
        return 0;
    g_rl_4e65dc = g_rl_4ea1a0 + (g_bf_4e65f4 - 176) / 16;
    if (g_bf_4eafb4 == 0)
        return 0;
    if (g_rl_4e65dc >= g_rl_4eb25c)
        return 0;
    char *cur = (char *)((uintptr_t)g_bf_53f1e0 + (0x53f5fcu - 0x53f1e0u));   /* DAT_0053f5fc: lords2.inf's map name */
    if (strcmp((const char *)&g_rl_4e8790[65 * g_rl_4e65dc], cur) == 0)
        return 0;
    strutil_bounded_copy(cur, &g_rl_4e8790[65 * g_rl_4e65dc], 64);
    int r;
    if (g_sim_mp_active != 0) {
        rl_42d4aa();
        rl_42d857();
        r = rl_43eda0(93, 0);
    } else {
        s_slot_0053e91c = 3;
        rl_42d4aa();
        rl_42b9c4();
        rl_42ba40();
        rl_42bf46();
        r = g_rl_553e5c;
        g_rl_5530f0 = g_rl_553e5c;
    }
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0041ef42 (21 B) — the title's file list page 5 (0x4148e4(5)). */
int rl_41ef42(void)
{
    return title_filelist(5);
}

/* ================================================================ the skirmish setup (title page 12 / 13) */

/* the territory records (save.h g_per_territory_420: retail 0x52f0b0, stride 420): the custom army's troop counts
 * (+0x16c, 7 words) and their total (+0x168) of the record DAT_00553efc */
#define TE16(t, va) AT(int16_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define TE32(t, va) AT(int32_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define CUST       ((int)s_slot_00553efc)
#define PAUX8(p, va) AT(uint8_t, g_player_aux_44, 0x553d50u, (va) + 44 * (p))

/* seams of the setup actions */
static void evq_init(void) { engine_retail_call(0x472ae0u, 0); }   /* TODO(rl-link): engine_event_queue_init 0x472ae0 (a port struct) */
static int  load_misc_sheet(void) { return (int)engine_retail_call(0x499034u, 0); }   /* TODO(rl-link): gfx_load_misc_sheet 0x499034 */
static int  load_system_sheet(int k) { return (int)engine_retail_call(0x499a1cu, 1, (uint32_t)k); }   /* TODO(rl-link): gfx_load_system_sheet_select 0x499a1c */
static int  chdir_base(void) { return rl_4af502(); }   /* io_path_chdir_base 0x4af502 (rl_host) */
static int  load_panel_sheet(int n) { return rl_4b25b0(n); }   /* gfx_load_panel_sheet 0x4b25b0 (rl_host) */
static int  read_file_at(const void *name, void *dst, uint32_t n, uint32_t off)   /* io_read_file_at 0x4af8d9 (rl_host) */
{ return rl_4af8d9((int)(uintptr_t)name, (int)(uintptr_t)dst, (int)n, (int)off); }
/* engine_retail_call takes up to 8 arguments: an 11-argument retail call goes through this cdecl adapter (bracketed
 * like any retail call; the standalone stops) */
typedef uint32_t (__cdecl *r11_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t);
static uint32_t __cdecl call11_adapter(uint32_t fn, uint32_t argv)
{
    const uint32_t *a = (const uint32_t *)(uintptr_t)argv;
    return ((r11_t)(uintptr_t)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
}
static int bind_hex_grid(const uint32_t a[11])   /* TODO(rl-link): engine_battlefield_render_bind_hex_grid_context 0x4bc020 */
{ return (int)engine_retail_call((uint32_t)(uintptr_t)call11_adapter, 2, 0x4bc020u, (uint32_t)(uintptr_t)a); }
static int bind_minimap(int a, int b, int c, int d, int e, int f)   /* TODO(rl-link): engine_battlefield_render_bind_minimap_context 0x4bc107 */
{ return (int)engine_retail_call(0x4bc107u, 6, (uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d, (uint32_t)e, (uint32_t)f); }
static void mp_leave_session(void) { engine_retail_call(0x4b743bu, 0); }   /* TODO(rl-extern): 0x4b743b */
static void mp_close(int a) { engine_retail_call(0x43e8f4u, 1, (uint32_t)a); }   /* TODO(rl-extern): 0x43e8f4 */

/* Port of FUN_0043d5b7 (146 B) — Go (the skirmish's battle start): in MP only when the host's ready flag
 * (DAT_005533e0[DAT_0056d5cc]) is set and — unless DAT_00591558 — after message 87 a connected session (DAT_00553248).
 * The battle reset (0x43f304), the event queue (0x472ae0), the setup's records (0x42ba40, 0x42c5ad), DAT_005533d0 = 1,
 * DAT_0053f018 = 0, the battlefield entry (0x4778a0), engine state 3. */
int rl_43d5b7(void)
{
    if (g_sim_mp_active != 0) {
        if (AT(int32_t, g_rl_5533e0, 0x5533e0u, 0x5533e0u + 4 * s_slot_0056d5cc) < 1)   /* by player: past 0x5533e8 (quirk) */
            return 0;
        if (g_rl_591558 == 0) {
            rl_43eda0(87, 0);
            if (g_rl_553248 == 0)
                return 0;
        }
    }
    bf_43f304();
    evq_init();
    rl_42ba40();
    rl_42c5ad();
    s_slot_005533d0 = 1;
    g_rl_53f018 = 0;
    int r = bf_4778a0();
    g_engine_state = 3;
    return r;
}

/* Port of FUN_0043d649 (94 B) — Back (the skirmish setup → the title): MP leaves the session (0x4b743b, 0x43e8f4(1));
 * DAT_005530f0 = 1, the skirmish mode off (DAT_0057a0f0), sub-state 0, the misc sheet (0x499034) and the system sheet 0
 * (0x499a1c), redraw. */
int rl_43d649(void)
{
    if (g_sim_mp_active != 0) {
        mp_leave_session();
        mp_close(1);
    }
    g_rl_5530f0 = 1;
    s_slot_0057a0f0 = 0;
    s_slot_0057c8fc = 0;
    load_misc_sheet();
    int r = load_system_sheet(0);
    engine_set_demand_redraw(2);
    return r;
}

/* the custom army's total (+0x168) = the sum of its 7 troop counts (+0x16c words, signed) */
static void cust_total(void)
{
    TE32(CUST, 0x52f218u) = 0;
    for (int i = 0; i < 7; i++)
        TE32(CUST, 0x52f218u) = add32(TE32(CUST, 0x52f218u), TE16(CUST, 0x52f21cu + 2 * i));
}

/* Port of FUN_0043d6c3 (200 B) — Cust.: one more troop of class DAT_0059154c when the army has room (0x42c4e2): its
 * count (+0x16c word) + 1, the total, the panel (0x42c476), redraw. */
int rl_43d6c3(void)
{
    int r = rl_42c4e2(K59154C);
    if (r == 0)
        return 0;
    TE16(CUST, 0x52f21cu + 2 * K59154C)++;
    cust_total();
    r = rl_42c476();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043d7a7 (219 B) — Cust.: one troop of class DAT_0059154c less (while its count > 0), the total, the
 * panel (0x42c476), redraw. */
int rl_43d7a7(void)
{
    int r = TE16(CUST, 0x52f21cu + 2 * K59154C);
    if (r <= 0)
        return r;
    TE16(CUST, 0x52f21cu + 2 * K59154C)--;
    cust_total();
    r = rl_42c476();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043d6a7 (28 B) — Cust. +: one troop more (0x43d6c3), message 82. */
int rl_43d6a7(void)
{
    rl_43d6c3();
    return rl_43eda0(82, 0);
}

/* Port of FUN_0043d78b (28 B) — Cust. −: one troop less (0x43d7a7), message 82. */
int rl_43d78b(void)
{
    rl_43d7a7();
    return rl_43eda0(82, 0);
}

/* Port of FUN_0043d882 (55 B) — Cust. max: troops of the class while the army has room (0x42c4e2 / 0x43d6c3),
 * message 82. */
int rl_43d882(void)
{
    while (rl_42c4e2(K59154C) != 0)
        rl_43d6c3();
    return rl_43eda0(82, 0);
}

/* Port of FUN_0043d8b9 (74 B) — Cust. none: troops of the class off while any (0x43d7a7), message 82. */
int rl_43d8b9(void)
{
    while (TE16(CUST, 0x52f21cu + 2 * K59154C) > 0)
        rl_43d7a7();
    return rl_43eda0(82, 0);
}

/* the setup's battle record change: the battle index DAT_0056d590 = the list's top DAT_0053f64c + row DAT_0053e9a8;
 * a siege (mode DAT_0053e91c 2) takes its castle kind DAT_004d4b58[battle] into DAT_0057c96c / DAT_0057c910 and loads
 * the skirmish data (0x498dcb); the setup's records (0x42b9c4, 0x42ba40) and page (0x42bf46), redraw */
static int battle_changed(void)
{
    s_slot_0056d590 = s_slot_0053e9a8 + s_slot_0053f64c;
    if ((int)s_slot_0053e91c == 2) {
        g_rl_57c96c = g_rl_4d4b58[s_slot_0056d590];
        s_slot_0057c910 = (uint32_t)g_rl_57c96c;
        rl_498dcb();
    }
    rl_42b9c4();
    rl_42ba40();
    int r = rl_42bf46();
    engine_set_demand_redraw(2);
    return r;
}
#define K591550 AT(int32_t, g_bf_59154c, 0x59154cu, 0x591550u)   /* DAT_00591550: the MP message's parameter */

/* Port of FUN_0043d929 (164 B) — the battle list's rows: a row DAT_0059154c other than the current (DAT_0053e9a8)
 * becomes it (MP message 83 with parameter 1 unless DAT_00591558), the battle changed. */
int rl_43d929(void)
{
    if (K59154C == (int)s_slot_0053e9a8)
        return (int)s_slot_0053e9a8;
    K591550 = 1;
    if (g_rl_591558 == 0)
        rl_43eda0(83, 0);
    s_slot_0053e9a8 = (uint32_t)K59154C;
    return battle_changed();
}

/* Port of FUN_0043d9cd (209 B) — the battle list's scroll arrows: the top DAT_0053f64c moves by DAT_0059154c (MP
 * message 83 / 2 unless DAT_00591558), clamped to 0..DAT_0053f0d4 − 6 (a clamp returns without the change), else the
 * battle changed. */
int rl_43d9cd(void)
{
    K591550 = 2;
    if (g_rl_591558 == 0)
        rl_43eda0(83, 0);
    s_slot_0053f64c = (uint32_t)((int)s_slot_0053f64c + K59154C);
    if ((int)s_slot_0053f0d4 - 6 < (int)s_slot_0053f64c) {
        s_slot_0053f64c = s_slot_0053f0d4 - 6;
        return (int)s_slot_0053f64c;
    }
    if ((int)s_slot_0053f64c < 0) {
        s_slot_0053f64c = 0;
        return (int)s_slot_0053f0d4 - 6;
    }
    return battle_changed();
}

/* Port of FUN_0043da9e (85 B) — the advantage toggle: DAT_0056899c flipped, both sides' advantage (byte +0x54 /
 * +0x80 of the player aux records 1 / 2) reset to 2, message 95, redraw. */
int rl_43da9e(void)
{
    g_rl_56899c = g_rl_56899c == 1 ? 0 : 1;
    PAUX8(1, 0x553d78u) = 2;
    PAUX8(2, 0x553d78u) = 2;
    int r = rl_43eda0(95, 0);
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043daf3 (185 B) — the advantage arrows: DAT_0059154c 1 moves one point from side 1 (+0x54) to side 2
 * (+0x80), 2 back; both clamped to 1..3 (unsigned bytes); message 96, the page (0x42bf46), redraw. */
int rl_43daf3(void)
{
    if (K59154C == 1) {
        PAUX8(1, 0x553d78u)--;
        PAUX8(2, 0x553d78u)++;
    } else if (K59154C == 2) {
        PAUX8(1, 0x553d78u)++;
        PAUX8(2, 0x553d78u)--;
    }
    if (PAUX8(1, 0x553d78u) > 3)
        PAUX8(1, 0x553d78u) = 3;
    if (PAUX8(1, 0x553d78u) < 1)
        PAUX8(1, 0x553d78u) = 1;
    if (PAUX8(2, 0x553d78u) > 3)
        PAUX8(2, 0x553d78u) = 3;
    if (PAUX8(2, 0x553d78u) < 1)
        PAUX8(2, 0x553d78u) = 1;
    rl_43eda0(96, 0);
    int r = rl_42bf46();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043dbac (113 B) — a side's difficulty button: player aux record DAT_0059154c's +0x28 byte cycles
 * 0..4; message 96, the page (0x42bf46), redraw. */
int rl_43dbac(void)
{
    if (++PAUX8(K59154C, 0x553d78u) > 4)
        PAUX8(K59154C, 0x553d78u) = 0;
    rl_43eda0(96, 0);
    int r = rl_42bf46();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043dc1d (358 B) — the setup's mode buttons (DAT_0059154c: 0 equal / 1 unequal / 2 siege / 3 the user
 * map; 3 without the file DAT_0055cd34 → 0): MP message 83 / 3 unless DAT_00591558; DAT_0053e91c = the mode, the
 * castle pass DAT_0056d5c0 (44 siege, else 28), the list length DAT_0053f0d4 (20 / 15 / 10), the siege flag
 * DAT_0053e8fc; a user map is read (0x42d4aa); the list from the top, the first battle (its castle kind for a
 * siege), the skirmish data (0x498dcb), the setup's records and page, redraw. */
int rl_43dc1d(void)
{
    K591550 = 3;
    if (g_rl_591558 == 0)
        rl_43eda0(83, 0);
    s_slot_0053e91c = (uint32_t)K59154C;
    if ((int)s_slot_0053e91c == 3 && g_rl_55cd34 != 0)
        s_slot_0053e91c = 0;
    s_slot_0056d5c0 = (int)s_slot_0053e91c == 2 ? 44 : 28;
    if ((int)s_slot_0053e91c == 3)
        s_slot_0053f0d4 = 20;
    else if ((int)s_slot_0053e91c == 2)
        s_slot_0053f0d4 = 15;
    else
        s_slot_0053f0d4 = 10;
    s_slot_0053e8fc = (int)s_slot_0053e91c == 2 ? 1 : 0;
    if ((int)s_slot_0053e91c == 3)
        rl_42d4aa();
    s_slot_0053f64c = 0;
    s_slot_0053e9a8 = 0;
    s_slot_0056d590 = s_slot_0053e9a8 + s_slot_0053f64c;
    if ((int)s_slot_0053e91c == 2) {
        g_rl_57c96c = g_rl_4d4b58[s_slot_0056d590];
        s_slot_0057c910 = (uint32_t)g_rl_57c96c;
    }
    rl_498dcb();
    rl_42b9c4();
    rl_42ba40();
    int r = rl_42bf46();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043dd83 (113 B) — the Attacker / Defender toggle: the human's side DAT_0053ef5c swaps between the local
 * player and DAT_0056d5cc (MP message 83 / 4 unless DAT_00591558); the setup's armies (0x42ac0c), records (0x42ba40)
 * and page (0x42bf46), redraw. */
int rl_43dd83(void)
{
    K591550 = 4;
    if (g_rl_591558 == 0)
        rl_43eda0(83, 0);
    if (s_slot_0053ef5c == s_slot_0057c8cc)
        s_slot_0053ef5c = s_slot_0056d5cc;
    else
        s_slot_0053ef5c = s_slot_0057c8cc;
    rl_42ac0c();
    rl_42ba40();
    int r = rl_42bf46();
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_0043ddf4 (114 B) — user.skr's file box (SP, or an MP host): with files (DAT_005653e8 > 0) open the
 * .skr list (DAT_005530f0 = 13, the page to return to DAT_00553e5c), the editor off (byte_4EABC2, DAT_0057d3c4,
 * byte_591548), redraw. */
int rl_43ddf4(void)
{
    if (g_sim_mp_active != 0 && g_rl_553248 == 0)
        return 0;
    if (g_rl_5653e8 <= 0)
        return 0;
    g_rl_4eabc2 = 0;
    int r = g_rl_5530f0;
    g_rl_553e5c = g_rl_5530f0;
    g_rl_57d3c4 = 0;
    g_rl_591548 = 0;
    g_rl_5530f0 = 13;
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_00498fb3 (129 B) — load the skirmish file `name` whole into a new `n`-byte block (the debug CRT's
 * malloc; io_read_file_at 0x4af8d9): the block, or NULL with "ERR:SKIRMISH Data alloc …" / "… load, couldn't find"
 * logged (the block is leaked on a failed read). */
int rl_498fb3(int name, int n)
{
    void *dst = rl_plat_malloc((uint32_t)n);
    if (dst == NULL) {
        rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4dbe08, 0x4dbe08u, 0x4dbe70u), name, 0);
        return 0;
    }
    if (read_file_at((const void *)(uintptr_t)(uint32_t)name, dst, (uint32_t)n, 0) == 0) {
        rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4dbe08, 0x4dbe08u, 0x4dbe98u), name, 0);
        return 0;
    }
    return (int)(uintptr_t)dst;
}

/* Port of FUN_00498dcb (488 B) — load the skirmish battlefield's data: the panel sheet (75000 B, 0x4b25b0), the
 * battlefield's sheets (0x49915e, 0x499366, 0x49964c), DAT_0053f050 = 2, the sprite / misc blocks (their names and
 * sizes in the .pl8 tables: DAT_004da550 + 0x154 / DAT_004da6b4, DAT_004daeb0 + 0x3c / DAT_004daefc) into DAT_0056d5a0 /
 * DAT_005530c8, the minimap pair (6000 B each: a siege's castle kind DAT_0057c96c 1 stone t2xstn1 / 2, else wood
 * t2xwod1 / 2; a field battle t2xbat1 twice) into DAT_0053f044 / DAT_0056d680 — every call allocates anew (the old
 * blocks leak) —, the hex grid (0x4bc020) and minimap (0x4bc107) contexts, "OK :SKIRMISH Data loaded OK.". Returns 1. */
int rl_498dcb(void)
{
    chdir_base();
    load_panel_sheet(75000);
    rl_49915e();
    rl_499366();
    rl_49964c();
    g_bf_53f050 = 2;
    g_bf_56d5a0 = rl_498fb3((int)((uintptr_t)g_bf_4da550 + 0x154u), g_rl_4da6b4);
    g_bf_5530c8 = rl_498fb3((int)((uintptr_t)g_bf_4daeb0 + 0x3cu), g_rl_4daefc);
#define NAME(va) ((int)(uintptr_t)&AT(uint8_t, g_rl_4dbe08, 0x4dbe08u, va))
    if ((int)s_slot_0053e91c == 2) {
        if (g_rl_57c96c == 1) {
            g_bf_53f044 = rl_498fb3(NAME(0x4dbe08u), 6000);
            g_bf_56d680 = rl_498fb3(NAME(0x4dbe14u), 6000);
        } else {
            g_bf_53f044 = rl_498fb3(NAME(0x4dbe20u), 6000);
            g_bf_56d680 = rl_498fb3(NAME(0x4dbe2cu), 6000);
        }
    } else {
        g_bf_53f044 = rl_498fb3(NAME(0x4dbe38u), 6000);
        g_bf_56d680 = rl_498fb3(NAME(0x4dbe44u), 6000);
    }
    const uint32_t hex[11] = { (uint32_t)g_bf_568208, (uint32_t)g_bf_57c928, (uint32_t)(uintptr_t)g_castles_state, 80,
                               80, 8, 0, 24, 15, 14, 32 };
    bind_hex_grid(hex);
    bind_minimap(g_bf_53f044, g_bf_56d680, g_bf_56d5a0, 466, 11, 2);
    rl_4afab9(NAME(0x4dbe50u), 0, 0);
#undef NAME
    return 1;
}

/* ================================================================ the MP session open (Multiple players) */

/* Port of FUN_0043f24b (185 B) — clear the MP chat state: DAT_0055cd80 = 0, the 8 x 65 chat lines DAT_005651e0, the
 * input line DAT_00553e80 and DAT_00565440 (65 bytes each). Returns 64 (retail's last loop EAX). */
int rl_43f24b(void)
{
    g_rl_55cd80 = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 65; j++)
            g_rl_5651e0[65 * i + j] = 0;
    for (int k = 0; k < 65; k++)
        g_rl_553e80[k] = 0;
    for (int m = 0; m < 65; m++)
        g_rl_565440[m] = 0;
    return 64;
}

/* Port of FUN_0043f484 (51 B) — forget the MP message buffers (DAT_005158d0 / d4, the two blocks DAT_005158e0 / e4). */
int rl_43f484(void)
{
    AT(int32_t, g_rl_5158d0, 0x5158d0u, 0x5158d0u) = 0;
    AT(int32_t, g_rl_5158d0, 0x5158d0u, 0x5158d4u) = 0;
    g_rl_5158e0 = 0;
    g_rl_5158e4 = 0;
    return 0;
}

/* Port of FUN_0043f352 (236 B) — the MP queues' reset (counters 0 / 1 / 2 at DAT_00512f40..98, DAT_0050d7b0 / b8), the
 * buffers forgotten (0x43f484) and two 0xb8d8-byte blocks allocated (the debug CRT's malloc). Returns 1 when both
 * exist. */
int rl_43f352(void)
{
    g_rl_512f88 = 0;
    g_rl_512f90[0] = 0;
    g_rl_512f98 = 0;
    g_rl_50d7b0[0] = 0;
    g_rl_50d7b8 = 0;
    g_rl_512f60[0] = 1;
    g_rl_512f68 = 1;
    g_rl_512f40[0] = 2;
    g_rl_512f48 = 2;
    rl_43f484();
    if (g_rl_5158e0 == 0)
        g_rl_5158e0 = (int32_t)(uintptr_t)rl_plat_malloc(0xb4f0u + 0x3e8u);
    if (g_rl_5158e4 == 0)
        g_rl_5158e4 = (int32_t)(uintptr_t)rl_plat_malloc(0xb4f0u + 0x3e8u);
    if (g_rl_5158e0 == 0)
        return 0;
    if (g_rl_5158e4 == 0)
        return 0;
    return 1;
}

/* the IDirectPlay object DAT_004e2a2c and its vtable slots (__stdcall, this first) */
#define DPLAY  AT(uint32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a2cu)
#define HWNDM  ((HWND)(uintptr_t)AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u))
#define DESC32(va) AT(int32_t, g_rl_4e5ab0, 0x4e5ab0u, va)   /* the DPSESSIONDESC DAT_004e5ab0 */
static uint32_t dp_slot(int off) { return ((const uint32_t *)(uintptr_t)*(const uint32_t *)(uintptr_t)DPLAY)[off / 4]; }
static int dp_call1(int off) { return ((int (__stdcall *)(uint32_t))(uintptr_t)dp_slot(off))(DPLAY); }
static void dp_drop(int close)   /* Close (+0x10) when asked, Release (+8), forget the object */
{
    if (close) dp_call1(0x10);
    dp_call1(0x08);
    DPLAY = 0;
}
/* the dialog procs (unported; retail's entries) — TODO(rl-extern): 0x4b7f78 (DialogFunc), 0x4b8335, 0x4b8439, 0x4b85b7 */
#define DLG(va) ((DLGPROC)(uintptr_t)(va))
static int mp_resume(void) { return (int)engine_retail_call(0x4b7a9au, 0); }   /* TODO(rl-extern): 0x4b7a9a */
static int mp_lobby_load(void) { return (int)engine_retail_call(0x49792eu, 0); }   /* TODO(rl-link): 0x49792e (engine.c / game_init.c) */
static void log3(uint32_t fmt_va, int a, int n) { rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4e2ae4, 0x4e2ae4u, fmt_va), a, n); }

/* Port of FUN_004b7585 (1118 B) — open the DirectPlay session for the session GUID `g0..g3` (by value): with a
 * SierraNW session (DAT_004e2a30) the lobby path (0x49792e, 0x497a34(1), 0x4b7a9a). Else the provider dialog (0x81,
 * DialogFunc): 1003 → 0x4b7a9a; no IDirectPlay (DAT_004e2a2c) → "OK :DP Not Created.", 0. Created: its caps logged
 * (GetCaps +0x38: baud, players, latency, header; a modem = 0x10000 / 250 / 8 → DAT_00553264), the session dialog
 * (0x6c): 1002 joins (DAT_00553248 = 0, DAT_004e5b08 = the GUID's address, dialog 0x82; nothing → "ERR:DP Could not
 * connect.", 0), 1004 hosts (a 1..2 provider DAT_005c9a74, else DAT_0058fe10 = 1 + Release, 0; DAT_00553248 = 1, the
 * DPSESSIONDESC DAT_004e5ab0 (0x50 B: 5 players, flags 0x44, the GUID twice), its name from dialog 0x74 (DAT_004e5a18,
 * else "Lords2 Session1"), Open +0x60 (failing: Release, 0)), anything else Close + Release, 0. Then the local player
 * "Nobleman" (CreatePlayer +0x18 into DAT_004e2a28; failing: Close + Release, 0): "OK :DP Local player created.", 1. */
int rl_4b7585(int g0, int g1, int g2, int g3)
{
    int guid[4] = { g0, g1, g2, g3 };
    if (g_rl_4e2a30 != 0) {
        mp_lobby_load();
        rl_497a34(1);
        return mp_resume();
    }
    g_rl_568464 = 0;
    if (DPLAY != 0 && g_rl_4e2a30 != 0)
        return 0;
    INT_PTR v = rl_plat_DialogBoxParamA(NULL, (const char *)(uintptr_t)0x81, HWNDM, DLG(0x4b7f78u), 0);
    if (v == 1003)
        return mp_resume();
    if (DPLAY == 0) {
        log3(0x4e2ae4u, 0, 0);   /* "OK :DP Not Created." */
        return 0;
    }
    log3(0x4e2af8u, 0, 0);   /* "OK :DP Created." */
    g_rl_553264 = 0;
    uint32_t caps[10];
    memset(caps, 0, sizeof caps);
    caps[0] = 40;
    ((int (__stdcall *)(uint32_t, void *, int))(uintptr_t)dp_slot(0x38))(DPLAY, caps, 1);
    log3(0x4e2b08u, 0, (int)caps[5] + 1);
    log3(0x4e2b2cu, 0, (int)caps[4] + 1);
    log3(0x4e2b54u, 0, (int)caps[6] + 1);
    log3(0x4e2b78u, 0, (int)caps[8] + 1);
    if (caps[4] == 0x10000u && caps[6] == 250u && caps[8] == 8u)
        g_rl_553264 = 1;
    log3(0x4e2b9cu, 0, g_rl_553264 + 1);
    v = rl_plat_DialogBoxParamA(NULL, (const char *)(uintptr_t)0x6c, HWNDM, DLG(0x4b8335u), 0);
    if (v == 1002) {
        log3(0x4e2bbcu, 0, 0);   /* "OK :DP Connecting." */
        g_rl_553248 = 0;
        g_rl_4e5b08 = (int32_t)(uintptr_t)guid;   /* retail: the address of its argument (a stack address in state) */
        rl_plat_DialogBoxParamA(NULL, (const char *)(uintptr_t)0x82, HWNDM, DLG(0x4b85b7u), 0);
        if (DPLAY == 0) {
            log3(0x4e2bd0u, 0, 0);   /* "ERR:DP Could not connect." */
            return 0;
        }
    } else if (v == 1004) {
        if (g_rl_5c9a74 < 1 || g_rl_5c9a74 > 2) {
            g_rl_58fe10 = 1;
            dp_drop(0);
            return 0;
        }
        g_rl_553248 = 1;
        uint8_t *desc = g_rl_4e5ab0;
        memset(desc, 0, 0x50);
        DESC32(0x4e5ab0u) = 80;
        DESC32(0x4e5ad8u) = 5;
        DESC32(0x4e5ab4u) = 68;
        memcpy(desc + 8, guid, 16);
        memcpy(desc + 0x18, guid, 16);
        if (rl_plat_DialogBoxParamA(NULL, (const char *)(uintptr_t)0x74, HWNDM, DLG(0x4b8439u), 0) != 0)
            DESC32(0x4e5ae0u) = (int32_t)(uintptr_t)&g_rl_4e5a18;
        else
            DESC32(0x4e5ae0u) = (int32_t)(uintptr_t)g_rl_4e2a38;
        DESC32(0x4e5ae4u) = 0;
        if (((int (__stdcall *)(uint32_t, void *, int))(uintptr_t)dp_slot(0x60))(DPLAY, desc, 2) != 0) {
            dp_drop(0);
            return 0;
        }
    } else {
        dp_drop(1);
        return 0;
    }
    uint32_t name[4];
    memset(name, 0, sizeof name);
    name[0] = 16;
    name[2] = (uint32_t)(uintptr_t)&AT(uint8_t, g_rl_4e2ae4, 0x4e2ae4u, 0x4e2becu);   /* "Nobleman" */
    if (((int (__stdcall *)(uint32_t, void *, void *, int, int, int, int))(uintptr_t)dp_slot(0x18))(
            DPLAY, g_rl_4e2a28, name, 0, 0, 0, 0) != 0) {
        dp_drop(1);
        return 0;
    }
    log3(0x4e2bf8u, 0, 0);   /* "OK :DP Local player created." */
    return 1;
}

static void mp_free_queues(void) { engine_retail_call(0x43f43eu, 0); }   /* TODO(rl-extern): 0x43f43e */
static void mp_link_init(void) { engine_retail_call(0x43f01du, 0); }   /* TODO(rl-extern): 0x43f01d */
static void mp_link_reset(void) { engine_retail_call(0x43fed2u, 0); }   /* TODO(rl-extern): 0x43fed2 */
static void mp_host_ready(void) { engine_retail_call(0x43306au, 0); }   /* TODO(rl-extern): 0x43306a */
static void mp_shutdown(int a) { engine_retail_call(0x4b7180u, 1, (uint32_t)a); }   /* TODO(rl-extern): 0x4b7180 */

/* Port of FUN_004b7250 (491 B) — Multiple players: the editor off, DAT_0055cd7c / DAT_0053f23c / MP off; the queues
 * (0x43f352; failing: 0x43f43e, 0); the session (0x4b7585 with the GUID DAT_004d57e0..ec; failing: MP off, 0). MP on,
 * the link (0x43f01d, 0x43fed2); a host is ready at once (0x43306a); a guest says hello (message 1), pumps the
 * messages (0x4b8c3a) until welcomed (DAT_0057c978) — hello again after 5 s, a full redraw at 100..200 ms; 10 s or an
 * abort (DAT_0058fe10): MP off, the local player destroyed (+0x24), "ERR:DP Timed out …", 0x4b7180(1), 0. Returns 1
 * when in. */
int rl_4b7250(void)
{
    g_rl_4eabc2 = 0;
    g_rl_55cd7c = 0;
    s_slot_0053f23c = 0;
    g_sim_mp_active = 0;
    if (rl_43f352() == 0) {
        mp_free_queues();
        return 0;
    }
    if (rl_4b7585(g_rl_4d57e0, g_rl_4d57e4, g_rl_4d57e8, g_rl_4d57ec) == 0) {
        g_sim_mp_active = 0;
        return 0;
    }
    g_sim_mp_active = 1;
    mp_link_init();
    mp_link_reset();
    g_rl_57c978 = 0;
    if (g_rl_553248 != 0) {
        mp_host_ready();
        g_rl_57c978 = 1;
        return 1;
    }
    g_rl_57d354 = 0;
    g_rl_58fe10 = 0;
    rl_43eda0(1, 0);
    g_rl_4e5a7c = (int32_t)rl_plat_GetTickCount();
    int again = 1;
    while (g_rl_57c978 == 0) {
        rl_4b8c3a();
        int dt = (int)(rl_plat_GetTickCount() - (uint32_t)g_rl_4e5a7c);
        if (g_rl_57c978 == 0 && again != 0 && dt > 5000) {
            again = 0;
            rl_43eda0(1, 0);
        }
        if (dt >= 10000 || g_rl_58fe10 != 0) {
            g_sim_mp_active = 0;
            if (DPLAY != 0)
                ((int (__stdcall *)(uint32_t, int32_t))(uintptr_t)dp_slot(0x24))(DPLAY, AT(int32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a28u));
            rl_4afab9((int)(uintptr_t)g_rl_4e2a8c, 0, dt + 1);
            mp_shutdown(1);
            return 0;
        }
        if (dt >= 100 && dt <= 200)
            rl_451f70();
    }
    return 1;
}

static int msg_pump(void) { return (int)engine_retail_call(0x40ec2cu, 0); }   /* TODO(rl-extern): 0x40ec2c */

/* Port of FUN_004adcf6 (413 B) — MP: wait (60 s, the message pump 0x40ec2c) until DAT_0057a0dc reaches the player
 * count DAT_00553f98 or no live player (+5 of the player records 1..5) is still unready (DAT_00553d30[i] 0): 1; a
 * player at 1 → 0; a closed pump stops the wait; a timeout logs "ERR:DP timed out on waiting for preload" + the five
 * states, 0. SP: 1. */
int rl_4adcf6(void)
{
    if (g_sim_mp_active == 0)
        return 1;
    g_rl_57a0dc = 1;
    uint32_t t0 = rl_plat_timeGetTime();
    int t = (int)t0;
    while ((int)(t0 + 60000u) > t) {
        t = (int)rl_plat_timeGetTime();
        if (msg_pump() == 0)
            break;
        if (g_rl_57a0dc >= (int)s_slot_00553f98)
            return 1;
        int waiting = 0;
        for (int i = 1; i < 6; i++) {
            if (AT(uint8_t, g_player_strategic_352, 0x57bf00u, 0x57bf05u + 352 * i) != 0) {
                if (g_rl_553d30[i] == 0)
                    waiting = 1;
                if (g_rl_553d30[i] == 1)
                    return 0;
            }
        }
        if (waiting == 0)
            return 1;
    }
    rl_4afab9((int)(uintptr_t)g_rl_4df08c, 0, t - (int)t0 + 1);
    rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df08c, 0x4df08cu, 0x4df0bcu), 0, g_rl_553d30[1] + 1);
    rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df08c, 0x4df08cu, 0x4df0d0u), 0, g_rl_553d30[2] + 1);
    rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df08c, 0x4df08cu, 0x4df0e4u), 0, g_rl_553d30[3] + 1);
    rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df08c, 0x4df08cu, 0x4df0f8u), 0, g_rl_553d30[4] + 1);
    rl_4afab9((int)(uintptr_t)&AT(uint8_t, g_rl_4df08c, 0x4df08cu, 0x4df10cu), 0, g_rl_553d30[5] + 1);
    return 0;
}

/* Port of FUN_004af70c (91 B) — a file's length (back in the base directory 0x4af502; _open _O_BINARY, _filelength,
 * _close), 0 when it does not open. */
int rl_4af70c(int name)
{
    chdir_base();
    int fd = rl_plat_open((const char *)(uintptr_t)(uint32_t)name, 0x8000, 0);
    if (fd == -1)
        return 0;
    int n = rl_plat_filelength(fd);
    rl_plat_close(fd);
    return n;
}

/* Port of FUN_004b3994 (84 B) — the battle result's cue `k` (0..8): S035_01.wav + 16 k (0x427990, kind 1), the audio
 * latch DAT_005533f0 = 1. */
int rl_4b3994(int k)
{
    if (k < 0 || k >= 9)
        return 0;
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)&g_rl_4e2168[16 * k], 1, 0);
}

/* ================================================================ the county field panel (terrain info, kind 19) */

static int num_units(int n, int str, int x, int y, const uint8_t *font, int color)   /* TODO(rl-link): labor_num_units 0x41ab67 */
{ return (int)engine_retail_call(0x41ab67u, 6, (uint32_t)n, (uint32_t)str, (uint32_t)x, (uint32_t)y, (uint32_t)(uintptr_t)font, (uint32_t)color); }
#define TI     g_rl_5681e0[0]                      /* DAT_005681e0: the county under the cursor */
#define ROW(k) (16 * g_rl_553d2c + (k))             /* the panel's first row DAT_00553d2c */

/* Port of FUN_0041cb3a (1887 B) — the county panel's field box, the county DAT_005681e0 owned locally: the farm
 * workers (+0xc4 with "units", 249 below the target +0xc8, 252 above the cap +0xcc, else 63) and the fields
 * (+0x224); with Advanced farming (DAT_0053f25c) the harvest's word (group 22, (+0x208 + 100) / 29); the grain
 * stock (+0x278 + its kind word 135 / 139 → group 77 strings 25 / 26; none: 24) and the stock's trend (+0x24c:
 * > 0 string 16, < 0 17 of its magnitude, 0 18); by the season DAT_0057c92c the cattle (1: +0x230 and +0x230 x
 * DAT_0057c8e0) or the land (2 / 3 / other: +0x248 in season 4, else +0x2fc, and the 3 / 2 / 1 fields per acre;
 * +0x240); the change rows (+0x178 negated, +0x22c: signed with labels, else "0"). The text cursor DAT_005cd404 is
 * reset to 0 before each composite line and read after each call (the callees advance it). */
int rl_41cb3a(void)
{
    if (CO8(TI, 0x53f9b5u) != LOCAL)
        return TI << 8;
    int c = 63;
    if (CO32(TI, 0x53fa78u) > CO32(TI, 0x53fa74u))
        c = 249;
    else if (CO32(TI, 0x53fa7cu) < CO32(TI, 0x53fa74u))
        c = 252;
    num_units(CO32(TI, 0x53fa74u), 32, 104, ROW(104), FONT, c);
    num_units(CO32(TI, 0x53fbd4u), 2, 296, ROW(104), FONT, 63);
    if ((int)s_slot_0053f25c == 1)
        wrap_line(22, (CO32(TI, 0x53fbb8u) + 100) / 29, 104, ROW(120), FONT, 63);
    g_gfx_line_x_acc = 0;
    if (CO32(TI, 0x53fc28u) == 0) {
        wrap_line(77, 24, 40, ROW(148), FONT, 63);
    } else {
        num_units(CO32(TI, 0x53fc28u), 2, 40, ROW(148), FONT, 63);
        if (AT(int16_t, g_per_army_768, 0x53f9b0u, 0x53fb5au + 768 * TI) == 135)
            wrap_line(77, 25, g_gfx_line_x_acc + 40, ROW(148), FONT, 63);
        else if (AT(int16_t, g_per_army_768, 0x53f9b0u, 0x53fb5au + 768 * TI) == 139)
            wrap_line(77, 26, g_gfx_line_x_acc + 40, ROW(148), FONT, 63);
    }
    if ((int)s_slot_0053f25c == 1) {
        g_gfx_line_x_acc = 0;
        if (CO32(TI, 0x53fbfcu) > 0) {
            num_units(CO32(TI, 0x53fbfcu), 2, g_gfx_line_x_acc + 40, ROW(164), FONT, 63);
            wrap_line(77, 16, g_gfx_line_x_acc + 40, ROW(164), FONT, 63);
        } else if (CO32(TI, 0x53fbfcu) < 0) {
            num_units(-CO32(TI, 0x53fbfcu), 2, g_gfx_line_x_acc + 40, ROW(164), FONT, 63);
            wrap_line(77, 17, g_gfx_line_x_acc + 40, ROW(164), FONT, 63);
        } else {
            wrap_line(77, 18, g_gfx_line_x_acc + 40, ROW(164), FONT, 63);
        }
    }
    if ((int)s_slot_0057c92c == 1) {
        g_gfx_line_x_acc = 0;
        num_units(CO32(TI, 0x53fbe0u), 2, g_gfx_line_x_acc + 40, ROW(192), FONT, 63);
        wrap_line(77, 1, g_gfx_line_x_acc + 40, ROW(192), FONT, 63);
        g_gfx_line_x_acc = 0;
        num_units((int32_t)((uint32_t)CO32(TI, 0x53fbe0u) * (uint32_t)g_rl_57c8e0), 2, g_gfx_line_x_acc + 40, ROW(208),
                  FONT, 63);
        wrap_line(77, 2, g_gfx_line_x_acc + 40, ROW(208), FONT, 63);
    } else {
        int per = (int)s_slot_0057c92c == 2 ? 3 : (int)s_slot_0057c92c == 3 ? 2 : 1;
        int land = (int)s_slot_0057c92c == 4 ? CO32(TI, 0x53fbf8u) : CO32(TI, 0x53fcacu);
        g_gfx_line_x_acc = 0;
        num_units(land, 2, g_gfx_line_x_acc + 40, ROW(192), FONT, 63);
        wrap_line(77, 3, g_gfx_line_x_acc + 40, ROW(192), FONT, 63);
        num_units(per, 66, g_gfx_line_x_acc + 40, ROW(192), FONT, 63);
        g_gfx_line_x_acc = 0;
        wrap_line(77, 0, g_gfx_line_x_acc + 40, ROW(208), FONT, 63);
        num_units(CO32(TI, 0x53fbf0u), 2, g_gfx_line_x_acc + 40, ROW(208), FONT, 63);
        wrap_line(77, 4, g_gfx_line_x_acc + 40, ROW(208), FONT, 63);
    }
    wrap_line(77, 27, 40, ROW(224), FONT, 63);
    if (CO32(TI, 0x53fb28u) == 0)
        label(0, 64, STR(g_rl_4d4240, 0x4d4240u, 0x4d4240u), 264, ROW(224), FONT, 63);
    else
        label_ls(-CO32(TI, 0x53fb28u), 0, (const uint8_t *)STR(g_rl_4d4240, 0x4d4240u, 0x4d4248u),
                 STR(g_rl_4d4240, 0x4d4240u, 0x4d4244u), 264, ROW(224), FONT, 63, 249);
    wrap_line(77, 28, 40, ROW(240), FONT, 63);
    if (CO32(TI, 0x53fbdcu) == 0)
        label(0, 64, STR(g_rl_4d4240, 0x4d4240u, 0x4d424cu), 264, ROW(240), FONT, 63);
    else
        label_ls(CO32(TI, 0x53fbdcu), 0, (const uint8_t *)STR(g_rl_4d4240, 0x4d4240u, 0x4d4254u),
                 STR(g_rl_4d4240, 0x4d4240u, 0x4d4250u), 264, ROW(240), FONT, 63, 249);
    return 0;
}
#undef ROW

/* ================================================================ the palette remap */

/* the nearest of the live palette's entries 10..245 (DAT_004ea1b0, RGB) to `rgb` (city-block distance, the first
 * minimum) */
static uint8_t nearest(const uint8_t *rgb)
{
    int best = 100000;
    uint8_t k0 = 0;
    for (int k = 10; k < 246; k++) {
        const uint8_t *p = &g_rl_4ea1b0[3 * k];
        int d = p[0] <= rgb[0] ? rgb[0] - p[0] : p[0] - rgb[0];
        d += p[1] <= rgb[1] ? rgb[1] - p[1] : p[1] - rgb[1];
        d += p[2] <= rgb[2] ? rgb[2] - p[2] : p[2] - rgb[2];
        if (best > d) {
            best = d;
            k0 = (uint8_t)k;
        }
    }
    return k0;
}

/* Port of FUN_004b14ff (872 B) — map the Windows system colours out of the back buffer: the remap table DAT_004eac60
 * cleared, entries 0..9 and 246..255 get the nearest of the live palette's 10..245 to the system palette's 20
 * colours (DAT_004d50f0: 0..9 and 10..19), then every back-buffer pixel below 10 or from 246 goes through it.
 * Nothing without a back buffer. Returns retail's AL (the last table byte or pixel written; 0 here). */
int rl_4b14ff(void)
{
    if (g_gfx_canvas_pixels == NULL)
        return 0;
    for (int i = 0; i < 256; i++)
        g_gfx_darken_remap[i] = 0;
    for (int j = 0; j < 10; j++)
        g_gfx_darken_remap[j] = nearest(&g_rl_4d50f0[3 * j]);
    for (int m = 246; m < 256; m++)
        g_gfx_darken_remap[m] = nearest(&AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d4e2eu + 3 * m));
    uint8_t *buf = g_gfx_canvas_pixels;
    for (int ii = 0; ii < 307200; ii++) {
        unsigned v = buf[ii];
        if (v < 10 || v >= 246)
            buf[ii] = g_gfx_darken_remap[v];
    }
    return 0;
}

/* ================================================================ the battlefield's group keys (skirmish / battle) */

/* Port of FUN_0043c77a (267 B) — WM_CHAR 'h' / 'v' (a1 0 / 1): in battle (engine state 3, no pause DAT_00553c6c) the
 * selected group DAT_0053e984's formation: the selection's input (0x499cb1(1)), its position (+0x1e / +0x20 words)
 * into DAT_00553f14 / 18, the order a1 + 1 (DAT_0059154c) with the local player's order serial (word DAT_00553100,
 * + 1 → DAT_00591550); MP sends message 67 (DAT_00591554 = 0), else the order (0x479e90(group, x, y, DAT_005533b8,
 * DAT_0053e874, a1 + 1)). */
int rl_43c77a(int a1)
{
    if (g_bf_553c6c != 0 || g_engine_state != 3)
        return 0;
    int g = (int)s_slot_0053e984;
    bf_499cb1(1);
    AT(int32_t, g_bf_553f14, 0x553f14u, 0x553f14u) = BG16(g, 0x56653eu);
    AT(int32_t, g_bf_553f14, 0x553f14u, 0x553f18u) = BG16(g, 0x566540u);
    K59154C = a1 + 1;
    g_bf_553100[LOCAL]++;
    K591550 = g_bf_553100[LOCAL];
    if (g_sim_mp_active != 0) {
        AT(int32_t, g_bf_59154c, 0x59154cu, 0x591554u) = 0;
        return rl_43eda0(67, 0);
    }
    return bf_479e90((int)s_slot_0053e984, BG16(g, 0x56653eu), BG16(g, 0x566540u), g_bf_5533b8, g_bf_53e874[0], a1 + 1);
}

/* Port of FUN_00479276 (229 B) — the player `p`'s battlefield selection as a bit mask: its 10 bytes DAT_0053e930 +
 * 10 p cleared, then bit (u − 1) set for every live unit u (1..80, +0x2c) selected by p (+9 == p). */
int rl_479276(int p)
{
    for (int i = 0; i < 10; i++)
        g_bf_53e930[10 * p + i] = 0;
    for (int j = 1; j < 81; j++)
        if (BU8(j, 0x5544acu) != 0 && BU8(j, 0x554489u) == (uint32_t)p)
            g_bf_53e930[10 * p + (j - 1) / 8] |= (uint8_t)(1 << ((j - 1) % 8));
    return 0;
}

/* Port of FUN_0047935b (280 B) — restore player `p`'s selection from its mask DAT_0053e930 + 10 p: each of p's units
 * (1..80, +0x2c == p) is selected (+9 = p, +0xa |= the player's colour bits +0xee) or deselected (+9 / +0xa = 0). */
int rl_47935b(int p)
{
    int u = 1;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 8; j++, u++) {
            if (BU8(u, 0x5544acu) != (uint32_t)p)
                continue;
            if ((g_bf_53e930[10 * p + i] & (1 << j)) != 0) {
                BU8(u, 0x554489u) = (uint8_t)p;
                BU8(u, 0x55448au) |= AT(uint8_t, g_player_strategic_352, 0x57bf00u, 0x57bfeeu + 352 * p);
            } else {
                BU8(u, 0x554489u) = 0;
                BU8(u, 0x55448au) = 0;
            }
        }
    }
    return 0;
}

/* Port of FUN_0043c885 (139 B) — Ctrl+1..9 on the battlefield (sub-state 2; key '1'..'9'): store the local player's
 * selection (0x479276) into group slot k (10 bytes of the 24-byte slot DAT_00553400 + 24 k, its +0xa flag = 1). */
int rl_43c885(int key)
{
    if ((int)s_slot_0057c8fc != 2 || key < '1' || key > '9')
        return 0;
    int k = key - '1';
    rl_479276(LOCAL);
    strutil_bounded_copy(&AT(uint8_t, s_slot_00553400, 0x553400u, 0x553400u + 24 * k), &g_bf_53e930[10 * LOCAL], 10);
    AT(uint8_t, s_slot_00553400, 0x553400u, 0x55340au + 24 * k) = 1;
    return 3 * k;
}

/* Port of FUN_0043c910 (344 B) — 1..9 on the battlefield: recall group slot k when stored (+0xa): the mask back into
 * the local player's selection (0x47935b), SP refreshes the selection (0x478987), the camera on the first selected
 * unit (its +0x20 / +0x22 cell − 7 → DAT_0055ce70 / 005678a0, clamped 0x47ed34), the map redraw gate = 2. */
int rl_43c910(int key)
{
    if ((int)s_slot_0057c8fc != 2 || key < '1' || key > '9')
        return 0;
    int k = key - '1';
    if (AT(uint8_t, s_slot_00553400, 0x553400u, 0x55340au + 24 * k) == 0)
        return 3 * k;
    strutil_bounded_copy(&g_bf_53e930[10 * LOCAL], &AT(uint8_t, s_slot_00553400, 0x553400u, 0x553400u + 24 * k), 10);
    rl_47935b(LOCAL);
    if (g_sim_mp_active == 0)
        bf_478987(LOCAL);
    for (int i = 1; i < 81; i++) {
        if (BU8(i, 0x5544acu) != 0 && BU8(i, 0x554489u) == (uint32_t)LOCAL) {
            s_slot_0055ce70 = (uint32_t)(BS16(i, 0x5544a0u) - 7);
            s_slot_005678a0 = (uint32_t)(BS16(i, 0x5544a2u) - 7);
            break;
        }
    }
    engine_camera_clamp_anchor((int *)&s_slot_0055ce70, (int *)&s_slot_005678a0);
    DIRTY->below_status_redraw = 2;
    return 0;
}
