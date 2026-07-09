#!/usr/bin/env python3
"""Thin client for the interactive probe daemon (tools/probe_daemon.py).

Sends one command to a running daemon (auto-discovered via
runs/probe/daemon.json) and prints the JSON reply.  Game state persists in the
daemon between calls, so you can click, screenshot, read memory, iterate.

    tools/probe.py shot                 # screenshot the live screen -> PNG path
    tools/probe.py shot /tmp/x.png      # ... to a specific path
    tools/probe.py click 558 469        # left-click at GAME px (558,469)
    tools/probe.py click 320 107 r      # right-click
    tools/probe.py move 300 300         # move cursor only (hover)
    tools/probe.py key 27               # press VK_ESCAPE (0x1B)
    tools/probe.py read 0x553edc i32    # read a VA
    tools/probe.py reads a=0x553edc:i32 b=0x553fc8:i32
    tools/probe.py state                # curated turn/treasury dump
    tools/probe.py poke 0x53e8fc 1      # write byte(s)
    tools/probe.py anchors              # last-fired anchor frames
    tools/probe.py sleep 500            # let the game advance ~500ms wall
    tools/probe.py quit                 # stop daemon + kill game

Coordinates are GAME space (640x480); the agent rescales to the pinned client.
"""
from __future__ import annotations

import json
import socket
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONTROL_JSON = ROOT / "runs" / "probe" / "daemon.json"


def _port() -> int:
    for i, a in enumerate(sys.argv):
        if a == "--port":
            return int(sys.argv[i + 1])
    if CONTROL_JSON.exists():
        return int(json.loads(CONTROL_JSON.read_text())["port"])
    return 27099


def _send(req: dict) -> dict:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(30.0)
    s.connect(("127.0.0.1", _port()))
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode()) if buf else {"ok": False, "err": "no reply"}


def _build(argv: list[str]) -> dict:
    # strip --port PORT
    a = []
    i = 0
    while i < len(argv):
        if argv[i] == "--port":
            i += 2
            continue
        a.append(argv[i])
        i += 1
    if not a:
        print(__doc__)
        sys.exit(2)
    cmd, rest = a[0], a[1:]
    if cmd == "shot":
        return {"cmd": "shot", "path": rest[0] if rest else None}
    if cmd == "click":
        return {"cmd": "click", "x": int(rest[0]), "y": int(rest[1]),
                "button": (rest[2] if len(rest) > 2 else "l")}
    if cmd == "move":
        return {"cmd": "move", "x": int(rest[0]), "y": int(rest[1])}
    if cmd == "postraw":                    # postraw M W X Y  (lp=(y<<16)|x)
        m, w, x, y = int(rest[0], 0), int(rest[1], 0), int(rest[2]), int(rest[3])
        return {"cmd": "postraw", "m": m, "w": w,
                "l": ((y & 0xFFFF) << 16) | (x & 0xFFFF)}
    if cmd == "key":
        return {"cmd": "key", "vk": int(rest[0], 0),
                "mode": (rest[1] if len(rest) > 1 else "press")}
    if cmd == "read":
        return {"cmd": "read", "va": int(rest[0], 0),
                "type": (rest[1] if len(rest) > 1 else "i32")}
    if cmd == "reads":
        specs = []
        for tok in rest:                       # name=VA:type
            name, _, vt = tok.partition("=")
            va, _, ty = vt.partition(":")
            specs.append({"name": name, "va": int(va, 0), "type": ty or "i32"})
        return {"cmd": "reads", "specs": specs}
    if cmd == "state":
        return {"cmd": "state", "nplayers": int(rest[0]) if rest else 5}
    if cmd == "endturn":
        return {"cmd": "endturn", "timeout": float(rest[0]) if rest else 10.0}
    if cmd == "poke":
        return {"cmd": "poke", "va": int(rest[0], 0),
                "bytes": [int(b, 0) & 0xFF for b in rest[1].split(",")]}
    if cmd in ("anchors", "ping", "quit"):
        return {"cmd": cmd}
    if cmd == "autodismiss":
        return {"cmd": "autodismiss", "n": int(rest[0]) if rest else 1}
    if cmd == "sleep":
        return {"cmd": "sleep", "ms": float(rest[0])}
    print(f"unknown command: {cmd}", file=sys.stderr)
    sys.exit(2)


def main():
    reply = _send(_build(sys.argv[1:]))
    print(json.dumps(reply, indent=2))
    sys.exit(0 if reply.get("ok") else 1)


if __name__ == "__main__":
    main()
