/*
 * Unit tests for engine_army_move_confirm — the screen-0x10 hover
 * classifier + click resolver (FUN_004a8e0b/FUN_004a9252 + siblings).
 *
 * The hover classifier only re-runs its classification when the hovered
 * cell CHANGES (retail DAT_005691e0, see engine_army_move_confirm.c) —
 * that "last seen cell" is a module static with no reset hook, so tests
 * that rely on a fresh classification each use a DISTINCT source column
 * (see SRC_COL_x below) rather than a shared one, to avoid one test's
 * leftover "last cell" masking the next test's hover_tick call.
 */

#include <string.h>

#include "t.h"
#include "engine_army_move_confirm.h"
#include "engine_confirm_dialog.h"
#include "engine_path_field.h"
#include "engine_event_queue.h"
#include "engine.h"                /* g_sim_mp_active */
#include "save.h"
#include "save_schema_globals.h"

#define AREC(key)  (&g_per_army_768[(key) * 0x300])
#define TREC(key)  (&g_per_territory_420[(key) * 0x1a4])
#define CELL(idx)  (&g_overworld_map[(idx) * 8])
#define AI32(p, o) (*(int32_t *)((p) + (o)))

/* Move-cost table offsets (mirrors engine_path_field.c's own private
 * COST_* — not exposed via the header, so redefined here). */
#define COST_E 0x42

enum { SRC_ROW = 10 };

/* Open a single EAST step from (col, SRC_ROW) — deliberately narrow so a
 * flood only ever touches 2 cells (source + 1 east) and can never reach
 * a grid edge.  A blanket "every direction costs 1" table was tried
 * first and crashed (ASan global-buffer-overflow): with every direction
 * open the flood wraps at row/col 0, and the field arrays (unlike the
 * terrain table) have no margin for that. */
static int16_t s_terrain[ENGINE_PATH_FIELD_CELLS + 0x83];

static void amc_reset(int src_col)
{
    memset(g_per_army_768, 0, sizeof g_per_army_768);
    memset(g_per_territory_420, 0, sizeof g_per_territory_420);
    memset(g_overworld_map, 0, sizeof g_overworld_map);
    memset(s_terrain, 0, sizeof s_terrain);
    int src = SRC_ROW * ENGINE_PATH_FIELD_DIM + src_col;
    s_terrain[src + COST_E] = 1;
    engine_path_field_bind_terrain(s_terrain);
    g_sim_mp_active = 0;
}

/* Flood the path field from (col, SRC_ROW) for `player` — mirrors what
 * engine_army_select_open_handoff does when arming the handoff. */
static void flood_from(int player, int col)
{
    engine_path_field_fill(player, col, SRC_ROW, 0);
}

static engine_army_move_hover_in_t hover_in_base(int player, int armed_county)
{
    engine_army_move_hover_in_t in;
    memset(&in, 0, sizeof in);
    in.hover_valid  = 1;
    in.local_player = player;
    in.armed_county = armed_county;
    in.tile_stride  = 8;
    in.map_width    = ENGINE_PATH_FIELD_DIM;
    return in;
}

/* Force the module's internal "armed" + "last hover cell" state to a
 * known-disarmed baseline before a test that needs one (hover_tick's
 * invalid-hover branch unconditionally clears the armed flag; a bogus
 * hover_cell guarantees the NEXT real call sees "cell changed"). */
static void amc_force_disarmed(void)
{
    engine_army_move_hover_in_t in = hover_in_base(0, 0);
    in.hover_valid = 0;
    engine_army_move_hover_state_t st = {0};
    engine_army_move_confirm_hover_tick(&in, &st);
}

/* ── hover_tick ─────────────────────────────────────────────────────── */

int test_army_move_hover_invalid_disarms(void)
{
    amc_reset(10);
    amc_force_disarmed();

    engine_army_move_click_state_t cst = {-1, -1};
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_move_confirm_click_resolve(1, 5, &cst, &evq);
    T_ASSERT_EQ_I(cst.screen, -1);   /* untouched: hover never armed it */
    return 0;
}

