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

#include "engine_tile.h"
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
 * based on the mem+7 byte and flags — to keep none of them firing we
 * keep flags bits 0+1 clear (no expansion/reshape) and let the wipe
 * leave mem+7 at 0xff before the read (no flags==0 branch). */
int test_castles_scenario_round_trip_byte13(void)
{
    const char *path = "/tmp/openlords2_test_scenario.dat";

    /* Seed g_castles_state with a known pattern in the bytes that
     * touch disk (+1 and +3 of each record).  Mask off bits 0+1 of the
     * +1 byte so the read fixup branches stay dormant. */
    for (size_t i = 0; i < sizeof g_castles_state; ++i) g_castles_state[i] = 0;
    for (int rec = 0; rec < 6400; ++rec) {
        g_castles_state[rec * 8 + 1] = (uint8_t)((rec * 7) & 0xfc);
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
        uint8_t want1 = (uint8_t)((rec * 7) & 0xfc);
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

/* The read-side fixup: when flags & 0x01 (and bit 1 is clear), the
 * engine calls engine_tile_expand_footprint_at on the anchor cell,
 * laying down the 3x3 building footprint and recording the offset in
 * g_footprint_list.  Verify by planting flags=0x01 with anchor 'X' on
 * disk: the read path should overwrite the 3x3 cells starting at the
 * anchor with the 'X'/'Y'/'Z'/'`'/'a'/'b'/'h'/'i'/'j' glyph row, mark
 * the center cell's +2 with 0x80, and stamp the slot's byte offset in
 * footprint_list[0][0]. */
int test_castles_read_fixup_flag_bit0(void)
{
    const char *path = "/tmp/openlords2_test_fixup_flag0.dat";
    if (castles_init_blank(path) != 0) T_FAIL("init_blank failed");

    /* Place the anchor at a record that's safely interior (col 5,
     * row 5 → byte offset 5*8 + 5*640 = 3240) so the 3x3 footprint
     * stays inside the grid. */
    const int anchor_byte = 5 * ENGINE_TILE_CELL_BYTES
                            + 5 * ENGINE_TILE_ROW_BYTES;

    for (size_t i = 0; i < sizeof g_castles_state; ++i) g_castles_state[i] = 0;
    g_castles_state[anchor_byte + 1] = 0x01;   /* flags: trigger expansion */
    g_castles_state[anchor_byte + 3] = 'X';    /* recognized anchor */
    if (castles_write_scenario(path, 1) != 0) T_FAIL("write_scenario failed");

    /* Wipe + reset runtime scratches so the test's pre-state is clean. */
    for (size_t i = 0; i < sizeof g_castles_state; ++i) g_castles_state[i] = 0;
    for (size_t i = 0; i < 6400; ++i) g_path_scratch [i] = 0;
    for (size_t i = 0; i < 6400; ++i) g_stage_scratch[i] = 0;
    for (size_t i = 0; i < sizeof g_footprint_storage_160; ++i)
        g_footprint_storage_160[i] = 0;

    if (castles_read_scenario(path, 1) != 0) T_FAIL("read_scenario failed");

    /* IMPORTANT — the disk-load loop overwrites +1 and +3 of every
     * record as it advances.  The expansion's writes to +1/+3 of cells
     * with byte_offset > anchor_byte are therefore CLOBBERED by later
     * iterations.  Only writes to +2/+4 (and the path/stage scratches
     * + footprint_list — all separate state) survive intact.  See the
     * "Castle load loop clobbers expansion +1/+3 writes" entry in
     * docs/findings/engine-quirks.md.
     *
     * Below we verify only the observables that survive that clobber. */

    /* Anchor cell: +1 starts at 0x01 from the disk read, then gets the
     * corner-neighbor flag bit ORed in by the neighbor-flag helper (the
     * anchor IS the upper-left corner of the 3x3 center, so it picks up
     * the 0x10 bit).  +3 = 'X' (first glyph of the 'X'-row footprint
     * table is the anchor char itself, so the value is preserved).
     * Anchor's +1/+3 is the ONLY cell whose +1/+3 expansion writes
     * survive — the load loop's disk write at this offset happens
     * BEFORE the expansion, not after. */
    T_ASSERT_EQ_U(g_castles_state[anchor_byte + 1], 0x11);
    T_ASSERT_EQ_U(g_castles_state[anchor_byte + 3], 'X');

    /* Anchor cell +2: set by the 3x3 walk to ((0 & 0xe3) | 0x04) = 0x04.
     * +2 is never touched by the load loop, so this survives. */
    T_ASSERT_EQ_U(g_castles_state[anchor_byte + 2], 0x04);

    /* Center cell of the 3x3 (one row + one col down from anchor): +2
     * picks up the center marker bit 0x80.  +3 is clobbered back to 0
     * by the load-loop iter at this byte offset. */
    int center = anchor_byte + ENGINE_TILE_CELL_BYTES + ENGINE_TILE_ROW_BYTES;
    T_ASSERT(g_castles_state[center + 2] & 0x80);

    /* footprint_list[0][0] holds the anchor byte offset — written by
     * the stage-6 push helper into a separate global, so unaffected by
     * the load-loop clobber. */
    T_ASSERT_EQ_U(g_footprint_list[0][0], anchor_byte);

    /* Stage scratch refresh: the orient-4 ('X' faces down) neighbor-flag
     * helper stamps +4 = 1 onto cell (col 6, row 5) (the helper writes
     * to neighbors of the 3x3 CENTER, which is (col 6, row 6)).  Cell
     * index = 6 + 5*80 = 406.  +4 is never touched by the load loop, so
     * this survives. */
    T_ASSERT_EQ_U(g_stage_scratch[6 + 5 * 80], 1);

    /* The orient-4 door-neighbor write stamps +4 = 2 onto (col 6, row 7)
     * — cell 6 + 7*80 = 566 — in stage_scratch.  +4 is never touched
     * by the load loop, so this survives. */
    T_ASSERT_EQ_U(g_stage_scratch[6 + 7 * 80], 2);

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
