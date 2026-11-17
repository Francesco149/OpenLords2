#!/usr/bin/env python3
"""tools/data_selfcheck.py — the port's own copies of retail's data, checked against Lords2.exe.

Under rhost and in the standalone every port global aliases retail's VA (`.set _g_x, 0xVA`): the bytes come from the
user's exe and the port's own definitions (src/*_globals.c and friends, with their initializers) are never linked. On
the way to "a port that loads nothing" those definitions must reproduce the data image. This tool says how far they do.

Method: compile every src/*.c with -fdata-sections (one COFF section per variable = its exact size), read each defined
symbol that rhost aliases to a retail VA (tools/rhost: BFA(name, VA) / A(name, VA)), and compare its bytes with the
exe's at that VA:
  match        byte equal
  diff         byte differs
  ptr-ok       a relocated slot whose target resolves to the retail value (an aliased symbol + addend, or a port
               function whose VA is the retail code pointer), and retail's slot is a .reloc HIGHLOW entry too
  ptr-diff     a relocated slot that resolves to another value
  ptr-port     a slot pointing at port-private data (a literal, a static): retail's slot points at data too and the
               C strings at both ends are equal (ptr-port-diff when not)
  ptr-code     a slot pointing at a port function with no known retail VA, retail's slot is a code pointer
  missing      the port defines the symbol without an initializer (zero) where retail's byte is not zero
  import       an .idata byte (import table / IAT: the loader's, not the port's to reproduce)
  own-copy     (catalog bytes no aliased definition covers) found verbatim, >= 8 bytes at a time, in a port data
               section under another name (an old port's k_ table, a string literal) — content only, not proof of use
Bytes past the exe's raw data (.bss) are expected zero. Overlapping definitions (two modules defining one VA range) are
reported. Catalog rows (docs/data/catalog.tsv) are then split into: covered by a port copy (and how), or not.

Output (DERIVED): docs/data-selfcheck.md (counts, per-row table; no byte contents). `--list` prints every diff.
Run inside tools/dev (needs i686-w64-mingw32-gcc).

Usage:  python3 tools/data_selfcheck.py [--exe PATH] [--list] [--jobs N]
"""
from __future__ import annotations

import argparse
import bisect
import concurrent.futures as cf
import glob
import os
import re
import struct
import subprocess
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import data_ledger as DL  # noqa: E402

ROOT = DL.ROOT
SRC = os.path.join(ROOT, 'src')
OBJ = os.path.join(ROOT, 'build', 'selfcheck-obj')
OUT_MD = os.path.join(ROOT, 'docs', 'data-selfcheck.md')
CFLAGS = ['-std=gnu11', '-O2', '-fdata-sections', '-ffunction-sections', '-w',
          '-DUNICODE=0', '-D_UNICODE=0', '-DWINVER=0x0500', '-D_WIN32_WINNT=0x0500']
ALIAS_RE = re.compile(r'\b(?:BFA|A)\(\s*([A-Za-z_]\w*)\s*,\s*(0x[0-9a-fA-F]+)\s*\)')
SET_RE = re.compile(r'\.set\s+_([A-Za-z_]\w*)\s*,\s*(0x[0-9a-fA-F]+)')
FN_VA_RE = re.compile(r'^(?:rl|bf)_([0-9a-f]{6})$')


# ---------------------------------------------------------------------------------------------------------- inputs
def load_aliases():
    out = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'tools', 'rhost', '*.[ch]'))):
        txt = open(p, encoding='utf-8', errors='replace').read()
        for rx in (ALIAS_RE, SET_RE):
            for n, v in rx.findall(txt):
                out.setdefault(n, int(v, 16))
    return out


