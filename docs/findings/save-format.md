# Save-file format — first map

Reverse-engineering of Lords2.exe's persistence layer.  Reconstructed
from the four read/write paths in `0x4ade93..0x4aea70` and a global-
serializer table at `0x4de960` (the "schema").  Castles.dat,
.sav files, and the in-memory state arrays all share a single
underlying schema; mapping it once unlocks everything downstream.

## TL;DR

```
.sav file (471828 bytes total)
├── 267028 bytes : serialized globals  (225 entries from schema table)
└── 204800 bytes : 16 × 12800 B castle blocks (verbatim Castles.dat copy)
```

The 267028 + 204800 split is exact; no headers, no separators, no
versioning.  The "schema" is hard-coded in the binary as a
`(void *ptr, u32 size)` table at `0x4de960`, walked until size==0 or
225 entries consumed.  `Castles.dat` is the on-disk version of one
specific entry (the castle-state mega-array) that the engine also
keeps stand-alone for the 16-scenario campaign cache.

## The schema table — `DAT_004de960`

225 entries × 8 bytes each (`ptr u32, size u32`).  Read/written
top-to-bottom in `FUN_004ade93` (save) and `FUN_004adfe6` (load) — no
field tagging.  The save format is **strictly positional**; reordering
the table would invalidate every existing save.

Total size of all entries: **267028 bytes**, matching `lastturn.sav -
204800` precisely.

### Major blocks (entries 0..12 — 99 % of byte volume)

Cross-referenced against the per-tick hash function `FUN_00440231`
which iterates these arrays at known strides, confirming the per-
element size and the gameplay-mode they belong to.

| # | VA          | bytes  | stride | slots | meaning (working theory)         |
|---|-------------|-------:|-------:|------:|----------------------------------|
| 0 | `0x522f90`  | 32768  | 2      | 16384 | overworld map (probably 128×128 hex tile_type+flags pairs) |
| 1 | `0x5440e0`  | 51200  | 8      | 6400  | **castles state** — current scenario block of Castles.dat |
| 2 | `0x553d50`  |   264  | 44     | 6     | per-player state (44 B × 6 players) |
| 3 | `0x57bf00`  |  2112  | 352    | 6     | per-player state (352 B × 6 players); strategic mode |
| 4 | `0x53f9b0`  | 13056  | 768    | 17    | per-army? per-scenario-cache?  (16 used slots + slot 0) |
| 5 | `0x52f0b0`  | 63420  | 420    | 151   | **per-territory** state (150 used + slot 0); field +0 = owner |
| 6 | `0x568dc0`  |   260  | 20     | 13    | 13 × 20 B records (unknown — possibly unit/army types) |
| 7 | `0x554480`  | 34992  | 432    | 81    | per-battle-hex state (80 used + slot 0); **combat mode** |
| 8 | `0x57a100`  |  7676  | 76     | 101   | per-combat-unit (100 used + slot 0); **combat mode** |
| 9 | `0x566520`  |  4212  | 52     | 81    | per-battle-hex aux (80 used + slot 0); **combat mode** |
|10 | `0x56d8c0`  | 51200  | 8      | 6400  | parallel to entry 1 — possibly previous-turn or AI shadow state |
|11 | `0x568480`  |   128  | -      | -     | small fixed buffer (unknown) |
|12 | `0x53ea00`  |  1360  | -      | -     | small fixed buffer (unknown) |

Sub-total entries 0..12: **262648 bytes** = 98.4 % of the global block.

Entry 5 / 7 / 8 / 9 all share the "slot 0 unused, slot 1..N used"
convention.  The per-tick hash loop in `FUN_00440231` confirms this
visually:

```c
// Strategic-mode hash (DAT_0057c8fc == 0)
for (i = 1; i < 0x97; i++)  // 1..150  → 150 territory records
    FUN_0044015a(&DAT_0052f0b0 + i * 0x1a4, 0x1a4, 0);

// Combat-mode hash (DAT_0057c8fc == 2)
for (i = 1; i < 0x51; i++)  // 1..80   → 80 battle-hex records
    FUN_0044015a(&DAT_00554480 + i * 0x1b0, 0x1b0, 0x12);
```

`DAT_0057c8fc` is the **engine gameplay mode** discriminator (0 = strategic
overworld, 2 = tactical combat).  Both branches share the rest of the
schema, so save files written from either mode produce
byte-identical-format records.

### Trailing slots (entries 13..215 — small scalars)

