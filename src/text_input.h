/*
 * OpenLords2 — text-input editor cluster.
 *
 * Pure-C port of the engine's 2000-byte text-input box editor — the
 * one-line text field used by save-name prompts, scenario titles,
 * chat lines, etc.  All state-mutating leaves and one trivial probe.
 *
 * Retail address map (13 functions, ~1.1 KB of decompiled code):
 *
 *   FUN_00401984 @ 0x401984 (156 B)  → text_input_clamp_cursor
 *   FUN_00401c70 @ 0x401c70  (35 B)  → text_input_backspace
 *   FUN_00401ca3 @ 0x401ca3  (25 B)  → text_input_toggle_insert_mode
 *   FUN_00401cbc @ 0x401cbc  (30 B)  → text_input_cursor_left
 *   FUN_00401cda @ 0x401cda  (34 B)  → text_input_cursor_right
 *   FUN_00401cfc @ 0x401cfc  (21 B)  → text_input_cursor_home
 *   FUN_00401d11 @ 0x401d11  (21 B)  → text_input_cursor_end
 *   FUN_00401d26 @ 0x401d26 (162 B)  → text_input_insert_char
 *   FUN_00401dc8 @ 0x401dc8  (64 B)  → text_input_delete_char
 *   FUN_00401e49 @ 0x401e49 (135 B)  → text_input_probe_at_cursor
 *   FUN_00401ed0 @ 0x401ed0 (121 B)  → text_input_strip_spaces
 *   FUN_00401f49 @ 0x401f49  (70 B)  → text_input_recompute_length
 *   FUN_004020ae @ 0x4020ae  (94 B)  → text_input_clear
 *   FUN_00402009 @ 0x402009 (165 B)  → text_input_init
 *   FUN_0040210c @ 0x40210c  (90 B)  → text_input_copy_out
 *   FUN_00401c3f @ 0x401c3f  (28 B)  → text_input_insert_char_and_clamp
 *   FUN_00401a20 @ 0x401a20 (543 B)  → text_input_filtered_keypress
 *
 * Thunks / aliases not exported:
 *
 *   FUN_00401c93 @ 0x401c93  (16 B)  — bare jmp to FUN_00401dc8
 *     (text_input_delete_char).  No state difference between the
 *     two addresses; the alias likely exists so a function table
 *     can point at a stable address.  Call text_input_delete_char
 *     directly.
 *
 * Data layout (mirrors retail globals):
 *
 *   buf[2000]        — DAT_005cd550, the text buffer.  Null-terminated.
 *   cursor           — DAT_005bb4a8, char offset of the cursor.
 *   length           — DAT_0058fe94, count of non-null bytes before
 *                      the terminator.  Recomputed by recompute_length;
 *                      otherwise hand-maintained by insert/delete via
 *                      the shift operations.
 *   max_length       — DAT_005cd4f4, hard character cap.
 *   max_pixel_width  — DAT_0058fd68, soft pixel-width cap (consulted
 *                      by the pixel-width measurer, not this module).
 *   insert_mode      — DAT_005c9280, 0/1 toggle.  See insert_char.
 *   enabled          — DAT_005aeb78, gate for insert/delete (read-only
 *                      probes still work when disabled).
 *   clamp_state      — DAT_005cd408, 0/1/2.  See clamp_cursor.
 *   pixel_overflow   — DAT_0057d3b0, 0/1.  Set by the pixel-width
 *                      measurer (separate module, not yet ported);
 *                      consumed by clamp_cursor to escalate
 *                      clamp_state to 2.
 *   probe_result     — _DAT_00591520, 0/1/2.  Set by probe_at_cursor.
 *   init_mode_param  — DAT_005c928c, opaque init parameter saved
 *                      verbatim for the renderer to read later.
 *   anchor_offset    — DAT_0058fd54, scrolling anchor used by the
 *                      renderer; this module only resets it.
 *
 * Deferred to follow-up chips:
 *   - Pixel-width measurer (FUN_00401f8f + FUN_004015b9 — font lookup).
 *   - Save / load (FUN_00402166 / FUN_0040220f — file IO).
 *   - Renderer / dispatcher (FUN_00402637 — 989 B, references font,
 *     dirty-rect, palette, keyboard state, cursor anchor, etc.).
 *
 * Preserved quirks:
 *
 *   #15 — clear() sets length=1 even though the buffer is all zeros.
 *         A subsequent recompute_length() would set length=0.  The
 *         "1" is a deliberate sentinel: the editor treats positions
 *         0..length+1 as valid, so length=1 means "cursor may sit at
 *         position 0 with the terminator visible at position 1".
 *
 *   #16 — init()'s payload-copy loop runs at local_8=-1 the first
 *         iteration (decremented past the end of the clearing loop)
 *         and indexes via `&buf[1] + local_8` — i.e. writes to buf[0]
 *         the first iteration via the address (buf+1)+(-1).  After N
 *         payload chars, local_8 = N-1; length is set to local_8+1 = N.
 *         No off-by-one bug — just a register-reuse optimization.
 *
 *   #17 — clamp_cursor clamps cursor>length to length+1, not length.
 *         The "+1" is the slot just past the visible terminator,
 *         which is a legal position for inserting at end-of-buffer.
 *         clamp_state==1 reports "cursor was past end and bumped to
 *         length+1".  clamp_state==2 reports "cursor was past max_length
 *         OR pixel_overflow is set".
 *
 *   #18 — insert_char's "overwrite" branch (insert_mode==0) writes
 *         buf[cursor] and bumps cursor unconditionally — no max_length
 *         check, no length update.  If the caller overwrites the
 *         terminator, length stays stale until recompute_length().
 *         The "insert" branch (insert_mode==1) requires length<max_length
 *         and uses strutil_shift_right to make room.
 *
 *   #19 — probe_at_cursor sets probe_result=2 specifically when
 *         clamp_state==1 AND buf[cursor]=='.' AND cursor>=8.  Used
 *         by the renderer to color the decimal-point indicator
 *         differently in number-entry fields.
 *
 * Pure C; no Win32 deps; all state held in the explicit state struct
 * (no globals).
 */

