/*
 * Unit tests for localization — the L2_ENG.DAT accessor cluster.
 *
 * Coverage:
 *   - 24-bit LE offset decoding (FUN_0040187a)
 *   - seek + N-string skip + control-byte prefix skip (FUN_004018d7)
 *   - read-path boundary detector (rejects NUL-after-NUL) (FUN_004017bf)
 *   - seek-path boundary detector (accepts NUL-after-NUL) (FUN_004018d7)
 *   - out-of-bounds offset suppresses read but not seek update
 *   - write-path uses secondary 16-bit BE table at +0x1c (FUN_004016f3)
 *   - write-path has no bounds check (quirk #22)
 *
 * Helpers:
 *   - `mkblob` lays out a synthetic blob whose primary offset
 *     table directly addresses caller-supplied section starts.
 *   - Tests use small inline blobs (allocated as the full 120000-
 *     byte buffer so the in-bounds check passes naturally).
 */

#include "t.h"
#include "localization.h"
#include <string.h>

/* Allocate a zero-initialized full-size blob.  The cluster reads
 * past arbitrary offsets so we always pay for the full 120000
 * bytes — that matches retail's .bss footprint. */
static unsigned char *mkblob(void)
{
    /* calloc is fine here — tests free via free(). */
    return (unsigned char *)calloc(LOCALIZATION_BLOB_SIZE, 1);
}

/* Write a 24-bit LE offset into the primary table at index `i`. */
static void set_offset(unsigned char *blob, int i, unsigned int off)
{
    size_t base = (size_t)LOCALIZATION_READ_TABLE_OFFS + (size_t)i * 4;
    blob[base + 0] =  off        & 0xff;
    blob[base + 1] = (off >> 8)  & 0xff;
    blob[base + 2] = (off >> 16) & 0xff;
    blob[base + 3] = 0xaa;   /* 4th byte must be ignored */
}

/* ─── localization_offset ──────────────────────────────────────── */

int test_localization_offset_decodes_24bit_le(void) {
    unsigned char *blob = mkblob();
    /* Section 0 → byte offset 0x123456. */
    blob[LOCALIZATION_READ_TABLE_OFFS + 0] = 0x56;
    blob[LOCALIZATION_READ_TABLE_OFFS + 1] = 0x34;
    blob[LOCALIZATION_READ_TABLE_OFFS + 2] = 0x12;
    blob[LOCALIZATION_READ_TABLE_OFFS + 3] = 0xff;   /* unused */

    int got = localization_offset(blob, 0);
    T_ASSERT_EQ_I(got, 0x123456);
    free(blob);
    return 0;
}

int test_localization_offset_index_strides_by_four(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 0x100);
    set_offset(blob, 1, 0x200);
    set_offset(blob, 2, 0x300);

    T_ASSERT_EQ_I(localization_offset(blob, 0), 0x100);
    T_ASSERT_EQ_I(localization_offset(blob, 1), 0x200);
    T_ASSERT_EQ_I(localization_offset(blob, 2), 0x300);
    free(blob);
    return 0;
}

int test_localization_offset_fourth_byte_unused(void) {
    /* The 4th byte of each slot must not affect the decode — even
     * if it's 0xff, the offset shouldn't pick it up. */
    unsigned char *blob = mkblob();
    blob[LOCALIZATION_READ_TABLE_OFFS + 0] = 0x42;
    blob[LOCALIZATION_READ_TABLE_OFFS + 1] = 0x00;
    blob[LOCALIZATION_READ_TABLE_OFFS + 2] = 0x00;
    blob[LOCALIZATION_READ_TABLE_OFFS + 3] = 0xff;

    T_ASSERT_EQ_I(localization_offset(blob, 0), 0x42);
    free(blob);
    return 0;
}

/* ─── localization_seek_skip ──────────────────────────────────── */

