/* OpenLords2 Trace Studio v3 — resident replay core (see replay_core.h).
 *
 * open() reads the whole container into memory and walks it once: each PALETTE and
 * PIXELS record registers a resource (indexed by its dense id; RAW planes are
 * referenced zero-copy into the file buffer, RLE planes are decoded once into an
 * owned buffer), and each FRAME record records a (palette_id, pixels_id,
 * present_idx) triple. render(idx) is then a palette expansion of the cited plane.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../format/olv3_format.h"
#include "replay_core.h"

typedef struct { const uint8_t *plane; int owned; } Pixels;   /* plane = w*h indices */
/* one draw's sparse write-delta (zero-copy into the file buffer) */
typedef struct { uint32_t op_type, m0, m1, m2, n; const uint32_t *offsets; const uint8_t *vals; } Draw;
typedef struct { uint32_t palette_id, pixels_id, present, base_id, draw_start, n_draws; } FrameRec;

struct Olv3Replay {
    uint8_t *data; size_t len;
    int W, H; uint32_t version;
    const uint8_t **pal;  int npal, cappal;   /* pal[id]   -> 256*4 bytes (into data)  */
    Pixels        *pix;   int npix, cappix;   /* pix[id].plane -> w*h indices          */
    Draw          *draws; int ndraws, capdraws;
    FrameRec      *frames; int nframes, capframes;
    uint8_t *buf;                              /* last render() BGRA (w*h*4)            */
    uint8_t *canvas;                           /* scratch index plane for stepped renders */
    int     *owner;                            /* per-pixel last-writer draw index (pick) */
};

static int grow_pal(Olv3Replay *r, uint32_t id)
{
    if ((int)id < r->cappal) return 1;
    int cap = r->cappal ? r->cappal : 64;
    while (cap <= (int)id) cap *= 2;
    const uint8_t **p = (const uint8_t **)realloc(r->pal, (size_t)cap * sizeof(*p));
    if (!p) return 0;
    for (int i = r->cappal; i < cap; i++) p[i] = NULL;
    r->pal = p; r->cappal = cap; return 1;
}
static int grow_pix(Olv3Replay *r, uint32_t id)
{
    if ((int)id < r->cappix) return 1;
    int cap = r->cappix ? r->cappix : 64;
    while (cap <= (int)id) cap *= 2;
    Pixels *p = (Pixels *)realloc(r->pix, (size_t)cap * sizeof(*p));
    if (!p) return 0;
    for (int i = r->cappix; i < cap; i++) { p[i].plane = NULL; p[i].owned = 0; }
    r->pix = p; r->cappix = cap; return 1;
}
static int push_draw(Olv3Replay *r, Draw d)
{
    if (r->ndraws == r->capdraws) {
        int cap = r->capdraws ? r->capdraws * 2 : 128;
        Draw *p = (Draw *)realloc(r->draws, (size_t)cap * sizeof(*p));
        if (!p) return 0;
        r->draws = p; r->capdraws = cap;
    }
    r->draws[r->ndraws++] = d;
    return 1;
}

