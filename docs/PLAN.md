# OpenLords2 — Project Plan

> Living document. Update as decisions change. Linked from `PROGRESS.md`.
> Last revised: 2026-05-24.

## 1. Goal

Produce a **drop-in replacement** for `Lords2.exe` (the main game
executable of *Lords of the Realm II*, Impressions Games / Sierra
On-Line, 1996) that is **behaviorally indistinguishable** from the
original for a user who owns a legitimate copy of the game.

Educational reverse-engineering + game preservation.  Not byte-
identical (1996 MSVC 4.20 codegen would be a goose chase — see §3).
Not redistributed.  No piracy enablement.

## 2. Hard constraints

1. **Never** redistribute game assets.  The vendor symlink to the
   user's GOG install, extracted asset bytes, decompiled C, golden
   screenshots produced from the original — all live under `vendor/`
   or sibling gitignored paths.
2. The repo ships only: source code, format specifications (text),
   test harness, build tooling, documentation.  Anyone cloning the
   repo gets nothing they couldn't write themselves.
3. **Behavioural fidelity > byte fidelity.**  Save-file compatibility
   is required; pixel-perfect rendering is the default goal but tiny
   GDI/DDraw rasterization deltas are tolerated.
4. **Faithfully reproduce engine quirks** — load-bearing bugs,
   hand-rolled hash variants, weird off-by-ones — rather than
   "fixing" them in our port.  Document each one in
   `docs/findings/engine-quirks.md`.

## 3. Tech stack (decided 2026-05-24)

| concern              | choice                                              |
|----------------------|-----------------------------------------------------|
| language             | C (C11)                                             |
| build toolchain      | mingw-w64 i686 cross compiler (32-bit Win32)        |
| graphics             | DirectDraw (engine uses `DDRAW.dll`)                |
| audio                | DirectSound (`DSOUND.dll`) + `WINMM.dll`            |
| video                | Smacker (`smackw32.DLL` / `Smackw2.dll`)            |
| multiplayer          | DirectPlay (`DPLAYX.dll`) — TBD                     |
| input                | Win32 messages + DirectInput (TBD)                  |
| portability          | phase 1: Win32 only; phase 2: abstract backend      |
| license              | MIT                                                 |

Why C over C++/Rust/Zig: matches Ghidra's decompiler output verbatim,
minimal translation friction.  We can always uplift later.

Why we **don't** chase byte-equivalence: the original was built with
the 1996-era MSVC 4 toolchain.  Link order, MERGE/ALIGN flags, and
PDB-stripping pass would diverge unavoidably.  The drop-in test is
"does the GOG install boot and play identically when this binary is
in Lords2.exe's place".

Why DirectDraw directly rather than wrapping into a modern API: the
original calls DDraw.  Wrapping DDraw-for-DDraw makes the test
harness simple — same lock path, same surface format, simple frame
diffs.  SDL2 / DXGI / etc. would force rasterization differences
from day one.  Phase 6 abstracts this.

## 4. Target binary — what we know

The original `Lords2.exe` (GOG, patch 2.0.0.3):

- 32-bit PE, Machine 0x014C, Subsystem 0x0002 (Windows GUI).
- 6 sections; preferred ImageBase `0x00400000`;
  `SizeOfImage 0x001E9000` (~1.9 MB); `SizeOfHeaders 0x400`.
- Entry point RVA `0x000C3370`.
- **No DRM packer.** Clean MSVC linker 4.20 PE.
- Imports: `KERNEL32`, `USER32`, `GDI32`, `DDRAW`, `smackw32`,
  `DSOUND`, `WINMM`, `DPLAYX`.
- 1.03 MB on disk → Ghidra import is fast (under 5 minutes for full
  auto-analysis on a modern host).

Companion binaries:

- `mapl2.exe` (253 KB) — map editor.  Imports `comdlg32` for file
  dialogs; otherwise the same DDraw/WinMM stack.  Not in scope for
  the drop-in but a useful side artifact for understanding the map
  format.
- `Smackw32.dll`, `Smackw2.dll` — Smacker video runtime (RAD Game
  Tools).  Closed-source.  We use these as-is; no plans to
  reimplement Smacker decode.
- `goggame-1207663263.dll` — GOG telemetry / Galaxy hook.  Not
  loaded by the game itself (it's a GOG runtime artifact); ignore.
- `GameuxInstallHelper.dll` — Windows Game Explorer integration.
  Not load-bearing.

## 5. Phased roadmap

1. **Bootstrap (this phase).**  Flake, setup script, launcher,
   Frida agent (hidden window + turbo + mute + msgbox capture),
   drop-in exe stub that opens a hidden window.  Smoke test the
   retail exe under the harness without disrupting the host.
2. **WinMain + main loop.**  Port the entry point, message pump,
   frame limiter.  Drop-in boots and exits cleanly.
3. **DDraw bring-up.**  Surface creation, mode-set (windowed only
   for the harness), per-frame Blt.  First pixels.
4. **Game-state init.**  Parse `L2.INI`, load `Castles.dat`,
   `C_hist.dat`, `History.dat`, `L2_maps.dat`.
5. **Renderer.**  PL8 sprite decoder, tile renderer, UI layer.
6. **Simulation.**  Turn loop, economy, AI, combat resolution.
7. **Audio + video.**  DirectSound bring-up, Smacker playback.
8. **Multiplayer.**  DirectPlay session setup (educational —
   probably won't ship working multiplayer).
9. **Cross-platform port.**  Phase 1 ends here; phase 2 lifts the
   renderer/audio/input behind a backend abstraction.

## 6. Workflow

> **2026-10-23:** superseded by the rhost methodology — `docs/audits/2026-10-23-methodology-reset.md`,
> `CLAUDE.md`. The text below is the original (Frida-era) plan.


See [`docs/AGENT-WORKFLOW.md`](AGENT-WORKFLOW.md) for the
day-to-day RE conventions: commits, findings docs, engine-quirks log,
how to use the Frida harness, when to use Ghidra vs r2.

## 7. Subagent policy

> **2026-10-23:** superseded — delegate mechanical / search work to subagents; keep RE interpretation
> and parity adjudication (openrecet policy, `CLAUDE.md` Conventions).


**Subagents are forbidden by default.**  RE work always involves
complex code analysis — even things that look like simple mechanical
work usually turn into rabbit holes.  Inline everything.  The only
carve-out is provably mechanical batch tasks (rename N symbols across
M files, regenerate a static table).