def load_fn_vas():
    """port function name -> retail VA (rl_<va> / bf_<va> names + src/rl_va_map.c's rows)."""
    out = {}
    p = os.path.join(SRC, 'rl_va_map.c')
    if os.path.exists(p):
        for m in re.finditer(r'static uint32_t m_([0-9a-f]{6})\(const uint32_t \*a\) \{ \(void\)a; (.*)', open(p).read()):
            for fn in re.findall(r'\b([A-Za-z_]\w*)\(', m.group(2)):
                if fn not in ('uint32_t', 'I', 'void', 'int32_t'):
                    out[fn] = int(m.group(1), 16)
                    break
    return out


def compile_all(jobs):
    os.makedirs(OBJ, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(SRC, '*.c')))

    def one(c):
        o = os.path.join(OBJ, os.path.basename(c)[:-2] + '.o')
        if os.path.exists(o) and os.path.getmtime(o) >= os.path.getmtime(c) and os.path.getmtime(o) > _hdr_mtime:
            return o, None
        r = subprocess.run(['i686-w64-mingw32-gcc', *CFLAGS, '-c', c, '-o', o],
                           capture_output=True, text=True)
        return o, (r.stderr if r.returncode else None)

    global _hdr_mtime
    _hdr_mtime = max(os.path.getmtime(h) for h in glob.glob(os.path.join(SRC, '*.h')) + glob.glob(
        os.path.join(SRC, 'gen', '*.h')) or [0])
    objs, errs = [], []
    with cf.ThreadPoolExecutor(jobs) as ex:
        for o, e in ex.map(one, srcs):
            (errs.append((o, e)) if e else objs.append(o))
    for o, e in errs:
        print('compile failed: %s\n%s' % (o, e[-2000:]), file=sys.stderr)
    if errs:
        sys.exit(1)
    return objs


def probe_sizes(by_obj, jobs):
    """{name: sizeof(name)}: per object, a TU that #includes its .c and stores sizeof of each defined name (COFF
    section sizes are padded to the section's alignment, so they can't give an object's size)."""
    def one(item):
        o, names = item
        base = os.path.basename(o)[:-2]
        c = os.path.join(OBJ, base + '.sz.c')
        so = os.path.join(OBJ, base + '.sz.o')
        body = '#include "%s"\n' % os.path.join(SRC, base + '.c') + ''.join(
            'const unsigned __attribute__((used)) __sz_%s = sizeof(%s);\n' % (n, n) for n in sorted(names))
        if not (os.path.exists(c) and open(c).read() == body and os.path.exists(so)
                and os.path.getmtime(so) >= os.path.getmtime(o)):
            open(c, 'w').write(body)
            r = subprocess.run(['i686-w64-mingw32-gcc', *CFLAGS, '-c', c, '-o', so], capture_output=True, text=True)
            if r.returncode:
                sys.exit('size probe failed: %s\n%s' % (c, r.stderr[-2000:]))
        co = Coff(so)
        out = {}
        for (name, sec, val, _cl) in co.symbols.values():
            if name.startswith('___sz_') and sec > 0:
                out[name[6:]] = struct.unpack_from('<I', co.sections[sec - 1]['data'], val)[0]
        return out

    sizes = {}
    with cf.ThreadPoolExecutor(jobs) as ex:
        for d in ex.map(one, sorted(by_obj.items())):
            sizes.update(d)
    return sizes