Olv3Replay *olv3_open(const char *path, char *err, int errlen)
{
    #define FAIL(msg) do { if (err) snprintf(err, errlen, "%s", msg); olv3_close(R); return NULL; } while (0)
    Olv3Replay *R = (Olv3Replay *)calloc(1, sizeof(*R));
    if (!R) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) FAIL("cannot open container");
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); FAIL("empty container"); }
    R->len = (size_t)sz;
    R->data = (uint8_t *)malloc(R->len);
    if (!R->data || fread(R->data, 1, R->len, f) != R->len) { fclose(f); FAIL("read container"); }
    fclose(f);

    const uint8_t *p = R->data, *end = R->data + R->len;
    if (olv3_ru(&p, end) != OLV3_MAGIC) FAIL("bad magic");
    R->version = olv3_ru(&p, end);
    R->W = (int)olv3_ru(&p, end);
    R->H = (int)olv3_ru(&p, end);
    if (R->W <= 0 || R->H <= 0 || R->W > 8192 || R->H > 8192) FAIL("bad dimensions");
    const size_t plane_sz = (size_t)R->W * R->H;

    for (;;) {
        uint32_t t = olv3_ru(&p, end);
        if (t == OLV3_EOF || t == 0xffffffffu) break;
        if (t == OLV3_PALETTE) {
            uint32_t id = olv3_ru(&p, end);
            if (p + 1024 > end) FAIL("truncated palette");
            if (!grow_pal(R, id)) FAIL("oom palette");
            R->pal[id] = p; p += 1024;
            if ((int)id >= R->npal) R->npal = (int)id + 1;
        } else if (t == OLV3_PIXELS) {
            uint32_t id = olv3_ru(&p, end);
            uint32_t enc = olv3_ru(&p, end);
            uint32_t w = olv3_ru(&p, end), h = olv3_ru(&p, end);
            uint32_t dl = olv3_ru(&p, end);
            if (p + dl > end) FAIL("truncated pixels");
            if ((int)w != R->W || (int)h != R->H) FAIL("frame dims != container dims");
            if (!grow_pix(R, id)) FAIL("oom pixels");
            if (enc == OLV3_ENC_RAW) {
                if (dl != plane_sz) FAIL("raw pixels wrong length");
                R->pix[id].plane = p; R->pix[id].owned = 0;       /* zero-copy */
            } else if (enc == OLV3_ENC_RLE) {
                uint8_t *plane = (uint8_t *)malloc(plane_sz);
                if (!plane) FAIL("oom rle plane");
                if (olv3_rle_decode(p, dl, plane, plane_sz) != plane_sz) { free(plane); FAIL("bad rle stream"); }
                R->pix[id].plane = plane; R->pix[id].owned = 1;
            } else {
                FAIL("unknown pixels encoding");
            }
            if ((int)id >= R->npix) R->npix = (int)id + 1;
            p += dl;
        } else if (t == OLV3_DRAW) {
            Draw d;
            d.op_type = olv3_ru(&p, end); d.m0 = olv3_ru(&p, end);
            d.m1 = olv3_ru(&p, end); d.m2 = olv3_ru(&p, end);
            d.n = olv3_ru(&p, end);
            if (p + (size_t)d.n * 5 > end) FAIL("truncated draw");
            d.offsets = (const uint32_t *)p; p += (size_t)d.n * 4;   /* offsets[n] (may be unaligned) */
            d.vals    = p;                    p += (size_t)d.n;       /* vals[n]                       */
            if (!push_draw(R, d)) FAIL("oom draws");
        } else if (t == OLV3_FRAME) {
            uint32_t pal_id = olv3_ru(&p, end);
            uint32_t pix_id = olv3_ru(&p, end);
            uint32_t present = olv3_ru(&p, end);
            uint32_t base_id = pix_id, n_draws = 0;
            if (R->version >= 2) { base_id = olv3_ru(&p, end); n_draws = olv3_ru(&p, end); }
            if (R->nframes == R->capframes) {
                R->capframes = R->capframes ? R->capframes * 2 : 64;
                R->frames = (FrameRec *)realloc(R->frames, (size_t)R->capframes * sizeof(FrameRec));
                if (!R->frames) FAIL("oom frames");
            }
            FrameRec *fr = &R->frames[R->nframes];
            fr->palette_id = pal_id; fr->pixels_id = pix_id; fr->present = present;
            fr->base_id = base_id; fr->n_draws = n_draws;
            fr->draw_start = (uint32_t)R->ndraws - n_draws;   /* the n_draws DRAWs just parsed */
            if (n_draws > (uint32_t)R->ndraws) FAIL("frame cites more draws than seen");
            R->nframes++;
        } else {
            FAIL("unknown record type");
        }
    }
    if (R->nframes == 0) FAIL("no kept frames");
    R->buf = (uint8_t *)malloc(plane_sz * 4);
    R->canvas = (uint8_t *)malloc(plane_sz);
    R->owner = (int *)malloc(plane_sz * sizeof(int));
    if (!R->buf || !R->canvas || !R->owner) FAIL("oom render buffers");
    return R;
    #undef FAIL
}

int olv3_count (const Olv3Replay *r) { return r ? r->nframes : 0; }
int olv3_width (const Olv3Replay *r) { return r ? r->W : 0; }
int olv3_height(const Olv3Replay *r) { return r ? r->H : 0; }
int olv3_present(const Olv3Replay *r, int i) { return (r && i >= 0 && i < r->nframes) ? (int)r->frames[i].present : -1; }

const uint8_t *olv3_indices(Olv3Replay *r, int idx)
{
    if (!r || idx < 0 || idx >= r->nframes) return NULL;
    uint32_t pid = r->frames[idx].pixels_id;
    if ((int)pid >= r->npix) return NULL;
    return r->pix[pid].plane;
}

const uint8_t *olv3_palette(Olv3Replay *r, int idx)
{
    if (!r || idx < 0 || idx >= r->nframes) return NULL;
    uint32_t pid = r->frames[idx].palette_id;
    if ((int)pid >= r->npal) return NULL;
    return r->pal[pid];
}

const uint8_t *olv3_render(Olv3Replay *r, int idx)
{
    const uint8_t *plane = olv3_indices(r, idx);
    const uint8_t *pal   = olv3_palette(r, idx);
    if (!plane || !pal || !r->buf) return NULL;
    olv3_expand_bgra(plane, pal, r->W, r->H, r->buf);
    return r->buf;
}

