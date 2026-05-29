/*
 * OpenLords2 — pre-init config (header).
 *
 * Port of the WinMain pre-init chain from `FUN_0040e9ab`:
 *
 *   1. Load `l2.eng` — localization data (120 KB blob, read into
 *      a static buffer for later string-table lookups by UI code
 *      that hasn't landed yet).
 *   2. Parse `l2.ini`/`sierra.ini` (`FUN_0040ecc4`) — six int/char
 *      values that gate audio + video + CD path selection in
 *      various downstream subsystems.
 *
 * `0x4ae1b8` (the `lords2.inf` 1128-byte persistence blob load)
 * is deferred — its consumer chain (0x4ae2bd, 0x4ae2ab,
 * 0x4ae310) hasn't been decoded yet, so loading the bytes
 * without a consumer would be write-only state.
 *
 * Today this slice is *purely preparatory*: no behavioural change
 * visible at runtime.  The values become load-bearing as we port
 * the consumers — Smacker (video_speed), Miles/DSound mixing
 * (sound_channels), and the CD-relative file resolver
 * (cd_drive_letter).
 */

#ifndef OPENLORDS2_CONFIG_H
#define OPENLORDS2_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* Cooked values parsed out of l2.ini / sierra.ini, with retail's
 * exact defaults pre-loaded for the "no file present" fallback.
 * Field names map 1:1 to .ini key names and (via the comments) to
 * retail's DAT_* addresses, so future ports of the consumers are
 * mechanical. */
typedef struct {
    char  cd_drive;        /* DAT_0053f200   default 'D'                */
    int   cd_rom_speed;    /* DAT_0053f5f8   default 2                  */
    int   physical_mem;    /* DAT_0053f5e8   default 8000               */
    int   cpu_class;       /* DAT_0053f5dc   default 0x1e6, 0x24a if 'P'*/
    int   cpu_mhz;         /* DAT_0053f5e0   default 0x42 = 66          */
    int   video_speed;     /* DAT_0053f5e4   default 0                  */
    int   sound_channels;  /* DAT_0053f5f0   default 0xd  = 13          */
    int   fast_machine;    /* DAT_0053f5f4   1 if CDROM>3 && cpu_mhz>70 */
} config_t;

extern config_t g_config;

/* Localization-buffer storage. */
#define CONFIG_L2ENG_MAX_BYTES 120000

/* Initialise globals + read l2.eng + parse l2.ini/sierra.ini.
 * Tolerates all files being missing — failure logs a one-liner and
 * leaves defaults in place. */
void config_init(void);

/* Pointer/size of the loaded l2.eng buffer.  Returns NULL/0 if the
 * file wasn't readable.  Callers (none yet) read string-table
 * entries from this — retail addresses these by offset inside the
 * 120-KB blob. */
const uint8_t *config_localization_data(size_t *size_out);

/* Pure-C surface for unit tests + reuse.  These are exactly the
 * routines under test in tests/test_config.c — kept here so the
 * Win32-side I/O glue (load_l2_eng, try_l2ini_to_sierra_ini) and
 * the pure parse logic are decoupled. */

/* Reset `out` to the same defaults retail eagerly applies at the
 * top of FUN_0040ecc4 (before any file read). */
void config_set_defaults(config_t *out);

/* Parse a sierra.ini-format buffer in-place.  Walks the same six
 * keys as retail (CDRomDrives, CDROM, SoundChannels, PhysicalMem,
 * VideoSpeed, CPU) and writes into `out`.  Keys not present are
 * left untouched — caller should `config_set_defaults` first if
 * they want the retail defaults-then-overrides semantics. */
void config_parse_sierra_ini(const char *buf, int n, config_t *out);

#endif /* OPENLORDS2_CONFIG_H */
