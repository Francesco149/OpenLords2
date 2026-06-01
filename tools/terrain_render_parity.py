#!/usr/bin/env python3
"""
tools/terrain_render_parity.py — strategic-map PASS-1 base-terrain parity.

Runs the C harness (tests/build/terrain_render_harness, built with
`make -C tests terrain-harness`) on the captured terrain dataset, applies
the captured retail palette, writes a 24-bpp BMP, and reports per-pixel
match vs the clean retail frame over the map region.

Usage:
    nix develop -c python3 tools/terrain_render_parity.py \
        --run runs/capture/strat-terrain1 \
        --pal runs/capture/strat-spray1/frames/frame_00009.pal \
        --truth runs/capture/strat-spray1/frames/frame_00009.bmp \
        --out runs/parity/terrain1
"""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = ROOT / "tests" / "build" / "terrain_render_harness"
CANVAS_W, CANVAS_H = 0x280, 0x1e0

SCALAR_ORDER = ["cam_x0", "cam_y0", "col_start", "row_start", "col_count",
                "row_count", "x_step", "y_step", "x_half_step", "view_mode",
                "render_mode"]


def load_scalars(run: Path):
    rows = json.loads((run / "globals_dump.json").read_text())
    by = {r["label"]: int.from_bytes(bytes.fromhex(r["hex"]), "little")
          for r in rows if r.get("hex")}
    missing = [k for k in SCALAR_ORDER if k not in by]
    if missing:
        sys.exit(f"missing scalars: {missing}")
    return by


def write_bmp(path: Path, pal: bytes, px: bytes, w=CANVAS_W, h=CANVAS_H):
    rb = w * 3
    pad = (4 - (rb & 3)) & 3
    rt = rb + pad
    img = rt * h
    with path.open("wb") as fp:
        fp.write(b"BM")
        fp.write(struct.pack("<I", 14 + 40 + img))
        fp.write(b"\x00\x00\x00\x00")
        fp.write(struct.pack("<I", 54))
        fp.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, img, 0, 0, 0, 0))
        z = b"\x00" * pad
        for y in range(h):
            ri = px[y * w:(y + 1) * w]
            ro = bytearray(rt)
            j = 0
            for idx in ri:
                p = idx * 4
                ro[j] = pal[p + 2]; ro[j + 1] = pal[p + 1]; ro[j + 2] = pal[p]
                j += 3
            fp.write(ro); fp.write(z)


def read_bmp24(path: Path):
    data = path.read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    td = h < 0
    h = abs(h)
    rb = w * 3
    pad = (4 - (rb & 3)) & 3
    rt = rb + pad
    rows = [data[off + (y if td else h - 1 - y) * rt:
                 off + (y if td else h - 1 - y) * rt + rb] for y in range(h)]
    return w, h, b"".join(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True, type=Path)
    ap.add_argument("--pal", required=True, type=Path)
    ap.add_argument("--truth", type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--harness", type=Path, default=HARNESS)
    args = ap.parse_args()

    if not args.harness.exists():
        sys.exit(f"harness not built: {args.harness} (make -C tests terrain-harness)")
    dump = args.run / "dump"
    args.out.mkdir(parents=True, exist_ok=True)
    raw = args.out / "port_terrain.raw"
    bmp = args.out / "port_terrain.bmp"

    sc = load_scalars(args.run)
    argv = [str(args.harness), str(dump), str(raw)] + [str(sc[k]) for k in SCALAR_ORDER]
    proc = subprocess.run(argv, capture_output=True, text=True)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        sys.exit(f"harness failed rc={proc.returncode}")
    stats = proc.stdout.strip()

    pal = args.pal.read_bytes()
    px = raw.read_bytes()
    if len(px) != CANVAS_W * CANVAS_H:
        sys.exit(f"raw size {len(px)}")
    write_bmp(bmp, pal, px)

    out = {"harness": stats, "out_bmp": str(bmp)}
    if args.truth:
        tw, th, truth = read_bmp24(args.truth)
        _, _, mine = read_bmp24(bmp)
        REGION_W = 480
        same = tot = 0
        for y in range(min(th, CANVAS_H)):
            for x in range(REGION_W):
                o = (y * tw + x) * 3
                m = (y * CANVAS_W + x) * 3
                tot += 1
                if truth[o:o + 3] == mine[m:m + 3]:
                    same += 1
        out["pixel_match"] = f"{same}/{tot} = {100*same/tot:.1f}%"
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
