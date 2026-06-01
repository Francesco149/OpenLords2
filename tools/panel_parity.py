#!/usr/bin/env python3
"""
tools/panel_parity.py — strategic-map territory-info panel parity check.

The panel unit test (test_engine_hud_panel.c / X(hud_panel_render_owned))
loads the real lastturn.sav, the misc_cty sprite bank + the three fonts +
l2.eng, then renders FUN_0040f7d3 (the owned-territory info panel) into a
640x480 8bpp canvas dumped to runs/parity/hud_panel/port.raw.

This tool applies the retail freeroam palette to that raw render, crops the
panel strip (x >= 0x1de, y in [0x9c, 0x1ae)) from both the port render and
the captured retail frame (runs/capture/load_clean_dump/frames/
frame_00000.bmp), scores the DRAWN-pixel match (port index != 0; the panel's
opaque parchment sprites cover most cells, the rest is the FUN_0040f5fd sand
backdrop this render does not draw), and pushes a retail|port|diff comparison
image to the llm-feed.

Run (after `make -C tests run` regenerates port.raw):
    nix develop -c python3 tools/panel_parity.py [--no-feed]
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
# Territory-info panel strip: title sprite 0x37 lands at (0x1de,0x9c); the
# end-turn sand strip (FUN_0040f5fd 0x39) starts at 0x1ae.
PN_X0, PN_Y0, PN_X1, PN_Y1 = 0x1de, 0x9c, CANVAS_W, 0x1ae
PN_W, PN_H = PN_X1 - PN_X0, PN_Y1 - PN_Y0


def load_palette(pal_path: Path) -> list[tuple[int, int, int]]:
    raw = pal_path.read_bytes()
    return [(raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2]) for i in range(256)]


def raw_to_image(raw_path: Path, pal: list[tuple[int, int, int]]) -> Image.Image:
    idx = raw_path.read_bytes()
    if len(idx) < CANVAS_W * CANVAS_H:
        sys.exit(f"{raw_path}: short raw ({len(idx)} bytes)")
    img = Image.new("RGB", (CANVAS_W, CANVAS_H))
    img.putdata([pal[b] for b in idx[: CANVAS_W * CANVAS_H]])
    return img


def crop_panel(img: Image.Image) -> Image.Image:
    return img.crop((PN_X0, PN_Y0, PN_X1, PN_Y1))


def diff_image(truth: Image.Image, port: Image.Image, port_idx):
    """Diff over the panel strip.  Cells the port leaves transparent
    (index 0) are the sand backdrop FUN_0040f5fd draws underneath (not
    rendered here) — scored separately.  Returns (diff_img, drawn_match,
    drawn_total, bg_cells)."""
    pt, pp = truth.load(), port.load()
    out = Image.new("RGB", truth.size)
    po = out.load()
    w, h = truth.size
    drawn_match = drawn_total = bg = 0
    for y in range(h):
        for x in range(w):
            if port_idx[y * PN_W + x] == 0:        # transparent backdrop cell
                po[x, y] = (40, 30, 15)
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
    ap.add_argument("--raw", default=str(ROOT / "runs/parity/hud_panel/port.raw"))
    ap.add_argument("--truth",
                    default=str(ROOT / "runs/capture/load_clean_dump/frames/frame_00000.bmp"))
    ap.add_argument("--pal",
                    default=str(ROOT / "runs/capture/load_clean_dump/frames/frame_00000.pal"))
    ap.add_argument("--out", default=str(ROOT / "runs/parity/hud_panel"))
    ap.add_argument("--no-feed", action="store_true")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    pal = load_palette(Path(args.pal))
    raw = Path(args.raw).read_bytes()
    port = raw_to_image(Path(args.raw), pal)
    truth = Image.open(args.truth).convert("RGB")

    port_idx = bytes(raw[(PN_Y0 + r) * CANVAS_W + PN_X0 + c]
                     for r in range(PN_H) for c in range(PN_W))

    port_pn = crop_panel(port)
    truth_pn = crop_panel(truth)
    diff, same, total, bg = diff_image(truth_pn, port_pn, port_idx)
    pct = 100.0 * same / total if total else 0.0
    print(f"panel strip {PN_W}x{PN_H} @ ({PN_X0},{PN_Y0}): DRAWN pixels "
          f"{same}/{total} match = {pct:.2f}%  (transparent backdrop: {bg})")

    scale, gut = 3, 8
    tile_w, tile_h = PN_W * scale, PN_H * scale
    sheet = Image.new("RGB", (tile_w * 3 + gut * 2, tile_h), (60, 60, 60))
    for i, im in enumerate((truth_pn, port_pn, diff)):
        sheet.paste(im.resize((tile_w, tile_h), Image.NEAREST), (i * (tile_w + gut), 0))
    sheet_path = out / "panel_compare.png"
    sheet.save(sheet_path)
    truth_pn.resize((tile_w, tile_h), Image.NEAREST).save(out / "panel_truth.png")
    port_pn.resize((tile_w, tile_h), Image.NEAREST).save(out / "panel_port.png")
    print(f"wrote {sheet_path}")

    if not args.no_feed:
        try:
            subprocess.run(
                ["nix", "run", "nixpkgs#python3", "--", str(FEED), "image",
                 str(sheet_path), "--title", "territory panel: retail | port | diff",
                 "--note", f"info panel {pct:.2f}% drawn-px match vs load_clean_dump "
                           f"(diff: red=mismatch, dark-tan=unrendered sand backdrop)"],
                capture_output=True, text=True, timeout=120)
        except Exception as e:  # noqa: BLE001
            sys.stderr.write(f"feed push failed: {e}\n")

    return 0 if (total and same == total) else 1


if __name__ == "__main__":
    raise SystemExit(main())
