/*
 * OpenLords2 — minimal file-IO helper.
 *
 * Wraps the open/seek/read/close dance into one call to match
 * retail's universal asset-load helper `FUN_004af8d9` at 0x4af8d9.
 *
 * The retail helper has a three-tier retry chain:
 *
 *   1. Try the file at the current working directory.
 *   2. Failing that, call `0x4af312(path)` — chdir into a path
 *      derived from the file's extension (`.smk` → `_smk_low` or
 *      `_smk_high` depending on `DAT_0053f5f4`; etc.), retry.
 *   3. Failing that, call `0x4af116(path)` — chdir into the
 *      "data root" derived from `DAT_0053f200`, retry.
 *
 * For the drop-in bootstrap we lean on `init_exe_dir`'s CWD anchor
 * to put us next to retail's data files in the typical (GOG-flat)
 * install, so tier-1 finds every asset.  Tiers 2/3 are deferred
 * until a future chip needs the CD-tree subdirectory layout (e.g.
 * intro Smacker) — see "Asset-load IO helper family" in
 * `docs/findings/ddraw-dsound-bringup.md` for the extension→subdir
 * mapping and the rationale.  The base directory those tiers pivot
 * around (retail `DAT_0058fe30`) and the revert-to-base after a
 * subdir descent (`0x4af502`) are ported in `src/io_path.c`
 * (`io_path.h`); tiers 2/3 will read it via `io_path_base()`.
 *
 * The five extension-manipulation helpers that tiers 2/3 share with
 * other call sites (FUN_004af54a/5ba/62c/675/6c9) are ported in
 * `src/strutil.c` as the `strutil_ext_*` family.
 */

#ifndef OPENLORDS2_IO_H
#define OPENLORDS2_IO_H

#include <stddef.h>

/* Open `path`, seek to `offset` (from start), read up to `len`
 * bytes into `buf`, close.  Returns bytes-read on success, 0 on
 * any failure (open / seek / read).  No `errno`-style detail —
 * the caller logs the failure context. */
int io_read_file_at(const char *path, void *buf, size_t len, long offset);

#endif /* OPENLORDS2_IO_H */
