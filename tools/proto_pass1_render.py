#!/usr/bin/env python3
"""
tools/proto_pass1_render.py — Python PROTOTYPE of strategic-map pass-1
(base-terrain) rendering, for de-risking the C port of the
FUN_0040526e cluster before writing it.

Mirrors, against the captured dataset (runs/capture/strat-terrain1/dump
+ globals_dump.json), exactly what the C drawer-A path will do:

  * camera walk  FUN_0040526e / FUN_00405ae9 / FUN_00405c2f  →  per-cell
    (cursor_x, cursor_y, phase)
  * drawer A     FUN_004063c1  →  blob select (desc[cell+2]&0x1c),
    sprite idx (desc[cell+3]), src_off (i32 @ blob+idx*0x10+0xc)
  * sky          FUN_0042a7f1  →  blob0 at index desc-derived 0x5c9288
  * blitters     table replay from src/gen/blit_table_{452820,45337b,4538f6}.h
                 (view-mode 0 only)

Drawer B (decorations, FUN_00406673) is NOT rendered yet — its cells are
counted so we know how much of the frame is still missing.

Writes an indexed canvas + palette-applied BMP and (optionally) a diff
vs the clean retail frame.

Usage:
    nix develop -c python3 tools/proto_pass1_render.py \
        --run runs/capture/strat-terrain1 \
        --pal runs/capture/strat-spray1/frames/frame_00009.pal \
        --truth runs/capture/strat-spray1/frames/frame_00009.bmp \
        --out runs/parity/proto1
"""
from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "src" / "gen"

CANVAS_W, CANVAS_H = 0x280, 0x1e0   # 640 x 480
PITCH = CANVAS_W
EMPTY = 0xfff0000
STRIDE = 0x104                      # tile-array row stride in bytes


def load_blit_table(va: str):
    """Parse src/gen/blit_table_<va>.h into (block_a, block_b) lists of
    (dest_off, src_off, size)."""
    text = (GEN / f"blit_table_{va}.h").read_text()
    blocks = {}
    cur = None
    row = re.compile(r"\{0x([0-9a-f]+),\s*0x([0-9a-f]+),\s*(\d+)\}")
    for ln in text.splitlines():
        m = re.search(r"k_blit_%s_([ab])\[\]" % va, ln)
        if m:
            cur = m.group(1)
            blocks[cur] = []
            continue
        if cur and "};" in ln:
            cur = None
            continue
        if cur:
            mm = row.search(ln)
            if mm:
                blocks[cur].append((int(mm.group(1), 16),
                                    int(mm.group(2), 16),
                                    int(mm.group(3))))
    return blocks.get("a", []), blocks.get("b", [])