# ---------------------------------------------------------------------------------------------------------- COFF
class Coff:
    """i386 COFF object: sections (name, size, data, relocs), symbols."""

    def __init__(self, path):
        d = open(path, 'rb').read()
        self.path = path
        mach, nsec, _, symptr, nsym, optsz, _ = struct.unpack_from('<HHIIIHH', d, 0)
        assert mach == 0x14c, path
        strtab = symptr + 18 * nsym
        self.strtab = d[strtab:]

        def sname(raw):
            if raw[:4] == b'\0\0\0\0':
                off, = struct.unpack_from('<I', raw, 4)
                return self._str(off)
            return raw.rstrip(b'\0').decode()

        self.sections = []   # dict(name, size, data, relocs[(off, symidx, type)], flags)
        for i in range(nsec):
            o = 20 + optsz + 40 * i
            nm = d[o:o + 8]
            if nm[:1] == b'/':
                name = self._str(int(nm[1:].rstrip(b'\0')))
            else:
                name = nm.rstrip(b'\0').decode()
            _, _, size, rawptr, relptr, _, nrel, _, flags = struct.unpack_from('<IIIIIIHHI', d, o + 8)
            data = d[rawptr:rawptr + size] if rawptr else bytes(size)
            rel = [struct.unpack_from('<IIH', d, relptr + 10 * k) for k in range(nrel)]
            self.sections.append(dict(name=name, size=size, data=data, relocs=rel, flags=flags))
        self.symbols = {}    # index -> (name, secnum, value, storage)
        i = 0
        while i < nsym:
            o = symptr + 18 * i
            raw = d[o:o + 8]
            name = sname(raw)
            value, secnum, _typ, sclass, naux = struct.unpack_from('<IhHBB', d, o + 8)
            self.symbols[i] = (name, secnum, value, sclass)
            i += 1 + naux

    def _str(self, off):
        e = self.strtab.find(b'\0', off)
        return self.strtab[off:e].decode()