Mostly 4-byte scalar globals — turn number, current player index,
cursor position, RNG state, etc.  A handful of 192/240/640-byte
medium tables sprinkled in.  Full table dump in `docs/findings/save-
schema-table.csv` (regenerable; see "Reproduction" below).

### Reserved/sentinel slots (entries 216..224)

The last 9 entries all point to `DAT_004eb2ac` (a 4-byte scratch
region).  On save they emit whatever's in that scratch; on load they
overwrite the scratch nine times before discarding.  These appear to
be **forward-compatibility reserved slots** — a planned-but-unused
mechanism for adding fields without bumping a save version.  Entry
87 (`0x4eafb0`) is similar.

In our drop-in we'll preserve the layout: write the same 36-40
"zero" bytes so retail can still load our saves.

## Castles.dat — 16 × 12800 B scenario cache

```
Castles.dat (204800 bytes = 0x32000)
├── block 1 : 12800 B  ─┐
├── block 2 : 12800 B   ├── one block per campaign scenario (1..16)
├── ...                 │
└── block 16: 12800 B  ─┘
```

Five functions touch it (all in the 0x4ad000..0x4aef00 range):

| addr      | role | summary |
|-----------|------|---------|
| `0x4ade93`| `savegame_save(path)`        | write 225-entry globals, then copy all 16 blocks from Castles.dat to .sav |
| `0x4adfe6`| `savegame_load(path)`        | read 225-entry globals from .sav, then copy all 16 blocks from .sav to Castles.dat |
| `0x4ae8a9`| `castles_init_blank()`       | zero-fill all 16 blocks of Castles.dat |
| `0x4ae968`| `castles_write_scenario(n)`  | lseek to `(n-1) * 0x3200`, write current in-memory castle table |
| `0x4aea70`| `castles_read_scenario(n)`   | lseek to `(n-1) * 0x3200`, read into in-memory castle table |

In-memory unpack (in `castles_read_scenario`): the 12800-byte block is
read into a scratch buffer at `DAT_004eb2b0`, then for each 2-byte
on-disk pair, one 8-byte in-memory record is populated:

```c
for (i = 0; i < 0x3200; i += 2) {                  // 6400 disk records
    DAT_005440e3[c] = scratch[i];                  // mem[+3] = disk byte 0
    DAT_005440e1[c] = scratch[i+1];                // mem[+1] = disk byte 1
    // ... further decoding using DAT_005440e7 (already populated
    //     elsewhere — probably the building/improvement type field)
    c += 8;                                        // advance to next in-memory record
}
```

Pack direction (in `castles_write_scenario`) is the mirror image: only
two bytes of each 8-byte record survive to disk — bytes at offset
`+1` (flags) and `+3` (some derived state).  The other six bytes of
the in-memory struct are runtime-computed.

### Per-record on-disk layout (2 bytes)

| offset | type | meaning (provisional) |
|--------|------|------------------------|
| +0     | u8   | record byte 0 — copied to in-memory `+3` |
| +1     | u8   | flags byte — copied to in-memory `+1`. Bits: `0x01` = ?, `0x02` = "reset to state 9", `0x80` = ? (see castles_read_scenario tail logic) |

### Per-record in-memory layout (8 bytes — `DAT_005440e0`)

Heavily-used in the simulation (378 byte-offset references across
`all.c`).  Updated after a full sweep of writers per offset; the
table below replaces the earlier provisional one.

