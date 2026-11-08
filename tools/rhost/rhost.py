#!/usr/bin/env python3
"""rhost.py — driver for the in-process retail host (tools/rhost) for Lords of the Realm II.

  rhost.py build                          build build/rhost/rhost.exe (nix devshell mingw)
  rhost.py stage                          upload rhost.exe + Windows scripts to C:\\ol2-rhost
  rhost.py setup                          one-time: stage + runner task + pristine game copy
  rhost.py run SCEN|TRACE.jsonl [..]      replay scenario(s) (tests/scenarios/<SCEN>/trace.jsonl)
  rhost.py boot [--frames N]              no input: boot → title (smoke test)
  rhost.py det SCEN [-n N] [--calibrate]  N runs, dump at end, diff (determinism gate)
  rhost.py corpus [--only RE] [--check|--bless]   every scenario; expected.json gate
  rhost.py verify [--only RE]             every scenario × every shadow hook → evidence table
  rhost.py diff A.rhs B.rhs               classify the differences between two dumps
  rhost.py record NAME [--from SCEN]      HUMAN PLAY on the Windows desktop (realtime, visible window)
                                          → tests/scenarios/NAME/trace.jsonl (--from: replay SCEN first)
  rhost.py shots SCEN --at F,F|A:B:STEP   screenshots (PNG + exact .idx index planes) at frames

Common opts: --frames N --hook SPEC --dump-at F,..|end --shot-at F,..|end --shot-every N --hash-every N
             --step-ms N --vbase MS -j J --target win|wine --extra "raw rhost args"
Targets: `win` (default) = the Windows box over `ssh win`: headless runs need no desktop (rhost's
DirectDraw is software), so they run straight from the ssh session; --show / record go through the
interactive-session queue runner `ol2-rhost-runner`. `wine` = this host, headless (wine's null display
driver, no X server), under a prefix whose C:\\ol2-rhost is the same layout: retail sees identical paths,
and the state hashes match Windows bit for bit.
Results: /mnt/ephemeral/openlords2/rhost-runs/<batch>/<run>/ (rhost.log; last line RESULT k=v ...).
"""
import argparse
import datetime
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HERE = os.path.join(ROOT, "tools", "rhost")
SCEN = os.path.join(ROOT, "tests", "scenarios")
BUILD = os.path.join(ROOT, "build", "rhost")
EPH = os.environ.get("OL2_EPHEMERAL", "/mnt/ephemeral/openlords2")
LOCAL_RUNS = os.environ.get("RHOST_RUNS", os.path.join(EPH, "rhost-runs"))
WIN = os.environ.get("RHOST_WIN", "win")
WROOT = "C:\\ol2-rhost"
WEXE = "ol2-rhost.exe"          # distinct name: sibling projects kill their own rhost.exe by path
VOLATILE = os.path.join(HERE, "volatile.txt")
SSH_NOISE = re.compile(r"post-quantum|pq\.html|may need to be upgraded|store now, decrypt later")
sys.path.insert(0, HERE)


def sh(cmd, check=True):
    r = subprocess.run(cmd, shell=isinstance(cmd, str), text=True)
    if check and r.returncode:
        raise SystemExit(f"command failed ({r.returncode}): {cmd}")
    return r


def ssh_win(ps, timeout=7200, check=True):
    r = subprocess.run(["ssh", "-o", "BatchMode=yes", WIN, ps], text=True, capture_output=True, timeout=timeout)
    err = "\n".join(l for l in r.stderr.splitlines() if not SSH_NOISE.search(l))
    if r.returncode != 0 and check:
        raise SystemExit(f"ssh win failed ({r.returncode}): {ps[:300]}\n{err}")
    return r.stdout


def scp(src, dst, recursive=False, check=True):
    cmd = ["scp", "-q", "-o", "BatchMode=yes"] + (["-r"] if recursive else []) + [src, dst]
    r = subprocess.run(cmd, text=True, capture_output=True)
    if check and r.returncode != 0:
        raise SystemExit(f"scp failed: {' '.join(cmd)}\n{r.stderr}")
    return r.returncode == 0


def win_job(job_id, ps_path_win, timeout=14400):
    """run C:\\...\\x.ps1 in the INTERACTIVE session through the ol2-rhost queue runner"""
    q = WROOT + "\\queue"
    ps = (f"if (!(Get-ScheduledTask -TaskName ol2-rhost-runner -ErrorAction SilentlyContinue)) {{ 'NO RUNNER: rhost.py setup'; exit 3 }}; "
          f"Copy-Item '{ps_path_win}' '{q}\\{job_id}.tmp'; Move-Item '{q}\\{job_id}.tmp' '{q}\\{job_id}.job.ps1'; "
          f"while (!(Test-Path '{q}\\{job_id}.done')) {{ Start-Sleep -Milliseconds 200 }}; "
          f"Get-Content '{q}\\{job_id}.out'; Remove-Item '{q}\\{job_id}.*'")
    return ssh_win(ps, timeout=timeout)


# ---------------------------------------------------------------- build / stage
def cmd_build(a=None):
    sh([os.path.join(ROOT, "tools", "dev"), "bash", os.path.join(HERE, "build.sh")])   # no resident nix evaluator


def cmd_stage(a=None):
    ssh_win(f"New-Item -ItemType Directory -Force {WROOT}\\bin,{WROOT}\\runs,{WROOT}\\queue,{WROOT}\\game | Out-Null; exit 0")
    # a running batch maps the old image: upload under a new name, then swap
    scp(os.path.join(BUILD, "rhost.exe"), f"{WIN}:C:/ol2-rhost/bin/{WEXE}.new")
    ssh_win(f"Move-Item -Force '{WROOT}\\bin\\{WEXE}.new' '{WROOT}\\bin\\{WEXE}'")
    for f in ("runner.ps1", "install-runner.ps1", "stage-game.ps1"):
        scp(os.path.join(HERE, "win", f), f"{WIN}:C:/ol2-rhost/{f}")
    print("staged", BUILD, "->", WROOT)


def cmd_setup(a):
    cmd_build()
    cmd_stage()
    print(ssh_win(f"powershell -NoProfile -ExecutionPolicy Bypass -File {WROOT}\\stage-game.ps1"))
    print(ssh_win(f"powershell -NoProfile -ExecutionPolicy Bypass -File {WROOT}\\install-runner.ps1"))


_PINNED = False


def rhost_stale():
    if os.environ.get("RHOST_NO_BUILD") or _PINNED:   # as is: another agent's WIP in the shared tree / a pinned exe
        return False
    exe = os.path.join(BUILD, "rhost.exe")
    if not os.path.exists(exe):
        return True
    t = os.path.getmtime(exe)
    srcs = [os.path.join(HERE, f) for f in os.listdir(HERE) if f.endswith((".c", ".h", ".sh", ".x"))]
    srcs += [os.path.join(ROOT, "src", f) for f in os.listdir(os.path.join(ROOT, "src")) if f.endswith((".c", ".h"))]
    return any(os.path.getmtime(p) > t for p in srcs)


def ensure_staged():
    exe = os.path.join(BUILD, "rhost.exe")
    stamp = os.path.join(BUILD, ".staged")
    if rhost_stale():
        cmd_build()
    wins = [os.path.join(HERE, "win", f) for f in os.listdir(os.path.join(HERE, "win"))]
    if not os.path.exists(stamp) or os.path.getmtime(stamp) < max([os.path.getmtime(exe)] + [os.path.getmtime(w) for w in wins]):
        cmd_stage()
        open(stamp, "w").close()


# ---------------------------------------------------------------- scenarios
def trace_meta(tr):
    meta = {}
    for line in open(tr):
        line = line.strip()
        if line.startswith("{") and '"meta"' in line:
            meta.update(json.loads(line)["meta"])
    return meta


