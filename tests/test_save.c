/*
 * test_save.c — round-trip tests for src/save.c.
 *
 * The interesting tests need a retail .sav file and Castles.dat to
 * round-trip — we can't ship those in the repo.  Path discovery:
 *
 *   1. $OPENLORDS2_GAME_DIR (set by `nix develop`'s default flake)
 *   2. <repo>/vendor/original/  (the setup.sh symlink)
 *
 * If neither yields the expected files, the test SKIPs (return 2)
 * with a hint instead of failing.  The schema-shape sanity check
 * runs unconditionally — it only inspects the in-binary schema table.
 */

#include "t.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "save.h"

static int read_file(const char *path, uint8_t **out_buf, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return -1; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t *b = (uint8_t *)malloc((size_t)n);
    if (!b) { fclose(f); return -1; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(b); return -1; }
    *out_buf = b;
    *out_len = (size_t)n;
    return 0;
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Resolve the retail data directory: env first, then vendor symlink.
 * Writes into `dst` (cap PATH_MAX-ish); returns 1 on success. */
static int resolve_game_dir(char *dst, size_t cap)
{
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    if (env && *env) {
        snprintf(dst, cap, "%s", env);
        return 1;
    }
    snprintf(dst, cap, "%s/vendor/original", OPENLORDS2_ROOT);
    return 1;
}

/* ─── tests ──────────────────────────────────────────────────────── */

/* Sanity: the generated schema table has exactly 225 entries summing
 * to 267028 bytes.  This re-derives the canonical .sav header-byte
 * count from the in-binary table; if it ever drifts (e.g. the CSV
 * was edited but the generator wasn't re-run), this catches it. */
int test_save_schema_table_shape(void)
{
    uint64_t total = 0;
    int      count = 0;
    for (int i = 0; i < SAVE_SCHEMA_COUNT; ++i) {
        if (g_save_schema[i].size == 0) break;
        total += g_save_schema[i].size;
        count++;
    }
    T_ASSERT_EQ_I(count, SAVE_SCHEMA_COUNT);
    T_ASSERT_EQ_U(total, SAVE_SCHEMA_BYTES);
    T_ASSERT_EQ_U(SAVE_SCHEMA_BYTES + CASTLES_FILE_BYTES, SAVE_FILE_BYTES);
    return 0;
}

/* Sanity: the duplicate-VA aliasing was preserved.  Entries 32 and 48
 * both point at retail VA 0x0053f010; entries 216..224 all point at
 * 0x004eb2ac.  Verify by writing through one alias and reading back
 * through the other. */
int test_save_schema_aliasing_preserved(void)
{
    T_ASSERT(g_save_schema[32].ptr == g_save_schema[48].ptr);
    for (int i = 217; i <= 224; ++i)
        T_ASSERT(g_save_schema[216].ptr == g_save_schema[i].ptr);

    /* Round-trip a byte through the aliased slot to be sure. */
    *(uint32_t *)g_save_schema[32].ptr = 0xdeadbeef;
    T_ASSERT_EQ_U(*(uint32_t *)g_save_schema[48].ptr, 0xdeadbeef);
    *(uint32_t *)g_save_schema[216].ptr = 0xcafef00d;
    T_ASSERT_EQ_U(*(uint32_t *)g_save_schema[224].ptr, 0xcafef00d);
    return 0;
}

/* The big round-trip: load → write → byte-compare.
 *
 * Procedure:
 *   1. Read retail lastturn.sav into a buffer (golden).
 *   2. save_load(retail .sav, tmp_castles.dat) — populates globals,
 *      writes a Castles.dat copy.
 *   3. save_write(tmp_out.sav, tmp_castles.dat) — re-serializes.
 *   4. Compare tmp_out.sav bytes to the golden.
 *
 * The tmp_castles.dat shuttle is necessary because save_load
 * destination is a Castles.dat file (matches retail), and save_write
 * needs a Castles.dat source.  Using a tmp copy keeps the retail
 * install untouched. */
int test_save_round_trip_retail_lastturn(void)
{
    char gdir[1024];
    if (!resolve_game_dir(gdir, sizeof gdir))
        T_SKIP("cannot resolve game dir");

    char sav_path[1100];
    snprintf(sav_path, sizeof sav_path, "%s/lastturn.sav", gdir);
    if (!file_exists(sav_path))
        T_SKIP("retail .sav not found at %s", sav_path);

    /* Read the golden bytes. */
    uint8_t *golden = NULL;
    size_t   gold_n = 0;
    if (read_file(sav_path, &golden, &gold_n) != 0)
        T_FAIL("failed to read %s", sav_path);
    if (gold_n != SAVE_FILE_BYTES) {
        free(golden);
        T_FAIL("golden .sav size %zu != expected %d",
               gold_n, SAVE_FILE_BYTES);
    }

    /* Scratch paths in /tmp. */
    const char *tmp_castles = "/tmp/openlords2_test_castles.dat";
    const char *tmp_out     = "/tmp/openlords2_test_out.sav";

    if (save_load(sav_path, tmp_castles) != 0) {
        free(golden);
        T_FAIL("save_load failed");
    }
    if (save_write(tmp_out, tmp_castles) != 0) {
        free(golden);
        T_FAIL("save_write failed");
    }

    uint8_t *out = NULL;
    size_t   out_n = 0;
    if (read_file(tmp_out, &out, &out_n) != 0) {
        free(golden);
        T_FAIL("failed to read tmp_out");
    }
    if (out_n != gold_n) {
        free(golden); free(out);
        T_FAIL("round-trip size %zu != golden %zu", out_n, gold_n);
    }

    /* Locate the first differing byte for actionable error output. */
    size_t first_diff = (size_t)-1;
    for (size_t i = 0; i < gold_n; ++i) {
        if (golden[i] != out[i]) { first_diff = i; break; }
    }
    if (first_diff != (size_t)-1) {
        fprintf(stderr,
                "  diff at byte %zu: golden=0x%02x out=0x%02x\n",
                first_diff, golden[first_diff], out[first_diff]);
        free(golden); free(out);
        remove(tmp_castles); remove(tmp_out);
        T_FAIL("round-trip mismatch");
    }

    free(golden); free(out);
    remove(tmp_castles); remove(tmp_out);
    return 0;
}

/* Castles.dat helpers: castles_init_blank produces a 204800-byte
 * all-zero file; castles_write/read_scenario round-trip the 2 bytes
 * of each 8-B record. */
int test_castles_init_blank_makes_zero_file(void)
{
    const char *path = "/tmp/openlords2_test_blank.dat";
    if (castles_init_blank(path) != 0)
        T_FAIL("castles_init_blank failed");

    uint8_t *buf = NULL;
    size_t   n   = 0;
    if (read_file(path, &buf, &n) != 0)
        T_FAIL("read back blank file");
    T_ASSERT_EQ_U(n, CASTLES_FILE_BYTES);
    for (size_t i = 0; i < n; ++i) {
        if (buf[i] != 0) {
            free(buf); remove(path);
            T_FAIL("non-zero byte at %zu (0x%02x)", i, buf[i]);
        }
    }
    free(buf);
    remove(path);
    return 0;
}

/* write_scenario then read_scenario should reproduce the +1 and +3
 * bytes of every record.  The retail read path also applies fixups
 * based on the mem+7 byte and flags — we keep flags==0 and mem+7==0
 * here so no fixups trigger. */
int test_castles_scenario_round_trip_byte13(void)
{
    const char *path = "/tmp/openlords2_test_scenario.dat";

    /* Seed g_castles_state with a known pattern in the bytes that
     * touch disk (+1 and +3 of each record).  Zero the rest to avoid
     * triggering the read-side fixup branches. */
    for (size_t i = 0; i < sizeof g_castles_state; ++i) g_castles_state[i] = 0;
    for (int rec = 0; rec < 6400; ++rec) {
        g_castles_state[rec * 8 + 1] = (uint8_t)((rec * 7) & 0xff);
        g_castles_state[rec * 8 + 3] = (uint8_t)((rec * 13) & 0xff);
    }

    /* Need the file to exist for r+b mode. */
    if (castles_init_blank(path) != 0)
        T_FAIL("init_blank failed");

    if (castles_write_scenario(path, 5) != 0)
        T_FAIL("write_scenario failed");

    /* Wipe g_castles_state so the read isn't a no-op. */
    for (size_t i = 0; i < sizeof g_castles_state; ++i)
        g_castles_state[i] = 0xff;

    if (castles_read_scenario(path, 5) != 0)
        T_FAIL("read_scenario failed");

    for (int rec = 0; rec < 6400; ++rec) {
        uint8_t want1 = (uint8_t)((rec * 7) & 0xff);
        uint8_t want3 = (uint8_t)((rec * 13) & 0xff);
        if (g_castles_state[rec * 8 + 1] != want1)
            T_FAIL("rec %d +1: got 0x%02x want 0x%02x",
                   rec, g_castles_state[rec * 8 + 1], want1);
        if (g_castles_state[rec * 8 + 3] != want3)
            T_FAIL("rec %d +3: got 0x%02x want 0x%02x",
                   rec, g_castles_state[rec * 8 + 3], want3);
    }

    remove(path);
    return 0;
}

/* Verify the scenario-slot indexing: blocks 1..16 land at distinct
 * 12800-B offsets, and writing block N doesn't touch block N+1. */
int test_castles_scenario_independent_slots(void)
{
    const char *path = "/tmp/openlords2_test_slots.dat";
    if (castles_init_blank(path) != 0) T_FAIL("init_blank failed");

    /* Write a marker in record 0 of slot 3 only. */
    for (size_t i = 0; i < sizeof g_castles_state; ++i) g_castles_state[i] = 0;
    g_castles_state[1] = 0xab;
    g_castles_state[3] = 0xcd;
    if (castles_write_scenario(path, 3) != 0) T_FAIL("write_scenario failed");

    /* Read slot 3 directly from disk and confirm the layout. */
    FILE *f = fopen(path, "rb");
    T_ASSERT(f != NULL);
    fseek(f, (long)(2 * CASTLES_BLOCK_BYTES), SEEK_SET);  /* slot 3 = idx 2 */
    uint8_t hdr[4] = {0};
    size_t got = fread(hdr, 1, 4, f);
    fclose(f);
    T_ASSERT_EQ_U(got, 4);
    /* Pack convention: disk[0]=mem[+3], disk[1]=mem[+1]. */
    T_ASSERT_EQ_U(hdr[0], 0xcd);
    T_ASSERT_EQ_U(hdr[1], 0xab);

    /* Slot 4 should still be all-zero. */
    f = fopen(path, "rb");
    T_ASSERT(f != NULL);
    fseek(f, (long)(3 * CASTLES_BLOCK_BYTES), SEEK_SET);  /* slot 4 = idx 3 */
    got = fread(hdr, 1, 4, f);
    fclose(f);
    T_ASSERT_EQ_U(got, 4);
    for (int i = 0; i < 4; ++i)
        T_ASSERT_EQ_U(hdr[i], 0);

    remove(path);
    return 0;
}

/* The read-side fixup: when flags & 0x02, the engine forces mem+7=9,
 * mem+4=0, and rewrites mem+2 to (mem+2 & 0xe3) | 4.  Verify by
 * planting flags=0x02 on disk and a non-trivial mem+2 in memory. */
int test_castles_read_fixup_flag_bit1(void)
{
    const char *path = "/tmp/openlords2_test_fixup.dat";
    if (castles_init_blank(path) != 0) T_FAIL("init_blank failed");

    /* Plant flags=0x02 in record 0 on disk by writing through
     * castles_state then write_scenario. */
    for (size_t i = 0; i < sizeof g_castles_state; ++i) g_castles_state[i] = 0;
    g_castles_state[1] = 0x02;
    g_castles_state[3] = 0x55;
    if (castles_write_scenario(path, 1) != 0) T_FAIL("write_scenario failed");

    /* Set mem+2 to a value that will be reshaped (e.g. 0xff →
     * 0xff & 0xe3 | 4 = 0xe7).  Set mem+4 and mem+7 to non-zero
     * sentinels so we can see the fixup zero/force them. */
    g_castles_state[0 + 2] = 0xff;
    g_castles_state[0 + 4] = 0xaa;
    g_castles_state[0 + 7] = 0xbb;

    if (castles_read_scenario(path, 1) != 0) T_FAIL("read_scenario failed");

    T_ASSERT_EQ_U(g_castles_state[0 + 1], 0x02);    /* preserved from disk */
    T_ASSERT_EQ_U(g_castles_state[0 + 3], 0x55);    /* preserved from disk */
    T_ASSERT_EQ_U(g_castles_state[0 + 7], 9);       /* forced */
    T_ASSERT_EQ_U(g_castles_state[0 + 4], 0);       /* zeroed */
    T_ASSERT_EQ_U(g_castles_state[0 + 2], 0xe7);    /* reshaped */

    remove(path);
    return 0;
}
