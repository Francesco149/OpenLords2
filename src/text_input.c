/*
 * text-input editor cluster — pure-C port of the FUN_00401984 family.
 * See text_input.h for the API doc, retail address map, and the
 * preserved-quirk callouts.
 */

#include "text_input.h"
#include "strutil.h"

#include <string.h>

/* ----- init / reset ----------------------------------------------- */

void text_input_init(text_input_state_t *s,
                     const char *payload,
                     int max_length,
                     int max_pixel_width,
                     int initial_mode)
{
    /* Mirror retail's decrement-before-test clearing loop, which
     * exits with local_8 == -1 so the next loop's
     * `&buf[1] + local_8` indexes start at buf[0]. (Quirk #16.) */
    int i;
    for (i = 1999; i >= 0; i--) {
        s->buf[i] = 0;
    }
    /* i == -1 here. */

    for (; *payload != '\0'; payload++) {
        /* `&DAT_005cd551 + local_8` in retail; equivalent to
         * `&buf[1] + i`, which is `&buf[i + 1]`. */
        s->buf[i + 1] = (unsigned char)*payload;
        i++;
    }
    /* i == (payload_len - 1) on exit; payload_len chars written
     * starting at buf[0]. */

    s->init_mode_param = initial_mode;
    s->length          = i + 1;
    s->max_length      = max_length;
    s->max_pixel_width = max_pixel_width;
    s->clamp_state     = initial_mode;
    s->pixel_overflow  = 0;
    s->anchor_offset   = 0;
    s->cursor          = 0;
}

void text_input_clear(text_input_state_t *s)
{
    /* Same decrement loop as init, but no payload copy. */
    for (int i = 1999; i >= 0; i--) {
        s->buf[i] = 0;
    }
    s->length         = 1;     /* Quirk #15 — sentinel "empty" length. */
    s->pixel_overflow = 0;
    s->anchor_offset  = 0;
    s->cursor         = 0;
}

/* ----- cursor motion ---------------------------------------------- */

void text_input_cursor_left(text_input_state_t *s)
{
    if (s->cursor > 0) {
        s->cursor--;
    }
}

void text_input_cursor_right(text_input_state_t *s)
{
    if (s->cursor < s->length) {
        s->cursor++;
    }
}

void text_input_cursor_home(text_input_state_t *s)
{
    s->cursor = 0;
}

void text_input_cursor_end(text_input_state_t *s)
{
    s->cursor = s->length;
}

/* ----- editing ---------------------------------------------------- */

void text_input_toggle_insert_mode(text_input_state_t *s)
{
    s->insert_mode ^= 1;
}

void text_input_insert_char(text_input_state_t *s, unsigned char c)
{
    if (!s->enabled) {
        return;
    }
    text_input_clamp_cursor(s);
    if (s->clamp_state == 2) {
        return;
    }

    if (s->insert_mode == 0) {
        /* Overwrite branch — no max_length check, no length update.
         * (Quirk #18.) */
        s->buf[s->cursor] = c;
        s->cursor++;
    } else if (s->length < s->max_length) {
        /* Insert branch — shift right to make room. */
        strutil_shift_right(s->buf + s->cursor,
                            s->buf + 1 + s->length);
        s->buf[s->cursor] = c;
        s->cursor++;
    }
}

void text_input_delete_char(text_input_state_t *s)
{
    if (!s->enabled) {
        return;
    }
    strutil_shift_left(s->buf + s->cursor, s->buf + s->length);
    text_input_clamp_cursor(s);
}

void text_input_backspace(text_input_state_t *s)
{
    if (s->cursor > 0) {
        s->cursor--;
        text_input_delete_char(s);
    }
}

void text_input_strip_spaces(text_input_state_t *s)
{
    /* Retail walks `i` from 0 to length, calling shift_left for
     * each space and advancing `i` afterward.  This means adjacent
     * spaces only get the first one removed per call. */
    for (int i = 0; i <= s->length && s->buf[i] != '\0'; i++) {
        if (s->buf[i] == ' ') {
            strutil_shift_left(s->buf + i, s->buf + s->length);
        }
    }
}

/* ----- queries / clamping ----------------------------------------- */

void text_input_clamp_cursor(text_input_state_t *s)
{
    /* NOTE: retail calls FUN_00401f8f (pixel-width measurer) here as
     * the first action, which has the side effect of setting
     * pixel_overflow.  We don't have that ported yet — callers that
     * want the full retail clamp semantics must set
     * s->pixel_overflow themselves before this call. */
    s->clamp_state = 0;
    if (s->length + 1 <= s->cursor) {
        s->cursor      = s->length + 1;
        s->clamp_state = 1;
    }
    if (s->max_length <= s->cursor) {
        s->cursor      = s->max_length;
        s->clamp_state = 2;
    }
    if (s->pixel_overflow != 0) {
        s->clamp_state = 2;
    }
    if (s->cursor < 0) {
        s->cursor = 0;
    }
}

void text_input_recompute_length(text_input_state_t *s)
{
    int i = 0;
    s->length = 0;
    while (s->buf[i] != '\0') {
        s->length++;
        i++;
    }
}

void text_input_probe_at_cursor(text_input_state_t *s)
{
    s->probe_result = (s->buf[s->cursor] == '\0') ? 1 : 0;
    if (s->clamp_state == 1 && s->buf[s->cursor] == '.') {
        s->probe_result = (s->cursor < 8) ? 1 : 2;
    }
}

void text_input_copy_out(const text_input_state_t *s, char *dest, int max_chars)
{
    int i = 0;
    while (s->buf[i] != '\0') {
        if (max_chars < 1) break;
        *dest = (char)s->buf[i];
        dest++;
        max_chars--;
        i++;
    }
    *dest = '\0';
}
