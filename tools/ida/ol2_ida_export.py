# ol2_ida_export.py — IDA 9.x headless analysis + export of Lords2.exe (GOG 2.0.0.3).
#
# Run on Windows via tools/ida/run_ida.ps1 (staged to C:\ol2-ida\):
#   idat -A -L<log> -Ool2:<out_dir>[;names=<names.tsv>][;noasm][;keepnames] -S<this> <exe|i64>
#
# Phases (see tools/ida/README.md):
#   1. analysis fixups (idempotent; a cached .i64 passes through unchanged):
#      - GOG patch: code appended to .text's raw data PAST its VirtualSize (vsize 0xceb06,
#        raw 0xcec00): 0x4050f0 `jmp 0x4cfb08` -> 16 ms GetTickCount gate -> `jmp 0x4050f6`,
#        globals 0x5ceff8/0x5ceffc in .data's page slack.  Segments are checked against the
#        PE headers and extended/filled from the file if the loader truncated them (IDA 9.4
#        does not); the stub + jump-back target become tail chunks of the detouring function.
#      - functions IDA missed: create one at every `55 8B EC` in undefined .text bytes.
#      - FLIRT false positives below the CRT (first import thunk): lib flag/name/type cleared.
#   2. the analysed DB is saved: <exe>.i64 is the cache, holding ONLY IDA's analysis + fixups.
#   3. port names (names=…, tools/ida/names.tsv: va<TAB>name) applied to non-library
#      function starts, IN MEMORY: the session exits without saving (DBFL_KILL) unless
#      `keepnames`, so the cache never accumulates stale names.
#   4. export (derived from the retail binary — never commit):
#        all_ida.c          Hex-Rays pseudocode, every function, header line
#                           "/* ===== name @ 0xVA (N bytes) FUN_VA [tags] ===== */"
#        all_ida.asm        disassembly per function ("; ===== …" header, VA-prefixed lines)
#        functions_ida.tsv  va size name flirt_lib cc nargs callers callees proto name_src chunks addr_taken
#        data_xrefs.tsv     target_va target_name func_va insn_va kind(r|w|o)
#        strings_ida.tsv    va len text(escaped) referencing funcs
#        export_summary.txt counts + timings
import os, time, struct
import idaapi, ida_auto, ida_hexrays, ida_funcs, ida_pro, idautils, ida_name, idc
import ida_bytes, ida_ua, ida_segment, ida_xref, ida_typeinf, ida_nalt, ida_lines, ida_loader

T0 = time.time()


def _opts():
    raw = idaapi.get_plugin_options("ol2") or r"C:\ol2-ida\out"
    parts = raw.split(";")
    o = {"out": parts[0]}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1); o[k] = v
        elif p:
            o[p] = True
    return o


O = _opts()
OUT = O["out"]
os.makedirs(OUT, exist_ok=True)
LOG = open(os.path.join(OUT, "export_log.txt"), "w", encoding="utf-8")


def log(*a):
    LOG.write(" ".join(str(x) for x in a) + "\n"); LOG.flush()


def segs():
    return [(ida_segment.get_segm_name(s), s.start_ea, s.end_ea)
            for s in (ida_segment.getnseg(i) for i in range(ida_segment.get_segm_qty()))]


