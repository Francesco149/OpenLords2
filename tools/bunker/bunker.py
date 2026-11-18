#!/usr/bin/env python3
"""bunker: rhost HEADLESS under wine on a temporary NixOS box, inside ONE bounded pool (tools/bunker/README.md).
Port of openrecet's tools/bunker, namespaced ol2-* (openrecet's orc-bunker pool shares the box). The third rhost host:
`rhost.py … --target bunker` (run / snap-check / corpus --check: rhost.run_batch → run_batch here) and live sessions
(tools/drive/drive.py: stage / spawn_session / forward / kill_session / session_alive / session_file).

    bunker.py setup                        # work dir, nix GC roots (wine, bwrap, python, fontconfig), the prefix
    bunker.py stage                        # rhost build (if stale) + bin/<sha12> + game dir + this dir's scripts (cached)
    bunker.py run SCEN.. [-j N] [--extra " --x"] [--frames N] [--check] [--keep]
    bunker.py status                       # the pool: live sessions, memory; disk
    bunker.py teardown                     # stop the pool (every OL2 bunker process), delete /var/lib/ol2-bunker

ssh: ONE multiplexed connection (ControlMaster, ControlPersist 2h, socket CTL) carries every command, rsync and live
session forward — bunker's sshd refuses connections past `MaxStartups 10:30:100`.
--check: end state vs the blessed expected.wine.json (bunker runs the same wine engine; rhost.expected_diffs).
Host: RHOST_BUNKER (default bunker). Local results: RHOST_BUNKER_RUNS (/mnt/ephemeral/openlords2/bunker-runs).
"""
import argparse
import fcntl
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import tarfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "rhost"))
import rhost  # noqa: E402
import diskguard  # noqa: E402

HOST = os.environ.get("RHOST_BUNKER", "bunker")
W = "/var/lib/ol2-bunker"
PY = f"{W}/gcroots/python/bin/python3"
POOL = f"{PY} {W}/bin/pool.py"
LOCAL = os.environ.get("RHOST_BUNKER_RUNS", os.path.join(rhost.EPH, "bunker-runs"))
WROOT, WEXE = rhost.WROOT, rhost.WEXE           # C:\ol2-rhost (= W/root, /ol2 in a container), ol2-rhost.exe
# nixpkgs attr → gcroots link. wine: wineWow64Packages.unstable (11.19, cache.nixos.org) — see README for the pin
NIX = {"wine-wow64-unstable": "wineWow64Packages.unstable", "bwrap": "bubblewrap", "python": "python3",
       "fontconfig": "fontconfig.out"}
WINE = "wine-wow64-unstable"
SCRIPTS = ("pool.py", "batch.py")
STAMP = ".ol2-staged"

# ---- the one ssh connection
CTL = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/ol2-{os.getuid()}", f"ol2-bunker-{re.sub(r'[^A-Za-z0-9.]', '_', HOST)}.ctl")
OPTS = ["-o", "BatchMode=yes", "-o", f"ControlPath={CTL}", "-o", "ControlMaster=no", "-o", "ServerAliveInterval=30",
        "-o", "ServerAliveCountMax=4"]
RSH = "ssh " + " ".join(OPTS)


def master_up():
    return subprocess.run(["ssh"] + OPTS + ["-O", "check", HOST], capture_output=True).returncode == 0


def ensure_master(timeout=8):
    """the multiplexing master (started once, under a lock: concurrent drive / rhost processes share it) → True if up"""
    if master_up():
        return True
    os.makedirs(os.path.dirname(CTL), exist_ok=True)
    with open(CTL + ".lock", "w") as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        if master_up():
            return True
        try:
            os.remove(CTL)                              # a stale socket of a dead master
        except OSError:
            pass
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={timeout}", "-o", f"ControlPath={CTL}",
                            "-o", "ControlMaster=yes", "-o", "ControlPersist=2h", "-o", "ServerAliveInterval=30",
                            "-o", "ServerAliveCountMax=4", "-fN", HOST],
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
                           timeout=timeout + 10)
        return r.returncode == 0 and master_up()


