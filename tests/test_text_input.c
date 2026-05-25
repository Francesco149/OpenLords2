/*
 * Unit tests for text_input — the 2000-byte editor cluster.
 *
 * Coverage: cursor motion, insert/overwrite modes, delete/backspace,
 * strip-spaces, init/clear, copy-out, probe-at-cursor, and the
 * clamp-state machine including the length+1 and pixel-overflow
 * quirks documented in text_input.h.
 */

#include "t.h"
#include "text_input.h"
#include <string.h>

/* Convenience: enable the editor and seed it with a payload + room. */
static void seed(text_input_state_t *s, const char *payload,
                 int max_length, int max_pixel_width)
{
    memset(s, 0, sizeof *s);
    text_input_init(s, payload, max_length, max_pixel_width, 0);
    s->enabled = 1;
}

/* ──────────────────────────────────────────────────────────────────
 * init / clear
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_init_copies_payload_into_buf0(void) {
    text_input_state_t s;
    memset(&s, 0xee, sizeof s);

    text_input_init(&s, "hello", 200, 480, 0);

    /* Payload landed at buf[0..4]; everything past it zero. */
    T_ASSERT_MEM_EQ(s.buf, "hello", 5);
    T_ASSERT_EQ_I(s.buf[5], 0);
    T_ASSERT_EQ_I(s.length, 5);
    T_ASSERT_EQ_I(s.max_length, 200);
    T_ASSERT_EQ_I(s.max_pixel_width, 480);
    T_ASSERT_EQ_I(s.cursor, 0);
    T_ASSERT_EQ_I(s.clamp_state, 0);
    T_ASSERT_EQ_I(s.init_mode_param, 0);
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    T_ASSERT_EQ_I(s.anchor_offset, 0);
    return 0;
}

int test_text_input_init_empty_payload_yields_length_zero(void) {
    /* Quirk #16: the `i = -1` register-reuse means length is
     * `-1 + 1 = 0` when payload is empty (not 1 like clear). */
    text_input_state_t s;
    text_input_init(&s, "", 100, 480, 0);
    T_ASSERT_EQ_I(s.length, 0);
    T_ASSERT_EQ_I(s.buf[0], 0);
    return 0;
}

int test_text_input_init_zeroes_full_buffer(void) {
    text_input_state_t s;
    /* Pre-poison every buffer byte. */
    memset(&s, 0, sizeof s);
    memset(s.buf, 0xaa, TEXT_INPUT_BUF_SIZE);

    text_input_init(&s, "hi", 100, 480, 0);

    /* Bytes after the payload are zero — not just one terminator. */
    for (int i = 2; i < TEXT_INPUT_BUF_SIZE; i++) {
        if (s.buf[i] != 0) {
            T_FAIL("buf[%d] = 0x%02x, expected 0", i, s.buf[i]);
        }
    }
    return 0;
}

int test_text_input_init_propagates_initial_mode(void) {
    /* clamp_state and init_mode_param both come from initial_mode. */
    text_input_state_t s;
    text_input_init(&s, "abc", 50, 320, 2);
    T_ASSERT_EQ_I(s.clamp_state, 2);
    T_ASSERT_EQ_I(s.init_mode_param, 2);
    return 0;
}

