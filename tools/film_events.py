#!/usr/bin/env python3
"""Capture a controlled retail random-event proof with an audit sidecar.

This is the reusable replacement for ad-hoc screenshots that only showed the
human treasury.  It drives the *real* event pass (FUN_00448819), records the
real deck slot assigned to every county, captures the localized event scroll,
and writes a JSON ledger containing every owner's event gate and treasury
delta.

The default fixture finds the human-owned county and chooses the retail RNG
byte that assigns event 0x132 to it.  Event 0x132 is the unconditional
``treasury += 1000`` handler at FUN_00449400.  Nothing in the deck is replaced:
neighboring AI counties keep their own (usually different) real deck slots.
The proof therefore distinguishes two claims that a screenshot cannot:

* the cursor advances once per county, so counties do not all share one card;
* AI-owned counties have an event-gate byte of zero, so their assigned deck
  entries are never applied at all.

Prerequisite (keep event scrolls open long enough to photograph them):

    nix develop -c python tools/probe_daemon.py \
        --segtrace tools/traces/new_game_to_map.jsonl --rng-seed canonical \
        --no-auto-dismiss --present

Capture:

    nix develop -c python tools/film_events.py \
        --out runs/probe/events/treasury_gift

Outputs: before.png, after.png, event_card.png, result.json.  The retail state
mutated by the fixture is restored by default after all evidence is written.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ai_pov as A  # noqa: E402


ROLL = 0x00448819
SHOW_EVENT = 0x00448D7E
DECK = 0x004D6108
RNG_BYTE = 0x0058FD60
YEAR = 0x00553EDC
REALM_COUNT = 0x0056D5DC
SCREEN = 0x004EAC50
EXTENDED_PAUSE = 0x0053E910
RENDER_BUSY = 0x004E8780

REC_BASE, REC_STRIDE = 0x0053F9B0, 0x300
R_EVENT_ACTIVE, R_OWNER, R_EVENT_ID = 0x000, 0x005, 0x1AA
R_SCRATCH = (0x1A8, 0x1FB, 0x1FC, 0x1FD)

STRAT_BASE, STRAT_STRIDE = 0x0057BF00, 0x160
S_EVENT_GATE, S_ACTIVE, S_CASH = 0x005, 0x004, 0x118

EVENT_ID = 0x132
EVENT_LABEL = "Treasury gift"
EVENT_DELTA = 1000


def rec(realm: int, off: int = 0) -> int:
    return REC_BASE + realm * REC_STRIDE + off


def strat(slot: int, off: int = 0) -> int:
    return STRAT_BASE + slot * STRAT_STRIDE + off


def send(req: dict) -> dict:
    reply = A.send(req)
    if not reply.get("ok"):
        raise RuntimeError(f"daemon command failed: {req.get('cmd')}: {reply.get('err')}")
    return reply


def read(va: int, typ: str = "i32") -> int:
    return int(send({"cmd": "read", "va": va, "type": typ})["val"])


def reads(specs: list[tuple[str, int, str]]) -> dict[str, int]:
    reply = send({"cmd": "reads", "specs": [
        {"name": name, "va": va, "type": typ} for name, va, typ in specs
    ]})
    return {k: int(v) for k, v in reply["vals"].items()}


def poke(va: int, value: int, width: int = 1) -> None:
    send({"cmd": "poke", "va": va,
          "bytes": list(int(value).to_bytes(width, "little", signed=value < 0))})


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(path: Path, *, popup: bool = False) -> dict:
    """Force a complete present, then take a synchronized post-render snapshot."""
    for attempt in range(8):
        A.poke_i32(A.REDRAW, 2)
        send({"cmd": "move", "x": 300 + (attempt & 1) * 24, "y": 240})
        send({"cmd": "sleep", "ms": 180 if popup else 260})
        reply = A.send({"cmd": "shotq", "path": str(path)})
        if reply.get("ok"):
            return {"path": str(path), "width": reply.get("w"),
                    "height": reply.get("h"), "sha256": sha256(path),
                    "capture_mode": "shotq",
                    "frame_composite": reply.get("frame_composite", {})}
    raise RuntimeError(f"could not capture {path}: {reply.get('err', 'no frame')}")


def deck_values() -> list[int]:
    vals: list[int] = []
    for start in range(0, 256, 64):
        specs = [(f"d{i}", DECK + i * 2, "u16") for i in range(start, start + 64)]
        got = reads(specs)
        vals.extend(got[f"d{i}"] for i in range(start, start + 64))
    return vals


def choose_rng(deck: list[int], county: int, event_id: int) -> tuple[int, int]:
    for index, value in enumerate(deck):
        delta = (index - county) & 0xFF
        if value == event_id and delta % 2 == 0 and delta // 2 < 128:
            return delta // 2, index
    raise RuntimeError(
        f"no 7-bit RNG byte assigns event 0x{event_id:x} to county {county}")


def choose_ai_gate_probe_rng(deck: list[int], realms: range, owners: dict,
                             human: int) -> tuple[int, list[dict]]:
    """Find a real cursor position that assigns nonzero cards to AI counties.

    Prefer a seed that assigns no card to a human county, so the gate-only
    counterexample cannot produce a human popup or side effect.
    """
    best = None
    for rng in range(128):
        ai_hits, human_hits = [], []
        for r in realms:
            owner = owners[f"r{r}"]
            index = (2 * rng + r) & 0xFF
            event = deck[index]
            if not event:
                continue
            row = {"county": r, "owner": owner, "deck_index": index,
                   "assigned_event": f"0x{event:03x}"}
            if owner == human:
                human_hits.append(row)
            elif owner != 0:
                ai_hits.append(row)
        monetary_hits = [row for row in ai_hits
                         if row["assigned_event"] in ("0x131", "0x132")]
        score = (not human_hits, len(monetary_hits), len(ai_hits),
                 -len(human_hits), -rng)
        if monetary_hits and (best is None or score > best[0]):
            best = (score, rng, ai_hits)
    if best is None:
        raise RuntimeError("real deck scan found no monetary assignment to an AI county")
    return best[1], best[2]


def snapshot(realms: range, players: list[int]) -> dict:
    specs: list[tuple[str, int, str]] = [
        ("year", YEAR, "i32"), ("rng", RNG_BYTE, "u8"),
        ("viewer", A.VIEWER, "i32"), ("curterr", A.CURTERR, "i32"),
        ("screen", SCREEN, "i32"), ("extended_pause", EXTENDED_PAUSE, "i32"),
    ]
    for r in realms:
        specs.extend([
            (f"r{r}_active", rec(r, R_EVENT_ACTIVE), "u8"),
            (f"r{r}_owner", rec(r, R_OWNER), "u8"),
            (f"r{r}_event", rec(r, R_EVENT_ID), "i16"),
            *[(f"r{r}_scratch_{off:x}", rec(r, off), "u8") for off in R_SCRATCH],
        ])
    for p in players:
        specs.extend([
            (f"p{p}_active", strat(p, S_ACTIVE), "u8"),
            (f"p{p}_gate", strat(p, S_EVENT_GATE), "u8"),
            (f"p{p}_cash", strat(p, S_CASH), "i32"),
        ])
    return reads(specs)


def restore(original: dict, realms: range, players: list[int]) -> None:
    # Clear any localized event scroll before restoring the backing event row.
    # These extended-pause scrolls use the event consumer, not ordinary RMB.
    if read(EXTENDED_PAUSE) != 0:
        send({"cmd": "autodismiss", "n": 1})
        for _ in range(20):
            time.sleep(0.10)
            if read(EXTENDED_PAUSE) == 0:
                break
        send({"cmd": "autodismiss", "n": 0})
    for _ in range(4):
        if read(SCREEN) == 0 and read(EXTENDED_PAUSE) == 0:
            break
        send({"cmd": "click", "x": 300, "y": 240, "button": "r"})
        time.sleep(0.12)
    poke(YEAR, original["year"], 4)
    poke(RNG_BYTE, original["rng"])
    poke(A.VIEWER, original["viewer"], 4)
    poke(A.CURTERR, original["curterr"], 4)
    for r in realms:
        poke(rec(r, R_EVENT_ACTIVE), original[f"r{r}_active"])
        poke(rec(r, R_EVENT_ID), original[f"r{r}_event"], 2)
        for off in R_SCRATCH:
            poke(rec(r, off), original[f"r{r}_scratch_{off:x}"])
    for p in players:
        poke(strat(p, S_CASH), original[f"p{p}_cash"], 4)
    A.poke_i32(A.REDRAW, 2)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=Path("runs/probe/events/treasury_gift"))
    ap.add_argument("--county", type=int, default=None,
                    help="human-owned county record (default: auto-detect the first one)")
    ap.add_argument("--cash", type=int, default=1000,
                    help="normalize every active player's treasury before the roll")
    ap.add_argument("--event", type=lambda s: int(s, 0), default=EVENT_ID,
                    help="event id to steer onto the human county (default: 0x132)")
    ap.add_argument("--leave-staged", action="store_true",
                    help="do not restore mutated retail state after capture")
    args = ap.parse_args()

    send({"cmd": "ping"})
    args.out.mkdir(parents=True, exist_ok=True)
    send({"cmd": "tomap"})

    nrealms = read(REALM_COUNT)
    if not 1 <= nrealms <= 0x96:
        raise RuntimeError(f"implausible realm/county count: {nrealms}")
    realms = range(1, nrealms + 1)
    owners = reads([(f"r{r}", rec(r, R_OWNER), "u8") for r in realms])
    players = sorted({owners[f"r{r}"] for r in realms if owners[f"r{r}"]})
    gates = reads([(f"p{p}", strat(p, S_EVENT_GATE), "u8") for p in players])
    human_players = [p for p in players if gates[f"p{p}"] != 0]
    if len(human_players) != 1:
        raise RuntimeError(f"expected exactly one event-enabled player, found {human_players}")
    human = human_players[0]
    human_counties = [r for r in realms if owners[f"r{r}"] == human]
    county = args.county if args.county is not None else human_counties[0]
    if county not in human_counties:
        raise RuntimeError(f"county {county} is not owned by human slot {human}: {human_counties}")

    deck = deck_values()
    rng, event_index = choose_rng(deck, county, args.event)
    gate_probe_rng, gate_probe_hits = choose_ai_gate_probe_rng(
        deck, realms, owners, human)
    original = snapshot(realms, players)
    captures: dict[str, dict] = {}
    try:
        # The event pass gates at year > 0x4f4 (1268).  Normalize treasuries so
        # its sole monetary mutation is obvious in both the HUD and the ledger.
        poke(YEAR, 1270, 4)
        poke(RNG_BYTE, rng)
        poke(A.VIEWER, human, 4)
        poke(A.CURTERR, county, 4)
        poke(SCREEN, 0, 4)
        poke(RENDER_BUSY, 0)
        for p in players:
            poke(strat(p, S_CASH), args.cash, 4)
        for r in realms:
            poke(rec(r, R_EVENT_ACTIVE), 0)
            poke(rec(r, R_EVENT_ID), 0, 2)

        # First run a gate-only counterexample with the untouched retail deck:
        # at least one AI county is genuinely assigned a nonzero/different card,
        # while no human county is assigned one. The AI event ids must still
        # remain zero because owner.event_gate==0 is tested before application.
        gate_before = snapshot(realms, players)
        send({"cmd": "callq", "va": ROLL, "args": [], "abi": "mscdecl",
              "pokes": [{"va": RNG_BYTE, "bytes": [gate_probe_rng]}]})
        gate_after = snapshot(realms, players)
        for row in gate_probe_hits:
            r = row["county"]
            row["applied_event"] = (f"0x{gate_after[f'r{r}_event']:03x}"
                                    if gate_after[f"r{r}_event"] else None)
        gate_probe_checks = {
            "real_deck_assigns_nonzero_card_to_ai": bool(gate_probe_hits),
            "assigned_ai_cards_are_not_applied": all(
                gate_after[f"r{row['county']}_event"] == 0 for row in gate_probe_hits),
            "ai_treasuries_unchanged_in_gate_probe": all(
                gate_after[f"p{p}_cash"] == gate_before[f"p{p}_cash"]
                for p in players if p != human),
        }
        if not all(gate_probe_checks.values()):
            raise RuntimeError(f"AI event-gate proof failed: {gate_probe_checks}")

        # Reset the event rows before the visible 0x132 run.
        for p in players:
            poke(strat(p, S_CASH), args.cash, 4)
        for r in realms:
            poke(rec(r, R_EVENT_ACTIVE), 0)
            poke(rec(r, R_EVENT_ID), 0, 2)
            for off in R_SCRATCH:
                poke(rec(r, off), 0)

        before = snapshot(realms, players)
        captures["before"] = capture(args.out / "before.png")

        # RNG_BYTE is a live mirror updated by the sim.  Apply it inside the
        # same engine-thread transaction as the roll so no intervening tick can
        # consume/replace the steered value.
        rolled = send({"cmd": "callq", "va": ROLL, "args": [], "abi": "mscdecl",
                       "pokes": [{"va": RNG_BYTE, "bytes": [rng]}]})
        time.sleep(0.12)
        after = snapshot(realms, players)

        rows = []
        other_human_events = []
        for r in realms:
            owner = owners[f"r{r}"]
            index = (2 * rng + r) & 0xFF
            assigned = deck[index]
            gate = gates.get(f"p{owner}", 0)
            applied = after[f"r{r}_event"]
            delta = after[f"p{owner}_cash"] - before[f"p{owner}_cash"] if owner else 0
            controller = ("human" if owner == human else
                          "ai" if owner in players else "neutral")
            row = {
                "county": r, "owner": owner,
                "controller": controller,
                "event_gate": gate, "deck_index": index,
                "assigned_event": f"0x{assigned:03x}" if assigned else None,
                "applied_event": f"0x{applied:03x}" if applied else None,
                "owner_treasury_before": before.get(f"p{owner}_cash", 0),
                "owner_treasury_after": after.get(f"p{owner}_cash", 0),
                "owner_treasury_delta": delta,
            }
            rows.append(row)
            if owner == human and r != county and assigned:
                other_human_events.append(row)

        if other_human_events:
            raise RuntimeError(
                "fixture is not isolated: another human county received an event: "
                + json.dumps(other_human_events))
        human_delta = after[f"p{human}_cash"] - before[f"p{human}_cash"]
        ai_deltas = {str(p): after[f"p{p}_cash"] - before[f"p{p}_cash"]
                     for p in players if p != human}
        checks = {
            "human_event_id_is_requested": after[f"r{county}_event"] == args.event,
            "human_treasury_delta_is_1000": human_delta == EVENT_DELTA,
            "all_ai_event_gates_are_zero": all(gates[f"p{p}"] == 0
                                                 for p in players if p != human),
            "all_ai_treasury_deltas_are_zero": all(v == 0 for v in ai_deltas.values()),
            "all_ai_applied_event_ids_are_zero": all(
                after[f"r{r}_event"] == 0 for r in realms
                if owners[f"r{r}"] in players and owners[f"r{r}"] != human),
        }
        if not all(checks.values()):
            raise RuntimeError(f"controlled event proof failed: {checks}")

        # The map tick normally consumes the active human event immediately.
        # If the fixture lands between those ticks, invoke that exact consumer
        # once ourselves. Either route is FUN_00448d7e and posts the localized
        # retail scroll only for the local owner.
        popup_state = None
        for _ in range(8):
            time.sleep(0.10)
            state = reads([("screen", SCREEN, "i32"),
                           ("extended_pause", EXTENDED_PAUSE, "i32")])
            if state["screen"] == 0x27 or state["extended_pause"] != 0:
                popup_state = state
                break
        if popup_state is None and read(rec(county, R_EVENT_ACTIVE), "u8"):
            send({"cmd": "callq", "va": SHOW_EVENT, "args": [county], "abi": "mscdecl"})
            for _ in range(12):
                time.sleep(0.10)
                state = reads([("screen", SCREEN, "i32"),
                               ("extended_pause", EXTENDED_PAUSE, "i32")])
                if state["screen"] == 0x27 or state["extended_pause"] != 0:
                    popup_state = state
                    break
        if popup_state is None:
            raise RuntimeError("event was applied but its localized scroll did not open")
        captures["event_card"] = capture(args.out / "event_card.png", popup=True)

        # Event scrolls use extended_pause and do not close through the ordinary
        # report-screen RMB path. Toggle the daemon's authentic scroll consumer
        # for one tick, then photograph the settled 2000-crown map.
        send({"cmd": "autodismiss", "n": 1})
        for _ in range(20):
            time.sleep(0.10)
            if read(EXTENDED_PAUSE) == 0:
                break
        send({"cmd": "autodismiss", "n": 0})
        if read(EXTENDED_PAUSE) != 0:
            raise RuntimeError("localized event scroll did not dismiss")
        captures["after"] = capture(args.out / "after.png")

        result = {
            "schema": "openlords2.event-proof.v1",
            "captured_at": dt.datetime.now(dt.timezone.utc).isoformat(),
            "source": {
                "binary": "retail Lords2.exe",
                "event_pass": "FUN_00448819",
                "event_pass_va": f"0x{ROLL:08x}",
                "popup_consumer": "FUN_00448d7e",
                "popup_consumer_va": f"0x{SHOW_EVENT:08x}",
                "deck_va": f"0x{DECK:08x}",
                "deck_modified": False,
                "capture_mode": "shotq (lock-free post-render copy + dirty-tile composite)",
            },
            "fixture": {
                "year": 1270, "rng_7bit": rng, "human_slot": human,
                "human_county": county, "event_id": f"0x{args.event:03x}",
                "event_label": EVENT_LABEL if args.event == EVENT_ID else None,
                "human_deck_index": event_index,
                "normalized_treasury": args.cash,
            },
            "treasury": {
                str(p): {"controller": "human" if p == human else "ai",
                         "event_gate": gates[f"p{p}"],
                         "before": before[f"p{p}_cash"],
                         "after": after[f"p{p}_cash"],
                         "delta": after[f"p{p}_cash"] - before[f"p{p}_cash"]}
                for p in players
            },
            "counties": rows,
            "ai_gate_probe": {
                "rng_7bit": gate_probe_rng,
                "deck_modified": False,
                "assigned_nonzero_ai_cards": gate_probe_hits,
                "checks": gate_probe_checks,
                "interpretation": (
                    "These AI counties were assigned nonzero real deck entries. "
                    "Their gate bytes were zero, so no event id was applied."
                ),
            },
            "popup_state": popup_state,
            "checks": checks,
            "captures": captures,
            "interpretation": (
                "The retail cursor assigned a separate real deck slot to every county. "
                "Only the event-enabled human owner applied its assigned card; AI owners "
                "had gate=0, so they applied no different/random event and received no delta."
            ),
        }
        (args.out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"out": str(args.out), "checks": checks,
                          "human_delta": human_delta, "ai_deltas": ai_deltas,
                          "popup_state": popup_state}, indent=2))
    finally:
        if not args.leave_staged:
            restore(original, realms, players)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
