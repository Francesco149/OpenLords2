#!/usr/bin/env python3
"""gen_names.py — derive tools/ida/names.tsv (retail VA -> port function name) from src/.

The port cites the retail function it implements as `FUN_<8 hex>` next to each port
function ("Port of FUN_00409e09 @ 0x409e09." above the definition, a file header
"pure-C port of retail FUN_00404a46", `name(...); /* FUN_x */` at call sites,
"FUN_x = name" / "FUN_x (…) → name" in prose).  This turns those citations into IDA
names so the pseudocode reads `engine_rng_step()` instead of `sub_404A46()`.

Evidence (weights in W; a (VA, name) pair's score is the sum over all its citations):
  trace         CALL_TRACE_ENTER(0xVA) in the function body
  doc:strong    comment directly above the definition (or first in its body) says
                "port/translation of [retail] FUN_x"
  doc:lead      that comment opens with FUN_x (also banner rules "/* ─── FUN_x — … */")
  doc:one       that comment cites exactly one FUN_x (weak: usually a callee mention)
  proto:*       the same, on a prototype (header doc comment or trailing `/* FUN_x */`)
  hdr           the file header says "port of FUN_x" -> the file's first exported function
  callsite      `name(...);  /* FUN_x ... */`
  map           "FUN_x = name", "FUN_x (…) → name"   (explicit mapping prose)
  prose         "name = FUN_x", "name (FUN_x)", "FUN_x (name)"
  (a strong/lead citation listing several VAs — "port of FUN_a / FUN_b" — in a comment set off
   from the declaration by a blank line is a section note: it counts as doc:one)
Highest score wins per VA; a name is given to one VA only; pairs below SCORE_MIN are dropped.  Names
are our own work (fine to commit); the decompiled output they decorate is not.

  python3 tools/ida/gen_names.py                 # write tools/ida/names.tsv
  python3 tools/ida/gen_names.py --check FUNCS   # also report VAs that are not function
                                                 # starts in an IDA functions_ida.tsv
"""
import argparse, csv, os, re, sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(ROOT, "tools", "ida", "names.tsv")

FUN = re.compile(r"FUN_([0-9a-fA-F]{8})")
STRONG = re.compile(r"(?i)\b(?:port(?:s|ed)?|translation|reimplementation|implementation)\s+of\s+"
                    r"(?:the\s+)?(?:retail\s+)?[`(\"]?FUN_([0-9a-fA-F]{8})")
MULTI = re.compile(r"`?\s*(?:@\s*0x[0-9a-fA-F]+\s*)?(?:/|,|\+|&|and|or|…|\.\.\.)\s*`?FUN_[0-9a-fA-F]{8}")
IDENT = r"[A-Za-z_][A-Za-z0-9_]*"
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "do", "else", "case"}


def blank(text):
    """Comments and string/char literals -> spaces (offsets kept); also return comment spans."""
    out = list(text); spans = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2); j = n if j < 0 else j + 2
            spans.append((i, j))
            for k in range(i, j):
                if out[k] != "\n": out[k] = " "
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i); j = n if j < 0 else j
            spans.append((i, j))
            for k in range(i, j): out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)): out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out), spans


def definitions(text, code):
    """Yield (name, sig_start, body_open, body_close) for top-level function definitions."""
    depth = 0; last_top = 0   # offset after the previous top-level ';' / '}' / preprocessor line
    last_item = 0             # same, ignoring preprocessor lines (file header reaches past #includes)
    i, n = 0, len(code)
    line_start = True
    while i < n:
        c = code[i]
        if line_start and depth == 0:
            m = re.match(r"[ \t]*#", code[i:i + 200])
            if m:   # preprocessor line (with continuations)
                j = i
                while True:
                    e = code.find("\n", j); e = n if e < 0 else e
                    if e > 0 and code[e - 1] == "\\": j = e + 1; continue
                    break
                i = e; last_top = e; continue
        line_start = c == "\n"
        if c == "{":
            if depth == 0:
                head = code[last_top:i]
                m = re.search(r"(" + IDENT + r")\s*\(([^{};]*)\)\s*$", head, re.S)
                if m and m.group(1) not in KEYWORDS and not re.search(r"=\s*$", head[:m.start()]):
                    # find matching close
                    d = 0; j = i
                    while j < n:
                        if code[j] == "{": d += 1
                        elif code[j] == "}":
                            d -= 1
                            if d == 0: break
                        j += 1
                    # declaration start: first code char after the last comment before the name
                    k = last_top
                    while k < n and code[k].isspace(): k += 1
                    yield m.group(1), last_top + m.start(), i, j, last_top, last_item, k
                    i = j + 1; last_top = last_item = i; continue
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: last_top = last_item = i + 1
        elif c == ";" and depth == 0:
            head = code[last_top:i]
            m = re.search(r"(" + IDENT + r")\s*\(([^{};()]*(?:\([^()]*\)[^{};()]*)*)\)\s*$", head, re.S)
            if m and m.group(1) not in KEYWORDS and not re.search(r"\b(typedef|return)\b|[=(]\s*$|\(\s*\*", head[:m.start()] + " "):
                k = last_top
                while k < n and code[k].isspace(): k += 1
                yield m.group(1), last_top + m.start(), None, i, last_top, last_item, k
            last_top = last_item = i + 1
        i += 1


