#!/usr/bin/env python3
"""film.py — a captioned gameplay video from a deterministic rhost replay.

    python3 tools/rhost/film.py SPEC.json [--run DIR] [--keep]

SPEC (JSON):
  {"scenario": "siege-castles",           # tests/scenarios/<s> (replayed on wine: pure retail)
   "trace": "/path/x.jsonl",               # … or any trace file instead of a scenario
   "out": "/path/clip.mp4",
   "title": "Castle damage is remembered",  # first caption-band line throughout (optional)
   "extra": "--movies",                    # more rhost args (optional; trace meta applies anyway)
   "standalone": false,                    # replay the PORT alone (rhost --standalone) instead of retail
   "compare": false,                       # side by side: retail (left) | the port alone (right), 1x each, with a
                                           #   running count of identical presented frames (index plane + palette)
   "segments": [                            # game-frame ranges, in order; frames outside every segment are cut
     {"from": 600, "to": 2400, "speed": 1, "caption": "…"},
     {"from": 2400, "to": 9000, "speed": 8, "caption": "…"}]}

One rhost run captures the span of all segments (`--cap A:N --cap-wav`: capture.olv3 + capture.vclk + capture.wav,
rh_shot.c / rh_dsound.c), then frames are laid out on the virtual clock (a Sleep inside a frame holds the picture, as on
the real machine), scaled 2x (nearest), a caption band added under the 640x480 picture, the mouse drawn from the trace's
input events (retail's cursor is a hardware cursor: not in the presented frame), and encoded with ffmpeg (H.264 + AAC).
Fast-forwarded segments keep their sound, time-compressed at the same pitch (atempo), and carry a fast-forward badge. Everything comes from the user's own game files at run time; outputs are
never committed (they show game assets)."""
import argparse, glob, json, os, struct, subprocess, sys, wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FPS = 30
SCALE = 2
W, H = 640, 480
BAND = 132
FONT = "/usr/share/fonts/noto/NotoSans-Regular.ttf"
FONT_B = "/usr/share/fonts/noto/NotoSans-Bold.ttf"
HZ = 22050


def read_olv3(path):
    """[(frame, palette_rgb (256,3) uint8, plane (H,W) uint8)] per captured frame (resources shared by id)."""
    d = open(path, "rb").read()
    magic, ver, w, h = struct.unpack_from("<4I", d, 0)
    assert magic == 0x33564C4F and (w, h) == (W, H), (hex(magic), w, h)
    o = 16
    pals, pixs, frames = {}, {}, []
    while o < len(d):
        (t,) = struct.unpack_from("<I", d, o); o += 4
        if t == 2:
            (pid,) = struct.unpack_from("<I", d, o); o += 4
            pals[pid] = np.frombuffer(d, np.uint8, 1024, o).reshape(256, 4)[:, :3].copy(); o += 1024
        elif t == 3:
            xid, enc, pw, ph, n = struct.unpack_from("<5I", d, o); o += 20
            assert enc == 0
            pixs[xid] = np.frombuffer(d, np.uint8, n, o).reshape(ph, pw); o += n
        elif t == 10:
            pid, xid, present, base, ndraw = struct.unpack_from("<5I", d, o); o += 20
            frames.append((present, pid, xid))
        elif t == 99:
            break
        else:
            raise SystemExit(f"olv3: record type {t} at {o - 4}")
    return [(f, pals[p], pixs[x]) for f, p, x in frames]


def mouse_track(trace):
    """frame -> (x, y, down) from the trace's mouse events, gliding to each new position over up to 12 frames."""
    ev = []
    for line in open(trace):
        if line.startswith("#") or not line.strip():
            continue
        e = json.loads(line)
        if "f" in e and e.get("m") in (0x200, 0x201, 0x202, 0x204, 0x205, 0x203):
            ev.append((e["f"], e["m"], e["l"] & 0xffff, (e["l"] >> 16) & 0xffff))
    return ev


