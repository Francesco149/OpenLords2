#!/usr/bin/env python3
"""
tools/build_comparison_atlas.py — build a llm-feed `comparison` atlas from
two images (port render + retail ground truth).

Emits a single atlas PNG laid out as
    row 0 = [ left | right ]   (always shown)
    row 1 = [ amplified abs-diff | amplified abs-diff ]  (revealed on click)
plus a spec JSON for `feed.py comparison --spec`.  Geometry matches what
openrecet's comparison_page.py emits (row0_pct / total_pct as % of atlas
width).  Optionally crops both inputs to a region (e.g. the map area).

Usage:
    nix develop -c python3 tools/build_comparison_atlas.py \
        --left  runs/parity/proto1/proto_pass1.bmp  --left-label port \
        --right runs/capture/strat-spray1/frames/frame_00009.bmp --right-label retail \
        --crop 0,0,480,480 --gain 6 \
        --out runs/parity/proto1/atlas.png --spec runs/parity/proto1/spec.json \
        --title "pass-1 base terrain"
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def load_rgb(path: Path, crop):
    im = Image.open(path).convert("RGB")
    if crop:
        x, y, w, h = crop
        im = im.crop((x, y, x + w, y + h))
    return np.asarray(im, dtype=np.uint8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--left", required=True, type=Path)
    ap.add_argument("--right", required=True, type=Path)
    ap.add_argument("--left-label", default="left")
    ap.add_argument("--right-label", default="right")
    ap.add_argument("--crop", help="x,y,w,h applied to both")
    ap.add_argument("--gain", type=float, default=6.0)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--spec", required=True, type=Path)
    ap.add_argument("--title", default="comparison")
    ap.add_argument("--note", default="")
    args = ap.parse_args()

    crop = tuple(int(v) for v in args.crop.split(",")) if args.crop else None
    L = load_rgb(args.left, crop)
    R = load_rgb(args.right, crop)
    h = min(L.shape[0], R.shape[0])
    w = min(L.shape[1], R.shape[1])
    L, R = L[:h, :w], R[:h, :w]

    diff = np.abs(L.astype(np.int16) - R.astype(np.int16))
    differ_px = int(np.count_nonzero(diff.any(axis=2)))
    meanabs = float(diff.mean())
    amp = np.clip(diff.astype(np.float32) * args.gain, 0, 255).astype(np.uint8)

    top = np.concatenate([L, R], axis=1)            # [left | right]
    bot = np.concatenate([amp, amp], axis=1)         # [diff | diff]
    atlas = np.concatenate([top, bot], axis=0)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(atlas, "RGB").save(args.out)

    atlas_w = atlas.shape[1]
    row0_pct = 100.0 * top.shape[0] / atlas_w
    total_pct = 100.0 * atlas.shape[0] / atlas_w
    spec = {
        "title": args.title, "note": args.note,
        "left_label": args.left_label, "right_label": args.right_label,
        "panels": [{
            "atlas": str(args.out.resolve()),
            "label": args.title,
            "row0_pct": round(row0_pct, 2),
            "total_pct": round(total_pct, 2),
            "differ_px": differ_px,
            "meanabs": round(meanabs, 3),
        }],
    }
    args.spec.write_text(json.dumps(spec, indent=2))
    print(json.dumps({"atlas": str(args.out), "spec": str(args.spec),
                      "differ_px": differ_px, "meanabs": round(meanabs, 3),
                      "size": f"{atlas_w}x{atlas.shape[0]}"}, indent=2))


if __name__ == "__main__":
    main()
