/*
 * OpenLords2 — minimal file-IO helper.
 *
 * Wraps the open/seek/read/close dance into one call to match
 * retail's universal asset-load helper `FUN_004af8d9` at 0x4af8d9.
 *
 * The retail helper has a three-tier retry chain:
 *
 *   1. Try the file at the current working directory.
 *   2. Failing that, call `FUN_004af312(path)` — chdir into a path
 *      derived from the file's extension (`.smk` → `_smk_low` or
 *      `_smk_high` depending on `DAT_0053f5f4`; etc.), retry.
 *   3. Failing that, call `FUN_004af116(path)` — chdir into the
 *      "data root" derived from `DAT_0053f200`, retry.
 *
 * For the drop-in bootstrap we lean on `init_exe_dir`'s CWD anchor
 * to put us next to retail's data files in the typical install, so
 * the simple tier-1 lookup suffices.  The retry chain is a TODO —
 * see `docs/findings/ddraw-dsound-bringup.md` "Open questions" for
 * the rationale.
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
