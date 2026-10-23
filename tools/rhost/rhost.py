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
    sh(["nix", "develop", ROOT, "--command", "bash", os.path.join(HERE, "build.sh")])


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


def rhost_stale():
    if os.environ.get("RHOST_NO_BUILD"):     # use the staged rhost as is (another agent's WIP in the shared tree)
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
    batch = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + tag
    bdir = os.path.join(root_local or LOCAL_RUNS, batch)
    os.makedirs(bdir)
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
        lines.append(f"$procs += Start-Process -FilePath '{WROOT}\\bin\\{WEXE}' -ArgumentList @({ps_args}) -PassThru "
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
        out = subprocess.run(["wine", f"{WROOT}\\bin\\{WEXE}"] + argv, capture_output=True, text=True, env=env,
                             timeout=getattr(a, "wine_timeout", 3600))
        open(os.path.join(bdir, name, "wine.out"), "w").write(out.stdout[-4000:] + out.stderr[-4000:])
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


def all_scenarios(only=None):
    if not os.path.isdir(SCEN):
        return []
    names = sorted(d for d in os.listdir(SCEN) if os.path.exists(os.path.join(SCEN, d, "trace.jsonl")))
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
    return 0 if all(res.get(r[0], {}).get("status") == "ok" for r in runs) else 1


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


def cmd_corpus(a):
    names = all_scenarios(a.only)
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
    for n in names:
        ep = os.path.join(SCEN, n, "expected.json" if tgt == "win" else f"expected.{tgt}.json")
        r = res.get(n, {})
        got = {k: r.get(k) for k in EXPECT_KEYS}
        if a.bless:
            if r.get("status") != "ok":
                print(f"  {n}: not blessed (status {r.get('status')})"); bad += 1; continue
            json.dump(got, open(ep, "w"), indent=1)
            print(f"  {n}: blessed {os.path.relpath(ep, ROOT)}")
        elif a.check:
            if not os.path.exists(ep):
                print(f"  {n}: NO expected.json (run --bless)"); bad += 1; continue
            exp = json.load(open(ep))
            diffs = [f"{k} {exp.get(k)}→{got.get(k)}" for k in EXPECT_KEYS if exp.get(k) != got.get(k)]
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


def cmd_verify(a):
    names = all_scenarios(a.only)
    runs = resolve_runs(names)
    a.hook = ",".join("shadow:" + h for h in VERIFY_HOOKS)
    batch, bdir, res, out = run_batch(a, runs, "verify")
    tot = {}
    for n in names:
        for note in res.get(n + "#notes", []):
            m = re.match(r"hook-result (\S+) mode=\S+ calls=(\d+) diverge=(\d+) skipped=(\d+)", note)
            if m:
                t = tot.setdefault(m.group(1), [0, 0, 0, 0])
                t[0] += int(m.group(2)); t[1] += int(m.group(3)); t[2] += int(m.group(4)); t[3] += int(m.group(2)) > 0
    print_results(res, names)
    print("\nhook                 calls  diverge  skipped  scenarios")
    for h in VERIFY_HOOKS:
        t = tot.get(h, [0, 0, 0, 0])
        print(f"{h:18s} {t[0]:8d} {t[1]:8d} {t[2]:8d} {t[3]:4d}/{len(names)}")
    print("\nevidence lines:")
    for h in VERIFY_HOOKS:
        t = tot.get(h, [0, 0, 0, 0])
        print(f"  {h}: rhost shadow: {t[0]} calls / {t[3]} scenarios, {t[1]} divergences")
    if not a.only:   # the whole corpus: record the evidence (docs/STATUS.md's runtime axis)
        src = "".join(open(os.path.join(HERE, f)).read() for f in os.listdir(HERE) if f.startswith("rh_hooks") and f.endswith(".c"))
        vas = dict(re.findall(r'\{\s*"(\w+)",\s*(0x[0-9a-fA-F]+)u', src))
        ep = os.path.join(ROOT, "docs", "rhost-evidence.json")
        ev = json.load(open(ep)) if os.path.exists(ep) else {}
        for h in VERIFY_HOOKS:
            t = tot.get(h, [0, 0, 0, 0])
            ev[h] = {"va": vas.get(h, "?"), "calls": t[0], "scenarios": t[3], "corpus": len(names), "diverge": t[1],
                     "skipped": t[2], "target": a.target, "date": datetime.date.today().isoformat()}
        json.dump(ev, open(ep, "w"), indent=1, sort_keys=True)
        print(f"evidence → {os.path.relpath(ep, ROOT)}")
    return 0 if all(tot.get(h, [0, 0])[1] == 0 for h in VERIFY_HOOKS) else 1


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
        sh(["nix", "develop", ROOT, "--command", "make", "-C", os.path.dirname(VIEWER)])
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
                        help="parallel runs (default: 8 on wine, 3 on win — sibling projects share the Windows box)")
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
    sp.add_argument("--bless", action="store_true"); sp.set_defaults(fn=cmd_corpus)
    sp = sub.add_parser("verify"); common(sp); sp.add_argument("--only"); sp.set_defaults(fn=cmd_verify)
    sp = sub.add_parser("coverage"); common(sp); sp.add_argument("scenarios", nargs="*"); sp.add_argument("--only")
    sp.set_defaults(fn=cmd_coverage)
    sp = sub.add_parser("view"); common(sp); sp.add_argument("scenario"); sp.add_argument("--b"); sp.add_argument("--b-hook", dest="b_hook")
    sp.add_argument("--cap", required=True, help="FROM:COUNT frames"); sp.add_argument("--shot"); sp.add_argument("--col", type=int, default=0)
    sp.set_defaults(fn=cmd_view)
    sp = sub.add_parser("prune"); sp.add_argument("--hours", type=float, default=PRUNE_HOURS); sp.set_defaults(fn=cmd_prune)
    sp = sub.add_parser("diff"); sp.add_argument("a"); sp.add_argument("b"); sp.add_argument("--show-n", type=int, default=40); sp.set_defaults(fn=cmd_diff)
    sp = sub.add_parser("record"); common(sp); sp.add_argument("name"); sp.add_argument("--from", dest="from_scen")
    sp.add_argument("--force", action="store_true"); sp.set_defaults(fn=cmd_record)
    a = p.parse_args()
    if getattr(a, "jobs", 0) is None:
        a.jobs = 3 if getattr(a, "target", "win") == "win" else 8
    r = a.fn(a)
    sys.exit(r or 0)


if __name__ == "__main__":
    main()
