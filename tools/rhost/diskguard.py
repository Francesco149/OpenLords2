"""diskguard — refuse to start rhost work on a host whose disk is low (2026-11-17).

A full C: on the shared Windows box wedged its sshd sessions (the real cause behind "ssh win collapsed"). Every path that
starts runs on a host stages first, and staging calls need(host): rhost.ensure_staged (win), rhost.wine_stage (wine /
maximus), bunker.stage (bunker), drive.staged_exe (rd live sessions), rhost play-install / play-test.

Below the floor → DiskLow (SystemExit, exit code 75): the batch / session does not start. The agent must NOT free space
itself (the Windows box and bunker are shared; maximus run output goes to cold via `offload park` — the orchestrator's
call) and must NOT route around it onto another host: it reports to the orchestrator and waits.

Floors (GB free): RHOST_DISK_FLOOR_WIN (20), RHOST_DISK_FLOOR_LOCAL (15), RHOST_DISK_FLOOR_BUNKER (10).
RHOST_DISK_GUARD=0 disables the check — only when the orchestrator says so.
One query per host per process every RECHECK_S (a long rd search re-checks as it spawns sessions).
"""
import os
import shutil
import sys
import time

EXIT = 75                     # EX_TEMPFAIL
RECHECK_S = 600
FLOOR = {"win": float(os.environ.get("RHOST_DISK_FLOOR_WIN", "20")),
         "wine": float(os.environ.get("RHOST_DISK_FLOOR_LOCAL", "15")),
         "bunker": float(os.environ.get("RHOST_DISK_FLOOR_BUNKER", "10"))}
_seen = {}                    # host → (time, free GB)


class DiskLow(SystemExit):
    def __init__(self, msg):
        self.msg = msg
        print(msg, flush=True)
        super().__init__(EXIT)


def _mod(name, attr):
    """the already-loaded module (rhost.py / bunker.py may be __main__: a fresh import would be a second copy)"""
    for m in (sys.modules.get(name), sys.modules.get("__main__")):
        if m is not None and hasattr(m, attr):
            return m
    return __import__(name)


def _free_win():
    rhost = _mod("rhost", "ssh_win")
    out = rhost.ssh_win("[math]::Floor((Get-PSDrive C).Free / 1MB)", timeout=60, check=False).strip()
    return int(out.splitlines()[-1]) / 1024, f"{rhost.WIN} C:"


def _free_wine():
    rhost = _mod("rhost", "EPH")
    p = rhost.EPH if os.path.isdir(rhost.EPH) else os.path.expanduser("~")
    return shutil.disk_usage(os.path.realpath(p)).free / 2 ** 30, f"maximus {os.path.realpath(p)}"


def _free_bunker():
    bunker = _mod("bunker", "POOL")
    out = bunker.ssh(f"df -Pk {bunker.W} 2>/dev/null || df -Pk /var/lib", check=False, capture=True, timeout=60)
    out = out.decode() if isinstance(out, bytes) else out
    return int(out.strip().splitlines()[-1].split()[3]) / 2 ** 20, f"bunker {bunker.HOST} /var/lib"


def free_gb(host):
    """(free GB, where) — raises on a failed query"""
    return {"win": _free_win, "wine": _free_wine, "bunker": _free_bunker}[host]()


def need(host):
    """raise DiskLow when HOST (win / wine / bunker) has less free disk than its floor"""
    if os.environ.get("RHOST_DISK_GUARD", "1") == "0" or host not in FLOOR:
        return
    t = _seen.get(host)
    if t and time.time() - t[0] < RECHECK_S and t[1] >= FLOOR[host]:
        return
    try:
        gb, where = free_gb(host)
    except (Exception, SystemExit) as e:   # host unreachable / odd output: the run's own ssh reports that; don't mask it
        print(f"[diskguard: could not read free disk on {host}: {str(e)[:200]} — not checked]", flush=True)
        return
    _seen[host] = (time.time(), gb)
    if gb < FLOOR[host]:
        raise DiskLow(
            f"\nDISK LOW — REFUSING TO START: {where} has {gb:.1f} GB free (floor {FLOOR[host]:.0f} GB).\n"
            f"A full disk on the Windows box wedges its sshd sessions; on bunker / maximus it kills runs mid-write.\n"
            f"AGENT: STOP here. Do not delete files to make room and do not move the work to another host.\n"
            f"Tell the orchestrator: \"disk low on {where} — {gb:.1f} GB free < {FLOOR[host]:.0f} GB floor — please free "
            f"space before I continue\", then wait for the go-ahead and re-run the same command.\n"
            f"(Orchestrator: our share there — win: C:\\ol2-rhost (`rhost.py prune --hours H`), maximus: "
            f"/mnt/ephemeral/openlords2 (`offload park --fast`), bunker: `bunker.py status`.)\n")
