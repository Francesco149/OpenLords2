# In-process retail host (rhost) for Lords2 — design notes and measurements

Usage: `tools/rhost/README.md`. Why: `docs/audits/2026-10-23-methodology-reset.md`. Lineage: openrecet
`tools/rhost` (D3D8, 2026-10-13) → OpenSummoners (DirectDraw7, 2026-10-14) → here.

## Lords2's runtime surface (what had to be virtualized)

Imports (GOG 2.0.0.3): KERNEL32 (CRT, files, heap, threads, events), USER32 (window, messages, cursor,
dialogs), GDI32 (5 calls: GetPaletteEntries, SetSystemPaletteUse, GetStockObject, PatBlt, GetDeviceCaps),
DDRAW (DirectDrawCreate), DSOUND (DirectSoundCreate), WINMM (timeGetTime, timeSetEvent, mmio*),
smackw32 (10 ordinals), DPLAYX (ordinals 1, 2). No registry, no DirectInput, no QPC.

- **Input** is window messages only (WndProc FUN_004b29be) + `GetCursorPos` in FUN_004b191e.
- **Frame** = FUN_004b99c0 (sim + render driver), called once per main-loop iteration with an empty
  queue (FUN_0040e890 ← FUN_0040e8bb ← loop @0x40eb4c). One call site.
- **Clocks**: `timeGetTime` (sim rate controller FUN_004bb978, FPS meter, UI anims, the GOG pacer
  FUN_004cfb08), `GetTickCount` (DPlay pump, MP), `time()` for log stamps.
- **Threads**: `CreateThread` only for SierraNW (online MP). The music streamer FUN_00426dd5 runs on
  winmm's multimedia-timer thread (`timeSetEvent(480 ms)`, FUN_00426b92) — rhost fires it on the main
  thread at frame boundaries.
- **Display path**: desktop not 8 bpp ⇒ retail promotes itself to fullscreen (`DAT_0053f228 = 1`,
  SetCooperativeLevel 0x13, SetDisplayMode 640×480×8). rhost reports a 32 bpp desktop, so retail takes
  this — every GOG user's — path; the fake DirectDraw makes the mode set a no-op.

## Findings made while building it

- **GOG patch code past `.text` VirtualSize.** `.text` vsize 0xceb06, rawsize 0xcec00; the patch put a
  detour at 0x4cfb08..0x4cfb48: 0x4050f0 (inside FUN_004050c0, the per-tick animation bump) `jmp`s there,
  the stub gates `DAT_0057d378++` on `GetTickCount() - DAT_005ceff8 >= 0x10` (a 16 ms animation pacer; IAT
  slot 0x5cf3d8, read twice) and `jmp`s back to 0x4050f6. A loader that copies only VirtualSize bytes runs
  into zeros on the first in-game frame (AV f1321); Windows maps the whole last page. The IDA export keeps
  the stub as a chunk of 0x4050c0 (tools/ida/README.md).
- **smackw32.dll is a Watcom PE**: every section's VirtualSize is 0 and `.bss` has a raw size with no raw
  data (PointerToRawData 0) — a mapper must zero-fill it (copying "from offset 0" puts the DOS header there:
  its `free()` then walks a garbage heap list). Its preferred base is 0x400000 (= retail's), so it is always
  relocated. Exports by ordinal; Lords2 imports 10 (SmackOpen 14, SoundOnOff 17, Close 18, DoFrame 19,
  NextFrame 21, ToBuffer 23, Goto 27, ToBufferRect 28, Wait 32, SetDirectSoundHWND 37).
- **Smacker plays the soundtrack through waveOut with CALLBACK_NULL and paces video by polling WHDR_DONE**
  (no timer, no DirectSound buffer): a waveOut fake that completes buffers instantly — or never — makes the
  intro crawl at ~1/6 speed; completing each buffer when the virtual clock has played its bytes gives the
  movie's own 12 fps. Retail's movie step (FUN_0042dbc7) returns early while SmackWait (ordinal 32) says wait.
- **The boot sequence with movies**: intro.smk (1,578 frames, 2:15), imptitle.smk, credits.smk, each to a
  "natural end" in status.txt, then the title at ~frame 11,000. In play: jail.smk (the lord captured);
  the victory/defeat screen 0x1c stays black and waits for a click when its movie is skipped.
- **Retail tolerates a failed SmackOpen**: FUN_0042da18 logs `ERR:SMK Could not open intro.smk` and
  returns 0; the game continues to the title. rhost skips every movie this way.
- **Report scrolls dismiss with RMB, the "Non 256 colour display" scroll appears** on any non-8 bpp
  desktop (retail's own warning, authentic for GOG users).
- Retail writes `status.txt` (its log), `castles.dat` and `lastturn.sav` into the game dir at game start —
  the overlay puts them in the run dir.

## Measurements (2026-10-23, Win11 native via ssh session 0 / wine 11.19 null driver)

| | Windows | wine |
|---|---|---|
| boot → title, 2000 frames | 33k fps (det runs) | 8–11k fps |
| in game, `--fb-hash` | ~2–3k fps | ~2–3k fps |
| `det boot -n 3` | 0 differing dwords | 0 differing dwords |
| state_sem after boot f2000 | fca73acf31662bc3 | fca73acf31662bc3 |
| corpus (3 scenarios) | = wine (state_sem, RNG, fb chain) | = Windows |
| batch round trip (1 run) | ~3 s | ~1 s |

The state is retail `.data/.bss` (0x4d2000..0x5cee60) + the heap arena (~4.2 MB in game). No
`volatile.txt` entries are needed: the fake HWND / DirectDraw / DirectSound / handles leave no
system-DLL pointer in retail memory.
