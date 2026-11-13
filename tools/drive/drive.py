#!/usr/bin/env python3
"""rd — drive retail Lords of the Realm II live, at savestate speed, and turn what you did into a verified trace.

One command per call; the run directory is the state. Behind a run sits a LIVE rhost process (retail as a coroutine,
`rhost --live`): it steps frames on demand, reads memory, takes in-process savestates, takes pictures and runs the
exploration search. That process is only a cache — a run is its MOVIE (every input event since boot); a dead session
is rebuilt by replaying it (~4,500 frames/s).

    rd new R --from fresh | SCENARIO[@F] | RUN[:CK|@F]                    start a run
    rd play R "c320,107 .*30 r300,240 kESC c@endturn" [--until EXPR] [--avoid EXPR] [--watch EXPR,..] [--every N]
    rd status R | rd eval R EXPR.. [--at CK | --at-frame F] | rd ram R list | read VA [N] | dwords VA [N] | define NAME .. | mark | diff
    rd look R [--at F,..]  | rd strip R --from F --to F [--every N]     pictures (the session's own frame: exact)
    rd ck R save NAME | load NAME | list        rd undo R        rd rewind R --to F
    rd search R --goal EXPR [--avoid EXPR] [--cell TUPLE] [--actions SPEC ..] [--score EXPR] [--apply|--apply-best] [-j N]
    rd scan R --from F [--to T] --watch EXPR,.. [--every N] [--changes] [--where EXPR]
    rd verify R        rd check R TASK.md      rd lint TASK.md --on RUN..
    rd file R NAME     tests/scenarios/NAME (then rhost.py corpus --bless --only NAME; verify)
    rd stop R | rd ls | rd movie R

Method and reference: tools/drive/README.md; the one-page card for a driving agent: tools/drive/DRIVER.md.
"""
import argparse
import ast
import fcntl
import hashlib
import json
import math
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "rhost"))
import rhost as RH  # noqa: E402
try:
    sys.path.insert(0, os.path.join(ROOT, "tools", "bunker"))
    import bunker as BK  # noqa: E402
except ImportError:                                       # (bunker tooling absent: win / wine only)
    BK = None

RUNS = os.environ.get("RD_RUNS", os.path.join(RH.EPH, "drive", "runs"))
SESS = os.path.join(RH.WINE_ROOT, "runs", "drive")     # wine sessions' run dirs (C:\ol2-rhost\runs\drive\..)
WIN_SESS = f"{RH.WROOT}\\runs\\drive"
VARS = os.path.join(HERE, "vars.json")
UI = os.path.join(HERE, "ui.json")
HOLD = int(os.environ.get("RD_HOLD", "2"))             # frames between a click's move / down / up (rhbot.py's default)

WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_RBUTTONDOWN, WM_RBUTTONUP = 0x200, 0x201, 0x202, 0x204, 0x205
WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x100, 0x101, 0x102
VK = {"ESC": 0x1B, "ENTER": 0x0D, "RETURN": 0x0D, "SPACE": 0x20, "TAB": 0x09, "BACK": 0x08, "LEFT": 0x25, "UP": 0x26,
      "RIGHT": 0x27, "DOWN": 0x28, "PGUP": 0x21, "PGDN": 0x22, "HOME": 0x24, "END": 0x23, "DEL": 0x2E, "PAUSE": 0x13,
      **{f"F{i}": 0x6F + i for i in range(1, 13)}}


# ------------------------------------------------------------------ input specs
def ui_points():
    return {k: v for k, v in json.load(open(UI)).items() if not k.startswith("_")} if os.path.exists(UI) else {}


def split_coord(s):
    """"320,107" / "@endturn" / "{ax},{ay + 4}" → (x, y) as int or ("expr", text)"""
    if s.startswith("@"):
        pts = ui_points()
        if s[1:] not in pts:
            raise SystemExit(f"unknown UI point {s!r} (tools/drive/ui.json: {' '.join(sorted(pts))})")
        return tuple(pts[s[1:]]["xy"])
    parts = split_top(s, ",")
    if len(parts) != 2:
        raise SystemExit(f"bad coordinates {s!r}: X,Y | @name | {{EXPR}},{{EXPR}}")
    out = []
    for p in parts:
        if p.startswith("{") and p.endswith("}"):
            out.append(("expr", p[1:-1]))
        else:
            try:
                out.append(int(p))
            except ValueError:
                raise SystemExit(f"bad coordinate {p!r} in {s!r}")
    return tuple(out)


def ev(m, w, xy):
    return ("e", m, w, xy)


