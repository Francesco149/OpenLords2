#!/usr/bin/env python3
"""tools/gen_mapblit.py — transliterate retail's unrolled strategic-map blitters from the IDA asm into C.

The terrain pass (src/engine_map_terrain.c) draws through ~24 fully unrolled blitters: the iso-tile copies
(0x452820 / 45337b / 4538f6 and their view-mode 1 / 2 twins) and the overhang strips (0x453e71 / 455f04 / 456fd8 /
4580a9 / 459258 / … ), each thousands of `mov al,[esi]; cmp al,0; jz; mov [edi+N],al; inc esi` lines with a row
loop. The earlier table extractors (extract_blit_table.py / extract_drawb_*.py) read Ghidra's decompile and modelled
the strip loop by hand (wrongly: the "dead" branch draws the top rows). This tool translates the asm itself,
instruction by instruction, into a C function — exact by construction:

  registers   eax/ebx/ecx/edx (uint32_t, al/ax sub-registers), esi/edi (uintptr_t), a push/pop stack
  memory      [esi+N] / [edi+N] byte / word / dword loads and stores (memcpy: unaligned)
  flags       every `cmp` / `test` is paired with the conditional jump that follows it (signed jl/jle/jge/jg)
  globals     Buf / xRight / dword_591524 / dword_591528 / Offset / dword_5BB478 / dword_5AEB6C → the port's
              retail-layout symbols (gfx_globals.h, engine_mapview_globals.h)
  prologue    push ebp … pusha (skipped); `popa` = return

Input: /mnt/ephemeral/openlords2/decompiled-ida/all_ida.asm (IDA 9.4 export, never redistributed).
Output: src/gen/mapblit_<va>.h (gitignored, like every derived table) with
    static void mapblit_<va>(const uint8_t *blob, int32_t a1, int32_t a2)
(a1 / a2 = the retail [ebp+arg_4] / [ebp+arg_8]; unused ones are ignored).

Usage: tools/dev python3 tools/gen_mapblit.py [VA ...]   (default: every blitter the terrain pass calls)
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASM = Path("/mnt/ephemeral/openlords2/decompiled-ida/all_ida.asm")
OUTDIR = ROOT / "src/gen"

# every blitter drawer A / B and the empty-cell drawers dispatch (view modes 0 / 1 / 2)
DEFAULT = ["452820", "45337b", "4538f6", "460501", "46079d", "4608de", "463b42", "463c06", "463c62",
           "453e71", "455f04", "456fd8", "4580a9", "459258",
           "460a1c", "4611ef", "461623", "461a54", "461efb",
           "463cbe", "463e91", "463fa5", "4640b6", "464209"]

GLOBALS = {
    "Buf": "(uint32_t)(uintptr_t)g_gfx_canvas_pixels",   # never used as a value except into edi (below)
    "xRight": "(uint32_t)g_gfx_canvas_width",
    "dword_591524": "(uint32_t)g_gfx_cursor_x",
    "dword_591528": "(uint32_t)g_gfx_cursor_y",
    "Offset": "(uint32_t)g_gfx_glyph_offset",
    "dword_5BB478": "(uint32_t)g_gfx_glyph_h",
    "dword_5AEB6C": "(uint32_t)g_mv_overhang_off",
}
ARGS = {"[ebp+arg_0]": "(uint32_t)0", "[ebp+arg_4]": "(uint32_t)a1", "[ebp+arg_8]": "(uint32_t)a2"}
R32 = {"eax", "ebx", "ecx", "edx"}
PTR = {"esi", "edi"}
JCC = {"jz": "==", "je": "==", "jnz": "!=", "jne": "!=", "jl": "<", "jle": "<=", "jg": ">", "jge": ">="}


def num(s: str) -> int:
    s = s.strip()
    neg = s.startswith("-")
    s = s.lstrip("+-")
    v = int(s[:-1], 16) if s.lower().endswith("h") else int(s, 0 if s.startswith("0x") else 10)
    return -v if neg else v


def func_lines(lines: list[str], va: str) -> list[tuple[int, str, str]]:
    pat = f"@ 0x{int(va, 16):08x} "
    out, on = [], False
    for ln in lines:
        if ln.startswith("; ===== "):
            if on:
                break
            on = pat in ln
            continue
        if on and ln.strip():
            m = re.match(r"^([0-9a-f]{8})\s+(\S+)\s*(.*)$", ln)
            if not m:
                raise SystemExit(f"{va}: unparsed line {ln!r}")
            ops = m.group(3).split(";")[0].strip()
            out.append((int(m.group(1), 16), m.group(2), ops))
    if not out:
        raise SystemExit(f"{va}: not in {ASM}")
    return out


def mem(op: str) -> tuple[str, int] | None:
    """[esi+N] / [edi] / [edi-N] → (reg, disp)"""
    m = re.fullmatch(r"\[(esi|edi)(?:([+-])([0-9A-Fa-f]+h?))?\]", op)
    if not m:
        return None
    d = num(m.group(3)) if m.group(3) else 0
    return m.group(1), (-d if m.group(2) == "-" else d)


def src_val(op: str) -> str:
    if op in R32 or op in PTR:
        return op if op in R32 else f"(uint32_t){op}"
    if op in GLOBALS:
        return GLOBALS[op]
    if op in ARGS:
        return ARGS[op]
    return f"(uint32_t){num(op)}u" if num(op) >= 0 else f"(uint32_t)({num(op)})"


def translate(va: str, ins: list[tuple[int, str, str]]) -> str:
    # strip the prologue (push ebp; mov ebp, esp; push ebx; push esi; push edi; pusha)
    pro = [("push", "ebp"), ("mov", "ebp, esp"), ("push", "ebx"), ("push", "esi"), ("push", "edi"), ("pusha", "")]
    if [(m, o) for _, m, o in ins[:6]] != pro:
        raise SystemExit(f"{va}: unexpected prologue {ins[:6]}")
    body = ins[6:]
    targets = set()
    for _, m, o in body:
        if m in JCC or m == "jmp":
            mm = re.fullmatch(r"loc_([0-9A-F]+)", o)
            if not mm:
                raise SystemExit(f"{va}: jump operand {o!r}")
            targets.add(int(mm.group(1), 16))
    out = []
    pend = None   # (lhs, rhs) of the last cmp / test
    done = False
    for i, (a, m, o) in enumerate(body):
        if done:   # the epilogue after popa: pop edi / pop esi / pop ebx / leave / retn
            if a in targets:
                raise SystemExit(f"{va}: jump into the epilogue at {a:06x}")
            continue
        if a in targets:
            out.append(f"L_{a:06x}:;")
        ops = [x.strip() for x in o.split(",")] if o else []
        if m == "popa":
            out.append("return;")
            done = True
            continue
        if m == "cmp":
            l, r = ops
            if l in ("al",):
                pend = ("(int8_t)(eax & 0xffu)", f"(int8_t){num(r)}")
            elif l in R32 or l in ARGS:
                pend = (f"(int32_t){src_val(l)}", f"(int32_t){src_val(r)}")
            else:
                raise SystemExit(f"{va}: cmp {o}")
            continue
        if m in JCC:
            if pend is None:
                raise SystemExit(f"{va}: {m} without cmp at {a:06x}")
            lbl = int(o[4:], 16)
            out.append(f"if ({pend[0]} {JCC[m]} {pend[1]}) goto L_{lbl:06x};")
            continue
        pend = None if m not in ("mov",) else pend   # mov does not touch the flags; anything else ends the pairing
        if m == "jmp":
            out.append(f"goto L_{int(o[4:], 16):06x};")
        elif m == "mov":
            d, s = ops
            md, ms = mem(d), mem(s)
            if md:
                reg, disp = md
                w = {"al": 1, "ax": 2, "eax": 4}[s]
                out.append(f"st{w}({reg} + ({disp}), eax);")
            elif ms:
                reg, disp = ms
                if d == "al":
                    out.append(f"eax = (eax & ~0xffu) | ld1({reg} + ({disp}));")
                elif d == "ax":
                    out.append(f"eax = (eax & ~0xffffu) | ld2({reg} + ({disp}));")
                elif d == "eax":
                    out.append(f"eax = ld4({reg} + ({disp}));")
                else:
                    raise SystemExit(f"{va}: mov {o}")
            elif d == "edi" and s == "Buf":
                out.append("edi = (uintptr_t)g_gfx_canvas_pixels;")
            elif d == "esi" and s == "[ebp+arg_0]":
                out.append("esi = (uintptr_t)blob;")
            elif d in R32:
                out.append(f"{d} = {src_val(s)};")
            else:
                raise SystemExit(f"{va}: mov {o}")
        elif m in ("add", "sub"):
            d, s = ops
            sign = "+" if m == "add" else "-"
            if d in PTR:
                # a 32-bit value added to a pointer: sign-extend (the 32-bit wrap of retail's add)
                out.append(f"{d} = {d} {sign} (uintptr_t)(intptr_t)(int32_t){src_val(s)};")
            elif d in R32:
                out.append(f"{d} = {d} {sign} {src_val(s)};")
            else:
                raise SystemExit(f"{va}: {m} {o}")
        elif m == "inc":
            (d,) = ops
            if d in PTR:
                out.append(f"{d} += 1;")
            elif d in R32:
                out.append(f"{d} += 1u;")
            else:
                raise SystemExit(f"{va}: inc {o}")
        elif m == "mul":
            if ops != ["edx"]:
                raise SystemExit(f"{va}: mul {o}")
            out.append("{ uint64_t p_ = (uint64_t)eax * edx; eax = (uint32_t)p_; edx = (uint32_t)(p_ >> 32); }")
        elif m == "push":
            (r,) = ops
            out.append(f"stk[sp++] = (uintptr_t){r};")
        elif m == "pop":
            (r,) = ops
            out.append(f"{r} = ({'uintptr_t' if r in PTR else 'uint32_t'})stk[--sp];")
        else:
            raise SystemExit(f"{va}: unhandled {m} {o} at {a:06x}")
    if not done:
        raise SystemExit(f"{va}: no popa")
    # edi += eax after the mul: eax is the 32-bit product — the sign-extension above makes the 64-bit host wrap
    # like retail's 32-bit add
    body_c = "\n    ".join(out)
    return f"""/* AUTO-GENERATED by tools/gen_mapblit.py from the IDA asm of 0x{va} — do not edit. */
