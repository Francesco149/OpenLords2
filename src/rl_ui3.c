/* rl_ui3.c — a retail-layout port, partition 'ui3' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * What the round-2 scenarios newly reach: the options menu's handlers (Advanced / Sounds / Display panels, the
 * speed arrows, help), the noble faces' message / alliance buttons, the army split (Divide: screen 0x11, its
 * arrows and the commit), the field prompt's siege-attack check, the Alt+digit .rec replay load and the menu
 * close. Findings: docs/findings/rl-ui3.md. */
#include "rl.h"
#include "rl_ui3_globals.h"
#include "rl_glue_globals.h"
#include "engine_bf_input_globals.h"   /* g_bf_59154c: the router param block */
#include "engine.h"                    /* g_sim_mp_active, the screen / redraw bridges */
#include "engine_combat.h"             /* g_combat_flag_53f248, g_combat_audio_latch, engine_combat_recompute_condition */
#include "engine_territory.h"          /* the bound dirty state (DAT_0053e9ac), 0x45240a, place_entity, current slot */
#include "engine_map_query.h"          /* g_cell_cursor_col / _row (DAT_00569570 / 78: a mirror), 0x46733c */
#include "engine_path_field.h"         /* 0x46f700 / 0x4701ac / 0x4707be */
#include "engine_army_ai.h"            /* the ranking-dirty flag DAT_0056d6a0 (a mirror) */
#include "engine_army_economy.h"       /* 0x44df5f / 0x4485a5 */
#include "engine_diplomacy_action.h"   /* g_diplo_action_mode (DAT_005651cc), g_diplo_action_taken (DAT_0052afa4) */
#include "engine_mapview_globals.h"    /* g_mv_ti_county (DAT_0052afbc) */
#include "engine_map_scene.h"          /* engine_map_scene_tick_frame (0x4050c0) */
#include "engine_title_render.h"       /* g_tr_p4_gate_b (DAT_00553248: the MP host flag) */
#include "gfx_globals.h"               /* g_gfx_menu_redraw_a / _b (DAT_004eb260 / DAT_004e65f8) */
#include "rl_io_plat.h"                /* WinHelpA */

/* other partitions' globals (their headers conflict in one unit with rl_glue_globals.h: declared here as there) */
extern int32_t g_rl_53f20c;          /* DAT_0053f20c (rl_io_globals.h): the Advanced panel's exploration option */
extern int32_t g_rl_53f218;          /* DAT_0053f218 (rl_io_globals.h): music on */
extern int32_t g_rl_53f228;          /* DAT_0053f228 (rl_io_globals.h): full screen */
extern int32_t g_rl_5689a4;          /* DAT_005689a4 (rl_io_globals.h): "cannot change display" said once */
extern uint8_t g_rl_4d89b8[0x84];    /* DAT_004d89b8.. (rl_render_globals.h): DAT_004d8a0c the castle tiers' garrison caps */
extern uint8_t g_rl_4e5a00[4];       /* hWndMain (rl_misc_globals.h) */

/* ---------------------------------------------------------------- records and retail's globals */
#define PL8(p, va)    AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define CO8(c, va)    AT(uint8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))           /* county record */
#define CO32(c, va)   AT(int32_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define TER8(i, va)   AT(uint8_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))      /* army record */
#define TERS8(i, va)  AT(int8_t,  g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
#define TER16(i, va)  AT(int16_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
#define TER32(i, va)  AT(int32_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
/* the muster rows DAT_0053f6a0: 128 B per player, 16 B per troop row i (0..6; row 7 = the siege engines),
 * +0 the count kept / staying, +4 the count split off */
#define MUSTER(p, i, off) AT(int32_t, g_rl_53f6a0, 0x53f6a0u, 0x53f6a0 + 128 * (p) + 16 * (i) + (off))
#define CAP(t)        AT(int32_t, g_rl_4d89b8, 0x4d89b8u, 0x4d8a0c + 4 * (t))                    /* DAT_004d8a0c[t] */
#define RECST(i)      AT(uint8_t, g_rl_50d7c0, 0x50d7c0u, 0x50d7f6 + 56 * (i))                    /* byte_50D7F6[56 i] */
#define SIEGE_SLOT(v) AT(int16_t, g_unit_table_20, 0x568dc0u, 0x568dc0 + 20 * (v))                /* word_568DC0[10 v] */