def parse_token(t):
    """one token → [("w", n) | ("e", m, w, l-or-(x, y))]"""
    m = re.fullmatch(r"(.*?)(?:\*(\d+))?", t)
    body, n = m.group(1), int(m.group(2) or 1)
    if not body:
        raise SystemExit(f"bad token {t!r}")
    if body == ".":
        return [("w", n)]
    k, rest = body[0], body[1:]
    if k in "cr":                                         # click: move, HOLD, down, HOLD, up, HOLD
        xy = split_coord(rest)
        dn, up, mk = (WM_LBUTTONDOWN, WM_LBUTTONUP, 1) if k == "c" else (WM_RBUTTONDOWN, WM_RBUTTONUP, 2)
        one = [ev(WM_MOUSEMOVE, 0, xy), ("w", HOLD), ev(dn, mk, xy), ("w", HOLD), ev(up, 0, xy), ("w", HOLD)]
        return one * n
    if k == "m":                                          # move the cursor, then HOLD frames
        return [ev(WM_MOUSEMOVE, 0, split_coord(rest)), ("w", HOLD)] * n
    if k == "h":                                          # hover: move, rest 20 frames (retail acts on mouse idle)
        return [ev(WM_MOUSEMOVE, 0, split_coord(rest)), ("w", 20)] * n
    if k == "d":                                          # drag with the left button: dX,Y-X2,Y2
        if "-" not in rest:
            raise SystemExit(f"drag: dX,Y-X2,Y2 ({t!r})")
        a, b = rest.split("-", 1)
        p, q = split_coord(a), split_coord(b)
        steps = []
        if all(isinstance(v, int) for v in p + q):
            for i in range(1, 5):
                steps += [ev(WM_MOUSEMOVE, 1, (p[0] + (q[0] - p[0]) * i // 4, p[1] + (q[1] - p[1]) * i // 4)), ("w", HOLD)]
        else:
            steps += [ev(WM_MOUSEMOVE, 1, q), ("w", HOLD)]
        return ([ev(WM_MOUSEMOVE, 0, p), ("w", HOLD), ev(WM_LBUTTONDOWN, 1, p), ("w", HOLD)] + steps +
                [ev(WM_LBUTTONUP, 0, q), ("w", HOLD)]) * n
    if k == "k":                                          # key: kESC kENTER kA k5 (down + char + up)
        name = rest.upper()
        if name in VK:
            vk, ch = VK[name], {"ENTER": 13, "RETURN": 13, "SPACE": 32, "TAB": 9, "BACK": 8, "ESC": 27}.get(name)
        elif len(rest) == 1 and rest.isalnum():
            vk, ch = ord(rest.upper()), ord(rest)
        else:
            raise SystemExit(f"unknown key {rest!r} (kESC kENTER kSPACE kF1 kA k5 kLEFT …)")
        one = [ev(WM_KEYDOWN, vk, 1)] + ([ev(WM_CHAR, ch, 1)] if ch is not None else []) + [("w", HOLD), ev(WM_KEYUP, vk, 0xC0000001), ("w", HOLD)]
        return one * n
    if k == "t":                                          # type text: tHello_World (_ = space)
        out = []
        for ch in rest.replace("_", " "):
            vk = ord(ch.upper()) if ch.isalnum() else 0x20
            out += [ev(WM_KEYDOWN, vk, 1), ev(WM_CHAR, ord(ch), 1), ("w", HOLD), ev(WM_KEYUP, vk, 0xC0000001), ("w", HOLD)]
        return out * n
    if k == "e":                                          # raw event: eM:W:L (decimal / 0x)
        p = rest.split(":")
        if len(p) != 3:
            raise SystemExit(f"raw event eM:W:L ({t!r})")
        return [("e", int(p[0], 0), int(p[1], 0), int(p[2], 0))] * n
    raise SystemExit(f"bad input token {t!r}: .*N  cX,Y  rX,Y  mX,Y  hX,Y  dX,Y-X2,Y2  kESC  tTEXT  eM:W:L  (X,Y: numbers, @name, {{EXPR}})")


def parse_spec(spec):
    """"c320,107 .*30 (c10,10 .*25)x3" → items"""
    spec = spec.strip().strip("\"'`").strip()
    out, i = [], 0
    while i < len(spec):
        if spec[i].isspace():
            i += 1
        elif spec[i] == "(":
            j, depth, brace = i, 0, 0
            while j < len(spec):
                ch = spec[j]
                brace += (ch == "{") - (ch == "}")
                if not brace:
                    depth += (ch == "(") - (ch == ")")
                    if depth == 0:
                        break
                j += 1
            if j >= len(spec):
                raise SystemExit("unclosed ( in the input spec")
            m = re.match(r"x(\d+)", spec[j + 1:])
            if not m:
                raise SystemExit("a group needs a repeat count: (c10,10 .*25)x3")
            out += parse_spec(spec[i + 1:j]) * int(m.group(1))
            i = j + 1 + m.end()
        else:
            j, brace = i, 0
            while j < len(spec) and (brace or not spec[j].isspace()):
                brace += (spec[j] == "{") - (spec[j] == "}")
                j += 1
            out += parse_token(spec[i:j])
            i = j
    return out


def items_to_segs(items, prog=None, sep=","):
    """items → server segments "w8,e512:0:7012672,…" (+ total frames); prog(expr) → program id for {EXPR} coordinates"""
    segs, total = [], 0
    for it in items:
        if it[0] == "w":
            if it[1] <= 0:
                continue
            total += it[1]
            if segs and segs[-1][0] == "w":
                segs[-1][1] += it[1]
            else:
                segs.append(["w", it[1]])
            continue
        _, m, w, l = it
        if isinstance(l, tuple):
            x, y = l
            if isinstance(x, int) and isinstance(y, int):
                l = (y << 16) | (x & 0xffff)
            else:
                if prog is None:
                    raise SystemExit("{EXPR} coordinates need a live session")
                px = prog(x[1] if isinstance(x, tuple) else str(x))
                py = prog(y[1] if isinstance(y, tuple) else str(y))
                segs.append(["e", f"{m}:{w}:p{px}/p{py}"])
                continue
        segs.append(["e", f"{m}:{w}:{l}"])
    return sep.join(f"w{v}" if k == "w" else f"e{v}" for k, v in segs), total


def segs_to_spec(path):
    """server path "w8,e512:0:L,…" → an input spec that replays it exactly (raw events + waits)"""
    out = []
    for s in filter(None, path.split(",")):
        if s[0] == "w":
            out.append(f".*{s[1:]}" if s[1:] != "1" else ".")
        else:
            m, w, l = (int(x) for x in s[1:].split(":"))
            nm = {WM_MOUSEMOVE: "m", WM_LBUTTONDOWN: "LD", WM_LBUTTONUP: "LU", WM_RBUTTONDOWN: "RD", WM_RBUTTONUP: "RU"}.get(m)
            out.append(f"e{m}:{w}:{l}" + (f"#{nm}({l & 0xffff},{l >> 16})" if nm and False else ""))
    return " ".join(out)


def describe_path(path):
    """a readable summary of a server path: clicks at (x, y) with frame offsets"""
    f, out, last_move = 0, [], None
    for s in filter(None, path.split(",")):
        if s[0] == "w":
            f += int(s[1:])
            continue
        m, w, l = (int(x) for x in s[1:].split(":"))
        x, y = l & 0xffff, l >> 16
        if m == WM_MOUSEMOVE:
            last_move = (x, y)
        elif m == WM_LBUTTONDOWN:
            out.append(f"+{f} L({x},{y})")
        elif m == WM_RBUTTONDOWN:
            out.append(f"+{f} R({x},{y})")
        elif m == WM_KEYDOWN:
            out.append(f"+{f} key {w:#x}")
    if last_move and not out:
        out.append(f"cursor ({last_move[0]},{last_move[1]})")
    return " ".join(out) + f"  [{f} frames]"


# ------------------------------------------------------------------ expressions → postfix programs
MAX_PROG_TOKENS = 598
MAX_STACK = 64
EVAL_CHUNK = 48
MAX_SAMPLED = 32
_ARITY = {"r8": 1, "r16": 1, "r32": 1, "s8": 1, "s16": 1, "s32": 1, "f32": 1, "f64": 1, "~": 1, "not": 1, "neg": 1, "abs": 1,
          "sqrt": 1, "sin": 1, "cos": 1, "sel": 3, "frame": 0}
_BINARY = {"+", "-", "*", "/", "//", "%", "&", "|", "^", "<<", ">>", "<", "<=", ">", ">=", "==", "!=", "and", "or", "min", "max",
           "atan2", "hypot", "pow"}
TYPES = {"u8": "r8", "s8": "s8", "u16": "r16", "s16": "s16", "u32": "r32", "s32": "s32", "i32": "s32", "i16": "s16", "f32": "f32", "f64": "f64"}
BINOP = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/", ast.FloorDiv: "//", ast.Mod: "%", ast.BitAnd: "&",
         ast.BitOr: "|", ast.BitXor: "^", ast.LShift: "<<", ast.RShift: ">>"}
CMPOP = {ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">=", ast.Eq: "==", ast.NotEq: "!="}


def stack_depth(tokens):
    sp = hi = 0
    for t in tokens:
        if t in _BINARY:
            sp -= 1
        elif t in _ARITY:
            sp += 1 - _ARITY[t]
        else:
            sp += 1
        hi = max(hi, sp)
    return hi


def prog_check(tokens, what="expression"):
    if len(tokens) > MAX_PROG_TOKENS:
        raise SystemExit(f"{what}: {len(tokens)} postfix tokens > {MAX_PROG_TOKENS} (the live server's limit): narrow it — "
                         "count(R, COND, LO, HI) over fewer slots")
    if stack_depth(tokens) > MAX_STACK:
        raise SystemExit(f"{what}: needs a stack of {stack_depth(tokens)} values > {MAX_STACK}")
    return tokens


def split_top(s, sep=","):
    out, depth, cur = [], 0, []
    for ch in s or "":
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            out.append("".join(cur)); cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return [x.strip() for x in out if x.strip()]


class Vars:
    """named state: tools/drive/vars.json (shared) + <run>/vars.json (the run's own). Records ("_records"): a name like
    C (counties) with base / stride / slots / fields → `C(k).owner` reads county k's owner (k any expression), and
    `count(C, COND[, LO, HI])` counts slots where COND holds, COND over the slot's fields by name (and I = the index)."""

    def __init__(self, run_dir=None, also=()):
        self.defs, self.records, self.local_path, self._slot = {}, {}, run_dir and os.path.join(run_dir, "vars.json"), None
        for p in [VARS] + [os.path.join(d, "vars.json") for d in also] + [self.local_path]:
            if p and os.path.exists(p):
                d = json.load(open(p))
                self.records.update(d.get("_records", {}))
                self.defs.update({k: v for k, v in d.items() if not k.startswith("_")})

    def define(self, name, d, shared=False):
        p = VARS if shared else self.local_path
        cur = json.load(open(p)) if os.path.exists(p) else {}
        cur[name] = d
        json.dump(cur, open(p + ".tmp", "w"), indent=1)
        os.replace(p + ".tmp", p)
        self.defs[name] = d

    def compile(self, expr, depth=0):
        try:
            tree = ast.parse(expr.strip(), mode="eval").body
        except SyntaxError as e:
            raise SystemExit(f"bad expression {expr!r}: {e.msg}")
        return self._c(tree, depth, expr)

    def _field(self, rec, idx_tokens, field, src):
        r = self.records[rec]
        if field not in r["fields"]:
            raise SystemExit(f"{rec}(..).{field}: no such field (fields of {rec}: {' '.join(sorted(r['fields']))}) in {src!r}")
        off, ty = r["fields"][field][:2]
        return idx_tokens + [str(int(str(r["stride"]), 0)), "*", str(int(str(r["base"]), 16) + int(str(off), 16)), "+", TYPES[ty]]

    def _c(self, n, depth, src):
        c = lambda x: self._c(x, depth, src)
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float, bool)):
            return [repr(float(n.value)) if isinstance(n.value, float) else str(int(n.value))]
        if isinstance(n, ast.Name):
            if self._slot is not None and n.id in self._slot:
                return list(self._slot[n.id])
            if n.id == "frame":
                return ["frame"]
            if n.id == "pi" and "pi" not in self.defs:
                return [repr(math.pi)]
            if n.id not in self.defs:
                raise SystemExit(f"unknown name {n.id!r} in {src!r} — `rd ram R list`, or define it (`rd ram R define`)")
            if depth > 12:
                raise SystemExit(f"variable definitions recurse at {n.id!r}")
            d = self.defs[n.id]
            if "expr" in d:
                return self.compile(d["expr"], depth + 1)
            return [str(int(str(d["addr"]), 16)), TYPES[d.get("type", "s32")]]
        if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Call) and isinstance(n.value.func, ast.Name) \
                and n.value.func.id in self.records and len(n.value.args) == 1:
            return self._field(n.value.func.id, c(n.value.args[0]), n.attr, src)
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name):
            f = n.func.id
            if f in TYPES and len(n.args) == 1:
                return c(n.args[0]) + [TYPES[f]]
            if f in ("min", "max") and len(n.args) >= 2:
                out = c(n.args[0])
                for a in n.args[1:]:
                    out += c(a) + [f]
                return out
            if f == "sum":
                return self._fold([c(e) for e in n.args])
            if f == "count":
                return self._fold(self._count_terms(n, depth, src))
            if f == "abs" and len(n.args) == 1:
                return c(n.args[0]) + ["abs"]
            if f == "int" and len(n.args) == 1:
                return c(n.args[0]) + ["1", "//"]
            if f in ("sqrt", "sin", "cos") and len(n.args) == 1:
                return c(n.args[0]) + [f]
            if f in ("atan2", "hypot", "pow") and len(n.args) == 2:
                return c(n.args[0]) + c(n.args[1]) + [f]
            raise SystemExit(f"unknown function {f}() in {src!r} (u8 s8 u16 s16 u32 s32 f32 min max abs int sqrt sin cos atan2 hypot "
                             f"pow sum count; records: {' '.join(f'{k}(i).field' for k in self.records)})")
        if isinstance(n, ast.BinOp) and isinstance(n.op, ast.Pow):
            return c(n.left) + c(n.right) + ["pow"]
        if isinstance(n, ast.BinOp) and type(n.op) in BINOP:
            return c(n.left) + c(n.right) + [BINOP[type(n.op)]]
        if isinstance(n, ast.UnaryOp):
            if isinstance(n.op, ast.USub):
                return c(n.operand) + ["neg"]
            if isinstance(n.op, ast.Not):
                return c(n.operand) + ["not"]
            if isinstance(n.op, ast.Invert):
                return c(n.operand) + ["~"]
            if isinstance(n.op, ast.UAdd):
                return c(n.operand)
        if isinstance(n, ast.BoolOp):
            out = c(n.values[0])
            for v in n.values[1:]:
                out += c(v) + ["and" if isinstance(n.op, ast.And) else "or"]
            return out
        if isinstance(n, ast.IfExp):
            return c(n.test) + c(n.body) + c(n.orelse) + ["sel"]
        if isinstance(n, ast.Compare):
            out, left = None, n.left
            for op, right in zip(n.ops, n.comparators):
                if isinstance(op, (ast.In, ast.NotIn)):
                    if not isinstance(right, (ast.Tuple, ast.List)) or not right.elts:
                        raise SystemExit(f"`in` needs a tuple of values in {src!r}")
                    one = None
                    for e in right.elts:
                        t = c(left) + c(e) + ["=="]
                        one = t if one is None else one + t + ["or"]
                    if isinstance(op, ast.NotIn):
                        one += ["not"]
                elif type(op) in CMPOP:
                    one = c(left) + c(right) + [CMPOP[type(op)]]
                else:
                    raise SystemExit(f"unsupported comparison in {src!r}")
                out = one if out is None else out + one + ["and"]
                left = right
            return out
        raise SystemExit(f"unsupported syntax in {src!r}: {ast.dump(n)[:80]}")

    @staticmethod
    def _fold(terms):
        out = list(terms[0]) if terms else ["0"]
        for t in terms[1:]:
            out += t + ["+"]
        return out

    def _count_terms(self, n, depth, src):
        """count(REC, COND[, LO, HI]) → one program term per slot: 1 when COND holds (fields by name, I = slot)"""
        if self._slot is not None:
            raise SystemExit(f"count() cannot nest ({src!r})")
        if len(n.args) not in (2, 4) or not isinstance(n.args[0], ast.Name) or n.args[0].id not in self.records:
            raise SystemExit(f"count(REC, COND[, LO, HI]) with REC one of {' '.join(self.records)} ({src!r})")
        r = self.records[n.args[0].id]
        lo, hi = r["slots"]
        if len(n.args) == 4:
            lo, hi = (int(ast.literal_eval(a)) for a in n.args[2:])
        terms = []
        for i in range(lo, hi + 1):
            base = int(str(r["base"]), 16) + i * int(str(r["stride"]), 0)
            self._slot = {k: [str(base + int(str(off), 16)), TYPES[ty]] for k, (off, ty, *_) in r["fields"].items()}
            self._slot["I"] = [str(i)]
            try:
                terms.append(self._c(n.args[1], depth, src) + ["0", "!="])
            finally:
                self._slot = None
        return terms

    def _terms(self, n, depth, src):
        if isinstance(n, ast.BinOp) and isinstance(n.op, (ast.Add, ast.Sub)):
            right = self._terms(n.right, depth, src)
            if isinstance(n.op, ast.Sub):
                right = [(-s, t) for s, t in right]
            return self._terms(n.left, depth, src) + right
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == "sum":
            return [x for e in n.args for x in self._terms(e, depth, src)]
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == "count":
            return [(1, t) for t in self._count_terms(n, depth, src)]
        if isinstance(n, ast.Name) and n.id in self.defs and "expr" in self.defs[n.id] and depth <= 12:
            d = self.defs[n.id]["expr"]
            return self._terms(ast.parse(d.strip(), mode="eval").body, depth + 1, d)
        return [(1, self._c(n, depth, src))]

    def compile_parts(self, expr):
        toks = self.compile(expr)
        if len(toks) <= MAX_PROG_TOKENS and stack_depth(toks) <= MAX_STACK:
            return [toks]
        terms = self._terms(ast.parse(expr.strip(), mode="eval").body, 0, expr)
        if len(terms) == 1:
            prog_check(toks, repr(expr))
        parts, cur = [], None
        for s, t in terms:
            if cur is not None and len(cur) + len(t) + 1 <= MAX_PROG_TOKENS:
                cur += t + (["+"] if s > 0 else ["-"])
            else:
                if cur is not None:
                    parts.append(cur)
                cur = t + ([] if s > 0 else ["neg"])
        parts.append(cur)
        return parts

    def program(self, expr, what=None):
        return prog_check(self.compile(expr), what or repr(expr))

    def compile_tuple(self, expr):
        tree = ast.parse(expr.strip(), mode="eval").body
        elts = tree.elts if isinstance(tree, ast.Tuple) else [tree]
        return [self._c(e, 0, expr) for e in elts]


# ------------------------------------------------------------------ the movie
class Movie:
    """every input event since boot [(frame, m, w, l)] + the frame it ends at + the start saves (files copied into the
    run dir before boot: the game's *.sav, castles.dat — tests/scenarios/<s>/_saves/)"""

    def __init__(self, events=None, frame=0, saves=None, extra=None):
        self.events, self.frame, self.saves, self.extra = events or [], frame, saves or [], extra or {}

    @staticmethod
    def from_server(text, saves, extra=None):
        kv = dict(t.split("=", 1) for t in text.split()[1:] if "=" in t)
        evs = [tuple(int(x) for x in e.split(":")) for e in filter(None, kv.get("ev", "").split(","))]
        return Movie(evs, int(kv["frame"]), saves, extra)

    @staticmethod
    def load(path):
        mv = Movie()
        last = 0
        for line in open(path):
            line = line.strip()
            if not line.startswith("{"):
                continue
            d = json.loads(line)
            if "meta" in d:
                m = d["meta"]
                if m.get("frames"):
                    mv.frame = int(m["frames"])
                mv.saves = [os.path.normpath(os.path.join(os.path.dirname(path), s)) for s in m.get("saves", [])]
                mv.extra = {k: v for k, v in m.items() if k in ("step_ms", "vbase", "movies")}
                continue
            f = int(d.get("f", d.get("t", 0)))
            mv.events.append((f, int(d["m"]), int(d.get("w", 0)), int(d.get("l", 0))))
            last = max(last, f)
        if not mv.frame:
            mv.frame = last
        return mv

    def write(self, path, comment="", rel_to=None):
        """a flat rhost trace (tests/scenarios format): replays to exactly self.frame"""
        base = rel_to or os.path.dirname(os.path.abspath(path))
        meta = dict(self.extra, frames=self.frame)
        if self.saves:
            meta["saves"] = [os.path.relpath(s, base) for s in self.saves]
        with open(path + ".tmp", "w") as f:
            f.write(f"# rhost trace (rd movie{': ' + comment if comment else ''}): frame-absolute from boot\n")
            f.write(json.dumps({"meta": meta}) + "\n")
            for fr, m, w, l in self.events:
                if fr > self.frame:
                    break
                f.write(f'{{"f": {fr}, "m": {m}, "w": {w}, "l": {l}}}\n')
        os.replace(path + ".tmp", path)

    def truncated(self, frame):
        return Movie([e for e in self.events if e[0] <= frame], min(frame, self.frame), self.saves, self.extra)


