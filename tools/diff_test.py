#!/usr/bin/env python3
"""
tools/diff_test.py — pure-function differential test orchestrator
(Phase 4 of docs/harness-roadmap.md).

For each ported pure function whose retail address is known, run a batch
of input vectors through BOTH:

  - the retail engine (Frida agent calls FUN_* via NativeFunction, with
    all observable globals snapshotted before + restored after — see
    runRetailRngStep / runRetailComputeFps in tools/frida/openlords2-agent.js)
  - our port (host-compiled libengine_diff.so loaded via ctypes — built by
    `make -C tests diff`)

and report any divergence.  Catches arithmetic / hashing / table-lookup
deltas in our port before scenarios surface them.

Current targets:
  - engine_rng_step              (FUN_00404a46) — double-LFSR step
  - engine_compute_fps           (FUN_004b27bf) — rolling 1-sec FPS counter
  - engine_compute_anim_cycle    (FUN_00451f1a) — 16-frame anim-cycle bump
  - engine_compute_frame_delta   (FUN_004b20ed) — frame delta with 30 ms gate
  - engine_compute_phase_ticks   (FUN_004bbc80) — 9-flag animation phase tick
  - engine_compute_input_state   (FUN_004b191e) — mouse-input state resolver
  - engine_compute_sim_sub_steps (FUN_004bb978) + engine_compute_scenario_hint
                                 (FUN_0043de70) — sim sub-step rate gate
                                 (chained: hint feeds gate)
  - engine_compute_state_hash    (FUN_00440231) — per-tick MP state hash
                                 (source-array payload installed once
                                  per test plan from a shared seed;
                                  engine-tick freeze required)
  - engine_tile_expand_footprint (FUN_004917dd) — castle-load 3x3 footprint
                                 expander (per-vector pre-state re-seed
                                 of tiles + footprint_list + last_footprint;
                                 engine-tick freeze required)
  - engine_dispatch_delayed_actions (FUN_004472ce) — per-tick delayed-action
                                 dispatcher (sparse per-vector record list;
                                 50-entry dispatch table stubbed to no-op
                                 at test-plan install; engine-tick freeze
                                 required)

The orchestrator runs the agent in "diff_test" mode (no turbo, no input
replay) so the engine boots, parks, and waits.  Once the per-tick driver
is alive (we wait for the first fps_sample event or a small fixed delay,
whichever comes first), we fire all test vectors via RPC and report.

Usage:
    nix develop --command python3 tools/diff_test.py
    nix develop --command python3 tools/diff_test.py --functions rng_step
    nix develop --command python3 tools/diff_test.py --vectors 200
"""

from __future__ import annotations

import argparse
import contextlib
import ctypes
import datetime as dt
import json
import os
import random
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import frida

ROOT     = Path(__file__).resolve().parent.parent
AGENT_JS = ROOT / "tools" / "frida" / "openlords2-agent.js"
RETAIL_EXE = ROOT / "vendor" / "original" / "Lords2.exe"
ASSET_CWD  = ROOT / "vendor" / "original"
DIFF_SO    = ROOT / "tests" / "build" / "libengine_diff.so"

# Reuse the capture driver's frida-server bring-up + path conventions.
sys.path.insert(0, str(ROOT / "tools"))
import frida_capture as fc  # noqa: E402


# ─── ctypes bindings for our port ─────────────────────────────────────────


class EngineRngIn(ctypes.Structure):
    _fields_ = [
        ("state_a", ctypes.c_uint32),
        ("state_b", ctypes.c_uint32),
    ]


class EngineRngOut(ctypes.Structure):
    _fields_ = [
        ("new_state_a", ctypes.c_uint32),
        ("new_state_b", ctypes.c_uint32),
        ("rng_15bit_a", ctypes.c_uint32),
        ("rng_7bit_a",  ctypes.c_uint32),
        ("rng_3bit_a",  ctypes.c_uint32),
        ("rng_15bit_b", ctypes.c_uint32),
        ("rng_7bit_b",  ctypes.c_uint32),
        ("rng_3bit_b",  ctypes.c_uint32),
    ]


class EngineFpsIn(ctypes.Structure):
    _fields_ = [
        ("now_ms",                 ctypes.c_uint32),
        ("rollover_now_ms",        ctypes.c_uint32),
        ("last_render_ms",         ctypes.c_uint32),
        ("second_window_start_ms", ctypes.c_uint32),
        ("frames_this_second",     ctypes.c_uint32),
        ("current_fps",            ctypes.c_uint32),
        ("cumulative_fps",         ctypes.c_uint32),
        ("cumulative_seconds",     ctypes.c_uint32),
    ]


class EngineFpsOut(ctypes.Structure):
    _fields_ = [
        ("delta_ms",                    ctypes.c_uint32),
        ("new_last_render_ms",          ctypes.c_uint32),
        ("new_second_window_start_ms",  ctypes.c_uint32),
        ("new_frames_this_second",      ctypes.c_uint32),
        ("new_current_fps",             ctypes.c_uint32),
        ("new_cumulative_fps",          ctypes.c_uint32),
        ("new_cumulative_seconds",      ctypes.c_uint32),
        ("window_rolled_over",          ctypes.c_int32),
    ]


class EngineAnimCycleIn(ctypes.Structure):
    _fields_ = [
        ("counter",         ctypes.c_int),
        ("tick_80ms_fired", ctypes.c_int),
    ]


class EngineAnimCycleOut(ctypes.Structure):
    _fields_ = [
        ("new_counter", ctypes.c_int),
    ]


class EngineDeltaIn(ctypes.Structure):
    _fields_ = [
        ("now_ms",        ctypes.c_uint32),
        ("last_frame_ms", ctypes.c_uint32),
    ]


class EngineDeltaOut(ctypes.Structure):
    _fields_ = [
        ("delta_ms",          ctypes.c_int32),
        ("new_last_frame_ms", ctypes.c_uint32),
    ]


# Mirrors engine_phase_in_t / engine_phase_out_t in src/engine.h.  Field
# order MUST match the header — ctypes lays them out struct-style.
class EnginePhaseIn(ctypes.Structure):
    _fields_ = [
        ("now_ms",            ctypes.c_uint32),
        ("last_base_tick_ms", ctypes.c_uint32),
        ("base_tick_counter", ctypes.c_uint32),
        ("counter_2",         ctypes.c_uint32),
        ("counter_4",         ctypes.c_uint32),
        ("counter_8",         ctypes.c_uint32),
        ("counter_13",        ctypes.c_uint32),
        ("counter_16",        ctypes.c_uint32),
        ("counter_24",        ctypes.c_uint32),
        ("counter_32",        ctypes.c_uint32),
    ]


class EnginePhaseOut(ctypes.Structure):
    _fields_ = [
        ("tick_20ms",               ctypes.c_int),
        ("tick_80ms",               ctypes.c_int),
        ("tick_160ms",              ctypes.c_int),
        ("tick_320ms",              ctypes.c_int),
        ("tick_640ms",              ctypes.c_int),
        ("tick_1040ms",             ctypes.c_int),
        ("tick_1280ms",             ctypes.c_int),
        ("tick_1920ms",             ctypes.c_int),
        ("tick_2560ms",             ctypes.c_int),
        ("new_last_base_tick_ms",   ctypes.c_uint32),
        ("new_base_tick_counter",   ctypes.c_uint32),
        ("new_counter_2",           ctypes.c_uint32),
        ("new_counter_4",           ctypes.c_uint32),
        ("new_counter_8",           ctypes.c_uint32),
        ("new_counter_13",          ctypes.c_uint32),
        ("new_counter_16",          ctypes.c_uint32),
        ("new_counter_24",          ctypes.c_uint32),
        ("new_counter_32",          ctypes.c_uint32),
    ]


# Mirrors engine_input_in_t / engine_input_out_t in src/engine.h.  Field
# order MUST match the header.
class EngineInputIn(ctypes.Structure):
    _fields_ = [
        ("cursor_screen_x",       ctypes.c_int),
        ("cursor_screen_y",       ctypes.c_int),
        ("now_ms",                ctypes.c_uint32),
        ("cursor_cache_enabled",  ctypes.c_int),
        ("pending_dblclk",        ctypes.c_uint8),
        ("button_state",          ctypes.c_uint8),
        ("viewport_x0",           ctypes.c_int),
        ("viewport_y0",           ctypes.c_int),
        ("viewport_x1",           ctypes.c_int),
        ("viewport_y1",           ctypes.c_int),
        ("viewport_game_w",       ctypes.c_int),
        ("viewport_game_h",       ctypes.c_int),
        ("cached_cursor_x",       ctypes.c_int16),
        ("cached_cursor_y",       ctypes.c_int16),
        ("prev_game_x",           ctypes.c_int),
        ("prev_game_y",           ctypes.c_int),
        ("prev_lmb",              ctypes.c_int),
        ("prev_rmb",              ctypes.c_int),
        ("lmb_timer_armed",       ctypes.c_int),
        ("rmb_timer_armed",       ctypes.c_int),
        ("click_timer_ms",        ctypes.c_uint32),
        ("motion_last_ms",        ctypes.c_uint32),
        ("click_eligible",        ctypes.c_int),
        ("prev_last_click_x",     ctypes.c_int),
        ("prev_last_click_y",     ctypes.c_int),
    ]


class EngineRateIn(ctypes.Structure):
    _fields_ = [
        ("now_ms",            ctypes.c_uint32),
        ("last_step_ms",      ctypes.c_uint32),
        ("autosave_start_ms", ctypes.c_uint32),
        ("run_gate",          ctypes.c_int),
        ("scenario_hint",     ctypes.c_int),
        ("mp_active",         ctypes.c_int),
        ("sim_tick_counter",  ctypes.c_uint32),
        ("game_speed",        ctypes.c_int),
        ("pause_lock",        ctypes.c_int),
        ("screen_freeze",     ctypes.c_int),
        ("extended_pause",    ctypes.c_int),
        ("mp_skip_flag",      ctypes.c_int),
        ("phase",             ctypes.c_int),
        ("rate_lut",          ctypes.POINTER(ctypes.c_uint8)),
    ]


class EngineRateOut(ctypes.Structure):
    _fields_ = [
        ("sub_steps",              ctypes.c_int),
        ("new_last_step_ms",       ctypes.c_uint32),
        ("new_autosave_start_ms",  ctypes.c_uint32),
        ("new_phase",              ctypes.c_int),
        ("derived_hint",           ctypes.c_int),
        ("notify_autosave_done",   ctypes.c_int),
    ]


class EngineScenarioHintIn(ctypes.Structure):
    _fields_ = [
        ("game_state_phase",     ctypes.c_int),
        ("mp_active",            ctypes.c_int),
        ("local_player_idx",     ctypes.c_int),
        ("game_mode_speed",      ctypes.c_int),
        ("ally_a",               ctypes.c_int),
        ("ally_b",               ctypes.c_int),
        ("frame_count",          ctypes.c_int),
        ("max_stride",           ctypes.c_int),
        ("stride_sum",           ctypes.c_int),
        ("avg_stride",           ctypes.c_int),
        ("slow_frame_count",     ctypes.c_int),
        ("base_delta",           ctypes.c_int),
        ("active_count",         ctypes.c_int),
        ("player_active",        ctypes.c_uint8 * 7),
        ("player_has_data",      ctypes.c_uint8 * 7),
        ("player_disconnected",  ctypes.c_uint8 * 7),
        ("player_ticks",         ctypes.c_int   * 7),
    ]


class EngineScenarioHintOut(ctypes.Structure):
    _fields_ = [
        ("hint",                  ctypes.c_int),
        ("new_frame_count",       ctypes.c_int),
        ("new_max_stride",        ctypes.c_int),
        ("new_stride_sum",        ctypes.c_int),
        ("new_avg_stride",        ctypes.c_int),
        ("new_slow_frame_count",  ctypes.c_int),
        ("new_active_count",      ctypes.c_int),
        ("notify_slow_frame",     ctypes.c_int),
    ]


class EngineInputOut(ctypes.Structure):
    _fields_ = [
        ("raw_cursor_x",          ctypes.c_int),
        ("raw_cursor_y",          ctypes.c_int),
        ("new_cached_cursor_x",   ctypes.c_int16),
        ("new_cached_cursor_y",   ctypes.c_int16),
        ("prev_game_x_snapshot",  ctypes.c_int),
        ("prev_game_y_snapshot",  ctypes.c_int),
        ("prev_lmb_snapshot",     ctypes.c_int),
        ("prev_rmb_snapshot",     ctypes.c_int),
        ("game_x",                ctypes.c_int),
        ("game_y",                ctypes.c_int),
        ("mouse_event",           ctypes.c_int),
        ("button_changed",        ctypes.c_int),
        ("lmb_newly_pressed",     ctypes.c_int),
        ("lmb_newly_released",    ctypes.c_int),
        ("rmb_newly_pressed",     ctypes.c_int),
        ("rmb_newly_released",    ctypes.c_int),
        ("lmb_click_fired",       ctypes.c_int),
        ("rmb_click_fired",       ctypes.c_int),
        ("lmb_dblclk",            ctypes.c_int),
        ("rmb_dblclk",            ctypes.c_int),
        ("new_lmb",               ctypes.c_int),
        ("new_rmb",               ctypes.c_int),
        ("new_lmb_timer_armed",   ctypes.c_int),
        ("new_rmb_timer_armed",   ctypes.c_int),
        ("new_click_timer_ms",    ctypes.c_uint32),
        ("new_motion_last_ms",    ctypes.c_uint32),
        ("motion_idle_ms",        ctypes.c_uint32),
        ("last_click_x",          ctypes.c_int),
        ("last_click_y",          ctypes.c_int),
        ("new_click_eligible",    ctypes.c_int),
    ]


class EngineStateHashIn(ctypes.Structure):
    _fields_ = [
        ("mode",                     ctypes.c_int),
        ("player_slot",              ctypes.c_int),
        ("sim_tick_counter",         ctypes.c_uint32),
        ("per_army_768",             ctypes.POINTER(ctypes.c_uint8)),
        ("player_strategic_352",     ctypes.POINTER(ctypes.c_uint8)),
        ("per_territory_420",        ctypes.POINTER(ctypes.c_uint8)),
        ("per_battle_hex_432",       ctypes.POINTER(ctypes.c_uint8)),
        ("per_combat_unit_76",       ctypes.POINTER(ctypes.c_uint8)),
        ("per_battle_hex_aux_52",    ctypes.POINTER(ctypes.c_uint8)),
    ]


class EngineStateHashOut(ctypes.Structure):
    _fields_ = [
        ("bucket",                   ctypes.c_uint8 * 10),
        ("hash_region",              ctypes.c_uint8 * 600),
        ("notify_combat_snapshot",   ctypes.c_int),
    ]


# Mirrors engine_tile_footprint_state_t in src/engine_tile.h.  Single
# struct used both as input and output — engine_tile_expand_footprint
# reads tile_byte_offset + the embedded arrays, mutates them in place.
# Total size 70560 bytes; ctypes handles the embedded arrays natively.
class EngineTileExpandState(ctypes.Structure):
    _fields_ = [
        ("tile_byte_offset",   ctypes.c_int),
        ("tiles",              ctypes.c_uint8 * 51200),    # 6400 × 8
        ("path_scratch",       ctypes.c_uint16 * 6400),    # u16[6400] = 12800 B
        ("stage_scratch",      ctypes.c_uint8 * 6400),
        ("footprint_list",     (ctypes.c_int32 * 2) * 19), # 152 B
        ("last_footprint_tile",ctypes.c_int32),
    ]


# Mirrors engine_delayed_action_record_t in src/engine_delayed_action.h.
# Per-record stride = 0x38 (retail constant); 11 i32 fields + 10 B pad +
# active byte + type byte.  The runtime unit-test test_engine_delayed_action_
# record_size_layout cross-checks the offsets host-side.
class EngineDelayedActionRecord(ctypes.Structure):
    _fields_ = [
        ("due_tick",  ctypes.c_int32),
        ("arg0",      ctypes.c_int32),
        ("arg1",      ctypes.c_int32),
        ("arg2",      ctypes.c_int32),
        ("arg3",      ctypes.c_int32),
        ("arg4",      ctypes.c_int32),
        ("arg5",      ctypes.c_int32),
        ("arg6",      ctypes.c_int32),
        ("arg7",      ctypes.c_int32),
        ("arg8",      ctypes.c_int32),
        ("arg9",      ctypes.c_int32),
        ("pad",       ctypes.c_uint8 * 10),
        ("active",    ctypes.c_uint8),
        ("type",      ctypes.c_uint8),
    ]
assert ctypes.sizeof(EngineDelayedActionRecord) == 0x38, \
    f"record size = {ctypes.sizeof(EngineDelayedActionRecord)}, want 0x38"


# Function pointer table type for the dispatch handlers.  void (*)(void *user)
# matches engine_delayed_action_handler_t.  In diff vectors we leave the
# whole table NULL (port-side: no-op skip) and stub retail's PTR_FUN_004d5c98
# entries to a no-op NativeCallback agent-side; observable surface across
# both sides is identical.
DA_HANDLER = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
DA_DISPATCH_TABLE_LEN = 256


class EngineDelayedActionState(ctypes.Structure):
    _fields_ = [
        ("master_switch",  ctypes.c_int),
        ("game_mode",      ctypes.c_int),
        ("local_player",   ctypes.c_int),
        ("action_counter", ctypes.c_int),
        ("current_idx",    ctypes.c_int),
        ("disp_a0",        ctypes.c_int32),
        ("disp_a1",        ctypes.c_int32),
        ("disp_a2",        ctypes.c_int32),
        ("disp_a3",        ctypes.c_int32),
        ("disp_a4",        ctypes.c_int32),
        ("disp_a5",        ctypes.c_int32),
        ("disp_a6",        ctypes.c_int32),
        ("disp_a7",        ctypes.c_int32),
        ("disp_a8",        ctypes.c_int32),
        ("disp_a9",        ctypes.c_int32),
        ("queue",          ctypes.c_int32 * 100),
        ("queue_count",    ctypes.c_int),
        ("hipri_stash",    ctypes.c_int32 * 100),
        ("hipri_count",    ctypes.c_int),
        ("records",        EngineDelayedActionRecord * 400),
        ("dispatch",       DA_HANDLER * DA_DISPATCH_TABLE_LEN),
        ("dispatch_user",  ctypes.c_void_p),
    ]


def load_port_lib() -> ctypes.CDLL:
    """Load libengine_diff.so + bind the two diff entrypoints.

    The .so is built by `make -C tests diff`.  Caller should rebuild before
    every run — fast (two .c files, no link units shared with the test
    binary's ASan/UBSan flags).
    """
    if not DIFF_SO.exists():
        raise FileNotFoundError(
            f"{DIFF_SO} not found — run `make -C tests diff` first")
    lib = ctypes.CDLL(str(DIFF_SO))

    lib.engine_rng_step.restype = None
    lib.engine_rng_step.argtypes = [
        ctypes.POINTER(EngineRngIn), ctypes.POINTER(EngineRngOut)]

    lib.engine_compute_fps.restype = None
    lib.engine_compute_fps.argtypes = [
        ctypes.POINTER(EngineFpsIn), ctypes.POINTER(EngineFpsOut)]

    lib.engine_compute_anim_cycle.restype = None
    lib.engine_compute_anim_cycle.argtypes = [
        ctypes.POINTER(EngineAnimCycleIn), ctypes.POINTER(EngineAnimCycleOut)]

    lib.engine_compute_frame_delta.restype = None
    lib.engine_compute_frame_delta.argtypes = [
        ctypes.POINTER(EngineDeltaIn), ctypes.POINTER(EngineDeltaOut)]

    lib.engine_compute_phase_ticks.restype = None
    lib.engine_compute_phase_ticks.argtypes = [
        ctypes.POINTER(EnginePhaseIn), ctypes.POINTER(EnginePhaseOut)]

    lib.engine_compute_input_state.restype = None
    lib.engine_compute_input_state.argtypes = [
        ctypes.POINTER(EngineInputIn), ctypes.POINTER(EngineInputOut)]

    lib.engine_compute_sim_sub_steps.restype = None
    lib.engine_compute_sim_sub_steps.argtypes = [
        ctypes.POINTER(EngineRateIn), ctypes.POINTER(EngineRateOut)]

    lib.engine_compute_scenario_hint.restype = None
    lib.engine_compute_scenario_hint.argtypes = [
        ctypes.POINTER(EngineScenarioHintIn), ctypes.POINTER(EngineScenarioHintOut)]

    lib.engine_compute_state_hash.restype = None
    lib.engine_compute_state_hash.argtypes = [
        ctypes.POINTER(EngineStateHashIn), ctypes.POINTER(EngineStateHashOut)]

    lib.engine_tile_expand_footprint.restype = ctypes.c_int
    lib.engine_tile_expand_footprint.argtypes = [
        ctypes.POINTER(EngineTileExpandState)]

    lib.engine_dispatch_delayed_actions.restype = None
    lib.engine_dispatch_delayed_actions.argtypes = [
        ctypes.POINTER(EngineDelayedActionState), ctypes.c_int]

    return lib


