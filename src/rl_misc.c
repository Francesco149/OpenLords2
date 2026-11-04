/* rl_misc.c — a retail-layout port, partition 'misc' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does. */
#include "rl.h"
#include "rl.h"
#include "rl_misc_globals.h"
#include "rl_io_globals.h"
#include "rl_render_globals.h"
#include "rl_io_plat.h"
#include "engine.h"
#include "engine_combat.h"
#include "engine_territory.h"
#include "engine_army_ai.h"

/* Port of FUN_0045242b (11 B) — an empty function (retail calls it every frame). */
int rl_45242b(void) { return 0; }
/* Port of FUN_004b1897 (11 B) — an empty function. */
int rl_4b1897(void) { return 0; }
/* Port of FUN_004b1dd5 (11 B) — an empty function. */
int rl_4b1dd5(void) { return 0; }
/* Port of FUN_004b12fb (21 B) — returns 1. */
int rl_4b12fb(void) { return 1; }

/* Port of FUN_004b18f8 (38 B) — post a synthetic WM_MOUSEMOVE (0x200) at (x, y) to the main window: the 16-bit x / y
 * are the message's wParam / lParam as retail passes them (sign-extended shorts). */
int rl_4b18f8(int x, int y)
{
    HWND w = (HWND)(uintptr_t)AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u);
    return (int)rl_plat_PostMessageA(w, 0x200u, (WPARAM)(int16_t)x, (LPARAM)(int16_t)y);
}

/* Port of FUN_004b209a (83 B) — a busy-wait: n × 10000 additions into an uninitialized local (no state); returns n. */
int rl_4b209a(int n)
{
    int i;
    for (i = 0; i < n; i++) { }
    return n;
}

/* Port of FUN_004b25fc (58 B) — free the current asset blob DAT_004eabec (the debug-CRT free 0x4c0580) and clear it and
 * its size DAT_004eabcc. Returns the free's EAX (garbage when nothing is freed: 0 here). */
int rl_4b25fc(void)
{
    int r = 0;
    if (g_bf_4eabec != 0) {
        r = rl_4c0580(g_bf_4eabec);
        AT(int32_t, g_rl_4eabcc, 0x4eabccu, 0x4eabccu) = 0;
        g_bf_4eabec = 0;
    }
    return r;
}

/* Port of FUN_004b26f8 (56 B) — on regaining the foreground: log "OK: unpaused" (0x4afab9) and clear the paused
 * flag DAT_004e65d0. Returns GetForegroundWindow's HWND, else the log's EAX. */
int rl_4b26f8(void)
{
    HWND f = rl_plat_GetForegroundWindow();
    int r = (int)(uintptr_t)f;
    if (f == (HWND)(uintptr_t)AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u)) {
        r = rl_4afab9((int)(uintptr_t)g_rl_4df658, 0, 0);
        g_rl_4e65d0 = 0;
    }
    return r;
}

/* Port of FUN_004b1cf3 (226 B) — set the mouse cursor by kind (2, 4, 5, 6, 12, 13, 14 → their loaded HCURSORs; else
 * and 0 → the arrow DAT_004ea834); the current one is kept in hCursor (DAT_004ea514). Returns SetCursor's. */
int rl_4b1cf3(int kind)
{
    int32_t c;
    switch (kind) {
    case 2:  c = g_rl_4eaba8; break;
    case 4:  c = g_rl_4e659c; break;
    case 5:  c = g_rl_4ea194; break;
    case 6:  c = g_rl_4ea518; break;
    case 12: c = g_rl_4eac58; break;
    case 13: c = g_rl_4eb258; break;
    case 14: c = g_rl_4eabe4; break;
    default: c = g_rl_4ea834; break;
    }
    if (kind == 0)
        c = g_rl_4ea834;
    AT(int32_t, g_rl_4ea514, 0x4ea514u, 0x4ea514u) = c;
    return (int)(uintptr_t)rl_plat_SetCursor((HCURSOR)(uintptr_t)(uint32_t)c);
}

