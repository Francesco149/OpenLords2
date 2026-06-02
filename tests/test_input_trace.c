/*
 * Unit tests for input_trace — the port-side TAS trace parser + replay
 * scheduler.  Drives input_trace_step over hand-authored traces and
 * asserts the anchor-relative scheduling, {wait} clock-rebase, and
 * capture emission match the retail segtrace semantics.
 */

#include "t.h"
#include "input_trace.h"

#include <string.h>

/* Win32 message numbers used in the traces (avoid pulling windows.h). */
enum { WM_MOUSEMOVE = 0x200, WM_LBUTTONDOWN = 0x201, WM_LBUTTONUP = 0x202 };

/* Step one frame with no anchor edge; return the actions. */
static input_trace_actions_t step(int frame, const char *anchor)
{
    input_trace_actions_t a;
    input_trace_step(frame, anchor, &a);
    return a;
}

int test_input_trace_parse_skips_comments_and_blanks(void)
{
    const char *trace =
        "# a comment\n"
        "\n"
        "{\"wait\": \"TITLE\"}\n"
        "{\"t\": 5, \"m\": 512, \"w\": 0, \"l\": 74496}\n"
        "   # indented comment\n"
        "{\"capture\": 2, \"label\": \"map\"}\n";
    T_ASSERT_EQ_I(input_trace_load_text(trace), 0);
    T_ASSERT_EQ_I(input_trace_op_count(), 3);   /* wait + input + capture */
    return 0;
}

int test_input_trace_wait_until_is_ignored(void)
{
    const char *trace =
        "{\"wait\": \"TITLE\"}\n"
        "{\"wait_until\": {\"va\":\"0x53f050\",\"type\":\"u8\",\"op\":\"==\",\"val\":1}}\n"
        "{\"t\": 1, \"m\": 512, \"w\": 0, \"l\": 0}\n";
    T_ASSERT_EQ_I(input_trace_load_text(trace), 0);
    T_ASSERT_EQ_I(input_trace_op_count(), 2);   /* wait_until dropped */
    return 0;
}

int test_input_trace_input_due_relative_to_anchor(void)
{
    const char *trace =
        "{\"wait\": \"TITLE\"}\n"
        "{\"t\": 5, \"m\": 513, \"w\": 1, \"l\": 74496}\n";
    input_trace_load_text(trace);

    /* TITLE fires at frame 0 → segment floor = 0. */
    input_trace_actions_t a = step(0, "TITLE");
    T_ASSERT_EQ_I(a.msg_count, 0);              /* t=5 not due yet */
    T_ASSERT_EQ_I(a.blocked, 0);                /* wait satisfied */

    for (int f = 1; f <= 4; f++) {
        a = step(f, 0);
        T_ASSERT_EQ_I(a.msg_count, 0);
    }
    a = step(5, 0);
    T_ASSERT_EQ_I(a.msg_count, 1);
    T_ASSERT_EQ_U(a.msgs[0].msg, (unsigned)WM_LBUTTONDOWN);
    T_ASSERT_EQ_U(a.msgs[0].wparam, 1u);
    T_ASSERT_EQ_I((int)a.msgs[0].lparam, 74496);
    T_ASSERT(input_trace_done());
    return 0;
}

