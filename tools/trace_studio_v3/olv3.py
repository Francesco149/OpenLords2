#!/usr/bin/env python3
"""Trace Studio v3 — olv3 capture-container reader + writer (Python).

A faithful Python counterpart to the flat olv3 container the C replay core reads
(format: format/olv3_format.h). Lords of the Realm II is 8bpp/640×480, so a frame
is an INDEX plane + a 256-entry palette; this module writes those (content-hash
dedup'd) and reads them back to reason about / verify a container.

Container = `[MAGIC][VERSION][W][H]` then a record stream: `PALETTE`/`PIXELS`
resources (each first-seen content gets a dense id) and `FRAME` commit markers
citing a (palette_id, pixels_id, present) triple. See olv3_format.h for the bytes.

Used by the capture drivers (retail agent frame payloads, port frame dumps both
feed Writer.add_frame) and by the test harness (make_synthetic). Streaming, native
endian (both ends i686), no external deps for the core path; numpy only for the
optional expand() verification helper.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = 0x33564C4F  # "OLV3"
VERSION = 1

# record types (mirror olv3_format.h)
PALETTE, PIXELS, FRAME, EOF = 2, 3, 10, 99
# PIXELS encodings
ENC_RAW, ENC_RLE = 0, 1

PALETTE_BYTES = 256 * 4  # 256 × (R,G,B,flags)


# ──────────────────────────────────────────────────────────────────────────────
# Writer — content-hash dedup of palettes + index planes across the window.
# ──────────────────────────────────────────────────────────────────────────────
class Writer:
    """Streaming olv3 container writer.

    add_frame(palette_1024, indices_whbytes, present): a palette/plane whose exact
    bytes were already emitted reuses its id (a static screen or a fixed palette is
    stored ONCE); otherwise a new resource record is written before the FRAME that
    first cites it. RAW pixels by default; pass rle=True to byte-RLE the plane (8bpp
    UI compresses well) — the C reader handles both.
    """

    def __init__(self, path: str | Path, w: int, h: int, rle: bool = False):
        self.path = Path(path)
        self.w, self.h = int(w), int(h)
        self.rle = rle
        self._f = self.path.open("wb")
        self._f.write(struct.pack("<IIII", MAGIC, VERSION, self.w, self.h))
        self._pal_ids: dict[bytes, int] = {}
        self._pix_ids: dict[bytes, int] = {}
        self._n_frames = 0
        self._bytes_raw = 0   # accounting: raw pixel bytes that dedup/encoding saved

    def _wu(self, *vals: int) -> None:
        self._f.write(struct.pack("<%dI" % len(vals), *vals))

    def _palette_id(self, pal: bytes) -> int:
        if len(pal) != PALETTE_BYTES:
            raise ValueError(f"palette must be {PALETTE_BYTES} bytes, got {len(pal)}")
        pid = self._pal_ids.get(pal)
        if pid is None:
            pid = len(self._pal_ids)
            self._pal_ids[pal] = pid
            self._wu(PALETTE, pid)
            self._f.write(pal)
        return pid

    def _pixels_id(self, idx: bytes) -> int:
        if len(idx) != self.w * self.h:
            raise ValueError(f"index plane must be {self.w*self.h} bytes, got {len(idx)}")
        pid = self._pix_ids.get(idx)
        if pid is None:
            pid = len(self._pix_ids)
            self._pix_ids[idx] = pid
            if self.rle:
                data, enc = _rle_encode(idx), ENC_RLE
            else:
                data, enc = idx, ENC_RAW
            self._wu(PIXELS, pid, enc, self.w, self.h, len(data))
            self._f.write(data)
        else:
            self._bytes_raw += len(idx)   # this frame's pixels cost nothing
        return pid

    def add_frame(self, palette_1024: bytes, indices_whbytes: bytes, present: int) -> None:
        pal_id = self._palette_id(palette_1024)
        pix_id = self._pixels_id(indices_whbytes)
        self._wu(FRAME, pal_id, pix_id, present & 0xFFFFFFFF)
        self._n_frames += 1

    def close(self) -> dict:
        self._wu(EOF)
        self._f.close()
        return {
            "path": str(self.path),
            "n_frames": self._n_frames,
            "n_palettes": len(self._pal_ids),
            "n_pixels": len(self._pix_ids),
            "size": self.path.stat().st_size,
            "dedup_saved_bytes": self._bytes_raw,
        }


def _rle_encode(buf: bytes) -> bytes:
    out = bytearray()
    i, n = 0, len(buf)
    while i < n:
        v = buf[i]
        run = 1
        while i + run < n and buf[i + run] == v and run < 255:
            run += 1
        out += bytes((run, v))
        i += run
    return bytes(out)


def _rle_decode(buf: bytes, outlen: int) -> bytes:
    out = bytearray()
    i = 0
    while i + 2 <= len(buf):
        cnt, val = buf[i], buf[i + 1]
        out += bytes((val,)) * cnt
        i += 2
    if len(out) != outlen:
        raise ValueError(f"rle decode {len(out)} != {outlen}")
    return bytes(out)


# ──────────────────────────────────────────────────────────────────────────────
# Reader — parse + reason about a container.
# ──────────────────────────────────────────────────────────────────────────────
@dataclass
class Frame:
    index: int          # 0-based kept-frame index
    palette_id: int
    pixels_id: int
    present: int        # absolute engine present counter at capture


class Container:
    def __init__(self, data: bytes):
        self.data = data
        self.w = self.h = 0
        self.version = 0
        self.palettes: dict[int, bytes] = {}     # id -> 1024 bytes
        self.pixels: dict[int, bytes] = {}        # id -> w*h index plane (decoded)
        self.frames: list[Frame] = []
        self._parse()

    @classmethod
    def load(cls, path: str | Path) -> "Container":
        return cls(Path(path).read_bytes())

    def _parse(self) -> None:
        d = self.data
        n = len(d)

        def u(off):
            return struct.unpack_from("<I", d, off)[0]

        if u(0) != MAGIC:
            raise ValueError(f"bad magic 0x{u(0):08x} (want 0x{MAGIC:08x})")
        self.version = u(4)
        self.w, self.h = u(8), u(12)
        plane = self.w * self.h
        p = 16
        while p < n:
            t = u(p); p += 4
            if t == PALETTE:
                pid = u(p); p += 4
                self.palettes[pid] = d[p:p + PALETTE_BYTES]; p += PALETTE_BYTES
            elif t == PIXELS:
                pid = u(p); enc = u(p + 4); w = u(p + 8); h = u(p + 12); dl = u(p + 16); p += 20
                raw = d[p:p + dl]; p += dl
                self.pixels[pid] = raw if enc == ENC_RAW else _rle_decode(raw, w * h)
            elif t == FRAME:
                pal_id, pix_id, present = u(p), u(p + 4), u(p + 8); p += 12
                self.frames.append(Frame(len(self.frames), pal_id, pix_id, present))
            elif t == EOF:
                break
            else:
                raise ValueError(f"unknown record type {t} at {p-4}")
        # light validation: every cited resource exists, every plane is full-size
        for f in self.frames:
            if f.palette_id not in self.palettes:
                raise ValueError(f"frame {f.index} cites missing palette {f.palette_id}")
            if f.pixels_id not in self.pixels:
                raise ValueError(f"frame {f.index} cites missing pixels {f.pixels_id}")
            if len(self.pixels[f.pixels_id]) != plane:
                raise ValueError(f"frame {f.index} plane wrong size")

    @property
    def n_frames(self) -> int:
        return len(self.frames)

    def indices(self, i: int) -> bytes:
        return self.pixels[self.frames[i].pixels_id]

    def palette(self, i: int) -> bytes:
        return self.palettes[self.frames[i].palette_id]

    def expand_rgb(self, i: int):
        """Frame i as an (h, w, 3) uint8 RGB array (numpy). Verification helper."""
        import numpy as np
        idx = np.frombuffer(self.indices(i), dtype=np.uint8)
        pal = np.frombuffer(self.palette(i), dtype=np.uint8).reshape(256, 4)[:, :3]
        return pal[idx].reshape(self.h, self.w, 3)

    def summary(self) -> dict:
        return {
            "version": self.version,
            "dims": f"{self.w}x{self.h}",
            "n_frames": self.n_frames,
            "n_palettes": len(self.palettes),
            "n_pixels": len(self.pixels),
            "present_first": self.frames[0].present if self.frames else None,
            "present_last": self.frames[-1].present if self.frames else None,
            "size": len(self.data),
        }


# ──────────────────────────────────────────────────────────────────────────────
# Synthetic generator — exercise the replay core + viewer before real capture.
# ──────────────────────────────────────────────────────────────────────────────
def make_synthetic(path: str | Path, w: int = 640, h: int = 480, n: int = 48,
                   shift: int = 0, rle: bool = False, pal_perturb: int = 0) -> dict:
    """Write a container of `n` test frames: a static checker background, a moving
    box, and a 1-px scrolling band, on a 256-colour ramp palette. `shift` offsets
    the moving box (give the 'retail' and 'port' containers different shifts so the
    diff panel lights up); `pal_perturb` nudges the top-6 palette entries (so a
    differing-palette pair exercises the palette panel). No numpy dependency."""
    pal = bytearray(256 * 4)
    for i in range(256):                       # a colourful ramp so the palette strip is legible
        pal[i*4+0] = (i * 7) & 0xFF            # R
        pal[i*4+1] = (i * 3) & 0xFF            # G
        pal[i*4+2] = i                          # B
        pal[i*4+3] = 0                          # flags
    for i in range(250, 256):                  # palette-builder divergence demo (few entries)
        pal[i*4+0] = (pal[i*4+0] + pal_perturb) & 0xFF
    pal = bytes(pal)

    cell = max(4, min(w, h) // 16)             # checker cell, scaled to the frame
    box = max(4, min(w, h) // 6)               # moving box size, scaled
    band = max(2, h // 60)                      # scrolling band height
    wr = Writer(path, w, h, rle=rle)
    for fr in range(n):
        plane = bytearray(w * h)
        # static checker background (index 16/32)
        for y in range(h):
            base = y * w
            cell_y = (y // cell) & 1
            for x in range(w):
                plane[base + x] = 16 if ((x // cell) & 1) ^ cell_y else 32
        # a moving box (index 200), position advances with the frame (+shift)
        bx = (5 + (fr + shift) * 6) % max(1, w - box)
        by = (5 + (fr + shift) * 3) % max(1, h - box)
        for y in range(by, by + box):
            row = y * w
            for x in range(bx, bx + box):
                plane[row + x] = 200
        # a scrolling band (palette ramp) so something changes every frame
        band_y = (fr * 4) % max(1, h - band)
        for y in range(band_y, band_y + band):
            row = y * w
            for x in range(w):
                plane[row + x] = (x + fr) & 0xFF
        wr.add_frame(pal, bytes(plane), present=100 + fr)
    return wr.close()


if __name__ == "__main__":
    import json
    import sys
    if len(sys.argv) >= 3 and sys.argv[1] == "synth":
        info = make_synthetic(sys.argv[2], n=int(sys.argv[3]) if len(sys.argv) > 3 else 48,
                              shift=int(sys.argv[4]) if len(sys.argv) > 4 else 0)
        print(json.dumps(info, indent=1))
    elif len(sys.argv) >= 2:
        print(json.dumps(Container.load(sys.argv[1]).summary(), indent=1))
    else:
        raise SystemExit("usage: olv3.py <cap.olv3> | synth <out.olv3> [n] [shift]")
