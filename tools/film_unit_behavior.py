#!/usr/bin/env python3
"""Film a checked tactical unit interaction with frame-aligned telemetry.

The ``archer-range`` preset enters a natural retail 28v28 battle, selects one
already-loaded archer formation and one already-loaded mace formation, places
them inside bow range, and records the real simulation/casualty path.  Starting
from natural armies matters visually: the engine loads the correct unit sprite
bank and bright battlefield palette before capture.  The older scratch-record
class override proved the behavior but rendered replacement classes through a
stale sprite/movie bank.

The result JSON distinguishes fixture setup, player orders, and observations.
The JSONL sidecar includes live positions, destinations, actions, weapon range,
health, and group decision clocks for a reusable editorial overlay.

Prerequisite: a clean probe daemon. Example:

    nix develop -c python tools/probe_daemon.py \
        --segtrace tools/traces/new_game_to_map.jsonl --rng-seed canonical \
        --run-id archer-range

    nix develop -c python tools/film_unit_behavior.py \
        --out runs/probe/archer-range/archer_range.mp4

Use ``--present`` only when a live Windows preview is required.  Production
``shotq`` capture is headless; avoiding the preview's recurring DDraw Lock also
makes natural presentation fixtures repeatable.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import film_battle as fb  # noqa: E402
import enter_battle as eb  # noqa: E402
import kiting_demo as kd  # noqa: E402


def manhattan(a: tuple[float, float], b: tuple[float, float]) -> float:
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def centroid(units: list[dict]) -> tuple[float, float]:
    live = [u for u in units if u["hp"] > 0]
    if not live:
        return (0.0, 0.0)
    return (sum(u["col"] for u in live) / len(live),
            sum(u["row"] for u in live) / len(live))


def closest_gap(left: list[dict], right: list[dict]) -> int:
    pairs = (manhattan((a["col"], a["row"]), (b["col"], b["row"]))
             for a in left if a["hp"] > 0 for b in right if b["hp"] > 0)
    return int(min(pairs, default=999))


def selected_units(snap: dict, ids: set[int]) -> list[dict]:
    return [u for u in snap.get("units", []) if u["idx"] in ids and u["hp"] > 0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", choices=["archer-range"], default="archer-range")
    ap.add_argument("--out", type=Path,
                    default=Path("runs/probe/archer-range/archer_range.mp4"))
    ap.add_argument("--port", type=int, default=27099)
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--max-seconds", type=float, default=8.0)
    ap.add_argument("--attacker", type=int, default=4,
                    help="natural strategic attacker record")
    ap.add_argument("--defender", type=int, default=5,
                    help="natural strategic defender record")
    args = ap.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    frame_dir = args.out.with_suffix("").with_name(args.out.stem + "_frames")
    telemetry_path = args.out.with_suffix(".telemetry.jsonl")
    result_path = args.out.with_suffix(".result.json")
    frame_dir.mkdir(parents=True, exist_ok=True)
    for old in frame_dir.glob("f*.png"):
        old.unlink()

    g = kd.Demo(args.port)
    film = fb.Film(args.port)
    eb.enter(g.d, args.attacker, args.defender)
    deploy_units = film.b.telemetry()["units"]
    archers = [u for u in deploy_units if u["side"] == 0 and u["unit_class"] == kd.CLASS["archer"]]
    melee = [u for u in deploy_units if u["side"] == 1 and u["unit_class"] == kd.CLASS["mace"]]
    if not archers or not melee:
        raise RuntimeError(f"natural fixture lacks archers/maces: {len(archers)} vs {len(melee)}")

    # Use one complete natural formation per side. Orders are formation-level:
    # relocating just one record leaves its formation centroid and the rest of
    # its members at the deploy line, so the isolated record never executes a
    # coherent volley. The rest of both armies stay at retail deployment.
    archer_ids = {u["idx"] for u in archers}
    melee_ids = {u["idx"] for u in melee}
    archer_forms = sorted({u["form"] for u in archers})
    melee_forms = sorted({u["form"] for u in melee})

    film.b.start()
    time.sleep(0.4)
    kd.arm_ingame(g.d)
    g.run(250)

    # Put the two selected formations on the verified bright western meadow.
    # Camera column 32 contains a corrupt animated battlefield feature in this
    # retail map; this region's palette is clean. Rows 73-74 also move the bridge
    # mostly above the viewport while keeping the original armies offscreen.
    # The closest pair starts ten hexes apart and the farthest archer at fifteen.
    archer_cells = [(4, 73), (5, 73), (6, 73), (7, 73),
                    (4, 74), (5, 74), (6, 74)]
    melee_cells = [(17, 73), (18, 73), (19, 73)]
    if len(archers) != len(archer_cells) or len(melee) != len(melee_cells):
        raise RuntimeError(
            f"natural formation shape changed: {len(archers)} archers, "
            f"{len(melee)} maces")
    for unit, (col, row) in zip(archers, archer_cells):
        g.setpos(unit["idx"], col, row, grid=True)
    for unit, (col, row) in zip(melee, melee_cells):
        g.setpos(unit["idx"], col, row, grid=True)
    g.d.poke(0x53E9AC, [8, 0, 0, 0])
    g.run(250)

    staged = film.b.telemetry()
    archers = selected_units(staged, archer_ids)
    melee = selected_units(staged, melee_ids)
    if any(u["weapon"] == 0 for u in archers):
        raise RuntimeError("attacker fixture is not all ranged")
    if any(u["weapon"] != 0 for u in melee):
        raise RuntimeError("defender fixture is not all melee")
    archer_range = max(u["range_hex"] for u in archers)
    archer_home = centroid(archers)
    melee_home = centroid(melee)
    initial_gap = closest_gap(archers, melee)
    film.b.center_on((archer_home[0] + melee_home[0]) / 2,
                     (archer_home[1] + melee_home[1]) / 2)

    records: list[dict] = []
    capture_start = time.monotonic()
    attack_orders = 0

    def order_volley() -> None:
        nonlocal attack_orders
        snap = film.b.telemetry()
        live_archers = selected_units(snap, archer_ids)
        live_melee = selected_units(snap, melee_ids)
        if not live_archers or not live_melee:
            return
        archer_center = centroid(live_archers)
        target = min(
            live_melee,
            key=lambda u: manhattan((u["col"], u["row"]), archer_center),
        )
        for form in archer_forms:
            film.b.order(form, target["col"], target["row"], target["idx"])
        attack_orders += 1

    def grab() -> bool:
        snap = film.b.telemetry()
        live_archers = selected_units(snap, archer_ids)
        live_melee = selected_units(snap, melee_ids)
        if not live_archers or not live_melee:
            return False
        a_center = centroid(live_archers)
        m_center = centroid(live_melee)
        gap = closest_gap(live_archers, live_melee)
        film.b.center_on((a_center[0] + m_center[0]) / 2,
                         (a_center[1] + m_center[1]) / 2)
        frame = len(records)
        name = f"f{frame:05d}.png"
        if not film.shot(str(frame_dir / name), settle_ms=70, mode="stable",
                         combat_only=True):
            # A successful shot rejected by combat_only means the battle changed
            # screens during the capture tick.  End cleanly without admitting a
            # ratings/menu frame into the footage or telemetry.
            if film.last_shot and film.last_shot.get("ok"):
                return False
            raise RuntimeError(f"shotq failed at frame {frame}")
        snap.update({
            "schema": "openlords2.battle-telemetry.v2",
            "frame": frame,
            "image": name,
            "phase": "free_fire" if gap > 1 else "contact",
            "elapsed_s": round(time.monotonic() - capture_start, 4),
            "capture_mode": "shotq",
            "scenario": args.scenario,
            "separation_hex": gap,
            "ranged_centroid": [round(a_center[0], 3), round(a_center[1], 3)],
            "melee_centroid": [round(m_center[0], 3), round(m_center[1], 3)],
            "ranged_home": [round(archer_home[0], 3), round(archer_home[1], 3)],
            "ranged_displacement_hex": round(manhattan(a_center, archer_home), 3),
            "ranged_range_hex": archer_range,
            "ranged_actions": dict(Counter(str(u["action"]) for u in live_archers)),
            "hp_by_side": {
                "0": sum(u["hp"] for u in live_archers),
                "1": sum(u["hp"] for u in live_melee),
            },
        })
        records.append(snap)
        return True

    deadline = time.monotonic() + args.max_seconds
    while time.monotonic() < deadline:
        if film.b.screen() not in (0x28, 0x29, 0x2A):
            break
        # Reassert the same legal formation-level focus-fire order periodically.
        # The fixture is already through the deploy->fight transition here, so
        # ordering before frame zero keeps the relocated formation from briefly
        # following its stale retail deployment anchor.
        if len(records) % 8 == 0:
            order_volley()
        if not grab():
            break
        g.d.rpc({"cmd": "sleep", "ms": max(5, round(1000 / args.fps) - 70)})

    if film.b.screen() in (0x28, 0x29, 0x2A):
        film.b.freeze()
    telemetry_path.write_text("".join(
        json.dumps(row, separators=(",", ":")) + "\n" for row in records))

    gaps = [row["separation_hex"] for row in records]
    archer_hp = [row["hp_by_side"]["0"] for row in records]
    melee_hp = [row["hp_by_side"]["1"] for row in records]
    displacements = [row["ranged_displacement_hex"] for row in records]
    ranged_actions = {
        int(action)
        for row in records
        for action, count in row["ranged_actions"].items()
        if count
    }
    checks = {
        "capture_has_eight_frames": len(records) >= 8,
        "capture_has_twenty_frames": len(records) >= 20,
        "starts_inside_archer_range": initial_gap <= archer_range,
        "ranged_attack_action_observed": 3 in ranged_actions,
        "enemy_melee_takes_damage": bool(melee_hp) and min(melee_hp) < melee_hp[0],
        "archers_take_no_return_damage": bool(archer_hp) and min(archer_hp) == archer_hp[0],
        "no_melee_contact_before_damage": (
            bool(gaps) and min(gaps) > 1 and bool(melee_hp) and min(melee_hp) < melee_hp[0]),
    }
    result = {
        "schema": "openlords2.unit-behavior-proof.v1",
        "captured_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "scenario": args.scenario,
        "claim": (
            "A natural archer formation inflicts casualties from range before "
            "a natural mace formation can return damage."
        ),
        "source": {
            "binary": "retail Lords2.exe",
            "ranged_handlers": ["FUN_00483337", "FUN_00484bf9"],
            "order_driver": "FUN_00479e90 retail formation order",
            "capture_mode": "shotq (lock-free post-render copy + daemon composite)",
        },
        "fixture": {
            "attacker": "one complete natural archer formation",
            "defender": "one complete natural mace formation",
            "natural_armies": [args.attacker, args.defender],
            "selected_archer_ids": sorted(archer_ids),
            "selected_melee_ids": sorted(melee_ids),
            "archer_forms": archer_forms,
            "melee_forms": melee_forms,
            "selected_live_positions_staged": True,
            "selected_occupancy_grid_updated": True,
            "unit_classes_or_sprite_banks_injected": False,
            "unselected_army_records_modified": False,
            "targets_injected": False,
            "health_or_stats_injected": False,
            "player_attack_orders": attack_orders,
        },
        "capture": {
            "video": str(args.out),
            "frames": len(records),
            "fps": args.fps,
            "frame_dir": str(frame_dir),
            "telemetry": str(telemetry_path),
            "separation_min_max": [min(gaps, default=None), max(gaps, default=None)],
            "archer_displacement_max": max(displacements, default=None),
            "archer_range_hex": archer_range,
            "ranged_actions_observed": sorted(ranged_actions),
            "hp_start_end": {
                "archers": [archer_hp[0], archer_hp[-1]] if archer_hp else None,
                "melee": [melee_hp[0], melee_hp[-1]] if melee_hp else None,
            },
        },
        "checks": checks,
    }
    result_path.write_text(json.dumps(result, indent=2) + "\n")

    if records:
        subprocess.run([
            "ffmpeg", "-y", "-framerate", str(args.fps),
            "-i", str(frame_dir / "f%05d.png"),
            "-r", str(args.fps), "-c:v", "libx264", "-pix_fmt", "yuv420p",
            "-crf", "18", "-movflags", "+faststart", str(args.out),
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print(json.dumps({"out": str(args.out), "frames": len(records),
                      "checks": checks}, indent=2))
    return 0 if all(checks.values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