def scenario_inputs(name):
    """→ (trace path, meta).  meta may carry step_ms / vbase (the recording's clock) and "saves": [files]
    copied into the run dir (the game's *.sav, user data: tests/scenarios/<s>/_saves/, gitignored)."""
    if name.endswith(".jsonl") and os.path.exists(name):
        tr = os.path.abspath(name)
    else:
        tr = os.path.join(SCEN, name, "trace.jsonl")
    if not os.path.exists(tr):
        raise SystemExit(f"no trace: {tr}")
    return tr, trace_meta(tr)


def rhost_args(a, run_win, trace_win, meta):
    gd = f"{WROOT}\\game"
    args = ["--exe", f"{gd}\\Lords2.exe", "--game", gd, "--run", run_win]
    if trace_win:
        args += ["--trace", trace_win]
    for k, flag in (("step_ms", "--step-ms"), ("vbase", "--vbase")):
        v = getattr(a, k, None)
        if v is None:
            v = meta.get(k)
        if v is not None:
            args += [flag, str(v)]
    if meta.get("movies"):                      # recorded with deterministic Smacker (rh_smack.c)
        args += ["--movies"]
    if getattr(a, "frames", None) is None and meta.get("frames"):
        args += ["--frames", str(meta["frames"])]
    for k, flag in (("frames", "--frames"), ("dump_at", "--dump-at"), ("hash_every", "--hash-every"),
                    ("shot_at", "--shot-at"), ("shot_every", "--shot-every"), ("log_every", "--log-every")):
        v = getattr(a, k, None)
        if v is not None and v != "":
            args += [flag, str(v)]
    if getattr(a, "fb_hash", False):
        args += ["--fb-hash"]
    if getattr(a, "coverage_file", None):
        args += ["--coverage", f"{run_win}\\coverage_in.txt"]
    if getattr(a, "show", False):
        args += ["--show"]
    if getattr(a, "extra", None):
        args += shlex.split(a.extra)
    return args


def make_batch(a, runs, tag, root_local=None):
    """runs: list of (run_name, trace_path|None, meta).  Writes the batch dir + go.ps1 (Windows) / argv.json."""
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for k in range(1000):   # parallel batches (agents) in the same second get -2, -3, ...
        batch = f"{stamp}-{tag}" + (f"-{k + 1}" if k else "")
        bdir = os.path.join(root_local or LOCAL_RUNS, batch)
        try:
            os.makedirs(bdir)
            break
        except FileExistsError:
            continue
    # the batch runs its OWN copy of the exe: a rebuild (another agent, the next pass) never swaps it mid-batch;
    # a multi-batch command (verify) pins one exe for all its batches
    shutil.copy2(getattr(a, "exe_pin", None) or os.path.join(BUILD, "rhost.exe"), os.path.join(bdir, WEXE))
    lines = ["$ErrorActionPreference = 'Continue'", f"$J = {max(1, a.jobs)}", "$procs = @()"]
    for name, trace, meta in runs:
        rdir = os.path.join(bdir, name)
        os.makedirs(rdir)
        if os.path.exists(VOLATILE):
            shutil.copy(VOLATILE, os.path.join(rdir, "volatile.txt"))
        for s in meta.get("saves", []):
            src = os.path.normpath(os.path.join(os.path.dirname(trace), s)) if trace else s
            shutil.copy(src, os.path.join(rdir, os.path.basename(s)))
        run_win = f"{WROOT}\\runs\\{batch}\\{name}"
        trace_win = None
        if trace:
            shutil.copy(trace, os.path.join(rdir, "trace.jsonl"))
            trace_win = f"{run_win}\\trace.jsonl"
        if getattr(a, "coverage_file", None):
            shutil.copy(a.coverage_file, os.path.join(rdir, "coverage_in.txt"))
        args = rhost_args(a, run_win, trace_win, meta)
        spec = ""
        if "--hook" in args:
            k = args.index("--hook"); spec = args[k + 1]; del args[k:k + 2]
        if getattr(a, "hook", None):
            spec = ",".join(x for x in (spec, a.hook) if x)
        open(os.path.join(rdir, "hooks.txt"), "w").write(spec)
        args += ["--hook", f"@{run_win}\\hooks.txt"]
        open(os.path.join(rdir, "argv.json"), "w").write(json.dumps(args))
        ps_args = ",".join("'" + x.replace("'", "''") + "'" for x in args)
        lines.append(f"while (@($procs | Where-Object {{ !$_.HasExited }}).Count -ge $J) {{ Start-Sleep -Milliseconds 20 }}")
        lines.append(f"$procs += Start-Process -FilePath '{WROOT}\\runs\\{batch}\\{WEXE}' -ArgumentList @({ps_args}) -PassThru "
                     f"-WindowStyle Hidden -RedirectStandardOutput '{run_win}\\stdout.txt' -RedirectStandardError '{run_win}\\stderr.txt'")
    lines.append("$procs | ForEach-Object { $_.WaitForExit() }")
    for name, *_ in runs:
        run_win = f"{WROOT}\\runs\\{batch}\\{name}"
        lines.append(f"Write-Output '=== {name}'")
        lines.append(f"if (Test-Path '{run_win}\\rhost.log') {{ Select-String -Path '{run_win}\\rhost.log' -Pattern 'RESULT|FATAL|DIVERGE|hook-result|MessageBox|UNIMPL|STALL|AV f' | ForEach-Object {{ $_.Line }} }}")
        lines.append(f"if (Test-Path '{run_win}\\stderr.txt') {{ Get-Content '{run_win}\\stderr.txt' -TotalCount 5 }}")
    open(os.path.join(bdir, "go.ps1"), "w").write("\r\n".join(lines) + "\r\n")
    return batch, bdir


def parse_out(out):
    results, cur = {}, None
    for line in out.splitlines():
        if line.startswith("=== "):
            cur = line[4:].strip()
        elif cur and line.startswith("RESULT"):
            results[cur] = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
        elif cur and line.strip():
            results.setdefault(cur + "#notes", []).append(line.strip())
    return results


# ---------------------------------------------------------------- wine target (this host, headless)
WINE_ROOT = os.environ.get("RHOST_WINE_ROOT", os.path.join(EPH, "rhost-wine", "root"))
WINE_PREFIX = os.environ.get("RHOST_WINEPREFIX", os.path.join(EPH, "rhost-wine", "prefix"))
WINE_GAME = os.environ.get("RHOST_WINE_GAME", os.path.join(EPH, "rhost-wine", "game"))


