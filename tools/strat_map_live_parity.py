#!/usr/bin/env python3
"""
tools/strat_map_live_parity.py — diff the LIVE drop-in's strategic-map
body render against the retail clean frame.

Unlike scene_render_parity.py (which runs the host scene harness to PRODUCE
the raw canvas), this consumes a raw 8bpp buffer already produced by the
live drop-in exe (`openlords2.exe --strat-map DIR`, which native-loads the
sprite banks via engine_pl8 and runs the ported map-body driver into its
DDraw back buffer, dumping DIR/port_strat.raw).  It palette-applies the
captured retail palette, scores against the clean retail frame, and pushes
zoomed truth|mine|diff crops to the llm-feed server.

Usage:
    nix develop -c python3 tools/strat_map_live_parity.py \\
        --raw   runs/capture/strat-terrain1/dump/port_strat.raw \\
        --truth runs/capture/strat-spray1/frames/frame_00009.bmp \\
        --pal   runs/capture/strat-spray1/frames/frame_00009.pal \\
        --out   runs/parity/strat-live1 [--no-feed]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from scene_render_parity import (  # noqa: E402
    CANVAS_W, CANVAS_H, MAP_W, MENUBAR_H,
    _apply_palette, _read_bmp24_rgb, _write_bmp24_from_rgb,
    _score, _zoom_crops,
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", required=True, type=Path,
                    help="live port raw 8bpp canvas (port_strat.raw)")
    ap.add_argument("--truth", required=True, type=Path)
    ap.add_argument("--pal", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--no-feed", action="store_true")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    pixels = args.raw.read_bytes()
    want = CANVAS_W * CANVAS_H
    if len(pixels) != want:
        sys.exit(f"raw size {len(pixels)} != {want} (expected {CANVAS_W}x{CANVAS_H})")

    palette = args.pal.read_bytes()
    mine = _apply_palette(palette, pixels)             # RGB 640x480
    bmp_path = args.out / "port_strat_live.bmp"
    _write_bmp24_from_rgb(bmp_path, mine, CANVAS_W, CANVAS_H)

    tw, th, truth = _read_bmp24_rgb(args.truth)

    full  = _score(truth, tw, mine, CANVAS_W, 0, min(th, CANVAS_H), 0, MAP_W)
    nobar = _score(truth, tw, mine, CANVAS_W, MENUBAR_H, min(th, CANVAS_H), 0, MAP_W)

    crops = _zoom_crops(truth, tw, mine, CANVAS_W, args.out, not args.no_feed)

    print(json.dumps({
        "source": "LIVE drop-in (openlords2.exe --strat-map), native engine_pl8 banks",
        "pixel_match_full_map_region": f"{full[0]}/{full[1]} = {100*full[0]/full[1]:.2f}%",
        "pixel_match_excl_menubar":   f"{nobar[0]}/{nobar[1]} = {100*nobar[0]/nobar[1]:.2f}%",
        "out_bmp": str(bmp_path),
        "zoom_crops": crops,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
