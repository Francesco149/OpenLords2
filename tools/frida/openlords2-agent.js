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

// ─── runtime state ──────────────────────────────────────────────────────

let g_base = null;
let g_start_real_ms = Date.now();

let g_hide_window         = false;
let g_owned_hwnds         = {};   // key = hwnd hex, val = {source}

let g_silent_audio_enabled = false;
let g_force_windowed       = false;
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
let g_show_window_after_ms     = 0;        // 0 = never auto-show
let g_show_window_client_w     = 640;      // post-show client size — engine's
let g_show_window_client_h     = 480;      // native render res, 1:1 pixel-perfect
let g_show_window_done         = false;
let g_show_window_latch        = false;    // one-shot bypass for hide hooks
let g_nf_show_window           = null;

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
let g_nf_surface_lock        = null;
let g_nf_surface_unlock      = null;
let g_nf_palette_get_entries = null;
let g_capture_scratch        = null;   // pre-allocated payload buffer

// Per-tick anchor counter. Bumped in engine_tick.onEnter (1-indexed: equals
// the tick number currently executing). Used both as the timestamp for
// recorded input events and as the schedule index for replaying them.
let g_tick_count          = 0;

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
                try { rva(ADDR.var_fullscreen_flag).writeU32(0); }
                catch (e) { err('force_windowed.flag_override', e.message); }
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
        try {
            post(hwnd, ev.m >>> 0, ev.w >>> 0, ev.l >>> 0);
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
            if (g_input_replay_events) {
                drainInputReplayForTick(g_tick_count);
            }
        },
        onLeave: function () {
            try {
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
                tryShowEngineWindow();
                tryAgentDrivenPresent();
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

// ─── init RPC ──────────────────────────────────────────────────────────

rpc.exports = {
    init: function (opts) {
        opts = opts || {};
        g_target_module_name = String(opts.module || MODULE_NAME);
        g_hide_window        = !!opts.hide_window;
        g_silent_audio_enabled = !!opts.silent_audio;
        g_force_windowed     = !!opts.force_windowed;
        g_turbo_enabled      = !!opts.turbo;
        g_gdi_seat_enabled   = !!opts.gdi_seat;
        g_agent_present_enabled = !!opts.agent_present;
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

        g_diff_test_enabled = !!opts.diff_test;

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

        const mod = Process.findModuleByName(g_target_module_name);
        if (!mod) {
            err('init', 'target module not loaded: ' + g_target_module_name);
            return;
        }
        g_base = mod.base;

        // Always-on hooks: HWND tracking, MessageBox capture, engine-tick
        // anchor (publishes fps_sample even without turbo so the harness
        // can plot a baseline curve).
        installCreateWindowExHooks();
        installMessageBoxRedirectHook();
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

        if (g_frame_capture_enabled) {
            log('frame_capture: armed (max=' +
                (g_frame_capture_max === 0 ? 'unlimited' : g_frame_capture_max) +
                ', warmup_secs=' + g_frame_capture_warmup_secs +
                ', cadence=1 capture per engine cum-secs rollover)');
        }

        if (g_input_record_enabled) {
            installInputRecordHook();
        }
        if (g_input_replay_events) {
            log('input_replay: armed (' + g_input_replay_events.length +
                ' events; first tick=' + g_input_replay_events[0].t +
                ', last tick=' +
                g_input_replay_events[g_input_replay_events.length - 1].t + ')');
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
              input_record:  g_input_record_enabled,
              input_replay_count:
                  g_input_replay_events ? g_input_replay_events.length : 0,
              diff_test:     g_diff_test_enabled});

        if (g_diff_test_enabled) {
            log('diff_test: armed (runRetailRngStep / runRetailComputeFps / ' +
                'runRetailAnimCycle / runRetailFrameDelta / runRetailPhaseTicks / ' +
                'runRetailInputState / runRetailSimSubSteps / runRetailStateHash / ' +
                'runRetailTileExpand RPCs live; orchestrator drives test vectors)');
        }
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
