/*
 * OpenLords2 — per-draw write-journal (see gfx_drawcap.h).
 *
 * State machine, per armed frame:
 *   begin_frame()         armed=1, have_base=0, pending=invalid, lists empty
 *   draw(meta)            finalize prev pending → diff shadow vs canvas;
 *                         on first draw snapshot base+shadow from canvas;
 *                         stage new pending meta
 *   flush()  (at unlock)  finalize the last pending (canvas still bound)
 *   write(path,present)   serialize base + draws to the sidecar
 *   end_frame()           armed=0 (buffers kept for reuse)
 *
 * Writes are stored in two shared growable arrays (offsets[] u32, vals[] u8); a
 * per-draw header carries {op,m0,m1,m2, start, count} indexing into them.  A
 * finalize with a zero-count delta records no header (empty-skip).
 */
#include "gfx_drawcap.h"
#include "gfx_paint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRAWCAP_MAGIC \
    (((uint32_t)'O') | ((uint32_t)'L' << 8) | ((uint32_t)'D' << 16) | ((uint32_t)'R' << 24))

typedef struct {
    uint32_t op_type, m0, m1, m2;
    uint32_t start, count;        /* slice of the shared offsets[]/vals[] arrays */
} DrawHdr;

/* ── scratch state (retained across frames; grows as needed) ── */
static uint8_t  *g_base;          /* canvas at the first draw (w*h)              */
static uint8_t  *g_shadow;        /* running canvas mirror for delta computation */
static int       g_cap_plane;     /* allocated size of g_base / g_shadow         */

static DrawHdr  *g_hdrs;          /* one per recorded (non-empty) draw           */
static int       g_nhdr, g_caphdr;

static uint32_t *g_offs;          /* changed-pixel offsets, all draws            */
static uint8_t  *g_vals;          /* changed-pixel new values, all draws         */
static int       g_nwrite, g_capwrite;

static int       g_w, g_h;        /* dims captured at base snapshot              */
static int       g_armed;
static int       g_have_base;
static int       g_pending_valid;
static uint32_t  g_pend_op, g_pend_m0, g_pend_m1, g_pend_m2;

static int ensure_plane(int n)
{
    if (g_cap_plane >= n && g_base && g_shadow) return 1;
    uint8_t *b = (uint8_t *)realloc(g_base, (size_t)n);
    uint8_t *s = (uint8_t *)realloc(g_shadow, (size_t)n);
    if (!b || !s) { free(b ? b : g_base); free(s ? s : g_shadow);
                    g_base = g_shadow = NULL; g_cap_plane = 0; return 0; }
    g_base = b; g_shadow = s; g_cap_plane = n;
    return 1;
}

static int ensure_writes(int extra)
{
    if (g_nwrite + extra <= g_capwrite) return 1;
    int cap = g_capwrite ? g_capwrite : 65536;
    while (cap < g_nwrite + extra) cap *= 2;
    uint32_t *o = (uint32_t *)realloc(g_offs, (size_t)cap * sizeof(*o));
    uint8_t  *v = (uint8_t  *)realloc(g_vals, (size_t)cap * sizeof(*v));
    if (!o || !v) { /* keep whichever realloc'd; a later append will retry */
        if (o) g_offs = o;
        if (v) g_vals = v;
        return 0;
    }
    g_offs = o; g_vals = v; g_capwrite = cap;
    return 1;
}

static int push_hdr(uint32_t op, uint32_t m0, uint32_t m1, uint32_t m2,
                    uint32_t start, uint32_t count)
{
    if (g_nhdr == g_caphdr) {
        int cap = g_caphdr ? g_caphdr * 2 : 256;
        DrawHdr *h = (DrawHdr *)realloc(g_hdrs, (size_t)cap * sizeof(*h));
        if (!h) return 0;
        g_hdrs = h; g_caphdr = cap;
    }
    g_hdrs[g_nhdr].op_type = op; g_hdrs[g_nhdr].m0 = m0;
    g_hdrs[g_nhdr].m1 = m1; g_hdrs[g_nhdr].m2 = m2;
    g_hdrs[g_nhdr].start = start; g_hdrs[g_nhdr].count = count;
    g_nhdr++;
    return 1;
}