int test_army_move_hover_too_close_disarms(void)
{
    enum { COL = 14 };
    amc_reset(COL);
    amc_force_disarmed();
    TREC(5)[0x0a] = COL; TREC(5)[0x0b] = SRC_ROW;
    flood_from(1, COL);   /* source cell itself: cost 1 */

    engine_army_move_hover_in_t in = hover_in_base(1, 5);
    in.hover_cell = SRC_ROW * ENGINE_PATH_FIELD_DIM + COL;   /* cost 1 < 2 */
    engine_army_move_hover_state_t st = {0};
    engine_army_move_confirm_hover_tick(&in, &st);

    engine_army_move_click_state_t cst = {-1, -1};
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_move_confirm_click_resolve(1, 5, &cst, &evq);
    T_ASSERT_EQ_I(cst.screen, -1);
    return 0;
}

int test_army_move_hover_decrements_cooldown(void)
{
    enum { COL = 18 };
    amc_reset(COL);
    amc_force_disarmed();
    TREC(5)[0x0a] = COL; TREC(5)[0x0b] = SRC_ROW;
    flood_from(1, COL);

    engine_army_move_hover_in_t in = hover_in_base(1, 5);
    in.hover_cell = SRC_ROW * ENGINE_PATH_FIELD_DIM + COL;
    engine_army_move_hover_state_t st;
    st.handoff_cooldown = 5;
    engine_army_move_confirm_hover_tick(&in, &st);
    T_ASSERT_EQ_I(st.handoff_cooldown, 4);
    return 0;
}

int test_army_move_hover_arms_on_reachable_cell_and_direct_dispatches(void)
{
    enum { COL = 22 };
    amc_reset(COL);
    amc_force_disarmed();
    TREC(5)[0x0a] = COL; TREC(5)[0x0b] = SRC_ROW;   /* armed county at the source */
    flood_from(1, COL);

    int hover_cell = SRC_ROW * ENGINE_PATH_FIELD_DIM + COL + 1;   /* one step E: cost 2 */
    engine_army_move_hover_in_t in = hover_in_base(1, 5);
    in.hover_cell = hover_cell;
    /* No classification bits set on this cell -> all six scratch globals
     * stay 0 -> click_resolve takes the DIRECT (no-dialog) path. */
    engine_army_move_hover_state_t st = {0};
    engine_army_move_confirm_hover_tick(&in, &st);

    engine_army_move_click_state_t cst = {-1, -1};
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_move_confirm_click_resolve(1, 5, &cst, &evq);
    T_ASSERT_EQ_I(cst.screen, 0);
    T_ASSERT_EQ_I(cst.demand_redraw, 2);
    /* Direct-execute path: army_move_confirm_execute reached the target
     * with a successful trace and stamped the move-state fields. */
    T_ASSERT_EQ_I(TREC(5)[0x14c], 2);
    return 0;
}

int test_army_move_hover_not_armed_click_resolve_is_noop(void)
{
    amc_reset(26);
    amc_force_disarmed();

    engine_army_move_click_state_t cst = {-1, -1};
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_move_confirm_click_resolve(1, 5, &cst, &evq);
    T_ASSERT_EQ_I(cst.screen, -1);
    T_ASSERT_EQ_I(evq.write_idx, 0);
    return 0;
}

/* ── click_resolve — the 0x11 popup path (classify_garrison) ─────────── */