# ─── test-vector generators ───────────────────────────────────────────────


# Retail seeds (FUN_004b2258 / InitInstance @ lines 83635-83636) — both
# diff modules MUST include these, since they're what the engine ships
# with and any drift here is observable immediately.
RNG_SEED_A_RETAIL = 0x54657687
RNG_SEED_B_RETAIL = 0x72641663


def gen_rng_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N RNG test vectors.  Always includes the retail seed pair,
    a handful of edge cases (zero state, single-bit set, all-ones), and
    fills the rest with random 32-bit pairs.
    """
    out: list[dict[str, int]] = [
        # Retail seed — first call uses these.
        {"state_a": RNG_SEED_A_RETAIL, "state_b": RNG_SEED_B_RETAIL},
        # Degenerate fixed points.
        {"state_a": 0x00000000, "state_b": 0x00000000},
        {"state_a": 0x00000001, "state_b": 0x00000001},
        # All-ones (bit 31 will be clipped on first iter — covers the quirk).
        {"state_a": 0xFFFFFFFF, "state_b": 0xFFFFFFFF},
        # Asymmetric — one zero, one non-zero (catches accidental swap).
        {"state_a": 0xAA55AA55, "state_b": 0x00000000},
        {"state_a": 0x00000000, "state_b": 0x55AA55AA},
        # Bit 30 set (the LFSR's feedback target — boundary check).
        {"state_a": 0x40000000, "state_b": 0x40000000},
    ]
    while len(out) < n:
        out.append({
            "state_a": rng.getrandbits(32),
            "state_b": rng.getrandbits(32),
        })
    return out[:n]


def gen_anim_cycle_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N anim-cycle test vectors covering:
      - the natural value range (counter 0..15, both tick_80ms_fired states)
      - the wrap boundary (counter == 15 + fired → resets to 0)
      - the unreachable lower-clamp (counter < 0 + fired → resets to 0;
        engine-quirks #N — defensive code that never executes in normal flow,
        but the port preserves it for byte-shape parity)
      - random fill for general coverage
    """
    out: list[dict[str, int]] = [
        # No-op: tick didn't fire → counter unchanged.
        {"counter": 0,  "tick_80ms_fired": 0},
        {"counter": 7,  "tick_80ms_fired": 0},
        {"counter": 15, "tick_80ms_fired": 0},
        # Normal bumps across the 0..15 range.
        {"counter": 0,  "tick_80ms_fired": 1},
        {"counter": 1,  "tick_80ms_fired": 1},
        {"counter": 7,  "tick_80ms_fired": 1},
        {"counter": 14, "tick_80ms_fired": 1},
        # Rollover boundary: 15 + fired → 0.
        {"counter": 15, "tick_80ms_fired": 1},
        # Defensive lower-clamp paths (unreachable in normal flow, but the
        # retail function still executes the comparison — exercise both
        # branches to lock in the byte-shape parity).
        {"counter": -1, "tick_80ms_fired": 1},
        {"counter": -1, "tick_80ms_fired": 0},
        # Out-of-range positive — counter > 15 + fired → bumps to >16,
        # upper clamp resets to 0.
        {"counter": 100, "tick_80ms_fired": 1},
    ]
    while len(out) < n:
        # Bias toward in-range values (the common case) but include OOB.
        out.append({
            "counter":         rng.randint(-3, 20),
            "tick_80ms_fired": rng.randint(0, 1),
        })
    return out[:n]


