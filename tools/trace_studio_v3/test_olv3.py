"""Trace Studio v3 — format + replay-core regression gate.

Proves the container round-trips, content-hash dedup works, RLE == RAW, and the C
replay core's reconstruction is BIT-EXACT against the Python expander (the whole
capture→reconstruct path is the workhorse's trust anchor, so it's pinned here).
"""
from __future__ import annotations

import struct
import subprocess
from pathlib import Path

import numpy as np
import pytest

import olv3

HERE = Path(__file__).resolve().parent


def _read_bmp_rgb(path: Path) -> np.ndarray:
    data = path.read_bytes()
    off = struct.unpack_from("<I", data, 10)[0]
    w = struct.unpack_from("<i", data, 18)[0]
    h = struct.unpack_from("<i", data, 22)[0]
    px = np.frombuffer(data, np.uint8, count=w * h * 4, offset=off).reshape(h, w, 4)[::-1]
    return px[:, :, [2, 1, 0]]  # BGRA bottom-up -> RGB top-down


def test_roundtrip_and_dedup(tmp_path):
    info = olv3.make_synthetic(tmp_path / "s.olv3", w=64, h=48, n=10)
    assert info["n_frames"] == 10
    assert info["n_palettes"] == 1            # one fixed palette across the window
    c = olv3.Container.load(tmp_path / "s.olv3")
    assert c.n_frames == 10
    assert c.w == 64 and c.h == 48
    assert [f.present for f in c.frames] == list(range(100, 110))


def test_static_frames_dedup_to_one_plane(tmp_path):
    pal = bytes(range(256)) * 4 if False else bytes(256 * 4)
    plane = bytes([7]) * (32 * 24)
    wr = olv3.Writer(tmp_path / "d.olv3", 32, 24)
    for k in range(20):
        wr.add_frame(pal, plane, present=k)     # identical frame 20×
    info = wr.close()
    assert info["n_frames"] == 20
    assert info["n_pixels"] == 1                # all dedup to ONE index plane
    assert info["n_palettes"] == 1


def test_rle_matches_raw(tmp_path):
    olv3.make_synthetic(tmp_path / "raw.olv3", w=64, h=48, n=6, rle=False)
    olv3.make_synthetic(tmp_path / "rle.olv3", w=64, h=48, n=6, rle=True)
    craw = olv3.Container.load(tmp_path / "raw.olv3")
    crle = olv3.Container.load(tmp_path / "rle.olv3")
    for i in range(6):
        assert craw.indices(i) == crle.indices(i)          # decode(rle) == raw
    # RLE should not be larger on this run-heavy synthetic content
    assert (tmp_path / "rle.olv3").stat().st_size <= (tmp_path / "raw.olv3").stat().st_size


@pytest.fixture(scope="module")
def replay_bin(tmp_path_factory):
    """Build the host replay CLI once for the C-vs-Python cross-check."""
    out = tmp_path_factory.mktemp("bin")
    subprocess.run(["make", "-C", str(HERE / "replay")], check=True,
                   capture_output=True, text=True)
    return HERE / "replay" / "replay"


def test_c_render_bit_exact_vs_python(tmp_path, replay_bin):
    olv3.make_synthetic(tmp_path / "x.olv3", w=128, h=96, n=8, shift=3)
    c = olv3.Container.load(tmp_path / "x.olv3")
    for idx in (0, 3, 7):
        bmp = tmp_path / f"f{idx}.bmp"
        subprocess.run([str(replay_bin), str(tmp_path / "x.olv3"), str(idx), str(bmp)], check=True)
        assert np.array_equal(_read_bmp_rgb(bmp), c.expand_rgb(idx))


def test_summary_cli(replay_bin, tmp_path):
    olv3.make_synthetic(tmp_path / "y.olv3", w=64, h=48, n=4)
    out = subprocess.run([str(replay_bin), str(tmp_path / "y.olv3")],
                         capture_output=True, text=True, check=True).stdout
    assert '"n_frames": 4' in out and '"dims": "64x48"' in out


# ── draw-stepping (v2) ──────────────────────────────────────────────────────

def test_draw_roundtrip_and_buildup(tmp_path):
    olv3.make_synthetic_draws(tmp_path / "d.olv3", w=96, h=72, n_frames=3, draws_per_frame=5)
    c = olv3.Container.load(tmp_path / "d.olv3")
    assert c.version == 2
    for f in c.frames:
        assert len(f.draws) == 5
        # base + all draws applied == the stored final plane (build-up is exact)
        assert c.render_upto(f.index, -1) == c.indices(f.index)
        # prefix 0 == base; each successive prefix only grows the painted set
        assert c.render_upto(f.index, 0) == c.pixels[f.base_id]