def up(timeout=3):
    """bunker answers (the master is up or can be made within TIMEOUT s)"""
    try:
        return ensure_master(timeout)
    except (subprocess.TimeoutExpired, OSError):
        return False


def ssh(cmd, check=True, capture=False, input=None, timeout=None):
    """CMD on the bunker (root), over the master → its exit code (stdout with capture)"""
    if not ensure_master():
        raise SystemExit(f"bunker ({HOST}) unreachable: no ssh master")
    r = subprocess.run(["ssh"] + OPTS + [HOST, cmd], text=input is None or isinstance(input, str),
                       stdout=subprocess.PIPE if capture else None, input=input, timeout=timeout)
    if check and r.returncode:
        raise SystemExit(f"ssh {HOST}: exit {r.returncode}: {cmd[:200]}")
    return r.stdout if capture else r.returncode


FWD_REG = CTL + ".fwd"         # "LPORT RPORT" per forward this host added on the master (rd reap cancels the dead ones:
                               # an rd killed mid-search left one, and a master with a forward never hits ControlPersist)


def _fwd_reg(add=None, drop=None):
    with open(FWD_REG, "a+") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        f.seek(0)
        rows = {tuple(map(int, l.split())) for l in f if len(l.split()) == 2}
        if add:
            rows.add(add)
        rows.discard(drop)
        f.seek(0)
        f.truncate()
        f.writelines(f"{a} {b}\n" for a, b in sorted(rows))
        return rows


def forward(lport, rport, cancel=False):
    """add (cancel) a local forward 127.0.0.1:LPORT → bunker 127.0.0.1:RPORT on the master → ok"""
    ok = subprocess.run(["ssh"] + OPTS + ["-O", "cancel" if cancel else "forward", "-L", f"127.0.0.1:{lport}:127.0.0.1:{rport}", HOST],
                        capture_output=True).returncode == 0
    if ok or cancel:
        _fwd_reg(drop=(lport, rport)) if cancel else _fwd_reg(add=(lport, rport))
    return ok


def reap(dry=False):
    """end hung / over-age sessions in the pool (pool.py reap), then cancel the master's forwards to sessions that are
    gone → the report lines"""
    if not ensure_master():
        return []
    ss = scripts_sig()                                  # (pool.py's reap / ports came 2026-11-16: stage it if stale)
    if (ssh(f"cat {W}/bin/{STAMP} 2>/dev/null || true", capture=True) or "").strip() != ss:
        stage_scripts()
        ssh(f"echo {ss} > {W}/bin/{STAMP}")
    out = ssh(f"{POOL} reap" + (" --dry" if dry else ""), check=False, capture=True, timeout=120) or ""
    lines = [l for l in out.splitlines() if l.strip()]
    live = set((ssh(f"{POOL} ports", check=False, capture=True, timeout=60) or "").split())
    for lp, rp in sorted(_fwd_reg()):
        if str(rp) not in live:
            lines.append(f"forward 127.0.0.1:{lp} -> bunker:{rp}: session gone, cancelled" + (" (dry)" if dry else ""))
            if not dry:
                forward(lp, rp, cancel=True)
    return lines


def rsync(*args):
    # --chown: the container maps its uid 1000 to root; files owned by the sender's uid would be unwritable inside
    if not ensure_master():
        raise SystemExit(f"bunker ({HOST}) unreachable: no ssh master")
    subprocess.run(["rsync", "-a", "--chown=root:root", "-e", RSH] + list(args), check=True)