#define PARAM0   AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)   /* DAT_0059154c: the button router's param0 */
#define LOCAL    ((int)s_slot_0057c8cc)                           /* DAT_0057c8cc: the local player */
#define SUBSTATE ((int32_t)s_slot_0057c8fc)                       /* DAT_0057c8fc: the frame driver's sub-state */
#define MP_ON    (g_sim_mp_active != 0)                           /* DAT_00553030 */
#define BELOW_STATUS() (engine_territory_dirty_state()->below_status_redraw)   /* DAT_0053e9ac (a mirror) */
#define NEWSLOT  engine_territory_current_slot()                  /* DAT_0052afe0 (a mirror) */
#define STRVA(blk, blkva, va) ((int)(uintptr_t)&AT(uint8_t, blk, blkva, va))   /* a retail string, as the int arg */

/* ---------------------------------------------------------------- seams (callees outside the partition) */
/* ported, not on retail's layout / not linked in rhost — TODO(rl-link) */
static int evq_post(int a, int b, int code, int d, int e, int f, int g, int h)   /* engine_event_queue_post 0x472bc5 (a port struct) */
{
    return (int)engine_retail_call(0x472bc5u, 8, (uint32_t)a, (uint32_t)b, (uint32_t)code, (uint32_t)d, (uint32_t)e,
                                   (uint32_t)f, (uint32_t)g, (uint32_t)h);
}
static void editor_open(const void *buf, int max, int w, int flag)   /* text_input_init 0x402009 (a port struct) */
{ rl_402009((int)(uintptr_t)buf, max, w, flag); }
static void editor_length(void) { rl_401f49(); }   /* text_input_recompute_length 0x401f49 (rl ui4) */
static int  cue_release(void) { return (int)engine_retail_call(0x427d19u, 0); }   /* audio_cue_release 0x427d19 (audio.c) */
static int  palette_full(void) { return (int)engine_retail_call(0x4b2922u, 0); }  /* gfx_set_palette_full_range 0x4b2922 (gfx.c) */
static int  read_file_at(int name, void *buf, int n, int off)   /* io_read_file_at 0x4af8d9 (io.c) */
{ return (int)engine_retail_call(0x4af8d9u, 4, (uint32_t)name, (uint32_t)(uintptr_t)buf, (uint32_t)n, (uint32_t)off); }
static int  roster_slot(int p) { return (int)engine_retail_call(0x4a9f72u, 1, (uint32_t)p); }   /* spawn_pick_roster_slot 0x4a9f72 (engine_combat.c: static) */
/* unported — TODO(rl-extern) */
static int  stream_stop(int a) { return (int)engine_retail_call(0x426d11u, 1, (uint32_t)a); }   /* 0x426d11: the music stream stop */

/* ================================================================ the options menu */

/* Port of FUN_00434556 (122 B) — Advanced: the farming toggle (router param0 forced to 1). MP sends message 0x32
 * and returns to the saved screen (DAT_0056d894); single player flips DAT_0053f25c. Redraw 2. */
int rl_434556(void)
{
    PARAM0 = 1;
    if (MP_ON)
        rl_43eda0(50, 0);
    else
        s_slot_0053f25c = s_slot_0053f25c != 1;
    if (MP_ON)
        engine_set_current_screen((uint8_t)g_rl_56d894);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_004345d0 (195 B) — Advanced: the foraging toggle (param0 2): as 0x434556 over DAT_0053f260; single
 * player then re-runs every realm's provision and plan (1..DAT_0056d5dc: 0x44df5f, 0x4485a5 with the season
 * phases DAT_0057c934 / DAT_0057c92c). */