# ---------------------------------------------------------------------------------------------------------- check
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', default=DL.DEF_EXE if os.path.exists(DL.DEF_EXE) else DL.ALT_EXE)
    ap.add_argument('--list', action='store_true', help='print every diff / ptr-diff span')
    ap.add_argument('--jobs', type=int, default=8)
    ap.add_argument('--nodef-json', metavar='PATH',
                    help='write the catalog bytes no aliased definition covers, as [va, len, row name] runs (JSON)')
    a = ap.parse_args()

    img = DL.Image(a.exe)
    relocs = set(img.relocs())
    text = img.sec['.text']
    text_lo, text_hi = text[1], text[1] + text[2]
    data_lo = min(s[1] for s in img.sections if s[0] != '.text')

    def is_code(v):
        return text_lo <= v < text_hi

    def retail_byte(va):
        o = img.off(va)
        if o is not None:
            return img.raw[o]
        sec = img.section_of(va)
        return 0 if sec else None   # .bss = zero; outside the image = None

    aliases = load_aliases()
    fn_va = load_fn_vas()
    objs = compile_all(a.jobs)

    # pass 1: every definition (aliased or not) with its object / section, for pointer targets
    defs = {}          # name -> (Coff, section index, value)
    dupdefs = []
    coffs = [Coff(o) for o in objs]
    for co in coffs:
        for idx, (name, sec, val, scl) in co.symbols.items():
            if sec > 0 and scl == 2:   # external, defined
                n = name[1:] if name.startswith('_') else name
                if n in defs and defs[n][0] is not co:
                    dupdefs.append((n, os.path.basename(defs[n][0].path), os.path.basename(co.path)))
                defs.setdefault(n, (co, sec - 1, val))
    by_obj = defaultdict(set)
    for n in aliases:
        if n in defs and defs[n][0].sections[defs[n][1]]['name'].startswith(('.data', '.rdata', '.bss')):
            by_obj[defs[n][0].path].add(n)
    sizes = probe_sizes(by_obj, a.jobs)

    status = {}        # VA -> status (one per retail byte the port defines)
    owner = {}         # VA -> symbol name
    overlaps = []
    diffs = []         # (name, va, n, kind)
    sym_rows = []      # (name, va, size, Counter)

    for name, va in sorted(aliases.items(), key=lambda t: t[1]):
        if name not in defs:
            continue
        co, si, val = defs[name]
        s = co.sections[si]
        sname = s['name']
        if not (sname.startswith(('.data', '.rdata', '.bss')) or sname.startswith('COMMON')):
            continue
        # with -fdata-sections the symbol owns its section; a shared section (.data, a struct with members) -> next sym
        size = sizes[name]
        data = s['data'][val:val + size]
        is_bss = sname.startswith('.bss') or (not any(data) and not s['relocs'])   # no initializer
        rel = {off - val: (symidx, typ) for (off, symidx, typ) in s['relocs'] if val <= off < val + size}
        cnt = Counter()
        k = 0
        while k < size:
            v = va + k
            if k in rel:
                symidx, typ = rel[k]
                tname, tsec, tval, tcls = co.symbols[symidx]
                tname = tname.lstrip('_')
                addend, = struct.unpack_from('<I', data, k)
                rv = img.u32(v) if img.off(v) is not None else None
                st = None
                if typ != 6:
                    st = 'ptr-diff'
                elif tsec > 0 and tcls == 3:   # section-relative (static / literal): resolve inside the object
                    tsecd = co.sections[tsec - 1]
                    tsname = tsecd['name']
                    # a static that rhost aliases? (rare) — else port-private data
                    st = 'ptr-port'
                    if rv is not None and v in relocs and not is_code(rv):
                        e = tsecd['data'].find(b'\0', addend)
                        mine = tsecd['data'][addend:e] if e >= 0 else None
                        theirs = img.cstr(rv)
                        if mine is None or theirs is None or mine != theirs:
                            st = 'ptr-port-diff'
                    elif tsname.startswith('.text'):
                        st = 'ptr-code' if (rv is not None and v in relocs and is_code(rv)) else 'ptr-diff'
                    else:
                        st = 'ptr-port-diff'
                else:
                    want = None
                    if tname in aliases:
                        want = (aliases[tname] + addend) & 0xffffffff
                    elif tname in fn_va or FN_VA_RE.match(tname):
                        want = fn_va.get(tname) or int(FN_VA_RE.match(tname).group(1), 16)
                        want += addend
                    if want is not None:
                        st = 'ptr-ok' if (rv == want and v in relocs) else 'ptr-diff'
                    elif tname in defs:
                        tco, tsi, tv = defs[tname]
                        if tco.sections[tsi]['name'].startswith('.text'):
                            st = 'ptr-code' if (rv is not None and v in relocs and is_code(rv)) else 'ptr-diff'
                        else:
                            st = 'ptr-port'
                    else:
                        st = 'ptr-code' if (rv is not None and v in relocs and is_code(rv)) else 'ptr-diff'
                for j in range(4):
                    _mark(status, owner, overlaps, va + k + j, st, name)
                cnt[st] += 4
                if st.endswith('diff'):
                    diffs.append((name, v, 4, st + ' -> ' + tname))
                k += 4
                continue
            rb = retail_byte(v)
            mb = 0 if is_bss else data[k]
            st = 'match' if rb == mb else ('missing' if is_bss else 'diff')
            if rb is None:
                st = 'outside'
            elif img.section_of(v) == '.idata':
                st = 'import'
            _mark(status, owner, overlaps, v, st, name)
            cnt[st] += 1
            if st == 'diff':
                if diffs and diffs[-1][0] == name and diffs[-1][3] == 'diff' and diffs[-1][1] + diffs[-1][2] == v:
                    diffs[-1] = (name, diffs[-1][1], diffs[-1][2] + 1, 'diff')
                else:
                    diffs.append((name, v, 1, 'diff'))
            k += 1
        sym_rows.append((name, va, size, cnt))

    # catalog rows
    catalog = DL.load_catalog()

    # content pass: a catalog stretch no aliased definition covers may still be in the port under its own name (an old
    # port's k_ table, a string literal). Search retail's bytes in every port data section, greedy longest match
    # >= MIN_RUN bytes (8-byte gram index); a hit marks the bytes `own-copy`, the section is the row's source.
    MIN_RUN = 8
    blob, spans = bytearray(), []      # spans: (start, end, label)
    aliased_secs = {n for n in aliases if n in defs}
    for co in coffs:
        for s in co.sections:
            if s['name'].split('$', 1)[-1] in aliased_secs:
                continue                     # an aliased definition: a hit there = a repeat at another retail VA
            if s['name'].startswith(('.data', '.rdata')) and s['size'] and any(s['data']):
                lab = os.path.basename(co.path)[:-2] + ':' + s['name']
                spans.append((len(blob), len(blob) + s['size'], lab))
                blob += s['data'] + b'\xff' * 8     # separator: no match crosses sections
    span_starts = [sp[0] for sp in spans]
    grams = defaultdict(list)
    for i in range(len(blob) - MIN_RUN + 1):
        g = bytes(blob[i:i + MIN_RUN])
        if g.count(0) < MIN_RUN // 2:         # skip mostly-zero grams (padding, sparse tables)
            lst = grams[g]
            if len(lst) < 64:
                lst.append(i)
    blob = bytes(blob)
    own = {}                           # VA -> port section label
    for (cva, csize, _cname, _ctype) in catalog:
        i = 0
        while i < csize:
            v = cva + i
            if status.get(v, 'uncovered') != 'uncovered':
                i += 1
                continue
            j = i
            while j < csize and status.get(cva + j, 'uncovered') == 'uncovered' and retail_byte(cva + j) is not None:
                j += 1
            run = bytes(retail_byte(cva + k) for k in range(i, j))
            k = 0
            while k + MIN_RUN <= len(run):
                best, bpos = 0, -1
                for pos in grams.get(run[k:k + MIN_RUN], ()):
                    n = MIN_RUN
                    while k + n < len(run) and pos + n < len(blob) and blob[pos + n] == run[k + n]:
                        n += 1
                    if n > best:
                        best, bpos = n, pos
                if best:
                    lab = spans[bisect.bisect_right(span_starts, bpos) - 1][2]
                    for m in range(best):
                        status[cva + i + k + m] = 'own-copy'
                        own[cva + i + k + m] = lab
                    k += best
                else:
                    k += 1
            i = max(j, i + 1)

    # raw retail VAs in the port's source (AT(..., 0xVA), RS(...), casts): a row the port reads that way needs a copy
    va_refs = defaultdict(set)         # VA -> {file}
    for c in glob.glob(os.path.join(SRC, '*.[ch]')):
        for m in re.finditer(r'\b0x(4[d-e][0-9a-fA-F]{4})u?\b', open(c, encoding='utf-8', errors='replace').read()):
            va_refs[int(m.group(1), 16)].add(os.path.basename(c))
    ref_vas = sorted(va_refs)

    rows = []
    for (cva, csize, cname, _ctype) in catalog:
        c = Counter(status.get(cva + i, 'uncovered') for i in range(csize))
        srcs = Counter(own[cva + i] for i in range(csize) if cva + i in own)
        files = set()
        if c.get('uncovered'):
            lo = bisect.bisect_left(ref_vas, cva)
            for v in ref_vas[lo:bisect.bisect_left(ref_vas, cva + csize)]:
                files |= va_refs[v]
        rows.append((cva, csize, cname, c, srcs, files))

    tot = Counter()
    for r in rows:
        tot.update(r[3])
    nz_tot = Counter()   # non-zero retail bytes only
    for (cva, csize, _n, _t) in catalog:
        for i in range(csize):
            b = retail_byte(cva + i)
            if b:
                nz_tot[status.get(cva + i, 'uncovered')] += 1

    if a.nodef_json:
        import json
        runs = []
        for (cva, csize, cname, _ctype) in catalog:
            i = 0
            while i < csize:
                if status.get(cva + i, 'uncovered') not in ('uncovered', 'own-copy'):
                    i += 1
                    continue
                j = i
                while j < csize and status.get(cva + j, 'uncovered') in ('uncovered', 'own-copy'):
                    j += 1
                runs.append([cva + i, j - i, cname])
                i = j
        with open(a.nodef_json, 'w') as f:
            json.dump(runs, f)
    write_md(img, a.exe, sym_rows, rows, tot, nz_tot, diffs, overlaps, len(aliases), len(objs))
    print('symbols checked %d; catalog bytes %d: %s' % (len(sym_rows), sum(tot.values()),
          ', '.join('%s %d' % kv for kv in tot.most_common())))
    print('non-zero catalog bytes %d: %s' % (sum(nz_tot.values()), ', '.join('%s %d' % kv for kv in nz_tot.most_common())))
    print('diff spans %d; overlapping definitions %d; names defined twice %d' % (len(diffs), len(overlaps), len(dupdefs)))
    if a.list:
        for n, v, ln, k in diffs:
            print('  %08x +%-5d %-14s %s' % (v, ln, n, k))
        for v, n1, n2 in overlaps[:200]:
            print('  overlap %08x %s / %s' % (v, n1, n2))


