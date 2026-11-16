#!/usr/bin/env python3
"""Runs ON bunker as root (gcroots python): the one bounded pool every OpenLords2 bunker process lives in
(tools/bunker/README.md). Port of openrecet's tools/bunker/pool.py, namespaced ol2-* (openrecet's orc-bunker-pool runs
next to it on the same box: never touched from here).

The pool = the transient unit `ol2-bunker-pool.service` (`sleep infinity` holding a cgroup: MemoryMax=4G
MemorySwapMax=0 CPUQuota=600% AllowedCPUs=0-3,6-9). Batches and live sessions JOIN it (the launcher writes its pid to
the pool's cgroup.procs before it execs bwrap), so the bound holds over ALL our bunker processes together.
Each container is its own bwrap (user / pid / mount namespaces, network shared) over a copy-on-write view of the
wine prefix (`--overlay-src prefix-headless --tmp-overlay /prefix`): one wineserver per container, the prefix never
written by two at once.

    pool.py ensure                                    # the pool unit (idempotent)
    pool.py run NAME [--rw-prefix] CMD..              # CMD in a container, foreground (batches; setup's wineboot)
    pool.py spawn NAME --wait S --max N --floor-mb M  # stdin = tar of the session dir → C:\\ol2-rhost\\runs\\drive\\NAME;
                                                      #   admitted, started detached → "LIVE <pid> <port>" | NOADMIT .. | NOSTART ..
    pool.py alive PID NAME | kill PID NAME | stat | free | ports
    pool.py reap [--dry]                              # end HUNG / over-age live sessions (spawn runs it first)
Env: OL2_MEM (4G) OL2_CPUS (0-3,6-9) OL2_QUOTA (600%) OL2_WINE (gcroots link name) OL2_HOST_FLOOR_MB (1024)
     OL2_STARTING_MB (150).
"""
import fcntl
import json
import os
import re
import subprocess
import sys
import tarfile
import time

W = "/var/lib/ol2-bunker"
UNIT = "ol2-bunker-pool"
CG = f"/sys/fs/cgroup/system.slice/{UNIT}.service"
LOCK = f"{W}/root/.admit.lock"           # shared with batch.py inside the containers (/ol2/.admit.lock)
SESS = f"{W}/root/runs/drive"
RUN_WIN = "C:\\ol2-rhost\\runs\\drive\\"
NAME_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*")
HOST_FLOOR = int(os.environ.get("OL2_HOST_FLOOR_MB", "1024")) << 20
# a session still replaying to its head (no live.port yet) will grow by ~this. README "Measured": a booted live session
# = ~56 MB in the pool's accounting (anon + overlay shmem), ~70 MB with 50 savestates (Lords2's state is ~6 MB)
STARTING_MB = int(os.environ.get("OL2_STARTING_MB", "150"))
# reap: a session whose CPU time has not moved for its own --live-idle + REAP_SLACK s is hung (an idle one exits by
# itself after --live-idle: rh_live.c) — 2026-11-13 two ev13d sessions sat 3 days in a modal "host heap exhausted"
# dialog; one older than REAP_MAX_AGE goes regardless (rd re-attaches by replay). Samples persist in REAP between calls.
REAP = f"{SESS}/.reap.json"
REAP_SLACK = int(os.environ.get("OL2_REAP_SLACK", "600"))
REAP_MAX_AGE = int(os.environ.get("OL2_REAP_MAX_AGE_H", "24")) * 3600


def gc(n):
    return os.path.realpath(f"{W}/gcroots/{n}")


def ensure():
    with open(f"{W}/.pool.lock", "w") as lk:
        fcntl.flock(lk, fcntl.LOCK_EX)
        if subprocess.run(["systemctl", "is-active", "--quiet", f"{UNIT}.service"]).returncode == 0 and os.path.exists(CG + "/cgroup.procs"):
            return
        subprocess.run(["systemctl", "reset-failed", f"{UNIT}.service"], stderr=subprocess.DEVNULL)
        subprocess.run(["systemd-run", "--quiet", "--collect", f"--unit={UNIT}",
                        "-p", f"MemoryMax={os.environ.get('OL2_MEM', '4G')}", "-p", "MemorySwapMax=0",
                        "-p", f"CPUQuota={os.environ.get('OL2_QUOTA', '600%')}", "-p", f"AllowedCPUs={os.environ.get('OL2_CPUS', '0-3,6-9')}",
                        "sleep", "infinity"], check=True)
        for _ in range(50):
            if os.path.exists(CG + "/cgroup.procs"):
                return
            time.sleep(0.1)
        raise SystemExit("pool unit did not come up")


