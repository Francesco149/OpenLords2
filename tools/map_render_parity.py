#!/usr/bin/env python3
"""
tools/map_render_parity.py — strategic-map render parity check.

Drives the host map-render harness (tests/build/map_render_harness, built
with `make -C tests harness`) with retail's captured live map state, applies
the captured retail palette to the resulting 8bpp canvas, and writes a 24-bpp
BMP that can be diffed against the captured retail frame with
tools/compare_frames.py.

The capture run must hold (from frida_capture.py --dump-globals/--dump-regions):
  <run>/globals_dump.json          scalar camera + mode bindings
  <run>/dump/*.bin                 tile_cells / descriptors / territory_records
                                   / blob_primary / blob_alt / blob_overlay
  <run>/frames/frame_XXXXX.pal     retail palette (256 RGBA entries, 1024 B)

Usage:
    nix develop -c python3 tools/map_render_parity.py \\
        --run runs/capture/strat-blobs1 \\
        --pal-frame 5 \\
        --out runs/parity/map1

Writes <out>/port_map.bmp and prints the harness stats + output path.
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = ROOT / "tests" / "build" / "map_render_harness"

# Scalar bind order the harness expects on argv (after dump_dir + out_raw).
SCALAR_ORDER = [
    "cam_x0", "cam_y0", "col_start", "row_start", "col_count", "row_count",
    "x_step", "y_step", "x_half_step", "view_mode", "render_mode",
    "rotation", "overlay_base", "y_viewport",
]

CANVAS_W, CANVAS_H = 0x280, 0x1e0


def _le_hex_to_int(hexs: str) -> int:
    """Little-endian hex string (e.g. '1f000000') → int."""
    b = bytes.fromhex(hexs)
    return int.from_bytes(b, "little", signed=False)


def _load_scalars(run: Path) -> dict[str, int]:
    rows = json.loads((run / "globals_dump.json").read_text())
    by_label = {r["label"]: _le_hex_to_int(r["hex"]) for r in rows if r.get("hex")}
    missing = [k for k in SCALAR_ORDER if k not in by_label]
    if missing:
        sys.exit(f"globals_dump.json missing scalars: {missing}")
    return by_label


def _write_paletted_bmp(out: Path, width: int, height: int,
                        palette: bytes, pixels: bytes) -> None:
    """24-bpp top-down BMP from 8-bpp paletted source.  palette = 256
    PALETTEENTRY (R,G,B,flags); pixels = width*height indices."""
    row_bgr = width * 3
    row_pad = (4 - (row_bgr & 3)) & 3
    row_total = row_bgr + row_pad
    img_size = row_total * height
    file_size = 14 + 40 + img_size
    with out.open("wb") as fp:
        fp.write(b"BM")
        fp.write(struct.pack("<I", file_size))
        fp.write(b"\x00\x00\x00\x00")
        fp.write(struct.pack("<I", 54))
        fp.write(struct.pack("<IiiHHIIiiII",
                             40, width, -height, 1, 24, 0, img_size, 0, 0, 0, 0))
        pad = b"\x00" * row_pad
        for y in range(height):
            row_in = pixels[y * width:(y + 1) * width]
            row_out = bytearray(row_total)
            j = 0
            for idx in row_in:
                p = idx * 4
                row_out[j] = palette[p + 2]      # B
                row_out[j + 1] = palette[p + 1]  # G
                row_out[j + 2] = palette[p]      # R
                j += 3
            fp.write(row_out)
            fp.write(pad)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True, type=Path,
                    help="capture run dir (holds globals_dump.json + dump/ + frames/)")
    ap.add_argument("--pal-frame", type=int, default=0,
                    help="frame index whose .pal to apply (default 0)")
    ap.add_argument("--out", required=True, type=Path,
                    help="output dir for port_map.bmp + port_map.raw")
    ap.add_argument("--harness", type=Path, default=HARNESS)
    args = ap.parse_args()

    run = args.run
    dump_dir = run / "dump"
    if not dump_dir.is_dir():
        sys.exit(f"no dump/ dir under {run}")
    if not args.harness.exists():
        sys.exit(f"harness not built: {args.harness} (run: make -C tests harness)")

    args.out.mkdir(parents=True, exist_ok=True)
    raw_path = args.out / "port_map.raw"
    bmp_path = args.out / "port_map.bmp"

    scalars = _load_scalars(run)
    argv = [str(args.harness), str(dump_dir), str(raw_path)]
    argv += [str(scalars[k]) for k in SCALAR_ORDER]

    proc = subprocess.run(argv, capture_output=True, text=True)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        sys.exit(f"harness failed (rc={proc.returncode})")
    stats = proc.stdout.strip()

    pal_path = run / "frames" / f"frame_{args.pal_frame:05d}.pal"
    if not pal_path.exists():
        sys.exit(f"no palette frame: {pal_path}")
    palette = pal_path.read_bytes()
    pixels = raw_path.read_bytes()
    if len(pixels) != CANVAS_W * CANVAS_H:
        sys.exit(f"raw size {len(pixels)} != {CANVAS_W * CANVAS_H}")

    _write_paletted_bmp(bmp_path, CANVAS_W, CANVAS_H, palette, pixels)

    print(json.dumps({
        "harness": stats,
        "scalars": {k: scalars[k] for k in SCALAR_ORDER},
        "out_bmp": str(bmp_path),
        "pal": str(pal_path),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
