#!/usr/bin/env python3
"""Autonomous economic-opening player — applies a turtle policy (tax + rations)
on the home county, then drives turns and logs the economy developing.  This is
the difference between PASSING turns (play_game.py) and PLAYING a retail LoTR2
game: it makes real decisions via the mapped action commands (settax / setrations
/ endturn) and measures the human economy against the AIs' (the cheat signal).

    # daemon up (headless) first, then:
    nix develop -c python3 tools/play_strategy.py --turns 15 --tax 4 --rations 3
"""
from __future__ import annotations

import argparse
import csv
import json
import socket
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTL = ROOT / "runs" / "probe" / "daemon.json"
SEASON = {1: "Spr", 2: "Sum", 3: "Aut", 4: "Win"}


def send(req: dict) -> dict:
    if not CTL.exists():
        sys.exit("no probe daemon (start tools/probe_daemon.py --present headless first)")
    port = json.loads(CTL.read_text())["port"]
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(120.0)
    s.connect(("127.0.0.1", port))
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        c = s.recv(65536)
        if not c:
            break
        buf += c
    s.close()
    return json.loads(buf.decode())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--turns", type=int, default=15)
    ap.add_argument("--tax", type=int, default=4, help="tax %% on the home county")
    ap.add_argument("--rations", type=int, default=3, help="0 None..5 Triple (3=Normal)")
    ap.add_argument("--out", default=str(ROOT / "runs" / "probe" / "strategy_log"))
    args = ap.parse_args()

    send({"cmd": "tomap"})
    print(f"economic-opening policy: tax {args.tax}% + rations level {args.rations}")
    print("  settax    ->", send({"cmd": "settax", "pct": args.tax}))
    print("  setrations->", send({"cmd": "setrations", "level": args.rations}))

    rows = []
    for t in range(1, args.turns + 1):
        r = send({"cmd": "endturn", "timeout": 25})
        st = r.get("state", {})
        adv = bool(r.get("advanced"))
        row = {"turn": t, "year": st.get("year"),
               "season": SEASON.get(st.get("season"), "?"),
               "human_cash": st.get("cash1"),
               "ai2_cash": st.get("cash2"), "ai3_cash": st.get("cash3"),
               "ai4_cash": st.get("cash4")}
        rows.append(row)
        print(f"  turn {t:>2}: {row['year']} {row['season']} | "
              f"human {row['human_cash']:>5} | "
              f"AI {[row['ai2_cash'], row['ai3_cash'], row['ai4_cash']]}"
              + ("" if adv else "  STALL"))
        if not adv:
            break

    p = Path(args.out + ".csv")
    with p.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {p} ({len(rows)} turns)")


if __name__ == "__main__":
    main()
