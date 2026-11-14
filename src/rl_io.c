/* rl_io.c — a retail-layout port, partition 'io' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * The platform (Win32 / winmm / smackw32 imports, retail's stateful CRT) goes through rl_io_plat.h; findings and
 * the shim's design: docs/findings/rl-io.md. */
#include "rl.h"
#include "rl_io_globals.h"
#include "rl_io_plat.h"
#include "engine.h"
#include "engine_roster.h"
#include "engine_territory.h"
#include "engine_dirty_rect.h"
#include "engine_combat.h"
#include "gfx_globals.h"
#include "gfx_text_label.h"
#include "strutil.h"

#include <string.h>

/* ---------------------------------------------------------------- seams (callees outside the partition) */
/* engine_npc_schedule.c / engine_map_init.c ports are not linked in rhost — TODO(rl-link) */
static int npc_schedule_init(void) { return rl_49caaa(); }   /* engine_npc_schedule_init (rl turn) */
static void map_init_set_camera(int mode) { rl_451fcc(mode); }   /* 0x451fcc (rl_ctl) */

/* ---------------------------------------------------------------- the game's init / reset stores */

/* Port of FUN_0041b013 (31 B) — zero DAT_00522f64 and DAT_00554024. */
int rl_41b013(void)
{
    g_rl_522f64 = 0;
    g_rl_554024 = 0;
    return 0;
}

/* Port of FUN_00426529 (11 B) — an empty function (retail's EAX passes through). */
int rl_426529(void) { return 0; }

/* Port of FUN_0045280b (11 B) — an empty function. */
int rl_45280b(void) { return 0; }

/* Port of FUN_004b0aaa (11 B) — an empty function (one argument, unused). */
int rl_4b0aaa(int a0) { (void)a0; return 0; }

/* Port of FUN_004ae2ab (18 B) — empty: zeroes its own argument slot (the caller's dead push). */
int rl_4ae2ab(int a0) { (void)a0; return 0; }

/* Port of FUN_0046ea9a (54 B) — zero the 44-byte record at `p` (a player aux record). Returns the last index (43). */
int rl_46ea9a(int p)
{
    unsigned i, r = 0;
    for (i = 0; i < 0x2c; i++) {
        r = i;
        ((uint8_t *)(uintptr_t)(uint32_t)p)[i] = 0;
    }
    return (int)r;
}

/* Port of FUN_0046f21f (72 B) — 0x46ea61 on each of the 7 realm records (DAT_0057bf00 + 352 i). Returns the last
 * call's EAX. */
int rl_46f21f(void)
{
    int r = 0;
    for (int i = 0; i <= 6; i++)
        r = rl_46ea61((int)(uintptr_t)(g_player_strategic_352 + 352 * i));
    return r;
}

/* Port of FUN_0046f267 (71 B) — zero the 6 player aux records (DAT_00553d50, 44 B each: 0x46ea9a). */
int rl_46f267(void)
{
    int r = 0;
    for (int i = 0; i < 6; i++)
        r = rl_46ea9a((int)(uintptr_t)(g_player_aux_44 + 44 * i));
    return r;
}

/* Port of FUN_004983b7 (161 B) — the economy tuning constants (prices, production, yields: the AI model's inputs). */
int rl_4983b7(void)
{
    g_rl_553f60 = 5;
    g_rl_57cb30 = 6;
    g_rl_553004 = 25;
    g_rl_567594 = 10;
    g_rl_57c8e0 = 12;
    g_rl_552ffc = 10;
    g_rl_5533bc = 5;
    g_rl_553538 = 10;
    g_rl_553218 = 3;
    g_rl_57d34c = 2;
    g_rl_52afc4 = 500;
    g_rl_568950 = 200;
    g_bf_57c8b4 = 5;
    g_bf_552ff0 = 260;
    g_bf_554090 = 15;
    return 0;
}

/* Port of FUN_004990f9 (101 B) — forget the nine sheet pointers 0x49915e frees (DAT_005530c8 …). */
int rl_4990f9(void)
{
    g_bf_5530c8 = 0;
    g_rl_553224 = 0;
    g_rl_56d8bc = 0;
    g_bf_568208 = 0;
    g_rl_53e914 = 0;
    g_rl_57d344 = 0;
    g_rl_55409c = 0;
    g_rl_56898c = 0;
    g_rl_55ce5c = 0;
    return 0;
}

/* Port of FUN_00499261 (261 B) — forget the 25 sheet pointers 0x499366 frees (DAT_005530c8 and DAT_00568208 again). */
int rl_499261(void)
{
    g_bf_5530c8 = 0;
    g_bf_568208 = 0;
    g_bf_57c928 = 0;
    g_bf_53f044 = 0;
    g_bf_56d680 = 0;
    g_bf_566518 = 0;
    g_bf_55ce58 = 0;
    g_bf_553250 = 0;
    g_bf_553f6c = 0;
    g_bf_522f78 = 0;
    g_bf_56d5a0 = 0;
    g_bf_53ef50 = 0;
    g_bf_53f0d0 = 0;
    g_bf_56d640 = 0;
    g_bf_56d684 = 0;
    g_bf_57c8d4 = 0;
    g_bf_52afb0 = 0;
    g_bf_53f650 = 0;
    g_bf_553f2c = 0;
    g_bf_55cd30 = 0;
    g_bf_56d890 = 0;
    g_bf_57c8bc = 0;
    g_bf_57cb28 = 0;
    g_bf_53f0bc = 0;
    g_bf_553258 = 0;
    return 0;
}

/* Port of FUN_00499619 (51 B) — forget the four work buffers 0x49964c frees (DAT_0057a0c0 …). */
int rl_499619(void)
{
    g_rl_57a0c0 = 0;
    g_rl_56d5b8 = 0;
    g_rl_53e918 = 0;
    g_rl_5533c4 = 0;
    return 0;
}

/* Port of FUN_00433cc1 (35 B) — the NPC schedule's start (DAT_0053f268 = DAT_0053f288 + 2 - DAT_00553f98), then
 * 0x49caaa. Returns its EAX. */
int rl_433cc1(void)
{
    s_slot_0053f268 = g_rl_53f288 + 2 - s_slot_00553f98;
    return npc_schedule_init();
}

/* Port of FUN_00451f70 (26 B) — mark the whole screen dirty (0x45240a) and 0x452160(1). Returns its EAX. */
int rl_451f70(void)
{
    engine_territory_mark_full_screen_dirty();
    return rl_452160(1);
}

/* Port of FUN_004b11ce (34 B) — 0x4b0ab5(DAT_004e6600), 0x4b1867, then the full-screen redraw 0x451f70. */
int rl_4b11ce(void)
{
    rl_4b0ab5((int)(uintptr_t)g_rl_4e6600);
    rl_4b1867();
    return rl_451f70();
}

/* Port of FUN_00497500 (211 B) — boot-time reset: forget every sheet / buffer pointer, the sheet set 0x4984dc (0 =
 * failure: return 0), the tuning constants, the camera, and the per-game flags. Returns 1. */
int rl_497500(void)
{
    rl_4990f9();
    rl_499261();
    rl_499619();
    if (rl_4984dc() == 0)   /* the season's sheets (rl turn) */
        return 0;
    rl_4983b7();
    s_slot_005653f8 = 0;
    s_slot_0056d6b0 = 0;
    rl_41b013();
    map_init_set_camera(0);
    rl_45280b();
    s_slot_0053f084 = 0;
    s_slot_00553d28 = 0;
    s_slot_00553540 = 0;
    g_rl_55ce90 = 1;
    g_rl_57d354 = 1;
    g_rl_553e60 = 0;
    g_rl_5533d4 = 0;
    g_rl_5689a4 = 0;
    g_rl_56d8b4 = 0;
    g_rl_53f0c0 = 0;
    g_rl_568998 = 0;
    return 1;
}

/* Port of FUN_004975d3 (170 B) — the second boot-time reset: frame-driver state, 0x499859 (0 = failure: return 0),
 * the camera, the per-game flags. Returns 1. */
int rl_4975d3(void)
{
    s_slot_0057c8fc = 0;
    s_slot_0057cb18 = 0;
    g_gfx_last_error = 0;   /* DAT_005aeb68 (gfx_globals.h) */
    g_rl_58fd74 = 1;
    g_rl_53f684 = 0;
    g_rl_553ed4 = 0;
    g_rl_55402c = 0;
    if (rl_499859() == 0)   /* the system files (rl turn) */
        return 0;
    map_init_set_camera(0);
    rl_45280b();
    g_rl_53f0c4 = 0;
    s_slot_0053f084 = 0;
    s_slot_00553d28 = 0;
    s_slot_00553540 = 0;
    g_rl_57d354 = 1;
    return 1;
}

/* Port of FUN_0049c995 (157 B) — the players' reset: scalar defaults, the realm records' 0x46ea61 pass (0x46f21f),
 * the aux records cleared (0x46f267), the visibility bits (0x49ca32), and each player 1..5's job slot (+0x0a = p)
 * and +0x118 = DAT_0053f278. Returns DAT_0053f278 (the last EAX). */
int rl_49c995(void)
{
    s_slot_0053f658 = 0;
    s_slot_0057c944 = 1;
    s_slot_0053f06c = 7;
    s_slot_00569584 = 1;
    s_slot_0053f678 = 4;
    rl_46f21f();
    rl_46f267();
    player_seed_visibility_bits();
    int r = 0;
    for (int i = 1; i < 6; i++) {
        g_player_strategic_352[352 * i + 0x0a] = (uint8_t)i;
        r = g_rl_53f278;
        AT(int32_t, g_player_strategic_352, 0x57bf00u, 0x57c018u + 352 * i) = r;
    }
    return r;
}

/* Port of FUN_004978ad (129 B) — a new single-player game: DAT_00553f98 = 1, the players' reset (0x49c995), the
 * human is player 1 (DAT_0057c8cc) seated with the name DAT_0053f1e0 (0x49bae9), job slot 1 mirrored into the aux
 * record, then 0x49caaa. Returns its EAX. */
int rl_4978ad(void)
{
    s_slot_00553f98 = 1;
    rl_49c995();
    s_slot_0057c8cc = 1;
    player_seat_assign(0, (int)s_slot_0057c8cc, g_bf_53f1e0);   /* DAT_0053f1e0: the player name */
    g_player_strategic_352[352 * s_slot_0057c8cc + 0x0a] = 1;
    g_player_aux_44[44 * s_slot_0057c8cc + 0x25] = g_player_strategic_352[352 * s_slot_0057c8cc + 0x0a];
    return npc_schedule_init();
}

/* Port of FUN_004ae2bd (83 B) — after reading lords2.inf: the year DAT_0053f204 == 2028 counts DAT_0053f5ec up,
 * else 0x4ae310 and both counters cleared; DAT_0053f238 = 0, DAT_0053f254 = 1. */
int rl_4ae2bd(void)
{
    int r = 0;
    if (g_rl_53f204 == 2028) {
        g_rl_53f5ec++;
    } else {
        r = rl_4ae310();
        g_rl_53f5ec = 0;
        s_slot_0053f258 = 0;
    }
    g_bf_53f238 = 0;
    g_rl_53f254 = 1;
    return r;
}

/* Port of FUN_0042d91b (83 B) — play a movie (0x42da18): started = 1, DAT_005169b0 = the screen to return to and the
 * screen is 0x22 (the movie screen); not started = 0 and the screen is `next` at once. */
int rl_42d91b(int name, int x, int y, int mode, int next)
{
    if (rl_42da18(name, x, y, mode) != 0) {
        g_rl_5169b0 = next;
        engine_set_current_screen(34);
        return 1;
    }
    engine_set_current_screen((uint8_t)next);
    return 0;
}

/* ---------------------------------------------------------------- the debug CRT's free + the sheet / buffer frees */

/* a string of retail's .rdata / .data inside the generated block `sym` (at retail VA `symva`) */
#define RS(sym, symva, va) ((const char *)&AT(uint8_t, sym, symva, va))
static void int3(void) { __asm__ volatile("int3"); }

/* Port of FUN_004c05a0 (1057 B) — the debug CRT's _free_dbg (dbgheap.c; Lords2.exe links the debug CRT): the
 * _CRTDBG_CHECK_ALWAYS_DF check, the client alloc hook DAT_004e4610 (_HOOK_FREE), the header checks (block type,
 * the no-man's-land bytes 0xfd before / after the data, an _IGNORE_BLOCK's line / request), then _lCurAlloc
 * DAT_004e5b2c -= size; _CRTDBG_DELAY_FREE_MEM_DF keeps the block as a dead _FREE_BLOCK, else it is unlinked from
 * the block list (_pFirstBlock DAT_004e5b28 / _pLastBlock DAT_004e5b20), filled with the dead-land byte and handed
 * to _free_base. Every failed check reports through _CrtDbgReport (int 3 when it asks for a break). `p` = the
 * user pointer (the header is 0x20 B below), `use` = the block type the caller frees (_NORMAL_BLOCK = 1). */
