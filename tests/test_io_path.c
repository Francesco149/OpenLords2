/*
 * test_io_path.c — coverage for io_path_chdir_base, the portable port
 * of retail FUN_004af502 (0x4af502), plus the base-dir capture/seam.
 *
 * Properties verified:
 *
 *   - capture_base + chdir away + chdir_base returns to the captured
 *     base (the success path; getcwd round-trip).
 *   - set_base seam installs an explicit base; chdir_base honours it.
 *   - a bogus base directory makes chdir_base fail (non-zero return)
 *     and leaves the cwd untouched (retail's error/log path).
 *   - set_base truncates at the retail 100-byte capture width and
 *     NULL clears the base.
 *
 * These use real cwd/chdir against scratch directories under the
 * system temp dir, restoring the original cwd at the end of each test.
 */

#include "t.h"
#include "io_path.h"
#include "engine_menu_input.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#  include <direct.h>
#  define OS_CHDIR  _chdir
#  define OS_GETCWD _getcwd
#  define OS_MKDIR(p) _mkdir(p)
#else
#  include <unistd.h>
#  define OS_CHDIR  chdir
#  define OS_GETCWD getcwd
#  define OS_MKDIR(p) mkdir((p), 0777)
#endif

/* Make a scratch dir under /tmp and return its absolute path in `out`
 * (resolved via getcwd after chdir, so it matches what chdir_base will
 * produce).  Returns 0 on success. */
static int make_and_resolve(const char *rel, char *out, size_t outsz)
{
    OS_MKDIR(rel);                 /* ignore EEXIST */
    if (OS_CHDIR(rel) != 0) return -1;
    if (OS_GETCWD(out, outsz) == NULL) return -1;
    return 0;
}

int test_io_path_chdir_base_roundtrip(void)
{
    char orig[256];
    if (OS_GETCWD(orig, sizeof orig) == NULL) T_SKIP("getcwd unavailable");

    /* Establish a base under /tmp. */
    char base[256];
    if (OS_CHDIR("/tmp") != 0) { OS_CHDIR(orig); T_SKIP("no /tmp"); }
    if (make_and_resolve("ol2_iopath_base", base, sizeof base) != 0) {
        OS_CHDIR(orig); T_SKIP("scratch mkdir failed");
    }
    T_ASSERT_EQ_I(io_path_capture_base(), 0);
    T_ASSERT_EQ_S(io_path_base(), base);

    /* Wander off into a sibling dir, then revert. */
    char away[256];
    if (OS_CHDIR("/tmp") != 0) { OS_CHDIR(orig); T_SKIP("no /tmp"); }
    if (make_and_resolve("ol2_iopath_away", away, sizeof away) != 0) {
        OS_CHDIR(orig); T_SKIP("scratch mkdir failed");
    }

    T_ASSERT_EQ_I(io_path_chdir_base(), 0);

    char now[256];
    T_ASSERT(OS_GETCWD(now, sizeof now) != NULL);
    T_ASSERT_EQ_S(now, base);

    OS_CHDIR(orig);
    return 0;
}

int test_io_path_set_base_seam(void)
{
    char orig[256];
    if (OS_GETCWD(orig, sizeof orig) == NULL) T_SKIP("getcwd unavailable");

    /* Resolve /tmp's canonical path, install it as the base via the
     * seam, wander elsewhere, revert. */
    char tmp[256];
    if (OS_CHDIR("/tmp") != 0) { OS_CHDIR(orig); T_SKIP("no /tmp"); }
    if (OS_GETCWD(tmp, sizeof tmp) == NULL) { OS_CHDIR(orig); T_SKIP("getcwd"); }
    OS_CHDIR(orig);

    io_path_set_base(tmp);
    T_ASSERT_EQ_S(io_path_base(), tmp);

    T_ASSERT_EQ_I(io_path_chdir_base(), 0);
    char now[256];
    T_ASSERT(OS_GETCWD(now, sizeof now) != NULL);
    T_ASSERT_EQ_S(now, tmp);

    OS_CHDIR(orig);
    return 0;
}

