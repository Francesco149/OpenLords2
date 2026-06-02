/*
 * OpenLords2 — pre-init config (port of `FUN_0040ecc4` + l2.eng load).
 *
 * Walks the same parse sequence as retail's l2.ini/sierra.ini
 * resolver, with the same key names and offset arithmetic — the
 * `strutil_find_substr` / `strutil_find_char` quirks (1-based
 * char-find, 0-based substring-find with "0 = not found OR offset 0"
 * ambiguity) are preserved verbatim.  See strutil.h for the
 * rationale.
 *
 * Lookup chain for the platform-config values:
 *
 *   1. Read l2.ini from CWD.  If "WindowsDir=..." is present, build
 *      `<WindowsDir>\sierra.ini`, read it, and parse the result.
 *      This handles the retail install where l2.ini sits next to
 *      Lords2.exe and points sideways at the global Sierra On-Line
 *      settings file under `C:\WINDOWS\` (the Sierra installer
 *      writes it there for cross-game sharing).
 *
 *   2. If either l2.ini was missing or the WindowsDir lookup failed,
 *      try `sierra.ini` directly from CWD.  This handles the GOG /
 *      bundled-with-game install case.
 *
 *   3. If still no sierra.ini, the defaults (loaded eagerly at the
 *      top of `config_init`) stay in place.  Retail logs
 *      `Err:Could not read sierra.ini` and continues — we mirror
 *      the log so transcripts match.
 *
 * On a successful sierra.ini read, the parser pulls six keys:
 *
 *   CDRomDrives=X       single-char drive letter      → cd_drive
 *   CDROM=N             int (CD speed)                → cd_rom_speed
 *   SoundChannels=N     int                           → sound_channels
 *   PhysicalMem=N       int (KB?)                     → physical_mem
 *   VideoSpeed=N        int                           → video_speed
 *   CPU=Class-MHz       Pentium/486 + clock           → cpu_class + cpu_mhz
 *
 * The "fast machine" derived flag fires when CDROM > 3 && cpu_mhz > 70,
 * matching retail's threshold.  No present consumer in our drop-in.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "io.h"
#include "strutil.h"

extern void log_line(const char *fmt, ...);

config_t g_config;

/* Localization buffer.  Sized to retail's `0x1d4c0 = 120000` and
 * lives in .bss so we don't pay for it until it's actually loaded. */
static uint8_t g_localization_buf[CONFIG_L2ENG_MAX_BYTES];
static size_t  g_localization_size;

/* Scratch read buffer for the .ini parser.  Retail uses 20000 bytes
 * heap-allocated via _malloc; we put it in .bss so the parser can't
 * fail at the allocation step (and so we don't drag a malloc into a
 * function that runs exactly once at startup). */
#define CONFIG_INI_SCRATCH 20000
static char g_ini_scratch[CONFIG_INI_SCRATCH];

/* Persisted player-prefs blob (retail's `lords2.inf`, 0x468 = 1128 B,
 * read verbatim into DAT_0053f1e0 by the bootstrap chain at 0x4ae158).
 * The leading bytes are the config-screen player setup the new-game
 * seat depends on: the player name field at +0x00 and the kingdom/title
 * name at +0x18 (both copied into the human's aux record by the SP seat
 * `player_seat_assign`), followed by the persisted custom-game settings
 * dwords.  Held raw; consumers index by offset. */
static uint8_t g_player_prefs[CONFIG_PLAYER_PREFS_BYTES];
static size_t  g_player_prefs_size;

/* ---------------------------------------------------------------- *
 *  Internal helpers.  These mirror retail's exact two-step pattern  *
 *  for "find KEY then find = then read value at the position past   *
 *  the =" — the convenience of expressing it three lines instead    *
 *  of seven is worth the helper.                                    *
 * ---------------------------------------------------------------- */

/* Returns the offset of the byte FOLLOWING the '=' after the first
 * occurrence of `key` in the first `max` bytes of `buf`, or 0 if
 * either `key` or the trailing `=` couldn't be found.
 *
 * Two deliberate deviations from retail's verbatim two-step:
 *
 *  (a) `find_substr`'s "0 = not-found OR found-at-offset-0" return
 *      quirk is disambiguated here.  If the substring really was
 *      found at offset 0, `buf[0..klen)` matches `key`; otherwise
 *      we treat 0 as "not found" and short-circuit.  Without this,
 *      a missing key combined with any other `=` in the file would
 *      have us parsing the wrong value — silently broken in retail,
 *      but ASan-trippy and behaviourally wrong.
 *
 *  (b) `find_char`'s bound is `max - off`, not `max`.  Retail passes
 *      the full 20000-byte malloc size to both calls — out-of-bounds
 *      past the key position, harmless in practice because retail's
 *      buffer is zero-padded past the file content.  Our pure-C
 *      surface receives arbitrarily-sized buffers, so clamping keeps
 *      the function ASan-clean.
 *
 * Both deviations are safer-than-retail and fix real bugs while
 * preserving the contract on well-formed inputs (every key has its
 * `=`, every value is followed by a newline or EOF). */