int rl_4c05a0(int p, int use)
{
    const char *file = RS(g_rl_4d0204, 0x4d0204u, 0x4d0204u);   /* "dbgheap.c" */
#define REPORT(type, f, line, fmt, a, b, c) (rl_plat_CrtDbgReport(type, f, line, NULL, fmt, a, b, c) == 1)
    if ((g_rl_4e4580 & 4) && rl_plat_CrtCheckMemory() == 0 &&
        REPORT(2, file, 0x3e1, RS(g_rl_4d0204, 0x4d0204u, 0x4d0210u), 0, 0, 0))
        int3();
    if (p == 0)
        return 0;
    if (rl_plat_alloc_hook((uint32_t)g_rl_4e4610[0], 3, (void *)(uintptr_t)(uint32_t)p, 0, use, 0, NULL, 0) == 0) {
        if (REPORT(0, NULL, 0, RS(g_rl_4d01a4, 0x4d01a4u, 0x4d01a4u),
                   (uint32_t)(uintptr_t)RS(g_rl_4d03c4, 0x4d03c4u, 0x4d04d4u), 0, 0))   /* "%s", "Client hook free failure.\n" */
            int3();
        return 0;
    }
    if (rl_plat_CrtIsValidHeapPointer((const void *)(uintptr_t)(uint32_t)p) == 0 &&
        REPORT(2, file, 0x3f3, RS(g_rl_4d0314, 0x4d0314u, 0x4d0314u), 0, 0, 0))
        int3();
    volatile uint32_t *b = (volatile uint32_t *)(uintptr_t)((uint32_t)p - 0x20u);   /* _CrtMemBlockHeader */
    if ((b[5] & 0xffff) != 4 && b[5] != 1 && (b[5] & 0xffff) != 2 && b[5] != 3 &&
        REPORT(2, file, 0x3f9, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d04acu), 0, 0, 0))
        int3();
    if (!(g_rl_4e4580 & 4)) {
        if (rl_plat_CheckBytes((const void *)(b + 7), g_rl_4e458c, 4) == 0 &&
            REPORT(1, NULL, 0, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d0480u),   /* "DAMAGE: before %hs block (#%d) at 0x%08X.\n" */
                   (uint32_t)g_rl_4e4598[b[5] & 0xffff], b[6], (uint32_t)(uintptr_t)(b + 8)))
            int3();
        if (rl_plat_CheckBytes((const void *)(uintptr_t)(b[4] + (uint32_t)(uintptr_t)(b + 8)), g_rl_4e458c, 4) == 0 &&
            REPORT(1, NULL, 0, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d0454u),   /* "DAMAGE: after …" */
                   (uint32_t)g_rl_4e4598[b[5] & 0xffff], b[6], (uint32_t)(uintptr_t)(b + 8)))
            int3();
    }
    if (b[5] == 3) {   /* _IGNORE_BLOCK */
        if ((b[3] != 0xfedcbabcu || b[6] != 0) && REPORT(2, file, 0x40e, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d0414u), 0, 0, 0))
            int3();
        memset((void *)b, g_rl_4e4590, b[4] + 0x24);
        rl_plat_free_base((void *)b);
        return 0;
    }
    if (b[5] == 2 && use == 1)   /* a _CRT_BLOCK freed as _NORMAL_BLOCK */
        use = 2;
    if (b[5] != (uint32_t)use && REPORT(2, file, 0x41b, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d03f4u), 0, 0, 0))
        int3();
    g_rl_4e5b2c -= (int32_t)b[4];
    if (g_rl_4e4580 & 2) {   /* _CRTDBG_DELAY_FREE_MEM_DF: keep it as a dead _FREE_BLOCK */
        b[5] = 0;
        memset((void *)(b + 8), g_rl_4e4590, b[4]);
        return 0;
    }
    if (b[0] != 0) {
        ((volatile uint32_t *)(uintptr_t)b[0])[1] = b[1];
    } else {
        if ((uint32_t)(uintptr_t)b != (uint32_t)g_rl_4e5b20 && REPORT(2, file, 0x42a, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d03dcu), 0, 0, 0))
            int3();
        g_rl_4e5b20 = (int32_t)b[1];
    }
    if (b[1] != 0) {
        *(volatile uint32_t *)(uintptr_t)b[1] = b[0];
    } else {
        if ((uint32_t)(uintptr_t)b != (uint32_t)g_rl_4e5b28 && REPORT(2, file, 0x434, RS(g_rl_4d03c4, 0x4d03c4u, 0x4d03c4u), 0, 0, 0))
            int3();
        g_rl_4e5b28 = (int32_t)b[0];
    }
    memset((void *)b, g_rl_4e4590, b[4] + 0x24);
    rl_plat_free_base((void *)b);
    return 0;
#undef REPORT
}

/* Port of FUN_004c0580 (25 B) — the debug CRT's free(): _free_dbg(p, _NORMAL_BLOCK). */
int rl_4c0580(int p) { return rl_4c05a0(p, 1); }

/* free a sheet pointer if set (the frees below re-read each global) */
#define FREE_IF(g) do { if ((g) != 0) rl_4c0580((int)(g)); } while (0)

/* Port of FUN_0049915e (259 B) — free the nine strategic sheets (DAT_005530c8 …), then forget them (0x4990f9). */
int rl_49915e(void)
{
    FREE_IF(g_bf_5530c8);
    FREE_IF(g_rl_553224);
    FREE_IF(g_rl_56d8bc);
    FREE_IF(g_bf_568208);
    FREE_IF(g_rl_53e914);
    FREE_IF(g_rl_57d344);
    FREE_IF(g_rl_55409c);
    FREE_IF(g_rl_56898c);
    FREE_IF(g_rl_55ce5c);
    return rl_4990f9();
}

/* Port of FUN_00499366 (691 B) — free the 25 battle / screen sheets (DAT_005530c8 …), then forget them (0x499261). */
int rl_499366(void)
{
    FREE_IF(g_bf_5530c8);
    FREE_IF(g_bf_568208);
    FREE_IF(g_bf_57c928);
    FREE_IF(g_bf_53f044);
    FREE_IF(g_bf_56d680);
    FREE_IF(g_bf_566518);
    FREE_IF(g_bf_55ce58);
    FREE_IF(g_bf_553250);
    FREE_IF(g_bf_553f6c);
    FREE_IF(g_bf_522f78);
    FREE_IF(g_bf_56d5a0);
    FREE_IF(g_bf_53ef50);
    FREE_IF(g_bf_53f0d0);
    FREE_IF(g_bf_56d640);
    FREE_IF(g_bf_56d684);
    FREE_IF(g_bf_57c8d4);
    FREE_IF(g_bf_52afb0);
    FREE_IF(g_bf_53f650);
    FREE_IF(g_bf_553f2c);
    FREE_IF(g_bf_55cd30);
    FREE_IF(g_bf_56d890);
    FREE_IF(g_bf_57c8bc);
    FREE_IF(g_bf_57cb28);
    FREE_IF(g_bf_53f0bc);
    FREE_IF(g_bf_553258);
    return rl_499261();
}

/* Port of FUN_0049964c (124 B) — free the four work buffers (DAT_0057a0c0 …), then forget them (0x499619). */
int rl_49964c(void)
{
    FREE_IF(g_rl_57a0c0);
    FREE_IF(g_rl_56d5b8);
    FREE_IF(g_rl_53e918);
    FREE_IF(g_rl_5533c4);
    return rl_499619();
}
#undef FREE_IF

/* Port of FUN_004996c8 (247 B) — the armory / county work buffers: sheet state 4 (DAT_0053f050), every sheet freed,
 * then four buffers malloc'd (0x4b320, 0x4b320, 0x3d090, 0x249f0 B); a failure counts how many succeeded,
 * 0x4b2636, logs "ERR:Error loading temp, cannot alloc …" with that count and exits (100). Returns 1. */
int rl_4996c8(void)
{
    int n = 0;
    g_bf_53f050 = 4;
    rl_49915e();
    rl_499366();
    rl_49964c();
    g_rl_57a0c0 = (int32_t)(uintptr_t)rl_plat_malloc(0x4b320u);
    if (g_rl_57a0c0 != 0) {
        n++;
        g_rl_56d5b8 = (int32_t)(uintptr_t)rl_plat_malloc(0x4b320u);
        if (g_rl_56d5b8 != 0) {
            n++;
            g_rl_53e918 = (int32_t)(uintptr_t)rl_plat_malloc(0x3d090u);
            if (g_rl_53e918 != 0) {
                n++;
                g_rl_5533c4 = (int32_t)(uintptr_t)rl_plat_malloc(0x249f0u);
                if (g_rl_5533c4 != 0)
                    return 1;
            }
        }
    }
    engine_retail_call(0x4b2636u, 0);   /* TODO(rl-extern) */
    rl_4afab9((int)(uintptr_t)RS(g_rl_4dbf0c, 0x4dbf0cu, 0x4dbf0cu), 0, n + 1);
    rl_plat_exit(100);
    return 0;
}

/* ---------------------------------------------------------------- the asset directory remap + file probes */

/* retail's strcmp (the MSVC CRT's, 0x4c2170): -1 / 0 / +1 on the first differing byte, unsigned */
static int rl_strcmp(const char *a, const char *b)
{
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    for (;; p++, q++) {
        if (*p != *q) return *p < *q ? -1 : 1;
        if (*p == 0) return 0;
    }
}
/* the extension scratch DAT_004eafa4 (4 B) and its writer 0x4af54a (strutil.c's strutil_ext_save keeps a
 * module-static scratch, not retail's): rl_4af54a (rl_host) */
static char *ext_scratch(void) { return (char *)g_rl_4eafa4; }
static void ext_save(int path) { rl_4af54a(path); }
/* back to the launch directory DAT_0058fe30 (0x4af502 = rl_4af502; io_path.c keeps its own copy) */
static int chdir_base(void) { return rl_4af502(); }
#define STR(va) RS(g_rl_4df3e0, 0x4df3e0u, va)   /* the remap's string table DAT_004df3e0.. */

/* Port of FUN_004af116 (508 B) — the CD tree: a .PL8 / .256 / .WAV / .SMK file (its extension, upper-cased into
 * DAT_004eafa4) chdirs to "<cd>:.\pl8", "<cd>:.\pl8", "<cd>:.\wav" or "<cd>:.\smk_high" / "smk_low" (DAT_0053f5f4 == 1:
 * high) — 30 bytes copied from the string table into the 40-byte DAT_005cd490 (the copy runs past each literal into
 * the next, quirk), the drive letter DAT_0053f200 put in byte 0. Returns _chdir's result; another extension: no
 * chdir, the last strcmp's (±1). */
int rl_4af116(int name)
{
    ext_save(name);
    char *ext = ext_scratch();
    strutil_to_upper_inplace((unsigned char *)ext);
    int r = rl_strcmp(STR(0x4df3e0u), ext);   /* "PL8" */
    if (r != 0) {
        r = rl_strcmp(STR(0x4df3e4u), ext);   /* "256" */
        if (r != 0) {
            r = rl_strcmp(STR(0x4df3e8u), ext);   /* "WAV" */
            if (r != 0) {
                r = rl_strcmp(STR(0x4df3ecu), ext);   /* "SMK" */
                if (r != 0)
                    return r;
            }
        }
    }
    uint8_t *path = &AT(uint8_t, g_rl_5cd420, 0x5cd420u, 0x5cd490u);   /* Path */
    for (int i = 0; i < 40; i++)
        path[i] = 0;
    if (rl_strcmp(STR(0x4df3f0u), ext) == 0)
        strutil_bounded_copy(path, STR(0x4df3f4u), 30);   /* "c:.\pl8" */
    else if (rl_strcmp(STR(0x4df404u), ext) == 0)
        strutil_bounded_copy(path, STR(0x4df408u), 30);   /* "c:.\pl8" */
    else if (rl_strcmp(STR(0x4df418u), ext) == 0)
        strutil_bounded_copy(path, STR(0x4df41cu), 30);   /* "c:.\wav" */
    else if (rl_strcmp(STR(0x4df42cu), ext) == 0)
        strutil_bounded_copy(path, g_rl_53f5f4 == 1 ? STR(0x4df430u) : STR(0x4df444u), 30);   /* smk_high / smk_low */
    path[0] = g_rl_53f200;
    return rl_plat_chdir((const char *)path);
}

/* Port of FUN_004af312 (496 B) — the install tree: the same extension test, then DAT_005cd420 = the first 40 bytes
 * of the launch directory DAT_0058fe30 + "\pl8" / "\pl8" / "\wav" / "\smk_low" / "\smk_high" — the smk choice is
 * INVERTED vs 0x4af116 (DAT_0053f5f4 == 1: low, quirk) — and chdir there. Same returns as 0x4af116. */
int rl_4af312(int name)
{
    ext_save(name);
    char *ext = ext_scratch();
    strutil_to_upper_inplace((unsigned char *)ext);
    int r = rl_strcmp(STR(0x4df458u), ext);   /* "PL8" */
    if (r != 0) {
        r = rl_strcmp(STR(0x4df45cu), ext);   /* "256" */
        if (r != 0) {
            r = rl_strcmp(STR(0x4df460u), ext);   /* "WAV" */
            if (r != 0) {
                r = rl_strcmp(STR(0x4df464u), ext);   /* "SMK" */
                if (r != 0)
                    return r;
            }
        }
    }
    char *dst = (char *)g_rl_5cd420;   /* Destination */
    for (int i = 0; i < 40; i++)
        dst[i] = (char)g_rl_58fe30[i];
    if (rl_strcmp(STR(0x4df468u), ext) == 0)
        strcat(dst, STR(0x4df46cu));   /* "\pl8" */
    else if (rl_strcmp(STR(0x4df474u), ext) == 0)
        strcat(dst, STR(0x4df478u));   /* "\pl8" */
    else if (rl_strcmp(STR(0x4df480u), ext) == 0)
        strcat(dst, STR(0x4df484u));   /* "\wav" */
    else if (rl_strcmp(STR(0x4df48cu), ext) == 0)
        strcat(dst, g_rl_53f5f4 == 1 ? STR(0x4df490u) : STR(0x4df49cu));   /* "\smk_low" / "\smk_high" */
    return rl_plat_chdir(dst);
}
#undef STR