class Renderer:
    def __init__(self, run: Path):
        d = run / "dump"
        self.cells = (d / "tile_cells.bin").read_bytes()
        self.desc = (d / "descriptors.bin").read_bytes()
        self.blobs = [
            (d / f"terr_blob{i}.bin").read_bytes() for i in range(5)
        ]
        g = {r["label"]: int.from_bytes(bytes.fromhex(r["hex"]), "little")
             for r in json.loads((run / "globals_dump.json").read_text())}
        self.g = g
        self.canvas = bytearray(CANVAS_W * CANVAS_H)
        # blit tables, view-mode-0 family
        self.tbl = {va: load_blit_table(va)
                    for va in ("452820", "45337b", "4538f6")}
        # stats
        self.n_drawA = self.n_sky = self.n_drawB_skipped = 0
        self.n_oob = self.n_badsrc = 0
        self.render_mode = g["render_mode"]
        self.view_mode = g["view_mode"]

    # -- tile array ---------------------------------------------------------
    def cell(self, col, row):
        off = row * STRIDE + col * 4
        return int.from_bytes(self.cells[off:off + 4], "little")

    # -- blitter (table replay) --------------------------------------------
    def blit(self, va, blob, src_off, cx, cy, phase):
        a, b = self.tbl[va]
        dest_base = cx + cy * PITCH
        runs = []
        if va == "452820":
            if phase != 2:
                runs.append(a)
            if phase != 1:
                runs.append(b)
        else:                       # 45337b / 4538f6 always full
            runs.append(a)
            runs.append(b)
        for tbl in runs:
            for (doff, soff, size) in tbl:
                s = src_off + soff
                d = dest_base + doff
                if s < 0 or s + size > len(blob):
                    self.n_oob += 1
                    continue
                if d < 0 or d + size > len(self.canvas):
                    self.n_oob += 1
                    continue
                self.canvas[d:d + size] = blob[s:s + size]

    def descriptor_src_off(self, blob, idx):
        e = idx * 0x10 + 0xc
        return int.from_bytes(blob[e:e + 4], "little", signed=True)

    # -- drawer A (FUN_004063c1) -------------------------------------------
    def drawer_a(self, cell, cx, cy, phase):
        d2 = self.desc[cell + 2]
        sel = d2 & 0x1c
        idx = self.desc[cell + 3]
        if self.render_mode == 1 and (d2 & 0x20) == 0:
            sel = 0
            idx = 0
        blob_i = {0x00: 0, 0x04: 1, 0x08: 2, 0x0c: 3, 0x10: 4}.get(sel)
        if blob_i is None:
            return                  # no draw (return path in retail)
        blob = self.blobs[blob_i]
        src_off = self.descriptor_src_off(blob, idx)
        if not (0 <= src_off < 0x4baf1):
            self.n_badsrc += 1
            return
        if self.view_mode != 0:
            return                  # only view-mode 0 family ported
        va = {3: "45337b", 4: "4538f6"}.get(phase, "452820")
        self.blit(va, blob, src_off, cx, cy, phase)
        self.n_drawA += 1

    # -- sky/empty (FUN_0042a7f1) ------------------------------------------
    def sky(self, c9288, cx, cy, phase):
        blob = self.blobs[0]
        src_off = self.descriptor_src_off(blob, c9288)
        if not (0 <= src_off < 0x4baf1):
            self.n_badsrc += 1
            return
        if self.view_mode != 0:
            return
        self.blit("452820", blob, src_off, cx, cy, phase)
        self.n_sky += 1

    # -- per-cell dispatch -------------------------------------------------
    def do_cell(self, col, row, cx, cy, phase, sky_gate=False):
        cv = self.cell(col, row)
        if cv < EMPTY:
            self.drawer_a(cv, cx, cy, phase)
            self.n_drawB_skipped += 1      # drawer B would run here
        else:
            c9288 = 0 if self.render_mode == 1 else (cv - EMPTY)
            # gate present in iterators (DAT_0053e9ac assumed 0); phantom
            # rows always draw.  6 < c9288 condition.
            if (not sky_gate) or (c9288 > 6):
                self.sky(c9288, cx, cy, phase)

    # -- camera walk -------------------------------------------------------
    def render(self):
        g = self.g
        x0, y0 = g["cam_x0"], g["cam_y0"]
        col_start, row_start = g["col_start"], g["row_start"]
        col_count, row_count = g["col_count"], g["row_count"]
        x_step, y_step, x_half = g["x_step"], g["y_step"], g["x_half_step"]

        cur_row = row_start

        # --- top phantom row, phase 2 ---
        cx, cy = x0, y0
        col = col_start
        for _ in range(col_count):
            self.do_cell(col, cur_row, cx, cy, 2)
            col += 1
            cx += x_step
        cy += y_step
        cur_row += 1

        def row_c2f(row, cy):
            # FUN_00405c2f: first cell phase 3, then col_count-1 phase 0,
            # then last cell phase 4.  half-step shift after first cell.
            cx = x0
            self.do_cell(col_start, row, cx, cy, 3, sky_gate=True)
            cx += x_half
            c = col_start + 1
            for _ in range(col_count - 1):
                self.do_cell(c, row, cx, cy, 0, sky_gate=True)
                c += 1
                cx += x_step
            self.do_cell(c, row, cx, cy, 4, sky_gate=True)

        def row_ae9(row, cy):
            # FUN_00405ae9: col_count cells, all phase 0, aligned.
            cx = x0
            c = col_start
            for _ in range(col_count):
                self.do_cell(c, row, cx, cy, 0, sky_gate=True)
                c += 1
                cx += x_step

        row_c2f(cur_row, cy)
        cy += y_step
        cur_row += 1

        for _ in range((row_count - 2) // 2):
            row_ae9(cur_row, cy)
            cy += y_step
            cur_row += 1
            row_c2f(cur_row, cy)
            cy += y_step
            cur_row += 1

        # --- bottom phantom row, phase 1 ---
        cx = x0
        col = col_start
        for _ in range(col_count):
            self.do_cell(col, cur_row, cx, cy, 1)
            col += 1
            cx += x_step


def write_bmp(path: Path, pal: bytes, pixels: bytes, w=CANVAS_W, h=CANVAS_H):
    row_bgr = w * 3
    pad = (4 - (row_bgr & 3)) & 3
    rt = row_bgr + pad
    img = rt * h
    with path.open("wb") as fp:
        fp.write(b"BM")
        fp.write(struct.pack("<I", 14 + 40 + img))
        fp.write(b"\x00\x00\x00\x00")
        fp.write(struct.pack("<I", 54))
        fp.write(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 24, 0, img, 0, 0, 0, 0))
        z = b"\x00" * pad
        for y in range(h):
            ri = pixels[y * w:(y + 1) * w]
            ro = bytearray(rt)
            j = 0
            for idx in ri:
                p = idx * 4
                ro[j] = pal[p + 2]
                ro[j + 1] = pal[p + 1]
                ro[j + 2] = pal[p]
                j += 3
            fp.write(ro)
            fp.write(z)


