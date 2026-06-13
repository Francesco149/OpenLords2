#!/usr/bin/env python3
"""Trace Studio v3 — capture orchestrator (drive both engines → containers → view).

The native counterpart to the retired web studio's capture.py. Drives the PORT and
RETAIL through the SAME compiled trace (a contiguous capture window via {caprange}),
each emitting raw 8bpp+palette frame payloads, then INGESTS those into two compact
.olv3 containers + a view.json identity-join — and launches the native viewer. No
PNG bake; the containers are the only artifacts and the viewer reconstructs frames
live.

Phases (run together by default, or independently):
  capture  drive port + retail with --capture-raw / frida_capture .raw dump
  build    ingest the raw frames → port.olv3 / retail.olv3 + view.json
  view     launch viewer.exe on the view.json (Windows process via WSLInterop)

Reuses the trace compiler + save-schema slot list from the (kept) trace model
modules; only the capture/transport/serve layers of the old package are retired.

  nix develop -c python3 tools/trace_studio_v3/olv3_capture.py \
      --trace endturn_auto --window MAP_FREEROAM:0:120 [--only port] [--no-view]
"""
from __future__ import annotations

import argparse
import datetime as dt
import os
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

import olv3                                          # noqa: E402
import viewbuild                                     # noqa: E402
import trace_ops as ts_ops                            # noqa: E402  (trace compiler)
import state_schema as ts_schema                      # noqa: E402  (save-schema slots)

SESS_ROOT = ROOT / "runs" / "olv3"
DEFAULT_REMOTE = os.environ.get("OPENLORDS2_FRIDA_REMOTE", "cutestation.soy:27042")


def _log(m: str) -> None:
    print(f"olv3_capture: {m}", flush=True)


# ── raw frame payload (shared port/retail format) ──────────────────────────────
def _read_raw(path: Path) -> tuple[bytes, bytes, int] | None:
    """A .raw frame payload → (palette_1024, indices_whbytes, present). The exact
    layout the agent + gfx_capture_backbuffer_raw emit: [w][h][present][1024][w*h]."""
    b = path.read_bytes()
    if len(b) < 12 + 1024:
        return None
    w, h, present = struct.unpack_from("<III", b, 0)
    pal = b[12:12 + 1024]
    px = b[12 + 1024: 12 + 1024 + w * h]
    if len(px) != w * h:
        return None
    return pal, px, present, w, h


# ── per-draw write-journal sidecar (port draw-step capture) ─────────────────────
DRAWS_MAGIC = 0x52444C4F   # 'OLDR' (bytes O,L,D,R little-endian)


def _read_draws(path: Path) -> tuple[bytes, list[dict]] | None:
    """A <label>.draws sidecar (gfx_drawcap_write) → (base_plane, draws). Layout
    (native LE): [u32 'OLDR'][w][h][present][n_draws][base w*h] then n_draws ×
    [op_type][m0][m1][m2][n][n×u32 offset][n×u8 val]. base + applying the draws in
    order == the frame's final plane. Returns None on any malformation."""
    b = path.read_bytes()
    if len(b) < 20:
        return None
    magic, w, h, _present, ndraws = struct.unpack_from("<IIIII", b, 0)
    if magic != DRAWS_MAGIC:
        return None
    off = 20
    base = b[off:off + w * h]; off += w * h
    if len(base) != w * h:
        return None
    draws: list[dict] = []
    for _ in range(ndraws):
        if off + 20 > len(b):
            return None
        op_type, m0, m1, m2, n = struct.unpack_from("<IIIII", b, off); off += 20
        if off + n * 4 + n > len(b):
            return None
        offsets = list(struct.unpack_from("<%dI" % n, b, off)) if n else []
        off += n * 4
        vals = b[off:off + n]; off += n
        draws.append({"op_type": op_type, "m0": m0, "m1": m1, "m2": m2,
                      "offsets": offsets, "vals": vals})
    return base, draws


def _verify_draws(base: bytes, draws: list[dict], final: bytes) -> int:
    """base + applying every draw delta == final? Return #mismatched pixels (0 = ok).
    The built-in correctness gate on the port's draw capture."""
    canvas = bytearray(base)
    for d in draws:
        for o, v in zip(d["offsets"], d["vals"]):
            canvas[o] = v
    return sum(1 for a, b in zip(canvas, final) if a != b)