int test_io_path_chdir_base_failure(void)
{
    char orig[256];
    if (OS_GETCWD(orig, sizeof orig) == NULL) T_SKIP("getcwd unavailable");

    /* Park in /tmp, then point the base at a directory that does not
     * exist.  retail logs "ERR:CHD ..." and leaves the cwd alone; we
     * surface the non-zero chdir result and assert the cwd is unmoved. */
    char before[256];
    if (OS_CHDIR("/tmp") != 0) { OS_CHDIR(orig); T_SKIP("no /tmp"); }
    if (OS_GETCWD(before, sizeof before) == NULL) { OS_CHDIR(orig); T_SKIP("getcwd"); }

    io_path_set_base("/nonexistent/ol2/definitely/not/here");
    T_ASSERT(io_path_chdir_base() != 0);

    char after[256];
    T_ASSERT(OS_GETCWD(after, sizeof after) != NULL);
    T_ASSERT_EQ_S(after, before);     /* failed chdir doesn't move cwd */

    OS_CHDIR(orig);
    return 0;
}

int test_io_path_set_base_truncate_and_clear(void)
{
    /* 99 chars fit; the 100th onward is dropped (retail 100-byte cap,
     * one byte reserved for the NUL). */
    char longp[200];
    memset(longp, 'a', sizeof longp);
    longp[sizeof longp - 1] = '\0';

    io_path_set_base(longp);
    T_ASSERT_EQ_U(strlen(io_path_base()), 99);

    io_path_set_base(NULL);
    T_ASSERT_EQ_S(io_path_base(), "");
    return 0;
}

/* io_path_probe_cd_audio (retail FUN_004aef7e): on a layout with no CD
 * `wav` directory the chdir fails, so the probe's only observable
 * effects are the gate (→1) and the path buffer.  Assert both, and that
 * the buffer is byte-exact including the string-pool overrun tail. */
int test_io_path_probe_cd_audio(void)
{
    char orig[256];
    if (OS_GETCWD(orig, sizeof orig) == NULL) T_SKIP("getcwd unavailable");

    /* Anchor the revert at the current dir so the tail chdir is a
     * no-op rather than logging an error. */
    io_path_set_base(orig);

    /* Drive letter feeds buffer byte 0 (retail DAT_0053f200). */
    g_config.cd_drive = 'D';

    /* Sentinel the gate so the probe's write is observable. */
    engine_menu_input_set_sp_available(7);

    io_path_probe_cd_audio();

    /* Gate forced to 1 on every retail path. */
    T_ASSERT_EQ_I(engine_menu_input_sp_available(), 1);

    /* Byte-exact 40-byte buffer: "D:.\\wav\0" + pad + pooled residue. */
    static const unsigned char want[40] = {
        0x44,0x3a,0x2e,0x5c,0x77,0x61,0x76,0x00,  /* "D:.\\wav\0" */
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x73,0x77,0x6f,0x72,0x5f,0x70,0x32,0x2e,  /* "swor_p2." */
        0x77,0x61,0x76,0x00,                      /* "wav\0"     */
        0x70,0x75,                                /* "pu"        */
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    };
    T_ASSERT_EQ_I(memcmp(io_path_cd_audio_path(), want, sizeof want), 0);

    /* cwd reverted to the anchored base. */
    char now[256];
    T_ASSERT(OS_GETCWD(now, sizeof now) != NULL);
    T_ASSERT_EQ_S(now, orig);

    OS_CHDIR(orig);
    return 0;
}

/* The drive letter is patched into byte 0 from g_config.cd_drive. */
int test_io_path_probe_cd_audio_drive_letter(void)
{
    char orig[256];
    if (OS_GETCWD(orig, sizeof orig) == NULL) T_SKIP("getcwd unavailable");
    io_path_set_base(orig);

    g_config.cd_drive = 'E';
    io_path_probe_cd_audio();
    T_ASSERT_EQ_I((unsigned char)io_path_cd_audio_path()[0], 'E');
    T_ASSERT_EQ_S(io_path_cd_audio_path(), "E:.\\wav");

    OS_CHDIR(orig);
    return 0;
}
