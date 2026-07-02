#ifndef OPENLORDS2_ENGINE_H
#define OPENLORDS2_ENGINE_H

#include <stdint.h>

/*
 * Per-tick driver + its two outer wrappers.  Ported from retail's
 * main-loop chain:
 *
 *     main loop body
 *         ↓
 *     engine_tick_guard()        ← FUN_0040e8bb @ 0x40e8bb
 *         ↓
 *     engine_tick_dispatch()     ← FUN_0040e890 @ 0x40e890
 *         ↓
 *     engine_tick()              ← FUN_004b99c0 @ 0x4b99c0 (scaffold only)
 *
 * See docs/findings/main-loop-tick.md for the full retail
 * decompile and engine-state-machine cross-reference.
 *
 * Today engine_tick() is just a scaffold — wall-clock frame counter
 * + the variable-rate sim sub-step loop (computes 0 sub-steps until
 * the engine reaches ingame state) + the rate-gated present call
 * (gfx_tick_present).  Each of the ~40 subsystem calls inside
 * retail's FUN_004b99c0 lands in its own slice as the corresponding
 * subsystem (sim, music, input, UI…) gets ported.
 */

/* Engine state machine.  Mirror of retail DAT_0052af94.
 *
 *   0     menus / pre-game
 *   2     transitioning into ingame (set by save-pump tail)
 *   3     ingame, sim ticking
 *   10    save in progress    \
 *   0xb   load in progress    } > 8 → engine_tick_dispatch latches
 *   > 8   any "fast-skip render" state — gates the FPS counter / map
 *         redraw / finalisers out of engine_tick's render tail.
 */
extern int g_engine_state;

/* Deferred-shutdown latch.  Mirror of retail DAT_005aeb68.
 *
 *   0   normal
 *   1   shutdown requested (e.g. VK_ESCAPE outside ingame).
 *       engine_tick_guard skips the dispatch when this is set;
 *       retail's main-loop tail then PostMessages WM_CLOSE.
 *   3   WM_CLOSE received, drain sim one frame then escalate to 1.
 *   4   sim asked for self-shutdown.  Consumed inside engine_tick's
 *       render tail (subsystem-teardown branch).
 *
 * Today the loop body doesn't yet PostMessage WM_CLOSE on the 1
 * transition — the WndProc's WM_CLOSE/VK_ESCAPE handlers still call
 * DestroyWindow inline as the OS-side quit path.  Once the sim that
 * can ack the deferred shutdown is ported, that inline call goes
 * away and this latch drives the dance.
 */
extern int g_shutdown_request;

/* Save/load pump request.  Mirror of retail DAT_005c9274.  Set by
 * engine_tick_dispatch when g_engine_state > 8; consumed by
 * engine_tick_guard which dispatches the SP or MP save-pump.  Both
 * pump paths are stubs today (FUN_004978ad / FUN_0049792e /
 * FUN_00497a34 are the level-load chain — many KB across most
 * subsystems).  No behavioural difference until g_engine_state can
 * actually transition above 8, which today it can't. */
extern int g_savepump_request;

/* Multiplayer-session active flag.  Mirror of retail DAT_00553030 —
 * the SP/MP master switch.  Always 0 in the drop-in today (SP only).
 * Defined in engine.c; declared here so strategic-layer modules
 * (engine_npc_schedule) read the single canonical flag.  Note: this is
 * a runtime flag, NOT a save-schema scalar — it is not persisted. */
extern int g_sim_mp_active;

/* Multiplayer session sub-variant.  Mirror of retail DAT_00553248 — set
 * to 1 by the MP message handler (message type 4, retail @0x28294 in
 * all.c) and reset to 0 on new-game init.  Pairs with g_sim_mp_active
 * across the engine (the recurring `mp_active == 0 || variant != 0`
 * guard).  In the turn-save backup chain it selects the file family:
 * variant != 0 → *.sva, variant == 0 → *.svb (see save_rotate_and_write).
 * Runtime flag, NOT persisted.  Always 0 in the SP drop-in today. */
extern int g_sim_mp_variant;

/* Turn-save backup-rotation hold counter.  Mirror of retail DAT_00553260.
 * Bumped to 2 on the MP turn-advance path (retail 0x49b973) and
 * counted back down; while >= 1 it suppresses rotation of the .sav
 * backup chain (see save_rotate_and_write — rotation fires only when
 * < 1).  Reset to 0 on new-game init, and never bumped in SP, so SP
 * always rotates.  Runtime flag, NOT persisted. */
extern int g_sim_save_rotate_hold;

/* Request a screen repaint — sets the demand-redraw cookie (retail
 * DAT_0057d340) the render-phase seam consumes.  `mode` is 1 or 2 (a
 * redraw-mode hint; the title arm ignores it).  Called by WndProc on
 * WM_MOVE / WM_ACTIVATEAPP restore, and by state handlers as they land. */
void engine_set_demand_redraw(int mode);

/* Live title-screen producer hooks (the boot→title transition
 * 0x42b780 is unported; these let a bring-up drive the engine into
 * the live title state the render seam expects).
 *
 *   engine_set_current_screen(0x1f)            — screen byte DAT_004eac50
 *   engine_set_viewport_game_extents(640,480)  — DAT_004eb274/DAT_004eac4c,
 *       so engine_compute_input_state maps the cursor 1:1 on the menu
 *   engine_bind_title_bg_reload_fn(fn)         — page-background loader
 *       called (before the canvas lock) on each full title redraw; the
 *       drop-in binds it to gfx_title_background_load. */
