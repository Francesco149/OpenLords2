# WinMain pre-init chain

The work `FUN_0040e9ab` does *before* `FUN_004b2258` (InitInstance)
runs: drive/cwd capture, localization-blob load, persistent-config
parse, machine-class parse.

All addresses are preferred-base (ImageBase `0x00400000`).

## Call site

`FUN_0040e9ab` runs four pre-init calls in this exact order before
handing control to InitInstance:

```c
DAT_005cd414 = __getdrive();                     // current drive number
__getcwd(&DAT_0058fe30, 100);                    // CWD (later used by .sav resolver)
FUN_004af8d9("l2.eng", &DAT_00591580, 120000, 0);// localization
FUN_004ae1b8();                                  // lords2.inf persistence load
FUN_0040ecc4();                                  // l2.ini / sierra.ini parse
FUN_004b2258(hInstance);                         // InitInstance
```

The `getdrive`/`getcwd` pair lands in two globals that the .sav
resolver and CD-relative file lookup chains read later (`FUN_004af312`
/ `FUN_004af116`).  In our drop-in we lean on `init_exe_dir`'s CWD
anchor and don't yet need the retry chain — see `src/io.h` for the
TODO.

## `FUN_0040ecc4` — l2.ini → sierra.ini parser (1099 B)

This is the function that fills six platform-config globals from an
INI file lookup, with a defaults-first then file-overrides pattern.

```
defaults
  ↓
read l2.ini from CWD
  ↓ found?
  ├─ yes → find "WindowsDir=" entry
  │         ↓ found?
  │         ├─ yes → read <WindowsDir>\sierra.ini
  │         │        ↓ found?
  │         │        ├─ yes → parse_sierra_ini, return
  │         │        └─ no  → fall through ↓
  │         └─ no  → log "Err:Could not read string WindowsDir= in l2.ini"
  │                  ↓
  │                  goto fail (DOES NOT try direct sierra.ini)
  └─ no
read sierra.ini from CWD
  ↓ found?
  ├─ yes → parse_sierra_ini, return
  └─ no  → fail: log "Err:Could not read sierra.ini", keep defaults
```

The asymmetry between "WindowsDir missing → give up" and "WindowsDir
present, sierra.ini at that path missing → fall through" reads as a
control-flow oversight in retail (the `goto LAB_0040f0e3` skips the
fallback that the "l2.ini missing" branch reaches via fall-through).
We preserve the quirk in `src/config.c` — see the comment on
`try_l2ini_to_sierra_ini` for the rationale.

### Defaults (loaded eagerly, before any file read)

| Global         | Default | Meaning                            |
|----------------|---------|------------------------------------|
| `DAT_0053f200` | `'D'`   | CD-ROM drive letter                |
| `DAT_0053f5f8` | `2`     | CD-ROM speed (×; 2× was 1996 norm) |
| `DAT_0053f5e8` | `8000`  | "PhysicalMem" — KB?  (8 MB-class)  |
| `DAT_0053f5dc` | `0x1e6` | CPU class flag (486)               |
| `DAT_0053f5e0` | `0x42`  | CPU MHz (66)                       |
| `DAT_0053f5e4` | `0`     | VideoSpeed                         |
| `DAT_0053f5f0` | `0xd`   | SoundChannels (13)                 |
| `DAT_0053f5f4` | `0`     | "fast machine" derived flag        |

### sierra.ini keys

The parser uses a fixed pattern for each key:

```c
off = find_substr(scratch, "KEY", 20000);
eq  = find_char(scratch + off, '=', 20000);
off = off + eq;            // points to first char past '='
if (off > 0) { ...consume scratch[off]... }
```

| Key             | Parse                                           | Global             |
|-----------------|-------------------------------------------------|--------------------|
| `CDRomDrives`   | single byte at `scratch[off]`                   | `DAT_0053f200`     |
| `CDROM`         | `atoi(scratch + off)`                           | `DAT_0053f5f8`     |
| `SoundChannels` | `atoi(scratch + off)`                           | `_DAT_0053f5f0`    |
| `PhysicalMem`   | `atoi(scratch + off)`                           | `_DAT_0053f5e8`    |
| `VideoSpeed`    | `atoi(scratch + off)`                           | `_DAT_0053f5e4`    |
| `CPU`           | first char picks class, then `atoi` after `-`   | `_DAT_0053f5dc` + `DAT_0053f5e0` |

`CPU` is the odd one out: it's a structured value like
`Pentium-90` or `486-66`.  First char of the value field decides
`cpu_class`:

```c
char c = scratch[off];
if (c == '>' || c == 'p' || c == 'P') cpu_class = 0x24a;  // Pentium
else                                  cpu_class = 0x1e6;  // 486
```

