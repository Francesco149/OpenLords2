"""transport/encode.py — per-group scrub videos.

Dense capture groups (a compiled {caprange}) encode to small all-intra mp4s
(`-g 1`, openrecet lesson: frame-exact seeking needs every frame to be a
keyframe).  Frames are label-ordered via the concat demuxer (labels aren't
numeric filenames).
"""
from __future__ import annotations

import subprocess
from pathlib import Path

VIDEO_FPS = 10
MIN_GROUP_FRAMES = 4          # below this a video adds nothing over stills


def encode_group(frames: list[Path], out_mp4: Path) -> bool:
    frames = [f for f in frames if f.exists()]
    if len(frames) < MIN_GROUP_FRAMES:
        return False
    lst = out_mp4.with_suffix(".frames.txt")
    lst.write_text("".join(
        f"file '{f.resolve()}'\nduration {1 / VIDEO_FPS}\n" for f in frames))
    try:
        proc = subprocess.run(
            ["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", str(lst),
             "-fps_mode", "cfr", "-r", str(VIDEO_FPS),
             "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
             "-g", "1", "-pix_fmt", "yuv420p", str(out_mp4)],
            capture_output=True, timeout=300)
        return proc.returncode == 0 and out_mp4.exists()
    except Exception:                               # noqa: BLE001
        return False
    finally:
        lst.unlink(missing_ok=True)