static int seek_past_equals(const char *buf, const char *key, int max)
{
    int off = strutil_find_substr(buf, key, max);
    if (off == 0) {
        /* Disambiguate: only "found at offset 0" if buf actually
         * starts with key.  Anything else is "not found". */
        size_t klen = strlen(key);
        if (max < (int)klen || memcmp(buf, key, klen) != 0) return 0;
    }
    int remaining = max - off;
    if (remaining < 0) remaining = 0;
    int eq  = strutil_find_char(buf + off, '=', remaining);
    if (eq == 0) return 0;
    return off + eq;
}

/* ---------------------------------------------------------------- *
 *  Pure-C parse of a sierra.ini-format buffer.  No globals, no I/O   *
 *  — testable in isolation under host gcc + ASan.                    *
 * ---------------------------------------------------------------- */
void config_set_defaults(config_t *out)
{
    out->cd_drive       = 'D';
    out->cd_rom_speed   = 2;
    out->physical_mem   = 8000;
    out->cpu_class      = 0x1e6;
    out->cpu_mhz        = 0x42;
    out->video_speed    = 0;
    out->sound_channels = 0xd;
    out->fast_machine   = 0;
}

void config_parse_sierra_ini(const char *buf, int n, config_t *out)
{
    int off;

    /* CDRomDrives=X — read the single byte right after the '='. */
    off = seek_past_equals(buf, "CDRomDrives", n);
    if (off > 0) out->cd_drive = buf[off];

    off = seek_past_equals(buf, "CDROM", n);
    if (off > 0) out->cd_rom_speed = atoi(buf + off);

    off = seek_past_equals(buf, "SoundChannels", n);
    if (off > 0) out->sound_channels = atoi(buf + off);

    off = seek_past_equals(buf, "PhysicalMem", n);
    if (off > 0) out->physical_mem = atoi(buf + off);

    off = seek_past_equals(buf, "VideoSpeed", n);
    if (off > 0) out->video_speed = atoi(buf + off);

    /* CPU= line — first char picks the class (Pentium-class vs.
     * 486-class), then an embedded '-' separates the class string
     * from the MHz number.  Retail logic verbatim. */
    off = seek_past_equals(buf, "CPU", n);
    if (off > 0) {
        char c = buf[off];
        if (c == '>' || c == 'p' || c == 'P') out->cpu_class = 0x24a;
        else                                  out->cpu_class = 0x1e6;
        /* Scan up to 20 bytes for '-'; atoi the byte after it. */
        int dash = 0;
        for (; dash < 0x14 && buf[off + dash] != '-'; dash++) { }
        out->cpu_mhz = atoi(buf + off + dash + 1);
    }

    if (out->cd_rom_speed > 3 && out->cpu_mhz > 0x46)
        out->fast_machine = 1;
}

/* ---------------------------------------------------------------- *
 *  Try l2.ini → look up "WindowsDir=" → read <WindowsDir>\sierra.ini *
 *  into g_ini_scratch.  Returns:                                     *
 *    1  — sierra.ini bytes are now in g_ini_scratch, ready to parse  *
 *    0  — sierra.ini at the WindowsDir path wasn't readable; caller  *
 *         should try direct sierra.ini in CWD                        *
 *   -1  — l2.ini missing OR WindowsDir key missing.  Retail's        *
 *         control flow gives up entirely in the WindowsDir-missing   *
 *         case (goto LAB_0040f0e3 skips the direct-sierra.ini        *
 *         fallback); we mirror that.                                  *
 *                                                                    *
 *  The asymmetry between "WindowsDir missing → give up" (bug?) and  *
 *  "WindowsDir present but path unreadable → fall through" is        *
 *  retail-as-shipped.  Preserved for behavioural parity with         *
 *  Lords2.exe; if it turns out to be a real-world problem, an        *
 *  intentional deviation would be straightforward.                  *
 * ---------------------------------------------------------------- */
