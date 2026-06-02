#!/usr/bin/env python3
"""
tools/mem_watch.py — Phase H memory-access watch driver.

Finds the *writer* of an engine memory region we can't locate by reading the
decompile (reached via indirect dispatch, or dropped from Ghidra's output).
Frida's MemoryAccessMonitor write-protects the region in retail; every access
traps with the faulting instruction's address. That address → the owning engine
function (via the port ledger) → the chip to port.

This is the unblock path for a render gap where the walker/draw reads a region
whose filler isn't ported, so it draws stale/zero. Point mem_watch at that
region, drive retail into the scene, and read off the writer VA.

Usage:

    tools/mem_watch.py \\
        --run-dir runs/memwatch-foo \\
        --region 0x573d...:64:some_field \\
        [--input-trace tests/scenarios/<scene>/trace.jsonl] \\
        [--access w] [--duration-ms 120000]

  --region VA:SIZE[:LABEL]   repeatable. VA is a Ghidra VA (ImageBase
                             0x00400000). SIZE in bytes. Page-granular —
                             a tight SIZE still watches the enclosing page(s);
                             keep it small to limit hot-page noise.

  --input-trace FILE         optional: drive retail into the target scene by
                             replaying a recorded input trace (PostMessageA).
                             Omit for init-time / boot-time writers.

The agent arms the monitor pre-resume (before the engine runs), so an init-time
writer during scene bootstrap is trapped on its first write.

Hot-page workarounds (MemoryAccessMonitor is page-granular + op-blind, so a
write inside a page that is READ every frame burns the re-arm budget on
neighbour reads before the write lands — see docs/findings/mem-watch.md #2):

  --arm-between ENTER:LEAVE   (lever 1, best) arm only inside a function window,
                             so the budget competes with neighbour reads ONLY
                             during the suspect call. ENTER may equal LEAVE.
  --snapshot-region VA:SIZE  (lever 2) poll + diff per tick instead of trapping;
                             op-agnostic, immune to read heat. Reports the
                             writer's TICK (then call-trace that tick). Misses
                             an intra-tick transient write.
  --rearm-cap N              (lever 4) raise the re-arm budget for a short window.

What does NOT help: a tighter --region. The monitor protects the whole 4 KiB
page regardless of region size, so neighbour-read heat is unchanged.

Output:
    <run_dir>/mem_watch.jsonl    one row per trapped access (--region)
    <run_dir>/mem_snapshot.jsonl one row per (region,tick) that changed (--snapshot-region)
    <run_dir>/agent.log          Frida send(log)/errors
    stdout                       a single JSON object: ranked writer table
                                 (each writer → owning engine function + port
                                 status), plus a "snapshot" block of change-ticks
                                 + offsets per snapshot region.
"""

from __future__ import annotations

import argparse
import bisect
import json
import sys
from pathlib import Path

# Reuse the capture plumbing (spawn / turbo / leak-proofing) wholesale.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida_capture as fc  # noqa: E402

ROOT          = fc.ROOT
LEDGER_JSON   = ROOT / "docs" / "port-ledger.json"
FUNCTIONS_CSV = ROOT / "docs" / "decompiled" / "functions.csv"


# ─── ledger / function-table lookup ────────────────────────────────────────


def _va_int(v) -> int:
    return int(v, 16) if isinstance(v, str) else int(v)


def _load_function_index() -> list[tuple[int, int, dict]]:
    """Sorted [(start_va, end_va, info)] of every engine function, so a
    faulting instruction VA maps to its containing function by binary search.

    Prefers port-ledger.json (carries port status + src); falls back to
    functions.csv (name/size only)."""
    intervals: list[tuple[int, int, dict]] = []
    if LEDGER_JSON.exists():
        ledger = json.loads(LEDGER_JSON.read_text())
        for fn in ledger.get("functions", []):
            start = _va_int(fn["va"])
            size  = int(fn.get("size", 0)) or 1
            intervals.append((start, start + size, {
                "name":   fn.get("name", ""),
                "status": fn.get("status", "unknown"),
                "src":    fn.get("src", []),
            }))
    elif FUNCTIONS_CSV.exists():
        import csv
        with FUNCTIONS_CSV.open() as f:
            for row in csv.DictReader(f):
                start = int(row["entry"], 16)
                size  = int(row["size"]) or 1
                intervals.append((start, start + size, {
                    "name":   row["name"],
                    "status": "unknown",
                    "src":    [],
                }))
    else:
        raise SystemExit(
            f"neither {LEDGER_JSON} nor {FUNCTIONS_CSV} found — "
            f"regenerate the ledger with tools/gen_port_ledger.py")

    intervals.sort(key=lambda iv: iv[0])
    return intervals