# ---------------------------------------------------------------- 1. loader fixup
def pe_sections(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    nsec, optsz = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    salign = struct.unpack_from("<I", d, pe + 24 + 32)[0]
    out = []
    o = pe + 24 + optsz
    for i in range(nsec):
        n, vs, va, rs, rp = struct.unpack_from("<8sIIII", d, o + 40 * i)
        out.append((n.rstrip(b"\0").decode("ascii", "replace"), base + va, vs, rs, rp))
    return d, base, salign, out


def input_exe():
    for p in (ida_nalt.get_input_file_path(),
              os.path.splitext(ida_loader.get_path(ida_loader.PATH_TYPE_IDB))[0]):
        if p and os.path.isfile(p) and not p.lower().endswith((".i64", ".idb")):
            return p
    return None


def walk_stub(stub, lo, hi):
    """Create insns along the stub's straight-line body; return (end, jump-back targets)."""
    p, backs = stub, []
    while p < hi:
        insn = ida_ua.insn_t()
        n = ida_ua.decode_insn(insn, p)
        if n <= 0:
            break
        ida_ua.create_insn(p)
        if insn.itype == idaapi.NN_jmp:
            if insn.ops[0].type == ida_ua.o_near and not (lo <= insn.ops[0].addr < hi):
                backs.append(insn.ops[0].addr)
            p += n; break
        if insn.itype in (idaapi.NN_retn, idaapi.NN_retf):
            p += n; break
        p += n
    return p, backs


FIXUP = {"extended": [], "stubs": []}


def loader_fixup():
    """Map sections like the Windows loader; make patch stubs code. Idempotent."""
    exe = input_exe()
    if not exe:
        log("fixup: input exe not found; skipped"); return
    data, base, salign, secs = pe_sections(exe)
    log("fixup: input %s" % exe)
    tails = []   # (lo, hi): executable raw bytes past a section's VirtualSize
    for i, (name, va, vs, rs, rp) in enumerate(secs):
        s = ida_segment.getseg(va)
        if not s or s.start_ea != va:
            log("  %-8s %08x: no IDA segment starts here; skipped" % (name, va)); continue
        want = va + ((max(vs, rs) + salign - 1) // salign) * salign
        if i + 1 < len(secs):
            want = min(want, secs[i + 1][1])
        nxt = ida_segment.get_next_seg(va)
        if nxt:
            want = min(want, nxt.start_ea)
        old = s.end_ea
        log("  %-8s va=%08x vsize=%05x raw=%05x  ida_end=%08x page_end=%08x" % (name, va, vs, rs, old, want))
        raw_hi = min(va + rs, want)
        if want > old:
            if not ida_segment.set_segm_end(va, want, ida_segment.SEGMOD_KEEP | ida_segment.SEGMOD_SILENT):
                log("  !! set_segm_end failed for", name); continue
            if raw_hi > old:
                # copy the raw bytes the loader dropped; the rest of the page stays uninitialised (bss-like)
                ida_bytes.put_bytes(old, data[rp + (old - va): rp + (raw_hi - va)])
            FIXUP["extended"].append("%s:%x->%x(raw<%x)" % (name, old, want, raw_hi))
        if (s.perm & ida_segment.SEGPERM_EXEC or name == ".text") and raw_hi > va + vs:
            tails.append((va + vs, raw_hi))   # executable raw bytes past VirtualSize
    # a stub = code in such a tail reached by a rel32 jmp/call from analysed code
    for lo, hi in tails:
        seg = ida_segment.getseg(lo)
        blob = ida_bytes.get_bytes(seg.start_ea, lo - seg.start_ea) or b""
        refs = []
        for k in range(len(blob) - 5):
            if blob[k] in (0xE8, 0xE9):
                t = (seg.start_ea + k + 5 + struct.unpack_from("<i", blob, k + 1)[0]) & 0xffffffff
                if lo <= t < hi and ida_bytes.is_code(ida_bytes.get_flags(seg.start_ea + k)):
                    refs.append((seg.start_ea + k, t, blob[k]))
        for src, stub, op in refs:
            end, backs = walk_stub(stub, lo, hi)
            FIXUP["stubs"].append({"src": src, "stub": stub, "end": end, "backs": backs, "op": "%02x" % op})
            ida_auto.plan_range(src, src + 5)
            ida_auto.plan_range(stub, end)
    ida_auto.auto_wait()
    for st in FIXUP["stubs"]:
        place_stub(st)


def place_stub(st):
    """Give the stub a home.  A jmp-detour is part of the detouring function: the stub and
    its jump-back target become tail chunks of it, so Hex-Rays sees ONE body with its frame
    (the jump-back target has no prologue of its own).  Ghidra's split into three functions
    decompiles to `JUMPOUT(0x4050F6)` and loses the render body.  A call-stub gets its own
    function."""
    src, stub, backs = st["src"], st["stub"], st["backs"]
    host = ida_funcs.get_func(src)
    if st["op"] == "e8" or not host:
        if not ida_funcs.get_func(stub):
            ida_funcs.add_func(stub)
        return
    cur = ida_funcs.get_func(stub)
    # drop separate functions at the stub / jump-back targets that exist only
    # because of the detour, then let IDA re-trace the host through the stub
    hs = host.start_ea
    if cur and cur.start_ea == hs:
        return       # already placed (cached DB)
    for ea in [stub] + backs:
        f = ida_funcs.get_func(ea)
        if f and f.start_ea == ea and f.start_ea != hs:
            others = [x for x in idautils.CodeRefsTo(ea, 0)
                      if not (st["stub"] <= x < st["end"]) and x != src
                      and not (ida_funcs.get_func(x) and ida_funcs.get_func(x).start_ea == ea)]
            if others:
                log("  keep function %x: other refs %s" % (ea, ",".join("%x" % x for x in others[:8])))
                continue
            ida_funcs.del_func(ea)
    ida_funcs.del_func(hs)
    ida_funcs.add_func(hs)
    ida_auto.auto_wait()
    f = ida_funcs.get_func(stub)
    if not f or f.start_ea != hs:
        ida_funcs.append_func_tail(ida_funcs.get_func(hs), stub, st["end"])


def annotate_stubs():
    for st in FIXUP["stubs"]:
        note = ("GOG 2.0.0.3 patch %s 0x%x -> stub 0x%x..0x%x%s.  The stub sits in .text raw data past "
                "VirtualSize (the Windows loader maps it); ol2_ida_export.py attaches it here as a tail "
                "chunk." % ("jmp-detour" if st["op"] == "e9" else "call",
                st["src"], st["stub"], st["end"],
                ", jumps back to " + ",".join("0x%x" % b for b in st["backs"]) if st["backs"] else ""))
        seen = set()
        for ea in [st["src"], st["stub"]] + st["backs"]:
            f = ida_funcs.get_func(ea)
            if f and f.start_ea not in seen:
                seen.add(f.start_ea)
                ida_funcs.set_func_cmt(f, note, False)   # non-repeatable: keep it off call sites
        idc.set_cmt(st["stub"], "GOG patch stub (from 0x%x)" % st["src"], 0)
        st["owner"] = ["%x" % ida_funcs.get_func(ea).start_ea if ida_funcs.get_func(ea) else "-"
                       for ea in [st["src"], st["stub"]] + st["backs"]]


def crt_start():
    """First import thunk (`jmp [IAT]`) in .text: MSVC links game objs first, then the
    libs (import thunks + the debug CRT) — everything below is game code."""
    if O.get("crt_lo"):
        return int(O["crt_lo"], 16)
    idata = [s for s in (ida_segment.getnseg(i) for i in range(ida_segment.get_segm_qty()))
             if ida_segment.get_segm_name(s) == ".idata"]
    for ea in idautils.Functions():
        insn = ida_ua.insn_t()
        if ida_ua.decode_insn(insn, ea) and insn.itype == idaapi.NN_jmpni and insn.ops[0].type == ida_ua.o_mem \
                and any(s.start_ea <= insn.ops[0].addr < s.end_ea for s in idata):
            return ea
    return TEXT_SEG.end_ea


def recover_prologue_funcs(lo, hi):
    """MSVC 4.2 /Od-style frames: every game function starts `push ebp; mov ebp, esp`.
    IDA misses the ones reached only through data (handler tables etc.); Ghidra finds
    them.  Create a function at every 55 8B EC in undefined (non-data) .text bytes."""
    total = 0
    for _ in range(8):
        blob = ida_bytes.get_bytes(lo, hi - lo) or b""
        added = 0
        k = blob.find(b"\x55\x8b\xec")
        while k >= 0:
            ea = lo + k
            fl = ida_bytes.get_flags(ea)
            if not ida_funcs.get_func(ea) and not ida_bytes.is_tail(fl) and not ida_bytes.is_data(fl):
                if ida_funcs.add_func(ea):
                    added += 1
            k = blob.find(b"\x55\x8b\xec", k + 1)
        ida_auto.auto_wait()
        total += added
        if not added:
            break
    return total


def unflirt(lo):
    """FLIRT (VC 2-14 runtime sigs) false-positives on tiny game functions below the CRT:
    empty `push ebp..leave; ret` bodies, global-zeroing inits named __cfltcvt_init, etc.
    Clear FUNC_LIB, drop the FLIRT name/type, and undo any noreturn it implied."""
    out = []
    for ea in list(idautils.Functions(TEXT_SEG.start_ea, lo)):
        f = ida_funcs.get_func(ea)
        if not (f.flags & ida_funcs.FUNC_LIB):
            continue
        nm = ida_name.get_name(ea)
        noret = bool(f.flags & ida_funcs.FUNC_NORET)
        f.flags &= ~(ida_funcs.FUNC_LIB | ida_funcs.FUNC_NORET)
        ida_funcs.update_func(f)
        if not nm.startswith("sub_"):
            ida_name.set_name(ea, "", ida_name.SN_NOWARN)
            ida_nalt.del_tinfo(ea)
        if noret:
            for x in idautils.CodeRefsTo(ea, 0):
                g = ida_funcs.get_func(x)
                if g:
                    ida_auto.plan_range(g.start_ea, g.end_ea)
        out.append("%x:%s%s" % (ea, nm, ":noret" if noret else ""))
    ida_auto.auto_wait()
    return out


TEXT_SEG = ida_segment.get_segm_by_name(".text")
IDB = ida_loader.get_path(ida_loader.PATH_TYPE_IDB)
db_was_new = not os.path.exists(IDB)
log("idb %s fresh=%s" % (IDB, db_was_new))
log("segments@load:", ["%s %x-%x" % s for s in segs()])
ida_auto.auto_wait()
loader_fixup()
ida_auto.auto_wait()
CRT_LO = crt_start()
FIXUP["crt_lo"] = "%x" % CRT_LO
FIXUP["prologue_funcs"] = recover_prologue_funcs(TEXT_SEG.start_ea, TEXT_SEG.end_ea)
UNFLIRTED = unflirt(CRT_LO)
FIXUP["unflirted"] = len(UNFLIRTED)
log("unflirted (FLIRT false positives below the CRT):", " ".join(UNFLIRTED))
annotate_stubs()
T_AUTO = time.time() - T0
log("fixup:", FIXUP)
log("auto_wait done %.1fs" % T_AUTO)
log("segments@analysed:", ["%s %x-%x" % s for s in segs()])

# ---------------------------------------------------------------- 2. save the analysed cache
ok = ida_loader.save_database(IDB, 0)
log("saved analysed DB %s ok=%s" % (IDB, ok))

# ---------------------------------------------------------------- 3. port names (in memory)
TEXT = ida_segment.get_segm_by_name(".text")
TEXT_LO, TEXT_HI = (TEXT.start_ea, TEXT.end_ea) if TEXT else (0, 0)
IMG_LO, IMG_HI = idaapi.inf_get_min_ea(), idaapi.inf_get_max_ea()
PORT = {}   # va -> name we applied
N = {"names_in": 0, "applied": 0, "lib_skip": 0, "not_func": 0, "inside_func": 0, "chunk_labels": 0, "failed": 0}
if O.get("names"):
    for line in open(O["names"], encoding="utf-8"):
        line = line.rstrip("\r\n")
        if not line or line.startswith("#") or line.startswith("va\t"):
            continue
        va, nm = line.split("\t")[:2]
        ea = int(va, 16); N["names_in"] += 1
        f = ida_funcs.get_func(ea)
        if not f:
            N["not_func"] += 1; log("name: %x %s not in any function" % (ea, nm)); continue
        if f.start_ea != ea:
            N["inside_func"] += 1
            tail = ida_funcs.get_fchunk(ea)
            if tail and tail.start_ea == ea and ida_name.set_name(ea, nm, ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE):
                N["chunk_labels"] += 1   # e.g. a detour's jump-back target: label the tail chunk
            log("name: %x %s is inside %s @ %x (+%x)%s" % (ea, nm, ida_name.get_name(f.start_ea), f.start_ea,
                ea - f.start_ea, " -> chunk label" if tail and tail.start_ea == ea else ""))
            continue
        if f.flags & ida_funcs.FUNC_LIB:
            N["lib_skip"] += 1; log("name: %x %s kept FLIRT name %s" % (ea, nm, ida_name.get_name(ea))); continue
        if ida_name.set_name(ea, nm, ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE):
            PORT[ea] = ida_name.get_name(ea); N["applied"] += 1
        else:
            N["failed"] += 1; log("name: set_name failed %x %s" % (ea, nm))
    log("names:", N)

# ---------------------------------------------------------------- 4. export
def fn_cc(ea):
    tif = ida_typeinf.tinfo_t()
    if not ida_nalt.get_tinfo(tif, ea):
        if not ida_typeinf.guess_tinfo(tif, ea):
            return "", -1, ""
    try:
        ccv = tif.get_cc() & ida_typeinf.CM_CC_MASK
    except Exception:
        ccv = -1
    cc = {ida_typeinf.CM_CC_CDECL: "cdecl", ida_typeinf.CM_CC_STDCALL: "stdcall",
          ida_typeinf.CM_CC_THISCALL: "thiscall", ida_typeinf.CM_CC_FASTCALL: "fastcall",
          ida_typeinf.CM_CC_PASCAL: "pascal"}.get(ccv, "cc%x" % (ccv & 0xff))
    try:
        nargs = tif.get_nargs()
    except Exception:
        nargs = -1
    return cc, nargs, ida_typeinf.print_tinfo("", 0, 0, ida_typeinf.PRTYPE_1LINE, tif, ida_name.get_name(ea), "") or ""


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


STRS = {}
for s in idautils.Strings():
    try:
        STRS[s.ea] = str(s)
    except Exception:
        pass


def in_image(v):
    return IMG_LO <= v < IMG_HI


def chunks(f):
    out = []
    it = ida_funcs.func_tail_iterator_t(f)
    ok = it.first()
    while ok:
        c = it.chunk()
        if c.start_ea != f.start_ea:
            out.append((c.start_ea, c.end_ea, ida_name.get_name(c.start_ea) if ida_bytes.has_user_name(ida_bytes.get_flags(c.start_ea)) else ""))
        ok = it.next()
    return out


ida_hexrays.init_hexrays_plugin()
nf = nlib = 0; fails = []; t1 = time.time()
fc = open(os.path.join(OUT, "all_ida.c"), "w", encoding="utf-8")
fa = None if O.get("noasm") else open(os.path.join(OUT, "all_ida.asm"), "w", encoding="utf-8")
ft = open(os.path.join(OUT, "functions_ida.tsv"), "w", encoding="utf-8")
fx = open(os.path.join(OUT, "data_xrefs.tsv"), "w", encoding="utf-8")
ft.write("va\tsize\tname\tflirt_lib\tcc\tnargs\tcallers\tcallees\tproto\tname_src\tchunks\taddr_taken\n")
fx.write("target_va\ttarget_name\tfunc_va\tinsn_va\tkind\n")
str_refs = {}
for ea in idautils.Functions():
    f = ida_funcs.get_func(ea); name = ida_name.get_name(ea)
    lib = 1 if f.flags & ida_funcs.FUNC_LIB else 0
    nlib += lib
    ch = chunks(f)
    size = (f.end_ea - f.start_ea) + sum(e - b for b, e, _ in ch)   # all chunks
    callers = sorted({ida_funcs.get_func(x).start_ea for x in idautils.CodeRefsTo(ea, 0) if ida_funcs.get_func(x)})
    callees = set()
    asm = []
    for item in idautils.FuncItems(ea):
        for x in idautils.CodeRefsFrom(item, 0):
            g = ida_funcs.get_func(x)
            if g and g.start_ea != ea and x == g.start_ea:
                callees.add(x)
        for xr in idautils.XrefsFrom(item, ida_xref.XREF_DATA):
            to = xr.to
            if not in_image(to) or (TEXT_LO <= to < TEXT_HI and ida_funcs.get_func(to)):
                continue
            kind = {ida_xref.dr_R: "r", ida_xref.dr_W: "w"}.get(xr.type, "o")
            fx.write("%08x\t%s\t%08x\t%08x\t%s\n" % (to, ida_name.get_name(to), ea, item, kind))
            if to in STRS:
                str_refs.setdefault(to, set()).add(ea)
        if fa is not None:
            asm.append("%08x  %s\n" % (item, ida_lines.tag_remove(idc.generate_disasm_line(item, 0) or "")))
    cc, nargs, proto = fn_cc(ea)
    src = "flirt" if lib else ("port" if ea in PORT else ("ida" if not ida_name.get_name(ea).startswith("sub_") else ""))
    chs = ",".join("%x-%x%s" % (b, e, ":" + n if n else "") for b, e, n in ch)
    taken = sorted({x for x in idautils.DataRefsTo(ea)})
    ft.write("%08x\t%d\t%s\t%d\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\n" % (ea, size, name, lib, cc, nargs,
             ",".join("%x" % c for c in callers), ",".join("%x" % c for c in sorted(callees)), proto, src, chs,
             ",".join("%x" % x for x in taken[:32])))
    tags = ["FUN_%08x" % ea]          # the port/ledger's token for this VA, for grep
    if lib: tags.append("[FLIRT lib]")
    if ea in PORT: tags.append("[port name]")
    if ch: tags.append("[chunks %s]" % chs)
    hdr = "%s @ 0x%08x (%d bytes) %s" % (name, ea, size, " ".join(tags))
    if fa is not None:
        fa.write("; ===== %s =====\n" % hdr); fa.writelines(asm); fa.write("\n")
    fc.write("/* ===== %s ===== */\n" % hdr)
    try:
        fc.write(str(ida_hexrays.decompile(ea)) + "\n\n")
    except Exception as e:
        fails.append(ea); fc.write("/* decompile failed: %r */\n\n" % e)
    nf += 1
for h in (fc, ft, fx):
    h.close()
if fa is not None:
    fa.close()
with open(os.path.join(OUT, "strings_ida.tsv"), "w", encoding="utf-8") as fs:
    fs.write("va\tlen\ttext\tfuncs\n")
    for va in sorted(STRS):
        fs.write("%08x\t%d\t%s\t%s\n" % (va, len(STRS[va]), esc(STRS[va]), ",".join("%x" % x for x in sorted(str_refs.get(va, ())))))
with open(os.path.join(OUT, "export_summary.txt"), "w") as fh:
    fh.write("input=%s md5=%s sha256=%s\n" % (ida_nalt.get_root_filename(), ida_nalt.retrieve_input_file_md5().hex(),
                                              ida_nalt.retrieve_input_file_sha256().hex()))
    fh.write("functions=%d flirt_lib=%d port_named=%d failed=%d fresh=%s auto_s=%.1f export_s=%.1f total_s=%.1f\n" % (
        nf, nlib, len(PORT), len(fails), db_was_new, T_AUTO, time.time() - t1, time.time() - T0))
    fh.write("names=%s\n" % N)
    fh.write("fixup=%s\n" % {k: (["stub %x..%x from %s@%x back=%s owner=%s" % (st["stub"], st["end"],
             "jmp" if st["op"] == "e9" else "call", st["src"], ",".join("%x" % b for b in st["backs"]),
             "/".join(st.get("owner", []))) for st in v] if k == "stubs" else v) for k, v in FIXUP.items()})
    fh.write("failed_vas=%s\n" % ",".join("%x" % x for x in fails))
log("done %.1fs" % (time.time() - T0))
LOG.close()
if not O.get("keepnames"):
    # the cache (.i64) was saved above with IDA's analysis only; drop this session's port names
    ida_loader.set_database_flag(ida_loader.DBFL_KILL)
ida_pro.qexit(0)
