#!/usr/bin/env python3
"""tools/data_ledger.py — the data-image ledger: every initialized byte the standalone takes from Lords2.exe.

The standalone maps retail's data image (headers, .rdata, .data's initialized part, .idata, .rsrc) at retail's VAs.
This tool carves that image into items and gives each one a documentation status, so "how much of the data image is
documented" is a number:

  documented    a row in the hand-edited catalog docs/data/catalog.tsv covers the item
  named         a non-generic port name sits on it (src/ / tools/rhost aliases, #define VA constants) or the PE
                structure names it (header fields, import slots, resources)
  referenced    only IDA data xrefs / absolute references (code relocs, data pointer slots) reach it
  unreferenced  nothing does (dead data, or reached only by pointer arithmetic from a neighbour)

Boundaries: section starts, IDA data-xref targets, IDA string starts, the targets of every absolute reference in the
.reloc table (from .text or from data), port-name VAs, catalog rows, PE structure (headers / imports / resources).
Inside a labeled stretch, a run of >= 16 zero bytes after the content splits off as a `zero` item; everything else up
to the next label stays one item. Catalog rows and PE structure pieces are never split.

Pointer slots: .reloc HIGHLOW entries inside the data image. Target in .text = code pointer (the standalone rewrites
it to the port), anywhere else in the image = data pointer (valid only at retail's VAs).

Outputs (DERIVED — never hand-edit): docs/data-ledger.md, docs/data-ledger.tsv. No byte contents are written (strings:
length only). Stdlib only, deterministic.

Usage:  python3 tools/data_ledger.py [--exe PATH] [--ida DIR]
"""
from __future__ import annotations

import argparse
import bisect
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF_EXE = os.path.join(ROOT, 'vendor', 'original', 'Lords2.exe')
ALT_EXE = '/mnt/ephemeral/gog/Lords of the Realm II/Lords2.exe'
DEF_IDA = '/mnt/ephemeral/openlords2/decompiled-ida'
CATALOG = os.path.join(ROOT, 'docs', 'data', 'catalog.tsv')
OUT_MD = os.path.join(ROOT, 'docs', 'data-ledger.md')
OUT_TSV = os.path.join(ROOT, 'docs', 'data-ledger.tsv')

STATUSES = ('documented', 'named', 'referenced', 'unreferenced')
GENERIC = re.compile(r'^(g_(rl|bf|tr|gfx|ui|io)_[0-9a-fA-F]{6,8}|s_slot_[0-9a-fA-F]{8}|g_[a-z0-9]+_[0-9a-fA-F]{6}|'
                     r'(DAT|dword|byte|word|unk|off|stru|asc|flt|dbl|qword|PTR|LAB|FUN)_[0-9a-fA-F_]+|_?[A-Z]_[0-9A-F]{6})$')
RT = {1: 'RT_CURSOR', 2: 'RT_BITMAP', 3: 'RT_ICON', 4: 'RT_MENU', 5: 'RT_DIALOG', 6: 'RT_STRING', 9: 'RT_ACCELERATOR',
      10: 'RT_RCDATA', 12: 'RT_GROUP_CURSOR', 14: 'RT_GROUP_ICON', 16: 'RT_VERSION', 24: 'RT_MANIFEST'}


