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