| offset | type | persistent? | meaning |
|--------|------|:-:|---------|
| +0     | u8   | no  | **terrain / improvement type** (small enum; see values below) |
| +1     | u8   | YES | **persistent flags**: bit `0x10` = "tile is part of a building/road footprint"; bits `0x04`, `0x08`, `0x20`, `0x40`, `0x80` also tested/set; `(flags & 0x60) == 0` tested as a combined gate |
| +2     | u8   | no  | **runtime flags**: zeroed/reset at footprint expansion (`& 0xe3` clears bits 2,3,4); bit `0x04` set on every footprint cell, `0x80` set on the building's center cell only; bit `0x01` set by certain combat-mode updates |
| +3     | u8   | YES | **display char / disk byte**: on disk this is the raw scenario byte (anchor type char `'@'/'C'/'X'/'['` for buildings, or `0x50..0x5F` for roads).  At runtime, footprint expansion overwrites the 8 non-anchor cells with `'A'..'R'` chars from the table `s__ABHIJPQR_004d9dd0` (9-char/orientation × 4 orientations).  Roads keep `disk_byte + 0x2c` |
| +4     | u8   | no  | **build / improvement stage**: small enum 0..15.  Set to 1 on the center cell by `FUN_004921e5`.  Drives the +7 transition table below.  Lookups via `(&DAT_004d7d80)[bVar1 * 2]` / `(&DAT_004d7b80)[bVar1 * 2]` (2-byte-stride per-orientation tables) |
| +5     | u8   | no  | **back-pointer to per-castle slot** (0..16).  Written via `(&DAT_005440e5)[*(int *)(&DAT_0055449c + slot * 0x1b0)] = slot;` — each per-castle struct (entry 6, 432-byte stride at `0x554480`, slot record `0x55449c` is `0x554480 + 0x1c`) stores a *tile byte offset* at +0x1c, and the back-pointer at the tile points to which castle owns it.  Cleared (`= 0`) when slot frees |
| +6     | u8   | no  | **back-pointer to combat-unit index**.  Written via `(&DAT_005440e6)[*(int *)(&DAT_0057a11c + unit * 0x4c)] = unit;` — per-combat-unit struct (entry 8, 76-byte stride at `0x57a100`, slot `0x57a11c` is `+0x1c`) stores a tile byte offset.  Only 3 references — combat-mode only |
| +7     | u8   | no  | **runtime visual stage / sprite frame**: small enum 0..15 driven by transitions on +4 (e.g. `+4==7 → +7=7`, `+4==8 → +7=8`, `+4==9 → +7=0xb`, `+4==10 → +7=0xe`).  Also read with neighbor offset `+ *(int *)(&DAT_004d8588 + idx * 4)` — autotiling against neighboring tiles' +7 |

#### +0 terrain/improvement enum (decoded from the scenario-byte translation table at `FUN_0048fe??` near `61153`)

| scenario byte | +0 value | likely meaning |
|--------------:|---------:|----------------|
| 0             | 1        | grass / empty plain |
| 2             | 4        | (terrain) |
| 0x10          | 7        | (terrain) |
| 0x12          | 8        | (terrain) |
| 0x14          | 9        | (terrain) |
| 9             | 0xb      | (terrain) |
| 4             | 0x14     | (terrain — possibly forest) |
| 0xf           | 0x1e     | (terrain) |
| 7             | 0x28     | (terrain) |
| 8             | 0x29     | (terrain) |
| 10            | 0xc      | (terrain) |
| 0x15          | 0xd      | (terrain) |
| 0x20..0x3F    | 3        | **building anchor** — `+3 = scenario byte`, `+1 |= 0x10` |
| 0x50..0x5F    | 6        | **road segment** — `+3 = scenario byte + 0x2c`, `+1 |= 0x10` |
| 0x40..0x4F    | pass-thru| stored verbatim — these are the footprint cells (`'@'..'O'`) the scenario file uses for pre-placed 3x3 patterns |
| other         | (handled by separate branches not shown) | |

The `+0 == 3` building-anchor cell is what `FUN_004917dd` (the unported
castle-load fixup) reads from `+3` to choose a 3x3 footprint orientation
(`'@'` → orient 0, `'C'` → 2, `'X'` → 4, `'['` → 6, others → no-op).

### Why 6400 records?

The overworld is **not** 6400 tiles — Lords II runs on a ~151
territory map (cross-confirmed: entry 5 has 151 slots).  So
`DAT_005440e0` is a finer-grained sub-grid.  The 80×80 read confirmed:
the scenario-byte translation function iterates `DAT_00569570` (x) ×
`DAT_00569578` (y) both `< 0x50` (80), so the grid is **80 × 80 =
6400** with stride 8 bytes per cell and row stride 640.  Neighbor
addressing uses `(y*80 + x) * 8` throughout (see `FUN_004921e5`,
`FUN_004917dd`).

## Two distinct gameplay-mode schemas, one shared save-blob

`FUN_00440231` (the per-tick hash) reveals that the engine's "live"
state is one of two disjoint subsets of the schema:

- **Strategic mode** (`DAT_0057c8fc == 0`):
  hashes entries 1 (castles), 3 (per-player strategic state),
  4 (16-slot table), 5 (territories).

- **Tactical/combat mode** (`DAT_0057c8fc == 2`):
  hashes entries 7 (battle hexes A), 8 (combat units), 9 (battle
  hexes B).