def pool_free():
    """bytes the pool can still take: MemoryMax − (current − reclaimable page cache); and the host's MemAvailable"""
    mx = int(open(CG + "/memory.max").read().split()[0])
    cur = int(open(CG + "/memory.current").read())
    st = dict(l.split() for l in open(CG + "/memory.stat"))
    used = cur - (int(st["file"]) - int(st["shmem"]))       # shmem (the overlays' tmpfs) is not reclaimable: no swap
    host = next(int(l.split()[1]) << 10 for l in open("/proc/meminfo") if l.startswith("MemAvailable:"))
    return mx - used, host


def bwrap_argv(rw_prefix=False, chdir=None):
    wp, bw, py, fc = gc(os.environ.get("OL2_WINE", "wine")), gc("bwrap") + "/bin/bwrap", gc("python") + "/bin/python3", \
        gc("fontconfig") + "/etc/fonts/fonts.conf"
    pre = ["--bind", f"{W}/prefix-headless", "/prefix"] if rw_prefix else \
        ["--overlay-src", f"{W}/prefix-headless", "--tmp-overlay", "/prefix"]
    return [bw, "--unshare-all", "--share-net", "--uid", "1000", "--gid", "1000", "--die-with-parent", "--new-session",
            "--cap-drop", "ALL", "--ro-bind", "/nix/store", "/nix/store", "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
            "--dir", "/home/ol2", "--bind", f"{W}/root", "/ol2"] + pre + \
           ["--ro-bind", f"{W}/bin", "/bunker", "--ro-bind", CG, "/pool"] + (["--chdir", chdir] if chdir else []) + \
           ["--clearenv", "--setenv", "PATH", f"{wp}/bin:{os.path.dirname(py)}", "--setenv", "HOME", "/home/ol2",
            "--setenv", "USER", "ol2", "--setenv", "XDG_RUNTIME_DIR", "/tmp", "--setenv", "FONTCONFIG_FILE", fc,
            "--setenv", "WINEPREFIX", "/prefix", "--setenv", "WINEDEBUG", "-all", "--setenv", "WINEDLLOVERRIDES", "mscoree,mshtml="]


def join():
    open(CG + "/cgroup.procs", "w").write(str(os.getpid()))


def sessions():
    """{session name: [host pids]} of the live sessions in the pool (their argv holds C:\\ol2-rhost\\runs\\drive\\NAME)"""
    out = {}
    try:
        pids = open(CG + "/cgroup.procs").read().split()
    except OSError:
        return out
    for p in pids:
        try:
            cl = open(f"/proc/{p}/cmdline", "rb").read().decode(errors="replace")
        except OSError:
            continue
        m = re.search(re.escape(RUN_WIN) + r"([A-Za-z0-9._-]+)", cl)
        if m:
            out.setdefault(m.group(1), []).append(int(p))
    return out


def cmd_run(argv):
    name, rw = argv[0], False
    argv = argv[1:]
    if argv and argv[0] == "--rw-prefix":
        rw, argv = True, argv[1:]
    ensure()
    pid = os.fork()
    if pid == 0:
        join()
        os.execv(gc("bwrap") + "/bin/bwrap", bwrap_argv(rw) + argv)
    _, st = os.waitpid(pid, 0)
    rc = os.waitstatus_to_exitcode(st)
    f, h = pool_free()
    print(f"[bunker {name}] pool memory.peak {int(open(CG + '/memory.peak').read()) >> 20} MB, free in the bound {f >> 20} MB "
          f"(MemoryMax {open(CG + '/memory.max').read().strip()})", flush=True)
    sys.exit(rc)