# ── ingest: raw frames → container, returns label→(idx,present) ────────────────
def ingest_port(port_dir: Path, out: Path) -> dict[str, dict]:
    """Port frames are named <label>.raw (captures.jsonl gives label→tick); a
    <label>.draws sidecar (when present, from --capture-draws) makes the frame
    draw-steppable."""
    rows = _read_jsonl(port_dir / "captures.jsonl") or _read_jsonl(port_dir / "frames-raw" / "captures.jsonl")
    return _ingest_by_label(port_dir, out, [(str(r.get("label") or ""), int(r.get("frame", 0))) for r in rows],
                            lambda lbl: _find(port_dir, f"{lbl}.raw"),
                            locate_draws=lambda lbl: _find(port_dir, f"{lbl}.draws"))


def ingest_retail(retail_dir: Path, out: Path) -> dict[str, dict]:
    """Retail frames are frame_NNNNN.raw in capture order; captures.jsonl maps
    label→tick and captures fire in ascending-tick order, so the i-th distinct tick
    pairs with frame_{i:05d} (mirrors the retired convert_retail). A matching
    frame_{i:05d}.draws sidecar (from --capture-draws) makes it draw-steppable."""
    rows = _read_jsonl(retail_dir / "captures.jsonl")
    ticks = sorted({int(r.get("frame", 0)) for r in rows if r.get("label")})
    tick_idx = {t: i for i, t in enumerate(ticks)}
    pairs = [(str(r.get("label") or ""), int(r.get("frame", 0))) for r in rows if r.get("label")]
    return _ingest_by_label(retail_dir, out, pairs,
                            lambda lbl, tick: _find(retail_dir, f"frame_{tick_idx[tick]:05d}.raw"),
                            need_tick=True,
                            locate_draws=lambda lbl, tick: _find(retail_dir, f"frame_{tick_idx[tick]:05d}.draws"))


def _ingest_by_label(side_dir, out, pairs, locate, need_tick=False,
                     locate_draws=None) -> dict[str, dict]:
    """Write a container from (label, tick) pairs; dedup'd. Returns label→{idx,present}.
    A frame's container index is its write order; identical frames dedup but each
    label still maps to a kept FRAME record, so distinct labels can share pixels.
    When `locate_draws` resolves a <label>.draws sidecar, the frame is committed
    draw-steppable (base + deltas) and the base/final consistency is verified."""
    wr = olv3.Writer(out, 640, 480)
    label_map: dict[str, dict] = {}
    n = 0
    n_drawsteppable = 0
    n_drawmismatch = 0
    for lbl, tick in pairs:
        raw = locate(lbl, tick) if need_tick else locate(lbl)
        if not raw or not raw.exists():
            continue
        got = _read_raw(raw)
        if not got:
            continue
        pal, px, present, w, h = got
        base, draws = None, None
        if locate_draws is not None:
            dp = locate_draws(lbl, tick) if need_tick else locate_draws(lbl)
            if dp and dp.exists():
                got_d = _read_draws(dp)
                if got_d:
                    base, draws = got_d
                    bad = _verify_draws(base, draws, px)
                    if bad:
                        n_drawmismatch += 1
                        _log(f"  WARN {lbl}: base+draws != final ({bad} px) — "
                             f"committing plain (draw capture dropped)")
                        base, draws = None, None
                    else:
                        n_drawsteppable += 1
        wr.add_frame(pal, px, present, base_whbytes=base, draws=draws)
        label_map[lbl] = {"idx": n, "present": present}
        n += 1
    info = wr.close()
    extra = (f", {n_drawsteppable} draw-steppable"
             + (f" ({n_drawmismatch} mismatched→plain)" if n_drawmismatch else "")
             if locate_draws is not None else "")
    _log(f"  {out.name}: {info['n_frames']} frames, {info['n_palettes']} palettes, "
         f"{info['n_pixels']} planes, {info['size']//1024} KB{extra}")
    return label_map


def _find(base: Path, name: str) -> Path | None:
    for cand in (base / name, base / "frames-raw" / name, base / "frames" / name):
        if cand.exists():
            return cand
    return None


def _read_jsonl(path: Path) -> list[dict]:
    import json
    if not path or not path.exists():
        return []
    out = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return out


def _read_state(side_dir: Path, label: str, retail: bool, tick: int) -> dict | None:
    import json
    cands = ([side_dir / "state" / f"state_{tick:05d}.json"] if retail
             else [side_dir / f"{label}.state.json", side_dir / "frames-raw" / f"{label}.state.json"])
    for c in cands:
        if c.exists():
            try:
                return json.loads(c.read_text())
            except (json.JSONDecodeError, OSError):
                return None
    return None