def read_bmp24(path: Path):
    data = path.read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    topdown = h < 0
    h = abs(h)
    row_bgr = w * 3
    pad = (4 - (row_bgr & 3)) & 3
    rt = row_bgr + pad
    rows = []
    for y in range(h):
        sy = y if topdown else (h - 1 - y)
        base = off + sy * rt
        rows.append(data[base:base + row_bgr])
    return w, h, b"".join(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True, type=Path)
    ap.add_argument("--pal", required=True, type=Path)
    ap.add_argument("--truth", type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    r = Renderer(args.run)
    r.render()

    args.out.mkdir(parents=True, exist_ok=True)
    pal = args.pal.read_bytes()
    bmp = args.out / "proto_pass1.bmp"
    write_bmp(bmp, pal, bytes(r.canvas))

    stats = {
        "drawA": r.n_drawA, "sky": r.n_sky,
        "drawB_skipped": r.n_drawB_skipped,
        "oob_copies": r.n_oob, "bad_src_off": r.n_badsrc,
        "out": str(bmp),
    }

    if args.truth:
        tw, th, truth = read_bmp24(args.truth)
        my = args.out / "_mine24.tmp"
        write_bmp(my, pal, bytes(r.canvas))
        _, _, mine = read_bmp24(my)
        my.unlink()
        # per-pixel exact match over the map region (left 480 cols)
        REGION_W = 480
        same = tot = 0
        for y in range(min(th, CANVAS_H)):
            for x in range(REGION_W):
                o = (y * tw + x) * 3
                m = (y * CANVAS_W + x) * 3
                tot += 1
                if truth[o:o + 3] == mine[m:m + 3]:
                    same += 1
        stats["map_region"] = f"{REGION_W}x{th}"
        stats["pixel_match"] = f"{same}/{tot} = {100*same/tot:.1f}%"

    print(json.dumps(stats, indent=2))


if __name__ == "__main__":
    main()