def _owner_of(va: int, intervals: list[tuple[int, int, dict]]) -> dict | None:
    """The function whose [start, end) contains `va`, or None (CRT / library)."""
    starts = [iv[0] for iv in intervals]
    i = bisect.bisect_right(starts, va) - 1
    if i < 0:
        return None
    start, end, info = intervals[i]
    if start <= va < end:
        out = dict(info)
        out["func_va"]  = f"0x{start:08x}"
        out["insn_off"] = va - start
        return out
    return None


# ─── region-spec parsing ────────────────────────────────────────────────────


def _parse_region(spec: str) -> dict:
    """`VA:SIZE[:LABEL]` → {va, size, label}."""
    parts = spec.split(":")
    if len(parts) < 2:
        raise argparse.ArgumentTypeError(
            f"--region expects VA:SIZE[:LABEL], got {spec!r}")
    va   = int(parts[0], 0)
    size = int(parts[1], 0)
    label = parts[2] if len(parts) >= 3 and parts[2] else f"0x{va:08x}"
    return {"va": va, "size": size, "label": label}


def _parse_arm_between(spec: str) -> dict:
    """`ENTER:LEAVE` → {enter, leave} (Ghidra VAs). ENTER may equal LEAVE."""
    parts = spec.split(":")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError(
            f"--arm-between expects ENTER_VA:LEAVE_VA, got {spec!r}")
    return {"enter": int(parts[0], 0), "leave": int(parts[1], 0)}


# ─── post-processing: rank trapped accesses by faulting instruction ────────


def _rank(run_dir: Path, regions: list[dict]) -> dict:
    jsonl = run_dir / "mem_watch.jsonl"
    intervals = _load_function_index()
    region_by_idx = {i: r for i, r in enumerate(regions)}

    by_from: dict[int, dict] = {}
    total = 0
    if jsonl.exists():
        for line in jsonl.read_text().splitlines():
            line = line.strip()
            if not line:
                continue
            ev = json.loads(line)
            total += 1
            frm = int(ev["from"])
            g = by_from.setdefault(frm, {
                "from": frm, "ops": {}, "n": 0,
                "first_frame": ev.get("frame"),
                "regions": set(), "offsets": set(),
            })
            g["n"] += 1
            g["ops"][ev["op"]] = g["ops"].get(ev["op"], 0) + 1
            ridx = int(ev.get("region", 0))
            g["regions"].add(ridx)
            base = region_by_idx.get(ridx, {}).get("va")
            if base is not None:
                g["offsets"].add(int(ev["addr"]) - base)

    writers = []
    for frm, g in sorted(by_from.items(), key=lambda kv: -kv[1]["n"]):
        owner = _owner_of(frm, intervals)
        offs = sorted(g["offsets"])
        writers.append({
            "from_va":      f"0x{frm:08x}",
            "n_accesses":   g["n"],
            "ops":          g["ops"],
            "first_frame":  g["first_frame"],
            "regions":      sorted(g["regions"]),
            "sample_offsets": [(f"+0x{o:x}" if o >= 0 else f"-0x{-o:x}")
                               for o in offs[:12]],
            "owner_func":   owner["func_va"] if owner else None,
            "owner_name":   owner["name"]    if owner else None,
            "owner_status": owner["status"]  if owner else "unmapped",
            "owner_src":    owner["src"]     if owner else [],
            "insn_offset":  (f"+0x{owner['insn_off']:x}" if owner else None),
        })

    # The chip(s) to port = writers whose owning function isn't already
    # ported. Surface them at the top.
    candidates = [w for w in writers
                  if w["owner_status"] in ("unported", "stubbed", "unmapped")]
    return {
        "total_accesses":   total,
        "distinct_writers": len(writers),
        "regions": [{"index": i, **r} for i, r in enumerate(regions)],
        "writers": writers,
        "port_candidates": candidates,
    }