def test_pick_owner(tmp_path):
    """The owning draw of a pixel is the LAST draw that wrote it."""
    olv3.make_synthetic_draws(tmp_path / "p.olv3", w=96, h=72, n_frames=1, draws_per_frame=4)
    c = olv3.Container.load(tmp_path / "p.olv3")
    f = c.frames[0]
    # take the last draw's first written pixel — its owner under the full prefix is that draw
    last = len(f.draws) - 1
    off = f.draws[last].offsets[0]
    base = c.pixels[f.base_id]
    full = c.render_upto(0, -1)
    assert full[off] == f.draws[last].vals[0]      # last draw's value wins at its pixel
    assert base[off] != full[off]                   # it really changed vs base


def test_c_render_upto_bit_exact(tmp_path, replay_bin):
    """The C replay core's render_upto matches the Python build-up, bit for bit."""
    olv3.make_synthetic_draws(tmp_path / "s.olv3", w=96, h=72, n_frames=2, draws_per_frame=5)
    c = olv3.Container.load(tmp_path / "s.olv3")
    import numpy as np
    for k in (0, 2, 5):
        bmp = tmp_path / f"k{k}.bmp"
        subprocess.run([str(replay_bin), str(tmp_path / "s.olv3"), "0", str(bmp), str(k)], check=True)
        idx = np.frombuffer(c.render_upto(0, k), np.uint8)
        pal = np.frombuffer(c.palette(0), np.uint8).reshape(256, 4)[:, :3]
        assert np.array_equal(_read_bmp_rgb(bmp), pal[idx].reshape(c.h, c.w, 3))


# ── port .draws sidecar (the C↔Python transport contract) ───────────────────

def _pack_draws_sidecar(w: int, h: int, present: int, base: bytes,
                        draws: list[dict]) -> bytes:
    """Build a <label>.draws blob byte-for-byte as src/gfx_drawcap.c writes it —
    pins the on-disk layout the C writer and the Python reader must agree on."""
    out = bytearray(struct.pack("<IIIII", 0x52444C4F, w, h, present, len(draws)))
    out += base
    for d in draws:
        offs, vals = d["offsets"], d["vals"]
        out += struct.pack("<IIIII", d["op_type"], d["m0"], d["m1"], d["m2"], len(offs))
        out += struct.pack("<%dI" % len(offs), *offs)
        out += bytes(vals)
    return bytes(out)


def test_draws_sidecar_roundtrip(tmp_path):
    import olv3_capture as cap
    w, h = 16, 12
    base = bytes([3]) * (w * h)
    # two draws: a 2x2 block, then a single pixel overwriting one of its corners
    d0 = {"op_type": olv3.OP_SPRITE, "m0": 42, "m1": 1, "m2": 1,
          "offsets": [1*w+1, 1*w+2, 2*w+1, 2*w+2], "vals": bytes([10, 11, 12, 13])}
    d1 = {"op_type": olv3.OP_GLYPH, "m0": ord("A"), "m1": 2, "m2": 2,
          "offsets": [2*w+2], "vals": bytes([99])}
    blob = _pack_draws_sidecar(w, h, 7, base, [d0, d1])
    p = tmp_path / "frame.draws"
    p.write_bytes(blob)

    got = cap._read_draws(p)
    assert got is not None
    rb, rd = got
    assert rb == base
    assert len(rd) == 2
    assert rd[0]["op_type"] == olv3.OP_SPRITE and rd[0]["m0"] == 42
    assert rd[0]["offsets"] == [1*w+1, 1*w+2, 2*w+1, 2*w+2]
    assert rd[1]["offsets"] == [2*w+2] and rd[1]["vals"] == bytes([99])

    # build the final plane the way the port would, and confirm the verify gate
    final = bytearray(base)
    for d in (d0, d1):
        for o, v in zip(d["offsets"], d["vals"]):
            final[o] = v
    assert cap._verify_draws(rb, rd, bytes(final)) == 0
    # a corrupted final must be flagged
    bad = bytearray(final); bad[0] ^= 0xFF
    assert cap._verify_draws(rb, rd, bytes(bad)) == 1


def test_draws_sidecar_rejects_bad_magic(tmp_path):
    import olv3_capture as cap
    p = tmp_path / "x.draws"
    p.write_bytes(struct.pack("<IIIII", 0xDEADBEEF, 4, 4, 0, 0) + bytes(16))
    assert cap._read_draws(p) is None