/* Port of FUN_004af767 (218 B) — does the asset exist: open it (_O_BINARY) here → 1; else in the install tree
 * (0x4af312) and in the CD tree (0x4af116), back to the launch directory after each (0x4af502) → 1 if either, 0. */
int rl_4af767(int name)
{
    const char *fn = (const char *)(uintptr_t)(uint32_t)name;
    int found = 0;
    int fd = rl_plat_open(fn, 0x8000, 0);
    if (fd >= 0) {
        rl_plat_close(fd);
        return 1;
    }
    rl_4af312(name);
    fd = rl_plat_open(fn, 0x8000, 0);
    if (fd >= 0) {
        found = 1;
        rl_plat_close(fd);
    }
    chdir_base();
    rl_4af116(name);
    fd = rl_plat_open(fn, 0x8000, 0);
    if (fd >= 0) {
        found = 1;
        rl_plat_close(fd);
    }
    chdir_base();
    return found;
}

/* Port of FUN_004af841 (152 B) — where is the asset: 1 = here, 2 = in the install tree (0x4af312; back to the
 * launch directory after), 0 = neither (the CD tree is not tried). */
int rl_4af841(int name)
{
    const char *fn = (const char *)(uintptr_t)(uint32_t)name;
    int where = 0;
    int fd = rl_plat_open(fn, 0x8000, 0);
    if (fd >= 0) {
        rl_plat_close(fd);
        return 1;
    }
    rl_4af312(name);
    fd = rl_plat_open(fn, 0x8000, 0);
    if (fd >= 0) {
        where = 2;
        rl_plat_close(fd);
    }
    chdir_base();
    return where;
}

/* ---------------------------------------------------------------- the save list + lords2.inf */

/* the save list DAT_004e8790: 100 names × 65 B */
#define SAVE_NAME(i) (&g_rl_4e8790[65 * (i)])

/* Port of FUN_004aee3e (320 B) — bubble-sort the save list (until a pass swaps nothing, at most 101 passes) by the
 * FIRST BYTE of each name only (quirk: "b2" and "b1" keep their order), stopping a pass at the first empty name;
 * a swap moves 64 of the 65 bytes. Returns the number of the last pass (retail's EAX). */
int rl_4aee3e(void)
{
    uint8_t tmp[100];
    int swapped = 1, pass = 0, n;
    for (;;) {
        n = pass++;
        if (n >= 101 || swapped == 0)
            return n;
        swapped = 0;
        for (int i = 0; i < 99; i++) {
            if (SAVE_NAME(i)[0] == 0 || SAVE_NAME(i + 1)[0] == 0)
                break;
            if (SAVE_NAME(i + 1)[0] < SAVE_NAME(i)[0]) {
                swapped = 1;
                strutil_bounded_copy(tmp, SAVE_NAME(i), 64);
                strutil_bounded_copy(SAVE_NAME(i), SAVE_NAME(i + 1), 64);
                strutil_bounded_copy(SAVE_NAME(i + 1), tmp, 64);
            }
        }
    }
}

/* Port of FUN_004aecf0 (334 B) — list the files matching `pattern` into the save list (DAT_004eb25c = the count, at
 * most 100; 64 bytes of each name; the extension stripped unless `keep_ext`; lower-cased), then sort it (0x4aee3e).
 * DAT_004ea1a0 (the list's cursor) = 0. Returns the sort's EAX, or INVALID_HANDLE_VALUE when nothing matches. */
