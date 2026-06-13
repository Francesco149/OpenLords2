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
typedef struct { uint32_t palette_id, pixels_id, present; } FrameRec;

struct Olv3Replay {
    uint8_t *data; size_t len;
    int W, H;
    const uint8_t **pal;  int npal, cappal;   /* pal[id]   -> 256*4 bytes (into data)  */
    Pixels        *pix;   int npix, cappix;   /* pix[id].plane -> w*h indices          */
    FrameRec      *frames; int nframes, capframes;
    uint8_t *buf;                              /* last render() BGRA (w*h*4)            */
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
    olv3_ru(&p, end); /* version */
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
        } else if (t == OLV3_FRAME) {
            uint32_t pal_id = olv3_ru(&p, end);
            uint32_t pix_id = olv3_ru(&p, end);
            uint32_t present = olv3_ru(&p, end);
            if (R->nframes == R->capframes) {
                R->capframes = R->capframes ? R->capframes * 2 : 64;
                R->frames = (FrameRec *)realloc(R->frames, (size_t)R->capframes * sizeof(FrameRec));
                if (!R->frames) FAIL("oom frames");
            }
            R->frames[R->nframes].palette_id = pal_id;
            R->frames[R->nframes].pixels_id  = pix_id;
            R->frames[R->nframes].present    = present;
            R->nframes++;
        } else {
            FAIL("unknown record type");
        }
    }
    if (R->nframes == 0) FAIL("no kept frames");
    R->buf = (uint8_t *)malloc(plane_sz * 4);
    if (!R->buf) FAIL("oom render buffer");
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
    free(r->pix); free(r->pal); free(r->frames); free(r->buf); free(r->data); free(r);
}