void engine_set_current_screen(int screen);
int  engine_current_screen(void);
void engine_set_viewport_game_extents(int w, int h);

typedef void (*engine_title_bg_reload_fn)(int page);
void engine_bind_title_bg_reload_fn(engine_title_bg_reload_fn fn);

/* Per-tick driver — scaffold of retail FUN_004b99c0.  Public so
 * engine_tick_dispatch can call it without going through the guard. */
void engine_tick(void);

/* Enable the virtual sim clock (segtrace/turbo determinism): every
 * sim-timing read advances by `step_ms` (0 → default 17) once per
 * engine_tick instead of using real timeGetTime.  Mirrors the retail
 * harness's --turbo virtual clock so the rate controller grants the same
 * sub-steps per tick and both sides stay frame-exact through a running
 * turn.  Call once at startup (the --turbo seat). */
void engine_virtual_clock_enable(uint32_t step_ms);

/* Reactive popup auto-dismiss (TAS `{"auto_dismiss":1}`): while on, the
 * in-game input phase dismisses any open modal popup each tick (the
 * position-independent right-click path), so a trace can run a turn
 * through tutorial/event scrolls without hand-timed clicks. */
void engine_set_popup_auto_dismiss(int on);
int  engine_popup_is_open(void);

/* --input-segtrace cursor seat: bypass the real GetCursorPos() poll in
 * engine_tick_input_step, substituting the WM_MOUSEMOVE-cached client
 * coords (g_mouse_x/y) instead.  Retail's raw-cursor edge-scroll gate
 * only works because retail runs fullscreen at screen-origin (0,0), so
 * GetCursorPos() naturally coincides with client-space coords; a hidden
 * headless window has no real mouse driving it, so GetCursorPos()
 * returns an arbitrary desktop position instead of the trace's
 * synthesized coordinate. Call once at startup when segtrace is
 * driving input (see project memory project_headless_cursor_gap). */
void engine_set_input_cursor_override(int enable);

/* DEV verification seat (--force-popup TYPE:CODE[:FLAG6[:PAYLOAD_A]]): paint
 * a popup arm directly over whatever's already on the canvas, bypassing the
 * live event producer/consumer.  See engine.c's engine_popup_scroll_render
 * for the arm ladder. */
void engine_dev_force_labor_render(int task);
void engine_dev_force_trade_render(int good);
void engine_dev_force_popup_render(int type, int32_t code, uint8_t flag6,
                                   int32_t payload_a);

/* Render-fast-skip dispatcher — port of retail FUN_0040e890.
 *   1. call engine_tick
 *   2. if g_engine_state > 8, latch g_savepump_request = 1
 */
void engine_tick_dispatch(void);

/* Surface-lost guard + save-pump dispatcher + deferred-shutdown
 * escalator.  Port of retail FUN_0040e8bb.  Returns 1 if engine_tick
 * was dispatched, 0 if skipped (surface still lost after a Restore
 * attempt, or shutdown latched). */
int engine_tick_guard(void);

/* ─── sim sub-step rate controller (port of FUN_004bb978) ──────────
 *
 * Decides how many sim sub-steps to run on this Win32-pump iteration.
 * Returns 0, 1, or 2.  Pure function: all inputs explicit, all state
 * mutations returned in the output struct.  The Win32 wrapper inside
 * engine.c bundles the live globals into the input struct, calls
 * this, then writes the deltas back.
 *
 * Today every input except `now_ms` is a stub-fed constant (the
 * scenario hint comes from FUN_0043de70 which gates on
 * `g_engine_state == 3` — our engine state mirror is hardcoded to 0)
 * so the function returns 0 sub-steps unconditionally.  The structural
 * seam is in place for when subsystems land that can drive ingame
 * state, at which point this function starts producing the real
 * variable-rate cadence retail uses.
 */

/* Inputs — one per global retail FUN_004bb978 reads.  The
 * cross-references to retail DAT_* addresses are in engine.c next
 * to the implementation so the header stays slim. */
typedef struct {
    uint32_t now_ms;             /* timeGetTime() at call site */
    uint32_t last_step_ms;       /* last accepted sim-step timestamp */
    uint32_t autosave_start_ms;  /* autosave-window start (0 = none) */
    int      run_gate;           /* 0 = sim halted, else may step */
    int      scenario_hint;      /* signed: <0 fast-skip, 0 freeze, >0 ms-per-step */
    int      mp_active;          /* multiplayer-session flag */
    uint32_t sim_tick_counter;   /* master sim tick (fast-skip threshold) */
    int      game_speed;         /* 0 = slow … 100 = fast */
    int      pause_lock;         /* non-zero = paused */
    int      screen_freeze;      /* per-screen sim-freeze flag (0/1/2) */
    int      extended_pause;     /* extra pause condition (e.g. dialog) */
    int      mp_skip_flag;       /* per-MP-player skip flag */
    int      phase;              /* phase counter, 0..0x9f */
    const uint8_t *rate_lut;     /* retail DAT_004e3260, ≥(clamped_hint/10+1)*0xa0 bytes */
} engine_rate_in_t;