int rl_4aecf0(int pattern, int keep_ext)
{
    WIN32_FIND_DATAA fd;
    g_rl_4ea1a0 = 0;
    g_rl_4eb25c = 0;
    for (int i = 0; i < 100; i++)
        SAVE_NAME(i)[0] = 0;
    HANDLE h = rl_plat_FindFirstFileA((const char *)(uintptr_t)(uint32_t)pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return (int)(intptr_t)h;
    while (g_rl_4eb25c < 100) {
        for (int j = 0; j < 64; j++)
            SAVE_NAME(g_rl_4eb25c)[j] = (uint8_t)fd.cFileName[j];
        if (keep_ext == 0)
            strutil_ext_strip((char *)SAVE_NAME(g_rl_4eb25c));
        strutil_to_lower_inplace(SAVE_NAME(g_rl_4eb25c));
        g_rl_4eb25c++;
        if (!rl_plat_FindNextFileA(h, &fd))
            break;
    }
    rl_plat_FindClose(h);
    int r = rl_4aee3e();
    g_rl_4ea1a0 = 0;
    return r;
}

/* Port of FUN_004ae1b8 (243 B) — boot: list the saves (*.sav, 0x4aecf0), the default save name DAT_004ea130 = the
 * first one (13 bytes) or "Lords2", then read lords2.inf (the 0x468-byte settings block DAT_0053f1e0) over the
 * defaults — keeping the CD drive letter DAT_0053f200 — and 0x4ae2bd. Returns retail's EAX: -1 when lords2.inf is
 * absent, else DAT_0053f208 with its low byte = the drive letter. */
int rl_4ae1b8(void)
{
    uint8_t drive = g_rl_53f200;
    rl_4aecf0((int)(uintptr_t)RS(g_rl_4df160, 0x4df160u, 0x4df160u), 0);   /* "*.sav" */
    if (g_rl_4eb25c != 0)
        strutil_bounded_copy(g_rl_4ea130, SAVE_NAME(0), 13);
    else
        strutil_bounded_copy(g_rl_4ea130, RS(g_rl_4df160, 0x4df160u, 0x4df168u), 13);   /* "Lords2" */
    g_rl_4ea1a0 = 0;
    g_rl_4e65dc = 0;
    rl_4ae310();
    rl_4ae2ab(g_rl_53f208);
    int fd = rl_plat_open(RS(g_rl_4df160, 0x4df160u, 0x4df170u), 0x8404, 0);   /* "lords2.inf" */
    if (fd == -1)
        return fd;
    rl_plat_read(fd, g_bf_53f1e0, 0x468u);
    rl_plat_close(fd);
    rl_4ae2bd();
    rl_4ae2ab(g_rl_53f208);
    g_rl_53f244 = 0;
    g_rl_53f200 = drive;
    return (int)(((uint32_t)g_rl_53f208 & 0xffffff00u) | drive);
}
#undef SAVE_NAME

/* Port of FUN_004b2575 (59 B) — free physical memory: GlobalMemoryStatus into DAT_004ea100 (zeroed, dwLength 32).
 * Returns dwAvailPhys (the movie log's "MEM: max alloc"). */
int rl_4b2575(void)
{
    MEMORYSTATUS *m = (MEMORYSTATUS *)g_rl_4ea100;
    memset(m, 0, 32);
    m->dwLength = 32;
    rl_plat_GlobalMemoryStatus(m);
    return (int)m->dwAvailPhys;
}

/* ---------------------------------------------------------------- the debug log status.txt */

/* Port of FUN_004b049b (103 B) — the log's clock: time / localtime / asctime (retail's CRT: its static tm and
 * string), the first 40 bytes of the string copied to DAT_004eb280 ("Wed Nov 04 10:12:34 2026\n" + what follows
 * asctime's 26-byte buffer). Returns retail's EAX: the last pointer's high bytes with the last byte copied. */
int rl_4b049b(void)
{
    int32_t t;
    rl_plat_time(&t);
    void *tm = rl_plat_localtime(&t);
    const uint8_t *s = (const uint8_t *)rl_plat_asctime(tm);
    uint32_t r = 0;
    for (int i = 0; i < 40; i++) {
        r = ((uint32_t)(uintptr_t)s & 0xffffff00u) | *s;
        g_rl_4eb280[i] = *s++;
    }
    return (int)r;
}

/* Port of FUN_004afe3a (179 B) — write the first `n` bytes of the line buffer DAT_004eadb0 to status.txt: the first
 * write of the run (DAT_004df2a0 == 0) truncates it, later ones append (_lseek to the end); DAT_004df2a0 = 1 after a
 * write. Returns _close's result, or -1 when the file cannot be opened (DAT_004df2a0 unchanged). */
int rl_4afe3a(int n)
{
    int fd;
    if (g_rl_4df2a0 == 0) {
        fd = rl_plat_open(RS(g_rl_4df4c8, 0x4df4c8u, 0x4df4c8u), 0x8301, 0x180);   /* _O_BINARY | _O_TRUNC | _O_CREAT | _O_WRONLY */
        if (fd == -1)
            return fd;
    } else {
        fd = rl_plat_open(RS(g_rl_4df4c8, 0x4df4c8u, 0x4df4d4u), 0x8101, 0x180);   /* _O_BINARY | _O_CREAT | _O_WRONLY */
        if (fd == -1)
            return fd;
        rl_plat_lseek(fd, 0, 2);
    }
    rl_plat_write(fd, g_rl_4eadb0, (unsigned)n);
    int r = rl_plat_close(fd);
    g_rl_4df2a0 = 1;
    return r;
}

/* Port of FUN_004afab9 (897 B) — log a line to status.txt: `msg` (at most 100 chars), then `arg` (a string, if set)
 * and the number `num - 1` (num != 0; formatted by 0x4022bd into DAT_0058fda0 when num > 0), each followed by two
 * spaces, after the time of day (DAT_004eb28a: asctime's chars 10..19) and two spaces; CR LF. The run's first line
 * is instead "msg" + the date (asctime's first 10 chars) + CR LF, written, then 60 dashes + CR LF (`arg` / `num`
 * not shown). Returns the last write's result (0x4afe3a). */
int rl_4afab9(int msg, int arg, int num)
{
    const uint8_t crlf[2] = { 13, 10 };
    const uint8_t *m = (const uint8_t *)(uintptr_t)(uint32_t)msg;
    unsigned n;
    int i;
    rl_4b049b();
    if (num > 0)   /* 0x4022bd: into DAT_0058fda0 */
        gfx_text_label_format_dec(num - 1, 0, 0);
    if (g_rl_4df2a0 == 0) {
        n = 0;
        for (i = 0; i < 100 && m[i] != 0; i++)
            g_rl_4eadb0[n++] = m[i];
        for (i = 0; i < 10; i++)
            g_rl_4eadb0[n++] = g_rl_4eb280[i];
        for (i = 0; i < 2; i++)
            g_rl_4eadb0[n++] = crlf[i];
        rl_4afe3a((int)n);
        n = 0;
        for (i = 0; i < 60; i++)
            g_rl_4eadb0[n++] = '-';
        for (i = 0; i < 2; i++)
            g_rl_4eadb0[n++] = crlf[i];
        return rl_4afe3a((int)n);
    }
    n = 0;
    for (i = 0; i < 10; i++)
        g_rl_4eadb0[n++] = g_rl_4eb280[10 + i];
    for (i = 0; i < 2; i++)
        g_rl_4eadb0[n++] = ' ';
    for (i = 0; i < 100 && m[i] != 0; i++)
        g_rl_4eadb0[n++] = m[i];
    if (arg != 0) {
        const uint8_t *a = (const uint8_t *)(uintptr_t)(uint32_t)arg;
        for (i = 0; i < 100 && a[i] != 0; i++)
            g_rl_4eadb0[n++] = a[i];
        for (i = 0; i < 2; i++)
            g_rl_4eadb0[n++] = ' ';
    }
    if (num != 0) {
        for (i = 0; i < 100 && g_gfx_format_buffer[i] != 0; i++)
            g_rl_4eadb0[n++] = g_gfx_format_buffer[i];
        for (i = 0; i < 2; i++)
            g_rl_4eadb0[n++] = ' ';
    }
    for (i = 0; i < 2; i++)
        g_rl_4eadb0[n++] = crlf[i];
    return rl_4afe3a((int)n);
}

/* ---------------------------------------------------------------- WAV files (mmio RIFF) */

#define FCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define PTR(T, v) ((T)(uintptr_t)(uint32_t)(v))

/* Port of FUN_004b92e0 (730 B) — open a WAV file and read its format: mmioOpen (MMIO_ALLOCBUF) here, else in the
 * install tree (0x4af312), else the CD tree (0x4af116) — back to the launch directory after a retry — descend the
 * RIFF 'WAVE' chunk into `riff`, the 'fmt ' chunk (>= 16 B): the PCMWAVEFORMAT (16 B) + cbSize (non-PCM: read; PCM:
 * 0) into a GlobalAlloc'd WAVEFORMATEX (*pwfx, 18 + cbSize B, the extra bytes read), ascend. *phmmio = the open
 * file (NULL on failure). Returns 0, else the failing step 1..10 (the format freed, the file closed, logged). */
int rl_4b92e0(int name, int phmmio, int pwfx, int riff)
{
    char *fn = PTR(char *, name);
    HGLOBAL *wfx = PTR(HGLOBAL *, pwfx);
    MMCKINFO *ck = PTR(MMCKINFO *, riff);
    MMCKINFO fmt;
    uint16_t pcm[8];
    uint16_t cb;
    int r = 20;
    *wfx = NULL;
    HMMIO h = rl_plat_mmioOpenA(fn, NULL, MMIO_ALLOCBUF);
    if (h == NULL) {
        rl_4af312(name);
        h = rl_plat_mmioOpenA(fn, NULL, MMIO_ALLOCBUF);
        if (h == NULL) {
            rl_4af116(name);
            h = rl_plat_mmioOpenA(fn, NULL, MMIO_ALLOCBUF);
            if (h == NULL) {
                r = 1;
                chdir_base();
                goto fail;
            }
        }
        chdir_base();
    }
    if (rl_plat_mmioDescend(h, ck, NULL, 0) != 0) { r = 2; goto fail; }
    if (ck->ckid != FCC('R', 'I', 'F', 'F') || ck->fccType != FCC('W', 'A', 'V', 'E')) { r = 3; goto fail; }
    fmt.ckid = FCC('f', 'm', 't', ' ');
    if (rl_plat_mmioDescend(h, &fmt, ck, MMIO_FINDCHUNK) != 0) { r = 4; goto fail; }
    if (fmt.cksize < 16) { r = 5; goto fail; }
    if (rl_plat_mmioRead(h, (char *)pcm, 16) != 16) { r = 6; goto fail; }
    if (pcm[0] == 1)
        cb = 0;
    else if (rl_plat_mmioRead(h, (char *)&cb, 2) != 2) { r = 7; goto fail; }
    *wfx = rl_plat_GlobalAlloc(0, (SIZE_T)cb + 18);
    if (*wfx == NULL) { r = 8; goto fail; }
    memcpy(*wfx, pcm, 16);
    *(uint16_t *)((uint8_t *)*wfx + 16) = cb;
    if (cb != 0 && rl_plat_mmioRead(h, (char *)*wfx + 18, cb) != (LONG)cb) { r = 9; goto fail; }
    if (rl_plat_mmioAscend(h, &fmt, 0) != 0) { r = 10; goto fail; }
    r = 0;
    *PTR(HMMIO *, phmmio) = h;
    return r;
fail:
    if (*wfx != NULL) {
        rl_plat_GlobalFree(*wfx);
        *wfx = NULL;
    }
    if (h != NULL) {
        rl_plat_mmioClose(h, 0);
        h = NULL;
    }
    rl_4afab9((int)(uintptr_t)RS(g_rl_4e3040, 0x4e3040u, 0x4e3040u), name, r + 1);   /* "ERR:WAV open file - " */
    *PTR(HMMIO *, phmmio) = h;
    return r;
}

/* Port of FUN_004b95ba (152 B) — seek to the RIFF chunk's data (riff->dwDataOffset + 4: past 'WAVE') and descend its
 * 'data' chunk into `data`. Returns 0, else 1 (the seek failed) or the descend's error (logged). */
int rl_4b95ba(int phmmio, int data, int riff)
{
    HMMIO h = *PTR(HMMIO *, phmmio);
    MMCKINFO *ck = PTR(MMCKINFO *, data), *parent = PTR(MMCKINFO *, riff);
    int r = 1;
    if (rl_plat_mmioSeek(h, (LONG)(parent->dwDataOffset + 4), SEEK_SET) != -1) {
        ck->ckid = FCC('d', 'a', 't', 'a');
        r = (int)rl_plat_mmioDescend(*PTR(HMMIO *, phmmio), ck, parent, MMIO_FINDCHUNK);
        if (r == 0)
            return 0;
    }
    rl_4afab9((int)(uintptr_t)RS(g_rl_4e3040, 0x4e3040u, 0x4e3058u), 0, 0);   /* "ERR:WAV start read data." */
    return r;
}

/* Port of FUN_004b9652 (423 B) — read min(want, data->cksize) bytes of the 'data' chunk into `dst` through the mmio
 * buffer (mmioGetInfo / mmioAdvance / mmioSetInfo); data->cksize counts down. *got = the bytes read. Returns 0, else
 * 1 (GetInfo), 2 (Advance), 3 (end of file), 5 (a run > 200000 B), 4 (SetInfo) with *got = 0. */
int rl_4b9652(int hmmio, int want, int dst, int data, int got)
{
    HMMIO h = PTR(HMMIO, hmmio);
    MMCKINFO *ck = PTR(MMCKINFO *, data);
    uint32_t *pgot = PTR(uint32_t *, got);
    MMIOINFO mi;
    int r;
    if (rl_plat_mmioGetInfo(h, &mi, 0) != 0) { r = 1; goto fail; }
    uint32_t n = (uint32_t)want;
    if (ck->cksize < n)
        n = ck->cksize;
    ck->cksize -= n;
    for (uint32_t done = 0; n > done; ) {
        if (mi.pchNext == mi.pchEndRead) {
            if (rl_plat_mmioAdvance(h, &mi, 0) != 0) { r = 2; goto fail; }
            if (mi.pchNext == mi.pchEndRead) { r = 3; goto fail; }
        }
        uint32_t size = (uint32_t)(mi.pchEndRead - mi.pchNext);
        if (n - done < size)
            size = n - done;
        if (size > 0x30d40u) { r = 5; goto fail; }
        memcpy(PTR(uint8_t *, dst) + done, mi.pchNext, size);
        done += size;
        mi.pchNext += size;
    }
    if (rl_plat_mmioSetInfo(h, &mi, 0) != 0) { r = 4; goto fail; }
    *pgot = n;
    return 0;
fail:
    *pgot = 0;
    return r;
}

/* Port of FUN_004b97f9 (86 B) — release a WAV: GlobalFree the format (*pwfx = NULL), mmioClose the file
 * (*phmmio = NULL). Returns 0. */
int rl_4b97f9(int phmmio, int pwfx)
{
    HGLOBAL *wfx = PTR(HGLOBAL *, pwfx);
    HMMIO *ph = PTR(HMMIO *, phmmio);
    if (*wfx != NULL) {
        rl_plat_GlobalFree(*wfx);
        *wfx = NULL;
    }
    if (*ph != NULL) {
        rl_plat_mmioClose(*ph, 0);
        *ph = NULL;
    }
    return 0;
}

/* Port of FUN_004b984f (361 B) — load a whole WAV: open + format (0x4b92e0), its 'data' chunk (0x4b95ba), a
 * GlobalAlloc'd copy of the samples (0x4b9652). *pwfx = the format, *pdata = the samples, *psize = their length; the
 * file is closed. Returns 0, else 1..4 (both blocks freed, logged). */
int rl_4b984f(int name, int psize, int pwfx, int pdata)
{
    HMMIO h;
    MMCKINFO riff, data;
    uint32_t got;
    HGLOBAL *pd = PTR(HGLOBAL *, pdata), *pw = PTR(HGLOBAL *, pwfx);
    int r;
    *pd = NULL;
    *pw = NULL;
    *PTR(uint32_t *, psize) = 0;
    if (rl_4b92e0(name, (int)(uintptr_t)&h, pwfx, (int)(uintptr_t)&riff) != 0) {
        r = 1;
    } else if (rl_4b95ba((int)(uintptr_t)&h, (int)(uintptr_t)&data, (int)(uintptr_t)&riff) != 0) {
        r = 2;
    } else {
        *pd = rl_plat_GlobalAlloc(0, data.cksize);
        if (*pd == NULL) {
            r = 3;
        } else if (rl_4b9652((int)(uintptr_t)h, (int)data.cksize, (int)(uintptr_t)*pd, (int)(uintptr_t)&data,
                             (int)(uintptr_t)&got) != 0) {
            r = 4;
        } else {
            *PTR(uint32_t *, psize) = got;
            r = 0;
            goto done;
        }
    }
    if (*pd != NULL) {
        rl_plat_GlobalFree(*pd);
        *pd = NULL;
    }
    if (*pw != NULL) {
        rl_plat_GlobalFree(*pw);
        *pw = NULL;
    }
    rl_4afab9((int)(uintptr_t)RS(g_rl_4e3040, 0x4e3040u, 0x4e3074u), name, r + 1);   /* "ERR:WAV loading wavefile data - " */
done:
    if (h != NULL) {
        rl_plat_mmioClose(h, 0);
        h = NULL;
    }
    return r;
}

/* ---------------------------------------------------------------- DirectSound: the WAV cue + the effect bank */

/* COM through the object's own vtable (rhost's fake DirectSound or the real one: the object retail created) */
#define VT(obj, off) (*(void **)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)(obj) + (off)))
typedef HRESULT (WINAPI *ds_create_t)(uint32_t ds, const void *desc, uint32_t *out, void *outer);
typedef HRESULT (WINAPI *ds_lock_t)(uint32_t b, uint32_t off, uint32_t n, void **p1, uint32_t *n1, void **p2, uint32_t *n2, uint32_t fl);
typedef HRESULT (WINAPI *ds_unlock_t)(uint32_t b, void *p1, uint32_t n1, void *p2, uint32_t n2);
typedef HRESULT (WINAPI *ds_play_t)(uint32_t b, uint32_t r1, uint32_t prio, uint32_t fl);
typedef HRESULT (WINAPI *ds_status_t)(uint32_t b, uint32_t *st);
typedef HRESULT (WINAPI *ds_this_t)(uint32_t b);                 /* Stop (+0x48), Release (+8) */
typedef HRESULT (WINAPI *ds_pos_t)(uint32_t b, uint32_t *play, uint32_t *write);
typedef HRESULT (WINAPI *ds_setpos_t)(uint32_t b, uint32_t pos);
#define DS_CREATE(ds, d, out)     ((ds_create_t)VT(ds, 0x0c))(ds, d, out, NULL)
#define DSB_POS(b, p, w)          ((ds_pos_t)VT(b, 0x10))(b, p, w)
#define DSB_STATUS(b, st)         ((ds_status_t)VT(b, 0x24))(b, st)
#define DSB_LOCK(b, o, n, a, an, c, cn, f) ((ds_lock_t)VT(b, 0x2c))(b, o, n, a, an, c, cn, f)
#define DSB_PLAY(b, f)            ((ds_play_t)VT(b, 0x30))(b, 0, 0, f)
#define DSB_SETPOS(b, p)          ((ds_setpos_t)VT(b, 0x34))(b, p)
#define DSB_STOP(b)               ((ds_this_t)VT(b, 0x48))(b)
#define DSB_UNLOCK(b, a, an, c, cn) ((ds_unlock_t)VT(b, 0x4c))(b, a, an, c, cn)
#define DS_RELEASE(b)             ((ds_this_t)VT(b, 0x08))(b)

#define PPDS (*(volatile uint32_t *)g_bf_4d4760)                       /* ppDS: the IDirectSound */
#define CUE  AT(uint32_t, g_rl_522aec, 0x522aec, 0x522aecu)            /* DAT_00522aec: the one cue buffer */
#define HR   g_bf_522b58                                               /* DAT_00522b58: the last HRESULT */
/* DAT_00522afc[k], k = 1..20: the effect bank's buffers (k = 0: DAT_00522afc itself) */
static volatile int32_t *sfx(int k) { return k == 0 ? &g_bf_522afc : &AT(int32_t, g_rl_522b00, 0x522b00, 0x522afcu + 4 * k); }
/* DSBUFFERDESC (20 B): dwSize, dwFlags, dwBufferBytes, dwReserved, lpwfxFormat */
typedef struct { uint32_t size, flags, bytes, reserved, wfx; } dsbdesc_t;

/* the cue release 0x427d19 (rl ui4) */
static void cue_release(void) { rl_427d19(); }

/* Port of FUN_00427c9b (126 B) — is the cue playing: ppDS and the cue buffer set, GetStatus succeeds (HRESULT in
 * DAT_00522b58) and says DSBSTATUS_PLAYING alone (== 1, not & 1: a looping cue reads 5 = not playing, quirk). */
int rl_427c9b(void)
{
    uint32_t st;
    if (PPDS == 0)
        return 0;
    if (CUE == 0)
        return 0;
    HR = DSB_STATUS(CUE, &st);
    if (HR != 0)
        return 0;
    return st == 1 ? 1 : 0;
}

/* Port of FUN_00427990 (779 B) — play a WAV cue: nothing while one plays (0x427c9b) — 0; the previous one released
 * (0x427d19); gated by the effect (kind 1: DAT_0053f20c) / speech (kind 0: DAT_0053f214) switches — 0. "null.wav"
 * plays nothing. Else the WAV is loaded whole (0x4b984f; retried in the install / CD trees, logged when missing),
 * a static buffer created (DSBCAPS 0xe2; 0xe3 when `loop`: DSBCAPS_PRIMARYBUFFER added, quirk) into DAT_00522aec, filled (Lock /
 * copy / Unlock) and played (looping when `loop`). Returns 1 when done (played, null, not found), 0 when a
 * DirectSound step failed (logged "ERR:Load and play WAV"). Both GlobalAlloc'd blocks are freed. */
int rl_427990(int name, int kind, int loop)
{
    HGLOBAL wfx = NULL, data = NULL;
    uint32_t size;
    void *p1, *p2;
    uint32_t n1, n2;
    if (PPDS == 0)
        return 0;
    if (rl_427c9b() != 0)
        return 0;
    cue_release();
    if (kind == 1 && g_rl_53f20c == 0)
        return 0;
    if (kind == 0 && g_bf_53f214 == 0)
        return 0;
    if (rl_strcmp(PTR(const char *, name), RS(g_rl_4d4864, 0x4d4864u, 0x4d496cu)) == 0)   /* "null.wav" */
        goto done;
    if (rl_4b984f(name, (int)(uintptr_t)&size, (int)(uintptr_t)&wfx, (int)(uintptr_t)&data) != 0) {
        rl_4af312(name);
        if (rl_4b984f(name, (int)(uintptr_t)&size, (int)(uintptr_t)&wfx, (int)(uintptr_t)&data) != 0) {
            rl_4af116(name);
            if (rl_4b984f(name, (int)(uintptr_t)&size, (int)(uintptr_t)&wfx, (int)(uintptr_t)&data) != 0) {
                chdir_base();
                rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4978u), name, 0);   /* "ERR:Not found file :" */
                goto done;
            }
        }
        chdir_base();
    }
    dsbdesc_t d;
    memset(&d, 0, sizeof d);
    d.size = 20;
    d.flags = loop != 0 ? 0xe3u : 0xe2u;
    d.bytes = size;
    d.wfx = (uint32_t)(uintptr_t)wfx;
    HR = DS_CREATE(PPDS, &d, (uint32_t *)&CUE);
    if (HR != 0)
        goto fail;
    uint32_t fl = 0;
    if (loop != 0)
        fl |= 1;   /* DSBPLAY_LOOPING */
    HR = DSB_LOCK(CUE, 0, size, &p1, &n1, &p2, &n2, 0);
    if (HR != 0)
        goto fail;
    strutil_bounded_copy(p1, data, (int)size);
    HR = DSB_UNLOCK(CUE, p1, n1, p2, 0);
    if (HR != 0)
        goto fail;
    HR = DSB_PLAY(CUE, fl);
    if (HR != 0)
        goto fail;
