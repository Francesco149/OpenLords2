#!/usr/bin/env python3
"""mpbot.py — author a multiplayer game live: N rhost processes (rhbot control channels, rhbot.py) joined through
rh_net's broker (rhnet.py), always stepped TOGETHER (every frame advance goes to every side), so their virtual clocks
never stall each other. A session is a background server; scripts are sent to it:

    mpbot.py serve NAME [-n 2] [--target wine]        start (boots every side to the title, frame 1500), stays up
    mpbot.py do NAME "run 600; p1 click 320 139; both click 430 226; shot lobby"
    mpbot.py py NAME "u = UI(p1); actions.raise_army(p1, u, 100)"   (tools/bot on a side; out(...) prints)
    mpbot.py save NAME SCEN [--force]                 → tests/scenarios/SCEN-p1 … (each side's input + deliveries)
    mpbot.py quit NAME

Steps (`;`-separated): run N | [p1|p2|..|both] click X Y | rclick X Y | move X Y | key VK [CHAR] | type TEXT |
  u32 VA | read VA N | shot TAG (every side: <rundir>/shot_TAG.png, a montage path is printed) | wait pK SCREEN [MAX].
`pK` addresses side K (player K-1; p1 hosts); an input step without one goes to p1. Inputs are posted to one side and
the frames after them run on all.  The out-traces replay alone (rhost.py run / corpus), like mp-record's."""
import json
import os
import socket
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rhost  # noqa: E402
import rhbot  # noqa: E402
import rhnet  # noqa: E402

STATE = os.path.join(rhost.EPH, "mpbot")


class Side:
    """one side as an rhbot Session (tools/bot's actions / UI take it): inputs go to this side, every frame advance
    runs all sides"""
    def __init__(self, mp, k):
        self.mp, self.k, self.s = mp, k, mp.s[k]
        self.name = self.s.name

    def run(self, n=1):
        self.mp.run(int(n))
        return self.s.status

    def post(self, m, w=0, l=0):
        return self.s.post(m, w, l)

    def read(self, va, n):
        return self.s.read(va, n)

    def u8(self, va):
        return self.s.u8(va)

    def u16(self, va):
        return self.s.u16(va)

    def u32(self, va):
        return self.s.u32(va)

    def i32(self, va):
        return self.s.i32(va)

    def shot(self, tag):
        return self.s.shot(tag)

    def move(self, x, y, frames=None):
        self.s.post(rhbot.WM_MOUSEMOVE, 0, rhbot.lp(x, y))
        self.mp.run(frames or 2)

    def click(self, x, y, right=False, frames=None):
        self.mp.click(self.k, x, y, right=right, hold=frames or 2)

    def key(self, vk, char=None, frames=None):
        self.mp.key(self.k, vk, char, hold=frames or 2)

    def type(self, text):
        for ch in text:
            self.key(ord(ch.upper()) if ch.isalnum() else (0x20 if ch == " " else 0), ch)

    @property
    def frame(self):
        return self.s.frame

    @property
    def screen(self):
        return self.s.screen

    @property
    def status(self):
        return self.s.status