def _snapshot_summary(run_dir: Path, snap_regions: list[dict]) -> dict:
    """Summarise mem_snapshot.jsonl: per region, the ticks at which it changed
    and the union of changed byte offsets — localises the writer's TICK.  Pair
    with a call-trace over that tick (or --arm-between bracketing the suspect
    call) to pin the faulting instruction."""
    jsonl = run_dir / "mem_snapshot.jsonl"
    per_region: dict[int, dict] = {}
    total = 0
    if jsonl.exists():
        for line in jsonl.read_text().splitlines():
            line = line.strip()
            if not line:
                continue
            ev = json.loads(line)
            total += 1
            ridx = int(ev.get("region", 0))
            g = per_region.setdefault(ridx, {
                "label": ev.get("label", ""),
                "va": int(ev.get("va", 0)),
                "change_ticks": [], "offsets": set(),
            })
            g["change_ticks"].append(int(ev.get("tick", 0)))
            for off, _old, _new in ev.get("changes", []):
                g["offsets"].add(int(off))
    out = []
    for i, r in enumerate(snap_regions):
        g = per_region.get(i)
        out.append({
            "index": i, "label": r["label"],
            "va": f"0x{r['va']:08x}", "size": r["size"],
            "change_events": len(g["change_ticks"]) if g else 0,
            "first_change_tick": min(g["change_ticks"]) if g and g["change_ticks"] else None,
            "last_change_tick":  max(g["change_ticks"]) if g and g["change_ticks"] else None,
            "changed_offsets": [f"+0x{o:x}" for o in sorted(g["offsets"])[:24]] if g else [],
        })
    return {"total_change_events": total, "snapshot_regions": out}