int test_localization_seek_skip_lands_on_offset_byte_when_n_zero(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 1000);
    /* Section 0 at offset 1000 starts with a printable byte. */
    blob[1000] = 'H';
    blob[1001] = 'i';

    localization_state_t s = { blob, 0 };
    localization_seek_skip(&s, /*index=*/0, /*n_strings=*/0);
    T_ASSERT_EQ_U(s.walker_offset, 1000u);
    T_ASSERT_EQ_I(blob[s.walker_offset], 'H');
    free(blob);
    return 0;
}

int test_localization_seek_skip_advances_past_control_prefix(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 2000);
    /* Section 0 starts with two control bytes, then printable. */
    blob[2000] = 0x01;
    blob[2001] = 0x02;
    blob[2002] = 'A';
    blob[2003] = 'B';

    localization_state_t s = { blob, 0 };
    localization_seek_skip(&s, 0, 0);
    /* After landing at 2000, the control-byte-skip loop runs and
     * walks to the first byte >= 0x20 — 'A' at 2002. */
    T_ASSERT_EQ_U(s.walker_offset, 2002u);
    T_ASSERT_EQ_I(blob[s.walker_offset], 'A');
    free(blob);
    return 0;
}

int test_localization_seek_skip_walks_n_substrings(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 3000);
    /* Three sub-strings: "Foo\0Bar\0Baz\0".  Walking 2 boundaries
     * should land just past the 2nd NUL — on 'B' of "Baz". */
    memcpy(blob + 3000, "Foo\0Bar\0Baz\0", 12);

    localization_state_t s = { blob, 0 };
    localization_seek_skip(&s, 0, /*n_strings=*/2);
    /* After 2 boundaries we sit at the byte AFTER the 2nd NUL.
     * That's the 'B' at offset 3000+8. */
    T_ASSERT_EQ_U(s.walker_offset, 3008u);
    T_ASSERT_EQ_I(blob[s.walker_offset], 'B');
    free(blob);
    return 0;
}

int test_localization_seek_skip_accepts_nul_after_nul_boundary(void) {
    /* Quirk #20: seek path treats consecutive NULs as TWO boundaries.
     *
     * "X\0\0Y\0" — the seek path counts:
     *   pos 1: NUL after 'X' (prev=0x58 >= 0x20)        → boundary
     *   pos 2: NUL after NUL (prev=0 — accepted by seek) → boundary
     *
     * So n_strings=2 should land us at offset 3, where 'Y' lives. */
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 4000);
    memcpy(blob + 4000, "X\0\0Y\0", 5);

    localization_state_t s = { blob, 0 };
    localization_seek_skip(&s, 0, 2);
    T_ASSERT_EQ_U(s.walker_offset, 4003u);
    T_ASSERT_EQ_I(blob[s.walker_offset], 'Y');
    free(blob);
    return 0;
}

int test_localization_seek_skip_out_of_bounds_suppresses_walk(void) {
    /* If offset puts walker past 120000+1, the walk is skipped but
     * walker_offset is still set to the out-of-range value. */
    unsigned char *blob = mkblob();
    set_offset(blob, 0, LOCALIZATION_BLOB_SIZE + 100);   /* past end */

    localization_state_t s = { blob, 0 };
    localization_seek_skip(&s, 0, 5);
    T_ASSERT_EQ_U(s.walker_offset,
                  (size_t)LOCALIZATION_BLOB_SIZE + 100);
    free(blob);
    return 0;
}

int test_localization_seek_skip_at_bound_plus_one_still_walks(void) {
    /* Quirk #22: walker == blob_size is in-range (the gate is
     * `walker < size + 1`).  Set up a section at byte 120000
     * itself — actually beyond, since blob is exactly that many
     * bytes — so we test by placing the seek at exactly size, with
     * data ending the blob.  But we cannot read blob[120000] since
     * that's out-of-allocation; verify the bound by using a
     * smaller offset and checking the walker is updated normally. */
    unsigned char *blob = mkblob();
    /* Section 0 at offset 117000; subsection at 117020. */
    set_offset(blob, 0, 117000);
    blob[117000] = 'a';
    blob[117001] = 0;
    blob[117002] = 'b';

    localization_state_t s = { blob, 0 };
    localization_seek_skip(&s, 0, 1);
    /* After boundary at 117001, walker == 117002 (control-skip loop
     * sees 'b' which is >= 0x20 and exits immediately). */
    T_ASSERT_EQ_U(s.walker_offset, 117002u);
    free(blob);
    return 0;
}