static void mapblit_{va}(const uint8_t *blob, int32_t a1, int32_t a2)
{{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    uintptr_t esi = 0, edi = 0, stk[16];
    int sp = 0;
    (void)a1; (void)a2; (void)ebx; (void)ecx; (void)edx; (void)stk; (void)sp;
    {body_c}
}}
"""


HELPERS = """#ifndef OPENLORDS2_MAPBLIT_HELPERS
#define OPENLORDS2_MAPBLIT_HELPERS
#include <stdint.h>
#include <string.h>
static inline uint32_t ld1(uintptr_t p) { return *(const uint8_t *)p; }
static inline uint32_t ld2(uintptr_t p) { uint16_t v; memcpy(&v, (const void *)p, 2); return v; }
static inline uint32_t ld4(uintptr_t p) { uint32_t v; memcpy(&v, (const void *)p, 4); return v; }
static inline void st1(uintptr_t p, uint32_t v) { *(uint8_t *)p = (uint8_t)v; }
static inline void st2(uintptr_t p, uint32_t v) { uint16_t w = (uint16_t)v; memcpy((void *)p, &w, 2); }
static inline void st4(uintptr_t p, uint32_t v) { memcpy((void *)p, &v, 4); }
#endif
"""


def main() -> int:
    vas = sys.argv[1:] or DEFAULT
    lines = ASM.read_text(errors="replace").splitlines()
    OUTDIR.mkdir(parents=True, exist_ok=True)
    (OUTDIR / "mapblit_helpers.h").write_text("/* AUTO-GENERATED by tools/gen_mapblit.py — do not edit. */\n" + HELPERS)
    for va in vas:
        va = va.lower().removeprefix("0x").lstrip("0") or "0"
        c = translate(va, func_lines(lines, va))
        (OUTDIR / f"mapblit_{va}.h").write_text(c)
        print(f"  -> src/gen/mapblit_{va}.h ({c.count(chr(10))} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