def wine_stage():
    """C:\\ol2-rhost → WINE_ROOT inside the prefix; game\\ = a pristine copy like stage-game.ps1's"""
    for d in ("bin", "runs"):
        os.makedirs(os.path.join(WINE_ROOT, d), exist_ok=True)
    src, dst = os.path.join(BUILD, "rhost.exe"), os.path.join(WINE_ROOT, "bin", WEXE)
    if os.path.exists(src) and not os.environ.get("RHOST_NO_BUILD") and \
            (not os.path.exists(dst) or os.path.getmtime(dst) < os.path.getmtime(src)):
        tmp = f"{dst}.{os.getpid()}.new"
        shutil.copy2(src, tmp)
        os.replace(tmp, dst)
    if not os.path.isdir(WINE_GAME):
        gog = os.path.join(ROOT, "vendor", "original")
        skip = re.compile(r"^(openlords2.*\.exe|unins000\..*|.*\.sav|.*\.json|.*\.jsonl|after_click.*|before_click.*|frame_\d+.*|port_.*|"
                          r"status\.txt|.*\.lnk|goggame-.*|gog\.ico)$", re.I)
        os.makedirs(WINE_GAME)
        for f in os.listdir(gog):
            p = os.path.join(gog, f)
            if os.path.isfile(p) and not skip.match(f):
                shutil.copy2(p, os.path.join(WINE_GAME, f))
    link = os.path.join(WINE_ROOT, "game")
    if not os.path.islink(link):
        os.symlink(WINE_GAME, link)
    if not os.path.isdir(os.path.join(WINE_PREFIX, "drive_c")):
        os.makedirs(WINE_PREFIX, exist_ok=True)
        subprocess.run(["wineboot", "-i"], env=dict(os.environ, WINEPREFIX=WINE_PREFIX, WINEDEBUG="-all"), capture_output=True)
    link = os.path.join(WINE_PREFIX, "drive_c", "ol2-rhost")
    if not os.path.islink(link):
        os.symlink(WINE_ROOT, link)
        # wine's null display driver: rhost's DirectDraw is software, so no X server is needed at all
        subprocess.run(["wine", "reg", "add", "HKCU\\Software\\Wine\\Drivers", "/v", "Graphics", "/d", "null", "/f"],
                       env=wine_env(), capture_output=True)


def wine_env():
    env = dict(os.environ, WINEPREFIX=WINE_PREFIX, WINEDEBUG="-all")
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    return env


def run_batch_wine(a, runs, tag):
    import concurrent.futures
    if rhost_stale():
        cmd_build()
    wine_stage()
    batch, bdir = make_batch(a, runs, tag, root_local=os.path.join(WINE_ROOT, "runs"))
    env = wine_env()

    def one(r):
        name = r[0]
        argv = json.load(open(os.path.join(bdir, name, "argv.json")))
        # wine's output goes to a FILE, not a pipe: a wine launch that has to start the prefix's wineserver hands it
        # our stdio, and the wineserver (shared with every other wine job in the prefix) would hold a pipe's write
        # end open long after the run — communicate() then never sees EOF and the batch hangs (2026-11-04)
        wout = os.path.join(bdir, name, "wine.out")
        with open(wout, "w") as f:
            subprocess.run(["wine", f"{WROOT}\\runs\\{batch}\\{WEXE}"] + argv, stdin=subprocess.DEVNULL, stdout=f,
                           stderr=subprocess.STDOUT, env=env, timeout=getattr(a, "wine_timeout", 3600))
        txt = open(wout, errors="replace").read()
        if len(txt) > 8000:
            open(wout, "w").write(txt[-8000:])
        log = os.path.join(bdir, name, "rhost.log")
        return name, open(log, errors="replace").read() if os.path.exists(log) else ""
    t0 = time.time()
    out_lines = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        for name, log in ex.map(one, runs):
            out_lines.append(f"=== {name}")
            out_lines += [l for l in log.splitlines() if re.search(r"RESULT|FATAL|DIVERGE|hook-result|MessageBox|UNIMPL|STALL|AV f", l)]
    out = "\n".join(out_lines)
    print(f"[wine batch {batch}: {len(runs)} run(s) in {time.time() - t0:.1f}s (-j{a.jobs}) → {bdir}]")
    return batch, bdir, parse_out(out), out


PRUNE_HOURS = float(os.environ.get("RHOST_PRUNE_HOURS", "48"))
_pruned = False


def prune_win(hours):
    """delete OUR run dirs (C:\\ol2-rhost\\runs\\*) older than `hours` — never anything outside C:\\ol2-rhost"""
    ps = (f"$cut = (Get-Date).AddHours(-{hours}); $n = 0; "
          f"Get-ChildItem -Directory '{WROOT}\\runs' -ErrorAction SilentlyContinue | "
          f"Where-Object {{ $_.LastWriteTime -lt $cut -and $_.FullName -like '{WROOT}\\runs\\*' -and $_.Name -notlike '*record*' }} | "
          f"ForEach-Object {{ Remove-Item -Recurse -Force $_.FullName -ErrorAction SilentlyContinue; $n++ }}; \"pruned $n\"")
    return ssh_win(ps, check=False).strip()


def cmd_prune(a):
    print(prune_win(a.hours))


def run_batch(a, runs, tag, fetch=("rhost.log",), interactive=False):
    global _pruned
    if getattr(a, "target", "win") == "wine":
        return run_batch_wine(a, runs, tag)
    ensure_staged()
    if not _pruned and PRUNE_HOURS > 0:     # once per invocation: keep our run dirs bounded on the shared box
        _pruned = True
        prune_win(PRUNE_HOURS)
    batch, bdir = make_batch(a, runs, tag)
    scp(bdir, f"{WIN}:C:/ol2-rhost/runs/", recursive=True)
    t0 = time.time()
    go = f"{WROOT}\\runs\\{batch}\\go.ps1"
    out = win_job(batch, go) if interactive else ssh_win(f"powershell -NoProfile -ExecutionPolicy Bypass -File {go}")
    results = parse_out(out)
    for name, *_ in runs:
        for f in fetch:
            dst = os.path.join(bdir, name) if "*" in f else os.path.join(bdir, name, f)
            scp(f"{WIN}:C:/ol2-rhost/runs/{batch}/{name}/{f}", dst, check=False)
    print(f"[batch {batch}: {len(runs)} run(s) in {time.time() - t0:.1f}s (-j{a.jobs}) → {bdir}]")
    return batch, bdir, results, out


def resolve_runs(names):
    out = []
    for n in names:
        tr, meta = scenario_inputs(n)
        out.append((os.path.splitext(os.path.basename(n))[0] if n.endswith(".jsonl") else n, tr, meta))
    return out


def all_scenarios(only=None, tracked=False):
    """scenario dirs with a trace.jsonl; tracked: only those git tracks (or has staged) — the pre-commit gate,
    so another agent's untracked, not-yet-blessed games in the shared tree do not block every commit"""
    if not os.path.isdir(SCEN):
        return []
    names = sorted(d for d in os.listdir(SCEN) if os.path.exists(os.path.join(SCEN, d, "trace.jsonl")))
    if tracked:
        ls = subprocess.run(["git", "-C", ROOT, "ls-files", "--", "tests/scenarios/*/trace.jsonl"], capture_output=True, text=True).stdout
        names = [n for n in names if f"tests/scenarios/{n}/trace.jsonl" in ls.split()]
    return [n for n in names if not only or re.search(only, n)]


def print_results(results, names):
    for n in names:
        r = results.get(n)
        if not r:
            print(f"{n:36s} NO RESULT  {' | '.join(results.get(n + '#notes', [])[:4])}")
            continue
        print(f"{n:36s} {r.get('status'):6s} {r.get('reason', ''):12s} f={r.get('frame'):>6s} tick={r.get('tick', '?'):>6s} "
              f"scr={r.get('screen')} rng={r.get('rng')} sem={r.get('state_sem')} {r.get('fps', '?')}fps complete={r.get('complete', '?')}")
        for note in results.get(n + "#notes", [])[:8]:
            print("    " + note)


def fetch_list(a):
    f = ["rhost.log", "stdout.txt", "stderr.txt", "status.txt", "shot_*.png", "frame_*.idx"]
    if getattr(a, "dump_at", None):
        f.append("state_*.rhs")
    return tuple(f)


# ---------------------------------------------------------------- commands
def cmd_run(a):
    runs = resolve_runs(a.scenarios)
    batch, bdir, res, out = run_batch(a, runs, "run", fetch=fetch_list(a), interactive=getattr(a, "show", False))
    print_results(res, [r[0] for r in runs])
    return 0 if all(res.get(r[0], {}).get("status") in FINISHED for r in runs) else 1