void olv3_close(Olv3Replay *r)
{
    if (!r) return;
    if (r->pix) for (int i = 0; i < r->cappix; i++) if (r->pix[i].owned) free((void *)r->pix[i].plane);
    free(r->pix); free(r->pal); free(r->draws); free(r->frames);
    free(r->buf); free(r->canvas); free(r->owner); free(r->data); free(r);
}

/* ── draw-stepping (v2) ── */
int olv3_draws(const Olv3Replay *r, int idx)
{
    return (r && idx >= 0 && idx < r->nframes) ? (int)r->frames[idx].n_draws : -1;
}

int olv3_draw_meta(const Olv3Replay *r, int idx, int k, int *op_type, int *m0, int *m1, int *m2)
{
    if (!r || idx < 0 || idx >= r->nframes) return 0;
    const FrameRec *f = &r->frames[idx];
    if (k < 0 || (uint32_t)k >= f->n_draws) return 0;
    const Draw *d = &r->draws[f->draw_start + k];
    if (op_type) *op_type = (int)d->op_type;
    if (m0) *m0 = (int)d->m0;
    if (m1) *m1 = (int)d->m1;
    if (m2) *m2 = (int)d->m2;
    return 1;
}

/* one unaligned u32 from the offsets array (the file buffer isn't 4-aligned). */
static inline uint32_t draw_off(const Draw *d, uint32_t i)
{ uint32_t v; memcpy(&v, (const uint8_t *)d->offsets + (size_t)i * 4, 4); return v; }

/* Build the scratch canvas = base plane + the first `kdraws` draws applied in
 * order, recording per-pixel ownership (highest draw that wrote each pixel) in
 * r->owner. kdraws<0 ⇒ all of the frame's draws. Returns 0 if the frame has no
 * base (shouldn't happen) — then the caller falls back to the final plane. */
static int build_canvas(Olv3Replay *r, int idx, int kdraws)
{
    const FrameRec *f = &r->frames[idx];
    if ((int)f->base_id >= r->npix || !r->pix[f->base_id].plane) return 0;
    const size_t N = (size_t)r->W * r->H;
    memcpy(r->canvas, r->pix[f->base_id].plane, N);
    for (size_t i = 0; i < N; i++) r->owner[i] = -1;
    uint32_t lim = (kdraws < 0 || (uint32_t)kdraws > f->n_draws) ? f->n_draws : (uint32_t)kdraws;
    for (uint32_t k = 0; k < lim; k++) {
        const Draw *d = &r->draws[f->draw_start + k];
        for (uint32_t i = 0; i < d->n; i++) {
            uint32_t off = draw_off(d, i);
            if (off < N) { r->canvas[off] = d->vals[i]; r->owner[off] = (int)k; }
        }
    }
    return 1;
}

const uint8_t *olv3_render_upto(Olv3Replay *r, int idx, int kdraws)
{
    if (!r || idx < 0 || idx >= r->nframes) return NULL;
    const FrameRec *f = &r->frames[idx];
    /* full frame (or a frame with no draw capture) ⇒ the stored final plane */
    if (f->n_draws == 0 || (kdraws >= 0 && (uint32_t)kdraws >= f->n_draws))
        return olv3_render(r, idx);
    const uint8_t *pal = olv3_palette(r, idx);
    if (!pal || !build_canvas(r, idx, kdraws)) return NULL;
    olv3_expand_bgra(r->canvas, pal, r->W, r->H, r->buf);
    return r->buf;
}

const uint8_t *olv3_render_solo(Olv3Replay *r, int idx, int j)
{
    if (!r || idx < 0 || idx >= r->nframes) return NULL;
    const FrameRec *f = &r->frames[idx];
    const uint8_t *pal = olv3_palette(r, idx);
    if (!pal || (int)f->base_id >= r->npix || !r->pix[f->base_id].plane) return NULL;
    if (j < 0 || (uint32_t)j >= f->n_draws) return NULL;
    const size_t N = (size_t)r->W * r->H;
    memcpy(r->canvas, r->pix[f->base_id].plane, N);     /* base + ONLY draw j */
    const Draw *d = &r->draws[f->draw_start + j];
    for (uint32_t i = 0; i < d->n; i++) { uint32_t off = draw_off(d, i); if (off < N) r->canvas[off] = d->vals[i]; }
    olv3_expand_bgra(r->canvas, pal, r->W, r->H, r->buf);
    return r->buf;
}

int olv3_pick(Olv3Replay *r, int idx, int kdraws, int x, int y)
{
    if (!r || idx < 0 || idx >= r->nframes || x < 0 || y < 0 || x >= r->W || y >= r->H) return -2;
    if (!build_canvas(r, idx, kdraws)) return -2;
    return r->owner[(size_t)y * r->W + x];   /* -1 = no draw painted it (the base shows) */
}
