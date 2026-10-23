# OpenLords2

An open-source reimplementation of **Lords of the Realm II**
(Impressions Games / Sierra On-Line, 1996).

This is an **educational reverse-engineering and game-preservation
project**.  The goal is a drop-in replacement for `Lords2.exe` that
behaves indistinguishably from the original for a user who owns a
legitimate copy of the game.

**Not distributed:** no game assets, no decompiled binary code, no
copyrighted content of any kind.  You supply your own copy of the game
(GOG, retail CD, …).

## Status

See [`docs/STATUS.md`](docs/STATUS.md) for a 60-second orientation
(port coverage %, current phase, top blocker — derived, never stale),
[`docs/PLAN.md`](docs/PLAN.md) for the roadmap,
[`docs/PROGRESS.md`](docs/PROGRESS.md) for the dated changelog, and
[`docs/port-ledger.md`](docs/port-ledger.md) for per-function port
status.

## Getting started (NixOS / WSL2)

```fish
nix develop
./tools/setup.sh        # symlinks game into vendor/, prepares analysis dirs
```

This enters a dev shell with the full RE toolchain (Ghidra, radare2,
mingw-w64 32-bit cross-compiler, Frida, Pillow/numpy/scikit-image, …)
and prepares the game files for analysis.

The game's `Lords2.exe` (GOG release, sha256 checked at setup time) is
a **clean 32-bit MSVC PE** (linker 4.20, 1996 era) — no DRM wrapper,
no Steamless/UPX/etc. — so the decompile pipeline runs against the
original bytes directly.  Imports include `DDRAW.dll` (DirectDraw
rendering), `DSOUND.dll` (DirectSound), `DPLAYX.dll` (DirectPlay
multiplayer), `WINMM.dll`, and `smackw32.DLL` (Smacker video for
cutscenes).

## Layout

```
docs/         design notes, file-format specs, progress log, engine quirks
src/          our C reimplementation (cross-compiled with mingw32)
tests/        unit suite + scenario harness (golden BMP + audio/input traces)
tools/        setup, build, extract, capture, contact-sheet, ghidra-headless,
              Frida agent + driver, launcher wrapper
vendor/       game files (gitignored) — symlink into the user's GOG install
ghidra/       Ghidra projects (gitignored — derived from the original binary)
runs/         test artifacts (gitignored)
```

## Workflow (methodology reset 2026-10-23)

Retail runs **inside our own process** — `tools/rhost/` ("rhost"): the user's `Lords2.exe` is mapped
at its base address and run against a software DirectDraw / DirectSound / window layer, a virtual
clock and a virtual message queue fed by an input trace. Runs are deterministic (identical state on
every run, and on Windows and wine alike), headless (never fullscreen, no display needed) and fast
(thousands of frames per second). Port functions are verified against retail **on identical state**:
shadow execution per call, and hybrid runs where the port replaces retail's function.

- `python3 tools/rhost/rhost.py corpus --check` — every scenario in `tests/scenarios/` vs its blessed hashes.
- `python3 tools/rhost/rhost.py verify` — every ported-function hook shadowed over the corpus.
- `python3 tools/rhost/rhost.py record NAME` — play on the Windows desktop; the input becomes a scenario.
- `python3 tools/rhost/rhbot.py "run 600; click 320 107; …"` — drive a live run from a script.
- `tools/bot/` — an LLM (TypeSafe's Jev decision model) plays the strategy game through rhost to
  generate scenarios.

See [`docs/audits/2026-10-23-methodology-reset.md`](docs/audits/2026-10-23-methodology-reset.md) and
[`tools/rhost/README.md`](tools/rhost/README.md). The earlier Frida-based harness is kept as legacy.

## Tests

Pure-C parsers and decoders carry unit tests under host gcc +
ASan/UBSan.  Win32-surface modules (gfx, audio, main) are smoke-
tested in-process via `tools/run-openlords2.sh`.

```fish
nix develop
make -C tests run          # unit tests
make -C src                # cross-compile drop-in (mingw32)
tools/run-openlords2.sh --frames 50
```

Shape mirrored from `/opt/src/openrecet/tests/` — single statically
linked test binary, X-macro test registry in `tests/test_main.c`,
tiny assertion macros in `tests/t.h`.

## Sibling projects

OpenLords2 shares conventions with two other RE projects living in
`/opt/src/`:

- **[openrecet]** — the first decomp project; established the
  Ghidra-headless / Frida-harness / co-author-trailer commits style.
- **[OpenMare]** — newer, refined workflow; the launcher Job-Object
  pattern, single-TU C builds, MessageBox redirect hooks, and turbo
  v2 virtual clock all originate there.

Cross-pollinate freely; never copy code across projects.

## License

MIT.  See [`LICENSE`](LICENSE).  The license covers OpenLords2's own
code only; no rights are granted to the original game.
