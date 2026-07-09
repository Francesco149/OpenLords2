#!/usr/bin/env python3
"""Drive a running probe daemon (tools/probe_daemon.py) through N end-turns,
capturing per-realm state each turn to CSV + JSON — the full-game-driving POC
and the per-turn state extractor the LoTR II video's charts need.

    # 1. boot the game to a clean map (separate shell, stays up):
    nix develop -c python3 tools/probe_daemon.py \
        --segtrace tools/traces/new_game_to_map.jsonl --rng-seed --run-id poc
    # 2. drive it:
    nix develop -c python3 tools/play_game.py --turns 20

Each turn: force click_eligible → click End-Turn → wait for the season to roll
(dismissing report/event scrolls) → snapshot the STRAT records.  Retail-only:
the port can't play a full game (§ harness-plan).  Stops early if a realm is
eliminated or the turn stops advancing.
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
SEASON_NAME = {1: "Spring", 2: "Summer", 3: "Autumn", 4: "Winter"}


def send(req: dict) -> dict:
    if not CTL.exists():
        sys.exit("no probe daemon running (start tools/probe_daemon.py first)")
    port = json.loads(CTL.read_text())["port"]
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(90.0)
    s.connect(("127.0.0.1", port))
    s.sendall((json.dumps(req) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        c = s.recv(65536)
        if not c:
            break
        buf += c
    s.close()
    return json.loads(buf.decode()) if buf else {"ok": False}


def rd(va: int) -> int:
    return send({"cmd": "read", "va": va, "type": "i32"})["val"]


def turn_no(year: int, season: int) -> int:
    # Monotonic turn index from the Winter-1268 start.  The YEAR ticks going
    # INTO Winter (season 4), so Winter is turn 0 of its displayed year:
    #   Winter y = (y-1268)*4 + 0 ; Spring/Summer/Autumn y = +1/+2/+3.
    return (year - 1268) * 4 + (0 if season == 4 else season)


def row_from_state(t: int, st: dict, adv: bool) -> dict:
    row = {"turn": t, "advanced": int(adv),
           "year": st.get("year"), "season": st.get("season"),
           "season_name": SEASON_NAME.get(st.get("season"), "?")}
    # players occupy slots 1..4 (slot 0 = neutral); slot 1 = human.
    for i in range(1, 5):
        role = "human" if st.get(f"elim{i}") == 1 else "AI"
        row[f"p{i}_role"] = role
        row[f"p{i}_pers"] = st.get(f"pers{i}")
        row[f"p{i}_cash"] = st.get(f"cash{i}")
        row[f"p{i}_active"] = st.get(f"active{i}")
        row[f"p{i}_rank"] = st.get(f"rank{i}")
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--turns", type=int, default=12)
    ap.add_argument("--timeout", type=float, default=25.0, help="per-turn resolve wait (s)")
    ap.add_argument("--out", default=str(ROOT / "runs" / "probe" / "game_log"))
    args = ap.parse_args()

    # Make sure we're on the free-roam map (dismiss the boot scroll via RMB).
    for _ in range(5):
        if rd(0x004EAC50) == 0:
            break
        send({"cmd": "click", "x": 300, "y": 240, "button": "r"})
        time.sleep(0.3)

    st0 = send({"cmd": "state"})["vals"]
    rows = [row_from_state(turn_no(st0["year"], st0["season"]), st0, True)]
    print(f"start: {st0['year']} {SEASON_NAME.get(st0['season'],'?')} | "
          f"cash(P1-4)={[st0[f'cash{i}'] for i in range(1,5)]}")

    for _ in range(args.turns):
        r = send({"cmd": "endturn", "timeout": args.timeout})
        st = r.get("state", {})
        adv = bool(r.get("advanced"))
        t = turn_no(st.get("year", st0["year"]), st.get("season", st0["season"]))
        row = row_from_state(t, st, adv)
        rows.append(row)
        cashes = [st.get(f"cash{i}") for i in range(1, 5)]
        actives = [st.get(f"active{i}") for i in range(1, 5)]
        print(f"turn {t:>3}: {row['year']} {row['season_name']:<6} | "
              f"cash(P1-4)={cashes} active={actives} "
              f"scrolls={r.get('scrolls_dismissed')}" + ("" if adv else "  <STALL>"))
        if not adv:
            print("  turn did not advance — stopping.")
            break
        live = [a for a in actives if a and a != 0]
        if len(live) <= 1:
            print("  only one realm left — game over.")
            break

    # write CSV + JSON
    csv_path = Path(args.out + ".csv")
    json_path = Path(args.out + ".json")
    with csv_path.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    json_path.write_text(json.dumps(rows, indent=2))
    print(f"\nwrote {csv_path} ({len(rows)} rows) + {json_path.name}")


if __name__ == "__main__":
    main()