static int try_l2ini_to_sierra_ini(void)
{
    int n = io_read_file_at("l2.ini", g_ini_scratch, CONFIG_INI_SCRATCH, 0);
    if (n <= 0) return -1;

    int off = seek_past_equals(g_ini_scratch, "WindowsDir",
                               CONFIG_INI_SCRATCH);
    if (off <= 0) {
        log_line("Err:Could not read string WindowsDir= in l2.ini");
        return -1;
    }

    /* Copy up to 20 bytes (retail's 0x14) past the '=' into a small
     * stack buffer, strip trailing CR/LF/junk, append "\sierra.ini",
     * try to read that path. */
    char path[40];
    memset(path, 0, sizeof(path));
    strutil_bounded_copy(path, g_ini_scratch + off, 0x14);
    strutil_strip_ctrl((unsigned char *)path);
    /* Defensive bound check before strcat — path is sized 40, suffix
     * is 11 bytes + null = 12.  Anything past 28 bytes of dir name
     * would overflow; trim conservatively. */
    if (strlen(path) > 28) path[28] = 0;
    strcat(path, "\\sierra.ini");

    n = io_read_file_at(path, g_ini_scratch, CONFIG_INI_SCRATCH, 0);
    return n > 0 ? 1 : 0;
}

/* ---------------------------------------------------------------- *
 *  l2.eng localization load.  Retail reads up to 120000 bytes from   *
 *  the file via `FUN_004af8d9(s_l2_eng_004d2784, &DAT_00591580,      *
 *  120000, 0)`.  No structure decoding here — the buffer is held    *
 *  raw and consumers index into it by offset.                        *
 * ---------------------------------------------------------------- */
static void load_l2_eng(void)
{
    int n = io_read_file_at("l2.eng", g_localization_buf,
                            CONFIG_L2ENG_MAX_BYTES, 0);
    if (n <= 0) {
        log_line("config: l2.eng not readable (got %d bytes) — UI text"
                 " lookups will return empty until file is present", n);
        g_localization_size = 0;
        return;
    }
    g_localization_size = (size_t)n;
    log_line("config: loaded l2.eng (%zu bytes)", g_localization_size);
}

/* ---------------------------------------------------------------- *
 *  lords2.inf player-prefs load.  Retail's bootstrap opens the file *
 *  (0x4ae158) and reads 0x468 bytes into DAT_0053f1e0; the new-game *
 *  config screen then seats the human player's name/title out of it. *
 *  Tolerates the file being absent (a fresh install ships one, but   *
 *  the headless seats must not hard-fail on it).                     *
 * ---------------------------------------------------------------- */
static void load_lords2_inf(void)
{
    int n = io_read_file_at("lords2.inf", g_player_prefs,
                            CONFIG_PLAYER_PREFS_BYTES, 0);
    if (n <= 0) {
        log_line("config: lords2.inf not readable (got %d bytes) —"
                 " player name/title default to empty", n);
        g_player_prefs_size = 0;
        return;
    }
    g_player_prefs_size = (size_t)n;
}

/* ---------------------------------------------------------------- */

void config_init(void)
{
    config_set_defaults(&g_config);

    load_l2_eng();
    load_lords2_inf();

    /* Three outcomes from the l2.ini lookup (see comment on
     * try_l2ini_to_sierra_ini):
     *   1  → sierra.ini already in scratch, parse it.
     *   0  → l2.ini gave a WindowsDir but its sierra.ini wasn't
     *        readable.  Fall back to direct sierra.ini in CWD.
     *  -1  → l2.ini missing or WindowsDir missing.  Retail bails
     *        here without trying CWD sierra.ini; we mirror.
     */
    int rv = try_l2ini_to_sierra_ini();
    if (rv == 0) {
        int n = io_read_file_at("sierra.ini", g_ini_scratch,
                                CONFIG_INI_SCRATCH, 0);
        rv = (n > 0) ? 1 : -1;
    }
    if (rv < 0) {
        log_line("Err:Could not read sierra.ini");
        return;
    }
    config_parse_sierra_ini(g_ini_scratch, CONFIG_INI_SCRATCH, &g_config);
    log_line("config: cd=%c cd_speed=%d sound_ch=%d mem=%d cpu=%s-%d "
             "video=%d fast=%d",
             g_config.cd_drive, g_config.cd_rom_speed,
             g_config.sound_channels, g_config.physical_mem,
             g_config.cpu_class == 0x24a ? "P" : "486",
             g_config.cpu_mhz, g_config.video_speed,
             g_config.fast_machine);
}

const uint8_t *config_localization_data(size_t *size_out)
{
    if (size_out) *size_out = g_localization_size;
    return g_localization_size ? g_localization_buf : NULL;
}

const uint8_t *config_player_prefs(size_t *size_out)
{
    if (size_out) *size_out = g_player_prefs_size;
    return g_player_prefs_size ? g_player_prefs : NULL;
}