# ── drive (reuse the proven command shapes) ────────────────────────────────────
def drive_port(compiled: Path, port_dir: Path, max_frames: int, timeout_ms: int,
               rng_seed: bool, draws: bool = True, force_redraw: bool = False) -> int:
    port_dir.mkdir(parents=True, exist_ok=True)
    # The game APPENDS to captures.jsonl and reuses <label>.{raw,draws,bmp}; clear
    # last run's artifacts so the container isn't polluted with stale/duplicate
    # frames (retail uses a fresh run-id dir, so only the port needs this).
    for pat in ("*.raw", "*.draws", "*.bmp", "*.state.json",
                "captures.jsonl", "anchors.jsonl"):
        for f in port_dir.glob(pat):
            f.unlink()
    cmd = [str(ROOT / "tools" / "run-openlords2.sh"), "--",
           "--input-segtrace", str(compiled), "--capture-to", str(port_dir),
           "--capture-raw", "--frames", str(max_frames), "--turbo"]
    if draws:
        cmd.append("--capture-draws")   # per-draw write-journal → <label>.draws
    if force_redraw:
        cmd.append("--capture-force-redraw")   # full map re-render on capture ticks
    if rng_seed:
        cmd.append("--rng-seed")
    env = dict(os.environ, OPENLORDS2_TIMEOUT_MS=str(timeout_ms))
    with (port_dir / "log.txt").open("w") as log:
        return subprocess.run(cmd, cwd=ROOT, env=env, stdout=log,
                              stderr=subprocess.STDOUT,
                              timeout=timeout_ms / 1000 + 120).returncode


def drive_retail(compiled: Path, retail_dir: Path, remote: str, rng_seed: bool,
                 slots: Path, draws: bool = True, force_redraw: bool = False) -> int:
    retail_dir.mkdir(parents=True, exist_ok=True)
    for pat in ("frame_*.raw", "frame_*.draws", "captures.jsonl", "anchors.jsonl"):
        for f in retail_dir.glob(pat):
            f.unlink()
    run_id = f"olv3-{dt.datetime.now():%Y%m%d-%H%M%S}"
    cmd = [sys.executable, str(ROOT / "tools" / "frida_capture.py"), "--remote", remote,
           "--skip-intro", "--turbo", "--input-segtrace", str(compiled),
           "--anchor-trace", "--state-dump-slots", str(slots), "--run-id", run_id]
    if draws:
        cmd.append("--capture-draws")   # per-draw write-journal → frame_<idx>.draws
    if force_redraw:
        cmd.append("--capture-force-redraw")   # full map re-render on capture ticks
    if rng_seed:
        cmd.append("--rng-seed")
    with (retail_dir / "log.txt").open("w") as log:
        rc = subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                            timeout=420).returncode
    # harvest the run dir's frames + side files into retail_dir
    run_dir = ROOT / "runs" / "capture" / run_id
    import shutil
    if (run_dir / "frames").exists():
        for pat in ("*.raw", "*.draws"):
            for f in (run_dir / "frames").glob(pat):
                shutil.copy2(f, retail_dir / f.name)
    for name in ("captures.jsonl", "anchors.jsonl"):
        if (run_dir / name).exists():
            shutil.copy2(run_dir / name, retail_dir / name)
    if (run_dir / "state").exists():
        (retail_dir / "state").mkdir(exist_ok=True)
        for f in (run_dir / "state").glob("*.json"):
            shutil.copy2(f, retail_dir / "state" / f.name)
    return rc