done:
    if (wfx != NULL) {
        rl_plat_GlobalFree(wfx);
        wfx = NULL;
    }
    if (data != NULL) {
        rl_plat_GlobalFree(data);
        data = NULL;
    }
    return 1;
fail:
    if (wfx != NULL) {
        rl_plat_GlobalFree(wfx);
        wfx = NULL;
    }
    if (data != NULL) {
        rl_plat_GlobalFree(data);
        data = NULL;
    }
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4990u), name, 0);   /* "ERR:Load and play WAV" */
    return 0;
}

/* Port of FUN_004262eb (117 B) — release effect buffer `k` (Stop, Release, DAT_00522afc[k] = 0). Returns k (retail's
 * EAX; ppDS unset: the caller's EAX, passed through). */
int rl_4262eb(int k)
{
    if (PPDS == 0)
        return 0;
    if (*sfx(k) == 0)
        return k;
    DSB_STOP((uint32_t)*sfx(k));
    DS_RELEASE((uint32_t)*sfx(k));
    *sfx(k) = 0;
    return k;
}

/* Port of FUN_00426360 (77 B) — stop effect buffer `k`. Returns Stop's HRESULT, or k when the buffer is unset. */
int rl_426360(int k)
{
    if (PPDS == 0)
        return 0;
    if (*sfx(k) == 0)
        return k;
    return (int)DSB_STOP((uint32_t)*sfx(k));
}

/* Port of FUN_00426216 (185 B) — play effect `k` from the start (Stop, then Play once; HRESULTs in DAT_00522b58),
 * when DirectSound, the buffer and the effects switch DAT_0053f214 are on. Returns 1 when Play succeeded, else 0. */
int rl_426216(int k)
{
    if (PPDS == 0)
        return 0;
    if (*sfx(k) == 0)
        return 0;
    if (g_bf_53f214 == 0)
        return 0;
    HR = DSB_STOP((uint32_t)*sfx(k));
    HR = DSB_PLAY((uint32_t)*sfx(k), 0);
    return HR == 0 ? 1 : 0;
}

/* Port of FUN_00425e4b (725 B) — load the effect bank: every buffer released (0x4262eb 1..20), then `n` (at most 20)
 * WAVs from the name table `names` (16 B apart) into DAT_00522afc[1..n]: loaded whole (0x4b984f, retried in the
 * install / CD trees; a missing one logged and skipped), a static buffer (0xe2) created, filled (Lock / copy /
 * Unlock). "null.wav" skips its slot. Returns 1, or 0 when a DirectSound step failed (logged, the rest not loaded). */
int rl_425e4b(int names, int n)
{
    if (PPDS == 0)
        return 0;
    for (int i = 1; i <= 20; i++)
        rl_4262eb(i);
    if (n > 20)
        n = 20;
    HGLOBAL wfx = NULL, data = NULL;
    for (int i = 0; i < n; i++, names += 16) {
        uint32_t size;
        void *p1, *p2;
        uint32_t n1, n2;
        wfx = NULL;
        data = NULL;
        if (rl_strcmp(PTR(const char *, names), RS(g_rl_4d4814, 0x4d4814u, 0x4d4814u)) != 0) {   /* "null.wav" */
            if (rl_4b984f(names, (int)(uintptr_t)&size, (int)(uintptr_t)&wfx, (int)(uintptr_t)&data) != 0) {
                rl_4af312(names);
                if (rl_4b984f(names, (int)(uintptr_t)&size, (int)(uintptr_t)&wfx, (int)(uintptr_t)&data) != 0) {
                    rl_4af116(names);
                    if (rl_4b984f(names, (int)(uintptr_t)&size, (int)(uintptr_t)&wfx, (int)(uintptr_t)&data) != 0) {
                        chdir_base();
                        rl_4afab9((int)(uintptr_t)RS(g_rl_4d4814, 0x4d4814u, 0x4d4820u), names, 0);   /* "ERR:Not found file :" */
                        goto next;
                    }
                }
                chdir_base();
            }
            dsbdesc_t d;
            memset(&d, 0, sizeof d);
            d.size = 20;
            d.flags = 0xe2u;
            d.bytes = size;
            d.wfx = (uint32_t)(uintptr_t)wfx;
            uint32_t *slot = (uint32_t *)&g_rl_522b00[i];
            HR = DS_CREATE(PPDS, &d, slot);
            if (HR != 0)
                goto fail;
            HR = DSB_LOCK((uint32_t)g_rl_522b00[i], 0, size, &p1, &n1, &p2, &n2, 0);
            if (HR != 0)
                goto fail;
            strutil_bounded_copy(p1, data, (int)size);
            HR = DSB_UNLOCK((uint32_t)g_rl_522b00[i], p1, n1, p2, 0);
            if (HR != 0)
                goto fail;
        }
next:
        if (wfx != NULL) {
            rl_plat_GlobalFree(wfx);
            wfx = NULL;
        }
        if (data != NULL) {
            rl_plat_GlobalFree(data);
            data = NULL;
        }
    }
    return 1;
fail:
    if (wfx != NULL) {
        rl_plat_GlobalFree(wfx);
        wfx = NULL;
    }
    if (data != NULL) {
        rl_plat_GlobalFree(data);
        data = NULL;
    }
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4814, 0x4d4814u, 0x4d4838u), names, 0);   /* "ERR:Loading WAV file :" */
    return 0;
}

/* ---------------------------------------------------------------- the effect / speech cue wrappers + the music */

#define CUE_KIND_FX 1
/* Port of FUN_004b36c0 (84 B) — confirm-dialog voice `i` (0..15): S010_01.wav + 16 i (DAT_004df7b8), as an effect.
 * The combat audio latch DAT_005533f0 = 1. Returns 0x427990's (out of range: nothing). */
int rl_4b36c0(int i)
{
    if (i < 0 || i >= 16)
        return 0;
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)RS(g_rl_4df7b8, 0x4df7b8u, 0x4df7b8u + 16 * i), CUE_KIND_FX, 0);
}

/* Port of FUN_004b3714 (84 B) — send-supplies voice `i` (0..15): S016_01.wav + 16 i (DAT_004df8b8). */
int rl_4b3714(int i)
{
    if (i < 0 || i >= 16)
        return 0;
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)RS(g_rl_4df7b8, 0x4df7b8u, 0x4df8b8u + 16 * i), CUE_KIND_FX, 0);
}

/* Port of FUN_004b3940 (84 B) — voice `i` (0..4): S071_02.wav + 16 i (DAT_004e2118). */
int rl_4b3940(int i)
{
    if (i < 0 || i > 4)
        return 0;
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)RS(g_rl_4e20d8, 0x4e20d8u, 0x4e2118u + 16 * i), CUE_KIND_FX, 0);
}

/* Port of FUN_004b3b92 (89 B) — event-scroll voice `i` (0..4): the previous cue cut (0x427d19), S246_02.wav + 16 i
 * (DAT_004e2470). */
int rl_4b3b92(int i)
{
    if (i < 0 || i >= 5)
        return 0;
    cue_release();
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)RS(g_rl_4e2470, 0x4e2470u, 0x4e2470u + 16 * i), CUE_KIND_FX, 0);
}

/* Port of FUN_004b37bc (388 B) — the terrain-info voice: on a county (DAT_0052afbc != 0) by its kind (+8: 1 = a
 * castle — S031_04 if the human's (+0 owner, a SIGNED byte vs DAT_0057c8cc) else S031_03; 4 = S031_02; 2 = S031_01;
 * other: none); off a county, when the cursor flag DAT_005440b0 & 0x80 and the zoom DAT_0057cb2c >= 21, the
 * hovered army's (DAT_005681e0) kind +0x1c0 (1..5) picks S071_02 + 16 (kind - 1). Returns 0x427990's. */
int rl_4b37bc(void)
{
    const char *s;
    int c = g_rl_52afbc;
    if (c != 0) {
        uint8_t kind = g_per_territory_420[420 * c + 8];
        if (kind == 1) {
            if ((int8_t)g_per_territory_420[420 * c] == (int32_t)s_slot_0057c8cc)
                s = RS(g_rl_4e20d8, 0x4e20d8u, 0x4e20d8u + 0x30);
            else
                s = RS(g_rl_4e20d8, 0x4e20d8u, 0x4e20d8u + 0x20);
        } else if (kind == 4) {
            s = RS(g_rl_4e20d8, 0x4e20d8u, 0x4e20d8u + 0x10);
        } else if (kind == 2) {
            s = RS(g_rl_4e20d8, 0x4e20d8u, 0x4e20d8u);
        } else {
            return 0;
        }
    } else {
        if (!(g_rl_5440b0 & 0x80) || g_rl_57cb2c < 21)
            return 0;
        uint8_t k = g_per_army_768[768 * g_rl_5681e0[0] + 0x1c0];
        if (k == 0 || k >= 6)
            return 0;
        s = RS(g_rl_4e20d8, 0x4e20d8u, 0x4e2118u + 16 * (k - 1));
    }
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)s, CUE_KIND_FX, 0);
}

/* Port of FUN_004b3acd (197 B) — the event scroll's narration, line by line: entry DAT_0052f004 of event `ev`'s row
 * (DAT_004e2228 + 5 ev - 1000) is the voice (0: done); while a cue plays the clock DAT_004e59fc is restarted; 1000 ms
 * after it ends the next entry is taken and voice 1..30 plays (S201_02.wav + 16 (v - 1)). Returns retail's EAX (the
 * row offset, the clock or its delta, or 0x427990's). */
int rl_4b3acd(int ev)
{
    int r = 5 * ev - 1000;
    int v = g_rl_4e2228[r + g_rl_52f004];
    if (v <= 0)
        return r;
    if (rl_427c9b() != 0) {
        r = (int)rl_plat_timeGetTime();
        g_rl_4e59fc = r;
        return r;
    }
    r = (int)rl_plat_timeGetTime() - g_rl_4e59fc;
    if (r < 1000)
        return r;
    g_rl_52f004++;
    if (v < 1 || v > 30)
        return r;
    g_combat_audio_latch = 1;
    return rl_427990((int)(uintptr_t)RS(g_rl_4e2228, 0x4e2228u, 0x4e2290u + 16 * v - 16), CUE_KIND_FX, 0);
}

/* the two streams (k = 0 music, 1 speech): DAT_00522bb0 + 84 k — the format, the HMMIO, the 'data' and RIFF
 * MMCKINFOs, the buffer, its size / chunk, the write cursor, flags */
#define ST(k, off) AT(uint32_t, g_rl_522bb0, 0x522bb0u, 0x522bb0u + 84u * (uint32_t)(k) + (off))
#define ST_ADDR(k, off) ((int)(uintptr_t)&ST(k, off))
enum { S_WFX = 0x00, S_HMMIO = 0x04, S_DATA = 0x08, S_RIFF = 0x1c, S_BUF = 0x30, S_SIZE = 0x34, S_CHUNK = 0x38,
       S_CURSOR = 0x3c, S_F40 = 0x40, S_STOP = 0x44, S_LOOP = 0x48, S_END = 0x4c, S_LEFT = 0x50 };
#define ST_DSB(k)   AT(uint32_t, g_rl_522b60, 0x522b60u, 0x522b80u + 4u * (uint32_t)(k))   /* DAT_00522b80[k] */
#define ST_NAME(k)  ((int)(uintptr_t)&AT(uint8_t, g_rl_522b60, 0x522b60u, 0x522b60u + 16u * (uint32_t)(k)))
#define ST_TIMER(k) g_rl_522b00[20 + (k)]                                                    /* uTimerID[k] */
#define U_RES    AT(uint32_t, g_rl_4d4754, 0x4d4754u, 0x4d4754u)   /* uResolution */
#define U_PERIOD AT(uint32_t, g_rl_4d4754, 0x4d4754u, 0x4d4758u)   /* uPeriod */
static int stream_stop(int k) { return rl_426d11(k); }   /* 0x426d11 (rl ui4) */
static uint16_t wfx_bits(int k) { return *(uint16_t *)(uintptr_t)(ST(k, S_WFX) + 14); }   /* wBitsPerSample */