def cmd_setup(a):
    links = " && ".join(f"nix build --out-link gcroots/{k} nixpkgs#{v}" for k, v in NIX.items())
    ssh(f"set -e; mkdir -p {W}/gcroots {W}/bin {W}/root/game {W}/root/bin {W}/root/runs/drive {W}/prefix-headless; cd {W}; "
        f"{links}; ln -sfn {WINE} gcroots/wine; readlink -f gcroots/*")
    stage_scripts()
    if ssh(f"test -e {W}/prefix-headless/system.reg", check=False):
        print("[creating the headless wine prefix (once)]")
        # wineboot, then wine's null display driver (as rhost.wine_stage does on the agent host): rhost's DirectDraw is software
        ssh(f"{POOL} run setup --rw-prefix python3 -c 'import subprocess as s; s.run([\"wineboot\", \"-i\"], capture_output=True); "
            f"s.run([\"wine\", \"reg\", \"add\", \"HKCU\\\\Software\\\\Wine\\\\Drivers\", \"/v\", \"Graphics\", \"/d\", \"null\", \"/f\"], "
            f"capture_output=True); s.run([\"wineserver\", \"-w\"])'")
    ssh(f"ln -sfn /ol2 {W}/prefix-headless/drive_c/ol2-rhost")


def stage_scripts():
    rsync(*[os.path.join(HERE, f) for f in SCRIPTS], f"{HOST}:{W}/bin/")


def exe_sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()[:12]


def game_sig():
    """names + sizes + mtimes of the local pristine game dir (symlinks followed) → the stamp a staged copy carries"""
    h, g = hashlib.sha1(), rhost.WINE_GAME
    for d, ds, fs in os.walk(g, followlinks=True):
        ds.sort()
        for f in sorted(fs):
            st = os.stat(os.path.join(d, f))
            h.update(f"{os.path.relpath(os.path.join(d, f), g)}\0{st.st_size}\0{int(st.st_mtime)}\n".encode())
    return h.hexdigest()[:16]


def scripts_sig():
    return hashlib.sha1(b"".join(open(os.path.join(HERE, f), "rb").read() for f in SCRIPTS)).hexdigest()[:16]


_staged = {}
_stage_lock = threading.Lock()


def stage_exe(path):
    """PATH (an ol2-rhost build) → W/root/bin/<sha12>/ol2-rhost.exe = C:\\ol2-rhost\\bin\\<sha12>\\ol2-rhost.exe → sha12"""
    sha = exe_sha(path)
    if sha not in _staged.setdefault("bins", set()):
        if "BIN" not in ssh(f"test -f {W}/root/bin/{sha}/{WEXE} && echo BIN || true", capture=True):
            ssh(f"mkdir -p {W}/root/bin/{sha}")
            rsync(path, f"{HOST}:{W}/root/bin/{sha}/{WEXE}.tmp")
            ssh(f"mv {W}/root/bin/{sha}/{WEXE}.tmp {W}/root/bin/{sha}/{WEXE}")
        _staged["bins"].add(sha)
    return sha


def stage(build=True):
    """rhost build (if stale) + bin\\<sha> + game + scripts on the bunker, each only when its stamp differs (one round
    trip when all are current) → sha12; the exe is C:\\ol2-rhost\\bin\\<sha12>\\ol2-rhost.exe (staged_exe())"""
    diskguard.need("bunker")
    with _stage_lock:
        if build and rhost.rhost_stale():
            rhost.cmd_build(None)
        if not os.path.isdir(rhost.WINE_GAME):
            rhost.wine_stage()                          # makes the pristine game copy (without saves / leftovers)
        exe = os.path.join(rhost.BUILD, "rhost.exe")
        sha = exe_sha(exe)
        if _staged.get("sha") == sha:
            return sha
        gs, ss = game_sig(), scripts_sig()
        have = ssh(f"cat {W}/root/game/{STAMP} 2>/dev/null; echo; cat {W}/bin/{STAMP} 2>/dev/null; echo; "
                   f"test -f {W}/root/bin/{sha}/{WEXE} && echo BIN || true", capture=True).split("\n")
        if "BIN" in have:
            _staged.setdefault("bins", set()).add(sha)
        stage_exe(exe)
        if have[0].strip() != gs:
            rsync("-L", "--delete", "--exclude", STAMP, rhost.WINE_GAME + "/", f"{HOST}:{W}/root/game/")
            ssh(f"echo {gs} > {W}/root/game/{STAMP}")
        if len(have) < 2 or have[1].strip() != ss:
            stage_scripts()
            ssh(f"echo {ss} > {W}/bin/{STAMP}; mkdir -p {W}/root/runs/drive")
        _staged["sha"] = sha
        return sha