/* Outputs — caller writes these back into the live globals.  No
 * mutation happens inside the pure function. */
typedef struct {
    int      sub_steps;           /* return value: 0, 1, or 2 */
    uint32_t new_last_step_ms;    /* writeback to last_step_ms global */
    uint32_t new_autosave_start_ms; /* writeback (only cleared, never set) */
    int      new_phase;           /* writeback (always advances by 1 mod 0xa0
                                   *           when the function reaches the
                                   *           LUT-lookup branch) */
    int      derived_hint;        /* writeback (clamped local_14 — currently
                                   *           write-only in retail too) */
    int      notify_autosave_done;/* 1 if the MP "autosave done" notification
                                   *  needs to fire (retail 0x43eda0(0xf, 0)) */
} engine_rate_out_t;

void engine_compute_sim_sub_steps(const engine_rate_in_t *in,
                                  engine_rate_out_t *out);

/* Verbatim image of retail's sub-step duty-cycle LUT DAT_004e3260
 * (15 × 0xa0 bytes — the full reachable extent including the as-shipped
 * OOB tail rows 11..14; see the definition in engine_rate.c and
 * engine-quirks #195).  Wired as engine_rate_in_t.rate_lut by
 * engine_tick_rate_step. */
extern const uint8_t g_engine_rate_lut[0xe60];

/* End-of-turn readiness predicate — Win32 wrapper around the pure
 * engine_turn_end_check (port of FUN_0049b762).  Returns 1 when the
 * strategic turn may advance.  Defined in engine.c; the pure core
 * lives in engine_turn.c.  No caller in the drop-in yet. */
int engine_sim_turn_resolved(void);

/* Win32 wrapper around the assembled per-turn driver (retail FUN_00448440
 * @ 0x448440).  Runs the AI brain + RNG stir (the head of retail's
 * sequence, which owns the masked-RNG global mirrors), then drives the
 * calendar block + 27 economy/realm passes via engine_turn_run_passes.
 * Bound to game_init's turn-driver seam (engine_game_init_bind_turn_driver_fn)
 * so a new game ticks the world to turn 1 exactly as retail does.
 * Defined in engine.c; the pure pass sequence lives in engine_turn.c. */
void engine_turn_advance(void);

/* Seed the live event queue's ring head from the loaded save block
 * (g_misc_128 = schema entry 11 = the first 128 bytes of retail's ring
 * at 0x568480).  The load route calls this after save_load so the
 * consumer sees the saved events exactly as retail does; the queue
 * indices/cooldown are boot-runtime (not schema) and keep their
 * defaults.  Defined in engine.c next to the ring↔save mirror. */
void engine_event_queue_seed_from_save(void);

/* Read / write the live double-LFSR RNG state (retail DAT_0058fe28/0058fe08).
 * game_init uses these to thread the global through the new-game seeder
 * FUN_00497e65, which in retail stirs this same global.  engine_rng_global_set
 * also refreshes the save mirror (entries 18/19). */
void engine_rng_global_get(uint32_t *state_a, uint32_t *state_b);
void engine_rng_global_set(uint32_t state_a, uint32_t state_b);

/* ─── scenario-rate hint (port of FUN_0043de70) ────────────────────
 *
 * Computes the `scenario_hint` value that engine_compute_sim_sub_steps
 * consumes.  In SP, trivially returns 1.  In MP, walks the 6-player
 * tick table (DAT_0053e8d0[i*4]) gated by per-slot active / has-data /
 * disconnected flags, computes my-lead-or-deficit vs the slowest peer,
 * and selects a ms-per-step value from one of three lookup ladders:
 *
 *   - "I'm behind" (delta < 1) — negative codes -2/-3/-4/-5/-6/-7/-8/-10
 *     keyed to how far behind, drives the fast-skip branches in
 *     sim_sub_steps.
 *   - "I'm ahead in mode-5 (fine-grained)" — 13-step ladder, 10..130 ms
 *     buckets, plus 140 ms ceiling.
 *   - "I'm ahead in coarse mode" — 8-step ladder, 20..130 ms buckets,
 *     plus 140 ms ceiling.
 *
 * A fourth path covers "mode==2 with <2 active allies" — uses
 * FUN_0043e50d (min-tick, no exclude filter) and a 4-step ladder
 * floored at base_delta*3.
 *
 * Pure function — every global read is an explicit input; every global
 * write is in the output struct.  Helpers (FUN_0043e433 count_active_
 * allies, FUN_0043e330 min_tick_filtered, FUN_0043e50d min_tick_all)
 * are inlined as file-static helpers in engine_rate.c.
 *
 * Side-effect avoidance: retail can call 0x43eda0(10, 0) when
 * slow_frame_count exceeds 100 (drives an MP broadcast event).  The
 * port surfaces this via `notify_slow_frame`; the diff orchestrator
 * pins `slow_frame_count ≤ 99` in test vectors so the trigger never
 * fires retail-side either.
 */