def cmd_boot(a):
    a.frames = a.frames or 1500
    runs = [("boot", None, {})]
    batch, bdir, res, out = run_batch(a, runs, "boot", fetch=fetch_list(a), interactive=getattr(a, "show", False))
    print_results(res, ["boot"])
    if not res.get("boot"):
        print(out[-3000:])
    return 0 if res.get("boot", {}).get("status") == "ok" else 1


def cmd_shots(a):
    fr = []
    for part in a.at.split(","):
        if ":" in part:
            lo, hi, st = (int(x) for x in part.split(":"))
            fr += list(range(lo, hi + 1, st))
        else:
            fr.append(int(part))
    a.shot_at = ",".join(str(f) for f in fr)
    return cmd_run(a)


def cmd_det(a):
    import rhs
    if a.scenario == "boot":
        runs = [(f"r{i}", None, {}) for i in range(a.n)]
        a.frames = a.frames or 1500
    else:
        tr, meta = scenario_inputs(a.scenario)
        runs = [(f"r{i}", tr, meta) for i in range(a.n)]
    a.dump_at = (a.dump_at + ",end") if a.dump_at else "end"
    batch, bdir, res, out = run_batch(a, runs, "det-" + os.path.basename(a.scenario), fetch=("rhost.log", "state_*.rhs"))
    print_results(res, [r[0] for r in runs])
    labels = sorted(f for f in os.listdir(os.path.join(bdir, "r0")) if f.startswith("state_") and f.endswith(".rhs")) \
        if os.path.isdir(os.path.join(bdir, "r0")) else []
    bad, learned = 0, set()
    for lab in labels or ["state_end.rhs"]:
        base = os.path.join(bdir, "r0", lab)
        for i in range(1, a.n):
            other = os.path.join(bdir, f"r{i}", lab)
            if not (os.path.exists(base) and os.path.exists(other)):
                print(f"--- r0 vs r{i} {lab}: dump missing"); bad += 1; continue
            print(f"--- r0 vs r{i} {lab}")
            rows = rhs.diff(base, other, show=20)
            real = [r for r in rows if r[3] not in ("ext-ptr(both)", "volatile-addr")]
            bad += len(real)
            learned |= {r[0] for r in rows if r[3] == "ext-ptr(both)"}
    sems = {res.get(r[0], {}).get("state_sem") for r in runs}
    print(f"DETERMINISM {'PASS' if not bad and len(sems) == 1 else 'FAIL'}: {bad} non-pointer differing dwords; "
          f"semantic hashes: {len(sems)} distinct; external-pointer slots: {len(learned)}")
    if a.calibrate and learned:
        old = set()
        if os.path.exists(VOLATILE):
            old = {int(l.split()[0], 16) for l in open(VOLATILE) if l.strip() and not l.startswith("#")}
        new = sorted(old | learned)
        with open(VOLATILE, "w") as f:
            f.write("# rhost volatile addresses: slots that hold system-DLL pointers (masked in state_sem).\n"
                    "# Learned by `rhost.py det SCEN --calibrate`. The goal is an EMPTY list (fake every source).\n")
            for v in new:
                f.write(f"{v:08x}\n")
        print(f"calibrated {VOLATILE}: {len(old)} → {len(new)} addresses")
    return 0 if not bad and len(sems) == 1 else 1


EXPECT_KEYS = ("frame", "tick", "screen", "rng", "state_sem", "fb")
FINISHED = ("ok", "exit")   # a run ends at its trace's end, or in retail's own ExitProcess (File > Quit): both complete


def cmd_corpus(a):
    names = all_scenarios(a.only, getattr(a, "tracked", False))
    if not names:
        raise SystemExit("no scenarios under tests/scenarios/")
    if a.bless or a.check:
        a.fb_hash = True
    runs = resolve_runs(names)
    if a.bless:   # bless only what reproduces: every scenario twice, both runs must agree
        runs = runs + [(n + "__again", tr, meta) for n, tr, meta in runs]
    batch, bdir, res, out = run_batch(a, runs, "corpus")
    print_results(res, names)
    if a.bless:
        for n in names:
            r1, r2 = res.get(n, {}), res.get(n + "__again", {})
            if any(r1.get(k) != r2.get(k) for k in EXPECT_KEYS):
                print(f"  {n}: NOT REPRODUCIBLE — {', '.join(f'{k} {r1.get(k)}/{r2.get(k)}' for k in EXPECT_KEYS if r1.get(k) != r2.get(k))}")
                res[n] = dict(r1, status="nondeterministic")
    done = sum(1 for n in names if res.get(n, {}).get("complete") == "1")
    print(f"CORPUS: {done}/{len(names)} complete; results in {bdir}")
    bad = 0
    tgt = getattr(a, "target", "win")
    if a.bless:
        for n in names:
            ep = os.path.join(SCEN, n, "expected.json" if tgt == "win" else f"expected.{tgt}.json")
            r = res.get(n, {})
            if r.get("status") not in FINISHED:
                print(f"  {n}: not blessed (status {r.get('status')})"); bad += 1; continue
            json.dump({k: r.get(k) for k in EXPECT_KEYS}, open(ep, "w"), indent=1)
            print(f"  {n}: blessed {os.path.relpath(ep, ROOT)}")
    elif a.check:
        for n, diffs in expected_diffs(names, res, tgt).items():
            print(f"  {n}: {'PASS' if not diffs else 'FAIL ' + '; '.join(diffs)}")
            bad += bool(diffs)
    if a.check:
        print(f"CORPUS CHECK: {'PASS' if not bad else f'FAIL ({bad})'}")
    return 1 if bad else 0


def verify_hooks():
    """every row of every hook table (rh_hooks.c + rh_hooks_*.c): {"name", 0xVAu, ...}"""
    out = []
    for f in sorted(os.listdir(HERE)):
        if f.startswith("rh_hooks") and f.endswith(".c"):
            out += re.findall(r'\{\s*"(\w+)",\s*0x[0-9a-fA-F]+u,', open(os.path.join(HERE, f)).read())
    return out


VERIFY_HOOKS = verify_hooks()


def expected_diffs(names, res, tgt):
    """{scenario: ["key exp→got", ...]} against the blessed expected[.TARGET].json (missing file → one entry)"""
    out = {}
    for n in names:
        ep = os.path.join(SCEN, n, "expected.json" if tgt == "win" else f"expected.{tgt}.json")
        if not os.path.exists(ep):
            out[n] = ["NO expected.json (run --bless)"]
            continue
        exp, r = json.load(open(ep)), res.get(n, {})
        out[n] = [f"{k} {exp.get(k)}→{r.get(k)}" for k in EXPECT_KEYS if exp.get(k) != r.get(k)]
    return out