/* Port of FUN_00426534 (1630 B) — set stream `k` up: open the WAV named DAT_00522b60 + 16 k (0x4b92e0: 1), PCM only
 * (2), its data chunk (0x4b95ba: 3); the buffer (created once, DAT_00522b80[k]: DAT_00544040++; size = uPeriod ×
 * DAT_004d475c / 10 (signed) × nAvgBytesPerSec / 100, rounded down to DAT_004d475c chunks; DSBCAPS 0xe0; failure: 4);
 * cursor / flags reset, `loop` kept; the whole buffer filled (Lock — its HRESULT is not checked, quirk — 0x4b9652;
 * a looping stream rewinds to the data and reads on, else the tail is silence and the chunks left are counted).
 * Returns 0; a read failure stops the stream (0x426d11, logged twice) and returns the log's EAX. */
int rl_426534(int k, int loop)
{
    void *p1, *p2;
    uint32_t n1, n2, got;
    if (rl_4b92e0(ST_NAME(k), ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_WFX), ST_ADDR(k, S_RIFF)) != 0)
        return 1;
    if (*(uint16_t *)(uintptr_t)ST(k, S_WFX) != 1) {   /* WAVE_FORMAT_PCM */
        rl_4b97f9(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_WFX));
        return 2;
    }
    if (rl_4b95ba(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_DATA), ST_ADDR(k, S_RIFF)) != 0) {
        rl_4b97f9(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_WFX));
        return 3;
    }
    if (ST_DSB(k) == 0) {
        g_rl_544040++;
        uint32_t avg = *(uint32_t *)(uintptr_t)(ST(k, S_WFX) + 8);   /* nAvgBytesPerSec */
        ST(k, S_SIZE) = avg * (uint32_t)((int32_t)(g_rl_4d475c * (int32_t)U_PERIOD) / 10) / 100u;
        ST(k, S_CHUNK) = ST(k, S_SIZE) / (uint32_t)g_rl_4d475c;
        ST(k, S_SIZE) = ST(k, S_CHUNK) * (uint32_t)g_rl_4d475c;
        dsbdesc_t d;
        memset(&d, 0, sizeof d);
        d.size = 20;
        d.flags = 0xe0u;
        d.bytes = ST(k, S_SIZE);
        d.wfx = ST(k, S_WFX);
        HR = DS_CREATE(PPDS, &d, (uint32_t *)&ST_DSB(k));
        if (HR != 0) {
            rl_4b97f9(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_WFX));
            return 4;
        }
    }
    ST(k, S_BUF) = ST_DSB(k);
    ST(k, S_END) = 0;
    ST(k, S_LEFT) = 0;
    ST(k, S_F40) = 0;
    ST(k, S_STOP) = 0;
    ST(k, S_LOOP) = (uint32_t)loop;
    HR = DSB_LOCK(ST_DSB(k), 0, ST(k, S_SIZE), &p1, &n1, &p2, &n2, 0);
    if (n1 != 0) {
        if (rl_4b9652((int)ST(k, S_HMMIO), (int)n1, (int)(uintptr_t)p1, ST_ADDR(k, S_DATA), (int)(uintptr_t)&got) != 0)
            goto fail;
        if (got < n1) {
            if (ST(k, S_LOOP) != 0) {
                uint8_t *at = (uint8_t *)p1;
                do {
                    at += got;
                    n1 -= got;
                    if (rl_4b95ba(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_DATA), ST_ADDR(k, S_RIFF)) != 0)
                        goto fail;
                    if (rl_4b9652((int)ST(k, S_HMMIO), (int)n1, (int)(uintptr_t)at, ST_ADDR(k, S_DATA), (int)(uintptr_t)&got) != 0)
                        goto fail;
                } while (got < n1);
            } else {
                ST(k, S_END) = 1;
                ST(k, S_LEFT) = got / ST(k, S_CHUNK);
                memset((uint8_t *)p1 + got, wfx_bits(k) == 8 ? 0x80 : 0, n1 - got);
            }
        }
    }
    HR = DSB_UNLOCK(ST_DSB(k), p1, n1, p2, 0);
    ST(k, S_CURSOR) = 0;
    ST(k, S_STOP) = 0;
    return 0;
fail:
    HR = DSB_UNLOCK(ST_DSB(k), p1, n1, p2, 0);
    ST(k, S_STOP) = 1;
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d489cu), ST_NAME(k), 0);   /* "ERR:DS-str ss stopped - pre stop " */
    stream_stop(k);
    return rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d48c0u), ST_NAME(k), 0);
}

/* Port of FUN_00426b92 (383 B) — start stream `k`: rewind its buffer (SetCurrentPosition 0) and play it looping, then
 * the multimedia timer (timeBeginPeriod uPeriod; timeSetEvent uPeriod / uResolution, periodic, callback 0x426dd5 with
 * dwUser = k → uTimerID[k]). Returns 0; 1 (no buffer, or timeBeginPeriod failed: stopped + released), 2 (no timer). */
int rl_426b92(int k)
{
    if (ST_DSB(k) == 0)
        return 1;
    HR = DSB_SETPOS(ST_DSB(k), 0);
    HR = DSB_PLAY(ST_DSB(k), 1);
    if (rl_plat_timeBeginPeriod(U_PERIOD) != 0) {
        HR = DSB_STOP(ST_DSB(k));
        rl_4b97f9(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_WFX));
        return 1;
    }
    ST_TIMER(k) = (int32_t)rl_plat_timeSetEvent(U_PERIOD, U_RES, rl_plat_stream_timer_cb(), (DWORD_PTR)(uint32_t)k, TIME_PERIODIC);
    if (ST_TIMER(k) != 0)
        return 0;
    rl_plat_timeEndPeriod(U_PERIOD);
    HR = DSB_STOP(ST_DSB(k));
    rl_4b97f9(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_WFX));
    return 2;
}

/* Port of FUN_004263ad (255 B) — play `name` on stream `k` (0 music: gated by DAT_0053f218; 1 speech: DAT_0053f214):
 * a running stream is stopped first (0x426d11), the name kept (16 B at DAT_00522b60 + 16 k), set up (0x426534;
 * failure: 1, logged) and started (0x426b92; failure: 2, logged). Returns 0 when playing. */
int rl_4263ad(int name, int k, int loop)
{
    if (PPDS == 0)
        return 0;
    if (g_bf_53f214 == 0 && k == 1)
        return 0;
    if (g_rl_53f218 == 0 && k == 0)
        return 0;
    if (ST_TIMER(k) != 0)
        stream_stop(k);
    strutil_bounded_copy((void *)(uintptr_t)ST_NAME(k), PTR(const void *, name), 16);
    if (rl_426534(k, loop) != 0) {
        rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4864u), name, 0);   /* "ERR:Cannot setup stream " */
        return 1;
    }
    if (rl_426b92(k) != 0) {
        rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4880u), name, 0);   /* "ERR:Cannot start stream " */
        return 2;
    }
    return 0;
}

/* refill a span of the stream buffer with silence by the format's bits (8: 0x80, 16: 0, other: untouched) */
static void silence_by_bits(int k, void *p, uint32_t n)
{
    if (wfx_bits(k) == 8)
        memset(p, 0x80, n);
    else if (wfx_bits(k) == 16)
        memset(p, 0, n);
}

/* Port of FUN_00426dd5 (3003 B) — the music streamer, winmm's timer callback (__stdcall; dwUser = the stream k;
 * rhost fires it on the main thread). A busy flag (InterlockedExchange on DAT_004d4764) keeps it single; a lost
 * buffer (GetStatus — through DAT_00522af0's vtable, quirk — & DSBSTATUS_BUFFERLOST) is skipped.
 * DAT_00544044 counts the ticks. When the play cursor has left the chunk at the write cursor: an ended stream counts
 * its chunks down (silence written; none left → stopped once, 0x426d11); else the chunk is locked and refilled from
 * the file (0x4b9652; a looping stream rewinds to its data, 0x4b95ba; a short read ends it: the rest silence, the
 * chunks left counted from the play cursor), unlocked and the write cursor moved on a chunk (wrapping). A read
 * failure stops it (DAT_00544028++, four log lines). Returns the last InterlockedExchange's (retail's EAX: 1). */
int rl_426dd5(int id, int msg, int user, int d1, int d2)
{
    (void)id; (void)msg; (void)d1; (void)d2;
    const int k = user;
    uint32_t st, play, wr;
    void *p1, *p2;
    uint32_t n1, n2, got;
    LONG busy = rl_plat_InterlockedExchange((LONG *)g_rl_4d4764, 1);
    if (busy != 0)
        return (int)busy;
    ((ds_status_t)VT(AT(uint32_t, g_rl_522aec, 0x522aecu, 0x522af0u), 0x24))(ST(k, S_BUF), &st);
    if (st & 2)   /* DSBSTATUS_BUFFERLOST */
        goto out;
    g_rl_544044++;
    DSB_POS(ST(k, S_BUF), &play, &wr);
    if (ST(k, S_CURSOR) == play || (ST(k, S_CURSOR) < play && ST(k, S_CURSOR) + ST(k, S_CHUNK) > play))
        goto out;
    if (ST(k, S_END) != 0 && ST(k, S_LEFT) == 0) {
        if (ST(k, S_STOP) == 0) {
            ST(k, S_STOP) = 1;
            stream_stop(k);
        }
        goto out;
    }
    if (ST(k, S_END) != 0 && ST(k, S_LEFT) != 0) {
        ST(k, S_LEFT)--;
        if ((int32_t)ST(k, S_LEFT) > 1 &&
            DSB_LOCK(ST(k, S_BUF), ST(k, S_CURSOR), ST(k, S_CHUNK), &p1, &n1, &p2, &n2, 0) == 0) {
            silence_by_bits(k, p1, n1);
            DSB_UNLOCK(ST(k, S_BUF), p1, n1, p2, n2);
            ST(k, S_CURSOR) += ST(k, S_CHUNK);
            if (ST(k, S_SIZE) <= ST(k, S_CURSOR))
                ST(k, S_CURSOR) -= ST(k, S_SIZE);
        }
        goto out;
    }
    if (DSB_LOCK(ST(k, S_BUF), ST(k, S_CURSOR), ST(k, S_CHUNK), &p1, &n1, &p2, &n2, 0) != 0)
        goto out;
    if (n1 != 0 && ST(k, S_STOP) == 0) {
        if (rl_4b9652((int)ST(k, S_HMMIO), (int)n1, (int)(uintptr_t)p1, ST_ADDR(k, S_DATA), (int)(uintptr_t)&got) != 0)
            goto fail;
        if (got < n1) {
            if (ST(k, S_LOOP) == 0) {
                silence_by_bits(k, (uint8_t *)p1 + got, n1 - got);
                ST(k, S_END) = 1;
                if (ST(k, S_CURSOR) >= play)
                    ST(k, S_LEFT) = (ST(k, S_CURSOR) - play) / ST(k, S_CHUNK);
                else
                    ST(k, S_LEFT) = (ST(k, S_CURSOR) + ST(k, S_SIZE) - play) / ST(k, S_CHUNK);
            } else {
                uint8_t *at = (uint8_t *)p1;
                do {
                    at += got;
                    n1 -= got;
                    if (rl_4b95ba(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_DATA), ST_ADDR(k, S_RIFF)) != 0)
                        goto fail;
                    if (rl_4b9652((int)ST(k, S_HMMIO), (int)n1, (int)(uintptr_t)at, ST_ADDR(k, S_DATA), (int)(uintptr_t)&got) != 0)
                        goto fail;
                } while (got < n1);
            }
        }
    } else if (n1 != 0 && ST(k, S_STOP) != 0) {
        memset(p1, wfx_bits(k) == 8 ? 0x80 : 0, n1);
    }
    if (n2 != 0 && ST(k, S_STOP) == 0) {
        if (rl_4b9652((int)ST(k, S_HMMIO), (int)n2, (int)(uintptr_t)p2, ST_ADDR(k, S_DATA), (int)(uintptr_t)&got) != 0)
            goto fail;
        if (got < n2) {
            if (ST(k, S_LOOP) == 0) {
                silence_by_bits(k, (uint8_t *)p2 + got, n2 - got);
                ST(k, S_END) = 1;
                if (ST(k, S_CURSOR) >= play)
                    ST(k, S_LEFT) = (ST(k, S_CURSOR) - play) / ST(k, S_CHUNK);
                else
                    ST(k, S_LEFT) = (ST(k, S_CURSOR) + ST(k, S_SIZE) - play) / ST(k, S_CHUNK);
            } else {
                uint8_t *at = (uint8_t *)p2;
                do {
                    at += got;
                    n2 -= got;
                    if (rl_4b95ba(ST_ADDR(k, S_HMMIO), ST_ADDR(k, S_DATA), ST_ADDR(k, S_RIFF)) != 0)
                        goto fail;
                    if (rl_4b9652((int)ST(k, S_HMMIO), (int)n2, (int)(uintptr_t)at, ST_ADDR(k, S_DATA), (int)(uintptr_t)&got) != 0)
                        goto fail;
                } while (got < n2);
            }
        }
    } else if (n2 != 0 && ST(k, S_STOP) != 0) {
        memset(p2, wfx_bits(k) == 8 ? 0x80 : 0, n2);
    }
    DSB_UNLOCK(ST(k, S_BUF), p1, n1, p2, n2);
    ST(k, S_CURSOR) += ST(k, S_CHUNK);
    if (ST(k, S_SIZE) <= ST(k, S_CURSOR))
        ST(k, S_CURSOR) -= ST(k, S_SIZE);
out:
    return (int)rl_plat_InterlockedExchange((LONG *)g_rl_4d4764, 0);