typedef struct {
    /* Engine state phase.  retail DAT_0052af94.  Must be 3 (ingame)
     * for the function to return non-zero; otherwise hint = 0. */
    int      game_state_phase;
    /* Multiplayer session active flag.  retail DAT_00553030. */
    int      mp_active;
    /* Local player slot index (0..6).  retail DAT_0057c8cc.  Slot 0
     * is unused in normal play; slots 1..6 carry the per-player state. */
    int      local_player_idx;
    /* Game-mode speed enum.  retail DAT_0057c8fc.
     *   0/1/3/4 = coarse 8-step ladder when ahead
     *   2       = ally-only filter + alone-in-mode-2 path possible
     *   5       = fine-grained 13-step ladder when ahead */
    int      game_mode_speed;
    /* Mode-2 ally player slots.  retail DAT_005681ec / DAT_005681d8.
     * Used by FUN_0043e433 + FUN_0043e330's mode-2 filter to restrict
     * the min-tick search to "me + my two allies". */
    int      ally_a;
    int      ally_b;
    /* Stride metrics (r/w).  Bumped on every throttle-branch call. */
    int      frame_count;          /* DAT_00515b20 — total throttled frames */
    int      max_stride;           /* DAT_00512fa8 — running max delta seen */
    int      stride_sum;           /* DAT_00515914 — running sum of deltas */
    int      avg_stride;           /* DAT_00515908 — stride_sum / frame_count
                                    *   (only OVERWRITTEN in throttle branch;
                                    *    catch-up + early-return paths carry
                                    *    through this value unchanged) */
    int      slow_frame_count;     /* DAT_004d5c90 — consecutive max-bucket
                                    *   hits.  Constrained ≤99 in diff
                                    *   vectors so the 100-trigger
                                    *   0x43eda0(10,0) never fires. */
    int      base_delta;           /* DAT_004d2160 — additive offset on the
                                    *   "I'm ahead" hint return; also a
                                    *   ×3 floor in the alone-in-mode-2 path */
    int      active_count;         /* DAT_0053f068 — write-only retail-side
                                    *   (carried through when none of the
                                    *   helpers that write it ran) */
    /* Per-player slot tables.  retail layouts:
     *   player_active[i]       = (&DAT_0057bf04)[i * 0x160]  (u8)
     *   player_has_data[i]     = (&DAT_0057bf05)[i * 0x160]  (u8)
     *   player_disconnected[i] = (&DAT_00553d76)[i * 0x2c]   (u8)
     *   player_ticks[i]        = *(int *)(&DAT_0053e8d0 + i*4)  (s32)
     * Only slots 1..6 are read; slot 0 is unused.  Strided layouts
     * collapse into 7-entry arrays for the port. */
    uint8_t  player_active[7];
    uint8_t  player_has_data[7];
    uint8_t  player_disconnected[7];
    int      player_ticks[7];
} engine_scenario_hint_in_t;

typedef struct {
    /* Return value: scenario_hint that engine_compute_sim_sub_steps
     * consumes.  0 = freeze, <0 = fast-skip, >0 = ms-per-step bucket. */
    int      hint;
    /* Writebacks.  When unchanged (catch-up branch, early returns), the
     * orchestrator restores the snapshotted value — so we return the
     * effective post-call state (potentially equal to in->). */
    int      new_frame_count;
    int      new_max_stride;
    int      new_stride_sum;
    int      new_avg_stride;
    int      new_slow_frame_count;
    int      new_active_count;
    /* 1 iff slow_frame_count would have hit 100 (retail fires
     * 0x43eda0(10, 0)).  Diff vectors are constrained so this
     * stays 0; tests exercise the flag directly. */
    int      notify_slow_frame;
} engine_scenario_hint_out_t;

void engine_compute_scenario_hint(const engine_scenario_hint_in_t *in,
                                  engine_scenario_hint_out_t *out);

/* ─── per-tick state hash (port of FUN_00440231) ───────────────────
 *
 * Computes a per-player 10-byte rolling "state hash" + a 600-byte
 * per-record byte-hash region.  Called every engine tick by retail's
 * per-tick driver as the input to the MP desync detector
 * (FUN_00440dd2 compares bucket[0] / bucket[9] across players,
 * setting a divergence latch on mismatch — that's how the engine
 * notices simulation drift between MP peers).
 *
 * Hash kernel = `FUN_0044015a`: sum bytes of a record from a per-array
 * offset to the end, return the low 8 bits.  The function selects
 * three source arrays based on game mode (DAT_0057c8fc):
 *
 *   - strategic (mode==0): per-army-768 + player-strategic-352 +
 *     per-territory-420  (slots 1..16, 1..5, 1..150)
 *   - combat (mode==2):    per-battle-hex-432 + per-combat-unit-76 +
 *     per-battle-hex-aux-52 (slots 1..80, 1..100, 1..80)
 *   - any other mode:      neither branch runs (output is the post-
 *     clear + post-bucket[7]=1 + post-bucket[9] state only)
 *
 * Layout of the 600-byte hash region (three 200-byte chunks):
 *
 *   [  0..199] — first  loop's per-slot hash bytes (slot N at index N)
 *   [200..399] — second loop's per-slot hash bytes
 *   [400..599] — third  loop's per-slot hash bytes
 *
 * `bucket[7] = 1` ALWAYS fires after the per-mode loops — so the
 * combat-mode per_battle_hex_aux hash sum (accumulated in bucket[7]
 * by the third combat loop) is OVERWRITTEN.  Preserved as a quirk.
 *
 * `bucket[0] = sum(bucket[1..8])` and `bucket[9] = (sim_tick_counter
 * & 0x7f) + 1` fire unconditionally.
 *
 * Side-effect surface: combat mode (==2) tail calls FUN_0043f4b7
 * (a snapshot/swap of three combat byte arrays into a malloc'd
 * scratch).  Surfaced as `notify_combat_snapshot` flag; the orchestrator
 * pins the snapshot-target pointer null retail-side so the memcpy
 * branch stays dormant during diffs.
 *
 * Pure function — every input explicit, every state mutation in out.
 * See engine_state_hash.c for the per-loop commentary and retail
 * cross-references.
 */
