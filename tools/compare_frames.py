#!/usr/bin/env python3
"""
tools/compare_frames.py — port↔retail frame comparison for render parity.

Given two captured frames (BMP/PNG from either the port's --capture-to or
retail's frida_capture --frame-capture), emit:

  - <out>/sidebyside.png   labelled A | B (e.g. port | retail), for eyeballing.
  - <out>/diff.png         B with differing pixels tinted red — *where* it
                           diverges, not just whether.
  - stdout                 one JSON line: identical?, n_diff_pixels, diff
                           fraction, max per-channel delta, sizes.

Bit-exact is the goal for the port (we render the same 8-bpp paletted pipeline
retail does), so the headline is `identical`. The diff overlay + stats triage
the gap when it isn't. Cross-capture-path note: the port writes BMP straight
from the back buffer; retail's frida path Locks the back surface and re-packs —
both end up 24-bpp from the same 8-bpp source, so a faithful port should match
retail bit-for-bit on a static screen.

Usage:
    nix develop -c python3 tools/compare_frames.py \\
        --a runs/.../frames/frame_00007.bmp --a-label port \\
        --b runs/capture/retail-mainmenu/frames/frame_00040.bmp --b-label retail \\
        --out runs/compare/mainmenu

Exit code: 0 if identical, 1 if they differ (so it can gate a parity check),
2 on error (missing file, size mismatch with --strict-size).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image


def _load_rgb(path: Path) -> Image.Image:
    img = Image.open(path)
    return img.convert("RGB")


def compare(a: Image.Image, b: Image.Image, tint=(255, 0, 0)) -> dict:
    """Compare two same-size RGB images. Returns stats + a diff-overlay image
    (b with differing pixels tinted) under key 'overlay'."""
    aw, ah = a.size
    bw, bh = b.size
    w, h = min(aw, bw), min(ah, bh)
    # Work on the overlapping region if sizes differ; size mismatch is itself
    # reported.
    ap = a.crop((0, 0, w, h)).load()
    overlay = b.crop((0, 0, w, h)).convert("RGB")
    op = overlay.load()
    bp = overlay.copy().load()

    n_diff = 0
    max_delta = 0
    for y in range(h):
        for x in range(w):
            ar, ag, ab_ = ap[x, y]
            br, bg, bb = bp[x, y]
            d = max(abs(ar - br), abs(ag - bg), abs(ab_ - bb))
            if d != 0:
                n_diff += 1
                if d > max_delta:
                    max_delta = d
                op[x, y] = tint
    total = w * h
    return {
        "overlay":       overlay,
        "size_a":        [aw, ah],
        "size_b":        [bw, bh],
        "compared_size": [w, h],
        "size_match":    (aw == bw and ah == bh),
        "n_diff_pixels": n_diff,
        "total_pixels":  total,
        "diff_fraction": (n_diff / total) if total else 0.0,
        "max_channel_delta": max_delta,
        "identical":     (n_diff == 0 and aw == bw and ah == bh),
    }


def side_by_side(a: Image.Image, b: Image.Image,
                 la: str, lb: str) -> Image.Image:
    """A | B with a thin separator and labels baked into a top strip."""
    from PIL import ImageDraw
    pad, strip = 4, 16
    w = a.width + b.width + pad * 3
    h = max(a.height, b.height) + strip + pad * 2
    canvas = Image.new("RGB", (w, h), (24, 24, 24))
    canvas.paste(a, (pad, strip + pad))
    canvas.paste(b, (a.width + pad * 2, strip + pad))
    d = ImageDraw.Draw(canvas)
    d.text((pad, 3), la, fill=(220, 220, 220))
    d.text((a.width + pad * 2, 3), lb, fill=(220, 220, 220))
    return canvas


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--a", type=Path, required=True, help="frame A (e.g. port)")
    ap.add_argument("--b", type=Path, required=True, help="frame B (e.g. retail)")
    ap.add_argument("--a-label", default="A")
    ap.add_argument("--b-label", default="B")
    ap.add_argument("--out", type=Path, required=True,
                    help="output dir for sidebyside.png + diff.png")
    ap.add_argument("--strict-size", action="store_true",
                    help="error (exit 2) if the two frames differ in size")
    args = ap.parse_args(argv)

    for p in (args.a, args.b):
        if not p.exists():
            print(json.dumps({"error": f"missing file: {p}"}))
            return 2

    a = _load_rgb(args.a)
    b = _load_rgb(args.b)

    if args.strict_size and a.size != b.size:
        print(json.dumps({"error": "size mismatch",
                          "size_a": list(a.size), "size_b": list(b.size)}))
        return 2

    res = compare(a, b)
    overlay = res.pop("overlay")

    args.out.mkdir(parents=True, exist_ok=True)
    sbs = side_by_side(a, b, args.a_label, args.b_label)
    sbs.save(args.out / "sidebyside.png")
    overlay.save(args.out / "diff.png")

    res["sidebyside"] = str(args.out / "sidebyside.png")
    res["diff"]       = str(args.out / "diff.png")
    res["a"] = str(args.a)
    res["b"] = str(args.b)
    print(json.dumps(res, indent=2))
    return 0 if res["identical"] else 1


if __name__ == "__main__":
    sys.exit(main())