#ifndef OPENLORDS2_TEXT_INPUT_H
#define OPENLORDS2_TEXT_INPUT_H

#include <stddef.h>

/* Buffer size — matches retail's hardcoded loop bounds. */
#define TEXT_INPUT_BUF_SIZE 2000

typedef struct {
    unsigned char buf[TEXT_INPUT_BUF_SIZE];
    int           cursor;
    int           length;
    int           max_length;
    int           max_pixel_width;
    unsigned char insert_mode;
    int           enabled;
    int           clamp_state;
    int           pixel_overflow;
    int           probe_result;
    int           init_mode_param;
    int           anchor_offset;
} text_input_state_t;

/* ----- init / reset ----------------------------------------------- */

/* Port of FUN_00402009 (init).  Zeros the 2000-byte buffer, copies
 * the null-terminated `payload` into it starting at buf[0], sets
 * length = strlen(payload), max_length = `max_length`, max_pixel_width
 * = `max_pixel_width`, init_mode_param = clamp_state = `initial_mode`,
 * pixel_overflow = anchor_offset = cursor = 0.
 *
 * Does NOT touch the `enabled` or `insert_mode` fields — those are
 * caller-managed.  In retail they live in different globals that
 * persist across init/clear cycles. */
void text_input_init(text_input_state_t *s,
                     const char *payload,
                     int max_length,
                     int max_pixel_width,
                     int initial_mode);

/* Port of FUN_004020ae (clear).  Zeros buf, sets length=1 (see
 * quirk #15), cursor=0, pixel_overflow=0, anchor_offset=0.
 * Leaves max_length, max_pixel_width, insert_mode, enabled,
 * clamp_state, init_mode_param, probe_result untouched. */
void text_input_clear(text_input_state_t *s);

/* ----- cursor motion ---------------------------------------------- */

/* Port of FUN_00401cbc.  cursor = max(0, cursor-1). */
void text_input_cursor_left(text_input_state_t *s);

/* Port of FUN_00401cda.  cursor = min(length, cursor+1).  Note the
 * upper bound is `length`, not `length+1` — caps at the terminator. */
void text_input_cursor_right(text_input_state_t *s);

/* Port of FUN_00401cfc.  cursor = 0. */
void text_input_cursor_home(text_input_state_t *s);

/* Port of FUN_00401d11.  cursor = length. */
void text_input_cursor_end(text_input_state_t *s);

/* ----- editing ---------------------------------------------------- */

/* Port of FUN_00401ca3.  Toggles insert_mode (XOR with 1). */
void text_input_toggle_insert_mode(text_input_state_t *s);

/* Port of FUN_00401d26 (insert_char).  Guard: no-op if !enabled OR
 * (after clamp_cursor) clamp_state == 2.  Then:
 *   insert_mode == 0 (overwrite) → buf[cursor]=c; cursor++.  No
 *                                  max_length check; no length update.
 *                                  (See quirk #18.)
 *   insert_mode != 0 (insert)    → if length<max_length:
 *                                    shift_right(buf+cursor, buf+1+length)
 *                                    buf[cursor]=c; cursor++. */
void text_input_insert_char(text_input_state_t *s, unsigned char c);

/* Port of FUN_00401c3f (28 B).  Calls insert_char(c) then re-runs
 * clamp_cursor.  insert_char already clamps internally at the
 * top — this wrapper re-clamps AFTER the insert so the post-advance
 * cursor is re-validated.  Used by callers that don't trust the
 * caller-side bookkeeping (e.g. paste-buffer flush). */