typedef struct {
    /* Game mode discriminator.  retail DAT_0057c8fc.
     *   0 = strategic overworld
     *   2 = tactical combat
     *   any other value = neither hash branch runs
     */
    int      mode;

    /* Per-player slot index (0..N) — selects which 10-byte bucket
     * (at &DAT_00569530[player_slot*10]) and which 600-byte hash
     * region (at &DAT_00565700[player_slot*600]) gets written.
     * retail DAT_0057c8cc.  Slot 0 is the unused floor; slots 1..6
     * carry per-player state in normal play. */
    int      player_slot;

    /* Sim tick counter — the low byte's low 7 bits +1 lands in
     * bucket[9].  retail DAT_0057c998 (full u32 passed; the function
     * truncates internally). */
    uint32_t sim_tick_counter;

    /* Strategic-mode source arrays (only read when mode == 0).  Each
     * pointer is to slot 0; the function reads slots 1..N at the
     * documented strides.  N specified inline below. */
    const uint8_t *per_army_768;          /* 17 × 768  — slots 1..16  read; offset 5,  hash 763 B/slot */
    const uint8_t *player_strategic_352;  /*  6 × 352  — slots 1..5   read; offset 6,  hash 346 B/slot */
    const uint8_t *per_territory_420;     /* 151 × 420 — slots 1..150 read; offset 0,  hash 420 B/slot */

    /* Combat-mode source arrays (only read when mode == 2). */
    const uint8_t *per_battle_hex_432;    /* 81 × 432  — slots 1..80  read; offset 18, hash 414 B/slot */
    const uint8_t *per_combat_unit_76;    /* 101 × 76  — slots 1..100 read; offset 4,  hash 72 B/slot  */
    const uint8_t *per_battle_hex_aux_52; /* 81 × 52   — slots 1..80  read; offset 0,  hash 52 B/slot  */
} engine_state_hash_in_t;

typedef struct {
    /* Post-call value of DAT_00569530[player_slot*10..player_slot*10+10].
     *   [0]    = sum of buckets[1..8]
     *   [1..3] = per-loop hash sums (strategic) or 0 (combat / other)
     *   [4]    = always 0 (unused slot)
     *   [5..6] = per-loop hash sums (combat) or 0 (strategic / other)
     *   [7]    = always 1 (overwrites combat loop-3 hash sum)
     *   [8]    = always 0 (unused slot)
     *   [9]    = (sim_tick_counter & 0x7f) + 1
     */
    uint8_t  bucket[10];

    /* Post-call value of DAT_00565700[player_slot*600..player_slot*600+600].
     * Cleared at entry, then loop N writes hash bytes at indices 1..max_slot
     * within its 200-byte chunk; other bytes stay 0. */
    uint8_t  hash_region[600];

    /* 1 iff mode == 2 (retail's combat-mode tail call to FUN_0043f4b7
     * would have fired).  The combat snapshot itself is NOT performed
     * by the port — it touches scratch pointers we haven't ported. */
    int      notify_combat_snapshot;
} engine_state_hash_out_t;

void engine_compute_state_hash(const engine_state_hash_in_t *in,
                               engine_state_hash_out_t *out);

/* ─── animation phase ticks (port of FUN_004bbc80) ─────────────────
 *
 * Maintains 9 "did event X fire this call" flags driven by
 * `timeGetTime`: a base 20 ms tick + 8 mod-N rollover flags gated
 * off it.  Downstream sprite/UI updaters consume the flags once per
 * frame to drive animation cadence.
 *
 * Pure function — every input explicit, every state mutation in
 * out.  See engine_phase.c for the per-flag commentary and retail
 * cross-references.
 */
typedef struct {
    uint32_t now_ms;             /* timeGetTime() at call site */
    uint32_t last_base_tick_ms;  /* timestamp of last accepted base tick */
    uint32_t base_tick_counter;  /* counts 20 ms ticks 0..3 toward the
                                  * 80 ms rollover */
    uint32_t counter_2;          /* 80 ms tick × 2  → 160 ms */
    uint32_t counter_4;          /* 80 ms tick × 4  → 320 ms */
    uint32_t counter_8;          /* 80 ms tick × 8  → 640 ms */
    uint32_t counter_13;         /* 80 ms tick × 13 → 1040 ms */
    uint32_t counter_16;         /* 80 ms tick × 16 → 1280 ms */
    uint32_t counter_24;         /* 80 ms tick × 24 → 1920 ms */
    uint32_t counter_32;         /* 80 ms tick × 32 → 2560 ms */
} engine_phase_in_t;