/* Port of FUN_004b3571 (80 B) — the intro movie: with `skip` == 1, or when intro.smk does not play (0x42d91b at
 * (40, 80), returning to the current screen), mark it done (DAT_005c9274 = 1). */
int rl_4b3571(int skip)
{
    if (skip == 1)
        g_rl_5c9274 = 1;
    else if (rl_42d91b((int)(uintptr_t)g_rl_4e24c0, 40, 80, 0, engine_current_screen()) == 0)
        g_rl_5c9274 = 1;
    return 0;
}

/* Port of FUN_004b28cd (85 B) — release the DirectPlay object DAT_004df288 (its vtable Release, slot 2) and, when the
 * surfaces are up (DAT_004e65ec), the surfaces (0x42eaa6). */
int rl_4b28cd(int a)
{
    int r = 0;
    if (g_rl_4df288 != 0) {
        uint32_t obj = (uint32_t)g_rl_4df288;
        uint32_t fn = ((uint32_t *)(uintptr_t)*(uint32_t *)(uintptr_t)obj)[2];
        r = ((int (__stdcall *)(uint32_t))(uintptr_t)fn)(obj);
        g_rl_4df288 = 0;
    }
    if (g_rl_4e65ec != 0) {
        r = (int)engine_retail_call(0x42eaa6u, 1, (uint32_t)a);   /* TODO(rl-link): gfx_release_surfaces */
        g_rl_4e65ec = 0;
    }
    return r;
}

/* Port of FUN_004bbe51 (86 B) — MP: 6 s after the last message (DAT_0055ce74) force a redraw and raise the timeout
 * flag DAT_0057c940. */
int rl_4bbe51(void)
{
    if (g_sim_mp_active != 0 && (int)(rl_plat_timeGetTime() - (uint32_t)g_rl_55ce74) >= 6000) {
        engine_set_demand_redraw(1);
        g_rl_57c940 = 1;
    }
    return 0;
}

/* Port of FUN_004bbea7 (110 B) — MP: is the battle prompt timed out (20 s with a pending fight DAT_00554408 > 0, else
 * 30 s since DAT_0057a0d0)? */
int rl_4bbea7(void)
{
    int limit = g_combat_ctrl_408 <= 0 ? 30000 : 20000;
    return g_sim_mp_active != 0 && (int)(rl_plat_timeGetTime() - g_combat_battle_clock) > limit;
}

/* Port of FUN_004bbf15 (116 B) — MP: is the battle animation done (a multi-wave siege, or 5 s since DAT_005689a0)? */
int rl_4bbf15(void)
{
    if (g_sim_mp_active == 0)
        return 0;
    if (g_combat_ctrl_52afac > 1 && s_slot_0053e8fc != 0)
        return 1;
    return (int)(rl_plat_timeGetTime() - g_combat_anim_clock) > 5000;
}

/* Port of FUN_00497879 (52 B) — leave the scenario screen: on scenario 10 count a win (DAT_0053f258), take the chosen
 * scenario DAT_0053f034 into DAT_00553e78, apply its config (0x499e5d — game_init's port is not linked:
 * TODO(rl-link)), screen 28. */
int rl_497879(void)
{
    if (g_rl_53f0c4 == 10)
        s_slot_0053f258++;
    g_rl_553e78 = (int32_t)s_slot_0053f034;
    int r = rl_499e5d();
    engine_set_current_screen(28);
    return r;
}

/* Port of FUN_004b0cb4 (314 B) — start a palette fade to the 256-entry RGB table `pal` (6-bit components; mode 1:
 * ×4/4, else ×4) from DAT_004e65c8 = the screen to return to; the fade's step count DAT_004e65e0 = 256; target table
 * DAT_004ea530; the palette's darken / restore 0x4b14ff (partition ui2), the full-screen redraw, screen 35. */