def comments_in(spans, lo, hi):
    return [(a, b) for a, b in spans if a >= lo and b <= hi]


def own_line(code, a):
    """True if the comment at offset a starts its line (not a trailing comment)."""
    ls = code.rfind("\n", 0, a) + 1
    return code[ls:a].strip() == ""


def trailing(code, spans, o):
    """The comment that trails offset o on the same line, if any."""
    e = code.find("\n", o); e = len(code) if e < 0 else e
    for a, b in spans:
        if o <= a < e and code[o:a].strip() == "":
            return a, b
    return None


def doc_vas(comment, section=False):
    """-> (va, rule) from a doc comment: 'port of FUN_x' (strong), opens with FUN_x
    (lead), or cites exactly one VA (one; weak — usually a callee/caller mention).
    section=True (comment set off by a blank line): a citation listing several VAs —
    "port of FUN_a / FUN_b" — describes the section, not this function: weak."""
    m = STRONG.search(comment)
    if m:
        return int(m.group(1), 16), "one" if section and MULTI.match(comment, m.end()) else "strong"
    body = re.sub(r"^[/*\s─━—=\-]+", "", comment)   # also banner rules: /* ─── FUN_x — … */
    m = re.match(r"(?:retail\s+)?`?FUN_([0-9a-fA-F]{8})", body)
    if m:
        return int(m.group(1), 16), "one" if section and MULTI.match(body, m.end()) else "lead"
    vs = {int(v, 16) for v in FUN.findall(comment)}
    if len(vs) == 1:
        return vs.pop(), "one"
    return None, None


# evidence weights; a (va, name) pair needs SCORE_MIN to be emitted
W = {"trace": 10, "doc:strong": 10, "doc:lead": 7, "doc:one": 2, "proto:strong": 8, "proto:lead": 6, "proto:one": 2,
     "hdr": 6, "callsite": 3, "map": 6, "prose": 4, "exported": 1}
SCORE_MIN = 5


def scan():
    ev = defaultdict(lambda: defaultdict(list))   # va -> name -> [(rule, where)]
    defined = {}                                  # name -> (where, static)
    texts = {}
    for dp, _, fs in os.walk(SRC):
        for f in sorted(fs):
            if f.endswith((".c", ".h")):
                p = os.path.join(dp, f)
                texts[os.path.relpath(p, ROOT)] = open(p, encoding="utf-8", errors="replace").read()
    protos = []
    for rel, text in sorted(texts.items()):
        code, spans = blank(text)
        lineof = lambda o: text.count("\n", 0, o) + 1
        hdr_va = None
        if spans and code[:spans[0][0]].strip() == "":
            hc = text[spans[0][0]:spans[0][1]]
            m = STRONG.search(hc)
            hdr_va = int(m.group(1), 16) if m and not MULTI.match(hc, m.end()) else None
        hdr_done = False
        for name, sig, bopen, bend, prev, prev_item, decl in definitions(text, code):
            where = "%s:%d" % (rel, lineof(sig))
            static = bool(re.search(r"\bstatic\b", code[decl:sig]))
            got = (None, None)
            lead = comments_in(spans, prev, sig)
            if lead:
                ca, cb = lead[-1]
                # directly above the declaration, on its own line (a trailing comment belongs to
                # the previous line), with at most one blank line between
                if cb <= decl and own_line(code, ca) and code[cb:decl].strip() == "" and code.count("\n", cb, decl) <= 2:
                    got = doc_vas(text[ca:cb], section=code.count("\n", cb, decl) == 2)
            if bopen is None:           # prototype: evidence for the name if it is defined somewhere
                tr = trailing(code, spans, bend + 1)    # `void f(void);  /* FUN_x */`
                if tr:
                    tv = doc_vas(text[tr[0]:tr[1]])
                    if tv[0] and tv[1] != "one":
                        got = tv
                if got[0]:
                    protos.append((got[0], name, "proto:" + got[1], where))
                continue
            defined.setdefault(name, (where, static))
            if not got[0]:
                # first comment inside the body, before any statement
                inner = comments_in(spans, bopen + 1, bend)
                if inner and code[bopen + 1:inner[0][0]].strip() == "":
                    got = doc_vas(text[inner[0][0]:inner[0][1]])
            if got[0]:
                ev[got[0]][name].append(("doc:" + got[1], where))
            m = re.search(r"\bCALL_TRACE_ENTER(?:_STUB)?\(\s*0x([0-9a-fA-F]+)", text[bopen:bend])
            if m:                       # the runtime call-trace probe names the retail VA outright
                ev[int(m.group(1), 16)][name].append(("trace", where))
            if hdr_va and not static and not hdr_done:
                # the file header's "port of FUN_x" -> the file's first exported function
                ev[hdr_va][name].append(("hdr", where)); hdr_done = True
    for va, name, rule, where in protos:
        if name in defined:
            ev[va][name].append((rule, where))
    # votes: call-site comments and prose equations naming a defined port function
    names = set(defined)
    for rel, text in texts.items():
        for m in re.finditer(r"\b(" + IDENT + r")\s*\([^;{}]*\)\s*;\s*/[*/]\s*`?FUN_([0-9a-fA-F]{8})\b", text):
            pre = text[text.rfind("\n", 0, m.start()) + 1:m.start()].strip()
            if pre and pre not in ("return", "else") and re.fullmatch(r"[A-Za-z_][\w\s\*]*", pre):
                continue    # a prototype's trailing comment (counted as proto:*), not a call site
            if m.group(1) in names:
                ev[int(m.group(2), 16)][m.group(1)].append(("callsite", rel))
        for rule, pat in (("map", r"FUN_([0-9a-fA-F]{8})`?\s*(?:=|==|is|aka)\s*`?(" + IDENT + r")\b"),
                          ("map", r"FUN_([0-9a-fA-F]{8})[^\n→]{0,40}→\s*(?:internal\s+|static\s+)?`?(" + IDENT + r")\b"),
                          ("prose", r"FUN_([0-9a-fA-F]{8})`?\s*\(\s*`?(" + IDENT + r")`?\s*\)")):
            for m in re.finditer(pat, text):
                if m.group(2) in names:
                    ev[int(m.group(1), 16)][m.group(2)].append((rule, rel))
        for pat in (r"\b(" + IDENT + r")`?\s*(?:=|is)\s*(?:retail\s+)?`?FUN_([0-9a-fA-F]{8})\b",
                    r"\b(" + IDENT + r")`?\s*(?:\(\))?\s*\(\s*(?:retail\s+|port of\s+)?`?FUN_([0-9a-fA-F]{8})`?\s*[),@]"):
            for m in re.finditer(pat, text):
                if m.group(1) in names:
                    ev[int(m.group(2), 16)][m.group(1)].append(("prose", rel))
    return ev, defined