# ------------------------------------------------------------------ a live rhost process
class SessionEnded(Exception):
    """the rhost process went away mid-command: the game exited or crashed"""


class Live:
    def __init__(self, port, timeout=3600):
        last = None
        for _ in range(150):
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError as e:
                last = e
                time.sleep(0.1)
        else:
            raise OSError(f"cannot connect to the live session on port {port}: {last}")
        self.s.settimeout(timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.f = self.s.makefile("rwb")
        self.nprog = 0

    def cmd(self, line, check=True):
        self.f.write(line.encode() + b"\n")
        self.f.flush()
        r = self.f.readline().decode(errors="replace").rstrip("\n")
        if not r:
            raise SessionEnded()
        if check and not r.startswith("ok"):
            raise SystemExit(f"live: {r}\n  (command: {line[:200]})")
        return r

    @staticmethod
    def kv(r):
        return dict(t.split("=", 1) for t in r.split()[1:] if "=" in t)

    def prog(self, tokens):
        prog_check(tokens, "program " + " ".join(tokens[:8]))
        if self.nprog >= 250:
            self.nprog = 0
        pid = self.nprog
        self.nprog += 1
        self.cmd(f"prog {pid} " + " ".join(tokens))
        return pid

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def num(v):
    return int(v) if v == int(v) and abs(v) < 1e15 else v


def fmt(v):
    if isinstance(v, float) and v != int(v):
        return f"{v:.4f}".rstrip("0").rstrip(".")
    return str(int(v))


def eval_exprs(lv, V, exprs):
    flat = [(i, p) for i, e in enumerate(exprs) for p in V.compile_parts(e)]
    vals = [0.0] * len(exprs)
    for c in range(0, len(flat), EVAL_CHUNK):
        batch = flat[c:c + EVAL_CHUNK]
        ids = [lv.prog(p) for _, p in batch]
        for (i, _), x in zip(batch, Live.kv(lv.cmd("eval " + ",".join(map(str, ids))))["v"].split(":")):
            vals[i] += float(x)
    return [num(v) for v in vals]


def sampled_cols(lv, V, exprs, room=MAX_SAMPLED):
    ids, slices = [], []
    for e in exprs:
        parts = V.compile_parts(e)
        slices.append((len(ids), len(ids) + len(parts)))
        if len(ids) + len(parts) > room:
            raise SystemExit(f"at most {room} sampled programs per run (the live server's limit) — split the watch list")
        ids += [lv.prog(p) for p in parts]
    return ids, slices


def fold_row(vals, slices):
    return [num(sum(vals[a:b])) for a, b in slices]


def parse_samples(text):
    rows = []
    for row in filter(None, (text or "").split(";")):
        p = row.split(":")
        rows.append((int(p[0]), [float(x) for x in p[1:]]))
    return rows


# ------------------------------------------------------------------ where live sessions run
# RD_HOST=auto (default): the Windows box (native; `ssh win`), else bunker (tools/bunker: wine in a bounded pool), else wine
# here. A session keeps the host it started on (session.json "host"). Game state is identical on all three (rhost corpus:
# expected.json == expected.wine.json — Lords2 has no float state that differs by host).
RD_HOST = os.environ.get("RD_HOST", "auto")
HOSTS = ("win", "bunker", "wine")
WIN_MAX_SESSIONS = int(os.environ.get("RD_WIN_MAX_SESSIONS", "6"))
BUNKER_MAX_SESSIONS = int(os.environ.get("RD_BUNKER_MAX_SESSIONS", "8"))
BUNKER_FLOOR_MB = int(os.environ.get("RD_BUNKER_MEM_FLOOR_MB", "512"))
LOCAL_MAX = int(os.environ.get("RD_LOCAL_MAX", "6"))
LOCAL_FLOOR_GB = float(os.environ.get("RD_LOCAL_FLOOR_GB", "8"))
ADMIT_WAIT = {"win": float(os.environ.get("RD_WIN_ADMIT_WAIT", "30")), "bunker": float(os.environ.get("RD_BUNKER_ADMIT_WAIT", "30"))}
_up, _up_lock = {"wine": True}, threading.Lock()


def win_up(timeout=4):
    try:
        return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={timeout}", RH.WIN, "echo ok"], capture_output=True,
                              text=True, timeout=timeout + 8).stdout.strip().endswith("ok")
    except (subprocess.TimeoutExpired, OSError):
        return False


def host_up(h):
    with _up_lock:
        if h not in _up:
            if h == "win":
                _up[h] = win_up(timeout=3)
            elif h == "bunker":
                _up[h] = bool(BK) and BK.up(timeout=3)
        return _up[h]


def bin_sha():
    return hashlib.sha256(open(os.path.join(RH.BUILD, "rhost.exe"), "rb").read()).hexdigest()[:12]


_stage_lock = threading.Lock()


def staged_exe(host):
    """build if stale; the exe content-addressed (bin\\<sha12>\\ol2-rhost.exe) so a rebuild never swaps a running session's image"""
    with _stage_lock:
        if RH.rhost_stale():
            RH.cmd_build(None)
        sha = bin_sha()
        if host == "bunker":
            return BK.staged_exe(BK.stage())
        if host == "win":
            stamp = os.path.join(RH.BUILD, f".staged-win-{sha}")
            if not os.path.exists(stamp):
                RH.ssh_win(f"New-Item -ItemType Directory -Force '{RH.WROOT}\\bin\\{sha}' | Out-Null; exit 0")
                RH.scp(os.path.join(RH.BUILD, "rhost.exe"), f"{RH.WIN}:C:/ol2-rhost/bin/{sha}/{RH.WEXE}.new")
                RH.ssh_win(f"Move-Item -Force '{RH.WROOT}\\bin\\{sha}\\{RH.WEXE}.new' '{RH.WROOT}\\bin\\{sha}\\{RH.WEXE}'; exit 0")
                open(stamp, "w").close()
            return f"{RH.WROOT}\\bin\\{sha}\\{RH.WEXE}"
        RH.wine_stage()
        d = os.path.join(RH.WINE_ROOT, "bin", sha)
        if not os.path.exists(os.path.join(d, RH.WEXE)):
            os.makedirs(d, exist_ok=True)
            tmp = os.path.join(d, f"{RH.WEXE}.{os.getpid()}.new")
            shutil.copy2(os.path.join(RH.BUILD, "rhost.exe"), tmp)
            os.replace(tmp, os.path.join(d, RH.WEXE))
        return f"{RH.WROOT}\\bin\\{sha}\\{RH.WEXE}"


def local_listening(port):
    for f in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for l in open(f).readlines()[1:]:
                p = l.split()
                if p[3] == "0A" and int(p[1].rsplit(":", 1)[1], 16) == port:
                    return True
        except OSError:
            pass
    return False


def free_local_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def win_ps(ps, timeout=600):
    for attempt in range(4):
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", RH.WIN, ps], text=True, capture_output=True, timeout=timeout)
        if r.returncode == 255 and attempt < 3:
            time.sleep(2 + 3 * attempt)
            continue
        return r.returncode, r.stdout
    return r.returncode, r.stdout


class NotAdmitted(SystemExit):
    """a host's admission gave no slot in time"""


def mem_available_gb():
    for l in open("/proc/meminfo"):
        if l.startswith("MemAvailable:"):
            return int(l.split()[1]) / 1048576
    return 0


def local_sessions():
    n = 0
    for p in os.listdir("/proc"):
        if p.isdigit():
            try:
                if b"\\runs\\drive\\" in open(f"/proc/{p}/cmdline", "rb").read() and b"ol2-rhost" in open(f"/proc/{p}/cmdline", "rb").read():
                    n += 1
            except OSError:
                pass
    return n


def win_spawn(name, ld, args, wait, tag):
    """ld (a local dir with the session's files) → C:\\ol2-rhost\\runs\\drive\\NAME; start ol2-rhost.exe there detached
    (WMI: outside the job sshd kills on disconnect), admitted (≤ RD_WIN_MAX_SESSIONS of our sessions, ≥ 1.5 GB free)"""
    exe = staged_exe("win")
    rd = f"{WIN_SESS}\\{name}"
    dst = f"{RH.WIN}:{WIN_SESS.replace(chr(92), '/')}/"
    if subprocess.run(["scp", "-q", "-r", ld, dst], capture_output=True).returncode != 0:
        win_ps(f"New-Item -ItemType Directory -Force '{WIN_SESS}' | Out-Null")
        RH.scp(ld, dst, recursive=True)
    cl = " ".join([exe] + [f'"{x}"' if " " in x else x for x in args])
    ps = ("$ErrorActionPreference = 'Stop'\n"
          f"$rd = '{rd}'; $cl = '{cl.replace(chr(39), chr(39) * 2)}'\n"
          "$m = New-Object System.Threading.Mutex($false, 'Global\\ol2-rhost-admit')\n"
          f"$deadline = (Get-Date).AddSeconds({wait}); $wpid = 0\n"
          "while ($true) {\n"
          "  try { [void]$m.WaitOne() } catch [System.Threading.AbandonedMutexException] {}\n"
          "  $mine = @(Get-CimInstance Win32_Process -Filter \"Name='ol2-rhost.exe'\" | Where-Object { $_.CommandLine -like '*\\runs\\drive\\*' }).Count\n"
          "  $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory\n"
          f"  if ($mine -lt {WIN_MAX_SESSIONS} -and $free -gt 1500000) {{\n"
          "    $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cl; CurrentDirectory = $rd }\n"
          "    $wpid = $r.ProcessId\n"
          "    try { (Get-Process -Id $wpid).PriorityClass = 'BelowNormal' } catch {}\n"
          "    Start-Sleep -Milliseconds 200; $m.ReleaseMutex(); break }\n"
          "  $m.ReleaseMutex()\n"
          f"  if ((Get-Date) -gt $deadline) {{ \"NOADMIT $mine rd sessions (cap {WIN_MAX_SESSIONS}), $([int]($free / 1024)) MB free (floor 1465)\"; Remove-Item $rd -Recurse -Force -ErrorAction SilentlyContinue; exit 0 }}\n"
          "  Start-Sleep -Milliseconds 500 }\n"
          "$t0 = Get-Date\n"
          "while (!(Test-Path \"$rd\\live.port\")) {\n"
          "  if (!(Get-Process -Id $wpid -ErrorAction SilentlyContinue) -or ((Get-Date) - $t0).TotalSeconds -gt 300) {\n"
          "    \"NOSTART pid $wpid\"; if (Test-Path \"$rd\\rhost.log\") { Get-Content \"$rd\\rhost.log\" -Tail 8 }; exit 0 }\n"
          "  Start-Sleep -Milliseconds 50 }\n"
          "\"LIVE $wpid \" + (Get-Content \"$rd\\live.port\" -TotalCount 1)\n")
    rc, out = win_ps(ps, timeout=wait + 400)
    if rc and not out.strip():                            # (seen once on a fresh box: the first session's script failed silently)
        subprocess.run(["scp", "-q", "-r", ld, dst], capture_output=True)
        rc, out = win_ps(ps, timeout=wait + 400)
    m = re.search(r"^LIVE (\d+) (\d+) ", out, re.M)
    if not m:
        if "NOADMIT" in out:
            raise NotAdmitted(f"live session {tag} on {RH.WIN}: not admitted — " + out.split("NOADMIT", 1)[1].strip())
        raise SystemExit(f"the live session did not start on {RH.WIN} ({rd}, rc {rc})\n{out[-800:]}")
    return int(m.group(1)), int(m.group(2))


