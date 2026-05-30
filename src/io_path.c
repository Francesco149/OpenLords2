/*
 * OpenLords2 — asset base-directory anchor + revert.  See io_path.h.
 *
 * Port of retail FUN_004af502 (0x4af502) plus the DAT_0058fe30 base
 * capture that the WinMain owner 0x40e9ab performs.
 */

#include "io_path.h"

#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  define OS_CHDIR  _chdir
#  define OS_GETCWD _getcwd
#else
#  include <unistd.h>
#  define OS_CHDIR  chdir
#  define OS_GETCWD getcwd
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