def resolve(ev, defined):
    """Score every (va, name); greedily give each VA its best name and each name one VA."""
    scored = []
    for va, by_name in ev.items():
        for name, items in by_name.items():
            score = sum(W[r] for r, _ in items) + (0 if defined[name][1] else W["exported"])
            best = max(items, key=lambda it: W[it[0]])
            scored.append((score, va, name, best[0], len(items), defined[name][0]))
    scored.sort(key=lambda t: (-t[0], t[1], t[2]))
    va_name, name_va = {}, {}
    for score, va, name, rule, votes, where in scored:
        if score < SCORE_MIN or va in va_name or name in name_va:
            continue
        va_name[va] = (name, rule, score, votes, where)
        name_va[name] = va
    return va_name, scored


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", default=OUT)
    ap.add_argument("--check", metavar="FUNCTIONS_IDA_TSV",
                    help="report VAs that are not function starts in this IDA export")
    ap.add_argument("-v", action="store_true", help="list the runner-up names per VA")
    a = ap.parse_args()
    ev, defined = scan()
    va_name, scored = resolve(ev, defined)
    with open(a.o, "w", encoding="utf-8") as fh:
        fh.write("# generated by tools/ida/gen_names.py from src/ FUN_<va> citations — do not edit;\n"
                 "# applied by ol2_ida_export.py (names=...) to non-library IDA function starts.\n")
        fh.write("va\tname\tbest_rule\tscore\tcites\tdefined_at\n")
        for va in sorted(va_name):
            name, rule, score, votes, where = va_name[va]
            fh.write("%08x\t%s\t%s\t%d\t%d\t%s\n" % (va, name, rule, score, votes, where))
    by = defaultdict(int)
    for v in va_name.values(): by[v[1]] += 1
    print("%s: %d names from %d cited VAs, %d port functions defined; best evidence: %s" % (
        os.path.relpath(a.o, ROOT), len(va_name), len(ev), len(defined),
        ", ".join("%s %d" % kv for kv in sorted(by.items(), key=lambda kv: -kv[1]))))
    if a.v:
        alt = defaultdict(list)
        for score, va, name, rule, votes, where in scored:
            if va_name.get(va, (None,))[0] != name:
                alt[va].append("%s(%s,%d)" % (name, rule, score))
        for va in sorted(alt):
            w = va_name.get(va)
            print("  %08x -> %s ; also: %s" % (va, "%s(%s,%d)" % (w[0], w[1], w[2]) if w else "-", ", ".join(alt[va][:6])))
    if a.check:
        starts = {int(r["va"], 16): r for r in csv.DictReader(open(a.check), delimiter="\t")}
        bad = [va for va in va_name if va not in starts]
        lib = [va for va in va_name if va in starts and starts[va]["flirt_lib"] == "1"]
        print("check vs %s: %d not function starts, %d FLIRT lib" % (a.check, len(bad), len(lib)))
        for va in sorted(bad):
            print("  not a start: %08x %s (%s)" % (va, va_name[va][0], va_name[va][4]))
        for va in sorted(lib):
            print("  lib: %08x %s = %s" % (va, va_name[va][0], starts[va]["name"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