# ─── cli ────────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", type=Path, required=True,
                    help="where to write mem_watch.jsonl + agent.log")
    ap.add_argument("--region", type=_parse_region, action="append",
                    metavar="VA:SIZE[:LABEL]",
                    help="trapped watch region (Ghidra VA). Repeatable. "
                         "At least one --region or --snapshot-region is required.")
    ap.add_argument("--access", choices=("w", "rw"), default="w",
                    help="metadata only — MemoryAccessMonitor is op-blind and "
                         "traps every first-touch; the ranker separates "
                         "read/write. Kept for documentation of intent.")
    ap.add_argument("--no-precise", action="store_true",
                    help="raw one-shot-per-page mode. By default the agent "
                         "re-arms on page-neighbor traps and records only "
                         "in-region accesses. Disable to see raw page traffic.")
    ap.add_argument("--rearm-cap", type=int, default=0, metavar="N",
                    help="lever 4 — precise-mode re-arm budget (0 = agent "
                         "default 8000). Raise when a short hot window just "
                         "overshoots the budget; pair with --arm-between.")
    ap.add_argument("--arm-between", type=_parse_arm_between, default=None,
                    metavar="ENTER_VA:LEAVE_VA",
                    help="lever 1 — arm the monitor ONLY between ENTER's onEnter "
                         "and LEAVE's onLeave (Ghidra VAs; ENTER may equal "
                         "LEAVE). Collapses a read-hot page's neighbour traffic "
                         "to the suspect call window so the budget survives to "
                         "the write. The high-value fix for a write inside a "
                         "page that is read every frame.")
    ap.add_argument("--snapshot-region", type=_parse_region, action="append",
                    metavar="VA:SIZE[:LABEL]",
                    help="lever 2 — poll this region each tick and report the "
                         "ticks it changed (op-agnostic, immune to page-read "
                         "heat). Localises the writer's TICK, not the faulting "
                         "instruction; misses an intra-tick transient write. "
                         "Repeatable. No page-fault cost.")
    ap.add_argument("--input-trace", type=Path, default=None,
                    help="drive retail into the target scene by replaying this "
                         "input trace (omit for boot/init-time writers)")
    ap.add_argument("--input-segtrace", type=Path, default=None,
                    help="anchor-segmented input trace (TAS format) to drive the "
                         "target scene; pairs with --rng-seed for deterministic "
                         "new-game capture")
    ap.add_argument("--rng-seed", nargs="?", const="canonical", default=None,
                    metavar="A[,B[,SINGLE]]",
                    help="pin the RNG seed (bare = retail-canonical) so a "
                         "new-game capture is reproducible across targets")
    ap.add_argument("--skip-intro", action="store_true",
                    help="skip the intro movie chain (reach the menu fast); "
                         "implied when driving a menu→scene segtrace")
    ap.add_argument("--remote", default=fc.DEFAULT_REMOTE,
                    help="frida-server host:port (default %(default)s)")
    ap.add_argument("--exe", type=Path, default=fc.RETAIL_EXE)
    ap.add_argument("--cwd", type=Path, default=fc.ASSET_CWD)
    ap.add_argument("--duration-ms", type=int, default=120_000,
                    help="wall-clock ceiling (default %(default)s)")
    ap.add_argument("--no-turbo", action="store_true",
                    help="don't virtualise the frame clock")
    ap.add_argument("--no-auto-start", action="store_true",
                    help="skip auto-launching frida-server.exe")
    ap.add_argument("--analyze-only", action="store_true",
                    help="skip the capture; just re-rank an existing "
                         "<run_dir>/mem_watch.jsonl")
    args = ap.parse_args(argv)

    if not args.region and not args.snapshot_region:
        ap.error("at least one --region or --snapshot-region is required")

    regions = [{**r, "access": args.access} for r in (args.region or [])]
    snap_regions = list(args.snapshot_region or [])
    args.run_dir.mkdir(parents=True, exist_ok=True)

    # Bare --rng-seed → canonical ({}), else parse A[,B[,SINGLE]].
    rng_seed: dict | None = None
    if args.rng_seed is not None:
        if args.rng_seed == "canonical":
            rng_seed = {}
        else:
            rng_seed = {}
            for k, tok in zip(("a", "b", "single"), args.rng_seed.split(",")):
                tok = tok.strip()
                if tok:
                    rng_seed[k] = int(tok, 0) & 0xffffffff

    if not args.analyze_only:
        cfg = fc.CaptureConfig(
            duration_ms=args.duration_ms,
            remote=args.remote, exe=args.exe, cwd=args.cwd,
            auto_start_server=not args.no_auto_start,
            hide_window=True,
            turbo=not args.no_turbo,
            silent_audio=True,
            input_replay_path=args.input_trace,
            input_segtrace_path=args.input_segtrace,
            # A menu→scene segtrace needs the intro skipped and anchor detection
            # on (its {wait} ops resolve against emitted anchors).
            skip_intro=args.skip_intro or args.input_segtrace is not None,
            anchor_trace=args.input_segtrace is not None,
            rng_seed=rng_seed,
            mem_watch=bool(regions),
            mem_watch_regions=regions,
            mem_watch_precise=not args.no_precise,
            mem_watch_rearm_cap=int(args.rearm_cap),
            mem_watch_arm_between=args.arm_between,
            mem_snapshot_regions=snap_regions,
        )
        result = fc._run_capture_impl(cfg, args.run_dir)
        # Capture diagnostics → file, not stdout (stdout stays one JSON object).
        (args.run_dir / "mem_watch_run.json").write_text(json.dumps({
            "exit_code":   result.exit_code,
            "elapsed_ms":  result.elapsed_ms,
        }, indent=2))

    summary: dict = {}
    if regions:
        summary = _rank(args.run_dir, regions)
    if snap_regions:
        summary["snapshot"] = _snapshot_summary(args.run_dir, snap_regions)
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
