/*
 * OpenLords2 — port-side TAS input-trace replay (TAS framework P5).
 *
 * The retail side of the TAS framework drives Lords2.exe from a JSONL
 * input trace via `frida_capture.py --input-segtrace` (tools/frida/
 * openlords2-agent.js posts the WM_* ops against the engine_tick
 * counter).  This module is the PORT mirror: the drop-in reads the SAME
 * trace and replays it through its own WndProc, so one trace drives both
 * engines to comparable frames for a 1:1 retail-vs-port diff.
 *
 * Trace schema (one flat JSON object per line; `#` / blank lines skipped),
 * identical to frida_capture.py's `_load_segtrace`:
 *
 *   {"t":N,"m":M,"w":W,"l":L}        input — post WndProc message M with
 *                                    wParam W, lParam L at tick N RELATIVE
 *                                    to the current segment's opening
 *                                    anchor frame.  Coords are GAME space
 *                                    (640x480), L = (y<<16)|x.
 *   {"wait":"ANCHOR"}                block until ANCHOR fires, then rebase
 *                                    the t-clock floor onto its frame.
 *   {"capture":N}                    capture a frame N ticks after the
 *   {"capture":N,"label":"..."}      segment's opening anchor.
 *   {"wait_until":{...}}             parsed-and-IGNORED (port gap — the
 *                                    load flow uses only {wait}; a global
 *                                    VA-read predicate is a follow-up).
 *
 * The module is Win32-free + host-testable: it returns descriptors (the
 * messages to post, the captures to take) and the caller (main.c) owns
 * the PostMessageA + gfx_capture_backbuffer side effects.
 */

#ifndef OPENLORDS2_INPUT_TRACE_H
#define OPENLORDS2_INPUT_TRACE_H

/* Generous fixed caps.  MAX_OPS sized for trace_studio's compiled dense
 * capture ranges (a {caprange} lowers to one {capture} op per kept frame);
 * hand-written traces stay a few dozen ops.  ~76B/op → ~620KB static. */
#define INPUT_TRACE_MAX_OPS            8192
#define INPUT_TRACE_MAX_FRAME_ACTIONS  16
#define INPUT_TRACE_MAX_LABEL          32
#define INPUT_TRACE_MAX_ANCHORS        16
#define INPUT_TRACE_MAX_ANCHOR_NAME    24

/* One Win32 message to post this frame (the trace's {m,w,l}). */
typedef struct {
    unsigned msg;       /* WM_* (the "m" field)            */
    unsigned wparam;    /* the "w" field                   */
    long     lparam;    /* the "l" field = (y<<16)|x, game */
} input_trace_msg_t;

/* One capture request this frame. */
typedef struct {
    int  frame;                         /* absolute target frame */
    char label[INPUT_TRACE_MAX_LABEL];  /* may be empty          */
} input_trace_capture_t;

/* Actions due this frame, filled by input_trace_step. */
typedef struct {
    input_trace_msg_t     msgs[INPUT_TRACE_MAX_FRAME_ACTIONS];
    int                   msg_count;
    input_trace_capture_t caps[INPUT_TRACE_MAX_FRAME_ACTIONS];
    int                   cap_count;
    int                   blocked;      /* nonzero: parked on an unmet {wait} */
    int                   set_auto_dismiss; /* -1 = no change this frame, else
                                             * the new reactive-dismiss mode
                                             * (1 on / 0 off) the caller applies */
} input_trace_actions_t;

/* Parse a trace file into the static op list.  Returns 0 on success,
 * non-zero on open/parse failure. */
int  input_trace_load(const char *path);

/* Parse a trace from an in-memory buffer (for host tests).  Returns 0 on
 * success.  `text` is NUL-terminated; lines are split on '\n'. */
int  input_trace_load_text(const char *text);

/* Advance the replay by one frame.  `frame` is the current frame counter;
 * `anchor_edge` is the anchor name that fired THIS frame, or NULL.  Fills
 * `out` with the messages to post + captures to take this frame.  Anchors
 * are remembered across frames, so a {wait} whose anchor already fired
 * (a "fast anchor") resolves immediately. */
void input_trace_step(int frame, const char *anchor_edge,
                      input_trace_actions_t *out);

/* Non-zero once every op has been consumed. */
int  input_trace_done(void);

/* How many ops were parsed (test instrumentation). */
int  input_trace_op_count(void);

/* Reset all replay + parse state (host tests). */
void input_trace_reset(void);

#endif /* OPENLORDS2_INPUT_TRACE_H */
