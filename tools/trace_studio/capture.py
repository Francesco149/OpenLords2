"""capture.py — the capture orchestrator.

resolve trace/session → lint → compile → drive port(+retail) concurrently →
convert to label-keyed PNGs → pair + pixel-diff + verdict → group videos →
session.json.

The standing loop this enables: keep ONE live session per trace (the session
name defaults to the trace stem, no timestamp — re-captures overwrite in
place), tweak the port, `capture --only port`, refresh the viewer.
"""
from __future__ import annotations

import json
import shutil
from dataclasses import dataclass
from pathlib import Path

from .analysis import pixeldiff
from .drive import runner
from .model import ops, session as sess_mod
from .paths import DEFAULT_REMOTE, ROOT, SESS_ROOT
from .transport import convert, encode


def _log(msg: str) -> None:
    print(f"trace_studio: {msg}", flush=True)


@dataclass
class CaptureConfig:
    trace: str | None = None
    session: str | None = None
    target: str = "both"            # both | port | retail
    only: str | None = None         # None | port | retail (re-run one side)
    amp: float = 6.0
    remote: str = DEFAULT_REMOTE
    rng_seed: bool = True
    port_max_frames: int = 4000
    port_timeout_ms: int = 240_000
    lint: bool = True
    prune_raw: bool = True
    encode_videos: bool = True
    watch: list[str] | None = None      # retail --watch NAME=0xVA:type specs


def _resolve_trace(spec: str) -> Path:
    p = Path(spec)
    if p.exists():
        return p.resolve()
    cand = ROOT / "tools" / "traces" / spec
    if cand.exists():
        return cand.resolve()
    cand = cand.with_suffix(".jsonl")
    if cand.exists():
        return cand.resolve()
    raise SystemExit(f"trace_studio: trace not found: {spec}")