fail:
    g_rl_544028++;
    DSB_UNLOCK(ST(k, S_BUF), p1, n1, p2, n2);
    ST(k, S_STOP) = 1;
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d48e4u), ST_NAME(k), 0);
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4908u), 0, g_rl_544040 + 1);
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4928u), 0, g_rl_544038 + 1);
    stream_stop(k);
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4864, 0x4d4864u, 0x4d4948u), ST_NAME(k), 0);
    return (int)rl_plat_InterlockedExchange((LONG *)g_rl_4d4764, 0);
}

/* Port of FUN_00499d7d (26 B) — load the effect bank: the 12 WAVs from click3.wav (DAT_004daf00, 0x425e4b). */
int rl_499d7d(void) { return rl_425e4b((int)(uintptr_t)RS(g_rl_4daf00, 0x4daf00u, 0x4daf00u), 12); }

/* Port of FUN_00499aca (487 B) — the scroll music (stream 0, looping) by the human's standing: DAT_005533f0 = 0; with
 * scroll2.wav present (0x4af841) or single-player, rank +0x29 <= 1 → scroll1, else by +0x60: <= 7 scroll1, <= 14
 * scroll2, <= 28 scroll3, <= 42 scroll4, else scroll5; a multiplayer install without it: scroll1 / +0x60 <= 33 scroll1,
 * else scroll4. Returns 0x4263ad's. */
int rl_499aca(void)
{
    const int p = (int)s_slot_0057c8cc;
#define SCR(va) rl_4263ad((int)(uintptr_t)RS(g_rl_4dbff8, 0x4dbff8u, va), 0, 1)
    g_combat_audio_latch = 0;
    if (rl_4af841((int)(uintptr_t)RS(g_rl_4dbff8, 0x4dbff8u, 0x4dbff8u)) != 0 || g_sim_mp_active == 0) {
        uint8_t rank = g_player_strategic_352[352 * p + 0x29], lvl = g_player_strategic_352[352 * p + 0x60];
        if (rank <= 1) return SCR(0x4dc028u);
        if (lvl <= 7) return SCR(0x4dc034u);
        if (lvl <= 14) return SCR(0x4dc040u);
        if (lvl <= 28) return SCR(0x4dc04cu);
        if (lvl <= 42) return SCR(0x4dc058u);
        return SCR(0x4dc064u);
    }
    if (g_player_strategic_352[352 * p + 0x29] <= 1) return SCR(0x4dc004u);
    if (g_player_strategic_352[352 * p + 0x60] <= 33) return SCR(0x4dc010u);
    return SCR(0x4dc01cu);
#undef SCR
}

/* ---------------------------------------------------------------- movies (smackw32) + the palette */

#define SMK      g_rl_4d4d58                                          /* DAT_004d4d58: the open Smack* */
#define SMKF(o)  (*(volatile uint32_t *)(uintptr_t)((uint32_t)SMK + (o)))   /* a field of the Smack struct */
#define BACKBUF  ((uint32_t)(uintptr_t)g_gfx_canvas_pixels)            /* Buf DAT_004ea1a8: the back buffer's pixels */
#define DDSURF   AT(uint32_t, g_rl_4eabd0, 0x4eabd0u, 0x4eabd0u)       /* DAT_004eabd0: the DirectDraw surface */
#define DD_PITCH   AT(uint32_t, g_rl_5169e0, 0x5169e0u, 0x5169f0u)    /* its DDSURFACEDESC DAT_005169e0: lPitch */
#define DD_SURFACE AT(uint32_t, g_rl_5169e0, 0x5169e0u, 0x516a04u)    /* lpSurface */
typedef HRESULT (WINAPI *dd_lock_t)(uint32_t s, void *rect, void *desc, uint32_t fl, void *ev);
typedef HRESULT (WINAPI *dd_unlock_t)(uint32_t s, void *p);
typedef HRESULT (WINAPI *dd_restore_t)(uint32_t s);
typedef HRESULT (WINAPI *dd_setentries_t)(uint32_t pal, uint32_t fl, uint32_t start, uint32_t n, void *e);

/* Port of FUN_0042e320 (66 B) — once (DAT_005169c0): hand Smacker the DirectSound object (SmackSoundUseDirectSound
 * ppDS). Returns 1. */
int rl_42e320(void)
{
    if (g_rl_5169c0 != 0)
        return 1;
    rl_plat_SmackSoundUseDirectSound(PPDS);
    g_rl_5169c0 = 1;
    return 1;
}

/* Port of FUN_0042e298 (136 B) — the movie's palette (Smack +0x6c: 256 RGB triplets) into DAT_004ea1b0. Returns
 * retail's EAX (the last triplet's address bits with its blue byte). */
int rl_42e298(void)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)((uint32_t)SMK + 0x6c);
    uint32_t r = 0;
    for (int i = 0; i < 256; i++) {
        uint8_t red = *p++;
        uint8_t green = *p++;
        r = (uint32_t)(uintptr_t)p;
        uint8_t blue = *p++;
        g_rl_4ea1b0[3 * i] = red;
        g_rl_4ea1b0[3 * i + 1] = green;
        g_rl_4ea1b0[3 * i + 2] = blue;
        r = (r & 0xffffff00u) | blue;
    }
    return (int)r;
}

/* Port of FUN_0042f58c (130 B) — push the palette to the DirectDraw palette DAT_004df288 (SetEntries 0..255): the
 * fullscreen table (0x42f306 → DAT_005165b0) when DAT_0053f228, else the windowed one (0x42f449 → DAT_00515d90).
 * The (first, last) arguments are unused. Returns SetEntries' HRESULT. */
int rl_42f58c(int first, int last)
{
    (void)first; (void)last;
    if (g_rl_53f228 != 0)
        rl_42f306();   /* (rl ui4) */
    else
        engine_retail_call(0x42f449u, 0);   /* TODO(rl-extern) */
    void *e = g_rl_53f228 != 0 ? (void *)&g_rl_5165b0 : (void *)&g_rl_515d90;
    return (int)((dd_setentries_t)VT(g_rl_4df288, 0x18))((uint32_t)g_rl_4df288, 0, 0, 256, e);
}

/* Port of FUN_0042e362 (31 B) — a movie palette change: copy it (0x42e298), push it (0x42f58c 0..255). */
int rl_42e362(void)
{
    rl_42e298();
    return rl_42f58c(0, 255);
}

/* the movie's directory: 1 = here, 2 = the install tree (0x4af312), 0 = the CD tree (0x4af116) */
static void movie_chdir(void)
{
    if (g_rl_5169bc == 0)
        rl_4af116((int)(uintptr_t)g_rl_5169d0);
    else if (g_rl_5169bc == 2)
        rl_4af312((int)(uintptr_t)g_rl_5169d0);
}

/* Port of FUN_0042dbc7 (873 B) — one movie step (the frame driver, and the first frame at open): nothing unless a
 * movie runs (DAT_00516a4c) — 0 — or while SmackWait says wait — 0. In the movie's directory: a palette change
 * (+0x68) is pushed (0x42e362); intro.smk reports its frame (0x41a166). With DirectDraw (DAT_004df28c) the surface
 * DAT_004eabd0 is locked (DDLOCK_WAIT; a lost one restored — then drawn WITHOUT a new lock, quirk — a failure logged
 * and DirectDraw use dropped), the frame decoded into it (SmackToBuffer at (DAT_00516a50, DAT_00516a54), pitch
 * DAT_005169f0) and SmackDoFrame, unlocked; a whole-screen frame (SmackToBufferRect, the rect = the movie) notes the
 * next frame in DAT_005169b8. Without it: SmackDoFrame, then (not the last frame) the next decoded into the back
 * buffer and its 20 × 10 tiles marked dirty. Then SmackNextFrame, or at the last frame SmackClose, "natural end"
 * logged and 0x42e060(0). Back to the launch directory. Returns 1. */
int rl_42dbc7(int first)
{
    (void)first;
    if (g_rl_516a4c == 0)
        return 0;
    rl_4b0aaa(250);
    if (rl_plat_SmackWait((uint32_t)SMK) != 0)
        return 0;
    movie_chdir();
    if (SMKF(0x68) != 0)
        rl_42e362();
    if (rl_strcmp((const char *)g_rl_5169d0, RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d5028u)) == 0)   /* "intro.smk" */
        engine_retail_call(0x41a166u, 1, SMKF(0x374));   /* TODO(rl-extern) */
    if (g_rl_4df28c != 0) {
        memset(g_rl_5169e0, 0, 0x6c);
        AT(uint32_t, g_rl_5169e0, 0x5169e0u, 0x5169e0u) = 0x6c;   /* DDSURFACEDESC.dwSize */
        HRESULT hr = ((dd_lock_t)VT(DDSURF, 0x64))(DDSURF, NULL, g_rl_5169e0, 1, NULL);
        for (;;) {
            if (hr == (HRESULT)0x887601c2) {   /* DDERR_SURFACELOST */
                hr = ((dd_restore_t)VT(DDSURF, 0x6c))(DDSURF);
                if (hr != 0)
                    break;
                continue;   /* hr = 0: drawn as if locked */
            }
            if (hr == 0) {
                rl_plat_SmackToBuffer((uint32_t)SMK, (uint32_t)g_rl_516a50, (uint32_t)g_rl_516a54, DD_PITCH,
                                      SMKF(8), DD_SURFACE, 0);
                rl_plat_SmackDoFrame((uint32_t)SMK);
                ((dd_unlock_t)VT(DDSURF, 0x80))(DDSURF, (void *)(uintptr_t)DD_SURFACE);
                if (rl_plat_SmackToBufferRect((uint32_t)SMK, 1) != 0 && SMKF(0x378) == 0 && SMKF(0x37c) == 0 &&
                    SMKF(4) == SMKF(0x380) && SMKF(0x384) == SMKF(8))
                    g_rl_5169b8 = (int32_t)(SMKF(0x374) + 1);
            } else {
                int e = (int)engine_retail_call(0x42f60eu, 1, (uint32_t)hr);   /* TODO(rl-extern): the DD error's name */
                rl_4afab9((int)(uintptr_t)RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d5034u), 0, e + 1);   /* "ERR:DD surface lock error:" */
                g_rl_4df28c = 0;
            }
            break;
        }
    } else {
        rl_plat_SmackDoFrame((uint32_t)SMK);
        if (SMKF(0xc) - 1 > SMKF(0x374)) {
            rl_plat_SmackToBuffer((uint32_t)SMK, (uint32_t)g_rl_516a50, (uint32_t)g_rl_516a54, 640, 480, BACKBUF, 0);
            engine_dirty_rect_mark_box_tiled(engine_territory_dirty_state(), g_rl_516a50, g_rl_516a54, 20, 10, 1);
        }
    }
    if (SMKF(0xc) - 1 > SMKF(0x374)) {
        rl_plat_SmackNextFrame((uint32_t)SMK);
        chdir_base();
        return 1;
    }
    if (SMK != 0) {
        rl_plat_SmackClose((uint32_t)SMK);
        SMK = 0;
    }
    chdir_base();
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d5050u), (int)(uintptr_t)g_rl_5169d0, 0);   /* "OK :SMK natural end of " */
    int m = rl_4b2575();
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d5068u), 0, m + 1);   /* "MEM:Max alloc after = " */
    engine_retail_call(0x42e060u, 1, 0u);   /* TODO(rl-extern) */
    return 1;
}

/* Port of FUN_0042da18 (431 B) — start movie `name` at (x, y) (`mode` 1: SMACKNOSKIP): Smacker gets DirectSound
 * (0x42e320; failure: 0), "starting" logged, its directory found (0x4af841 → DAT_005169bc) and entered, SmackOpen
 * (SMACKTRACK1 with DirectSound; -1 = no extra buffer). Opened: every sheet freed (0x49915e / 0x499366 / 0x49964c), the
 * screen marked (0x451f70), DAT_00516a4c = 1, the first frame decoded into the back buffer and stepped (0x42dbc7)
 * — 1. Not opened (rhost without --movies): logged with the free memory — 0. Back to the launch directory. */
int rl_42da18(int name, int x, int y, int mode)
{
    g_rl_516a4c = 0;
    if (rl_42e320() == 0)
        return 0;
    rl_4afab9((int)(uintptr_t)RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d4fe0u), name, 0);   /* "OK :SMK starting smack " */
    SMK = 0;
    g_rl_516a50 = x;
    g_rl_516a54 = y;
    g_rl_5169b4 = mode;
    strutil_bounded_copy(g_rl_5169d0, PTR(const void *, name), 16);
    g_rl_5169bc = rl_4af841((int)(uintptr_t)g_rl_5169d0);
    movie_chdir();
    SMK = 0;
    uint32_t fl = 0;
    if (PPDS != 0) {
        fl = 0x2000;   /* SMACKTRACK1 */
        if (mode == 1)
            fl |= 0x400;   /* SMACKNOSKIP */
    }
    SMK = (int32_t)rl_plat_SmackOpen((const char *)g_rl_5169d0, fl, 0xffffffffu);
    if (SMK == 0) {
        chdir_base();
        rl_4afab9((int)(uintptr_t)RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d4ff8u), (int)(uintptr_t)g_rl_5169d0, 0);   /* "ERR:SMK Could not open " */
        int m = rl_4b2575();
        rl_4afab9((int)(uintptr_t)RS(g_rl_4d4fe0, 0x4d4fe0u, 0x4d5010u), 0, m + 1);   /* "MEM:Max alloc after = " */
        return 0;
    }
    rl_49915e();
    rl_499366();
    rl_49964c();
    rl_451f70();
    g_rl_516a4c = 1;
    rl_plat_SmackToBuffer((uint32_t)SMK, (uint32_t)g_rl_516a50, (uint32_t)g_rl_516a54, 640, 480, BACKBUF, 0);
    rl_42dbc7(1);
    chdir_base();
    return 1;
}