class Mouse:
    def __init__(self, ev):
        self.ev = ev

    def at(self, f):
        x, y, down, prev, pf, click = 320, 240, False, (320, 240), None, None
        for ef, m, ex, ey in self.ev:
            if ef > f:
                break
            prev, pf = (x, y), ef
            x, y = ex, ey
            if m in (0x201, 0x204):
                down, click = True, ef
            elif m in (0x202, 0x205):
                down = False
        # glide: look ahead to the next move event, start moving up to 12 frames before it
        nxt = next(((ef, ex, ey) for ef, m, ex, ey in self.ev if ef > f), None)
        if nxt and (nxt[1], nxt[2]) != (x, y):
            span = min(12, max(1, nxt[0] - (pf or 0)))
            k = (f - (nxt[0] - span)) / span
            if k > 0:
                k = k * k * (3 - 2 * k)
                x, y = x + (nxt[1] - x) * k, y + (nxt[2] - y) * k
        return x, y, down, (f - click) if click is not None else 999


ARROW = [(0, 0), (0, 17), (4, 13), (7, 20), (10, 19), (7, 12), (12, 12)]


def draw_cursor(dr, x, y, down, since_click, sc=SCALE):
    if since_click < 14:
        r = (6 + since_click * 2.2) * sc / SCALE
        a = int(255 * (1 - since_click / 14))
        dr.ellipse([x * sc - r, y * sc - r, x * sc + r, y * sc + r], outline=(255, 230, 120, a), width=3 if sc > 1 else 2)
    pts = [(x * sc + px * 1.4 * sc / SCALE, y * sc + py * 1.4 * sc / SCALE) for px, py in ARROW]
    dr.polygon(pts, fill=(255, 255, 255, 255) if not down else (255, 230, 120, 255), outline=(0, 0, 0, 255))


def tempo(pcm, speed):
    """stereo int16 at HZ, played `speed` times faster at the same pitch (ffmpeg atempo, chained in factors <= 2)"""
    if len(pcm) == 0:
        return pcm
    f, chain = float(speed), []
    while f > 2.0:
        chain.append("atempo=2.0"); f /= 2.0
    chain.append(f"atempo={f:.6f}")
    out = subprocess.run(["ffmpeg", "-loglevel", "error", "-f", "s16le", "-ar", str(HZ), "-ac", "2", "-i", "-",
                          "-af", ",".join(chain), "-f", "s16le", "-ar", str(HZ), "-ac", "2", "-"],
                         input=np.ascontiguousarray(pcm, dtype="<i2").tobytes(), capture_output=True, check=True).stdout
    return np.frombuffer(out, "<i2").reshape(-1, 2)


def ff_badge(dr, right, top, speed, font):
    """a fast-forward badge: two triangles + the factor, on a translucent plate, its top-right corner at (right, top)"""
    label = f"{speed:g}x"
    tw = dr.textlength(label, font=font)
    w, h = 62 + tw, 40
    x0, y0 = right - w, top
    dr.rounded_rectangle([x0, y0, right, y0 + h], radius=8, fill=(10, 12, 18, 170))
    for k in range(2):
        ox = x0 + 10 + k * 18
        dr.polygon([(ox, y0 + 9), (ox, y0 + h - 9), (ox + 18, y0 + h / 2)], fill=(255, 255, 255, 235))
    dr.text((x0 + 52, y0 + 6), label, font=font, fill=(255, 255, 255, 235))


def caption_band(title, text, speed, fonts, width=W * SCALE):
    img = Image.new("RGB", (width, BAND), (18, 18, 22))
    dr = ImageDraw.Draw(img)
    fb, fr, fs = fonts
    y = 10
    if title:
        dr.text((20, y), title, font=fb, fill=(240, 205, 120)); y += 34
    # wrap the caption
    words, line, lines = (text or "").split(), "", []
    for w_ in words:
        t = (line + " " + w_).strip()
        if dr.textlength(t, font=fr) > width - 40:
            lines.append(line); line = w_
        else:
            line = t
    if line:
        lines.append(line)
    for ln in lines[:3]:
        dr.text((20, y), ln, font=fr, fill=(235, 235, 235)); y += 28
    return np.asarray(img)


