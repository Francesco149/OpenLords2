/*
 * OpenLords2 — port-side TAS input-trace replay.  See input_trace.h.
 */

#include <stdio.h>
#include <string.h>

#include "input_trace.h"

/* ─── parsed op list ──────────────────────────────────────────────── */

enum { OP_INPUT, OP_WAIT, OP_CAPTURE, OP_AUTODISMISS };

typedef struct {
    int  kind;
    /* INPUT */
    int      t;
    unsigned msg, wparam;
    long     lparam;
    /* WAIT */
    char anchor[INPUT_TRACE_MAX_ANCHOR_NAME];
    /* CAPTURE */
    int  n;
    char label[INPUT_TRACE_MAX_LABEL];
} op_t;

static op_t g_ops[INPUT_TRACE_MAX_OPS];
static int  g_op_count;

/* ─── replay state ────────────────────────────────────────────────── */

static int g_cursor;        /* next op to process */
static int g_seg_floor;     /* frame the current segment's anchor opened */

/* Anchors observed since the trace started, with the frame each fired —
 * a {wait} resolves against this set, so an anchor that fired BEFORE the
 * cursor reached its {wait} (a "fast anchor", e.g. the map flips before a
 * click's mouseup op is posted) still satisfies it. */
static struct { char name[INPUT_TRACE_MAX_ANCHOR_NAME]; int frame; }
            g_seen[INPUT_TRACE_MAX_ANCHORS];
static int  g_seen_count;

/* ─── tiny JSON-field extractors (fixed flat schema) ──────────────── */

/* Find `key` (quoted, e.g. "\"t\"") then `: <int>`.  Returns 1 + value. */
static int extract_int(const char *line, const char *key, long *out)
{
    const char *p = strstr(line, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') return 0;
    long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    *out = neg ? -v : v;
    return 1;
}

/* Find `key` then `: "<string>"`.  Copies into buf (capped). */
static int extract_str(const char *line, const char *key, char *buf, int n)
{
    const char *p = strstr(line, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < n - 1) buf[i++] = *p++;
    buf[i] = 0;
    return (*p == '"');
}

/* Parse one trace line, appending an op (or skipping comment/blank/
 * wait_until/unknown).  Returns 0 always (a malformed line is skipped, not
 * fatal — mirrors the lenient retail loader). */
static void parse_line(const char *line)
{
    /* Trim leading whitespace; skip blanks + `#` comments. */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '\n' || *line == '\r' || *line == '#')
        return;
    if (g_op_count >= INPUT_TRACE_MAX_OPS)
        return;

    op_t *op = &g_ops[g_op_count];
    memset(op, 0, sizeof *op);

    if (strstr(line, "\"wait_until\"")) {
        /* Parsed-and-ignored: the port has no global VA-read predicate yet
         * (the load flow uses only {wait}).  Drop the op. */
        return;
    }
    if (strstr(line, "\"wait\"")) {
        op->kind = OP_WAIT;
        if (!extract_str(line, "\"wait\"", op->anchor, sizeof op->anchor))
            return;
        g_op_count++;
        return;
    }
    if (strstr(line, "\"capture\"")) {
        long n = 0;
        op->kind = OP_CAPTURE;
        if (!extract_int(line, "\"capture\"", &n))
            return;
        op->n = (int)n;
        extract_str(line, "\"label\"", op->label, sizeof op->label); /* optional */
        g_op_count++;
        return;
    }
    if (strstr(line, "\"auto_dismiss\"")) {
        /* Reactive mode toggle: while on, the engine dismisses any open
         * modal popup each tick (right-click path, position-independent).
         * Drains immediately (not anchor-relative).  Value 1 = on, 0 = off. */
        long v = 1;
        op->kind = OP_AUTODISMISS;
        extract_int(line, "\"auto_dismiss\"", &v);   /* default on */
        op->n = (int)v;
        g_op_count++;
        return;
    }
    if (strstr(line, "\"m\"")) {
        long t = 0, m = 0, w = 0, l = 0;
        op->kind = OP_INPUT;
        if (!extract_int(line, "\"t\"", &t)) return;
        if (!extract_int(line, "\"m\"", &m)) return;
        extract_int(line, "\"w\"", &w);   /* default 0 */
        extract_int(line, "\"l\"", &l);   /* default 0 */
        op->t      = (int)t;
        op->msg    = (unsigned)m;
        op->wparam = (unsigned)w;
        op->lparam = l;
        g_op_count++;
        return;
    }
    /* Unknown line — skip. */
}

