/* rl_quit.c — a retail-layout port, partition 'quit' (tools/bf/parts.py): the game's exit — WM_DESTROY's movie /
 * sound teardown, WinMain's exit (lords2.inf, the sprite frees), the DirectDraw / DirectSound / DirectPlay releases,
 * the exit log. Reached by tests/scenarios/game-quit (File > Quit > yes). Retail semantics on retail's layout: rl.h. */
#include "rl.h"
#include "rl_quit_globals.h"
#include "rl_io_globals.h"
/* other partitions' globals (their headers conflict with each other's widths: declared here as they declare them) */
extern int32_t g_rl_4e6974;                  /* rl_glue_globals.h: IDirectDraw DAT_004e6974 */
extern int32_t g_rl_5158e0, g_rl_5158e4;     /* rl_ui2_globals.h: the two fixed sprite blocks */
extern int32_t g_rl_554038;                  /* rl_render_globals.h: the net-play divergence count */
#include "rl_io_plat.h"
#include "engine_title_render.h"
#include "gfx_globals.h"

/* a COM call through the object's vtable (slot k: +4k; 2 = Release) */
typedef uint32_t (__stdcall *rl_com_fn)(uint32_t self);
static uint32_t com_call(uint32_t obj, int k)
{
    return ((rl_com_fn)(uintptr_t)((const uint32_t *)(uintptr_t)*(const uint32_t *)(uintptr_t)obj)[k])(obj);
}

/* Port of FUN_0042dfe2 (126 B) — a movie screen's teardown (DAT_00516a4c set): the movie's sprite / palette frees by its
 * kind DAT_005169bc (0: 0x4af116, 2: 0x4af312, on the movie name 0x5169d0), the open SMACK DAT_004d4d58 closed
 * (smackw32 #18), the base dir back (0x4af502). Callers read no return value. */
int rl_42dfe2(void)
{
    if (g_rl_516a4c == 0)
        return 0;
    if (g_rl_5169bc == 0)
        rl_4af116((int)(uintptr_t)g_rl_5169d0);
    else if (g_rl_5169bc == 2)
        rl_4af312((int)(uintptr_t)g_rl_5169d0);
    if (g_rl_4d4d58 != 0) {
        rl_plat_SmackClose((uint32_t)g_rl_4d4d58);
        g_rl_4d4d58 = 0;
    }
    return rl_4af502();
}

/* Port of FUN_00425d35 (184 B) — release DirectSound (ppDS, DAT_004d4760): stop both music streams (0x426d11(0..1)),
 * free the 20 cue slots (0x4262eb(1..20)), the cue buffer (0x427d19), IDirectSound::Release (its count →
 * DAT_00522b58), ppDS = 0, "OK :DS Released.". Returns 1. */
int rl_425d35(void)
{
    uint32_t *pds = (uint32_t *)g_bf_4d4760;
    if (*pds == 0)
        return 1;
    for (int i = 0; i < 2; i++)
        rl_426d11(i);
    for (int j = 1; j <= 20; j++)
        rl_4262eb(j);
    rl_427d19();
    g_bf_522b58 = (int32_t)com_call(*pds, 2);
    *pds = 0;
    rl_4afab9((int)(uintptr_t)g_rl_4d4800, 0, 0);
    return 1;
}

/* Port of FUN_004ae15f (89 B) — write the settings block 0x53f1e0 (0x468 B) to lords2.inf (_open O_WRONLY | O_CREAT |
 * O_TRUNC | O_BINARY = 0x8301, 0600). The block starts at the title page's name buffer (g_tr_p4_name_buf, 32 B in the
 * standalone: the 0x468-B settings block 0x53f1e0 must become ONE object there — DUP 53f1e0 — before this runs). */
int rl_4ae15f(void)
{
    int fd = rl_plat_open((const char *)g_rl_4df154, 0x8301, 0x180);
    if (fd == -1)
        return fd;
    rl_plat_write(fd, &AT(uint8_t, g_tr_p4_name_buf, 0x53f1e0u, 0x53f1e0u), 0x468u);
    return rl_plat_close(fd);
}

/* Port of FUN_0043f43e (70 B) — free the two fixed sprite blocks DAT_005158e0 / DAT_005158e4 (0x4c0580) and the rest
 * through 0x43f484. */
