#!/usr/bin/env python3
"""
Generate the save-schema sources from
`docs/findings/save-schema-table.csv`.

The CSV is the canonical mapping of the 225-entry save-file schema table
at retail address `DAT_004de960`.  This script emits three files:

  - `src/save_schema_globals.h` — `extern` declarations for the scalar
    tail storage (one per unique non-named retail VA).  These are
    SHARED globals: any gameplay TU that ports a function reading/writing
    one of these engine globals includes this header and references the
    same `s_slot_<va>` storage the save system serialises.  This is the
    runtime home that removes the "scalar-global wall" — porting gameplay
    leaves no longer risks a second, desynced copy (see PROGRESS
    2026-05-29).

  - `src/save_schema_globals.c` — the storage *definitions* for those
    externs (compiled + linked once).  Picked up by `src/Makefile`'s
    `$(wildcard *.c)`; listed explicitly in `tests/Makefile`.

  - `src/save_schema_table.h` — the 225-entry `g_save_schema[]` table in
    entry-index order, pointing each row at the right storage.  Included
    once from `src/save.c`.

The 13 named big-block globals (entries 0..12, plus the footprint
overlay) are declared `extern` in `save.h` and defined in `save.c`; the
table just references them by name.  Entries with duplicate VAs alias
the same buffer (preserves the retail-side aliasing of slots 32/48 at
`0x0053f010` and slots 216..224 at `0x004eb2ac`).

Re-run after editing the CSV.  The generated files are checked in so the
build doesn't need Python.
"""

import csv
from pathlib import Path

REPO       = Path(__file__).resolve().parents[1]
CSV        = REPO / "docs/findings/save-schema-table.csv"
OUT        = REPO / "src/save_schema_table.h"
OUT_GLOB_H = REPO / "src/save_schema_globals.h"
OUT_GLOB_C = REPO / "src/save_schema_globals.c"

# Named schema slots.  These get human-readable storage names in save.h
# (declared as `extern` there + defined in save.c) instead of the default
# anonymous `s_slot_<va>` file-statics.  See docs/findings/save-format.md
# §"Major blocks" for entries 0..12; later entries promoted as their
# subsystems get ported.
NAMED_BIG_BLOCKS = [
    # (entry idx, retail VA, size, C identifier)
    (  0, 0x00522f90, 32768, "g_overworld_map"),
    (  1, 0x005440e0, 51200, "g_castles_state"),
    (  2, 0x00553d50,   264, "g_player_aux_44"),
    (  3, 0x0057bf00,  2112, "g_player_strategic_352"),
    (  4, 0x0053f9b0, 13056, "g_per_army_768"),
    (  5, 0x0052f0b0, 63420, "g_per_territory_420"),
    (  6, 0x00568dc0,   260, "g_unit_table_20"),
    (  7, 0x00554480, 34992, "g_per_battle_hex_432"),
    (  8, 0x0057a100,  7676, "g_per_combat_unit_76"),
    (  9, 0x00566520,  4212, "g_per_battle_hex_aux_52"),
    ( 10, 0x0056d8c0, 51200, "g_castles_shadow"),
    ( 11, 0x00568480,   128, "g_misc_128"),
    ( 12, 0x0053ea00,  1360, "g_misc_1360"),
    # Footprint list overlay (engine_tile expansion target).  Storage is
    # a single 160 B buffer in save.c; the g_footprint_list /
    # g_last_footprint_tile names are macros over it (see save.h).
    (110, 0x00553c80,   160, "g_footprint_storage_160"),
    # Player roster (seat -> army-id pairs).  The CSV size below is the
    # *persisted* size (6 B = first 3 {id,seat} pairs); the runtime
    # structure is 12 B (6 pairs) — see engine_roster.c.  The hand-owned
    # extern g_player_roster[12] in save.h is larger than this slot, and
    # the schema row serialises only its leading 6 bytes (faithful to
    # retail, whose save table at 0x568db0 also only persists 6 B).  The
    # tuple's size stays 6 so the CSV-match assert below passes; the 12 B
    # is purely in the hand-written extern.
    (140, 0x00568db0,     6, "g_player_roster"),
]