/* ─── public API ──────────────────────────────────────────────────── */

void input_trace_reset(void)
{
    g_op_count   = 0;
    g_cursor     = 0;
    g_seg_floor  = 0;
    g_seen_count = 0;
}

int input_trace_load_text(const char *text)
{
    input_trace_reset();
    if (!text) return 1;
    char line[512];
    int  i = 0;
    for (const char *p = text;; p++) {
        if (*p == '\n' || *p == '\0') {
            line[i] = '\0';
            parse_line(line);
            i = 0;
            if (*p == '\0') break;
        } else if (i < (int)sizeof line - 1) {
            line[i++] = *p;
        }
    }
    return 0;
}

int input_trace_load(const char *path)
{
    input_trace_reset();
    FILE *f = fopen(path, "r");
    if (!f) return 1;
    char line[512];
    while (fgets(line, sizeof line, f))
        parse_line(line);
    fclose(f);
    return 0;
}

int input_trace_op_count(void) { return g_op_count; }
int input_trace_done(void)     { return g_cursor >= g_op_count; }

static int anchor_seen_frame(const char *name, int *frame)
{
    for (int i = 0; i < g_seen_count; i++) {
        if (strcmp(g_seen[i].name, name) == 0) {
            if (frame) *frame = g_seen[i].frame;
            return 1;
        }
    }
    return 0;
}

void input_trace_step(int frame, const char *anchor_edge,
                      input_trace_actions_t *out)
{
    memset(out, 0, sizeof *out);
    out->set_auto_dismiss = -1;        /* no change unless an op drains below */

    /* Remember a newly-fired anchor (first occurrence wins). */
    if (anchor_edge && anchor_edge[0] && !anchor_seen_frame(anchor_edge, 0) &&
        g_seen_count < INPUT_TRACE_MAX_ANCHORS) {
        strncpy(g_seen[g_seen_count].name, anchor_edge,
                INPUT_TRACE_MAX_ANCHOR_NAME - 1);
        g_seen[g_seen_count].name[INPUT_TRACE_MAX_ANCHOR_NAME - 1] = '\0';
        g_seen[g_seen_count].frame = frame;
        g_seen_count++;
    }

    /* Drain due ops in order.  Input/capture ops fire at seg_floor + t/n;
     * a {wait} parks the cursor until its anchor is seen, then rebases the
     * floor onto the anchor's frame. */
    while (g_cursor < g_op_count) {
        op_t *op = &g_ops[g_cursor];

        if (op->kind == OP_WAIT) {
            int afr = 0;
            if (anchor_seen_frame(op->anchor, &afr)) {
                g_seg_floor = afr;     /* rebase the t-clock onto the anchor */
                g_cursor++;
                continue;
            }
            out->blocked = 1;          /* park until the anchor fires */
            break;
        }

        if (op->kind == OP_INPUT) {
            if (frame < g_seg_floor + op->t)
                break;                 /* not due — later ops aren't either */
            if (out->msg_count < INPUT_TRACE_MAX_FRAME_ACTIONS) {
                out->msgs[out->msg_count].msg    = op->msg;
                out->msgs[out->msg_count].wparam = op->wparam;
                out->msgs[out->msg_count].lparam = op->lparam;
                out->msg_count++;
            }
            g_cursor++;
            continue;
        }

        if (op->kind == OP_AUTODISMISS) {
            /* Mode toggle — drains immediately (no t-gate); the caller
             * applies it to the engine's reactive-dismiss flag. */
            out->set_auto_dismiss = op->n;
            g_cursor++;
            continue;
        }

        if (op->kind == OP_CAPTURE) {
            if (frame < g_seg_floor + op->n)
                break;
            if (out->cap_count < INPUT_TRACE_MAX_FRAME_ACTIONS) {
                out->caps[out->cap_count].frame = g_seg_floor + op->n;
                strncpy(out->caps[out->cap_count].label, op->label,
                        INPUT_TRACE_MAX_LABEL - 1);
                out->caps[out->cap_count].label[INPUT_TRACE_MAX_LABEL - 1] = '\0';
                out->cap_count++;
            }
            g_cursor++;
            continue;
        }

        g_cursor++;   /* defensive: unknown kind */
    }
}