def opt(argv, k, d):
    return type(d)(argv[argv.index(k) + 1]) if k in argv else d


def cmd_spawn(argv):
    name = argv[0]
    if not NAME_RE.fullmatch(name):
        raise SystemExit(f"bad session name {name!r}")
    wait, cap, floor = opt(argv, "--wait", 60.0), opt(argv, "--max", 8), opt(argv, "--floor-mb", 512) << 20
    rd = f"{SESS}/{name}"
    os.makedirs(rd)
    with tarfile.open(fileobj=sys.stdin.buffer, mode="r|") as t:
        t.extractall(rd, filter="data")
    for d, _, fs in os.walk(rd):                       # the container's uid 1000 is host root: root-owned = writable inside
        for x in [d] + [os.path.join(d, f) for f in fs]:
            os.chown(x, 0, 0)
    spec = json.load(open(f"{rd}/spawn.json"))       # {"exe": C:\..\ol2-rhost.exe, "args": [...]}
    ensure()
    try:
        cmd_reap([])                                   # (hung sessions hold slots + memory forever otherwise)
    except Exception as e:                             # noqa: BLE001 — admission must not fail on the reaper
        print(f"[reap failed: {e}]", flush=True)
    deadline = time.time() + wait
    with open(LOCK, "a") as lk:
        while True:
            fcntl.flock(lk, fcntl.LOCK_EX)
            ss = sessions()
            starting = sum(1 for n in ss if not os.path.exists(f"{SESS}/{n}/live.port"))
            free, host = pool_free()
            if len(ss) < cap and free - starting * (STARTING_MB << 20) >= floor and host >= HOST_FLOOR:
                break
            fcntl.flock(lk, fcntl.LOCK_UN)
            if time.time() >= deadline:
                print(f"NOADMIT {len(ss)} live sessions (cap {cap}, {starting} starting), {free >> 20} MB free in the "
                      f"{int(open(CG + '/memory.max').read()) >> 20} MB bound (floor {floor >> 20} + {STARTING_MB}/starting), "
                      f"host MemAvailable {host >> 20} MB (floor {HOST_FLOOR >> 20})")
                subprocess.run(["rm", "-rf", rd])
                return
            time.sleep(0.5)
        so = open(f"{rd}/stdout.txt", "w")
        pid = os.fork()
        if pid == 0:                                   # detached: own session, joined to the pool, outlives this ssh command
            os.setsid()
            join()
            os.dup2(os.open("/dev/null", os.O_RDONLY), 0)
            os.dup2(so.fileno(), 1)
            os.dup2(os.open("/dev/null", os.O_WRONLY), 2)
            # sh stays bwrap's parent (`; :` = no exec): --die-with-parent would otherwise end the session with this
            # pool.py; killing the sh (pool.py kill) takes bwrap and its pid namespace (wine, wineserver) down with it
            os.execv("/bin/sh", ["/bin/sh", "-c", '"$@"; :', "sh"] + bwrap_argv(chdir=f"/ol2/runs/drive/{name}") +
                     ["wine", spec["exe"]] + spec["args"])
        so.close()
        time.sleep(0.3)
        fcntl.flock(lk, fcntl.LOCK_UN)
    t0 = time.time()
    while not os.path.exists(f"{rd}/live.port"):
        if not os.path.exists(f"/proc/{pid}") or open(f"/proc/{pid}/stat").read().split(") ")[1][0] == "Z" or time.time() - t0 > 300:
            tail = open(f"{rd}/rhost.log", errors="replace").read()[-600:] if os.path.exists(f"{rd}/rhost.log") else ""
            print(f"NOSTART pid {pid}\n{tail}")
            return
        time.sleep(0.05)
    print(f"LIVE {pid} {open(f'{rd}/live.port').read().split()[0]} ")


def ours(pid, name):
    """PID is this session's container (its argv names the session's run dir)"""
    t = (RUN_WIN + name).encode()
    try:
        args = open(f"/proc/{pid}/cmdline", "rb").read().split(b"\0")
    except OSError:
        return False
    return any(a == t or a.startswith(t + b"\\") for a in args)