But **all** entries are saved/loaded regardless of mode — saves
written during combat preserve the strategic state, and vice versa.
This is how the game seamlessly transitions back to the strategic
map after a battle resolves: the strategic arrays were sitting
untouched in memory the whole time.

## Reproduction

The schema table is fully derivable from the binary:

```bash
python3 -c '
import struct
with open("vendor/original/Lords2.exe", "rb") as f: data = f.read()
e_lfanew = struct.unpack_from("<I", data, 0x3c)[0]
opt_off  = e_lfanew + 0x18
ib       = struct.unpack_from("<I", data, opt_off + 0x1c)[0]
opt_sz   = struct.unpack_from("<H", data, e_lfanew + 0x14)[0]
sec_off  = opt_off + opt_sz
nsec     = struct.unpack_from("<H", data, e_lfanew + 0x06)[0]
secs = []
for i in range(nsec):
    s = sec_off + i*40
    va, raw = struct.unpack_from("<I I", data, s + 0x0c)[0], struct.unpack_from("<I", data, s + 0x14)[0]
    sz = max(struct.unpack_from("<I", data, s + 0x08)[0],
             struct.unpack_from("<I", data, s + 0x10)[0])
    secs.append((va, sz, raw))
def off(va):
    rva = va - ib
    for va2,sz,raw in secs:
        if va2 <= rva < va2 + sz: return raw + (rva - va2)

o, total = off(0x4de960), 0
for i in range(225):
    ptr, size = struct.unpack_from("<II", data, o + i*8)
    print(f"{i:3d} 0x{ptr:08x} {size:6d}")
    total += size
print("total:", total)
'
```

Cross-check: `total == 267028`, and `267028 + 204800 == 471828` =
the size of any one of `lastturn.sav`, `old_turn.sav`, `safeturn.sav`.

## Implementation note for our port

We don't need a separate "save format spec" — the schema **is** the
table.  The drop-in needs to:

1. Allocate the global blocks (DAT_00522f90 / 005440e0 / 0052f0b0 /
   etc.) at the same VAs.  For now we can put them in any `.data`
   section, since save compatibility doesn't need address parity.
2. Build the same 225-entry table pointing into our globals.
3. Use the same I/O sequence (write/read each entry; copy all 16
   Castles.dat blocks).

Save-file compatibility is *positional* — write the same bytes in
the same order and retail's `0x4adfe6` will load our saves.
Conversely, we load retail saves the same way.

## What this unlocks

This finding directly unblocks several sub-step body ports.  The
sub-step bodies (`FUN_004472ce` MP sim tick, `FUN_0049b8b0` turn-
timeout, etc.) read and mutate these globals; once the buffers
exist at fixed VAs and we can populate them from a real save file,
the ports can be diff-tested against retail with a known starting
state instead of all-zero stubs.

## Unknowns left

- The remaining +0 terrain enum values (`4, 7, 8, 9, 0xb, 0xc, 0xd,
  0x14, 0x1e, 0x28, 0x29`) — we have the scenario-byte ↔ +0 mapping
  but not yet the gameplay meaning (which is grass / mountain / forest
  / hills / etc).  Cross-confirmable from sprite-pack indexing in the
  overworld renderer.
- Exact bit semantics of `+1`'s `0x04 / 0x08 / 0x20 / 0x40 / 0x80`
  bits — `0x10` confirmed as "footprint cell", the others read/set
  with no obvious anchor yet.
- Block index → scenario-number mapping.  We assumed 1..16 = 16
  campaign scenarios; haven't yet matched these to the scenario
  list shown in the campaign select screen.
- Entries 11 (128 B at 0x568480) and 12 (1360 B at 0x53ea00) — both
  are small fixed-size buffers whose contents we haven't traced.
- The 4212-byte parallel hex array (entry 9) and the 51200-byte
  parallel castles array (entry 10) — why two of each?
- `c_hist.dat` and `history.dat` — neither file is referenced by
  string in Lords2.exe or mapl2.exe.  They date from 1996 (the
  others were patched into 2026).  Possibly vestigial from an
  earlier build; possibly read via a sprintf-built path we haven't
  found.  Low priority until the main schema is fully mapped.

📍 Source: `0x4ade93` (save), `0x4adfe6` (load), `0x4ae8a9..0x4aea70`
(Castles.dat I/O), `0x4de960` (the schema table itself), `0x440231`
(per-tick hash — confirms strides for entries 1, 3, 4, 5, 7, 8, 9).
