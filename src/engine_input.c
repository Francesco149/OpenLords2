/*
 * Mouse-input state resolver — pure-C port of retail FUN_004b191e
 * @ 0x4b191e (981 bytes).
 *
 * Runs once per Win32-pump iteration as part of the render-phase
 * head (right after the pre-tick block + sim sub-step loop, before
 * the screen-dispatch).  Inputs are: a screen-space cursor position
 * (retail calls GetCursorPos here), a wall-clock timestamp, the
 * WndProc-mirrored button + dblclk bitmasks, and the prior frame's
 * input state.  Outputs are the resolved per-frame events the
 * UI/screen renderers consume: motion flag, button-edge flags,
 * 300 ms delayed-single-click events, double-click events, and the
 * cursor position in game-coord space.
 *
 * Pure function: every input explicit, every state mutation
 * returned in out.  No globals, no Win32, no timeGetTime — all of
 * that is in the wrapper (engine_tick_input_step in engine.c).
 *
 * Reference: docs/findings/main-loop-tick.md §"render-phase head".
 *
 * Retail global ↔ field mapping (mirrors held in engine.c):
 *
 *   GetCursorPos() result      in->cursor_screen_x, _y
 *   timeGetTime()              in->now_ms
 *   DAT_0057cac4               in->cursor_cache_enabled
 *   DAT_004eada1               in->pending_dblclk   (g_mouse_dblclk)
 *   DAT_004eabc2               in->button_state     (g_mouse_buttons)
 *   DAT_004ea180..18c          in->viewport_x0/y0/x1/y1 (g_client_screen)
 *   DAT_004eb274 / DAT_004eac4c in->viewport_game_w/h
 *   DAT_004eabbe / DAT_004eabc0 in->cached_cursor_x/y (g_mouse_x/y)
 *   DAT_004e65fc / DAT_004e65f4 in->prev_game_x/y
 *   DAT_004e65cc / DAT_004e8780 in->prev_lmb/rmb
 *   DAT_004e65e8 / DAT_004e65c4 in->lmb/rmb_timer_armed
 *   _DAT_004e59f8              in->click_timer_ms
 *   _DAT_004e59f4              in->motion_last_ms
 *   DAT_004e65f0               in->click_eligible
 *
 *   DAT_004e6594 / _98         out->raw_cursor_x/y
 *   DAT_004eabbe / _ac0        out->new_cached_cursor_x/y
 *   DAT_004ea198 / DAT_004ea190 out->prev_game_x/y_snapshot
 *   DAT_004eabbc / DAT_004eabc3 out->prev_lmb/rmb_snapshot
 *   DAT_004e65fc / DAT_004e65f4 out->game_x/y
 *   DAT_004ea4b0               out->mouse_event
 *   _DAT_004eb264              out->button_changed
 *   DAT_004eafb4               out->lmb_newly_pressed
 *   DAT_004e65d8               out->lmb_newly_released
 *   DAT_004eabe0               out->rmb_newly_pressed
 *   DAT_004e6900               out->rmb_newly_released
 *   DAT_004eabf0               out->lmb_click_fired
 *   DAT_004ea51c               out->rmb_click_fired
 *   DAT_004ea4b4               out->rmb_dblclk
 *   DAT_004eabc5               out->lmb_dblclk
 *   DAT_004e65cc / DAT_004e8780 out->new_lmb/rmb
 *   DAT_004e65e8 / DAT_004e65c4 out->new_lmb/rmb_timer_armed
 *   _DAT_004e59f8              out->new_click_timer_ms
 *   _DAT_004e59f4              out->new_motion_last_ms
 *   _DAT_004eabfc              out->motion_idle_ms
 *   DAT_004eac04 / DAT_004eac08 out->last_click_x/y
 *   DAT_004e65f0               out->new_click_eligible
 *
 *   (After return: wrapper sets DAT_004eada1 = 0 — the function's
 *    final statement.  Modeled as out->consume_pending_dblclk = 1,
 *    always true; field is informational.)
 *
 * Behaviour summary (covered by tests):
 *
 *   - GetCursorPos result is always written to raw_cursor_x/y.
 *   - Cache update (cached_cursor_x/y from screen coords) gated on
 *     cursor_cache_enabled.  When false, the WndProc-cached client-
 *     space coords carry through.
 *   - Game-coord cursor = cached_cursor * (game_extent/viewport_extent).
 *     Computed via two FUN_00404dc1/d6b helpers — both inlined as
 *     `(a*100)/b` and `(a*b)/100` with the div-by-zero clamp on the
 *     first form.
 *   - Per-frame event flags all cleared at entry.  Dblclk bits read
 *     from pending_dblclk; if set, also clear the matching pending-
 *     click timer (dblclk supersedes single-click).
 *   - Button state cached into new_lmb/rmb; edge-detected against
 *     prev_lmb/rmb (which is also snapshotted into prev_*_snapshot
 *     mirrors for downstream consumers).
 *   - 300 ms delayed-single-click: when a timer is armed and 300 ms
 *     elapsed since the timer started, fire the click_fired event +
 *     disarm the timer.  Note: retail uses `_DAT_004e59f8` for both
 *     LMB and RMB timer baselines — a single shared "last click
 *     timestamp", so press+release on EITHER button resets the
 *     300 ms window for BOTH.  Quirk preserved; tests pin it.
 *   - Motion detection: if game cursor changed, set mouse_event +
 *     reset motion timer.  Else compute idle ms since last motion.
 *   - Button edge: if state changed, set mouse_event + button_changed.
 *     On press, set lmb/rmb_newly_pressed.  On release, always set
 *     lmb/rmb_newly_released + (if click_eligible) arm the timer
 *     and record the click position.
 *   - Final step: consume the pending_dblclk bits (wrapper sets the
 *     global to 0).
 */