int test_army_move_click_garrison_opens_dialog_then_posts_0x11_on_yes(void)
{
    enum { COL = 30 };
    amc_reset(COL);
    amc_force_disarmed();
    /* Armed county 5 at the source cell; hovered cell (one step E) holds
     * army 9, same owner as county 5 (garrison-bit 0x80 set, terrain type
     * > 0x14). */
    TREC(5)[0x0a] = COL; TREC(5)[0x0b] = SRC_ROW;
    /* armed county + the garrison army are BOTH owned by local_player (1)
     * — a realistic same-player scenario.  audience for this event is
     * the armed county's owner, which must equal local_player or the
     * event queue's own broadcast filter (audience==0 || audience==
     * local_player) silently drops the post. */
    TREC(5)[0] = 1;
    /* keeplvl 7 -> k_garrison_cap[7] == 50 (engine_combat.c); troops
     * already at the armed county (60) must exceed that cap to take the
     * 0xa6 (the actual popup) branch rather than the 0x11b ("no room at
     * all") or execute (plenty of room) branches. */
    AI32(TREC(5), 0x168) = 60;
    TREC(5)[0x154] = 5;          /* presence accumulator baseline (+0x154) minus
                                  * +0x153 (0 here) must be >= cost (2) or the
                                  * hover tick re-clears all six classification
                                  * globals it just set (a defensive re-check
                                  * retail runs after a successful path trace) */
    flood_from(1, COL);

    int hover_cell = SRC_ROW * ENGINE_PATH_FIELD_DIM + COL + 1;
    CELL(hover_cell)[1] = 0x80;     /* garrison bit */
    CELL(hover_cell)[0] = 0x15;     /* terrain type > 0x14 */
    CELL(hover_cell)[7] = 9;        /* head_army = 9 */
    AREC(9)[5] = 1;                 /* army 9 owner == armed county's owner (1) */
    AREC(9)[0x1c0] = 7;              /* keep level 7 -> garrison cap 50 (< 60)  */
    AREC(9)[0x1c3] = 0;              /* not "upgrade in progress"               */
    AI32(AREC(9), 0x1bc) = 0;        /* no target link -> cap stays uncapped    */

    engine_army_move_hover_in_t in = hover_in_base(1, 5);
    in.hover_cell = hover_cell;
    engine_army_move_hover_state_t st = {0};
    engine_army_move_confirm_hover_tick(&in, &st);

    engine_army_move_click_state_t cst = {-1, -1};
    engine_event_queue_state_t evq;
    engine_event_queue_init(&evq);
    engine_army_move_confirm_click_resolve(1, 5, &cst, &evq);

    /* Dialog opened, NOT yet fired — screen is the confirm-dialog's. */
    T_ASSERT_EQ_I(cst.screen, ENGINE_CONFIRM_DIALOG_SCREEN);
    T_ASSERT_EQ_I(evq.write_idx, 0);

    /* Click "Yes" (settle the type-5 press animation — see
     * test_engine_confirm_dialog.c for the button geometry). */
    engine_menu_click_in_t press;
    memset(&press, 0, sizeof press);
    press.cursor_x = 0xa0 + 64 + 4;
    press.cursor_y = 0xa0 + 46 + 4;
    press.lmb_newly_pressed = 1;
    engine_confirm_dialog_input_step(&press);
    press.lmb_newly_pressed = 0;
    for (int i = 0; i < 20; i++) {
        engine_confirm_dialog_input_step(&press);
    }

    T_ASSERT_EQ_I(evq.write_idx, 1);
    T_ASSERT_EQ_I(evq.ring[0].code, 0xa6);
    T_ASSERT_EQ_I(evq.ring[0].audience, 1);      /* armed county's owner */
    T_ASSERT_EQ_I(evq.ring[0].payload32a, 5);     /* armed_county key */
    T_ASSERT_EQ_I(evq.ring[0].flag5, 0x11);
    T_ASSERT_EQ_I(evq.ring[0].flag6, 9);           /* the garrison army's key */

    int redraw = -1;
    T_ASSERT_EQ_I(engine_army_move_confirm_poll_screen_reset(&redraw), 1);
    T_ASSERT_EQ_I(redraw, 2);
    T_ASSERT_EQ_I(engine_army_move_confirm_poll_screen_reset(&redraw), 0);
    return 0;
}