int rl_4345d0(void)
{
    PARAM0 = 2;
    if (MP_ON) {
        rl_43eda0(50, 0);
    } else {
        s_slot_0053f260 = s_slot_0053f260 != 1;
        for (int i = 1; i <= (int32_t)s_slot_0056d5dc; i++) {
            engine_army_economy_provision(i, (int)s_slot_0057c934);
            engine_army_economy_plan_realm(i, (int)s_slot_0057c92c);
        }
    }
    if (MP_ON)
        engine_set_current_screen((uint8_t)g_rl_56d894);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434693 (122 B) — Advanced: the exploration toggle (param0 3): as 0x434556 over DAT_0053f264. */
int rl_434693(void)
{
    PARAM0 = 3;
    if (MP_ON)
        rl_43eda0(50, 0);
    else
        s_slot_0053f264 = s_slot_0053f264 != 1;
    if (MP_ON)
        engine_set_current_screen((uint8_t)g_rl_56d894);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_0043480c (62 B) — a help page: help event 291 with 19 in its fifth argument (the How do I pages
 * 292..295 are 0x43484a .. 0x434904, rl_ui2), back to the saved screen (DAT_0056d894), the action taken
 * (DAT_0052afa4). */
int rl_43480c(void)
{
    evq_post(0, LOCAL, 291, 0, 19, 0, 0, 0);
    engine_set_current_screen((uint8_t)g_rl_56d894);
    g_diplo_action_taken = 1;
    return 0;
}

/* Port of FUN_00434942 (42 B) — Help Options' Start game help: WinHelpA(hWndMain, "l2help.hlp", HELP_FINDER 3,
 * 1); redraw 2. (rhost stubs WinHelpA: rh_win.c.) */
int rl_434942(void)
{
    HWND w = (HWND)(uintptr_t)AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u);
    rl_plat_WinHelpA(w, (const char *)g_rl_4d5748, 3, 1);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_004349a4 (133 B) — Sounds: the music toggle DAT_0053f218. On: the strategic music (0x499aca,
 * sub-state 0) or the battle music (0x477b2f, sub-state 2); off: the stream stopped (0x426d11(0)) and the cue
 * released (0x427d19). Redraw 2. */
int rl_4349a4(void)
{
    g_rl_53f218 = g_rl_53f218 != 1;
    if (g_rl_53f218 == 0) {
        stream_stop(0);
        cue_release();
    } else if (SUBSTATE == 0) {
        rl_499aca();
    } else if (SUBSTATE == 2) {
        bf_477b2f();
    }
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434a29 (113 B) — Sounds: the effects toggle DAT_0053f214; turned on, the ambient loop restarts
 * (0x499d7d strategic, 0x499d97 battle). Redraw 2. */
int rl_434a29(void)
{
    g_bf_53f214 = g_bf_53f214 != 1;
    if (g_bf_53f214 == 1) {
        if (SUBSTATE == 0)
            rl_499d7d();
        else if (SUBSTATE == 2)
            bf_499d97();
    }
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434a9a (59 B) — an options toggle of DAT_0053f20c (two buttons: the tables at 0x4ddca8 / 0x4dde70);
 * redraw 2. */
int rl_434a9a(void)
{
    g_rl_53f20c = g_rl_53f20c != 1;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434ad5 (59 B) — an options toggle of DAT_0053f248 (the result movies; two buttons: 0x4ddcc0 /
 * 0x4ddeb8); redraw 2. */
int rl_434ad5(void)
{
    g_combat_flag_53f248 = g_combat_flag_53f248 != 1;
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00434b10 (281 B) — Display: Full screen. Back to the map (screen 0, or 0x29 on the battlefield);
 * redraw 2. Full screen at a depth other than 8 bpp (DAT_0053f22c): post 0x104 ("cannot change display").
 * Windowed at <= 640 px wide (DAT_004e65a0) the first time: post 0x12b and remember it (DAT_005689a4). Else
 * flip DAT_0053f228 and re-create the display (0x4b28cd(1), the full palette 0x4b2922, then the repaint
 * 0x451f70) with DAT_004df290 raised around it. */
int rl_434b10(void)
{
    engine_set_current_screen(SUBSTATE != 0 ? 41 : 0);
    engine_set_demand_redraw(2);
    if (g_rl_53f228 == 1 && g_rl_53f22c != 8)
        return evq_post(0, LOCAL, 260, 0, 0, 0, 0, 0);
    if (g_rl_53f228 == 0 && g_rl_4e65a0 <= 640 && g_rl_5689a4 == 0) {
        g_rl_5689a4 = 1;
        return evq_post(0, LOCAL, 299, 0, 0, 0, 0, 0);
    }
    g_rl_4df290 = 1;
    g_rl_53f228 = g_rl_53f228 == 0;
    rl_4b28cd(1);
    palette_full();
    g_rl_4df290 = 0;
    return rl_451f70();
}

/* Port of FUN_00434e9e (43 B) — a speed arrow up: the slider's value (*DAT_005cd410) += its step DAT_0058fd64
 * while below its maximum DAT_005c9260. */
int rl_434e9e(void)
{
    int32_t *v = (int32_t *)(uintptr_t)(uint32_t)g_rl_5cd410;
    if (*v < g_rl_5c9260)
        *v += g_rl_58fd64;
    return 0;
}

/* Port of FUN_00434ec9 (48 B) — a speed arrow down: −= the step while above the minimum DAT_005c927c. */
int rl_434ec9(void)
{
    int32_t *v = (int32_t *)(uintptr_t)(uint32_t)g_rl_5cd410;
    if (*v > g_rl_5c927c)
        *v -= g_rl_58fd64;
    return 0;
}

/* Port of FUN_0040df62 (107 B) — close the pull-down menu: the menu bar's redraw flags (DAT_004eb260 0 → 2 around
 * the empty 0x4b1de0, DAT_004e65f8 1), the map below the status bar repainted (DAT_0053e9ac), back to the saved
 * screen (DAT_0056d894), the menu's open / hover state cleared (DAT_00522cb0 / DAT_00522cb4 / DAT_00522cac), the
 * rankings dirty (DAT_0056d6a0); redraw 2. */
int rl_40df62(void)
{
    g_gfx_menu_redraw_a = 0;
    /* 0x4b1de0: empty (prologue / epilogue only) */
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    BELOW_STATUS() = 1;
    engine_set_current_screen((uint8_t)g_rl_56d894);
    g_rl_522cb0 = 0;
    g_rl_522cb4 = 0;
    g_rl_522cac = 0;
    engine_army_ai_set_ranking_dirty(1);
    engine_set_demand_redraw(2);
    return (uint8_t)g_rl_56d894;
}

/* Port of FUN_004477e7 (503 B) — Alt+digit (0x4b29be): load a recorded game state into the 400 × 56-B table
 * DAT_0050d7c0 (0x5780 B from offset 0): key 0 "master.rec" / "slave.rec", 2..5 "masterN.rec" / "slaveN.rec" (MP
 * host DAT_00553248 picks master); any other key reads nothing. Then every entry's +0x36 state 2 becomes 1. */
int rl_4477e7(int key)
{
    static const uint32_t k_name[6][2] = {   /* {slave, master}: the strings in DAT_004d6080.. */
        { 0x4d608cu, 0x4d6080u }, { 0, 0 }, { 0x4d60a4u, 0x4d6098u }, { 0x4d60bcu, 0x4d60b0u },
        { 0x4d60d4u, 0x4d60c8u }, { 0x4d60ecu, 0x4d60e0u },
    };
    if (key == 0 || (key >= 2 && key <= 5)) {
        uint32_t va = k_name[key][g_tr_p4_gate_b != 0];
        read_file_at(STRVA(g_rl_4d6080, 0x4d6080u, va), g_rl_50d7c0, 0x5780, 0);
    }
    for (int i = 0; i < 400; i++)
        if (RECST(i) == 2)
            RECST(i) = 1;
    return 0;
}

/* ================================================================ the noble faces' buttons */

/* Port of FUN_004361da (79 B) — the noble's message button: the diplomacy screen 0x1a in mode 2, the editor on
 * its lords2.inf text (byte_53F1E0 + 0x1a0: 200 chars, 10000 px, flag 0), the map scene ticked (0x4050c0);
 * redraw 2. */
int rl_4361da(void)
{
    engine_set_current_screen(26);
    g_diplo_action_mode = 2;
    editor_open(g_rl_53f380, 200, 10000, 0);
    editor_length();
    engine_map_scene_tick_frame();
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00436229 (161 B) — the noble's second message button: mode 4 (text +0x330) when the local realm's
 * +0x81 byte is DAT_00553fbc, else mode 3 (text +0x268); as 0x4361da otherwise. */
int rl_436229(void)
{
    engine_set_current_screen(26);
    if ((int)PL8(LOCAL, 0x57bf81) == (int32_t)s_slot_00553fbc) {
        g_diplo_action_mode = 4;
        editor_open(g_rl_53f380 + 400, 200, 10000, 0);
    } else {
        g_diplo_action_mode = 3;
        editor_open(g_rl_53f380 + 200, 200, 10000, 0);
    }
    editor_length();
    engine_map_scene_tick_frame();
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_004362ca (84 B) — the noble's alliance button (ask the ally to attack): no county picked
 * (DAT_005530b0), the diplomacy screen 0x1a in mode 5, the map scene ticked with the menu-bar flag DAT_004eb260 at
 * 0 (then 2) and DAT_004e65f8 1; redraw 2. (0x4b1de0 before the tick is empty; 0x4b1dd5 after it, rl_misc.) */
int rl_4362ca(void)
{
    s_slot_005530b0 = 0;
    engine_set_current_screen(26);
    g_diplo_action_mode = 5;
    g_gfx_menu_redraw_a = 0;
    engine_map_scene_tick_frame();
    rl_4b1dd5();
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    engine_set_demand_redraw(2);
    return 0;
}

/* ================================================================ the field prompt */

/* Port of FUN_0043bbe7 (283 B) — the battle prompt's siege attack (DAT_005533d0 set): no siege (DAT_0053e8fc 0) →
 * event 110; the attacking army (DAT_00568218) not the local player's, or 2 or fewer siege turns (DAT_0055401c)
 * → 111; the assault already ordered (DAT_0052af9c) → 157; else MP message 0x45 or the siege assault (0x496b9f). */
int rl_43bbe7(void)
{
    int owner = TERS8((int32_t)s_slot_00568218, 0x52f0b0);
    if (s_slot_005533d0 == 0)
        return owner;
    if (s_slot_0053e8fc == 0)
        return evq_post(0, LOCAL, 110, 0, 0, 0, 0, 0);
    if (LOCAL != owner || (int32_t)s_slot_0055401c <= 2)
        return evq_post(0, LOCAL, 111, 0, 0, 0, 0, 0);
    if (s_slot_0052af9c != 0)
        return evq_post(0, LOCAL, 157, 0, 0, 0, 0, 0);
    if (MP_ON)
        return rl_43eda0(69, 0);
    return bf_496b9f();
}

/* ================================================================ the army split (Divide: screen 0x11) */

static void split_sums(void)   /* both totals over the 8 rows: DAT_00554468 the kept, DAT_00554040 the split-off */
{
    g_rl_554040 = 0;
    g_rl_554468 = 0;
    for (int i = 0; i < 8; i++)
        g_rl_554468 += MUSTER(LOCAL, i, 0);
    for (int i = 0; i < 8; i++)
        g_rl_554040 += MUSTER(LOCAL, i, 4);
}

/* Port of FUN_004378b3 (584 B) — the army panel's Divide: the army is DAT_0052afbc (kept in DAT_00553074); no target
 * county / cell yet (DAT_0053f080, DAT_00568d98). An army whose +0x153 is > 0 (signed; the split cooldown: an
 * in-place split adds 5 to both halves) cannot: event 0x95, back to the map (screen 0, the full repaint,
 * DAT_0053e9ac), the action taken (DAT_0052afa4). Else the local player's muster rows get the army's 7 troop counts
 * (+0x16c, int16) kept and none split off, row 7 its siege engines (+0x196), both totals; the split screen 0x11
 * (DAT_0055321c 18 when +0x197 is set, else 16), the cue s017_01.wav with the voice latch DAT_005533f0; redraw 2. */
int rl_4378b3(void)
{
    g_rl_553074 = g_mv_ti_county;
    g_rl_53f080 = 0;
    g_rl_568d98 = 0;
    int a = g_rl_553074;
    if (TERS8(a, 0x52f203) > 0) {
        evq_post(0, LOCAL, 149, 0, 0, 0, 0, 0);
        engine_set_current_screen(0);
        engine_territory_mark_full_screen_dirty();
        engine_set_demand_redraw(2);
        BELOW_STATUS() = 1;
        g_diplo_action_taken = 1;
        return 0;
    }
    g_rl_55321c = TER8(a, 0x52f247) != 0 ? 18 : 16;
    for (int i = 0; i < 7; i++)
        MUSTER(LOCAL, i, 0) = TER16(a, 0x52f21c + 2 * i);
    for (int i = 0; i < 7; i++)
        MUSTER(LOCAL, i, 4) = 0;
    MUSTER(LOCAL, 7, 0) = TER8(a, 0x52f246);
    MUSTER(LOCAL, 7, 4) = 0;
    split_sums();
    engine_set_current_screen(17);
    g_combat_audio_latch = 1;
    rl_427990((int)(uintptr_t)g_rl_4d57a0, 1, 0);
    engine_set_demand_redraw(2);
    return 0;
}

/* Port of FUN_00437afb (618 B) — the split screen's OK (param0 != 0) / Cancel (0): back to the map (screen 0, the
 * full repaint, DAT_0053e9ac; Cancel: screen 4). OK: both totals; the minimum per half 50 (0 with a target county
 * DAT_0053f080); the split-off may not exceed 5000, or with a target county DAT_004d8a0c[county +0x1c0] less the
 * men (+0x168) of the army at county +0x1bc. An empty half clears param0 (no message); below the minimum → event
 * 0x94; over the cap → 0x11a; else MP message 0x2d or the split (0x437fd7). */
int rl_437afb(void)
{
    engine_set_current_screen(0);
    engine_territory_mark_full_screen_dirty();
    engine_set_demand_redraw(2);
    BELOW_STATUS() = 1;
    if (PARAM0 == 0) {
        engine_set_current_screen(4);
        return 0;
    }
    split_sums();
    int min = g_rl_53f080 != 0 ? 0 : 50;
    int cap;
    if (g_rl_53f080 != 0) {
        int c = g_rl_53f080;
        int g = CO32(c, 0x53fb6c);
        cap = g != 0 ? CAP(CO8(c, 0x53fb70)) - TER32(g, 0x52f218) : CAP(CO8(c, 0x53fb70));
    } else {
        cap = 5000;
    }
    if (g_rl_554468 == 0 || g_rl_554040 == 0)
        PARAM0 = 0;
    if (g_rl_554468 < min || g_rl_554040 < min) {
        if (g_rl_554468 != 0 && g_rl_554040 != 0)
            return evq_post(0, LOCAL, 148, 0, 0, 0, 0, 0);
        return min;
    }
    if (g_rl_554040 > cap)
        return evq_post(0, LOCAL, 282, 0, 0, 0, 0, 0);
    if (MP_ON)
        return rl_43eda0(45, 0);
    return rl_437fd7(LOCAL, g_rl_553074, g_rl_53f080, g_rl_568d98);
}

/* Port of FUN_00437d65 (313 B) — a split row's arrow toward the kept half (param0 = the row): one man back
 * (row 7: all the siege engines); both totals. Returns the last row read (row 7's split-off count). */
int rl_437d65(void)
{
    if (PARAM0 == 7) {
        if (MUSTER(LOCAL, 7, 4) != 0) {
            MUSTER(LOCAL, 7, 0) = MUSTER(LOCAL, 7, 4);
            MUSTER(LOCAL, 7, 4) = 0;
        }
    } else if (MUSTER(LOCAL, PARAM0, 4) != 0) {
        MUSTER(LOCAL, PARAM0, 4)--;
        MUSTER(LOCAL, PARAM0, 0)++;
    }
    split_sums();
    return MUSTER(LOCAL, 7, 4);
}

/* Port of FUN_00437e9e (313 B) — the arrow toward the split-off half: the mirror of 0x437d65. */
int rl_437e9e(void)
{
    if (PARAM0 == 7) {
        if (MUSTER(LOCAL, 7, 0) != 0) {
            MUSTER(LOCAL, 7, 4) = MUSTER(LOCAL, 7, 0);
            MUSTER(LOCAL, 7, 0) = 0;
        }
    } else if (MUSTER(LOCAL, PARAM0, 0) != 0) {
        MUSTER(LOCAL, PARAM0, 0)--;
        MUSTER(LOCAL, PARAM0, 4)++;
    }
    split_sums();
    return MUSTER(LOCAL, 7, 4);
}

/* Port of FUN_00437fd7 (1706 B) — split army `a` of player `p` (rows from p's muster, DAT_0053f6a0): a free cell
 * near the army (0x46733c: the cell cursor DAT_00569570 / 78) and a new army record placed there (0x46e1b0 kind 1:
 * DAT_0052afe0); none → nothing. The new army takes realm +5 (→ +1), the old one's +0x10, the date DAT_00553edc
 * (+0x164), +0x166, +2, a roster slot (0x4a9f72 → +0x14f), the split-off troop counts; the old one keeps the rest;
 * both sums (+0x168) over the 8 rows; the old one's condition recomputed when +0x199 is set (0x4a80db); both
 * +0x14c = 2, the new +0x14b |= 1, +6 = 1. With a target county `c`: the target cell `off` / 8 (col % 64, row / 64:
 * +0x16 / +0x14, +0x17 / +0x15, C division), +0x14e = 0, the path from the new army's cell (0x46f700 / 0x4701ac;
 * reachable: copied, 0x4707be, +0x14c = 2); the local player back to the map. Without: both +0x153 += 5 (signed
 * byte). The siege engines (+0x197 type, +0x196 count, +0x195) go with the split-off half when its row 7 is not
 * empty, and word_568DC0[10 type] names the new army. Returns retail's EAX on every path. */
int rl_437fd7(int p, int a, int c, int off)
{
    int r = engine_map_query_nearest_feature_ring(TERS8(a, 0x52f0ba), TERS8(a, 0x52f0bb));
    if (r == 0)
        return r;
    r = engine_territory_place_entity(1, g_cell_cursor_col, g_cell_cursor_row, p);
    if (r == 0)
        return r;
    TER8(NEWSLOT, 0x52f200) = 1;
    TER8(NEWSLOT, 0x52f0b1) = PL8(p, 0x57bf05);
    TER8(NEWSLOT, 0x52f0c0) = TER8(a, 0x52f0c0);
    TER16(NEWSLOT, 0x52f214) = (int16_t)s_slot_00553edc;
    TER8(NEWSLOT, 0x52f216) = TER8(a, 0x52f216);
    TER8(NEWSLOT, 0x52f0b2) = TER8(a, 0x52f0b2);
    uint8_t slot = (uint8_t)roster_slot(p);
    TER8(NEWSLOT, 0x52f1ff) = slot;
    for (int i = 0; i < 7; i++)
        TER16(NEWSLOT, 0x52f21c + 2 * i) = (int16_t)MUSTER(p, i, 4);
    for (int i = 0; i < 7; i++)
        TER16(a, 0x52f21c + 2 * i) = (int16_t)MUSTER(p, i, 0);
    TER32(NEWSLOT, 0x52f218) = 0;
    for (int i = 0; i <= 7; i++)
        TER32(NEWSLOT, 0x52f218) += MUSTER(p, i, 4);
    TER32(a, 0x52f218) = 0;
    for (int i = 0; i <= 7; i++)
        TER32(a, 0x52f218) += MUSTER(p, i, 0);
    if (TER8(a, 0x52f249) != 0)
        engine_combat_recompute_condition(a);
    TER8(NEWSLOT, 0x52f1fc) = 2;
    TER8(a, 0x52f1fc) = 2;
    TER8(NEWSLOT, 0x52f1fb) |= 1;
    TER8(NEWSLOT, 0x52f0b6) = 1;
    if (c != 0) {
        off /= 8;
        TER8(NEWSLOT, 0x52f0c6) = (uint8_t)(off % 64);
        TER8(NEWSLOT, 0x52f0c4) = TER8(NEWSLOT, 0x52f0c6);
        TER8(NEWSLOT, 0x52f0c7) = (uint8_t)(off / 64);
        TER8(NEWSLOT, 0x52f0c5) = TER8(NEWSLOT, 0x52f0c7);
        TER8(NEWSLOT, 0x52f1fe) = 0;
        engine_path_field_fill(0, TERS8(NEWSLOT, 0x52f0ba), TERS8(NEWSLOT, 0x52f0bb), 0);
        if (engine_path_field_trace(0, TERS8(NEWSLOT, 0x52f0c6), TERS8(NEWSLOT, 0x52f0c7)) != 0) {
            engine_path_field_copy_record(0, NEWSLOT);
            TER8(NEWSLOT, 0x52f1fc) = 2;
        }
        if (LOCAL == p) {
            engine_set_current_screen(0);
            engine_set_demand_redraw(2);
        }
    } else {
        TER8(NEWSLOT, 0x52f203) = (uint8_t)(TERS8(NEWSLOT, 0x52f203) + 5);
        TER8(a, 0x52f203) = (uint8_t)(TERS8(a, 0x52f203) + 5);
    }
    int type = TER8(a, 0x52f247);
    if (type == 0)
        return 84 * a;
    if (MUSTER(p, 7, 4) == 0)
        return p << 7;
    TER8(NEWSLOT, 0x52f246) = TER8(a, 0x52f246);
    TER8(a, 0x52f246) = 0;
    TER8(NEWSLOT, 0x52f247) = TER8(a, 0x52f247);
    TER8(a, 0x52f247) = 0;
    TER8(NEWSLOT, 0x52f245) = TER8(a, 0x52f245);
    TER8(a, 0x52f245) = 0;
    SIEGE_SLOT(type) = (int16_t)NEWSLOT;
    return NEWSLOT;
}
