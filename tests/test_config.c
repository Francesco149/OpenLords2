/*
 * test_config.c — unit tests for src/config.c.
 *
 * Exercises the pure-C entry points (`config_set_defaults` and
 * `config_parse_sierra_ini`) without going through the Win32 I/O
 * glue.  The buffer-based parser is fed synthetic ini fragments;
 * the defaults table is asserted byte-for-byte against retail.
 *
 * What is NOT tested here:
 *   - `config_init` (touches the filesystem + globals)
 *   - `load_l2_eng` (file I/O)
 *   - `try_l2ini_to_sierra_ini` (file I/O + retail asymmetry)
 *
 * Those would need a temp-dir fixture + per-OS path handling.  The
 * pure parser covers the interesting branches; the I/O glue is a
 * thin shell over `io_read_file_at`.
 */

#include "t.h"
#include "config.h"

#include <string.h>

/* Mirror production's scratch-buffer shape: a fixed-size,
 * zero-initialised buffer with the file content copied in.  Retail
 * uses a 20000-byte malloc that gets zero-fill once the file is
 * read into the first N bytes, and the parser legitimately reads
 * the post-file zero region for several string scans (it's how the
 * CPU= dash-scan stays in-bounds when CPU values are short).  Our
 * production code uses a static-bss scratch of the same size; we
 * mirror that here so the test environment exactly matches what
 * ships. */
#define TEST_INI_SCRATCH 4096   /* smaller than production but still
                                 * dwarfs any test input, so the
                                 * pattern matches */
static void parse(const char *src, config_t *out)
{
    static char buf[TEST_INI_SCRATCH];
    memset(buf, 0, sizeof buf);
    size_t n = strlen(src);
    if (n > sizeof buf) n = sizeof buf;
    memcpy(buf, src, n);
    config_set_defaults(out);
    config_parse_sierra_ini(buf, (int)sizeof buf, out);
}

/* ─────────────────────────── defaults ───────────────────────────── */

/* Byte-for-byte against retail's `DAT_0053f200 = 0x44; ...` block at
 * the top of FUN_0040ecc4.  Any defaults drift here means we've
 * diverged from retail's "no sierra.ini found" semantics. */
int test_config_defaults_match_retail(void)
{
    config_t c;
    memset(&c, 0xee, sizeof c);
    config_set_defaults(&c);
    T_ASSERT_EQ_I(c.cd_drive,       'D');
    T_ASSERT_EQ_I(c.cd_rom_speed,   2);
    T_ASSERT_EQ_I(c.physical_mem,   8000);
    T_ASSERT_EQ_I(c.cpu_class,      0x1e6);
    T_ASSERT_EQ_I(c.cpu_mhz,        0x42);
    T_ASSERT_EQ_I(c.video_speed,    0);
    T_ASSERT_EQ_I(c.sound_channels, 0xd);
    T_ASSERT_EQ_I(c.fast_machine,   0);
    return 0;
}

int test_config_parse_empty_keeps_defaults(void)
{
    config_t c;
    parse("", &c);
    T_ASSERT_EQ_I(c.cd_drive,       'D');
    T_ASSERT_EQ_I(c.cd_rom_speed,   2);
    T_ASSERT_EQ_I(c.physical_mem,   8000);
    T_ASSERT_EQ_I(c.cpu_class,      0x1e6);
    T_ASSERT_EQ_I(c.cpu_mhz,        0x42);
    T_ASSERT_EQ_I(c.video_speed,    0);
    T_ASSERT_EQ_I(c.sound_channels, 0xd);
    T_ASSERT_EQ_I(c.fast_machine,   0);
    return 0;
}

/* ─────────────────────────── per-key parse ──────────────────────── */

/* CDRomDrives=X — single-byte read, no atoi.  The byte right after
 * '=' lands directly in cd_drive. */
int test_config_parse_cdrom_drives_letter(void)
{
    config_t c;
    parse("# header line so key sits past offset 0\n"
          "[Sierra]\nCDRomDrives=F\n", &c);
    T_ASSERT_EQ_I(c.cd_drive, 'F');
    return 0;
}

int test_config_parse_cdrom_speed(void)
{
    config_t c;
    parse("[Sierra]\nCDROM=8\n", &c);
    T_ASSERT_EQ_I(c.cd_rom_speed, 8);
    return 0;
}

int test_config_parse_sound_channels(void)
{
    config_t c;
    parse("[Sierra]\nSoundChannels=16\n", &c);
    T_ASSERT_EQ_I(c.sound_channels, 16);
    return 0;
}

int test_config_parse_physical_mem(void)
{
    config_t c;
    parse("[Sierra]\nPhysicalMem=65536\n", &c);
    T_ASSERT_EQ_I(c.physical_mem, 65536);
    return 0;
}

int test_config_parse_video_speed(void)
{
    config_t c;
    parse("[Sierra]\nVideoSpeed=2\n", &c);
    T_ASSERT_EQ_I(c.video_speed, 2);
    return 0;
}

/* ─────────────────────────── CPU= branches ──────────────────────── */

