/*
 * Unit tests for the embedded char→glyph index table (gfx_glyph_table.c).
 *
 * Guards the two-pointer scheme and a handful of values extracted from
 * Lords2.exe, so a bad re-extraction or an accidental edit to the array
 * is caught immediately.
 */

#include "t.h"
#include "gfx_glyph_table.h"

/* The render map is the measure map advanced by 0x20.  The renderer
 * indexes it with (c - 0x20), so render[c - 0x20] == measure[c] for
 * every char code — the core invariant the whole text path rests on. */
int test_glyph_table_render_is_measure_plus_0x20(void)
{
    const uint8_t *measure = gfx_glyph_measure_map();
    const uint8_t *render  = gfx_glyph_render_map();
    T_ASSERT(render == measure + 0x20);
    for (int c = 0x20; c <= 0xff; c++) {
        T_ASSERT_EQ_I(render[c - 0x20], measure[c]);
    }
    return 0;
}

/* Letters map to the contiguous glyph ranges the decompile shows:
 * 'A'..'Z' → 0x1b.. and 'a'..'z' → 0x01.. (1-based indices). */
int test_glyph_table_letters(void)
{
    const uint8_t *m = gfx_glyph_measure_map();
    T_ASSERT_EQ_I(m['A'], 0x1b);
    T_ASSERT_EQ_I(m['Z'], 0x34);
    T_ASSERT_EQ_I(m['a'], 0x01);
    T_ASSERT_EQ_I(m['z'], 0x1a);
    return 0;
}

/* Digits and a couple of punctuation marks. */
int test_glyph_table_digits_and_punct(void)
{
    const uint8_t *m = gfx_glyph_measure_map();
    T_ASSERT_EQ_I(m['0'], 0x3e);
    T_ASSERT_EQ_I(m['1'], 0x35);
    T_ASSERT_EQ_I(m['9'], 0x3d);
    T_ASSERT_EQ_I(m['!'], 0x3f);
    T_ASSERT_EQ_I(m['?'], 0x4b);
    return 0;
}

/* Space and unmapped control bytes resolve to 0 ("no glyph"). */
int test_glyph_table_space_and_unmapped_are_zero(void)
{
    const uint8_t *m = gfx_glyph_measure_map();
    T_ASSERT_EQ_I(m[' '], 0x00);
    T_ASSERT_EQ_I(m['@'], 0x00);
    T_ASSERT_EQ_I(m[0x00], 0x00);
    T_ASSERT_EQ_I(m[0xfe], 0x00);
    /* The render side reaches the same cells via (c - 0x20). */
    const uint8_t *r = gfx_glyph_render_map();
    T_ASSERT_EQ_I(r['A' - 0x20], 0x1b);
    T_ASSERT_EQ_I(r['0' - 0x20], 0x3e);
    return 0;
}
