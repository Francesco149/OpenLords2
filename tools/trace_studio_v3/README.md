# Trace Studio v3 — native trace viewer

The OpenLords2 render/state parity **workhorse**: drive the port and retail
through one trace, reconstruct any frame on either side in well under a
millisecond, and scrub the two side-by-side with a live pixel + index + palette +
engine-state diff. Modeled on the openrecet sibling's `trace_studio_v3`, adapted
to what Lords of the Realm II is — an **8bpp / 640×480 DirectDraw** game.

It replaces the old web trace studio (`tools/trace_studio` + `trace_studio_web`):
no PNG baking, no browser, no `:8780` server. The two `.olv3` containers are the
only artifacts; the native viewer reconstructs frames on demand.

## Why this shape (vs. openrecet)

openrecet is D3D8: every sprite is a `DrawPrimitive`, so it captures the D3D
command stream and replays it into a real d3d8 device (~3 ms/frame). Lords 2
software-composites every sprite into **one** back-surface and presents it with a
single `Blt` — retail's per-sprite draws are invisible at the DDraw boundary. So
the reconstructable unit here is the back-surface itself: an **8-bit index plane +
the 256-entry palette**. Reconstructing a frame is a palette LUT expansion (index →
BGRA), **sub-millisecond, pure CPU** — no GPU, no replay device, no DWM fragility.

Both engines already produce this exact unit at the present seam (the retail Frida
agent's `captureFrameOnce`; the port's locked 8bpp canvas), so one container format
captures both sides bit-identically.

## Diffs it shows (richer than RGB-only)

- **INDEX diff** — the raw 8bpp planes (what the *blitter* wrote). An index
  mismatch means the compositor diverged.
- **palette diff** — matching indices but a different palette colour means the
  *palette builder* diverged (e.g. `FUN_0042f306` vs `FUN_0042f449`).
- **RGB diff** — the palette-expanded frames (what the eye sees), amplified ×6.
- **engine state** — the save-schema slot snapshot, port vs retail, identity-keyed.

## Layout

```
format/olv3_format.h   container format + the index→BGRA expansion (shared C header)
replay/replay_core.c   open a container once, reconstruct any frame on demand
replay/replay.c        headless CLI: summary / render a frame to .bmp (host-native too)
viewer/viewer.cpp      Dear ImGui + D3D9 viewer (cross-built i686 mingw, runs on Windows)
olv3.py                container reader/writer + synthetic generator (Python)
viewbuild.py           view.json builder (the port↔retail identity join)
olv3_capture.py        orchestrator: drive both engines → containers → view → launch
test_olv3.py           format + replay-core regression gate (C render == Python, bit-exact)
```

## Build

```
nix develop -c make -C tools/trace_studio_v3/viewer    # viewer.exe (ImGui, ~30 s first time)
nix develop -c make -C tools/trace_studio_v3/replay     # host replay CLI (for tests/headless)
```

## Use

```
# drive both engines through a trace, build containers + view.json, launch the viewer
nix develop -c python3 tools/trace_studio_v3/olv3_capture.py --trace grid_load_dense

# re-run only the port (keep the cached retail side), don't relaunch
nix develop -c python3 tools/trace_studio_v3/olv3_capture.py --trace grid_load_dense --only port --no-view

# ingest already-captured raws into containers + view without re-driving
nix develop -c python3 tools/trace_studio_v3/olv3_capture.py --session grid_dense --no-capture
```

The viewer (a Windows process via WSLInterop) takes a `view.json`:

```
viewer.exe <view.json>                 interactive 3-panel scrub
viewer.exe --shot out.bmp --col N <view.json>   headless one-frame render (self-verify / feed)
```

Keys: `,`/`.` ±1 · arrows ±10 · Home/End · `1`/`2`/`3` panels · `i` index/rgb diff ·
`w` worst · `n` next divergence · `m` note mode.

## Notes loop

In note mode, drag a box on a panel and type a note to flag a divergence; notes
persist to the view's `notes_path` (a Windows-local JSON the viewer can write). The
agent reads the same file to pick up flagged regions.

## Capture format

A trace's `{caprange:[START,COUNT], label:"grp"}` op compiles (via the kept
`trace_studio.model.ops`) to one `{capture}` per frame, so the engines capture a
contiguous window anchored to a semantic event — the load-stretch between port and
retail cancels because both capture at `anchor+offset`. Each captured frame is the
raw payload `[u32 w][u32 h][u32 present][256×(R,G,B,flags)][w*h 8bpp]`:
- retail: emitted by the agent, saved as `frame_NNNNN.raw` by `frida_capture.py`;
- port: `gfx_capture_backbuffer_raw` under `--capture-raw`, named `<label>.raw`.
