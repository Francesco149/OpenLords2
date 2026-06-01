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

# End-turn button strip (FUN_0041a734): misc_cty sprite 0x3b at (0x1de,0x1cc)
# + the centred "End turn" label.  Scored separately from the info panel so
# the headline info-panel number stays comparable across runs.
ET_X0, ET_Y0, ET_X1, ET_Y1 = 0x1de, 0x1cc, CANVAS_W, CANVAS_H
ET_W, ET_H = ET_X1 - ET_X0, ET_Y1 - ET_Y0


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


def diff_image(truth: Image.Image, port: Image.Image, port_idx, w):
    """Diff over a strip.  Cells the port leaves transparent (index 0) are
    the sand backdrop FUN_0040f5fd draws underneath (not rendered here) —
    scored separately.  Returns (diff_img, drawn_match, drawn_total,
    bg_cells)."""
    pt, pp = truth.load(), port.load()
    out = Image.new("RGB", truth.size)
    po = out.load()
    cw, ch = truth.size
    drawn_match = drawn_total = bg = 0
    for y in range(ch):
        for x in range(cw):
            if port_idx[y * w + x] == 0:           # transparent backdrop cell
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


def score_region(name, raw, port, truth, x0, y0, x1, y1, out, label, push):
    """Score the DRAWN-pixel match for one strip region; write/optionally
    push a retail|port|diff comparison.  Returns (match, total)."""
    w, h = x1 - x0, y1 - y0
    port_idx = bytes(raw[(y0 + r) * CANVAS_W + x0 + c]
                     for r in range(h) for c in range(w))
    port_pn = port.crop((x0, y0, x1, y1))
    truth_pn = truth.crop((x0, y0, x1, y1))
    diff, same, total, bg = diff_image(truth_pn, port_pn, port_idx, w)
    pct = 100.0 * same / total if total else 0.0
    print(f"{name} {w}x{h} @ ({x0},{y0}): DRAWN pixels "
          f"{same}/{total} match = {pct:.2f}%  (transparent backdrop: {bg})")

    scale, gut = 3, 8
    tw, th = w * scale, h * scale
    sheet = Image.new("RGB", (tw * 3 + gut * 2, th), (60, 60, 60))
    for i, im in enumerate((truth_pn, port_pn, diff)):
        sheet.paste(im.resize((tw, th), Image.NEAREST), (i * (tw + gut), 0))
    sheet_path = out / f"{label}_compare.png"
    sheet.save(sheet_path)
    print(f"wrote {sheet_path}")

    if push:
        try:
            subprocess.run(
                ["nix", "run", "nixpkgs#python3", "--", str(FEED), "image",
                 str(sheet_path), "--title", f"{name}: retail | port | diff",
                 "--note", f"{pct:.2f}% drawn-px match vs load_clean_dump "
                           f"(diff: red=mismatch, dark-tan=unrendered backdrop)"],
                capture_output=True, text=True, timeout=120)
        except Exception as e:  # noqa: BLE001
            sys.stderr.write(f"feed push failed: {e}\n")
    return same, total


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

    push = not args.no_feed
    same, total = score_region("territory panel", raw, port, truth,
                               PN_X0, PN_Y0, PN_X1, PN_Y1, out, "panel", push)
    et_same, et_total = score_region("end-turn strip", raw, port, truth,
                                     ET_X0, ET_Y0, ET_X1, ET_Y1, out, "endturn", push)

    ok = total and same == total and et_total and et_same == et_total
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