int rl_43f43e(void)
{
    if (g_rl_5158e0 != 0)
        rl_4c0580(g_rl_5158e0);
    if (g_rl_5158e4 != 0)
        rl_4c0580(g_rl_5158e4);
    return rl_43f484();
}

/* Port of FUN_0049767d (36 B) — WinMain's exit: lords2.inf (0x4ae15f), the sprite frees (0x43f43e), the strip /
 * palette / name reloads' frees (0x49915e, 0x499366, 0x49964c). */
int rl_49767d(void)
{
    rl_4ae15f();
    rl_43f43e();
    rl_49915e();
    rl_499366();
    return rl_49964c();
}

/* Port of FUN_0042eaa6 (214 B) — release the three DirectDraw surfaces DAT_004ea12c / 004eafac / 004eabd0, clear the
 * canvas pointer Buf (DAT_004ea1a8); with `all`, release IDirectDraw DAT_004e6974 too ("OK :DD Released."). Returns 1. */
int rl_42eaa6(int all)
{
    uint32_t *s2 = (uint32_t *)g_rl_4eabd0;
    if (g_rl_4ea12c != 0) {
        com_call((uint32_t)g_rl_4ea12c, 2);
        g_rl_4ea12c = 0;
    }
    if (g_rl_4eafac != 0) {
        com_call((uint32_t)g_rl_4eafac, 2);
        g_rl_4eafac = 0;
    }
    if (*s2 != 0) {
        com_call(*s2, 2);
        *s2 = 0;
    }
    g_gfx_canvas_pixels = NULL;
    if (all != 0 && g_rl_4e6974 != 0) {
        com_call((uint32_t)g_rl_4e6974, 2);
        g_rl_4e6974 = 0;
        rl_4afab9((int)(uintptr_t)g_rl_4d51dc, 0, 0);
    }
    return 1;
}

/* Port of FUN_004b7180 (208 B) — close the multiplayer transport (`go` nonzero): IDirectPlay DAT_004e2a2c (Close,
 * Release; "OK :DP Released."), else the serial / modem link DAT_004e2a30 (0x4b6a15, 0x4b92a0(1); "OK :SIGS
 * closed."). Returns 1 (0 when `go` is 0). */
int rl_4b7180(int go)
{
    uint32_t *dp = (uint32_t *)(g_rl_4e2a28 + 4);          /* DAT_004e2a2c */
    if (go == 0)
        return 0;
    if (*dp != 0) {
        com_call(*dp, 4);
        com_call(*dp, 2);
        *dp = 0;
        rl_4afab9((int)(uintptr_t)(g_rl_4e2a64 + 0), 0, 0);
    } else if (g_rl_4e2a30 != 0) {
        rl_4b6a15((uint32_t)g_rl_4e2a30);                 /* the session's Close (rl mpnet2; __thiscall in retail) */
        if (g_rl_4e2a30 != 0)
            rl_4b92a0((uint32_t)g_rl_4e2a30, 1);          /* its scalar deleting destructor (rl mpnet2) */
        g_rl_4e2a30 = 0;
        rl_4afab9((int)(uintptr_t)(g_rl_4e2a64 + 20), 0, 0);
    }
    return 1;
}

/* Port of FUN_004b2636 (130 B) — the exit log: the timer stop (0x4b25fc), the net-play counters DAT_00554038 /
 * DAT_00553ec8 and the average frame rate DAT_004ea0f4 / DAT_004ea83c (a zero count made 1), "OK :Exited
 * normally". */
int rl_4b2636(void)
{
    rl_4b25fc();
    if (g_rl_4ea83c == 0)
        g_rl_4ea83c = 1;
    rl_4afab9((int)(uintptr_t)(g_rl_4df5d8 + 0), 0, g_rl_554038 + 1);
    rl_4afab9((int)(uintptr_t)(g_rl_4df5d8 + 36), 0, g_rl_553ec8 + 1);
    rl_4afab9((int)(uintptr_t)(g_rl_4df5d8 + 64), 0, (int)((uint32_t)g_rl_4ea0f4 / (uint32_t)g_rl_4ea83c + 1u));
    return rl_4afab9((int)(uintptr_t)(g_rl_4df5d8 + 88), 0, 0);
}
