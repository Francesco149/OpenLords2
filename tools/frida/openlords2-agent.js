// tools/frida/openlords2-agent.js
//
// Frida agent for the harness against the retail Lords2.exe.
//
// Same conventions as the sibling projects (OpenMare, openrecet): one
// module-level `init({...})` RPC sets every behavior, and the agent emits
// `kind`-tagged JSON events over send() that the Python driver consumes.
//
// Phase A — non-engine-specific hooks (no Ghidra-derived addresses yet).
// Everything here is keyed on imported DLL exports (user32, kernel32,
// winmm, ddraw, dsound), which is robust across Lords2.exe rebuilds and
// patch versions:
//
//   user32!CreateWindowEx{A,W}        — track owned HWNDs
//   user32!ShowWindow                 — rewrite SHOW→HIDE on owned HWNDs
//   user32!ShowWindowAsync            — same
//   user32!SetWindowPos               — strip SWP_SHOWWINDOW on owned HWNDs
//   user32!SetWindowLong{A,W}         — strip WS_VISIBLE on GWL_STYLE writes
//   user32!ChangeDisplaySettings*     — return DISP_CHANGE_SUCCESSFUL no-op
//   user32!MessageBox{A,W}            — capture body, return IDOK (always on)
//   kernel32!Sleep                    — no-op (turbo v1) when enabled
//   ddraw!DirectDrawCreate            — wrap IDirectDraw::SetCooperativeLevel
//   ddraw!DirectDrawCreateEx          — wrap IDirectDraw7::SetCooperativeLevel
//   dsound!DirectSoundCreate          — wrap IDirectSound::SetCooperativeLevel
//                                        + IDirectSoundBuffer::SetVolume clamp
//   winmm!waveOutSetVolume            — clamp to 0 (silent_audio)
//
// As Ghidra resolves the engine addresses (main-loop body, frame counter,
// audio init wrappers), we extend with engine-internal hooks behind init
// flags.  This file matches the patterns OpenMare's openmare-agent.js
// uses; refer there for the full Phase B/C extensions.
//
// Message protocol (stringified JSON via send()):
//   {kind:"ready",  module, base, fridaVersion, ...flags}
//   {kind:"log",    msg}
//   {kind:"error",  where, msg}
//   {kind:"hwnd_owned",         hwnd, source}
//   {kind:"hwnd_owned_detail",  hwnd, api, cls, title}
//   {kind:"hide_force",         api, hwnd, original}
//   {kind:"display_settings_blocked", api, flags}
//   {kind:"messagebox_intercepted",  api, hwnd, caption, body, type}
//   {kind:"ddraw_cooplevel_rewrite", iface, hwnd, original, forced}
//   {kind:"dsound_cooplevel_rewrite", iface, hwnd, original, forced}
//   {kind:"audio_mute",         api, original}
//   {kind:"fps_sample",         current_fps, cumulative_fps,
//                               cumulative_seconds, avg_fps,
//                               virtual_now_ms, t_ms}
//   {kind:"frame",              frame_idx, width, height, pitch,
//                               virtual_now_ms, t_ms} (+ binary payload)
//   {kind:"input_event",        t (tick), m (msg), w (wparam), l (lparam),
//                               hwnd}
//   {kind:"input_replay_post",  idx, t, m, w, l, tick_now}

'use strict';

// ─── module identity ────────────────────────────────────────────────────

const MODULE_NAME = 'Lords2.exe';
const IMAGE_BASE  = ptr('0x00400000');
let   g_target_module_name = MODULE_NAME;

// ─── retail address table (Lords2.exe, build verified 2026-05-24) ──────
// VA → resolved against the actual module base via rva() at hook time.
// Cross-referenced in docs/findings/main-loop-tick.md.
const ADDR = {
    fn_engine_tick:    0x004b99c0,  // FUN_004b99c0 — per-tick driver (sim + render)
    // Reactive popup auto-dismiss ({"auto_dismiss":1}) — mirror of the port's
    // engine.c:2098 chip.  While a modal scroll is open (DAT_0053e910 != 0) the
    // per-tick hook sets the rmb-newly-released flag (DAT_004e6900) and calls
    // the open-scroll input handler FUN_0047685d, whose right-click path closes
    // the scroll (FUN_00476768) → unfreezes the SP sim, same tick.
    fn_popup_scroll_input: 0x0047685d,  // FUN_0047685d — open-scroll input/dismiss
    fn_popup_dismiss:      0x00476768,  // FUN_00476768 — the dismiss (clears 0x53e910)
    var_extended_pause:    0x0053e910,  // DAT_0053e910 — modal-scroll freeze (dword)
    var_rmb_released:      0x004e6900,  // DAT_004e6900 — rmb-newly-released (byte)
    // Fullscreen-mode global (1 = exclusive, 0 = windowed) — read 50+
    // places throughout the engine.  FUN_0042e5c0 auto-promotes to 1 on
    // any non-8bpp desktop, so retail effectively always goes
    // fullscreen on modern hardware.  We override to 0 in the
    // SetCooperativeLevel hook so retail takes its (mostly-dead-code)
    // windowed branch: overlapped-window style, 640×480 centered, Blt
    // instead of BltFast for the present.  See engine-quirks.md #1.
    var_fullscreen_flag: 0x0053f228,  // DAT_0053f228
    // FPS-counter globals (read once per engine second-rollover to publish
    // an `fps_sample` event for turbo verification).
    var_fps_current:   0x004ea174,  // DAT_004ea174 — current FPS (refreshed once / sec)
    var_fps_cum_sum:   0x004ea0f4,  // DAT_004ea0f4 — cumulative FPS sum (for avg)
    var_fps_cum_secs:  0x004ea83c,  // DAT_004ea83c — cumulative seconds counter
    // DDraw surface + palette pointers — populated by FUN_0042ec91 once the
    // engine has finished bring-up.  See docs/findings/ddraw-dsound-bringup.md
    // for the surface-creation chain.  Read directly from BSS rather than
    // hooking IDirectDraw::CreateSurface — three pointer reads is cheaper +
    // simpler than vtable interception, and the engine never recreates these
    // mid-run.
    var_dd_primary:    0x004eabd0,  // IDirectDrawSurface* — primary
    var_dd_back:       0x004eafac,  // IDirectDrawSurface* — back (offscreen plain)
    var_dd_palette:    0x004df288,  // IDirectDrawPalette* — 256-entry palette
    // Input subsystem — WndProc and the cached main-HWND global.  See
    // docs/findings/wndproc.md for the message-dispatch breakdown.  Hook the
    // class wndproc directly rather than user32!DispatchMessage* because the
    // wndproc has the four args already on the stack (hwnd, msg, wp, lp) —
    // DispatchMessage would force us to dereference the MSG* and would also
    // catch internal posts (e.g. PostMessageA from inside the engine on
    // Alt-X) that we don't want to round-trip.
    fn_wndproc:        0x004b29be,  // FUN_004b29be — class wndproc
    var_main_hwnd:     0x004e5a00,  // DAT_004e5a00 — cached main HWND
    // Main-loop present gate (FUN_0040e9ab): the per-iteration present
    // (FUN_0040e8bb) runs only while (surface_lost == 0 && app_active != 0),
    // else the loop Sleep(200)s — so a frida-spawned, created-hidden window
    // that never wins WM_ACTIVATEAPP=TRUE stays at ~5 Hz with no present and
    // looks frozen.  The interactive seat pokes these every tick.
    var_app_active:    0x004e65a8,  // DAT_004e65a8 — s32 (WM_ACTIVATEAPP)
    var_surface_lost:  0x004e65d0,  // DAT_004e65d0 — s32 (DDraw surface lost)
    // ── Pure-function differential targets (Phase 4 of harness-roadmap).
    // Two retail functions whose pure-C ports live under src/engine_*.c:
    //   FUN_00404a46  — double-LFSR step (engine_rng_step / src/engine_rng.c)
    //   FUN_004b27bf  — rolling 1-sec FPS counter (engine_compute_fps /
    //                   src/engine_fps.c)
    // Each is called with the engine's per-tick driver suspended; the diff
    // wrappers snapshot all observable globals, write a test-vector's inputs,
    // invoke the retail function via NativeFunction, snapshot outputs, then
    // restore the snapshot so the engine isn't disturbed.
    fn_engine_rng_step:        0x00404a46,  // FUN_00404a46 — double-LFSR step
    fn_engine_compute_fps:     0x004b27bf,  // FUN_004b27bf — FPS counter
    fn_engine_anim_cycle:      0x00451f1a,  // FUN_00451f1a — 16-frame anim cycle bump
    fn_engine_frame_delta:     0x004b20ed,  // FUN_004b20ed — frame delta w/ 30ms gate
    fn_engine_phase_ticks:     0x004bbc80,  // FUN_004bbc80 — 9-flag animation phase ticks
    fn_engine_input_state:     0x004b191e,  // FUN_004b191e — mouse-input state resolver
    fn_engine_sim_sub_steps:   0x004bb978,  // FUN_004bb978 — sim sub-step rate gate
    fn_engine_state_hash:      0x00440231,  // FUN_00440231 — per-tick MP state hash
    // engine_tick_dispatch (FUN_0040e890) — wraps the per-tick driver FUN_004b99c0.
    // Replaced with a no-op during sim_sub_steps diff vectors to prevent the
    // engine main thread from racing into the sub-step body with our injected
    // ingame-state globals.  Lower-level than FUN_004b99c0 itself so the existing
    // engine_tick anchor (attached to 0x4b99c0) doesn't have to deal with a
    // body-replaced function (Interceptor.attach + Interceptor.replace on the
    // same address have undefined interaction order — safer to swap the caller).
    fn_engine_tick_dispatch:   0x0040e890,  // FUN_0040e890 — per-tick dispatcher
    // RNG state + 6 output mirrors (see docs/findings/engine-quirks.md #5
    // and src/engine_rng.c for the mirror-↔-port mapping).
    var_rng_state_a:     0x0058fe28,  // DAT_0058fe28 — state_a (the seed-init at WinMain)
    var_rng_state_b:     0x0058fe08,  // DAT_0058fe08 — state_b
    // Single-LFSR RNG state (FUN_00404b2c) — seeded 0x34518632 at InitInstance;
    // the new-game overworld randomisers (engine_territory_*) thread it.
    var_rng_state_single: 0x00591564, // DAT_00591564 — single-LFSR state

    // Anim-phase cadence state (engine_phase.c mapping) — pinned pre-ingame
    // by --phase-pin so both engines enter state 3 with identical counters
    // and the shared anim frames (torches/flags/demo figures/marches) stay
    // frame-exact at anchor+offset captures.
    var_phase_last_base_ms:  0x004e5b10, // _DAT_004e5b10 — last 20ms base-tick timestamp
    var_phase_base_counter:  0x005aeb2c, // DAT_005aeb2c — 20ms ticks 0..3 → 80ms
    var_phase_counter_2:     0x0058feb4, // DAT_0058feb4 — 80ms ×2 → 160ms
    var_phase_counter_4:     0x0058feb0, // DAT_0058feb0 — 80ms ×4 → 320ms
    var_phase_counter_8:     0x0058feb8, // DAT_0058feb8 — 80ms ×8 → 640ms
    var_phase_counter_13:    0x005aea54, // DAT_005aea54 — torch-flame frame 0..0xc
    var_phase_counter_16:    0x005aea44, // DAT_005aea44 — 0..0xf
    var_phase_counter_24:    0x005aea48, // DAT_005aea48 — demo-figure frame 0..0x17
    var_phase_counter_32:    0x005aea4c, // DAT_005aea4c — 0..0x1f
    var_rng_15bit_a:     0x005aea58,  // DAT_005aea58 — state_a & 0x7fff
    var_rng_7bit_a:      0x0058fd7c,  // DAT_0058fd7c — state_a & 0x7f
    var_rng_3bit_a:      0x0058fd70,  // _DAT_0058fd70 — state_a & 0x7
    var_rng_15bit_b:     0x005aea5c,  // _DAT_005aea5c — state_b & 0x7fff
    var_rng_7bit_b:      0x0058fd60,  // DAT_0058fd60 — state_b & 0x7f
    var_rng_3bit_b:      0x0058fd6c,  // _DAT_0058fd6c — state_b & 0x7
    // FPS counter state (6 globals) — see src/engine_fps.c for the mapping.
    var_fps_frames:           0x004eada4,  // DAT_004eada4 — frames_this_second
    var_fps_last_render:      0x004e59e0,  // _DAT_004e59e0 — last_render_ms
    var_fps_window_start:     0x004eaba0,  // DAT_004eaba0 — second_window_start_ms
    // (var_fps_current, var_fps_cum_sum, var_fps_cum_secs already above —
    // ride the same heartbeat.)
    // Anim-cycle state (2 globals) — see src/engine_anim_cycle.c.  Bumped
    // once per engine_tick (gated on the 80 ms phase tick).
    var_anim_cycle_counter:   0x0053e9a4,  // DAT_0053e9a4 — counter (0..15)
    var_phase_tick_80ms:      0x00591510,  // DAT_00591510 — 80 ms phase tick flag
    // Frame-delta state (1 global) — see src/engine_delta.c.
    var_frame_delta_last:     0x004e59ec,  // _DAT_004e59ec — last_frame_ms
    // Phase-ticks state (10 globals + 9 flags) — see src/engine_phase.c.
    // 8 of the 9 flags are cleared by retail at entry to FUN_004bbc80; the
    // 1040 ms flag is sticky.  The diff RPC clears all 9 going in so the
    // post-call readback is "did THIS call set the flag".
    var_phase_last_base_tick_ms: 0x004e5b10, // _DAT_004e5b10
    var_phase_base_tick_counter: 0x005aeb2c, //  DAT_005aeb2c
    var_phase_counter_2:         0x0058feb4, //  DAT_0058feb4 (×2  → 160 ms)
    var_phase_counter_4:         0x0058feb0, //  DAT_0058feb0 (×4  → 320 ms)
    var_phase_counter_8:         0x0058feb8, //  DAT_0058feb8 (×8  → 640 ms)
    var_phase_counter_13:        0x005aea54, //  DAT_005aea54 (×13 → 1040 ms)
    var_phase_counter_16:        0x005aea44, //  DAT_005aea44 (×16 → 1280 ms)
    var_phase_counter_24:        0x005aea48, //  DAT_005aea48 (×24 → 1920 ms)
    var_phase_counter_32:        0x005aea4c, //  DAT_005aea4c (×32 → 2560 ms)
    var_phase_tick_20ms:         0x0058fcb0, //  DAT_0058fcb0 — flag (base tick)
    // var_phase_tick_80ms already declared above (DAT_00591510, also used by
    // anim_cycle as the 80 ms gate signal).
    var_phase_tick_160ms:        0x0057d3ac, //  DAT_0057d3ac — flag
    var_phase_tick_320ms:        0x0057d3c8, //  DAT_0057d3c8 — flag
    var_phase_tick_640ms:        0x0058fd08, //  DAT_0058fd08 — flag
    var_phase_tick_1040ms:       0x005aeb28, // _DAT_005aeb28 — flag (sticky in wrapper)
    var_phase_tick_1280ms:       0x005aeb3c, //  DAT_005aeb3c — flag
    var_phase_tick_1920ms:       0x005aeb38, //  DAT_005aeb38 — flag
    var_phase_tick_2560ms:       0x005aeb30, //  DAT_005aeb30 — flag
    // Input-state mirrors (FUN_004b191e — engine_compute_input_state).  ~30
    // globals split across input (read at entry, restored after), state
    // (read at entry + overwritten in-place), and output (cleared at entry
    // or conditionally written).  See src/engine_input.c for the per-field
    // mapping.  Sizes follow Ghidra's labelling: byte-stride adjacency in
    // the 0x4eabXX cluster forces u8 reads/writes; cached_cursor_{x,y} are
    // i16; everything at 4-byte-aligned slots is s32/u32.
    //   Mode + WndProc-mirrored inputs.
    var_input_cursor_cache_enabled: 0x0057cac4, //  DAT_0057cac4 — s32
    var_input_pending_dblclk:       0x004eada1, //  DAT_004eada1 — u8 (g_mouse_dblclk)
    var_input_button_state:         0x004eabc2, //  DAT_004eabc2 — u8 (g_mouse_buttons)
    //   Viewport bounds (RECT in screen-coord space) + game-coord extents.
    var_input_viewport_x0:          0x004ea180, //  DAT_004ea180 — s32
    var_input_viewport_y0:          0x004ea184, //  DAT_004ea184 — s32
    var_input_viewport_x1:          0x004ea188, //  DAT_004ea188 — s32
    var_input_viewport_y1:          0x004ea18c, //  DAT_004ea18c — s32
    var_input_viewport_game_w:      0x004eb274, //  DAT_004eb274 — s32
    var_input_viewport_game_h:      0x004eac4c, //  DAT_004eac4c — s32
    //   Cached cursor (WM_MOUSEMOVE-tracked) — i16 each (CONCAT22 in WndProc).
    var_input_cached_cursor_x:      0x004eabbe, //  DAT_004eabbe — i16
    var_input_cached_cursor_y:      0x004eabc0, //  DAT_004eabc0 — i16
    //   Per-frame raw cursor (always written from GetCursorPos result).
    var_input_raw_cursor_x:         0x004e6594, //  DAT_004e6594 — s32
    var_input_raw_cursor_y:         0x004e6598, //  DAT_004e6598 — s32
    //   Snapshots of prior state (written unconditionally before computing
    //   the new state).  Byte-stride-packed in the 0x4eabXX cluster.
    var_input_prev_game_x_snap:     0x004ea198, //  DAT_004ea198 — s32
    var_input_prev_game_y_snap:     0x004ea190, //  DAT_004ea190 — s32
    var_input_prev_lmb_snap:        0x004eabbc, //  DAT_004eabbc — u8
    var_input_prev_rmb_snap:        0x004eabc3, //  DAT_004eabc3 — u8
    //   Game-coord cursor (prev_game_{x,y} on entry, game_{x,y} on exit).
    var_input_game_x:               0x004e65fc, //  DAT_004e65fc — s32
    var_input_game_y:               0x004e65f4, //  DAT_004e65f4 — s32
    //   Per-frame event flags (cleared at entry, conditionally set).
    var_input_mouse_event:          0x004ea4b0, //  DAT_004ea4b0 — s32
    var_input_button_changed:       0x004eb264, // _DAT_004eb264 — s32
    var_input_lmb_newly_pressed:    0x004eafb4, //  DAT_004eafb4 — s32
    var_input_lmb_newly_released:   0x004e65d8, //  DAT_004e65d8 — s32
    var_input_rmb_newly_pressed:    0x004eabe0, //  DAT_004eabe0 — s32
    var_input_rmb_newly_released:   0x004e6900, //  DAT_004e6900 — s32
    var_input_lmb_click_fired:      0x004eabf0, //  DAT_004eabf0 — s32
    var_input_rmb_click_fired:      0x004ea51c, //  DAT_004ea51c — s32
    var_input_lmb_dblclk:           0x004eabc5, //  DAT_004eabc5 — u8 (byte-adjacent)
    var_input_rmb_dblclk:           0x004ea4b4, //  DAT_004ea4b4 — s32 (4-aligned)
    //   Resolved button state (prev_lmb/rmb on entry, new_lmb/rmb on exit).
    var_input_lmb:                  0x004e65cc, //  DAT_004e65cc — s32
    var_input_rmb:                  0x004e8780, //  DAT_004e8780 — s32
    //   Pending-click timer state + shared 300 ms baseline.
    var_input_lmb_timer_armed:      0x004e65e8, //  DAT_004e65e8 — s32
    var_input_rmb_timer_armed:      0x004e65c4, //  DAT_004e65c4 — s32
    var_input_click_timer_ms:       0x004e59f8, // _DAT_004e59f8 — u32
    var_input_motion_last_ms:       0x004e59f4, // _DAT_004e59f4 — u32
    var_input_motion_idle_ms:       0x004eabfc, // _DAT_004eabfc — u32
    //   Click-eligibility latch (char in Ghidra → u8).
    var_input_click_eligible:       0x004e65f0, //  DAT_004e65f0 — u8
    //   Last-click coords (carried through; overwritten on click-arming).
    var_input_last_click_x:         0x004eac04, //  DAT_004eac04 — s32
    var_input_last_click_y:         0x004eac08, //  DAT_004eac08 — s32
    // Sim sub-step rate gate (FUN_004bb978 — engine_compute_sim_sub_steps) +
    // its delegated hint helper (FUN_0043de70 — engine_compute_scenario_hint).
    // ~30 scalars + 4 per-player strided arrays (slots 1..6 used) + a 2400-byte
    // rate-gate LUT.  See src/engine_rate.c for the per-global commentary.
    //   FUN_004bb978 direct reads/writes:
    var_sim_last_step_ms:           0x004e5b0c, // _DAT_004e5b0c — u32 r/w
    var_sim_autosave_start_ms:      0x0056d8b4, //  DAT_0056d8b4 — u32 r/w
    var_sim_mp_active:              0x00553030, //  DAT_00553030 — s32 r
    var_sim_tick_counter:           0x0057c998, //  DAT_0057c998 — u32 r
    var_sim_derived_hint:           0x004d215c, //  DAT_004d215c — s32 w
    var_sim_phase:                  0x004d2164, //  DAT_004d2164 — s32 r/w (0..0x9f)
    var_sim_game_speed:             0x0053f230, // _DAT_0053f230 — s32 r (0..100)
    var_sim_pause_lock:             0x0053f238, //  DAT_0053f238 — s32 r
    var_sim_current_screen:         0x004eac50, //  DAT_004eac50 — u8 r (table idx)
    var_sim_extended_pause:         0x0053e910, //  DAT_0053e910 — s32 r
    var_sim_mp_player_index:        0x005440a4, //  DAT_005440a4 — u8 r (table idx)
    //   FUN_004bb978 indirect table reads — single byte at table[idx]; we
    //   inject the byte at the exact index via the screen_freeze_byte /
    //   mp_skip_byte input fields rather than mirroring the full 256-byte
    //   table.  Index control comes from current_screen / mp_player_index.
    var_sim_screen_freeze_table:    0x004e31b0, //  DAT_004e31b0 — u8[]
    var_sim_mp_skip_table:          0x004e3240, //  DAT_004e3240 — u8[]
    //   Rate-gate LUT (FUN_004bb978 reads 1 byte at computed index).
    //   Index range [0, 14*0xa0 + 0x9f] = [0, 0x95f] = 2400 bytes.  We
    //   inject the full 2400 bytes once per test plan via installSimSubStepsRateLut
    //   so vectors share a deterministic LUT.  See engine_rate.c's OOB-note for
    //   why the documented size (0x6e0) is exceeded by 481 B.
    var_sim_rate_gate_lut:          0x004e3260, //  DAT_004e3260 — u8[2400] r
    //   FUN_0043de70 reads/writes:
    var_sim_game_state_phase:       0x0052af94, //  DAT_0052af94 — s32 r (must be 3)
    var_sim_local_player_idx:       0x0057c8cc, //  DAT_0057c8cc — s32 r (1..6)
    var_sim_game_mode_speed:        0x0057c8fc, //  DAT_0057c8fc — u8 r (0/2/5)
    var_sim_ally_a:                 0x005681ec, //  DAT_005681ec — s32 r
    var_sim_ally_b:                 0x005681d8, //  DAT_005681d8 — s32 r
    var_sim_frame_count:            0x00515b20, //  DAT_00515b20 — s32 r/w
    var_sim_max_stride:             0x00512fa8, //  DAT_00512fa8 — s32 r/w
    var_sim_stride_sum:             0x00515914, //  DAT_00515914 — s32 r/w
    var_sim_avg_stride:             0x00515908, //  DAT_00515908 — s32 w (carry-through)
    var_sim_slow_frame_count:       0x004d5c90, //  DAT_004d5c90 — s32 r/w (≤99 to avoid notify)
    var_sim_base_delta:             0x004d2160, //  DAT_004d2160 — s32 r
    var_sim_active_count:           0x0053f068, //  DAT_0053f068 — s32 w (carry-through)
    //   Per-player strided arrays.  7 entries (slots 0..6, 1..6 used).
    //   The base address is the slot-0 byte; per-slot byte is at base+i*stride.
    var_sim_player_active_base:     0x0057bf04, //  &DAT_0057bf04 — stride 0x160, u8
    var_sim_player_has_data_base:   0x0057bf05, //  &DAT_0057bf05 — stride 0x160, u8 (+1)
    var_sim_player_disconnected_base: 0x00553d76, //  &DAT_00553d76 — stride 0x2c, u8
    var_sim_player_ticks_base:      0x0053e8d0, //  &DAT_0053e8d0 — stride 4, s32
    // Per-tick state hash (FUN_00440231 — engine_compute_state_hash).
    //   Inputs: re-uses var_sim_game_mode_speed (DAT_0057c8fc, mode),
    //   var_sim_local_player_idx (DAT_0057c8cc, player_slot),
    //   var_sim_tick_counter (DAT_0057c998, sim_tick_counter).
    //   Source arrays — slot-0 bases.  Each indexed by slot*stride at the
    //   strides documented in docs/findings/save-format.md.  The hash
    //   function reads slots 1..N (slot 0 unused per the schema convention).
    var_hash_per_army_768:          0x0053f9b0, //  DAT_0053f9b0 — 17  × 0x300 (768)
    var_hash_player_strategic_352:  0x0057bf00, //  DAT_0057bf00 — 6   × 0x160 (352)
    var_hash_per_territory_420:     0x0052f0b0, //  DAT_0052f0b0 — 151 × 0x1a4 (420)
    var_hash_per_battle_hex_432:    0x00554480, //  DAT_00554480 — 81  × 0x1b0 (432)
    var_hash_per_combat_unit_76:    0x0057a100, //  DAT_0057a100 — 101 × 0x4c  (76)
    var_hash_per_battle_hex_aux_52: 0x00566520, //  DAT_00566520 — 81  × 0x34  (52)
    //   Output regions — slot-0 bases.  Per player_slot: 10-byte bucket
    //   at +slot*10, 600-byte hash region at +slot*600.
    var_hash_bucket_base:           0x00569530, //  DAT_00569530 — stride 10
    var_hash_region_base:           0x00565700, //  DAT_00565700 — stride 600
    // Castle-load footprint expansion (FUN_004917dd — engine_tile_expand_footprint)
    // plus its four helpers (FUN_004921e5 / FUN_00471c1f / FUN_00471d30 /
    // FUN_0048ee46).  Five globals it touches; FUN_004917dd is __cdecl(int)
    // so we pass tile_byte_offset as the lone argument via NativeFunction.
    // See docs/findings/save-format.md ("Per-record in-memory layout") for
    // the per-tile struct + scratch-array layouts.  Reuses the shared
    // engine-tick freeze (installSimSubStepsEngineFreeze) since the engine
    // main thread also reads from tiles/path_scratch/stage_scratch during
    // render + pathfinding.
    fn_engine_tile_expand_footprint:0x004917dd, // FUN_004917dd
    var_tile_tiles:                 0x005440e0, //  DAT_005440e0 — 6400 × 8 (51200 B)
    var_tile_path_scratch:          0x00507250, //  DAT_00507250 — 6400 × u16 (12800 B)
    var_tile_stage_scratch:         0x004f7d80, //  DAT_004f7d80 — 6400 × u8  (6400 B)
    var_tile_footprint_list:        0x00553c80, //  DAT_00553c80 — 19 × 2 × i32 (152 B)
    var_tile_last_footprint:        0x00553d18, // _DAT_00553d18 — i32 (4 B)
    // Delayed-action dispatcher (FUN_004472ce — engine_dispatch_delayed_actions).
    // Drains the 400-slot pending-action table once per sim sub-step; per-record
    // 0x38 stride; 50-entry function-pointer dispatch table at 0x4d5c98.  The
    // dispatch handlers (FUN_004479de .. FUN_00448422) touch a sprawling set of
    // global state — for the diff harness we stub the entire table to a no-op
    // NativeCallback so the function's observable surface is just the queue/
    // record/scalar globals listed below.  Reuses the shared engine-tick freeze
    // (installSimSubStepsEngineFreeze) since the engine main thread also
    // dispatches FUN_004472ce as part of its own per-tick driver.
    fn_engine_delayed_action_dispatch: 0x004472ce, // FUN_004472ce
    var_da_dispatch_table:          0x004d5c98, // PTR_FUN_004d5c98 — 50 × ptr (200 B, .rdata)
    var_da_master_switch:           0x00553030, //  DAT_00553030  — int gate (0 = bail)
    var_da_game_mode:               0x0057c8fc, //  DAT_0057c8fc  — int (2 enables MP reorder)
    var_da_local_player:            0x005681ec, //  DAT_005681ec  — int player slot
    var_da_action_counter:          0x00512f6c, //  DAT_00512f6c  — int (decremented per dispatch)
    var_da_current_idx:             0x0051590c, //  DAT_0051590c  — int (last dispatched record)
    var_da_records:                 0x0050d7c0, //  DAT_0050d7c0  — 400 × 0x38 (22400 B)
    var_da_queue:                   0x00515990, //  DAT_00515990  — 100 × i32 (400 B)
    var_da_queue_count:             0x00515910, //  DAT_00515910  — int
    var_da_hipri_stash:             0x0050d620, //  DAT_0050d620  — 100 × i32 (400 B)
    var_da_hipri_count:             0x00512f58, //  DAT_00512f58  — int
    // 10 dispatch-argument mirrors — retail copies record.arg0..arg9 here
    // immediately before invoking the per-type handler.  Scattered addresses
    // (cf. record's contiguous arg0..arg9); we list them in arg-order so the
    // python orchestrator + ctypes binding can match by index.
    var_da_disp_a0:                 0x005158e8, //  DAT_005158e8  ← record.arg0
    var_da_disp_a1:                 0x005158a8, //  DAT_005158a8  ← record.arg1
    var_da_disp_a2:                 0x005158a4, //  DAT_005158a4  ← record.arg2
    var_da_disp_a3:                 0x005158b0, //  DAT_005158b0  ← record.arg3
    var_da_disp_a4:                 0x005158ac, //  DAT_005158ac  ← record.arg4
    var_da_disp_a5:                 0x005158bc, //  DAT_005158bc  ← record.arg5
    var_da_disp_a6:                 0x005158b4, //  DAT_005158b4  ← record.arg6
    var_da_disp_a7:                 0x005158c4, // _DAT_005158c4  ← record.arg7
    var_da_disp_a8:                 0x005158c0, // _DAT_005158c0  ← record.arg8
    var_da_disp_a9:                 0x0050d7bc, // _DAT_0050d7bc  ← record.arg9
    // ── Intro-movie skip (skip_intro flag) ──────────────────────────────
    // The boot intro chain (intro.smk → imptitle.smk → credits.smk → title
    // screen 0x1f) is engine-time-paced and dominates every menu/UI capture
    // run's warmup (~23 real-sec under turbo).  Rather than fast-forwarding
    // the movies, we force each one to its final frame so the engine runs its
    // OWN intro→title state transitions exactly as retail intends — the
    // FUN_0042e060 chaining (intro→imptitle→credits) plus the DAT_005530f0 /
    // DAT_0057d340 title-state setup — just without waiting hundreds of frames.
    //   FUN_0042dbc7 — per-frame Smacker stepper.  Its done branch
    //     (current_frame >= frame_count-1) closes the handle + calls
    //     FUN_0042e060 to chain the next movie / finish on 0x1f.
    //   FUN_0042da18 — the Smacker open; it calls FUN_0042dbc7 once at open
    //     time, BEFORE FUN_0042d91b writes the return-state global
    //     (_DAT_005169b0).  Forcing done during that nested call corrupts the
    //     return-state plumbing, so we bracket it with g_smack_in_open and
    //     skip forcing while inside it.
    //   DAT_004d4d58 — Smack* handle (0 when no movie open).  Struct fields
    //     +0x0c = frame count (int), +0x374 = current frame (uint) — both
    //     confirmed from FUN_0042dbc7's own frame-progress check.
    //   Gate on engine state == 1 (var_sim_game_state_phase, DAT_0052af94)
    //     so only the boot intro chain is affected, not in-game cutscenes.
    fn_smack_step:       0x0042dbc7, // FUN_0042dbc7 — per-frame Smacker stepper
    fn_smack_open:       0x0042da18, // FUN_0042da18 — Smacker open (steps once at open)
    var_smack_handle:    0x004d4d58, // DAT_004d4d58 — Smack* handle (0 = none)
    // _SmackWait import (smackw32 Ordinal_32) IAT slot.  Returns 0 = "frame
    // ready", nonzero = "not ready" (FUN_0042dbc7 returns early on nonzero,
    // never reaching its done→chain branch).  Under silent-audio / non-turbo
    // the decode thread never reports ready, so the frame-snap alone leaves the
    // movie stuck at screen 0x22.  We force this to 0 during skip so the
    // stepper always reaches the (snapped) last-frame done branch.
    var_smack_wait_iat:  0x005cf5d8, // IAT slot → smackw32 _SmackWait
};

// Per-slot strides — used inside the runRetailSimSubSteps RPC + the LUT
// install/uninstall helpers.  Sizes verified against the Ghidra retail
// decompile (FUN_004bb978 / FUN_0043de70 access patterns).
const SIM_PLAYER_SLOT_COUNT      = 7;            // slots 0..6 (1..6 used)
const SIM_PLAYER_ACTIVE_STRIDE   = 0x160;        // bytes
const SIM_PLAYER_DISCONN_STRIDE  = 0x2c;         // bytes
const SIM_PLAYER_TICKS_STRIDE    = 4;            // bytes
const SIM_RATE_LUT_BYTES         = 2400;         // (14*0xa0 + 0x9f) + 1 → 0x960

// State-hash source-array sizes (matches docs/findings/save-format.md
// entry 4/3/5/7/8/9).  Pre-multiplied: (max_slot + 1) × stride so the
// install helper can snapshot the full retail-side blocks at fixed
// offsets.  Combined: 125,468 bytes — snapshotted once per test plan.
const STATE_HASH_PER_ARMY_SLOTS         = 17;
const STATE_HASH_PER_ARMY_STRIDE        = 0x300;     // 768
const STATE_HASH_PER_ARMY_BYTES         = STATE_HASH_PER_ARMY_SLOTS * STATE_HASH_PER_ARMY_STRIDE;
const STATE_HASH_PLAYER_STRATEGIC_SLOTS = 6;
const STATE_HASH_PLAYER_STRATEGIC_STRIDE = 0x160;    // 352
const STATE_HASH_PLAYER_STRATEGIC_BYTES = STATE_HASH_PLAYER_STRATEGIC_SLOTS * STATE_HASH_PLAYER_STRATEGIC_STRIDE;
const STATE_HASH_PER_TERRITORY_SLOTS    = 151;
const STATE_HASH_PER_TERRITORY_STRIDE   = 0x1a4;     // 420
const STATE_HASH_PER_TERRITORY_BYTES    = STATE_HASH_PER_TERRITORY_SLOTS * STATE_HASH_PER_TERRITORY_STRIDE;
const STATE_HASH_PER_BATTLE_HEX_SLOTS   = 81;
const STATE_HASH_PER_BATTLE_HEX_STRIDE  = 0x1b0;     // 432
const STATE_HASH_PER_BATTLE_HEX_BYTES   = STATE_HASH_PER_BATTLE_HEX_SLOTS * STATE_HASH_PER_BATTLE_HEX_STRIDE;
const STATE_HASH_PER_COMBAT_UNIT_SLOTS  = 101;
const STATE_HASH_PER_COMBAT_UNIT_STRIDE = 0x4c;      // 76
const STATE_HASH_PER_COMBAT_UNIT_BYTES  = STATE_HASH_PER_COMBAT_UNIT_SLOTS * STATE_HASH_PER_COMBAT_UNIT_STRIDE;
const STATE_HASH_PER_BATTLE_AUX_SLOTS   = 81;
const STATE_HASH_PER_BATTLE_AUX_STRIDE  = 0x34;      // 52
const STATE_HASH_PER_BATTLE_AUX_BYTES   = STATE_HASH_PER_BATTLE_AUX_SLOTS * STATE_HASH_PER_BATTLE_AUX_STRIDE;
const STATE_HASH_BUCKET_BYTES           = 10;         // per player_slot
const STATE_HASH_REGION_BYTES           = 600;        // per player_slot

// Castle-load footprint expansion byte-counts (FUN_004917dd state surface).
// tiles + footprint_list + last_footprint are the read+write inputs (need
// pre-seeding per call so each vector starts from a known baseline);
// path_scratch + stage_scratch are pure outputs (fully recomputed from
// tiles by FUN_00471c1f / FUN_00471d30) so they don't need pre-seeding.
// Backups for ALL five are snapshotted at install so retail's pre-test
// memory is restored cleanly at uninstall.  Combined: 70556 bytes.
const TILE_EXPAND_TILES_BYTES           = 51200;      // 6400 × 8
const TILE_EXPAND_PATH_SCRATCH_BYTES    = 12800;      // 6400 × u16
const TILE_EXPAND_STAGE_SCRATCH_BYTES   = 6400;       // 6400 × u8
const TILE_EXPAND_FOOTPRINT_LIST_BYTES  = 152;        // 19 × 2 × i32
const TILE_EXPAND_LAST_FOOTPRINT_BYTES  = 4;          // 1 × i32

// Delayed-action dispatcher (FUN_004472ce) state-surface byte counts.
// records is 400 × 0x38; the 50-entry dispatch table is 50 × i32 ptrs.
// Queue/hipri_stash are 100 × i32 each; the 10 dispatch-arg mirrors are
// scattered across .data (snapshotted individually).
const DA_RECORD_BYTES               = 0x38;            // per-slot stride
const DA_RECORD_COUNT               = 400;
const DA_RECORDS_BYTES              = DA_RECORD_BYTES * DA_RECORD_COUNT; // 22400
const DA_QUEUE_BYTES                = 400;             // 100 × i32
const DA_HIPRI_BYTES                = 400;             // 100 × i32
const DA_DISPATCH_TABLE_LEN         = 50;
const DA_DISPATCH_TABLE_BYTES       = DA_DISPATCH_TABLE_LEN * 4;     // 200
const DA_DISP_MIRROR_COUNT          = 10;

// ─── runtime state ──────────────────────────────────────────────────────

let g_base = null;
let g_start_real_ms = Date.now();

let g_hide_window         = false;
let g_owned_hwnds         = {};   // key = hwnd hex, val = {source}

let g_silent_audio_enabled = false;
let g_force_windowed       = false;
// When force_windowed is on, we normally also force DAT_0053f228 = 0 so the
// engine takes its windowed-style branch.  But that flag ALSO selects retail's
// 8bpp palette builder (FUN_0042f449, reserved-range blackout) over the
// full-color one (FUN_0042f306, which loads the .256 into idx 1..254 — the
// dark-red caps + bright-red hover highlight a real player sees).  Set this to
// capture retail's *full-color* rendering while staying windowed (no fullscreen
// screen-hog): we keep the cooperative-level rewrite but skip the flag
// override, so retail renders the FUN_0042f306 palette into the windowed back
// surface we grab.  The window becomes a desktop-res popup (cosmetic) but the
// captured back-surface pixels are palette-correct.  See engine-quirks #115.
let g_keep_retail_palette  = false;
let g_turbo_enabled        = false;
let g_gdi_seat_enabled     = false;

// Agent-driven continuous GDI present (interactive verification only).
// When enabled, engine_tick.onLeave calls presentBackToWindowViaGdi
// every g_agent_present_every_ticks engine_ticks regardless of whether
// retail itself Blts to the primary.  Niche: lets a human see the
// engine render during Smacker intro / other stretches where retail's
// own rate-gate is suppressed.  Pairs with g_show_window_after_ms so
// the window doesn't stay hidden after init.
let g_agent_present_enabled    = false;
let g_agent_present_every_ticks = 100;     // ~20 Hz at default turbo step

// Continuous frame streaming for the recorder.  When on, engine_tick.onLeave
// captures + sends a frame at most every g_stream_interval_ms REAL ms — decoupled
// from the daemon's command channel (probe_read/click) so recording does NOT
// starve driving the way a daemon-side probe_shot poll-loop does.  A real-time
// gate keeps fps stable regardless of the (turbo-variable) tick rate.
let g_stream_enabled     = false;
let g_stream_interval_ms = 33;      // ~30 fps
let g_last_stream_ms     = 0;
let g_show_window_after_ms     = 0;        // 0 = never auto-show
let g_show_window_client_w     = 640;      // post-show client size — engine's
let g_show_window_client_h     = 480;      // native render res, 1:1 pixel-perfect
let g_show_window_done         = false;
let g_show_window_latch        = false;    // one-shot bypass for hide hooks
let g_nf_show_window           = null;

// Interactive seat (real-mouse play).  When enabled, engine_tick.onLeave
// forces the main-loop present gate open (var_app_active=1,
// var_surface_lost=0) every tick so the engine runs full-speed and
// processes input regardless of whether the frida-spawned window ever won
// foreground/WM_ACTIVATEAPP — without this it Sleep(200)s and feels frozen.
// Pairs with g_agent_present_enabled (GDI seat shows the frames) and
// g_show_window_after_ms (window made visible).
let g_force_active             = false;
let g_nf_set_foreground        = null;
let g_nf_get_client_rect_fa    = null;     // GetClientRect for the scale fix
let g_fa_rect_buf              = null;     // scratch RECT for the scale fix

// Turbo v2 — virtualised winmm!timeGetTime, gated to the engine main thread.
// `g_virtual_now_ms` is bumped by `g_turbo_step_ms` on every per-tick driver
// entry; main-thread timeGetTime calls have their retval overwritten with
// it. Side threads (DPlay, Smacker, DSound mixer) keep wall-clock. See the
// rationale block above installVirtualClockHook(). Mirrors OpenMare's
// pattern (openmare-agent.js installFrameCounterHook / installVirtualClockHook).
let g_turbo_step_ms       = 17;
let g_virtual_now_ms      = 0;
let g_clock_virtualised   = false;
let g_main_thread_id      = -1;   // captured on first per-tick driver entry

// FPS sample emission. Once the engine's cumulative_seconds counter ticks,
// we publish the engine's FPS readout so the harness can plot the trajectory
// without having to spelunk memory itself.
let g_last_fps_cum_secs   = 0;
let g_fps_sample_count    = 0;

// Frame capture. Lazy: only resolves NativeFunction stubs the first time the
// engine's DDraw globals (var_dd_primary / _back / _palette) are non-NULL.
// Capture cadence rides the FPS-sample heartbeat — one frame per engine
// cumulative-seconds rollover, but only AFTER g_frame_capture_warmup_secs
// virtual-seconds have elapsed (lets the engine finish DDraw bring-up +
// first-screen renders before we start sampling — captures pre-render are
// just the uninitialised back surface, which paints uniformly black).  At
// default turbo step (17 ms/tick) the per-real-second yield is ~500
// rollovers, so cap via g_frame_capture_max for longer runs.
let g_frame_capture_enabled    = false;
let g_frame_capture_max        = 0;   // 0 = unlimited
let g_frame_capture_count      = 0;
let g_frame_capture_warmup_secs = 0;
// One-shot globals dump — list of {va, size, label}; read once at the
// first captured frame and emitted as a `globals_dump` message.
let g_dump_globals             = [];
let g_globals_dumped           = false;
// One-shot large-region dump — list of {va, size, label, deref}; read once
// at the first captured frame and emitted as one `globals_blob` message per
// region with the raw bytes as a binary side payload (so 300 KB sprite banks
// don't have to round-trip as a JSON number array).  `deref` reads the 32-bit
// pointer stored at `va` first, then reads `size` bytes from that target —
// used for the heap-allocated PL8/RLE sprite banks reached via DAT_* pointers.
let g_dump_regions             = [];
let g_regions_dumped           = false;
// Globals poke — list of {va, bytes}; written into engine memory at the
// top of every tick so the value survives the engine's own writes (used
// to force scenario state, e.g. a name-box string, before a capture).
let g_poke_globals             = [];
let g_nf_surface_lock        = null;
let g_nf_surface_unlock      = null;
let g_nf_palette_get_entries = null;
let g_capture_scratch        = null;   // pre-allocated payload buffer
let g_last_capture_w         = 0;      // dims of the last captureFrameOnce frame
let g_last_capture_h         = 0;

// Trace Studio v3 draw-stepping (retail side). A CModule mirrors the port's
// gfx_drawcap: at armed (capture) ticks, each hooked dispatch diffs the locked
// back buffer (DAT_004ea1a8) against a running shadow, accumulating per-draw
// write-deltas; engine_tick.onLeave flushes + serializes them into the same
// OLDR sidecar the port emits. Native (CModule) so the per-dispatch diff +
// not-armed early-out cost nothing on the hot path.
let g_capture_draws          = false;
let g_capture_force_redraw   = false;  // force full strat re-render on capture ticks (DAT_0057d340=2)
// Suppress retail's mouse edge-scroll during capture. The strat map edge-scroll
// (FUN_00432221 → FUN_00431f59) reads the LIVE Windows cursor (GetCursorPos,
// screen coords) vs GetSystemMetrics screen bounds, so in a headless capture a
// host cursor parked at a screen edge scrolls the map one cell — a
// nondeterministic divergence from the port (which has no edge-scroll). Neutering
// FUN_00432221 (→ return 0, "did not scroll") makes the captured camera the
// faithfully-loaded save scroll on both engines. Default off (normal interactive
// runs keep edge-scroll); olv3 parity captures turn it on.
let g_suppress_edge_scroll   = false;
let g_edge_scroll_cb         = null;   // keep the NativeCallback alive (no GC)
let g_drawcap_cm             = null;   // the CModule
let g_dc_begin               = null;   // NativeFunction wrappers
let g_dc_flush_ptr           = null;
let g_dc_end                 = null;
let g_drawcap_active          = false; // CModule installed + hooks attached
// Big buffers are JS-allocated (Memory.alloc → committed) and passed to the
// CModule as symbols, NOT CModule static BSS (multi-MB CModule BSS is not
// reliably backed — it faulted at a fixed static-array boundary). The JS owns
// them, so drawcapSend reads them directly.
let g_dc_base                = null;   // base plane (W*H)
let g_dc_shadow              = null;   // running canvas mirror (W*H)
let g_dc_offs                = null;   // changed-pixel offsets (MAXW u32)
let g_dc_vals                = null;   // changed-pixel values  (MAXW u8)
let g_dc_hdrs                = null;   // per-draw headers (MAXH × 6 u32)
let g_dc_state               = null;   // scalar state int[16] (CModule globals aren't writable)
// dc_st[] indices — MUST match the CModule's S_* #defines.
const DC_S_ARMED = 0, DC_S_PENDING = 1, DC_S_HAVEBASE = 2, DC_S_W = 3, DC_S_H = 4,
      DC_S_NHDR = 5, DC_S_NWRITE = 6, DC_S_OVERFLOW = 7,
      // 8..11 = OP/M0/M1/M2 (CModule-internal staging). 12/13 = the FUN_00408438
      // return-address window for the map-structure leaf gate (see
      // installDrawCaptureHooks / dc_on_map_sprite).
      DC_S_RA_LO = 12, DC_S_RA_HI = 13;

// Per-tick anchor counter. Bumped in engine_tick.onEnter (1-indexed: equals
// the tick number currently executing). Used both as the timestamp for
// recorded input events and as the schedule index for replaying them.
let g_tick_count          = 0;

// ─── deterministic anchors (TAS framework P1) ───────────────────────────
// Emit a named event whenever an observable engine state transition occurs,
// keyed on g_tick_count (== the port's g_frame_counter at the same
// FUN_004b99c0 boundary).  The anchor stream lets the harness align port and
// retail by *event* instead of absolute frame, and localises the first
// divergence.  See docs/plans/tas-framework.md.
//   engine_state = DAT_0052af94 (var_sim_game_state_phase): 1 boot-intro,
//                  3 INGAME, 0xa saving, 0xb loading.
//   screen_id    = DAT_004eac50 (var_sim_current_screen):   0x1f title,
//                  0 strategic-map/menu, 0x28..0x2a loading.
let g_anchor_enabled     = false;
let g_anchor_booted      = false;
let g_anchor_prev_state  = -1;
let g_anchor_prev_screen = -1;

// Input recording. When armed, the WndProc hook emits one `input_event`
// per dispatched input message (WM_KEY*/WM_CHAR/WM_MOUSE*), stamped with
// g_tick_count at moment of dispatch.  The dispatch happens *between*
// engine_tick calls (engine's main loop is PeekMessage → DispatchMessage
// → engine_tick), so the stamp identifies "the most recent tick before
// the event was consumed by the engine".  Replay reverses this: posting
// in engine_tick.onEnter (right after g_tick_count is bumped) makes the
// messages get processed before the next engine_tick — matching the
// recording's timing.
let g_input_record_enabled = false;

// Intro-movie skip (skip_intro flag).  See the ADDR.fn_smack_step block for
// the full rationale.  g_smack_in_open brackets the open-time FUN_0042dbc7
// call so it isn't fast-finished; g_skip_intro_forced counts the movies we've
// short-circuited (for a bounded log).
let g_skip_intro_enabled   = false;
let g_smack_in_open        = 0;       // >0 while inside FUN_0042da18
let g_skip_intro_forced    = 0;

// Input replay. Holds the trace as an array of {t, m, w, l} objects sorted
// by t. g_replay_idx advances as events are posted.  The PostMessageA stub
// is resolved lazily on first use (user32.dll is loaded before we ever get
// here because the engine itself imports it).
let g_input_replay_events  = null;     // array | null
let g_replay_idx           = 0;
let g_replay_post_count    = 0;
let g_nf_post_message_a    = null;

// Pure-function differential test machinery (Phase 4 of harness-roadmap).
// `diff_test` init opt installs:
//   - the always-on timeGetTime hook (its onLeave checks a scripted-return
//     queue before falling through to the turbo virtual clock — lets us
//     control exactly what FUN_004b27bf sees on its two-call rollover path)
//   - the runRetailRngStep / runRetailFpsStep RPC entrypoints (always live in
//     rpc.exports, but they refuse to fire if `g_diff_test_enabled` is false,
//     so a normal capture run can't accidentally trip them)
//
// The clock queue holds u32 timestamps for the next N timeGetTime calls.
// When non-empty, the queue is drained head-first (FIFO) and the dequeued
// value is what timeGetTime returns; once empty, the existing virtual /
// wall-clock path takes over.  Diff tests push a small batch (1 or 2
// values) per call; they should never leak into the post-test engine state
// because the tests run synchronously and clear the queue at finish.
//
// NativeFunction stubs are resolved lazily on first call: the resolution
// has to happen AFTER g_base is set (RVA translation depends on it).
let g_diff_test_enabled       = false;
let g_diff_test_clock_queue   = [];     // [u32 ms, ...] consumed by timeGetTime hook
// Scripted cursor queue — parallel to clock_queue but for the user32!
// GetCursorPos hook (FUN_004b191e calls it exactly once at entry).  Entries
// are {x: i32, y: i32}; queue is drained FIFO from the GetCursorPos onLeave
// when the calling thread matches the diff RPC's V8 thread and the queue
// is non-empty.  See installGetCursorPosHook below for the hook body.
let g_diff_test_cursor_queue  = [];
// Only the thread currently executing a diff RPC may consume the scripted
// clock queue.  Without this gate the engine main thread (still ticking
// during diff tests) would race on the queue and steal scripted values
// intended for the retail function call we're driving.  Set non-null at
// the top of each diff RPC, cleared in its `finally`.
let g_diff_test_caller_tid    = -1;
let g_nf_retail_rng_step      = null;
let g_nf_retail_compute_fps   = null;
let g_nf_retail_anim_cycle    = null;
let g_nf_retail_frame_delta   = null;
let g_nf_retail_phase_ticks   = null;
let g_nf_retail_input_state   = null;
let g_nf_retail_sim_sub_steps = null;
let g_nf_retail_state_hash    = null;
let g_nf_retail_tile_expand   = null;
let g_nf_retail_delayed_action = null;

// Sim sub-step rate-gate LUT install state.  installSimSubStepsRateLut() is
// a one-shot per test plan: snapshot the original 2400 bytes once, write the
// orchestrator-supplied bytes, leave installed until uninstallSimSubStepsRateLut()
// flushes the snapshot back.  Lazy snapshot lets re-runs of the test plan
// re-install without piling up duplicate backups.
let g_sim_rate_lut_backup = null;       // Uint8Array of original bytes (or null)

// Engine-tick freeze state.  installSimSubStepsEngineFreeze() replaces
// FUN_0040e890 with a no-op so the engine main thread skips the per-tick
// driver entirely.  Necessary during sim_sub_steps diff vectors because:
//   - we inject game_state_phase=3 (ingame), mp_active=1, and per-player
//     arrays into globals retail's engine main thread also reads;
//   - if engine_tick fires during the inject→call→readback→restore window,
//     its own FUN_004bb978 returns >0 (driven by our injected state) and
//     the sub-step body runs — calls retail's sim sub-functions that
//     expect a fully-initialised ingame state which we have NOT provided.
//   - those functions then crash on unmapped pointer reads (DAT_0050d7c0
//     deferred-action table, DAT_0057bf04+offset player struct internals).
// Freezing engine_tick guarantees no parallel calls reach the sub-step body,
// at the cost of suspending sim/render ticking entirely for the duration.
// Pair install with uninstall to restore normal behavior.
let g_sim_engine_freeze_callback = null;   // NativeCallback (or null = not installed)

// State-hash source-array install state.  installStateHashSources() snapshots
// retail's 6 source arrays once (lazy on first call), then fills them with
// the orchestrator-supplied deterministic bytes (xorshift32 from a seed).
// uninstallStateHashSources() flushes the snapshots back.  Combined snapshot
// size is ~125 KB; one snapshot per test plan keeps the per-vector RPC lean.
let g_state_hash_per_army_backup           = null;
let g_state_hash_player_strategic_backup   = null;
let g_state_hash_per_territory_backup      = null;
let g_state_hash_per_battle_hex_backup     = null;
let g_state_hash_per_combat_unit_backup    = null;
let g_state_hash_per_battle_aux_backup     = null;

// Tile-expand backup state.  installTileExpandBackup() snapshots the 5
// globals it touches once (lazy on first install); uninstallTileExpandBackup()
// flushes them back at end of test plan.  Per-call pre-state seeding goes
// straight to the live globals — the snapshot only exists to put retail
// back to a consistent pre-test state when the test plan finishes.
let g_tile_expand_tiles_backup             = null;
let g_tile_expand_path_scratch_backup      = null;
let g_tile_expand_stage_scratch_backup     = null;
let g_tile_expand_footprint_list_backup    = null;
let g_tile_expand_last_footprint_backup    = null;

// Delayed-action backup state.  installDelayedActionStub() snapshots the
// 50-entry dispatch table (so we can restore retail's real handlers at
// uninstall) plus the 5 scalars / 3 arrays / 10 dispatch-arg mirrors that
// per-call writes touch — so retail's pre-test state survives the test
// plan even though every call overwrites all of them.  Also allocates a
// single no-op NativeCallback once and stamps it into every dispatch-
// table slot, so the dispatcher's `(*table[type])()` is harmless. */
let g_da_dispatch_table_backup     = null;   // Uint8Array(200)
let g_da_records_backup            = null;   // Uint8Array(22400)
let g_da_queue_backup              = null;   // Uint8Array(400)
let g_da_hipri_backup              = null;   // Uint8Array(400)
let g_da_scalar_backup             = null;   // {master_switch, game_mode, ...}
let g_da_disp_mirror_backup        = null;   // [10] of i32
let g_da_noop_handler              = null;   // NativeCallback (void, [], stdcall)

// engine_tick depth tracker — 1 while the engine main thread is executing
// FUN_004b99c0, 0 otherwise.  Used by per-tick race detection in diff RPCs
// for retail functions that the engine calls every tick (e.g. FUN_00451f1a):
// if the depth is non-zero at any point during the RPC, the engine may have
// fired the target function with our injected globals — retry on the
// orchestrator side.  Maintained inside the engine_tick interceptor; safe
// to read from the V8/RPC thread.
let g_engine_in_tick          = 0;

// ─── helpers ────────────────────────────────────────────────────────────

function rva(va) { return g_base.add(va - IMAGE_BASE.toUInt32()); }
// Live in-module pointer → Ghidra VA (inverse of rva).  Used by mem_watch to
// report the faulting instruction / accessed-data addresses as Ghidra VAs.
function toGhidraVa(p) {
    return (p.sub(g_base).toUInt32() + IMAGE_BASE.toUInt32()) >>> 0;
}
function nowMs()  { return (Date.now() - g_start_real_ms) | 0; }
function log(msg) { send({kind: 'log',   msg: String(msg)}); }
function err(where, msg) { send({kind: 'error', where: String(where), msg: String(msg)}); }

function vtableSlot(thisPtr, idx) {
    return thisPtr.readPointer().add(idx * Process.pointerSize).readPointer();
}

// ─── owned-HWND tracking + window-hide multi-API hooks ──────────────────
// SW_HIDE = 0, SW_SHOW = 5 (WinUser.h, fixed since 9x).
// SWP_SHOWWINDOW = 0x40, SWP_HIDEWINDOW = 0x80.
// GWL_STYLE = -16, WS_VISIBLE = 0x10000000.
//
// "Process-owned" HWNDs = those returned from a user32!CreateWindowEx{A,W}
// we observed.  This excludes MessageBox / system error popups (kernel-
// internal create path) so they still surface for the human running the
// harness.  We rewrite all four show-path APIs (ShowWindow, ShowWindowAsync,
// SetWindowPos+SWP_SHOWWINDOW, SetWindowLong+WS_VISIBLE) when targeting
// owned HWNDs.

const SW_HIDE          = 0;
const SW_SHOWNORMAL    = 1;
const SW_SHOW          = 5;
const SWP_SHOWWINDOW   = 0x40;
const SWP_HIDEWINDOW   = 0x80;
const GWL_STYLE        = -16;
const WS_VISIBLE       = 0x10000000;

function hwndKey(h) { return h.toString(); }
function isOwnedHwnd(h) {
    return h && !h.isNull() && hwndKey(h) in g_owned_hwnds;
}

function markOwnedHwnd(h, source) {
    if (!h || h.isNull()) return;
    const k = hwndKey(h);
    if (k in g_owned_hwnds) return;
    g_owned_hwnds[k] = {source: source};
    send({kind: 'hwnd_owned', hwnd: k, source: source});
}

// CreateWindowEx argv (x86 stdcall, args[0..11]):
//   0  dwExStyle
//   1  lpClassName
//   2  lpWindowName
//   3  dwStyle       ← we strip WS_VISIBLE here if g_hide_window
//   4  x
//   5  y
//   6  nWidth
//   7  nHeight
//   8  hWndParent
//   9  hMenu
//   10 hInstance
//   11 lpParam
//
// Why strip WS_VISIBLE at create time rather than only hook ShowWindow:
// Lords2.exe (and many 1996-era engines) pass WS_VISIBLE to CreateWindowEx
// directly, so the window is shown *during* the create call — by the
// time our onLeave fires to register the hwnd in g_owned_hwnds, the
// window has already been displayed on screen.  ShowWindow never gets
// called separately, so a hide_window hook on ShowWindow alone is
// insufficient.  Verified 2026-05-24 against retail Lords2.exe: a
// borderless black window appeared during init despite all four
// show-path hooks being armed.

function installCreateWindowExHooks() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installCreateWindowExHooks', 'user32.dll not loaded'); return; }
    let count = 0;
    ['CreateWindowExA', 'CreateWindowExW'].forEach(function (name) {
        const fn = u32.findExportByName(name);
        if (!fn) { err('installCreateWindowExHooks', name + ' not found'); return; }
        Interceptor.attach(fn, {
            onEnter: function (args) {
                this.cls = null;
                this.wn  = null;
                this.stripped_visible = false;
                try {
                    const clsArg = args[1];
                    if (clsArg && !clsArg.isNull()) {
                        const lo = clsArg.toUInt32() & 0xffff;
                        const hi = (clsArg.toUInt32() >>> 16) & 0xffff;
                        if (hi === 0 && lo !== 0) {
                            this.cls = '#atom_' + lo.toString(16);
                        } else {
                            this.cls = (name === 'CreateWindowExA')
                                ? clsArg.readCString()
                                : clsArg.readUtf16String();
                        }
                    }
                    const wnArg = args[2];
                    if (wnArg && !wnArg.isNull()) {
                        this.wn = (name === 'CreateWindowExA')
                            ? wnArg.readCString()
                            : wnArg.readUtf16String();
                    }
                } catch (_) { /* unreadable strings are harmless */ }

                // Strip WS_VISIBLE pre-create if hide_window is on.
                // We don't have an hwnd yet to gate by isOwnedHwnd, so we
                // apply this to every CreateWindowEx from the target
                // process — that excludes MessageBox / system error popups
                // (they go through a kernel-internal path that doesn't
                // hit this export), which is exactly the dividing line
                // isOwnedHwnd was approximating.
                if (g_hide_window) {
                    const style = args[3].toUInt32();
                    if (style & WS_VISIBLE) {
                        const newStyle = (style & ~WS_VISIBLE) >>> 0;
                        args[3] = ptr(newStyle);
                        this.stripped_visible = true;
                    }
                }
            },
            onLeave: function (retval) {
                markOwnedHwnd(retval, name);
                if (retval && !retval.isNull()) {
                    send({kind:  'hwnd_owned_detail',
                          hwnd:  hwndKey(retval),
                          api:   name,
                          cls:   this.cls || '',
                          title: this.wn  || ''});
                    if (this.stripped_visible) {
                        send({kind: 'hide_force', api: name + '/WS_VISIBLE',
                              hwnd: hwndKey(retval), original: WS_VISIBLE});
                    }
                }
            },
        });
        count++;
    });
    log('CreateWindowEx tracking installed (' + count + '/2)');
}

function installWindowHideHooks() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installWindowHideHooks', 'user32.dll not loaded'); return; }

    const showWindow = u32.findExportByName('ShowWindow');
    if (showWindow) {
        Interceptor.attach(showWindow, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd = args[0];
                const cmd  = args[1].toInt32();
                if (cmd === SW_HIDE) return;
                if (!isOwnedHwnd(hwnd)) return;
                // One-shot bypass: when the agent is programmatically
                // showing the window post-init (g_show_window_after_ms
                // path), let the SW_SHOW through and clear the latch.
                if (g_show_window_latch) {
                    g_show_window_latch = false;
                    return;
                }
                args[1] = ptr(SW_HIDE);
                send({kind: 'hide_force', api: 'ShowWindow',
                      hwnd: hwndKey(hwnd), original: cmd});
            },
        });
    } else { err('installWindowHideHooks', 'ShowWindow not found'); }

    const showWindowAsync = u32.findExportByName('ShowWindowAsync');
    if (showWindowAsync) {
        Interceptor.attach(showWindowAsync, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd = args[0];
                const cmd  = args[1].toInt32();
                if (cmd === SW_HIDE) return;
                if (!isOwnedHwnd(hwnd)) return;
                args[1] = ptr(SW_HIDE);
                send({kind: 'hide_force', api: 'ShowWindowAsync',
                      hwnd: hwndKey(hwnd), original: cmd});
            },
        });
    }

    const setWindowPos = u32.findExportByName('SetWindowPos');
    if (setWindowPos) {
        Interceptor.attach(setWindowPos, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd  = args[0];
                if (!isOwnedHwnd(hwnd)) return;
                const flags = args[6].toInt32();
                if ((flags & SWP_SHOWWINDOW) === 0) return;
                const newFlags = (flags & ~SWP_SHOWWINDOW) | SWP_HIDEWINDOW;
                args[6] = ptr(newFlags);
                send({kind: 'hide_force', api: 'SetWindowPos',
                      hwnd: hwndKey(hwnd), original: flags});
            },
        });
    } else { err('installWindowHideHooks', 'SetWindowPos not found'); }

    ['SetWindowLongA', 'SetWindowLongW'].forEach(function (name) {
        const fn = u32.findExportByName(name);
        if (!fn) return;
        Interceptor.attach(fn, {
            onEnter: function (args) {
                if (!g_hide_window) return;
                const hwnd   = args[0];
                const nIndex = args[1].toInt32();
                if (nIndex !== GWL_STYLE) return;
                if (!isOwnedHwnd(hwnd)) return;
                const newLong = args[2].toInt32();
                if ((newLong & WS_VISIBLE) === 0) return;
                args[2] = ptr((newLong & ~WS_VISIBLE) >>> 0);
                send({kind: 'hide_force', api: name,
                      hwnd: hwndKey(hwnd), original: newLong});
            },
        });
    });

    log('hide_window hooks installed: ShowWindow + ShowWindowAsync + ' +
        'SetWindowPos + SetWindowLong{A,W}');
}

// ─── MessageBox → stderr redirect (always on) ─────────────────────────
// Same workflow rationale as the C-side dev_hooks in src/dev_hooks.c:
// any modal popup from the vendor exe (engine error path, missing
// DirectX, missing CD, save-game corruption) will stop the process
// dead in agent-mode runs where nobody's watching the screen.

let g_msgbox_a_cb = null;
let g_msgbox_w_cb = null;

function installMessageBoxRedirectHook() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installMessageBoxRedirect', 'user32.dll not loaded'); return; }

    const mbA = u32.findExportByName('MessageBoxA');
    if (mbA) {
        g_msgbox_a_cb = new NativeCallback(function (hwnd, text, caption, type) {
            let captionStr = '(empty)';
            let bodyStr    = '(empty)';
            try { if (!caption.isNull()) captionStr = caption.readAnsiString(); } catch (e) {}
            try { if (!text   .isNull()) bodyStr    = text   .readAnsiString(); } catch (e) {}
            send({kind: 'messagebox_intercepted',
                  api: 'MessageBoxA',
                  hwnd: hwnd.toString(),
                  caption: captionStr,
                  body: bodyStr,
                  type: type >>> 0});
            return 1;  // IDOK
        }, 'int', ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall');
        Interceptor.replace(mbA, g_msgbox_a_cb);
        log('msgbox_redirect: MessageBoxA → IDOK @ ' + mbA);
    } else {
        err('installMessageBoxRedirect', 'user32!MessageBoxA not exported');
    }

    const mbW = u32.findExportByName('MessageBoxW');
    if (mbW) {
        g_msgbox_w_cb = new NativeCallback(function (hwnd, text, caption, type) {
            let captionStr = '(empty)';
            let bodyStr    = '(empty)';
            try { if (!caption.isNull()) captionStr = caption.readUtf16String(); } catch (e) {}
            try { if (!text   .isNull()) bodyStr    = text   .readUtf16String(); } catch (e) {}
            send({kind: 'messagebox_intercepted',
                  api: 'MessageBoxW',
                  hwnd: hwnd.toString(),
                  caption: captionStr,
                  body: bodyStr,
                  type: type >>> 0});
            return 1;  // IDOK
        }, 'int', ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall');
        Interceptor.replace(mbW, g_msgbox_w_cb);
        log('msgbox_redirect: MessageBoxW → IDOK @ ' + mbW);
    } else {
        err('installMessageBoxRedirect', 'user32!MessageBoxW not exported');
    }
}

// ─── Save-file sandbox redirect (reproducibility) ─────────────────────
// Redirect every save-file open (the .sav backup family + Castles.dat) into a
// per-run sandbox dir so a trace-capture run NEVER reads or writes the real
// game-dir saves.  Retail's save_load (FUN_004adfe6) writes Castles.dat from
// the loaded .sav tail and the turn-save rotation (FUN_0049a453) overwrites
// lastturn/old_turn/safeturn — both MUTATE the install dir, which made captures
// non-reproducible and once corrupted the test reference (see memory
// project_save_redirect_reproducibility).  The fix mirrors openrecet's
// installSaveRedirectHook: hook kernel32!CreateFileW + CreateFileA (the CRT's
// fopen funnels through one of these regardless of static/dynamic CRT linkage),
// and on a matching open rewrite the path argument in place to
// <sandbox>\<basename>.  The save LIST still enumerates the real dir (we don't
// hook FindFirstFile), so the harness pre-seeds the sandbox with the reference
// under every name a list row might select — whichever row the trace clicks,
// the OPEN lands on the seeded reference.  The port mirror is save.c
// save_set_sandbox (--save-sandbox).
let g_save_sandbox = null;            // Windows path to the per-run sandbox dir
const g_save_redirect_keep = [];      // keep redirected path buffers alive

function installSaveRedirectHook(sandboxWin) {
    sandboxWin = String(sandboxWin).replace(/[\\/]+$/, '');

    function saveBase(name) {
        const m = /[^\\/]*$/.exec(name);
        return m ? m[0] : name;
    }
    function isSaveBase(name) {
        const b = saveBase(name).toLowerCase();
        // The SP/MP turn-save families (.sav/.svb/.sva) + the castle DB. Match
        // by suffix so any save the menu list opens is caught; "castles.dat" is
        // matched exactly so other .dat assets (L2_maps.dat, …) are untouched.
        return b === 'castles.dat'
            || b.endsWith('.sav') || b.endsWith('.svb') || b.endsWith('.sva');
    }

    function attachOne(exportName, wide) {
        // Frida 17.x: resolve the module instance first, then the export off it
        // (the legacy Module.getExportByName(name, export) global was removed).
        const k32 = Process.findModuleByName('kernel32.dll');
        const addr = k32 ? k32.findExportByName(exportName) : null;
        if (!addr) { err('saveRedirect', 'no ' + exportName); return; }
        Interceptor.attach(addr, {
            onEnter: function (args) {
                try {
                    const p = args[0];
                    if (p.isNull()) return;
                    const orig = wide ? p.readUtf16String() : p.readAnsiString();
                    if (!orig || !isSaveBase(orig)) return;
                    const np = sandboxWin + '\\' + saveBase(orig);
                    const buf = wide ? Memory.allocUtf16String(np)
                                     : Memory.allocAnsiString(np);
                    g_save_redirect_keep.push(buf);   // outlive the call
                    args[0] = buf;
                    log('saveRedirect ' + exportName + ': ' + orig + ' -> ' + np);
                } catch (e) { err('saveRedirect.onEnter', e.message); }
            }
        });
    }

    attachOne('CreateFileW', true);
    attachOne('CreateFileA', false);
    log('save redirect armed → sandbox ' + sandboxWin);
}

// ─── force windowed — three layers ─────────────────────────────────────
// Lords II is a 1996 DDraw-1.x game.  It uses the legacy
// `IDirectDraw::SetCooperativeLevel` API to take exclusive fullscreen
// (`DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN`) before calling `SetDisplayMode`
// to switch the desktop to 640×480 (or 800×600 patched) at 256-color.
//
// On a modern Windows 10/11 host that's a heavyweight mode-switch that
// minimises other windows and disrupts the entire desktop.  For the
// dev loop, we want windowed mode with no resolution change.
//
// Strategy (three layers, install all of them; any one is sufficient
// but defense in depth):
//
//   L1 (GDI):    stub user32!ChangeDisplaySettings*A/W
//   L2 (DDraw):  wrap ddraw!DirectDrawCreate{,Ex} so we can patch the
//                returned IDirectDraw / IDirectDraw7 vtable's
//                SetCooperativeLevel slot to strip the EXCLUSIVE +
//                FULLSCREEN bits.
//   L3 (extra):  hook ddraw!SetDisplayMode (only if the engine somehow
//                calls it directly — defensive; Lords II goes through
//                the COM interface, not a flat export).
//
// DDraw cooperative-level flags (ddraw.h):
//   DDSCL_FULLSCREEN  = 0x0001
//   DDSCL_EXCLUSIVE   = 0x0010
//   DDSCL_NORMAL      = 0x0008
// We want NORMAL (windowed), so OR in 0x08 and strip 0x01 | 0x10.
const DDSCL_FULLSCREEN = 0x0001;
const DDSCL_EXCLUSIVE  = 0x0010;
const DDSCL_NORMAL     = 0x0008;

let g_ddraw1_set_cooplevel_orig = null;
let g_ddraw7_set_cooplevel_orig = null;
let g_dsound_set_cooplevel_orig = null;

function installChangeDisplaySettingsStub() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('installForceWindowed[L1]', 'user32.dll not loaded'); return; }
    const DISP_OK = 0;  // DISP_CHANGE_SUCCESSFUL
    let stubbed = 0;
    ['ChangeDisplaySettingsA', 'ChangeDisplaySettingsW'].forEach(function (name) {
        const exp = u32.findExportByName(name);
        if (!exp) return;
        Interceptor.replace(exp, new NativeCallback(function (devmode, flags) {
            send({kind: 'display_settings_blocked', api: name, flags: flags >>> 0});
            return DISP_OK;
        }, 'int32', ['pointer', 'uint32'], 'stdcall'));
        stubbed++;
    });
    ['ChangeDisplaySettingsExA', 'ChangeDisplaySettingsExW'].forEach(function (name) {
        const exp = u32.findExportByName(name);
        if (!exp) return;
        Interceptor.replace(exp, new NativeCallback(function (devname, devmode, hwnd, flags, param) {
            send({kind: 'display_settings_blocked', api: name, flags: flags >>> 0});
            return DISP_OK;
        }, 'int32', ['pointer', 'pointer', 'pointer', 'uint32', 'pointer'], 'stdcall'));
        stubbed++;
    });
    log('force_windowed[L1]: ' + stubbed + '/4 ChangeDisplaySettings* stubbed');
}

// Wrap IDirectDraw{,7}::SetCooperativeLevel — slot 0x14 (20) in both the
// IDirectDraw and IDirectDraw7 vtables (it's at the same vtable offset
// in every version of the interface; see ddraw.h IUnknown(3) + the seven
// pre-SetCooperativeLevel methods).
//
// Slot offsets (DDraw interface, identical layout across DDraw1..7):
//   0   QueryInterface
//   1   AddRef
//   2   Release
//   3   Compact
//   4   CreateClipper
//   5   CreatePalette
//   6   CreateSurface
//   7   DuplicateSurface
//   8   EnumDisplayModes
//   9   EnumSurfaces
//   10  FlipToGDISurface
//   11  GetCaps
//   12  GetDisplayMode
//   13  GetFourCCCodes
//   14  GetGDISurface
//   15  GetMonitorFrequency
//   16  GetScannerLine
//   17  GetVerticalBlankStatus
//   18  Initialize
//   19  RestoreDisplayMode
//   20  SetCooperativeLevel              ← here, x86 byte offset 0x50
//   21  SetDisplayMode                    ← x86 byte offset 0x54
//
// Be conservative: only patch each unique vtable pointer once.

const g_patched_ddraw_vtables = {};

function patchDDrawVtable(ifacePtr, ifaceName) {
    try {
        const vtable = ifacePtr.readPointer();
        const vtKey  = vtable.toString();
        if (vtKey in g_patched_ddraw_vtables) return;
        g_patched_ddraw_vtables[vtKey] = true;

        // SetCooperativeLevel @ slot 20 ⇒ byte offset 20 * sizeof(void*) = 0x50
        const setCoopLevel = vtable.add(20 * Process.pointerSize).readPointer();
        Interceptor.attach(setCoopLevel, {
            onEnter: function (args) {
                const hwnd     = args[1];
                const original = args[2].toUInt32();
                // Strip EXCLUSIVE + FULLSCREEN; force NORMAL.
                const forced =
                    (original & ~(DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)) | DDSCL_NORMAL;
                args[2] = ptr(forced);
                // Override the fullscreen-flag global so the engine's
                // post-SetCooperativeLevel branch (which inspects
                // DAT_0053f228 to choose windowed-style vs. popup-style
                // window) takes the windowed path: overlapped-window
                // style + 640×480 centered.  Without this, retail's
                // auto-fullscreen-promote on non-8bpp desktops leaves
                // the global set to 1 and our window ends up as a
                // borderless popup squashed to desktop res.  See
                // engine-quirks.md #1 / ddraw-dsound-bringup.md.
                //
                // ...unless keep_retail_palette is set: then we leave the flag
                // at 1 so the engine builds the full-color palette
                // (FUN_0042f306, red caps + red hover highlight) instead of the
                // 8bpp blackout (FUN_0042f449).  The window degrades to a
                // desktop-res popup, but the back-surface pixels we capture are
                // palette-correct.  See engine-quirks #115.
                if (!g_keep_retail_palette) {
                    try { rva(ADDR.var_fullscreen_flag).writeU32(0); }
                    catch (e) { err('force_windowed.flag_override', e.message); }
                } else {
                    log('force_windowed: keep_retail_palette on — leaving '
                        + 'DAT_0053f228 = 1 (full-color FUN_0042f306 palette)');
                }
                send({kind: 'ddraw_cooplevel_rewrite',
                      iface: ifaceName,
                      hwnd:  hwnd.toString(),
                      original: original,
                      forced:   forced});
            },
        });

        // SetDisplayMode @ slot 21 ⇒ byte offset 0x54 — make it a no-op too.
        // Some games call this even in windowed mode; for our case we never
        // want a resolution change.
        const setDispMode = vtable.add(21 * Process.pointerSize).readPointer();
        Interceptor.replace(setDispMode, new NativeCallback(function (self, w, h, bpp) {
            send({kind: 'log',
                  msg: 'force_windowed: ' + ifaceName +
                       '::SetDisplayMode(' + w + 'x' + h + 'x' + bpp + ') blocked'});
            return 0;  // DD_OK
        }, 'int32', ['pointer', 'uint32', 'uint32', 'uint32'], 'stdcall'));

        log('force_windowed[L2]: ' + ifaceName +
            ' vtable patched (SetCooperativeLevel @ ' + setCoopLevel + ', SetDisplayMode stubbed)');
    } catch (e) {
        err('patchDDrawVtable', e.message);
    }
}

function installForceWindowedDDraw() {
    function armDDrawCreate() {
        const ddraw = Process.findModuleByName('ddraw.dll');
        if (!ddraw) return false;

        const ddc = ddraw.findExportByName('DirectDrawCreate');
        if (ddc) {
            Interceptor.attach(ddc, {
                onEnter: function (args) {
                    // args[1] = *LPDIRECTDRAW (out param)
                    this.outPP = args[1];
                },
                onLeave: function (retval) {
                    if (retval.toInt32() !== 0) return;
                    if (!this.outPP || this.outPP.isNull()) return;
                    const iface = this.outPP.readPointer();
                    if (iface.isNull()) return;
                    patchDDrawVtable(iface, 'IDirectDraw');
                },
            });
            log('force_windowed[L2]: ddraw!DirectDrawCreate armed');
        }

        const ddcEx = ddraw.findExportByName('DirectDrawCreateEx');
        if (ddcEx) {
            Interceptor.attach(ddcEx, {
                onEnter: function (args) {
                    this.outPP = args[1];
                },
                onLeave: function (retval) {
                    if (retval.toInt32() !== 0) return;
                    if (!this.outPP || this.outPP.isNull()) return;
                    const iface = this.outPP.readPointer();
                    if (iface.isNull()) return;
                    patchDDrawVtable(iface, 'IDirectDraw7');
                },
            });
            log('force_windowed[L2]: ddraw!DirectDrawCreateEx armed');
        }
        return true;
    }

    if (!armDDrawCreate()) {
        // ddraw.dll not loaded yet — watch LoadLibrary and arm later.
        const k32 = Process.findModuleByName('kernel32.dll');
        if (!k32) {
            err('installForceWindowed[L2]', 'kernel32.dll not loaded');
            return;
        }
        const arm = function (name, isWide) {
            const exp = k32.findExportByName(name);
            if (!exp) return;
            Interceptor.attach(exp, {
                onEnter: function (args) {
                    this.lib = isWide ? args[0].readUtf16String() : args[0].readCString();
                },
                onLeave: function () {
                    if (!this.lib) return;
                    if (!/ddraw\.dll/i.test(this.lib)) return;
                    armDDrawCreate();
                },
            });
        };
        arm('LoadLibraryA', false);
        arm('LoadLibraryW', true);
        log('force_windowed[L2]: ddraw.dll not loaded; armed LoadLibrary* watch');
    }
}

// ─── silent audio ──────────────────────────────────────────────────────
// Two paths:
//   1. winmm!waveOutSetVolume — direct PCM volume (the Sierra startup
//      chime, and any WAV played via PlaySound-style APIs).  Clamp to 0.
//   2. dsound!DirectSoundCreate → wrap IDirectSound::SetCooperativeLevel
//      AND patch IDirectSoundBuffer::SetVolume on every secondary buffer.
//      SetVolume takes a LONG in 1/100th dB, range -10000 (silent) to 0.
//
// We always also mute the master via waveOutSetVolume which dominates on
// 1996-era engines anyway.

function installWaveOutMute() {
    const winmm = Process.findModuleByName('winmm.dll');
    if (!winmm) { err('silent_audio', 'winmm.dll not loaded'); return; }
    const wosv = winmm.findExportByName('waveOutSetVolume');
    if (!wosv) { err('silent_audio', 'winmm!waveOutSetVolume not found'); return; }
    Interceptor.attach(wosv, {
        onEnter: function (args) {
            // waveOutSetVolume(HWAVEOUT, DWORD dwVolume).  Low/high WORDs
            // are left/right channels, 0xFFFF = max.  Force to 0.
            const original = args[1].toUInt32();
            args[1] = ptr(0);
            send({kind: 'audio_mute', api: 'waveOutSetVolume', original: original});
        },
    });
    log('silent_audio: winmm!waveOutSetVolume → 0');
}

// IDirectSoundBuffer vtable layout (dsound.h, x86) — SetVolume at slot 15:
//   0..2  IUnknown
//   3     GetCaps
//   4     GetCurrentPosition
//   5     GetFormat
//   6     GetVolume
//   7     GetPan
//   8     GetFrequency
//   9     GetStatus
//   10    Initialize
//   11    Lock
//   12    Play
//   13    SetCurrentPosition
//   14    SetFormat
//   15    SetVolume             ← here, byte offset 0x3C
//   16    SetPan
//   17    SetFrequency
//   18    Stop
//   19    Unlock
//   20    Restore
//
// IDirectSound vtable layout (dsound.h, x86) — SetCooperativeLevel at slot 6:
//   0..2  IUnknown
//   3     CreateSoundBuffer
//   4     GetCaps
//   5     DuplicateSoundBuffer
//   6     SetCooperativeLevel    ← here, byte offset 0x18
//   7     Compact
//   8     GetSpeakerConfig
//   9     SetSpeakerConfig
//   10    Initialize

const g_patched_dsound_vtables = {};
const g_patched_dsbuf_vtables  = {};

function patchDSoundVtable(ifacePtr) {
    try {
        const vtable = ifacePtr.readPointer();
        const vtKey  = vtable.toString();
        if (vtKey in g_patched_dsound_vtables) return;
        g_patched_dsound_vtables[vtKey] = true;

        // SetCooperativeLevel @ slot 6.  Strip exclusive bits — DSound
        // exclusive mode steals audio focus from other processes.
        // DSSCL_NORMAL      = 1
        // DSSCL_PRIORITY    = 2
        // DSSCL_EXCLUSIVE   = 3
        // DSSCL_WRITEPRIMARY= 4
        const setCoopLevel = vtable.add(6 * Process.pointerSize).readPointer();
        Interceptor.attach(setCoopLevel, {
            onEnter: function (args) {
                const hwnd     = args[1];
                const original = args[2].toUInt32();
                // Force NORMAL (1) regardless of what the engine asked for.
                args[2] = ptr(1);
                send({kind: 'dsound_cooplevel_rewrite',
                      iface:    'IDirectSound',
                      hwnd:     hwnd.toString(),
                      original: original,
                      forced:   1});
            },
        });

        // CreateSoundBuffer @ slot 3 — patch every returned buffer's
        // SetVolume so engine volume changes can't unmute us.
        const createSB = vtable.add(3 * Process.pointerSize).readPointer();
        Interceptor.attach(createSB, {
            onLeave: function (retval) {
                if (retval.toInt32() !== 0) return;
                // args[2] = *LPDIRECTSOUNDBUFFER out param.  We need it
                // from the args saved in onEnter via this.context isn't
                // available here — re-derive via this.ctx (set in onEnter).
                // Wait — we didn't set onEnter. Need both.
            },
        });
        // Re-do CreateSoundBuffer with both onEnter + onLeave.
        // (Frida doesn't allow attaching twice to the same address from
        // the same invocation; instead we do it below in one block.)
        log('silent_audio: IDirectSound vtable patched (SetCooperativeLevel)');
    } catch (e) {
        err('patchDSoundVtable', e.message);
    }
}

// Helper: patch a freshly-returned IDirectSoundBuffer's SetVolume so the
// engine's volume calls clamp to -10000 (silent).
function patchDSoundBufferVtable(bufPtr) {
    try {
        const vtable = bufPtr.readPointer();
        const vtKey  = vtable.toString();
        if (vtKey in g_patched_dsbuf_vtables) return;
        g_patched_dsbuf_vtables[vtKey] = true;
        const setVolume = vtable.add(15 * Process.pointerSize).readPointer();
        Interceptor.attach(setVolume, {
            onEnter: function (args) {
                if (!g_silent_audio_enabled) return;
                const original = args[1].toInt32();
                if (original === -10000) return;  // already silent
                args[1] = ptr((-10000) >>> 0);  // sign-extended as DWORD
                send({kind: 'audio_mute',
                      api: 'IDirectSoundBuffer::SetVolume',
                      original: original});
            },
        });
        log('silent_audio: IDirectSoundBuffer::SetVolume clamp armed @ ' + setVolume);
    } catch (e) {
        err('patchDSoundBufferVtable', e.message);
    }
}

function installSilentAudioDSound() {
    function armDSoundCreate() {
        const dsound = Process.findModuleByName('dsound.dll');
        if (!dsound) return false;
        const dsc = dsound.findExportByName('DirectSoundCreate');
        if (dsc) {
            Interceptor.attach(dsc, {
                onEnter: function (args) { this.outPP = args[1]; },
                onLeave: function (retval) {
                    if (retval.toInt32() !== 0) return;
                    if (!this.outPP || this.outPP.isNull()) return;
                    const iface = this.outPP.readPointer();
                    if (iface.isNull()) return;
                    patchDSoundVtable(iface);

                    // Wrap CreateSoundBuffer (slot 3) to catch every buffer
                    // the engine subsequently creates.  This pass attaches
                    // properly with onEnter+onLeave.
                    try {
                        const vtable = iface.readPointer();
                        const createSB = vtable.add(3 * Process.pointerSize).readPointer();
                        Interceptor.attach(createSB, {
                            onEnter: function (args) { this.bufPP = args[3]; },
                            onLeave: function (rc) {
                                if (rc.toInt32() !== 0) return;
                                if (!this.bufPP || this.bufPP.isNull()) return;
                                const buf = this.bufPP.readPointer();
                                if (buf.isNull()) return;
                                patchDSoundBufferVtable(buf);
                            },
                        });
                        log('silent_audio: IDirectSound::CreateSoundBuffer hooked');
                    } catch (e) {
                        err('armDSoundCreate.CreateSoundBuffer', e.message);
                    }
                },
            });
            log('silent_audio: dsound!DirectSoundCreate armed');
        }
        return true;
    }

    if (!armDSoundCreate()) {
        const k32 = Process.findModuleByName('kernel32.dll');
        if (!k32) { err('silent_audio', 'kernel32.dll not loaded'); return; }
        const arm = function (name, isWide) {
            const exp = k32.findExportByName(name);
            if (!exp) return;
            Interceptor.attach(exp, {
                onEnter: function (args) {
                    this.lib = isWide ? args[0].readUtf16String() : args[0].readCString();
                },
                onLeave: function () {
                    if (!this.lib) return;
                    if (!/dsound\.dll/i.test(this.lib)) return;
                    armDSoundCreate();
                },
            });
        };
        arm('LoadLibraryA', false);
        arm('LoadLibraryW', true);
        log('silent_audio: dsound.dll not loaded; armed LoadLibrary* watch');
    }
}

// ─── turbo ──────────────────────────────────────────────────────────────
// Two cooperating pieces (mirrors OpenMare's pattern):
//
//   v1) kernel32!Sleep → no-op. Lords2 sleeps in its render-rate limiter
//       (see docs/findings/render-rate-limiter.md); zeroing Sleep lets the
//       main loop spin as fast as the host CPU allows.
//
//   v2) winmm!timeGetTime → virtual clock, gated to the engine main thread.
//       Seeded from a one-shot real timeGetTime read at install time; bumped
//       by g_turbo_step_ms on every entry to the per-tick driver
//       FUN_004b99c0 (the natural per-frame anchor). All retail timeGetTime
//       call sites that we care about — the FPS counter (FUN_004b27bf), the
//       frame-delta generator (FUN_004b20ed), the sim rate-gate
//       (FUN_004bb978) — read this virtualised clock; DPlay's GetTickCount
//       reads stay on real wall-clock (the DPlay pump uses GetTickCount, not
//       timeGetTime — verified via decompile grep, see main-loop-tick.md).
//
// Without v2, v1 alone makes the loop spin pointlessly: timeGetTime still
// returns wall-clock, so the rate-gate falls back to "no progress yet" and
// the FPS counter just counts more iterations per real second.  With v2 on,
// the engine's *perceived* time moves at (1000 / step_ms) Hz — at the
// default 17 ms/iter, ~58 ticks/s — which is what the FPS counter samples
// to compute current_fps.
//
// Side-thread safety: only main-thread callers get the virtual return;
// DSound/Smacker/audio threads see the real timeGetTime untouched so their
// internal scheduling isn't disrupted.

function installTurboSleepHook() {
    const k32 = Process.findModuleByName('kernel32.dll');
    if (!k32) { err('installTurbo', 'kernel32.dll not loaded'); return; }
    const sleepAddr = k32.findExportByName('Sleep');
    if (!sleepAddr) { err('installTurbo', 'kernel32!Sleep not found'); return; }
    Interceptor.replace(sleepAddr, new NativeCallback(
        function (ms) { /* swallow */ },
        'void',
        ['uint32'],
        'stdcall'));
    log('turbo v1: kernel32!Sleep no-op installed');
}

// Tracks whether installTimeGetTimeHook() has already attached, so callers
// (turbo + diff_test) can request it independently without double-attaching.
let g_timeGetTime_hooked = false;

function installTimeGetTimeHook() {
    if (g_timeGetTime_hooked) return;
    const winmm = Process.findModuleByName('winmm.dll');
    if (!winmm) { err('installTimeGetTime', 'winmm.dll not loaded'); return; }
    const tgt = winmm.findExportByName('timeGetTime');
    if (!tgt) { err('installTimeGetTime', 'winmm!timeGetTime not found'); return; }

    // Seed the virtual clock with the current real timeGetTime so the
    // engine's first read sees a sane baseline — the FPS counter +
    // frame-delta both subtract this from later samples, so a zero seed
    // would make the first window look like the engine has been running
    // for 49.7 days.  Use NativeFunction (not Interceptor.replace) so the
    // seed read doesn't re-enter our own onLeave handler.
    const realTGT = new NativeFunction(tgt, 'uint32', [], 'stdcall');
    g_virtual_now_ms = realTGT() >>> 0;

    Interceptor.attach(tgt, {
        onLeave: function (retval) {
            // Diff-test scripted clock takes priority over the turbo virtual
            // clock — but ONLY when the calling thread matches the diff RPC's
            // V8 thread.  The engine main thread keeps ticking during diff
            // tests; without the gate it would race on the queue and steal
            // scripted values meant for our retail-function call.
            // Queue is FIFO; an empty queue (or wrong thread) falls through
            // to the existing virtual / wall-clock path.
            if (g_diff_test_clock_queue.length > 0 &&
                Process.getCurrentThreadId() === g_diff_test_caller_tid) {
                const v = g_diff_test_clock_queue.shift();
                retval.replace(ptr(v >>> 0));
                return;
            }
            if (!g_clock_virtualised) return;
            if (g_main_thread_id === -1) return;
            if (Process.getCurrentThreadId() !== g_main_thread_id) return;
            retval.replace(ptr(g_virtual_now_ms >>> 0));
        },
    });

    g_timeGetTime_hooked = true;
    log('timeGetTime hook installed @ ' + tgt +
        ' (seed=' + g_virtual_now_ms + ', diff_test_queue_check=on)');
}

// Backwards-compat helper for the turbo install path — installs the hook
// (idempotent) and arms the virtualisation flag.  The log line wording is
// load-bearing: tools/smoke-test.py's --turbo assertion greps for the exact
// "turbo v2: winmm!timeGetTime virtualised" prefix.
function installVirtualClockHook() {
    installTimeGetTimeHook();
    g_clock_virtualised = true;
    log('turbo v2: winmm!timeGetTime virtualised ' +
        '(step=' + g_turbo_step_ms + ' ms/tick, main-thread-gated)');
}

// ─── GetCursorPos scripted-return hook (diff_test only) ─────────────────
//
// FUN_004b191e (engine_compute_input_state) calls user32!GetCursorPos
// exactly once at entry.  To drive the function deterministically from
// runRetailInputState we install an onLeave hook that — when called by
// the diff RPC's V8 thread AND the queue is non-empty — overwrites the
// LPPOINT struct with our scripted (x, y) and forces the return value
// to TRUE.
//
// Queue is FIFO, gated on caller_tid (same shape as g_diff_test_clock_queue
// for timeGetTime).  Engine-thread calls fall through unchanged — they
// see the real cursor pos.  The hook is idempotent.
let g_getcursorpos_hooked = false;
function installGetCursorPosHook() {
    if (g_getcursorpos_hooked) return;
    const user32 = Process.findModuleByName('user32.dll');
    if (!user32) {
        err('installGetCursorPosHook', 'user32.dll not loaded');
        return;
    }
    const fn = user32.findExportByName('GetCursorPos');
    if (!fn) {
        err('installGetCursorPosHook', 'user32!GetCursorPos not found');
        return;
    }
    Interceptor.attach(fn, {
        onEnter: function (args) {
            // Capture lpPoint so onLeave can overwrite the POINT struct.
            // POINT layout: { LONG x; LONG y; } — 8 bytes, x at +0.
            this.lpPoint = args[0];
        },
        onLeave: function (retval) {
            if (g_diff_test_cursor_queue.length > 0 &&
                Process.getCurrentThreadId() === g_diff_test_caller_tid) {
                const v = g_diff_test_cursor_queue.shift();
                if (!this.lpPoint.isNull()) {
                    this.lpPoint.writeS32(v.x | 0);
                    this.lpPoint.add(4).writeS32(v.y | 0);
                }
                retval.replace(ptr(1));  // BOOL TRUE — always succeed.
            }
        },
    });
    g_getcursorpos_hooked = true;
    log('GetCursorPos hook installed @ ' + fn + ' (diff_test cursor queue)');
}

// ─── frame capture ──────────────────────────────────────────────────────
// Lords II uses 8bpp paletted DDraw surfaces (640×480 in both windowed and
// fullscreen modes — see docs/findings/ddraw-dsound-bringup.md).  The back
// surface is DDSCAPS_OFFSCREENPLAIN | SYSTEMMEMORY → directly lockable; no
// sysmem-bounce dance needed (cf. openrecet's D3D8 capture path, which
// has to GetBackBuffer + CreateImageSurface + CopyRects to land somewhere
// lockable).
//
// IDirectDrawSurface vtable slots (see docs/findings/ddraw-dsound-bringup.md):
//   5    Blt          (byte offset 0x14)
//   25   Lock         (byte offset 0x64)
//   32   Unlock       (byte offset 0x80)
//
// IDirectDrawPalette vtable slots:
//   4    GetEntries   (byte offset 0x10)
//
// DDSURFACEDESC layout (108 bytes, dwSize=108 for DDraw1):
//   +0   dwSize
//   +4   dwFlags
//   +8   dwHeight
//   +12  dwWidth
//   +16  lPitch (union with dwLinearSize)
//   ...
//   +36  lpSurface
//
// Lock signature:
//   HRESULT Lock(this, LPRECT, LPDDSURFACEDESC, DWORD flags, HANDLE event)
// Unlock signature:
//   HRESULT Unlock(this, LPVOID lpSurfaceData)
// GetEntries signature:
//   HRESULT GetEntries(this, DWORD flags, DWORD start, DWORD count,
//                      LPPALETTEENTRY out)
//
// PALETTEENTRY is 4 bytes: peRed, peGreen, peBlue, peFlags.

const DDLOCK_WAIT      = 0x00000001;
const DDLOCK_READONLY  = 0x00000020;
const DDSURFACEDESC_SZ = 108;
const PALETTE_BYTES    = 256 * 4;     // 256 × PALETTEENTRY
const FRAME_HEADER_SZ  = 12;          // u32 width, u32 height, u32 frame_idx

function vfn(thisPtr, slotIdx, retType, argTypes) {
    const vtable = thisPtr.readPointer();
    const fn     = vtable.add(slotIdx * Process.pointerSize).readPointer();
    return new NativeFunction(fn, retType, argTypes, 'stdcall');
}

function captureFrameOnce() {
    let back = NULL;
    let locked = false;
    try {
        const primary = rva(ADDR.var_dd_primary).readPointer();
        back          = rva(ADDR.var_dd_back).readPointer();
        const palette = rva(ADDR.var_dd_palette).readPointer();
        // DDraw init may not have completed yet on very-early ticks; skip
        // silently and try again on the next cum-secs rollover.
        if (primary.isNull() || back.isNull() || palette.isNull()) return;

        // Lazy NativeFunction resolution. Vtables are stable once DDraw is
        // up, so cache after first success.
        if (!g_nf_surface_lock) {
            g_nf_surface_lock = vfn(back, 25, 'int32',
                ['pointer', 'pointer', 'pointer', 'uint32', 'pointer']);
            g_nf_surface_unlock = vfn(back, 32, 'int32',
                ['pointer', 'pointer']);
            g_nf_palette_get_entries = vfn(palette, 4, 'int32',
                ['pointer', 'uint32', 'uint32', 'uint32', 'pointer']);
        }

        // Only dwSize needs to be set before Lock (the API uses it to
        // detect DDSURFACEDESC vs DDSURFACEDESC2).  Other fields are
        // out-params filled in by the implementation.
        const desc = Memory.alloc(DDSURFACEDESC_SZ);
        desc.writeU32(DDSURFACEDESC_SZ);

        const hr = g_nf_surface_lock(back, NULL, desc,
                                     DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        if (hr !== 0) {
            err('captureFrame.Lock', '0x' + (hr >>> 0).toString(16));
            return;
        }
        locked = true;

        const height    = desc.add(8 ).readU32();
        const width     = desc.add(12).readU32();
        const pitch     = desc.add(16).readS32();
        const lpSurface = desc.add(36).readPointer();
        if (lpSurface.isNull() || width === 0 || height === 0) {
            err('captureFrame.descCheck',
                'w=' + width + ' h=' + height + ' surf=' + lpSurface);
            return;
        }
        g_last_capture_w = width; g_last_capture_h = height;

        // Allocate / grow scratch buffer for one frame's worth of payload:
        //   [header 12] [palette 1024] [tight pixels w*h]
        const payloadSz = FRAME_HEADER_SZ + PALETTE_BYTES + width * height;
        if (!g_capture_scratch || g_capture_scratch._sz < payloadSz) {
            g_capture_scratch = Memory.alloc(payloadSz);
            g_capture_scratch._sz = payloadSz;
        }
        const scratch  = g_capture_scratch;
        const palStart = scratch.add(FRAME_HEADER_SZ);
        const pxStart  = scratch.add(FRAME_HEADER_SZ + PALETTE_BYTES);

        // Header.
        scratch.writeU32(width);
        scratch.add(4).writeU32(height);
        scratch.add(8).writeU32(g_frame_capture_count);

        // Palette → palStart (256 PALETTEENTRY, 4 bytes each).
        const phr = g_nf_palette_get_entries(palette, 0, 0, 256, palStart);
        if (phr !== 0) {
            err('captureFrame.GetEntries',
                '0x' + (phr >>> 0).toString(16));
            // continue anyway — leaves palette as previous-frame's residue
            // (or zeroed on first call), which the Python side renders as
            // black/garbage but doesn't crash the capture.
        }

        // Tight-pack pixels row by row.  If pitch == width we could do a
        // single Memory.copy; the row-loop handles both cases.
        for (let y = 0; y < height; y++) {
            Memory.copy(pxStart.add(y * width),
                        lpSurface.add(y * pitch),
                        width);
        }

        const ab = scratch.readByteArray(payloadSz);
        g_frame_capture_count++;
        send({kind:    'frame',
              frame_idx: g_frame_capture_count - 1,
              width:    width,
              height:   height,
              pitch:    pitch,
              virtual_now_ms: g_virtual_now_ms,
              t_ms:     nowMs()}, ab);

        // One-shot globals dump — read the configured engine regions at
        // the first captured frame so a parity port can seed its state
        // from retail's exact values (name buffers, selection slots,
        // gating flags).  Memory reads are independent of the DDraw lock.
        if (!g_globals_dumped && g_dump_globals.length > 0) {
            g_globals_dumped = true;
            const regions = g_dump_globals.map(function (s) {
                let bytes = null;
                try {
                    bytes = Array.from(new Uint8Array(
                        rva(s.va).readByteArray(s.size)));
                } catch (e) { bytes = null; }
                return {va: s.va, size: s.size, label: s.label, bytes: bytes};
            });
            send({kind: 'globals_dump',
                  frame_idx: g_frame_capture_count - 1,
                  regions: regions});
        }

        // One-shot large-region dump — one message per region, raw bytes as a
        // binary side payload.  For `deref` regions the 32-bit pointer stored
        // at `va` is read first (the heap-allocated sprite-bank base), then
        // `size` bytes are read from that absolute target.
        if (!g_regions_dumped && g_dump_regions.length > 0) {
            g_regions_dumped = true;
            for (let i = 0; i < g_dump_regions.length; i++) {
                const s = g_dump_regions[i];
                let base = rva(s.va);
                let resolved = base;
                let ok = true;
                try {
                    if (s.deref) {
                        resolved = base.readPointer();
                    }
                } catch (e) {
                    ok = false;
                    err('dump_regions.deref', s.label + ': ' + e.message);
                }
                // Clamp the read to the enclosing mapped range so a generous
                // requested size on a heap-allocated bank (whose exact length
                // we don't know) never walks off the end into unmapped memory.
                let readSize = s.size;
                try {
                    const range = Process.findRangeByAddress(resolved);
                    if (range) {
                        const avail = range.base.add(range.size)
                                          .sub(resolved).toUInt32();
                        if (avail < readSize) readSize = avail;
                    }
                } catch (e) { /* fall back to requested size */ }
                let ab = null;
                if (ok) {
                    try {
                        ab = resolved.readByteArray(readSize);
                    } catch (e) {
                        ok = false;
                        err('dump_regions.read', s.label + ': ' + e.message);
                    }
                }
                send({kind: 'globals_blob',
                      frame_idx: g_frame_capture_count - 1,
                      label: s.label,
                      va: s.va,
                      size: readSize,
                      requested: s.size,
                      deref: !!s.deref,
                      resolved: ok ? resolved.toString() : null,
                      ok: ok}, ok ? ab : undefined);
            }
        }
    } catch (e) {
        err('captureFrameOnce', e.message);
    } finally {
        if (locked && !back.isNull() && g_nf_surface_unlock) {
            try { g_nf_surface_unlock(back, NULL); }
            catch (e) { err('captureFrame.Unlock', e.message); }
        }
    }
}

// ─── DDraw → GDI present seat ──────────────────────────────────────────
// Modern Windows DWM (Desktop Window Manager) intercepts windowed
// DDraw NORMAL-mode Blts to the primary surface: the Blt returns
// DD_OK and the back-buffer legitimately has the rendered pixels,
// but DWM never propagates the paint to the window's redirection-
// bitmap backing store — the visible window stays at whatever DWM
// last composited (typically WM_ERASEBKGND's background-erase).
//
// Retail Lords2.exe sidesteps this entirely because its FUN_0042e5c0
// auto-fullscreen-promotes on any non-8bpp desktop (always, on
// modern hardware) — exclusive coop + SetDisplayMode put DDraw
// outside DWM's intercept path.  But our harness strips those flags
// (force_windowed[L2]) so it can dev-loop without taking over the
// desktop, leaving retail rendering invisible.
//
// Fix: intercept IDirectDrawSurface::Blt on the primary surface
// (vtable slot 5, byte offset 0x14), and on back→primary Blts do a
// GDI StretchDIBits from the back buffer to the window's HDC.  GDI
// calls DO go through DWM's intercept and update the redirection
// bitmap correctly.  We let the original Blt run too — it's a DWM
// no-op now but its return code is what the engine observes.
//
// Mirror implementation in src/gfx.c's present_via_gdi (drop-in's
// windowed present path), same workaround rationale documented in
// engine-quirks.md / project_dwm_ddraw_normal memory.
//
// Lazy install: the primary surface pointer (var_dd_primary) is NULL
// until retail finishes FUN_0042ec91 (surface creation), so we poll
// in engine_tick.onLeave and patch the vtable the first time all
// three globals (primary, back, palette) are non-NULL.

const DDBLT_COLORFILL = 0x00000400;
const DIB_RGB_COLORS  = 0;
const SRCCOPY         = 0x00CC0020;
const COLORONCOLOR    = 3;
const BITMAPINFO_SZ   = 40 + 256 * 4;     // BITMAPINFOHEADER + 256 RGBQUAD

let g_primary_blt_patched   = false;
let g_gdi_present_count     = 0;
let g_gdi_blt_observed      = 0;   // Blt() invocations on the primary vtable
let g_gdi_blt_skipped_fill  = 0;   // DDBLT_COLORFILL (no source) — let through
let g_gdi_blt_skipped_other = 0;   // src != back or this != primary
let g_nf_gdi_get_dc         = null;
let g_nf_gdi_release_dc     = null;
let g_nf_gdi_get_client_rect= null;
let g_nf_gdi_stretch_dibs   = null;
let g_nf_gdi_set_stretch_mode = null;
let g_gdi_bitmapinfo_buf    = null;       // scratch: BITMAPINFOHEADER + 256 RGBQUAD
let g_gdi_palette_buf       = null;       // scratch: 256 PALETTEENTRY readback
let g_gdi_rect_buf          = null;       // scratch: RECT for GetClientRect
let g_gdi_row_scratch       = null;       // scratch: pitch-adjusted rows for StretchDIBits
let g_gdi_row_scratch_sz    = 0;

function resolveGdiSeatFunctions() {
    if (g_nf_gdi_stretch_dibs) return true;
    const u32 = Process.findModuleByName('user32.dll');
    const gdi = Process.findModuleByName('gdi32.dll');
    if (!u32 || !gdi) {
        err('gdi_seat', 'user32.dll or gdi32.dll not loaded');
        return false;
    }
    const u_getdc = u32.findExportByName('GetDC');
    const u_reldc = u32.findExportByName('ReleaseDC');
    const u_gcr   = u32.findExportByName('GetClientRect');
    const g_sdb   = gdi.findExportByName('StretchDIBits');
    const g_ssbm  = gdi.findExportByName('SetStretchBltMode');
    if (!u_getdc || !u_reldc || !u_gcr || !g_sdb || !g_ssbm) {
        err('gdi_seat', 'missing GDI/user32 export');
        return false;
    }
    g_nf_gdi_get_dc         = new NativeFunction(u_getdc, 'pointer',
                                ['pointer'], 'stdcall');
    g_nf_gdi_release_dc     = new NativeFunction(u_reldc, 'int32',
                                ['pointer', 'pointer'], 'stdcall');
    g_nf_gdi_get_client_rect= new NativeFunction(u_gcr,   'int32',
                                ['pointer', 'pointer'], 'stdcall');
    g_nf_gdi_stretch_dibs   = new NativeFunction(g_sdb, 'int32',
        ['pointer', 'int', 'int', 'int', 'int',
         'int', 'int', 'int', 'int',
         'pointer', 'pointer', 'uint32', 'uint32'], 'stdcall');
    g_nf_gdi_set_stretch_mode = new NativeFunction(g_ssbm, 'int32',
                                ['pointer', 'int'], 'stdcall');
    return true;
}

// Lock the back surface, walk its 8bpp bytes through the engine's
// DDraw palette, StretchDIBits into the window's client area, unlock.
// Returns true on success.  Caller has already verified that all
// three globals (back, palette, hwnd) are non-NULL.
function presentBackToWindowViaGdi(back, palette, hwnd) {
    if (!resolveGdiSeatFunctions()) return false;

    // Reuse the frame-capture path's lazy NativeFunction stubs
    // (already declared at module scope for the frame-capture path).
    if (!g_nf_surface_lock) {
        g_nf_surface_lock = vfn(back, 25, 'int32',
            ['pointer', 'pointer', 'pointer', 'uint32', 'pointer']);
        g_nf_surface_unlock = vfn(back, 32, 'int32',
            ['pointer', 'pointer']);
    }
    if (!g_nf_palette_get_entries) {
        g_nf_palette_get_entries = vfn(palette, 4, 'int32',
            ['pointer', 'uint32', 'uint32', 'uint32', 'pointer']);
    }

    const desc = Memory.alloc(DDSURFACEDESC_SZ);
    desc.writeU32(DDSURFACEDESC_SZ);
    const hr = g_nf_surface_lock(back, NULL, desc,
                                 DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (hr !== 0) {
        // Surface may legitimately be lost (alt-tab in fullscreen, ...).
        // Bail; the next present cycle will retry.
        return false;
    }

    try {
        const height    = desc.add(8 ).readU32();
        const width     = desc.add(12).readU32();
        const pitch     = desc.add(16).readS32();
        const lpSurface = desc.add(36).readPointer();
        if (lpSurface.isNull() || width === 0 || height === 0) {
            return false;
        }

        // BITMAPINFO buffer — header + 256 RGBQUAD palette.  Zero on
        // first build; palette refreshed per present (DDraw palette can
        // mutate when retail does palette-cycle effects or screen-state
        // transitions, so a stale cache would be wrong).
        if (!g_gdi_bitmapinfo_buf) {
            g_gdi_bitmapinfo_buf = Memory.alloc(BITMAPINFO_SZ);
            // BITMAPINFOHEADER (one-time): biSize, biPlanes, biBitCount,
            // biCompression=BI_RGB, biClrUsed.
            g_gdi_bitmapinfo_buf.writeU32(40);                    // biSize
            g_gdi_bitmapinfo_buf.add(12).writeU16(1);             // biPlanes
            g_gdi_bitmapinfo_buf.add(14).writeU16(8);             // biBitCount
            g_gdi_bitmapinfo_buf.add(16).writeU32(0);             // biCompression
            g_gdi_bitmapinfo_buf.add(32).writeU32(256);           // biClrUsed
        }
        const bi = g_gdi_bitmapinfo_buf;
        bi.add(4 ).writeS32(width);                               // biWidth
        bi.add(8 ).writeS32(-height);                             // biHeight (top-down)

        // Palette → bi+40 as RGBQUAD (B,G,R,reserved).  GetEntries
        // hands us PALETTEENTRY (R,G,B,peFlags) — swap R↔B on copy.
        if (!g_gdi_palette_buf) g_gdi_palette_buf = Memory.alloc(256 * 4);
        const palOut = g_gdi_palette_buf;
        const phr = g_nf_palette_get_entries(palette, 0, 0, 256, palOut);
        if (phr !== 0) {
            // Palette read failed — paint with whatever's currently in
            // bi+40 (may be all-zero on the first frame).  Keeps the
            // pipeline alive instead of erroring out mid-render.
            err('gdi_seat.GetEntries', '0x' + (phr >>> 0).toString(16));
        } else {
            const rgbQuads = bi.add(40);
            for (let i = 0; i < 256; i++) {
                const r = palOut.add(i * 4 + 0).readU8();
                const g = palOut.add(i * 4 + 1).readU8();
                const b = palOut.add(i * 4 + 2).readU8();
                rgbQuads.add(i * 4 + 0).writeU8(b);
                rgbQuads.add(i * 4 + 1).writeU8(g);
                rgbQuads.add(i * 4 + 2).writeU8(r);
                rgbQuads.add(i * 4 + 3).writeU8(0);
            }
        }

        // GetClientRect → destination size.  StretchDIBits scales from
        // src (back's natural 640×480 or whatever it is) to dst (the
        // window's current client size — accommodates user resize).
        if (!g_gdi_rect_buf) g_gdi_rect_buf = Memory.alloc(16);
        g_nf_gdi_get_client_rect(hwnd, g_gdi_rect_buf);
        const dst_w = g_gdi_rect_buf.add(8 ).readS32() -
                      g_gdi_rect_buf.add(0 ).readS32();
        const dst_h = g_gdi_rect_buf.add(12).readS32() -
                      g_gdi_rect_buf.add(4 ).readS32();
        if (dst_w <= 0 || dst_h <= 0) {
            // Minimised / hidden — nothing to draw.  Treat as a no-op
            // success so the caller doesn't think we failed and try
            // again next frame at the same rate.
            return true;
        }

        // StretchDIBits expects 8bpp source rows padded to a 4-byte
        // boundary.  DDraw sysmem back surfaces usually have
        // pitch == width (640) which is already 4-byte aligned, but
        // some drivers pad to 16 or 32 — copy into a tight-stride
        // scratch buffer when pitch differs.
        const row_stride = (width + 3) & ~3;
        let pixSrc = lpSurface;
        if (pitch !== row_stride) {
            const needed = row_stride * height;
            if (!g_gdi_row_scratch || g_gdi_row_scratch_sz < needed) {
                g_gdi_row_scratch = Memory.alloc(needed);
                g_gdi_row_scratch_sz = needed;
            }
            for (let y = 0; y < height; y++) {
                Memory.copy(g_gdi_row_scratch.add(y * row_stride),
                            lpSurface.add(y * pitch),
                            width);
            }
            pixSrc = g_gdi_row_scratch;
        }

        const hdc = g_nf_gdi_get_dc(hwnd);
        if (hdc.isNull()) return false;
        try {
            g_nf_gdi_set_stretch_mode(hdc, COLORONCOLOR);
            g_nf_gdi_stretch_dibs(hdc,
                0, 0, dst_w, dst_h,
                0, 0, width, height,
                pixSrc, bi, DIB_RGB_COLORS, SRCCOPY);
        } finally {
            g_nf_gdi_release_dc(hwnd, hdc);
        }
        g_gdi_present_count++;
        return true;
    } finally {
        try { g_nf_surface_unlock(back, NULL); }
        catch (e) { err('gdi_seat.Unlock', e.message); }
    }
}

// Agent-driven continuous GDI present.  When `g_agent_present_enabled`
// is on, engine_tick.onLeave drives a present every
// g_agent_present_every_ticks ticks regardless of whether retail's
// own present pipeline is firing.  Interactive verification only —
// retail's Blt-to-primary is the real signal for a "production"
// behavioural test.
//
// Skips silently when surfaces aren't ready yet (same NULL checks as
// the Blt-vtable seat).
function tryAgentDrivenPresent() {
    if (!g_agent_present_enabled) return;
    if ((g_tick_count % g_agent_present_every_ticks) !== 0) return;
    const back    = rva(ADDR.var_dd_back   ).readPointer();
    const palette = rva(ADDR.var_dd_palette).readPointer();
    if (back.isNull() || palette.isNull()) return;
    const hwnd    = rva(ADDR.var_main_hwnd ).readPointer();
    if (hwnd.isNull()) return;
    presentBackToWindowViaGdi(back, palette, hwnd);
}

// Recorder stream: send a frame every g_stream_every_ticks ticks from inside the
// engine tick (synchronized with the game's own render, no external lock race).
function maybeStreamFrame() {
    if (!g_stream_enabled) return;
    const t = nowMs();
    if ((t - g_last_stream_ms) < g_stream_interval_ms) return;
    g_last_stream_ms = t;
    try { captureFrameOnce(); } catch (e) { err('maybeStreamFrame', e.message); }
}

// Force the main-loop present gate open so the engine runs at full speed
// and services input every iteration (see var_app_active note in ADDR).
// A frida-spawned window rarely wins foreground, so WM_ACTIVATEAPP=TRUE
// never sets var_app_active; we just write it directly each tick.  Cheap
// (two s32 stores), idempotent, and harmless if retail also sets them.
function tryForceAppActive() {
    if (!g_force_active) return;
    rva(ADDR.var_app_active).writeS32(1);
    rva(ADDR.var_surface_lost).writeS32(0);
    // Windowed-input fix: with the cursor cache ENABLED, FUN_004b191e
    // overwrites the cached cursor with raw GetCursorPos *screen* coords and
    // maps them as if the client were at screen (0,0) — only true in
    // fullscreen.  Disabling it makes the engine use the WndProc's
    // WM_MOUSEMOVE-delivered *client* coords (lParam), which map correctly
    // for a windowed window at any position — so the real mouse lands where
    // the user points.
    rva(ADDR.var_input_cursor_cache_enabled).writeS32(0);

    // Scale fix: the engine maps cursor→game using DAT_004ea180..18c, which
    // the WndProc fills with the *fullscreen* GetSystemMetrics rect whenever
    // DAT_0053f228 != 0 (retail's default) — so input is scaled by
    // game/desktop even though the GDI seat stretches the 640×480 backbuffer
    // into the *actual* window client (GetClientRect).  Overwrite the four
    // globals with the real client size each tick so the input scale matches
    // the on-screen image; then a click lands on what the user sees.
    try {
        if (!g_nf_get_client_rect_fa) {
            const u32 = Process.findModuleByName('user32.dll');
            const fn = u32 && u32.findExportByName('GetClientRect');
            if (fn) {
                g_nf_get_client_rect_fa = new NativeFunction(fn, 'int32',
                    ['pointer', 'pointer'], 'stdcall');
            }
        }
        const hwnd = rva(ADDR.var_main_hwnd).readPointer();
        if (g_nf_get_client_rect_fa && !hwnd.isNull()) {
            if (!g_fa_rect_buf) g_fa_rect_buf = Memory.alloc(16);
            g_nf_get_client_rect_fa(hwnd, g_fa_rect_buf);
            const w = g_fa_rect_buf.add(8).readS32();
            const h = g_fa_rect_buf.add(12).readS32();
            if (w > 0 && h > 0) {
                rva(0x004ea180).writeS32(0);   // client rect, screen-relative;
                rva(0x004ea184).writeS32(0);   // only the w/h deltas are read
                rva(0x004ea188).writeS32(w);
                rva(0x004ea18c).writeS32(h);
            }
        }
    } catch (e) { /* best-effort; leave the rect alone on error */ }
}

// Programmatically show the engine's main window once enough time
// has elapsed since spawn.  Latch-protected so our hide hook lets the
// SW_SHOW through.  One-shot: clears g_show_window_done.
//
// SW_SHOWNORMAL (not SW_SHOW): SW_SHOW honours the window's "last
// show state" (which is the never-shown default after our hide hooks
// stripped WS_VISIBLE during init — leaves the window minimised on
// many DWM configurations).  SW_SHOWNORMAL forces the normal
// restored state.
//
// After the show, resize the client area to 800×600 (user
// preference) — the engine's windowed branch sized it to 640×480
// natively, but our GDI seat's StretchDIBits scales transparently so
// any client size works.  Add the non-client frame insets via
// AdjustWindowRect.
function tryShowEngineWindow() {
    if (g_show_window_done) return;
    if (g_show_window_after_ms <= 0) return;
    if (nowMs() < g_show_window_after_ms) return;
    const hwnd = rva(ADDR.var_main_hwnd).readPointer();
    if (hwnd.isNull()) return;
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) return;
    if (!g_nf_show_window) {
        const fn = u32.findExportByName('ShowWindow');
        if (!fn) return;
        g_nf_show_window = new NativeFunction(fn, 'int32',
            ['pointer', 'int32'], 'stdcall');
    }
    g_show_window_latch = true;
    g_nf_show_window(hwnd, SW_SHOWNORMAL);
    g_show_window_done = true;
    log('agent: ShowWindow(SW_SHOWNORMAL) called after ' +
        g_show_window_after_ms + ' ms warmup');

    // Interactive: try to foreground/activate so real mouse + keyboard land
    // naturally and the window is on top.  Best-effort — Windows often
    // refuses focus-steal to a non-foreground process; tryForceAppActive
    // keeps the engine ticking regardless, so this is just a nicety.
    if (g_force_active) {
        try {
            if (!g_nf_set_foreground) {
                const sfw = u32.findExportByName('SetForegroundWindow');
                if (sfw) {
                    g_nf_set_foreground = new NativeFunction(sfw, 'int32',
                        ['pointer'], 'stdcall');
                }
            }
            if (g_nf_set_foreground) g_nf_set_foreground(hwnd);
            const btt = u32.findExportByName('BringWindowToTop');
            if (btt) {
                new NativeFunction(btt, 'int32', ['pointer'], 'stdcall')(hwnd);
            }
        } catch (e) {
            err('tryShowEngineWindow.foreground', e.message);
        }
    }

    // Resize client area to 800×600.  AdjustWindowRect computes the
    // outer size needed to wrap that client size under the current
    // window style.
    try {
        const getStyle = u32.findExportByName('GetWindowLongA');
        const adjust   = u32.findExportByName('AdjustWindowRect');
        const setPos   = u32.findExportByName('SetWindowPos');
        if (getStyle && adjust && setPos) {
            const nfStyle = new NativeFunction(getStyle, 'int32',
                ['pointer', 'int32'], 'stdcall');
            const nfAdjust = new NativeFunction(adjust, 'int32',
                ['pointer', 'uint32', 'int32'], 'stdcall');
            const nfSetPos = new NativeFunction(setPos, 'int32',
                ['pointer', 'pointer', 'int32', 'int32', 'int32',
                 'int32', 'uint32'], 'stdcall');
            const GWL_STYLE_ = -16;
            const style = nfStyle(hwnd, GWL_STYLE_) >>> 0;
            const rect = Memory.alloc(16);
            rect.writeS32(0);
            rect.add(4 ).writeS32(0);
            rect.add(8 ).writeS32(g_show_window_client_w);
            rect.add(12).writeS32(g_show_window_client_h);
            nfAdjust(rect, style, 0);
            const w = rect.add(8 ).readS32() - rect.add(0).readS32();
            const h = rect.add(12).readS32() - rect.add(4).readS32();
            // SWP_NOMOVE (2) | SWP_NOZORDER (4) | SWP_NOACTIVATE (0x10)
            // = 0x16 — keep current position + z-order + don't steal
            // focus.
            nfSetPos(hwnd, NULL, 0, 0, w, h, 0x16);
            log('agent: window resized to ' + g_show_window_client_w +
                'x' + g_show_window_client_h + ' client ' +
                '(outer ' + w + 'x' + h + ')');
        }
    } catch (e) {
        err('tryShowEngineWindow.resize', e.message);
    }

    send({kind: 'agent_show_window',
          hwnd: hwnd.toString(),
          after_ms: g_show_window_after_ms,
          t_ms: nowMs()});
}

// Patch the primary surface's IDirectDrawSurface::Blt slot (slot 5,
// byte offset 0x14).  Idempotent — once patched we never touch it
// again.  Called lazily from engine_tick.onLeave when both surfaces
// (var_dd_primary and var_dd_back) and the palette pointer are
// non-NULL.
function installPrimaryBltGdiSeat() {
    if (g_primary_blt_patched) return;
    const primary = rva(ADDR.var_dd_primary).readPointer();
    const back    = rva(ADDR.var_dd_back   ).readPointer();
    const palette = rva(ADDR.var_dd_palette).readPointer();
    if (primary.isNull() || back.isNull() || palette.isNull()) return;
    if (!resolveGdiSeatFunctions()) return;

    try {
        const vtable = primary.readPointer();
        const bltFn  = vtable.add(5 * Process.pointerSize).readPointer();
        Interceptor.attach(bltFn, {
            onEnter: function (args) {
                this.handled = false;
                if (!g_gdi_seat_enabled) return;
                // args: this, lpDestRect, lpDDSrcSurface, lpSrcRect, flags, lpDDBltFx
                const self  = args[0];
                const src   = args[2];
                const flags = args[4].toUInt32();
                g_gdi_blt_observed++;
                // COLORFILL Blts have no source surface — let through.
                // (Retail uses these for the initial black-fill in
                // FUN_0042eb7c.  Visible result is just a black flash,
                // not worth GDI presenting.)
                if (flags & DDBLT_COLORFILL) { g_gdi_blt_skipped_fill++; return; }
                if (src.isNull()) { g_gdi_blt_skipped_other++; return; }
                // Only intercept back→primary Blts.  Compare against
                // the live globals so a re-init (with a different
                // primary pointer) doesn't intercept the wrong surface.
                const cachedPrimary = rva(ADDR.var_dd_primary).readPointer();
                const cachedBack    = rva(ADDR.var_dd_back   ).readPointer();
                if (cachedPrimary.isNull() || cachedBack.isNull()) {
                    g_gdi_blt_skipped_other++; return;
                }
                if (!self.equals(cachedPrimary)) {
                    g_gdi_blt_skipped_other++; return;
                }
                if (!src.equals(cachedBack)) {
                    g_gdi_blt_skipped_other++; return;
                }
                const cachedPalette = rva(ADDR.var_dd_palette).readPointer();
                if (cachedPalette.isNull()) return;
                const hwnd = rva(ADDR.var_main_hwnd).readPointer();
                if (hwnd.isNull()) return;

                this.handled = presentBackToWindowViaGdi(
                    cachedBack, cachedPalette, hwnd);
                // First-few-presents diagnostic — emit a heartbeat
                // event so the Python driver can confirm the seat is
                // actually firing without crawling the agent log.
                // Throttle: first 3 events, then one per 100.
                if (this.handled &&
                    (g_gdi_present_count <= 3 ||
                     (g_gdi_present_count % 100) === 0)) {
                    send({kind: 'gdi_seat_present',
                          count: g_gdi_present_count,
                          t_ms:  nowMs()});
                }
            },
            onLeave: function (retval) {
                // Override DDraw's HRESULT to DD_OK after a successful
                // GDI present — guarantees the engine's per-call retval
                // check doesn't disagree with what we drew, even on
                // hardware/driver combos where DDraw's invisible Blt
                // would normally return a benign warning hresult.
                if (this.handled) retval.replace(ptr(0));
            },
        });
        g_primary_blt_patched = true;
        log('gdi_seat: IDirectDrawSurface::Blt patched ' +
            '(primary=' + primary + ', vtable=' + vtable +
            ', Blt @ ' + bltFn + ')');
    } catch (e) {
        err('installPrimaryBltGdiSeat', e.message);
    }
}

// ─── input record / replay ─────────────────────────────────────────────
// WM_* values we care about.  The WndProc-side recorder filters the full
// message stream down to this set so the trace stays sparse — every other
// message (WM_PAINT, WM_TIMER, WM_ACTIVATEAPP, …) is irrelevant for
// scenario reproduction.  Retail's WndProc handles 0x100..0x102 and
// 0x200..0x206 directly; 0x104..0x106 (system key / sys-char) go to
// DefWindowProcA but are still part of the user-input stream the engine
// can react to via PostMessage redirection (see WM_SYSCHAR Alt-X).
const WM_KEYDOWN       = 0x100;
const WM_KEYUP         = 0x101;
const WM_CHAR          = 0x102;
const WM_SYSKEYDOWN    = 0x104;
const WM_SYSKEYUP      = 0x105;
const WM_SYSCHAR       = 0x106;
const WM_MOUSEMOVE     = 0x200;
const WM_LBUTTONDOWN   = 0x201;
const WM_LBUTTONUP     = 0x202;
const WM_LBUTTONDBLCLK = 0x203;
const WM_RBUTTONDOWN   = 0x204;
const WM_RBUTTONUP     = 0x205;
const WM_RBUTTONDBLCLK = 0x206;

function isRecordedInputMsg(m) {
    return (m >= WM_KEYDOWN && m <= WM_SYSCHAR && m !== 0x103 && m !== 0x107)
        || (m >= WM_MOUSEMOVE && m <= WM_RBUTTONDBLCLK);
}

function installInputRecordHook() {
    Interceptor.attach(rva(ADDR.fn_wndproc), {
        onEnter: function (args) {
            const msg = args[1].toUInt32();
            if (!isRecordedInputMsg(msg)) return;
            // wparam is a UINT_PTR (DWORD on x86), lparam is LPARAM (signed
            // DWORD). Emit both as raw 32-bit values; the consumer is
            // responsible for sign-interpreting lparam fields if needed
            // (e.g. WM_MOUSEMOVE packs signed shorts into the low/high).
            send({kind: 'input_event',
                  t: g_tick_count,
                  m: msg,
                  w: args[2].toUInt32(),
                  l: args[3].toUInt32(),
                  hwnd: args[0].toString()});
        },
    });
    log('input_record: WndProc hook armed @ ' + rva(ADDR.fn_wndproc) +
        ' (FUN_004b29be)');
}

// Resolve PostMessageA lazily. user32.dll is mapped before our agent loads
// (Lords2.exe imports it directly), so this should never fail in practice.
function resolvePostMessageA() {
    if (g_nf_post_message_a) return g_nf_post_message_a;
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) { err('input_replay', 'user32.dll not loaded'); return null; }
    const exp = u32.findExportByName('PostMessageA');
    if (!exp) { err('input_replay', 'user32!PostMessageA not found'); return null; }
    g_nf_post_message_a = new NativeFunction(
        exp, 'int32', ['pointer', 'uint32', 'uint32', 'uint32'], 'stdcall');
    return g_nf_post_message_a;
}

// Rescale a mouse-message lParam from game space (640x480, the authored
// trace space) to the current window-client space, so the engine's
// cached→game mapping (FUN_004b191e) reproduces the authored game coord
// regardless of the capture display resolution.  lParam packs signed 16-bit
// client coords: lo=x, hi=y.
//
// The engine does NOT compute the ideal ratio — it composes two truncating
// integer helpers (FUN_00404dc1 + FUN_00404d6b):
//     scale = (game_extent * 100) / viewport_extent      ← truncates
//     game  = (cached_client * scale) / 100              ← truncates
// e.g. a 646×486 client gives scale_x = 64000/646 = 99 (not 99.07) and
// scale_y = 48000/486 = 98 (not 98.76), so inverting the *ideal* ratio
// leaves every click a few px short of the authored coord, growing with the
// coordinate (the s22 tooltip x-offset).  Invert the exact quantized form
// instead: cx = ceil(gx*100/scale) is the smallest client coord the engine
// maps back to gx — exact whenever scale <= 100 (client >= game extent),
// nearest-above otherwise.  PostMessage happily carries client coords past
// the real window edge, so game coords the truncated scale makes unreachable
// by a physical cursor (e.g. x >= 634 at scale 99) still reproduce.
//
// Returns the input unchanged when any extent is unreadable / non-positive
// or a coord is negative (degrades to the historical 1:1 assumption).
function rescaleMouseLParamToClient(l) {
    let vpw, vph, gw, gh;
    try {
        vpw = rva(0x4ea188).readS32() - rva(0x4ea180).readS32();  // x1 - x0
        vph = rva(0x4ea18c).readS32() - rva(0x4ea184).readS32();  // y1 - y0
        gw  = rva(0x4eb274).readS32();                            // game extent w
        gh  = rva(0x4eac4c).readS32();                            // game extent h
    } catch (e) { return l; }
    if (vpw <= 0 || vph <= 0 || gw <= 0 || gh <= 0) return l;
    const scaleX = Math.floor(gw * 100 / vpw);   // FUN_00404dc1, truncating
    const scaleY = Math.floor(gh * 100 / vph);
    if (scaleX <= 0 || scaleY <= 0) return l;
    if (scaleX === 100 && scaleY === 100) return l;               // already 1:1
    let gx = l & 0xffff;          if (gx & 0x8000) gx -= 0x10000;
    let gy = (l >>> 16) & 0xffff; if (gy & 0x8000) gy -= 0x10000;
    if (gx < 0 || gy < 0) return l;   // off-client coords: leave untouched
    const cx = Math.ceil(gx * 100 / scaleX) & 0xffff;
    const cy = Math.ceil(gy * 100 / scaleY) & 0xffff;
    return ((cy << 16) | cx) >>> 0;
}

// Detect observable engine-state transitions and fire the corresponding
// anchors (TAS framework).  Called from engine_tick.onEnter every tick right
// after g_tick_count is bumped, so `frame` is this tick's id.  Anchors are
// edge-triggered and may fire more than once over a run (e.g. MAP_FREEROAM
// after each load).  Two consumers:
//   - --anchor-trace: fireAnchor send()s the {anchor,frame} JSONL event.
//   - --input-segtrace: g_anchor_fired (name->frame, reset each tick) lets the
//     segtrace `wait ANCHOR` op consume this tick's firing.
// So anchor *detection* runs whenever EITHER is enabled; emission is gated on
// g_anchor_enabled alone.
// g_anchor_last persists the most-recent fire frame of each anchor across
// ticks (segtrace `wait` resolves against it — an anchor can fire before the
// trace's processing cursor reaches the matching `wait`, e.g. while still
// dispatching the prior segment's inputs).
let g_anchor_last = {};         // name -> last fire frame (persisted)

// ─── RNG seed pin (--rng-seed, TAS framework determinism) ────────────────
// Retail/port don't seed their RNG from the same wall-clock instant, so a
// port↔retail comparison must FORCE the same seed for the new-game map gen to
// match.  Pin the three authoritative LFSR states each tick UNTIL the game
// goes ingame (engine_state==3) — holds the seed through the menu so the
// synchronous map generator (FUN_00497ced, runs in the title→ingame transition
// tick) consumes exactly the pinned value; then stop so the live sim RNG runs.
// Defaults are retail's own InitInstance seeds (so pinning a normal retail run
// reproduces its own map; the point is to force the PORT to match).
let g_rng_seed_enabled = false;
let g_rng_seed_a       = 0x54657687;   // DAT_0058fe28 (engine_rng.c)
let g_rng_seed_b       = 0x72641663;   // DAT_0058fe08
let g_rng_seed_single  = 0x34518632;   // DAT_00591564

function pinRngSeed() {
    if (!g_rng_seed_enabled) return;
    let state;
    try { state = rva(ADDR.var_sim_game_state_phase).readS32(); }
    catch (e) { return; }
    if (state === 3) return;            // ingame — let the live RNG run
    try {
        rva(ADDR.var_rng_state_a).writeU32(g_rng_seed_a >>> 0);
        rva(ADDR.var_rng_state_b).writeU32(g_rng_seed_b >>> 0);
        rva(ADDR.var_rng_state_single).writeU32(g_rng_seed_single >>> 0);
    } catch (e) { /* not mapped yet — retry next tick */ }
}

// Anim-phase pin (--phase-pin) — the anim-phase sibling of the RNG pin.
// The shared phase counters (torch flames DAT_005aea54, demo figures
// DAT_005aea48, flag waves, march cadence) are pure functions of the tick
// count since boot, and port/retail spend a DIFFERENT number of ticks in the
// boot/menu/load stretch — so at anchor+offset the two engines sample
// different anim frames even when every pixel-producing byte matches.  Hold
// all cadence counters at 0 AND re-base the 20 ms base-tick timestamp to the
// current (virtual) clock every tick until the game goes ingame
// (engine_state == 3); from the flip both engines advance the counters in
// lockstep relative to the anchor.  Pre-ingame phase-gated cosmetics (caret
// blink) freeze while pinned — symmetric on both engines, not captured.
let g_phase_pin_enabled = false;

function pinPhaseCounters() {
    if (!g_phase_pin_enabled) return;
    let state;
    try { state = rva(ADDR.var_sim_game_state_phase).readS32(); }
    catch (e) { return; }
    if (state === 3) return;            // ingame — let the cadence run
    try {
        // Re-base the timestamp only under the virtual clock (--turbo, the
        // only seat parity captures run on); without it the counters alone
        // still pin, the cadence phase just isn't re-based.
        if (g_clock_virtualised) {
            rva(ADDR.var_phase_last_base_ms).writeU32(g_virtual_now_ms >>> 0);
        }
        rva(ADDR.var_phase_base_counter).writeU32(0);
        rva(ADDR.var_phase_counter_2).writeU32(0);
        rva(ADDR.var_phase_counter_4).writeU32(0);
        rva(ADDR.var_phase_counter_8).writeU32(0);
        rva(ADDR.var_phase_counter_13).writeU32(0);
        rva(ADDR.var_phase_counter_16).writeU32(0);
        rva(ADDR.var_phase_counter_24).writeU32(0);
        rva(ADDR.var_phase_counter_32).writeU32(0);
    } catch (e) { /* not mapped yet — retry next tick */ }
}

function fireAnchor(name, frame) {
    g_anchor_last[name] = frame >>> 0;
    if (g_anchor_enabled) {
        send({kind: 'anchor', anchor: name, frame: frame >>> 0});
    }
}

function anchorTick(frame) {
    if (!g_anchor_enabled && !g_seg_enabled) return;
    let state, screen;
    try {
        state  = rva(ADDR.var_sim_game_state_phase).readS32();
        screen = rva(ADDR.var_sim_current_screen).readU8();
    } catch (e) { return; }     // globals not mapped yet — try next tick

    if (!g_anchor_booted) { g_anchor_booted = true; fireAnchor('BOOT', frame); }

    // Screen-id edges.
    if (screen !== g_anchor_prev_screen && screen === 0x1f) {
        fireAnchor('TITLE', frame);
    }
    // Engine-state edges.
    if (state !== g_anchor_prev_state) {
        if (state === 0xb) fireAnchor('LOADING_START', frame);
        if (g_anchor_prev_state === 0xb) fireAnchor('LOADING_END', frame);
        if (state === 3 && g_anchor_prev_state !== 3) fireAnchor('NEW_GAME', frame);
    }
    // Compound: strategic map live (ingame, map screen).  Fire on entry to the
    // (state==3 && screen==0) condition.
    const map_now  = (state === 3 && screen === 0);
    const map_prev = (g_anchor_prev_state === 3 && g_anchor_prev_screen === 0);
    if (map_now && !map_prev) fireAnchor('MAP_FREEROAM', frame);

    g_anchor_prev_state  = state;
    g_anchor_prev_screen = screen;
}

// ─── anchor-segmented input trace (--input-segtrace, TAS framework P2) ────
// A superset of the sparse {t,m,w,l} replay.  Ops (normalised by the driver):
//   {kind:'input', t,m,w,l}  — within-segment WM_* event; t is RELATIVE to the
//                              current segment's opening anchor frame.
//   {kind:'wait', anchor}    — segment break: hold until ANCHOR fires this run,
//                              then rebase segment-0 onto that anchor's frame.
//   {kind:'wait_until', va,type,op,val} — hold until a live-global predicate
//                              holds, then rebase segment-0 onto the current frame.
//   {kind:'capture', n,label} — schedule an anchor-relative capture at
//                              (segment_base + n); immune to load jitter.
// Processed greedily each tick; a blocking op (`wait`/`wait_until`/a future
// `input`) stops the loop until satisfied.  See docs/plans/tas-framework.md.
let g_seg_enabled       = false;
let g_seg_ops           = [];
let g_seg_idx           = 0;
let g_seg_base          = 0;        // absolute frame mapped to current segment t=0
let g_seg_wait_floor    = -1;       // frame of the last resolved wait/wait_until
let g_seg_done_sent     = false;
let g_capture_at_frames = null;     // Set<int> | null — absolute frames to capture

// Reactive popup auto-dismiss (segtrace {"auto_dismiss":N}) — mirror of the
// port's engine_set_popup_auto_dismiss.  While on, the per-tick hook closes any
// open modal scroll (DAT_0053e910 != 0) the same tick, so a turn trace runs
// through the 256c warning + brain-posted event scrolls with no hand-timed
// clicks (and no freeze desync vs the port).
let g_auto_dismiss      = 0;
let g_popup_dismiss_fn  = null;     // lazy NativeFunction(FUN_00476768)
let g_auto_dismiss_logged = false;  // one-shot log on first dismiss

// ─── save-schema state dump (trace_studio state parity) ──────────────────
// At each segtrace {capture} instant, read the 225 save-schema slots and
// send {kind:'state_dump', frame, slots:[…]} — scalars (size ≤ 4) as
// unsigned ints, big blocks as 32-bit FNV-1a hashes ("h:xxxxxxxx").  The
// port writes the IDENTICAL format from its g_save_schema walk
// (src/state_dump.c) — the hash must match bit-for-bit.
let g_state_dump_slots = null;      // [{i, va, size}] | null

function stateFnv1a(u8) {
    let h = 0x811c9dc5;
    for (let i = 0; i < u8.length; i++) {
        h ^= u8[i];
        h = Math.imul(h, 16777619);
    }
    return h >>> 0;
}

function stateFnv1aLane(u8, lane) {
    // FNV-1a over bytes lane, lane+8, lane+16, … (mirrors state_dump.c lane8).
    let h = 0x811c9dc5;
    for (let o = lane; o < u8.length; o += 8) {
        h ^= u8[o];
        h = Math.imul(h, 16777619);
    }
    return h >>> 0;
}

function stateDumpOnce(frame) {
    if (!g_state_dump_slots) return;
    const out = new Array(g_state_dump_slots.length);
    const sub = {};   // per-block 16-range sub-hashes (mirrors state_dump.c)
    const lane8 = {};  // per-8-byte-lane hashes for 8 B/cell blocks (map field diff)
    const f2bits = {}; // per-bit hashes of cell field +2 (map render/autotile flags)
    for (let i = 0; i < g_state_dump_slots.length; i++) {
        const s = g_state_dump_slots[i];
        try {
            const u8 = new Uint8Array(rva(s.va).readByteArray(s.size));
            if (s.size <= 4) {
                let v = 0;
                for (let k = s.size - 1; k >= 0; k--) v = v * 256 + u8[k];
                out[i] = v >>> 0;
            } else {
                out[i] = 'h:' + ('0000000' + stateFnv1a(u8).toString(16)).slice(-8);
                const step = Math.floor(s.size / 16);
                const ranges = new Array(16);
                for (let r = 0; r < 16; r++) {
                    const off = r * step;
                    const len = (r === 15) ? s.size - off : step;
                    ranges[r] = 'h:' + ('0000000' +
                        stateFnv1a(u8.subarray(off, off + len)).toString(16)).slice(-8);
                }
                const key = String(s.i !== undefined ? s.i : i);
                sub[key] = ranges;
                if ((s.size % 8) === 0) {
                    const lanes = new Array(8);
                    for (let lane = 0; lane < 8; lane++)
                        lanes[lane] = 'h:' + ('0000000' +
                            stateFnv1aLane(u8, lane).toString(16)).slice(-8);
                    lane8[key] = lanes;
                    // per-bit hashes of cell field +2 (map render/autotile
                    // flags) — mirrors state_dump.c f2bits.
                    const bits = new Array(8);
                    for (let bit = 0; bit < 8; bit++) {
                        let h = 0x811c9dc5;
                        for (let o = 2; o < u8.length; o += 8) {
                            h ^= (u8[o] >> bit) & 1;
                            h = Math.imul(h, 16777619);
                        }
                        bits[bit] = 'h:' + ('0000000' + (h >>> 0).toString(16)).slice(-8);
                    }
                    f2bits[key] = bits;
                }
            }
        } catch (e) {
            out[i] = null;              // region not mapped — surfaced as null
        }
    }
    send({kind: 'state_dump', frame: frame, slots: out, sub: sub, lane8: lane8, f2bits: f2bits});
}

// ─── per-frame state watch (--watch NAME=0xVA:type, TAS framework) ───────
// Emit {kind:"watch",name,frame,val} whenever a watched global CHANGES value
// (edge-emitted to stay lean) — the per-frame state oracle for authoring
// wait_until predicates + diagnosing divergence.  Reuses segReadTyped.
let g_watch_specs = [];         // [{name, va, type}]
let g_watch_last  = {};         // name -> last emitted value

function watchTick(frame) {
    if (g_watch_specs.length === 0) return;
    for (let i = 0; i < g_watch_specs.length; i++) {
        const s = g_watch_specs[i];
        let v;
        try { v = segReadTyped(s.va, s.type); } catch (e) { continue; }
        if (g_watch_last[s.name] !== v) {
            g_watch_last[s.name] = v;
            send({kind: 'watch', name: s.name, frame: frame >>> 0, val: v});
        }
    }
}

function segReadTyped(va, type) {
    const p = rva(va >>> 0);
    switch (type) {
        case 'u8':  return p.readU8();
        case 'i8':  return p.readS8();
        case 'u16': return p.readU16();
        case 'i16': return p.readS16();
        case 'u32': return p.readU32();
        case 'i32': return p.readS32();
        case 'f32': return p.readFloat();
        default:    return p.readS32();
    }
}

function segCmp(a, op, b) {
    switch (op) {
        case '<=': return a <= b;
        case '>=': return a >= b;
        case '<':  return a <  b;
        case '>':  return a >  b;
        case '==': return a === b;
        case '!=': return a !== b;
        default:   return false;
    }
}

function segtraceTick(frame) {
    if (!g_seg_enabled) return;
    while (g_seg_idx < g_seg_ops.length) {
        const op = g_seg_ops[g_seg_idx];

        if (op.kind === 'input') {
            if ((g_seg_base + op.t) > frame) break;   // future event — hold
            const post = resolvePostMessageA();
            if (!post) break;                          // user32 not resolved yet
            const hwnd = rva(ADDR.var_main_hwnd).readPointer();
            if (hwnd.isNull()) break;                  // window not up — retry
            let lp = op.l >>> 0;
            if (op.m >= WM_MOUSEMOVE && op.m <= WM_RBUTTONDBLCLK) {
                lp = rescaleMouseLParamToClient(lp);
            }
            try {
                post(hwnd, op.m >>> 0, op.w >>> 0, lp >>> 0);
                g_replay_post_count++;
                send({kind: 'input_replay_post', idx: g_seg_idx, t: op.t,
                      m: op.m, w: op.w, l: op.l, tick_now: frame});
            } catch (e) { err('segtrace.post', e.message); }
            g_seg_idx++;
            continue;
        }

        if (op.kind === 'wait') {
            // Resolve on the anchor's first firing STRICTLY AFTER the previous
            // wait's frame (g_seg_wait_floor).  Keyed on the floor — not on when
            // the cursor reaches this op — so an anchor that fired while we were
            // still dispatching this segment's inputs (game-start can flip to
            // the map within ~2 frames of the triggering click, before the
            // click's mouseup op is even posted) still satisfies the wait.  The
            // strict `>` keeps a repeated anchor (e.g. a double MAP_FREEROAM)
            // from re-satisfying a later wait on the same firing.
            const lf = g_anchor_last[op.anchor];
            if (lf !== undefined && lf > g_seg_wait_floor) {
                g_seg_base       = lf;
                g_seg_wait_floor = lf;
                send({kind: 'segtrace_wait', anchor: op.anchor, frame: g_seg_base});
                g_seg_idx++;
                continue;
            }
            break;                                     // still waiting
        }

        if (op.kind === 'wait_until') {
            let v;
            try { v = segReadTyped(op.va, op.type); } catch (e) { break; }
            if (segCmp(v, op.op, op.val)) {
                g_seg_base       = frame;
                g_seg_wait_floor = frame;
                send({kind: 'segtrace_wait_until', va: op.va >>> 0,
                      val: v, frame: frame});
                g_seg_idx++;
                continue;
            }
            break;
        }

        if (op.kind === 'capture') {
            const at = (g_seg_base + (op.n | 0)) >>> 0;
            if (!g_capture_at_frames) g_capture_at_frames = new Set();
            g_capture_at_frames.add(at);
            send({kind: 'segtrace_capture_sched', at: at, n: (op.n | 0),
                  label: String(op.label || '')});
            g_seg_idx++;
            continue;
        }

        if (op.kind === 'auto_dismiss') {
            // State toggle.  Timed form ({"t":N}) gates at seg_base+N like an
            // input op — REQUIRED whenever the toggle must land after other
            // t-stamped activity, because captures are pre-armed out-of-band
            // here (g_capture_at_frames) while the port drains them in-order:
            // a bare toggle after a {capture} fires at different times on the
            // two engines (session-21 hint-dismiss race).  Bare form applies
            // as soon as the cursor reaches it, as before.
            if (op.t >= 0 && (g_seg_base + op.t) > frame) break;
            g_auto_dismiss = (op.n | 0);
            log('segtrace: auto_dismiss = ' + g_auto_dismiss + ' @ frame ' + frame);
            g_seg_idx++;
            continue;
        }

        g_seg_idx++;                                   // unknown op — skip
    }

    // Completion: every op consumed and no capture still pending.
    if (!g_seg_done_sent && g_seg_idx >= g_seg_ops.length &&
        (!g_capture_at_frames || g_capture_at_frames.size === 0)) {
        g_seg_done_sent = true;
        send({kind: 'segtrace_done', frame: frame});
    }
}

// Engine-thread command queue (owner idea).  probeEnqueueCall pushes a
// {va,args} call; the per-tick hook drains it HERE — so the call runs in the
// engine's OWN context (main thread, between ticks), no V8-vs-engine race.  A
// naked call from the V8 thread crashed the game (FUN_0043ac23); this is the
// safe path, and the same substrate the AI-POV render hooks will use.
let g_call_queue = [];
let g_call_seq   = 0;
function drainCallQueue() {
    if (g_call_queue.length === 0) return;
    const q = g_call_queue; g_call_queue = [];
    for (let i = 0; i < q.length; i++) {
        const c = q[i];
        try {
            const fn = new NativeFunction(rva(c.va >>> 0), 'int32',
                c.args.map(function () { return 'int32'; }), c.abi);
            const ret = fn.apply(null, c.args);
            send({kind: 'call_result', id: c.id, va: c.va, ret: (ret | 0)});
        } catch (e) {
            send({kind: 'call_result', id: c.id, va: c.va, err: e.message});
        }
    }
}

// Reactive popup auto-dismiss — the retail mirror of the port's engine.c:2098
// chip.  Called from engine_tick.onEnter (before the tick body, so the freeze
// is cleared before retail's rate controller checks it): while a modal scroll
// is open (DAT_0053e910 != 0), set the rmb-newly-released flag (DAT_004e6900)
// and call the open-scroll input handler FUN_0047685d, whose right-click path
// closes the scroll — exactly what the port does.  One-shot per scroll (the
// dismiss clears DAT_0053e910, so the next tick's read is 0).
function autoDismissTick() {
    if (!g_auto_dismiss) return;
    let ep;
    try { ep = rva(ADDR.var_extended_pause).readU32(); } catch (e) { return; }
    if (ep === 0) return;                       // no modal scroll open
    try {
        // Call the dismiss directly (FUN_00476768 — what FUN_0047685d's
        // right-click path invokes): clears DAT_0053e910 → unfreezes the SP
        // sim this tick.  Also set the rmb-released flag for parity with the
        // port's chip (in case a later in-tick handler reads it).
        rva(ADDR.var_rmb_released).writeU8(1);  // DAT_004e6900
        if (!g_popup_dismiss_fn) {
            g_popup_dismiss_fn = new NativeFunction(
                rva(ADDR.fn_popup_dismiss), 'void', []);
        }
        g_popup_dismiss_fn();                   // FUN_00476768
        if (!g_auto_dismiss_logged) {
            g_auto_dismiss_logged = true;
            log('auto_dismiss: dismissed modal scroll (ep was 0x' +
                ep.toString(16) + ')');
        }
    } catch (e) { err('autoDismiss', e.message); }
}

// Drain all replay events whose stamp <= the current tick. Called from
// engine_tick.onEnter right after g_tick_count is bumped: posted messages
// land in the main thread's queue and are picked up by the engine's
// PeekMessage on its very next iteration — i.e., right after this tick
// returns and before the next tick starts. That matches the recording
// timing exactly: events were recorded when DispatchMessage fired
// "between tick g_tick_count and the next", and replayed between the
// same two.
function drainInputReplayForTick(tick) {
    if (!g_input_replay_events) return;
    if (g_replay_idx >= g_input_replay_events.length) return;
    const post = resolvePostMessageA();
    if (!post) return;
    const hwnd = rva(ADDR.var_main_hwnd).readPointer();
    if (hwnd.isNull()) return;     // wait until the main window exists
    while (g_replay_idx < g_input_replay_events.length) {
        const ev = g_input_replay_events[g_replay_idx];
        if (ev.t > tick) break;
        // Mouse-coordinate rescale: trace coords are authored in game space
        // (640x480, the engine's logical UI extent, DAT_004eb274/DAT_004eac4c).
        // The engine maps the WndProc-cached *client* cursor to game coords by
        //   game = cached * game_extent / viewport_screen_extent
        // (FUN_004b191e; viewport screen rect = DAT_004ea180..18c).  When the
        // capture runs at the desktop resolution that screen extent is NOT
        // 640x480 (e.g. 1920x1080), so a 640x480-space lParam scales down and
        // misses every hotspot.  Pre-multiply mouse-message coords by
        // viewport/game so the engine's own divide lands back on the authored
        // game coord, resolution-independently.  Non-mouse messages pass
        // through untouched.
        let lp = ev.l >>> 0;
        if (ev.m >= WM_MOUSEMOVE && ev.m <= WM_RBUTTONDBLCLK) {
            lp = rescaleMouseLParamToClient(lp);
        }
        try {
            post(hwnd, ev.m >>> 0, ev.w >>> 0, lp >>> 0);
            g_replay_post_count++;
            send({kind: 'input_replay_post',
                  idx: g_replay_idx,
                  t: ev.t,
                  m: ev.m,
                  w: ev.w,
                  l: ev.l,
                  tick_now: tick});
        } catch (e) {
            err('drainInputReplay.PostMessageA', e.message);
        }
        g_replay_idx++;
    }
}

// ─── controller-liveness watchdog (leak-proofing) ───────────────────────
//
// Retail is spawned by the elevated frida-server, so if the controller
// (frida_capture.py) is SIGKILLed — no finally, no controller-side watchdog
// can run — the elevated process would leak and be unkillable from non-admin
// WSL.  Defence: the controller sends a heartbeat() RPC every couple seconds;
// this in-target watchdog self-terminates retail (ExitProcess) if the
// heartbeats go silent for g_heartbeat_timeout_ms.  The kill logic lives
// INSIDE the target, so it survives the controller dying by any means.
let g_heartbeat_timeout_ms = 0;          // 0 = disabled
let g_last_heartbeat_ms    = 0;
let g_heartbeat_armed      = false;

function armHeartbeatWatchdog(timeoutMs) {
    g_heartbeat_timeout_ms = timeoutMs | 0;
    g_last_heartbeat_ms    = Date.now();
    if (g_heartbeat_timeout_ms <= 0 || g_heartbeat_armed) return;
    g_heartbeat_armed = true;
    setInterval(function () {
        if (g_heartbeat_timeout_ms <= 0) return;
        if ((Date.now() - g_last_heartbeat_ms) <= g_heartbeat_timeout_ms) return;
        // Controller has gone silent — self-terminate so we don't leak.
        try {
            const ep = Module.findExportByName('kernel32.dll', 'ExitProcess');
            if (ep) {
                new NativeFunction(ep, 'void', ['uint32'])(0);
            }
        } catch (e) { /* nothing left to do if even this fails */ }
    }, 1000);
    log('heartbeat watchdog armed (timeout ' + g_heartbeat_timeout_ms + 'ms)');
}

// ─── call tracer (Phase G.3) ────────────────────────────────────────────
//
// When call_trace is enabled the agent hooks every Ghidra-VA in
// g_call_trace_vas and emits one event per invocation into a per-tick
// buffer, flushed once per engine_tick (in engine_tick.onLeave) as a
// `call_trace_batch`.  tools/call_trace_diff.py diffs the resulting
// retail trace against the port's --call-trace JSONL to surface
// unported (retail-only) and divergent (port-only) calls per frame.
//
// Schema per event: {va, ret_va, frame, ts, thr}.  `va` is the Ghidra
// VA hooked; `ret_va` is module-relative (add IMAGE_BASE for a Ghidra
// VA) — identical convention to src/call_trace.c.  `frame` is
// g_tick_count, which is bumped in engine_tick.onEnter at the same
// FUN_004b99c0 boundary the port's g_engine_frame_counter uses, so the
// two traces align 1:1.
let g_call_trace_enabled = false;
let g_call_trace_frames  = null;    // Set<int> or null (null = every frame)
let g_call_trace_vas     = [];      // [int] Ghidra-VAs to hook
let g_call_trace_hooked  = false;
let g_call_trace_buffer  = [];
let g_call_trace_n_ok    = 0;
let g_call_trace_n_fail  = 0;
// Cum-sec-gated window mode: start emitting once the engine's cumulative-
// seconds counter reaches g_call_trace_min_cum_secs (i.e. we've reached the
// target scene — same "reach a scene" knob frame-capture warmup uses), then
// emit for g_call_trace_max_frames ticks and stop.  Lets us trace a specific
// scene reached under turbo without knowing its (unpredictable) tick number.
let g_call_trace_min_cum_secs = 0;  // 0 = disabled (use frame filter instead)
let g_call_trace_max_frames   = 8;
let g_call_trace_window_open  = false;
let g_call_trace_window_end   = 0;

// Per-batch event cap.  The pre-bring-up startup window (CRT init with
// hundreds of hooks firing) can dump huge bursts into one batch; GLib's
// DBus transport tops out at 128MiB per blob and frida-server kills the
// channel (target dies with no diagnostic) if exceeded.  Force a flush
// whenever the buffer crosses this many events so each send() stays
// bounded.
const CALL_TRACE_FLUSH_AT = 20000;

function callTraceShouldEmit() {
    if (!g_call_trace_enabled) return false;
    if (g_call_trace_min_cum_secs > 0) return g_call_trace_window_open;
    if (g_call_trace_frames === null) return true;
    return g_call_trace_frames.has(g_tick_count);
}

// Once per tick: open the cum-sec-gated emit window when the engine reaches
// the target scene, and close it after max_frames ticks.  Called from
// engine_tick.onEnter (g_tick_count already bumped).
function callTraceWindowTick() {
    if (!g_call_trace_enabled || g_call_trace_min_cum_secs <= 0) return;
    if (!g_call_trace_window_open) {
        const cs = rva(ADDR.var_fps_cum_secs).readU32();
        if (cs >= g_call_trace_min_cum_secs) {
            g_call_trace_window_open = true;
            g_call_trace_window_end  = g_tick_count + g_call_trace_max_frames;
            send({kind: 'call_trace_window_open',
                  cum_secs: cs, tick: g_tick_count,
                  until_tick: g_call_trace_window_end});
        }
    } else if (g_tick_count >= g_call_trace_window_end) {
        g_call_trace_window_open = false;
        g_call_trace_enabled     = false;   // window done — stop emitting
        send({kind: 'call_trace_window_done', tick: g_tick_count});
    }
}

function callTraceFlush(frameNumber) {
    if (g_call_trace_buffer.length === 0) return;
    const events = g_call_trace_buffer;
    g_call_trace_buffer = [];
    send({kind:  'call_trace_batch',
          frame: frameNumber,
          count: events.length,
          events: events});
}

// ─── memory-access watch (Phase H / D.7) ────────────────────────────────
//
// Finds the WRITER of an engine memory region we can't locate by reading the
// decompile (reached via indirect dispatch, or dropped from Ghidra's output).
// MemoryAccessMonitor write-protects the region in retail; every access traps
// with the faulting instruction's address → the owning engine function (via
// tools/mem_watch.py + the port ledger) → the chip to port.
//
// MemoryAccessMonitor semantics (the two hard-won lessons, carried from
// openrecet's D.7 build log):
//   1. PAGE-GRANULAR + OP-BLIND. It notifies on the FIRST access of each
//      monitored 4KiB page then disables that page; it can't tell reads from
//      writes at arm time. So we capture EVERY trapped op and let the driver
//      rank by op — dropping reads here risks a read silently consuming the
//      page's one-shot before the writer we're hunting runs. On a COLD page
//      (region written at a discrete event, no per-frame neighbour traffic)
//      this nails the writer; on a READ-HOT page precise-mode's re-arm budget
//      burns on neighbour reads before the write lands — keep regions tight.
//   2. HW write watchpoint is byte-granular but CRASHED retail in openrecet —
//      not implemented here; revisit with single-thread arming + tick-freeze.
let g_mem_watch_enabled  = false;
let g_mem_watch_regions  = [];    // [{va, size, label, access}] — Ghidra VAs
let g_mem_watch_ranges   = [];    // cached [{base: NativePointer, size}] for re-arm
let g_mem_watch_buffer   = [];    // batched {op, from, addr, region, label}
let g_mem_watch_n        = 0;     // in-region hits recorded this session
let g_mem_watch_neighbor = 0;     // page-neighbor traps skipped (precise mode)
let g_mem_watch_rearm    = 0;     // re-arm count (budget against the cap)
let g_mem_watch_precise  = true;
const MEM_WATCH_FLUSH_AT  = 50000;
let g_mem_watch_rearm_cap = 8000;  // max re-arms before giving up on a hot page
                                   // (configurable via --rearm-cap; lever 4)
const MEM_WATCH_MAX_HITS  = 64;    // stop re-arming after this many in-region hits

// Bracketed arming (lever 1, --arm-between ENTER:LEAVE).  Instead of arming
// MemoryAccessMonitor pre-resume (so it competes with a read-hot page's
// neighbour traffic for the WHOLE run), defer the arm to a function window:
// arm on ENTER's onEnter, disarm on LEAVE's onLeave.  The budget then only
// burns DURING the suspect call — turning "boot→frame-N of hot traffic" into a
// few-thousand-instruction window.  ENTER may equal LEAVE (bracket that call).
let g_mem_watch_arm_between    = null;   // {enter:VA, leave:VA} or null
let g_mem_watch_bracket_active = false;

// Snapshot-diff mode (lever 2, --snapshot-region).  Poll regions each tick and
// emit the changed bytes + tick.  Op-agnostic and immune to page-read heat —
// it never traps, so a read-hot page is irrelevant.  Localises the WRITER'S
// TICK (then a call-trace over that tick pins the function); does NOT catch an
// intra-tick transient write that is overwritten before tick end (use the
// bracketed trap for those).
let g_mem_snapshot_enabled = false;
let g_mem_snapshot_regions = [];   // [{va, size, label}] — Ghidra VAs
let g_mem_snapshot_last    = [];   // [Uint8Array|null] last-seen bytes per region
let g_mem_snapshot_buffer  = [];   // batched change records

function memWatchFlush(frameNumber) {
    if (g_mem_watch_buffer.length === 0) return;
    const events = g_mem_watch_buffer;
    g_mem_watch_buffer = [];
    send({kind: 'mem_access_batch',
          frame: frameNumber,
          count: events.length,
          events: events});
}

// Whether a trapped access landed inside the recorded extent of region `idx`
// (vs. a page neighbor that merely shares the 4KiB page).
function memWatchInRegion(idx, addrPtr) {
    const r = g_mem_watch_regions[idx];
    if (!r) return false;
    const base = rva(r.va);
    return addrPtr.compare(base) >= 0 &&
           addrPtr.compare(base.add(r.size)) < 0;
}

// (Re-)arm MemoryAccessMonitor over the cached ranges. Idempotent —
// MemoryAccessMonitor.enable() replaces any prior monitor, which is exactly
// the re-arm we want after a page's one-shot fires.
function memWatchArm() {
    MemoryAccessMonitor.enable(g_mem_watch_ranges, {onAccess: memWatchOnAccess});
}

function memWatchOnAccess(details) {
    const idx = details.rangeIndex | 0;
    const region = g_mem_watch_regions[idx] || {};
    const inRegion = memWatchInRegion(idx, details.address);

    if (g_mem_watch_precise && !inRegion) {
        // Page neighbor consumed this page's one-shot. Re-arm and keep
        // hunting, unless the page is so hot we've burned the budget.
        g_mem_watch_neighbor++;
        if (g_mem_watch_rearm < g_mem_watch_rearm_cap) {
            g_mem_watch_rearm++;
            try { memWatchArm(); }
            catch (e) { err('memWatchArm', e.message); }
        } else if (g_mem_watch_rearm === g_mem_watch_rearm_cap) {
            g_mem_watch_rearm++;   // log-once sentinel
            log('mem_watch: re-arm budget (' + g_mem_watch_rearm_cap +
                ') exhausted on a hot page for region "' +
                (region.label || idx) + '" before any in-region write — ' +
                'consider a narrower region or a HW write breakpoint.');
        }
        return;
    }

    // In-region hit (or non-precise mode: record everything).
    g_mem_watch_n++;
    g_mem_watch_buffer.push({
        op:     details.operation,            // read | write | execute
        from:   toGhidraVa(details.from),     // faulting insn VA
        addr:   toGhidraVa(details.address),  // accessed data VA
        region: idx,
        label:  region.label || '',
        ts:     nowMs(),
    });
    if (g_mem_watch_buffer.length >= MEM_WATCH_FLUSH_AT) {
        memWatchFlush(g_tick_count);
    }
    // Keep watching so additional distinct writers of the same field surface,
    // up to a sane cap. (In non-precise mode the one-shot has disabled the
    // page and we deliberately let it stay disabled.)
    if (g_mem_watch_precise &&
        g_mem_watch_n < MEM_WATCH_MAX_HITS &&
        g_mem_watch_rearm < g_mem_watch_rearm_cap) {
        g_mem_watch_rearm++;
        try { memWatchArm(); }
        catch (e) { err('memWatchArm', e.message); }
    }
}

function installMemoryWatch(regions, precise, deferArm) {
    g_mem_watch_regions = (regions || []).map(function (r) {
        return {
            va:     r.va | 0,
            size:   (r.size | 0) || 16,
            label:  String(r.label || ('0x' + (r.va >>> 0).toString(16))),
            access: (r.access === 'rw') ? 'rw' : 'w',
        };
    });
    if (g_mem_watch_regions.length === 0) {
        err('installMemoryWatch', 'no regions given');
        return false;
    }
    g_mem_watch_precise  = (precise !== false);
    g_mem_watch_n        = 0;
    g_mem_watch_neighbor = 0;
    g_mem_watch_rearm    = 0;
    g_mem_watch_ranges   = g_mem_watch_regions.map(function (r) {
        return {base: rva(r.va), size: r.size};
    });

    // Bracketed mode configures the ranges now but defers the actual arm to the
    // ENTER hook (installMemWatchBracket), so MemoryAccessMonitor is only live
    // during the suspect call window.
    if (deferArm) {
        g_mem_watch_enabled = true;
        log('mem_watch: configured ' + g_mem_watch_regions.length +
            ' region(s) [deferred arm — bracketed]: ' +
            g_mem_watch_regions.map(function (r) {
                return r.label + '@0x' + (r.va >>> 0).toString(16) +
                       '+' + r.size + '(' + r.access + ')';
            }).join(', '));
        send({kind: 'mem_watch_ready',
              precise: g_mem_watch_precise,
              regions: g_mem_watch_regions.map(function (r) {
                  return {va: r.va, size: r.size, label: r.label, access: r.access};
              })});
        return true;
    }

    try {
        memWatchArm();
    } catch (e) {
        err('installMemoryWatch', e.message + ' ' + (e.stack || ''));
        return false;
    }

    g_mem_watch_enabled = true;
    log('mem_watch: armed ' + g_mem_watch_regions.length + ' region(s) [' +
        (g_mem_watch_precise ? 'precise' : 'raw') + ']: ' +
        g_mem_watch_regions.map(function (r) {
            return r.label + '@0x' + (r.va >>> 0).toString(16) +
                   '+' + r.size + '(' + r.access + ')';
        }).join(', '));
    send({kind: 'mem_watch_ready',
          precise: g_mem_watch_precise,
          regions: g_mem_watch_regions.map(function (r) {
              return {va: r.va, size: r.size, label: r.label, access: r.access};
          })});
    return true;
}

// Lever 1 — bracketed arming.  Attaches the ENTER/LEAVE pair so the monitor is
// armed only inside the window.  ENTER==LEAVE brackets exactly that call (arm
// onEnter, disarm onLeave of the same function, re-armed for each call).
function installMemWatchBracket() {
    const b = g_mem_watch_arm_between;
    if (!b) return;
    Interceptor.attach(rva(b.enter), {
        onEnter: function () {
            if (g_mem_watch_bracket_active) return;   // already inside the window
            g_mem_watch_bracket_active = true;
            try { memWatchArm(); }
            catch (e) { err('memWatchBracket.arm', e.message); }
        },
    });
    Interceptor.attach(rva(b.leave), {
        onLeave: function () {
            if (!g_mem_watch_bracket_active) return;
            g_mem_watch_bracket_active = false;
            try { MemoryAccessMonitor.disable(); }
            catch (e) { err('memWatchBracket.disarm', e.message); }
            memWatchFlush(g_tick_count);
            log('mem_watch: bracket window closed @ 0x' +
                (b.leave >>> 0).toString(16) + ' (hits=' + g_mem_watch_n +
                ' neighbor=' + g_mem_watch_neighbor +
                ' rearm=' + g_mem_watch_rearm + '/' + g_mem_watch_rearm_cap + ')');
        },
    });
    log('mem_watch: bracketed arm window enter=0x' +
        (b.enter >>> 0).toString(16) + ' leave=0x' +
        (b.leave >>> 0).toString(16) + ' (deferred arm)');
}

// Lever 2 — snapshot-diff.  Capture the baseline once (pre-resume), then
// memWatchSnapshotPoll() each tick compares + emits changed bytes.
function memSnapshotInit() {
    g_mem_snapshot_last = g_mem_snapshot_regions.map(function (r) {
        try {
            const ab = rva(r.va).readByteArray(r.size);
            return ab ? new Uint8Array(ab) : null;
        } catch (e) { return null; }
    });
    log('mem_snapshot: baseline captured for ' +
        g_mem_snapshot_regions.length + ' region(s): ' +
        g_mem_snapshot_regions.map(function (r) {
            return r.label + '@0x' + (r.va >>> 0).toString(16) + '+' + r.size;
        }).join(', '));
}

function memSnapshotFlush(frameNumber) {
    if (g_mem_snapshot_buffer.length === 0) return;
    const events = g_mem_snapshot_buffer;
    g_mem_snapshot_buffer = [];
    send({kind: 'mem_snapshot', frame: frameNumber, events: events});
}

function memSnapshotPoll(frameNumber) {
    if (!g_mem_snapshot_enabled) return;
    for (let i = 0; i < g_mem_snapshot_regions.length; i++) {
        const r = g_mem_snapshot_regions[i];
        let cur;
        try {
            const ab = rva(r.va).readByteArray(r.size);
            if (!ab) continue;
            cur = new Uint8Array(ab);
        } catch (e) { continue; }
        const prev = g_mem_snapshot_last[i];
        if (!prev) { g_mem_snapshot_last[i] = cur; continue; }
        const changes = [];
        const lim = Math.min(prev.length, cur.length);
        for (let o = 0; o < lim; o++) {
            if (cur[o] !== prev[o]) changes.push([o, prev[o], cur[o]]);
        }
        if (changes.length) {
            g_mem_snapshot_buffer.push({
                region: i, label: r.label, va: r.va,
                tick: frameNumber, n: changes.length, changes: changes,
            });
            g_mem_snapshot_last[i] = cur;
        }
    }
    memSnapshotFlush(frameNumber);
}

function installCallTraceHooks(vasArray) {
    if (g_call_trace_hooked) return;
    // Each closure captures `va` directly (let-binding per iteration) so
    // each hook reports its own address without reading a register.
    for (let i = 0; i < vasArray.length; i++) {
        const va = vasArray[i] | 0;
        try {
            Interceptor.attach(rva(va), {
                onEnter: function () {
                    if (!callTraceShouldEmit()) return;
                    g_call_trace_buffer.push({
                        va:     va,
                        ret_va: this.returnAddress.sub(g_base).toUInt32(),
                        // Stamp the frame at call time (g_tick_count now),
                        // not at flush time — a hooked function can fire
                        // between ticks (message pump / render outside the
                        // tick body), and attributing it to the flush frame
                        // would be off-by-one vs the port's per-call stamp.
                        frame:  g_tick_count,
                        ts:     nowMs(),
                        thr:    this.threadId,
                    });
                    if (g_call_trace_buffer.length >= CALL_TRACE_FLUSH_AT) {
                        callTraceFlush(g_tick_count);
                    }
                },
            });
            g_call_trace_n_ok++;
        } catch (e) {
            // Frida couldn't trampoline this VA (unsupported prefix, a
            // prior hook on the same byte, etc.).  Counted so the driver
            // can spot a degraded run.
            g_call_trace_n_fail++;
        }
    }
    g_call_trace_hooked = true;
    log('call_trace: hooked ' + g_call_trace_n_ok + ' VAs (' +
        g_call_trace_n_fail + ' failed) of ' + vasArray.length);
    send({kind:   'call_trace_hooked',
          n_ok:   g_call_trace_n_ok,
          n_fail: g_call_trace_n_fail,
          n_req:  vasArray.length});
}

// Per-tick anchor — `FUN_004b99c0` is called once per main-loop iteration.
// We use its onEnter as:
//   1. main-thread-id capture (first entry only) for the virtual-clock gate.
//   2. virtual-clock advance by g_turbo_step_ms (when turbo v2 is active).
//   3. g_tick_count++ — 1-indexed tick id used as the timestamp for input
//      recording / replay.
//   4. Input-replay drain (posts queued WM_* messages for this tick).
// We use its onLeave to publish an `fps_sample` event whenever the engine's
// FPS-counter cumulative_seconds counter ticks — this is the verification
// knob for turbo: without turbo, fps_sample.current_fps lands wherever
// retail's render path actually clocks (~30 in Lords2's default cap);
// with turbo at 17 ms/tick we expect ~58.
//
// Hook installs unconditionally — the bump is gated on g_clock_virtualised
// so a non-turbo run still benefits from the FPS-sample emission without
// disturbing wall-clock time.

// ─── intro-movie skip (skip_intro flag) ───────────────────────────────
// See the ADDR.fn_smack_step block for the rationale.  Attaches (not
// replaces) two Smacker functions so the engine keeps driving its own
// intro→title state machine:
//   FUN_0042da18 (open) — bracketed by g_smack_in_open so the open-time
//     FUN_0042dbc7 call is left untouched (forcing it done there corrupts
//     the return-state plumbing: FUN_0042d91b writes _DAT_005169b0 only
//     AFTER FUN_0042da18 returns).
//   FUN_0042dbc7 (step) — on each main-loop call (not during open), if a
//     boot-intro movie is playing (engine state == 1), snap the current
//     frame to the last frame so the stepper takes its done→chain branch
//     this iteration.  intro / imptitle / credits then finish one per frame.
const SMACK_FRAMES_OFF   = 0x0c;   // Smack* + 0x0c  = frame count (int)
const SMACK_FRAMENUM_OFF = 0x374;  // Smack* + 0x374 = current frame (uint)

function installSkipIntroHook() {
    Interceptor.attach(rva(ADDR.fn_smack_open), {
        onEnter: function () { g_smack_in_open++; },
        onLeave: function () { if (g_smack_in_open > 0) g_smack_in_open--; },
    });
    Interceptor.attach(rva(ADDR.fn_smack_step), {
        onEnter: function () {
            if (g_smack_in_open > 0) return;     // open-time step — leave alone
            // Only fast-finish the boot intro chain (engine state == 1); leave
            // in-game cutscenes (state != 1) to play normally.
            if (rva(ADDR.var_sim_game_state_phase).readS32() !== 1) return;
            const handle = rva(ADDR.var_smack_handle).readPointer();
            if (handle.isNull()) return;
            const count = handle.add(SMACK_FRAMES_OFF).readS32();
            if (count <= 1) return;
            handle.add(SMACK_FRAMENUM_OFF).writeU32((count - 1) >>> 0);
            g_skip_intro_forced++;
            if (g_skip_intro_forced <= 8) {
                log('skip_intro: snapped Smacker movie to final frame ' +
                    (count - 1) + ' (handle=' + handle + ', #' +
                    g_skip_intro_forced + ')');
            }
        },
    });
    // Force _SmackWait → 0 ("ready") during a step-time boot-intro snap so the
    // stepper always reaches its done→chain branch even when the decode thread
    // never reports ready (silent-audio / non-turbo).  The same gate as the
    // snap (skip on, engine state 1, not inside open) so the open-time first
    // frame decodes normally and in-game movies are untouched.
    const waitFn = rva(ADDR.var_smack_wait_iat).readPointer();
    if (!waitFn.isNull()) {
        Interceptor.attach(waitFn, {
            onLeave: function (retval) {
                if (g_smack_in_open > 0) return;
                if (rva(ADDR.var_sim_game_state_phase).readS32() !== 1) return;
                if (rva(ADDR.var_smack_handle).readPointer().isNull()) return;
                retval.replace(ptr(0));          // report "frame ready"
            },
        });
        log('skip_intro: _SmackWait force-ready armed @ ' + waitFn);
    } else {
        log('skip_intro: WARNING _SmackWait IAT slot empty (smackw32 not ' +
            'resolved yet); frame-snap only');
    }
    log('skip_intro: armed (FUN_0042dbc7 step + FUN_0042da18 open guard; ' +
        'each boot-intro Smacker movie ends on its next main-loop frame)');
}

function installEngineTickHook() {
    Interceptor.attach(rva(ADDR.fn_engine_tick), {
        onEnter: function () {
            if (g_main_thread_id === -1) {
                g_main_thread_id = Process.getCurrentThreadId();
                log('engine_tick: main thread id captured = ' + g_main_thread_id);
            }
            if (g_clock_virtualised) {
                g_virtual_now_ms = (g_virtual_now_ms + g_turbo_step_ms) >>> 0;
            }
            g_tick_count = (g_tick_count + 1) >>> 0;
            g_engine_in_tick = 1;
            pinRngSeed();
            pinPhaseCounters();
            anchorTick(g_tick_count);
            segtraceTick(g_tick_count);
            autoDismissTick();   // reactive modal-scroll dismiss (mirror of port)
            drainCallQueue();    // run queued action calls ON the engine thread
            watchTick(g_tick_count);
            // Arm draw capture BEFORE this tick renders, iff it's a scheduled
            // capture tick (segtraceTick above may have just added it for {capture:0}).
            if (g_drawcap_active && g_capture_at_frames &&
                g_capture_at_frames.has(g_tick_count)) {
                g_dc_begin();
            }
            // --capture-force-redraw: force a full strat re-render this tick so the
            // map composites into the draw journal (DAT_0057d340 = g_demand_redraw).
            if (g_capture_force_redraw && g_capture_at_frames &&
                g_capture_at_frames.has(g_tick_count)) {
                try { rva(0x0057d340).writeS32(2); } catch (e) { err('force_redraw', e.message); }
            }
            if (g_call_trace_enabled && g_call_trace_min_cum_secs > 0) {
                callTraceWindowTick();
            }
            if (g_input_replay_events) {
                drainInputReplayForTick(g_tick_count);
            }
            // Poke forced-state regions every tick so they survive the
            // engine's own per-frame writes (e.g. hold a name-box string).
            for (let i = 0; i < g_poke_globals.length; i++) {
                try {
                    rva(g_poke_globals[i].va).writeByteArray(g_poke_globals[i].bytes);
                } catch (e) { /* region not mapped yet — retry next tick */ }
            }
        },
        onLeave: function () {
            try {
                // Anchor-relative capture (--input-segtrace {capture}).  Checked
                // every tick (before the cum-secs early-return) so the exact
                // scheduled frame is grabbed.  Captured here, strictly between
                // engine_tick iterations, same as the heartbeat path.
                if (g_capture_at_frames && g_capture_at_frames.has(g_tick_count)) {
                    // captureFrameOnce tight-packs the final 640x480 plane into
                    // g_capture_scratch (after [hdr 12][palette 1024]); finalize
                    // the last pending draw against THAT (the render pass already
                    // unlocked the back buffer, so DAT_004ea1a8 is stale here).
                    captureFrameOnce();
                    if (g_drawcap_active) {
                        try {
                            if (g_capture_scratch &&
                                g_last_capture_w === 640 && g_last_capture_h === 480) {
                                g_dc_flush_ptr(g_capture_scratch.add(12 + 1024), 640, 480);
                            }
                        } catch (e) { err('drawcap.flush', e.message); }
                        try {
                            drawcapSend(g_frame_capture_count - 1);
                        } catch (e) { err('drawcap.send', e.message); }
                        g_dc_end();
                    }
                    stateDumpOnce(g_tick_count);   // schema snapshot, same instant
                    g_capture_at_frames.delete(g_tick_count);
                }
                const cum_secs = rva(ADDR.var_fps_cum_secs).readU32();
                if (cum_secs === g_last_fps_cum_secs) return;
                g_last_fps_cum_secs = cum_secs;
                const current_fps = rva(ADDR.var_fps_current).readU32();
                const cum_sum     = rva(ADDR.var_fps_cum_sum).readU32();
                const avg_fps     = cum_secs > 0 ? (cum_sum / cum_secs) | 0 : 0;
                g_fps_sample_count++;
                send({kind: 'fps_sample',
                      current_fps: current_fps,
                      cumulative_fps: cum_sum,
                      cumulative_seconds: cum_secs,
                      avg_fps: avg_fps,
                      virtual_now_ms: g_virtual_now_ms,
                      t_ms: nowMs()});
                // Frame-capture rides the same heartbeat — one capture per
                // engine cumulative-seconds rollover, until the configured
                // cap is reached.  Skip the first N rollovers so the engine
                // has time to finish DDraw bring-up + paint a real first
                // frame (the back surface is uninitialised before that, so
                // captures land all-black).  Capture is safe here because
                // we're strictly between engine_tick iterations.
                if (g_frame_capture_enabled &&
                    cum_secs > g_frame_capture_warmup_secs &&
                    (g_frame_capture_max === 0 ||
                     g_frame_capture_count < g_frame_capture_max)) {
                    captureFrameOnce();
                }
            } catch (e) {
                err('engine_tick.onLeave', e.message);
            } finally {
                // Flush this tick's call-trace buffer (events were pushed
                // by the hooked leaf functions during the tick, stamped
                // with the current g_tick_count).  No-op unless enabled.
                if (g_call_trace_enabled) {
                    callTraceFlush(g_tick_count);
                }
                if (g_mem_watch_enabled) {
                    memWatchFlush(g_tick_count);
                }
                if (g_mem_snapshot_enabled) {
                    memSnapshotPoll(g_tick_count);
                }
                g_engine_in_tick = 0;
                // Lazy install of the DDraw → GDI present seat.  The
                // primary surface pointer doesn't exist until retail's
                // FUN_0042ec91 returns; engine_tick is the cheapest
                // available "post bring-up" hook to poll.  Once patched
                // the call short-circuits via g_primary_blt_patched.
                if (g_gdi_seat_enabled && !g_primary_blt_patched) {
                    installPrimaryBltGdiSeat();
                }
                // Interactive verification path — drive a continuous
                // GDI present + auto-show the engine's main window.
                // Both early-return when their gating flags are off.
                tryForceAppActive();
                tryShowEngineWindow();
                tryAgentDrivenPresent();
                maybeStreamFrame();
            }
        },
    });
    log('engine_tick anchor installed @ ' + rva(ADDR.fn_engine_tick) +
        ' (FUN_004b99c0)');
}

// ─── pure-function differential test (Phase 4 of harness-roadmap) ─────
// Retail functions whose pure-C ports live under src/engine_*.c:
//   FUN_00404a46  (engine_rng_step)            — double-LFSR step, no Win32 deps
//   FUN_004b27bf  (engine_compute_fps)         — calls winmm!timeGetTime twice
//                                                  on the rollover branch
//   FUN_00451f1a  (engine_compute_anim_cycle)  — bumps a mod-16 counter once
//                                                  per 80 ms phase tick
//   FUN_004b20ed  (engine_compute_frame_delta) — calls timeGetTime once,
//                                                  computes delta with 30 ms gate
//
// Each runRetail* RPC snapshots ALL observable globals (state + output
// mirrors), writes the test-vector's inputs, invokes the retail function
// via NativeFunction, snapshots outputs, then restores the snapshot — so
// the engine itself never sees a perturbation, no matter how many vectors
// the orchestrator runs through.
//
// The orchestrator is responsible for diffing the returned outputs against
// our port (compiled host-side via tests/Makefile's libengine path, called
// via ctypes from tools/diff_test.py).  This agent does NOT compare; it
// only reports the retail observation.

function resolveRetailFunctions() {
    if (g_nf_retail_rng_step) return true;
    if (g_base === null) {
        err('diff_test', 'g_base unset (init not completed yet?)');
        return false;
    }
    g_nf_retail_rng_step = new NativeFunction(
        rva(ADDR.fn_engine_rng_step), 'void', [], 'stdcall');
    g_nf_retail_compute_fps = new NativeFunction(
        rva(ADDR.fn_engine_compute_fps), 'int32', [], 'stdcall');
    g_nf_retail_anim_cycle = new NativeFunction(
        rva(ADDR.fn_engine_anim_cycle), 'void', [], 'stdcall');
    g_nf_retail_frame_delta = new NativeFunction(
        rva(ADDR.fn_engine_frame_delta), 'int32', [], 'stdcall');
    g_nf_retail_phase_ticks = new NativeFunction(
        rva(ADDR.fn_engine_phase_ticks), 'void', [], 'stdcall');
    g_nf_retail_input_state = new NativeFunction(
        rva(ADDR.fn_engine_input_state), 'void', [], 'stdcall');
    // FUN_004bb978 returns the sub-step count (0/1/2) — declared int32.
    g_nf_retail_sim_sub_steps = new NativeFunction(
        rva(ADDR.fn_engine_sim_sub_steps), 'int32', [], 'stdcall');
    g_nf_retail_state_hash = new NativeFunction(
        rva(ADDR.fn_engine_state_hash), 'void', [], 'stdcall');
    // FUN_004917dd is __cdecl(int param_1) — declare with the cdecl ABI
    // and one int32 arg.  All the other targets are 'stdcall' / 0 args
    // because they read inputs from globals; this one takes the
    // tile_byte_offset on the stack.
    g_nf_retail_tile_expand = new NativeFunction(
        rva(ADDR.fn_engine_tile_expand_footprint),
        'void', ['int32'], 'mscdecl');
    // FUN_004472ce is __cdecl(int current_tick) — same shape as tile_expand
    // (the only other cdecl target so far).  Declared 'void' (return value
    // unused on the retail side too).
    g_nf_retail_delayed_action = new NativeFunction(
        rva(ADDR.fn_engine_delayed_action_dispatch),
        'void', ['int32'], 'mscdecl');
    return true;
}

// One-shot per test plan: snapshot retail's 2400-byte rate-gate LUT at
// DAT_004e3260, then write the orchestrator-supplied LUT bytes (a Uint8Array-
// like array of 2400 u8s).  Re-runs (e.g. retries) skip the snapshot on
// subsequent calls.  Pair with uninstallSimSubStepsRateLut() to flush the
// backup back when the test plan finishes.
function installSimSubStepsRateLut(lut_bytes) {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (!Array.isArray(lut_bytes) || lut_bytes.length !== SIM_RATE_LUT_BYTES) {
        return {error: 'lut_bytes must be a ' + SIM_RATE_LUT_BYTES +
                       '-element array (got ' +
                       (Array.isArray(lut_bytes) ? lut_bytes.length : typeof lut_bytes) +
                       ')'};
    }
    const lut_ptr = rva(ADDR.var_sim_rate_gate_lut);
    if (g_sim_rate_lut_backup === null) {
        g_sim_rate_lut_backup = new Uint8Array(SIM_RATE_LUT_BYTES);
        const src = lut_ptr.readByteArray(SIM_RATE_LUT_BYTES);
        g_sim_rate_lut_backup.set(new Uint8Array(src));
    }
    const buf = new Uint8Array(SIM_RATE_LUT_BYTES);
    for (let i = 0; i < SIM_RATE_LUT_BYTES; i++) {
        buf[i] = lut_bytes[i] & 0xff;
    }
    lut_ptr.writeByteArray(buf.buffer);
    return {ok: true, snapshotted: g_sim_rate_lut_backup !== null};
}

function uninstallSimSubStepsRateLut() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_sim_rate_lut_backup === null) {
        return {ok: true, restored: false};
    }
    rva(ADDR.var_sim_rate_gate_lut).writeByteArray(g_sim_rate_lut_backup.buffer);
    g_sim_rate_lut_backup = null;
    return {ok: true, restored: true};
}

// Engine-tick freeze: replace FUN_0040e890 (engine_tick_dispatch) with a no-op.
// The engine main thread's message pump keeps running (PeekMessage /
// DispatchMessage) so the process stays responsive; but the sim sub-step loop
// + render phase + LFSR + autosave pump are all skipped.  Used to isolate
// sim_sub_steps diff vectors from the engine main thread's parallel reads of
// our injected globals — see g_sim_engine_freeze_callback's commentary.
function installSimSubStepsEngineFreeze() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_sim_engine_freeze_callback !== null) {
        // Already installed — caller might be retrying a test plan run.
        return {ok: true, already_installed: true};
    }
    // No-op callback: returns immediately, no state change.  Stdcall
    // matches retail's calling convention for FUN_0040e890 (no args,
    // no return value).
    g_sim_engine_freeze_callback = new NativeCallback(
        function () { /* engine-tick suspended for diff */ },
        'void', [], 'stdcall');
    Interceptor.replace(rva(ADDR.fn_engine_tick_dispatch),
                        g_sim_engine_freeze_callback);
    // Flush the interceptor so the replacement is live before we return.
    Interceptor.flush();
    return {ok: true, already_installed: false};
}

function uninstallSimSubStepsEngineFreeze() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_sim_engine_freeze_callback === null) {
        return {ok: true, restored: false};
    }
    Interceptor.revert(rva(ADDR.fn_engine_tick_dispatch));
    Interceptor.flush();
    g_sim_engine_freeze_callback = null;
    return {ok: true, restored: true};
}

// ─── per-tick state hash (FUN_00440231 — engine_compute_state_hash) ─────
//
// Same install/runRetail/uninstall shape as sim_sub_steps, but the
// pre-installed payload here is the SIX strided source arrays (combined
// ~125 KB) rather than a single 2400-byte LUT.
//
// Wire-traffic optimisation: source arrays are filled from a 32-bit seed
// via a shared xorshift32 algorithm (`stateHashFillBytes` below).  The
// orchestrator generates the same bytes Python-side and passes them to
// the port; only the SEED travels over the Frida wire.  Saves ~125 KB
// of JSON per install vs sending the raw bytes.
//
// engine-tick freeze: REQUIRED before the source arrays get written.
// FUN_00440231 reads the same source arrays the engine main thread also
// reads (per-territory, per-army, etc.).  Without the freeze, an engine
// tick during our write would hash inconsistent partial state, then our
// retail call would hash the consistent post-write state — different
// per-call buckets, false-positive race-pass — or worse, the engine
// could crash on the perturbed arrays.  Re-use the existing
// installSimSubStepsEngineFreeze() since the swap target is the same.

// Shared 32-bit xorshift.  Match the Python implementation in
// tools/diff_test.py one-for-one.  Initial state must be non-zero
// (xorshift can't escape 0); the orchestrator guarantees seed != 0.
function stateHashFillBytes(seed, buf, len) {
    let s = (seed >>> 0) || 1;
    const u8 = new Uint8Array(buf);
    for (let i = 0; i < len; i++) {
        s ^= (s << 13) >>> 0;
        s ^= (s >>> 17);
        s ^= (s << 5)  >>> 0;
        s = s >>> 0;
        u8[i] = s & 0xff;
    }
}

function installStateHashSources(seed) {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (typeof seed !== 'number') {
        return {error: 'seed must be a number'};
    }
    // Lazy snapshot — only on first install.  Re-installs (e.g. retries)
    // share the same backup so we always restore the original retail bytes.
    //
    // Critical: COPY the bytes into a fresh Uint8Array (with .set()), don't
    // construct a view directly from readByteArray's return.  Frida's
    // readByteArray returns an ArrayBuffer that can be reclaimed/freed by
    // the runtime between RPC calls — a view onto that buffer becomes
    // dangling and reading from it later (during uninstall) destroys the
    // script context.  Mirrors the install_sim_sub_steps_rate_lut pattern.
    if (g_state_hash_per_army_backup === null) {
        const snap = function (addr, len) {
            const dst = new Uint8Array(len);
            dst.set(new Uint8Array(rva(addr).readByteArray(len)));
            return dst;
        };
        g_state_hash_per_army_backup         = snap(ADDR.var_hash_per_army_768,          STATE_HASH_PER_ARMY_BYTES);
        g_state_hash_player_strategic_backup = snap(ADDR.var_hash_player_strategic_352,  STATE_HASH_PLAYER_STRATEGIC_BYTES);
        g_state_hash_per_territory_backup    = snap(ADDR.var_hash_per_territory_420,     STATE_HASH_PER_TERRITORY_BYTES);
        g_state_hash_per_battle_hex_backup   = snap(ADDR.var_hash_per_battle_hex_432,    STATE_HASH_PER_BATTLE_HEX_BYTES);
        g_state_hash_per_combat_unit_backup  = snap(ADDR.var_hash_per_combat_unit_76,    STATE_HASH_PER_COMBAT_UNIT_BYTES);
        g_state_hash_per_battle_aux_backup   = snap(ADDR.var_hash_per_battle_hex_aux_52, STATE_HASH_PER_BATTLE_AUX_BYTES);
    }
    // Synthesize the deterministic payload from the seed.  Each array
    // gets ITS OWN contiguous slice of the xorshift stream — sub-seed
    // each by mixing the array index into the seed so different arrays
    // get different bytes even at small offsets.  Chunk the writes at
    // <=32 KB to stay under the Frida RPC-buffer threshold that triggers
    // a script-destroyed error on larger single writeByteArray calls
    // (per_territory at 63420 B reliably trips it).
    const CHUNK = 32 * 1024;
    const arrays = [
        {addr: ADDR.var_hash_per_army_768,          len: STATE_HASH_PER_ARMY_BYTES,           tag: 0},
        {addr: ADDR.var_hash_player_strategic_352,  len: STATE_HASH_PLAYER_STRATEGIC_BYTES,   tag: 1},
        {addr: ADDR.var_hash_per_territory_420,     len: STATE_HASH_PER_TERRITORY_BYTES,      tag: 2},
        {addr: ADDR.var_hash_per_battle_hex_432,    len: STATE_HASH_PER_BATTLE_HEX_BYTES,     tag: 3},
        {addr: ADDR.var_hash_per_combat_unit_76,    len: STATE_HASH_PER_COMBAT_UNIT_BYTES,    tag: 4},
        {addr: ADDR.var_hash_per_battle_hex_aux_52, len: STATE_HASH_PER_BATTLE_AUX_BYTES,     tag: 5},
    ];
    for (const a of arrays) {
        const ab = new ArrayBuffer(a.len);
        stateHashFillBytes((seed + 0x9e3779b1 * a.tag) >>> 0, ab, a.len);
        const u8 = new Uint8Array(ab);
        const base = rva(a.addr);
        for (let off = 0; off < a.len; off += CHUNK) {
            const len = Math.min(CHUNK, a.len - off);
            base.add(off).writeByteArray(u8.slice(off, off + len).buffer);
        }
    }
    return {ok: true, snapshotted: g_state_hash_per_army_backup !== null,
            payload_bytes: STATE_HASH_PER_ARMY_BYTES + STATE_HASH_PLAYER_STRATEGIC_BYTES +
                           STATE_HASH_PER_TERRITORY_BYTES + STATE_HASH_PER_BATTLE_HEX_BYTES +
                           STATE_HASH_PER_COMBAT_UNIT_BYTES + STATE_HASH_PER_BATTLE_AUX_BYTES};
}

function uninstallStateHashSources() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_state_hash_per_army_backup === null) {
        return {ok: true, restored: false};
    }
    // Restore each backup back to retail's memory in chunks of <=32 KB.
    // Direct full-array writeByteArray of buffers approaching 64 KB
    // (per_territory is 63420 B) reliably destroyed the script context
    // mid-teardown — narrowing to <=32 KB chunks keeps Frida's RPC
    // buffer under the threshold that triggers the crash.  See
    // findings/pure-function-diff.md for the post-mortem.
    const CHUNK = 32 * 1024;
    function restoreInChunks(addr, backup) {
        const total = backup.length;
        const base = rva(addr);
        for (let off = 0; off < total; off += CHUNK) {
            const len = Math.min(CHUNK, total - off);
            // Subarray gives a view; .slice() forces a fresh ArrayBuffer
            // for writeByteArray to consume.
            base.add(off).writeByteArray(backup.slice(off, off + len).buffer);
        }
    }
    restoreInChunks(ADDR.var_hash_per_army_768,          g_state_hash_per_army_backup);
    restoreInChunks(ADDR.var_hash_player_strategic_352,  g_state_hash_player_strategic_backup);
    restoreInChunks(ADDR.var_hash_per_territory_420,     g_state_hash_per_territory_backup);
    restoreInChunks(ADDR.var_hash_per_battle_hex_432,    g_state_hash_per_battle_hex_backup);
    restoreInChunks(ADDR.var_hash_per_combat_unit_76,    g_state_hash_per_combat_unit_backup);
    restoreInChunks(ADDR.var_hash_per_battle_hex_aux_52, g_state_hash_per_battle_aux_backup);
    g_state_hash_per_army_backup         = null;
    g_state_hash_player_strategic_backup = null;
    g_state_hash_per_territory_backup    = null;
    g_state_hash_per_battle_hex_backup   = null;
    g_state_hash_per_combat_unit_backup  = null;
    g_state_hash_per_battle_aux_backup   = null;
    return {ok: true, restored: true};
}

// State-hash diff RPC.  Per-vector inject of 3 scalars (mode, player_slot,
// sim_tick_counter); snapshot+restore of the destination 10-byte bucket
// and 600-byte hash region for the indicated player_slot.  Source-array
// bytes were installed once per test plan via installStateHashSources()
// and are NOT touched per-call.
//
// Engine freeze: orchestrator MUST install installSimSubStepsEngineFreeze
// before the first call (and uninstall at the end).  Without the freeze,
// FUN_00440231's source-array reads race with the engine main thread.
function runRetailStateHash(in_obj) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    if (g_state_hash_per_army_backup === null) {
        return {error: 'installStateHashSources not called'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;
    const slot = in_obj.player_slot | 0;
    // Snapshot the 3 scalar inputs + the destination bucket + region.
    // Mode is u8 in the existing var_sim_game_mode_speed entry.
    // COPY bytes into fresh Uint8Arrays (see installStateHashSources for
    // why a view onto readByteArray's buffer is unsafe across RPC calls).
    const _snap_bucket = new Uint8Array(STATE_HASH_BUCKET_BYTES);
    _snap_bucket.set(new Uint8Array(
        rva(ADDR.var_hash_bucket_base).add(slot * STATE_HASH_BUCKET_BYTES)
            .readByteArray(STATE_HASH_BUCKET_BYTES)));
    const _snap_region = new Uint8Array(STATE_HASH_REGION_BYTES);
    _snap_region.set(new Uint8Array(
        rva(ADDR.var_hash_region_base).add(slot * STATE_HASH_REGION_BYTES)
            .readByteArray(STATE_HASH_REGION_BYTES)));
    const snap = {
        mode:            rva(ADDR.var_sim_game_mode_speed ).readU8(),
        player_slot:     rva(ADDR.var_sim_local_player_idx).readS32(),
        tick_counter:    rva(ADDR.var_sim_tick_counter    ).readU32(),
        bucket: _snap_bucket,
        region: _snap_region,
    };
    let out;
    try {
        rva(ADDR.var_sim_game_mode_speed ).writeU8 (in_obj.mode             & 0xff);
        rva(ADDR.var_sim_local_player_idx).writeS32(slot);
        rva(ADDR.var_sim_tick_counter    ).writeU32(in_obj.sim_tick_counter >>> 0);
        g_nf_retail_state_hash();
        out = {
            bucket: Array.from(new Uint8Array(
                rva(ADDR.var_hash_bucket_base).add(slot * STATE_HASH_BUCKET_BYTES)
                    .readByteArray(STATE_HASH_BUCKET_BYTES))),
            hash_region: Array.from(new Uint8Array(
                rva(ADDR.var_hash_region_base).add(slot * STATE_HASH_REGION_BYTES)
                    .readByteArray(STATE_HASH_REGION_BYTES))),
            // FUN_00440231 doesn't return notify_combat_snapshot directly —
            // the orchestrator derives it from the input mode.  Echo it
            // for parity with the port output.
            notify_combat_snapshot: ((in_obj.mode | 0) === 2) ? 1 : 0,
        };
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out.raced = ((tick_count_after !== tick_count_before) ||
                     in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0;
    } finally {
        rva(ADDR.var_sim_game_mode_speed ).writeU8 (snap.mode);
        rva(ADDR.var_sim_local_player_idx).writeS32(snap.player_slot);
        rva(ADDR.var_sim_tick_counter    ).writeU32(snap.tick_counter);
        rva(ADDR.var_hash_bucket_base).add(slot * STATE_HASH_BUCKET_BYTES)
            .writeByteArray(snap.bucket.buffer);
        rva(ADDR.var_hash_region_base).add(slot * STATE_HASH_REGION_BYTES)
            .writeByteArray(snap.region.buffer);
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// ─── castle-load footprint expansion (FUN_004917dd — engine_tile_expand_footprint) ─
//
// Stateful function — reads + mutates 5 globals across 70.5 KB.  Each
// vector regenerates its pre-state in-place from a per-test-plan seed
// (same xorshift32 the orchestrator uses Python-side), so consecutive
// calls all start from the SAME deterministic baseline.  Only the
// per-call inputs that change between vectors are tile_byte_offset (the
// arg to FUN_004917dd) and the anchor char written to tiles[offset+3].
//
// Wire traffic per call:
//   in:  {seed_only_for_install} +
//        {tile_byte_offset: i32, anchor_char: u8} per vector
//   pre-call writes (agent local, no RPC):
//        tiles (51200), footprint_list (152), last_footprint_tile (4)
//   pre-call writes from in_obj:
//        tiles[offset+3] = anchor_char (1 byte)
//   readback:
//        tiles + path_scratch + stage_scratch + footprint_list +
//        last_footprint_tile (70556 bytes, chunked at <=32 KB each)
//
// Engine freeze: REQUIRED.  tiles/path_scratch/stage_scratch are read
// by the engine's render + pathfinding paths every tick.  Re-use the
// shared install (installSimSubStepsEngineFreeze) — the orchestrator
// adds 'tile_expand' to its shared_freeze_targets set.

// Same xorshift32 + sub-seed mixing as state_hash, but for the 3
// input arrays (tiles / footprint_list / last_footprint_tile).  Tag 0
// is tiles (largest, dominant cost), 1 is footprint_list, 2 is
// last_footprint_tile.  Python mirror lives in tools/diff_test.py as
// tile_expand_fill_bytes — both sides MUST stay bit-identical.
function tileExpandFillBytes(seed, buf, len) {
    let s = (seed >>> 0) || 1;
    const u8 = new Uint8Array(buf);
    for (let i = 0; i < len; i++) {
        s ^= (s << 13) >>> 0;
        s ^= (s >>> 17);
        s ^= (s << 5)  >>> 0;
        s = s >>> 0;
        u8[i] = s & 0xff;
    }
}

// Snapshot retail's 5 globals (lazy, once per test plan).  Restoration
// at uninstall puts the engine back to its pre-test state.
function installTileExpandBackup() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_tile_expand_tiles_backup !== null) {
        return {ok: true, already_installed: true};
    }
    const snap = function (addr, len) {
        const dst = new Uint8Array(len);
        dst.set(new Uint8Array(rva(addr).readByteArray(len)));
        return dst;
    };
    g_tile_expand_tiles_backup          = snap(ADDR.var_tile_tiles,          TILE_EXPAND_TILES_BYTES);
    g_tile_expand_path_scratch_backup   = snap(ADDR.var_tile_path_scratch,   TILE_EXPAND_PATH_SCRATCH_BYTES);
    g_tile_expand_stage_scratch_backup  = snap(ADDR.var_tile_stage_scratch,  TILE_EXPAND_STAGE_SCRATCH_BYTES);
    g_tile_expand_footprint_list_backup = snap(ADDR.var_tile_footprint_list, TILE_EXPAND_FOOTPRINT_LIST_BYTES);
    g_tile_expand_last_footprint_backup = snap(ADDR.var_tile_last_footprint, TILE_EXPAND_LAST_FOOTPRINT_BYTES);
    return {ok: true, already_installed: false,
            snapshot_bytes: TILE_EXPAND_TILES_BYTES +
                            TILE_EXPAND_PATH_SCRATCH_BYTES +
                            TILE_EXPAND_STAGE_SCRATCH_BYTES +
                            TILE_EXPAND_FOOTPRINT_LIST_BYTES +
                            TILE_EXPAND_LAST_FOOTPRINT_BYTES};
}

function uninstallTileExpandBackup() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_tile_expand_tiles_backup === null) {
        return {ok: true, restored: false};
    }
    // Same chunked-restore pattern as state_hash: keep individual writes
    // <=32 KB to stay well clear of the Frida buffer threshold (tiles at
    // 51200 B alone would trip a single writeByteArray).
    const CHUNK = 32 * 1024;
    function restoreInChunks(addr, backup) {
        const total = backup.length;
        const base = rva(addr);
        for (let off = 0; off < total; off += CHUNK) {
            const len = Math.min(CHUNK, total - off);
            base.add(off).writeByteArray(backup.slice(off, off + len).buffer);
        }
    }
    restoreInChunks(ADDR.var_tile_tiles,          g_tile_expand_tiles_backup);
    restoreInChunks(ADDR.var_tile_path_scratch,   g_tile_expand_path_scratch_backup);
    restoreInChunks(ADDR.var_tile_stage_scratch,  g_tile_expand_stage_scratch_backup);
    restoreInChunks(ADDR.var_tile_footprint_list, g_tile_expand_footprint_list_backup);
    restoreInChunks(ADDR.var_tile_last_footprint, g_tile_expand_last_footprint_backup);
    g_tile_expand_tiles_backup          = null;
    g_tile_expand_path_scratch_backup   = null;
    g_tile_expand_stage_scratch_backup  = null;
    g_tile_expand_footprint_list_backup = null;
    g_tile_expand_last_footprint_backup = null;
    return {ok: true, restored: true};
}

// Per-call: re-fill the 3 input arrays from the test-plan seed (each
// array sub-seeded with the same (seed + 0x9e3779b1 * tag) mixing the
// orchestrator uses), inject the anchor byte at tiles[offset+3], call
// FUN_004917dd(offset), read back the 5 globals.  No per-call snapshot/
// restore — the next call's pre-state seeding overwrites the prior
// call's output, and uninstall flushes the original retail bytes back
// at test-plan teardown.
function runRetailTileExpand(in_obj) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    if (g_tile_expand_tiles_backup === null) {
        return {error: 'installTileExpandBackup not called'};
    }
    if (typeof in_obj.seed !== 'number') {
        return {error: 'in_obj.seed must be a number'};
    }
    const seed   = in_obj.seed >>> 0;
    const offset = in_obj.tile_byte_offset | 0;
    const anchor = in_obj.anchor_char & 0xff;

    g_diff_test_caller_tid = Process.getCurrentThreadId();
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;

    let out;
    try {
        // Re-fill tiles (51200) from sub-seed 0.  Chunked at <=32 KB so
        // writeByteArray stays under Frida's instability threshold.
        const CHUNK = 32 * 1024;
        const arrays = [
            {addr: ADDR.var_tile_tiles,          len: TILE_EXPAND_TILES_BYTES,          tag: 0},
            {addr: ADDR.var_tile_footprint_list, len: TILE_EXPAND_FOOTPRINT_LIST_BYTES, tag: 1},
            {addr: ADDR.var_tile_last_footprint, len: TILE_EXPAND_LAST_FOOTPRINT_BYTES, tag: 2},
        ];
        for (const a of arrays) {
            const ab = new ArrayBuffer(a.len);
            tileExpandFillBytes((seed + 0x9e3779b1 * a.tag) >>> 0, ab, a.len);
            const u8 = new Uint8Array(ab);
            const base = rva(a.addr);
            for (let off = 0; off < a.len; off += CHUNK) {
                const len = Math.min(CHUNK, a.len - off);
                base.add(off).writeByteArray(u8.slice(off, off + len).buffer);
            }
        }
        // Inject the per-vector anchor at tiles[offset + 3].  Bounds check
        // is the orchestrator's responsibility — out-of-range offsets are
        // legitimate test inputs (the function early-returns if the byte
        // isn't a recognized anchor, before doing any other reads).
        if (offset + 3 >= 0 && offset + 3 < TILE_EXPAND_TILES_BYTES) {
            rva(ADDR.var_tile_tiles).add(offset + 3).writeU8(anchor);
        }

        // Call retail.  __cdecl(int) — pass tile_byte_offset.
        g_nf_retail_tile_expand(offset);

        // Read back all 5 globals in chunks of <=32 KB.  Copy each chunk
        // into a fresh Uint8Array so we don't ship a dangling view of
        // Frida's internal ArrayBuffer (see installStateHashSources for
        // the same rationale).
        function readChunked(addr, len) {
            const dst = new Uint8Array(len);
            const base = rva(addr);
            for (let off = 0; off < len; off += CHUNK) {
                const sz = Math.min(CHUNK, len - off);
                dst.set(new Uint8Array(base.add(off).readByteArray(sz)), off);
            }
            return Array.from(dst);
        }
        out = {
            tiles:               readChunked(ADDR.var_tile_tiles,          TILE_EXPAND_TILES_BYTES),
            path_scratch:        readChunked(ADDR.var_tile_path_scratch,   TILE_EXPAND_PATH_SCRATCH_BYTES),
            stage_scratch:       readChunked(ADDR.var_tile_stage_scratch,  TILE_EXPAND_STAGE_SCRATCH_BYTES),
            footprint_list:      readChunked(ADDR.var_tile_footprint_list, TILE_EXPAND_FOOTPRINT_LIST_BYTES),
            last_footprint_tile: rva(ADDR.var_tile_last_footprint).readS32(),
        };
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out.raced = ((tick_count_after !== tick_count_before) ||
                     in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0;
    } finally {
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// ─── generic per-realm turn-pass differ ──────────────────────────────────
//
// The turn-advance engine's per-realm passes (FUN_0044bd9f tax-approval,
// FUN_0044bf7a development-spread, …) are __stdcall void() functions that take
// NO arguments — they loop over the live realm count and mutate the shared save
// globals (g_per_army_768, g_player_strategic_352, g_misc_1360, g_overworld_map)
// plus a couple of scalar settings.  Rather than a bespoke RPC per pass, this
// one generic differ takes a plan describing the memory regions to snapshot /
// inject / read back and the function VA to invoke:
//
//   in_obj = {
//     fn_va: <retail VA of the pass, e.g. 0x44bd9f>,
//     regions: [ {va, len, inject:[bytes]|null, read:bool}, … ],
//   }
//
// It snapshots every region (so the engine sees no perturbation), writes the
// injected bytes, calls retail, reads back the regions flagged `read`, then
// restores the snapshots in a finally block.  Returns
//   { regions: { "<va>": [bytes], … }, raced: 0|1 }.
//
// Engine freeze REQUIRED (shared installSimSubStepsEngineFreeze): the passes
// read s_slot_0056d5dc and walk g_per_army_768, which the engine main thread
// also touches once a game is live; freezing the per-tick driver keeps our
// injected state stable across the call.
function runRetailRealmPass(in_obj) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (typeof in_obj.fn_va !== 'number' || !Array.isArray(in_obj.regions)) {
        return {error: 'in_obj must be {fn_va:number, regions:[…]}'};
    }
    const CHUNK = 32 * 1024;

    function writeChunked(base, u8, len) {
        for (let off = 0; off < len; off += CHUNK) {
            const sz = Math.min(CHUNK, len - off);
            base.add(off).writeByteArray(u8.slice(off, off + sz).buffer);
        }
    }
    function readChunked(base, len) {
        const dst = new Uint8Array(len);
        for (let off = 0; off < len; off += CHUNK) {
            const sz = Math.min(CHUNK, len - off);
            dst.set(new Uint8Array(base.add(off).readByteArray(sz)), off);
        }
        return dst;
    }

    g_diff_test_caller_tid = Process.getCurrentThreadId();
    const tc0 = g_tick_count;
    const it0 = g_engine_in_tick;
    const snaps = [];
    const out = {regions: {}};
    try {
        // Snapshot (COPY) + inject every region.
        for (const reg of in_obj.regions) {
            const base = rva(reg.va);
            snaps.push({base, len: reg.len, snap: readChunked(base, reg.len)});
            if (reg.inject) {
                writeChunked(base, Uint8Array.from(reg.inject), reg.len);
            }
        }
        // Invoke retail.  Defaults to __stdcall void() with no args (the
        // per-realm void() passes); pass in_obj.args (list of ints) + optional
        // in_obj.cc ('mscdecl' for __cdecl) for passes that take an argument,
        // e.g. FUN_0044b80b(int realm).
        const argvals  = Array.isArray(in_obj.args) ? in_obj.args : [];
        const argtypes = argvals.map(function () { return 'int'; });
        const cc       = in_obj.cc || 'stdcall';
        new NativeFunction(rva(in_obj.fn_va), 'void', argtypes, cc).apply(null, argvals);
        // Read back the requested regions BEFORE restoring.
        for (const reg of in_obj.regions) {
            if (reg.read) {
                out.regions[String(reg.va)] =
                    Array.from(readChunked(rva(reg.va), reg.len));
            }
        }
        out.raced = ((g_tick_count !== tc0) ||
                     it0 !== 0 || g_engine_in_tick !== 0) ? 1 : 0;
    } finally {
        // Restore snapshots in reverse so the engine sees no perturbation.
        for (let i = snaps.length - 1; i >= 0; i--) {
            writeChunked(snaps[i].base, snaps[i].snap, snaps[i].len);
        }
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// ─── delayed-action dispatcher (FUN_004472ce — engine_dispatch_delayed_actions) ─
//
// Drains the 400-slot pending-action table; dispatches each due record by
// calling a per-type handler from the 50-entry PTR_FUN_004d5c98 table.
// The real handlers touch dozens of unrelated globals — combat-action
// commits, MP broadcasts, etc. — none of which we have ported.  For the
// diff we therefore install a no-op stub at every entry of the dispatch
// table at test-plan start (and restore the original 200 bytes at end);
// the function's observable surface becomes just the queue/record/scalar
// globals.  This mirrors the approach for the dispatch handler whose
// "side effect" is purely the post-call state of records[active=0] +
// the decremented action_counter + the 10 dispatch-arg mirrors that hold
// the LAST dispatched record's args.
//
// Wire traffic per call:
//   in:  scalars (5 × i32) + disp_pre (10 × i32) + sparse record list +
//        current_tick (1 × i32)
//   pre-call writes: zero-fill records (22400 B), 4 i32 scalars +
//                    10 i32 mirrors + per-record fields
//   readback:        records (22400 B) + queue (400) + queue_count +
//                    hipri_stash (400) + hipri_count + 4 scalars +
//                    10 mirrors  (~23.7 KB total, chunked at <=32 KB)
//
// Engine freeze: REQUIRED.  The engine main thread's per-tick driver
// also dispatches FUN_004472ce — running our injected records concurrently
// would observably interleave with our RPC's call.  Re-use the shared
// installSimSubStepsEngineFreeze; orchestrator adds 'delayed_action' to
// its shared_freeze_targets set.

// Allocate the no-op handler that will be stamped into every dispatch
// table slot.  Stdcall + no args + void: matches retail's (*table[type])()
// shape.  Lazy-allocated on first install so the test-plan re-runs don't
// pile up duplicate callbacks.
function installDelayedActionStub() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_da_dispatch_table_backup !== null) {
        return {ok: true, already_installed: true};
    }
    // 1. Snapshot the 200-byte dispatch table.  PTR_FUN_004d5c98 sits in
    //    .rdata; we must Memory.protect it RWX before writing.  Make a
    //    pristine copy of the original bytes first so uninstall can flush
    //    them back.
    const tablePtr = rva(ADDR.var_da_dispatch_table);
    Memory.protect(tablePtr, DA_DISPATCH_TABLE_BYTES, 'rwx');
    g_da_dispatch_table_backup = new Uint8Array(DA_DISPATCH_TABLE_BYTES);
    g_da_dispatch_table_backup.set(
        new Uint8Array(tablePtr.readByteArray(DA_DISPATCH_TABLE_BYTES)));

    // 2. Snapshot the input/output buffers we'll overwrite each call.
    //    Records (22400 B), queue (400 B), hipri_stash (400 B), 5 scalars,
    //    10 dispatch-arg mirrors.  Restored at uninstall.
    const snap = function (addr, len) {
        const dst = new Uint8Array(len);
        dst.set(new Uint8Array(rva(addr).readByteArray(len)));
        return dst;
    };
    g_da_records_backup = snap(ADDR.var_da_records, DA_RECORDS_BYTES);
    g_da_queue_backup   = snap(ADDR.var_da_queue,   DA_QUEUE_BYTES);
    g_da_hipri_backup   = snap(ADDR.var_da_hipri_stash, DA_HIPRI_BYTES);
    g_da_scalar_backup = {
        master_switch:  rva(ADDR.var_da_master_switch ).readS32(),
        game_mode:      rva(ADDR.var_da_game_mode     ).readS32(),
        local_player:   rva(ADDR.var_da_local_player  ).readS32(),
        action_counter: rva(ADDR.var_da_action_counter).readS32(),
        current_idx:    rva(ADDR.var_da_current_idx   ).readS32(),
        queue_count:    rva(ADDR.var_da_queue_count   ).readS32(),
        hipri_count:    rva(ADDR.var_da_hipri_count   ).readS32(),
    };
    g_da_disp_mirror_backup = [
        rva(ADDR.var_da_disp_a0).readS32(),
        rva(ADDR.var_da_disp_a1).readS32(),
        rva(ADDR.var_da_disp_a2).readS32(),
        rva(ADDR.var_da_disp_a3).readS32(),
        rva(ADDR.var_da_disp_a4).readS32(),
        rva(ADDR.var_da_disp_a5).readS32(),
        rva(ADDR.var_da_disp_a6).readS32(),
        rva(ADDR.var_da_disp_a7).readS32(),
        rva(ADDR.var_da_disp_a8).readS32(),
        rva(ADDR.var_da_disp_a9).readS32(),
    ];

    // 3. Stamp the no-op handler into every dispatch-table slot.  We
    //    overwrite all 50 entries with the same NativeCallback address —
    //    cheaper than 50 Interceptor.replace calls, and the table is in
    //    .data-ish memory (we Memory.protect'd above), so plain writes
    //    suffice.  No Interceptor.flush() needed since we never modify
    //    code, only data.
    if (g_da_noop_handler === null) {
        g_da_noop_handler = new NativeCallback(
            function () { /* dispatch handler stubbed for diff */ },
            'void', [], 'stdcall');
    }
    const handlerPtr = g_da_noop_handler;
    // Write 50 × 4-byte pointer values.  Use writeU32 per slot — the
    // handler's address is a NativePointer; treat its integer value as
    // the 32-bit slot value (we're on a 32-bit target, so pointers fit).
    const handlerAddr = handlerPtr.toUInt32();
    const tableBuf = new Uint8Array(DA_DISPATCH_TABLE_BYTES);
    const view = new DataView(tableBuf.buffer);
    for (let i = 0; i < DA_DISPATCH_TABLE_LEN; i++) {
        view.setUint32(i * 4, handlerAddr, /*littleEndian*/ true);
    }
    tablePtr.writeByteArray(tableBuf.buffer);
    return {ok: true, already_installed: false,
            snapshot_bytes: DA_DISPATCH_TABLE_BYTES + DA_RECORDS_BYTES +
                            DA_QUEUE_BYTES + DA_HIPRI_BYTES +
                            7 * 4 + DA_DISP_MIRROR_COUNT * 4};
}

function uninstallDelayedActionStub() {
    if (g_base === null) {
        return {error: 'init not completed yet'};
    }
    if (g_da_dispatch_table_backup === null) {
        return {ok: true, restored: false};
    }
    // Chunked restore for the records buffer (22400 B > 16 KB but well
    // under the 32 KB threshold — single writeByteArray is fine).  Mirror
    // the tile_expand pattern just for consistency.
    const CHUNK = 32 * 1024;
    function restoreInChunks(addr, backup) {
        const total = backup.length;
        const base = rva(addr);
        for (let off = 0; off < total; off += CHUNK) {
            const len = Math.min(CHUNK, total - off);
            base.add(off).writeByteArray(backup.slice(off, off + len).buffer);
        }
    }
    // Restore the dispatch table.  The .rdata page is still RWX from the
    // install-side protect — Frida doesn't track our protect changes, so
    // we don't try to put it back to RO; harmless since we won't write
    // again.
    rva(ADDR.var_da_dispatch_table).writeByteArray(g_da_dispatch_table_backup.buffer);

    restoreInChunks(ADDR.var_da_records,     g_da_records_backup);
    restoreInChunks(ADDR.var_da_queue,       g_da_queue_backup);
    restoreInChunks(ADDR.var_da_hipri_stash, g_da_hipri_backup);

    const s = g_da_scalar_backup;
    rva(ADDR.var_da_master_switch ).writeS32(s.master_switch);
    rva(ADDR.var_da_game_mode     ).writeS32(s.game_mode);
    rva(ADDR.var_da_local_player  ).writeS32(s.local_player);
    rva(ADDR.var_da_action_counter).writeS32(s.action_counter);
    rva(ADDR.var_da_current_idx   ).writeS32(s.current_idx);
    rva(ADDR.var_da_queue_count   ).writeS32(s.queue_count);
    rva(ADDR.var_da_hipri_count   ).writeS32(s.hipri_count);

    const m = g_da_disp_mirror_backup;
    rva(ADDR.var_da_disp_a0).writeS32(m[0]);
    rva(ADDR.var_da_disp_a1).writeS32(m[1]);
    rva(ADDR.var_da_disp_a2).writeS32(m[2]);
    rva(ADDR.var_da_disp_a3).writeS32(m[3]);
    rva(ADDR.var_da_disp_a4).writeS32(m[4]);
    rva(ADDR.var_da_disp_a5).writeS32(m[5]);
    rva(ADDR.var_da_disp_a6).writeS32(m[6]);
    rva(ADDR.var_da_disp_a7).writeS32(m[7]);
    rva(ADDR.var_da_disp_a8).writeS32(m[8]);
    rva(ADDR.var_da_disp_a9).writeS32(m[9]);

    g_da_dispatch_table_backup = null;
    g_da_records_backup        = null;
    g_da_queue_backup          = null;
    g_da_hipri_backup          = null;
    g_da_scalar_backup         = null;
    g_da_disp_mirror_backup    = null;
    // Keep g_da_noop_handler alive (don't null) so re-installs can reuse
    // the same NativeCallback.  GC will reclaim it when the script unloads.
    return {ok: true, restored: true};
}

// Per-call: zero-fill the records region, apply the sparse record list,
// write the 5 scalars + 10 disp-arg mirrors, call FUN_004472ce(tick),
// read back everything that can have changed.
function runRetailDelayedAction(in_obj) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    if (g_da_dispatch_table_backup === null) {
        return {error: 'installDelayedActionStub not called'};
    }
    if (typeof in_obj.current_tick !== 'number') {
        return {error: 'in_obj.current_tick must be a number'};
    }

    g_diff_test_caller_tid = Process.getCurrentThreadId();
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;

    let out;
    try {
        // 1. Zero-fill the 22400-byte records region so prior-call state
        //    doesn't leak.  Single writeByteArray — 22400 < 32 KB.
        rva(ADDR.var_da_records).writeByteArray(
            new Uint8Array(DA_RECORDS_BYTES).buffer);

        // 2. Apply the sparse record list.  Each entry stamps a 0x38-byte
        //    record at records[idx].  We do per-field writes (rather than
        //    building a 0x38-byte buffer) because the records are usually
        //    sparse — a handful of active records per vector at most.
        const recs = in_obj.records || [];
        for (let r = 0; r < recs.length; r++) {
            const rec = recs[r];
            const base = rva(ADDR.var_da_records).add((rec.idx | 0) * DA_RECORD_BYTES);
            base.add(0x00).writeS32(rec.due_tick | 0);
            const args = rec.args || [];
            for (let a = 0; a < 10; a++) {
                base.add(0x04 + a * 4).writeS32(args[a] | 0);
            }
            // +0x2c..+0x35 padding stays zero from the prior wipe.
            base.add(0x36).writeU8((rec.active | 0) & 0xff);
            base.add(0x37).writeU8((rec.type   | 0) & 0xff);
        }

        // 3. Write the input scalars.  Includes queue_count and hipri_count
        //    even though retail clears them at entry — for master_switch=0
        //    vectors the function bails before the clear, and the pre-call
        //    values carry through to the readback.  Same rationale for the
        //    queue / hipri_stash byte arrays a few lines down.
        rva(ADDR.var_da_master_switch ).writeS32(in_obj.master_switch  | 0);
        rva(ADDR.var_da_game_mode     ).writeS32(in_obj.game_mode      | 0);
        rva(ADDR.var_da_local_player  ).writeS32(in_obj.local_player   | 0);
        rva(ADDR.var_da_action_counter).writeS32(in_obj.action_counter | 0);
        rva(ADDR.var_da_current_idx   ).writeS32(in_obj.current_idx    | 0);
        rva(ADDR.var_da_queue_count   ).writeS32(in_obj.queue_count    | 0);
        rva(ADDR.var_da_hipri_count   ).writeS32(in_obj.hipri_count    | 0);

        // Pre-seed queue + hipri_stash with the vector-supplied bytes.
        // For master_switch=1 cases retail clears these at entry, so the
        // values get clobbered immediately; for master_switch=0 they
        // survive and contribute to the diff.  Cheapest to always write
        // them (a single 400-byte writeByteArray each).
        const queueBuf = new Uint8Array(DA_QUEUE_BYTES);
        const hipriBuf = new Uint8Array(DA_HIPRI_BYTES);
        const qPre = in_obj.queue_pre || [];
        const hPre = in_obj.hipri_pre || [];
        const qView = new DataView(queueBuf.buffer);
        const hView = new DataView(hipriBuf.buffer);
        for (let i = 0; i < qPre.length && i * 4 + 4 <= DA_QUEUE_BYTES; i++) {
            qView.setInt32(i * 4, qPre[i] | 0, true);
        }
        for (let i = 0; i < hPre.length && i * 4 + 4 <= DA_HIPRI_BYTES; i++) {
            hView.setInt32(i * 4, hPre[i] | 0, true);
        }
        rva(ADDR.var_da_queue      ).writeByteArray(queueBuf.buffer);
        rva(ADDR.var_da_hipri_stash).writeByteArray(hipriBuf.buffer);

        // 4. Seed the 10 dispatch-arg mirrors.  These get overwritten if
        //    any record is dispatched; otherwise they carry through.
        const pre = in_obj.disp_pre || [0,0,0,0,0,0,0,0,0,0];
        rva(ADDR.var_da_disp_a0).writeS32(pre[0] | 0);
        rva(ADDR.var_da_disp_a1).writeS32(pre[1] | 0);
        rva(ADDR.var_da_disp_a2).writeS32(pre[2] | 0);
        rva(ADDR.var_da_disp_a3).writeS32(pre[3] | 0);
        rva(ADDR.var_da_disp_a4).writeS32(pre[4] | 0);
        rva(ADDR.var_da_disp_a5).writeS32(pre[5] | 0);
        rva(ADDR.var_da_disp_a6).writeS32(pre[6] | 0);
        rva(ADDR.var_da_disp_a7).writeS32(pre[7] | 0);
        rva(ADDR.var_da_disp_a8).writeS32(pre[8] | 0);
        rva(ADDR.var_da_disp_a9).writeS32(pre[9] | 0);

        // 5. Call retail.  __cdecl(int current_tick).
        g_nf_retail_delayed_action(in_obj.current_tick | 0);

        // 6. Read back records + queue + hipri + counts + scalars + mirrors.
        //    Single readByteArray each — all chunks ≤32 KB.  Copy into a
        //    fresh Uint8Array so we don't ship a dangling view onto the
        //    Frida ArrayBuffer.
        function readBytes(addr, len) {
            const dst = new Uint8Array(len);
            dst.set(new Uint8Array(rva(addr).readByteArray(len)));
            return Array.from(dst);
        }
        out = {
            records:        readBytes(ADDR.var_da_records,     DA_RECORDS_BYTES),
            queue:          readBytes(ADDR.var_da_queue,       DA_QUEUE_BYTES),
            hipri_stash:    readBytes(ADDR.var_da_hipri_stash, DA_HIPRI_BYTES),
            queue_count:    rva(ADDR.var_da_queue_count   ).readS32(),
            hipri_count:    rva(ADDR.var_da_hipri_count   ).readS32(),
            action_counter: rva(ADDR.var_da_action_counter).readS32(),
            current_idx:    rva(ADDR.var_da_current_idx   ).readS32(),
            disp: [
                rva(ADDR.var_da_disp_a0).readS32(),
                rva(ADDR.var_da_disp_a1).readS32(),
                rva(ADDR.var_da_disp_a2).readS32(),
                rva(ADDR.var_da_disp_a3).readS32(),
                rva(ADDR.var_da_disp_a4).readS32(),
                rva(ADDR.var_da_disp_a5).readS32(),
                rva(ADDR.var_da_disp_a6).readS32(),
                rva(ADDR.var_da_disp_a7).readS32(),
                rva(ADDR.var_da_disp_a8).readS32(),
                rva(ADDR.var_da_disp_a9).readS32(),
            ],
        };
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out.raced = ((tick_count_after !== tick_count_before) ||
                     in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0;
    } finally {
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// Sim sub-step rate gate diff.  Wraps FUN_004bb978, which internally calls
// FUN_0043de70 (engine_compute_scenario_hint).  The orchestrator runs BOTH
// our ports chained together host-side; the RPC drives ONE retail call that
// transitively exercises both retail functions.
//
// Snapshot/inject surface is larger than any prior diff target:
//   ~20 scalars (FUN_004bb978 + FUN_0043de70 inputs/outputs combined),
//   4 per-player strided arrays (7 slots × {active, has_data, disconnected,
//   ticks}), plus 2 single-byte table reads (screen_freeze, mp_skip — we
//   write the byte at the table index the function will read, vs. the full
//   256-byte table).  The 2400-byte rate-gate LUT is installed once per
//   test plan separately (installSimSubStepsRateLut) so this hot path stays
//   lean.
//
// Constraints baked into the orchestrator-side vector generator (NOT
// enforced here — agent trusts caller):
//   - autosave_start_ms = 0 → skip the 5000 ms autosave-expiry branch, so
//     FUN_0043eda0(0xf, 0) never fires.
//   - slow_frame_count ≤ 99 → after `++` retail stays ≤ 100, so the gate
//     `100 < value` is false and FUN_0043eda0(10, 0) never fires.
// With those, the whole call chain has zero retail-side side effects beyond
// the snapshot+restore surface — so the RPC restores cleanly.
function runRetailSimSubSteps(in_obj) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    g_diff_test_clock_queue.length = 0;
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;

    // Snapshot every scalar + every per-player array slot we'll touch.
    // The rate-gate LUT itself is NOT snapshotted here — that's handled
    // by installSimSubStepsRateLut at test-plan boundaries.
    const snap = {
        // FUN_004bb978 scalars
        last_step_ms:        rva(ADDR.var_sim_last_step_ms      ).readU32(),
        autosave_start_ms:   rva(ADDR.var_sim_autosave_start_ms ).readU32(),
        mp_active:           rva(ADDR.var_sim_mp_active         ).readS32(),
        tick_counter:        rva(ADDR.var_sim_tick_counter      ).readU32(),
        derived_hint:        rva(ADDR.var_sim_derived_hint      ).readS32(),
        phase:               rva(ADDR.var_sim_phase             ).readS32(),
        game_speed:          rva(ADDR.var_sim_game_speed        ).readS32(),
        pause_lock:          rva(ADDR.var_sim_pause_lock        ).readS32(),
        current_screen:     rva(ADDR.var_sim_current_screen     ).readU8 (),
        extended_pause:      rva(ADDR.var_sim_extended_pause    ).readS32(),
        mp_player_index:    rva(ADDR.var_sim_mp_player_index    ).readU8 (),
        // FUN_0043de70 scalars
        game_state_phase:    rva(ADDR.var_sim_game_state_phase  ).readS32(),
        local_player_idx:    rva(ADDR.var_sim_local_player_idx  ).readS32(),
        game_mode_speed:     rva(ADDR.var_sim_game_mode_speed   ).readU8 (),
        ally_a:              rva(ADDR.var_sim_ally_a            ).readS32(),
        ally_b:              rva(ADDR.var_sim_ally_b            ).readS32(),
        frame_count:         rva(ADDR.var_sim_frame_count       ).readS32(),
        max_stride:          rva(ADDR.var_sim_max_stride        ).readS32(),
        stride_sum:          rva(ADDR.var_sim_stride_sum        ).readS32(),
        avg_stride:          rva(ADDR.var_sim_avg_stride        ).readS32(),
        slow_frame_count:    rva(ADDR.var_sim_slow_frame_count  ).readS32(),
        base_delta:          rva(ADDR.var_sim_base_delta        ).readS32(),
        active_count:        rva(ADDR.var_sim_active_count      ).readS32(),
        // Indirect single-byte table reads at runtime indices.  Index
        // is in_obj.current_screen / in_obj.mp_player_index — both also
        // injected on each call.
        screen_freeze_byte:  rva(ADDR.var_sim_screen_freeze_table)
                                .add(in_obj.current_screen & 0xff).readU8(),
        mp_skip_byte:        rva(ADDR.var_sim_mp_skip_table)
                                .add(in_obj.mp_player_index & 0xff).readU8(),
        // Per-player arrays — snapshot all 7 slots' worth of each table
        // (slots 1..6 used, 0 unused but read for parity).
        player_active:       [], player_has_data:    [],
        player_disconnected: [], player_ticks:       [],
    };
    for (let i = 0; i < SIM_PLAYER_SLOT_COUNT; i++) {
        snap.player_active[i] = rva(ADDR.var_sim_player_active_base)
            .add(i * SIM_PLAYER_ACTIVE_STRIDE).readU8();
        snap.player_has_data[i] = rva(ADDR.var_sim_player_has_data_base)
            .add(i * SIM_PLAYER_ACTIVE_STRIDE).readU8();
        snap.player_disconnected[i] = rva(ADDR.var_sim_player_disconnected_base)
            .add(i * SIM_PLAYER_DISCONN_STRIDE).readU8();
        snap.player_ticks[i] = rva(ADDR.var_sim_player_ticks_base)
            .add(i * SIM_PLAYER_TICKS_STRIDE).readS32();
    }

    let out;
    try {
        // Inject scalars.
        rva(ADDR.var_sim_last_step_ms      ).writeU32(in_obj.last_step_ms       >>> 0);
        rva(ADDR.var_sim_autosave_start_ms ).writeU32(in_obj.autosave_start_ms  >>> 0);
        rva(ADDR.var_sim_mp_active         ).writeS32(in_obj.mp_active          |  0);
        rva(ADDR.var_sim_tick_counter      ).writeU32(in_obj.tick_counter       >>> 0);
        // derived_hint is a canary (clear so post-call readback is "did THIS
        // call write it").
        rva(ADDR.var_sim_derived_hint      ).writeS32(0);
        rva(ADDR.var_sim_phase             ).writeS32(in_obj.phase              |  0);
        rva(ADDR.var_sim_game_speed        ).writeS32(in_obj.game_speed         |  0);
        rva(ADDR.var_sim_pause_lock        ).writeS32(in_obj.pause_lock         |  0);
        rva(ADDR.var_sim_current_screen    ).writeU8 (in_obj.current_screen     & 0xff);
        rva(ADDR.var_sim_extended_pause    ).writeS32(in_obj.extended_pause     |  0);
        rva(ADDR.var_sim_mp_player_index   ).writeU8 (in_obj.mp_player_index    & 0xff);
        rva(ADDR.var_sim_game_state_phase  ).writeS32(in_obj.game_state_phase   |  0);
        rva(ADDR.var_sim_local_player_idx  ).writeS32(in_obj.local_player_idx   |  0);
        rva(ADDR.var_sim_game_mode_speed   ).writeU8 (in_obj.game_mode_speed    & 0xff);
        rva(ADDR.var_sim_ally_a            ).writeS32(in_obj.ally_a             |  0);
        rva(ADDR.var_sim_ally_b            ).writeS32(in_obj.ally_b             |  0);
        rva(ADDR.var_sim_frame_count       ).writeS32(in_obj.frame_count        |  0);
        rva(ADDR.var_sim_max_stride        ).writeS32(in_obj.max_stride         |  0);
        rva(ADDR.var_sim_stride_sum        ).writeS32(in_obj.stride_sum         |  0);
        rva(ADDR.var_sim_avg_stride        ).writeS32(in_obj.avg_stride         |  0);
        rva(ADDR.var_sim_slow_frame_count  ).writeS32(in_obj.slow_frame_count   |  0);
        rva(ADDR.var_sim_base_delta        ).writeS32(in_obj.base_delta         |  0);
        rva(ADDR.var_sim_active_count      ).writeS32(in_obj.active_count       |  0);
        // Inject the two single-byte table reads at the same indices the
        // function will use — we control both index and byte.
        rva(ADDR.var_sim_screen_freeze_table)
            .add(in_obj.current_screen & 0xff).writeU8(in_obj.screen_freeze_byte & 0xff);
        rva(ADDR.var_sim_mp_skip_table)
            .add(in_obj.mp_player_index & 0xff).writeU8(in_obj.mp_skip_byte & 0xff);
        // Per-player arrays.  Slot 0 is unused but injected for snapshot
        // parity (slot-0 active is always 0 in retail; vectors always set
        // it to 0 too).
        for (let i = 0; i < SIM_PLAYER_SLOT_COUNT; i++) {
            rva(ADDR.var_sim_player_active_base).add(i * SIM_PLAYER_ACTIVE_STRIDE)
                .writeU8(in_obj.player_active[i] & 0xff);
            rva(ADDR.var_sim_player_has_data_base).add(i * SIM_PLAYER_ACTIVE_STRIDE)
                .writeU8(in_obj.player_has_data[i] & 0xff);
            rva(ADDR.var_sim_player_disconnected_base).add(i * SIM_PLAYER_DISCONN_STRIDE)
                .writeU8(in_obj.player_disconnected[i] & 0xff);
            rva(ADDR.var_sim_player_ticks_base).add(i * SIM_PLAYER_TICKS_STRIDE)
                .writeS32(in_obj.player_ticks[i] | 0);
        }

        // Scripted timeGetTime — one value drained by FUN_004bb978's lone
        // timeGetTime call at function head.
        g_diff_test_clock_queue.push(in_obj.now_ms >>> 0);

        // Invoke retail.  Return is the sub-step count (0/1/2).
        const sub_steps = g_nf_retail_sim_sub_steps();

        // Read every output: return value + every dirty global.
        out = {
            sub_steps:               sub_steps | 0,
            new_last_step_ms:        rva(ADDR.var_sim_last_step_ms     ).readU32(),
            new_autosave_start_ms:   rva(ADDR.var_sim_autosave_start_ms).readU32(),
            new_phase:               rva(ADDR.var_sim_phase            ).readS32(),
            derived_hint:            rva(ADDR.var_sim_derived_hint     ).readS32(),
            new_frame_count:         rva(ADDR.var_sim_frame_count      ).readS32(),
            new_max_stride:          rva(ADDR.var_sim_max_stride       ).readS32(),
            new_stride_sum:          rva(ADDR.var_sim_stride_sum       ).readS32(),
            new_avg_stride:          rva(ADDR.var_sim_avg_stride       ).readS32(),
            new_slow_frame_count:    rva(ADDR.var_sim_slow_frame_count ).readS32(),
            new_active_count:        rva(ADDR.var_sim_active_count     ).readS32(),
        };
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out.raced = ((tick_count_after !== tick_count_before) ||
                     in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0;
    } finally {
        // Restore every snapshotted field.
        rva(ADDR.var_sim_last_step_ms      ).writeU32(snap.last_step_ms);
        rva(ADDR.var_sim_autosave_start_ms ).writeU32(snap.autosave_start_ms);
        rva(ADDR.var_sim_mp_active         ).writeS32(snap.mp_active);
        rva(ADDR.var_sim_tick_counter      ).writeU32(snap.tick_counter);
        rva(ADDR.var_sim_derived_hint      ).writeS32(snap.derived_hint);
        rva(ADDR.var_sim_phase             ).writeS32(snap.phase);
        rva(ADDR.var_sim_game_speed        ).writeS32(snap.game_speed);
        rva(ADDR.var_sim_pause_lock        ).writeS32(snap.pause_lock);
        rva(ADDR.var_sim_current_screen    ).writeU8 (snap.current_screen);
        rva(ADDR.var_sim_extended_pause    ).writeS32(snap.extended_pause);
        rva(ADDR.var_sim_mp_player_index   ).writeU8 (snap.mp_player_index);
        rva(ADDR.var_sim_game_state_phase  ).writeS32(snap.game_state_phase);
        rva(ADDR.var_sim_local_player_idx  ).writeS32(snap.local_player_idx);
        rva(ADDR.var_sim_game_mode_speed   ).writeU8 (snap.game_mode_speed);
        rva(ADDR.var_sim_ally_a            ).writeS32(snap.ally_a);
        rva(ADDR.var_sim_ally_b            ).writeS32(snap.ally_b);
        rva(ADDR.var_sim_frame_count       ).writeS32(snap.frame_count);
        rva(ADDR.var_sim_max_stride        ).writeS32(snap.max_stride);
        rva(ADDR.var_sim_stride_sum        ).writeS32(snap.stride_sum);
        rva(ADDR.var_sim_avg_stride        ).writeS32(snap.avg_stride);
        rva(ADDR.var_sim_slow_frame_count  ).writeS32(snap.slow_frame_count);
        rva(ADDR.var_sim_base_delta        ).writeS32(snap.base_delta);
        rva(ADDR.var_sim_active_count      ).writeS32(snap.active_count);
        rva(ADDR.var_sim_screen_freeze_table)
            .add(in_obj.current_screen & 0xff).writeU8(snap.screen_freeze_byte);
        rva(ADDR.var_sim_mp_skip_table)
            .add(in_obj.mp_player_index & 0xff).writeU8(snap.mp_skip_byte);
        for (let i = 0; i < SIM_PLAYER_SLOT_COUNT; i++) {
            rva(ADDR.var_sim_player_active_base).add(i * SIM_PLAYER_ACTIVE_STRIDE)
                .writeU8(snap.player_active[i]);
            rva(ADDR.var_sim_player_has_data_base).add(i * SIM_PLAYER_ACTIVE_STRIDE)
                .writeU8(snap.player_has_data[i]);
            rva(ADDR.var_sim_player_disconnected_base).add(i * SIM_PLAYER_DISCONN_STRIDE)
                .writeU8(snap.player_disconnected[i]);
            rva(ADDR.var_sim_player_ticks_base).add(i * SIM_PLAYER_TICKS_STRIDE)
                .writeS32(snap.player_ticks[i]);
        }
        g_diff_test_clock_queue.length = 0;
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// Engine-thread race notes — there is NO on-target guard.
//
// First attempt installed an Interceptor.attach onEnter on each retail
// function that spun while caller_tid was set.  This deadlocked: Frida's
// V8 isolate is single-threaded; the engine main thread's onEnter
// callback grabs the V8 lock to run its JS body; the spin holds the lock
// forever; the V8 RPC starves and never clears caller_tid.
//
// Second attempt called SuspendThread on the engine main thread.  This
// deadlocked too: Frida's V8 heap shares the process heap with the
// engine; suspending the engine while it held an internal heap lock
// wedged the next V8 allocation.
//
// Pragmatic approach: skip the guard, accept a small per-call race
// window, detect it on the orchestrator side, retry.  For FPS the tell
// is "the function we called did NOT see our scripted clock" — measurable
// from the returned delta_ms.  See tools/diff_test.py's race-retry path.

// RNG diff: 8 retail globals (2 state + 6 masked output mirrors).  The
// function takes no args + reads/writes only these globals, so the snapshot
// + restore is exhaustive — there is no other observable state.
function runRetailRngStep(state_a, state_b) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    // Snapshot — race-safe because installDiffTestGuards holds the engine
    // at fn_engine_rng_step.onEnter for the duration of this RPC.
    const snap = {
        state_a:     rva(ADDR.var_rng_state_a).readU32(),
        state_b:     rva(ADDR.var_rng_state_b).readU32(),
        rng_15bit_a: rva(ADDR.var_rng_15bit_a).readU32(),
        rng_7bit_a:  rva(ADDR.var_rng_7bit_a ).readU32(),
        rng_3bit_a:  rva(ADDR.var_rng_3bit_a ).readU32(),
        rng_15bit_b: rva(ADDR.var_rng_15bit_b).readU32(),
        rng_7bit_b:  rva(ADDR.var_rng_7bit_b ).readU32(),
        rng_3bit_b:  rva(ADDR.var_rng_3bit_b ).readU32(),
    };
    let out;
    try {
        rva(ADDR.var_rng_state_a).writeU32(state_a >>> 0);
        rva(ADDR.var_rng_state_b).writeU32(state_b >>> 0);
        g_nf_retail_rng_step();
        out = {
            new_state_a:  rva(ADDR.var_rng_state_a ).readU32(),
            new_state_b:  rva(ADDR.var_rng_state_b ).readU32(),
            rng_15bit_a:  rva(ADDR.var_rng_15bit_a ).readU32(),
            rng_7bit_a:   rva(ADDR.var_rng_7bit_a  ).readU32(),
            rng_3bit_a:   rva(ADDR.var_rng_3bit_a  ).readU32(),
            rng_15bit_b:  rva(ADDR.var_rng_15bit_b ).readU32(),
            rng_7bit_b:   rva(ADDR.var_rng_7bit_b  ).readU32(),
            rng_3bit_b:   rva(ADDR.var_rng_3bit_b  ).readU32(),
        };
    } finally {
        rva(ADDR.var_rng_state_a).writeU32(snap.state_a);
        rva(ADDR.var_rng_state_b).writeU32(snap.state_b);
        rva(ADDR.var_rng_15bit_a).writeU32(snap.rng_15bit_a);
        rva(ADDR.var_rng_7bit_a ).writeU32(snap.rng_7bit_a);
        rva(ADDR.var_rng_3bit_a ).writeU32(snap.rng_3bit_a);
        rva(ADDR.var_rng_15bit_b).writeU32(snap.rng_15bit_b);
        rva(ADDR.var_rng_7bit_b ).writeU32(snap.rng_7bit_b);
        rva(ADDR.var_rng_3bit_b ).writeU32(snap.rng_3bit_b);
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// FPS diff: 6 retail globals + 2 scripted timeGetTime returns.  FUN_004b27bf
// calls timeGetTime() exactly twice on a rollover frame (once for the delta,
// once for the new window-start basis), and exactly once on a non-rollover
// frame.  We push BOTH values to the clock queue and let the function drain
// whichever it needs; any leftover is cleared at the end so the next call
// starts fresh.
//
// `now_ms` and `rollover_now_ms` are caller-supplied — for a non-rollover
// test, set them equal; for a rollover test, set rollover_now_ms slightly
// later (matches retail's "two real wall-clock reads back-to-back, ms
// apart" shape that engine_compute_fps's pure port preserves).
function runRetailComputeFps(in_now_ms, in_rollover_now_ms,
                             in_last_render_ms, in_window_start_ms,
                             in_frames_this_second, in_current_fps,
                             in_cumulative_fps, in_cumulative_seconds) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    // Always start with an empty clock queue so a prior test's leftover
    // can't bleed in.
    g_diff_test_clock_queue.length = 0;
    // Snapshot — race-safe because installDiffTestGuards holds the engine
    // at fn_engine_compute_fps.onEnter for the duration of this RPC.
    const snap = {
        frames:       rva(ADDR.var_fps_frames      ).readU32(),
        last_render:  rva(ADDR.var_fps_last_render ).readU32(),
        window_start: rva(ADDR.var_fps_window_start).readU32(),
        current_fps:  rva(ADDR.var_fps_current     ).readU32(),
        cum_fps:      rva(ADDR.var_fps_cum_sum     ).readU32(),
        cum_secs:     rva(ADDR.var_fps_cum_secs    ).readU32(),
    };
    let out;
    try {
        rva(ADDR.var_fps_frames      ).writeU32(in_frames_this_second >>> 0);
        rva(ADDR.var_fps_last_render ).writeU32(in_last_render_ms     >>> 0);
        rva(ADDR.var_fps_window_start).writeU32(in_window_start_ms    >>> 0);
        rva(ADDR.var_fps_current     ).writeU32(in_current_fps        >>> 0);
        rva(ADDR.var_fps_cum_sum     ).writeU32(in_cumulative_fps     >>> 0);
        rva(ADDR.var_fps_cum_secs    ).writeU32(in_cumulative_seconds >>> 0);
        // Script clock returns.  Push BOTH values: the rollover branch
        // needs both reads; non-rollover only consumes the first.  Any
        // leftover is cleared in the outer finally.
        g_diff_test_clock_queue.push(in_now_ms          >>> 0);
        g_diff_test_clock_queue.push(in_rollover_now_ms >>> 0);
        const delta_ms = g_nf_retail_compute_fps() | 0;
        out = {
            delta_ms:                    delta_ms,
            new_frames_this_second:      rva(ADDR.var_fps_frames      ).readU32(),
            new_last_render_ms:          rva(ADDR.var_fps_last_render ).readU32(),
            new_second_window_start_ms:  rva(ADDR.var_fps_window_start).readU32(),
            new_current_fps:             rva(ADDR.var_fps_current     ).readU32(),
            new_cumulative_fps:          rva(ADDR.var_fps_cum_sum     ).readU32(),
            new_cumulative_seconds:      rva(ADDR.var_fps_cum_secs    ).readU32(),
        };
    } finally {
        rva(ADDR.var_fps_frames      ).writeU32(snap.frames);
        rva(ADDR.var_fps_last_render ).writeU32(snap.last_render);
        rva(ADDR.var_fps_window_start).writeU32(snap.window_start);
        rva(ADDR.var_fps_current     ).writeU32(snap.current_fps);
        rva(ADDR.var_fps_cum_sum     ).writeU32(snap.cum_fps);
        rva(ADDR.var_fps_cum_secs    ).writeU32(snap.cum_secs);
        g_diff_test_clock_queue.length = 0;
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// Anim-cycle diff: 2 retail globals (the counter mirror + the 80 ms phase
// tick flag).  FUN_00451f1a takes no args + reads/writes only these two
// globals — snapshot + restore is exhaustive.
//
// Race shape differs from RNG/FPS: the engine calls FUN_00451f1a once per
// engine_tick (inside FUN_004b99c0's pre-tick block, gated on the same
// phase tick flag we're injecting), so any tick that fires during our RPC
// window will exercise the function with our injected globals.  Detection
// piggybacks on g_engine_in_tick + g_tick_count:
//   - g_engine_in_tick != 0 at any sample point → engine is mid-tick →
//     might have called or will call FUN_00451f1a with our values
//   - g_tick_count advanced during the RPC → a fresh engine_tick fired,
//     necessarily calling FUN_00451f1a once
// The orchestrator retries on raced=true.
function runRetailAnimCycle(counter, tick_80ms_fired) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;
    // counter is a signed int in retail (the `< 0` defensive clamp is the
    // tell).  Read signed so vectors that inject negative values round-trip
    // cleanly through JSON without u32-vs-i32 sign confusion on readback.
    const snap = {
        counter:         rva(ADDR.var_anim_cycle_counter).readS32(),
        phase_tick_80ms: rva(ADDR.var_phase_tick_80ms   ).readU32(),
    };
    let out;
    try {
        rva(ADDR.var_anim_cycle_counter).writeS32(counter | 0);
        rva(ADDR.var_phase_tick_80ms   ).writeU32(tick_80ms_fired >>> 0);
        g_nf_retail_anim_cycle();
        const new_counter = rva(ADDR.var_anim_cycle_counter).readS32();
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out = {
            new_counter: new_counter,
            raced: ((tick_count_after !== tick_count_before) ||
                    in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0,
        };
    } finally {
        rva(ADDR.var_anim_cycle_counter).writeS32(snap.counter | 0);
        rva(ADDR.var_phase_tick_80ms   ).writeU32(snap.phase_tick_80ms);
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// Frame-delta diff: 1 retail global (last_frame_ms) + 1 scripted timeGetTime
// return.  FUN_004b20ed calls timeGetTime() exactly once, computes
// delta = now - last_frame_ms, and either suppresses (delta < 30) or
// advances the baseline.  Returns the delta (int).
//
// Race shape matches anim_cycle (called once per engine_tick in the
// pre-tick block, right after FUN_00451f1a).  Same structural detector:
// raced=1 if g_tick_count advanced or g_engine_in_tick was non-zero at
// any sample point during the RPC.
function runRetailFrameDelta(in_now_ms, in_last_frame_ms) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    // Start with an empty queue so a prior test's leftover can't bleed in.
    g_diff_test_clock_queue.length = 0;
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;
    const snap = {
        last_frame_ms: rva(ADDR.var_frame_delta_last).readU32(),
    };
    let out;
    try {
        rva(ADDR.var_frame_delta_last).writeU32(in_last_frame_ms >>> 0);
        g_diff_test_clock_queue.push(in_now_ms >>> 0);
        const delta_ms = g_nf_retail_frame_delta() | 0;
        const new_last_frame_ms = rva(ADDR.var_frame_delta_last).readU32();
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out = {
            delta_ms:          delta_ms,
            new_last_frame_ms: new_last_frame_ms,
            raced: ((tick_count_after !== tick_count_before) ||
                    in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0,
        };
    } finally {
        rva(ADDR.var_frame_delta_last).writeU32(snap.last_frame_ms);
        g_diff_test_clock_queue.length = 0;
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// Phase-ticks diff: 10 retail globals (last_base_tick_ms + 9 counters) +
// 9 flag globals + 1 scripted timeGetTime return.  FUN_004bbc80 calls
// timeGetTime() exactly once and writes 9 flags (8 cleared at entry, the
// 1040 ms flag is sticky → not cleared at entry).  We clear ALL 9 going
// in so the post-call readback reflects only this call's writes, which is
// the shape the pure port outputs.
//
// Race shape matches anim_cycle / frame_delta (called once per engine_tick
// in the pre-tick block of FUN_004b99c0).  No structural detector here —
// the orchestrator uses a function-specific check on new_last_base_tick_ms
// (mirrors frame_delta's pattern).
function runRetailPhaseTicks(in_now_ms,
                             in_last_base_tick_ms,
                             in_base_tick_counter,
                             in_counter_2,  in_counter_4,
                             in_counter_8,  in_counter_13,
                             in_counter_16, in_counter_24,
                             in_counter_32) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    // Start with an empty queue so a prior test's leftover can't bleed in.
    g_diff_test_clock_queue.length = 0;
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;
    const snap = {
        last_base_tick_ms: rva(ADDR.var_phase_last_base_tick_ms).readU32(),
        base_tick_counter: rva(ADDR.var_phase_base_tick_counter).readS32(),
        counter_2:         rva(ADDR.var_phase_counter_2 ).readS32(),
        counter_4:         rva(ADDR.var_phase_counter_4 ).readS32(),
        counter_8:         rva(ADDR.var_phase_counter_8 ).readS32(),
        counter_13:        rva(ADDR.var_phase_counter_13).readS32(),
        counter_16:        rva(ADDR.var_phase_counter_16).readS32(),
        counter_24:        rva(ADDR.var_phase_counter_24).readS32(),
        counter_32:        rva(ADDR.var_phase_counter_32).readS32(),
        tick_20ms:         rva(ADDR.var_phase_tick_20ms  ).readU32(),
        tick_80ms:         rva(ADDR.var_phase_tick_80ms  ).readU32(),
        tick_160ms:        rva(ADDR.var_phase_tick_160ms ).readU32(),
        tick_320ms:        rva(ADDR.var_phase_tick_320ms ).readU32(),
        tick_640ms:        rva(ADDR.var_phase_tick_640ms ).readU32(),
        tick_1040ms:       rva(ADDR.var_phase_tick_1040ms).readU32(),
        tick_1280ms:       rva(ADDR.var_phase_tick_1280ms).readU32(),
        tick_1920ms:       rva(ADDR.var_phase_tick_1920ms).readU32(),
        tick_2560ms:       rva(ADDR.var_phase_tick_2560ms).readU32(),
    };
    let out;
    try {
        // Inputs — 10 state globals (counters are signed in retail's
        // `int N < counter` comparisons; readS32/writeS32 round-trips
        // negative test values cleanly through JSON).
        rva(ADDR.var_phase_last_base_tick_ms).writeU32(in_last_base_tick_ms >>> 0);
        rva(ADDR.var_phase_base_tick_counter).writeS32(in_base_tick_counter | 0);
        rva(ADDR.var_phase_counter_2 ).writeS32(in_counter_2  | 0);
        rva(ADDR.var_phase_counter_4 ).writeS32(in_counter_4  | 0);
        rva(ADDR.var_phase_counter_8 ).writeS32(in_counter_8  | 0);
        rva(ADDR.var_phase_counter_13).writeS32(in_counter_13 | 0);
        rva(ADDR.var_phase_counter_16).writeS32(in_counter_16 | 0);
        rva(ADDR.var_phase_counter_24).writeS32(in_counter_24 | 0);
        rva(ADDR.var_phase_counter_32).writeS32(in_counter_32 | 0);
        // Clear all 9 flags (retail clears 8 at entry; the 1040 ms flag
        // is sticky and only set on rollover — pre-clearing matches the
        // pure port's "all flags start at 0, set on rollover" shape).
        rva(ADDR.var_phase_tick_20ms  ).writeU32(0);
        rva(ADDR.var_phase_tick_80ms  ).writeU32(0);
        rva(ADDR.var_phase_tick_160ms ).writeU32(0);
        rva(ADDR.var_phase_tick_320ms ).writeU32(0);
        rva(ADDR.var_phase_tick_640ms ).writeU32(0);
        rva(ADDR.var_phase_tick_1040ms).writeU32(0);
        rva(ADDR.var_phase_tick_1280ms).writeU32(0);
        rva(ADDR.var_phase_tick_1920ms).writeU32(0);
        rva(ADDR.var_phase_tick_2560ms).writeU32(0);
        // Scripted clock for the single timeGetTime() call.
        g_diff_test_clock_queue.push(in_now_ms >>> 0);
        g_nf_retail_phase_ticks();
        // Readback ordering is load-bearing: 18 fields take many V8/JS
        // round-trips to read, and the engine main thread can squeeze a
        // FUN_004bbc80 call in BETWEEN any two reads.  We want a single
        // canary that detects "engine raced at any point during readback".
        // `last_base_tick_ms` is that canary — read FIRST and LAST.  If the
        // start and end values disagree, or if either disagrees with the
        // expected post-call value, retail's writebacks were clobbered
        // mid-readback and the orchestrator will retry.
        //
        // Order chosen: flags first (smallest contiguous group), counters
        // next, then last_base.  Putting last_base last makes the simple
        // "end-value matches expected" check cover the entire readback.
        const last_base_start = rva(ADDR.var_phase_last_base_tick_ms).readU32();
        out = {
            tick_20ms:             rva(ADDR.var_phase_tick_20ms  ).readU32(),
            tick_80ms:             rva(ADDR.var_phase_tick_80ms  ).readU32(),
            tick_160ms:            rva(ADDR.var_phase_tick_160ms ).readU32(),
            tick_320ms:            rva(ADDR.var_phase_tick_320ms ).readU32(),
            tick_640ms:            rva(ADDR.var_phase_tick_640ms ).readU32(),
            tick_1040ms:           rva(ADDR.var_phase_tick_1040ms).readU32(),
            tick_1280ms:           rva(ADDR.var_phase_tick_1280ms).readU32(),
            tick_1920ms:           rva(ADDR.var_phase_tick_1920ms).readU32(),
            tick_2560ms:           rva(ADDR.var_phase_tick_2560ms).readU32(),
            new_base_tick_counter: rva(ADDR.var_phase_base_tick_counter).readS32(),
            new_counter_2:         rva(ADDR.var_phase_counter_2 ).readS32(),
            new_counter_4:         rva(ADDR.var_phase_counter_4 ).readS32(),
            new_counter_8:         rva(ADDR.var_phase_counter_8 ).readS32(),
            new_counter_13:        rva(ADDR.var_phase_counter_13).readS32(),
            new_counter_16:        rva(ADDR.var_phase_counter_16).readS32(),
            new_counter_24:        rva(ADDR.var_phase_counter_24).readS32(),
            new_counter_32:        rva(ADDR.var_phase_counter_32).readS32(),
            // Canary read LAST.  Orchestrator's race detector checks this.
            new_last_base_tick_ms: rva(ADDR.var_phase_last_base_tick_ms).readU32(),
        };
        // Also surface the start canary so the orchestrator can sanity-
        // check the readback was atomic (start == end).  Equal values
        // mean no race during readback; unequal means the engine fired
        // FUN_004bbc80 mid-readback.
        out.last_base_tick_ms_start = last_base_start;
        // Structural race indicator — same shape as anim_cycle.  Engine
        // is mid-tick during our RPC ⇒ might have run FUN_004bbc80
        // concurrently (multi-core, no kernel-level serialisation).
        // Used by the orchestrator's engine-clear race-pass classifier
        // to distinguish "harness flakiness" from a real port bug.
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out.raced = ((tick_count_after !== tick_count_before) ||
                     in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0;
    } finally {
        rva(ADDR.var_phase_last_base_tick_ms).writeU32(snap.last_base_tick_ms);
        rva(ADDR.var_phase_base_tick_counter).writeS32(snap.base_tick_counter);
        rva(ADDR.var_phase_counter_2 ).writeS32(snap.counter_2);
        rva(ADDR.var_phase_counter_4 ).writeS32(snap.counter_4);
        rva(ADDR.var_phase_counter_8 ).writeS32(snap.counter_8);
        rva(ADDR.var_phase_counter_13).writeS32(snap.counter_13);
        rva(ADDR.var_phase_counter_16).writeS32(snap.counter_16);
        rva(ADDR.var_phase_counter_24).writeS32(snap.counter_24);
        rva(ADDR.var_phase_counter_32).writeS32(snap.counter_32);
        rva(ADDR.var_phase_tick_20ms  ).writeU32(snap.tick_20ms);
        rva(ADDR.var_phase_tick_80ms  ).writeU32(snap.tick_80ms);
        rva(ADDR.var_phase_tick_160ms ).writeU32(snap.tick_160ms);
        rva(ADDR.var_phase_tick_320ms ).writeU32(snap.tick_320ms);
        rva(ADDR.var_phase_tick_640ms ).writeU32(snap.tick_640ms);
        rva(ADDR.var_phase_tick_1040ms).writeU32(snap.tick_1040ms);
        rva(ADDR.var_phase_tick_1280ms).writeU32(snap.tick_1280ms);
        rva(ADDR.var_phase_tick_1920ms).writeU32(snap.tick_1920ms);
        rva(ADDR.var_phase_tick_2560ms).writeU32(snap.tick_2560ms);
        g_diff_test_clock_queue.length = 0;
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// Input-state diff: ~30 retail globals + 1 scripted timeGetTime + 1
// scripted GetCursorPos.  FUN_004b191e is called with NO args (all I/O via
// BSS globals).  See `var_input_*` ADDR entries above for the per-field
// width rationale (byte-stride adjacency in the 0x4eabXX cluster forces
// u8 reads/writes; cached_cursor_{x,y} are i16; everything else s32/u32).
//
// Race shape: function is called once per engine_tick in the render-phase
// head.  We use the structural raced flag (engine_in_tick at any sample
// point) for retry decisions.  Function-specific race-free check:
// raw_cursor_{x,y} after the call MUST equal the scripted cursor we
// injected via GetCursorPos — if it doesn't, the engine raced and ran its
// own FUN_004b191e (which calls real GetCursorPos and overwrites the
// raw_cursor globals).
function runRetailInputState(in_obj) {
    if (!g_diff_test_enabled) {
        return {error: 'diff_test not enabled at init'};
    }
    if (!resolveRetailFunctions()) {
        return {error: 'NativeFunction resolution failed'};
    }
    g_diff_test_caller_tid = Process.getCurrentThreadId();
    g_diff_test_clock_queue.length = 0;
    g_diff_test_cursor_queue.length = 0;
    const tick_count_before = g_tick_count;
    const in_tick_before    = g_engine_in_tick;
    // Snapshot every global we touch (inputs and outputs alike) so the
    // engine's own ticking state isn't disturbed across the RPC.
    const snap = {
        cursor_cache_enabled: rva(ADDR.var_input_cursor_cache_enabled).readS32(),
        pending_dblclk:       rva(ADDR.var_input_pending_dblclk      ).readU8 (),
        button_state:         rva(ADDR.var_input_button_state        ).readU8 (),
        viewport_x0:          rva(ADDR.var_input_viewport_x0         ).readS32(),
        viewport_y0:          rva(ADDR.var_input_viewport_y0         ).readS32(),
        viewport_x1:          rva(ADDR.var_input_viewport_x1         ).readS32(),
        viewport_y1:          rva(ADDR.var_input_viewport_y1         ).readS32(),
        viewport_game_w:      rva(ADDR.var_input_viewport_game_w     ).readS32(),
        viewport_game_h:      rva(ADDR.var_input_viewport_game_h     ).readS32(),
        cached_cursor_x:      rva(ADDR.var_input_cached_cursor_x     ).readS16(),
        cached_cursor_y:      rva(ADDR.var_input_cached_cursor_y     ).readS16(),
        raw_cursor_x:         rva(ADDR.var_input_raw_cursor_x        ).readS32(),
        raw_cursor_y:         rva(ADDR.var_input_raw_cursor_y        ).readS32(),
        prev_game_x_snap:     rva(ADDR.var_input_prev_game_x_snap    ).readS32(),
        prev_game_y_snap:     rva(ADDR.var_input_prev_game_y_snap    ).readS32(),
        prev_lmb_snap:        rva(ADDR.var_input_prev_lmb_snap       ).readU8 (),
        prev_rmb_snap:        rva(ADDR.var_input_prev_rmb_snap       ).readU8 (),
        game_x:               rva(ADDR.var_input_game_x              ).readS32(),
        game_y:               rva(ADDR.var_input_game_y              ).readS32(),
        mouse_event:          rva(ADDR.var_input_mouse_event         ).readS32(),
        button_changed:       rva(ADDR.var_input_button_changed      ).readS32(),
        lmb_newly_pressed:    rva(ADDR.var_input_lmb_newly_pressed   ).readS32(),
        lmb_newly_released:   rva(ADDR.var_input_lmb_newly_released  ).readS32(),
        rmb_newly_pressed:    rva(ADDR.var_input_rmb_newly_pressed   ).readS32(),
        rmb_newly_released:   rva(ADDR.var_input_rmb_newly_released  ).readS32(),
        lmb_click_fired:      rva(ADDR.var_input_lmb_click_fired     ).readS32(),
        rmb_click_fired:      rva(ADDR.var_input_rmb_click_fired     ).readS32(),
        lmb_dblclk:           rva(ADDR.var_input_lmb_dblclk          ).readU8 (),
        rmb_dblclk:           rva(ADDR.var_input_rmb_dblclk          ).readS32(),
        lmb:                  rva(ADDR.var_input_lmb                 ).readS32(),
        rmb:                  rva(ADDR.var_input_rmb                 ).readS32(),
        lmb_timer_armed:      rva(ADDR.var_input_lmb_timer_armed     ).readS32(),
        rmb_timer_armed:      rva(ADDR.var_input_rmb_timer_armed     ).readS32(),
        click_timer_ms:       rva(ADDR.var_input_click_timer_ms      ).readU32(),
        motion_last_ms:       rva(ADDR.var_input_motion_last_ms      ).readU32(),
        motion_idle_ms:       rva(ADDR.var_input_motion_idle_ms      ).readU32(),
        click_eligible:       rva(ADDR.var_input_click_eligible      ).readU8 (),
        last_click_x:         rva(ADDR.var_input_last_click_x        ).readS32(),
        last_click_y:         rva(ADDR.var_input_last_click_y        ).readS32(),
    };
    let out;
    try {
        // Inject inputs.  Order: mode + WndProc mirrors first, viewport,
        // cached cursor, then state mirrors (prev_game/lmb/rmb, timers,
        // click_eligible, last_click).  Per-frame OUT flags are cleared
        // at retail's entry so no need to pre-clear here, but we DO need
        // to clear the snap-mirrors (prev_lmb_snap, prev_rmb_snap,
        // lmb_dblclk, rmb_dblclk) because retail writes them
        // unconditionally — without a deterministic baseline the
        // post-call readback can't distinguish "retail wrote 0" from
        // "snapshot already had 0".  Same shape as phase_ticks's
        // pre-clear of the 8 non-sticky flags.
        rva(ADDR.var_input_cursor_cache_enabled).writeS32(in_obj.cursor_cache_enabled | 0);
        rva(ADDR.var_input_pending_dblclk      ).writeU8 (in_obj.pending_dblclk & 0xff);
        rva(ADDR.var_input_button_state        ).writeU8 (in_obj.button_state   & 0xff);
        rva(ADDR.var_input_viewport_x0         ).writeS32(in_obj.viewport_x0 | 0);
        rva(ADDR.var_input_viewport_y0         ).writeS32(in_obj.viewport_y0 | 0);
        rva(ADDR.var_input_viewport_x1         ).writeS32(in_obj.viewport_x1 | 0);
        rva(ADDR.var_input_viewport_y1         ).writeS32(in_obj.viewport_y1 | 0);
        rva(ADDR.var_input_viewport_game_w     ).writeS32(in_obj.viewport_game_w | 0);
        rva(ADDR.var_input_viewport_game_h     ).writeS32(in_obj.viewport_game_h | 0);
        rva(ADDR.var_input_cached_cursor_x     ).writeS16(in_obj.cached_cursor_x | 0);
        rva(ADDR.var_input_cached_cursor_y     ).writeS16(in_obj.cached_cursor_y | 0);
        // prev_game_{x,y} feed DAT_004e65fc/4 (which retail snapshots to
        // DAT_004ea198/0 then overwrites with the new game-coord cursor).
        rva(ADDR.var_input_game_x              ).writeS32(in_obj.prev_game_x | 0);
        rva(ADDR.var_input_game_y              ).writeS32(in_obj.prev_game_y | 0);
        // prev_lmb/rmb feed DAT_004e65cc / DAT_004e8780 — same shape.
        rva(ADDR.var_input_lmb                 ).writeS32(in_obj.prev_lmb | 0);
        rva(ADDR.var_input_rmb                 ).writeS32(in_obj.prev_rmb | 0);
        rva(ADDR.var_input_lmb_timer_armed     ).writeS32(in_obj.lmb_timer_armed | 0);
        rva(ADDR.var_input_rmb_timer_armed     ).writeS32(in_obj.rmb_timer_armed | 0);
        rva(ADDR.var_input_click_timer_ms      ).writeU32(in_obj.click_timer_ms >>> 0);
        rva(ADDR.var_input_motion_last_ms      ).writeU32(in_obj.motion_last_ms >>> 0);
        rva(ADDR.var_input_click_eligible      ).writeU8 (in_obj.click_eligible & 0xff);
        rva(ADDR.var_input_last_click_x        ).writeS32(in_obj.prev_last_click_x | 0);
        rva(ADDR.var_input_last_click_y        ).writeS32(in_obj.prev_last_click_y | 0);
        // Clear snap mirrors so the post-call readback is "did THIS call
        // write them" (retail writes them unconditionally; clearing keeps
        // the semantics clean for restore + diff).
        rva(ADDR.var_input_prev_game_x_snap    ).writeS32(0);
        rva(ADDR.var_input_prev_game_y_snap    ).writeS32(0);
        rva(ADDR.var_input_prev_lmb_snap       ).writeU8 (0);
        rva(ADDR.var_input_prev_rmb_snap       ).writeU8 (0);
        rva(ADDR.var_input_lmb_dblclk          ).writeU8 (0);
        rva(ADDR.var_input_rmb_dblclk          ).writeS32(0);
        rva(ADDR.var_input_motion_idle_ms      ).writeU32(0);

        // Scripted clock + cursor — both consumed inside FUN_004b191e.
        g_diff_test_clock_queue.push(in_obj.now_ms >>> 0);
        g_diff_test_cursor_queue.push({x: in_obj.cursor_screen_x | 0,
                                       y: in_obj.cursor_screen_y | 0});
        g_nf_retail_input_state();

        // Readback canary: raw_cursor_{x,y} must equal our injected cursor
        // (retail writes them unconditionally from local_24).  Read FIRST
        // as the start canary; the orchestrator's _input_state_race_free
        // check confirms they match the expected post-call value.
        const raw_x_start = rva(ADDR.var_input_raw_cursor_x).readS32();
        const raw_y_start = rva(ADDR.var_input_raw_cursor_y).readS32();
        out = {
            raw_cursor_x:         raw_x_start,
            raw_cursor_y:         raw_y_start,
            new_cached_cursor_x:  rva(ADDR.var_input_cached_cursor_x   ).readS16(),
            new_cached_cursor_y:  rva(ADDR.var_input_cached_cursor_y   ).readS16(),
            prev_game_x_snapshot: rva(ADDR.var_input_prev_game_x_snap  ).readS32(),
            prev_game_y_snapshot: rva(ADDR.var_input_prev_game_y_snap  ).readS32(),
            prev_lmb_snapshot:    rva(ADDR.var_input_prev_lmb_snap     ).readU8 (),
            prev_rmb_snapshot:    rva(ADDR.var_input_prev_rmb_snap     ).readU8 (),
            game_x:               rva(ADDR.var_input_game_x            ).readS32(),
            game_y:               rva(ADDR.var_input_game_y            ).readS32(),
            mouse_event:          rva(ADDR.var_input_mouse_event       ).readS32(),
            button_changed:       rva(ADDR.var_input_button_changed    ).readS32(),
            lmb_newly_pressed:    rva(ADDR.var_input_lmb_newly_pressed ).readS32(),
            lmb_newly_released:   rva(ADDR.var_input_lmb_newly_released).readS32(),
            rmb_newly_pressed:    rva(ADDR.var_input_rmb_newly_pressed ).readS32(),
            rmb_newly_released:   rva(ADDR.var_input_rmb_newly_released).readS32(),
            lmb_click_fired:      rva(ADDR.var_input_lmb_click_fired   ).readS32(),
            rmb_click_fired:      rva(ADDR.var_input_rmb_click_fired   ).readS32(),
            lmb_dblclk:           rva(ADDR.var_input_lmb_dblclk        ).readU8 (),
            rmb_dblclk:           rva(ADDR.var_input_rmb_dblclk        ).readS32(),
            new_lmb:              rva(ADDR.var_input_lmb               ).readS32(),
            new_rmb:              rva(ADDR.var_input_rmb               ).readS32(),
            new_lmb_timer_armed:  rva(ADDR.var_input_lmb_timer_armed   ).readS32(),
            new_rmb_timer_armed:  rva(ADDR.var_input_rmb_timer_armed   ).readS32(),
            new_click_timer_ms:   rva(ADDR.var_input_click_timer_ms    ).readU32(),
            new_motion_last_ms:   rva(ADDR.var_input_motion_last_ms    ).readU32(),
            motion_idle_ms:       rva(ADDR.var_input_motion_idle_ms    ).readU32(),
            last_click_x:         rva(ADDR.var_input_last_click_x      ).readS32(),
            last_click_y:         rva(ADDR.var_input_last_click_y      ).readS32(),
            new_click_eligible:   rva(ADDR.var_input_click_eligible    ).readU8 (),
            // End canary — re-read raw_cursor to detect race-during-readback.
            raw_cursor_x_end:     rva(ADDR.var_input_raw_cursor_x      ).readS32(),
            raw_cursor_y_end:     rva(ADDR.var_input_raw_cursor_y      ).readS32(),
        };
        const tick_count_after = g_tick_count;
        const in_tick_after    = g_engine_in_tick;
        out.raced = ((tick_count_after !== tick_count_before) ||
                     in_tick_before !== 0 || in_tick_after !== 0) ? 1 : 0;
    } finally {
        // Restore every snapshotted field — engine sees a no-op.
        rva(ADDR.var_input_cursor_cache_enabled).writeS32(snap.cursor_cache_enabled);
        rva(ADDR.var_input_pending_dblclk      ).writeU8 (snap.pending_dblclk);
        rva(ADDR.var_input_button_state        ).writeU8 (snap.button_state);
        rva(ADDR.var_input_viewport_x0         ).writeS32(snap.viewport_x0);
        rva(ADDR.var_input_viewport_y0         ).writeS32(snap.viewport_y0);
        rva(ADDR.var_input_viewport_x1         ).writeS32(snap.viewport_x1);
        rva(ADDR.var_input_viewport_y1         ).writeS32(snap.viewport_y1);
        rva(ADDR.var_input_viewport_game_w     ).writeS32(snap.viewport_game_w);
        rva(ADDR.var_input_viewport_game_h     ).writeS32(snap.viewport_game_h);
        rva(ADDR.var_input_cached_cursor_x     ).writeS16(snap.cached_cursor_x);
        rva(ADDR.var_input_cached_cursor_y     ).writeS16(snap.cached_cursor_y);
        rva(ADDR.var_input_raw_cursor_x        ).writeS32(snap.raw_cursor_x);
        rva(ADDR.var_input_raw_cursor_y        ).writeS32(snap.raw_cursor_y);
        rva(ADDR.var_input_prev_game_x_snap    ).writeS32(snap.prev_game_x_snap);
        rva(ADDR.var_input_prev_game_y_snap    ).writeS32(snap.prev_game_y_snap);
        rva(ADDR.var_input_prev_lmb_snap       ).writeU8 (snap.prev_lmb_snap);
        rva(ADDR.var_input_prev_rmb_snap       ).writeU8 (snap.prev_rmb_snap);
        rva(ADDR.var_input_game_x              ).writeS32(snap.game_x);
        rva(ADDR.var_input_game_y              ).writeS32(snap.game_y);
        rva(ADDR.var_input_mouse_event         ).writeS32(snap.mouse_event);
        rva(ADDR.var_input_button_changed      ).writeS32(snap.button_changed);
        rva(ADDR.var_input_lmb_newly_pressed   ).writeS32(snap.lmb_newly_pressed);
        rva(ADDR.var_input_lmb_newly_released  ).writeS32(snap.lmb_newly_released);
        rva(ADDR.var_input_rmb_newly_pressed   ).writeS32(snap.rmb_newly_pressed);
        rva(ADDR.var_input_rmb_newly_released  ).writeS32(snap.rmb_newly_released);
        rva(ADDR.var_input_lmb_click_fired     ).writeS32(snap.lmb_click_fired);
        rva(ADDR.var_input_rmb_click_fired     ).writeS32(snap.rmb_click_fired);
        rva(ADDR.var_input_lmb_dblclk          ).writeU8 (snap.lmb_dblclk);
        rva(ADDR.var_input_rmb_dblclk          ).writeS32(snap.rmb_dblclk);
        rva(ADDR.var_input_lmb                 ).writeS32(snap.lmb);
        rva(ADDR.var_input_rmb                 ).writeS32(snap.rmb);
        rva(ADDR.var_input_lmb_timer_armed     ).writeS32(snap.lmb_timer_armed);
        rva(ADDR.var_input_rmb_timer_armed     ).writeS32(snap.rmb_timer_armed);
        rva(ADDR.var_input_click_timer_ms      ).writeU32(snap.click_timer_ms);
        rva(ADDR.var_input_motion_last_ms      ).writeU32(snap.motion_last_ms);
        rva(ADDR.var_input_motion_idle_ms      ).writeU32(snap.motion_idle_ms);
        rva(ADDR.var_input_click_eligible      ).writeU8 (snap.click_eligible);
        rva(ADDR.var_input_last_click_x        ).writeS32(snap.last_click_x);
        rva(ADDR.var_input_last_click_y        ).writeS32(snap.last_click_y);
        g_diff_test_clock_queue.length = 0;
        g_diff_test_cursor_queue.length = 0;
        g_diff_test_caller_tid = -1;
    }
    return out;
}

// ─── Trace Studio v3 draw-step capture (retail) ─────────────────────────
//
// A CModule mirror of src/gfx_drawcap.c. KEY CONSTRAINT (found by isolation
// test): in this Frida build the CModule's OWN .data/.bss globals are NOT
// writable — writing a CModule global faults instantly — but read/write through
// a JS-passed pointer works. So this CModule has ZERO mutable globals: ALL
// state (scalars in dc_st[], + the plane/delta buffers) lives in JS-allocated
// memory passed as pointer-slot symbols. The engine globals are passed as
// symbols (their absolute addresses): reading `g_canvas_base` reads the pointer
// stored at DAT_004ea1a8 (the locked back buffer); `g_canvas_stride/height` and
// `g_glyph_cursor_x/y` are the i32 globals. dc_begin/draw/flush accumulate
// per-draw deltas; the JS reads dc_base/dc_hdrs/dc_offs/dc_vals and builds the
// OLDR payload. base + Σ(deltas) == the final plane EXACTLY (each delta is
// shadow→canvas), so a write that bypasses a hooked dispatch only folds into
// the adjacent draw — never a wrong pixel; the Python ingest re-verifies this.
const DRAWCAP_NPX  = 640 * 480;
const DRAWCAP_MAXW = 900000;            // cap on accumulated write entries/frame
const DRAWCAP_MAXH = 16384;             // cap on draws/frame
const DRAWCAP_CMODULE_SRC = `
#include <gum/guminterceptor.h>
#include <glib.h>

extern unsigned char * g_canvas_base;    /* *(DAT_004ea1a8) — back-buffer base */
extern int g_canvas_stride;              /* DAT_004eb274 (== width, no padding) */
extern int g_canvas_height;              /* DAT_004eac4c                        */
extern int g_glyph_cursor_x;             /* DAT_00591524                        */
extern int g_glyph_cursor_y;             /* DAT_00591528                        */
extern int g_sprite_dir_idx;             /* DAT_005c9288 — map struct/overlay sprite-dir index */
extern int g_cell_value;                 /* DAT_00567954 — terrain cell value   */

/* ALL mutable state lives in JS-allocated memory passed as pointer-slot symbols.
 * The CModule has ZERO writable globals: in this Frida build the CModule's own
 * .data/.bss is NOT writable (writing a CModule global faults), but read/write
 * through a JS-passed pointer works (proven by isolation test). dc_st is a JS
 * int array (see DC_S_* indices); the buffers are JS Memory.alloc'd. Each symbol
 * is the address of a pointer-slot, so 'extern T * x' reads the slot → buffer. */
extern int           * dc_st;            /* scalar state (DC_S_* slots)          */
extern unsigned char * dc_base_buf;      /* DRAWCAP_NPX  */
extern unsigned char * dc_shadow_buf;    /* DRAWCAP_NPX  */
extern unsigned int  * dc_offs_buf;      /* DRAWCAP_MAXW */
extern unsigned char * dc_vals_buf;      /* DRAWCAP_MAXW */
extern unsigned int  * dc_hdrs_buf;      /* DRAWCAP_MAXH * 6 (op,m0,m1,m2,start,count) */

#define DC_W 640
#define DC_H 480
#define DC_NPX (DC_W * DC_H)
#define DC_MAXW 900000
#define DC_MAXH 16384

/* dc_st[] indices (kept in sync with the JS DC_S_* constants) */
#define S_ARMED    0
#define S_PENDING  1
#define S_HAVEBASE 2
#define S_W        3
#define S_H        4
#define S_NHDR     5
#define S_NWRITE   6
#define S_OVERFLOW 7
#define S_OP       8
#define S_M0       9
#define S_M1      10
#define S_M2      11
#define S_RA_LO   12   /* FUN_00408438 lo bound — map-structure leaf gate */
#define S_RA_HI   13   /* FUN_00408438 hi bound                            */

void dc_begin (void) {
  dc_st[S_ARMED] = 1; dc_st[S_HAVEBASE] = 0; dc_st[S_PENDING] = 0;
  dc_st[S_NHDR] = 0; dc_st[S_NWRITE] = 0; dc_st[S_OVERFLOW] = 0;
}

static void dc_finalize (unsigned char * cv) {
  int n, i; unsigned int start, count;
  if (!dc_st[S_PENDING]) return;
  dc_st[S_PENDING] = 0;
  n = dc_st[S_W] * dc_st[S_H];
  start = (unsigned int) dc_st[S_NWRITE];
  for (i = 0; i < n; i++) {
    if (dc_shadow_buf[i] != cv[i]) {
      if (dc_st[S_NWRITE] >= DC_MAXW) { dc_st[S_OVERFLOW] = 1; break; }
      dc_offs_buf[dc_st[S_NWRITE]] = (unsigned int) i;
      dc_vals_buf[dc_st[S_NWRITE]] = cv[i];
      dc_st[S_NWRITE]++;
      dc_shadow_buf[i] = cv[i];
    }
  }
  count = (unsigned int) dc_st[S_NWRITE] - start;
  if (count && dc_st[S_NHDR] < DC_MAXH) {
    unsigned int * h = &dc_hdrs_buf[dc_st[S_NHDR] * 6];
    h[0] = (unsigned int) dc_st[S_OP]; h[1] = (unsigned int) dc_st[S_M0];
    h[2] = (unsigned int) dc_st[S_M1]; h[3] = (unsigned int) dc_st[S_M2];
    h[4] = start; h[5] = count;
    dc_st[S_NHDR]++;
  }
}

static void dc_draw (unsigned int op, unsigned int m0, unsigned int m1, unsigned int m2) {
  unsigned char * cv;
  int i;
  if (!dc_st[S_ARMED]) return;
  cv = g_canvas_base;
  if (cv == 0) return;
  if (g_canvas_stride != DC_W || g_canvas_height != DC_H) return;  /* only 640x480 tight */
  if (dc_st[S_PENDING]) dc_finalize(cv);
  if (!dc_st[S_HAVEBASE]) {
    dc_st[S_W] = DC_W; dc_st[S_H] = DC_H;
    for (i = 0; i < DC_NPX; i++) { dc_base_buf[i] = cv[i]; dc_shadow_buf[i] = cv[i]; }
    dc_st[S_HAVEBASE] = 1;
  }
  dc_st[S_OP] = (int) op; dc_st[S_M0] = (int) m0;
  dc_st[S_M1] = (int) m1; dc_st[S_M2] = (int) m2; dc_st[S_PENDING] = 1;
}

/* Finalize the last pending draw against a CALLER-SUPPLIED canvas pointer. The
 * engine's render pass has already unlocked the back buffer by engine_tick.onLeave,
 * so the DAT_004ea1a8 pointer is stale there — the caller passes captureFrameOnce's
 * freshly-packed final plane (valid Frida memory, tight 640x480) instead. */
void dc_flush_ptr (unsigned char * cv, int w, int h) {
  if (!dc_st[S_ARMED] || !dc_st[S_PENDING]) { dc_st[S_PENDING] = 0; return; }
  if (cv == 0 || w != dc_st[S_W] || h != dc_st[S_H]) { dc_st[S_PENDING] = 0; return; }
  dc_finalize(cv);
}

void dc_end (void) { dc_st[S_ARMED] = 0; dc_st[S_PENDING] = 0; }

/* onEnter for the glyph dispatcher FUN_00402a14(font, char_table, char_code). */
void dc_on_glyph (GumInvocationContext * ic) {
  unsigned int cc;
  if (!dc_st[S_ARMED]) return;
  cc = GPOINTER_TO_UINT(gum_invocation_context_get_nth_argument(ic, 2));
  dc_draw(1u /*GLYPH*/, cc & 0xffu,
          (unsigned int) g_glyph_cursor_x, (unsigned int) g_glyph_cursor_y);
}

/* onEnter for the generic sprite dispatchers FUN_00404442 (canvas-clip) and
 * FUN_00404547 (caller-clip): (blob, sprite_index, x, y[, l,t,r,b]). Mirrors the
 * port's gfx_dispatch_sprite / _clipped drawcap_draw — the HUD/UI sprites. Meta
 * comes from the ARGS (the dest globals aren't staged until inside the fn). */
void dc_on_dispatch_sprite (GumInvocationContext * ic) {
  if (!dc_st[S_ARMED]) return;
  dc_draw(0u /*SPRITE*/,
          GPOINTER_TO_UINT(gum_invocation_context_get_nth_argument(ic, 1)),
          GPOINTER_TO_UINT(gum_invocation_context_get_nth_argument(ic, 2)),
          GPOINTER_TO_UINT(gum_invocation_context_get_nth_argument(ic, 3)));
}

/* onEnter for the leaf sprite blitters (main 4b43b1/446d/44d2, overlay RLE
 * 4b4537/4814/48b8) ONLY when called from the map-structure drawer FUN_00408438.
 * That fn inlines the dispatch and calls a leaf DIRECTLY, so the return address
 * lands inside its [lo,hi) window; HUD/panel/generic callers (gfx_panel,
 * gfx_sprite_render, FUN_00404442) reach the same leaves via other frames and
 * are gated out — exactly as the port FOLDS those (no drawcap_draw there).
 * Mirrors the port's engine_map_render main + overlay drawcap_draw. Meta = the
 * sprite-dir index + cursor FUN_00408438 staged just before the blit. */
void dc_on_map_sprite (GumInvocationContext * ic) {
  unsigned int ra;
  if (!dc_st[S_ARMED]) return;
  ra = (unsigned int) (gsize) gum_invocation_context_get_return_address(ic);
  if (ra < (unsigned int) dc_st[S_RA_LO] || ra >= (unsigned int) dc_st[S_RA_HI]) return;
  dc_draw(0u /*SPRITE*/, (unsigned int) g_sprite_dir_idx,
          (unsigned int) g_glyph_cursor_x, (unsigned int) g_glyph_cursor_y);
}

/* onEnter for the terrain per-cell drawers FUN_004063c1 (drawer_a) and
 * FUN_0042a7f1 (empty_cell) — map-exclusive, one draw per cell. drawer_b
 * (FUN_00406673) is NOT hooked so its writes FOLD into the cell's draw, matching
 * the port's do_cell (drawer_a + drawer_b == one draw). Meta = cell value +
 * cursor (the row iterator stages both before the call). */
void dc_on_terrain_cell (GumInvocationContext * ic) {
  if (!dc_st[S_ARMED]) return;
  (void) ic;
  dc_draw(0u /*SPRITE*/, (unsigned int) g_cell_value,
          (unsigned int) g_glyph_cursor_x, (unsigned int) g_glyph_cursor_y);
}
`;

// Neuter the strat-map mouse edge-scroll dispatcher FUN_00432221 (returns
// undefined4: 1 = scrolled this tick, 0 = did not). Replacing it with a constant
// 0 keeps DAT_005651b4/8 (the cell-scroll col/row) at the loaded-save value, so a
// host cursor parked at a screen edge can't scroll the captured map. See
// g_suppress_edge_scroll.
function installEdgeScrollSuppress() {
    if (g_edge_scroll_cb) return;
    g_edge_scroll_cb = new NativeCallback(function () { return 0; }, 'uint32', []);
    Interceptor.replace(rva(0x00432221), g_edge_scroll_cb);
    log('edge-scroll: FUN_00432221 neutered (capture determinism)');
}

function installDrawCaptureHooks() {
    if (g_drawcap_cm) return;
    // JS-owned buffers (committed) handed to the CModule via pointer-holding
    // slots (the canonical CModule data-symbol pattern).
    g_dc_state  = Memory.alloc(16 * 4);    // zero-filled by Frida
    g_dc_base   = Memory.alloc(DRAWCAP_NPX);
    g_dc_shadow = Memory.alloc(DRAWCAP_NPX);
    g_dc_offs   = Memory.alloc(DRAWCAP_MAXW * 4);
    g_dc_vals   = Memory.alloc(DRAWCAP_MAXW);
    g_dc_hdrs   = Memory.alloc(DRAWCAP_MAXH * 24);
    const slot = (p) => { const s = Memory.alloc(Process.pointerSize); s.writePointer(p); return s; };
    g_drawcap_cm = new CModule(DRAWCAP_CMODULE_SRC, {
        g_canvas_base:    rva(0x004ea1a8),
        g_canvas_stride:  rva(0x004eb274),
        g_canvas_height:  rva(0x004eac4c),
        g_glyph_cursor_x: rva(0x00591524),
        g_glyph_cursor_y: rva(0x00591528),
        g_sprite_dir_idx: rva(0x005c9288),
        g_cell_value:     rva(0x00567954),
        dc_st:         slot(g_dc_state),
        dc_base_buf:   slot(g_dc_base),
        dc_shadow_buf: slot(g_dc_shadow),
        dc_offs_buf:   slot(g_dc_offs),
        dc_vals_buf:   slot(g_dc_vals),
        dc_hdrs_buf:   slot(g_dc_hdrs),
    });
    g_dc_begin     = new NativeFunction(g_drawcap_cm.dc_begin,     'void', []);
    g_dc_flush_ptr = new NativeFunction(g_drawcap_cm.dc_flush_ptr, 'void', ['pointer', 'int', 'int']);
    g_dc_end       = new NativeFunction(g_drawcap_cm.dc_end,       'void', []);
    // The map-structure leaf gate window = [FUN_00408438, +size). A leaf blit
    // whose return address lands here was called directly by the map drawer
    // (which inlines the dispatch), distinguishing it from HUD/panel callers.
    const F408438 = rva(0x00408438);
    g_dc_state.add(DC_S_RA_LO * 4).writeU32(F408438.toUInt32());
    g_dc_state.add(DC_S_RA_HI * 4).writeU32(F408438.add(2072).toUInt32());  // FUN_00408438 = 2072 B
    // Symmetric draw-attribution hooks, one Interceptor.attach per dispatch
    // mirroring a port gfx_drawcap_draw site:
    //   glyph         FUN_00402a14                     (gfx_dispatch.c glyph)
    //   UI sprite     FUN_00404442 / FUN_00404547      (gfx_dispatch_sprite[_clipped])
    //   map struct    4b43b1/446d/44d2 + 4b4537/4814/48b8, gated to FUN_00408438
    //                                                  (engine_map_render main + overlay)
    //   terrain cell  FUN_004063c1 / FUN_0042a7f1      (engine_map_terrain do_cell)
    Interceptor.attach(rva(0x00402a14), { onEnter: g_drawcap_cm.dc_on_glyph });
    [0x00404442, 0x00404547].forEach(
        (va) => Interceptor.attach(rva(va), { onEnter: g_drawcap_cm.dc_on_dispatch_sprite }));
    [0x004b43b1, 0x004b446d, 0x004b44d2, 0x004b4537, 0x004b4814, 0x004b48b8].forEach(
        (va) => Interceptor.attach(rva(va), { onEnter: g_drawcap_cm.dc_on_map_sprite }));
    [0x004063c1, 0x0042a7f1].forEach(
        (va) => Interceptor.attach(rva(va), { onEnter: g_drawcap_cm.dc_on_terrain_cell }));
    g_drawcap_active = true;
    log('drawcap: CModule installed — glyph + UI-sprite + map-struct + terrain hooks');
}

// Read the CModule's accumulated draws → OLDR payload → send (frida_capture
// writes frame_<idx>.draws). No-op if the frame recorded no base (no draws).
function drawcapSend(frameIdx) {
    if (!g_drawcap_cm || !g_dc_state) return;
    const st = (i) => g_dc_state.add(i * 4).readInt();   // scalars live in dc_st[]
    if (st(DC_S_HAVEBASE) === 0) return;
    const w = st(DC_S_W), h = st(DC_S_H);
    if (w !== 640 || h !== 480) { err('drawcap.send', 'bad dims ' + w + 'x' + h); return; }
    const nhdr = Math.min(Math.max(st(DC_S_NHDR), 0), DRAWCAP_MAXH);
    const nwrite = Math.min(Math.max(st(DC_S_NWRITE), 0), DRAWCAP_MAXW);
    const total = 20 + w * h + nhdr * 20 + nwrite * 4 + nwrite;
    const buf = new Uint8Array(total);
    const dv = new DataView(buf.buffer);
    let o = 0;
    dv.setUint32(o, 0x52444C4F, true); o += 4;   // 'OLDR'
    dv.setUint32(o, w, true); o += 4;
    dv.setUint32(o, h, true); o += 4;
    dv.setUint32(o, frameIdx >>> 0, true); o += 4;
    dv.setUint32(o, nhdr, true); o += 4;
    buf.set(new Uint8Array(g_dc_base.readByteArray(w * h)), o); o += w * h;
    const hdrs = new Uint32Array(g_dc_hdrs.readByteArray(nhdr * 24));
    const offs = nwrite ? new Uint32Array(g_dc_offs.readByteArray(nwrite * 4)) : new Uint32Array(0);
    const vals = nwrite ? new Uint8Array(g_dc_vals.readByteArray(nwrite)) : new Uint8Array(0);
    for (let k = 0; k < nhdr; k++) {
        const b = k * 6;
        dv.setUint32(o, hdrs[b],     true); o += 4;  // op
        dv.setUint32(o, hdrs[b + 1], true); o += 4;  // m0
        dv.setUint32(o, hdrs[b + 2], true); o += 4;  // m1
        dv.setUint32(o, hdrs[b + 3], true); o += 4;  // m2
        const start = hdrs[b + 4], count = hdrs[b + 5];
        dv.setUint32(o, count, true); o += 4;
        for (let i = 0; i < count; i++) { dv.setUint32(o, offs[start + i], true); o += 4; }
        for (let i = 0; i < count; i++) { buf[o++] = vals[start + i]; }
    }
    if (st(DC_S_OVERFLOW)) err('drawcap', 'write buffer overflow @ frame ' + frameIdx);
    send({kind: 'draws', frame_idx: frameIdx, n_draws: nhdr}, buf.buffer);
}

// ─── init RPC ──────────────────────────────────────────────────────────

rpc.exports = {
    init: function (opts) {
        opts = opts || {};
        g_target_module_name = String(opts.module || MODULE_NAME);
        g_hide_window        = !!opts.hide_window;
        g_silent_audio_enabled = !!opts.silent_audio;
        g_force_windowed     = !!opts.force_windowed;
        g_keep_retail_palette = !!opts.keep_retail_palette;
        g_turbo_enabled      = !!opts.turbo;
        g_gdi_seat_enabled   = !!opts.gdi_seat;
        g_agent_present_enabled = !!opts.agent_present;
        g_force_active       = !!opts.force_active;
        const apEvery        = (opts.agent_present_every_ticks | 0);
        g_agent_present_every_ticks = apEvery > 0 ? apEvery : 100;
        g_show_window_after_ms = (opts.show_window_after_ms | 0);
        const swW = (opts.show_window_client_w | 0);
        const swH = (opts.show_window_client_h | 0);
        if (swW > 0) g_show_window_client_w = swW;
        if (swH > 0) g_show_window_client_h = swH;
        // Turbo step in virtual-ms per per-tick-driver iteration. The
        // default (17 ms) yields ~58 ticks/s perceived time, close to
        // 60 Hz; raise it to fast-forward.  0 disables turbo v2 even if
        // opts.turbo is set, falling back to turbo v1 (Sleep no-op only).
        const stepOpt = (opts.turbo_step_ms | 0);
        g_turbo_step_ms = stepOpt > 0 ? stepOpt : 17;

        g_frame_capture_enabled     = !!opts.frame_capture;
        g_frame_capture_max         = (opts.frame_capture_max | 0);
        g_frame_capture_warmup_secs = (opts.frame_capture_warmup_secs | 0);
        g_frame_capture_count       = 0;
        g_capture_draws             = !!opts.capture_draws;
        g_capture_force_redraw      = !!opts.capture_force_redraw;
        g_suppress_edge_scroll      = !!opts.suppress_edge_scroll;
        g_save_sandbox = (typeof opts.save_sandbox === 'string'
                          && opts.save_sandbox) ? opts.save_sandbox : null;

        g_dump_globals = Array.isArray(opts.dump_globals)
            ? opts.dump_globals.map(function (s) {
                  return {va: (s.va >>> 0), size: (s.size | 0),
                          label: String(s.label || '')};
              })
            : [];
        g_globals_dumped = false;

        g_dump_regions = Array.isArray(opts.dump_regions)
            ? opts.dump_regions.map(function (s) {
                  return {va: (s.va >>> 0), size: (s.size | 0),
                          label: String(s.label || ''), deref: !!s.deref};
              })
            : [];
        g_regions_dumped = false;

        g_poke_globals = Array.isArray(opts.poke_globals)
            ? opts.poke_globals.map(function (s) {
                  return {va: (s.va >>> 0),
                          bytes: (s.bytes || []).map(function (b) {
                              return b & 0xff; })};
              })
            : [];

        g_diff_test_enabled = !!opts.diff_test;

        // Deterministic anchors (TAS framework P1).
        g_anchor_enabled     = !!opts.anchor_trace;
        g_anchor_booted      = false;
        g_anchor_prev_state  = -1;
        g_anchor_prev_screen = -1;
        g_anchor_last        = {};

        // Anchor-segmented input trace (TAS framework P2).  opts.input_segtrace
        // is an array of ops already normalised by the driver (kind in
        // input/wait/wait_until/capture).  Anchor detection auto-runs whenever
        // this is set (anchorTick gates on g_anchor_enabled || g_seg_enabled).
        g_seg_ops = Array.isArray(opts.input_segtrace)
            ? opts.input_segtrace.map(function (o) {
                  const k = String(o.kind || '');
                  if (k === 'input') {
                      return {kind: 'input', t: (o.t | 0), m: (o.m | 0),
                              w: (o.w | 0), l: (o.l | 0)};
                  }
                  if (k === 'wait')       return {kind: 'wait', anchor: String(o.anchor)};
                  if (k === 'wait_until') return {kind: 'wait_until', va: (o.va >>> 0),
                                                  type: String(o.type || 'i32'),
                                                  op: String(o.op || '=='),
                                                  val: o.val};
                  if (k === 'capture')    return {kind: 'capture', n: (o.n | 0),
                                                  label: String(o.label || '')};
                  if (k === 'auto_dismiss') {
                      return {kind: 'auto_dismiss', n: (o.n | 0),
                              t: (o.t === undefined || o.t === null) ? -1 : (o.t | 0)};
                  }
                  return {kind: 'nop'};
              })
            : [];
        g_seg_enabled       = g_seg_ops.length > 0;
        g_seg_idx           = 0;
        g_seg_base          = 0;
        g_seg_wait_floor    = -1;
        g_seg_done_sent     = false;
        g_capture_at_frames = null;

        // Save-schema state dump at capture instants (trace_studio).
        g_state_dump_slots = Array.isArray(opts.state_dump_slots)
            ? opts.state_dump_slots.map(function (s) {
                  return {i: (s.i | 0), va: (s.va >>> 0), size: (s.size | 0)};
              })
            : null;
        if (g_state_dump_slots !== null && g_state_dump_slots.length === 0)
            g_state_dump_slots = null;

        // Per-frame state watch (--watch NAME=0xVA:type).
        g_watch_specs = Array.isArray(opts.watch)
            ? opts.watch.map(function (w) {
                  return {name: String(w.name), va: (w.va >>> 0),
                          type: String(w.type || 'i32')};
              })
            : [];
        g_watch_last = {};

        // RNG seed pin (--rng-seed).  opts.rng_seed is null or {a,b,single}.
        g_rng_seed_enabled = !!opts.rng_seed;
        if (opts.rng_seed) {
            if (opts.rng_seed.a      !== undefined) g_rng_seed_a      = opts.rng_seed.a >>> 0;
            if (opts.rng_seed.b      !== undefined) g_rng_seed_b      = opts.rng_seed.b >>> 0;
            if (opts.rng_seed.single !== undefined) g_rng_seed_single = opts.rng_seed.single >>> 0;
        }

        // Anim-phase pin (--phase-pin) — boolean; see pinPhaseCounters.
        g_phase_pin_enabled = !!opts.phase_pin;

        g_skip_intro_enabled = !!opts.skip_intro;
        g_smack_in_open      = 0;
        g_skip_intro_forced  = 0;

        g_input_record_enabled = !!opts.input_record;
        // input_replay arrives as an array of {t, m, w, l} events sorted by
        // t.  Coerce defensively — a malformed entry would otherwise blow
        // up inside PostMessageA where the error message is opaque.
        if (Array.isArray(opts.input_replay) && opts.input_replay.length > 0) {
            g_input_replay_events = opts.input_replay.map(function (e) {
                return {t: (e.t | 0),
                        m: (e.m | 0),
                        w: (e.w | 0),
                        l: (e.l | 0)};
            }).sort(function (a, b) { return a.t - b.t; });
        } else {
            g_input_replay_events = null;
        }
        g_replay_idx           = 0;
        g_replay_post_count    = 0;

        // Call tracer (Phase G.3).  opts.call_trace_vas is an array of
        // Ghidra VAs to hook; opts.call_trace_frames (optional) restricts
        // emission to the listed sim frames (g_tick_count values).
        g_call_trace_enabled = !!opts.call_trace &&
                               Array.isArray(opts.call_trace_vas) &&
                               opts.call_trace_vas.length > 0;
        g_call_trace_vas = g_call_trace_enabled
            ? opts.call_trace_vas.map(function (v) { return v | 0; })
            : [];
        g_call_trace_frames =
            (Array.isArray(opts.call_trace_frames) &&
             opts.call_trace_frames.length > 0)
                ? new Set(opts.call_trace_frames.map(function (v) { return v | 0; }))
                : null;
        g_call_trace_min_cum_secs = (opts.call_trace_min_cum_secs | 0);
        g_call_trace_max_frames   = (opts.call_trace_max_frames | 0) || 8;
        g_call_trace_window_open  = false;
        g_call_trace_window_end   = 0;
        g_call_trace_hooked = false;
        g_call_trace_buffer = [];
        g_call_trace_n_ok   = 0;
        g_call_trace_n_fail = 0;

        // Memory-access watch (Phase H).  opts.mem_watch_regions is an array of
        // {va, size, label, access}; armed pre-resume below so an init-time
        // writer during scene bootstrap is trapped on its first write.
        g_mem_watch_enabled = !!opts.mem_watch &&
                              Array.isArray(opts.mem_watch_regions) &&
                              opts.mem_watch_regions.length > 0;
        // Lever 4 — configurable re-arm budget.
        g_mem_watch_rearm_cap = (opts.mem_watch_rearm_cap | 0) || 8000;
        // Lever 1 — bracketed arm window {enter, leave} (Ghidra VAs).
        g_mem_watch_arm_between =
            (opts.mem_watch_arm_between &&
             typeof opts.mem_watch_arm_between.enter === 'number' &&
             typeof opts.mem_watch_arm_between.leave === 'number')
                ? {enter: opts.mem_watch_arm_between.enter | 0,
                   leave: opts.mem_watch_arm_between.leave | 0}
                : null;
        // Lever 2 — snapshot-diff regions [{va, size, label}].
        g_mem_snapshot_regions =
            (Array.isArray(opts.mem_snapshot_regions)
                ? opts.mem_snapshot_regions
                : []).map(function (r) {
                    return {va: r.va | 0,
                            size: (r.size | 0) || 16,
                            label: String(r.label ||
                                ('0x' + ((r.va | 0) >>> 0).toString(16)))};
                });
        g_mem_snapshot_enabled = g_mem_snapshot_regions.length > 0;

        const mod = Process.findModuleByName(g_target_module_name);
        if (!mod) {
            err('init', 'target module not loaded: ' + g_target_module_name);
            return;
        }
        g_base = mod.base;

        // Leak-proofing: arm the controller-liveness watchdog first thing, so
        // even if init throws later the target won't outlive a dead controller.
        armHeartbeatWatchdog(opts.heartbeat_timeout_ms | 0);

        // Always-on hooks: HWND tracking, MessageBox capture, engine-tick
        // anchor (publishes fps_sample even without turbo so the harness
        // can plot a baseline curve).
        installCreateWindowExHooks();
        installMessageBoxRedirectHook();
        // Save virtualization — install pre-resume so it catches any boot-path
        // Castles.dat open AND the trace's later save load + turn-save rotation,
        // so a capture replay never reads or writes the real game-dir saves
        // (reproducibility — see installSaveRedirectHook above).
        if (g_save_sandbox) {
            try { installSaveRedirectHook(g_save_sandbox); }
            catch (e) { err('installSaveRedirectHook', e.message); }
        }
        installEngineTickHook();

        // Conditionally-on hooks based on init flags.
        if (g_hide_window)     installWindowHideHooks();
        if (g_force_windowed)  { installChangeDisplaySettingsStub();
                                 installForceWindowedDDraw(); }
        if (g_gdi_seat_enabled) {
            // Hook installs lazily from engine_tick.onLeave (the
            // primary surface pointer doesn't exist until retail's
            // FUN_0042ec91 returns).  Verify at runtime with
            // "gdi_seat: IDirectDrawSurface::Blt patched" in the
            // agent log.
            log('gdi_seat: armed (lazy install on first engine_tick ' +
                'after retail finishes DDraw surface bring-up)');
        }
        if (g_agent_present_enabled) {
            log('agent_present: armed (every ' +
                g_agent_present_every_ticks + ' engine_ticks, ' +
                'via presentBackToWindowViaGdi — interactive only)');
        }
        if (g_force_active) {
            log('force_active: armed (poking var_app_active=1 / '
                + 'var_surface_lost=0 each tick so the main loop never '
                + 'Sleep(200)s — interactive real-mouse seat)');
        }
        if (g_show_window_after_ms > 0) {
            log('show_window_after_ms: ' + g_show_window_after_ms +
                ' (will ShowWindow(SW_SHOW) once elapsed)');
        }
        if (g_silent_audio_enabled) { installWaveOutMute();
                                      installSilentAudioDSound(); }
        if (g_turbo_enabled) {
            // Order matters: virtualise timeGetTime BEFORE the Sleep
            // no-op so the engine never observes a real-clock baseline
            // once turbo is armed.  The clock is gated on the main
            // thread, which won't be known until the first engine_tick
            // entry — so g_clock_virtualised goes true at install but
            // the gate inside the onLeave hook fails silently until
            // engine_tick captures the thread id.
            try { installVirtualClockHook(); }
            catch (e) { err('installVirtualClockHook', e.message); }
            try { installTurboSleepHook(); }
            catch (e) { err('installTurboSleepHook', e.message); }
        } else if (g_diff_test_enabled) {
            // Diff-test mode without turbo still needs the timeGetTime hook
            // for the FPS function's scripted-clock queue.  The hook is
            // idempotent, so a turbo+diff combined run only attaches once.
            try { installTimeGetTimeHook(); }
            catch (e) { err('installTimeGetTimeHook', e.message); }
        }
        if (g_diff_test_enabled) {
            // GetCursorPos scripted-return — required for runRetailInputState
            // (FUN_004b191e reads the cursor pos via GetCursorPos at entry).
            // Idempotent; only installs once.
            try { installGetCursorPosHook(); }
            catch (e) { err('installGetCursorPosHook', e.message); }
        }

        if (g_anchor_enabled) {
            log('anchor_trace: armed (emits {anchor,frame} on engine-state / ' +
                'screen-id transitions: BOOT/TITLE/NEW_GAME/LOADING_*/MAP_FREEROAM)');
        }
        if (g_seg_enabled) {
            log('input_segtrace: armed (' + g_seg_ops.length + ' ops; ' +
                'wait/wait_until/capture + anchor-relative frames)');
        }
        if (g_rng_seed_enabled) {
            log('rng_seed: armed (pinned each pre-ingame tick: a=0x' +
                (g_rng_seed_a >>> 0).toString(16) + ' b=0x' +
                (g_rng_seed_b >>> 0).toString(16) + ' single=0x' +
                (g_rng_seed_single >>> 0).toString(16) + ')');
        }
        if (g_watch_specs.length > 0) {
            log('watch: armed (' + g_watch_specs.length +
                ' globals, edge-emitted to watch.jsonl)');
        }

        if (g_frame_capture_enabled) {
            log('frame_capture: armed (max=' +
                (g_frame_capture_max === 0 ? 'unlimited' : g_frame_capture_max) +
                ', warmup_secs=' + g_frame_capture_warmup_secs +
                ', cadence=1 capture per engine cum-secs rollover)');
        }

        if (g_skip_intro_enabled) {
            try { installSkipIntroHook(); }
            catch (e) { err('installSkipIntroHook', e.message); }
        }

        if (g_input_record_enabled) {
            installInputRecordHook();
        }
        if (g_capture_draws) {
            try { installDrawCaptureHooks(); }
            catch (e) { err('installDrawCaptureHooks', e.message); }
        }
        if (g_suppress_edge_scroll) {
            try { installEdgeScrollSuppress(); }
            catch (e) { err('installEdgeScrollSuppress', e.message); }
        }
        if (g_input_replay_events) {
            log('input_replay: armed (' + g_input_replay_events.length +
                ' events; first tick=' + g_input_replay_events[0].t +
                ', last tick=' +
                g_input_replay_events[g_input_replay_events.length - 1].t + ')');
        }

        if (g_call_trace_enabled) {
            try { installCallTraceHooks(g_call_trace_vas); }
            catch (e) { err('installCallTraceHooks', e.message); }
            log('call_trace: armed (' + g_call_trace_vas.length + ' VAs' +
                (g_call_trace_frames
                    ? ', frames=' + Array.from(g_call_trace_frames).join(',')
                    : ', every frame') + ')');
        }

        // Arm mem_watch PRE-RESUME so an init-time writer is trapped on its
        // first write (init runs before the controller calls device.resume).
        // With a bracketed window (--arm-between) the arm is deferred to the
        // ENTER hook so the monitor is only live during the suspect call.
        if (g_mem_watch_enabled) {
            const defer = g_mem_watch_arm_between !== null;
            installMemoryWatch(opts.mem_watch_regions,
                               opts.mem_watch_precise !== false, defer);
            if (defer) installMemWatchBracket();
        }
        // Snapshot-diff baseline (polled per tick thereafter).
        if (g_mem_snapshot_enabled) {
            memSnapshotInit();
        }

        send({kind: 'ready',
              module:        g_target_module_name,
              base:          g_base.toString(),
              imageBase:     IMAGE_BASE.toString(),
              fridaVersion:  Frida.version,
              hide_window:   g_hide_window,
              force_windowed: g_force_windowed,
              silent_audio:  g_silent_audio_enabled,
              turbo:         g_turbo_enabled,
              turbo_step_ms: g_turbo_step_ms,
              gdi_seat:      g_gdi_seat_enabled,
              frame_capture: g_frame_capture_enabled,
              frame_capture_max: g_frame_capture_max,
              frame_capture_warmup_secs: g_frame_capture_warmup_secs,
              skip_intro:    g_skip_intro_enabled,
              input_record:  g_input_record_enabled,
              input_replay_count:
                  g_input_replay_events ? g_input_replay_events.length : 0,
              call_trace:    g_call_trace_enabled,
              call_trace_vas: g_call_trace_vas.length,
              diff_test:     g_diff_test_enabled});

        if (g_diff_test_enabled) {
            log('diff_test: armed (runRetailRngStep / runRetailComputeFps / ' +
                'runRetailAnimCycle / runRetailFrameDelta / runRetailPhaseTicks / ' +
                'runRetailInputState / runRetailSimSubSteps / runRetailStateHash / ' +
                'runRetailTileExpand / runRetailDelayedAction RPCs live; ' +
                'orchestrator drives test vectors)');
        }
    },

    // ─── Interactive probe RPC (tools/probe_daemon.py) ───────────────────
    // Live screenshot + input + memory against a running retail, WITHOUT a
    // pre-authored segtrace — the exploratory front-end for discovering click
    // coords + state signals before compiling a deterministic trace.  All are
    // thin wrappers on the same internals the segtrace path uses (so a coord
    // found here transfers 1:1 to a {t,m,w,l} trace line).
    probeShot: function () {
        // Grab the current back-surface + send it as a 'frame' message (same
        // path as captureFrameOnce).  Returns the frame_idx attached, or -1.
        try {
            const idx = g_frame_capture_count;
            captureFrameOnce();
            return idx;
        } catch (e) { err('probeShot', e.message); return -1; }
    },
    probeStreamStart: function (intervalMs) {
        g_stream_interval_ms = (intervalMs | 0) > 0 ? (intervalMs | 0) : 33;
        g_last_stream_ms = 0;
        g_stream_enabled = true;
        return g_stream_interval_ms;
    },
    probeStreamStop: function () { g_stream_enabled = false; return true; },
    probePost: function (m, w, l) {
        // Post one WM_ message to the cached main HWND; mouse lParams rescaled
        // to the pinned 640x480 client exactly like segtraceTick's input op.
        try {
            const post = resolvePostMessageA();
            if (!post) return false;
            const hwnd = rva(ADDR.var_main_hwnd).readPointer();
            if (hwnd.isNull()) return false;
            let lp = (l >>> 0);
            if (m >= WM_MOUSEMOVE && m <= WM_RBUTTONDBLCLK) {
                lp = rescaleMouseLParamToClient(lp);
            }
            post(hwnd, m >>> 0, w >>> 0, lp >>> 0);
            return true;
        } catch (e) { err('probePost', e.message); return false; }
    },
    probeRead: function (va, type) {
        try { return segReadTyped(va >>> 0, String(type || 'i32')); }
        catch (e) { return null; }
    },
    probeReads: function (specs) {
        // specs: [{name, va, type}] → {name: value}; unreadable → null.
        const out = {};
        const a = Array.isArray(specs) ? specs : [];
        for (let i = 0; i < a.length; i++) {
            const s = a[i];
            try { out[String(s.name)] = segReadTyped((s.va >>> 0),
                                                     String(s.type || 'i32')); }
            catch (e) { out[String(s.name)] = null; }
        }
        return out;
    },
    probePoke: function (va, bytes) {
        try {
            const a = (Array.isArray(bytes) ? bytes : [])
                          .map(function (b) { return b & 0xff; });
            rva(va >>> 0).writeByteArray(a);
            return true;
        } catch (e) { err('probePoke', e.message); return false; }
    },
    probeAnchors: function () { return g_anchor_last || {}; },
    probeCall: function (va, args, abi) {
        // Call a retail function at a Ghidra VA with up to N int args.
        // ⚠ UNSAFE for engine-context fns: called from the V8 thread it races the
        // engine main thread — CRASHED the game on FUN_0043ac23 (end-turn) 2026-07-09.
        // To drive actions this way SAFELY, schedule the call ON the engine thread
        // (queue it in the per-tick hook / freeze the tick during the call, like
        // diff_test's engine-tick-freeze).  OK today for pure/leaf reads.
        // Returns the int retval or null.
        try {
            const a = (Array.isArray(args) ? args : []).map(function (x) { return x | 0; });
            const fn = new NativeFunction(rva(va >>> 0), 'int32',
                                          a.map(function () { return 'int32'; }),
                                          String(abi || 'mscdecl'));
            return fn.apply(null, a);
        } catch (e) { err('probeCall', e.message); return null; }
    },
    probeEnqueueCall: function (va, args, abi) {
        // SAFE direct call: enqueue a fn call to run on the ENGINE thread in the
        // per-tick hook (drainCallQueue).  Returns a call id; the result comes
        // back as a {kind:'call_result', id} message once the tick executes it.
        g_call_seq = (g_call_seq + 1) | 0;
        g_call_queue.push({
            id: g_call_seq, va: (va >>> 0),
            args: (Array.isArray(args) ? args : []).map(function (x) { return x | 0; }),
            abi: String(abi || 'mscdecl')});
        return g_call_seq;
    },
    probeAutoDismiss: function (n) {
        // Toggle the reactive event-scroll dismiss (var_extended_pause != 0 →
        // call the dismiss fn each tick).  Event scrolls (e.g. Flooding) set
        // var_extended_pause but DON'T dismiss on RMB, unlike the 0x27 entry
        // scrolls — this is the only thing that clears them.
        g_auto_dismiss = (n | 0);
        return g_auto_dismiss;
    },

    // Controller liveness ping — resets the in-target watchdog timer.  If
    // these stop arriving for g_heartbeat_timeout_ms the agent self-terminates
    // retail so a dead controller can't leak an elevated process.
    heartbeat: function () {
        g_last_heartbeat_ms = Date.now();
        return true;
    },

    // Late-install the call tracer after init (e.g. only once a scenario
    // has driven retail into the target scene, to avoid hooking through
    // the noisy CRT/bring-up window).  vas: [Ghidra-VA]; frames: optional
    // [sim-frame] filter.
    installCallTrace: function (vas, frames) {
        g_call_trace_enabled = Array.isArray(vas) && vas.length > 0;
        g_call_trace_vas = g_call_trace_enabled
            ? vas.map(function (v) { return v | 0; }) : [];
        g_call_trace_frames =
            (Array.isArray(frames) && frames.length > 0)
                ? new Set(frames.map(function (v) { return v | 0; })) : null;
        g_call_trace_buffer = [];
        g_call_trace_n_ok = 0;
        g_call_trace_n_fail = 0;
        if (!g_call_trace_enabled) return false;
        try { installCallTraceHooks(g_call_trace_vas); return true; }
        catch (e) { err('installCallTrace', e.message); return false; }
    },

    getCallTraceStatus: function () {
        return {enabled: g_call_trace_enabled,
                hooked:  g_call_trace_hooked,
                n_ok:    g_call_trace_n_ok,
                n_fail:  g_call_trace_n_fail,
                n_vas:   g_call_trace_vas.length,
                buffered: g_call_trace_buffer.length};
    },

    // Late-install the memory watch after init (e.g. once a scenario has
    // driven retail into the scene right before the region is written).
    installMemoryWatch: function (regions, precise) {
        const ok = installMemoryWatch(regions, precise);
        g_mem_watch_enabled = ok;
        return ok;
    },

    // Drain the buffer + return counters; the driver calls this at the end of
    // a run so the last partial batch isn't lost.
    getMemWatchStatus: function () {
        memWatchFlush(g_tick_count);
        return {enabled:  g_mem_watch_enabled,
                precise:  g_mem_watch_precise,
                n_hits:   g_mem_watch_n,
                n_neighbor: g_mem_watch_neighbor,
                n_rearm:  g_mem_watch_rearm,
                regions:  g_mem_watch_regions.length};
    },

    runRetailRngStep: function (state_a, state_b) {
        return runRetailRngStep(state_a, state_b);
    },

    runRetailComputeFps: function (now_ms, rollover_now_ms,
                                   last_render_ms, window_start_ms,
                                   frames_this_second, current_fps,
                                   cumulative_fps, cumulative_seconds) {
        return runRetailComputeFps(now_ms, rollover_now_ms,
                                   last_render_ms, window_start_ms,
                                   frames_this_second, current_fps,
                                   cumulative_fps, cumulative_seconds);
    },

    runRetailAnimCycle: function (counter, tick_80ms_fired) {
        return runRetailAnimCycle(counter, tick_80ms_fired);
    },

    runRetailFrameDelta: function (now_ms, last_frame_ms) {
        return runRetailFrameDelta(now_ms, last_frame_ms);
    },

    runRetailPhaseTicks: function (now_ms,
                                   last_base_tick_ms, base_tick_counter,
                                   counter_2,  counter_4,
                                   counter_8,  counter_13,
                                   counter_16, counter_24, counter_32) {
        return runRetailPhaseTicks(
            now_ms,
            last_base_tick_ms, base_tick_counter,
            counter_2,  counter_4,
            counter_8,  counter_13,
            counter_16, counter_24, counter_32);
    },

    runRetailInputState: function (in_obj) {
        return runRetailInputState(in_obj);
    },

    runRetailSimSubSteps: function (in_obj) {
        return runRetailSimSubSteps(in_obj);
    },

    installSimSubStepsRateLut: function (lut_bytes) {
        return installSimSubStepsRateLut(lut_bytes);
    },

    uninstallSimSubStepsRateLut: function () {
        return uninstallSimSubStepsRateLut();
    },

    installSimSubStepsEngineFreeze: function () {
        return installSimSubStepsEngineFreeze();
    },

    uninstallSimSubStepsEngineFreeze: function () {
        return uninstallSimSubStepsEngineFreeze();
    },

    installStateHashSources: function (seed) {
        return installStateHashSources(seed);
    },

    uninstallStateHashSources: function () {
        return uninstallStateHashSources();
    },

    runRetailStateHash: function (in_obj) {
        return runRetailStateHash(in_obj);
    },

    installTileExpandBackup: function () {
        return installTileExpandBackup();
    },

    uninstallTileExpandBackup: function () {
        return uninstallTileExpandBackup();
    },

    runRetailTileExpand: function (in_obj) {
        return runRetailTileExpand(in_obj);
    },

    installDelayedActionStub: function () {
        return installDelayedActionStub();
    },

    uninstallDelayedActionStub: function () {
        return uninstallDelayedActionStub();
    },

    runRetailDelayedAction: function (in_obj) {
        return runRetailDelayedAction(in_obj);
    },

    runRetailRealmPass: function (in_obj) {
        return runRetailRealmPass(in_obj);
    },

    // Diagnostic counters for the DDraw → GDI seat.  Reflects everything
    // the Interceptor.attach onEnter observed; null counters mean the
    // seat never patched (engine didn't reach surface bring-up).
    getGdiSeatStats: function () {
        return {
            patched:         g_primary_blt_patched,
            blt_observed:    g_gdi_blt_observed,
            blt_skipped_fill: g_gdi_blt_skipped_fill,
            blt_skipped_other: g_gdi_blt_skipped_other,
            gdi_presents:    g_gdi_present_count,
        };
    },
};