def cmd_verify(a):
    """shadow every hook over the corpus, in passes: a hook called inside another shadowed call runs retail there
    (rh_in_shadow), so pass 1 compares the outermost hooks, pass 2 shadows only the hooks pass 1 never reached
    (the next nesting level), ... until a pass reaches nothing new. Then the hybrid pass: every hook in replace
    mode at once must reproduce the blessed corpus bit-exactly (corpus --check).
    --hooks RE: only the rows matching RE (shadow passes + a hybrid pass with just those replaced; no evidence).
    RH_SH_SAMPLE rows report `sampled` (calls not shadowed: retail ran), not counted in `calls`; RH_SH_HOST rows (host
    level: never shadowed) only `sampled` — the hybrid pass is their evidence."""
    global _PINNED
    names = all_scenarios(a.only)
    rows = [h for h in VERIFY_HOOKS if not getattr(a, "hooks", None) or re.search(a.hooks, h)]
    if not rows:
        raise SystemExit(f"--hooks {a.hooks!r}: no hook row matches")
    runs = resolve_runs(names)
    if rhost_stale():
        cmd_build()
    a.exe_pin = os.path.join(LOCAL_RUNS, f"verify-{os.getpid()}.exe")   # every pass runs this exact build
    shutil.copy2(os.path.join(BUILD, "rhost.exe"), a.exe_pin)
    _PINNED = True
    tot, depth, todo, failed = {}, {}, list(rows), []
    for p in range(1, 17):
        a.hook = ",".join("shadow:" + h for h in todo)
        batch, bdir, res, out = run_batch(a, runs, f"verify{p}")
        print(f"--- shadow pass {p}: {len(todo)} hook(s)")
        print_results(res, names)
        failed += [f"pass {p} {n}: {res.get(n, {}).get('status', 'NO RESULT')}" for n in names if res.get(n, {}).get("status") not in FINISHED]
        # a shadow keeps retail's post-state: every pass must end exactly where the blessed corpus does
        failed += [f"pass {p} {n}: end state ≠ corpus ({'; '.join(d)})" for n, d in expected_diffs(names, res, a.target).items()
                   if [x for x in d if not x.startswith("fb ")]]
        got = {}
        for n in names:
            for note in res.get(n + "#notes", []):
                m = re.match(r"hook-result (\S+) mode=\S+ calls=(\d+) diverge=(\d+) skipped=(\d+)(?: sampled=(\d+))?", note)
                if m:   # sampled: RH_SH_SAMPLE calls not shadowed (retail ran), not in calls
                    t = got.setdefault(m.group(1), [0, 0, 0, 0, 0])
                    t[0] += int(m.group(2)); t[1] += int(m.group(3)); t[2] += int(m.group(4))
                    t[3] += int(m.group(2)) + int(m.group(5) or 0) > 0
                    t[4] += int(m.group(5) or 0)
        # reached: shadowed calls, or (RH_SH_HOST rows: never shadowed — the hybrid pass proves them) sampled ones
        reached = [h for h in todo if got.get(h, [0])[0] > 0 or got.get(h, [0, 0, 0, 0, 0])[4] > 0]
        for h in reached:
            tot[h], depth[h] = got[h], p
        todo = [h for h in todo if h not in reached]
        if not reached or not todo:
            break
    a.hook, a.fb_hash = ",".join("replace:" + h for h in rows), True
    batch, bdir, res, out = run_batch(a, runs, "verify-hybrid")
    print(f"--- hybrid pass: {len(rows)} hook(s) replaced, corpus --check")
    print_results(res, names)
    hyb = {n: d for n, d in expected_diffs(names, res, a.target).items() if d}
    for n, d in hyb.items():
        print(f"  {n}: FAIL {'; '.join(d)}")
    print("\nhook                          depth    calls  diverge  skipped  scenarios   sampled")
    for h in rows:
        t = tot.get(h, [0, 0, 0, 0, 0])
        print(f"{h:28s} {depth.get(h, '-'):>5} {t[0]:8d} {t[1]:8d} {t[2]:8d} {t[3]:4d}/{len(names)} {t[4] or '':>9}")
    for f in failed:
        print(f"  RUN FAILED {f}")
    print(f"\nVERIFY: {sum(1 for h in rows if tot.get(h, [0, 0])[1] == 0 and h in tot)}/{len(rows)} reached with "
          f"0 divergences, {sum(1 for t in tot.values() if t[1])} diverging, {len(todo)} never called; "
          f"hybrid {'PASS' if not hyb else f'FAIL ({len(hyb)})'}; {len(failed)} failed run(s)")
    if not a.only and not a.hooks and not failed:   # the whole corpus: record the evidence (docs/STATUS.md's runtime axis)
        src = "".join(open(os.path.join(HERE, f)).read() for f in os.listdir(HERE) if f.startswith("rh_hooks") and f.endswith(".c"))
        vas = dict(re.findall(r'\{\s*"(\w+)",\s*(0x[0-9a-fA-F]+)u', src))
        ep = os.path.join(ROOT, "docs", "rhost-evidence.json")
        today = datetime.date.today().isoformat()
        ev = {"hooks": {}, "hybrid": {"hooks": len(rows), "corpus": len(names), "failing": sorted(hyb),
                                      "target": a.target, "date": today}}
        for h in rows:
            t = tot.get(h, [0, 0, 0, 0, 0])
            ev["hooks"][h] = {"va": vas.get(h, "?"), "depth": depth.get(h, 0), "calls": t[0], "scenarios": t[3],
                              "corpus": len(names), "diverge": t[1], "skipped": t[2], "target": a.target, "date": today}
            if t[4]:
                ev["hooks"][h]["sampled"] = t[4]
        json.dump(ev, open(ep, "w"), indent=1, sort_keys=True)
        print(f"evidence → {os.path.relpath(ep, ROOT)}")
    os.remove(a.exe_pin)
    return 0 if not failed and not hyb and all(t[1] == 0 for t in tot.values()) else 1


def function_table():
    """retail functions → {va: (name, size)}: the IDA export if present, else the Ghidra functions.csv"""
    fn = {}
    ida = os.path.join(EPH, "decompiled-ida", "functions_ida.tsv")
    if os.path.exists(ida):
        hdr = None
        for line in open(ida):
            parts = line.rstrip("\n").split("\t")
            if hdr is None:
                hdr = parts
                continue
            r = dict(zip(hdr, parts))
            try:
                fn[int(r.get("va") or r.get("ea"), 16)] = (r.get("name", ""), int(r.get("size", "0"), 0))
            except (ValueError, TypeError):
                pass
        if fn:
            return fn, "ida"
    import csv
    for r in csv.DictReader(open(os.path.join(ROOT, "docs", "decompiled", "functions.csv"))):
        if r["is_thunk"] != "true":
            fn[int(r["entry"], 16)] = (r["name"], int(r["size"]))
    return fn, "ghidra"


def cmd_coverage(a):
    """first-hit coverage of every retail function over the corpus (or the given scenarios)"""
    fn, src = function_table()
    cov_in = os.path.join(LOCAL_RUNS, "coverage_in.txt")
    os.makedirs(LOCAL_RUNS, exist_ok=True)
    open(cov_in, "w").write("".join(f"{va:x}\n" for va in sorted(fn)))
    a.coverage_file = cov_in
    runs = resolve_runs(a.scenarios or all_scenarios(a.only))
    names = [r[0] for r in runs]
    batch, bdir, res, out = run_batch(a, runs, "coverage", fetch=("rhost.log", "coverage.txt"))
    root = bdir
    hit = {}
    for n in names:
        p = os.path.join(root, n, "coverage.txt")
        if os.path.exists(p):
            for line in open(p):
                va, f = line.split()
                hit.setdefault(int(va, 16), []).append(n)
    tot_b = sum(sz for _, sz in fn.values())
    hit_b = sum(fn[v][1] for v in hit if v in fn)
    print(f"COVERAGE ({src} function table): {len(hit)}/{len(fn)} functions, {hit_b}/{tot_b} bytes "
          f"({100.0 * hit_b / max(1, tot_b):.1f}%) over {len(names)} scenario(s)")
    outp = os.path.join(bdir, "coverage_summary.tsv")
    with open(outp, "w") as f:
        f.write("va\tname\tsize\tscenarios\n")
        for va in sorted(hit):
            nm, sz = fn.get(va, ("?", 0))
            f.write(f"{va:08x}\t{nm}\t{sz}\t{','.join(hit[va])}\n")
    print(f"→ {outp}")
    return 0


VIEWER = os.path.join(ROOT, "tools", "trace_studio_v3", "viewer", "viewer.exe")