typedef struct {
    /* Did-fire flags (0 or 1).  Eight are non-sticky in retail (the
     * wrapper clears each on every call before reading the new
     * value); the 1040 ms flag is sticky (consumers clear it
     * themselves). */
    int tick_20ms;
    int tick_80ms;
    int tick_160ms;
    int tick_320ms;
    int tick_640ms;
    int tick_1040ms;
    int tick_1280ms;
    int tick_1920ms;
    int tick_2560ms;
    /* Writebacks. */
    uint32_t new_last_base_tick_ms;
    uint32_t new_base_tick_counter;
    uint32_t new_counter_2;
    uint32_t new_counter_4;
    uint32_t new_counter_8;
    uint32_t new_counter_13;
    uint32_t new_counter_16;
    uint32_t new_counter_24;
    uint32_t new_counter_32;
} engine_phase_out_t;

void engine_compute_phase_ticks(const engine_phase_in_t *in,
                                engine_phase_out_t *out);

/* ─── frame delta (port of FUN_004b20ed) ───────────────────────────
 *
 * Wall-clock delta-ms generator with a 30 ms low-pass: deltas below
 * 0x1e (including negatives from clock wrap or future-seeded
 * baselines — the gate is signed) report as 0 and hold the
 * baseline; otherwise advance the baseline and report the delta.
 * Downstream sprite-animation updaters consume the result.
 *
 * Pure function — every input explicit, every state mutation in
 * out.  See engine_delta.c for the per-field commentary and retail
 * cross-references.
 */
typedef struct {
    uint32_t now_ms;          /* timeGetTime() at call site */
    uint32_t last_frame_ms;   /* timestamp of last accepted frame */
} engine_delta_in_t;

typedef struct {
    int      delta_ms;            /* 0 if suppressed, else ms since last */
    uint32_t new_last_frame_ms;   /* baseline writeback (advanced or held) */
} engine_delta_out_t;

void engine_compute_frame_delta(const engine_delta_in_t *in,
                                engine_delta_out_t *out);

/* ─── 16-frame animation cycle (port of FUN_00451f1a) ──────────────
 *
 * Bumps a mod-16 counter once per 80 ms phase tick.  Downstream
 * sprite renderers use the counter as a frame index into a 16-frame
 * strip (full cycle is 16 × 80 ms = 1280 ms).  See engine_anim_cycle.c
 * for the per-field commentary and the unreachable-lower-clamp
 * engine quirk.
 */
typedef struct {
    int counter;          /* current value (always 0..15 in normal flow) */
    int tick_80ms_fired;  /* gate — consumes g_phase_tick_80ms output */
} engine_anim_cycle_in_t;

typedef struct {
    int new_counter;      /* counter writeback (0..15) */
} engine_anim_cycle_out_t;

void engine_compute_anim_cycle(const engine_anim_cycle_in_t *in,
                               engine_anim_cycle_out_t *out);

/* ─── mouse-input resolver (port of FUN_004b191e) ──────────────────
 *
 * Per-iteration mouse-state update.  Inputs: a screen-space cursor
 * position (retail calls GetCursorPos), wall-clock timestamp, the
 * WndProc-mirrored button + dblclk bitmasks, viewport bounds, and
 * the prior frame's resolved input state.  Outputs: motion event
 * flag, button-edge flags, 300 ms delayed-single-click events,
 * double-click events, cursor position in game-coord space, and
 * a small set of state-writeback fields the wrapper feeds back
 * into the file-static mirrors.
 *
 * Pure function — every input explicit, every state mutation in out.
 * See engine_input.c for the per-field commentary, helpers, and the
 * shared-300 ms-timer quirk between LMB and RMB.
 */
typedef struct {
    /* Win32-provided inputs (wrapper fills from GetCursorPos +
     * timeGetTime). */
    int      cursor_screen_x;
    int      cursor_screen_y;
    uint32_t now_ms;

    /* Mode flag.  retail DAT_0057cac4 — gates the per-frame cursor-
     * cache overwrite (cache_enabled==0 means the WM_MOUSEMOVE-cached
     * client-space coords carry through; ==1 means the screen-space
     * GetCursorPos result is stored instead). */
    int      cursor_cache_enabled;

    /* WndProc-mirrored event bitmasks.  bit 0 = LMB, bit 1 = RMB. */
    uint8_t  pending_dblclk;     /* g_mouse_dblclk (DAT_004eada1) */
    uint8_t  button_state;       /* g_mouse_buttons (DAT_004eabc2) */

    /* Viewport bounds — screen-space rectangle the cursor is
     * mapped against.  retail DAT_004ea180..18c (g_client_screen). */
    int      viewport_x0;
    int      viewport_y0;
    int      viewport_x1;
    int      viewport_y1;
    /* Viewport extents in game-coord space.  retail DAT_004eb274 / DAT_004eac4c. */
    int      viewport_game_w;
    int      viewport_game_h;

    /* Prior cached cursor (WM_MOUSEMOVE-tracked).  retail DAT_004eabbe/c0. */
    int16_t  cached_cursor_x;
    int16_t  cached_cursor_y;
    /* Prior game-coord cursor.  retail DAT_004e65fc / DAT_004e65f4. */
    int      prev_game_x;
    int      prev_game_y;
    /* Prior resolved LMB/RMB state (0 or 1).  retail DAT_004e65cc / DAT_004e8780. */
    int      prev_lmb;
    int      prev_rmb;

    /* Pending-click timer state.  retail DAT_004e65e8 / DAT_004e65c4. */
    int      lmb_timer_armed;
    int      rmb_timer_armed;
    /* Shared click-timer baseline.  retail _DAT_004e59f8 — both LMB
     * and RMB releases reset this; both expiries read it.  Quirk
     * preserved (see engine_input.c). */
    uint32_t click_timer_ms;
    /* Last-motion timestamp.  retail _DAT_004e59f4. */
    uint32_t motion_last_ms;
    /* Click-eligibility latch.  retail DAT_004e65f0.  Cleared on
     * release-into-click; presumably set elsewhere on press over
     * an interactive UI region (writer not yet ported).  Today our
     * wrapper hardcodes it to 0 — no UI yet — so the click-arming
     * branches stay inert. */
    int      click_eligible;
    /* Prior last-click coords.  retail DAT_004eac04 / DAT_004eac08.
     * Carried through unchanged on non-click frames; overwritten in
     * the click-armed release branches. */
    int      prev_last_click_x;
    int      prev_last_click_y;
} engine_input_in_t;

