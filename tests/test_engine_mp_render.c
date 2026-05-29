/*
 * Unit tests for engine_mp_render — the two MP render-phase overlays
 * (ports of FUN_0041f15a and FUN_00423b4f).
 *
 * The valuable logic is the player-indicator's timer/gating.  We bind a
 * NULL sprite asset so gfx_sprite_render_from_blob trips its NULL gate
 * and fires the log hook — letting us detect "a render was attempted"
 * via a counter, without staging a full sprite blob or screen-sized
 * canvas.  Pixel-level sprite/text rendering is covered by the
 * gfx_sprite_render / gfx_wrap suites.
 */

#include "t.h"
#include "gfx_paint.h"
#include "gfx_text.h"
#include "gfx_sprite_render.h"
#include "localization.h"
#include "engine_mp_render.h"

#include <string.h>

static int g_log_calls;
static void log_counter(uint32_t fmt_addr, int idx_plus_one)
{
    (void)fmt_addr; (void)idx_plus_one;
    g_log_calls++;
}

/* Small canvas so the status-line text render has a valid dest (its
 * coords are off-canvas → clipped, which is fine). */
static uint8_t g_canvas[64 * 64];
static unsigned char g_loc[LOCALIZATION_BLOB_SIZE + 16];
static uint8_t g_font[512];
static uint8_t g_table[256];

static void mp_reset(void)
{
    g_log_calls = 0;
    memset(g_canvas, 0, sizeof g_canvas);
    gfx_paint_bind_canvas(g_canvas, 64, 64);
    gfx_sprite_render_set_log(log_counter);
    gfx_sprite_render_clear_error();
    gfx_text_set_no_shadow(0);

    /* Minimal loc blob: entry 0x20 → a short string at offset 100. */
    memset(g_loc, 0, sizeof g_loc);
    g_loc[8 + 0x20 * 4] = 100;
    memcpy(g_loc + 100, "X", 2);
    memset(g_font, 0, sizeof g_font);
    memset(g_table, 0, 256);

    engine_mp_render_bind_menu_asset(NULL);    /* NULL → render trips log */
    engine_mp_render_bind_status_asset(NULL);
    engine_mp_render_bind_text(g_font, &g_table[0x20], g_loc);
}

/* ── player indicator (FUN_0041f15a) ─────────────────────────────── */

int test_mp_indicator_renders_and_keeps_timer_before_10s(void)
{
    mp_reset();
    int timer = 1000;
    engine_mp_render_player_indicator(/*mp_active=*/1, &timer,
                                      /*now_ms=*/2000, /*slot=*/1);
    T_ASSERT_EQ_I(timer, 1000);       /* 1000 ms elapsed < 10 s → kept */
    T_ASSERT_EQ_I(g_log_calls, 1);    /* render attempted (NULL asset) */
    return 0;
}

int test_mp_indicator_clears_timer_after_10s_still_renders(void)
{
    mp_reset();
    int timer = 1000;
    engine_mp_render_player_indicator(1, &timer, /*now_ms=*/12000, 2);
    T_ASSERT_EQ_I(timer, 0);          /* 11 s elapsed > 10 s → cleared */
    T_ASSERT_EQ_I(g_log_calls, 1);    /* blit still happens this frame */
    return 0;
}

int test_mp_indicator_inactive_no_render_no_clear(void)
{
    mp_reset();
    int timer = 1000;
    engine_mp_render_player_indicator(/*mp_active=*/0, &timer, 99999, 1);
    T_ASSERT_EQ_I(timer, 1000);
    T_ASSERT_EQ_I(g_log_calls, 0);    /* gated out → no render */
    return 0;
}

int test_mp_indicator_zero_timer_no_render(void)
{
    mp_reset();
    int timer = 0;
    engine_mp_render_player_indicator(1, &timer, 99999, 1);
    T_ASSERT_EQ_I(timer, 0);
    T_ASSERT_EQ_I(g_log_calls, 0);
    return 0;
}

int test_mp_indicator_exactly_10s_not_yet_cleared(void)
{
    mp_reset();
    int timer = 1000;
    /* elapsed == 10000 exactly; gate is `10000 < elapsed` (strict). */
    engine_mp_render_player_indicator(1, &timer, 11000, 1);
    T_ASSERT_EQ_I(timer, 1000);       /* not cleared at the boundary */
    T_ASSERT_EQ_I(g_log_calls, 1);
    return 0;
}

/* ── status line (FUN_00423b4f) ──────────────────────────────────── */

int test_mp_status_line_attempts_sprite_render(void)
{
    mp_reset();
    engine_mp_render_status_line();
    /* The sprite (status asset NULL) trips the log exactly once; the
     * text render is a clipped no-op on the tiny canvas. */
    T_ASSERT_EQ_I(g_log_calls, 1);
    return 0;
}

int test_mp_status_line_no_crash_runs_clean(void)
{
    mp_reset();
    engine_mp_render_status_line();
    engine_mp_render_status_line();   /* idempotent, no state corruption */
    T_ASSERT_EQ_I(g_log_calls, 2);
    return 0;
}
