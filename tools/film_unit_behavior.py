#!/usr/bin/env python3
"""Film a checked tactical unit interaction with frame-aligned telemetry.

The ``archer-range`` preset stages an even retail battle, puts the player's
archer line twelve hexes from an untouched AI mace formation, and records the
real simulation/casualty path.  It is a deliberately narrow proof: ranged
attacks remove enemy troops while the archers take no return damage.

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
makes small synthetic battles repeatable.
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


def side_units(snap: dict, side: int) -> list[dict]:
    return [u for u in snap.get("units", []) if u["side"] == side and u["hp"] > 0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", choices=["archer-range"], default="archer-range")
    ap.add_argument("--out", type=Path,
                    default=Path("runs/probe/archer-range/archer_range.mp4"))
    ap.add_argument("--port", type=int, default=27099)
    ap.add_argument("--fps", type=int, default=20)
    ap.add_argument("--max-seconds", type=float, default=8.0)
    ap.add_argument("--troops", type=int, default=16,
                    help="troops per side; 16 gives four unit records per formation")
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
    g.stage({kd.CLASS["archer"]: args.troops},
            {kd.CLASS["mace"]: args.troops})
    # Read the compact deploy model before starting the battle.  A full
    # telemetry sweep here would leave the tiny staged fight running while
    # hundreds of fields are fetched, consuming most of its lifetime.
    deploy_units = g.units()
    archers = [u for u in deploy_units if u["side"] == 0]
    melee = [u for u in deploy_units if u["side"] == 1]
    if not archers or not melee:
        raise RuntimeError(f"fixture deploy mismatch: {len(archers)} vs {len(melee)}")
    archer_forms = sorted({u["form"] for u in archers})
    melee_forms = sorted({u["form"] for u in melee})
    melee_home = centroid(melee)

    # This is a synthetic fixture, so enter its already-staged fight through the
    # two verified simulation gates instead of injecting a deploy-screen click.
    # The click path races retail's formation initializer in small battles and
    # can end the process before the first capture.
    kd.arm_ingame(g.d)
    g.run(250)

    # Match the proven kiting fixture: stage only the player line on live render
    # coordinates, inside its 15-hex weapon range.  The defender's positions,
    # occupancy grid, stats, targets, and health stay untouched.  Projectile
    # combat does not use the attacker's melee-occupancy cell.
    firing_row = 20
    start_col = 45
    for offset, unit in enumerate(archers):
        g.setpos(unit["idx"], start_col + offset, firing_row, grid=False)
    g.d.poke(0x53E9AC, [8, 0, 0, 0])

    staged = film.b.telemetry()
    archers = side_units(staged, 0)
    melee = side_units(staged, 1)
    if any(u["weapon"] == 0 for u in archers):
        raise RuntimeError("attacker fixture is not all ranged")
    if any(u["weapon"] != 0 for u in melee):
        raise RuntimeError("defender fixture is not all melee")
    archer_range = max(u["range_hex"] for u in archers)
    archer_home = centroid(archers)
    initial_gap = closest_gap(archers, melee)
    film.b.center_on((archer_home[0] + melee_home[0]) / 2,
                     (archer_home[1] + melee_home[1]) / 2)

    records: list[dict] = []
    capture_start = time.monotonic()
    attack_orders = 0

    def order_volley() -> None:
        nonlocal attack_orders
        snap = film.b.telemetry()
        live_melee = side_units(snap, 1)
        if not live_melee:
            return
        target = min(live_melee, key=lambda u: u["hp"])
        for form in archer_forms:
            film.b.order(form, target["col"], target["row"], target["idx"])
        attack_orders += 1

    def grab() -> bool:
        snap = film.b.telemetry()
        live_archers = side_units(snap, 0)
        live_melee = side_units(snap, 1)
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
        # Let the unassisted opening show for a few frames, then issue the same
        # legal focus-fire order a player would use.  Ordering during the
        # deploy->fight transition can trip retail's battle-end path before the
        # first capture, so the first command is intentionally frame-gated.
        if len(records) >= 8 and (len(records) - 8) % 8 == 0:
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
        "claim": "Archers inflict casualties from range before melee can return damage.",
        "source": {
            "binary": "retail Lords2.exe",
            "ranged_handlers": ["FUN_00483337", "FUN_00484bf9"],
            "order_driver": "FUN_00479e90 retail formation order",
            "capture_mode": "shotq (lock-free post-render copy + daemon composite)",
        },
        "fixture": {
            "attacker": f"{args.troops} archers",
            "defender": f"{args.troops} maces",
            "archer_forms": archer_forms,
            "melee_forms": melee_forms,
            "player_live_positions_staged": True,
            "player_occupancy_grid_modified": False,
            "defender_state_injected": False,
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