def main():
    rows = []
    with CSV.open() as f:
        for r in csv.DictReader(f):
            rows.append((int(r["idx"]), int(r["va"], 16), int(r["size"])))
    assert len(rows) == 225, f"expected 225 entries, got {len(rows)}"

    # Sanity-check the named big-block VAs/sizes.
    for idx, va, sz, _name in NAMED_BIG_BLOCKS:
        r_idx, r_va, r_sz = rows[idx]
        assert (r_idx, r_va, r_sz) == (idx, va, sz), \
            f"named-block mismatch at idx {idx}: CSV {r_va:#x}/{r_sz}, " \
            f"expected {va:#x}/{sz}"

    # All named-block VAs — skipped when emitting anonymous file-static
    # storage (they get their own extern decl in save.h + def in save.c).
    named_vas = {va for _, va, _, _ in NAMED_BIG_BLOCKS}

    # For tail entries (everything not in NAMED_BIG_BLOCKS), gather
    # unique-VA → max-size and VA → entry-idx list (for comments).
    named_idxs = {idx for idx, _, _, _ in NAMED_BIG_BLOCKS}
    unique_va_size: dict[int, int] = {}
    va_to_idxs: dict[int, list[int]] = {}
    for idx in range(225):
        if idx in named_idxs:
            continue
        _, va, sz = rows[idx]
        if va in named_vas:
            # A non-named entry that aliases a named VA — fall through and
            # let the table-emission step point it at the named ident.
            continue
        va_to_idxs.setdefault(va, []).append(idx)
        if va in unique_va_size:
            # Duplicate VA must have a consistent size (it does in our data).
            assert unique_va_size[va] == sz, \
                f"size mismatch for duplicate VA {va:#x}"
        else:
            unique_va_size[va] = sz

    def slot_decl(va, sz, *, storage):
        """C declaration for one scalar slot.  storage='' for a
        definition, 'extern ' for a declaration."""
        ident = f"s_slot_{va:08x}"
        if sz == 4:
            return f"{storage}uint32_t {ident};"
        return f"{storage}uint8_t  {ident}[{sz}];"

    def idx_comment(va):
        idxs = va_to_idxs[va]
        if len(idxs) == 1:
            return f"/* entry {idxs[0]:3d} */"
        return "/* entries " + ",".join(str(i) for i in idxs) + " */"

    # ---- save_schema_globals.h : extern declarations (shared) -------
    gh = [
        "/*",
        " * src/save_schema_globals.h — GENERATED by tools/gen_save_schema.py.",
        " * Do not hand-edit; re-run the generator if the CSV changes.",
        " *",
        " * SHARED storage for the scalar save-schema tail entries.  These",
        " * are the runtime homes for engine globals the save system also",
        " * serialises: a gameplay function ported into any TU includes this",
        " * header and reads/writes the same `s_slot_<va>` the save schema",
        " * points at — no second, desynced copy.  Definitions live in",
        " * save_schema_globals.c.  Rename a slot to a friendly identifier by",
        " * promoting it into NAMED_BIG_BLOCKS in the generator (→ extern in",
        " * save.h, def in save.c), or alias it with a #define in the owning",
        " * subsystem's header.  See docs/findings/save-format.md.",
        " */",
        "",
        "#ifndef OPENLORDS2_SAVE_SCHEMA_GLOBALS_H",
        "#define OPENLORDS2_SAVE_SCHEMA_GLOBALS_H",
        "",
        "#include <stdint.h>",
        "",
    ]
    for va in sorted(unique_va_size):
        gh.append(f"{slot_decl(va, unique_va_size[va], storage='extern '):<40} "
                  f"{idx_comment(va)}")
    gh.append("")
    gh.append("#endif /* OPENLORDS2_SAVE_SCHEMA_GLOBALS_H */")
    gh.append("")
    OUT_GLOB_H.write_text("\n".join(gh))

    # ---- save_schema_globals.c : definitions ------------------------
    gc = [
        "/*",
        " * src/save_schema_globals.c — GENERATED by tools/gen_save_schema.py.",
        " * Do not hand-edit; re-run the generator if the CSV changes.",
        " *",
        " * Storage definitions for the shared scalar save-schema globals",
        " * declared in save_schema_globals.h.  Zero-initialised (BSS),",
        " * matching retail's zero-on-start globals.",
        " */",
        "",
        "#include \"save_schema_globals.h\"",
        "",
    ]
    for va in sorted(unique_va_size):
        gc.append(f"{slot_decl(va, unique_va_size[va], storage=''):<40} "
                  f"{idx_comment(va)}")
    gc.append("")
    OUT_GLOB_C.write_text("\n".join(gc))

    lines = [
        "/*",
        " * src/save_schema_table.h — GENERATED by tools/gen_save_schema.py.",
        " * Do not hand-edit; re-run the generator if the CSV changes.",
        " *",
        " * The 225-entry save-file schema table from retail address",
        " * DAT_004de960.  Scalar-tail storage is now SHARED — declared in",
        " * save_schema_globals.h, defined in save_schema_globals.c — so",
        " * gameplay ports can touch the same globals.  The 13 named big",
        " * blocks (entries 0..12) are declared in save.h and defined in",
        " * save.c.",
        " */",
        "",
        "#ifndef OPENLORDS2_SAVE_SCHEMA_TABLE_H",
        "#define OPENLORDS2_SAVE_SCHEMA_TABLE_H",
        "",
        "#include <stdint.h>",
        "#include \"save.h\"",
        "#include \"save_schema_globals.h\"",
        "",
    ]

    # Emit the schema table.
    lines += [
        "/* 225-entry schema table.  Mirrors DAT_004de960 from retail. */",
        "const save_entry_t g_save_schema[SAVE_SCHEMA_COUNT] = {",
    ]
    # Build lookups for named-block idents — by idx for direct named
    # entries, and by VA so aliasing rows reuse the named storage too.
    big_by_idx = {idx: ident for idx, _, _, ident in NAMED_BIG_BLOCKS}
    big_by_va  = {va:  ident for _, va, _, ident in NAMED_BIG_BLOCKS}
    for idx, va, sz in rows:
        if idx in big_by_idx:
            ptr = big_by_idx[idx]
        elif va in big_by_va:
            ptr = big_by_va[va]
        else:
            ident = f"s_slot_{va:08x}"
            ptr = f"&{ident}" if sz == 4 else ident
        comment = f"/* {idx:3d}  retail {va:#010x} */"
        lines.append(f"    {{ {ptr:<27}, {sz:6d} }}, {comment}")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* OPENLORDS2_SAVE_SCHEMA_TABLE_H */")
    lines.append("")

    OUT.write_text("\n".join(lines))
    print(f"wrote {OUT.name}, {OUT_GLOB_H.name}, {OUT_GLOB_C.name}  "
          f"({len(unique_va_size)} shared scalar tail VAs)")


if __name__ == "__main__":
    main()