class MP:
    def __init__(self, name, n=2, target="wine", log=print):
        self.n, self.name = n, name
        self.broker = rhnet.Broker(n, log=log)
        self.s = []
        for k in range(n):
            self.s.append(rhbot.Session(f"{name}-p{k + 1}", target=target,
                                        extra=["--net-sigs", f"{k}/{n}", "--net-peer", f"127.0.0.1:{self.broker.port}"]))

    def run(self, frames):
        errs = []

        def one(s):
            try:
                s.run(frames)
            except Exception as e:   # noqa: BLE001
                errs.append(e)
        ts = [threading.Thread(target=one, args=(s,)) for s in self.s]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
        if errs:
            raise errs[0]

    def click(self, k, x, y, right=False, hold=2):
        down, up, mk = ((rhbot.WM_RBUTTONDOWN, rhbot.WM_RBUTTONUP, rhbot.MK_RBUTTON) if right
                        else (rhbot.WM_LBUTTONDOWN, rhbot.WM_LBUTTONUP, rhbot.MK_LBUTTON))
        ks = range(self.n) if k is None else [k]
        for kk in ks:
            self.s[kk].post(rhbot.WM_MOUSEMOVE, 0, rhbot.lp(x, y))
        self.run(hold)
        for kk in ks:
            self.s[kk].post(down, mk, rhbot.lp(x, y))
        self.run(hold)
        for kk in ks:
            self.s[kk].post(up, 0, rhbot.lp(x, y))
        self.run(hold)

    def key(self, k, vk, ch=None, hold=2):
        ks = range(self.n) if k is None else [k]
        for kk in ks:
            self.s[kk].post(rhbot.WM_KEYDOWN, vk, 1)
            if ch is not None:
                self.s[kk].post(rhbot.WM_CHAR, ord(ch) if isinstance(ch, str) else ch, 1)
        self.run(hold)
        for kk in ks:
            self.s[kk].post(rhbot.WM_KEYUP, vk, 0xC0000001)
        self.run(hold)

    def status(self):
        return " | ".join(f"p{k + 1} f{s.frame} scr {s.screen:#x} tick {s.status.get('tick')}" for k, s in enumerate(self.s))

    def script(self, text, out):
        for step in [x.strip() for x in text.replace("\n", ";").split(";") if x.strip()]:
            w = step.split()
            k = 0
            if w[0] == "both":
                k, w = None, w[1:]
            elif w[0][:1] == "p" and w[0][1:].isdigit():
                k, w = int(w[0][1:]) - 1, w[1:]
            op, a = w[0], w[1:]
            if op == "run":
                self.run(int(a[0]))
            elif op in ("click", "rclick"):
                self.click(k, int(a[0]), int(a[1]), right=op == "rclick")
            elif op == "move":
                for kk in (range(self.n) if k is None else [k]):
                    self.s[kk].post(rhbot.WM_MOUSEMOVE, 0, rhbot.lp(int(a[0]), int(a[1])))
                self.run(2)
            elif op == "key":
                self.key(k, int(a[0], 0), a[1] if len(a) > 1 else None)
            elif op == "type":
                for ch in " ".join(a).replace("_", " "):
                    self.key(k, ord(ch.upper()) if ch.isalnum() else (0x20 if ch == " " else 0), ch)
            elif op == "u32":
                for kk in (range(self.n) if k is None else [k]):
                    out(f"  p{kk + 1} {a[0]} = {self.s[kk].u32(int(a[0], 16)):#x} ({self.s[kk].i32(int(a[0], 16))})")
            elif op == "read":
                for kk in (range(self.n) if k is None else [k]):
                    out(f"  p{kk + 1} {a[0]}: {self.s[kk].read(int(a[0], 16), int(a[1], 0)).hex()}")
            elif op == "shot":
                ps = [s.shot(a[0]) for s in self.s]
                m = rhbot.montage(ps, os.path.join(STATE, f"{self.name}_{a[0]}.png"), cols=self.n, scale=1.0)
                out(f"  shot {m}")
            elif op == "wait":
                want, mx = int(a[0], 0), int(a[1]) if len(a) > 1 else 3000
                kk = 0 if k is None else k
                for _ in range(mx // 10):
                    if self.s[kk].screen == want:
                        break
                    self.run(10)
            else:
                raise ValueError(f"unknown step {step!r}")
            out(f"{step:30s} -> {self.status()}")

    def save(self, scen, force=False):
        made = []
        for k, s in enumerate(self.s):
            src = os.path.join(s.rundir, "bot.jsonl")
            d = os.path.join(rhost.SCEN, f"{scen}-p{k + 1}")
            if os.path.exists(os.path.join(d, "trace.jsonl")) and not force:
                raise ValueError(f"{d}/trace.jsonl exists (--force)")
            lines = open(src).read().splitlines()
            lines = [l for l in lines if l.strip()]
            nd = sum(1 for l in lines if '"n":' in l)
            last = max([int(json.loads(l).get("f", 0)) for l in lines if l.startswith("{") and '"f":' in l] or [0])
            frames = s.frame
            os.makedirs(d, exist_ok=True)
            hdr = [f"# {scen}-p{k + 1}: side {k + 1} of {self.n} of a SIGS game authored over mpbot (rh_net.c); "
                   f"{nd} deliveries stamped by receive poll — replays alone"]
            open(os.path.join(d, "trace.jsonl"), "w").write(
                "\n".join(hdr + lines + [json.dumps({"meta": {"frames": frames}})]) + "\n")
            made.append(f"{d} ({len(lines)} lines, {nd} deliveries, frames {frames}, last input f{last})")
        return made

    def quit(self):
        for s in self.s:
            try:
                s.quit()
            except Exception:   # noqa: BLE001
                pass


_ENV = {}


def py_env(mp, out):
    if not _ENV:
        sys.path.insert(0, os.path.join(rhost.ROOT, "tools", "bot"))
        import actions, l2state, mapview, ui as uimod   # noqa: E401
        _ENV.update(actions=actions, l2state=l2state, mapview=mapview, UI=uimod.UI, uimod=uimod)
        for k in range(mp.n):
            _ENV[f"p{k + 1}"] = Side(mp, k)
        _ENV["mp"] = mp
        uis = [_ENV["UI"](_ENV[f"p{k + 1}"], log=lambda m: _ENV["out"](m)) for k in range(mp.n)]
        _ENV["uis"] = uis

        def settle_all(max_frames=6000):
            """every side to an idle map (each side's UI answers its own scrolls / prompts)"""
            idle = 0
            for _ in range(max_frames // 40):
                for u in uis:
                    try:
                        u.settle(max_frames=40, step=20)
                    except uimod.Stuck:
                        pass
                if all(u.s.screen == 0 and not u.modal() for u in uis):
                    idle += 1
                    if idle >= 2:
                        return True
                else:
                    idle = 0
            return False

        def end_turn_all(max_frames=30000):
            """every side presses End turn; returns when every side's season moved and the maps are idle"""
            before = [(u.season(), u.year()) for u in uis]
            for u in uis:
                u.s.move(*uimod.END_TURN, frames=4)
                u.s.click(*uimod.END_TURN)
            for _ in range(max_frames // 400):
                settle_all(400)
                if all((u.season(), u.year()) != b for u, b in zip(uis, before)) and settle_all(2000):
                    return [(u.season(), u.year()) for u in uis]
            raise uimod.Stuck(f"end_turn_all: seasons {[(u.season(), u.year()) for u in uis]} from {before}")
        _ENV.update(settle_all=settle_all, end_turn_all=end_turn_all)
    _ENV["out"] = lambda *a: out(" ".join(str(x) for x in a))
    _ENV["print"] = _ENV["out"]
    return _ENV


def serve(name, n, target):
    os.makedirs(STATE, exist_ok=True)
    mp = MP(name, n, target)
    mp.run(1500)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", 0))
    srv.listen(4)
    open(os.path.join(STATE, f"{name}.port"), "w").write(str(srv.getsockname()[1]))
    print(f"mpbot {name}: {n} sides at the title ({mp.status()}); port {srv.getsockname()[1]}", flush=True)
    srv.settimeout(3600)
    while True:
        try:
            c, _ = srv.accept()
        except socket.timeout:
            break
        f = c.makefile("rwb")
        req = json.loads(f.readline())
        lines = []
        try:
            if req["op"] == "do":
                mp.script(req["steps"], lines.append)
            elif req["op"] == "py":          # tools/bot on a side: p1, p2 … (Side), mp, UI, actions, l2state, mapview
                env = py_env(mp, lines.append)
                exec(compile(req["code"], "<mpbot py>", "exec"), env)
                lines.append(mp.status())
            elif req["op"] == "save":
                lines += mp.save(req["scen"], req.get("force"))
            elif req["op"] == "quit":
                lines.append("quitting")
            lines.append("OK")
        except Exception as e:   # noqa: BLE001
            lines.append(f"ERROR {e!r}")
        f.write(("\n".join(lines) + "\n").encode())
        f.flush()
        f.close()
        c.shutdown(socket.SHUT_RDWR)
        c.close()
        if req["op"] == "quit":
            break
    mp.quit()
    os.unlink(os.path.join(STATE, f"{name}.port"))


def client(name, req):
    port = int(open(os.path.join(STATE, f"{name}.port")).read())
    c = socket.create_connection(("127.0.0.1", port))
    c.sendall((json.dumps(req) + "\n").encode())
    out = b""
    while True:
        b = c.recv(65536)
        if not b:
            break
        out += b
    print(out.decode(), end="")
    return 0 if out.decode().rstrip().endswith("OK") or "quitting" in out.decode() else 1


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="op", required=True)
    p = sub.add_parser("serve"); p.add_argument("name"); p.add_argument("-n", type=int, default=2)
    p.add_argument("--target", default="wine", choices=("wine", "win"))
    p = sub.add_parser("do"); p.add_argument("name"); p.add_argument("steps")
    p = sub.add_parser("py"); p.add_argument("name"); p.add_argument("code")
    p = sub.add_parser("save"); p.add_argument("name"); p.add_argument("scen"); p.add_argument("--force", action="store_true")
    p = sub.add_parser("quit"); p.add_argument("name")
    a = ap.parse_args()
    if a.op == "serve":
        serve(a.name, a.n, a.target)
    elif a.op == "do":
        sys.exit(client(a.name, {"op": "do", "steps": a.steps}))
    elif a.op == "py":
        sys.exit(client(a.name, {"op": "py", "code": a.code}))
    elif a.op == "save":
        sys.exit(client(a.name, {"op": "save", "scen": a.scen, "force": a.force}))
    else:
        sys.exit(client(a.name, {"op": "quit"}))