# ---------------------------------------------------------------------------------------------------------- PE image
class Image:
    def __init__(self, path):
        self.raw = open(path, 'rb').read()
        d = self.raw
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        self.e_lfanew = pe
        nsec, = struct.unpack_from('<H', d, pe + 6)
        optsz, = struct.unpack_from('<H', d, pe + 20)
        opt = pe + 24
        self.opt, self.optsz = opt, optsz
        self.base, = struct.unpack_from('<I', d, opt + 28)
        self.size_headers, = struct.unpack_from('<I', d, opt + 60)
        self.dirs = [struct.unpack_from('<II', d, opt + 96 + 8 * i) for i in range(16)]
        self.sections = []   # (name, va, vsize, rawsize, rawptr)
        for i in range(nsec):
            o = opt + optsz + 40 * i
            nm, vs, rva, rs, rp = struct.unpack_from('<8sIIII', d, o)
            self.sections.append((nm.rstrip(b'\0').decode(), self.base + rva, vs, rs, rp))
        self.sectab = (opt + optsz, 40 * nsec)
        self.sec = {s[0]: s for s in self.sections}

    def init_range(self, name):
        """[va, end) of the bytes the file initializes in section `name`."""
        _, va, vs, rs, _ = self.sec[name]
        return va, va + min(vs, rs)

    def off(self, va):
        if self.base <= va < self.base + self.size_headers:
            return va - self.base
        for _, sva, vs, rs, rp in self.sections:
            if sva <= va < sva + min(vs, rs):
                return rp + va - sva
        return None

    def b(self, va, n):
        o = self.off(va)
        return self.raw[o:o + n]

    def u32(self, va):
        return struct.unpack_from('<I', self.raw, self.off(va))[0]

    def u16(self, va):
        return struct.unpack_from('<H', self.raw, self.off(va))[0]

    def cstr(self, va, maxlen=512):
        o = self.off(va)
        e = self.raw.find(b'\0', o, o + maxlen)
        return self.raw[o:e] if e >= 0 else None

    def section_of(self, va):
        if self.base <= va < self.base + 0x1000:
            return 'headers'
        for nm, sva, vs, _, _ in self.sections:
            if sva <= va < sva + max(vs, 1):
                return nm
        return None

    def relocs(self):
        """VAs of every IMAGE_REL_BASED_HIGHLOW entry."""
        rva, size = self.dirs[5]
        out = []
        va = self.base + rva
        end = va + size
        while va < end:
            page, blk = struct.unpack_from('<II', self.raw, self.off(va))
            if blk < 8:
                break
            for i in range((blk - 8) // 2):
                e = self.u16(va + 8 + 2 * i)
                if e >> 12 == 3:
                    out.append(self.base + page + (e & 0xfff))
            va += blk
        return out


# ---------------------------------------------------------------------------------------------------------- inputs
def read_tsv(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        hdr = f.readline().rstrip('\n').split('\t')
        for line in f:
            yield dict(zip(hdr, line.rstrip('\n').split('\t')))


def load_functions(ida):
    starts, ends, names = [], [], []
    rng = []
    for r in read_tsv(os.path.join(ida, 'functions_ida.tsv')):
        va = int(r['va'], 16)
        rng.append((va, va + int(r['size']), va))
        for ch in filter(None, (r.get('chunks') or '').split(',')):
            a, b = ch.split(':')[0].split('-')
            rng.append((int(a, 16), int(b, 16), va))
        names.append((va, r['name']))
    rng.sort()
    return rng, dict(names)


VA_RE = r'0x0*([0-9a-fA-F]{6})u?\b'
NAME_PATS = [
    re.compile(r'\b(?:BFA|A)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*' + VA_RE),
    re.compile(r'\.set\s+_?([A-Za-z_][A-Za-z0-9_]*)\s*,\s*' + VA_RE),
    re.compile(r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+\(?\s*' + VA_RE + r'\s*\)?\s*(?:/[*/].*)?$', re.M),
]
GEN_TOKEN = re.compile(r'\bg_(?:rl|bf)_([0-9a-f]{6})\b')
LIT = re.compile(r'\b0x0*(4d[0-9a-fA-F]{4}|4e[0-5][0-9a-fA-F]{3}|5c[f][0-9a-fA-F]{3}|5d[0-3][0-9a-fA-F]{3})u?\b')


def scan_port(lo, hi):
    """names: va -> set(names); lits: va -> set(files) for VA literals in src/ (the port's code)."""
    names = defaultdict(set)
    lits = defaultdict(set)
    roots = [(os.path.join(ROOT, 'src'), True), (os.path.join(ROOT, 'tools', 'rhost'), False)]
    for d, is_src in roots:
        if not os.path.isdir(d):
            continue
        for dp, dn, fn in os.walk(d):
            dn[:] = sorted(x for x in dn if x != 'gen')
            for f in sorted(fn):
                if not f.endswith(('.c', '.h', '.S', '.s')):
                    continue
                p = os.path.join(dp, f)
                try:
                    t = open(p, encoding='utf-8', errors='replace').read()
                except OSError:
                    continue
                rel = os.path.relpath(p, ROOT)
                for pat in NAME_PATS:
                    for m in pat.finditer(t):
                        va = int(m.group(2), 16)
                        if lo <= va < hi:
                            names[va].add(m.group(1))
                for m in GEN_TOKEN.finditer(t):
                    va = int(m.group(1), 16)
                    if lo <= va < hi:
                        names[va].add('g_rl_%06x' % va if 'rl_' in m.group(0) else 'g_bf_%06x' % va)
                if is_src:
                    for m in LIT.finditer(t):
                        va = int(m.group(1), 16)
                        if lo <= va < hi:
                            lits[va].add(rel)
    return names, lits


def load_catalog():
    rows = []
    if not os.path.exists(CATALOG):
        return rows
    for r in read_tsv(CATALOG):
        if not r.get('va') or r['va'].startswith('#'):
            continue
        rows.append((int(r['va'], 16), int(r['size'], 0), r.get('name', ''), r.get('type', '')))
    rows.sort()
    for a, b in zip(rows, rows[1:]):
        if a[0] + a[1] > b[0]:
            sys.exit('catalog: overlapping rows %#x+%d / %#x' % (a[0], a[1], b[0]))
    return rows


# ---------------------------------------------------------------------------------------------------------- PE structure
def pe_structure(img):
    """[(va, end, kind, name)] — header pieces, import tables, resource tree + blobs."""
    B = img.base
    out = []
    out.append((B, B + 0x40, 'header', 'IMAGE_DOS_HEADER'))
    out.append((B + 0x40, B + img.e_lfanew, 'header', 'DOS stub'))
    out.append((B + img.e_lfanew, B + img.opt, 'header', 'PE signature + IMAGE_FILE_HEADER'))
    out.append((B + img.opt, B + img.opt + img.optsz, 'header', 'IMAGE_OPTIONAL_HEADER32'))
    st, sl = img.sectab
    out.append((B + st, B + st + sl, 'header', 'section table'))
    out.append((B + st + sl, B + img.size_headers, 'header', 'header padding'))
    # debug directory (sits at the start of .rdata)
    rva, size = img.dirs[6]
    if size:
        out.append((B + rva, B + rva + size, 'header', 'IMAGE_DEBUG_DIRECTORY'))
        for i in range(size // 28):
            dsz, drva = struct.unpack_from('<II', img.raw, img.off(B + rva + 28 * i + 16))
            if drva:
                out.append((B + drva, B + drva + dsz, 'header', 'debug data %d' % i))
    # imports
    iat_names = {}
    rva, size = img.dirs[1]
    va = B + rva
    n = 0
    while True:
        ilt, _, _, name, iat = struct.unpack_from('<IIIII', img.raw, img.off(va + 20 * n))
        n += 1
        if not (ilt or name or iat):
            break
        dll = img.cstr(B + name).decode('ascii', 'replace')
        out.append((B + name, B + name + len(dll) + 1, 'import', 'dll name %s' % dll))
        thunks = ilt or iat
        k = 0
        hn = []
        while True:
            t = img.u32(B + thunks + 4 * k)
            if not t:
                break
            if t & 0x80000000:
                fn = '#%d' % (t & 0xffff)
            else:
                fn = img.cstr(B + t + 2).decode('ascii', 'replace')
                hn.append((B + t, (B + t + 2 + len(fn) + 2) & ~1, fn))
            iat_names[B + iat + 4 * k] = '%s!%s' % (dll, fn)
            out.append((B + iat + 4 * k, B + iat + 4 * k + 4, 'import', 'IAT %s!%s' % (dll, fn)))
            k += 1
        out.append((B + iat + 4 * k, B + iat + 4 * k + 4, 'import', 'IAT %s terminator' % dll))
        if ilt:
            out.append((B + ilt, B + ilt + 4 * (k + 1), 'import', 'ILT %s (%d)' % (dll, k)))
        for hs, he, fn in hn:
            out.append((hs, he, 'import', 'hint/name %s!%s' % (dll, fn)))
    out.append((va, va + 20 * n, 'import', 'IMAGE_IMPORT_DESCRIPTOR[%d]' % n))
    # resources
    rva, size = img.dirs[2]
    rbase = B + rva

    def rname(entry_name):
        if entry_name & 0x80000000:
            o = rbase + (entry_name & 0x7fffffff)
            ln = img.u16(o)
            out.append((o, o + 2 + 2 * ln, 'resource', 'rsrc name string'))
            return img.b(o + 2, 2 * ln).decode('utf-16-le', 'replace')
        return entry_name

    def walk(o, path):
        nn, ni = struct.unpack_from('<HH', img.raw, img.off(o + 12))
        cnt = nn + ni
        out.append((o, o + 16 + 8 * cnt, 'resource', 'rsrc directory %s' % ('/'.join(map(str, path)) or 'root')))
        for i in range(cnt):
            name, tgt = struct.unpack_from('<II', img.raw, img.off(o + 16 + 8 * i))
            nm = rname(name)
            if not path and isinstance(nm, int):
                nm = RT.get(nm, nm)
            if tgt & 0x80000000:
                walk(rbase + (tgt & 0x7fffffff), path + [nm])
            else:
                de = rbase + tgt
                drva, dsz = struct.unpack_from('<II', img.raw, img.off(de))
                p = '/'.join(map(str, path + [nm]))
                out.append((de, de + 16, 'resource', 'rsrc data entry %s' % p))
                out.append((B + drva, B + drva + dsz, 'resource', 'resource %s (%d B)' % (p, dsz)))
    if size:
        walk(rbase, [])
    return out, iat_names


# ---------------------------------------------------------------------------------------------------------- carving
def printable_str(img, va, end):
    o = img.off(va)
    lim = min(end - va, 1024)
    e = img.raw.find(b'\0', o, o + lim)
    if e < 0 or e - o < 4:
        return None
    s = img.raw[o:e]
    if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return e - o
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--exe', default=DEF_EXE if os.path.exists(DEF_EXE) else ALT_EXE)
    ap.add_argument('--ida', default=DEF_IDA)
    a = ap.parse_args()
    img = Image(a.exe)
    B = img.base

    regions = [('headers',) + (B, B + img.size_headers)]
    for nm in ('.rdata', '.data', '.idata', '.rsrc'):
        regions.append((nm,) + img.init_range(nm))
    text_lo, text_hi = img.sec['.text'][1], img.sec['.text'][1] + img.sec['.text'][2]
    img_lo, img_hi = B, max(s[1] + s[2] for s in img.sections)

    def in_regions(va):
        return any(lo <= va < hi for _, lo, hi in regions)

    # --- functions, xrefs, strings
    frng, fnames = load_functions(a.ida)
    fstarts = [r[0] for r in frng]

    def fof(va):
        i = bisect.bisect_right(fstarts, va) - 1
        best = None
        for j in range(i, max(i - 64, -1), -1):
            s, e, f = frng[j]
            if s <= va < e:
                best = f
                break
        return best

    code_refs = defaultdict(set)      # target va -> funcs
    for r in read_tsv(os.path.join(a.ida, 'data_xrefs.tsv')):
        t = int(r['target_va'], 16)
        if in_regions(t):
            code_refs[t].add(int(r['func_va'], 16))
    ida_str = {}
    for r in read_tsv(os.path.join(a.ida, 'strings_ida.tsv')):
        va = int(r['va'], 16)
        if in_regions(va):
            ida_str[va] = int(r['len'])

    # --- relocations
    slots = {}                         # slot va (in data image) -> ('code'|'data', target)
    n_text_relocs = 0
    for rv in img.relocs():
        if text_lo <= rv < text_hi:
            t = img.u32(rv) if img.off(rv) is not None else None
            if t is not None and in_regions(t):
                f = fof(rv)
                code_refs[t].add(f if f is not None else rv)
            n_text_relocs += 1
            continue
        if not in_regions(rv):
            continue
        t = img.u32(rv)
        cls = 'code' if text_lo <= t < text_hi else 'data'
        slots[rv] = (cls, t)
    data_refs = defaultdict(set)       # target va -> slot vas
    for sv, (cls, t) in slots.items():
        if cls == 'data' and in_regions(t):
            data_refs[t].add(sv)

    # --- port names and literals
    pnames, plits = scan_port(img_lo, img_hi)
    catalog = load_catalog()
    pe_items, iat_names = pe_structure(img)

    # --- boundaries
    labels = set(code_refs) | set(ida_str) | set(data_refs) | {v for v in pnames if in_regions(v)}
    pe_by_start = {}
    for s, e, k, n in pe_items:
        if in_regions(s):
            labels.add(s)
            if in_regions(e - 1):
                labels.add(e)
            pe_by_start.setdefault(s, (e, k, n))
    cat_by_start = {}
    for va, size, nm, ty in catalog:
        labels.add(va)
        labels.add(va + size)
        cat_by_start[va] = (va + size, nm, ty)
    # atomic ranges: no interior boundary (catalog rows; PE structure pieces)
    atomic = sorted({(va, va + size) for va, size, _, _ in catalog} |
                    {(s, e) for s, e, _, _ in pe_items if in_regions(s) and e > s})
    at_starts = [x[0] for x in atomic]

    def in_catalog(va):
        i = bisect.bisect_right(at_starts, va) - 1
        while i >= 0:
            s, e = atomic[i]
            if s < va < e:
                return True
            if va - s > 0x4000:
                return False
            i -= 1
        return False

    items = []   # dicts
    slot_list = sorted(slots)
    for sec, lo, hi in regions:
        cuts = sorted({lo, hi} | {x for x in labels if lo < x < hi and not in_catalog(x)})
        for s, e in zip(cuts, cuts[1:]):
            pieces = [(s, e, s in labels or s == lo)]
            if s not in cat_by_start and s not in pe_by_start:
                pieces = split_piece(img, s, e, slot_list)
            for ps, pe_, lab in pieces:
                items.append({'va': ps, 'end': pe_, 'sec': sec})

    # --- classify
    for it in items:
        s, e = it['va'], it['end']
        n = e - s
        data = img.b(s, n)
        nz = n - data.count(0)
        i0 = bisect.bisect_left(slot_list, s)
        i1 = bisect.bisect_left(slot_list, e)
        ss = slot_list[i0:i1]
        cp = sum(1 for x in ss if slots[x][0] == 'code')
        dp = len(ss) - cp
        it.update(size=n, nonzero=nz, code_ptrs=cp, data_ptrs=dp)
        refs = set()
        drefs = set()
        for x in range(s, e):
            if x in code_refs:
                refs |= code_refs[x]
            if x in data_refs:
                drefs |= data_refs[x]
        it['funcs'] = sorted(refs)
        it['drefs'] = sorted(drefs)
        nm = set()
        for v in pnames:
            if s <= v < e:
                nm |= pnames[v]
        it['names'] = sorted(nm)
        lf = set()
        for v in plits:
            if s <= v < e:
                lf |= plits[v]
        it['port_files'] = len(lf)
        pe = pe_by_start.get(s)
        it['pe'] = pe[2] if pe and pe[0] == e else (pe[2] + ' (part)' if pe else '')
        cat = cat_by_start.get(s)
        it['cat'] = cat[1] if cat else ''
        it['cat_type'] = cat[2] if cat else ''
        # kind
        if it['sec'] == 'headers' or (pe and pe[1] == 'header'):
            kind = 'header'
        elif it['sec'] == '.idata' or (pe and pe[1] == 'import'):
            kind = 'import'
        elif it['sec'] == '.rsrc' or (pe and pe[1] == 'resource'):
            kind = 'resource'
        elif ss and (cp + dp) * 4 == n:
            kind = 'ptr(code)' if dp == 0 else 'ptr(data)' if cp == 0 else 'ptr(mixed)'
        elif nz == 0:
            kind = 'zero'
        elif s in ida_str or (not ss and printable_str(img, s, e)):
            kind = 'string'
        elif n <= 8 and not ss:
            kind = 'scalar'
        else:
            kind = 'table'
        if cat and kind not in ('zero', 'header', 'import', 'resource') and not kind.startswith('ptr'):
            kind = 'string' if cat[2].startswith('char') else 'scalar' if n <= 8 and '[' not in cat[2] else 'table'
        it['kind'] = kind
        if kind == 'string':
            it['strlen'] = ida_str.get(s) or printable_str(img, s, e) or 0
        else:
            it['strlen'] = ''
        # status
        named = [x for x in it['names'] if not GENERIC.match(x)]
        if it['cat']:
            st = 'documented'
        elif named or (it['pe'] and kind in ('header', 'import', 'resource')):
            st = 'named'
        elif refs or drefs or it['port_files']:
            st = 'referenced'
        else:
            st = 'unreferenced'
        it['status'] = st

    items.sort(key=lambda x: x['va'])
    check_cover(items, regions)
    write_tsv(items)
    write_md(img, items, regions, slots, n_text_relocs, fnames, catalog)
    print('%d items, %d B; wrote %s, %s' % (len(items), sum(i['size'] for i in items),
                                           os.path.relpath(OUT_TSV, ROOT), os.path.relpath(OUT_MD, ROOT)))


def split_piece(img, s, e, slot_list):
    """A labeled stretch [s, e): split off a trailing zero run (>= 16 B). Returns [(start, end, labeled)].
    (Pointer runs are NOT split from their labeled head: retail's menu / hotspot records are a referenced header
    followed by unlabeled pointer slots, one record.)"""
    parts = [(s, e)]
    out = []
    for a, b in parts:
        data = img.b(a, b - a)
        # trailing zero run
        k = len(data)
        while k > 0 and data[k - 1] == 0:
            k -= 1
        z0 = a + k
        # keep the string terminator / alignment with the content: the zero item starts 4-aligned
        z0 = (z0 + 4) & ~3 if k else a
        if k and b - z0 >= 16:
            out.append((a, z0, a == s))
            out.append((z0, b, False))
        else:
            out.append((a, b, a == s))
    return out


def check_cover(items, regions):
    for sec, lo, hi in regions:
        its = [i for i in items if i['sec'] == sec]
        assert its[0]['va'] == lo and its[-1]['end'] == hi, sec
        for x, y in zip(its, its[1:]):
            assert x['end'] == y['va'], (hex(x['end']), hex(y['va']))


def fmt_funcs(fs, n=4):
    return ','.join('%06x' % f for f in fs[:n]) + (',…' if len(fs) > n else '')


def write_tsv(items):
    cols = ['va', 'size', 'section', 'kind', 'status', 'nonzero', 'code_ptrs', 'data_ptrs', 'strlen', 'n_funcs',
            'funcs', 'n_data_refs', 'port_files', 'port_names', 'pe', 'catalog']
    with open(OUT_TSV, 'w', newline='\n') as f:
        f.write('# DERIVED by tools/data_ledger.py — do not edit; regenerate: python3 tools/data_ledger.py\n')
        f.write('\t'.join(cols) + '\n')
        for i in items:
            f.write('\t'.join(str(x) for x in [
                '%06x' % i['va'], i['size'], i['sec'], i['kind'], i['status'], i['nonzero'], i['code_ptrs'],
                i['data_ptrs'], i['strlen'], len(i['funcs']), fmt_funcs(i['funcs'], 8), len(i['drefs']),
                i['port_files'], ','.join(i['names'][:6]) + (',…' if len(i['names']) > 6 else ''), i['pe'],
                i['cat']]) + '\n')


def write_md(img, items, regions, slots, n_text_relocs, fnames, catalog):
    L = []
    w = L.append
    w('# Data-image ledger (DERIVED)')
    w('')
    w('Generated by `tools/data_ledger.py` from the user\'s `Lords2.exe`, the IDA export and the port sources — '
      '**do not edit**; regenerate with `python3 tools/data_ledger.py`. Hand-written documentation goes in '
      '`docs/data/catalog.tsv` (one row per item: `va size name type meaning evidence`); per-item rows: '
      '`docs/data-ledger.tsv`. No byte contents are recorded here.')
    w('')
    w('Scope: every byte the standalone maps from the exe — headers, `.rdata`, the initialized part of `.data`, '
      '`.idata`, `.rsrc`. `.text` (code) and `.reloc` are out of scope; `.data` beyond its raw size is `.bss` (zero, '
      'not from the file).')
    w('')
    w('Status: **documented** = a catalog row · **named** = a non-generic port name (or a PE structure name: header, '
      'import slot, resource) · **referenced** = IDA data xrefs / absolute references (code relocs, data pointer '
      'slots, port VA literals) only · **unreferenced** = nothing.')
    w('')
    secs = [r[0] for r in regions]
    w('## Totals')
    w('')
    w('| section | range | bytes | non-zero | items | ' + ' | '.join(STATUSES) + ' |')
    w('|---|---|--:|--:|--:|' + '--:|' * len(STATUSES))
    tot = Counter()
    for sec, lo, hi in regions + [('all', None, None)]:
        its = [i for i in items if sec == 'all' or i['sec'] == sec]
        by = Counter()
        byn = Counter()
        for i in its:
            by[i['status']] += i['size']
            byn[i['status']] += 1
        rng = '%06x–%06x' % (lo, hi) if lo else ''
        w('| %s | %s | %d | %d | %d | %s |' % (
            sec, rng, sum(i['size'] for i in its), sum(i['nonzero'] for i in its), len(its),
            ' | '.join('%d B / %d' % (by[s], byn[s]) for s in STATUSES)))
    w('')
    w('Non-zero bytes per status (zero bytes need no documentation beyond their extent):')
    w('')
    w('| section | ' + ' | '.join(STATUSES) + ' |')
    w('|---|' + '--:|' * len(STATUSES))
    for sec in secs + ['all']:
        its = [i for i in items if sec == 'all' or i['sec'] == sec]
        by = Counter()
        for i in its:
            by[i['status']] += i['nonzero']
        nzt = sum(by.values()) or 1
        w('| %s | %s |' % (sec, ' | '.join('%d (%.1f %%)' % (by[s], 100.0 * by[s] / nzt) for s in STATUSES)))
    w('')
    w('By kind (all sections):')
    w('')
    w('| kind | items | bytes | non-zero |')
    w('|---|--:|--:|--:|')
    kc, kb, kn = Counter(), Counter(), Counter()
    for i in items:
        kc[i['kind']] += 1
        kb[i['kind']] += i['size']
        kn[i['kind']] += i['nonzero']
    for k in sorted(kc):
        w('| %s | %d | %d | %d |' % (k, kc[k], kb[k], kn[k]))
    w('')
    # pointer slots
    w('## Pointer slots (.reloc HIGHLOW entries inside the data image)')
    w('')
    code = sorted(v for v, (c, _) in slots.items() if c == 'code')
    data = sorted(v for v, (c, _) in slots.items() if c == 'data')
    w('%d relocations in `.text` (code\'s own absolute references; their data targets count as references). In the '
      'data image: **%d code-pointer slots** (target in `.text`: the standalone rewrites each to its port) and **%d '
      'data-pointer slots** (valid only at retail\'s VAs).' % (n_text_relocs, len(code), len(data)))
    w('')
    w('| section | code-pointer slots | distinct code targets | data-pointer slots | distinct data targets |')
    w('|---|--:|--:|--:|--:|')
    for sec in secs:
        cs = [v for v in code if img.section_of(v) == sec or (sec == 'headers' and img.section_of(v) == 'headers')]
        ds = [v for v in data if img.section_of(v) == sec]
        w('| %s | %d | %d | %d | %d |' % (sec, len(cs), len({slots[v][1] for v in cs}), len(ds),
                                          len({slots[v][1] for v in ds})))
    w('')
    tgt_named = Counter()
    for v in code:
        t = slots[v][1]
        nm = fnames.get(t)
        tgt_named['function start (IDA)' if nm is not None else 'inside a function / no IDA function'] += 1
    w('Code-pointer slot targets: ' + ', '.join('%d %s' % (n, k) for k, n in sorted(tgt_named.items())) + '.')
    w('')
    w('Code-pointer tables (items holding code-pointer slots, by slot count):')
    w('')
    w('| va | size | kind | status | code ptrs | data ptrs | funcs | port names |')
    w('|---|--:|---|---|--:|--:|---|---|')
    cpi = sorted((i for i in items if i['code_ptrs']), key=lambda i: (-i['code_ptrs'], i['va']))
    for i in cpi[:40]:
        w('| %06x | %d | %s | %s | %d | %d | %s | %s |' % (
            i['va'], i['size'], i['kind'], i['status'], i['code_ptrs'], i['data_ptrs'],
            fmt_funcs(i['funcs']), ', '.join(i['names'][:3])))
    if len(cpi) > 40:
        w('| … %d more | | | | %d | | | |' % (len(cpi) - 40, sum(i['code_ptrs'] for i in cpi[40:])))
    w('')
    # top undocumented
    w('## Largest undocumented items (non-zero bytes)')
    w('')
    w('| va | size | non-zero | section | kind | status | ptrs c/d | funcs (n: first) | port / PE names |')
    w('|---|--:|--:|---|---|---|---|---|---|')
    und = sorted((i for i in items if i['status'] != 'documented' and i['kind'] not in ('header', 'import')),
                 key=lambda i: (-i['nonzero'], i['va']))
    for i in und[:40]:
        w('| %06x | %d | %d | %s | %s | %s | %d/%d | %d: %s | %s |' % (
            i['va'], i['size'], i['nonzero'], i['sec'], i['kind'], i['status'], i['code_ptrs'], i['data_ptrs'],
            len(i['funcs']), fmt_funcs(i['funcs'], 3), ', '.join(i['names'][:3]) or i['pe']))
    w('')
    w('## Catalog')
    w('')
    w('%d rows in `docs/data/catalog.tsv`, %d B.' % (len(catalog), sum(c[1] for c in catalog)))
    w('')
    with open(OUT_MD, 'w', newline='\n') as f:
        f.write('\n'.join(L))


if __name__ == '__main__':
    main()
