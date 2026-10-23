/* rh_shot.c — the presented frame at exact frames: PNG screenshots (--shot-at / --shot-every) and
 * the frame hash chain (--fb-hash). The frame is the fake DirectDraw primary (rh_ddraw.c): an exact
 * 8-bit index plane + palette, independent of any window / desktop / GPU.
 * (PNG writer: stored deflate blocks, no zlib — openrecet tools/rhost.) */
#include "rhost.h"

static uint32_t crc_tab[256];
static uint32_t crc32_upd(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; }
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8]; be32(h, n); memcpy(h + 4, type, 4); fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc32_upd(0xffffffffu, (const uint8_t *)type, 4); c = crc32_upd(c, d, n) ^ 0xffffffffu;
    uint8_t t[4]; be32(t, c); fwrite(t, 1, 4, f);
}
static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    size_t raw_n = (size_t)h * (1 + 3 * w);
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) { raw[y * (1 + 3 * w)] = 0; memcpy(raw + y * (1 + 3 * w) + 1, rgb + (size_t)y * 3 * w, 3 * w); }
    size_t nblk = (raw_n + 65534) / 65535, z_n = 2 + raw_n + nblk * 5 + 4;
    uint8_t *z = malloc(z_n), *o = z;
    *o++ = 0x78; *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw_n; off += 65535) {
        uint32_t len = (uint32_t)(raw_n - off < 65535 ? raw_n - off : 65535);
        *o++ = off + len >= raw_n; *o++ = len & 0xff; *o++ = len >> 8; *o++ = ~len & 0xff; *o++ = (~len >> 8) & 0xff;
        memcpy(o, raw + off, len); o += len;
    }
    be32(o, (b << 16) | a); o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ih[13]; be32(ih, w); be32(ih + 4, h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13); chunk(f, "IDAT", z, (uint32_t)(o - z)); chunk(f, "IEND", NULL, 0);
    fclose(f); free(raw); free(z);
    return 0;
}

void rh_shot(const char *tag)
{
    const uint8_t *pix; int pitch, w, h; const PALETTEENTRY *pal;
    int bpp = rh_dd_present(&pix, &pitch, &w, &h, &pal);
    if (bpp != 8) { rh_log("shot %s: no 8 bpp primary yet\n", tag); return; }
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const PALETTEENTRY *e = &pal[pix[y * pitch + x]];
            uint8_t *o = rgb + ((size_t)y * w + x) * 3;
            o[0] = e->peRed; o[1] = e->peGreen; o[2] = e->peBlue;
        }
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\shot_%s.png", O.rundir, tag);
    if (write_png(p, rgb, w, h)) rh_log("shot %s: cannot write %s\n", tag, p);
    free(rgb);
    /* the exact frame too: 640x480 index plane + 256 RGB palette entries (Trace Studio v3's input) */
    snprintf(p, sizeof p, "%s\\frame_%s.idx", O.rundir, tag);
    FILE *f = fopen(p, "wb");
    if (f) {
        uint32_t hd[4] = { 0x31584449u /* "IDX1" */, (uint32_t)w, (uint32_t)h, g_frame };
        fwrite(hd, 4, 4, f);
        for (int y = 0; y < h; y++) fwrite(pix + y * pitch, 1, (size_t)w, f);
        for (int i = 0; i < 256; i++) { uint8_t c[3] = { pal[i].peRed, pal[i].peGreen, pal[i].peBlue }; fwrite(c, 1, 3, f); }
        fclose(f);
    }
}

int rh_fb_hash(uint64_t *out)
{
    const uint8_t *pix; int pitch, w, h; const PALETTEENTRY *pal;
    if (rh_dd_present(&pix, &pitch, &w, &h, &pal) != 8) return 0;
    uint64_t f = 1469598103934665603ull;
    for (int y = 0; y < h; y++) {           /* 8 index bytes per step (w is a multiple of 8: 640) */
        const uint8_t *l = pix + y * pitch;
        int x = 0;
        for (; x + 8 <= w; x += 8) { uint64_t v; memcpy(&v, l + x, 8); f ^= v; f *= 1099511628211ull; f ^= f >> 29; }
        for (; x < w; x++) { f ^= l[x]; f *= 1099511628211ull; }
    }
    for (int i = 0; i < 256; i++) { f ^= (uint32_t)pal[i].peRed | (uint32_t)pal[i].peGreen << 8 | (uint32_t)pal[i].peBlue << 16; f *= 1099511628211ull; }
    *out = f;
    return 1;
}
