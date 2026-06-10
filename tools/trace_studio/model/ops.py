"""model/ops.py — the canonical segtrace parser, linter and compiler.

THE design rule (from openrecet's Phase-3 finding, where every new segtrace op
had to be taught to THREE parsers): the engine parsers are FROZEN at the
primitive op set —

    {"t":N,"m":M,"w":W,"l":L}     input (src/input_trace.c + agent)
    {"wait":"ANCHOR"}             clock-rebase
    {"capture":N,"label":"..."}   anchor-relative labeled capture
    {"wait_until":{...}}          retail-only predicate (port ignores)

ALL sugar lives here, lowered to primitives both engines already speak:

    {"caprange":[START,COUNT],"label":"grp","stride":S}
        → one {capture} per kept frame, labeled "grp.OOOO" where OOOO is the
          zero-padded offset from START.  Offset-naming makes an overview
          (stride 8) and a drill (stride 1) of the same range agree on the
          label of any shared instant.

Pairing is by LABEL on both sides (the port names files <label>.bmp; the
retail harness persists label→tick in captures.jsonl) — immune to the
kept-count/ordinal drift class of bugs by construction.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

# Anchor vocabularies (agent anchorTick / main.c current_tas_anchor).
RETAIL_ANCHORS = {"BOOT", "TITLE", "NEW_GAME", "LOADING_START",
                  "LOADING_END", "MAP_FREEROAM"}
PORT_ANCHORS = {"TITLE", "MAP_FREEROAM"}

# Engine caps (src/input_trace.h) the compiler must respect.
MAX_OPS = 8192
MAX_FRAME_ACTIONS = 16
MAX_LABEL = 31              # INPUT_TRACE_MAX_LABEL - 1 (NUL)

LABEL_RX = re.compile(r"^[A-Za-z0-9_.\-]+$")


def load_lines(path: Path) -> list[str]:
    return path.read_text().splitlines()


def parse(lines: list[str]) -> list[dict]:
    """Parse studio-trace lines into op dicts.

    Each op carries `_kind` (comment|input|wait|capture|caprange|wait_until|
    unknown), `_line` (1-based source line) and `_raw` (the source text).
    Malformed JSON → _kind unknown (lint surfaces it; the C parser would
    silently skip, which is exactly what lint exists to catch).
    """
    ops: list[dict] = []
    for i, raw in enumerate(lines, 1):
        s = raw.strip()
        op: dict = {"_line": i, "_raw": raw}
        if not s or s.startswith("#"):
            op["_kind"] = "comment"
            ops.append(op)
            continue
        try:
            d = json.loads(s)
            if not isinstance(d, dict):
                raise ValueError("not an object")
        except ValueError:
            op["_kind"] = "unknown"
            ops.append(op)
            continue
        op.update(d)
        if "caprange" in d:
            op["_kind"] = "caprange"
        elif "capture" in d:
            op["_kind"] = "capture"
        elif "wait" in d:
            op["_kind"] = "wait"
        elif "wait_until" in d:
            op["_kind"] = "wait_until"
        elif "auto_dismiss" in d:
            op["_kind"] = "auto_dismiss"
        elif "m" in d and "t" in d:
            op["_kind"] = "input"
        else:
            op["_kind"] = "unknown"
        ops.append(op)
    return ops


def _range_label(group: str, off: int) -> str:
    return f"{group}.{off:04d}"


def compile_ops(ops: list[dict]) -> tuple[list[str], list[dict]]:
    """Lower studio ops to primitive trace lines + the capture plan.

    Returns (compiled_lines, plan) where plan is the ordered capture list:
    [{"label", "group" (None for plain captures), "n", "seg"}].  `seg` is the
    0-based segment index (count of {wait}s before the op) — labels are
    anchor-relative WITHIN their segment.
    """
    out: list[str] = []
    plan: list[dict] = []
    seg = 0
    for op in ops:
        k = op["_kind"]
        if k == "comment":
            out.append(op["_raw"])
        elif k == "wait":
            seg += 1
            out.append(json.dumps({"wait": op["wait"]}))
        elif k == "wait_until":
            out.append(json.dumps({"wait_until": op["wait_until"]}))
        elif k == "auto_dismiss":
            # Reactive mode toggle — passes through verbatim; both engine
            # parsers (src/input_trace.c, openlords2-agent.js) honour it.
            out.append(json.dumps({"auto_dismiss": int(op["auto_dismiss"])}))
        elif k == "input":
            # Maximally explicit primitives — every engine parser sees all
            # four fields regardless of its own default-handling.
            out.append(json.dumps({"t": op["t"], "m": op["m"],
                                   "w": op.get("w", 0),
                                   "l": op.get("l", 0)}))
        elif k == "capture":
            label = str(op.get("label") or "")
            out.append(json.dumps({"capture": op["capture"], "label": label}))
            plan.append({"label": label, "group": None,
                         "n": int(op["capture"]), "seg": seg})
        elif k == "caprange":
            start, count = int(op["caprange"][0]), int(op["caprange"][1])
            stride = max(1, int(op.get("stride", 1)))
            group = str(op.get("label") or "")
            out.append(f"# caprange {group}: [{start},{count}] stride {stride}")
            for off in range(0, count, stride):
                lbl = _range_label(group, off)
                out.append(json.dumps({"capture": start + off, "label": lbl}))
                plan.append({"label": lbl, "group": group,
                             "n": start + off, "seg": seg})
        # unknown lines are dropped from the compiled trace (lint flags them)
    return out, plan


def lint(ops: list[dict], target: str = "both") -> list[dict]:
    """Preflight a studio trace.  Returns [{level: error|warn, code, msg}]."""
    f: list[dict] = []

    def err(code: str, op: dict | None, msg: str) -> None:
        where = f"line {op['_line']}: " if op else ""
        f.append({"level": "error", "code": code, "msg": where + msg})

    def warn(code: str, op: dict | None, msg: str) -> None:
        where = f"line {op['_line']}: " if op else ""
        f.append({"level": "warn", "code": code, "msg": where + msg})

    seen_wait = False
    last_t = None
    for op in ops:
        k = op["_kind"]
        if k == "unknown":
            err("unknown-op", op, f"unparseable op: {op['_raw'][:60]!r} "
                "(the engine parsers would silently skip this)")
        elif k == "wait":
            seen_wait = True
            last_t = None
            a = str(op["wait"])
            if a not in RETAIL_ANCHORS:
                err("bad-anchor", op, f"unknown anchor {a!r} "
                    f"(vocab: {', '.join(sorted(RETAIL_ANCHORS))})")
            elif target in ("both", "port") and a not in PORT_ANCHORS:
                warn("port-anchor-gap", op,
                     f"anchor {a!r} is not emitted by the port yet "
                     f"(port vocab: {', '.join(sorted(PORT_ANCHORS))}) — "
                     "the port will park on this {wait} forever")
        elif k == "wait_until":
            if target in ("both", "port"):
                warn("wait-until-port", op, "wait_until is retail-only; the "
                     "port parses-and-ignores it (replay may desync)")
        elif k == "input":
            t = int(op["t"])
            if t < 0:
                err("neg-t", op, "negative t")
            if last_t is not None and t < last_t:
                warn("t-order", op, f"t={t} goes backwards within its segment "
                     f"(prev {last_t}) — ops drain in order, this one fires "
                     "late")
            last_t = t
        elif k in ("capture", "caprange"):
            label = str(op.get("label") or "")
            if not label:
                err("no-label", op, f"{k} without a label (pairing is "
                    "label-keyed)")
            elif not LABEL_RX.match(label):
                err("bad-label", op, f"label {label!r} (charset "
                    "[A-Za-z0-9_.-]; it becomes a filename on both sides)")
            if k == "caprange":
                if (not isinstance(op["caprange"], list)
                        or len(op["caprange"]) != 2):
                    err("bad-caprange", op, "caprange wants [START,COUNT]")
                elif label and LABEL_RX.match(label):
                    start, count = op["caprange"]
                    if int(count) <= 0:
                        err("bad-caprange", op, "COUNT must be > 0")
                    last_lbl = _range_label(label,
                                            max(0, int(count) - 1))
                    if len(last_lbl) > MAX_LABEL:
                        err("label-len", op, f"generated label {last_lbl!r} "
                            f"exceeds {MAX_LABEL} chars (port cap)")
            elif len(label) > MAX_LABEL:
                err("label-len", op, f"label exceeds {MAX_LABEL} chars "
                    "(port cap)")
            if not seen_wait:
                warn("boot-relative", op, "capture before the first {wait} "
                     "is boot-relative — fragile across engines")

    # Post-compile structural checks (label dups, op count, per-frame caps).
    compiled, plan = compile_ops(ops)
    labels = [p["label"] for p in plan]
    dups = {x for x in labels if labels.count(x) > 1}
    if dups:
        err("dup-label", None, f"duplicate capture labels after compile: "
            f"{', '.join(sorted(dups)[:8])}")
    n_engine_ops = sum(1 for ln in compiled
                       if ln.strip() and not ln.strip().startswith("#"))
    if n_engine_ops > MAX_OPS:
        err("op-overflow", None, f"compiled trace has {n_engine_ops} ops "
            f"> port cap {MAX_OPS} (raise the stride or split the trace)")
    per_frame: dict[tuple[int, int], int] = {}
    for p in plan:
        key = (p["seg"], p["n"])
        per_frame[key] = per_frame.get(key, 0) + 1
    worst = max(per_frame.values(), default=0)
    if worst > MAX_FRAME_ACTIONS:
        err("frame-actions", None, f"{worst} captures on one frame > port "
            f"per-frame cap {MAX_FRAME_ACTIONS}")
    if not plan:
        warn("no-captures", None, "trace captures nothing — the session will "
             "have no frames to compare")
    return f


def read_jsonl(path: Path) -> list[dict]:
    """Lenient JSONL reader for the anchors/captures side files."""
    rows: list[dict] = []
    if not path.exists():
        return rows
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except ValueError:
            continue
    return rows
