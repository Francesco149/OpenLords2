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
  - engine_rng_step        (FUN_00404a46) — double-LFSR step
  - engine_compute_fps     (FUN_004b27bf) — rolling 1-sec FPS counter

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
    failures:   list[dict[str, Any]] = field(default_factory=list)


def _print_summary(results: list[DiffResult]) -> int:
    print()
    print("=" * 72)
    rc = 0
    for r in results:
        status = "OK" if r.fail_count == 0 else "FAIL"
        print(f"  {r.function:24s} {status}  "
              f"{r.pass_count}/{r.total} pass, {r.fail_count} fail")
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
            "turbo":          False,        # no turbo — diff RPCs control time
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

        # ─── execute test plans ───
        for fn_name, vectors in test_plans:
            log(f"[diff] {fn_name}: {len(vectors)} vectors")
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
                    if bad and not _fps_race_free(vec, retail):
                        # Even after retries the race is still firing.
                        # Tag the failure so the summary makes it obvious
                        # this is harness flakiness, not a port bug.
                        bad = ["RACE:" + k for k in bad]
                else:
                    continue
                if bad:
                    r.fail_count += 1
                    r.failures.append({
                        "idx": i, "vector": vec,
                        "retail": retail, "port": port, "diff": bad})
                else:
                    r.pass_count += 1
            log(f"[diff] {fn_name}: {r.pass_count}/{r.total} pass")
            results.append(r)

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
    ap.add_argument("--functions", default="rng_step,fps_step",
                    help="comma-separated subset to test "
                         "(default: rng_step,fps_step)")
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