# ── orchestrate ────────────────────────────────────────────────────────────────
def main() -> int:
    ap = argparse.ArgumentParser(description="Trace Studio v3 capture orchestrator.")
    ap.add_argument("--trace", help="trace name under tools/traces/ or a path")
    ap.add_argument("--session", help="session name (default: trace stem)")
    ap.add_argument("--only", choices=("port", "retail"), help="re-run one side (keep the other)")
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--no-rng-seed", action="store_true")
    ap.add_argument("--port-max-frames", type=int, default=4000)
    ap.add_argument("--port-timeout-ms", type=int, default=240_000)
    ap.add_argument("--draws", action=argparse.BooleanOptionalAction, default=True,
                    help="emit per-draw write-journals (.draws) on BOTH engines for draw-stepping + cross-engine draw diff (default on)")
    ap.add_argument("--force-redraw", action="store_true",
                    help="force a full map re-render on each capture tick so the MAP composites into the draw journal (cross-engine map-draw diff; default off = HUD-only steady draws)")
    ap.add_argument("--no-capture", action="store_true", help="skip driving; ingest existing raws")
    ap.add_argument("--no-view", action="store_true", help="build containers + view.json, don't launch")
    args = ap.parse_args()

    src = _resolve_trace(args.trace) if args.trace else None
    sess = args.session or (src.stem if src else None)
    if not sess:
        raise SystemExit("olv3_capture: need --trace or --session")
    sess_dir = SESS_ROOT / sess
    sess_dir.mkdir(parents=True, exist_ok=True)
    port_dir, retail_dir = sess_dir / "port", sess_dir / "retail"
    rng_seed = not args.no_rng_seed

    # compile the trace (reuse the kept trace compiler)
    compiled = sess_dir / "trace.compiled.jsonl"
    if src is not None:
        op_list = ts_ops.parse(ts_ops.load_lines(src))
        lines, plan = ts_ops.compile_ops(op_list)
        compiled.write_text("\n".join(lines) + "\n")
        _log(f"session {sess}: {len(plan)} captures from {src.name}")
    slots = sess_dir / "state_slots.json"
    ts_schema.write_slots_file(slots)

    run_port = args.only in (None, "port") and not args.no_capture
    run_retail = args.only in (None, "retail") and not args.no_capture
    if run_port:
        _log("driving port…")
        try:
            rc = drive_port(compiled, port_dir, args.port_max_frames, args.port_timeout_ms,
                            rng_seed, draws=args.draws, force_redraw=args.force_redraw)
            _log(f"port rc={rc}")
        except subprocess.TimeoutExpired:
            _log("port TIMED OUT (continuing with what landed)")
    if run_retail:
        _log("driving retail…")
        try:
            rc = drive_retail(compiled, retail_dir, args.remote, rng_seed, slots,
                              draws=args.draws, force_redraw=args.force_redraw)
            _log(f"retail rc={rc}")
        except subprocess.TimeoutExpired:
            _log("retail TIMED OUT (continuing with what landed)")

    # ingest → containers
    port_olv3, retail_olv3 = sess_dir / "port.olv3", sess_dir / "retail.olv3"
    port_map = ingest_port(port_dir, port_olv3)
    retail_map = ingest_retail(retail_dir, retail_olv3)
    _log(f"ingested: port {len(port_map)} retail {len(retail_map)} labels")

    # build columns: union of labels, paired by identity label
    labels = sorted(set(port_map) | set(retail_map),
                    key=lambda l: (port_map.get(l, retail_map.get(l, {})).get("present", 0), l))
    cols: list[dict] = []
    for off, lbl in enumerate(labels):
        pm, rm = port_map.get(lbl), retail_map.get(lbl)
        col = {"label": lbl, "offset": off}
        if pm:
            col["port_idx"] = pm["idx"]; col["port_present"] = pm["present"]
        if rm:
            col["retail_idx"] = rm["idx"]; col["retail_present"] = rm["present"]
        if not pm:
            col["gap"] = "port"
        elif not rm:
            col["gap"] = "retail"
        ps = _read_state(port_dir, lbl, False, pm["present"] if pm else 0) if pm else None
        rs = _read_state(retail_dir, lbl, True, rm["present"] if rm else 0) if rm else None
        if ps or rs:
            col["state"] = {"port": ps or {}, "retail": rs or {}}
        cols.append(col)

    n_pair = sum(1 for c in cols if "port_idx" in c and "retail_idx" in c)
    view = viewbuild.write_view(
        sess_dir / "view.json", port_container=port_olv3, retail_container=retail_olv3,
        columns=cols, scenario=sess, anchor=src.stem if src else sess,
        verdict=f"{n_pair} paired / {len(cols)} cols", notes_path=sess_dir / "notes.json")
    _log(f"view.json → {view}  ({len(cols)} cols, {n_pair} paired)")

    if args.no_view:
        return 0
    viewer = HERE / "viewer" / "viewer.exe"
    if not viewer.exists():
        _log(f"viewer not built ({viewer}) — run: make -C {viewer.parent}")
        return 0
    win_view = viewbuild.win_path(view)
    _log(f"launching viewer on {win_view}")
    subprocess.Popen([str(viewer), win_view])
    return 0


def _resolve_trace(spec: str) -> Path:
    p = Path(spec)
    if p.exists():
        return p.resolve()
    for cand in (ROOT / "tools" / "traces" / spec, ROOT / "tools" / "traces" / f"{spec}.jsonl"):
        if cand.exists():
            return cand.resolve()
    raise SystemExit(f"olv3_capture: trace not found: {spec}")


if __name__ == "__main__":
    raise SystemExit(main())