def cmd_view(a):
    """Trace Studio v3 on two rhost runs of a frame window, paired by frame number: A = retail (pure), B = the
    "port" side — another scenario (--b) or the same one with hooks (--b-hook replace:NAME: hybrid vs pure).
    Opens viewer.exe on the Windows desktop (the runner), or renders one column headless (--shot)."""
    lo, n = (int(x) for x in a.cap.split(":"))
    a.extra = ((a.extra or "") + f" --cap {lo}:{n}").strip()
    ra = resolve_runs([a.scenario])[0]
    rb = resolve_runs([a.b or a.scenario])[0]
    runs = [("A-" + ra[0], ra[1], ra[2]), ("B-" + rb[0], rb[1], rb[2])]
    saved = a.hook
    a.hook = None
    ensure_staged()
    batch, bdir = make_batch(a, runs[:1], "view")
    if a.b_hook:
        a.hook = a.b_hook
    # the B run goes into the same batch dir with its own hook spec
    b_batch, b_dir = make_batch(a, runs[1:], "viewB")
    a.hook = saved
    shutil.move(os.path.join(b_dir, runs[1][0]), os.path.join(bdir, runs[1][0]))
    go_b = open(os.path.join(b_dir, "go.ps1")).read().replace(b_batch, batch)
    go_a = open(os.path.join(bdir, "go.ps1")).read()
    open(os.path.join(bdir, "go.ps1"), "w").write(go_a + go_b)
    shutil.rmtree(b_dir)
    for r in runs:   # argv.json / hooks.txt inside the moved run dir still name viewB's batch
        for f in ("argv.json",):
            p = os.path.join(bdir, r[0], f)
            open(p, "w").write(open(p).read().replace(b_batch, batch))
    wrun = f"{WROOT}\\runs\\{batch}"
    cols = [{"label": f"f{lo + i}", "offset": i, "port_idx": i, "retail_idx": i, "port_present": lo + i,
             "retail_present": lo + i} for i in range(n)]
    view = {"scenario": a.scenario + (f" vs {a.b}" if a.b else "") + (f" [{a.b_hook}]" if a.b_hook else ""),
            "anchor": "frame", "verdict": "", "note": "rhost: A = retail (left/retail), B = the other run (port side)",
            "has_state": False, "port_container": f"{wrun}\\{runs[1][0]}\\capture.olv3",
            "retail_container": f"{wrun}\\{runs[0][0]}\\capture.olv3", "frames": cols,
            "notes_path": f"{wrun}\\notes.json"}
    open(os.path.join(bdir, "view.json"), "w").write(json.dumps(view, indent=1))
    scp(bdir, f"{WIN}:C:/ol2-rhost/runs/", recursive=True)
    out = ssh_win(f"powershell -NoProfile -ExecutionPolicy Bypass -File {wrun}\\go.ps1")
    print_results(parse_out(out), [r[0] for r in runs])
    if not os.path.exists(VIEWER):
        sh([os.path.join(ROOT, "tools", "dev"), "make", "-C", os.path.dirname(VIEWER)])
    ssh_win(f"New-Item -ItemType Directory -Force {WROOT}\\viewer | Out-Null")
    scp(VIEWER, f"{WIN}:C:/ol2-rhost/viewer/viewer.exe")
    job = os.path.join(bdir, "viewer.ps1")
    if a.shot:
        open(job, "w").write(f"& '{WROOT}\\viewer\\viewer.exe' --shot '{wrun}\\view.bmp' --col {a.col} '{wrun}\\view.json'\r\n")
    else:
        open(job, "w").write(f"Start-Process -FilePath '{WROOT}\\viewer\\viewer.exe' -ArgumentList '{wrun}\\view.json'\r\n")
    scp(job, f"{WIN}:C:/ol2-rhost/runs/{batch}/viewer.ps1")
    print(win_job(batch + "-viewer", f"{wrun}\\viewer.ps1", timeout=600))
    if a.shot:
        scp(f"{WIN}:C:/ol2-rhost/runs/{batch}/view.bmp", a.shot, check=False)
        print("shot →", a.shot)
    else:
        print(f"viewer opened on the Windows desktop: {wrun}\\view.json")
    return 0


def cmd_diff(a):
    import rhs
    rhs.diff(a.a, a.b, show=a.show_n)


def cmd_record(a):
    """human play: a visible, realtime window on the Windows desktop; closing it ends the recording"""
    d = os.path.join(SCEN, a.name)
    if os.path.exists(os.path.join(d, "trace.jsonl")) and not a.force:
        raise SystemExit(f"{d}/trace.jsonl exists (--force to overwrite)")
    meta, trace = {}, None
    if a.from_scen:
        trace, meta = scenario_inputs(a.from_scen)
    a.extra = ((a.extra or "") + " --record recorded.jsonl").strip()
    a.frames = None
    runs = [(a.name, trace, meta)]
    batch, bdir, res, out = run_batch(a, runs, "record-" + a.name, fetch=("rhost.log", "recorded.jsonl"), interactive=True)
    print_results(res, [a.name])
    rec = os.path.join(bdir, a.name, "recorded.jsonl")
    if not os.path.exists(rec):
        raise SystemExit("no recording came back")
    os.makedirs(d, exist_ok=True)
    lines = open(rec).read().splitlines()
    hdr = [f"# {a.name}: recorded {datetime.date.today()} by a human in rhost (batch {batch})"]
    if a.from_scen:
        hdr.append(f"# continues {a.from_scen} (its events are the prefix)")
    open(os.path.join(d, "trace.jsonl"), "w").write("\n".join(hdr + lines) + "\n")
    print(f"wrote {d}/trace.jsonl ({len(lines) - 1} events)")



# ---------------------------------------------------------------- free-play capture (the Desktop shortcut)
# The owner plays on the Windows desktop through C:\ol2-rhost\play\play.cmd (win/play.ps1): pure retail in rhost,
# --record --movies, one session per launch, the game's own writes (saves) carried to the next session.
PLAY = WROOT + "\\play"
PLAY_FILES = [os.path.join(HERE, "win", f) for f in ("play.ps1", "play.cmd")] + [f for f in (VOLATILE,) if os.path.exists(f)]
SHORTCUT = "Lords II capture"
PLAY_LOCAL = os.path.join(LOCAL_RUNS, "play")


def play_install(root):
    """freeze the current rhost build in ROOT\\bin\\<sha> (never pruned, untouched by later stages) + the scripts → sha"""
    import hashlib
    ensure_staged()
    exe = os.path.join(BUILD, "rhost.exe")
    sha = hashlib.sha256(open(exe, "rb").read()).hexdigest()[:12]
    ssh_win(f"New-Item -ItemType Directory -Force '{root}\\bin\\{sha}','{root}\\sessions' | Out-Null; "
            f"if (!(Test-Path '{root}\\bin\\{sha}\\{WEXE}')) {{ Copy-Item '{WROOT}\\bin\\{WEXE}' '{root}\\bin\\{sha}\\{WEXE}.tmp'; "
            f"Move-Item '{root}\\bin\\{sha}\\{WEXE}.tmp' '{root}\\bin\\{sha}\\{WEXE}' }}; exit 0")
    # the staged copy must be this build (ensure_staged just staged it)
    got = ssh_win(f"(Get-FileHash '{root}\\bin\\{sha}\\{WEXE}' -Algorithm SHA256).Hash").strip().lower()
    if not got.startswith(sha):
        raise SystemExit(f"play-install: {root}\\bin\\{sha} holds {got[:12]}, not this build")
    up = root.replace(chr(92), "/")
    for f in PLAY_FILES:
        if f.endswith((".cmd", ".ps1")):   # CRLF for cmd.exe / PowerShell
            tmp = os.path.join(BUILD, os.path.basename(f))
            open(tmp, "w", newline="\r\n").write(open(f).read())
            f = tmp
        scp(f, f"{WIN}:{up}/{os.path.basename(f)}")
    errs = ssh_win("$t = $null; $x = $null; "
                   f"[void][System.Management.Automation.Language.Parser]::ParseFile('{root}\\play.ps1', [ref]$t, [ref]$x); "
                   "foreach ($i in $x) { \"play.ps1:$($i.Extent.StartLineNumber): $($i.Message)\" }; exit 0").strip()
    if errs:
        raise SystemExit("play.ps1 does not parse:\n" + errs)
    return sha