void text_input_insert_char_and_clamp(text_input_state_t *s, unsigned char c);

/* Port of FUN_00401a20 (543 B).  Filtered character dispatcher for
 * keyboard events.  Classifies `c` and passes it through to
 * insert_char if it's allowed in the current input mode, then runs
 * clamp_cursor.
 *
 * Permissive set (always passes regardless of init_mode_param):
 *   - 0x20 (space)
 *   - 0x2d (hyphen-minus)
 *   - 0x30..0x39 (ASCII digits)
 *   - 0x41..0x5a (ASCII uppercase)
 *   - 0x61..0x7a (ASCII lowercase)
 *   - 0x80..0x9a (extended Latin / Code-page-437 range)
 *   - 0xa0..0xa7 (more extended range)
 *   - 0xe1 (specific extended char — sharp-s? germane to localized
 *     names)
 *
 * Conditional set (passes only when init_mode_param == 0):
 *   - 0x2c (comma)
 *   - 0x2e (period)
 *   - 0x3f (question mark)
 *   - 0x21 (exclamation)
 *
 * Case folding: when init_mode_param != 0 AND c is in 0x41..0x5a
 * (uppercase ASCII), the char is lowercased before insert
 * (strutil_to_lower_char).  This implements the "case-insensitive"
 * input mode used for player/save names — punctuation is
 * suppressed and letters normalize to lowercase.
 *
 * All other bytes are silently filtered out (no insert).  Both
 * branches end with clamp_cursor. */
void text_input_filtered_keypress(text_input_state_t *s, unsigned char c);

/* Port of FUN_00401dc8 (delete_char).  Guard: no-op if !enabled.
 * shift_left(buf+cursor, buf+length); then clamp_cursor.  Deletes
 * the char at cursor; length is NOT updated (clamp won't update it
 * either — caller must recompute_length if they care). */
void text_input_delete_char(text_input_state_t *s);

/* Port of FUN_00401c70 (backspace).  if cursor>0: cursor--; then
 * delete_char.  Net effect: deletes the char just left of cursor
 * and leaves cursor pointing at where that char was. */
void text_input_backspace(text_input_state_t *s);

/* Port of FUN_00401ed0 (strip_spaces).  Walks buf[0..length], and
 * for each space byte, does shift_left(buf+i, buf+length).  Note
 * the loop bound advances `i` after shifting — so two adjacent
 * spaces only get the first one shifted out per call.
 *
 * (Retail behavior is preserved verbatim; callers in practice
 * invoke this once and then recompute_length.) */
void text_input_strip_spaces(text_input_state_t *s);

/* ----- queries / clamping ----------------------------------------- */

/* Port of FUN_00401984 (clamp_cursor).  Re-derives clamp_state from
 * (cursor, length, max_length, pixel_overflow):
 *
 *   clamp_state := 0
 *   if length+1 <= cursor:   cursor = length+1; clamp_state = 1
 *   if max_length <= cursor: cursor = max_length; clamp_state = 2
 *   if pixel_overflow:       clamp_state = 2
 *   if cursor < 0:           cursor = 0
 *
 * NOTE: In retail this function calls FUN_00401f8f (pixel-width
 * measurer) before the clamp checks, which has the side effect of
 * setting pixel_overflow.  The port omits that call (the measurer is
 * in a separate, not-yet-ported module).  Callers that want the
 * full retail semantics must compute pixel_overflow themselves and
 * write it to `s->pixel_overflow` before calling. */
void text_input_clamp_cursor(text_input_state_t *s);

/* Port of FUN_00401f49 (recompute_length).  length = strlen(buf).
 * Walks from buf[0] until the first null byte. */
void text_input_recompute_length(text_input_state_t *s);

/* Port of FUN_00401e49 (probe_at_cursor).  Sets probe_result:
 *   = 0 normally
 *   = 1 if buf[cursor] == '\0' (cursor sits on the terminator)
 *   = 1 if clamp_state == 1 AND buf[cursor] == '.' AND cursor < 8
 *   = 2 if clamp_state == 1 AND buf[cursor] == '.' AND cursor >= 8
 * Used by the renderer to color the EOT marker / decimal indicator. */
void text_input_probe_at_cursor(text_input_state_t *s);

/* Port of FUN_0040210c (copy_out).  Copies up to `max_chars` bytes
 * from buf to `dest`, stopping at the first null byte in buf.
 * Always writes a null terminator at dest[written].  `dest` must
 * have room for `max_chars + 1` bytes in the worst case (or
 * `min(strlen(buf), max_chars) + 1`). */
void text_input_copy_out(const text_input_state_t *s, char *dest, int max_chars);

#endif /* OPENLORDS2_TEXT_INPUT_H */