Then a forward scan for `-` (max 20 bytes), and `atoi` the byte
after the dash gives `cpu_mhz`.

### Derived flag

```c
if (CDROM > 3 && cpu_mhz > 70) fast_machine = 1;
```

So a 4× CD-ROM and a faster-than-66 MHz CPU promotes the box to
"fast machine".  Pentium-90 / Pentium-100 systems with 4× CD-ROMs
(common late-1996 hardware) would set this; the original Pentium-66
release machines would not.  No present consumer in our drop-in;
likely gates the Smacker "high quality" vs "low quality" stream
selection (cf. the `_smk_low` / `_smk_high` extension steering in
`FUN_004af312`).

## `FUN_004ae1b8` — lords2.inf persistence load (deferred)

```c
FUN_004aecf0("*.sav", 0);          // scan saves directory?
__open("lords2.inf", 0x8404);      // O_RDONLY|O_BINARY, fileno
__read(fd, &DAT_0053f1e0, 0x468);  // 1128 bytes
__close(fd);
FUN_004ae2bd();                    // process loaded block
FUN_004ae2ab();                    // ?
```

Loads a 1128-byte binary blob (player profile?  saved-game index?)
into `DAT_0053f1e0+`.  The consumer chain (`FUN_004ae2bd`,
`FUN_004ae2ab`, `FUN_004ae310`, `FUN_004aecf0`) hasn't been
decoded.  Loading the bytes without a consumer would be write-only
state, so this is **deferred** — port lands when the consumer
chain is.

## `l2.eng` — localization blob

Single file read of up to 120 KB into `DAT_00591580+`.  No
structural decoding visible at the load site — the buffer is held
raw and consumed by string-table lookups distributed across the UI
code.

In the real game-dir, `L2.eng` is 100016 bytes; smaller localized
files (`BATTLES.ENG`, `TROOPS.ENG`, etc.) sit alongside but are
*not* loaded here — they're loaded on-demand by their respective
UI screens.

## String-helper family

`FUN_0040ecc4` is one of the heaviest consumers of the engine's
small string-utility quartet:

| Address       | Size | Role                                     | Ported as              |
|---------------|------|------------------------------------------|------------------------|
| `FUN_00401000`| 133 B| bounded substring search (0-based)       | `strutil_find_substr`  |
| `FUN_00401085`|  85 B| bounded character search (1-based)       | `strutil_find_char`    |
| `FUN_00401136`|  64 B| bounded byte copy (no null terminator)   | `strutil_bounded_copy` |
| `FUN_004012a2`|  97 B| strip control / 8-bit-set bytes          | `strutil_strip_ctrl`   |
| `FUN_004010da`|  92 B| bounded memcmp (used only by 401000)     | inlined in find_substr |
| `FUN_004c2090`| 224 B| MSVC `strcat` (CRT, not engine code)     | libc `strcat`          |

`find_substr`'s `0 = not found OR found at offset 0` ambiguity is
preserved verbatim.  All retail callsites work around it by knowing
that real .ini key matches sit at offsets > 0 (`.ini` files start
with `[Section]` headers or blank lines, never directly with a key
name).

`find_char`'s 1-based return convention is the safer of the two —
distinguishes "not found" (0) from "found at offset 0" (1).

`strip_ctrl` looks for the first byte outside `[0x20, 0xa6]` and
null-terminates there.  Effective for stripping trailing CR/LF and
8-bit junk from .ini value lines.

## Port mapping

| Retail            | OpenLords2                       |
|-------------------|----------------------------------|
| `FUN_00401000`    | `src/strutil.c` `strutil_find_substr` |
| `FUN_00401085`    | `src/strutil.c` `strutil_find_char`   |
| `FUN_00401136`    | `src/strutil.c` `strutil_bounded_copy`|
| `FUN_004012a2`    | `src/strutil.c` `strutil_strip_ctrl`  |
| `FUN_0040ecc4`    | `src/config.c`  `config_init` (+ parse_sierra_ini, try_l2ini_to_sierra_ini) |
| l2.eng load       | `src/config.c`  `load_l2_eng`         |
| `FUN_004ae1b8`    | **deferred** — consumer chain not yet decoded |

Wired into the drop-in's WinMain right after `init_exe_dir` so the
CWD is anchored to the game-dir before any file lookups run.

> 📍 Sources: `docs/decompiled/by-address/{40e9ab,40ecc4,4ae1b8,
> 401000,401085,401136,4012a2,4010da}.c`, Ghidra `FUN_0040ecc4` /
> `FUN_004ae1b8` / `FUN_0040e9ab`.
