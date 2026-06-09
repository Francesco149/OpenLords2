"""model/session.py — the session manifest (schema ol2-trace-studio-v1).

A session is one studio trace + its captured/diffed artifacts:

    runs/trace-studio/<session>/
      trace.jsonl            studio trace (editable; sugar allowed)
      trace.compiled.jsonl   compiler output — what both sides replayed
      port/    frames/<label>.png  anchors.jsonl  captures.jsonl  log.txt
      retail/  frames/<label>.png  anchors.jsonl  captures.jsonl  …
      diff/    <label>.png
      <group>.<side>.mp4     per-group scrub videos (dense groups)
      session.json           this manifest

Captures pair by LABEL.  `captures[]` keeps trace order.  Slots reserved for
later phases: state (save-schema field diffs), marks (user annotations).
"""
from __future__ import annotations

import json
from pathlib import Path

SCHEMA = "ol2-trace-studio-v1"


def write_session(sess_dir: Path, manifest: dict) -> None:
    (sess_dir / "session.json").write_text(
        json.dumps(manifest, indent=1) + "\n")


def load_session(sess_dir: Path) -> dict | None:
    p = sess_dir / "session.json"
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text())
    except ValueError:
        return None


def session_summary(sess_dir: Path) -> dict:
    """One row for the session list (server /api/sessions)."""
    m = load_session(sess_dir) or {}
    v = m.get("verdict") or {}
    return {
        "name": sess_dir.name,
        "schema": m.get("schema"),
        "trace": m.get("source_trace"),
        "target": m.get("target"),
        "n_captures": m.get("n_captures", 0),
        "verdict": v.get("status"),
        "n_diff": v.get("n_diff"),
        "mtime": int((sess_dir / "session.json").stat().st_mtime)
        if (sess_dir / "session.json").exists() else 0,
    }


def list_sessions(root: Path) -> list[dict]:
    if not root.exists():
        return []
    rows = [session_summary(d) for d in sorted(root.iterdir()) if d.is_dir()]
    rows.sort(key=lambda r: -r["mtime"])
    return rows