/* ---------------------------------------------------------------- new game / the map list / the MP pump */

/* Port of FUN_0046a101 (165 B) — the custom maps present: map01.pl8 .. map15.pl8 (DAT_004d6af8, 16 B apart), each
 * probed four times (index i / 4 for i = 0..59, quirk); DAT_00554018 counts the hits, DAT_0050a460 lists their i. */
int rl_46a101(void)
{
    int i, n = 0;
    for (i = 0; i < 60; i++)
        g_rl_50a460[i] = 0;
    g_rl_554018 = 0;
    for (i = 0; i < 60; i++) {
        if (rl_4af767((int)(uintptr_t)RS(g_rl_4d6af8, 0x4d6af8u, 0x4d6af8u + 16 * (i / 4))) != 0) {
            g_rl_554018++;
            g_rl_50a460[n++] = (uint8_t)i;
        }
    }
    return 0;
}

/* Port of FUN_00497a34 (697 B) — enter the new-game setup (`fresh` 0: from the title): every sheet freed; a game in
 * progress (DAT_0057a0f0 == 1) only restarts the setup music (setup3.wav) and 0x42b780. Else the system sheet
 * (0x499a1c), the custom map list (0x46a101), the setup globals reset, the strategic map built (0x46a037 / 0x467770 /
 * 0x4298c1), the player count clamped (0x4ae5e2), the NPC schedule (0x433cc1), the map (0x498270); unless DAT_005aeb68,
 * setup.wav (its latch off then on), the welcome voice once (DAT_0055ce90: s011_01.wav), the screen marked, engine
 * state 2, the misc sheet (0x499034); multiplayer: screen 0x1f + the name field (0x4b756b, text input over DAT_0053f1e0
 * 16 chars at 192); single: screen 0x3a with startXXX.pl8 present (DAT_0057c920 = 0x1f) else 0x1f; the title scene
 * (0x41e7e1) and the setup choices cleared. Returns retail's EAX (5 on the full path). */
int rl_497a34(int fresh)
{
    int r;
    rl_49915e();
    rl_499366();
    rl_49964c();
    if (s_slot_0057a0f0 == 1) {
        rl_4263ad((int)(uintptr_t)RS(g_rl_4dbc2c, 0x4dbc2cu, 0x4dbc2cu), 0, 1);   /* "setup3.wav" */
        r = rl_42b780();   /* partition skirmish */
        g_rl_5c9274 = 0;
        return r;
    }
    rl_499a1c(0);   /* gfx_load_system_sheet_select (rl turn) */
    s_slot_0053f034 = 0;
    rl_46a101();
    g_rl_53e90c = 0;
    g_rl_57cacc = 0;
    s_slot_0053f64c = 0;
    s_slot_0053e9a8 = 0;
    s_slot_0053f0d4 = 10;
    s_slot_0053e91c = 0;
    g_rl_57c96c = 0;
    s_slot_00522f60 = 0;
    s_slot_0056d590 = 0;
    g_rl_553ef8 = 0;
    s_slot_0057a0f0 = 0;
    g_rl_5aeb8c = 0;
    rl_46a037((int)s_slot_0053f034);
    rl_467770((int)s_slot_0053f034);
    rl_4298c1(0);                                        /* engine_map_init_build_cells (rl ui4) */
    s_slot_0053f268 = 4;
    engine_retail_call(0x4ae5e2u, 1, s_slot_0053e8a4);   /* TODO(rl-link): engine_title_render_setup_clamp_player_count */
    rl_433cc1();
    rl_498270();                                         /* engine_map_init_strategic (rl turn) */
    r = g_gfx_last_error;   /* DAT_005aeb68 */
    if (g_gfx_last_error != 0)
        return r;
    g_combat_audio_latch = 0;
    rl_4263ad((int)(uintptr_t)RS(g_rl_4dbc2c, 0x4dbc2cu, 0x4dbc38u), 0, 1);   /* "setup.wav" */
    g_combat_audio_latch = 1;
    if (g_rl_55ce90 != 0) {
        rl_427990((int)(uintptr_t)RS(g_rl_4dbc2c, 0x4dbc2cu, 0x4dbc44u), 1, 0);   /* "s011_01.wav" */
        g_rl_55ce90 = 0;
    }
    engine_territory_mark_full_screen_dirty();
    g_engine_state = 2;
    rl_499034();   /* gfx_load_misc_sheet (rl turn) */
    if (g_sim_mp_active != 0) {
        engine_set_current_screen(0x1f);
        engine_retail_call(0x4b756bu, 0);   /* TODO(rl-extern) */
        g_rl_57c940 = 0;
        g_rl_55ce74 = (int32_t)rl_plat_timeGetTime();
        g_rl_5530f0 = 4;
        rl_402009((int)(uintptr_t)g_bf_53f1e0, 16, 192, 0);                                   /* text_input_init (rl ui4) */
        rl_401f49();                                                                         /* text_input_recompute_length (rl ui4) */
    } else {
        if (rl_4af767((int)(uintptr_t)RS(g_rl_4dbc2c, 0x4dbc2cu, 0x4dbc50u)) != 0) {   /* "startXXX.pl8" */
            engine_set_current_screen(0x3a);
            g_rl_57c920 = 0x1f;
        } else {
            engine_set_current_screen(0x1f);
        }
        g_rl_5530f0 = 1;
    }
    r = (int)engine_retail_call(0x41e7e1u, 0);   /* TODO(rl-link): engine_title_render_scene */
    g_rl_567d00 = 0;
    g_rl_53f038 = 10;
    g_rl_5c9274 = 0;
    if (g_sim_mp_active == 0) {
        for (int i = 0; i < 6; i++) {
            r = i;
            g_rl_57cb40[i] = 0;
        }
    }
    if (fresh == 0)
        g_rl_57cb40[1] = 1;
    for (int j = 1; j < 6; j++) {
        r = j;
        g_rl_5530e0[j] = 0;
    }
    return r;
}

/* ---------------------------------------------------------------- multiplayer: the message pump */

/* the SierraNW object's methods are __thiscall (ECX = the object): engine_retail_call is cdecl, so it calls this
 * cdecl adapter, which makes the thiscall (rhost: bracketed like any retail call; the standalone stops) */
typedef uint32_t (__attribute__((thiscall)) *tc0_t)(uint32_t self);
typedef uint32_t (__attribute__((thiscall)) *tc1_t)(uint32_t self, uint32_t a);
typedef uint32_t (__attribute__((thiscall)) *tc3_t)(uint32_t self, uint32_t a, uint32_t b, uint32_t c);
static uint32_t __cdecl thiscall_adapter(uint32_t fn, uint32_t self, uint32_t n, uint32_t a, uint32_t b, uint32_t c)
{
    if (n == 0) return ((tc0_t)(uintptr_t)fn)(self);
    if (n == 1) return ((tc1_t)(uintptr_t)fn)(self, a);
    return ((tc3_t)(uintptr_t)fn)(self, a, b, c);
}
static uint32_t nw_call(uint32_t va, uint32_t self, uint32_t n, uint32_t a, uint32_t b, uint32_t c)
{   /* TODO(rl-extern): the SierraNW methods 0x4b6a15 (Close) / 0x4b92a0 (delete); the others: rl_mpnet.c */
    return engine_retail_call((uint32_t)(uintptr_t)thiscall_adapter, 6, va, self, n, a, b, c);
}
static void mp_event(int kind, int arg) { rl_43e6a2(kind, arg); }   /* the DirectPlay system events (rl_mpmsg3.c) */
#define NW   g_rl_4e2a30                                            /* DAT_004e2a30: the SierraNW session */
#define DPLAY AT(uint32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a2cu)     /* DAT_004e2a2c: the IDirectPlay */
#define SELF_ID AT(int32_t, g_rl_4e2a28, 0x4e2a28u, 0x4e2a28u)    /* DAT_004e2a28: our player id */
typedef HRESULT (WINAPI *dp_receive_t)(uint32_t dp, int32_t *from, int32_t *to, uint32_t fl, void *buf, uint32_t *n);
#define DPS(va) ((int)(uintptr_t)RS(g_rl_4e2eb0, 0x4e2eb0u, va))

/* Port of FUN_004b8c3a (1217 B) — multiplayer only (DAT_00553030): the tick gap (GetTickCount since DAT_0053e998, its
 * maximum in DAT_0053f05c), then every waiting message: from DirectPlay (Receive, DPRECEIVE_ALL, into DAT_00515c50,
 * 0x104 B) or the SierraNW session (gone: shut down — the MP state reset, screen 0x1f, a full redraw — 0; the
 * players flagged 1 announced (0x43e6a2(2, i + 1)) and, all present, the start (0x43e6a2(4, 0)); a message read with
 * its sender + 1). A system message (sender 0; SierraNW: DebugBreak) by its type: 3 / 5 (+8 the player) → events 1 /
 * 2, 0x31 → 3, 0x101 → 4, 7 → 5, 0x21 → 6, 0x102 → 7, 0x103 → 8; our own ignored (logged); a player's: 0x43eac1.
 * Stops at DPERR_NOMESSAGES; other errors logged. Returns 0. */
int rl_4b8c3a(void)
{
    if (g_sim_mp_active == 0)
        return 0;
    uint32_t t = rl_plat_GetTickCount();
    int32_t dt = (int32_t)(t - (uint32_t)g_rl_53e998);
    g_rl_53e998 = (int32_t)t;
    if (g_rl_53f05c < dt)
        g_rl_53f05c = dt;
    for (;;) {
        int32_t from, to;
        uint32_t size;
        HRESULT hr;
        if (DPLAY != 0) {
            size = 0x104;
            hr = ((dp_receive_t)VT(DPLAY, 0x64))(DPLAY, &from, &to, 1, g_rl_515c50, &size);
        } else {
            if (NW == 0)
                return 0;
            if (rl_4b6d44((uint32_t)NW) == 0) {
                rl_plat_OutputDebugStringA(RS(g_rl_4e2eb0, 0x4e2eb0u, 0x4e2eb0u));   /* "RGM SIGS disconnected, shut down\n" */
                engine_retail_call(0x43f43eu, 0);   /* TODO(rl-extern) */
                g_rl_568464 = 0;
                nw_call(0x4b6a15u, (uint32_t)NW, 0, 0, 0, 0);
                if (NW != 0)
                    nw_call(0x4b92a0u, (uint32_t)NW, 1, 1, 0, 0);
                NW = 0;
                g_sim_mp_active = 0;
                engine_set_current_screen(0x1f);
                g_rl_5530f0 = 1;
                engine_set_demand_redraw(2);
                return 0;
            }
            int any = 0;
            for (int i = 0; i < 5; i++) {
                if (g_rl_4e5a90[i] == 1) {
                    mp_event(2, i + 1);
                    g_rl_4e5a90[i] = 2;
                    any = 1;
                }
            }
            if (any == 1 && g_rl_553248 == 0) {
                int all = 1;
                int n = rl_4b6e1b((uint32_t)NW);
                for (int j = 0; n > j; j++)
                    if (g_rl_4e5a90[j] == 0) {
                        all = 0;
                        break;
                    }
                if (all == 1)
                    mp_event(4, 0);
            }
            to = 0;
            size = (uint32_t)rl_4b6b71((uint32_t)NW, (int)(uintptr_t)g_rl_515c50, 0x104, (int)(uintptr_t)&from);
            hr = size != 0 ? 0 : (HRESULT)0x887700be;
            from++;
        }
        switch ((uint32_t)hr) {
        case 0x80070057u: rl_4afab9(DPS(0x4e2f10u), 0, 0); return 0;            /* "ERR:DP bad receive - invalid params" */
        case 0x80004005u: rl_4afab9(DPS(0x4e2f58u), 0, 0); return 0;            /* "… generic" */
        case 0x88770082u: rl_4afab9(DPS(0x4e2eecu), 0, 0); return 0;            /* "… invalid object" */
        case 0x8877001eu: rl_4afab9(DPS(0x4e2f34u), 0, (int)size + 1); return 0; /* "… buff too small" */
        case 0x887700beu: return 0;                                              /* DPERR_NOMESSAGES */
        case 0:
            break;
        default: rl_4afab9(DPS(0x4e2f78u), 0, 0); return 0;                     /* "… error in error rep" */
        }
        if (from == 0) {
            if (NW != 0)
                rl_plat_DebugBreak();
            int32_t type = *(int32_t *)g_rl_515c50;
            int32_t who = *(int32_t *)(g_rl_515c50 + 8);
            switch (type) {
            case 3: mp_event(1, who); break;
            case 5: mp_event(2, who); break;
            case 0x31: mp_event(3, 0); break;
            case 0x101: mp_event(4, 0); break;
            case 7: mp_event(5, 0); break;
            case 0x21: mp_event(6, 0); break;
            case 0x102: mp_event(7, 0); break;
            case 0x103: mp_event(8, 0); break;
            default: break;
            }
        } else if (from == SELF_ID) {
            rl_4afab9(DPS(0x4e2ed4u), 0, from + 1);   /* "OK :DP ignore self mess" */
        } else {
            engine_retail_call(0x43eac1u, 1, (uint32_t)from);   /* TODO(rl-extern) */
        }
    }
}
