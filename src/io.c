/*
 * OpenLords2 — minimal file-IO helper.  Port of the *first tier*
 * of retail `FUN_004af8d9 @ 0x4af8d9`.  See io.h for the deferred
 * retry chain.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "io.h"

int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{
    if (!path || !buf || len == 0) return 0;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int got = 0;
    if (SetFilePointer(h, (LONG)offset, NULL, FILE_BEGIN)
            != INVALID_SET_FILE_POINTER) {
        DWORD n = 0;
        if (ReadFile(h, buf, (DWORD)len, &n, NULL))
            got = (int)n;
    }
    CloseHandle(h);
    return got;
}
