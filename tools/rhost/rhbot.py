#!/usr/bin/env python3
"""rhbot.py — drive a live rhost run from Python over the control channel (rh_bot.c).

    from rhbot import Session
    with Session("explore", target="wine") as s:      # or target="win" (ssh -R tunnel)
        s.run(600)                                     # boot to the title
        s.click(320, 107)                              # "Single player"
        s.run(120); s.shot("menu")                     # → <rundir>/shot_menu.png
        print(s.u32(0x57c998))                         # retail memory
    # s.out_trace: every input posted, as a trace — `rhost.py run` replays it bit-exactly, no bot

Every input goes through rhost's virtual queue at a frame boundary and is recorded, so a session IS a
scenario. `write` pokes exist for exploration only (the run is then no longer a pure replay).
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rhost  # noqa: E402

WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_RBUTTONDOWN, WM_RBUTTONUP = 0x200, 0x201, 0x202, 0x204, 0x205
WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x100, 0x101, 0x102
MK_LBUTTON, MK_RBUTTON = 1, 2


def lp(x, y):
    return (int(y) << 16) | (int(x) & 0xffff)


class Session:
    def __init__(self, name="bot", target="wine", trace=None, extra=(), port=None, step_ms=None, vbase=None,
                 hold=2, log=None):
        self.name, self.target, self.hold = name, target, hold
        self.port = port or (27100 + os.getpid() % 800)
        self.batch = datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-bot-" + name
        meta = rhost.trace_meta(trace) if trace else {}
        win_run = f"{rhost.WROOT}\\runs\\{self.batch}\\{name}"
        args = ["--exe", f"{rhost.WROOT}\\game\\Lords2.exe", "--game", f"{rhost.WROOT}\\game", "--run", win_run,
                "--bot", f"127.0.0.1:{self.port}", "--out-trace", "bot.jsonl", "--log-every", "0"]
        if step_ms or meta.get("step_ms"):
            args += ["--step-ms", str(step_ms or meta["step_ms"])]
        if vbase or meta.get("vbase"):
            args += ["--vbase", str(vbase or meta["vbase"])]
        if trace:
            args += ["--trace", f"{win_run}\\trace.jsonl"]
        args += list(extra)
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", self.port))
        self.srv.listen(1)
        if target == "wine":
            if rhost.rhost_stale():
                rhost.cmd_build()
            rhost.wine_stage()
            self.rundir = os.path.join(rhost.WINE_ROOT, "runs", self.batch, name)
            os.makedirs(self.rundir)
            if trace:
                shutil.copy(trace, os.path.join(self.rundir, "trace.jsonl"))
            self.proc = subprocess.Popen(["wine", f"{rhost.WROOT}\\bin\\{rhost.WEXE}"] + args, env=rhost.wine_env(),
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        else:
            rhost.ensure_staged()
            self.rundir = os.path.join(rhost.LOCAL_RUNS, self.batch, name)
            os.makedirs(self.rundir)
            rhost.ssh_win(f"New-Item -ItemType Directory -Force '{win_run}' | Out-Null")
            if trace:
                rhost.scp(trace, f"{rhost.WIN}:C:/ol2-rhost/runs/{self.batch}/{name}/trace.jsonl")
            cmd = "& '" + f"{rhost.WROOT}\\bin\\{rhost.WEXE}" + "' " + " ".join("'" + x.replace("'", "''") + "'" for x in args)
            self.proc = subprocess.Popen(["ssh", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes",
                                          "-R", f"{self.port}:127.0.0.1:{self.port}", rhost.WIN, cmd],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.srv.settimeout(120)
        self.c, _ = self.srv.accept()
        self.c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.f = self.c.makefile("rwb")
        self.status = self._recv()          # the stop at connect (before retail's entry)
        self.result = None

    # ---- protocol
    def _recv(self):
        line = self.f.readline()
        if not line:
            raise ConnectionError(f"rhost closed the connection (see {self.rundir}/rhost.log)")
        return json.loads(line)

    def _cmd(self, **kw):
        self.f.write((json.dumps(kw) + "\n").encode())
        self.f.flush()
        r = self._recv()
        self.status = r
        if "error" in r:
            raise RuntimeError(f"rhost: {r['error']} for {kw}")
        return r

    @property
    def frame(self):
        return self.status.get("frame")

    @property
    def screen(self):
        return self.status.get("screen")

    def run(self, n=1):
        return self._cmd(cmd="run", n=int(n))

    def post(self, m, w=0, l=0):
        return self._cmd(cmd="post", m=m, w=w, l=l)

    def read(self, va, n):
        return bytes.fromhex(self._cmd(cmd="read", va=va, n=n)["hex"])

    def u8(self, va):
        return self.read(va, 1)[0]

    def u16(self, va):
        return int.from_bytes(self.read(va, 2), "little")

    def u32(self, va):
        return int.from_bytes(self.read(va, 4), "little")

    def i32(self, va):
        return int.from_bytes(self.read(va, 4), "little", signed=True)

    def write(self, va, data):
        return self._cmd(cmd="write", va=va, hex=bytes(data).hex())

    def hash(self):
        return self._cmd(cmd="hash")

    def dump(self, tag):
        self._cmd(cmd="dump", tag=tag)
        return self._fetch(f"state_{tag}.rhs")

    def shot(self, tag):
        self._cmd(cmd="shot", tag=tag)
        self._fetch(f"frame_{tag}.idx")
        return self._fetch(f"shot_{tag}.png")

    def _fetch(self, fname):
        p = os.path.join(self.rundir, fname)
        if self.target == "win":
            rhost.scp(f"{rhost.WIN}:C:/ol2-rhost/runs/{self.batch}/{self.name}/{fname}", p, check=False)
        return p

    # ---- input helpers (game coordinates, 640x480)
    def move(self, x, y, frames=None):
        self.post(WM_MOUSEMOVE, 0, lp(x, y))
        self.run(frames or self.hold)

    def click(self, x, y, right=False, frames=None):
        down, up, mk = (WM_RBUTTONDOWN, WM_RBUTTONUP, MK_RBUTTON) if right else (WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON)
        self.post(WM_MOUSEMOVE, 0, lp(x, y)); self.run(frames or self.hold)
        self.post(down, mk, lp(x, y)); self.run(frames or self.hold)
        self.post(up, 0, lp(x, y)); self.run(frames or self.hold)

    def key(self, vk, char=None, frames=None):
        self.post(WM_KEYDOWN, vk, 1)
        if char is not None:
            self.post(WM_CHAR, ord(char) if isinstance(char, str) else char, 1)
        self.run(frames or self.hold)
        self.post(WM_KEYUP, vk, 0xC0000001)
        self.run(frames or self.hold)

    def type(self, text):
        for ch in text:
            vk = ord(ch.upper()) if ch.isalnum() else (0x20 if ch == " " else 0)
            self.key(vk, ch)

    # ---- end
    def quit(self):
        try:
            self._cmd(cmd="quit")
        except (ConnectionError, OSError, json.JSONDecodeError):
            pass
        try:
            self.proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.c.close(); self.srv.close()
        log = self._fetch("rhost.log")
        self.out_trace = self._fetch("bot.jsonl")
        if os.path.exists(log):
            for line in open(log, errors="replace"):
                if line.startswith("RESULT "):
                    self.result = dict(kv.split("=", 1) for kv in line.split()[1:] if "=" in kv)
        return self.result

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self.result is None:
            self.quit()


if __name__ == "__main__":
    # smoke: boot, click Single player, shot
    with Session("smoke", target=sys.argv[1] if len(sys.argv) > 1 else "wine") as s:
        s.run(600)
        print("title:", s.status)
        print(s.shot("title"))
        s.click(320, 107)
        s.run(200)
        print("after click:", s.status)
        print(s.shot("after"))
    print(s.result, s.out_trace)