def cmd_play_install(a):
    """freeze this rhost build as the launcher's, upload the scripts, (re)create the Desktop shortcut"""
    sha = play_install(PLAY)
    ssh_win(f"Set-Content '{PLAY}\\current.txt' '{sha}'; "
            "$d = [Environment]::GetFolderPath('Desktop'); $w = New-Object -ComObject WScript.Shell; "
            f"$l = $w.CreateShortcut((Join-Path $d '{SHORTCUT}.lnk')); $l.TargetPath = '{PLAY}\\play.cmd'; "
            f"$l.WorkingDirectory = '{PLAY}'; $l.IconLocation = '{WROOT}\\game\\Lords2.exe,0'; "
            "$l.Description = 'Lords of the Realm II: play and record (OpenLords2 rhost)'; $l.Save(); exit 0")
    print(f"play-install: rhost {sha} frozen in {PLAY}\\bin\\{sha}; Desktop shortcut '{SHORTCUT}' → {PLAY}\\play.cmd")
    return 0


PS_SESSIONS = ("Get-ChildItem '@ROOT@\\sessions' -Directory -ErrorAction SilentlyContinue | ForEach-Object { "
               "$m = Join-Path $_.FullName 'meta.json'; $o = @{ session = $_.Name; done = (Test-Path $m) }; "
               "if (Test-Path $m) { $o.meta = Get-Content $m -Raw | ConvertFrom-Json }; "
               "$o | ConvertTo-Json -Depth 6 -Compress }")


def win_sessions(root=PLAY):
    out = ssh_win(PS_SESSIONS.replace("@ROOT@", root))
    return [json.loads(l) for l in out.splitlines() if l.strip().startswith("{")]


def play_scen(session):
    return "hp-" + session


def cmd_play_status(a):
    ss = win_sessions(a.root)
    if not ss:
        print(f"no capture sessions under {a.root}\\sessions"); return 0
    for s in ss:
        m = s.get("meta") or {}
        r = m.get("result") or {}
        have = os.path.exists(os.path.join(SCEN, play_scen(s["session"]), "trace.jsonl"))
        print(f"{s['session']}  {'done' if s['done'] else 'in progress / killed':20s} f={r.get('frame', '-'):>8} "
              f"{m.get('wall_s', '-')!s:>6}s  {r.get('reason', '-'):14s} start={','.join(m.get('start_files') or []) or '-'}"
              f"  {'imported' if have else ''}")
    return 0


def cmd_play_fetch(a):
    """import finished sessions as tests/scenarios/hp-<stamp>/ (+ _saves/ for the start files) and self-check each:
    a headless replay (wine) must end on the recording's frame and state hash"""
    ss = [s for s in win_sessions(a.root) if s["done"] and (s.get("meta") or {}).get("result")]
    if a.session:
        ss = [s for s in ss if s["session"] in a.session]
    todo = [s for s in ss if a.force or not os.path.exists(os.path.join(SCEN, play_scen(s["session"]), "trace.jsonl"))]
    if not todo:
        print("play-fetch: nothing new"); return 0
    runs, recorded = [], {}
    for s in todo:
        st, m = s["session"], s["meta"]
        loc = os.path.join(PLAY_LOCAL, st)
        os.makedirs(loc, exist_ok=True)
        wd = f"{a.root}\\sessions\\{st}".replace(chr(92), "/")
        for f in ("trace.jsonl", "meta.json", "rhost.log"):
            scp(f"{WIN}:{wd}/{f}", os.path.join(loc, f))
        name = play_scen(st)
        d = os.path.join(SCEN, name)
        os.makedirs(d, exist_ok=True)
        saves = []
        for f in m.get("start_files") or []:
            os.makedirs(os.path.join(d, "_saves"), exist_ok=True)
            scp(f"{WIN}:{wd}/start/{f}", os.path.join(d, "_saves", f))
            saves.append(f"_saves/{f}")
        lines = open(os.path.join(loc, "trace.jsonl")).read().splitlines()
        hdr = [f"# {name}: free play recorded {m.get('started')} by a human in rhost (Desktop '{SHORTCUT}', build {m.get('bin')}), "
               f"{m.get('wall_s')} s wall"]
        if saves:
            hdr.append("# starts with the files the previous session's game wrote (its saves): _saves/ (user data, gitignored)")
            hdr.append(json.dumps({"meta": {"saves": saves}}))
        open(os.path.join(d, "trace.jsonl"), "w").write("\n".join(hdr + lines) + "\n")
        print(f"wrote {d}/trace.jsonl ({len(lines)} lines)")
        runs.append((name, os.path.join(d, "trace.jsonl"), trace_meta(os.path.join(d, "trace.jsonl"))))
        recorded[name] = m["result"]
    if a.no_check:
        return 0
    a.target, a.frames = "wine", None
    for k in ("hook", "dump_at", "shot_at", "shot_every", "hash_every", "log_every", "step_ms", "vbase", "extra"):
        setattr(a, k, None)
    a.fb_hash = a.show = False
    a.jobs = a.jobs or 4
    batch, bdir, res, out = run_batch(a, runs, "play-check")
    bad = 0
    for name, *_ in runs:
        r, rec = res.get(name, {}), recorded[name]
        ok = r.get("frame") == rec.get("frame") and r.get("state_sem") == rec.get("state_sem") and r.get("rng") == rec.get("rng")
        bad += not ok
        print(f"{name}: recorded f={rec.get('frame')} sem={rec.get('state_sem')} | replay f={r.get('frame')} "
              f"sem={r.get('state_sem')} → {'PASS' if ok else 'FAIL'}")
    return 1 if bad else 0


def cmd_play_test(a):
    """the launcher end to end, headless: play.ps1 from ssh (session 0: the window is off-screen) in
    C:\\ol2-rhost\\playtest, --frames N with no input; run twice so the second session starts from the first's carry"""
    root = WROOT + "\\playtest"
    sha = play_install(root)
    ssh_win(f"Remove-Item '{root}\\sessions\\*','{root}\\carry' -Recurse -Force -ErrorAction SilentlyContinue; exit 0")
    for i in range(2):
        print(ssh_win(f"powershell -NoProfile -ExecutionPolicy Bypass -File {root}\\play.ps1 -Root '{root}' -Bin '{sha}' "
                      f"-TestArgs '--frames {a.frames or 1500}'", check=False))
        time.sleep(1.1)   # distinct session stamps
    a.root = root
    return cmd_play_status(a)



# ---------------------------------------------------------------- the standalone's own coverage report
IDA_FUNCS = os.path.join(EPH, "decompiled-ida", "functions_ida.tsv")


def ida_func_index():
    """[(va, size, name)] sorted (the IDA export), for naming a gap and its caller"""
    out = []
    if os.path.exists(IDA_FUNCS):
        for l in open(IDA_FUNCS).read().splitlines()[1:]:
            c = l.split("\t")
            out.append((int(c[0], 16), int(c[1]), c[2]))
    return sorted(out)