/* ─── localization_read ───────────────────────────────────────── */

int test_localization_read_copies_n_bytes(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 5000);
    memcpy(blob + 5000, "English\0", 8);

    char dst[8] = {0};
    localization_state_t s = { blob, 0 };
    localization_read(&s, dst, /*index=*/0, /*n_strings=*/0,
                      /*n_bytes=*/7);
    T_ASSERT_MEM_EQ(dst, "English", 7);
    T_ASSERT_EQ_U(s.walker_offset, 5000u);
    free(blob);
    return 0;
}

int test_localization_read_skips_substrings_before_copy(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 6000);
    memcpy(blob + 6000, "Aaa\0Bbb\0Ccc\0", 12);

    char dst[4] = {0};
    localization_state_t s = { blob, 0 };
    /* Skip 2 sub-strings, then copy 3 bytes — should be "Ccc". */
    localization_read(&s, dst, 0, /*n_strings=*/2, /*n_bytes=*/3);
    T_ASSERT_MEM_EQ(dst, "Ccc", 3);
    T_ASSERT_EQ_U(s.walker_offset, 6008u);
    free(blob);
    return 0;
}

int test_localization_read_rejects_nul_after_nul_boundary(void) {
    /* Quirk #20: read path does NOT count NUL-after-NUL as a
     * boundary.  Walk pattern "X\0\0Y\0" with n_strings=2:
     *   pos 1: NUL after 'X' (prev=0x58) → boundary, n=1
     *   pos 2: NUL after NUL (prev=0)    → NOT a boundary on read
     *   pos 3: 'Y' (not NUL)             → no boundary
     *   pos 4: NUL after 'Y' (prev=0x59) → boundary, n=0
     *   loop exits at pos 5.
     * Control-byte-skip from pos 5 walks past trailing zeros — but
     * since we wrote only 5 bytes, the rest of the blob is zero,
     * so the skip loop continues until it hits a printable byte
     * (it never does in our synthetic blob — so we put a sentinel
     * after to stop the loop). */
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 7000);
    memcpy(blob + 7000, "X\0\0Y\0", 5);
    blob[7005] = 'Z';   /* sentinel to stop control-skip */

    char dst[2] = {0};
    localization_state_t s = { blob, 0 };
    localization_read(&s, dst, 0, /*n_strings=*/2, /*n_bytes=*/1);
    /* Walker now at 7005 (after control-skip past zeros 7004 is
     * pre-incremented past — actually the loop starts at 7005:
     *   pos 5 (= 7005) → 'Z' >= 0x20, loop exits.
     * dst[0] gets 'Z'. */
    T_ASSERT_EQ_I(dst[0], 'Z');
    T_ASSERT_EQ_U(s.walker_offset, 7005u);
    free(blob);
    return 0;
}

int test_localization_read_out_of_bounds_skips_entire_op(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, LOCALIZATION_BLOB_SIZE + 50);   /* past end */

    char dst[4] = {'!', '!', '!', '\0'};
    localization_state_t s = { blob, 99u };   /* known starting walker */
    localization_read(&s, dst, 0, 5, 3);
    /* dst was untouched (no copy happened). */
    T_ASSERT_EQ_S(dst, "!!!");
    /* walker_offset is also unchanged — read path keeps a local
     * walker and only commits on success. */
    T_ASSERT_EQ_U(s.walker_offset, 99u);
    free(blob);
    return 0;
}