def run_capture(cfg: CaptureConfig) -> int:
    # ── session + trace resolution ───────────────────────────────────────────
    src: Path | None = _resolve_trace(cfg.trace) if cfg.trace else None
    sess = cfg.session or (src.stem if src else None)
    if not sess:
        raise SystemExit("trace_studio: need --trace or --session")
    sess_dir = SESS_ROOT / sess
    sess_dir.mkdir(parents=True, exist_ok=True)
    trace_path = sess_dir / "trace.jsonl"
    if src is not None:
        if (trace_path.exists()
                and trace_path.read_text() != src.read_text()):
            _log(f"session trace updated from {src.name}")
        shutil.copy2(src, trace_path)
    if not trace_path.exists():
        raise SystemExit(f"trace_studio: session {sess} has no trace.jsonl "
                         "(pass --trace)")

    manifest_old = sess_mod.load_session(sess_dir) or {}
    target = cfg.target
    want_port = target in ("both", "port")
    want_retail = target in ("both", "retail")
    run_port = want_port and cfg.only in (None, "port")
    run_retail = want_retail and cfg.only in (None, "retail")
    if cfg.only and not (run_port or run_retail):
        raise SystemExit(f"trace_studio: --only {cfg.only} conflicts with "
                         f"--target {target}")

    # ── lint + compile (one canonical parser; engines replay the output) ─────
    op_list = ops.parse(ops.load_lines(trace_path))
    findings = ops.lint(op_list, target)
    for fi in findings:
        _log(f"lint {fi['level'].upper()} [{fi['code']}] {fi['msg']}")
    if cfg.lint and any(fi["level"] == "error" for fi in findings):
        raise SystemExit("trace_studio: lint failed — fix the trace or "
                         "--no-lint to bypass")
    compiled_lines, plan = ops.compile_ops(op_list)
    compiled = sess_dir / "trace.compiled.jsonl"
    compiled.write_text("\n".join(compiled_lines) + "\n")
    groups: list[str] = []
    for p in plan:
        if p["group"] and p["group"] not in groups:
            groups.append(p["group"])
    _log(f"session {sess}  captures={len(plan)} "
         f"groups={groups or '-'}  target={target}"
         + (f"  only={cfg.only}" if cfg.only else ""))

    # ── clear the sides being re-run (keep the cached other side) ────────────
    port_dir = sess_dir / "port"
    retail_dir = sess_dir / "retail"
    for side_dir, run in ((port_dir, run_port), (retail_dir, run_retail)):
        if run and side_dir.exists():
            shutil.rmtree(side_dir)
    if (sess_dir / "diff").exists():
        shutil.rmtree(sess_dir / "diff")

    # ── drive ────────────────────────────────────────────────────────────────
    result = runner.drive(
        compiled=compiled, port_dir=port_dir, retail_dir=retail_dir,
        run_port=run_port, run_retail=run_retail, remote=cfg.remote,
        rng_seed=cfg.rng_seed, port_max_frames=cfg.port_max_frames,
        port_timeout_ms=cfg.port_timeout_ms, watch=cfg.watch)
    for k in ("port_error", "retail_error"):
        if result.get(k):
            _log(f"{k}: {result[k]}")
    if run_port and result.get("port_rc") not in (0, None):
        _log(f"port rc={result.get('port_rc')} — continuing with what landed "
             "(see port/log.txt)")

    # ── convert + pair + diff ────────────────────────────────────────────────
    port_map = convert.convert_port(port_dir) if want_port else {}
    retail_map = convert.convert_retail(retail_dir) if want_retail else {}
    _log(f"captured: port {len(port_map)}/{len(plan)}  "
         f"retail {len(retail_map)}/{len(plan)}")
    compare_retail = want_retail and want_port
    captures = pixeldiff.build_captures(
        plan, port_map, retail_map, sess_dir, cfg.amp, compare_retail)
    vd = pixeldiff.verdict(captures, compare_retail)

    # ── group videos (dense ranges) ──────────────────────────────────────────
    videos: dict[str, dict] = {}
    if cfg.encode_videos:
        for g in groups:
            labels = [p["label"] for p in plan if p["group"] == g]
            vids: dict[str, str] = {}
            for side, base in (("port", port_dir / "frames"),
                               ("retail", retail_dir / "frames"),
                               ("diff", sess_dir / "diff")):
                frames = [base / f"{lb}.png" for lb in labels]
                out = sess_dir / f"{g}.{side}.mp4"
                if encode.encode_group(frames, out):
                    vids[side] = out.name
            if vids:
                videos[g] = vids

    # ── manifest ─────────────────────────────────────────────────────────────
    anchors = {
        "port": ops.read_jsonl(port_dir / "anchors.jsonl"),
        "retail": ops.read_jsonl(retail_dir / "anchors.jsonl"),
    }
    manifest = {
        "schema": sess_mod.SCHEMA,
        "session": sess,
        "source_trace": str(src) if src else manifest_old.get("source_trace"),
        "target": target,
        "amp": cfg.amp,
        "rng_seed": cfg.rng_seed,
        "n_captures": len(plan),
        "n_port": len(port_map),
        "n_retail": len(retail_map),
        "captures": captures,
        "groups": [{"name": g,
                    "labels": [p["label"] for p in plan if p["group"] == g],
                    "videos": videos.get(g, {})} for g in groups],
        "anchors": anchors,
        "verdict": vd,
        "errors": {k: v for k, v in result.items() if k.endswith("_error")},
    }
    sess_mod.write_session(sess_dir, manifest)

    if cfg.prune_raw:
        # Keep the raws of a side whose conversion didn't cover the plan —
        # they're the only evidence left for debugging that side.
        if len(port_map) == len(plan):
            convert.prune_raw(port_dir)
        if len(retail_map) == len(plan):
            convert.prune_raw(retail_dir)

    # ── summary ──────────────────────────────────────────────────────────────
    _log(f"verdict: {vd['status']}"
         + (f"  n_diff={vd['n_diff']} n_missing={vd.get('n_missing')}"
            if vd.get("n_diff") is not None else "")
         + (f"  worst={vd['worst']['label']} ({vd['worst']['n_px']}px "
            f"{vd['worst']['pct']}%)" if vd.get("worst") else ""))
    _log(f"session.json → {sess_dir}")
    print(f"\nview:  nix develop -c python3 tools/trace_studio.py serve "
          f"--session {sess}\n", flush=True)
    return 0


def cmd_lint(trace: str, target: str = "both") -> int:
    path = _resolve_trace(trace)
    op_list = ops.parse(ops.load_lines(path))
    findings = ops.lint(op_list, target)
    for fi in findings:
        print(f"{fi['level'].upper():5} [{fi['code']}] {fi['msg']}")
    _, plan = ops.compile_ops(op_list)
    print(f"{path.name}: {len(plan)} captures, "
          f"{sum(1 for f in findings if f['level'] == 'error')} errors, "
          f"{sum(1 for f in findings if f['level'] == 'warn')} warnings")
    return 1 if any(f["level"] == "error" for f in findings) else 0


def cmd_compile(trace: str) -> int:
    path = _resolve_trace(trace)
    op_list = ops.parse(ops.load_lines(path))
    compiled, plan = ops.compile_ops(op_list)
    print("\n".join(compiled))
    print(f"# {len(plan)} planned captures", flush=True)
    return 0
