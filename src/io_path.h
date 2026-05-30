/*
 * OpenLords2 — asset "base directory" anchor + revert.
 *
 * Retail keeps the game's launch directory in a 100-byte global
 * (`DAT_0058fe30`), captured once by `__getcwd(&DAT_0058fe30, 100)`
 * at the top of the WinMain message-loop owner (0x40e9ab).  The
 * asset-load retry chain then descends into
 * extension-derived subdirectories (`.smk` → `_smk_low`/`_smk_high`,
 * the CD data tree, …) via `0x4af312` / `0x4af116`, and afterwards
 * reverts the cwd back to that base via `FUN_004af502` (0x4af502).
 *
 * This module owns the base-dir buffer and the revert.  The
 * subdir-descent helpers (tiers 2/3 of the asset chain, see io.h)
 * land in later chips and will read the base via `io_path_base()`.
 *
 * CRT-portable: retail's `__chdir`/`__getcwd` are the MSVCRT
 * `_chdir`/`_getcwd` wrappers (→ Set/GetCurrentDirectoryA).  We use
 * the same CRT calls (`_chdir`/`_getcwd` on Windows, POSIX
 * `chdir`/`getcwd` on the host) so this TU builds — and is tested —
 * on both, unlike the Win32-API io.c.
 */

#ifndef OPENLORDS2_IO_PATH_H
#define OPENLORDS2_IO_PATH_H

/* Capture the current working directory as the asset base directory
 * — mirror of retail's `__getcwd(&DAT_0058fe30, 100)` in WinMain.
 * Returns 0 on success, -1 if the cwd could not be read (the base is
 * cleared to "" in that case).  Truncates at the retail 100-byte
 * capture width. */
int io_path_capture_base(void);

/* Install an explicit base directory.  Primarily a host-test seam;
 * the live build captures via io_path_capture_base.  `dir` is copied
 * and truncated to the retail 100-byte width; NULL clears it. */
void io_path_set_base(const char *dir);

/* Read-only view of the captured base directory (empty string until
 * captured/set). */
const char *io_path_base(void);

/* Revert the cwd to the captured base directory — port of retail
 * FUN_004af502 @ 0x4af502.
 *
 * Retail is `void` and, on chdir failure, formats the status line
 * "ERR:CHD  not reserted is now <cwd>" into status.txt via the
 * structured logger 0x4afab9.  That status.txt writer is not ported
 * (elided everywhere — see gfx.c); we route the diagnostic through
 * the drop-in dev log instead, and return the chdir result for
 * host-test observability: 0 on success, non-zero if the base dir
 * could not be entered. */
int io_path_chdir_base(void);

#endif /* OPENLORDS2_IO_PATH_H */