typedef struct {
    /* Per-frame raw cursor (always written). */
    int      raw_cursor_x;            /* DAT_004e6594 */
    int      raw_cursor_y;            /* DAT_004e6598 */

    /* Cached cursor — either freshly written from cursor_screen_x/y
     * (cursor_cache_enabled) or carried through unchanged. */
    int16_t  new_cached_cursor_x;     /* DAT_004eabbe */
    int16_t  new_cached_cursor_y;     /* DAT_004eabc0 */

    /* Snapshots of prior state for downstream consumers.  Retail
     * writes these unconditionally before computing the new state. */
    int      prev_game_x_snapshot;    /* DAT_004ea198 */
    int      prev_game_y_snapshot;    /* DAT_004ea190 */
    int      prev_lmb_snapshot;       /* DAT_004eabbc */
    int      prev_rmb_snapshot;       /* DAT_004eabc3 */

    /* Resolved game-coord cursor for this frame. */
    int      game_x;                  /* DAT_004e65fc */
    int      game_y;                  /* DAT_004e65f4 */

    /* Per-frame event flags (all 0 unless the corresponding edge
     * fired this frame). */
    int      mouse_event;             /* DAT_004ea4b0 — any mouse activity */
    int      button_changed;          /* _DAT_004eb264 — LMB or RMB edge */
    int      lmb_newly_pressed;       /* DAT_004eafb4 */
    int      lmb_newly_released;      /* DAT_004e65d8 */
    int      rmb_newly_pressed;       /* DAT_004eabe0 */
    int      rmb_newly_released;      /* DAT_004e6900 */
    int      lmb_click_fired;         /* DAT_004eabf0 — 300 ms expiry */
    int      rmb_click_fired;         /* DAT_004ea51c — 300 ms expiry */
    int      lmb_dblclk;              /* DAT_004eabc5 — from WndProc dblclk bit */
    int      rmb_dblclk;              /* DAT_004ea4b4 */

    /* Resolved LMB/RMB state (0 or 1) — cached for next frame. */
    int      new_lmb;                 /* DAT_004e65cc */
    int      new_rmb;                 /* DAT_004e8780 */

    /* Pending-click timer writebacks. */
    int      new_lmb_timer_armed;     /* DAT_004e65e8 */
    int      new_rmb_timer_armed;     /* DAT_004e65c4 */
    uint32_t new_click_timer_ms;      /* _DAT_004e59f8 */
    uint32_t new_motion_last_ms;      /* _DAT_004e59f4 */
    /* Idle ms since the last motion (0 on motion frames). */
    uint32_t motion_idle_ms;          /* _DAT_004eabfc */

    /* Last-click coordinates (in game-coord space).  Updated on
     * release-into-click; otherwise carried through. */
    int      last_click_x;            /* DAT_004eac04 */
    int      last_click_y;            /* DAT_004eac08 */

    /* Click-eligibility writeback (cleared on release-into-click). */
    int      new_click_eligible;      /* DAT_004e65f0 */
} engine_input_out_t;

void engine_compute_input_state(const engine_input_in_t *in,
                                engine_input_out_t *out);

/* ─── FPS counter (port of FUN_004b27bf) ───────────────────────────
 *
 * Rolling 1-second FPS measurement.  Bumps frames-this-second on every
 * call; once the window elapses (> 1000 ms), publishes the current FPS
 * and rolls the cumulative-FPS / cumulative-seconds counters used by
 * retail's session-diag (FUN_004b2636 — reports average FPS).
 *
 * Also returns ms since the previous frame (delta_ms) — retail's
 * caller discards this, but the value is computed regardless because
 * the baseline (`last_render_ms`) is unconditionally advanced on every
 * call.
 *
 * Pure function — every input explicit, every state mutation in out.
 * See engine_fps.c for the per-field commentary and the two preserved
 * quirks (signed rollover gate; two-timeGetTime-call shape).
 */
typedef struct {
    uint32_t now_ms;                  /* timeGetTime() at call site */
    uint32_t rollover_now_ms;         /* second timeGetTime() — used only
                                       * inside the rollover branch as the
                                       * new window-start basis (matches
                                       * retail's 2-call shape) */
    uint32_t last_render_ms;          /* _DAT_004e59e0 */
    uint32_t second_window_start_ms;  /* DAT_004eaba0 */
    uint32_t frames_this_second;      /* DAT_004eada4 */
    uint32_t current_fps;             /* DAT_004ea174 */
    uint32_t cumulative_fps;          /* DAT_004ea0f4 */
    uint32_t cumulative_seconds;      /* DAT_004ea83c */
} engine_fps_in_t;

