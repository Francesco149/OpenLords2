"""analysis/feedpush.py — push a session's captures to the llm-feed.

Builds, per paired label, one atlas PNG (row 0 = [retail | port] always
visible; row 1 = the amplified diff revealed on click — geometry cloned from
openrecet's comparison_page.py) and pushes a `comparison` item via
/opt/src/llm-feed/feed.py.  This is the standing "show render work as
retail-vs-port diffs" channel; the studio's serve UI is the deep-dive.
"""
from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

FEED = Path("/opt/src/llm-feed/feed.py")

PANEL_W = 460
PANEL_H = 345
LABEL_H = 18


def _panel(img: Image.Image, text: str) -> Image.Image:
    out = Image.new("RGB", (PANEL_W, PANEL_H + LABEL_H), (12, 12, 14))
    d = ImageDraw.Draw(out)
    d.text((4, 2), text, fill=(180, 220, 255))
    out.paste(img.resize((PANEL_W, PANEL_H), Image.NEAREST), (0, LABEL_H))
    return out


def _atlas(retail_png: Path, port_png: Path, label: str, amp: float,
           out_path: Path) -> dict | None:
    try:
        ra = Image.open(retail_png).convert("RGB")
        pa = Image.open(port_png).convert("RGB")
    except OSError:
        return None
    cw, ch = min(ra.width, pa.width), min(ra.height, pa.height)
    a = np.asarray(ra.crop((0, 0, cw, ch)), dtype=np.int16)
    b = np.asarray(pa.crop((0, 0, cw, ch)), dtype=np.int16)
    d = np.abs(a - b)
    differ = int((d.max(axis=2) > 0).sum())
    meanabs = float(d.mean())
    diff_rgb = np.clip(d * amp, 0, 255).astype(np.uint8)

    atlas_w = PANEL_W * 2
    row0_h = PANEL_H + LABEL_H
    diff_disp_h = max(1, round(atlas_w * ch / cw))
    diff_fit = Image.fromarray(diff_rgb, "RGB").resize(
        (atlas_w, diff_disp_h), Image.NEAREST)
    row1_h = diff_disp_h + LABEL_H
    diff_panel = Image.new("RGB", (atlas_w, row1_h), (12, 12, 14))
    dd = ImageDraw.Draw(diff_panel)
    dd.text((4, 2), f"diff x{amp:g} · {differ} px differ · "
                    f"mean|abs|/ch {meanabs:.2f} · black = bit-identical",
            fill=(255, 240, 120))
    diff_panel.paste(diff_fit, (0, LABEL_H))

    total_h = row0_h + row1_h
    atlas = Image.new("RGB", (atlas_w, total_h), (12, 12, 14))
    atlas.paste(_panel(ra, f"retail · {label}"), (0, 0))
    atlas.paste(_panel(pa, f"port · {label}"), (PANEL_W, 0))
    atlas.paste(diff_panel, (0, row0_h))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(out_path, optimize=True)
    return {
        "atlas": str(out_path),
        "label": label,
        "differ_px": differ,
        "meanabs": round(meanabs, 2),
        "row0_pct": round(100.0 * row0_h / atlas_w, 4),
        "total_pct": round(100.0 * total_h / atlas_w, 4),
    }


def push_session(sess_dir: Path, *, labels: list[str] | None = None,
                 amp: float = 6.0, title: str | None = None,
                 note: str | None = None) -> int:
    manifest = json.loads((sess_dir / "session.json").read_text())
    todo = [c for c in manifest.get("captures", [])
            if "port" in c and "retail" in c
            and (labels is None or c["label"] in labels)]
    if not todo:
        print("feedpush: no paired captures to push")
        return 1
    atlas_dir = sess_dir / "feed-atlases"
    panels = []
    for c in todo:
        lb = c["label"]
        p = _atlas(sess_dir / "retail" / "frames" / f"{lb}.png",
                   sess_dir / "port" / "frames" / f"{lb}.png",
                   lb, amp, atlas_dir / f"{lb}.png")
        if p:
            panels.append(p)
    if not panels:
        print("feedpush: atlas build failed for every label")
        return 1
    spec = {
        "title": title or f"trace_studio · {manifest['session']}",
        "note": note or "",
        "left_label": "retail",
        "right_label": "port",
        "panels": panels,
    }
    with tempfile.NamedTemporaryFile("w", suffix=".json",
                                     delete=False) as f:
        json.dump(spec, f)
        spec_path = f.name
    proc = subprocess.run(
        ["nix", "run", "nixpkgs#python3", "--", str(FEED),
         "comparison", "--spec", spec_path],
        capture_output=True, text=True)
    print(proc.stdout.strip() or proc.stderr.strip())
    return proc.returncode