def win_forward(wpid, wport):
    """an ssh local forward to the session's port, alive exactly as long as the Windows process"""
    for attempt in range(4):
        lp = free_local_port()
        f = subprocess.Popen(["ssh", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes", "-o", "ServerAliveInterval=30",
                              "-o", "ServerAliveCountMax=4", "-L", f"127.0.0.1:{lp}:127.0.0.1:{wport}", RH.WIN,
                              f"Wait-Process -Id {wpid} -ErrorAction SilentlyContinue"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        t0 = time.time()
        while f.poll() is None and time.time() - t0 < 30:
            if local_listening(lp):
                return f.pid, lp
            time.sleep(0.05)
        try:
            os.killpg(f.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        f.wait()
        time.sleep(1 + 2 * attempt)
    raise SystemExit(f"no ssh forward to the live session (pid {wpid}, port {wport}) on {RH.WIN}")


def session_files(ld, movie, upto):
    """the session's run-dir files: the start saves, the movie prefix as trace.jsonl → rhost args"""
    for s in movie.saves:
        shutil.copy(s, os.path.join(ld, os.path.basename(s)))
    args = []
    for k, flag in (("step_ms", "--step-ms"), ("vbase", "--vbase")):
        if movie.extra.get(k) is not None:
            args += [flag, str(movie.extra[k])]
    if movie.extra.get("movies"):
        args += ["--movies"]
    if upto > 0:
        movie.truncated(upto).write(os.path.join(ld, "trace.jsonl"))
        args += ["--trace", "@RUN@\\trace.jsonl", "--live-until", str(upto)]
    return args


def spawn_live(tag, movie, upto=None, idle=1800, wait=300, host=None):
    """a live rhost on HOST: replays MOVIE to frame UPTO first → the session dict {host, pid, port, rundir[, fwd, wport]};
    `port` is always a local port (wine: the session's own; win / bunker: an ssh forward's)"""
    if host is None and RD_HOST == "auto":
        for h in ("win", "bunker"):
            if not host_up(h):
                continue
            try:
                return spawn_live(tag, movie, upto, idle, min(wait, ADMIT_WAIT[h]), h)
            except NotAdmitted as e:
                print(f"[{e} — trying the next host]")
        host = "wine"
    host = host or (RD_HOST if RD_HOST != "auto" else "wine")
    upto = movie.frame if upto is None else upto
    name = f"{tag}-{os.getpid()}-{int(time.time() * 1000) % 10 ** 9}"
    win = f"{WIN_SESS}\\{name}"
    if host in ("win", "bunker"):
        ld = os.path.join(RUNS, ".stage", name)
    else:
        exe = staged_exe("wine")
        ld = os.path.join(SESS, name)
    os.makedirs(ld)
    gd = f"{RH.WROOT}\\game"
    args = ["--exe", f"{gd}\\Lords2.exe", "--game", gd, "--run", win, "--live", "0", "--live-idle", str(idle), "--log-every", "0"]
    args += [a.replace("@RUN@", win) for a in session_files(ld, movie, upto)]
    if host == "win":
        try:
            wpid, wport = win_spawn(name, ld, args, wait, tag)
        finally:
            shutil.rmtree(ld, ignore_errors=True)
        s = {"host": "win", "pid": wpid, "wport": wport, "rundir": win}
        try:
            s["fwd"], s["port"] = win_forward(wpid, wport)
        except BaseException:
            kill_session(s)
            raise
        return s
    if host == "bunker":
        try:
            files = {f: open(os.path.join(ld, f), "rb").read() for f in os.listdir(ld)}
        finally:
            shutil.rmtree(ld, ignore_errors=True)
        exe = staged_exe("bunker")
        r = BK.spawn_session(name, files, exe, args, wait, BUNKER_MAX_SESSIONS, BUNKER_FLOOR_MB)
        if r[0] == "NOADMIT":
            raise NotAdmitted(f"live session {tag} on bunker: not admitted — {r[1]}")
        if r[0] != "LIVE":
            raise SystemExit(f"the live session did not start on bunker ({win})\n{r[1]}")
        s = {"host": "bunker", "pid": r[1], "wport": r[2], "rundir": win, "name": name}
        try:
            for attempt in range(4):
                lp = free_local_port()
                if BK.forward(lp, r[2]):
                    s["port"] = lp
                    break
                time.sleep(0.5 + attempt)
            else:
                raise SystemExit(f"no forward to the live session on bunker (pid {r[1]}, port {r[2]})")
        except BaseException:
            kill_session(s)
            raise
        return s
    t0 = time.time()
    while local_sessions() >= LOCAL_MAX or mem_available_gb() < LOCAL_FLOOR_GB:
        if time.time() - t0 > wait:
            shutil.rmtree(ld, ignore_errors=True)
            raise NotAdmitted(f"live session {tag} here: not admitted — {local_sessions()} sessions (cap {LOCAL_MAX}), "
                              f"{mem_available_gb():.1f} GB available (floor {LOCAL_FLOOR_GB})")
        time.sleep(1)
    p = subprocess.Popen(["wine", exe] + args, stdout=open(os.path.join(ld, "stdout.txt"), "w"), stderr=subprocess.STDOUT,
                         stdin=subprocess.DEVNULL, env=RH.wine_env(), start_new_session=True, cwd=ld)
    pf = os.path.join(ld, "live.port")
    while not os.path.exists(pf):
        if p.poll() is not None or time.time() - t0 > 300:
            tail = open(os.path.join(ld, "rhost.log"), errors="replace").read()[-600:] if os.path.exists(os.path.join(ld, "rhost.log")) else ""
            raise SystemExit(f"the live session did not start ({ld})\n{tail}")
        time.sleep(0.05)
    return {"host": "wine", "pid": p.pid, "port": int(open(pf).read().split()[0]), "rundir": ld}


def kill_session(s):
    """end OUR session (never by name): wine — its process group; win — the forward, then the process by PID after
    checking its command line; bunker — pool.py by PID + name; drop its run dir"""
    if not s:
        return
    if s.get("host") == "bunker":
        if s.get("port") and s.get("wport"):
            BK.forward(s["port"], s["wport"], cancel=True)
        if s.get("pid") and s.get("name"):
            BK.kill_session(s["pid"], s["name"])
        return
    if s.get("host") == "win":
        if s.get("fwd"):
            try:
                os.killpg(s["fwd"], signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
        rd = s.get("rundir", "")
        if not rd.startswith(WIN_SESS + "\\") or not s.get("pid"):
            return
        win_ps(f"$p = Get-CimInstance Win32_Process -Filter 'ProcessId={int(s['pid'])}'; "
               f"if ($p -and $p.Name -eq 'ol2-rhost.exe' -and $p.CommandLine -like '*{rd}*') {{ Stop-Process -Id {int(s['pid'])} -Force; "
               f"Wait-Process -Id {int(s['pid'])} -Timeout 10 -ErrorAction SilentlyContinue }}; "
               f"Remove-Item '{rd}' -Recurse -Force -ErrorAction SilentlyContinue; exit 0", timeout=120)
        return
    try:
        os.killpg(s["pid"], signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    rundir = s.get("rundir")
    if rundir and os.path.realpath(rundir).startswith(os.path.realpath(SESS) + os.sep):
        shutil.rmtree(rundir, ignore_errors=True)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


def sess_alive(s):
    if s and s.get("host") == "bunker":
        return bool(s.get("port")) and local_listening(s["port"]) and BK.session_alive(s["pid"], s["name"])
    return bool(s) and pid_alive(s["fwd"] if s.get("host") == "win" else s["pid"])


def sess_file(s, f, dst):
    """copy file F of the session's run dir to DST → True when it exists"""
    if s.get("host") == "bunker":
        data = BK.session_file(s.get("name", ""), f)
        if data is None:
            return False
        open(dst, "wb").write(data)
        return True
    if s.get("host") == "win":
        return subprocess.run(["scp", "-q", f"{RH.WIN}:{s['rundir'].replace(chr(92), '/')}/{f}", dst], capture_output=True).returncode == 0 \
            and os.path.exists(dst)
    src = os.path.join(s["rundir"], f)
    if not os.path.exists(src):
        return False
    shutil.copy(src, dst)
    return True


# ------------------------------------------------------------------ a run
class Run:
    def __init__(self, name, create=False, lock=True):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise SystemExit(f"bad run name {name!r}")
        self.name, self.dir = name, os.path.join(RUNS, name)
        if create:
            os.makedirs(self.dir, exist_ok=True)
        elif not os.path.exists(os.path.join(self.dir, "meta.json")):
            raise SystemExit(f"no run {name!r} (rd ls; rd new {name} --from ..)")
        self.lock = None
        if lock:
            self.lock = open(os.path.join(self.dir, ".lock"), "w")
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                self.lock.close()
                raise SystemExit(f"run {name} is busy: another rd command is working on it — wait for it")
        self.meta_p, self.sess_p, self.movie_p = (os.path.join(self.dir, f) for f in ("meta.json", "session.json", "movie.jsonl"))
        self.meta = json.load(open(self.meta_p)) if os.path.exists(self.meta_p) else {"checkpoints": {}, "poked": False}
        self.movie = Movie.load(self.movie_p) if os.path.exists(self.movie_p) else Movie()
        self.vars = Vars(self.dir)
        self.live, self.sess = None, None

    def save_meta(self):
        self.meta["frame"] = self.movie.frame
        json.dump(self.meta, open(self.meta_p + ".tmp", "w"), indent=1)
        os.replace(self.meta_p + ".tmp", self.meta_p)

    def save_sess(self):
        json.dump(self.sess, open(self.sess_p + ".tmp", "w"))
        os.replace(self.sess_p + ".tmp", self.sess_p)

    def sync_movie(self):
        r = self.live.cmd("movie")
        self.movie = Movie.from_server(r, self.movie.saves, self.movie.extra)
        self.movie.write(self.movie_p, comment=self.name, rel_to=self.dir)
        if "poked=1" in r:
            self.meta["poked"] = True
        self.save_meta()

    def attach(self, cached_only=False):
        if self.live:
            return self.live
        s = json.load(open(self.sess_p)) if os.path.exists(self.sess_p) else None
        if s and sess_alive(s) and s.get("bin") == bin_sha():
            try:
                lv = Live(s["port"])
                if int(Live.kv(lv.cmd("ping"))["frame"]) == self.movie.frame:
                    self.live, self.sess = lv, s
                    return lv
                lv.close()
            except (OSError, SessionEnded):
                pass
        if cached_only:
            return None
        if s:
            kill_session(s)
            if s.get("snaps") or s.get("mark") is not None:
                print("[the session was rebuilt (it ended, or rhost was rebuilt): its savestates are gone — checkpoints load by replay]")
        t0 = time.time()
        h = spawn_live(self.name, self.movie)
        try:
            self.live = Live(h["port"])
            k = Live.kv(self.live.cmd("ping"))
        except BaseException:
            kill_session(h)
            raise
        if int(k["frame"]) != self.movie.frame:
            raise SystemExit(f"the replayed session is at frame {k['frame']}, the movie at {self.movie.frame} ({h['rundir']}/rhost.log on {h['host']})")
        self.sess = dict(h, epoch=int(k["epoch"]), snaps={}, mark=None, bin=bin_sha())
        self.save_sess()
        if self.movie.frame > 5000:
            print(f"[session started on {h['host']}: replayed {self.movie.frame} frames in {time.time() - t0:.1f}s]")
        return self.live

    def stop(self):
        s = json.load(open(self.sess_p)) if os.path.exists(self.sess_p) else None
        if s:
            kill_session(s)
            os.remove(self.sess_p)

    def snap(self):
        return int(Live.kv(self.live.cmd("snap"))["id"])

    def sid_ok(self, u):
        return bool(self.sess and u.get("sid") is not None and u.get("pid") == self.sess["pid"] and u.get("epoch") == self.sess["epoch"])

    def push_undo(self, tail=False):
        u = {"sid": self.snap() if self.live else None, "pid": self.sess and self.sess["pid"], "epoch": self.sess and self.sess["epoch"],
             "frame": self.movie.frame, "cks": dict(self.meta.get("checkpoints", {})), "tail": None}
        if tail:
            os.makedirs(os.path.join(self.dir, "undo"), exist_ok=True)
            u["tail"] = f"undo/{int(time.time() * 1000)}-f{self.movie.frame}.jsonl"
            self.movie.write(os.path.join(self.dir, u["tail"]), comment=self.name + " (undo copy)", rel_to=self.dir)
        stack = self.meta.setdefault("undo", [])
        stack.append(u)
        while len(stack) > 8:
            old = stack.pop(0)
            if self.sid_ok(old) and old["sid"] not in ({v[0] for v in self.sess["snaps"].values()} | {self.sess.get("mark")}):
                self.live.cmd(f"free {old['sid']}", check=False)
            if old.get("tail") and os.path.exists(os.path.join(self.dir, old["tail"])):
                os.remove(os.path.join(self.dir, old["tail"]))
        self.save_meta()

    def drop_undo(self):
        stack = self.meta.get("undo", [])
        if not stack:
            return
        u = stack.pop()
        if self.live and self.sid_ok(u) and u["sid"] not in ({v[0] for v in self.sess["snaps"].values()} | {self.sess.get("mark")}):
            self.live.cmd(f"free {u['sid']}", check=False)
        if u.get("tail") and os.path.exists(os.path.join(self.dir, u["tail"])):
            os.remove(os.path.join(self.dir, u["tail"]))
        self.save_meta()

    def ev(self, *exprs):
        return eval_exprs(self.attach(), self.vars, exprs)

    def status_line(self):
        lv = self.attach()
        k = Live.kv(lv.cmd("ping"))
        names = [n for n, d in self.vars.defs.items() if d.get("status")]
        vals = self.ev(*names) if names else []
        s = f"frame {k['frame']}  " + "  ".join(f"{n}={fmt(v)}" for n, v in zip(names, vals))
        if self.meta.get("poked"):
            s += "  [POKED: memory was written by hand — this run's movie does not reproduce it]"
        return s

    def play(self, spec, until=None, avoid=None, watch=(), every=0, quiet=False, rows=40, undo=True):
        lv = self.attach()
        segs, total = items_to_segs(parse_spec(spec), lambda e: lv.prog(self.vars.compile(e)))
        if not segs:
            raise SystemExit("empty input spec")
        if undo:
            self.push_undo()
        cmd = f"run {segs}"
        if until:
            cmd += f" u={lv.prog(self.vars.program(until))}"
        if avoid:
            cmd += f" a={lv.prog(self.vars.program(avoid))}"
        ids, wsl = sampled_cols(lv, self.vars, watch)
        ev_names, esl = [], []
        for n in [n for n, d in self.vars.defs.items() if d.get("event")]:
            parts = self.vars.compile_parts(n)
            if len(ids) + len(parts) > MAX_SAMPLED:
                continue
            esl.append((len(ids), len(ids) + len(parts)))
            ids += [lv.prog(p) for p in parts]
            ev_names.append(n)
        nw = len(watch) and wsl[-1][1]
        cols = list(watch) + ev_names
        if cols:
            cmd += " s=" + ",".join(map(str, ids)) + f" e={every if (every and watch) else 1}"
        r = lv.cmd(cmd)
        k = Live.kv(r)
        self.sync_movie()
        self.save_sess()
        out = {"ran": int(k["ran"]), "stop": k["stop"], "frame": int(k["frame"]), "rows": [], "events": []}
        if cols and k.get("samples"):
            prev = None
            for f, vals in parse_samples(k["samples"]):
                if watch and (not every or (f - (out["frame"] - out["ran"])) % every == 0):
                    out["rows"].append((f, fold_row(vals, wsl)))
                evs = fold_row(vals, esl)
                if prev is not None:
                    for n, a, b in zip(ev_names, prev, evs):
                        if a != b:
                            out["events"].append((f, n, a, b))
                prev = evs
        if not quiet:
            print(f"ran {out['ran']} frames ({out['stop']})  " + self.status_line())
            by = {}
            for f, n, a, b in out["events"]:
                by.setdefault(n, []).append((f, a, b))
            busy = {n for n, v in by.items() if len(v) > 8}
            for n in sorted(busy):
                v = by[n]
                print(f"  {n}: {len(v)} changes f{v[0][0]}..f{v[-1][0]}, {fmt(v[0][1])} → {fmt(v[-1][2])}")
            sparse = [e for e in out["events"] if e[1] not in busy]
            for f, n, a, b in sparse[:60]:
                print(f"  f{f}: {n} {fmt(a)} → {fmt(b)}")
            if len(sparse) > 60:
                print(f"  … {len(sparse) - 60} more events")
            if watch:
                print("  frame  " + "  ".join(watch))
                if len(out["rows"]) > rows:
                    print(f"  … {len(out['rows']) - rows} earlier rows not shown (--rows N)")
                for f, vals in out["rows"][-rows:]:
                    print(f"  {f:6d} " + "  ".join(fmt(v) for v in vals))
        return out

    def load_frame(self, frame, sid=None, movie=None):
        if sid is not None and self.live:
            r = self.live.cmd(f"restore {sid}", check=False)
            if r.startswith("ok") and int(Live.kv(r)["frame"]) == frame:
                self.sync_movie()
                return "savestate"
        if self.live:
            self.live.close()
        self.stop()
        self.live = None
        self.movie = (movie or self.movie).truncated(frame)
        self.movie.write(self.movie_p, comment=self.name, rel_to=self.dir)
        self.save_meta()
        self.attach()
        return "replay"

    def close(self):
        if self.live:
            self.live.close()
            self.live = None
        if self.lock:
            self.lock.close()
            self.lock = None


# ------------------------------------------------------------------ starting points
def resolve_start(src):
    """fresh | SCENARIO[@F] | RUN[:CK|@F] | TRACE.jsonl[@F] → Movie"""
    if src == "fresh":
        return Movie()
    m = re.fullmatch(r"(.+?)(?:@(\d+)|:([A-Za-z0-9_.-]+))?", src)
    name, at, ck = m.group(1), m.group(2), m.group(3)
    if os.path.exists(os.path.join(RUNS, name, "meta.json")):
        o = Run(name, lock=False)
        mv = o.movie
        if ck:
            if ck not in o.meta.get("checkpoints", {}):
                raise SystemExit(f"run {name} has no checkpoint {ck!r} ({' '.join(o.meta.get('checkpoints', {}))})")
            mv = mv.truncated(o.meta["checkpoints"][ck]["frame"])
        o.close()
    else:
        p = name if name.endswith(".jsonl") else os.path.join(RH.SCEN, name, "trace.jsonl")
        if not os.path.exists(p):
            raise SystemExit(f"--from {src!r}: no run, scenario or trace by that name")
        mv = Movie.load(p)
    if at:
        mv = mv.truncated(int(at))
    return mv


# ------------------------------------------------------------------ commands
def cmd_new(a):
    if os.path.exists(os.path.join(RUNS, a.run, "meta.json")):
        if not a.force:
            raise SystemExit(f"run {a.run} exists (--force replaces it)")
        old = Run(a.run)
        old.stop()
        old.close()
        shutil.rmtree(os.path.join(RUNS, a.run))
    mv = resolve_start(a.src)
    r = Run(a.run, create=True)
    saves = []
    for s in mv.saves:                                    # the run keeps its own copy of the start saves
        os.makedirs(os.path.join(r.dir, "saves"), exist_ok=True)
        d = os.path.join(r.dir, "saves", os.path.basename(s))
        shutil.copy(s, d)
        saves.append(d)
    mv.saves = saves
    r.movie, r.meta = mv, {"checkpoints": {}, "poked": False, "start": a.src, "start_frame": mv.frame, "created": time.strftime("%Y-%m-%d %H:%M")}
    r.movie.write(r.movie_p, comment=a.run, rel_to=r.dir)
    r.save_meta()
    t0 = time.time()
    r.attach()
    print(f"run {a.run}: from {a.src} at frame {mv.frame} ({len(mv.events)} events; session on {r.sess['host']} in {time.time() - t0:.1f}s)")
    print(r.status_line())
    r.close()


def cmd_play(a):
    r = Run(a.run)
    r.play(a.spec, a.until, a.avoid, split_top(a.watch) if a.watch else (), a.every, rows=a.rows)
    r.close()


def cmd_status(a):
    r = Run(a.run)
    print(r.status_line())
    lv = r.attach()
    print("  " + lv.cmd("ping")[3:])
    r.close()


def cmd_eval(a):
    if a.at or a.at_frame is not None:
        r = Run(a.run)
        frame = r.meta["checkpoints"][a.at]["frame"] if a.at else a.at_frame
        V = r.vars
        mv = r.movie.truncated(frame)
        r.close()
        h = spawn_live("eval", mv, idle=120)
        try:
            lv = Live(h["port"])
            vals = eval_exprs(lv, V, a.exprs)
            lv.close()
        finally:
            kill_session(h)
        print(f"(at frame {frame}: a side replay)")
    else:
        r = Run(a.run)
        vals = r.ev(*a.exprs)
        r.close()
    for e, v in zip(a.exprs, vals):
        print(f"{e} = {repr(v) if a.raw else fmt(v)}" + (f"  (0x{int(v) & 0xffffffff:x})" if isinstance(v, int) and abs(v) > 9 else ""))


def cmd_movie(a):
    r = Run(a.run, lock=False)
    print(f"{r.movie_p}: {len(r.movie.events)} events, {r.movie.frame} frames, saves {[os.path.basename(s) for s in r.movie.saves]}")
    tail = r.movie.events[-a.n:] if a.n else []
    for f, m, w, l in tail:
        print(f"  f{f}: m={m:#x} w={w:#x} ({l & 0xffff},{l >> 16})")


def cmd_ram(a):
    r = Run(a.run)
    op = a.op
    if op == "list":
        for n, d in sorted(r.vars.defs.items()):
            what = d.get("expr") or f"{d.get('type', 's32')} @ {d.get('addr')}"
            print(f"  {n:22s} {what:44s} {'[status] ' if d.get('status') else ''}{'[event] ' if d.get('event') else ''}{d.get('note', '')}")
        for k, rec in r.vars.records.items():
            print(f"  record {k}(i).FIELD  base {rec['base']} stride {rec['stride']} slots {rec['slots'][0]}..{rec['slots'][1]}  {rec.get('note', '')}")
            print("     fields: " + " ".join(sorted(rec["fields"])))
        r.close()
        return
    if op == "define":
        name = a.args[0] if a.args else None
        if not name or not (a.addr or a.expr):
            raise SystemExit("rd ram R define NAME --addr HEX [--type s32] | --expr EXPR [--status] [--event] [--note ..]")
        d = {"expr": a.expr} if a.expr else {"addr": a.addr.lower().removeprefix("0x"), "type": a.type}
        if a.status:
            d["status"] = True
        if a.event:
            d["event"] = True
        if a.note:
            d["note"] = a.note
        r.vars.define(name, d, shared=a.shared)
        print(f"defined {name} = {d}; now {fmt(r.ev(name)[0])}")
        r.close()
        return
    lv = r.attach()
    if op in ("read", "dwords", "str"):
        va = int(a.args[0], 16)
        n = int(a.args[1], 0) if len(a.args) > 1 else (64 if op != "dwords" else 16)
        nb = n * 4 if op == "dwords" else n
        h = bytes.fromhex(lv.cmd(f"r {va:x} {nb}")[3:])
        if op == "str":
            print(h.split(b"\0")[0].decode("latin-1"))
        elif op == "dwords":
            for i in range(0, len(h), 16):
                ws = [int.from_bytes(h[j:j + 4], "little") for j in range(i, min(i + 16, len(h)), 4)]
                print(f"  {va + i:08x}: " + " ".join(f"{w:08x}" for w in ws) + "   " + " ".join(str(w - (1 << 32) if w >= 1 << 31 else w) for w in ws))
        else:
            for i in range(0, len(h), 16):
                print(f"  {va + i:08x}: {h[i:i + 16].hex(' ')}")
    elif op == "mark":
        sid = r.snap()
        r.sess["mark"] = sid
        r.save_sess()
        print(f"marked at frame {r.movie.frame} (savestate {sid}); act, then `rd ram {a.run} diff`")
    elif op == "diff":
        if r.sess.get("mark") is None:
            raise SystemExit("no mark in this session (rd ram R mark first)")
        lo, hi = a.lo or "0", a.hi or "ffffffff"
        k = Live.kv(lv.cmd(f"diff {r.sess['mark']} max={a.max} lo={lo} hi={hi}"))
        rows = [tuple(int(x, 16) for x in d.split(":")) for d in filter(None, k.get("d", "").split(","))]
        if a.minus:
            noise = set(json.load(open(os.path.join(r.dir, f"diff-{a.minus}.json"))))
            rows = [x for x in rows if x[0] not in noise]
        if a.save:
            json.dump([x[0] for x in rows], open(os.path.join(r.dir, f"diff-{a.save}.json"), "w"))
        print(f"{k.get('n')} dwords differ since the mark ({len(rows)} shown after filters)")
        for va, was, now in rows[:a.show]:
            s = lambda v: v - (1 << 32) if v >= 1 << 31 else v
            print(f"  {va:08x}: {was:08x} → {now:08x}   ({s(was)} → {s(now)})")
    elif op == "w":
        va, hx = int(a.args[0], 16), a.args[1]
        lv.cmd(f"w {va:x} {hx}")
        r.meta["poked"] = True
        r.save_meta()
        print("poked (the run is now POKED: it cannot be filed)")
    else:
        raise SystemExit("rd ram R list | read VA [N] | dwords VA [N] | str VA | define NAME .. | mark | diff | w VA HEX")
    r.close()


def cmd_ck(a):
    r = Run(a.run)
    cks = r.meta.setdefault("checkpoints", {})
    if a.op == "list":
        for n, c in sorted(cks.items(), key=lambda x: x[1]["frame"]):
            print(f"  {n:16s} frame {c['frame']:>7d}  {c.get('note', '')}")
        r.close()
        return
    if not a.name:
        raise SystemExit("rd ck R save|load NAME")
    lv = r.attach()
    if a.op == "save":
        sid = r.snap()
        cks[a.name] = {"frame": r.movie.frame, "note": " ".join(a.note)}
        r.sess["snaps"][a.name] = [sid, r.movie.frame]
        r.save_sess()
        r.save_meta()
        print(f"checkpoint {a.name} at frame {r.movie.frame}")
    else:
        if a.name not in cks:
            raise SystemExit(f"no checkpoint {a.name!r}")
        r.push_undo(tail=True)
        f = cks[a.name]["frame"]
        sv = r.sess["snaps"].get(a.name)
        how = r.load_frame(f, sv[0] if sv and sv[1] == f else None)
        for n in [n for n, c in cks.items() if c["frame"] > f]:
            del cks[n]
        r.save_meta()
        print(f"loaded {a.name} (frame {f}) by {how}; later checkpoints dropped")
    r.close()


def cmd_undo(a):
    r = Run(a.run)
    st = r.meta.get("undo", [])
    if not st:
        raise SystemExit("nothing to undo")
    u = st.pop()
    r.attach(cached_only=True)
    mv = Movie.load(os.path.join(r.dir, u["tail"])) if u.get("tail") else None
    how = r.load_frame(u["frame"], u["sid"] if r.sid_ok(u) else None, mv)
    r.meta["checkpoints"] = u["cks"]
    r.save_meta()
    print(f"undone: back at frame {u['frame']} (by {how})  " + r.status_line())
    r.close()


def cmd_rewind(a):
    r = Run(a.run)
    to = a.to if a.to is not None else r.movie.frame - a.frames
    if not 0 <= to <= r.movie.frame:
        raise SystemExit(f"--to {to}: outside 0..{r.movie.frame}")
    r.attach(cached_only=True)
    r.push_undo(tail=True)
    how = r.load_frame(to)
    r.meta["checkpoints"] = {n: c for n, c in r.meta.get("checkpoints", {}).items() if c["frame"] <= to}
    r.save_meta()
    print(f"rewound to frame {to} (by {how})  " + r.status_line())
    r.close()


# ---- what the screen shows: text rows, hotspots (rh_live.c recorders)
_IDA_NAMES = None


def ida_name(va):
    global _IDA_NAMES
    if _IDA_NAMES is None:
        _IDA_NAMES = {}
        p = os.path.join(RH.EPH, "decompiled-ida", "functions_ida.tsv")
        if os.path.exists(p):
            for l in open(p):
                f = l.split("\t")
                if len(f) > 2 and re.fullmatch(r"[0-9a-f]{8}", f[0]):
                    _IDA_NAMES[int(f[0], 16)] = f[2]
    return _IDA_NAMES.get(va, f"FUN_{va:08x}")


def rec_rows(lv, what):
    k = Live.kv(lv.cmd(what))
    rows = []
    for r in filter(None, k.get("rows", "").split(";")):
        p = r.split(":")
        rows.append((int(p[0]), [int(x) for x in p[1:5]], bytes.fromhex(p[5]) if len(p) > 5 else b""))
    return rows, k.get("stale") == "1"


def text_of(b):
    return "".join(chr(c) if 32 <= c < 127 or c >= 160 else ("|" if c in (10, 13) else "·") for c in b)


def hotspots(lv):
    """[(x0, y0, x1, y1, action_va, type, kind)] — the hotspot tables retail's input handlers tested in the last frame"""
    rows, stale = rec_rows(lv, "hot")
    out = []
    for kind, (ox, oy, tbl, n), _ in rows:
        if not (0 < n <= 64):
            continue
        h = bytes.fromhex(lv.cmd(f"r {tbl & 0xffffffff:x} {n * 24}")[3:])
        for i in range(n):
            x0, y0, x1, y1 = (int.from_bytes(h[i * 24 + 2 * j:i * 24 + 2 * j + 2], "little", signed=True) for j in range(4))
            act = int.from_bytes(h[i * 24 + 8:i * 24 + 12], "little")
            out.append((ox + x0, oy + y0, ox + x1, oy + y1, act, h[i * 24 + 15], kind, tbl & 0xffffffff, i))
    return out, stale


def cmd_text(a):
    r = Run(a.run)
    rows, stale = rec_rows(r.attach(), "text")
    print(f"text drawn since screen {fmt(r.ev('screen')[0])} opened ({len(rows)} rows)" + ("  [stale: the state was restored — play a frame]" if stale else ""))
    last = max((f for _, (_, _, _, f), _ in rows), default=0)
    for kind, (x, y, w, f), s in sorted(rows, key=lambda t: (t[1][1], t[1][0])):
        if a.all or f >= last - a.age:
            print(f"  ({x:3d},{y:3d}){' c' if kind == 1 else '  '} {text_of(s)}" + (f"   [drawn f{f}]" if f < last else ""))
    old = sum(1 for _, (_, _, _, f), _ in rows if f < last - a.age)
    if old and not a.all:
        print(f"  ({old} rows drawn more than {a.age} frames before the newest, f{last}, not shown: --all; they may be covered now)")
    r.close()


def cmd_hot(a):
    r = Run(a.run)
    hs, stale = hotspots(r.attach())
    print(f"{len(hs)} hotspots tested in the last frame" + ("  [stale: the state was restored — play a frame]" if stale else ""))
    for x0, y0, x1, y1, act, ty, kind, tbl, i in hs:
        print(f"  c{(x0 + x1) // 2},{(y0 + y1) // 2}   rect ({x0},{y0})-({x1},{y1})  {ida_name(act)}  type {ty}  table {tbl:x}[{i}]{' hover' if kind == 3 else ''}")
    r.close()


# ---- search
DEFAULT_WAIT = 25                                         # an in-game LMB fires 300 ms after release (≥ 25 frames between map clicks)


def default_actions(r):
    """the screen's clickable points (ui.json "screens": per screen id) + a wait; else a coarse grid"""
    pts = ui_points()
    scr = int(r.ev("screen")[0]) if "screen" in r.vars.defs else -1
    hs, _ = hotspots(r.attach())
    acts = sorted({f"c{(x0 + x1) // 2},{(y0 + y1) // 2} .*{DEFAULT_WAIT}" for x0, y0, x1, y1, *_ in hs if x1 > x0 and y1 > y0})
    acts += [f"c@{n} .*{DEFAULT_WAIT}" for n, p in pts.items() if scr in p.get("screens", [])]
    if not acts:
        acts = [f"c{x},{y} .*{DEFAULT_WAIT}" for x in range(40, 640, 80) for y in range(30, 480, 60)]
    return acts + [f".*{DEFAULT_WAIT}"]


def action_arg(specs, prog=None):
    acts = []
    for s in specs:
        seg, total = items_to_segs(parse_spec(s), prog, sep="+")
        if not total:
            raise SystemExit(f"action {s!r} needs at least one frame (a wait)")
        acts.append(seg)
    return "|".join(acts)


def search_progs(V, a):
    P = {"goal": V.program(a.goal, f"--goal {a.goal!r}"),
         "cell": [prog_check(t, f"--cell {a.cell!r}") for t in V.compile_tuple(a.cell)],
         "avoid": a.avoid and V.program(a.avoid, f"--avoid {a.avoid!r}"),
         "score": a.score and V.program(a.score, f"--score {a.score!r}")}
    return P


def explore_cmd(lv, V, a, seed, apply, P):
    g = lv.prog(P["goal"])
    cells = [lv.prog(t) for t in P["cell"]]
    cmd = f"explore g={g} c={','.join(map(str, cells))} acts={action_arg(a.actions, lambda e: lv.prog(V.compile(e)))}"
    if P["avoid"]:
        cmd += f" a={lv.prog(P['avoid'])}"
    if P["score"]:
        cmd += f" score={lv.prog(P['score'])} greed={a.greed}"
    if a.hold:
        cmd += f" hold={a.hold} hacts={action_arg(a.hold_actions or ['.'], lambda e: lv.prog(V.compile(e)))}"
        if a.hold_no_goal:
            cmd += " hgoal=0"
    cmd += f" t={int(a.time_limit * 1000)} refine={int(a.refine * 1000)} seed={seed} maxlen={a.max_len} sticky={a.sticky} apply={apply}"
    return cmd


def cmd_search(a):
    if a.apply_best:
        a.apply = True
    r = Run(a.run)
    if not a.cell:
        a.cell = r.vars.defs.get("_cell", {}).get("expr") or "(screen, state, sel_county, frame // 300)"
    P = search_progs(r.vars, a)
    lv = r.attach()
    if not a.actions:
        a.actions = default_actions(r)
        print(f"[actions: {len(a.actions)} — {' | '.join(a.actions[:6])}{' …' if len(a.actions) > 6 else ''}]")
    if r.ev(a.goal)[0]:
        print("the goal already holds here — nothing to search")
        r.close()
        return 0
    r.push_undo()
    st = {"applied": False}
    try:
        return search_body(r, lv, a, P, st)
    finally:
        if not st["applied"]:
            try:
                r.drop_undo()
            except (OSError, SessionEnded):
                pass
        r.save_sess()
        r.close()


def search_body(r, lv, a, P, st):
    t0 = time.time()
    results = []
    apply_mode = 2 if a.apply_best else 1 if a.apply else 0
    if a.jobs <= 1:
        res = lv.cmd(explore_cmd(lv, r.vars, a, a.seed, apply_mode, P), check=False)
        if not res.startswith("ok"):
            raise SystemExit("search: " + res)
        results.append((Live.kv(res), "main"))
        if Live.kv(res).get("applied") == "1":
            st["applied"] = True
    else:
        workers, errs, passed = [], [], {}
        main = r.sess.get("host", "wine")
        order = [main] + ([h for h in HOSTS if h != main] if RD_HOST == "auto" else [])

        def boot(i):
            last = None
            for h in order:
                if h != main and not host_up(h):
                    continue
                try:
                    workers.append((i, spawn_live(f"{r.name}-w{i}", r.movie, idle=600, wait=0, host=h)))
                    return
                except BaseException as e:      # noqa: B902
                    last = e
                    passed.setdefault(h, str(e).split(" — ", 1)[-1][:160])
            errs.append(str(last))
        ts = [threading.Thread(target=boot, args=(i,)) for i in range(1, a.jobs)]
        [t.start() for t in ts]

        def one(lvx, seed, apply, tag):
            try:
                res = lvx.cmd(explore_cmd(lvx, r.vars, a, seed, apply, P), check=False)
            except BaseException as e:          # noqa: B902
                res = f"err {type(e).__name__}: {e}"
            results.append((Live.kv(res) if res.startswith("ok") else {"found": "0", "err": res}, tag))
        try:
            [t.join() for t in ts]
            if errs:
                print(f"[{len(errs)} worker(s) did not start: {errs[0][:160]}]")
            workers.sort()
            by = {}
            for tag, h in [("main", main)] + [(f"w{i}", hs["host"]) for i, hs in workers]:
                by.setdefault(h, []).append(tag)
            print(f"[{len(workers) + 1} sessions at frame {r.movie.frame}, {time.time() - t0:.1f}s to start the extra ones; "
                  + ", ".join(f"{h}: {' '.join(t)}" for h, t in by.items()) + "]")
            ths = [threading.Thread(target=one, args=(lv, a.seed, 0, f"main@{main}"))]
            lvs = []
            for i, h in workers:
                lx = Live(h["port"]); lvs.append(lx)
                ths.append(threading.Thread(target=one, args=(lx, a.seed + i * 7919, 0, f"w{i}@{h['host']}")))
            [t.start() for t in ths]
            [t.join() for t in ths]
            [lx.close() for lx in lvs]
        finally:
            ks = [threading.Thread(target=kill_session, args=(h,)) for i, h in workers]
            [t.start() for t in ks]
            [t.join() for t in ks]
    errs = [(k["err"], tag) for k, tag in results if "err" in k]
    if errs and len(errs) == len(results):
        raise SystemExit(f"search: {errs[0][0]}  [{errs[0][1]}]")
    for e, tag in errs:
        print(f"[session {tag} failed: {e[:300]}]")
    results = [(k, tag) for k, tag in results if "err" not in k]
    found = [(int(k["frames"]), k, tag) for k, tag in results if k.get("found") == "1"]
    ticks = sum(int(k.get("ticks", 0)) for k, _ in results)
    cells = max(int(k.get("cells", 0)) for k, _ in results)
    wall = time.time() - t0
    if not found:
        k = results[0][0]
        print(f"found: false   {cells} cells, {ticks} frames searched in {wall:.1f}s ({ticks / max(wall, 0.01):.0f} frames/s)")
        print(f"  newest cells (frames@{a.cell}): " + "  ".join(k.get("newest", "").split(";")))
        print("  one cell only = your actions change nothing the cell sees; split the goal, add the missing action, or put the "
              "mechanic's state in --cell")
        if a.score:
            cands = [(float(k["bscore"]), -int(k["bframes"]), i) for i, (k, _) in enumerate(results) if k.get("bpath")]
            if not cands:
                print(f"  score: nothing scored above the start ({results[0][0].get('score0', '?')})")
            else:
                bs, _, i = max(cands)
                k = results[i][0]
                print(f"  best score {fmt(bs)} (start {fmt(float(k['score0']))}) after {k['bframes']} frames: {describe_path(k['bpath'])}")
                if a.save:
                    open(a.save, "w").write(segs_to_spec(k["bpath"]) + "\n")
                if a.apply_best:
                    if k.get("applied") != "1":
                        lv.cmd("run " + k["bpath"])
                    st["applied"] = True
                    r.sync_movie()
                    print(f"  applied the path to that cell (--apply-best): score now {fmt(r.ev(a.score)[0])}  " + r.status_line())
                    return 1
        r.sync_movie()
        return 1
    frames, k, tag = min(found, key=lambda x: x[0])
    print(f"found: true   {frames} frames  ({cells} cells, {ticks} frames in {wall:.1f}s = {ticks / max(wall, 0.01):.0f} frames/s"
          + (f", best of {len(found)}/{len(results)} sessions [{tag}]" if len(results) > 1 else "") + ")")
    if k.get("arrive"):
        print(f"  goal reached at +{k['arrive']}, then held {a.hold} frames (the path includes them)")
    print(f"  path: {describe_path(k['path'])}")
    if a.save:
        open(a.save, "w").write(segs_to_spec(k["path"]) + "\n")
    if a.apply:
        if a.jobs > 1 or k.get("applied") != "1":
            lv.cmd("run " + k["path"])
        st["applied"] = True
        holds, *av = r.ev(a.goal, *([a.avoid] if a.avoid else []))
        r.sync_movie()
        print(f"  applied: goal {'holds' if holds else 'DOES NOT HOLD'}" + (f", avoid {'TRUE' if av[0] else 'clear'}" if av else "")
              + "  " + r.status_line())
    else:
        r.sync_movie()
        print("  not applied (--apply plays it; --save FILE keeps it: rd play R \"$(cat FILE)\")")
    return 0


# ---- pictures: the session's own presented frame (software DirectDraw: exact, no replay)
def shot_now(r, tag):
    lv = r.attach()
    lv.cmd(f"shot {tag}")
    vd = os.path.join(r.dir, "view")
    os.makedirs(vd, exist_ok=True)
    dst = os.path.join(vd, f"{tag}.png")
    if not sess_file(r.sess, f"shot_{tag}.png", dst):
        raise SystemExit("the session wrote no picture")
    return dst


def cmd_look(a):
    r = Run(a.run)
    if not a.at:
        print(f"frame {r.movie.frame}: {shot_now(r, f'f{r.movie.frame:06d}')}")
        r.close()
        return
    frames = sorted(int(x) for x in a.at.split(","))
    got = shots_by_replay(r, frames)
    for f in frames:
        print(f"frame {f}: {got.get(f, '(none)')}")
    r.close()


def shots_by_replay(r, frames, tag="look"):
    """earlier frames: a side session replays the movie, a picture at each frame"""
    frames = [f for f in sorted(set(frames)) if 0 < f <= r.movie.frame]
    if not frames:
        raise SystemExit("no frames to shoot (1..movie length)")
    h = spawn_live(tag, r.movie, upto=frames[0], idle=120)
    got = {}
    vd = os.path.join(r.dir, "view")
    os.makedirs(vd, exist_ok=True)
    try:
        lv = Live(h["port"])
        rest = Movie([e for e in r.movie.events if e[0] > frames[0]], r.movie.frame)
        at = frames[0]
        for f in frames:
            if f > at:
                lv.cmd("run " + movie_segs(rest, at, f))
                at = f
            lv.cmd(f"shot f{f:06d}")
            dst = os.path.join(vd, f"f{f:06d}.png")
            if sess_file(h, f"shot_f{f:06d}.png", dst):
                got[f] = dst
        lv.close()
    finally:
        kill_session(h)
    return got


def movie_segs(mv, lo, hi):
    """the movie's events of frames lo+1..hi as a live `run` (events at frame F are posted at the boundary g_frame == F)"""
    segs, at = [], lo
    for f, m, w, l in mv.events:
        if f <= lo or f > hi:
            continue
        if f > at:
            segs.append(f"w{f - at}")
            at = f
        segs.append(f"e{m}:{w}:{l}")
    if hi > at:
        segs.append(f"w{hi - at}")
    return ",".join(segs)


def sheet(paths, out, cols=4, scale=0.5, labels=None):
    from PIL import Image, ImageDraw
    ims = [Image.open(p).convert("RGB") for p in paths]
    w, h = int(ims[0].width * scale), int(ims[0].height * scale)
    rows = -(-len(ims) // cols)
    sh = Image.new("RGB", (cols * w, rows * h), (20, 20, 20))
    d = ImageDraw.Draw(sh)
    for i, im in enumerate(ims):
        x, y = (i % cols) * w, (i // cols) * h
        sh.paste(im.resize((w, h), Image.LANCZOS), (x, y))
        if labels:
            d.rectangle([x, y, x + 8 + 7 * len(labels[i]), y + 14], fill=(0, 0, 0))
            d.text((x + 3, y + 1), labels[i], fill=(255, 255, 0))
    sh.save(out)
    return out


def cmd_strip(a):
    r = Run(a.run)
    lo, hi = a.frm, a.to if a.to is not None else r.movie.frame
    every = a.every or max(1, (hi - lo) // 11)
    frames = list(range(max(1, lo), hi + 1, every))[:48]
    got = shots_by_replay(r, frames, tag="strip")
    if got:
        out = os.path.join(r.dir, "view", f"strip_{lo}_{hi}.png")
        fs = sorted(got)
        sheet([got[f] for f in fs], out, cols=a.cols, labels=[f"f{f}" for f in fs])
        print(f"strip {lo}..{hi} every {every}: {out}")
    r.close()


# ---- earlier frames
def cmd_scan(a):
    r = Run(a.run, lock=False)
    src = Run(a.at_run, lock=False) if a.at_run else r
    mv, V = src.movie, r.vars
    lo, hi = a.frm, min(a.to if a.to is not None else mv.frame, mv.frame)
    exprs = split_top(a.watch) + ([a.where] if a.where else [])
    h = spawn_live("scan", mv, upto=lo, idle=120)
    try:
        lv = Live(h["port"])
        ids, sl = sampled_cols(lv, V, exprs)
        rows, at = [], lo
        while at < hi:
            b = min(hi, at + 5000)
            k = Live.kv(lv.cmd(f"run {movie_segs(mv, at, b)} s={','.join(map(str, ids))} e=1"))
            rows += [(f, fold_row(v, sl)) for f, v in parse_samples(k.get("samples"))]
            at = b
        lv.close()
    finally:
        kill_session(h)
    nw, prev, shown = len(split_top(a.watch)), None, 0
    print("  frame  " + "  ".join(split_top(a.watch)))
    for f, vals in rows:
        if a.where and not vals[-1]:
            continue
        w = vals[:nw]
        if a.changes and w == prev:
            continue
        if a.every and (f - lo) % a.every:
            continue
        prev = w
        print(f"  {f:6d} " + "  ".join(repr(v) if a.raw else fmt(v) for v in w))
        shown += 1
    print(f"({shown} rows of {len(rows)} frames {lo + 1}..{hi})")


# ---- verify / check / lint / file
def cmd_verify(a):
    """the movie replayed in a FRESH plain rhost (no coroutine, no savestates) reaches the live session's state"""
    r = Run(a.run)
    lv = r.attach()
    live = Live.kv(lv.cmd("hash"))
    tr = os.path.join(r.dir, "verify.trace.jsonl")
    r.movie.write(tr, comment=r.name, rel_to=r.dir)
    host = a.target or (r.sess or {}).get("host", "wine")
    b = argparse.Namespace(jobs=1, target=host, extra=None, frames=None, fb_hash=False)
    _, bd, res, out = RH.run_batch(b, [("verify", tr, json.load(open(tr)) if False else Movie_meta(tr))], "rd-verify")
    v = res.get("verify") or {}
    rng = live.get("rng", "").replace("/", "/")
    same = v.get("frame") == live.get("frame") and v.get("rng") == rng and v.get("state_sem") == live.get("sem")
    print(f"fresh replay ({host}): frame {v.get('frame')} rng {v.get('rng')} sem {v.get('state_sem')}")
    print(f"live session ({r.sess.get('host')}): frame {live.get('frame')} rng {rng} sem {live.get('sem')}")
    if r.meta.get("poked"):
        print("note: this run was POKED: the movie cannot reproduce hand-written memory")
    print("MATCH" if same else "MISMATCH")
    r.close()
    return 0 if same else 1


def Movie_meta(tr):
    return RH.trace_meta(tr)


def load_task(path):
    txt = open(path).read()
    m = re.match(r"---\n(.*?)\n---\n?(.*)", txt, re.S)
    if not m:
        raise SystemExit(f"{path}: needs a --- header ---")
    t = {"name": os.path.splitext(os.path.basename(path))[0], "start": "fresh", "steps": [], "forbid": [], "text": m.group(2).strip()}
    in_list = None
    for line in m.group(1).splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        s = re.match(r"\s+-\s*(.+)$", line)
        if in_list and s:
            item, hit = s.group(1), None
            for c in re.finditer(r":\s+", item):
                try:
                    ast.parse(item[c.end():].strip(), mode="eval")
                except SyntaxError:
                    continue
                hit = (item[:c.start()].strip(), item[c.end():].split("#")[0].strip())
                break
            if not hit:
                raise SystemExit(f"{path}: cannot read step {line.strip()!r} (\"- label: EXPRESSION\")")
            t[in_list].append(hit)
            continue
        k = re.match(r"(\w+):\s*(.*?)\s*(?:#.*)?$", line)
        if not k:
            raise SystemExit(f"{path}: cannot read header line {line!r}")
        in_list = k.group(1) if k.group(1) in ("steps", "forbid") else None
        if not in_list:
            t[k.group(1)] = k.group(2)
    if not t["steps"]:
        raise SystemExit(f"{path}: no steps")
    return t


def walk_movie(movie, V, preds):
    """a fresh session fed the movie frame by frame → for each predicate the frames where it holds"""
    h = spawn_live("check", movie, upto=0, idle=300)
    try:
        lv = Live(h["port"])
        ids = [lv.prog(V.program(e)) for _, e in preds]
        truth = [[] for _ in preds]
        at = 0
        while at < movie.frame:
            b = min(movie.frame, at + 4000)
            k = Live.kv(lv.cmd(f"run {movie_segs(movie, at, b)} s={','.join(map(str, ids))} e=1"))
            for f, vals in parse_samples(k.get("samples")):
                for i, x in enumerate(vals):
                    if x:
                        truth[i].append(f)
            at = b
        end = Live.kv(lv.cmd("hash"))
        lv.close()
        return truth, end
    finally:
        kill_session(h)


def cmd_check(a):
    r = Run(a.run)
    t = load_task(a.task)
    after = r.meta.get("start_frame", 0)
    preds = t["steps"] + t["forbid"]
    truth, end = walk_movie(r.movie, r.vars, preds)
    ns, at, ok, first_step = len(t["steps"]), after, True, None
    print(f"task {t['name']}: {ns} steps, {len(t['forbid'])} forbids; movie {r.movie.frame} frames; steps judged after frame {after}")
    for i, (name, expr) in enumerate(t["steps"]):
        hit = next((f for f in truth[i] if f > at), None)
        if hit is None:
            ok = False
            print(f"  step {i + 1} NOT REACHED: {name}: {expr}")
            break
        first_step = hit if first_step is None else first_step
        print(f"  step {i + 1} at frame {hit}: {name}")
        at = hit
    for j, (name, expr) in enumerate(t["forbid"]):
        bad = [f for f in truth[ns + j] if first_step is not None and f >= first_step]
        if bad:
            ok = False
            print(f"  FORBIDDEN at frame {bad[0]} ({len(bad)} frames): {name}: {expr}")
    if end.get("frame") != str(r.movie.frame):
        print(f"  (the replay ended at frame {end.get('frame')}, the movie at {r.movie.frame})")
        ok = False
    print("PASS" if ok else "FAIL")
    r.close()
    return 0 if ok else 1


def cmd_lint(a):
    t = load_task(a.task)
    for name in a.on:
        r = Run(name, lock=False)
        truth, _ = walk_movie(r.movie, r.vars, t["steps"] + t["forbid"])
        after = r.meta.get("start_frame", 0)
        print(f"{name} ({r.movie.frame} frames; its start prefix ends at {after}):")
        for (label, expr), fs in zip(t["steps"] + t["forbid"], truth):
            if not fs:
                print(f"  never      {label}: {expr}")
            else:
                pre = sum(1 for f in fs if f <= after)
                print(f"  {len(fs):6d} f  first {fs[0]:>7d} last {fs[-1]:>7d}" + (f"  [{pre} inside the start prefix]" if pre else "") + f"  {label}")


def cmd_file(a):
    r = Run(a.run)
    if r.meta.get("poked"):
        raise SystemExit("this run was POKED: its movie does not reproduce its state — not filed")
    d = os.path.join(RH.SCEN, a.name)
    if os.path.exists(os.path.join(d, "trace.jsonl")) and not a.force:
        raise SystemExit(f"tests/scenarios/{a.name} exists (--force)")
    os.makedirs(d, exist_ok=True)
    mv = Movie(r.movie.events, r.movie.frame, [], r.movie.extra)
    if r.movie.saves:
        sd = os.path.join(d, "_saves")
        os.makedirs(sd, exist_ok=True)
        for s in r.movie.saves:
            shutil.copy(s, os.path.join(sd, os.path.basename(s)))
            mv.saves.append(os.path.join(sd, os.path.basename(s)))
    p = os.path.join(d, "trace.jsonl")
    mv.write(p, comment=f"{a.name}, authored with rd from {r.meta.get('start', '?')}" + (f"; {' '.join(a.note)}" if a.note else ""), rel_to=d)
    print(f"filed {os.path.relpath(p, ROOT)} ({r.movie.frame} frames). Next: rhost.py corpus --bless --only '^{a.name}$' "
          f"(--target wine too); rhost.py verify --only '^{a.name}$'")
    r.close()


CORPUS_COV = os.path.join(os.path.dirname(RUNS), "corpus_coverage.tsv")


def corpus_coverage(refresh=False):
    """{va} the corpus reaches (rhost.py coverage, cached; --refresh-corpus re-runs it)"""
    if refresh or not os.path.exists(CORPUS_COV):
        out = subprocess.run([os.path.join(ROOT, "tools", "dev"), "python3", os.path.join(RH.HERE, "rhost.py"), "coverage", "--target", "wine", "-j", "8"],
                             capture_output=True, text=True).stdout
        m = re.search(r"→ (\S+coverage_summary\.tsv)", out)
        if not m:
            raise SystemExit("rhost.py coverage failed:\n" + out[-800:])
        shutil.copy(m.group(1), CORPUS_COV)
    return {int(l.split("\t")[0], 16) for l in open(CORPUS_COV).readlines()[1:]}


def cmd_cov(a):
    """the retail functions this run's movie reaches that no corpus scenario does (a replay with --coverage)"""
    r = Run(a.run, lock=False)
    corpus = corpus_coverage(a.refresh_corpus)
    fn, _ = RH.function_table()
    cov_in = os.path.join(r.dir, "coverage_in.txt")
    open(cov_in, "w").write("".join(f"{va:x}\n" for va in sorted(fn)))
    tr = os.path.join(r.dir, "cov.trace.jsonl")
    r.movie.write(tr, comment=r.name, rel_to=r.dir)
    b = argparse.Namespace(jobs=1, target=a.target or "wine", extra=None, coverage_file=cov_in)
    _, bd, res, out = RH.run_batch(b, [("cov", tr, RH.trace_meta(tr))], "rd-cov", fetch=("rhost.log", "coverage.txt"))
    p = os.path.join(bd, "cov", "coverage.txt")
    if not os.path.exists(p):
        raise SystemExit(f"no coverage from the replay ({bd})")
    hit = {int(l.split()[0], 16): int(l.split()[1]) for l in open(p) if l.strip()}
    new = sorted((v for v in hit if v not in corpus and v in fn), key=lambda v: hit[v])
    nb = sum(fn[v][1] for v in new)
    tot = sum(sz for _, sz in fn.values())
    print(f"{r.name}: {len(hit)} retail functions reached; {len(new)} NEW to the corpus ({nb} bytes, +{100.0 * nb / tot:.2f}% of retail code)")
    for v in new[:a.show]:
        print(f"  {v:08x} {fn[v][0]:40s} {fn[v][1]:6d} B  first at frame {hit[v]}")
    if len(new) > a.show:
        print(f"  … {len(new) - a.show} more (--show N)")
    json.dump({"new": [f"{v:08x}" for v in new], "bytes": nb, "reached": len(hit)}, open(os.path.join(r.dir, "cov.json"), "w"))
    shutil.rmtree(bd, ignore_errors=True)


def cmd_stop(a):
    r = Run(a.run)
    r.stop()
    print(f"stopped the live session of {a.run} (the run and its movie stay; the next command replays)")
    r.close()


def cmd_ls(a):
    if not os.path.isdir(RUNS):
        return
    for n in sorted(os.listdir(RUNS)):
        mp = os.path.join(RUNS, n, "meta.json")
        if not os.path.exists(mp):
            continue
        m = json.load(open(mp))
        sp = os.path.join(RUNS, n, "session.json")
        s = json.load(open(sp)) if os.path.exists(sp) else None
        live = sess_alive(s)
        print(f"{n:28s} frame {m.get('frame', 0):>7d}  from {m.get('start', '?'):28s} {(s.get('host', 'wine') if live else '-'):6s} "
              f"{' '.join(sorted(m.get('checkpoints', {})))}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("new"); p.add_argument("run"); p.add_argument("--from", dest="src", required=True); p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_new)
    p = sp.add_parser("play"); p.add_argument("run"); p.add_argument("spec"); p.add_argument("--until"); p.add_argument("--avoid")
    p.add_argument("--watch"); p.add_argument("--every", type=int, default=0); p.add_argument("--rows", type=int, default=40); p.set_defaults(fn=cmd_play)
    p = sp.add_parser("status"); p.add_argument("run"); p.set_defaults(fn=cmd_status)
    p = sp.add_parser("text"); p.add_argument("run"); p.add_argument("--all", action="store_true"); p.add_argument("--age", type=int, default=120)
    p.set_defaults(fn=cmd_text)
    p = sp.add_parser("hot"); p.add_argument("run"); p.set_defaults(fn=cmd_hot)
    p = sp.add_parser("movie"); p.add_argument("run"); p.add_argument("-n", type=int, default=20); p.set_defaults(fn=cmd_movie)
    p = sp.add_parser("eval"); p.add_argument("run"); p.add_argument("exprs", nargs="+"); p.add_argument("--at"); p.add_argument("--at-frame", type=int)
    p.add_argument("--raw", action="store_true"); p.set_defaults(fn=cmd_eval)
    p = sp.add_parser("scan"); p.add_argument("run"); p.add_argument("--from", dest="frm", type=int, required=True); p.add_argument("--to", type=int)
    p.add_argument("--watch", required=True); p.add_argument("--every", type=int, default=0); p.add_argument("--changes", action="store_true")
    p.add_argument("--where"); p.add_argument("--at-run"); p.add_argument("--raw", action="store_true"); p.set_defaults(fn=cmd_scan)
    p = sp.add_parser("ram"); p.add_argument("run"); p.add_argument("op"); p.add_argument("args", nargs="*")
    p.add_argument("--addr"); p.add_argument("--type", default="s32"); p.add_argument("--expr"); p.add_argument("--status", action="store_true")
    p.add_argument("--event", action="store_true"); p.add_argument("--note"); p.add_argument("--shared", action="store_true")
    p.add_argument("--max", type=int, default=4000); p.add_argument("--show", type=int, default=60); p.add_argument("--lo"); p.add_argument("--hi")
    p.add_argument("--minus"); p.add_argument("--save"); p.set_defaults(fn=cmd_ram)
    p = sp.add_parser("ck"); p.add_argument("run"); p.add_argument("op", choices=("save", "load", "list")); p.add_argument("name", nargs="?"); p.add_argument("note", nargs="*"); p.set_defaults(fn=cmd_ck)
    p = sp.add_parser("undo"); p.add_argument("run"); p.set_defaults(fn=cmd_undo)
    p = sp.add_parser("rewind"); p.add_argument("run"); p.add_argument("--to", type=int); p.add_argument("--frames", type=int, default=0); p.set_defaults(fn=cmd_rewind)
    p = sp.add_parser("search"); p.add_argument("run"); p.add_argument("--goal", required=True); p.add_argument("--avoid"); p.add_argument("--cell")
    p.add_argument("--actions", nargs="+"); p.add_argument("--time-limit", type=float, default=60); p.add_argument("--refine", type=float, default=5)
    p.add_argument("--apply", action="store_true"); p.add_argument("--save"); p.add_argument("--seed", type=int, default=1); p.add_argument("--max-len", type=int, default=4)
    p.add_argument("--sticky", type=int, default=50); p.add_argument("-j", "--jobs", type=int, default=1)
    p.add_argument("--score"); p.add_argument("--greed", type=float, default=3.0); p.add_argument("--apply-best", action="store_true")
    p.add_argument("--hold", type=int, default=0); p.add_argument("--hold-actions", nargs="+"); p.add_argument("--hold-no-goal", action="store_true")
    p.set_defaults(fn=cmd_search)
    p = sp.add_parser("look"); p.add_argument("run"); p.add_argument("--at"); p.set_defaults(fn=cmd_look)
    p = sp.add_parser("strip"); p.add_argument("run"); p.add_argument("--from", dest="frm", type=int, required=True); p.add_argument("--to", type=int)
    p.add_argument("--every", type=int); p.add_argument("--cols", type=int, default=4); p.set_defaults(fn=cmd_strip)
    p = sp.add_parser("verify"); p.add_argument("run"); p.add_argument("--target", choices=("win", "wine", "bunker")); p.set_defaults(fn=cmd_verify)
    p = sp.add_parser("check"); p.add_argument("run"); p.add_argument("task"); p.set_defaults(fn=cmd_check)
    p = sp.add_parser("lint"); p.add_argument("task"); p.add_argument("--on", nargs="+", required=True); p.set_defaults(fn=cmd_lint)
    p = sp.add_parser("file"); p.add_argument("run"); p.add_argument("name"); p.add_argument("note", nargs="*"); p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_file)
    p = sp.add_parser("cov"); p.add_argument("run"); p.add_argument("--show", type=int, default=40); p.add_argument("--refresh-corpus", action="store_true")
    p.add_argument("--target", choices=("win", "wine", "bunker")); p.set_defaults(fn=cmd_cov)
    p = sp.add_parser("stop"); p.add_argument("run"); p.set_defaults(fn=cmd_stop)
    sp.add_parser("ls").set_defaults(fn=cmd_ls)
    argv, i = [], 1
    while i < len(sys.argv):
        t = sys.argv[i]
        if t in ("--goal", "--avoid", "--until", "--score", "--cell", "--watch", "--expr", "--where") and i + 1 < len(sys.argv) \
                and sys.argv[i + 1].startswith("-") and not sys.argv[i + 1].startswith("--"):
            argv.append(f"{t}={sys.argv[i + 1]}"); i += 2
        else:
            argv.append(t); i += 1
    a = ap.parse_args(argv)
    try:
        sys.exit(a.fn(a) or 0)
    except SessionEnded:
        print("the live session ended during this command: the game exited (Quit) or crashed. The run is unchanged (its movie "
              "ends before this command); the next rd command replays it.", file=sys.stderr)
        sys.exit(3)


if __name__ == "__main__":
    main()