def _mark(status, owner, overlaps, va, st, name):
    if va in owner and owner[va] != name:
        if not overlaps or overlaps[-1][1:] != (owner[va], name):
            overlaps.append((va, owner[va], name))
        # keep the worse verdict
        if status[va] in ('match', 'ptr-ok', 'ptr-port', 'ptr-code'):
            status[va] = st
        return
    owner[va] = name
    status[va] = st


def write_md(img, exe, sym_rows, rows, tot, nz_tot, diffs, overlaps, n_alias, n_obj):
    L = []
    w = L.append
    w('# Data self-check — the port\'s own copies of retail\'s data vs Lords2.exe')
    w('')
    w('DERIVED by `tools/data_selfcheck.py` — never hand-edit. The port\'s definitions of aliased globals (initializers in '
      '`src/`, never linked under rhost / the standalone) compared with the exe at their VAs, byte by byte, and '
      'mapped onto the catalog rows (`docs/data/catalog.tsv`). Statuses: see the tool\'s docstring.')
    w('')
    w('%d objects compiled, %d aliased names, %d defined in src/ as data.' % (n_obj, n_alias, len(sym_rows)))
    w('')
    w('| catalog bytes | all | non-zero in retail |')
    w('|---|---:|---:|')
    keys = sorted(set(tot) | set(nz_tot), key=lambda k: -tot.get(k, 0))
    for k in keys:
        w('| %s | %d | %d |' % (k, tot.get(k, 0), nz_tot.get(k, 0)))
    w('| **total** | %d | %d |' % (sum(tot.values()), sum(nz_tot.values())))
    w('')
    bad = [s for s in sym_rows if any(k.endswith('diff') for k in s[3])]
    w('## Port definitions that differ from the exe (%d)' % len(bad))
    w('')
    if bad:
        w('| symbol | VA | size | diff bytes | ptr-diff bytes |')
        w('|---|---|---:|---:|---:|')
        for n, va, size, c in bad:
            w('| `%s` | %08x | %d | %d | %d |' % (n, va, size, c['diff'], c['ptr-diff'] + c['ptr-port-diff']))
        w('')
    w('## Overlapping definitions (%d)' % len(overlaps))
    w('')
    for v, n1, n2 in overlaps:
        w('- %08x `%s` / `%s`' % (v, n1, n2))
    w('')
    w('## Catalog rows')
    w('')
    w('`own-copy found in` = the port sections holding the most matched bytes; `uncovered, src names its VA` = source '
      'files that spell a VA inside the row (the port reads it through retail\'s image: it needs a definition).')
    w('')
    w('| VA | size | name | port copy | own-copy found in | uncovered, src names its VA |')
    w('|---|---:|---|---|---|---|')
    for cva, csize, cname, c, srcs, files in rows:
        w('| %08x | %d | %s | %s | %s | %s |' % (cva, csize, cname, ', '.join('%s %d' % kv for kv in sorted(c.items())),
                                                ', '.join('`%s`' % k for k, _ in srcs.most_common(3)),
                                                ' '.join(sorted(files))))
    w('')
    open(OUT_MD, 'w').write('\n'.join(L))


if __name__ == '__main__':
    main()