int test_text_input_clear_sets_length_one_sentinel(void) {
    /* Quirk #15: clear() sets length=1 even though buf is all zero. */
    text_input_state_t s;
    text_input_init(&s, "junkjunk", 80, 480, 0);
    s.cursor = 4;
    s.pixel_overflow = 1;
    s.anchor_offset = 12;

    text_input_clear(&s);

    T_ASSERT_EQ_I(s.length, 1);
    T_ASSERT_EQ_I(s.cursor, 0);
    T_ASSERT_EQ_I(s.pixel_overflow, 0);
    T_ASSERT_EQ_I(s.anchor_offset, 0);
    T_ASSERT_EQ_I(s.buf[0], 0);
    T_ASSERT_EQ_I(s.buf[1999], 0);
    /* max_length / max_pixel_width persist across clear. */
    T_ASSERT_EQ_I(s.max_length, 80);
    T_ASSERT_EQ_I(s.max_pixel_width, 480);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * cursor motion
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_cursor_left_floor_at_zero(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor = 1;
    text_input_cursor_left(&s);
    T_ASSERT_EQ_I(s.cursor, 0);
    text_input_cursor_left(&s);
    T_ASSERT_EQ_I(s.cursor, 0);   /* doesn't go negative */
    return 0;
}

int test_text_input_cursor_right_caps_at_length(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor = 2;
    text_input_cursor_right(&s);
    T_ASSERT_EQ_I(s.cursor, 3);
    text_input_cursor_right(&s);
    T_ASSERT_EQ_I(s.cursor, 3);   /* doesn't go past length */
    return 0;
}

int test_text_input_cursor_home_and_end(void) {
    text_input_state_t s;
    seed(&s, "abcdef", 100, 480);
    s.cursor = 2;
    text_input_cursor_end(&s);
    T_ASSERT_EQ_I(s.cursor, 6);
    text_input_cursor_home(&s);
    T_ASSERT_EQ_I(s.cursor, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * clamp_cursor — the heart of the state machine
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_clamp_cursor_in_range_no_state(void) {
    text_input_state_t s;
    seed(&s, "hello", 100, 480);
    s.cursor = 3;
    text_input_clamp_cursor(&s);
    T_ASSERT_EQ_I(s.cursor, 3);
    T_ASSERT_EQ_I(s.clamp_state, 0);
    return 0;
}

int test_text_input_clamp_cursor_past_length_bumps_to_length_plus_one(void) {
    /* Quirk #17: cursor>length lands at length+1, not length. */
    text_input_state_t s;
    seed(&s, "hello", 100, 480);
    s.cursor = 20;   /* well past length (5) */
    text_input_clamp_cursor(&s);
    T_ASSERT_EQ_I(s.cursor, 6);
    T_ASSERT_EQ_I(s.clamp_state, 1);
    return 0;
}

int test_text_input_clamp_cursor_at_length_no_clamp(void) {
    /* cursor == length is fine — clamp condition is length+1 <= cursor. */
    text_input_state_t s;
    seed(&s, "hello", 100, 480);
    s.cursor = 5;     /* == length */
    text_input_clamp_cursor(&s);
    T_ASSERT_EQ_I(s.cursor, 5);
    T_ASSERT_EQ_I(s.clamp_state, 0);
    return 0;
}

int test_text_input_clamp_cursor_past_max_length_state_two(void) {
    /* Need length+1 > max_length for the max-length clamp to fire
     * (the length+1 clamp runs first and would otherwise pin cursor
     * to length+1 well inside max_length). */
    text_input_state_t s;
    seed(&s, "hellohello", 8, 480);   /* length 10, max_length 8 */
    s.cursor = 50;
    text_input_clamp_cursor(&s);
    /* First clamp: cursor → length+1 = 11, clamp_state=1.
     * Second clamp: max_length (8) <= 11 → cursor=8, clamp_state=2. */
    T_ASSERT_EQ_I(s.cursor, 8);
    T_ASSERT_EQ_I(s.clamp_state, 2);
    return 0;
}

int test_text_input_clamp_cursor_pixel_overflow_sets_state_two(void) {
    /* Pixel-width measurer is not ported; caller sets the flag.
     * clamp_cursor reads it and escalates clamp_state to 2 without
     * moving the cursor. */
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor = 1;
    s.pixel_overflow = 1;
    text_input_clamp_cursor(&s);
    T_ASSERT_EQ_I(s.cursor, 1);
    T_ASSERT_EQ_I(s.clamp_state, 2);
    return 0;
}

int test_text_input_clamp_cursor_negative_floored_to_zero(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor = -5;
    text_input_clamp_cursor(&s);
    T_ASSERT_EQ_I(s.cursor, 0);
    /* clamp_state unchanged by the negative-clamp; the earlier
     * (length+1 <= -5 → false) keeps it at 0. */
    T_ASSERT_EQ_I(s.clamp_state, 0);
    return 0;
}

int test_text_input_clamp_cursor_resets_state_each_call(void) {
    /* clamp_state := 0 at the top; a previously-set 2 doesn't stick. */
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.clamp_state = 2;
    s.cursor      = 1;
    text_input_clamp_cursor(&s);
    T_ASSERT_EQ_I(s.clamp_state, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * insert_char
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_insert_char_disabled_is_noop(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.enabled = 0;
    text_input_insert_char(&s, 'X');
    T_ASSERT_EQ_S((const char *)s.buf, "abc");
    T_ASSERT_EQ_I(s.cursor, 0);
    return 0;
}

int test_text_input_insert_char_overwrite_mode_replaces_in_place(void) {
    /* insert_mode == 0 → overwrite branch (quirk #18). */
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.insert_mode = 0;
    s.cursor = 1;
    text_input_insert_char(&s, 'X');
    /* buf becomes "aXc" — the 'b' was overwritten. */
    T_ASSERT_EQ_S((const char *)s.buf, "aXc");
    T_ASSERT_EQ_I(s.cursor, 2);
    /* length stays stale at 3 (no recompute). */
    T_ASSERT_EQ_I(s.length, 3);
    return 0;
}

int test_text_input_insert_char_insert_mode_shifts_right(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.insert_mode = 1;
    s.cursor = 1;
    text_input_insert_char(&s, 'X');
    /* buf becomes "aXbc". */
    T_ASSERT_EQ_S((const char *)s.buf, "aXbc");
    T_ASSERT_EQ_I(s.cursor, 2);
    return 0;
}

int test_text_input_insert_char_insert_mode_full_buffer_no_op(void) {
    /* Insert branch requires length < max_length. */
    text_input_state_t s;
    seed(&s, "abc", 3, 480);   /* length 3 == max_length 3 */
    s.insert_mode = 1;
    s.cursor = 1;
    text_input_insert_char(&s, 'X');
    /* Insert dropped; buffer and cursor unchanged. */
    T_ASSERT_EQ_S((const char *)s.buf, "abc");
    T_ASSERT_EQ_I(s.cursor, 1);
    return 0;
}

int test_text_input_insert_char_clamp_state_two_blocks(void) {
    /* If clamp_cursor sets clamp_state==2, the insert is skipped. */
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.insert_mode = 0;
    s.cursor = 1;
    s.pixel_overflow = 1;        /* will trigger state==2 inside clamp */
    text_input_insert_char(&s, 'X');
    /* No write happened. */
    T_ASSERT_EQ_S((const char *)s.buf, "abc");
    T_ASSERT_EQ_I(s.cursor, 1);
    return 0;
}

int test_text_input_insert_char_at_end_extends_in_overwrite(void) {
    /* Overwrite at cursor==length writes past terminator; length
     * stays stale but the new char is visible until next recompute. */
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.insert_mode = 0;
    s.cursor = 3;
    text_input_insert_char(&s, 'X');
    /* buf[3]='X', cursor=4.  length stayed at 3 (quirk #18). */
    T_ASSERT_EQ_I(s.buf[3], 'X');
    T_ASSERT_EQ_I(s.buf[4], 0);
    T_ASSERT_EQ_I(s.cursor, 4);
    T_ASSERT_EQ_I(s.length, 3);
    /* Recompute fixes it. */
    text_input_recompute_length(&s);
    T_ASSERT_EQ_I(s.length, 4);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * delete_char / backspace
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_delete_char_removes_at_cursor(void) {
    text_input_state_t s;
    seed(&s, "abcdef", 100, 480);
    s.cursor = 2;
    text_input_delete_char(&s);
    /* 'c' (at idx 2) was removed; "abdef\0". */
    T_ASSERT_EQ_S((const char *)s.buf, "abdef");
    T_ASSERT_EQ_I(s.cursor, 2);
    return 0;
}

int test_text_input_delete_char_disabled_is_noop(void) {
    text_input_state_t s;
    seed(&s, "abcdef", 100, 480);
    s.enabled = 0;
    s.cursor  = 2;
    text_input_delete_char(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "abcdef");
    return 0;
}

int test_text_input_backspace_at_zero_is_noop(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor = 0;
    text_input_backspace(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "abc");
    T_ASSERT_EQ_I(s.cursor, 0);
    return 0;
}

int test_text_input_backspace_deletes_prev_char(void) {
    text_input_state_t s;
    seed(&s, "abcdef", 100, 480);
    s.cursor = 3;
    text_input_backspace(&s);
    /* Deletes 'c' (at index 2); cursor now at 2 pointing to 'd'. */
    T_ASSERT_EQ_S((const char *)s.buf, "abdef");
    T_ASSERT_EQ_I(s.cursor, 2);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * strip_spaces — preserves the one-pass quirk
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_strip_spaces_single_space(void) {
    text_input_state_t s;
    seed(&s, "ab cd", 100, 480);
    text_input_strip_spaces(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "abcd");
    return 0;
}

int test_text_input_strip_spaces_adjacent_keeps_one(void) {
    /* The single-pass loop advances `i` after shifting, so adjacent
     * spaces only get the first one stripped. */
    text_input_state_t s;
    seed(&s, "a  bc", 100, 480);
    text_input_strip_spaces(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "a bc");
    return 0;
}

int test_text_input_strip_spaces_no_spaces_unchanged(void) {
    text_input_state_t s;
    seed(&s, "abcdef", 100, 480);
    text_input_strip_spaces(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "abcdef");
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * recompute_length
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_recompute_length_basic(void) {
    text_input_state_t s;
    seed(&s, "hello world", 100, 480);
    s.length = 999;   /* corrupt */
    text_input_recompute_length(&s);
    T_ASSERT_EQ_I(s.length, 11);
    return 0;
}

int test_text_input_recompute_length_empty(void) {
    text_input_state_t s;
    memset(&s, 0, sizeof s);
    s.length = 17;
    text_input_recompute_length(&s);
    T_ASSERT_EQ_I(s.length, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * toggle_insert_mode
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_toggle_insert_mode_flips(void) {
    text_input_state_t s;
    memset(&s, 0, sizeof s);
    s.insert_mode = 0;
    text_input_toggle_insert_mode(&s);
    T_ASSERT_EQ_I(s.insert_mode, 1);
    text_input_toggle_insert_mode(&s);
    T_ASSERT_EQ_I(s.insert_mode, 0);
    return 0;
}

int test_text_input_toggle_insert_mode_xor_bit0(void) {
    /* Retail XORs the whole byte with 1 — high bits would persist
     * if set somehow.  We exercise it. */
    text_input_state_t s;
    memset(&s, 0, sizeof s);
    s.insert_mode = 0x80;     /* bit 7 set */
    text_input_toggle_insert_mode(&s);
    T_ASSERT_EQ_I(s.insert_mode, 0x81);
    text_input_toggle_insert_mode(&s);
    T_ASSERT_EQ_I(s.insert_mode, 0x80);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * probe_at_cursor — quirk #19
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_probe_at_cursor_on_char_zero(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor      = 1;        /* 'b' */
    s.clamp_state = 0;
    text_input_probe_at_cursor(&s);
    T_ASSERT_EQ_I(s.probe_result, 0);
    return 0;
}

int test_text_input_probe_at_cursor_on_terminator_is_one(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    s.cursor      = 3;        /* sits on '\0' */
    s.clamp_state = 0;
    text_input_probe_at_cursor(&s);
    T_ASSERT_EQ_I(s.probe_result, 1);
    return 0;
}

int test_text_input_probe_at_cursor_decimal_lt_eight_is_one(void) {
    /* clamp_state==1, buf[cursor]=='.', cursor<8 → result=1. */
    text_input_state_t s;
    seed(&s, "12.45", 100, 480);
    s.cursor      = 2;        /* '.' */
    s.clamp_state = 1;
    text_input_probe_at_cursor(&s);
    T_ASSERT_EQ_I(s.probe_result, 1);
    return 0;
}

int test_text_input_probe_at_cursor_decimal_ge_eight_is_two(void) {
    /* clamp_state==1, buf[cursor]=='.', cursor>=8 → result=2. */
    text_input_state_t s;
    seed(&s, "12345678.99", 100, 480);
    s.cursor      = 8;        /* '.' at idx 8 */
    s.clamp_state = 1;
    text_input_probe_at_cursor(&s);
    T_ASSERT_EQ_I(s.probe_result, 2);
    return 0;
}

int test_text_input_probe_at_cursor_decimal_without_clamp_state_one(void) {
    /* Decimal point with clamp_state == 0 → no override. */
    text_input_state_t s;
    seed(&s, "12.45", 100, 480);
    s.cursor      = 2;
    s.clamp_state = 0;
    text_input_probe_at_cursor(&s);
    T_ASSERT_EQ_I(s.probe_result, 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * copy_out
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_copy_out_truncates_at_max_chars(void) {
    text_input_state_t s;
    seed(&s, "abcdef", 100, 480);
    char out[8];
    memset(out, 0xaa, sizeof out);
    text_input_copy_out(&s, out, 3);
    /* Three chars + terminator. */
    T_ASSERT_EQ_S(out, "abc");
    T_ASSERT_EQ_I(out[3], 0);
    /* out[4]..out[7] still poisoned (we didn't touch them). */
    T_ASSERT_EQ_I((unsigned char)out[4], 0xaa);
    return 0;
}

int test_text_input_copy_out_full_string_when_max_large(void) {
    text_input_state_t s;
    seed(&s, "hello", 100, 480);
    char out[16] = {0};
    text_input_copy_out(&s, out, 16);
    T_ASSERT_EQ_S(out, "hello");
    return 0;
}

int test_text_input_copy_out_zero_max_writes_only_terminator(void) {
    text_input_state_t s;
    seed(&s, "abc", 100, 480);
    char out[4];
    memset(out, 0xaa, sizeof out);
    text_input_copy_out(&s, out, 0);
    T_ASSERT_EQ_I(out[0], 0);
    T_ASSERT_EQ_I((unsigned char)out[1], 0xaa);   /* not touched */
    return 0;
}

int test_text_input_copy_out_empty_buf_writes_only_terminator(void) {
    text_input_state_t s;
    memset(&s, 0, sizeof s);   /* buf is empty */
    char out[4];
    memset(out, 0xaa, sizeof out);
    text_input_copy_out(&s, out, 10);
    T_ASSERT_EQ_I(out[0], 0);
    return 0;
}

/* ──────────────────────────────────────────────────────────────────
 * end-to-end editing scenario
 * ────────────────────────────────────────────────────────────────── */

int test_text_input_scenario_type_then_backspace(void) {
    /* Simulate typing "HELP" with insert mode then backspacing twice. */
    text_input_state_t s;
    memset(&s, 0, sizeof s);
    text_input_init(&s, "", 80, 480, 0);
    s.enabled     = 1;
    s.insert_mode = 1;

    /* Insert path needs length tracking — the insert branch checks
     * length<max_length but doesn't update length itself.  Run
     * recompute_length between operations to keep state honest. */
    text_input_insert_char(&s, 'H'); text_input_recompute_length(&s);
    text_input_insert_char(&s, 'E'); text_input_recompute_length(&s);
    text_input_insert_char(&s, 'L'); text_input_recompute_length(&s);
    text_input_insert_char(&s, 'P'); text_input_recompute_length(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "HELP");
    T_ASSERT_EQ_I(s.cursor, 4);

    text_input_backspace(&s);   /* removes 'P' */
    text_input_recompute_length(&s);
    text_input_backspace(&s);   /* removes 'L' */
    text_input_recompute_length(&s);
    T_ASSERT_EQ_S((const char *)s.buf, "HE");
    T_ASSERT_EQ_I(s.cursor, 2);
    return 0;
}