int test_input_trace_wait_rebases_clock(void)
{
    const char *trace =
        "{\"wait\": \"TITLE\"}\n"
        "{\"t\": 2, \"m\": 1, \"w\": 0, \"l\": 0}\n"
        "{\"wait\": \"MAP_FREEROAM\"}\n"
        "{\"t\": 3, \"m\": 2, \"w\": 0, \"l\": 0}\n";
    input_trace_load_text(trace);

    step(0, "TITLE");                           /* floor = 0 */
    input_trace_actions_t a = step(2, 0);
    T_ASSERT_EQ_I(a.msg_count, 1);
    T_ASSERT_EQ_U(a.msgs[0].msg, 1u);

    /* Now parked on {wait: MAP_FREEROAM}. */
    a = step(3, 0);
    T_ASSERT_EQ_I(a.blocked, 1);
    T_ASSERT_EQ_I(a.msg_count, 0);

    /* MAP fires at frame 10 → floor rebases to 10; the t=3 op is due at 13. */
    a = step(10, "MAP_FREEROAM");
    T_ASSERT_EQ_I(a.blocked, 0);
    T_ASSERT_EQ_I(a.msg_count, 0);              /* 10 < 10+3 */
    a = step(12, 0);
    T_ASSERT_EQ_I(a.msg_count, 0);
    a = step(13, 0);
    T_ASSERT_EQ_I(a.msg_count, 1);
    T_ASSERT_EQ_U(a.msgs[0].msg, 2u);
    T_ASSERT(input_trace_done());
    return 0;
}

int test_input_trace_capture_anchor_relative(void)
{
    const char *trace =
        "{\"wait\": \"MAP_FREEROAM\"}\n"
        "{\"capture\": 2, \"label\": \"map_enter\"}\n"
        "{\"capture\": 60, \"label\": \"map_settled\"}\n";
    input_trace_load_text(trace);

    /* MAP at frame 30 → captures at 32 and 90. */
    input_trace_actions_t a = step(30, "MAP_FREEROAM");
    T_ASSERT_EQ_I(a.cap_count, 0);
    a = step(32, 0);
    T_ASSERT_EQ_I(a.cap_count, 1);
    T_ASSERT_EQ_I(a.caps[0].frame, 32);
    T_ASSERT_EQ_S(a.caps[0].label, "map_enter");
    a = step(89, 0);
    T_ASSERT_EQ_I(a.cap_count, 0);
    a = step(90, 0);
    T_ASSERT_EQ_I(a.cap_count, 1);
    T_ASSERT_EQ_S(a.caps[0].label, "map_settled");
    T_ASSERT(input_trace_done());
    return 0;
}

int test_input_trace_fast_anchor_resolves(void)
{
    /* The anchor fires while the cursor is still parked on an earlier
     * (not-yet-due) input op — the {wait} must still resolve against it. */
    const char *trace =
        "{\"wait\": \"A\"}\n"
        "{\"t\": 100, \"m\": 9, \"w\": 0, \"l\": 0}\n"
        "{\"wait\": \"B\"}\n"
        "{\"capture\": 0, \"label\": \"after_b\"}\n";
    input_trace_load_text(trace);

    step(0, "A");                               /* floor = 0 */
    /* B fires at frame 5, but the t=100 input is still pending. */
    input_trace_actions_t a = step(5, "B");
    T_ASSERT_EQ_I(a.msg_count, 0);
    /* At frame 100 the input fires, then {wait:B} resolves to B's frame
     * (5), and capture:0 → frame 5 fires immediately. */
    a = step(100, 0);
    T_ASSERT_EQ_I(a.msg_count, 1);
    T_ASSERT_EQ_I(a.cap_count, 1);
    T_ASSERT_EQ_I(a.caps[0].frame, 5);          /* rebased onto B@5 */
    T_ASSERT_EQ_S(a.caps[0].label, "after_b");
    T_ASSERT(input_trace_done());
    return 0;
}

int test_input_trace_multiple_msgs_same_frame(void)
{
    const char *trace =
        "{\"wait\": \"TITLE\"}\n"
        "{\"t\": 3, \"m\": 512, \"w\": 0, \"l\": 100}\n"
        "{\"t\": 3, \"m\": 513, \"w\": 1, \"l\": 100}\n";
    input_trace_load_text(trace);
    step(0, "TITLE");
    input_trace_actions_t a = step(3, 0);
    T_ASSERT_EQ_I(a.msg_count, 2);
    T_ASSERT_EQ_U(a.msgs[0].msg, (unsigned)WM_MOUSEMOVE);
    T_ASSERT_EQ_U(a.msgs[1].msg, (unsigned)WM_LBUTTONDOWN);
    T_ASSERT(input_trace_done());
    return 0;
}