def staged_exe(sha=None):
    """the Windows path of the staged exe (default: stage() now)"""
    return f"{WROOT}\\bin\\{sha or stage()}\\{WEXE}"


def cmd_stage(a=None):
    sha = stage()
    print(sha, staged_exe(sha))


GREP = re.compile(r"RESULT|FATAL|DIVERGE|hook-result|MessageBox|UNIMPL|STALL|AV f")     # = rhost.run_batch_wine's


def run_batch(a, runs, tag, fetch=None, keep=False):
    """rhost.run_batch's contract for --target bunker: A = rhost's options, RUNS = (name, trace, meta) → (batch, local
    bdir, {name: RESULT}, out). make_batch locally (the identical argv.json as `--target wine`), push, run every run
    in one container in the pool (-j at a time), fetch the run dirs back."""
    if getattr(a, "show", False):
        raise SystemExit("--target bunker is headless (no X server there): drop --show, or use win")
    stage()
    os.makedirs(LOCAL, exist_ok=True)
    batch, bdir = rhost.make_batch(a, runs, tag, root_local=LOCAL)
    os.remove(os.path.join(bdir, "go.ps1"))       # the Windows runner script: not used here
    sha = stage_exe(os.path.join(bdir, WEXE))     # the batch's own exe (exe_pin or the build), content-addressed
    os.remove(os.path.join(bdir, WEXE))
    rsync(bdir + "/", f"{HOST}:{W}/root/runs/{batch}/")
    t0 = time.time()
    jobs = max(1, getattr(a, "jobs", 1) or 1)
    to = int(getattr(a, "wine_timeout", 0) or 4 * 3600)
    rc = ssh(f"{POOL} run {batch} python3 /bunker/batch.py /ol2/runs/{batch} '{staged_exe(sha)}' -j {jobs} --timeout {to}",
             check=False)
    wall = time.time() - t0
    rsync(f"{HOST}:{W}/root/runs/{batch}/", bdir + "/")
    if not keep:
        ssh(f"rm -rf {W}/root/runs/{batch}")
    out_lines = []
    for n, *_ in runs:
        out_lines.append(f"=== {n}")
        log = os.path.join(bdir, n, "rhost.log")
        if os.path.exists(log):
            out_lines += [l.rstrip("\n") for l in open(log, errors="replace") if GREP.search(l)]
    out = "\n".join(out_lines)
    print(f"[bunker batch {batch}: {len(runs)} run(s) in {wall:.0f}s, -j{jobs}, container rc {rc} → {bdir}]")
    return batch, bdir, rhost.parse_out(out), out


def bstat(bdir, n):
    try:
        return json.load(open(os.path.join(bdir, n, "bunker.json")))
    except OSError:
        return {}


def cmd_run(a):
    runs = rhost.resolve_runs(a.scenarios)
    b = argparse.Namespace(target="bunker", jobs=a.j, frames=a.frames, extra=a.extra or None, fb_hash=a.check)
    _, bd, res, _ = run_batch(b, runs, "bunker-run", keep=a.keep)
    names = [r[0] for r in runs]
    diffs = rhost.expected_diffs(names, res, "wine") if a.check else {}
    ok = 0
    for n in names:
        r, st = res.get(n), bstat(bd, n)
        if not r:
            print(f"{n:32s} NO RESULT  → {os.path.join(bd, n)}")
            continue
        fr, ms = int(r.get("frame", "0") or 0), float(r.get("wall_ms", "0") or 0)
        line = (f"{n:32s} {r.get('status')} f={fr:>7d} tick={r.get('tick')} {st.get('wall_s', 0):7.1f}s  "
                f"peak rss {st.get('peak_rss_kb', 0) // 1024:5d} MB  rng={r.get('rng')} sem={r.get('state_sem')}")
        if a.check:
            ok += not diffs.get(n)
            line += "  " + ("= expected.wine" if not diffs.get(n) else "DIFF " + "; ".join(diffs[n]))
        print(line)
    if a.check:
        print(f"bunker check: {ok}/{len(names)} equal to expected.wine.json")
        return 0 if ok == len(names) else 1
    return 0