def cmd_alive(argv):
    pid, name = int(argv[0]), argv[1]
    print("ALIVE" if NAME_RE.fullmatch(name) and ours(pid, name) else "DEAD")


def cmd_kill(argv):
    kill(int(argv[0]), argv[1])
    print("KILLED")


def kill(pid, name):
    if not NAME_RE.fullmatch(name):
        raise SystemExit(f"bad session name {name!r}")
    if ours(pid, name):
        try:
            os.kill(pid, 9)                            # bwrap --die-with-parent: its pid namespace (wine, wineserver) goes with it
        except ProcessLookupError:
            pass
        for _ in range(100):
            if name not in sessions():
                break
            time.sleep(0.05)
        for p in sessions().get(name, []):             # stragglers outside the namespace's reach (none expected)
            if ours(p, name):
                try:
                    os.kill(p, 9)
                except ProcessLookupError:
                    pass
    subprocess.run(["rm", "-rf", f"{SESS}/{name}"])


def proc_stat(p):
    """→ (utime + stime ticks, start ticks since boot)"""
    f = open(f"/proc/{p}/stat").read().rsplit(") ", 1)[1].split()
    return int(f[11]) + int(f[12]), int(f[19])


def cmd_reap(argv):
    dry = "--dry" in argv
    tck = os.sysconf("SC_CLK_TCK")
    btime = next(int(l.split()[1]) for l in open("/proc/stat") if l.startswith("btime"))
    now = time.time()
    try:
        old = json.load(open(REAP))
    except (OSError, ValueError):
        old = {}
    new = {}
    for name, pids in sessions().items():
        cpu, start, root, idle = 0, None, None, 1800
        for p in pids:
            try:
                c, st = proc_stat(p)
                cl = open(f"/proc/{p}/cmdline", "rb").read().split(b"\0")
            except OSError:
                continue
            cpu += c
            if cl and cl[0] == b"/bin/sh":
                root, start = p, st
            if b"--live-idle" in cl:
                idle = int(cl[cl.index(b"--live-idle") + 1] or 1800)
        if root is None:
            continue
        key = f"{name}/{start}"
        o = old.get(key)
        since = o["since"] if o and o["cpu"] == cpu else now
        age = now - (btime + start / tck)
        why = (f"hung: no CPU for {now - since:.0f}s (--live-idle {idle})" if now - since > idle + REAP_SLACK else
               f"over age ({age / 3600:.1f} h)" if age > REAP_MAX_AGE else None)
        if why:
            print(f"REAP {name} pid {root}: {why}" + (" (dry)" if dry else ""), flush=True)
            if not dry:
                kill(root, name)
                continue
        new[key] = {"cpu": cpu, "since": since}
    if not dry:
        json.dump(new, open(REAP + ".tmp", "w"))
        os.replace(REAP + ".tmp", REAP)


def cmd_ports(argv):
    """the bunker-side ports of the live sessions (rd reap cancels forwards to any other)"""
    ss = sessions()
    print(" ".join(open(f"{SESS}/{n}/live.port").read().split()[0] for n in ss if os.path.exists(f"{SESS}/{n}/live.port")))


def cmd_stat(argv):
    try:
        free, host = pool_free()
        peak = int(open(CG + "/memory.peak").read()) >> 20
    except OSError:
        print("pool: down")
        return
    ss = sessions()
    print(f"pool: {len(ss)} live session(s), {free >> 20} MB free in the bound (peak {peak} MB), host MemAvailable {host >> 20} MB")
    for n in sorted(ss):
        print(f"  {n}  pids {' '.join(map(str, ss[n]))}" + ("" if os.path.exists(f"{SESS}/{n}/live.port") else "  (starting)"))


def main():
    cmd, argv = sys.argv[1], sys.argv[2:]
    {"ensure": lambda a: ensure(), "run": cmd_run, "spawn": cmd_spawn, "alive": cmd_alive, "kill": cmd_kill,
     "stat": cmd_stat, "reap": cmd_reap, "ports": cmd_ports, "free": lambda a: print(*[x >> 20 for x in pool_free()])}[cmd](argv)


if __name__ == "__main__":
    main()
