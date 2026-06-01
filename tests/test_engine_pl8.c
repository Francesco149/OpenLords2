/*
 * test_engine_pl8.c — deterministic byte-match validation of the
 * strategic-map .pl8 sprite-bank loader (src/engine_pl8.c, port of
 * retail FUN_004984dc).
 *
 * The loader mallocs + raw-reads each .pl8 bank named in the generated
 * table (src/gen/pl8_table.h) for the current season / view-mode.  We
 * drive it with the SAME state retail held when we captured the live
 * map blobs — season 1d (s_slot_0057c934 = 4), normal view
 * (s_slot_0057cb18 = 0), table offset 24 — then compare each loaded
 * bank, byte for byte, against the captured retail in-memory blobs in
 *   runs/capture/strat-terrain1/dump/
 * (terr_blob0..4 = banks 0..4; blob_primary/alt/overlay = banks
 * 5/6/7).  A match proves the table (filenames + sizes), the season/
 * view offset arithmetic, AND the bank→slot ladder all reproduce
 * retail's loaded sprite data exactly — the pivot from the harness's
 * captured blobs to the port's own native load.
 *
 * The captured dump blobs are heap OVER-reads (the capture estimated a
 * larger window than the true bank size), so each terr_blob.bin is a
 * superset; we compare the first `bank_size` bytes (the malloc/read
 * length retail used), which IS the whole .pl8 file content.
 *
 * Requires OPENLORDS2_GAME_DIR (the retail .pl8 files) and the capture
 * dump; SKIPs cleanly when either is absent.
 *
 * Provides a faithful POSIX io_read_file_at here (retail's
 * __open/__lseek/__read leaf FUN_004af8d9): the production io.c is
 * Win32-only and excluded from the host test binary.
 */

#include "t.h"
#include "engine_pl8.h"
#include "gen/pl8_table.h"
#include "save_schema_globals.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#  include <io.h>
#  define OS_OPEN  _open
#  define OS_READ  _read
#  define OS_LSEEK _lseek
#  define OS_CLOSE _close
#  ifndef O_BINARY
#    define O_BINARY 0
#  endif
#else
#  include <unistd.h>
#  define OS_OPEN  open
#  define OS_READ  read
#  define OS_LSEEK lseek
#  define OS_CLOSE close
#  define O_BINARY 0
#endif

/* Faithful port of the file-read leaf FUN_004af8d9 (tier 1 only). */
int io_read_file_at(const char *path, void *buf, size_t len, long offset)
{
    if (!path || !buf || len == 0) return 0;
    int fd = OS_OPEN(path, O_RDONLY | O_BINARY);
    if (fd < 0) return 0;
    int got = 0;
    if (OS_LSEEK(fd, offset, SEEK_SET) != (long)-1) {
        long n = (long)OS_READ(fd, buf, (unsigned)len);
        if (n > 0) got = (int)n;
    }
    OS_CLOSE(fd);
    return got;
}

static uint8_t *slurp(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t rd = fread(b, 1, (size_t)n, f);
    fclose(f);
    *len_out = rd;
    return b;
}

/* slot → captured dump filename (the live retail in-memory blob). */
static const char *DUMP_NAME[ENGINE_PL8_BANK_COUNT] = {
    "terr_blob0.bin", "terr_blob1.bin", "terr_blob2.bin",
    "terr_blob3.bin", "terr_blob4.bin",
    "blob_primary.bin", "blob_alt.bin", "blob_overlay.bin",
};

int test_engine_pl8_banks_match_captured_blobs(void)
{
    const char *gamedir = getenv("OPENLORDS2_GAME_DIR");
    if (!gamedir || !gamedir[0])
        T_SKIP("OPENLORDS2_GAME_DIR unset (need retail .pl8 files)");

    char dumpdir[1024];
    snprintf(dumpdir, sizeof dumpdir,
             "%s/runs/capture/strat-terrain1/dump", OPENLORDS2_ROOT);
    struct stat st;
    char probe[1280];
    snprintf(probe, sizeof probe, "%s/terr_blob0.bin", dumpdir);
    if (stat(probe, &st) != 0)
        T_SKIP("capture dump missing (%s)", dumpdir);

    /* Drive the same state the capture held: season 1d, normal view. */
    s_slot_0057c934 = 4;   /* season → offset 24 (the *1d block) */
    s_slot_0057cb18 = 0;   /* view-mode normal */

    int ok = engine_pl8_load_strategic_banks(gamedir);
    if (!ok) {
        engine_pl8_free_strategic_banks();
        T_FAIL("engine_pl8_load_strategic_banks failed (missing/short "
               ".pl8 files under %s?)", gamedir);
    }

    int rc = 0;
    for (int i = 0; i < ENGINE_PL8_BANK_COUNT; i++) {
        const uint8_t *bank = engine_pl8_bank(i);
        size_t bsz = engine_pl8_bank_size(i);
        if (!bank || bsz == 0) {
            fprintf(stderr, "  fail: bank %d not loaded\n", i);
            rc = 1; continue;
        }
        char path[1280];
        snprintf(path, sizeof path, "%s/%s", dumpdir, DUMP_NAME[i]);
        size_t dl = 0;
        uint8_t *dump = slurp(path, &dl);
        if (!dump) {
            fprintf(stderr, "  fail: cannot read %s\n", path);
            rc = 1; continue;
        }
        /* The loaded buffer is malloc(table_size); the .pl8 file is a
         * little shorter, so only the file-length prefix is valid data
         * (the tail is malloc slack on both sides).  Compare the bytes
         * the read actually filled, bounded by the dump window. */
        size_t cmp = bsz < dl ? bsz : dl;
        size_t first_diff = (size_t)-1;
        for (size_t k = 0; k < cmp; k++) {
            if (bank[k] != dump[k]) { first_diff = k; break; }
        }
        if (first_diff != (size_t)-1) {
            fprintf(stderr, "  fail: bank %d (%s) differs at byte %zu "
                    "(port 0x%02x vs retail 0x%02x), cmp=%zu bsz=%zu "
                    "dump=%zu\n", i, DUMP_NAME[i], first_diff,
                    bank[first_diff], dump[first_diff], cmp, bsz, dl);
            rc = 1;
        } else {
            fprintf(stderr, "  ok: bank %d (%s) matches %zu bytes\n",
                    i, DUMP_NAME[i], cmp);
        }
        free(dump);
    }

    engine_pl8_free_strategic_banks();
    return rc;
}

/* Offset arithmetic: season/view-mode → table base index, exercised
 * without touching the filesystem (pure table indexing sanity). */
int test_engine_pl8_season_offset(void)
{
    /* season 1 (1a) view 0 → offset 0 → bank 0 = base1a.pl8 */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[0].name, "base1a.pl8");
    /* season 4 (1d) view 0 → offset 24 → bank 0 = base1d.pl8 */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[0 + 24].name, "base1d.pl8");
    /* banks 5/6/7 are season-invariant across the 1d block */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[5 + 24].name, "sprite1a.pl8");
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[6 + 24].name, "sprite1b.pl8");
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[7 + 24].name, "flags1a.pl8");
    /* view-mode 2 → offset 0x20 → bank 0 = base2a.pl8 */
    T_ASSERT_EQ_S(PL8_STRATEGIC_TABLE[0 + 0x20].name, "base2a.pl8");
    return 0;
}
