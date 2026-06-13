/* OpenLords2 Trace Studio v3 — capture container format (shared: writers + replay).
 *
 * A flat, streaming, self-describing record sequence — the same philosophy as the
 * openrecet sibling's orv3_format.h, but tailored to what Lords of the Realm II
 * actually IS: an 8bpp / 640×480 DirectDraw game that software-composites every
 * sprite into ONE back-surface and presents it with a single Blt. Retail's
 * per-sprite draws are INVISIBLE at the DDraw boundary, so (unlike openrecet's
 * D3D8 command stream) there is no draw program to replay — the reconstructable
 * unit is the back-surface itself: an 8-bit INDEX plane + the 256-entry PALETTE.
 *
 * Reconstructing a frame is therefore a palette LUT expansion (index → BGRA),
 * sub-millisecond and pure-CPU — no GPU, no replay device, no DWM fragility. Both
 * engines already produce this exact unit at the present seam (the retail Frida
 * agent's captureFrameOnce; the port's locked 8bpp gfx canvas + gfx_palette_rgb),
 * so ONE container format captures BOTH sides bit-identically and the compare is
 * fair.
 *
 * MULTI-FRAME WINDOW + CONTENT-HASH DEDUP: a container holds a WINDOW of frames.
 * Palettes and index planes are written the first time their CONTENT is seen
 * (assigned an id, dedup'd by content hash); later frames cite the id. A palette
 * that never changes across the window is stored ONCE; a static screen's pixels
 * are stored ONCE. So window storage ≈ (#distinct palettes) + (#distinct frames)
 * rather than N × 300 KB.
 *
 * Streaming (no seeking, no global tables): a writer appends records as frames
 * arrive; a reader walks them once, creating each resource as it appears and
 * committing a frame at each FRAME record. Native endian (both ends are i686).
 * Each record: [u32 type] then a fixed payload per type.
 *
 * Per-frame ENGINE STATE (the save-schema slot snapshot) is NOT stored here — it
 * lives in the Python-emitted view.json (per-column, identity-keyed), exactly as
 * the openrecet viewer carries it. The container stays purely visual + minimal.
 */
#ifndef OLV3_FORMAT_H
#define OLV3_FORMAT_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OLV3_MAGIC   0x33564C4Fu   /* "OLV3" (little-endian bytes O,L,V,3) */
#define OLV3_VERSION 1u

/* Container = [u32 MAGIC][u32 VERSION][u32 W][u32 H] then a record stream. */
enum {
    OLV3_PALETTE = 2,   /* id, 256*4 bytes: per entry R,G,B,flags (DDraw PALETTEENTRY order) */
    OLV3_PIXELS  = 3,   /* id, encoding, w, h, datalen, data[] (8bpp index plane, see ENC_*) */
    OLV3_FRAME   = 10,  /* palette_id, pixels_id, present_idx — the kept-frame commit marker */
    OLV3_EOF     = 99,
};

/* PIXELS payload encodings. RAW always works; the others are size optimisations a
 * writer MAY use and every reader MUST handle. (Phase-1 writers emit RAW; the
 * decoder below already understands RLE so a writer can opt in with zero format
 * churn.) */
enum {
    OLV3_ENC_RAW = 0,   /* w*h bytes, one index per pixel, tight rows                       */
    OLV3_ENC_RLE = 1,   /* byte-run length encoding: repeated [u8 count][u8 value], count>0 */
};

/* ── tiny write helpers (writers) ── */
static inline void olv3_wu(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static inline void olv3_wbytes(FILE *f, const void *p, uint32_t n) { fwrite(&n, 4, 1, f); if (n) fwrite(p, 1, n, f); }

/* ── tiny read helper (readers): u32 at *pp, advancing it; 0xffffffff past end ── */
static inline uint32_t olv3_ru(const uint8_t **pp, const uint8_t *end)
{
    if (*pp + 4 > end) return 0xffffffffu;
    uint32_t v; memcpy(&v, *pp, 4); *pp += 4; return v;
}

/* FNV-1a 64 over a byte range — the content hash writers use to dedup palettes
 * and index planes (a palette/frame whose bytes were already emitted reuses its
 * id). Shared so a C writer and the Python writer agree on identity. */
static inline uint64_t olv3_fnv1a(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

/* ── RLE codec (encoding == OLV3_ENC_RLE) ──
 * Stream of [u8 count][u8 value] pairs, count in 1..255. Decode is the only side
 * the replay core needs; encode is here so a writer can shrink a container in
 * place. Returns bytes written to `out` (== w*h on a well-formed stream), or 0 on
 * overflow/short input. */
static inline size_t olv3_rle_decode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap)
{
    size_t o = 0, i = 0;
    while (i + 2 <= inlen) {
        uint32_t cnt = in[i++], val = in[i++];
        if (!cnt || o + cnt > outcap) return 0;
        memset(out + o, (int)val, cnt); o += cnt;
    }
    return o;
}
static inline size_t olv3_rle_encode(const uint8_t *in, size_t inlen, uint8_t *out, size_t outcap)
{
    size_t o = 0, i = 0;
    while (i < inlen) {
        uint8_t v = in[i]; size_t run = 1;
        while (i + run < inlen && in[i + run] == v && run < 255) run++;
        if (o + 2 > outcap) return 0;
        out[o++] = (uint8_t)run; out[o++] = v; i += run;
    }
    return o;
}

/* Expand an 8bpp index plane through a 256-entry R,G,B,flags palette into a tight
 * w*h*4 BGRA buffer (caller-allocated). The ONE reconstruction primitive — used by
 * the replay core for display and by any headless verify. BGRA (B,G,R,255) is what
 * the viewer's D3D9 X8R8G8B8 texture upload and a .bmp expect. Alpha forced opaque. */
static inline void olv3_expand_bgra(const uint8_t *idx, const uint8_t *pal_rgbx,
                                    int w, int h, uint8_t *out_bgra)
{
    const int n = w * h;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = pal_rgbx + (size_t)idx[i] * 4;   /* R,G,B,flags */
        uint8_t *o = out_bgra + (size_t)i * 4;
        o[0] = e[2]; o[1] = e[1]; o[2] = e[0]; o[3] = 255;  /* B,G,R,A */
    }
}

#endif /* OLV3_FORMAT_H */