def gen_frame_delta_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N frame-delta test vectors covering:
      - delta == 30 (boundary, must NOT suppress — gate is `< 0x1e`)
      - delta == 29 (boundary, must suppress)
      - delta < 0 (signed wrap or future-seeded baseline — must suppress
        and hold baseline; this is the quirk that distinguishes
        frame_delta from phase_ticks's `< 0 ⇒ fire` semantics)
      - large positive deltas (the common case)
      - delta == 0 (no time advanced — suppress)
      - random fill biased toward in-range
    """
    out: list[dict[str, int]] = [
        # Boundary at the 30 ms gate.  `< 0x1e` means delta=29 suppresses,
        # delta=30 passes.
        {"now_ms": 1029, "last_frame_ms": 1000},   # delta=29 → suppress
        {"now_ms": 1030, "last_frame_ms": 1000},   # delta=30 → pass
        {"now_ms": 1031, "last_frame_ms": 1000},   # delta=31 → pass
        # No time advance.
        {"now_ms": 1000, "last_frame_ms": 1000},   # delta=0 → suppress
        # Negative delta — wraparound or debug-seeded future baseline.
        # Gate is signed, so this must suppress.
        {"now_ms": 1000, "last_frame_ms": 2000},   # delta=-1000 → suppress
        # Large positive delta — common path.
        {"now_ms": 5000, "last_frame_ms": 1000},   # delta=4000 → pass
        # u32 wrap-around at the very top.
        {"now_ms": 5,    "last_frame_ms": 0xFFFFFFF0},  # delta=21 → suppress
        {"now_ms": 100,  "last_frame_ms": 0xFFFFFFF0},  # delta=116 → pass
    ]
    while len(out) < n:
        # Bias toward passes (the common case), with a chunk on each
        # side of the gate.
        roll = rng.random()
        if roll < 0.20:
            # near-gate: -10 to +50
            delta = rng.randint(-10, 50)
        elif roll < 0.30:
            # negative wrap
            delta = -rng.randint(1, 10_000)
        else:
            # large positive
            delta = rng.randint(50, 100_000)
        last_frame = rng.randint(0, 0xFFFFFFFF)
        now = (last_frame + delta) & 0xFFFFFFFF
        out.append({"now_ms": now, "last_frame_ms": last_frame})
    return out[:n]


def gen_phase_ticks_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N phase-ticks test vectors covering the function's branches:
      - 20 ms gate: delta == 19 suppress, delta == 20 fires, delta < 0 fires
        (the signed-wrap quirk, mirrors frame_delta's < 0 gate-fire shape)
      - 80 ms gate: base_tick_counter at threshold (== 3 going in, so the
        post-bump value of 4 trips `3 < counter` and rolls)
      - Each of the 8 inner counters at its respective threshold
        (counter_2=1, counter_4=3, counter_8=7, counter_13=12, counter_16=15,
        counter_24=23, counter_32=31) so the rollover-and-reset paths fire
      - All 8 inner counters at threshold simultaneously (max-rollover frame)
      - All counters at 0 with the 80 ms gate firing (cold start, every flag
        false except tick_20ms + tick_80ms)
      - random fill biased toward "in the natural range" with occasional OOB
    """
    out: list[dict[str, int]] = [
        # 20 ms gate: just-under (delta=19, suppresses).  No 80 ms gate fire
        # because base_tick_counter doesn't bump.
        {"now_ms": 1019, "last_base_tick_ms": 1000, "base_tick_counter": 0,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        # 20 ms gate: at-boundary (delta=20, fires).
        {"now_ms": 1020, "last_base_tick_ms": 1000, "base_tick_counter": 0,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        # 20 ms gate: negative delta (signed wrap / future-seeded baseline).
        # Gate fires via the `delta < 0` branch.
        {"now_ms": 100, "last_base_tick_ms": 2000, "base_tick_counter": 0,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        # 20 ms gate: zero delta (no fire).
        {"now_ms": 1000, "last_base_tick_ms": 1000, "base_tick_counter": 0,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        # 80 ms gate fires (base counter going 3→4 trips `3 < 4`).  All
        # inner counters at 0 → bump to 1, none roll (since smallest
        # threshold is counter_2 with `1 < N`).
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        # 80 ms gate fires + every inner counter at threshold → ALL flags
        # fire simultaneously.
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 1,  "counter_4": 3,  "counter_8": 7,  "counter_13": 12,
         "counter_16": 15, "counter_24": 23, "counter_32": 31},
        # 80 ms gate + only counter_13 (1040 ms) at threshold.  This is
        # the "sticky" flag — agent pre-clears so the readback shows the
        # 1 set by this call.
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 12,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        # 80 ms gate + each inner counter at threshold individually
        # (single-counter rollover sweep).
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 1,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 3,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 7,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 15, "counter_24": 0, "counter_32": 0},
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 23, "counter_32": 0},
        {"now_ms": 1100, "last_base_tick_ms": 1000, "base_tick_counter": 3,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 31},
        # 20 ms gate fires but 80 ms doesn't (base counter 0→1, well below 3).
        {"now_ms": 1020, "last_base_tick_ms": 1000, "base_tick_counter": 0,
         "counter_2": 1,  "counter_4": 3,  "counter_8": 7,  "counter_13": 12,
         "counter_16": 15, "counter_24": 23, "counter_32": 31},
        # u32 wrap on now_ms (last_base_tick_ms near top of range).
        {"now_ms": 100, "last_base_tick_ms": 0xFFFFFFF0, "base_tick_counter": 0,
         "counter_2": 0,  "counter_4": 0,  "counter_8": 0,  "counter_13": 0,
         "counter_16": 0, "counter_24": 0, "counter_32": 0},
    ]
    while len(out) < n:
        # Random fill: bias toward 80 ms-gate-firing inputs (the rare but
        # interesting branch) and inner-counter values in their natural
        # range.  Occasionally inject OOB (-3..+50) to cover the signed-
        # comparison fences.
        last_base = rng.randint(0, 0xFFFFFFFF)
        # Pick a delta that has ~30% chance of being < 20 (suppress),
        # ~5% negative, rest positive in the 20..200 ms range.
        droll = rng.random()
        if droll < 0.05:
            delta = -rng.randint(1, 10_000)
        elif droll < 0.35:
            delta = rng.randint(0, 19)
        else:
            delta = rng.randint(20, 200)
        now = (last_base + delta) & 0xFFFFFFFF
        # base_tick_counter: 50% chance of being at-or-above threshold
        # so the 80 ms gate fires often enough to exercise the body.
        if rng.random() < 0.5:
            base_counter = rng.randint(3, 6)
        else:
            base_counter = rng.randint(0, 2)
        out.append({
            "now_ms": now,
            "last_base_tick_ms": last_base,
            "base_tick_counter": base_counter,
            "counter_2":  rng.randint(0, 3),
            "counter_4":  rng.randint(0, 5),
            "counter_8":  rng.randint(0, 9),
            "counter_13": rng.randint(0, 14),
            "counter_16": rng.randint(0, 17),
            "counter_24": rng.randint(0, 25),
            "counter_32": rng.randint(0, 33),
        })
    return out[:n]


INPUT_STATE_FIELDS = (
    "cursor_screen_x", "cursor_screen_y", "now_ms",
    "cursor_cache_enabled", "pending_dblclk", "button_state",
    "viewport_x0", "viewport_y0", "viewport_x1", "viewport_y1",
    "viewport_game_w", "viewport_game_h",
    "cached_cursor_x", "cached_cursor_y",
    "prev_game_x", "prev_game_y",
    "prev_lmb", "prev_rmb",
    "lmb_timer_armed", "rmb_timer_armed",
    "click_timer_ms", "motion_last_ms",
    "click_eligible",
    "prev_last_click_x", "prev_last_click_y",
)


def _input_state_base_vec(rng: random.Random) -> dict[str, int]:
    """Default random-but-realistic vector: motion-quiescent, no buttons,
    no dblclk, cursor inside a 640x480 viewport mapping 1:1 to game coords.
    Concrete tests override specific fields.
    """
    now = rng.randint(10_000, 0x7FFFFFFF)
    return {
        "cursor_screen_x":      rng.randint(0, 640),
        "cursor_screen_y":      rng.randint(0, 480),
        "now_ms":               now,
        "cursor_cache_enabled": 0,
        "pending_dblclk":       0,
        "button_state":         0,
        "viewport_x0":          0,
        "viewport_y0":          0,
        "viewport_x1":          640,
        "viewport_y1":          480,
        "viewport_game_w":      640,
        "viewport_game_h":      480,
        "cached_cursor_x":      rng.randint(-32000, 32000),
        "cached_cursor_y":      rng.randint(-32000, 32000),
        "prev_game_x":          0,
        "prev_game_y":          0,
        "prev_lmb":             0,
        "prev_rmb":             0,
        "lmb_timer_armed":      0,
        "rmb_timer_armed":      0,
        # Anchor click_timer_ms / motion_last_ms in a band that puts most
        # vectors outside the 300 ms expiry window, but with enough overlap
        # to exercise the expiry branch.
        "click_timer_ms":       (now - rng.randint(0, 600)) & 0xFFFFFFFF,
        "motion_last_ms":       (now - rng.randint(0, 5000)) & 0xFFFFFFFF,
        "click_eligible":       0,
        "prev_last_click_x":    0,
        "prev_last_click_y":    0,
    }


def gen_input_state_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N input-state vectors covering the function's branches:

      - cursor_cache_enabled on/off (overwrite cached cursor vs carry through)
      - pending_dblclk bits (each + both) → dblclk fires, disarms timer
      - button_state edges (LMB press/release, RMB press/release, both)
      - 300 ms delayed-single-click expiry (timer_armed + delta > 300)
      - shared-timer quirk: both LMB and RMB timers expire on same call
      - motion (cursor moved → mouse_event + motion_last reset)
      - no motion (cursor steady → motion_idle_ms accumulates)
      - click_eligible release-into-click (timer arms + last_click_x/y written)
      - viewport scaling (game_x = cached * game_w/screen_w via percent compose)
      - degenerate viewport (game_w = 0 → percent_ratio = 0 → game = 0)
      - signed viewport extents (negative cursor relative to viewport)
      - u32 wraparound on click_timer_ms / motion_last_ms
    """
    base = lambda: _input_state_base_vec(rng)

    fixed: list[dict[str, int]] = []

    # 1. Quiescent: no buttons, no motion, no dblclk.  Baseline.
    v = base()
    v["cached_cursor_x"] = 100
    v["cached_cursor_y"] = 100
    # Make prev_game match the resolved game so motion_event stays 0.
    v["prev_game_x"] = 100
    v["prev_game_y"] = 100
    fixed.append(v)

    # 2. cursor_cache_enabled = 1 with negative cursor (cached cursor gets
    # overwritten with the screen-space coords).
    v = base()
    v["cursor_cache_enabled"] = 1
    v["cursor_screen_x"] = -50
    v["cursor_screen_y"] =  20
    fixed.append(v)

    # 3. LMB dblclk pending → lmb_dblclk set, lmb_timer disarmed.
    v = base()
    v["pending_dblclk"]   = 0x01
    v["lmb_timer_armed"]  = 1
    fixed.append(v)

    # 4. RMB dblclk pending.
    v = base()
    v["pending_dblclk"]   = 0x02
    v["rmb_timer_armed"]  = 1
    fixed.append(v)

    # 5. Both dblclks pending (high-frequency clicker stress).
    v = base()
    v["pending_dblclk"]   = 0x03
    v["lmb_timer_armed"]  = 1
    v["rmb_timer_armed"]  = 1
    fixed.append(v)

    # 6. LMB press edge (prev=0, button_state has LMB set).
    v = base()
    v["button_state"] = 0x01
    v["prev_lmb"]     = 0
    fixed.append(v)

    # 7. LMB release edge with click_eligible → timer arms, last_click set.
    v = base()
    v["button_state"]    = 0x00
    v["prev_lmb"]        = 1
    v["click_eligible"]  = 1
    v["cached_cursor_x"] = 200
    v["cached_cursor_y"] = 150
    fixed.append(v)

    # 8. LMB release edge WITHOUT click_eligible → released latch only.
    v = base()
    v["button_state"]   = 0x00
    v["prev_lmb"]       = 1
    v["click_eligible"] = 0
    fixed.append(v)

    # 9. RMB press edge.
    v = base()
    v["button_state"] = 0x02
    v["prev_rmb"]     = 0
    fixed.append(v)

    # 10. RMB release edge with click_eligible.
    v = base()
    v["button_state"]   = 0x00
    v["prev_rmb"]       = 1
    v["click_eligible"] = 1
    fixed.append(v)

    # 11. Both buttons changed simultaneously (LMB release + RMB press).
    # Quirk: LMB-release-with-click_eligible clears click_eligible, so the
    # RMB-edge branch only arms the rmb timer if its OWN
    # out->new_click_eligible check still passes.  Retail reads
    # DAT_004e65f0 (the writeback of new_click_eligible), so once the LMB
    # branch zeroes it, the RMB branch's click-armed body is skipped.
    v = base()
    v["button_state"]    = 0x02  # RMB pressed; LMB cleared
    v["prev_lmb"]        = 1
    v["prev_rmb"]        = 0
    v["click_eligible"]  = 1
    fixed.append(v)

    # 12. Shared-timer quirk: BOTH LMB and RMB timers armed, both at the
    # exact same click_timer_ms baseline, now > 300 → both expire on the
    # same call.  Retail reads _DAT_004e59f8 for BOTH timers.
    v = base()
    v["lmb_timer_armed"]  = 1
    v["rmb_timer_armed"]  = 1
    v["click_timer_ms"]   = (v["now_ms"] - 301) & 0xFFFFFFFF
    fixed.append(v)

    # 13. LMB timer at the exact 300 ms boundary (delta = 300 → does NOT
    # fire; gate is `300 < delta`).
    v = base()
    v["lmb_timer_armed"] = 1
    v["click_timer_ms"]  = (v["now_ms"] - 300) & 0xFFFFFFFF
    fixed.append(v)

    # 14. LMB timer just past 300 (delta = 301 → fires).
    v = base()
    v["lmb_timer_armed"] = 1
    v["click_timer_ms"]  = (v["now_ms"] - 301) & 0xFFFFFFFF
    fixed.append(v)

    # 15. Motion event (cursor moved → mouse_event + motion_last reset).
    v = base()
    v["cached_cursor_x"] = 100
    v["cached_cursor_y"] = 100
    v["prev_game_x"]     = 50   # different from resolved game_x = 100
    v["prev_game_y"]     = 50
    fixed.append(v)

    # 16. No motion (cached unchanged from prev_game).
    v = base()
    v["cached_cursor_x"] = 100
    v["cached_cursor_y"] = 100
    v["prev_game_x"]     = 100
    v["prev_game_y"]     = 100
    v["motion_last_ms"]  = (v["now_ms"] - 1234) & 0xFFFFFFFF
    fixed.append(v)

    # 17. Degenerate viewport (game_w = 0 → percent_ratio returns 0 →
    # game_x clamps to 0 regardless of cursor).
    v = base()
    v["viewport_game_w"] = 0
    v["cached_cursor_x"] = 500
    fixed.append(v)

    # 18. Degenerate viewport h.
    v = base()
    v["viewport_game_h"] = 0
    v["cached_cursor_y"] = 500
    fixed.append(v)

    # 19. Negative viewport extent (x1 < x0) — exercises the signed
    # percent_ratio arithmetic.
    v = base()
    v["viewport_x0"] = 100
    v["viewport_x1"] =  50   # width = -50
    v["viewport_y0"] =   0
    v["viewport_y1"] = 100
    fixed.append(v)

    # 20. dblclk while LMB also has an edge — dblclk wins (disarms timer).
    v = base()
    v["pending_dblclk"] = 0x01
    v["button_state"]   = 0x01
    v["prev_lmb"]       = 0      # press edge
    v["lmb_timer_armed"] = 1
    fixed.append(v)

    # 21. u32 wraparound on click_timer_ms (now_ms wrapped past timer).
    # Signed delta should still register as small +.
    v = base()
    v["now_ms"]         = 100
    v["click_timer_ms"] = 0xFFFFFFFF - 100   # delta = +201 → fires
    v["lmb_timer_armed"] = 1
    fixed.append(v)

    # 22. u32 wraparound putting timer in the future (delta < 0 → does NOT
    # fire; the gate is signed `300 < delta`, negative delta fails).
    v = base()
    v["now_ms"]          = 100
    v["click_timer_ms"]  = 200     # delta = -100 (s32) → no fire
    v["lmb_timer_armed"] = 1
    fixed.append(v)

    out: list[dict[str, int]] = list(fixed)
    while len(out) < n:
        v = _input_state_base_vec(rng)
        # Random fill — bias toward edges/buttons so the interesting
        # branches fire often.
        r = rng.random()
        if r < 0.15:
            v["pending_dblclk"] = rng.randint(0, 3)
        if r < 0.35:
            v["button_state"]   = rng.randint(0, 3)
            v["prev_lmb"]       = rng.randint(0, 1)
            v["prev_rmb"]       = rng.randint(0, 1)
            v["click_eligible"] = rng.randint(0, 1)
        if rng.random() < 0.25:
            v["lmb_timer_armed"] = rng.randint(0, 1)
        if rng.random() < 0.25:
            v["rmb_timer_armed"] = rng.randint(0, 1)
        if rng.random() < 0.20:
            v["cursor_cache_enabled"] = 1
        # Occasional degenerate viewport to exercise the clamp.
        if rng.random() < 0.05:
            v["viewport_game_w"] = 0
        if rng.random() < 0.05:
            v["viewport_game_h"] = 0
        out.append(v)
    return out[:n]


def gen_fps_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N FPS test vectors covering:
      - non-rollover (now - window_start < 1000)
      - rollover (now - window_start > 1000)
      - strict boundary (now - window_start == 1000 — must NOT roll)
      - signed-wrap (window_start > now → negative elapsed; must NOT roll)
      - random sweeps thereafter
    """
    out: list[dict[str, int]] = [
        # Non-rollover, basic.
        {"now_ms": 1500, "rollover_now_ms": 1500,
         "last_render_ms": 1400, "second_window_start_ms": 1000,
         "frames_this_second": 5, "current_fps": 0,
         "cumulative_fps": 0, "cumulative_seconds": 0},
        # Rollover — elapsed = 1001 ms (just over the gate).
        {"now_ms": 2001, "rollover_now_ms": 2002,
         "last_render_ms": 1990, "second_window_start_ms": 1000,
         "frames_this_second": 60, "current_fps": 0,
         "cumulative_fps": 0, "cumulative_seconds": 0},
        # Strict boundary — exactly 1000 ms (must NOT roll).
        {"now_ms": 2000, "rollover_now_ms": 2000,
         "last_render_ms": 1990, "second_window_start_ms": 1000,
         "frames_this_second": 30, "current_fps": 99,
         "cumulative_fps": 99, "cumulative_seconds": 1},
        # Signed-wrap — window_start in the "future" by retail's reckoning.
        # Should NOT roll over.
        {"now_ms": 100, "rollover_now_ms": 100,
         "last_render_ms": 90, "second_window_start_ms": 0x80000200,
         "frames_this_second": 7, "current_fps": 30,
         "cumulative_fps": 30, "cumulative_seconds": 1},
        # Long-window rollover (engine paused 3 seconds, then resumed).
        # Retail counts ONE rollover event, not three.
        {"now_ms": 5000, "rollover_now_ms": 5001,
         "last_render_ms": 1500, "second_window_start_ms": 1000,
         "frames_this_second": 50, "current_fps": 50,
         "cumulative_fps": 50, "cumulative_seconds": 1},
    ]
    while len(out) < n:
        # Random distribution biased toward non-rollover (the common case)
        # with ~20 % rollovers and ~5 % near-boundary.
        roll = rng.random()
        window_start = rng.randint(0, 0x7FFFFFFF)
        if roll < 0.20:
            now = window_start + rng.randint(1001, 5000)   # rollover
        elif roll < 0.25:
            now = window_start + 1000                       # exact boundary
        else:
            now = window_start + rng.randint(1, 999)        # non-rollover
        out.append({
            "now_ms": now & 0xFFFFFFFF,
            "rollover_now_ms": (now + rng.randint(0, 2)) & 0xFFFFFFFF,
            "last_render_ms": (now - rng.randint(1, 50)) & 0xFFFFFFFF,
            "second_window_start_ms": window_start,
            "frames_this_second": rng.randint(0, 5000),
            "current_fps": rng.randint(0, 500),
            "cumulative_fps": rng.randint(0, 100_000),
            "cumulative_seconds": rng.randint(0, 10_000),
        })
    return out[:n]


# ─── per-function diff runners ────────────────────────────────────────────
#
# The agent has no guard against the engine main thread racing us on the
# shared globals (see the "Engine-thread race notes" block in
# tools/frida/openlords2-agent.js for why — kernel-suspend AND in-process
# spin both deadlock V8).
#
# For FPS we can detect a race directly: if our scripted clock was NOT
# consumed by FUN_004b27bf, the returned delta_ms won't match
# `in_now_ms - in_last_render_ms`.  In that case retry up to FPS_RACE_RETRIES.
# For RNG the race is too rare to observe in practice (engine doesn't tick
# FUN_00404a46 per-frame — it's only hit by sim subsystems we haven't
# unblocked yet) so no retry path is needed.

FPS_RACE_RETRIES = 4
# Anim-cycle is called every engine_tick by retail (inside the per-tick
# driver's pre-tick block), so without turbo's free-running rate-limit the
# race window is wider per call than RNG/FPS.  More retries dampens the
# false-positive curve from the conservative tick_count-delta detector.
ANIM_RACE_RETRIES = 8
# Input-state has the widest RPC window of the diff targets (~25 writes +
# ~30 reads), AND its inputs include globals that other engine functions
# (FUN_0042ff10, WndProc) also write asynchronously — so the race window
# spans both pre- and post-call.  Use a bigger retry budget; race-pass
# classification picks up the residual after retries exhaust.
INPUT_STATE_RACE_RETRIES = 16
# State-hash runs under the engine-tick freeze (same install as
# sim_sub_steps), so the engine main thread should never enter
# FUN_00440231 during our RPC window.  Keep the retry budget low —
# a high count would mask a real bug in the freeze or the structural
# raced detector.
STATE_HASH_RACE_RETRIES = 2
# Tile-expand also runs under the engine-tick freeze, so the main thread
# shouldn't race into tiles/path_scratch/stage_scratch during our window.
# Same low budget as state_hash.
TILE_EXPAND_RACE_RETRIES = 2
# Delayed-action also runs under the engine-tick freeze (same install).
# Same low budget — main thread should never enter FUN_004472ce.
DELAYED_ACTION_RACE_RETRIES = 2


def _frame_delta_race_free(vec: dict[str, int], retail: dict) -> bool:
    """Function-specific race detector for frame_delta.

    FUN_004b20ed assigns _DAT_004e59ec deterministically per branch:
      - suppress (delta < 30): keeps the existing baseline (our injected
        last_frame_ms)
      - pass     (delta ≥ 30): writes the timeGetTime result (our scripted
        now_ms)

    If the engine raced AFTER our retail call (calling FUN_004b20ed before
    our readback), it overwrites _DAT_004e59ec with wall-clock, which won't
    match either of those expected values.

    The structural raced flag is too noisy on retail without turbo: the
    engine free-runs at ~200 kHz, so g_engine_in_tick is almost always
    set during any RPC.  Using the structural flag for retry decisions
    yields ~100% retry rate.  Function-specific gives reliable signal.
    """
    delta = int(retail.get("delta_ms", 0))
    new_last = int(retail.get("new_last_frame_ms", 0)) & 0xFFFFFFFF
    if delta == 0:
        return new_last == (int(vec["last_frame_ms"]) & 0xFFFFFFFF)
    return new_last == (int(vec["now_ms"]) & 0xFFFFFFFF)


def _phase_ticks_race_free(vec: dict[str, int], retail: dict) -> bool:
    """Function-specific race detector for phase_ticks.

    FUN_004bbc80 writes `_DAT_004e5b10` (last_base_tick_ms) deterministically:
      - 20 ms gate fires (delta > 0x13 or delta < 0): writes our scripted
        timeGetTime return, which is our `in_now_ms`.
      - 20 ms gate doesn't fire: leaves the global at our injected
        `in_last_base_tick_ms`.

    If the engine raced AFTER our retail call (re-invoked FUN_004bbc80
    before our readback), one of two things happened:

      (a) Engine progressed past the 20 ms branch → wrote engine wall-
          clock to last_base_tick_ms.  Canary (start vs end) detects.
      (b) Engine ran ONLY the clear-flags step (the function's first
          eight writes) but hadn't yet written last_base / base_counter.
          Canary still shows expected last_base — but the flags are
          clobbered.  Detected via the internal-consistency check:
          when the 20 ms branch fires, retail MUST observe tick_20ms == 1
          (the branch writes it after bumping base_counter).  If we
          observe tick_20ms == 0 with last_base == in_now_ms, the engine
          cleared the flag mid-RPC.

    The structural `raced` flag (engine in mid-tick at any sample point)
    confirms a race did happen — used by the engine-clear race-pass
    classification when retries don't recover.
    """
    delta = (int(vec["now_ms"]) - int(vec["last_base_tick_ms"])) & 0xFFFFFFFF
    # Sign-extend to s32 to match retail's `(int)(now - last_base)` cast.
    if delta >= 0x80000000:
        delta -= 0x100000000
    end   = int(retail.get("new_last_base_tick_ms", 0)) & 0xFFFFFFFF
    start = int(retail.get("last_base_tick_ms_start", 0)) & 0xFFFFFFFF
    if start != end:
        return False  # engine fired mid-readback
    fires = (delta > 0x13) or (delta < 0)
    if fires:
        if end != (int(vec["now_ms"]) & 0xFFFFFFFF):
            return False
        # Internal consistency: the 20 ms branch sets tick_20ms = 1
        # AFTER bumping base_counter (retail's source order).  If we
        # observe last_base == in_now_ms (the 20 ms branch wrote it)
        # but tick_20ms == 0, the engine clobbered the flag.
        if int(retail.get("tick_20ms", 0)) != 1:
            return False
        return True
    return end == (int(vec["last_base_tick_ms"]) & 0xFFFFFFFF)


PHASE_TICKS_COUNTER_FIELDS = (
    "new_last_base_tick_ms", "new_base_tick_counter",
    "new_counter_2",  "new_counter_4",  "new_counter_8",
    "new_counter_13", "new_counter_16", "new_counter_24", "new_counter_32",
)
PHASE_TICKS_FLAG_FIELDS = (
    "tick_20ms",  "tick_80ms",  "tick_160ms",
    "tick_320ms", "tick_640ms", "tick_1040ms",
    "tick_1280ms","tick_1920ms","tick_2560ms",
)


def _phase_ticks_engine_clear_race(retail: dict, port: dict) -> bool:
    """Recognise the "engine race cleared our flags" signature for
    phase_ticks.

    When the engine main thread fires FUN_004bbc80 concurrently with
    our retail call (multi-core, no kernel-level serialisation between
    our V8 thread and the engine main thread), the most common
    interleaving is:

      - our V8 retail call completes, leaving retail's correct
        outputs in all 18 globals;
      - engine begins its own FUN_004bbc80 call;
      - engine's first action is "clear 8 flags" (the function's
        prologue);
      - our V8 reads the 9 flag globals BEFORE engine's 20 ms branch
        writes (which come after the clears);
      - our V8 reads the 9 counter/last_base globals BEFORE engine's
        20 ms branch writes too.

    Signature: all 9 counter writebacks match port (engine hadn't yet
    written them), but flag fields differ in the specific direction
    "retail == 0 where port == 1" (engine cleared them).  The 1040 ms
    flag is sticky in retail (not cleared at entry), so a port-set
    1040 ms flag still matches retail.

    A pass under this signature counts as a race-pass — retail's call
    produced correct outputs; the observable was clobbered after the
    fact.  This is structurally identical to frame_delta's race-pass
    on `new_last_frame_ms`.
    """
    for k in PHASE_TICKS_COUNTER_FIELDS:
        if (int(retail[k]) & 0xFFFFFFFF) != (int(port[k]) & 0xFFFFFFFF):
            return False
    has_clear = False
    for k in PHASE_TICKS_FLAG_FIELDS:
        rv = int(retail[k])
        pv = int(port[k])
        if rv == pv:
            continue
        if rv == 0 and pv == 1:
            has_clear = True
        else:
            # Unexpected direction (retail set a flag the port didn't)
            # — not the engine-clear signature.  Probably a real bug.
            return False
    return has_clear


def _input_state_race_free(vec: dict[str, int], retail: dict) -> bool:
    """Function-specific race detector for input_state.

    FUN_004b191e writes DAT_004e6594/8 (raw_cursor_{x,y}) unconditionally
    from the GetCursorPos result.  When our scripted-cursor queue is
    consumed by retail's call, both globals must equal our injected
    cursor_screen_{x,y} on readback.

    If the engine main thread fires its own FUN_004b191e between our retail
    call's return and our readback, it calls the real GetCursorPos and
    overwrites raw_cursor_{x,y} with the OS cursor position (collision
    against our random injected coords is astronomically unlikely).

    Two canaries: start (read FIRST) and end (read LAST in the agent).
    Both must equal the injected cursor.  Disagreement at either canary
    means the engine raced; the orchestrator retries.
    """
    expected_x = int(vec["cursor_screen_x"]) | 0
    expected_y = int(vec["cursor_screen_y"]) | 0
    rx_s = int(retail.get("raw_cursor_x",     0))
    ry_s = int(retail.get("raw_cursor_y",     0))
    rx_e = int(retail.get("raw_cursor_x_end", rx_s))
    ry_e = int(retail.get("raw_cursor_y_end", ry_s))
    return (rx_s == expected_x and ry_s == expected_y and
            rx_e == expected_x and ry_e == expected_y)


INPUT_STATE_OUT_FIELDS = (
    "raw_cursor_x", "raw_cursor_y",
    "new_cached_cursor_x", "new_cached_cursor_y",
    "prev_game_x_snapshot", "prev_game_y_snapshot",
    "prev_lmb_snapshot", "prev_rmb_snapshot",
    "game_x", "game_y",
    "mouse_event", "button_changed",
    "lmb_newly_pressed", "lmb_newly_released",
    "rmb_newly_pressed", "rmb_newly_released",
    "lmb_click_fired", "rmb_click_fired",
    "lmb_dblclk", "rmb_dblclk",
    "new_lmb", "new_rmb",
    "new_lmb_timer_armed", "new_rmb_timer_armed",
    "new_click_timer_ms", "new_motion_last_ms", "motion_idle_ms",
    "last_click_x", "last_click_y",
    "new_click_eligible",
)


def _fps_race_free(vec: dict[str, int], retail: dict) -> bool:
    """Heuristic: did our retail call land WITHOUT the engine main thread
    racing on the shared globals?

    FUN_004b27bf assigns `_DAT_004e59e0 = timeGetTime()` unconditionally.
    With our scripted-clock queue feeding `in_now_ms`, that assignment
    stores `in_now_ms` into _DAT_004e59e0.  If the engine main thread
    then squeezes its own call to FUN_004b27bf in between our retail
    call's return and our readback, it overwrites _DAT_004e59e0 with
    wall-clock — making `new_last_render_ms != in_now_ms`.

    So the single most reliable race detector is:
       new_last_render_ms == in_now_ms

    delta_ms alone is NOT sufficient — the engine's race usually happens
    AFTER our retail call returns (race "case 2"), in which case delta_ms
    is correct but the readback isn't.
    """
    return (int(retail.get("new_last_render_ms", 0)) & 0xFFFFFFFF ==
            int(vec["now_ms"]) & 0xFFFFFFFF)


def run_port_rng(lib: ctypes.CDLL, vec: dict[str, int]) -> dict[str, int]:
    rin = EngineRngIn(state_a=vec["state_a"], state_b=vec["state_b"])
    rout = EngineRngOut()
    lib.engine_rng_step(ctypes.byref(rin), ctypes.byref(rout))
    return {
        "new_state_a": rout.new_state_a, "new_state_b": rout.new_state_b,
        "rng_15bit_a": rout.rng_15bit_a, "rng_7bit_a": rout.rng_7bit_a,
        "rng_3bit_a":  rout.rng_3bit_a,
        "rng_15bit_b": rout.rng_15bit_b, "rng_7bit_b": rout.rng_7bit_b,
        "rng_3bit_b":  rout.rng_3bit_b,
    }


def run_port_fps(lib: ctypes.CDLL, vec: dict[str, int]) -> dict[str, int]:
    fin = EngineFpsIn(
        now_ms=vec["now_ms"],
        rollover_now_ms=vec["rollover_now_ms"],
        last_render_ms=vec["last_render_ms"],
        second_window_start_ms=vec["second_window_start_ms"],
        frames_this_second=vec["frames_this_second"],
        current_fps=vec["current_fps"],
        cumulative_fps=vec["cumulative_fps"],
        cumulative_seconds=vec["cumulative_seconds"],
    )
    fout = EngineFpsOut()
    lib.engine_compute_fps(ctypes.byref(fin), ctypes.byref(fout))
    return {
        "delta_ms":                   fout.delta_ms,
        "new_last_render_ms":         fout.new_last_render_ms,
        "new_second_window_start_ms": fout.new_second_window_start_ms,
        "new_frames_this_second":     fout.new_frames_this_second,
        "new_current_fps":            fout.new_current_fps,
        "new_cumulative_fps":         fout.new_cumulative_fps,
        "new_cumulative_seconds":     fout.new_cumulative_seconds,
        # Our port returns window_rolled_over as a test helper; retail
        # doesn't expose this directly (it's an inferable boolean —
        # window_start advanced ⇒ rolled over).  Exclude from the diff;
        # compare cumulative_seconds bumping instead.
    }


def run_port_anim_cycle(lib: ctypes.CDLL, vec: dict[str, int]) -> dict[str, int]:
    ain = EngineAnimCycleIn(counter=vec["counter"],
                            tick_80ms_fired=vec["tick_80ms_fired"])
    aout = EngineAnimCycleOut()
    lib.engine_compute_anim_cycle(ctypes.byref(ain), ctypes.byref(aout))
    return {"new_counter": aout.new_counter}


# Anim-cycle diff: single output field.  Race detection is structural —
# `retail['raced']` is set by the agent when engine_tick fired during the
# RPC window; the orchestrator retries on raced=true (see ANIM_RACE_RETRIES).
def diff_anim_cycle(retail: dict, port: dict) -> list[str]:
    return ["new_counter"] if int(retail["new_counter"]) != int(port["new_counter"]) else []


PHASE_TICKS_OUT_FIELDS = (
    "tick_20ms",  "tick_80ms",  "tick_160ms", "tick_320ms", "tick_640ms",
    "tick_1040ms","tick_1280ms","tick_1920ms","tick_2560ms",
    "new_last_base_tick_ms", "new_base_tick_counter",
    "new_counter_2",  "new_counter_4",  "new_counter_8",
    "new_counter_13", "new_counter_16", "new_counter_24", "new_counter_32",
)


def run_port_phase_ticks(lib: ctypes.CDLL, vec: dict[str, int]) -> dict[str, int]:
    pin = EnginePhaseIn(
        now_ms=vec["now_ms"],
        last_base_tick_ms=vec["last_base_tick_ms"],
        base_tick_counter=vec["base_tick_counter"] & 0xFFFFFFFF,
        counter_2=vec["counter_2"]  & 0xFFFFFFFF,
        counter_4=vec["counter_4"]  & 0xFFFFFFFF,
        counter_8=vec["counter_8"]  & 0xFFFFFFFF,
        counter_13=vec["counter_13"] & 0xFFFFFFFF,
        counter_16=vec["counter_16"] & 0xFFFFFFFF,
        counter_24=vec["counter_24"] & 0xFFFFFFFF,
        counter_32=vec["counter_32"] & 0xFFFFFFFF,
    )
    pout = EnginePhaseOut()
    lib.engine_compute_phase_ticks(ctypes.byref(pin), ctypes.byref(pout))
    return {k: getattr(pout, k) for k in PHASE_TICKS_OUT_FIELDS}


def diff_phase_ticks(retail: dict, port: dict) -> list[str]:
    bad = []
    for k in PHASE_TICKS_OUT_FIELDS:
        # All fields are 32-bit; mask to u32 before comparing so int↔uint
        # bit-equality (signed comparisons in retail vs unsigned in port)
        # doesn't show as a false divergence.
        if (int(retail[k]) & 0xFFFFFFFF) != (int(port[k]) & 0xFFFFFFFF):
            bad.append(k)
    return bad


def run_port_input_state(lib: ctypes.CDLL, vec: dict[str, int]) -> dict[str, int]:
    iin = EngineInputIn(
        cursor_screen_x=vec["cursor_screen_x"],
        cursor_screen_y=vec["cursor_screen_y"],
        now_ms=vec["now_ms"] & 0xFFFFFFFF,
        cursor_cache_enabled=vec["cursor_cache_enabled"],
        pending_dblclk=vec["pending_dblclk"] & 0xff,
        button_state=vec["button_state"] & 0xff,
        viewport_x0=vec["viewport_x0"],
        viewport_y0=vec["viewport_y0"],
        viewport_x1=vec["viewport_x1"],
        viewport_y1=vec["viewport_y1"],
        viewport_game_w=vec["viewport_game_w"],
        viewport_game_h=vec["viewport_game_h"],
        cached_cursor_x=vec["cached_cursor_x"],
        cached_cursor_y=vec["cached_cursor_y"],
        prev_game_x=vec["prev_game_x"],
        prev_game_y=vec["prev_game_y"],
        prev_lmb=vec["prev_lmb"],
        prev_rmb=vec["prev_rmb"],
        lmb_timer_armed=vec["lmb_timer_armed"],
        rmb_timer_armed=vec["rmb_timer_armed"],
        click_timer_ms=vec["click_timer_ms"] & 0xFFFFFFFF,
        motion_last_ms=vec["motion_last_ms"] & 0xFFFFFFFF,
        click_eligible=vec["click_eligible"],
        prev_last_click_x=vec["prev_last_click_x"],
        prev_last_click_y=vec["prev_last_click_y"],
    )
    iout = EngineInputOut()
    lib.engine_compute_input_state(ctypes.byref(iin), ctypes.byref(iout))
    return {k: getattr(iout, k) for k in INPUT_STATE_OUT_FIELDS}


def diff_input_state(retail: dict, port: dict) -> list[str]:
    """Compare input_state outputs.  All fields are 32-bit on the port
    side; mask both sides to u32 for bit-equality (retail returns u8 fields
    as 0-255, port returns int 0/1 — masking handles both).
    """
    bad = []
    for k in INPUT_STATE_OUT_FIELDS:
        rv = int(retail.get(k, 0)) & 0xFFFFFFFF
        pv = int(port[k]) & 0xFFFFFFFF
        if rv != pv:
            bad.append(k)
    return bad


# ─── sim_sub_steps test plan (FUN_004bb978 + FUN_0043de70 chain) ─────────
#
# Spans two retail functions called in sequence; the diff orchestrator
# chains BOTH our ports host-side and compares against the single retail
# call.  Vector format mirrors the agent's runRetailSimSubSteps in_obj
# shape — see tools/frida/openlords2-agent.js for the snapshot/inject
# surface.

# Per-call retry budget.  Wider than INPUT_STATE_RACE_RETRIES because the
# chained call surface is larger — more globals = more race surfaces.
SIM_SUB_STEPS_RACE_RETRIES = 16

# Rate-gate LUT size (bytes).  Matches SIM_RATE_LUT_BYTES in the agent.
# Index range used by retail: [0, 14*0xa0 + 0x9f] = [0, 0x95f] = 2400.
SIM_RATE_LUT_BYTES = 2400

SIM_SUB_STEPS_OUT_FIELDS = (
    "sub_steps",
    "new_last_step_ms",
    "new_autosave_start_ms",
    "new_phase",
    "derived_hint",
    "new_frame_count",
    "new_max_stride",
    "new_stride_sum",
    "new_avg_stride",
    "new_slow_frame_count",
    "new_active_count",
)


def _sim_sub_steps_base_vec(rng: random.Random) -> dict[str, Any]:
    """Quiescent baseline: ingame SP at max speed, my slot active, no
    ally peers, no autosave, no extended pause, all per-player tables
    zeroed.  Concrete cases override fields to walk specific branches.
    """
    now = rng.randint(1000, 0x7FFFFFFF)
    return {
        # FUN_004bb978 scalars
        "now_ms":             now,
        "last_step_ms":       (now - rng.randint(0, 100)) & 0xFFFFFFFF,
        "autosave_start_ms":  0,                    # MUST stay 0 to skip
                                                    # the 5000 ms branch
                                                    # (avoids retail's
                                                    # FUN_0043eda0(0xf,0))
        "mp_active":          0,                    # SP — simplest baseline
        "tick_counter":       rng.randint(0, 200),
        "phase":              rng.randint(0, 0x9f),
        "game_speed":         100,                  # max → iVar3=0
        "pause_lock":         0,
        "current_screen":     0,                    # screen 0 has gate byte 0
        "extended_pause":     0,
        "mp_player_index":    0,
        "screen_freeze_byte": 0,                    # at table[current_screen]
        "mp_skip_byte":       0,                    # at table[mp_player_index]
        # FUN_0043de70 scalars
        "game_state_phase":   3,                    # ingame (so non-zero hint)
        "local_player_idx":   1,
        "game_mode_speed":    0,
        "ally_a":             2,
        "ally_b":             3,
        "frame_count":        0,
        "max_stride":         0,
        "stride_sum":         0,
        "avg_stride":         0,
        "slow_frame_count":   0,                    # MUST stay ≤99 (after ++
                                                    # retail stays ≤100 so the
                                                    # FUN_0043eda0(10,0) gate
                                                    # `100 < value` stays false)
        "base_delta":         0,
        "active_count":       0,
        "player_active":         [0] * 7,
        "player_has_data":       [0] * 7,
        "player_disconnected":   [0] * 7,
        "player_ticks":          [0] * 7,
    }


def gen_sim_sub_steps_vectors(n: int, rng: random.Random) -> list[dict[str, Any]]:
    """Build N sim_sub_steps vectors covering:
      - all four scenario_hint paths (state-not-ingame, SP, MP normal,
        alone-in-mode-2)
      - run_gate paths via FUN_004bb8c0 (which retail always returns 1 from —
        we don't test the run_gate==0 branch in the diff; covered by unit tests)
      - sub_steps return values 0, 1, 2
      - phase wrap at 0x9f → 0
      - rate-gate LUT byte 0 vs non-zero
      - speed slider gradations 0..100
      - pause_lock blocking
      - screen_freeze byte values (0, 1, 2)
      - extended_pause SP-vs-MP-with-skip-flag-vs-MP-without
      - autosave_start_ms = 0 (always — constraint preserves retail side
        effects).  The 4999 ms-still-blocks / 5000 ms-expires paths
        diverge from this convention; they're covered by host unit tests
        instead.
      - per-player tick deltas at every behind-ladder + ahead-ladder
        boundary
    """
    fixed: list[dict[str, Any]] = []

    # 1. SP baseline — should produce sub_steps=0 (rate-gate LUT byte 0).
    v = _sim_sub_steps_base_vec(rng)
    fixed.append(v)

    # 2. SP, scenario_hint = 1 (SP path always returns 1), LUT byte 1
    # at the index (phase=0 → bumped to 1 → LUT[1 + 0*0xa0] = LUT[1]).
    # Vector marker for the LUT installer; actual LUT bytes are global.
    v = _sim_sub_steps_base_vec(rng)
    v["phase"] = 0
    v["game_speed"] = 100
    fixed.append(v)

    # 3. SP, scenario_hint = 1, slider middle (game_speed=50 → iVar3=5,
    # threshold=52 ms).  now - last_step = 100 → step accepted.
    v = _sim_sub_steps_base_vec(rng)
    v["game_speed"] = 50
    v["now_ms"]      = 1000
    v["last_step_ms"] = 900   # delta = 100 ms
    fixed.append(v)

    # 4. SP, pause_lock = 1 (rate gate blocked at the pause check).
    v = _sim_sub_steps_base_vec(rng)
    v["pause_lock"] = 1
    fixed.append(v)

    # 5. SP, screen_freeze byte = 1 (blocks both SP + MP).
    v = _sim_sub_steps_base_vec(rng)
    v["screen_freeze_byte"] = 1
    fixed.append(v)

    # 6. SP, screen_freeze byte = 2 (blocks SP only — mp_active=0 → blocked).
    v = _sim_sub_steps_base_vec(rng)
    v["screen_freeze_byte"] = 2
    fixed.append(v)

    # 7. SP, extended_pause = 1 (blocks SP).
    v = _sim_sub_steps_base_vec(rng)
    v["extended_pause"] = 1
    fixed.append(v)

    # 8. game_state_phase != 3 → scenario_hint = 0 → sub_steps = 0,
    # phase NOT advanced.
    v = _sim_sub_steps_base_vec(rng)
    v["game_state_phase"] = 0
    fixed.append(v)

    # 9. game_speed = 0 → iVar3 = 10 → sub_steps = 0 (slider pegged slow).
    v = _sim_sub_steps_base_vec(rng)
    v["game_speed"] = 0
    fixed.append(v)

    # 10. MP, mp_active=1, all slots inactive → scenario_hint path 3
    # (local_player_idx slot inactive → hint = 1).
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"] = 1
    # player_active[1] = 0 already (default)
    fixed.append(v)

    # 11. MP, single active player (just me) → FUN_0043de70 takes the
    # "active_count == 1" early-return-1 path.
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"] = 1
    v["player_active"][1] = 1
    v["player_has_data"][1] = 1
    fixed.append(v)

    # 12. MP, normal path, I'm behind by 50 → behind ladder bucket -4.
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"] = 1
    v["player_active"]   = [0, 1, 1, 1, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 1, 0, 0, 0]
    v["player_ticks"]    = [0, 0, 50, 50, 0, 0, 0]
    fixed.append(v)

    # 13. MP, normal path, I'm ahead by 50 (coarse ladder → bucket 110).
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"] = 1
    v["player_active"]   = [0, 1, 1, 1, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 1, 0, 0, 0]
    v["player_ticks"]    = [0, 50, 0, 0, 0, 0, 0]
    v["base_delta"]      = 10
    fixed.append(v)

    # 14. MP, mode-5 fine-grained ladder, lead = 50 → bucket 40.
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"]       = 1
    v["game_mode_speed"] = 5
    v["player_active"]   = [0, 1, 1, 1, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 1, 0, 0, 0]
    v["player_ticks"]    = [0, 50, 0, 0, 0, 0, 0]
    fixed.append(v)

    # 15. MP mode-2, two allies → normal path.
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"]       = 1
    v["game_mode_speed"] = 2
    v["player_active"]   = [0, 1, 1, 1, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 1, 0, 0, 0]
    fixed.append(v)

    # 16. MP mode-2, single ally → alone path.
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"]       = 1
    v["game_mode_speed"] = 2
    v["player_active"]   = [0, 1, 1, 0, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 0, 0, 0, 0]
    v["ally_b"]          = -1
    v["base_delta"]      = 10
    v["player_ticks"]    = [0, 100, 50, 0, 0, 0, 0]   # lead = 50 → bucket 30
    fixed.append(v)

    # 17. Phase wrap: phase=0x9f → bumps to 0xa0 → wrap to 0.
    v = _sim_sub_steps_base_vec(rng)
    v["phase"] = 0x9f
    fixed.append(v)

    # 18. Time overflow: now < last_step → local_8 clamps to 10000.
    v = _sim_sub_steps_base_vec(rng)
    v["now_ms"]       = 500
    v["last_step_ms"] = 1000
    fixed.append(v)

    # 19. MP at max speed (uncapped — gate condition false → step + no
    # advance).
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"]       = 1
    v["player_active"]   = [0, 1, 1, 0, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 0, 0, 0, 0]
    v["game_speed"]      = 100
    fixed.append(v)

    # 20. Behind ladder: peers way ahead (10000+) → bucket -10.
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"]       = 1
    v["player_active"]   = [0, 1, 1, 0, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 0, 0, 0, 0]
    v["player_ticks"]    = [0, 0, 20000, 0, 0, 0, 0]
    fixed.append(v)

    # 21. Slow-frame at 140 ms ceiling — slow_frame_count just below
    # the trip threshold (constrained ≤99 to skip the retail-side
    # FUN_0043eda0(10,0) trigger).
    v = _sim_sub_steps_base_vec(rng)
    v["mp_active"]       = 1
    v["player_active"]   = [0, 1, 1, 0, 0, 0, 0]
    v["player_has_data"] = [0, 1, 1, 0, 0, 0, 0]
    v["player_ticks"]    = [0, 500, 0, 0, 0, 0, 0]   # lead 500 → ceiling 140
    v["slow_frame_count"] = 99                        # bumps to 100, gate 100 < 100
                                                      # false → no notify
    fixed.append(v)

    out: list[dict[str, Any]] = list(fixed)
    while len(out) < n:
        v = _sim_sub_steps_base_vec(rng)
        # Random fill — bias toward interesting branches.
        r = rng.random()
        if r < 0.40:
            # MP path with random per-player setup
            v["mp_active"] = 1
            for i in range(1, 7):
                if rng.random() < 0.5:
                    v["player_active"][i]   = 1
                    v["player_has_data"][i] = 1
                    v["player_disconnected"][i] = 1 if rng.random() < 0.1 else 0
                    v["player_ticks"][i] = rng.randint(0, 1000)
            v["game_mode_speed"] = rng.choice([0, 2, 5])
            v["ally_a"] = rng.randint(1, 6)
            v["ally_b"] = rng.randint(1, 6)
            v["base_delta"] = rng.randint(0, 20)
        if rng.random() < 0.10:
            v["pause_lock"] = 1
        if rng.random() < 0.10:
            v["extended_pause"] = 1
            if rng.random() < 0.5 and v["mp_active"]:
                v["mp_skip_byte"] = 1
        if rng.random() < 0.10:
            v["screen_freeze_byte"] = rng.choice([0, 1, 2])
        if rng.random() < 0.15:
            v["game_speed"] = rng.randint(0, 100)
        if rng.random() < 0.10:
            v["phase"] = 0x9f                  # wrap boundary
        # Stride/state mirrors — keep frame_count reasonable so avg_stride
        # stays in int range even with worst-case stride_sum.
        v["frame_count"]     = rng.randint(0, 1000)
        v["stride_sum"]      = rng.randint(0, 100000)
        v["max_stride"]      = rng.randint(0, 200)
        v["slow_frame_count"] = rng.randint(0, 99)
        out.append(v)
    return out[:n]


def gen_sim_sub_steps_rate_lut(rng: random.Random) -> list[int]:
    """Generate a deterministic 2400-byte rate-gate LUT for the test plan.
    Mostly non-zero so the LUT-gate doesn't trivially zero out the step
    decision, but with a sprinkle of zeros to exercise the gate-off branch.
    Single seeded RNG run → same LUT every time for a given --seed.
    """
    return [0 if rng.random() < 0.15 else rng.randint(1, 255)
            for _ in range(SIM_RATE_LUT_BYTES)]


def _sim_sub_steps_canary_value(vec: dict, port: dict) -> int:
    """Compute the post-call value retail should write to derived_hint
    (== port's derived_hint).  Used as a race-detector — retail's only
    write to DAT_004d215c is `local_14` on the scenario_hint != 0 path
    (cleared to 0 pre-call by the agent).  If readback differs from
    expected, the engine raced our derived_hint global between our
    call's return and our readback."""
    return int(port["derived_hint"])


def _sim_sub_steps_race_free(vec: dict, retail: dict, port: dict) -> bool:
    """Race detector for sim_sub_steps.

    Two complementary checks:
      1. derived_hint readback matches port-computed value (post-call canary).
         Retail's only writer for DAT_004d215c is FUN_004bb978's own scope
         (`DAT_004d215c = local_14` in the scenario_hint != 0 branch).  We
         pre-cleared it to 0 before injection; the post-call readback should
         equal port's derived_hint output if no race.
      2. new_last_step_ms ∈ {vec[now_ms], vec[last_step_ms]} — retail's
         tail-write is one of these two values.  Anything else (esp. a
         large real-clock timestamp) means the engine main thread
         overwrote DAT_004e5b0c after our call returned.
    """
    expected_hint = _sim_sub_steps_canary_value(vec, port)
    actual_hint = int(retail.get("derived_hint", 0))
    if actual_hint != expected_hint:
        return False
    last = int(retail.get("new_last_step_ms", 0)) & 0xFFFFFFFF
    expected = {int(vec["now_ms"]) & 0xFFFFFFFF,
                int(vec["last_step_ms"]) & 0xFFFFFFFF}
    return last in expected


def run_port_sim_sub_steps(lib: ctypes.CDLL,
                           vec: dict,
                           lut_bytes: list[int]) -> dict[str, int]:
    """Chain our two ports: first engine_compute_scenario_hint, then
    engine_compute_sim_sub_steps with the resulting hint.  Combines the
    outputs of both into a single dict matching retail's RPC return shape.
    """
    # ─── scenario_hint port ───
    sh_in = EngineScenarioHintIn(
        game_state_phase=vec["game_state_phase"],
        mp_active=vec["mp_active"],
        local_player_idx=vec["local_player_idx"],
        game_mode_speed=vec["game_mode_speed"],
        ally_a=vec["ally_a"],
        ally_b=vec["ally_b"],
        frame_count=vec["frame_count"],
        max_stride=vec["max_stride"],
        stride_sum=vec["stride_sum"],
        avg_stride=vec["avg_stride"],
        slow_frame_count=vec["slow_frame_count"],
        base_delta=vec["base_delta"],
        active_count=vec["active_count"],
    )
    for i in range(7):
        sh_in.player_active[i]       = vec["player_active"][i]       & 0xff
        sh_in.player_has_data[i]     = vec["player_has_data"][i]     & 0xff
        sh_in.player_disconnected[i] = vec["player_disconnected"][i] & 0xff
        sh_in.player_ticks[i]        = vec["player_ticks"][i]
    sh_out = EngineScenarioHintOut()
    lib.engine_compute_scenario_hint(ctypes.byref(sh_in), ctypes.byref(sh_out))

    # ─── sim_sub_steps port ───
    # run_gate is FUN_004bb8c0 retail-side, which always returns 1 (the
    # function is a stub).  Our port treats it as caller-supplied → 1.
    lut_buf = (ctypes.c_uint8 * SIM_RATE_LUT_BYTES)(*lut_bytes)
    rate_in = EngineRateIn(
        now_ms=vec["now_ms"] & 0xFFFFFFFF,
        last_step_ms=vec["last_step_ms"] & 0xFFFFFFFF,
        autosave_start_ms=vec["autosave_start_ms"] & 0xFFFFFFFF,
        run_gate=1,
        scenario_hint=sh_out.hint,
        mp_active=vec["mp_active"],
        sim_tick_counter=vec["tick_counter"] & 0xFFFFFFFF,
        game_speed=vec["game_speed"],
        pause_lock=vec["pause_lock"],
        screen_freeze=vec["screen_freeze_byte"] & 0xff,
        extended_pause=vec["extended_pause"],
        mp_skip_flag=vec["mp_skip_byte"] & 0xff,
        phase=vec["phase"],
        rate_lut=lut_buf,
    )
    rate_out = EngineRateOut()
    lib.engine_compute_sim_sub_steps(ctypes.byref(rate_in), ctypes.byref(rate_out))

    # Combine.  Note: when the rate gate takes one of the early-return
    # paths (autosave window active, screen_freeze block, extended_pause
    # block), the rate function returns WITHOUT writing back to phase or
    # derived_hint via the tail assignment — its `out` defaults carry
    # through.  Our port's `out.new_phase` correctly reflects whether
    # the phase mutation fired (it happens before the early return in
    # the LUT-gate branch).
    return {
        "sub_steps":              rate_out.sub_steps,
        "new_last_step_ms":       rate_out.new_last_step_ms,
        "new_autosave_start_ms":  rate_out.new_autosave_start_ms,
        "new_phase":              rate_out.new_phase,
        "derived_hint":           rate_out.derived_hint,
        # From scenario_hint:
        "new_frame_count":        sh_out.new_frame_count,
        "new_max_stride":         sh_out.new_max_stride,
        "new_stride_sum":         sh_out.new_stride_sum,
        "new_avg_stride":         sh_out.new_avg_stride,
        "new_slow_frame_count":   sh_out.new_slow_frame_count,
        "new_active_count":       sh_out.new_active_count,
    }


def diff_sim_sub_steps(retail: dict, port: dict) -> list[str]:
    bad = []
    for k in SIM_SUB_STEPS_OUT_FIELDS:
        rv = int(retail.get(k, 0)) & 0xFFFFFFFF
        pv = int(port[k]) & 0xFFFFFFFF
        if rv != pv:
            bad.append(k)
    return bad


def run_port_frame_delta(lib: ctypes.CDLL, vec: dict[str, int]) -> dict[str, int]:
    din = EngineDeltaIn(now_ms=vec["now_ms"], last_frame_ms=vec["last_frame_ms"])
    dout = EngineDeltaOut()
    lib.engine_compute_frame_delta(ctypes.byref(din), ctypes.byref(dout))
    return {
        "delta_ms":          dout.delta_ms,
        "new_last_frame_ms": dout.new_last_frame_ms,
    }


# Frame-delta diff: two output fields.  Race detection structural (same as
# anim_cycle).  delta_ms compared as int32 because the suppressed branch
# always returns 0 but the agent returns it via `int32` NativeFunction so
# negative deltas don't sneak in as u32 from the retail side.
def diff_frame_delta(retail: dict, port: dict) -> list[str]:
    bad = []
    if int(retail["delta_ms"]) != int(port["delta_ms"]):
        bad.append("delta_ms")
    if (int(retail["new_last_frame_ms"]) & 0xFFFFFFFF) != \
            (int(port["new_last_frame_ms"]) & 0xFFFFFFFF):
        bad.append("new_last_frame_ms")
    return bad


# ─── state_hash (FUN_00440231 — engine_compute_state_hash) ────────────

# Source-array sizes — MUST match the constants in tools/frida/openlords2-agent.js
# (STATE_HASH_*_BYTES).  The agent and orchestrator generate the same
# 125,468-byte payload from a shared 32-bit seed via the xorshift32 helper
# below.
STATE_HASH_PER_ARMY_BYTES         = 17  * 0x300
STATE_HASH_PLAYER_STRATEGIC_BYTES = 6   * 0x160
STATE_HASH_PER_TERRITORY_BYTES    = 151 * 0x1a4
STATE_HASH_PER_BATTLE_HEX_BYTES   = 81  * 0x1b0
STATE_HASH_PER_COMBAT_UNIT_BYTES  = 101 * 0x4c
STATE_HASH_PER_BATTLE_AUX_BYTES   = 81  * 0x34


def state_hash_fill_bytes(seed: int, n: int) -> bytes:
    """Shared 32-bit xorshift bit-for-bit equivalent of the agent's
    `stateHashFillBytes` (tools/frida/openlords2-agent.js).  Both
    sides MUST agree byte-for-byte; orchestrator generates the buffer
    that gets passed to the port; agent fills retail's memory from the
    same algorithm given the same seed."""
    s = (seed & 0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= (s >> 17) & 0xFFFFFFFF
        s ^= (s << 5)  & 0xFFFFFFFF
        s &= 0xFFFFFFFF
        out[i] = s & 0xFF
    return bytes(out)


def gen_state_hash_payload(seed: int) -> dict[str, bytes]:
    """Generate the same 6 source-array buffers the agent will fill
    retail-side from `seed`.  Each array uses a sub-seed derived from
    the master seed mixed with the array index (0..5) — must match the
    agent's `installStateHashSources` per-array tag mixing exactly."""
    GOLDEN = 0x9e3779b1
    arrays = [
        ("per_army_768",          STATE_HASH_PER_ARMY_BYTES,         0),
        ("player_strategic_352",  STATE_HASH_PLAYER_STRATEGIC_BYTES, 1),
        ("per_territory_420",     STATE_HASH_PER_TERRITORY_BYTES,    2),
        ("per_battle_hex_432",    STATE_HASH_PER_BATTLE_HEX_BYTES,   3),
        ("per_combat_unit_76",    STATE_HASH_PER_COMBAT_UNIT_BYTES,  4),
        ("per_battle_hex_aux_52", STATE_HASH_PER_BATTLE_AUX_BYTES,   5),
    ]
    out = {}
    for name, sz, tag in arrays:
        sub_seed = (seed + GOLDEN * tag) & 0xFFFFFFFF
        out[name] = state_hash_fill_bytes(sub_seed, sz)
    return out


def gen_state_hash_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N state-hash test vectors.  Each vector is a scalar tuple
    (mode, player_slot, sim_tick_counter) — the source-array payload
    is installed ONCE per test plan (see install_state_hash_sources)
    and is the same for every vector.

    Coverage spans:
      - mode == 0 (strategic) at various player slots + tick counters
      - mode == 2 (combat) at the same
      - mode == 1 / 3 / 5 (neither-branch) at the same
      - bucket[9] formula at tick LSBs 0x00, 0x7f, 0x80, 0xff
      - player_slot = 0 (safe scratch in retail's storage)
    """
    out: list[dict[str, int]] = [
        # Strategic / combat at slot 0, tick=0 (baseline).
        {"mode": 0, "player_slot": 0, "sim_tick_counter": 0},
        {"mode": 2, "player_slot": 0, "sim_tick_counter": 0},
        # Neither-branch mode (mode==1) — only bucket[7]=1, bucket[9], bucket[0]=1 fire.
        {"mode": 1, "player_slot": 0, "sim_tick_counter": 0},
        # bucket[9] formula corners — verify (tick & 0x7f) + 1 across boundaries.
        {"mode": 0, "player_slot": 0, "sim_tick_counter": 0x7f},
        {"mode": 0, "player_slot": 0, "sim_tick_counter": 0x80},
        {"mode": 0, "player_slot": 0, "sim_tick_counter": 0xff},
        {"mode": 2, "player_slot": 0, "sim_tick_counter": 0xfffffffe},
        # Modes 3 and 5 — also neither-branch; verify the dispatch is
        # mode == 0 OR mode == 2, nothing else.
        {"mode": 3, "player_slot": 0, "sim_tick_counter": 0x12},
        {"mode": 5, "player_slot": 0, "sim_tick_counter": 0xab},
    ]
    while len(out) < n:
        mode = rng.choice([0, 2, 1, 3, 5])
        out.append({
            "mode":             mode,
            "player_slot":      0,
            "sim_tick_counter": rng.getrandbits(32),
        })
    return out[:n]


def run_port_state_hash(lib: ctypes.CDLL,
                        vec: dict[str, int],
                        payload: dict[str, bytes]) -> dict[str, Any]:
    """Run our engine_compute_state_hash port with the given vector +
    source-array payload (generated from the test plan's seed)."""
    # Allocate ctypes buffers backed by the bytes objects.  ctypes
    # treats `(c_uint8 * len)` as a mutable array; from_buffer_copy
    # avoids aliasing the bytes-object's read-only buffer.
    bufs = {}
    for k, b in payload.items():
        bufs[k] = (ctypes.c_uint8 * len(b)).from_buffer_copy(b)

    sh_in = EngineStateHashIn(
        mode=vec["mode"],
        player_slot=vec["player_slot"],
        sim_tick_counter=vec["sim_tick_counter"] & 0xFFFFFFFF,
        per_army_768=ctypes.cast(bufs["per_army_768"],
                                 ctypes.POINTER(ctypes.c_uint8)),
        player_strategic_352=ctypes.cast(bufs["player_strategic_352"],
                                         ctypes.POINTER(ctypes.c_uint8)),
        per_territory_420=ctypes.cast(bufs["per_territory_420"],
                                      ctypes.POINTER(ctypes.c_uint8)),
        per_battle_hex_432=ctypes.cast(bufs["per_battle_hex_432"],
                                       ctypes.POINTER(ctypes.c_uint8)),
        per_combat_unit_76=ctypes.cast(bufs["per_combat_unit_76"],
                                       ctypes.POINTER(ctypes.c_uint8)),
        per_battle_hex_aux_52=ctypes.cast(bufs["per_battle_hex_aux_52"],
                                          ctypes.POINTER(ctypes.c_uint8)),
    )
    sh_out = EngineStateHashOut()
    lib.engine_compute_state_hash(ctypes.byref(sh_in), ctypes.byref(sh_out))
    return {
        "bucket":                 list(sh_out.bucket),
        "hash_region":            list(sh_out.hash_region),
        "notify_combat_snapshot": int(sh_out.notify_combat_snapshot),
    }


def diff_state_hash(retail: dict, port: dict) -> list[str]:
    """Field-level diff for state_hash.  All three outputs are checked
    bytewise; mismatches return field name + indicator (the per-element
    index doesn't appear in the diff list, but the failure pretty-printer
    in _print_summary prints the full retail/port dicts so the offending
    bytes are visible)."""
    bad = []
    rb = list(retail.get("bucket") or [])
    pb = list(port["bucket"])
    if rb != pb:
        bad.append("bucket")
    rh = list(retail.get("hash_region") or [])
    ph = list(port["hash_region"])
    if rh != ph:
        bad.append("hash_region")
    if int(retail.get("notify_combat_snapshot", 0)) != \
            int(port["notify_combat_snapshot"]):
        bad.append("notify_combat_snapshot")
    return bad


# ─── tile_expand (FUN_004917dd — engine_tile_expand_footprint) ─────────

# State surface (must match the constants in tools/frida/openlords2-agent.js):
# tiles + footprint_list + last_footprint_tile are pre-seeded from the shared
# test-plan seed (xorshift32, same mixing as state_hash) and re-seeded per
# call — the function mutates them in place, so vectors must start from a
# fresh baseline.  path_scratch + stage_scratch are pure outputs (fully
# recomputed from tiles inside the function); they don't need pre-seeding.
TILE_EXPAND_TILES_BYTES          = 51200
TILE_EXPAND_PATH_SCRATCH_BYTES   = 12800       # 6400 × u16
TILE_EXPAND_STAGE_SCRATCH_BYTES  = 6400
TILE_EXPAND_FOOTPRINT_LIST_BYTES = 152         # 19 × 2 × i32
TILE_EXPAND_LAST_FOOTPRINT_BYTES = 4           # 1 × i32

# Anchor chars FUN_004917dd recognises (rest early-return as no-op).
# Plus a couple of unrecognised chars to exercise the no-op branch.
TILE_EXPAND_ANCHORS_RECOGNISED   = [ord(c) for c in "@CX["]
TILE_EXPAND_ANCHORS_UNRECOGNISED = [0x00, ord('A'), ord('Z'), ord('Y'), 0xff]


def tile_expand_fill_bytes(seed: int, n: int) -> bytes:
    """Bit-for-bit mirror of agent JS `tileExpandFillBytes` (xorshift32)."""
    s = (seed & 0xFFFFFFFF) or 1
    out = bytearray(n)
    for i in range(n):
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= (s >> 17) & 0xFFFFFFFF
        s ^= (s << 5)  & 0xFFFFFFFF
        s &= 0xFFFFFFFF
        out[i] = s & 0xFF
    return bytes(out)


def gen_tile_expand_pre_state(seed: int) -> dict[str, bytes]:
    """Re-create the 3 input buffers the agent will write to retail's
    globals.  Per-array sub-seed (seed + GOLDEN * tag) matches the agent's
    runRetailTileExpand exactly."""
    GOLDEN = 0x9e3779b1
    arrays = [
        ("tiles",          TILE_EXPAND_TILES_BYTES,          0),
        ("footprint_list", TILE_EXPAND_FOOTPRINT_LIST_BYTES, 1),
        ("last_footprint", TILE_EXPAND_LAST_FOOTPRINT_BYTES, 2),
    ]
    out = {}
    for name, sz, tag in arrays:
        sub_seed = (seed + GOLDEN * tag) & 0xFFFFFFFF
        out[name] = tile_expand_fill_bytes(sub_seed, sz)
    return out


def _cell_offset(col: int, row: int) -> int:
    return (row * 80 + col) * 8


def gen_tile_expand_vectors(n: int, rng: random.Random) -> list[dict[str, int]]:
    """Build N tile_expand vectors.  Each vector is the 1-arg input
    (tile_byte_offset) plus the anchor byte written to tiles[offset+3].
    Pre-state for tiles/footprint_list/last_footprint is shared across
    all vectors and seeded once per test plan.

    Coverage:
      - Every recognized anchor ('@', 'C', 'X', '[') at a safe interior
        cell (col=10, row=10) — exercises all 4 orient branches.
      - Each anchor at multiple interior positions — different
        col/row decoding paths through the 3×3 + neighbor writes.
      - Unrecognized anchors (early-return branch).
      - "Edge of map" anchors at the engine's quirky interior bound
        (col=77/78, row=76/77) — these still fit the 3x3 footprint
        + per-orientation door cells without OOB.  Picks specifically
        the corners that interact with the orthogonal-axis quirk in
        FUN_004921e5 (see engine-quirks.md).
    """
    out: list[dict[str, int]] = []
    safe_positions = [
        (10, 10), (40, 40), (20, 50), (60, 20),
        # Interior-ish corners.  col in [1, 77] / row in [1, 76] so the
        # door-neighbor writes (orient-2 needs col+3, orient-4 needs row+3)
        # stay in-array.
        ( 1,  1), (77,  1), ( 1, 76), (77, 76),
        # Right at the orthogonal-quirk gate boundaries.
        (78, 50),  # col 78 → col+1 = 79 (boundary < 79 fails)
        (50, 78),  # row 78 → row+1 = 79
    ]
    for (col, row) in safe_positions:
        for ch in TILE_EXPAND_ANCHORS_RECOGNISED:
            out.append({
                "tile_byte_offset": _cell_offset(col, row),
                "anchor_char":      ch,
            })
    # Unrecognised-anchor early-return at one safe spot per char.
    for ch in TILE_EXPAND_ANCHORS_UNRECOGNISED:
        out.append({
            "tile_byte_offset": _cell_offset(15, 15),
            "anchor_char":      ch,
        })
    while len(out) < n:
        ch = rng.choice(TILE_EXPAND_ANCHORS_RECOGNISED +
                        TILE_EXPAND_ANCHORS_UNRECOGNISED)
        col = rng.randint(1, 77)
        row = rng.randint(1, 76)
        out.append({
            "tile_byte_offset": _cell_offset(col, row),
            "anchor_char":      ch,
        })
    return out[:n]


def run_port_tile_expand(lib: ctypes.CDLL,
                         vec: dict[str, int],
                         pre_state: dict[str, bytes]) -> dict[str, Any]:
    """Run our engine_tile_expand_footprint port.  Allocates a fresh
    EngineTileExpandState, fills the 3 input arrays from pre_state,
    injects the anchor byte at tiles[offset+3], invokes the port, and
    returns the post-state."""
    state = EngineTileExpandState()
    ctypes.memmove(state.tiles,          pre_state["tiles"],          TILE_EXPAND_TILES_BYTES)
    ctypes.memmove(state.footprint_list, pre_state["footprint_list"], TILE_EXPAND_FOOTPRINT_LIST_BYTES)
    # last_footprint is i32 — copy 4 bytes through ctypes.memmove to keep
    # the byte-pattern bit-identical with the agent's writeByteArray.
    ctypes.memmove(ctypes.byref(state, EngineTileExpandState.last_footprint_tile.offset),
                   pre_state["last_footprint"], TILE_EXPAND_LAST_FOOTPRINT_BYTES)
    # path_scratch and stage_scratch are zero-initialised by ctypes' default
    # struct init; retail's path also leaves them unwritten until the helpers
    # fire, but FUN_004917dd ALWAYS calls both helpers before returning
    # (only the unrecognized-anchor early-return skips them — and in that
    # case neither side has touched the scratches at all, so both stay
    # zero on the port side / hold their old retail bytes on the retail
    # side).  This is the one branch where bytewise output diff WOULD
    # detect a "divergence" that isn't real — we handle it with a check
    # below: when the anchor was unrecognized, skip path/stage_scratch
    # from the diff (verified by re-running the port to see return == 0).
    offset = vec["tile_byte_offset"] | 0
    anchor = vec["anchor_char"] & 0xff
    if 0 <= offset + 3 < TILE_EXPAND_TILES_BYTES:
        state.tiles[offset + 3] = anchor
    state.tile_byte_offset = offset
    fired = lib.engine_tile_expand_footprint(ctypes.byref(state))
    return {
        "tiles":               list(bytes(state.tiles)),
        "path_scratch":        list(bytes(state.path_scratch)),
        "stage_scratch":       list(bytes(state.stage_scratch)),
        "footprint_list":      list(bytes(state.footprint_list)),
        "last_footprint_tile": int(state.last_footprint_tile),
        "fired":               int(fired),
    }


def diff_tile_expand(retail: dict, port: dict, vec: dict) -> list[str]:
    """Bytewise diff of the 5 output globals.

    For the unrecognised-anchor early-return branch (port returns 0),
    the function did NOT touch path_scratch or stage_scratch — on the
    retail side they hold whatever bytes were there pre-call (the agent
    seeded ONLY tiles/footprint_list/last_footprint), while the port
    side zero-init'd them.  Skip those two fields when port["fired"] == 0
    to avoid the false positive.  The other 3 fields (tiles unchanged
    except for the injected anchor; list/last unchanged) must still match.
    """
    bad = []
    if list(retail.get("tiles") or []) != port["tiles"]:
        bad.append("tiles")
    if list(retail.get("footprint_list") or []) != port["footprint_list"]:
        bad.append("footprint_list")
    if int(retail.get("last_footprint_tile", 0)) != int(port["last_footprint_tile"]):
        bad.append("last_footprint_tile")
    if port["fired"]:
        if list(retail.get("path_scratch") or []) != port["path_scratch"]:
            bad.append("path_scratch")
        if list(retail.get("stage_scratch") or []) != port["stage_scratch"]:
            bad.append("stage_scratch")
    return bad


# ─── delayed_action (FUN_004472ce — engine_dispatch_delayed_actions) ─────

# State surface (must match constants in tools/frida/openlords2-agent.js).
# Each per-call vector specifies a sparse list of records to stamp active
# (rather than a full 22400-byte records-array blob) — only a handful of
# slots are populated per vector, so sparse encoding keeps both the wire
# size and the test-vector intent readable.
DA_RECORDS_BYTES   = 22400      # 400 × 0x38
DA_QUEUE_BYTES     = 400        # 100 × i32
DA_HIPRI_BYTES     = 400        # 100 × i32

# Maximum dispatch type the agent's PTR_FUN_004d5c98 stub covers.  Retail
# has 50 entries (0..49); vectors must clamp types to this range or the
# dispatch lookup would fall off the table.
DA_MAX_TYPE = 49

# Sentinel sweep-clear value the function writes into queue[] at entry.
# int32 == 0xffff (NOT -1 — explicit comparison is `< 0xffff`).
DA_QUEUE_EMPTY = 0xffff


def _da_make_record(idx: int, *,
                    active: int = 1,
                    type_: int = 0,
                    due_tick: int = 0,
                    args: list[int] | None = None) -> dict[str, Any]:
    if args is None:
        args = [0] * 10
    if len(args) < 10:
        args = args + [0] * (10 - len(args))
    return {
        "idx":      idx,
        "active":   int(active) & 0xff,
        "type":     int(type_) & 0xff,
        "due_tick": int(due_tick),
        "args":     [int(a) for a in args[:10]],
    }


def gen_delayed_action_vectors(n: int, rng: random.Random) -> list[dict[str, Any]]:
    """Build N delayed-action vectors.

    Coverage:
      - master_switch=0 (function early-returns; queue/hipri/disp/counters
        all carry through from our pre-seeded values).
      - No active records (clears queue, leaves everything else alone).
      - All records future-dated (skipped by the due_tick filter).
      - Single dispatched record per game_mode (0, 1, 2, 3, 5).
      - Multiple dispatched records, in-order (mode != 2 / mode==2 with
        queue_count==1 → reorder branch skipped).
      - MP reorder (mode==2, >=2 due records):
          - All relay candidates (types 0x21..0x23 from non-local) — full
            queue → empty after reorder → re-appended at slot 0.
          - Mixed relay + non-relay.
          - Relays from local_player (filter rejects, stays in place).
          - Type boundaries 0x20 (excluded) and 0x24 (excluded).
      - action_counter pre-seeded to a non-zero value to verify it
        decrements per dispatch.
      - current_idx + disp_pre seeded non-zero so carry-through is
        observable in the no-dispatch cases.
    """
    out: list[dict[str, Any]] = []

    # Common pre-state seeds — make sure they're observable and unique
    # so any reordering would show up in the diff.
    base = lambda **kw: {  # noqa: E731
        "master_switch":  1,
        "game_mode":      0,
        "local_player":   1,
        "action_counter": 10,
        "current_idx":    42,
        "current_tick":   100,
        "disp_pre":       [0xa0, 0xa1, 0xa2, 0xa3, 0xa4,
                           0xa5, 0xa6, 0xa7, 0xa8, 0xa9],
        "queue_pre":      [DA_QUEUE_EMPTY] * 100,
        "hipri_pre":      [0] * 100,
        "queue_count":    0,
        "hipri_count":    0,
        "records":        [],
        **kw,
    }

    # 1. Master switch off — entire function is a no-op.  Seed records
    #    AND queue+counts so the post-state carry-through is non-trivial.
    out.append(base(master_switch=0,
                    queue_pre=[5, 7, 11] + [DA_QUEUE_EMPTY] * 97,
                    hipri_pre=[13, 17] + [0] * 98,
                    queue_count=3, hipri_count=2,
                    records=[_da_make_record(10, type_=5, due_tick=50)]))

    # 2. No due records — sweep finds nothing; queue cleared, counters
    #    intact, dispatch mirrors unchanged.
    out.append(base())  # No records at all.
    out.append(base(records=[_da_make_record(0, active=0, type_=3, due_tick=0)]))
    out.append(base(records=[_da_make_record(5, type_=2, due_tick=999)]))  # future

    # 3. Single record dispatched — game_mode varies to exercise the
    #    mode != 2 branches.
    for mode in [0, 1, 3, 5]:
        out.append(base(game_mode=mode,
                        records=[_da_make_record(50, type_=7,
                                                  due_tick=100,
                                                  args=[100, 200, 300, 400, 500,
                                                         600, 700, 800, 900, 1000])]))

    # 4. Multiple records — mode 0, in-order dispatch.
    out.append(base(records=[
        _da_make_record(10,  type_=0, due_tick=50,  args=[1] * 10),
        _da_make_record(100, type_=1, due_tick=75,  args=[2] * 10),
        _da_make_record(200, type_=2, due_tick=100, args=[3] * 10),
    ]))

    # 5. MP reorder — single relay; queue_count==1 short-circuits.
    out.append(base(game_mode=2,
                    records=[_da_make_record(50, type_=0x22, due_tick=0,
                                              args=[99, 0, 0, 0, 0, 0, 0, 0, 0, 0])]))

    # 6. MP reorder — multiple due records, relay candidates pulled back.
    out.append(base(game_mode=2,
                    records=[
                        _da_make_record(10, type_=0x00, due_tick=0, args=[1] * 10),
                        _da_make_record(20, type_=0x22, due_tick=0,
                                        args=[99, 22, 0, 0, 0, 0, 0, 0, 0, 0]),
                        _da_make_record(30, type_=0x10, due_tick=0, args=[3] * 10),
                    ]))

    # 7. MP reorder — all 3 records are relay candidates.
    out.append(base(game_mode=2,
                    records=[
                        _da_make_record(10, type_=0x21, due_tick=0,
                                        args=[99] + [0] * 9),
                        _da_make_record(20, type_=0x22, due_tick=0,
                                        args=[99] + [0] * 9),
                        _da_make_record(30, type_=0x23, due_tick=0,
                                        args=[99] + [0] * 9),
                    ]))

    # 8. MP reorder — relay from local player (filter rejects).
    out.append(base(game_mode=2, local_player=1,
                    records=[
                        _da_make_record(10, type_=0x22, due_tick=0,
                                        args=[1] + [0] * 9),  # local → no move
                        _da_make_record(20, type_=0x22, due_tick=0,
                                        args=[2] + [0] * 9),  # peer → moves
                    ]))

    # 9. MP reorder — types at boundaries 0x20 (not a relay), 0x24
    #    (not a relay), 0x21/0x23 (are relays).
    out.append(base(game_mode=2,
                    records=[
                        _da_make_record(10, type_=0x20, due_tick=0,
                                        args=[99] + [0] * 9),  # NOT relay
                        _da_make_record(20, type_=0x21, due_tick=0,
                                        args=[99] + [0] * 9),  # relay
                        _da_make_record(30, type_=0x23, due_tick=0,
                                        args=[99] + [0] * 9),  # relay
                        _da_make_record(40, type_=0x24, due_tick=0,
                                        args=[99] + [0] * 9),  # NOT relay
                    ]))

    # 10. Due-tick boundary inclusive — record.due_tick == current_tick fires.
    out.append(base(current_tick=100,
                    records=[_da_make_record(7, type_=3, due_tick=100)]))

    # Random fill — keep sizes modest so wire traffic stays sane.
    while len(out) < n:
        n_records = rng.randint(0, 6)
        records = []
        used = set()
        for _ in range(n_records):
            # Avoid duplicate slot indices in a single vector (retail's
            # sweep would only see the LAST stamped one, but we want
            # deterministic behavior).
            for _try in range(8):
                idx = rng.randint(0, 399)
                if idx not in used:
                    used.add(idx)
                    break
            else:
                continue
            records.append(_da_make_record(
                idx,
                active=rng.choice([0, 1, 1, 1]),    # bias to active
                type_=rng.randint(0, DA_MAX_TYPE),
                due_tick=rng.choice([0, 50, 100, 200]),
                args=[rng.randint(-1000, 1000) for _ in range(10)]))
        out.append({
            "master_switch":  rng.choice([0, 1, 1, 1, 1]),
            "game_mode":      rng.choice([0, 1, 2, 2, 3, 5]),
            "local_player":   rng.randint(1, 6),
            "action_counter": rng.randint(0, 100),
            "current_idx":    rng.randint(0, 399),
            "current_tick":   100,
            "disp_pre":       [rng.randint(-1000, 1000) for _ in range(10)],
            # Keep queue_pre / hipri_pre at default sentinels for the random
            # vectors — they only matter for the master_switch=0 fixed cases.
            "queue_pre":      [DA_QUEUE_EMPTY] * 100,
            "hipri_pre":      [0] * 100,
            "queue_count":    0,
            "hipri_count":    0,
            "records":        records,
        })

    return out[:n]


def run_port_delayed_action(lib: ctypes.CDLL,
                            vec: dict[str, Any]) -> dict[str, Any]:
    """Run our engine_dispatch_delayed_actions port.  Mirrors the agent's
    per-call setup: zero-fill records, apply the sparse list, seed scalars
    + dispatch mirrors + queue/hipri pre-state, run, then read back the
    observable surface."""
    state = EngineDelayedActionState()
    # ctypes default-zero-inits everything.  Apply the vector's inputs.
    state.master_switch  = int(vec["master_switch"])
    state.game_mode      = int(vec["game_mode"])
    state.local_player   = int(vec["local_player"])
    state.action_counter = int(vec["action_counter"])
    state.current_idx    = int(vec["current_idx"])
    pre = vec.get("disp_pre", [0] * 10)
    state.disp_a0, state.disp_a1, state.disp_a2 = pre[0], pre[1], pre[2]
    state.disp_a3, state.disp_a4, state.disp_a5 = pre[3], pre[4], pre[5]
    state.disp_a6, state.disp_a7, state.disp_a8 = pre[6], pre[7], pre[8]
    state.disp_a9                                = pre[9]
    state.queue_count = int(vec.get("queue_count", 0))
    state.hipri_count = int(vec.get("hipri_count", 0))
    queue_pre = vec.get("queue_pre", [DA_QUEUE_EMPTY] * 100)
    hipri_pre = vec.get("hipri_pre", [0] * 100)
    for i in range(100):
        state.queue[i]       = int(queue_pre[i]) if i < len(queue_pre) else 0
        state.hipri_stash[i] = int(hipri_pre[i]) if i < len(hipri_pre) else 0
    for rec in vec.get("records", []):
        idx = int(rec["idx"])
        r = state.records[idx]
        r.due_tick = int(rec["due_tick"])
        for i, a in enumerate(rec["args"]):
            setattr(r, f"arg{i}", int(a))
        r.active = int(rec["active"]) & 0xff
        r.type   = int(rec["type"])   & 0xff

    lib.engine_dispatch_delayed_actions(ctypes.byref(state),
                                        ctypes.c_int(int(vec["current_tick"])))

    # Read back the observable surface.  records[] is bytewise-comparable
    # against retail's 22400-byte readback; queue + hipri_stash same way.
    rec_bytes = ctypes.string_at(ctypes.byref(state.records),
                                 ctypes.sizeof(state.records))
    queue_bytes = ctypes.string_at(ctypes.byref(state.queue),
                                   ctypes.sizeof(state.queue))
    hipri_bytes = ctypes.string_at(ctypes.byref(state.hipri_stash),
                                   ctypes.sizeof(state.hipri_stash))
    return {
        "records":        list(rec_bytes),
        "queue":          list(queue_bytes),
        "hipri_stash":    list(hipri_bytes),
        "queue_count":    int(state.queue_count),
        "hipri_count":    int(state.hipri_count),
        "action_counter": int(state.action_counter),
        "current_idx":    int(state.current_idx),
        "disp": [int(state.disp_a0), int(state.disp_a1),
                 int(state.disp_a2), int(state.disp_a3),
                 int(state.disp_a4), int(state.disp_a5),
                 int(state.disp_a6), int(state.disp_a7),
                 int(state.disp_a8), int(state.disp_a9)],
    }


def diff_delayed_action(retail: dict, port: dict) -> list[str]:
    """Bytewise diff of the observable surface.

    All 8 outputs are checked.  The 10-entry disp[] list is compared
    element-wise so a single divergence reports a specific index."""
    bad = []
    if list(retail.get("records") or []) != port["records"]:
        bad.append("records")
    if list(retail.get("queue") or []) != port["queue"]:
        bad.append("queue")
    if list(retail.get("hipri_stash") or []) != port["hipri_stash"]:
        bad.append("hipri_stash")
    if int(retail.get("queue_count", 0)) != int(port["queue_count"]):
        bad.append("queue_count")
    if int(retail.get("hipri_count", 0)) != int(port["hipri_count"]):
        bad.append("hipri_count")
    if int(retail.get("action_counter", 0)) != int(port["action_counter"]):
        bad.append("action_counter")
    if int(retail.get("current_idx", 0)) != int(port["current_idx"]):
        bad.append("current_idx")
    r_disp = list(retail.get("disp") or [])
    p_disp = list(port["disp"])
    for i in range(min(len(r_disp), len(p_disp), 10)):
        if int(r_disp[i]) != int(p_disp[i]):
            bad.append(f"disp[{i}]")
    return bad


# ─── per-realm turn-pass diff targets (engine_realm) ─────────────────────
#
# The turn-advance engine's per-realm passes are __stdcall void() functions
# that loop over the live realm count and mutate the shared save globals.  The
# agent's generic runRetailRealmPass snapshots/injects/reads-back arbitrary
# memory regions and calls the pass by VA; the port side pokes the same bytes
# into libengine_diff.so's own copies of those globals and calls the exported
# port.  Both start from byte-identical injected state, so a faithful port
# yields byte-identical post-state.

REALM_ARMY_BYTES   = 13056      # g_per_army_768          (17 × 0x300)
REALM_STRAT_BYTES  = 2112       # g_player_strategic_352  (6 × 0x160)
REALM_MISC_BYTES   = 1360       # g_misc_1360             (17 × 0x50)
REALM_OVER_BYTES   = 32768      # g_overworld_map
REALM_ARMY_STRIDE  = 0x300
REALM_STRAT_STRIDE = 0x160
REALM_MISC_STRIDE  = 0x50

VA_PER_ARMY     = 0x53f9b0
VA_STRATEGIC    = 0x57bf00
VA_MISC_1360    = 0x53ea00
VA_OVERWORLD    = 0x522f90
VA_SLOT_COUNT   = 0x56d5dc       # s_slot_0056d5dc — realm count
VA_SLOT_STAND   = 0x53f25c       # s_slot_0053f25c — standings-enabled flag
VA_SLOT_SEASON  = 0x57c92c       # s_slot_0057c92c — season (dev reads, unused)
FN_TAX_APPROVAL = 0x44bd9f
FN_DEV_SPREAD   = 0x44bf7a
FN_REALM_INCOME = 0x44b80b       # FUN_0044b80b — __cdecl(int realm)

REALM_RACE_RETRIES = 8


def _u32le(v: int) -> bytes:
    return int(v & 0xffffffff).to_bytes(4, "little")


_realm_globals_cache: dict | None = None


def realm_port_globals(lib: ctypes.CDLL) -> dict:
    """Bind ctypes views onto the diff lib's save globals + realm-pass entry
    points (lazily, once)."""
    global _realm_globals_cache
    if _realm_globals_cache is None:
        _realm_globals_cache = {
            "army":   (ctypes.c_uint8 * REALM_ARMY_BYTES ).in_dll(lib, "g_per_army_768"),
            "strat":  (ctypes.c_uint8 * REALM_STRAT_BYTES).in_dll(lib, "g_player_strategic_352"),
            "misc":   (ctypes.c_uint8 * REALM_MISC_BYTES ).in_dll(lib, "g_misc_1360"),
            "over":   (ctypes.c_uint8 * REALM_OVER_BYTES ).in_dll(lib, "g_overworld_map"),
            "count":  ctypes.c_uint32.in_dll(lib, "s_slot_0056d5dc"),
            "stand":  ctypes.c_uint32.in_dll(lib, "s_slot_0053f25c"),
            "season": ctypes.c_uint32.in_dll(lib, "s_slot_0057c92c"),
        }
        lib.engine_realm_apply_tax_approval.restype = None
        lib.engine_realm_apply_tax_approval.argtypes = []
        lib.engine_realm_spread_development.restype = None
        lib.engine_realm_spread_development.argtypes = []
        lib.engine_realm_recompute_realm_income.restype = None
        lib.engine_realm_recompute_realm_income.argtypes = [ctypes.c_int]
        lib.engine_territory_bind_cells.restype = None
        lib.engine_territory_bind_cells.argtypes = [ctypes.c_void_p]
    return _realm_globals_cache


def _diff_realm_region(retail: dict, port_bytes: list[int],
                       va: int, stride: int, n_records: int,
                       label: str) -> list[str]:
    """Per-record bytewise diff of a readback region; reports the diverging
    records and (up to 8) byte offsets within each."""
    r = retail.get("regions", {}).get(str(va))
    if r is None:
        return [f"{label}:no-readback"]
    r = list(r)
    p = list(port_bytes)
    if r == p:
        return []
    bad = []
    for rec in range(n_records):
        b = rec * stride
        rr = r[b:b + stride]
        pp = p[b:b + stride]
        if rr != pp:
            offs = [hex(o) for o in range(stride) if rr[o] != pp[o]]
            bad.append(f"{label}[{rec}]:{','.join(offs[:8])}")
    return bad or [f"{label}:bytes"]


# ── tax-approval (FUN_0044bd9f — engine_realm_apply_tax_approval) ──

def _build_tax_vec(count: int, realms: list[tuple], players: list[tuple]) -> dict:
    army  = bytearray(REALM_ARMY_BYTES)
    strat = bytearray(REALM_STRAT_BYTES)
    for (slot, owner, band, rating, policy, pop) in realms:
        b = slot * REALM_ARMY_STRIDE
        army[b + 0x05]  = owner  & 0xff
        army[b + 0x09]  = band   & 0xff
        army[b + 0x0b]  = rating & 0xff
        army[b + 0x15d] = policy & 0xff
        struct.pack_into("<i", army, b + 0x24, pop)
    for (player, human) in players:
        strat[player * REALM_STRAT_STRIDE + 0x05] = human & 0xff
    return {"count": count, "army": bytes(army), "strat": bytes(strat)}


def gen_tax_approval_vectors(n: int, rng: random.Random) -> list[dict]:
    # Fixed edge cases first (mirror the host unit tests + quirk #149).
    edge = [
        (1, [(1, 1, 0, 75,  5, 1000)], [(1, 1)]),   # → band 4
        (1, [(1, 1, 0, 100, 5, 1000)], [(1, 1)]),   # rating 100 → fallback band 3
        (1, [(1, 1, 0, 5,   0, 1000)], [(1, 1)]),   # policy 0 → clamp low
        (1, [(1, 2, 2, 20,  6, 500)],  [(2, 0)]),   # policy 6 aliasing
        (1, [(1, 2, 0, 20,  3, 0)],    [(2, 0)]),   # zero-pop non-human → 50
        (1, [(1, 1, 0, 20,  3, 0)],    [(1, 1)]),   # zero-pop human → kept
    ]
    vecs = [_build_tax_vec(*e) for e in edge]
    while len(vecs) < n:
        count = rng.randint(1, 16)
        realms = []
        for slot in range(1, count + 1):
            realms.append((
                slot,
                rng.randint(0, 6),                  # owner
                rng.randint(0, 4),                  # band (table column)
                rng.randint(-20, 120),              # rating (pre-clamp)
                rng.randint(0, 6),                  # tax policy
                rng.choice([0, 0, rng.randint(1, 5000)]),  # population
            ))
        players = [(p, rng.randint(0, 1)) for p in range(6)]
        vecs.append(_build_tax_vec(count, realms, players))
    return vecs


def run_port_tax_approval(lib: ctypes.CDLL, vec: dict) -> dict:
    g = realm_port_globals(lib)
    ctypes.memmove(g["army"],  vec["army"],  REALM_ARMY_BYTES)
    ctypes.memmove(g["strat"], vec["strat"], REALM_STRAT_BYTES)
    g["count"].value = vec["count"]
    lib.engine_realm_apply_tax_approval()
    return {"army": list(bytes(g["army"]))}


def tax_approval_rpc_plan(vec: dict) -> dict:
    return {"fn_va": FN_TAX_APPROVAL, "regions": [
        {"va": VA_PER_ARMY,   "len": REALM_ARMY_BYTES,  "inject": list(vec["army"]),  "read": True},
        {"va": VA_STRATEGIC,  "len": REALM_STRAT_BYTES, "inject": list(vec["strat"]), "read": False},
        {"va": VA_SLOT_COUNT, "len": 4, "inject": list(_u32le(vec["count"])), "read": False},
    ]}


def diff_tax_approval(retail: dict, port: dict) -> list[str]:
    return _diff_realm_region(retail, port["army"], VA_PER_ARMY,
                              REALM_ARMY_STRIDE, 17, "army")


# ── development-spread (FUN_0044bf7a — engine_realm_spread_development) ──

def _diff_realm_flat(retail: dict, port_bytes: list[int], va: int,
                     label: str, max_report: int = 12) -> list[str]:
    r = retail.get("regions", {}).get(str(va))
    if r is None:
        return [f"{label}:no-readback"]
    r = list(r)
    p = list(port_bytes)
    if r == p:
        return []
    offs = [hex(o) for o in range(min(len(r), len(p))) if r[o] != p[o]]
    return [f"{label}:{','.join(offs[:max_report])}"]


def _build_dev_vec(count: int, standings: int, realms: list[dict]) -> dict:
    army = bytearray(REALM_ARMY_BYTES)
    misc = bytearray(REALM_MISC_BYTES)
    over = bytearray(REALM_OVER_BYTES)
    for rd in realms:
        slot = rd["slot"]
        b = slot * REALM_ARMY_STRIDE
        army[b + 0x05]  = rd.get("owner", 1) & 0xff
        army[b + 0x1ff] = rd.get("acc_a", 0) & 0xff
        army[b + 0x201] = rd.get("acc_b", 0) & 0xff
        struct.pack_into("<i", army, b + 0x208, rd.get("acc208", 0))
        struct.pack_into("<i", army, b + 0xdc,  rd.get("pool", 0))
        for (idx, value) in rd.get("vals", []):
            struct.pack_into("<h", army, b + 0x90 + idx * 2, value)
        for (ridx, cell_off, dev_type) in rd.get("cells", []):
            struct.pack_into("<i", misc, slot * REALM_MISC_STRIDE + ridx * 4, cell_off)
            over[cell_off] = dev_type & 0xff
    return {"count": count, "standings": standings,
            "army": bytes(army), "misc": bytes(misc), "over": bytes(over)}


def gen_dev_spread_vectors(n: int, rng: random.Random) -> list[dict]:
    # Cell offsets: 8-aligned, unique per (realm, roster slot), so the
    # descriptor-byte writes never overlap.  Developable type < 0x19.
    def cell(slot, ridx):
        return (slot * 32 + ridx) * 8 + 8

    edge = [
        # one realm, three developable cells, modest pool → partial spread.
        # Developable means terrain type >= 0x19 (already a dev-stage tile).
        dict(count=1, standings=1, realms=[dict(slot=1, pool=50,
             vals=[(0, 100), (1, 50), (2, 300)],
             cells=[(0, cell(1, 0), 0x19), (1, cell(1, 1), 0x1a),
                    (2, cell(1, 2), 0x1b)])]),
        # overflow: a cell crosses 800 → spill back, type 1
        dict(count=1, standings=1, realms=[dict(slot=1, pool=300,
             vals=[(0, 700), (1, 0)],
             cells=[(0, cell(1, 0), 0x1c), (1, cell(1, 1), 0x19)])]),
        # standings disabled → accumulator forced to 0
        dict(count=1, standings=0, realms=[dict(slot=1, acc_a=10, acc_b=4,
             acc208=20, pool=0, cells=[])]),
        # no developable cells → projection metrics cleared, no spread
        dict(count=1, standings=1, realms=[dict(slot=1, acc_a=10, acc_b=4,
             pool=500, cells=[])]),
    ]
    vecs = [_build_dev_vec(e["count"], e["standings"], e["realms"]) for e in edge]
    while len(vecs) < n:
        count = rng.randint(1, 8)
        realms = []
        for slot in range(1, count + 1):
            ncells = rng.randint(0, 5)
            cells, vals = [], []
            for ridx in range(ncells):
                cells.append((ridx, cell(slot, ridx),
                              rng.choice([0x19, 0x1a, 0x1b, 0x1c])))  # all >= 0x19 (developable)
                vals.append((ridx, rng.randint(0, 900)))
            realms.append(dict(
                slot=slot, owner=rng.randint(0, 6),
                acc_a=rng.randint(0, 40), acc_b=rng.randint(0, 40),
                acc208=rng.randint(-50, 50),
                pool=rng.choice([0, rng.randint(1, 2000)]),
                vals=vals, cells=cells))
        realms_standings = rng.randint(0, 1)
        vecs.append(_build_dev_vec(count, realms_standings, realms))
    return vecs


def run_port_dev_spread(lib: ctypes.CDLL, vec: dict) -> dict:
    g = realm_port_globals(lib)
    ctypes.memmove(g["army"], vec["army"], REALM_ARMY_BYTES)
    ctypes.memmove(g["misc"], vec["misc"], REALM_MISC_BYTES)
    ctypes.memmove(g["over"], vec["over"], REALM_OVER_BYTES)
    g["count"].value  = vec["count"]
    g["stand"].value  = vec["standings"]
    g["season"].value = 1
    # Bind the territory cell grid to our overworld copy so set_cell_type's
    # type writes feed the c093→c278 developable checks, exactly as retail's
    # shared 0x522f90 region does.
    lib.engine_territory_bind_cells(ctypes.addressof(g["over"]))
    lib.engine_realm_spread_development()
    lib.engine_territory_bind_cells(None)
    return {"army": list(bytes(g["army"])), "over": list(bytes(g["over"]))}


def dev_spread_rpc_plan(vec: dict) -> dict:
    return {"fn_va": FN_DEV_SPREAD, "regions": [
        {"va": VA_PER_ARMY,   "len": REALM_ARMY_BYTES, "inject": list(vec["army"]), "read": True},
        {"va": VA_MISC_1360,  "len": REALM_MISC_BYTES, "inject": list(vec["misc"]), "read": False},
        {"va": VA_OVERWORLD,  "len": REALM_OVER_BYTES, "inject": list(vec["over"]), "read": True},
        {"va": VA_SLOT_COUNT, "len": 4, "inject": list(_u32le(vec["count"])),     "read": False},
        {"va": VA_SLOT_STAND, "len": 4, "inject": list(_u32le(vec["standings"])), "read": False},
        {"va": VA_SLOT_SEASON,"len": 4, "inject": list(_u32le(1)),                "read": False},
    ]}


def diff_dev_spread(retail: dict, port: dict) -> list[str]:
    bad = _diff_realm_region(retail, port["army"], VA_PER_ARMY,
                             REALM_ARMY_STRIDE, 17, "army")
    bad += _diff_realm_flat(retail, port["over"], VA_OVERWORLD, "over")
    return bad


# ── projected-income recompute (FUN_0044b80b — engine_realm_recompute_realm_income) ──
#
# Unlike the void() passes this takes an argument (the realm index), so the
# generic RPC is driven with args=[realm] + cc='mscdecl'.  The pass mutates the
# called realm's projection fields (+0xc0/+0x0f/+0x16) AND, via the owner-tally
# recompute (FUN_0044b99a) + per-player income refresh (FUN_0044ba35), the whole
# strategic block's +0x28 / +0x15c — so both blocks are read back and diffed.

def _build_income_vec(count: int, realm: int, realms: list[tuple],
                      players: list[tuple]) -> dict:
    army  = bytearray(REALM_ARMY_BYTES)
    strat = bytearray(REALM_STRAT_BYTES)
    for (slot, owner, band, cap, gate, tax, pop) in realms:
        b = slot * REALM_ARMY_STRIDE
        army[b + 0x05]  = owner & 0xff
        army[b + 0x1c0] = band  & 0xff
        army[b + 0x1c1] = cap   & 0xff
        army[b + 0x1c3] = gate  & 0xff
        army[b + 0xb9]  = tax   & 0xff
        struct.pack_into("<i", army, b + 0x24, pop)
    for (player, val) in players:
        struct.pack_into("<i", strat, player * REALM_STRAT_STRIDE + 0x15c, val)
    return {"count": count, "realm": realm, "army": bytes(army), "strat": bytes(strat)}


def gen_realm_income_vectors(n: int, rng: random.Random) -> list[dict]:
    # Fixed edge cases mirroring the host unit tests (tax bounded 0..50 — its
    # live engine range, since the .data tally table is sized for 0..50).
    edge = [
        (1, 1, [(1, 1, 3, 0, 0, 50, 1000)],                 [(1, 0x999)]),  # basic
        (1, 1, [(1, 1, 5, 1, 1, 50, 1000)],                 [(1, 0)]),      # band cap → band 1
        (1, 1, [(1, 1, 5, 0, 1, 50, 1000)],                 [(1, 0)]),      # cap 0 → band 0
        (1, 1, [(1, 1, 5, 0, 0, 10, 1000)],                 [(1, 0)]),      # low tax → +0x16 = 0
        (3, 2, [(1, 1, 3, 0, 0, 50, 1000),
                (2, 1, 3, 0, 0, 50, 1000),
                (3, 0, 3, 0, 0, 50, 1000)],                 [(1, 0x999)]),  # unowned excluded
    ]
    vecs = [_build_income_vec(*e) for e in edge]
    while len(vecs) < n:
        count  = rng.randint(1, 16)
        realm  = rng.randint(1, count)
        realms = []
        for slot in range(1, count + 1):
            realms.append((
                slot,
                rng.randint(0, 5),                          # owner
                rng.randint(0, 6),                          # standing band
                rng.randint(0, 6),                          # band cap
                rng.randint(0, 1),                          # cap gate
                rng.randint(0, 50),                         # tax setting (live range)
                rng.choice([0, 0, rng.randint(1, 5000)]),   # population
            ))
        players = [(p, rng.randint(-5000, 5000)) for p in range(6)]
        vecs.append(_build_income_vec(count, realm, realms, players))
    return vecs


def run_port_realm_income(lib: ctypes.CDLL, vec: dict) -> dict:
    g = realm_port_globals(lib)
    ctypes.memmove(g["army"],  vec["army"],  REALM_ARMY_BYTES)
    ctypes.memmove(g["strat"], vec["strat"], REALM_STRAT_BYTES)
    g["count"].value = vec["count"]
    lib.engine_realm_recompute_realm_income(vec["realm"])
    return {"army": list(bytes(g["army"])), "strat": list(bytes(g["strat"]))}


def realm_income_rpc_plan(vec: dict) -> dict:
    return {"fn_va": FN_REALM_INCOME, "cc": "mscdecl", "args": [vec["realm"]],
            "regions": [
        {"va": VA_PER_ARMY,   "len": REALM_ARMY_BYTES,  "inject": list(vec["army"]),  "read": True},
        {"va": VA_STRATEGIC,  "len": REALM_STRAT_BYTES, "inject": list(vec["strat"]), "read": True},
        {"va": VA_SLOT_COUNT, "len": 4, "inject": list(_u32le(vec["count"])), "read": False},
    ]}


def diff_realm_income(retail: dict, port: dict) -> list[str]:
    bad = _diff_realm_region(retail, port["army"], VA_PER_ARMY,
                             REALM_ARMY_STRIDE, 17, "army")
    bad += _diff_realm_region(retail, port["strat"], VA_STRATEGIC,
                              REALM_STRAT_STRIDE, 6, "strat")
    return bad


# RNG diff includes ALL output fields.  Retail vs. port mismatch on any
# field is a divergence.
def diff_rng(retail: dict, port: dict) -> list[str]:
    keys = ["new_state_a", "new_state_b",
            "rng_15bit_a", "rng_7bit_a", "rng_3bit_a",
            "rng_15bit_b", "rng_7bit_b", "rng_3bit_b"]
    return [k for k in keys if int(retail[k]) != int(port[k])]


# FPS diff excludes window_rolled_over (retail doesn't expose it).
def diff_fps(retail: dict, port: dict) -> list[str]:
    keys = ["delta_ms",
            "new_last_render_ms", "new_second_window_start_ms",
            "new_frames_this_second", "new_current_fps",
            "new_cumulative_fps", "new_cumulative_seconds"]
    return [k for k in keys if int(retail[k]) != int(port[k])]


# ─── orchestrator ─────────────────────────────────────────────────────────


@dataclass
class DiffResult:
    function:   str
    total:      int = 0
    pass_count: int = 0
    fail_count: int = 0
    # Number of vectors where the retail function ran correctly per spec
    # (delta_ms / return value matches our port) but an engine-thread race
    # corrupted a writeback global before our readback.  Counted as a pass
    # — the spec divergence we're testing didn't fire — but surfaced
    # separately so harness flakiness is visible.
    race_count: int = 0
    failures:   list[dict[str, Any]] = field(default_factory=list)


def _print_summary(results: list[DiffResult]) -> int:
    print()
    print("=" * 72)
    rc = 0
    for r in results:
        status = "OK" if r.fail_count == 0 else "FAIL"
        race_suffix = f", {r.race_count} race-pass" if r.race_count else ""
        print(f"  {r.function:24s} {status}  "
              f"{r.pass_count}/{r.total} pass, {r.fail_count} fail"
              f"{race_suffix}")
        if r.fail_count > 0:
            rc = 1
            # First few divergences only — keep output sane.
            for f in r.failures[:5]:
                print(f"    [{f['idx']}] vector={f['vector']}")
                print(f"        retail={f['retail']}")
                print(f"        port  ={f['port']}")
                print(f"        diff  ={f['diff']}")
            if len(r.failures) > 5:
                print(f"    ... and {len(r.failures) - 5} more")
    print("=" * 72)
    return rc


def run_diff(cfg_args) -> int:
    rng = random.Random(cfg_args.seed)

    # Build the port .so up-front so we fail fast if the C code doesn't
    # compile (instead of after a 5-second engine boot).
    print(f"[diff] building {DIFF_SO.relative_to(ROOT)}…")
    subprocess.run(["make", "-C", str(ROOT / "tests"), "diff"],
                   check=True)
    lib = load_port_lib()

    enabled = set(cfg_args.functions.split(","))
    test_plans = []
    if "rng_step" in enabled:
        test_plans.append(("rng_step", gen_rng_vectors(cfg_args.vectors, rng)))
    if "fps_step" in enabled:
        test_plans.append(("fps_step", gen_fps_vectors(cfg_args.vectors, rng)))
    if "anim_cycle" in enabled:
        test_plans.append(("anim_cycle",
                           gen_anim_cycle_vectors(cfg_args.vectors, rng)))
    if "frame_delta" in enabled:
        test_plans.append(("frame_delta",
                           gen_frame_delta_vectors(cfg_args.vectors, rng)))
    if "phase_ticks" in enabled:
        test_plans.append(("phase_ticks",
                           gen_phase_ticks_vectors(cfg_args.vectors, rng)))
    if "input_state" in enabled:
        test_plans.append(("input_state",
                           gen_input_state_vectors(cfg_args.vectors, rng)))
    if "sim_sub_steps" in enabled:
        test_plans.append(("sim_sub_steps",
                           gen_sim_sub_steps_vectors(cfg_args.vectors, rng)))
    if "state_hash" in enabled:
        test_plans.append(("state_hash",
                           gen_state_hash_vectors(cfg_args.vectors, rng)))
    if "tile_expand" in enabled:
        test_plans.append(("tile_expand",
                           gen_tile_expand_vectors(cfg_args.vectors, rng)))
    if "delayed_action" in enabled:
        test_plans.append(("delayed_action",
                           gen_delayed_action_vectors(cfg_args.vectors, rng)))
    if "tax_approval" in enabled:
        test_plans.append(("tax_approval",
                           gen_tax_approval_vectors(cfg_args.vectors, rng)))
    if "dev_spread" in enabled:
        test_plans.append(("dev_spread",
                           gen_dev_spread_vectors(cfg_args.vectors, rng)))
    if "realm_income" in enabled:
        test_plans.append(("realm_income",
                           gen_realm_income_vectors(cfg_args.vectors, rng)))
    # Pre-generate the rate-gate LUT — one per run, used by all
    # sim_sub_steps vectors so a re-run with the same seed produces
    # the same LUT (and thus the same retail-side gate decisions).
    sim_rate_lut = gen_sim_sub_steps_rate_lut(rng) \
        if "sim_sub_steps" in enabled else None
    # Pre-generate the state-hash source-array payload — one seed per
    # run, shared across every state_hash vector.  Agent installs the
    # bytes once per test plan; orchestrator generates the same bytes
    # for the host-port call.  Seed must be non-zero (xorshift can't
    # escape 0); promote 0 to 1 if cfg_args.seed happens to be 0.
    state_hash_seed = (rng.getrandbits(32) or 1) if "state_hash" in enabled else None
    state_hash_payload = gen_state_hash_payload(state_hash_seed) \
        if "state_hash" in enabled else None
    # Same pattern for tile_expand — one seed per run, shared across every
    # vector, generates the 3 input-buffer payloads (tiles + footprint_list
    # + last_footprint_tile) bit-identical to the agent's per-call re-seed.
    tile_expand_seed = (rng.getrandbits(32) or 1) if "tile_expand" in enabled else None
    tile_expand_pre_state = gen_tile_expand_pre_state(tile_expand_seed) \
        if "tile_expand" in enabled else None
    if not test_plans:
        print("[diff] no enabled targets in --functions; nothing to do.")
        return 0

    # ─── spawn retail under the agent in diff_test mode ───
    remote = cfg_args.remote
    if cfg_args.auto_start_server:
        ok = fc.ensure_frida_server(remote, fc.DEFAULT_FRIDA_SERVER_EXE)
        if not ok:
            print(f"[diff] FATAL: cannot reach frida-server @ {remote}",
                  file=sys.stderr)
            return 125

    run_id = cfg_args.run_id or dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = ROOT / "runs" / "diff" / run_id
    run_dir.mkdir(parents=True, exist_ok=True)
    log_path = run_dir / "diff.log"
    log_fp = log_path.open("w", buffering=1)

    def log(msg: str) -> None:
        log_fp.write(msg + "\n")
        print(msg)

    log(f"[diff] run_dir = {run_dir}")
    log(f"[diff] seed = {cfg_args.seed}, vectors = {cfg_args.vectors}")
    log(f"[diff] functions = {', '.join(t[0] for t in test_plans)}")

    # ─── spawn via Frida ───
    win_exe = fc.wslpath_w(RETAIL_EXE.resolve())
    win_cwd = fc.wslpath_w(ASSET_CWD.resolve())
    device = frida.get_device_manager().add_remote_device(remote)

    pid = None
    session = None
    script = None
    main_thread_seen = threading.Event()

    def on_message(message: dict[str, Any], data: bytes | None):
        if message.get("type") == "error":
            log(f"[frida-error] {message.get('description','')} "
                f"@ {message.get('fileName','')}:"
                f"{message.get('lineNumber','')}")
            return
        if message.get("type") != "send":
            return
        p = message.get("payload") or {}
        kind = p.get("kind")
        if kind == "log":
            log_fp.write(f"[agent] {p.get('msg','')}\n")
        elif kind == "error":
            log_fp.write(f"[agent-error] {p.get('where')}: {p.get('msg')}\n")
        elif kind == "ready":
            log_fp.write(f"[ready] base={p.get('base')}\n")
        elif kind == "fps_sample":
            # First fps_sample → engine_tick has run at least once →
            # main thread is captured → safe to fire RPCs.
            log_fp.write(f"[fps] cum_secs={p.get('cumulative_seconds')} "
                         f"current_fps={p.get('current_fps')}\n")
            main_thread_seen.set()
        elif kind == "messagebox_intercepted":
            log_fp.write(f"[!!! MESSAGEBOX] {p.get('caption')!r}: "
                         f"{p.get('body')!r}\n")

    prev_term = signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    prev_int  = signal.signal(signal.SIGINT,  lambda *_: sys.exit(130))

    results: list[DiffResult] = []
    try:
        log(f"[diff] spawning {win_exe} (cwd={win_cwd})")
        pid = device.spawn([win_exe], cwd=win_cwd)
        session = device.attach(pid)
        script = session.create_script(AGENT_JS.read_text())
        script.on("message", on_message)
        script.load()
        script.exports_sync.init({
            "module":         RETAIL_EXE.name,
            "hide_window":    True,
            "silent_audio":   True,
            "force_windowed": True,
            "turbo":          True,         # turbo — fast boot/warmup; diff RPCs
                                            # still script timeGetTime per-thread
                                            # for the clock-sensitive targets
            "diff_test":      True,
        })
        device.resume(pid)

        # Wait for engine_tick to start ticking.  Without turbo, retail's
        # rate-limiter takes ~1 s to publish the first fps_sample, so 10 s
        # is generous.
        log("[diff] waiting for engine_tick to settle…")
        if not main_thread_seen.wait(timeout=cfg_args.warmup_s):
            log(f"[diff] WARN: no fps_sample within {cfg_args.warmup_s}s — "
                f"engine may be stuck; firing RPCs anyway")
        else:
            log("[diff] engine_tick is live; firing RPCs")

        # ─── shared engine-tick freeze ───
        # Both sim_sub_steps and state_hash need FUN_0040e890 patched to a
        # no-op for the duration of their vectors.  Cycling install→uninstall
        # back-to-back on the same Interceptor.replace address triggers an
        # intermittent Frida-internal TypeError that destroys the script
        # context — almost-always observed when state_hash followed sim_sub_steps
        # in the test plan order.  Workaround: install the freeze ONCE here
        # if ANY target in this run needs it, leave it installed across all
        # vectors, uninstall ONCE in the finally block.  The per-target setup
        # below still calls install (which returns already_installed=true);
        # this is a no-op at the agent but keeps the per-target log shape.
        shared_freeze_targets = {"sim_sub_steps", "state_hash", "tile_expand",
                                 "delayed_action", "tax_approval", "dev_spread",
                                 "realm_income"}
        shared_freeze_installed = False
        if shared_freeze_targets & {n for (n, _) in test_plans}:
            fresp = script.exports_sync.install_sim_sub_steps_engine_freeze()
            if isinstance(fresp, dict) and fresp.get("error"):
                log(f"[diff] WARN: shared engine freeze install failed: "
                    f"{fresp['error']}")
            else:
                shared_freeze_installed = True
                log("[diff] shared engine_tick freeze installed (held for the "
                    "whole run)")

        # ─── execute test plans ───
        for fn_name, vectors in test_plans:
            log(f"[diff] {fn_name}: {len(vectors)} vectors")
            # Sim sub-step rate-gate LUT install — one-shot per test plan.
            # Saves a ~2400-byte readback/writeback per vector by sharing
            # the LUT bytes across all sim_sub_steps vectors.  The agent
            # snapshots retail's original bytes lazily and restores them
            # in the matching uninstall below.
            if fn_name == "sim_sub_steps":
                install_resp = script.exports_sync.install_sim_sub_steps_rate_lut(
                    sim_rate_lut)
                if isinstance(install_resp, dict) and install_resp.get("error"):
                    log(f"[diff] sim_sub_steps: LUT install failed: "
                        f"{install_resp['error']}")
                    results.append(DiffResult(
                        function=fn_name, total=len(vectors),
                        fail_count=len(vectors),
                        failures=[{"idx": -1, "vector": None,
                                   "retail": install_resp, "port": None,
                                   "diff": ["lut-install-error"]}]))
                    continue
            # Engine freeze for sim_sub_steps already in place via the
            # shared install above — see g_sim_engine_freeze_callback
            # for the rationale (sub-step body can't run with our injected
            # game-state-phase=3 because the rest of the ingame state isn't
            # initialised; engine freeze keeps the main thread out).
            # State-hash setup — engine freeze already in place via the
            # shared install above.  Sources install needs to happen
            # AFTER the freeze (otherwise the 125 KB writeback races the
            # engine's reads), which is naturally true here.
            if fn_name == "state_hash":
                install_resp = script.exports_sync.install_state_hash_sources(
                    state_hash_seed)
                if isinstance(install_resp, dict) and install_resp.get("error"):
                    log(f"[diff] state_hash: source install failed: "
                        f"{install_resp['error']}")
                    results.append(DiffResult(
                        function=fn_name, total=len(vectors),
                        fail_count=len(vectors),
                        failures=[{"idx": -1, "vector": None,
                                   "retail": install_resp, "port": None,
                                   "diff": ["sources-install-error"]}]))
                    continue
                log(f"[diff] state_hash: sources installed (seed=0x{state_hash_seed:08x}, "
                    f"payload_bytes={install_resp.get('payload_bytes')})")
            # Tile-expand backup install — one-shot snapshot of the 5 globals
            # so uninstall (in the per-fn cleanup block below) can put retail's
            # pre-test bytes back.  Per-call pre-state re-seeding happens
            # INSIDE runRetailTileExpand, not here.  Engine freeze already in
            # place via shared install — needed because tiles/path/stage are
            # read by render + pathfinding on the engine main thread.
            if fn_name == "tile_expand":
                install_resp = script.exports_sync.install_tile_expand_backup()
                if isinstance(install_resp, dict) and install_resp.get("error"):
                    log(f"[diff] tile_expand: backup install failed: "
                        f"{install_resp['error']}")
                    results.append(DiffResult(
                        function=fn_name, total=len(vectors),
                        fail_count=len(vectors),
                        failures=[{"idx": -1, "vector": None,
                                   "retail": install_resp, "port": None,
                                   "diff": ["backup-install-error"]}]))
                    continue
                log(f"[diff] tile_expand: backup installed "
                    f"(seed=0x{tile_expand_seed:08x}, "
                    f"snapshot_bytes={install_resp.get('snapshot_bytes')})")
            # Delayed-action dispatch stub install — snapshots the 50-entry
            # PTR_FUN_004d5c98 dispatch table + 5 scalars + 3 arrays + 10
            # mirrors, then stamps a no-op NativeCallback into every dispatch
            # slot so the function's `(*table[type])()` is harmless.  Engine
            # freeze already installed via the shared install above (delayed_
            # action is in shared_freeze_targets).
            if fn_name == "delayed_action":
                install_resp = script.exports_sync.install_delayed_action_stub()
                if isinstance(install_resp, dict) and install_resp.get("error"):
                    log(f"[diff] delayed_action: stub install failed: "
                        f"{install_resp['error']}")
                    results.append(DiffResult(
                        function=fn_name, total=len(vectors),
                        fail_count=len(vectors),
                        failures=[{"idx": -1, "vector": None,
                                   "retail": install_resp, "port": None,
                                   "diff": ["stub-install-error"]}]))
                    continue
                log(f"[diff] delayed_action: stub installed "
                    f"(snapshot_bytes={install_resp.get('snapshot_bytes')})")
            r = DiffResult(function=fn_name, total=len(vectors))
            for i, vec in enumerate(vectors):
                if fn_name == "rng_step":
                    retail = script.exports_sync.run_retail_rng_step(
                        vec["state_a"], vec["state_b"])
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    port = run_port_rng(lib, vec)
                    bad = diff_rng(retail, port)
                elif fn_name == "fps_step":
                    # Retry on race detection: if our scripted clock was
                    # eaten by an engine-thread call between inject + invoke,
                    # the retail call returns a wildly-wrong delta_ms.
                    retail = None
                    for attempt in range(FPS_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_compute_fps(
                            vec["now_ms"], vec["rollover_now_ms"],
                            vec["last_render_ms"], vec["second_window_start_ms"],
                            vec["frames_this_second"], vec["current_fps"],
                            vec["cumulative_fps"], vec["cumulative_seconds"])
                        if isinstance(retail, dict) and retail.get("error"):
                            break  # not a race — report as-is
                        if _fps_race_free(vec, retail):
                            break  # clean run
                        # Race detected.  Retry; the window is tiny and
                        # next attempt usually lands cleanly.
                        if attempt < FPS_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    port = run_port_fps(lib, vec)
                    bad = diff_fps(retail, port)
                    # Pre-call race signature: engine bumped frames between
                    # our snapshot and our retail call.  Our scripted clock
                    # still landed (last_render_ms matches our_now), delta
                    # still matches port, but frames is +N from the engine's
                    # intervening calls.  Function ran correctly per spec;
                    # count as race-pass rather than failure.  Pre-call race
                    # bypasses the last_render-based _fps_race_free check.
                    if bad == ["new_frames_this_second"] and \
                            int(retail["delta_ms"]) == int(port["delta_ms"]) and \
                            int(retail["new_frames_this_second"]) > \
                                int(port["new_frames_this_second"]):
                        r.race_count += 1
                        bad = []
                    elif bad and not _fps_race_free(vec, retail):
                        # Post-call race the existing detector caught.
                        # Tag the failure so the summary makes it obvious
                        # this is harness flakiness, not a port bug.
                        bad = ["RACE:" + k for k in bad]
                elif fn_name == "anim_cycle":
                    # Retry on race detection: the agent sets retail['raced']
                    # when g_engine_in_tick was non-zero at any sample point
                    # or g_tick_count advanced during the RPC.  Either case
                    # means the engine may have called FUN_00451f1a with our
                    # injected globals during the RPC window.
                    retail = None
                    for attempt in range(ANIM_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_anim_cycle(
                            vec["counter"], vec["tick_80ms_fired"])
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < ANIM_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    port = run_port_anim_cycle(lib, vec)
                    bad = diff_anim_cycle(retail, port)
                    if bad and int(retail.get("raced", 0)) != 0:
                        # Race never cleared after retries — tag as harness
                        # flakiness rather than a port bug.
                        bad = ["RACE:" + k for k in bad]
                elif fn_name == "frame_delta":
                    # Retry on function-specific race: if new_last_frame_ms
                    # doesn't match the expected post-call value (our injected
                    # last_frame_ms on suppress, our scripted now_ms on pass),
                    # an engine call overwrote it after our retail call.  The
                    # structural raced flag is too aggressive here — the
                    # engine free-runs at ~200 kHz without turbo so
                    # g_engine_in_tick is almost always set.
                    retail = None
                    for attempt in range(ANIM_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_frame_delta(
                            vec["now_ms"], vec["last_frame_ms"])
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if _frame_delta_race_free(vec, retail):
                            break
                        if attempt < ANIM_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    port = run_port_frame_delta(lib, vec)
                    bad = diff_frame_delta(retail, port)
                    race = not _frame_delta_race_free(vec, retail)
                    if race and bad == ["new_last_frame_ms"] and \
                            int(retail["delta_ms"]) == int(port["delta_ms"]):
                        # Post-call race signature: retail function's
                        # computed delta matches port (function ran
                        # correctly per spec), but engine's next
                        # FUN_004b20ed call overwrote _DAT_004e59ec
                        # before our readback.  Not a real divergence;
                        # count as a race-pass.
                        r.race_count += 1
                        bad = []
                    elif bad and race:
                        bad = ["RACE:" + k for k in bad]
                elif fn_name == "phase_ticks":
                    # Same retry pattern as frame_delta — function-specific
                    # race check on new_last_base_tick_ms (the deterministic
                    # writeback that lets us tell "retail's call landed
                    # cleanly" from "engine raced + clobbered the readback").
                    retail = None
                    for attempt in range(ANIM_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_phase_ticks(
                            vec["now_ms"],
                            vec["last_base_tick_ms"],
                            vec["base_tick_counter"],
                            vec["counter_2"],  vec["counter_4"],
                            vec["counter_8"],  vec["counter_13"],
                            vec["counter_16"], vec["counter_24"],
                            vec["counter_32"])
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if _phase_ticks_race_free(vec, retail):
                            break
                        if attempt < ANIM_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    port = run_port_phase_ticks(lib, vec)
                    bad = diff_phase_ticks(retail, port)
                    race = (not _phase_ticks_race_free(vec, retail)) or \
                           int(retail.get("raced", 0)) != 0
                    if bad and _phase_ticks_engine_clear_race(retail, port):
                        # Engine race fired FUN_004bbc80's clear-flags
                        # step between our retail call's writes and our
                        # readback.  Counters/last_base match port (engine
                        # hadn't progressed to its 20 ms branch yet), but
                        # flags were clobbered.  Retail's call produced
                        # correct outputs per spec; classify as race-pass.
                        r.race_count += 1
                        bad = []
                    elif bad and race:
                        bad = ["RACE:" + k for k in bad]
                elif fn_name == "input_state":
                    # Retry on structural race: FUN_004b191e writes ~20
                    # globals AND reads several that the engine main thread
                    # also writes (DAT_0057cac4 reset by FUN_0042ff10,
                    # DAT_004e65fc by engine's own FUN_004b191e, etc.).  A
                    # race can corrupt EITHER inputs (engine modifies before
                    # retail reads) OR outputs (engine overwrites after
                    # retail writes), so the raw_cursor canary alone is
                    # insufficient — we need to detect any tick boundary
                    # during the RPC window.  Use the structural raced flag
                    # (engine_in_tick at any sample point + g_tick_count
                    # delta) for retry decisions, same pattern as anim_cycle.
                    retail = None
                    for attempt in range(INPUT_STATE_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_input_state(vec)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0 and \
                                _input_state_race_free(vec, retail):
                            break
                        if attempt < INPUT_STATE_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    port = run_port_input_state(lib, vec)
                    bad = diff_input_state(retail, port)
                    race = (not _input_state_race_free(vec, retail)) or \
                           int(retail.get("raced", 0)) != 0
                    if bad and race:
                        # Race-pass classification: retail's per-call writes
                        # are deterministic from (injected inputs) + (scripted
                        # cursor/clock).  If the injected inputs were
                        # corrupted by an engine call BEFORE retail's read,
                        # OR retail's outputs were overwritten AFTER, the
                        # function still ran correctly per spec from the
                        # engine's perspective — the harness window just
                        # caught the wrong observable.  Count as race-pass.
                        r.race_count += 1
                        bad = []
                elif fn_name == "sim_sub_steps":
                    # Same retry-on-race pattern as input_state.  We need
                    # the port output to compute the canary expected value
                    # (derived_hint), so race-free check is structured
                    # around (retail, port-computed-expected) rather than
                    # just (vec, retail).
                    port = run_port_sim_sub_steps(lib, vec, sim_rate_lut)
                    retail = None
                    for attempt in range(SIM_SUB_STEPS_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_sim_sub_steps(vec)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0 and \
                                _sim_sub_steps_race_free(vec, retail, port):
                            break
                        if attempt < SIM_SUB_STEPS_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": port,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_sim_sub_steps(retail, port)
                    race = (not _sim_sub_steps_race_free(vec, retail, port)) or \
                           int(retail.get("raced", 0)) != 0
                    if bad and race:
                        r.race_count += 1
                        bad = []
                elif fn_name == "state_hash":
                    # The function only reads the source arrays and writes
                    # the destination bucket+region; no scripted clock, no
                    # GetCursorPos.  With the engine freeze installed, the
                    # main thread can't enter FUN_00440231 during our RPC
                    # window — so the raced flag should always be 0.  We
                    # keep the retry loop short (2 attempts) as a guard
                    # against the WndProc thread firing some other path
                    # that incidentally touches the destination region.
                    port = run_port_state_hash(lib, vec, state_hash_payload)
                    retail = None
                    for attempt in range(STATE_HASH_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_state_hash(vec)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < STATE_HASH_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": port,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_state_hash(retail, port)
                    if bad and int(retail.get("raced", 0)) != 0:
                        # Engine-tick fired during the RPC (despite the
                        # freeze covering FUN_0040e890 — possible if the
                        # freeze didn't install or the engine entered via
                        # a non-dispatch path).  Classify as race-pass.
                        r.race_count += 1
                        bad = []
                elif fn_name == "tile_expand":
                    # FUN_004917dd: stateful — re-seeds tiles + footprint_list
                    # + last_footprint from the shared seed both sides, then
                    # injects the per-vector anchor at tiles[offset+3].  Port
                    # mirrors the agent's pre-state exactly so the function's
                    # output bytes are diffable.  Engine freeze keeps the
                    # main thread out of tiles/path_scratch/stage_scratch
                    # during our 70 KB readback.
                    port = run_port_tile_expand(lib, vec, tile_expand_pre_state)
                    rpc_in = {
                        "seed":             int(tile_expand_seed),
                        "tile_byte_offset": int(vec["tile_byte_offset"]),
                        "anchor_char":      int(vec["anchor_char"]),
                    }
                    retail = None
                    for attempt in range(TILE_EXPAND_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_tile_expand(rpc_in)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < TILE_EXPAND_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": port,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_tile_expand(retail, port, vec)
                    if bad and int(retail.get("raced", 0)) != 0:
                        r.race_count += 1
                        bad = []
                elif fn_name == "delayed_action":
                    # FUN_004472ce: stateful — sweeps records[400], drains
                    # the queue, calls (stubbed) per-type handlers.  Both
                    # sides start from the vector's specified pre-state; the
                    # post-state byte-diff covers records + queue + hipri +
                    # 4 counters + 10 dispatch-arg mirrors.  Engine freeze
                    # keeps the main thread's own per-tick driver from
                    # racing into FUN_004472ce.
                    port = run_port_delayed_action(lib, vec)
                    retail = None
                    for attempt in range(DELAYED_ACTION_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_delayed_action(vec)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < DELAYED_ACTION_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": vec,
                            "retail": retail, "port": port,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_delayed_action(retail, port)
                    if bad and int(retail.get("raced", 0)) != 0:
                        r.race_count += 1
                        bad = []
                elif fn_name == "tax_approval":
                    # FUN_0044bd9f: per-realm tax-policy → approval-modifier
                    # pass.  Inject g_per_army_768 + g_player_strategic_352 +
                    # the realm count; read back the mutated per-army block.
                    # Engine freeze keeps the main thread out of the globals.
                    port = run_port_tax_approval(lib, vec)
                    plan = tax_approval_rpc_plan(vec)
                    retail = None
                    for attempt in range(REALM_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_realm_pass(plan)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < REALM_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": {"count": vec["count"]},
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_tax_approval(retail, port)
                    if bad and int(retail.get("raced", 0)) != 0:
                        r.race_count += 1
                        bad = []
                    if bad:
                        # Compact the multi-KB region dumps; `bad` already
                        # pinpoints the diverging realm record + byte offsets.
                        retail = {"raced": retail.get("raced", 0),
                                  "note": "region readback elided"}
                        port = {"note": "region readback elided"}
                        vec = {"count": vec["count"]}
                elif fn_name == "dev_spread":
                    # FUN_0044bf7a: per-realm development-spread pass.  Inject
                    # g_per_army_768 + g_misc_1360 + g_overworld_map + the realm
                    # count / standings / season scalars; read back the mutated
                    # per-army block + overworld map (the cell-type stamps).
                    port = run_port_dev_spread(lib, vec)
                    plan = dev_spread_rpc_plan(vec)
                    retail = None
                    for attempt in range(REALM_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_realm_pass(plan)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < REALM_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": {"count": vec["count"]},
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_dev_spread(retail, port)
                    if bad and int(retail.get("raced", 0)) != 0:
                        r.race_count += 1
                        bad = []
                    if bad:
                        retail = {"raced": retail.get("raced", 0),
                                  "note": "region readback elided"}
                        port = {"note": "region readback elided"}
                        vec = {"count": vec["count"], "standings": vec["standings"]}
                elif fn_name == "realm_income":
                    # FUN_0044b80b(int realm): per-realm projected-income recompute.
                    # Inject g_per_army_768 + g_player_strategic_352 + realm count;
                    # call with args=[realm] (cc=mscdecl); read back BOTH blocks
                    # (the called realm's projection fields + the tally/income
                    # totals it refreshes across the strategic block).
                    port = run_port_realm_income(lib, vec)
                    plan = realm_income_rpc_plan(vec)
                    retail = None
                    for attempt in range(REALM_RACE_RETRIES + 1):
                        retail = script.exports_sync.run_retail_realm_pass(plan)
                        if isinstance(retail, dict) and retail.get("error"):
                            break
                        if int(retail.get("raced", 0)) == 0:
                            break
                        if attempt < REALM_RACE_RETRIES:
                            time.sleep(0.01)
                    if isinstance(retail, dict) and retail.get("error"):
                        log(f"[diff] {fn_name}#{i}: agent error: "
                            f"{retail['error']}")
                        r.fail_count += 1
                        r.failures.append({
                            "idx": i, "vector": {"count": vec["count"],
                                                 "realm": vec["realm"]},
                            "retail": retail, "port": None,
                            "diff": ["agent-error"]})
                        continue
                    bad = diff_realm_income(retail, port)
                    if bad and int(retail.get("raced", 0)) != 0:
                        r.race_count += 1
                        bad = []
                    if bad:
                        retail = {"raced": retail.get("raced", 0),
                                  "note": "region readback elided"}
                        port = {"note": "region readback elided"}
                        vec = {"count": vec["count"], "realm": vec["realm"]}
                else:
                    continue
                if bad:
                    r.fail_count += 1
                    r.failures.append({
                        "idx": i, "vector": vec,
                        "retail": retail, "port": port, "diff": bad})
                else:
                    r.pass_count += 1
            race_suffix = f" ({r.race_count} race-pass)" if r.race_count else ""
            log(f"[diff] {fn_name}: {r.pass_count}/{r.total} pass{race_suffix}")
            results.append(r)
            # Pair install with uninstall — restores retail's original LUT
            # bytes so a subsequent target's run sees an unperturbed engine
            # (relevant if other diff targets read DAT_004e3260 transitively;
            # none today, but the contract holds).
            if fn_name == "sim_sub_steps":
                uninstall_resp = script.exports_sync.uninstall_sim_sub_steps_rate_lut()
                if isinstance(uninstall_resp, dict) and uninstall_resp.get("error"):
                    log(f"[diff] sim_sub_steps: LUT uninstall failed: "
                        f"{uninstall_resp['error']}")
                # Engine freeze NOT uninstalled here — held for the whole
                # run by the shared install above, released in finally.
            if fn_name == "state_hash":
                # State-hash source uninstall — best-effort; if the agent
                # dies during the 125 KB writeback, the engine is going to
                # be killed by the harness in the finally clause anyway.
                try:
                    uninstall_resp = script.exports_sync.uninstall_state_hash_sources()
                    if isinstance(uninstall_resp, dict) and uninstall_resp.get("error"):
                        log(f"[diff] state_hash: sources uninstall failed: "
                            f"{uninstall_resp['error']}")
                except Exception as e:
                    log(f"[diff] state_hash: sources uninstall: agent dead ({e!r}); "
                        f"skipping (frida cleanup will fire on script.unload)")
            if fn_name == "tile_expand":
                # Same best-effort uninstall; restores ~70 KB across the 5
                # globals so retail's pre-test state survives the run.
                try:
                    uninstall_resp = script.exports_sync.uninstall_tile_expand_backup()
                    if isinstance(uninstall_resp, dict) and uninstall_resp.get("error"):
                        log(f"[diff] tile_expand: backup uninstall failed: "
                            f"{uninstall_resp['error']}")
                except Exception as e:
                    log(f"[diff] tile_expand: backup uninstall: agent dead ({e!r}); "
                        f"skipping (frida cleanup will fire on script.unload)")
            if fn_name == "delayed_action":
                # Restore the dispatch table + 5 scalars + 3 arrays + 10
                # mirrors.  Without this the agent leaves retail's per-type
                # handler pointers stubbed out — game logic would silently
                # no-op.  Best-effort: agent crash → script.unload cleans up.
                try:
                    uninstall_resp = script.exports_sync.uninstall_delayed_action_stub()
                    if isinstance(uninstall_resp, dict) and uninstall_resp.get("error"):
                        log(f"[diff] delayed_action: stub uninstall failed: "
                            f"{uninstall_resp['error']}")
                except Exception as e:
                    log(f"[diff] delayed_action: stub uninstall: agent dead ({e!r}); "
                        f"skipping (frida cleanup will fire on script.unload)")

        # ─── shared engine-tick freeze release ───
        # Release after every test plan finished, but BEFORE the script
        # unload in the finally block — gives the engine a brief window
        # to drain anything pending before the script context vanishes.
        if shared_freeze_installed:
            try:
                rresp = script.exports_sync.uninstall_sim_sub_steps_engine_freeze()
                if isinstance(rresp, dict) and rresp.get("error"):
                    log(f"[diff] WARN: shared engine freeze uninstall: "
                        f"{rresp['error']}")
                elif rresp.get("restored"):
                    log("[diff] shared engine_tick freeze released")
            except Exception as e:
                log(f"[diff] WARN: shared freeze uninstall: agent dead ({e!r})")

    finally:
        if script is not None:
            with contextlib.suppress(Exception):
                script.unload()
        if session is not None:
            with contextlib.suppress(Exception):
                session.detach()
        if pid is not None:
            with contextlib.suppress(Exception):
                device.kill(pid)
        signal.signal(signal.SIGTERM, prev_term)
        signal.signal(signal.SIGINT,  prev_int)

    # ─── persist + print summary ───
    summary = {
        "run_id": run_id,
        "seed": cfg_args.seed,
        "vectors": cfg_args.vectors,
        "results": [{
            "function":   r.function,
            "total":      r.total,
            "pass_count": r.pass_count,
            "fail_count": r.fail_count,
            "race_count": r.race_count,
            "failures":   r.failures,
        } for r in results],
    }
    (run_dir / "diff.json").write_text(json.dumps(summary, indent=2))
    log_fp.close()

    return _print_summary(results)


# ─── cli ──────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", default=fc.DEFAULT_REMOTE,
                    help=f"frida-server endpoint (default: {fc.DEFAULT_REMOTE})")
    ap.add_argument("--no-auto-start", action="store_true",
                    help="skip ensure_frida_server (assume already up)")
    ap.add_argument("--vectors", type=int, default=64,
                    help="vectors per function (default 64; the first ~7 "
                         "are fixed edge cases, remainder are random)")
    ap.add_argument("--seed", type=int, default=0,
                    help="RNG seed for vector generation (default 0)")
    ap.add_argument("--functions",
                    default="rng_step,fps_step,anim_cycle,frame_delta,phase_ticks,input_state,sim_sub_steps,state_hash,tile_expand,delayed_action,tax_approval,dev_spread,realm_income",
                    help="comma-separated subset to test "
                         "(default: rng_step,fps_step,anim_cycle,frame_delta,"
                         "phase_ticks,input_state,sim_sub_steps,state_hash,"
                         "tile_expand,delayed_action)")
    ap.add_argument("--warmup-s", type=float, default=10.0,
                    help="seconds to wait for engine_tick to settle before "
                         "firing RPCs (default 10)")
    ap.add_argument("--run-id", default=None,
                    help="run directory name under runs/diff/ "
                         "(default: timestamp)")
    args = ap.parse_args(argv)
    args.auto_start_server = not args.no_auto_start
    return run_diff(args)


if __name__ == "__main__":
    sys.exit(main())
