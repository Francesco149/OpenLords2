#!/usr/bin/env python3
"""Interactive probe daemon for retail Lords2.exe (live screenshot + input +
memory) — the exploratory front-end that DISCOVERS click coords / state signals
before they get compiled into a deterministic segtrace.

It spawns + attaches ONCE and keeps the frida session alive, so game state
persists across many `tools/probe.py <cmd>` invocations (unlike frida_capture,
which spawns/reaps per run).  Optionally bootstraps the game into a known state
with a segtrace (e.g. new-game -> clean map) first, then hands control to you.

    # boot to a clean strategic map, seed-pinned, then wait for probe commands:
    nix develop -c python3 tools/probe_daemon.py \
        --segtrace tools/traces/new_game_to_map.jsonl --rng-seed &
    tools/probe.py shot            # -> writes a PNG of the live screen
    tools/probe.py click 558 469   # -> left-click the End-Turn button (game px)
    tools/probe.py state           # -> curated turn/treasury VA dump
    tools/probe.py quit            # -> kill the game + stop the daemon

Commands are line-delimited JSON over 127.0.0.1:<port>; see tools/probe.py.
Turbo is OFF by default (real-time, so you can watch a click take effect);
pass --turbo to fast-forward AI think-time.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import sys
import threading
import time
from pathlib import Path

# Reuse frida_capture's bootstrap (frida-server autostart, path xlate, consts).
sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida  # noqa: E402
import frida_capture as fc  # noqa: E402

ROOT = fc.ROOT
CONTROL_JSON = ROOT / "runs" / "probe" / "daemon.json"   # client auto-discovery

# Windows message ids (subset the game reads).
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x200, 0x201, 0x202
WM_RBUTTONDOWN, WM_RBUTTONUP = 0x204, 0x205
WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x100, 0x101, 0x102

# Curated VA set for `state` — the turn/economy signals learned from the RE
# (g_player_strategic_352 @ 0x0057bf00, stride 0x160; +0x118 cash, +0x4 active,
# +0x5 eliminated, +0x2b rank, +0x7 personality).  Players occupy slots 1..4;
# slot 0 is neutral.
STRAT_BASE, STRAT_STRIDE = 0x0057BF00, 0x160
# In-game LMB clicks are gated by click_eligible (DAT_004e65f0): a real mouse
# keeps it armed; synthetic input must force it to 1 before the press or the
# resolver drops the click at the eligibility gate (found empirically).
CLICK_ELIGIBLE_VA = 0x004E65F0

STATE_SCALARS = {
    "screen": (0x004EAC50, "i32"),   # var_sim_current_screen (0=map, 0x27=scroll)
    "season": (0x0057C934, "i32"),   # s_slot_0057c934 — season 1..4, ticks EVERY turn
    "year":   (0x00553EDC, "i32"),   # HUD year — ticks only at Winter (every 4 turns)
    "tprog":  (0x00553FC8, "i32"),   # g_sim_turn_progress
    "tend":   (0x0056D6A4, "i32"),   # g_sim_turn_end_pending
    "epause": (0x0053E910, "i32"),   # modal-scroll freeze (auto_dismiss reads)
}
END_TURN_XY = (558, 469)             # decoded End-Turn button centre (DAT_004dc680[5])
SCROLL_DISMISS_XY = (300, 240)       # RMB anywhere on a screen-0x27 scroll dismisses it
READY_BASE = 0x00515D60              # g_sim_turn_ready_flags[10]


def _parse_rng_seed(spec):
    """Mirror of frida_capture's (nested, unimportable) parser: None -> None,
    'canonical' -> {} (agent's retail defaults), else 'a,b,single' ints."""
    if spec is None:
        return None
    if spec == "canonical":
        return {}
    out = {}
    for k, tok in zip(("a", "b", "single"), spec.split(",")):
        tok = tok.strip()
        if tok:
            out[k] = int(tok, 0) & 0xFFFFFFFF
    return out


def _state_specs(nplayers: int = 5) -> list[dict]:
    specs = [{"name": n, "va": va, "type": t} for n, (va, t) in STATE_SCALARS.items()]
    for i in range(nplayers):
        specs.append({"name": f"ready{i}", "va": READY_BASE + i * 4, "type": "i32"})
    for s in range(nplayers):
        base = STRAT_BASE + s * STRAT_STRIDE
        specs += [
            {"name": f"cash{s}", "va": base + 0x118, "type": "i32"},
            {"name": f"active{s}", "va": base + 0x004, "type": "u8"},
            {"name": f"elim{s}", "va": base + 0x005, "type": "u8"},
            {"name": f"rank{s}", "va": base + 0x02B, "type": "u8"},
            {"name": f"pers{s}", "va": base + 0x007, "type": "u8"},
        ]
    return specs


class ProbeDaemon:
    def __init__(self, args):
        self.args = args
        self.run_dir = ROOT / "runs" / "probe" / (args.run_id or "session")
        (self.run_dir / "shots").mkdir(parents=True, exist_ok=True)
        self.log = (self.run_dir / "daemon.log").open("w", buffering=1)
        self.device = None
        self.pid = None
        self.session = None
        self.script = None
        self._stop = threading.Event()
        # Latest frame delivered by the agent (kind:'frame').
        self._frame_lock = threading.Lock()
        self._frame = None                # (idx, w, h, palette_bytes, pixels)
        self._frame_meta = {}             # compositor diagnostics for latest idx
        self._frame_evt = threading.Event()
        self._frame_composite = None       # persistent presented 8-bit canvas
        self._frame_composite_dims = None
        self._frame_composite_screen = None
        self._rec = None                  # active recording {proc, thread, stop, ...}
        self._shot_seq = 0
        # engine-thread call results, keyed by call id
        self._call_results = {}
        self._call_evt = threading.Event()
        # accumulated call-trace events (agent flushes a batch every tick)
        self._ct_lock = threading.Lock()
        self._ct_events = []

    def _logline(self, s: str):
        self.log.write(s + "\n")

    # ── frida message pump ────────────────────────────────────────────────
    def _on_message(self, message, data):
        if message.get("type") == "error":
            self._logline(f"[frida-error] {message.get('description','')}")
            return
        if message.get("type") != "send":
            return
        p = message.get("payload") or {}
        kind = p.get("kind")
        if kind == "frame" and data is not None:
            try:
                w, h, fi = struct.unpack("<III", data[:12])
                pal = data[12:12 + 1024]
                px = data[12 + 1024: 12 + 1024 + w * h]
                px, comp_meta = self._compose_frame(w, h, pal, px, p.get("screen"))
                self._frame_meta = {fi: comp_meta}
                rec = self._rec
                if rec is not None and rec.get("proc") is not None:
                    self._rec_write(rec, w, h, pal, px)
                else:
                    with self._frame_lock:
                        self._frame = (fi, w, h, pal, px)
                    self._frame_evt.set()
            except Exception as e:  # noqa: BLE001
                self._logline(f"[frame] decode failed: {e}")
            return
        if kind == "call_result":
            self._call_results[p.get("id")] = p
            self._call_evt.set()
            return
        if kind == "call_trace_batch":
            with self._ct_lock:
                self._ct_events.extend(p.get("events", []))
            return
        if kind in ("log", "error"):
            self._logline(f"[agent] {p.get('msg') or p.get('where')}: "
                          f"{p.get('msg','')}")
            return
        # keep anchors / segtrace milestones visible in the log
        if kind in ("anchor", "segtrace_wait", "segtrace_done",
                    "messagebox_intercepted"):
            self._logline(f"[{kind}] {json.dumps(p)}")

    def _compose_frame(self, w, h, palette, pixels, screen):
        """Rebuild retail's persistent primary plane from dirty back frames.

        Lords II often redraws only one or more dirty rectangles; untouched
        back-buffer bytes are disposable and commonly index 0.  Treating that
        transient plane as a complete frame causes the black flashes seen in
        captures.  We retain the previous presented canvas and copy only 16px
        tiles that contain rendered pixels.  Near-full frames replace the
        canvas outright.  A screen-id transition also resets it, so genuinely
        sparse/black menus do not inherit the prior scene.
        """
        raw = bytes(pixels)
        if not getattr(self.args, "frame_composite", True):
            return raw, {"mode": "disabled"}
        dims = (w, h)
        changed_screen = (self._frame_composite_screen is not None and
                          screen is not None and screen != self._frame_composite_screen)
        reset = (self._frame_composite is None or
                 self._frame_composite_dims != dims or changed_screen)
        tile = 16
        active = []
        total = 0
        view = memoryview(raw)
        # "Empty" dirty-plane pixels are not guaranteed to use palette index
        # zero. Classify by the live RGB palette so a black-mapped nonzero index
        # cannot make every tile look occupied.
        visible_index = bytearray(256)
        for i in range(256):
            rgb = palette[i * 4:i * 4 + 3]
            visible_index[i] = 1 if rgb and max(rgb) > 12 else 0
        for y0 in range(0, h, tile):
            y1 = min(h, y0 + tile)
            for x0 in range(0, w, tile):
                x1 = min(w, x0 + tile)
                total += 1
                dirty = any(any(visible_index[v] for v in view[y * w + x0:y * w + x1])
                            for y in range(y0, y1))
                if dirty:
                    active.append((x0, y0, x1, y1))
        coverage = len(active) / total if total else 1.0
        # Full terrain/map frames light essentially every tile.  A conservative
        # 90% threshold leaves modal scroll/HUD dirty planes in composite mode.
        if reset or coverage >= 0.90:
            canvas = bytearray(raw)
            mode = "reset" if reset else "full"
        else:
            canvas = bytearray(self._frame_composite)
            for x0, y0, x1, y1 in active:
                for y in range(y0, y1):
                    a, b = y * w + x0, y * w + x1
                    canvas[a:b] = raw[a:b]
            mode = "dirty_tiles"
        self._frame_composite = canvas
        self._frame_composite_dims = dims
        self._frame_composite_screen = screen
        return bytes(canvas), {
            "mode": mode, "screen": screen, "tile_px": tile,
            "active_tiles": len(active), "total_tiles": total,
            "active_tile_fraction": round(coverage, 6),
        }

    # ── lifecycle ─────────────────────────────────────────────────────────
    def start(self):
        a = self.args
        if not fc.ensure_frida_server(a.remote, fc.DEFAULT_FRIDA_SERVER_EXE):
            raise SystemExit(f"frida-server unreachable at {a.remote}")
        exe = Path(a.exe).resolve()
        cwd = Path(a.cwd).resolve()
        argv = [fc.wslpath_w(exe)]
        win_cwd = fc.wslpath_w(cwd)
        self.device = frida.get_device_manager().add_remote_device(a.remote)
        fc._reap_by_name(self.device, exe.name, self.log)

        # Optional bootstrap segtrace (drive to a known state, then persist).
        seg_ops = []
        if a.segtrace:
            seg_ops = fc._load_segtrace(Path(a.segtrace).resolve())

        self._logline(f"[spawn] {argv} cwd={win_cwd}")
        self.pid = self.device.spawn(argv, cwd=win_cwd)
        self.session = self.device.attach(self.pid)
        self.script = self.session.create_script(fc.AGENT_JS.read_text())
        self.script.on("message", self._on_message)
        self.script.load()

        init_cfg = {
            "module": exe.name,
            "hide_window": True, "silent_audio": True, "force_windowed": True,
            "keep_retail_palette": True,
            "turbo": bool(a.turbo), "turbo_step_ms": int(a.turbo_step_ms),
            # A frida-spawned window never wins WM_ACTIVATEAPP, so var_app_active
            # stays 0 and the IN-GAME input handler drops HUD clicks (End-Turn,
            # county buttons).  force_active writes var_app_active=1 each tick so
            # gameplay clicks land — menu input works without it, gameplay doesn't.
            "force_active": not a.no_force_active,
            # The in-game UI dispatcher may only run its input-consume phase when
            # the present/render path runs — --interactive enables agent_present
            # + shows the window and is the known-good in-game input config.
            "agent_present": bool(a.present),
            "agent_present_every_ticks": 3 if a.present else 100,
            "show_window_after_ms": int(a.show_ms),
            "show_window_client_w": 640, "show_window_client_h": 480,
            "gdi_seat": bool(a.present), "skip_intro": True,
            "frame_capture": False, "frame_capture_max": 1_000_000,
            "frame_capture_warmup_secs": 0,
            "anchor_trace": True,
            # Neuter retail's map edge-scroll (FUN_00432221): moving the cursor
            # to a UI element near a screen edge (e.g. the bottom-right End-Turn
            # button) otherwise scrolls the map away every action.
            "suppress_edge_scroll": not a.no_suppress_scroll,
            "input_segtrace": seg_ops,
            "rng_seed": _parse_rng_seed(a.rng_seed),
            "heartbeat_timeout_ms": fc.HEARTBEAT_TIMEOUT_MS,
        }
        self.script.exports_sync.init(init_cfg)
        self.device.resume(self.pid)
        # Reactively clear event scrolls (Flooding etc.) that freeze the sim —
        # they set var_extended_pause but don't dismiss on RMB.
        if not a.no_auto_dismiss:
            time.sleep(0.5)
            self.script.exports_sync.probe_auto_dismiss(1)
        self._logline(f"[ready] pid={self.pid} turbo={a.turbo} "
                      f"segtrace={a.segtrace}")
        threading.Thread(target=self._heartbeat_loop, daemon=True).start()

    def _heartbeat_loop(self):
        # < HEARTBEAT_TIMEOUT_MS or the in-target watchdog kills the game.
        while not self._stop.is_set():
            try:
                self.script.exports_sync.heartbeat()
            except Exception:  # noqa: BLE001
                pass
            self._stop.wait(2.0)

    def stop(self):
        self._stop.set()
        try:
            if self.pid is not None:
                self.device.kill(self.pid)
        except Exception:  # noqa: BLE001
            pass

    # ── command dispatch ──────────────────────────────────────────────────
    def _post(self, m, w, l):
        return bool(self.script.exports_sync.probe_post(m, w, l))

    def _click(self, x, y, button="l", gap=0.05):
        # Space move/down/up so the engine's PeekMessage drains each on a
        # separate frame (a 0-tick down+up is dropped) — mirrors the traces'
        # move@t, down@t+2, up@t+4 cadence.  Force click_eligible=1 right before
        # the press: synthetic input never sets the gate the real mouse keeps
        # armed, so the resolver would otherwise drop the click.
        lp = ((int(y) & 0xFFFF) << 16) | (int(x) & 0xFFFF)
        down, up = ((WM_LBUTTONDOWN, WM_LBUTTONUP) if button == "l"
                    else (WM_RBUTTONDOWN, WM_RBUTTONUP))
        wparam = 1 if button == "l" else 2
        self._post(WM_MOUSEMOVE, 0, lp)
        time.sleep(gap)
        self.script.exports_sync.probe_poke(CLICK_ELIGIBLE_VA, [1, 0, 0, 0])
        self._post(down, wparam, lp)
        time.sleep(gap)
        self._post(up, 0, lp)
        return {"ok": True, "x": x, "y": y, "button": button, "l": lp}

    def _endturn(self, timeout_s=25.0):
        """Click End-Turn and wait for the turn to actually roll (season
        changes), dismissing any report/event scrolls (screen 0x27) that pop
        during resolution.  Re-clicks if the turn hasn't rolled after a while
        (a click can be dropped, or a year-rollover Winter resolves slowly).
        Returns the new season/year or advanced=False on timeout."""
        rd = lambda va: self.script.exports_sync.probe_read(va, "i32")
        s0, y0 = rd(0x0057C934), rd(0x00553EDC)
        deadline = time.monotonic() + timeout_s
        dismissed = 0
        last_click = 0.0
        while time.monotonic() < deadline:
            scr = rd(0x004EAC50)
            if scr == 0x27:                     # report/event scroll — RMB it
                self._click(*SCROLL_DISMISS_XY, "r")
                dismissed += 1
                time.sleep(0.15)
            elif scr == 0 and (time.monotonic() - last_click) > 4.0:
                # on the clean map, our turn — (re)click End-Turn.  A re-click
                # during AI resolution is a harmless no-op (not our turn).
                self._click(*END_TURN_XY, "l")
                last_click = time.monotonic()
            time.sleep(0.2)
            s1, y1 = rd(0x0057C934), rd(0x00553EDC)
            if s1 != s0 or y1 != y0:
                for _ in range(6):              # clear trailing new-turn scrolls
                    if rd(0x004EAC50) != 0x27:
                        break
                    self._click(*SCROLL_DISMISS_XY, "r")
                    dismissed += 1
                    time.sleep(0.15)
                return {"ok": True, "advanced": True, "season": s1, "year": y1,
                        "prev_season": s0, "scrolls_dismissed": dismissed}
        return {"ok": True, "advanced": False, "season": rd(0x0057C934),
                "year": rd(0x00553EDC), "scrolls_dismissed": dismissed}

    def _callq(self, va, args, abi, timeout, pokes=None):
        """Enqueue an atomic poke(s)+call transaction on the ENGINE thread.

        Applying volatile fixture inputs in the same between-ticks transaction
        prevents the live sim from replacing them between a controller poke and
        the call that is meant to consume them.
        """
        self._call_evt.clear()
        cid = self.script.exports_sync.probe_enqueue_call(va, args, abi, pokes or [])
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if cid in self._call_results:
                r = self._call_results.pop(cid)
                return {"ok": "err" not in r, "ret": r.get("ret"),
                        "err": r.get("err"), "id": cid}
            self._call_evt.wait(0.2)
            self._call_evt.clear()
        return {"ok": False, "err": "call timed out (engine not ticking?)", "id": cid}

    def _to_map(self):
        """Get back to the free-roam map (screen 0, no modal freeze)."""
        rd = lambda va: self.script.exports_sync.probe_read(va, "i32")
        for _ in range(6):
            if rd(0x004EAC50) == 0 and rd(0x0053E910) == 0:
                return True
            self._click(*SCROLL_DISMISS_XY, "r")
            time.sleep(0.3)
        return rd(0x004EAC50) == 0

    def _panel_set(self, open_xy, open_screen, up_xy, down_xy, floor_clicks,
                   value, label):
        """Open a HUD arrow-panel, floor it to 0/min, raise `value` steps, close.
        Absolute setter with relative arrows (no need to read the field)."""
        rd = lambda va: self.script.exports_sync.probe_read(va, "i32")
        self._to_map()
        self._click(*open_xy, "l")
        time.sleep(0.4)
        scr = rd(0x004EAC50)
        if scr != open_screen:
            return {"ok": False, "err": f"{label} panel not open (screen {scr})"}
        for _ in range(floor_clicks):
            self._click(*down_xy, "l")
            time.sleep(0.1)
        for _ in range(int(value)):
            self._click(*up_xy, "l")
            time.sleep(0.1)
        self._click(*SCROLL_DISMISS_XY, "r")
        time.sleep(0.3)
        return {"ok": True, label: int(value)}

    def _set_tax(self, pct):
        # Tax panel 0x15: ↑ (197,178) / ↓ (243,178).  Floor 12 = down to 0%.
        return self._panel_set((515, 218), 0x15, (197, 178), (243, 178),
                               12, pct, "tax")

    def _set_rations(self, level):
        # Rations panel 0x19: level 0 None..5 Triple; "Wanted" ↑ (357,146) /
        # ↓ (384,146) (validated live; FUN_0043a379 is the SEPARATE grain/meat
        # mix slider at y=0xdc, not this level control).
        return self._panel_set((600, 218), 0x19, (357, 146), (384, 146),
                               6, level, "rations")

    def _capture_raw(self, timeout=1.0, force_redraw=False):
        """Trigger ONE frame capture and return (idx, w, h, pal, px), or None.
        captureFrameOnce bumps its counter every call, so each probe_shot yields a
        fresh frame_idx — no need to wait for the game to mark itself dirty."""
        self._frame_evt.clear()
        with self._frame_lock:
            prev = self._frame[0] if self._frame else -1
        if force_redraw:
            try:
                self.script.exports_sync.probe_poke(0x0057d340, [2])  # mark map dirty
            except Exception:
                pass
        self.script.exports_sync.probe_shot()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._frame_evt.wait(0.05):
                with self._frame_lock:
                    if self._frame and self._frame[0] != prev:
                        return self._frame
                self._frame_evt.clear()
        with self._frame_lock:
            return self._frame

    def _rec_write(self, rec, w, h, pal, px):
        """Feed one streamed frame to the recording's ffmpeg (called from the
        frida message thread).  Frames arrive AUTONOMOUSLY from the agent, so this
        never blocks driving on the command channel."""
        if w != rec["w"] or h != rec["h"]:
            return
        try:
            rec["proc"].stdin.write(_pal_to_rgb_img(w, h, pal, px).tobytes())
            rec["n"] += 1
        except (BrokenPipeError, ValueError):
            pass

    def _record_start(self, path, fps=30):
        """Record real gameplay: the AGENT streams a frame every ~1000/fps real ms
        (decoupled from driving), the daemon pipes them to ffmpeg (rawvideo → h264).
        Drive turns/camera/battles while it runs; in turbo the result is a real
        fast-motion timelapse."""
        import subprocess
        if self._rec:
            return {"ok": False, "err": "already recording"}
        fr = self._capture_raw(timeout=2.0)          # one frame for the dims
        if not fr:
            return {"ok": False, "err": "no frame (is the game presenting?)"}
        _, w, h, _, _ = fr
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        proc = subprocess.Popen(
            ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
             "-s", f"{w}x{h}", "-r", str(fps), "-i", "-",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", str(path)],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self._rec = {"proc": proc, "path": str(path), "w": w, "h": h,
                     "fps": fps, "n": 0, "t0": time.monotonic()}
        interval = self.script.exports_sync.probe_stream_start(max(1, round(1000 / fps)))
        return {"ok": True, "path": str(path), "w": w, "h": h, "fps": fps,
                "interval_ms": interval}

    def _record_stop(self):
        if not self._rec:
            return {"ok": False, "err": "not recording"}
        try:
            self.script.exports_sync.probe_stream_stop()
        except Exception:
            pass
        time.sleep(0.25)                              # let in-flight frames drain
        rec, self._rec = self._rec, None
        try:
            rec["proc"].stdin.close()
        except Exception:
            pass
        try:
            rec["proc"].wait(timeout=15)
        except Exception:
            rec["proc"].kill()
        dur = time.monotonic() - rec["t0"]
        return {"ok": True, "path": rec["path"], "frames": rec["n"],
                "duration_s": round(dur, 2),
                "avg_fps": round(rec["n"] / dur, 1) if dur > 0 else 0}

    def _shot(self, path=None, direct=False, sync=False, stable=False):
        # Retry a few times: under the shown window / present seat the DDraw
        # back surface can be momentarily lost, so one probeShot may yield nothing.
        # direct=True reads *DAT_004ea1a8 straight from memory (no DDraw Lock) —
        # robust while frozen / right after driving pokes (the stepped battle-
        # capture path).  Poke the redraw counter before calling to force a fresh
        # render of the current frame.
        # stable=True arms a lock-free software-framebuffer copy at the next
        # engine_tick.onLeave.  It runs after a completed render on the engine
        # thread: no async tear and no RPC-thread DDraw Lock (which crashes combat).
        # The persistent compositor below fills any intentionally dirty-only tiles.
        # sync=True requests a one-shot capture INSIDE engine_tick.onLeave (after
        # the full render+present) so the framebuffer is never read mid-render —
        # the clean path for LIVE moving frames (no torn/dirty-rect artifacts).
        # Needs the tick running (a live battle at 0x29); the frame arrives ~1 tick
        # later and we wait on the frame event below.
        if stable:
            grab = self.script.exports_sync.probe_shot_stable
        elif sync:
            grab = self.script.exports_sync.probe_shot_sync
        elif direct:
            grab = self.script.exports_sync.probe_shot_direct
        else:
            grab = self.script.exports_sync.probe_shot
        got = None
        attempts = 8 if stable else 6
        for _ in range(attempts):
            self._frame_evt.clear()
            with self._frame_lock:
                prev = self._frame[0] if self._frame else -1
            rc = grab()
            deadline = time.monotonic() + (0.75 if stable else 1.0)
            while time.monotonic() < deadline:
                if self._frame_evt.wait(0.2):
                    with self._frame_lock:
                        if self._frame and self._frame[0] != prev:
                            got = self._frame
                            break
                    self._frame_evt.clear()
            if got is not None:
                break
            time.sleep(0.01 if stable else 0.2)
        if got is None:
            return {"ok": False, "err": "no frame"}
        fi, w, h, pal, px = got
        if path:
            out = Path(path)
        else:
            self._shot_seq += 1
            out = self.run_dir / "shots" / f"shot_{self._shot_seq:04d}.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        _save_png(out, w, h, pal, px)
        return {"ok": True, "path": str(out), "w": w, "h": h, "frame_idx": fi,
                "frame_composite": self._frame_meta.get(fi, {})}

    def dispatch(self, req: dict) -> dict:
        cmd = req.get("cmd")
        try:
            if cmd == "ping":
                return {"ok": True, "pid": self.pid}
            if cmd == "shot":
                return self._shot(req.get("path"), bool(req.get("direct")))
            if cmd == "shotd":            # Lock-free direct-framebuffer shot (async, may tear)
                return self._shot(req.get("path"), direct=True)
            if cmd == "shotq":            # synchronized lock-free post-render shot
                return self._shot(req.get("path"), stable=True)
            if cmd == "shots":            # diagnostic in-tick DDraw-lock shot
                return self._shot(req.get("path"), sync=True)
            if cmd == "record":
                if req.get("action") == "start":
                    return self._record_start(req["path"], int(req.get("fps", 30)))
                return self._record_stop()
            if cmd == "click":
                return self._click(req["x"], req["y"], req.get("button", "l"),
                                   float(req.get("gap", 0.05)))
            if cmd == "postraw":            # post ONE raw WM message (diagnostics)
                return {"ok": self._post(int(req["m"]), int(req["w"]),
                                         int(req["l"]))}
            if cmd == "move":
                lp = ((int(req["y"]) & 0xFFFF) << 16) | (int(req["x"]) & 0xFFFF)
                return {"ok": self._post(WM_MOUSEMOVE, 0, lp)}
            if cmd == "key":
                vk = int(req["vk"])
                mode = req.get("mode", "press")
                if mode in ("down", "press"):
                    self._post(WM_KEYDOWN, vk, 0x00000001)
                if mode in ("up", "press"):
                    self._post(WM_KEYUP, vk, 0xC0000001)
                return {"ok": True, "vk": vk, "mode": mode}
            if cmd == "read":
                v = self.script.exports_sync.probe_read(int(req["va"]),
                                                        req.get("type", "i32"))
                return {"ok": True, "val": v}
            if cmd == "reads":
                v = self.script.exports_sync.probe_reads(req["specs"])
                return {"ok": True, "vals": v}
            if cmd == "state":
                v = self.script.exports_sync.probe_reads(
                    _state_specs(int(req.get("nplayers", 5))))
                return {"ok": True, "vals": v}
            if cmd == "endturn":
                r = self._endturn(float(req.get("timeout", 10.0)))
                r["state"] = self.script.exports_sync.probe_reads(_state_specs(5))
                return r
            if cmd == "tomap":
                return {"ok": self._to_map()}
            if cmd == "settax":
                return self._set_tax(int(req["pct"]))
            if cmd == "setrations":
                return self._set_rations(int(req["level"]))
            if cmd == "poke":
                ok = self.script.exports_sync.probe_poke(int(req["va"]),
                                                         req["bytes"])
                return {"ok": bool(ok)}
            if cmd == "call":
                ret = self.script.exports_sync.probe_call(
                    int(req["va"]), req.get("args", []), req.get("abi", "mscdecl"))
                return {"ok": ret is not None, "ret": ret}
            if cmd == "callq":
                return self._callq(int(req["va"]), req.get("args", []),
                                   req.get("abi", "mscdecl"),
                                   float(req.get("timeout", 5.0)),
                                   req.get("pokes", []))
            if cmd == "xcall":            # call any agent export by snake_case name
                fn = getattr(self.script.exports_sync, req["fn"])
                ret = fn(*req.get("args", []))
                return {"ok": True, "ret": ret}
            if cmd == "ctreset":          # clear accumulated call-trace events
                with self._ct_lock:
                    self._ct_events = []
                return {"ok": True}
            if cmd == "ctget":            # aggregate accumulated call-trace events
                with self._ct_lock:
                    evs = list(self._ct_events)
                    if req.get("clear"):
                        self._ct_events = []
                counts, callers = {}, {}
                for e in evs:
                    va, rv = e.get("va"), e.get("ret_va")
                    counts[va] = counts.get(va, 0) + 1
                    callers.setdefault(va, {})[rv] = callers.setdefault(va, {}).get(rv, 0) + 1
                return {"ok": True, "total": len(evs), "counts": counts, "callers": callers}
            if cmd == "anchors":
                return {"ok": True, "anchors": self.script.exports_sync.probe_anchors()}
            if cmd == "autodismiss":
                return {"ok": True, "n": self.script.exports_sync.probe_auto_dismiss(
                    int(req.get("n", 1)))}
            if cmd == "sleep":
                time.sleep(float(req.get("ms", 200)) / 1000.0)
                return {"ok": True}
            if cmd == "quit":
                return {"ok": True, "bye": True}
            return {"ok": False, "err": f"unknown cmd {cmd!r}"}
        except Exception as e:  # noqa: BLE001
            self._logline(f"[dispatch] {cmd} failed: {e!r}")
            return {"ok": False, "err": repr(e)}

    def serve(self, port: int):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(4)
        port = srv.getsockname()[1]
        CONTROL_JSON.parent.mkdir(parents=True, exist_ok=True)
        CONTROL_JSON.write_text(json.dumps(
            {"port": port, "pid": self.pid, "run_dir": str(self.run_dir)}))
        self._logline(f"[serve] listening on 127.0.0.1:{port}")
        print(f"[probe_daemon] ready on 127.0.0.1:{port} "
              f"(pid={self.pid}); run_dir={self.run_dir}", flush=True)
        try:
            while not self._stop.is_set():
                conn, _ = srv.accept()
                with conn:
                    buf = b""
                    while not buf.endswith(b"\n"):
                        chunk = conn.recv(65536)
                        if not chunk:
                            break
                        buf += chunk
                    if not buf:
                        continue
                    try:
                        req = json.loads(buf.decode())
                    except Exception as e:  # noqa: BLE001
                        conn.sendall((json.dumps({"ok": False,
                                                  "err": f"bad json: {e}"}) + "\n").encode())
                        continue
                    reply = self.dispatch(req)
                    conn.sendall((json.dumps(reply) + "\n").encode())
                    if reply.get("bye"):
                        break
        finally:
            try:
                CONTROL_JSON.unlink()
            except Exception:  # noqa: BLE001
                pass
            self.stop()


def _pal_to_rgb_img(w: int, h: int, palette: bytes, pixels: bytes):
    """Palettized frame → a PIL RGB image (shared by shot-save + the recorder)."""
    from PIL import Image
    rgb = bytearray(768)                       # palette is 256 * (R,G,B,flags)
    for i in range(256):
        rgb[i * 3 + 0] = palette[i * 4 + 0]
        rgb[i * 3 + 1] = palette[i * 4 + 1]
        rgb[i * 3 + 2] = palette[i * 4 + 2]
    img = Image.frombytes("P", (w, h), bytes(pixels))
    img.putpalette(bytes(rgb))
    return img.convert("RGB")


def _save_png(out: Path, w: int, h: int, palette: bytes, pixels: bytes):
    _pal_to_rgb_img(w, h, palette, pixels).save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=fc.DEFAULT_REMOTE)
    ap.add_argument("--exe", default=str(fc.RETAIL_EXE))
    ap.add_argument("--cwd", default=str(fc.ASSET_CWD))
    ap.add_argument("--port", type=int, default=27099)
    ap.add_argument("--run-id", default="session")
    ap.add_argument("--segtrace", default=None,
                    help="bootstrap trace to drive to a known state first")
    ap.add_argument("--rng-seed", nargs="?", const="canonical", default=None)
    ap.add_argument("--no-force-active", action="store_true",
                    help="don't force var_app_active=1 (gameplay clicks may drop)")
    ap.add_argument("--no-suppress-scroll", action="store_true",
                    help="leave retail's map edge-scroll active (default: neutered)")
    ap.add_argument("--no-auto-dismiss", action="store_true",
                    help="don't reactively clear event scrolls (keep them for capture)")
    ap.add_argument("--present", action="store_true",
                    help="enable the GDI present seat (replicate --interactive's render path)")
    ap.add_argument("--show-ms", type=int, default=0,
                    help="show the game window after N ms (0=stay hidden)")
    ap.add_argument("--turbo", action="store_true",
                    help="fast-forward (off by default so you can watch)")
    ap.add_argument("--turbo-step-ms", type=int, default=17)
    ap.add_argument("--no-frame-composite", dest="frame_composite", action="store_false",
                    help="diagnostic: save transient dirty back planes without rebuilding the presented frame")
    ap.set_defaults(frame_composite=True)
    args = ap.parse_args()

    d = ProbeDaemon(args)
    d.start()
    d.serve(args.port)


if __name__ == "__main__":
    main()
