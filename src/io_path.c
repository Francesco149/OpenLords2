/*
 * OpenLords2 — asset base-directory anchor + revert.  See io_path.h.
 *
 * Port of retail FUN_004af502 (0x4af502) plus the DAT_0058fe30 base
 * capture that the WinMain owner 0x40e9ab performs.
 */

#include "io_path.h"
#include "engine_menu_input.h"   /* CD-audio gate setter (retail DAT_005c9a74) */
#include "strutil.h"             /* strutil_bounded_copy (retail FUN_00401136) */
#include "config.h"              /* g_config.cd_drive (retail DAT_0053f200)    */

#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <io.h>        /* _open / _close / _filelength            */
#  include <fcntl.h>     /* _O_RDONLY / _O_BINARY                   */
#  define OS_CHDIR  _chdir
#  define OS_GETCWD _getcwd
#  define OS_OPEN_RO_BIN(p) _open((p), _O_RDONLY | _O_BINARY)
#  define OS_CLOSE   _close
#  define OS_FILELEN(fd) _filelength(fd)
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  define OS_CHDIR  chdir
#  define OS_GETCWD getcwd
#  define OS_OPEN_RO_BIN(p) open((p), O_RDONLY)   /* O_BINARY is a no-op on POSIX */
#  define OS_CLOSE   close
static long os_filelen(int fd)
{
    struct stat st;
    return (fstat(fd, &st) == 0) ? (long)st.st_size : -1;
}
#  define OS_FILELEN(fd) os_filelen(fd)
#endif

/* Drop-in dev log (defined in main.c; stubbed in the test driver). */
extern void log_line(const char *fmt, ...);

/* retail DAT_0058fe30 is filled with getcwd(buf, 100); the cwd scratch
 * DAT_004e6910 in FUN_004af502 is likewise a 100-byte buffer.  Mirror
 * that width exactly. */
#define IO_PATH_CAP 100

static char g_base_dir[IO_PATH_CAP];

void io_path_set_base(const char *dir)
{
    if (dir == NULL) {
        g_base_dir[0] = '\0';
        return;
    }
    size_t n = strlen(dir);
    if (n >= sizeof g_base_dir)
        n = sizeof g_base_dir - 1;
    memcpy(g_base_dir, dir, n);
    g_base_dir[n] = '\0';
}

int io_path_capture_base(void)
{
    if (OS_GETCWD(g_base_dir, sizeof g_base_dir) == NULL) {
        g_base_dir[0] = '\0';
        return -1;
    }
    return 0;
}

const char *io_path_base(void)
{
    return g_base_dir;
}

int io_path_chdir_base(void)
{
    int rc = OS_CHDIR(g_base_dir);
    if (rc != 0) {
        /* retail: __getcwd(&DAT_004e6910, 100);
         *         0x4afab9(0x4df4a8, &DAT_004e6910, 0)
         * — a status.txt line "ERR:CHD  not reserted is now <cwd>"
         * (string verbatim, sic).  The status.txt logger is unported;
         * route through the dev log instead. */
        char cwd[IO_PATH_CAP];
        if (OS_GETCWD(cwd, sizeof cwd) == NULL)
            cwd[0] = '\0';
        log_line("ERR:CHD  not reserted is now %s", cwd);
    }
    return rc;
}

/* ── seasonal CD-audio probe (retail FUN_004aef7e @ 0x4aef7e) ─────── */

/* retail DAT_005cd490 — shared scratch path buffer.  FUN_004aef7e
 * zeroes its first 0x28 (40) bytes, copies a 30-byte template into it,
 * then patches byte 0 with the CD drive letter.  The asset-chdir
 * helper 0x4af116 (deferred) reuses the same global, so it is owned
 * here in the path module rather than privately by the probe. */
#define CD_PATH_CAP 40

static char g_cd_audio_path[CD_PATH_CAP];

/* The 30 bytes retail copies via FUN_00401136(0x4df3ac, 0x5cd490, 0x1e).
 * Source rdata is "c:.\\wav\0" followed by linker pad and the *next*
 * pooled string literals — the copy length (0x1e = 30) overruns the
 * 8-byte "c:.\\wav" literal into the adjacent "swor_p2.wav\0" and the
 * first two bytes of "pumkin.wav" ("pu").  Those trailing 22 bytes are
 * string-table residue, not path data (the chdir/open below stop at the
 * NUL at offset 7), but they ARE the byte-exact contents of DAT_005cd490
 * after the copy, so they are reproduced verbatim.  See engine-quirks
 * "FUN_004aef7e path-template string-pool overrun". */
static const unsigned char CD_PATH_TEMPLATE[30] = {
    0x63,0x3a,0x2e,0x5c,0x77,0x61,0x76,0x00,  /* "c:.\\wav\0" */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* pad */
    0x73,0x77,0x6f,0x72,0x5f,0x70,0x32,0x2e,  /* "swor_p2." */
    0x77,0x61,0x76,0x00,                      /* "wav\0" */
    0x70,0x75,                                /* "pu" (head of "pumkin.wav") */
};

/* CD wav files retail probes once the chdir into the wav dir succeeds. */
static const char CD_WAV_SWORD[]   = "swor_p2.wav";
static const char CD_WAV_PUMKIN[]  = "pumkin.wav";
static const char CD_WAV_PUMKIN2[] = "pumkin2.wav";

#define CD_FULL_AUDIO_BYTES 151000000L   /* retail 0x90013c0 size threshold */

void io_path_probe_cd_audio(void)
{
    /* gate = 1.  retail stores 1 unconditionally here, repeats it in the
     * dead `DAT_0053f684 == 1` branch, and again in both ≥-threshold
     * file branches — every store is the same value, so a single write
     * captures the entire observable gate effect. */
    engine_menu_input_set_sp_available(1);

    /* Build "<drive>:.\\wav" into the shared scratch buffer: zero 40
     * bytes, copy the 30-byte template (string-pool overrun and all),
     * then overwrite byte 0 with the CD drive letter (DAT_0053f200). */
    memset(g_cd_audio_path, 0, sizeof g_cd_audio_path);
    strutil_bounded_copy(g_cd_audio_path, CD_PATH_TEMPLATE,
                         (int)sizeof CD_PATH_TEMPLATE);
    g_cd_audio_path[0] = g_config.cd_drive;

    /* Probe the CD wav directory.  On the flat drop-in / host layout the
     * chdir fails and the whole block is skipped (gate already 1). */
    if (OS_CHDIR(g_cd_audio_path) != -1) {
        int fd = OS_OPEN_RO_BIN(CD_WAV_SWORD);
        if (fd != -1) {
            OS_CLOSE(fd);
            fd = OS_OPEN_RO_BIN(CD_WAV_PUMKIN);
            if (fd == -1) {
                fd = OS_OPEN_RO_BIN(CD_WAV_PUMKIN2);
                if (fd != -1) {
                    long len = OS_FILELEN(fd);
                    OS_CLOSE(fd);
                    if (len >= CD_FULL_AUDIO_BYTES)
                        engine_menu_input_set_sp_available(1);
                }
            } else {
                long len = OS_FILELEN(fd);
                OS_CLOSE(fd);
                if (len >= CD_FULL_AUDIO_BYTES)
                    engine_menu_input_set_sp_available(1);
            }
        }
    }

    /* retail tail-calls FUN_004af502 to restore the launch directory. */
    io_path_chdir_base();
}

const char *io_path_cd_audio_path(void)
{
    return g_cd_audio_path;
}