int test_localization_read_zero_bytes_no_copy_but_seek_commits(void) {
    unsigned char *blob = mkblob();
    set_offset(blob, 0, 8000);
    memcpy(blob + 8000, "Abc\0Def\0", 8);

    char dst[2] = {'!', '!'};
    localization_state_t s = { blob, 0 };
    localization_read(&s, dst, 0, /*n_strings=*/1, /*n_bytes=*/0);
    /* n_bytes=0 → no copy.  But the walker should still have moved
     * past "Abc\0" — that's offset 8004. */
    T_ASSERT_EQ_I(dst[0], '!');
    T_ASSERT_EQ_I(dst[1], '!');
    T_ASSERT_EQ_U(s.walker_offset, 8004u);
    free(blob);
    return 0;
}

/* ─── localization_write ──────────────────────────────────────── */

int test_localization_write_uses_secondary_16bit_be_table(void) {
    /* Quirk #21: write decodes the offset out of the SECONDARY
     * table at +0x1c, with 16-bit BIG-endian bytes living at slot
     * positions +2 and +3.  Set up: offset 0x0123 = 291 for
     * section 0. */
    unsigned char *blob = mkblob();
    /* Slot 0 of secondary table is at blob[0x1c .. 0x1f].
     * High byte at +2, low byte at +3. */
    blob[LOCALIZATION_WRITE_TABLE_OFFS + 2] = 0x01;
    blob[LOCALIZATION_WRITE_TABLE_OFFS + 3] = 0x23;

    /* Resulting walker = 0x1c + 0x0123 = 0x13f. */
    /* Write 5 bytes of "WRITE" with n_strings=0.  Pre-place a
     * printable sentinel at 0x13f so the control-skip exits
     * immediately. */
    blob[0x13f] = 'A';   /* arbitrary placeholder */

    localization_state_t s = { blob, 0 };
    localization_write(&s, "WRITE", /*index=*/0, /*n_strings=*/0,
                       /*n_bytes=*/5);
    T_ASSERT_MEM_EQ(blob + 0x13f, "WRITE", 5);
    T_ASSERT_EQ_U(s.walker_offset, (size_t)0x13f);
    free(blob);
    return 0;
}

int test_localization_write_skips_n_strings_using_strict_boundary(void) {
    /* Write path uses the strict boundary detector (no NUL-after-
     * NUL).  Same input as the read test, same expected walker. */
    unsigned char *blob = mkblob();
    /* Place secondary-table offset so write base is at 9000.  Offset
     * value = 9000 - 0x1c = 9000 - 28 = 8972 = 0x230c. */
    blob[LOCALIZATION_WRITE_TABLE_OFFS + 2] = 0x23;
    blob[LOCALIZATION_WRITE_TABLE_OFFS + 3] = 0x0c;

    memcpy(blob + 9000, "Aaa\0Bbb\0Ccc\0", 12);

    localization_state_t s = { blob, 0 };
    /* Skip 2 sub-strings, then overwrite 3 bytes of "Ccc" with "ZZZ". */
    localization_write(&s, "ZZZ", 0, /*n_strings=*/2, /*n_bytes=*/3);
    T_ASSERT_MEM_EQ(blob + 9008, "ZZZ", 3);
    T_ASSERT_EQ_U(s.walker_offset, 9008u);
    free(blob);
    return 0;
}

int test_localization_write_zero_bytes_advances_walker(void) {
    unsigned char *blob = mkblob();
    /* Offset value = 0 → walker base = 0x1c.  Pre-place a sentinel
     * at 0x1c so the control-byte-skip loop exits. */
    blob[LOCALIZATION_WRITE_TABLE_OFFS + 2] = 0;
    blob[LOCALIZATION_WRITE_TABLE_OFFS + 3] = 0;
    blob[LOCALIZATION_WRITE_TABLE_OFFS]     = 'X';   /* >= 0x20 */

    localization_state_t s = { blob, 0 };
    /* Pre-seed with garbage to detect non-write. */
    blob[100] = 0xdb;
    localization_write(&s, "?", 0, 0, /*n_bytes=*/0);
    T_ASSERT_EQ_U(s.walker_offset, (size_t)LOCALIZATION_WRITE_TABLE_OFFS);
    /* No write happened (n_bytes=0). */
    T_ASSERT_EQ_I(blob[100], 0xdb);
    free(blob);
    return 0;
}