int rl_4b0cb4(int ret_screen, int mode, int pal)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)pal;
    g_rl_4e65c8 = ret_screen;
    g_rl_4e65e0 = 256;
    for (int i = 0; i < 256; i++) {
        if (mode == 1) {
            g_rl_4ea530[3 * i]     = (uint8_t)(4 * p[3 * i] / 4);
            g_rl_4ea530[3 * i + 1] = (uint8_t)(4 * p[3 * i + 1] / 4);
            g_rl_4ea530[3 * i + 2] = (uint8_t)(4 * p[3 * i + 2] / 4);
        } else {
            g_rl_4ea530[3 * i]     = (uint8_t)(4 * p[3 * i]);
            g_rl_4ea530[3 * i + 1] = (uint8_t)(4 * p[3 * i + 1]);
            g_rl_4ea530[3 * i + 2] = (uint8_t)(4 * p[3 * i + 2]);
        }
    }
    rl_4b14ff();   /* partition ui2 */
    engine_territory_mark_full_screen_dirty();
    engine_set_current_screen(35);
    return 0;
}

/* one component's step of the fade: towards the target by 12, snapping when within 12 */
static uint8_t fade_step(uint8_t cur, uint8_t tgt, int *moved)
{
    if (cur > tgt) {
        cur = (unsigned)tgt + 12 >= cur ? tgt : (uint8_t)(cur - 12);
        *moved = 1;
    } else if (cur < tgt) {
        cur = tgt - 12 <= cur ? tgt : (uint8_t)(cur + 12);
        *moved = 1;
    }
    return cur;
}

/* Port of FUN_004b0e03 (875 B) — the palette fade's tick (screen 35): every 20 ms (a backwards clock counts as 10 s)
 * entries 10..245 of the live palette DAT_004ea1b0 move 12 towards the target DAT_004ea530; when nothing moved or the
 * step count DAT_004e65e0 ran out, the whole target is copied, the screen goes back (DAT_004e65c8) with a full redraw
 * (DAT_0057d340 = 2); then the palette is set (0x42f58c(0, 255)). */
int rl_4b0e03(void)
{
    if (engine_current_screen() != 35)
        return engine_current_screen();
    uint32_t t = rl_plat_timeGetTime();
    int dt = (int)(t - (uint32_t)g_rl_4e59f0);
    if (dt < 0)
        dt = 10000;
    if (dt < 20)
        return (int)(t - (uint32_t)g_rl_4e59f0);
    g_rl_4e59f0 = (int32_t)t;
    if (g_rl_4e65e0 != 0)
        g_rl_4e65e0--;
    int moved = 0;
    uint8_t *live = g_rl_4ea1b0;
    for (int i = 10; i < 246; i++)
        for (int k = 0; k < 3; k++)
            live[3 * i + k] = fade_step(live[3 * i + k], g_rl_4ea530[3 * i + k], &moved);
    if (moved == 0)
        g_rl_4e65e0 = 0;
    if (g_rl_4e65e0 <= 0) {
        for (int j = 0; j < 256; j++)
            for (int k = 0; k < 3; k++)
                live[3 * j + k] = g_rl_4ea530[3 * j + k];
        engine_set_current_screen((uint8_t)g_rl_4e65c8);
        engine_set_demand_redraw(2);
    }
    return rl_42f58c(0, 255);
}

/* Port of FUN_0049a3e6 (109 B) — screen 36 (never set by retail): the end-of-year save — the sheet reload (0x4984dc),
 * the strategic render (0x40f5fd), 0x4b14ff, the present 0x452160(1), the ranking cookie, the redraw, a fade to the
 * palette at 0x5691f0, the save rotation (0x49a453) — all through retail (TODO(rl-link)). */
int rl_49a3e6(void)
{
    int r = engine_current_screen();
    if (r == 36) {
        g_rl_57c968 = 1;
        rl_4984dc();
        engine_retail_call(0x40f5fdu, 1, 3u);
        rl_4b14ff();
        engine_retail_call(0x452160u, 1, 1u);
        engine_army_ai_set_ranking_dirty(1);
        engine_territory_mark_full_screen_dirty();
        rl_4b0cb4(0, 0, (int)(uintptr_t)g_rl_5691f0);
        return rl_49a453();
    }
    return r;
}