#include "engine.h"

/* Retail FUN_00404dc1 — `(num*100)/denom`, or 0 if denom == 0. */
static int percent_ratio(int num, int denom)
{
    if (denom == 0) return 0;
    return (num * 100) / denom;
}

/* Retail FUN_00404d6b — `(value * percent) / 100`.  No clamp. */
static int apply_percent(int value, int percent)
{
    return (value * percent) / 100;
}

void engine_compute_input_state(const engine_input_in_t *in,
                                engine_input_out_t *out)
{
    /* Raw cursor — always written. */
    out->raw_cursor_x = in->cursor_screen_x;
    out->raw_cursor_y = in->cursor_screen_y;

    /* Cache update — gated on cursor_cache_enabled.  When false, the
     * WM_MOUSEMOVE-cached client-space coords (which the WndProc
     * already wrote into cached_cursor_x/y) carry through unchanged.
     * When true, the per-frame screen-space cursor overwrites it. */
    if (in->cursor_cache_enabled) {
        out->new_cached_cursor_x = (int16_t)in->cursor_screen_x;
        out->new_cached_cursor_y = (int16_t)in->cursor_screen_y;
    } else {
        out->new_cached_cursor_x = in->cached_cursor_x;
        out->new_cached_cursor_y = in->cached_cursor_y;
    }

    /* Snapshot prior game-coord + button state into the "prev_*"
     * mirrors that downstream consumers read.  Retail does this
     * unconditionally before computing the new values. */
    out->prev_game_x_snapshot = in->prev_game_x;
    out->prev_game_y_snapshot = in->prev_game_y;
    out->prev_lmb_snapshot    = in->prev_lmb;
    out->prev_rmb_snapshot    = in->prev_rmb;

    /* New game-coord cursor — scale the cached cursor by the
     * viewport's game/screen extent ratio.  Two-step compose:
     *   percent = (game_extent * 100) / screen_extent   (clamped)
     *   game    = (cached * percent) / 100
     * Mathematically game = cached * game_extent / screen_extent,
     * but the percent-step keeps intermediate values bounded for
     * retail's int32 arithmetic.  Both helpers are FUN_00404dc1 +
     * FUN_00404d6b, inlined as static helpers above. */
    int viewport_screen_w = in->viewport_x1 - in->viewport_x0;
    int viewport_screen_h = in->viewport_y1 - in->viewport_y0;
    int scale_x = percent_ratio(in->viewport_game_w, viewport_screen_w);
    int scale_y = percent_ratio(in->viewport_game_h, viewport_screen_h);
    out->game_x = apply_percent((int)out->new_cached_cursor_x, scale_x);
    out->game_y = apply_percent((int)out->new_cached_cursor_y, scale_y);

    /* Per-frame event flags — cleared at entry.  Selectively set
     * below. */
    out->mouse_event         = 0;
    out->button_changed      = 0;
    out->lmb_newly_pressed   = 0;
    out->lmb_newly_released  = 0;
    out->rmb_newly_pressed   = 0;
    out->rmb_newly_released  = 0;
    out->lmb_click_fired     = 0;
    out->rmb_click_fired     = 0;

    /* Carry through state that's only conditionally written below. */
    out->new_lmb_timer_armed = in->lmb_timer_armed;
    out->new_rmb_timer_armed = in->rmb_timer_armed;
    out->new_click_timer_ms  = in->click_timer_ms;
    out->new_motion_last_ms  = in->motion_last_ms;
    out->new_click_eligible  = in->click_eligible;
    out->last_click_x        = in->prev_last_click_x;   /* carried through;
                                     * overwritten only inside the
                                     * click-armed release branches */
    out->last_click_y        = in->prev_last_click_y;

    /* Pending double-clicks — bit 0 = LMB, bit 1 = RMB.  WndProc sets
     * these on WM_*BUTTONDBLCLK; this function consumes them.  If a
     * dblclk fired, clear the matching pending-single-click timer
     * (dblclk supersedes the delayed single click). */
    out->rmb_dblclk = (in->pending_dblclk & 0x02) ? 1 : 0;
    if (out->rmb_dblclk) {
        out->new_rmb_timer_armed = 0;
    }
    out->lmb_dblclk = (in->pending_dblclk & 0x01) ? 1 : 0;
    if (out->lmb_dblclk) {
        out->new_lmb_timer_armed = 0;
    }

    /* New cached button state — bit 0 = LMB, bit 1 = RMB.  WndProc
     * maintains the bitmask in pending_button_state (g_mouse_buttons);
     * this step extracts the two bits into the "bool" mirrors that
     * the rest of the function and downstream renderers use. */
    int new_lmb = (in->button_state & 0x01) ? 1 : 0;
    int new_rmb = (in->button_state & 0x02) ? 1 : 0;
    out->new_lmb = new_lmb;
    out->new_rmb = new_rmb;

    /* 300 ms delayed-single-click expiry.  Retail uses a SHARED
     * timer baseline (_DAT_004e59f8) for both LMB and RMB — press
     * either button + release, then 300 ms later the single-click
     * fires for whichever timer was armed.  If both timers happen
     * to be armed at the same time (clicked LMB then RMB within
     * 300 ms with no dblclk between), both fire on the same call. */
    if (out->new_rmb_timer_armed != 0 &&
        300 < (int32_t)(in->now_ms - in->click_timer_ms)) {
        out->new_rmb_timer_armed = 0;
        out->rmb_click_fired     = 1;
    }
    if (out->new_lmb_timer_armed != 0 &&
        300 < (int32_t)(in->now_ms - in->click_timer_ms)) {
        out->new_lmb_timer_armed = 0;
        out->lmb_click_fired     = 1;
    }

    /* Motion detection — compare new game-coord cursor against the
     * snapshot of the previous frame's game-coord cursor. */
    if (out->prev_game_x_snapshot != out->game_x) {
        out->mouse_event = 1;
    }
    if (out->prev_game_y_snapshot != out->game_y) {
        out->mouse_event = 1;
    }
    if (out->mouse_event == 0) {
        out->motion_idle_ms = in->now_ms - in->motion_last_ms;
        /* motion_last_ms unchanged */
    } else {
        out->new_motion_last_ms = in->now_ms;
        out->motion_idle_ms     = 0;
    }

    /* LMB edge.  Compare snapshot (= prior frame's resolved LMB) to
     * the current bit.  On press: set lmb_newly_pressed.  On release:
     * always set lmb_newly_released; if click_eligible, also arm
     * the timer and record the click position. */
    if (out->prev_lmb_snapshot != new_lmb) {
        out->mouse_event    = 1;
        out->button_changed = 1;
        if (new_lmb) {
            out->lmb_newly_pressed = 1;
        } else {
            /* Retail expresses this as `(DAT_004e65d8 = 1, DAT_004e65f0 != 0)`
             * — the lmb_newly_released latch is set UNCONDITIONALLY on
             * release; the click-arming body runs only if click_eligible. */
            out->lmb_newly_released = 1;
            if (in->click_eligible) {
                out->last_click_x        = out->game_x;
                out->new_click_eligible  = 0;
                out->new_lmb_timer_armed = 1;
                out->new_click_timer_ms  = in->now_ms;
                out->last_click_y        = out->game_y;
            }
        }
    }

    /* RMB edge — mirror of LMB block. */
    if (out->prev_rmb_snapshot != new_rmb) {
        out->mouse_event    = 1;
        out->button_changed = 1;
        if (new_rmb) {
            out->rmb_newly_pressed = 1;
        } else {
            out->rmb_newly_released = 1;
            if (out->new_click_eligible) {
                out->last_click_x        = out->game_x;
                out->new_click_eligible  = 0;
                out->new_rmb_timer_armed = 1;
                out->new_click_timer_ms  = in->now_ms;
                out->last_click_y        = out->game_y;
            }
        }
    }

    /* Retail's final statement: `DAT_004eada1 = 0` — consume the
     * pending dblclk bits.  We model this implicitly: the wrapper
     * clears g_mouse_dblclk after every call (matches retail's
     * unconditional clear, regardless of whether dblclk fired). */
}
