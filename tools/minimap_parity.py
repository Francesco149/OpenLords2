#!/usr/bin/env python3
"""
tools/minimap_parity.py — strategic-map minimap parity check.

The minimap unit test (test_engine_minimap.c / X(minimap_render_political))
loads the real lastturn.sav, loads the two 128x128 grids from Map01.pl8 via
the ported FUN_0046a037, and renders the political minimap (FUN_00410cbd via
FUN_00410aa9) into a 640x480 8bpp canvas dumped to
runs/parity/minimap/port.raw.

This tool applies the retail freeroam palette to that raw render, crops the
128x128 minimap box from both the port render and the captured retail frame
(runs/capture/load_clean_dump/frames/frame_00000.bmp), scores the pixel match,
and pushes a retail|port|diff comparison image to the llm-feed so the result
can be eyeballed (never trust raw pixel sampling for render analysis).

Run (after `make -C tests run` regenerates port.raw):
    nix develop -c python3 tools/minimap_parity.py [--no-feed]
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FEED = Path("/opt/src/llm-feed/feed.py")

CANVAS_W, CANVAS_H = 640, 480
# FUN_00410aa9(0x1e0, 0x19) blits/plots at (0x1e0-2, 0x19+3) over 128x128.
MM_X, MM_Y, MM_W, MM_H = 0x1e0 - 2, 0x19 + 3, 0x80, 0x80


def load_palette(pal_path: Path) -> list[tuple[int, int, int]]:
    raw = pal_path.read_bytes()
    # 256 entries, 4 bytes each (R, G, B, pad).
    return [(raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2]) for i in range(256)]


def raw_to_image(raw_path: Path, pal: list[tuple[int, int, int]]) -> Image.Image:
    idx = raw_path.read_bytes()
    if len(idx) < CANVAS_W * CANVAS_H:
        sys.exit(f"{raw_path}: short raw ({len(idx)} bytes)")
    img = Image.new("RGB", (CANVAS_W, CANVAS_H))
    img.putdata([pal[b] for b in idx[: CANVAS_W * CANVAS_H]])
    return img


def crop_mm(img: Image.Image) -> Image.Image:
    return img.crop((MM_X, MM_Y, MM_X + MM_W, MM_Y + MM_H))


def diff_image(truth: Image.Image, port: Image.Image, port_idx):
    """Diff over the minimap box.  Cells the minimap leaves transparent
    (port index 0) are the panel background's pixels — retail draws its
    sand parchment there underneath the minimap, so they are scored
    separately, not as minimap errors.  Returns (diff_img, drawn_match,
    drawn_total, bg_cells)."""
    pt, pp = truth.load(), port.load()
    out = Image.new("RGB", truth.size)
    po = out.load()
    w, h = truth.size
    drawn_match = drawn_total = bg = 0
    for y in range(h):
        for x in range(w):
            if port_idx[y * MM_W + x] == 0:        # transparent backdrop cell
                po[x, y] = (40, 30, 15)            # mark as "panel background"
                bg += 1
                continue
            drawn_total += 1
            if pt[x, y] == pp[x, y]:
                po[x, y] = (20, 20, 20)
                drawn_match += 1
            else:
                po[x, y] = (255, 0, 0)
    return out, drawn_match, drawn_total, bg


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", default=str(ROOT / "runs/parity/minimap/port.raw"))
    ap.add_argument("--truth",
                    default=str(ROOT / "runs/capture/load_clean_dump/frames/frame_00000.bmp"))
    ap.add_argument("--pal",
                    default=str(ROOT / "runs/capture/load_clean_dump/frames/frame_00000.pal"))
    ap.add_argument("--out", default=str(ROOT / "runs/parity/minimap"))
    ap.add_argument("--no-feed", action="store_true")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    pal = load_palette(Path(args.pal))
    raw = Path(args.raw).read_bytes()
    port = raw_to_image(Path(args.raw), pal)
    truth = Image.open(args.truth).convert("RGB")

    # Port indices for just the minimap box (row-major MM_W*MM_H).
    port_idx = bytes(raw[(MM_Y + r) * CANVAS_W + MM_X + c]
                     for r in range(MM_H) for c in range(MM_W))

    port_mm = crop_mm(port)
    truth_mm = crop_mm(truth)
    diff, same, total, bg = diff_image(truth_mm, port_mm, port_idx)
    pct = 100.0 * same / total if total else 0.0
    print(f"minimap box {MM_W}x{MM_H} @ ({MM_X},{MM_Y}): DRAWN pixels "
          f"{same}/{total} match = {pct:.2f}%  "
          f"(transparent backdrop cells: {bg})")

    # 3-up retail | port | diff, upscaled 4x with a 1px gutter.
    scale, gut = 4, 8
    tile_w, tile_h = MM_W * scale, MM_H * scale
    sheet = Image.new("RGB", (tile_w * 3 + gut * 2, tile_h), (60, 60, 60))
    for i, im in enumerate((truth_mm, port_mm, diff)):
        sheet.paste(im.resize((tile_w, tile_h), Image.NEAREST), (i * (tile_w + gut), 0))
    sheet_path = out / "minimap_compare.png"
    sheet.save(sheet_path)
    truth_mm.resize((tile_w, tile_h), Image.NEAREST).save(out / "minimap_truth.png")
    port_mm.resize((tile_w, tile_h), Image.NEAREST).save(out / "minimap_port.png")
    print(f"wrote {sheet_path}")

    if not args.no_feed:
        try:
            subprocess.run(
                ["nix", "run", "nixpkgs#python3", "--", str(FEED), "image",
                 str(sheet_path), "--title", "minimap: retail | port | diff",
                 "--note", f"political minimap {pct:.2f}% drawn-px match vs load_clean_dump "
                           f"(diff: red=mismatch, dark-tan=transparent backdrop)"],
                capture_output=True, text=True, timeout=120)
        except Exception as e:  # noqa: BLE001
            sys.stderr.write(f"feed push failed: {e}\n")

    return 0 if (total and same == total) else 1


if __name__ == "__main__":
    raise SystemExit(main())