/* Close the pending draw: append every shadow≠canvas pixel as a write, advance
 * the shadow, and record a header iff the delta is non-empty. */
static void finalize_pending(const uint8_t *canvas)
{
    if (!g_pending_valid) return;
    g_pending_valid = 0;
    const int n = g_w * g_h;
    const uint32_t start = (uint32_t)g_nwrite;
    for (int i = 0; i < n; i++) {
        if (g_shadow[i] != canvas[i]) {
            if (!ensure_writes(1)) break;          /* OOM: truncate this draw */
            g_offs[g_nwrite] = (uint32_t)i;
            g_vals[g_nwrite] = canvas[i];
            g_nwrite++;
            g_shadow[i] = canvas[i];
        }
    }
    uint32_t count = (uint32_t)g_nwrite - start;
    if (count) push_hdr(g_pend_op, g_pend_m0, g_pend_m1, g_pend_m2, start, count);
}

void gfx_drawcap_begin_frame(void)
{
    g_armed = 1;
    g_have_base = 0;
    g_pending_valid = 0;
    g_nhdr = 0;
    g_nwrite = 0;
}

int gfx_drawcap_armed(void) { return g_armed; }

void gfx_drawcap_draw(int op_type, int m0, int m1, int m2)
{
    if (!g_armed) return;
    uint8_t *canvas = gfx_paint_canvas_pixels();
    if (!canvas) return;
    int w = gfx_paint_canvas_width();
    int h = gfx_paint_canvas_height();
    if (w <= 0 || h <= 0) return;

    if (g_pending_valid)
        finalize_pending(canvas);

    if (!g_have_base) {
        if (!ensure_plane(w * h)) return;          /* OOM: stay disarmed-ish */
        g_w = w; g_h = h;
        memcpy(g_base, canvas, (size_t)w * h);
        memcpy(g_shadow, canvas, (size_t)w * h);
        g_have_base = 1;
    }

    g_pend_op = (uint32_t)op_type;
    g_pend_m0 = (uint32_t)m0;
    g_pend_m1 = (uint32_t)m1;
    g_pend_m2 = (uint32_t)m2;
    g_pending_valid = 1;
}

void gfx_drawcap_flush(void)
{
    if (!g_armed || !g_pending_valid) return;
    uint8_t *canvas = gfx_paint_canvas_pixels();
    if (!canvas) { g_pending_valid = 0; return; }
    finalize_pending(canvas);
}

int gfx_drawcap_count(void)    { return g_nhdr; }
int gfx_drawcap_has_base(void) { return g_have_base; }

int gfx_drawcap_write(const char *path, unsigned present)
{
    if (!path || !g_have_base) return 0;
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    uint32_t hdr[5] = { DRAWCAP_MAGIC, (uint32_t)g_w, (uint32_t)g_h,
                        present, (uint32_t)g_nhdr };
    fwrite(hdr, sizeof(uint32_t), 5, fp);
    fwrite(g_base, 1, (size_t)g_w * g_h, fp);
    for (int k = 0; k < g_nhdr; k++) {
        const DrawHdr *d = &g_hdrs[k];
        uint32_t rec[5] = { d->op_type, d->m0, d->m1, d->m2, d->count };
        fwrite(rec, sizeof(uint32_t), 5, fp);
        fwrite(g_offs + d->start, sizeof(uint32_t), d->count, fp);
        fwrite(g_vals + d->start, 1, d->count, fp);
    }
    fclose(fp);
    return 1;
}

void gfx_drawcap_end_frame(void)
{
    g_armed = 0;
    g_pending_valid = 0;
}