def ida_name(idx, va):
    import bisect
    i = bisect.bisect_right(idx, (va, 1 << 30, "")) - 1
    if i >= 0 and idx[i][0] <= va < idx[i][0] + max(idx[i][1], 1):
        return idx[i][2] + ("" if idx[i][0] == va else f"+{va - idx[i][0]:#x}")
    return "?"


def cmd_standalone(a):
    """the corpus under --standalone (no retail code): per scenario how far it runs, the first gap (the retail function
    it reached + the caller), and — for a run that completes — frame / tick / RNG / fb (every presented frame) against
    the blessed pure-retail values, and state_sem (the whole state: since the CRT port, rl_crt.c, the standalone's
    state is retail's)"""
    names = all_scenarios(a.only)
    a.fb_hash = True
    a.extra = ((a.extra or "") + " --standalone").strip()
    runs = resolve_runs(names)
    batch, bdir, res, out = run_batch(a, runs, "standalone")
    idx = ida_func_index()
    cur, gaps = None, {}
    for line in out.splitlines():
        if line.startswith("=== "):
            cur = line[4:].strip()
        m = re.search(r"reached retail code at ([0-9a-f]{8}) \(no port for it yet; return address ([0-9a-f]{8})\) frame (\d+)", line)
        if cur and m:
            gaps[cur] = (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)))
    tgt = getattr(a, "target", "win")
    full, same, rows = 0, 0, []
    for n in names:
        r = res.get(n, {})
        ep = os.path.join(SCEN, n, "expected.json" if tgt == "win" else f"expected.{tgt}.json")
        exp = json.load(open(ep)) if os.path.exists(ep) else {}
        if n in gaps:
            va, ret, f = gaps[n]
            line = f"{n:28s} GAP   f={f:>6}/{exp.get('frame', '?'):>6}  {va:08x} {ida_name(idx, va)}  from {ret:08x} {ida_name(idx, ret)}"
        elif r.get("status") in FINISHED:
            full += 1
            keys = [k for k in ("frame", "tick", "rng", "fb", "state_sem") if exp.get(k) != r.get(k)]
            same += not keys
            line = f"{n:28s} DONE  f={r.get('frame'):>6}  {'= pure retail (frame tick rng fb state)' if not keys else 'differs: ' + ', '.join(f'{k} {exp.get(k)}→{r.get(k)}' for k in keys)}"
        else:
            note = " | ".join(res.get(n + "#notes", [])[:2])
            line = f"{n:28s} {str(r.get('status')).upper():5s} f={r.get('frame', '?'):>6}  {note}"
        rows.append(line)
        print(line)
    print(f"STANDALONE: {len(names)} scenarios — {full} ran to the end ({same} equal to pure retail in frame / tick / RNG / "
          f"every presented frame / state_sem), {len(gaps)} stopped at a gap; distinct gaps: {len({g[0] for g in gaps.values()})}")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    def common(sp):
        sp.add_argument("--frames", type=int)
        sp.add_argument("--hook")
        sp.add_argument("--dump-at", dest="dump_at")
        sp.add_argument("--shot-at", dest="shot_at")
        sp.add_argument("--shot-every", dest="shot_every", type=int)
        sp.add_argument("--hash-every", dest="hash_every", type=int)
        sp.add_argument("--log-every", dest="log_every", type=int)
        sp.add_argument("--step-ms", dest="step_ms", type=int)
        sp.add_argument("--vbase", type=int)
        sp.add_argument("--fb-hash", dest="fb_hash", action="store_true")
        sp.add_argument("--show", action="store_true")
        sp.add_argument("-j", "--jobs", type=int, default=None,
                        help="parallel runs (default: $RHOST_JOBS, else 8 on wine, 3 on win — sibling projects share the Windows box)")
        sp.add_argument("--target", choices=("win", "wine"), default=os.environ.get("RHOST_TARGET", "win"))
        sp.add_argument("--extra")
    sub.add_parser("build").set_defaults(fn=cmd_build)
    sub.add_parser("stage").set_defaults(fn=cmd_stage)
    sub.add_parser("setup").set_defaults(fn=cmd_setup)
    sp = sub.add_parser("run"); common(sp); sp.add_argument("scenarios", nargs="+"); sp.set_defaults(fn=cmd_run)
    sp = sub.add_parser("boot"); common(sp); sp.set_defaults(fn=cmd_boot)
    sp = sub.add_parser("shots"); common(sp); sp.add_argument("scenarios", nargs="+"); sp.add_argument("--at", required=True); sp.set_defaults(fn=cmd_shots)
    sp = sub.add_parser("det"); common(sp); sp.add_argument("scenario"); sp.add_argument("-n", type=int, default=3)
    sp.add_argument("--calibrate", action="store_true"); sp.set_defaults(fn=cmd_det)
    sp = sub.add_parser("corpus"); common(sp); sp.add_argument("--only"); sp.add_argument("--check", action="store_true")
    sp.add_argument("--bless", action="store_true")
    sp.add_argument("--tracked", action="store_true", help="only scenarios git tracks (the pre-commit gate)")
    sp.set_defaults(fn=cmd_corpus)
    sp = sub.add_parser("verify"); common(sp); sp.add_argument("--only")
    sp.add_argument("--hooks", help="regex: verify only the matching hook rows (iteration; writes no evidence)")
    sp.set_defaults(fn=cmd_verify)
    sp = sub.add_parser("coverage"); common(sp); sp.add_argument("scenarios", nargs="*"); sp.add_argument("--only")
    sp.set_defaults(fn=cmd_coverage)
    sp = sub.add_parser("view"); common(sp); sp.add_argument("scenario"); sp.add_argument("--b"); sp.add_argument("--b-hook", dest="b_hook")
    sp.add_argument("--cap", required=True, help="FROM:COUNT frames"); sp.add_argument("--shot"); sp.add_argument("--col", type=int, default=0)
    sp.set_defaults(fn=cmd_view)
    sp = sub.add_parser("prune"); sp.add_argument("--hours", type=float, default=PRUNE_HOURS); sp.set_defaults(fn=cmd_prune)
    sp = sub.add_parser("diff"); sp.add_argument("a"); sp.add_argument("b"); sp.add_argument("--show-n", type=int, default=40); sp.set_defaults(fn=cmd_diff)
    sp = sub.add_parser("record"); common(sp); sp.add_argument("name"); sp.add_argument("--from", dest="from_scen")
    sp.add_argument("--force", action="store_true"); sp.set_defaults(fn=cmd_record)
    sp = sub.add_parser("standalone"); common(sp); sp.add_argument("--only"); sp.set_defaults(fn=cmd_standalone)
    sub.add_parser("play-install").set_defaults(fn=cmd_play_install)
    sp = sub.add_parser("play-status"); sp.add_argument("--root", default=PLAY); sp.set_defaults(fn=cmd_play_status)
    sp = sub.add_parser("play-fetch"); sp.add_argument("--root", default=PLAY); sp.add_argument("session", nargs="*")
    sp.add_argument("--force", action="store_true"); sp.add_argument("--no-check", dest="no_check", action="store_true")
    sp.add_argument("-j", "--jobs", type=int, default=None); sp.set_defaults(fn=cmd_play_fetch)
    sp = sub.add_parser("play-test"); sp.add_argument("--frames", type=int); sp.set_defaults(fn=cmd_play_test)
    a = p.parse_args()
    if getattr(a, "jobs", 0) is None:
        a.jobs = int(os.environ.get("RHOST_JOBS", "0") or 0) or (3 if getattr(a, "target", "win") == "win" else 8)   # RHOST_JOBS: parallel agents
    r = a.fn(a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