/* Three character classes select Pentium-class (0x24a):
 *   'P' — most common (Pentium, Pentium II, Pentium Pro, ...)
 *   'p' — lowercase variant
 *   '>' — sentinel for "anything past Pentium" (retail quirk —
 *         probably intended as a future-proofing marker, never
 *         actually used in real sierra.ini files).
 * Anything else → 486-class (0x1e6). */
int test_config_parse_cpu_class_pentium_upper_p(void)
{
    config_t c;
    parse("[Sierra]\nCPU=Pentium-100\n", &c);
    T_ASSERT_EQ_I(c.cpu_class, 0x24a);
    return 0;
}

int test_config_parse_cpu_class_pentium_lower_p(void)
{
    config_t c;
    parse("[Sierra]\nCPU=pentium-90\n", &c);
    T_ASSERT_EQ_I(c.cpu_class, 0x24a);
    return 0;
}

int test_config_parse_cpu_class_pentium_gt_sigil(void)
{
    config_t c;
    parse("[Sierra]\nCPU=>200-200\n", &c);
    T_ASSERT_EQ_I(c.cpu_class, 0x24a);
    return 0;
}

int test_config_parse_cpu_class_486_other(void)
{
    config_t c;
    parse("[Sierra]\nCPU=486-66\n", &c);
    T_ASSERT_EQ_I(c.cpu_class, 0x1e6);
    return 0;
}

/* The MHz number sits past the first '-' in the value (scanned up
 * to 20 bytes from '='). */
int test_config_parse_cpu_mhz_after_dash(void)
{
    config_t c;
    parse("[Sierra]\nCPU=Pentium-133\n", &c);
    T_ASSERT_EQ_I(c.cpu_mhz, 133);
    return 0;
}

/* ──────────────────── fast_machine threshold ─────────────────────── */

/* CD > 3 (so ≥ 4) AND cpu_mhz > 70 (so ≥ 71) → fast_machine = 1. */
int test_config_parse_fast_machine_threshold_both_satisfied(void)
{
    config_t c;
    parse("[Sierra]\nCDROM=4\nCPU=Pentium-90\n", &c);
    T_ASSERT_EQ_I(c.cd_rom_speed, 4);
    T_ASSERT_EQ_I(c.cpu_mhz, 90);
    T_ASSERT_EQ_I(c.fast_machine, 1);
    return 0;
}

/* CD exactly 3 (boundary) → NOT fast.  Retail's threshold is `>`
 * not `>=` so 3× drives stay slow even with a Pentium. */
int test_config_parse_fast_machine_threshold_cd_alone_insufficient(void)
{
    config_t c;
    parse("[Sierra]\nCDROM=3\nCPU=Pentium-200\n", &c);
    T_ASSERT_EQ_I(c.fast_machine, 0);
    return 0;
}

/* cpu_mhz exactly 70 (boundary) → NOT fast.  Threshold is `> 0x46`
 * = `> 70` so a P-70 stays slow. */
int test_config_parse_fast_machine_threshold_cpu_alone_insufficient(void)
{
    config_t c;
    parse("[Sierra]\nCDROM=8\nCPU=Pentium-70\n", &c);
    T_ASSERT_EQ_I(c.fast_machine, 0);
    return 0;
}

/* ─────────────────────── missing keys ────────────────────────────── */

/* If a key isn't in the buffer at all, the existing field value
 * (here, the default 0xd = 13 for sound_channels) is preserved.
 * Crucial for the "partial sierra.ini" case where some keys are
 * present and others aren't. */
int test_config_parse_missing_key_keeps_existing(void)
{
    config_t c;
    parse("[Sierra]\nCDROM=12\n", &c);
    T_ASSERT_EQ_I(c.cd_rom_speed, 12);
    T_ASSERT_EQ_I(c.sound_channels, 0xd);   /* untouched default */
    T_ASSERT_EQ_I(c.physical_mem, 8000);
    return 0;
}

/* ─────────────── real-world sierra.ini shape ─────────────────────── */

/* A realistic-shaped sierra.ini hits every key.  This is the test
 * that catches "I broke the parser" — every key, real values, one
 * assertion per key. */
int test_config_parse_real_sierra_ini_shape(void)
{
    const char *src =
        "[Sierra]\n"
        "WindowsDir=C:\\WINDOWS\n"
        "CDRomDrives=E\n"
        "CDROM=8\n"
        "SoundChannels=15\n"
        "PhysicalMem=131072\n"
        "VideoSpeed=2\n"
        "CPU=Pentium-200\n";

    config_t c;
    parse(src, &c);
    T_ASSERT_EQ_I(c.cd_drive,       'E');
    T_ASSERT_EQ_I(c.cd_rom_speed,   8);
    T_ASSERT_EQ_I(c.sound_channels, 15);
    T_ASSERT_EQ_I(c.physical_mem,   131072);
    T_ASSERT_EQ_I(c.video_speed,    2);
    T_ASSERT_EQ_I(c.cpu_class,      0x24a);
    T_ASSERT_EQ_I(c.cpu_mhz,        200);
    T_ASSERT_EQ_I(c.fast_machine,   1);    /* CD=8 > 3, MHz=200 > 70 */
    return 0;
}
