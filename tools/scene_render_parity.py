#!/usr/bin/env python3
"""
tools/scene_render_parity.py — composed strategic-map scene parity check.

Drives tests/build/scene_render_harness (built with `make -C tests
scene-harness`), which composes the two ported map clusters — pass-1 base
terrain (engine_map_terrain) + the feature walker (engine_map_render) — into
one 8bpp canvas against retail's captured live map state.  Applies the retail
palette, scores the result against a clean retail frame, and — per the
standing render-verification process — emits zoomed truth|mine|diff crops and
pushes them to the llm-feed server so the diff can be eyeballed (never trust
raw pixel-value sampling for render analysis).

Usage:
    nix develop -c python3 tools/scene_render_parity.py \\
        --run runs/capture/strat-terrain1 \\
        --truth runs/capture/strat-spray1/frames/frame_00009.bmp \\
        --pal   runs/capture/strat-spray1/frames/frame_00009.pal \\
        --out   runs/parity/scene1 [--no-feed]
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = ROOT / "tests" / "build" / "scene_render_harness"
FEED = Path("/opt/src/llm-feed/feed.py")

SCALAR_ORDER = [
    "cam_x0", "cam_y0", "col_start", "row_start", "col_count", "row_count",
    "x_step", "y_step", "x_half_step", "view_mode", "render_mode",
    "rotation", "overlay_base", "y_viewport", "arrow_marker_base",
]
CANVAS_W, CANVAS_H = 0x280, 0x1e0
MAP_W = 480              # map viewport width (HUD panel is to the right)
MENUBAR_H = 24          # top File/Options/Help bar — drawn by a separate fn


def _le_hex_to_int(hexs: str) -> int:
    return int.from_bytes(bytes.fromhex(hexs), "little", signed=False)


def _load_scalars(run: Path) -> dict[str, int]:
    rows = json.loads((run / "globals_dump.json").read_text())
    by_label = {r["label"]: _le_hex_to_int(r["hex"]) for r in rows if r.get("hex")}
    missing = [k for k in SCALAR_ORDER if k not in by_label]
    if missing:
        sys.exit(f"globals_dump.json missing scalars: {missing}")
    return by_label


def _apply_palette(palette: bytes, pixels: bytes) -> bytes:
    """8bpp indices → packed RGB bytes (R,G,B per pixel)."""
    out = bytearray(len(pixels) * 3)
    for i, idx in enumerate(pixels):
        p = idx * 4
        out[i * 3] = palette[p]
        out[i * 3 + 1] = palette[p + 1]
        out[i * 3 + 2] = palette[p + 2]
    return bytes(out)


def _read_bmp24_rgb(path: Path):
    data = path.read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    topdown = h < 0
    h = abs(h)
    row_bgr = w * 3
    rt = row_bgr + ((4 - (row_bgr & 3)) & 3)
    rows = []
    for y in range(h):
        sy = y if topdown else (h - 1 - y)
        base = off + sy * rt
        line = bytearray(w * 3)
        for x in range(w):
            o = base + x * 3
            line[x * 3] = data[o + 2]      # R
            line[x * 3 + 1] = data[o + 1]  # G
            line[x * 3 + 2] = data[o]      # B
        rows.append(bytes(line))
    return w, h, b"".join(rows)


def _write_bmp24_from_rgb(path: Path, rgb: bytes, w: int, h: int) -> None:
    row_bgr = w * 3
    pad = (4 - (row_bgr & 3)) & 3
    rt = row_bgr + pad
    with path.open("wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<I", 14 + 40 + rt * h))
        f.write(b"\x00\x00\x00\x00")
        f.write(struct.pack("<I", 54))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, rt * h, 0, 0, 0, 0))
        z = b"\x00" * pad
        for y in range(h):
            ln = rgb[y * row_bgr:(y + 1) * row_bgr]
            bgr = bytearray(row_bgr)
            bgr[0::3] = ln[2::3]
            bgr[1::3] = ln[1::3]
            bgr[2::3] = ln[0::3]
            f.write(bgr)
            f.write(z)


def _px(buf, w, x, y):
    o = (y * w + x) * 3
    return buf[o:o + 3]


def _score(truth, tw, mine, mw, y0, y1, x0, x1):
    same = tot = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            tot += 1
            if _px(truth, tw, x, y) == _px(mine, mw, x, y):
                same += 1
    return same, tot


def _push_feed(path: Path, title: str, note: str) -> None:
    try:
        subprocess.run(["nix", "run", "nixpkgs#python3", "--", str(FEED),
                        "image", str(path), "--title", title, "--note", note],
                       capture_output=True, text=True, timeout=120)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"feed push failed: {e}\n")


def _zoom_crops(truth, tw, mine, mw, out: Path, push: bool):
    """Per the standing process: build truth|mine|diff zoom crops, save PNGs,
    push to feed."""
    try:
        from PIL import Image
    except ImportError:
        sys.stderr.write("PIL unavailable — skipping zoom crops\n")
        return []
    import numpy as np
    T = Image.frombytes("RGB", (tw, len(truth) // tw // 3), bytes(truth))
    M = Image.frombytes("RGB", (mw, len(mine) // mw // 3), bytes(mine))

    def panel(x, y, w, h, zoom, name, title, note):
        tc = T.crop((x, y, x + w, y + h))
        mc = M.crop((x, y, x + w, y + h))
        ta = np.array(tc); ma = np.array(mc)
        d = (ta != ma).any(2)
        dc = ma.copy(); dc[d] = [255, 0, 255]
        di = Image.fromarray(dc)
        big = lambda im: im.resize((w * zoom, h * zoom), Image.NEAREST)
        canvas = Image.new("RGB", (w * zoom * 3 + 20, h * zoom), (30, 30, 30))
        canvas.paste(big(tc), (0, 0))
        canvas.paste(big(mc), (w * zoom + 10, 0))
        canvas.paste(big(di), (w * zoom * 2 + 20, 0))
        p = out / name
        canvas.save(p)
        if push:
            _push_feed(p, title, note)
        return str(p)

    paths = [
        panel(0, 0, 240, 60, 3, "z_topbar.png",
              "scene: top band", "truth | mine | diff(magenta)"),
        panel(120, 110, 140, 140, 4, "z_body.png",
              "scene: terrain+features body", "truth | mine | diff(magenta)"),
        panel(0, 360, 240, 120, 3, "z_bottomleft.png",
              "scene: bottom-left units", "truth | mine | diff(magenta)"),
    ]
    return paths


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True, type=Path)
    ap.add_argument("--truth", required=True, type=Path)
    ap.add_argument("--pal", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--harness", type=Path, default=HARNESS)
    ap.add_argument("--no-feed", action="store_true")
    args = ap.parse_args()

    dump_dir = args.run / "dump"
    if not args.harness.exists():
        sys.exit(f"harness not built: {args.harness} (make -C tests scene-harness)")

    args.out.mkdir(parents=True, exist_ok=True)
    raw_path = args.out / "port_scene.raw"
    bmp_path = args.out / "port_scene.bmp"

    scalars = _load_scalars(args.run)
    argv = [str(args.harness), str(dump_dir), str(raw_path)]
    argv += [str(scalars[k]) for k in SCALAR_ORDER]
    proc = subprocess.run(argv, capture_output=True, text=True)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        sys.exit(f"harness failed (rc={proc.returncode})")
    harness_stats = proc.stdout.strip()

    palette = args.pal.read_bytes()
    pixels = raw_path.read_bytes()
    mine = _apply_palette(palette, pixels)          # RGB, 640x480
    _write_bmp24_from_rgb(bmp_path, mine, CANVAS_W, CANVAS_H)

    tw, th, truth = _read_bmp24_rgb(args.truth)

    full = _score(truth, tw, mine, CANVAS_W, 0, min(th, CANVAS_H), 0, MAP_W)
    nobar = _score(truth, tw, mine, CANVAS_W, MENUBAR_H, min(th, CANVAS_H), 0, MAP_W)

    crops = _zoom_crops(truth, tw, mine, CANVAS_W, args.out, not args.no_feed)

    print(json.dumps({
        "harness": harness_stats,
        "pixel_match_full_map_region": f"{full[0]}/{full[1]} = {100*full[0]/full[1]:.2f}%",
        "pixel_match_excl_menubar":   f"{nobar[0]}/{nobar[1]} = {100*nobar[0]/nobar[1]:.2f}%",
        "out_bmp": str(bmp_path),
        "zoom_crops": crops,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
