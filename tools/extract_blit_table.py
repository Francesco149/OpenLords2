#!/usr/bin/env python3
"""
tools/extract_blit_table.py — byte-exact table extractor for the unrolled
isometric-tile blitters of the strategic-map terrain layer
(FUN_00452820 / FUN_004538f6 / FUN_0045337b, all UNPORTED).

These retail functions are ~thousands of bytes of fully-unrolled
`*(dest + OFF) = *(src + IDX)` copies — a fixed diamond/parallelogram tile
shape with no RLE or palette logic.  Rather than hand-transcribe hundreds of
statements, parse the Ghidra decompile (docs/decompiled/by-address/<va>.c)
into a `(dest_off, src_off, size)` copy table and emit a C array that a
compact replay loop runs — byte-exact by construction.

The decompile's pointer arithmetic — the dest var (`iVar2`, a byte pointer)
is identical across all three blitters, but the SOURCE var differs and the
extractor auto-detects it from the `param_3 + DAT_0058fe04` init line:

  FUN_00452820:  src `puVar1 = (undefined2 *)(param_3 + DAT_0058fe04)`
                 → element-indexed, scale 2 (index N = byte 2*N)
  FUN_004538f6/  src `iVar1   =                param_3 + DAT_0058fe04`
  FUN_0045337b   → plain byte pointer, scale 1 (iVar1 + N = byte N)

  dest  `iVar2 + 0xD`  = dest byte 0xD (byte pointer, scale 1, always)
  copy size from the LHS cast: undefined1→1, undefined2→2, undefined4→4
  src forms (SRC = detected var, S = its scale):
              *SRC                       → byte 0
              SRC[N]                     → byte S*N
              *(undefined[124] *)(SRC + N)→ byte S*N

Phase blocks (FUN_00452820): the body is
    if (param_4 != 2) { <BLOCK A — top half>; if (param_4 == 1) return; }
    <BLOCK B — bottom half>
so phase 1 = A only, phase 2 = B only, else = A then B.  The extractor
splits the copy list at those control-flow lines and emits one table per
block.

Usage:
    nix develop -c python3 tools/extract_blit_table.py 452820 [4538f6 ...]
Writes src/gen/blit_table_<va>.h (one or two C arrays + block boundaries).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BYADDR = ROOT / "docs/decompiled/by-address"
OUTDIR = ROOT / "src/gen"

def _num(s: str) -> int:
    return int(s, 16) if s.lower().startswith("0x") else int(s)


def _detect_vars(lines: list[str]) -> tuple[str, str, int]:
    """Return (dest_var, src_var, src_scale) from the function prologue.

    dest is whatever is assigned `DAT_004ea1a8 + ...` (the framebuffer cursor);
    src is whatever is assigned `param_3 + DAT_0058fe04` — with an
    `(undefined2 *)` cast it is element-indexed (scale 2), else a byte pointer
    (scale 1)."""
    dest_var = src_var = None
    src_scale = 1
    for ln in lines:
        m = re.match(r"\s*(\w+)\s*=\s*DAT_004ea1a8 \+", ln)
        if m:
            dest_var = m.group(1)
        m = re.match(r"\s*(\w+)\s*=\s*(\([^)]*\*\)\s*)?\(?param_3 \+ DAT_0058fe04",
                     ln)
        if m:
            src_var = m.group(1)
            cast = m.group(2) or ""
            src_scale = 2 if "undefined2" in cast else 1
    if not dest_var or not src_var:
        raise SystemExit(f"could not detect dest/src vars (dest={dest_var}, "
                         f"src={src_var})")
    return dest_var, src_var, src_scale


def extract(va: str) -> dict:
    path = BYADDR / f"{va}.c"
    text = path.read_text(errors="replace")
    lines = text.splitlines()

    dest_var, src_var, scale = _detect_vars(lines)
    v = re.escape(src_var)
    copy_re = re.compile(
        r"\*\(undefined([124]) \*\)\(" + re.escape(dest_var) +
        r" \+ (0x[0-9a-fA-F]+|\d+)\)\s*=\s*(.+?);")
    bare_re = re.compile(r"\*" + v + r"$")
    idx_re = re.compile(v + r"\[(0x[0-9a-fA-F]+|\d+)\]$")
    cast_re = re.compile(r"\*\(undefined[124] \*\)\(" + v +
                         r" \+ (0x[0-9a-fA-F]+|\d+)\)$")

    def src_byte(rhs: str) -> int | None:
        rhs = rhs.strip()
        if bare_re.match(rhs):
            return 0
        m = idx_re.match(rhs) or cast_re.match(rhs)
        if m:
            return scale * _num(m.group(1))
        return None

    # Find the phase control-flow boundaries (FUN_00452820 shape). If absent
    # (single-block blitter), everything is BLOCK A.
    split_at = None      # line index of `if (param_4 == 1) goto`
    for i, ln in enumerate(lines):
        if "param_4 == 1" in ln and "goto" in ln:
            split_at = i
            break

    block_a, block_b = [], []
    for i, ln in enumerate(lines):
        m = copy_re.search(ln)
        if not m:
            continue
        size = int(m.group(1))
        dest = _num(m.group(2))
        src = src_byte(m.group(3))
        if src is None:
            raise SystemExit(f"{va}.c:{i+1}: unparsed RHS: {m.group(3)!r}")
        entry = (dest, src, size)
        if split_at is not None and i > split_at:
            block_b.append(entry)
        else:
            block_a.append(entry)
    return {"va": va, "a": block_a, "b": block_b, "has_split": split_at is not None}


def emit_header(info: dict) -> str:
    va = info["va"]
    a, b = info["a"], info["b"]
    out = [f"/* AUTO-GENERATED by tools/extract_blit_table.py from",
           f" * docs/decompiled/by-address/{va}.c — do not edit by hand.",
           " *",
           " * Byte-exact copy table for the unrolled iso-tile blitter",
           f" * FUN_00{va}.  Each entry = {{dest byte offset, src byte offset,",
           " * size}}; the replay loop does dest[off] = src[src_off] for `size`",
           " * bytes.  dest = canvas + cursor_x + cursor_y*pitch (pitch 640;",
           " * rows step 638 = a 2px-left iso shear).  src = blob + src_off.",
           " *",
           " * Phase: A = top half, B = bottom half; phase 1 = A only,",
           " * phase 2 = B only, else = A then B.",
           " */",
           f"#ifndef OPENLORDS2_BLIT_TABLE_{va.upper()}_H",
           f"#define OPENLORDS2_BLIT_TABLE_{va.upper()}_H",
           "",
           "#include <stdint.h>",
           "",
           "#ifndef OPENLORDS2_BLIT_COPY_T_DEFINED",
           "#define OPENLORDS2_BLIT_COPY_T_DEFINED",
           "typedef struct { uint16_t dest_off; uint16_t src_off; uint8_t size; }"
           " blit_copy_t;",
           "#endif",
           ""]

    def arr(name, tbl):
        s = [f"static const blit_copy_t {name}[] = {{"]
        for (d, so, sz) in tbl:
            s.append(f"    {{0x{d:x}, 0x{so:x}, {sz}}},")
        s.append("};")
        s.append(f"enum {{ {name}_n = {len(tbl)} }};")
        return "\n".join(s)

    out.append(arr(f"k_blit_{va}_a", a))
    out.append("")
    out.append(arr(f"k_blit_{va}_b", b))
    out.append("")
    out.append("#endif")
    return "\n".join(out) + "\n"


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit("usage: extract_blit_table.py <va> [<va> ...]")
    OUTDIR.mkdir(parents=True, exist_ok=True)
    for va in sys.argv[1:]:
        info = extract(va)
        a, b = info["a"], info["b"]
        # Sanity report.
        def span(tbl):
            if not tbl:
                return "(empty)"
            ds = [d for d, _, _ in tbl]
            ss = [s for _, s, _ in tbl]
            return (f"{len(tbl)} copies, dest 0x{min(ds):x}..0x{max(ds):x}, "
                    f"src 0x{min(ss):x}..0x{max(ss):x}")
        print(f"{va}: block A {span(a)}; block B {span(b)}")
        (OUTDIR / f"blit_table_{va}.h").write_text(emit_header(info))
        print(f"  -> src/gen/blit_table_{va}.h")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