typedef struct {
    uint32_t delta_ms;                    /* return value (ms since previous
                                           * frame) — retail discards it */
    uint32_t new_last_render_ms;
    uint32_t new_second_window_start_ms;
    uint32_t new_frames_this_second;
    uint32_t new_current_fps;
    uint32_t new_cumulative_fps;
    uint32_t new_cumulative_seconds;
    int      window_rolled_over;          /* 1 iff the 1-second window
                                           * rolled over this call; helps
                                           * tests + lets the wrapper note
                                           * which writebacks were the
                                           * "live" ones */
} engine_fps_out_t;

void engine_compute_fps(const engine_fps_in_t *in, engine_fps_out_t *out);

/* ─── Double-LFSR RNG advance (port of FUN_00404a46) ───────────────
 *
 * 31-iteration Galois LFSR step over two independent 32-bit state
 * words (taps at bits 0 and 4, feedback injected into bit 30).
 * Produces six bit-masked outputs per call (15/7/3 bits for each
 * state).  Consumers read the output mirrors directly; the bounded
 * bitmask sampler (retail FUN_00404b99) re-runs this advance up to
 * 10 times if the 15-bit value lands above the requested ceiling.
 *
 * Retail seeds (FUN_004b2258 / InitInstance @ lines 83635-83636):
 *   state_a = 0x54657687, state_b = 0x72641663.
 *
 * The LFSR is effectively 31-bit (bit 31 always 0 after the first
 * call — see engine_rng.c).  This is a deliberate engine quirk
 * preserved for byte-shape parity.
 */
typedef struct {
    uint32_t state_a;        /* DAT_0058fe28 */
    uint32_t state_b;        /* DAT_0058fe08 */
} engine_rng_in_t;

typedef struct {
    uint32_t new_state_a;    /* DAT_0058fe28 writeback */
    uint32_t new_state_b;    /* DAT_0058fe08 writeback */
    /* Six output mirrors — masked snapshots of the new state. */
    uint32_t rng_15bit_a;    /* DAT_005aea58  (state_a & 0x7fff) */
    uint32_t rng_7bit_a;     /* DAT_0058fd7c  (state_a & 0x7f)   */
    uint32_t rng_3bit_a;     /* _DAT_0058fd70 (state_a & 0x7)    */
    uint32_t rng_15bit_b;    /* _DAT_005aea5c (state_b & 0x7fff) */
    uint32_t rng_7bit_b;     /* DAT_0058fd60  (state_b & 0x7f)   */
    uint32_t rng_3bit_b;     /* _DAT_0058fd6c (state_b & 0x7)    */
} engine_rng_out_t;

void engine_rng_step(const engine_rng_in_t *in, engine_rng_out_t *out);

/* ─── Single-LFSR RNG advance (port of FUN_00404b2c) ───────────────
 *
 * A third, independent 31-bit Galois LFSR — structurally one half of
 * engine_rng_step above (same taps at bits 0/4, same bit-30 feedback
 * target, same 31 iterations), but on its own state word and with a
 * single 7-bit output mirror instead of six.
 *
 * Retail seed (FUN_004b2258 / InitInstance @ line 83637, the third
 * RNG seed set right after the double-LFSR pair): state = 0x34518632.
 *
 * Retail call shape is `FUN_00404b2c(); v = DAT_005c9a84 & 7;` — the
 * advance writes the 7-bit mirror DAT_005c9a84, and callers mask it
 * further.  The first ported consumer is the new-game terrain-tile
 * randomiser (engine_territory_randomise_special_tiles, retail 0x46dd34);
 * like the double-LFSR new-game seeders in game_init.c, it threads this
 * state word through a pointer rather than reading a global, so no stateful
 * Win32 wrapper exists yet.  One lands in engine.c only once a consumer
 * needs the state to persist across calls without an orchestrator to thread
 * it (e.g. the strategic samplers at 0x47*) — the live 0x4676e0 then
 * owns the seeded state and threads it through the grid seeders in order.
 *
 * Same effectively-31-bit quirk as the double LFSR (bit 31 always 0
 * after the first advance — feedback targets bit 30, unsigned >>1
 * clears bit 31). */
typedef struct {
    uint32_t state;          /* DAT_00591564 */
} engine_rng_single_in_t;

typedef struct {
    uint32_t new_state;      /* DAT_00591564 writeback */
    uint32_t rng_7bit;       /* DAT_005c9a84 (new_state & 0x7f) */
} engine_rng_single_out_t;

void engine_rng_step_single(const engine_rng_single_in_t *in,
                            engine_rng_single_out_t *out);

/* Read-only accessors for the FPS counter + frame counter mirrors.
 * Used by main.c at shutdown to print a smoke summary — the values
 * confirm that the per-tick driver is actually ticking and the FPS
 * counter is alive.  No setters; the wrappers in engine.c own writes. */
uint32_t engine_get_current_fps(void);
uint32_t engine_get_cumulative_fps(void);
uint32_t engine_get_cumulative_seconds(void);
uint32_t engine_get_frame_counter(void);

#endif /* OPENLORDS2_ENGINE_H */