# ---- live sessions (tools/drive/drive.py calls these)
def spawn_session(name, files, exe_win, args, wait, cap, floor_mb):
    """FILES {name: bytes} → C:\\ol2-rhost\\runs\\drive\\NAME on the bunker; admitted + started detached in the pool;
    → ("LIVE", pid, port) | ("NOADMIT", msg) | ("NOSTART", msg). One ssh round trip: the dir travels as a tar on stdin."""
    files = dict(files, **{"spawn.json": json.dumps({"exe": exe_win, "args": args}).encode()})
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w") as t:
        for n, data in files.items():
            ti = tarfile.TarInfo(n)
            ti.size, ti.mtime, ti.mode = len(data), int(time.time()), 0o644
            t.addfile(ti, io.BytesIO(data))
    out = ssh(f"{POOL} spawn {name} --wait {wait} --max {cap} --floor-mb {floor_mb}", check=False, capture=True,
              input=buf.getvalue(), timeout=wait + 420)
    out = out.decode(errors="replace") if isinstance(out, bytes) else (out or "")
    m = re.search(r"^LIVE (\d+) (\d+)", out, re.M)
    if m:
        return "LIVE", int(m.group(1)), int(m.group(2))
    if "NOADMIT" in out:
        return "NOADMIT", out.split("NOADMIT", 1)[1].strip()
    return "NOSTART", out[-800:]


def session_alive(pid, name):
    try:
        return ssh(f"{POOL} alive {int(pid)} {name}", check=False, capture=True, timeout=30).strip() == "ALIVE"
    except (subprocess.TimeoutExpired, SystemExit):
        return False


def kill_session(pid, name):
    ssh(f"{POOL} kill {int(pid)} {name}", check=False, capture=True, timeout=60)


def session_file(name, f):
    """bytes of file F in the session's run dir, None when absent"""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name) or not re.fullmatch(r"[A-Za-z0-9._-]+", f):
        return None
    if not ensure_master():
        return None
    r = subprocess.run(["ssh"] + OPTS + [HOST, f"cat {W}/root/runs/drive/{name}/{f}"], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def cmd_status(a):
    ssh(f"{POOL} stat; systemctl list-units --no-legend 'ol2-bunker-*'; du -sh {W} 2>/dev/null; free -m | head -2")


def cmd_teardown(a):
    ssh(f"for u in $(systemctl list-units --no-legend --plain 'ol2-bunker-*' | cut -d' ' -f1); do systemctl stop $u; done; "
        f"rm -rf {W}; find /nix/var/nix/gcroots/auto -lname '{W}/*' -print -delete")
    subprocess.run(["ssh"] + OPTS + ["-O", "exit", HOST], capture_output=True)
    print(f"[torn down: {W} removed; its GC roots go with it — `nix-collect-garbage` on {HOST} frees the store paths]")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest="cmd", required=True)
    sp.add_parser("setup").set_defaults(fn=cmd_setup)
    sp.add_parser("stage").set_defaults(fn=cmd_stage)
    sp.add_parser("status").set_defaults(fn=cmd_status)
    sp.add_parser("teardown").set_defaults(fn=cmd_teardown)
    r = sp.add_parser("run")
    r.add_argument("scenarios", nargs="+")
    r.add_argument("-j", type=int, default=4)
    r.add_argument("--frames", type=int)
    r.add_argument("--extra", default="")
    r.add_argument("--check", action="store_true", help="end state vs expected.wine.json")
    r.add_argument("--keep", action="store_true", help="keep the run dirs on the bunker")
    r.set_defaults(fn=cmd_run)
    a = p.parse_args()
    r = a.fn(a)
    sys.exit(r if isinstance(r, int) else 0)


if __name__ == "__main__":
    main()