def run_capture(spec, lo, hi, standalone=None):
    extra = f"--cap {lo}:{hi - lo + 1} --cap-wav " + spec.get("extra", "")
    cmd = [sys.executable, os.path.join(ROOT, "tools/rhost/rhost.py"), "run", spec.get("trace") or spec["scenario"], "--target", "wine",
           "--extra=" + extra.strip()]
    if spec.get("frames"):
        cmd += ["--frames", str(spec["frames"])]
    if spec.get("standalone") if standalone is None else standalone:
        cmd[-1] += " --standalone"
    print("+", " ".join(cmd), flush=True)
    out = subprocess.run(cmd, capture_output=True, text=True)
    print(out.stdout[-1500:], out.stderr[-1500:])
    name = os.path.splitext(os.path.basename(spec["trace"]))[0] if spec.get("trace") else spec["scenario"]
    runs = sorted(glob.glob("/mnt/ephemeral/openlords2/rhost-wine/root/runs/*/" + name + "/capture.olv3"),
                  key=os.path.getmtime)
    if not runs:
        raise SystemExit("no capture.olv3 produced")
    return os.path.dirname(runs[-1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("--run", help="reuse a run dir that already holds capture.olv3 / .vclk / .wav")
    ap.add_argument("--port-run", help="compare: reuse the port's (--standalone) run dir")
    a = ap.parse_args()
    spec = json.load(open(a.spec))
    segs = spec["segments"]
    lo, hi = min(s["from"] for s in segs), max(s["to"] for s in segs)
    run = a.run or run_capture(spec, lo, hi)
    frames = read_olv3(os.path.join(run, "capture.olv3"))
    port = None
    if spec.get("compare"):
        prun = a.port_run or run_capture(spec, lo, hi, standalone=True)
        port = read_olv3(os.path.join(prun, "capture.olv3"))
        assert [f for f, _, _ in port] == [f for f, _, _ in frames], "retail / port captured different frame lists"
        same = np.array([np.array_equal(p1, p2) and np.array_equal(x1, x2)
                         for (_, p1, x1), (_, p2, x2) in zip(frames, port)])
        print(f"compare: {int(same.sum())}/{len(same)} captured frames identical (retail {run} | port {prun})")
        cum_same = np.cumsum(same)
    vc = np.fromfile(os.path.join(run, "capture.vclk"), dtype="<u4").reshape(-1, 2)
    assert len(vc) == len(frames), (len(vc), len(frames))
    fnum = vc[:, 0].astype(np.int64); vclk = vc[:, 1].astype(np.int64)
    t0 = int(vclk[0])
    with wave.open(os.path.join(run, "capture.wav")) as wf:
        assert wf.getframerate() == HZ and wf.getnchannels() == 2
        pcm = np.frombuffer(wf.readframes(wf.getnframes()), "<i2").reshape(-1, 2)
    trace = spec.get("trace") or os.path.join(ROOT, "tests/scenarios", spec["scenario"], "trace.jsonl")
    mouse = Mouse(mouse_track(trace))
    fonts = (ImageFont.truetype(FONT_B, 24), ImageFont.truetype(FONT, 21), ImageFont.truetype(FONT, 16))
    out = spec["out"]
    tmpv, tmpa = out + ".video.mp4", out + ".audio.wav"
    scale = 1 if port is not None else SCALE
    top = 48 if port is not None else 0
    ow, oh = W * 2 if port is not None else W * SCALE, top + H * scale + BAND
    if port is not None:
        lab = Image.new("RGB", (ow, top), (18, 18, 22)); dl = ImageDraw.Draw(lab)
        dl.text((12, 10), "Original Lords2.exe", font=fonts[0], fill=(240, 205, 120))
        dl.text((W + 12, 10), "OpenLords2: the port alone", font=fonts[0], fill=(150, 220, 150))
        label = np.asarray(lab)
    enc = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{ow}x{oh}",
                            "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "slow", "-crf", str(spec.get("crf", 28)),
                            "-tune", "animation", "-pix_fmt", "yuv420p", tmpv], stdin=subprocess.PIPE)
    audio = []
    band_cache = {}
    nout = 0
    for s in segs:
        i0 = int(np.searchsorted(fnum, s["from"])); i1 = int(np.searchsorted(fnum, s["to"], side="right")) - 1
        ta, tb = int(vclk[i0]), int(vclk[i1])
        speed = s.get("speed", 1)
        key = (s.get("caption", ""), speed)
        if key not in band_cache:
            band_cache[key] = caption_band(spec.get("title", ""), s.get("caption", ""), speed, fonts, ow)
        band = band_cache[key]
        n = max(1, int((tb - ta) / speed * FPS / 1000))
        last_idx, last_rgb = -1, None
        for j in range(n):
            t = ta + j * 1000 * speed / FPS
            idx = int(np.searchsorted(vclk, t, side="right")) - 1
            idx = max(i0, min(idx, i1))
            if idx != last_idx:
                f, pal, plane = frames[idx]
                rgb = pal[plane]
                if port is not None:
                    _, ppal, pplane = port[idx]
                    last_rgb = np.hstack([rgb, ppal[pplane]])
                else:
                    last_rgb = np.repeat(np.repeat(rgb, SCALE, 0), SCALE, 1)
                last_idx = idx
            img = Image.fromarray(last_rgb.copy())
            if not s.get("no_mouse"):
                mx, my, down, since = mouse.at(int(fnum[idx]))
                ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
                dov = ImageDraw.Draw(ov)
                for xo in ((0, W) if port is not None else (0,)):
                    draw_cursor(dov, mx + xo, my, down, since, scale)
                img = Image.alpha_composite(img.convert("RGBA"), ov).convert("RGB")
            if speed > 1 and port is None:
                ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
                ff_badge(ImageDraw.Draw(ov), img.size[0] - 14, 14 + (0 if port is not None else 30), speed, fonts[0])
                img = Image.alpha_composite(img.convert("RGBA"), ov).convert("RGB")
            rows = [np.asarray(img), band]
            if port is not None:
                bimg = Image.fromarray(band.copy()); db = ImageDraw.Draw(bimg)
                n_c, n_s = idx + 1, int(cum_same[idx])
                txt = f"presented frames compared so far: {n_c:,}   identical: {n_s:,}"
                db.text((ow - db.textlength(txt, font=fonts[2]) - 20, BAND - 26), txt, font=fonts[2],
                        fill=(150, 220, 150) if n_s == n_c else (255, 120, 120))
                lab = label
                if speed > 1:
                    li = Image.fromarray(label.copy()).convert("RGBA"); ov = Image.new("RGBA", li.size, (0, 0, 0, 0))
                    ff_badge(ImageDraw.Draw(ov), ow - 12, 4, speed, fonts[0])
                    lab = np.asarray(Image.alpha_composite(li, ov).convert("RGB"))
                rows = [lab, np.asarray(img), np.asarray(bimg)]
            enc.stdin.write(np.vstack(rows).tobytes())
            nout += 1
        # audio: speed 1 = the samples; speed > 1 = the segment's whole audio time-compressed without a pitch shift
        # (ffmpeg atempo); speed < 1 (a hold) = the segment's audio once, then silence. 30 ms fades at every cut.
        na = int(round(n * HZ / FPS))
        sa, sb = int((ta - t0) * HZ / 1000), int((tb - ta) * HZ / 1000)
        if speed > 1:
            chunk = tempo(pcm[sa:sa + sb], speed)
        else:
            chunk = pcm[sa:sa + min(na, sb)]
        chunk = chunk[:na]
        fade = min(len(chunk), int(0.03 * HZ))
        if fade:
            env = np.ones(len(chunk)); env[:fade] = np.linspace(0, 1, fade); env[-fade:] = np.linspace(1, 0, fade)
            chunk = (chunk * env[:, None]).astype(np.int16)
        if len(chunk) < na:
            chunk = np.vstack([chunk, np.zeros((na - len(chunk), 2), np.int16)])
        audio.append(chunk)
        print(f"segment {s['from']}..{s['to']} x{speed}: {n} frames", flush=True)
    enc.stdin.close(); enc.wait()
    with wave.open(tmpa, "wb") as wf:
        wf.setnchannels(2); wf.setsampwidth(2); wf.setframerate(HZ)
        wf.writeframes(np.vstack(audio).tobytes())
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmpv, "-i", tmpa, "-c:v", "copy", "-c:a", "aac", "-b:a", "96k",
                    "-movflags", "+faststart", "-shortest", out], check=True)
    os.remove(tmpv); os.remove(tmpa)
    print(f"{out}: {nout} frames, {nout / FPS:.1f} s, {os.path.getsize(out) / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
