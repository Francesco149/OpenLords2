#!/usr/bin/env python3
"""tools/gen_data_defs.py — port definitions for catalog bytes no aliased definition covers (data self-check worklist).

Input: `tools/data_selfcheck.py --nodef-json PATH` (runs [va, len, catalog row]). For every run whose catalog row
matches --rows, emits one definition initialized from the exe (retail's bytes; pointer slots are raw retail VAs, as in
rl_link_globals.c: the standalone places data at retail's VAs and thunks every code entry) and its rhost alias.
  - a run that is a whole catalog row takes the row's name (`_ctype` → g_crt_ctype); a partial run g_rl_<va>
  - text (mostly printable / NUL) → char[] from a string literal; 4-aligned → uint32_t[] in hex; else uint8_t[]
  - .rdata → const
Writes --out-c (storage; like every *_globals.c NOT linked into rhost), --out-h (externs) and --out-alias (the
BFA rows, included by an rhost hooks file). Pure data: rhost behaviour does not change.

Usage:  python3 tools/gen_data_defs.py --nodef NODEF.json --rows REGEX --stem rl_crt [--exe PATH]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data_ledger as DL  # noqa: E402

ROOT = DL.ROOT


def is_text(b: bytes) -> bool:
    if len(b) < 8:
        return False
    pr = sum(1 for c in b if 32 <= c < 127 or c in (0, 9, 10, 13))
    return pr * 100 >= len(b) * 95 and sum(1 for c in b if 32 < c < 127) * 3 >= len(b)


def c_string(b: bytes) -> str:
    """b as concatenated C literals, a line per string (its NUL padding kept on it); exact: a sized char[] holds every
    byte, the literal's own terminator the last one when it is a NUL."""
    if b.endswith(b'\0'):
        b = b[:-1]
    out, cur, after_nul = [], '', False
    for c in b:
        ch = chr(c)
        if c == 0:
            cur += '\\0'
            after_nul = True
            continue
        if after_nul:            # a new string: a new literal (also keeps a digit off the \0 escape)
            out.append(cur)
            cur, after_nul = '', False
        if ch in '"\\':
            cur += '\\' + ch
        elif ch == '\n':
            cur += '\\n'
        elif ch == '\r':
            cur += '\\r'
        elif ch == '\t':
            cur += '\\t'
        elif 32 <= c < 127:
            cur += ch
        else:
            cur += '\\%03o' % c
    out.append(cur)
    return '\n    '.join('"%s"' % s for s in out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=DL.DEF_EXE if os.path.exists(DL.DEF_EXE) else DL.ALT_EXE)
    ap.add_argument('--nodef', required=True)
    ap.add_argument('--rows', required=True, help='regex on the catalog row name')
    ap.add_argument('--stem', required=True, help='e.g. rl_crt → src/rl_crt_data_globals.{c,h} (rhost builds leave *_globals.c out), '
                    'tools/rhost/rh_rl_crt_data_globals_alias.h')
    a = ap.parse_args()

    img = DL.Image(a.exe)
    cat = {cva: (csize, cname, ctype) for (cva, csize, cname, ctype) in DL.load_catalog()}
    types = {cname: ctype for (_v, _s, cname, ctype) in DL.load_catalog()}
    rx = re.compile(a.rows)
    runs = [r for r in json.load(open(a.nodef)) if rx.search(r[2])]
    stem = a.stem + '_data_globals'
    macro = 'RH_' + stem.upper() + '_ALIASES'
    taken = set()                          # names src/ or rhost already use (an old port's own table of that name)
    for d in (os.path.join(ROOT, 'src'), os.path.join(ROOT, 'tools', 'rhost')):
        for fn in os.listdir(d):
            if fn.endswith(('.c', '.h')) and stem not in fn:
                taken |= set(re.findall(r'\b[A-Za-z_]\w*\b', open(os.path.join(d, fn), errors='replace').read()))
    defs, externs, aliases = [], [], []
    for va, n, row in runs:
        whole = va in cat and cat[va][0] == n and cat[va][1] == row
        name = row if whole else 'g_rl_%06x' % va
        if name == '_ctype':
            name = 'g_crt_ctype'
        if name in taken:
            name = 'g_rl_%06x' % va
        sec = img.section_of(va)
        const = 'const ' if sec == '.rdata' else ''
        b = bytes(img.b(va, n))
        b = b + bytes(n - len(b))          # .bss tail
        if not whole and not any(b):
            continue                       # alignment padding inside a row: the image's zero fill
        if not whole:                      # a partial run: drop the padding at its ends (dword-rounded when aligned)
            lo = next(i for i in range(n) if b[i])
            hi = max(i for i in range(n) if b[i]) + 1
            if (va + lo) % 4 == 0:
                hi = min(n, lo + (hi - lo + 3) // 4 * 4)
            va, n, b = va + lo, hi - lo, b[lo:hi]
            name = 'g_rl_%06x' % va
        note = '/* %s 0x%06x..0x%06x%s */' % (sec, va, va + n, '' if name == row else ': ' + row)
        if types.get(row, '').startswith('char') and is_text(b):
            decl = '%schar %s[%d]' % (const, name, n)
            init = c_string(b)
            defs.append('%s\n%s =\n    %s;' % (note, decl, init))
        elif va % 4 == 0 and n % 4 == 0:
            decl = '%suint32_t %s[%d]' % (const, name, n // 4)
            ws = ['0x%08x' % int.from_bytes(b[i:i + 4], 'little') for i in range(0, n, 4)]
            lines = [', '.join(ws[i:i + 8]) for i in range(0, len(ws), 8)]
            defs.append('%s\n%s = {\n    %s\n};' % (note, decl, ',\n    '.join(lines)))
        elif va % 2 == 0 and n % 2 == 0:
            decl = '%suint16_t %s[%d]' % (const, name, n // 2)
            ws = ['0x%04x' % int.from_bytes(b[i:i + 2], 'little') for i in range(0, n, 2)]
            lines = [', '.join(ws[i:i + 12]) for i in range(0, len(ws), 12)]
            defs.append('%s\n%s = {\n    %s\n};' % (note, decl, ',\n    '.join(lines)))
        else:
            decl = '%suint8_t %s[%d]' % (const, name, n)
            bs = ['0x%02x' % c for c in b]
            lines = [', '.join(bs[i:i + 16]) for i in range(0, len(bs), 16)]
            defs.append('%s\n%s = {\n    %s\n};' % (note, decl, ',\n    '.join(lines)))
        externs.append('extern %s;%s' % (decl, ' ' * max(1, 60 - len(decl))) + note)
        aliases.append('    BFA(%s,0x%06x) \\' % (name, va))

    hdr = 'src/%s.h' % stem
    with open(os.path.join(ROOT, 'src', stem + '.c'), 'w') as f:
        f.write('/* %s.c — GENERATED (tools/gen_data_defs.py --rows %r): retail\'s data the port had no definition for,\n'
                ' * from the exe (catalog docs/data/catalog.tsv; pointer slots = raw retail VAs). Storage for a build without\n'
                ' * rhost — NOT linked into rhost (aliases: tools/rhost/rh_%s_alias.h). Checked by tools/data_selfcheck.py. */\n'
                '#include "%s.h"\n\n' % (stem, a.rows, stem, stem))
        f.write('\n\n'.join(defs) + '\n')
    with open(os.path.join(ROOT, hdr), 'w') as f:
        f.write('/* %s.h — GENERATED (tools/gen_data_defs.py): see %s.c. */\n#pragma once\n#include <stdint.h>\n\n' % (stem, stem))
        f.write('\n'.join(externs) + '\n')
    with open(os.path.join(ROOT, 'tools', 'rhost', 'rh_%s_alias.h' % stem), 'w') as f:
        f.write('/* rh_%s_alias.h — GENERATED (tools/gen_data_defs.py): every %s.h symbol at its retail VA. */\n'
                '#pragma once\n#define BFA(n, v) ".globl _" #n "\\n\\t.set _" #n ", " #v "\\n\\t"\n#define %s \\\n'
                % (stem, hdr, macro))
        f.write('\n'.join(aliases) + '\n\n')
    print('%d definitions' % len(defs))


if __name__ == '__main__':
    main()
