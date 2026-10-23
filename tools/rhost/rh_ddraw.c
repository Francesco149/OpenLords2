/* rh_ddraw.c — a software DirectDraw for retail Lords2 (8 bpp palettized, 640×480).
 *
 * Why fake it: the real DirectDraw needs a display device (an interactive session), takes the
 * screen over in retail's fullscreen path (GOG default: the desktop is not 8 bpp), and its
 * objects are system-DLL pointers that differ run to run. Here every surface is plain memory
 * in the fixed media pool (rh_media.c), every interface object lives in the heap arena with a
 * vtable copied to the fixed page — so retail's DirectDraw state hashes identically on every
 * host (Windows session 0, the desktop, wine), and the presented frame is an exact 8-bit index
 * plane + palette (the input of the Trace Studio v3 index/palette diffs).
 *
 * Model: the "display" is the primary surface's current memory with the primary's palette.
 * Flip swaps the memory of a flip chain (front ↔ back), as DirectDraw does. Blt / BltFast copy
 * rectangles with source color keys; COLORFILL fills. No stretching (logged if asked for).
 * GetDC uses an 8 bpp DIB section that is copied back on ReleaseDC (logged: retail is not known
 * to use it). Vtable slots retail is not known to use log "UNIMPL" and fail. */
#include "rhost.h"
#include <ddraw.h>

#define LOGDD(...) do { if (O.log_dd) rh_log(__VA_ARGS__); } while (0)
static long g_unimpl;
#define UNIMPL(what) do { if (g_unimpl++ < 50) rh_log("ddraw: UNIMPL %s (frame %u)\n", what, g_frame); return E_NOTIMPL; } while (0)

static const GUID IID_DD1  = { 0x6C14DB80, 0xA733, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };
static const GUID IID_DD2  = { 0xB3A6F3E0, 0x2B43, 0x11CF, { 0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56 } };
static const GUID IID_DDS1 = { 0x6C14DB81, 0xA733, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };
static const GUID IID_DDS2 = { 0x57805885, 0x6eec, 0x11cf, { 0x94, 0x41, 0xa8, 0x23, 0x03, 0xc1, 0x0e, 0x27 } };
static const GUID IID_DDS3 = { 0xDA044E00, 0x69B2, 0x11D0, { 0xA1, 0xD5, 0x00, 0xAA, 0x00, 0xB8, 0xDF, 0xBB } };
static const GUID IID_UNK  = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

typedef struct dd_pal  { const void *vtbl; uint32_t refs; uint32_t caps; PALETTEENTRY e[256]; } dd_pal;
typedef struct dd_clip { const void *vtbl; uint32_t refs; uint32_t hwnd; } dd_clip;
typedef struct dd_surf {
    const void *vtbl; uint32_t refs;
    uint32_t w, h, pitch, bpp, caps, flags;
    uint8_t *pix;
    struct dd_surf *back, *front;     /* flip chain: front->back, back->front */
    dd_pal *pal; dd_clip *clip;
    uint32_t has_ck_src, ck_src_lo, ck_src_hi, has_ck_dst, ck_dst_lo, ck_dst_hi;
    uint32_t locks, serial;
    uint32_t dib_dc, dib_bmp;          /* GetDC: real GDI objects (never seen by retail) */
} dd_surf;
typedef struct dd_obj { const void *vtbl; uint32_t refs; uint32_t coop, mode_w, mode_h, mode_bpp, ver; } dd_obj;

static dd_obj *g_dd;
static dd_surf *g_primary;
static uint32_t g_serial, g_nsurf, g_nblt, g_nfast, g_nflip, g_nlock;

/* ---- vtables: built in the host, copied to the fixed page (their address lands in retail state) ---- */
static void *g_vt_dd1, *g_vt_dd2, *g_vt_surf, *g_vt_pal, *g_vt_clip;
static void *fixed_vtbl(void **src, int n) { void *p = rh_fixed_alloc(n * 4); memcpy(p, src, n * 4); return p; }

static void pal_rgb_default(PALETTEENTRY *e) { for (int i = 0; i < 256; i++) { e[i].peRed = e[i].peGreen = e[i].peBlue = (BYTE)i; e[i].peFlags = 0; } }

/* ================================================================ IUnknown helpers */
static ULONG WINAPI unk_AddRef(void *self) { return ++((uint32_t *)self)[1]; }

/* ================================================================ palette */
static HRESULT WINAPI pal_QI(dd_pal *p, REFIID iid, void **out) { (void)iid; *out = p; p->refs++; return DD_OK; }
static ULONG WINAPI pal_Release(dd_pal *p) { if (--p->refs) return p->refs; rh_arena_free(p); return 0; }
static HRESULT WINAPI pal_GetCaps(dd_pal *p, DWORD *c) { *c = p->caps; return DD_OK; }
static HRESULT WINAPI pal_GetEntries(dd_pal *p, DWORD fl, DWORD start, DWORD n, PALETTEENTRY *e)
{ (void)fl; if (start + n > 256) return DDERR_INVALIDPARAMS; memcpy(e, p->e + start, n * sizeof *e); return DD_OK; }
static HRESULT WINAPI pal_Initialize(dd_pal *p, void *dd, DWORD fl, PALETTEENTRY *e) { (void)p; (void)dd; (void)fl; (void)e; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI pal_SetEntries(dd_pal *p, DWORD fl, DWORD start, DWORD n, PALETTEENTRY *e)
{
    (void)fl;
    if (start + n > 256) return DDERR_INVALIDPARAMS;
    memcpy(p->e + start, e, n * sizeof *e);
    LOGDD("ddraw: pal %p SetEntries %lu+%lu (f%u)\n", (void *)p, (unsigned long)start, (unsigned long)n, g_frame);
    return DD_OK;
}

/* ================================================================ clipper */
static HRESULT WINAPI clip_QI(dd_clip *c, REFIID iid, void **out) { (void)iid; *out = c; c->refs++; return DD_OK; }
static ULONG WINAPI clip_Release(dd_clip *c) { if (--c->refs) return c->refs; rh_arena_free(c); return 0; }
static HRESULT WINAPI clip_GetClipList(dd_clip *c, RECT *r, RGNDATA *d, DWORD *n) { (void)c; (void)r; (void)d; (void)n; UNIMPL("Clipper::GetClipList"); }
static HRESULT WINAPI clip_GetHWnd(dd_clip *c, HWND *h) { *h = (HWND)(uintptr_t)c->hwnd; return DD_OK; }
static HRESULT WINAPI clip_Initialize(dd_clip *c, void *dd, DWORD fl) { (void)c; (void)dd; (void)fl; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI clip_IsClipListChanged(dd_clip *c, BOOL *b) { (void)c; *b = FALSE; return DD_OK; }
static HRESULT WINAPI clip_SetClipList(dd_clip *c, RGNDATA *d, DWORD fl) { (void)c; (void)d; (void)fl; UNIMPL("Clipper::SetClipList"); }
static HRESULT WINAPI clip_SetHWnd(dd_clip *c, DWORD fl, HWND h) { (void)fl; c->hwnd = (uint32_t)(uintptr_t)h; return DD_OK; }

/* ================================================================ surfaces */
static dd_surf *surf_new(uint32_t w, uint32_t h, uint32_t bpp, uint32_t caps)
{
    dd_surf *s = rh_arena_alloc(sizeof *s);
    s->vtbl = g_vt_surf; s->refs = 1;
    s->w = w; s->h = h; s->bpp = bpp; s->caps = caps;
    s->pitch = (w * (bpp / 8) + 3) & ~3u;
    s->pix = rh_media_alloc(s->pitch * h);
    s->serial = ++g_serial; g_nsurf++;
    return s;
}
static void surf_destroy(dd_surf *s)
{
    if (s->back && s->back->front == s) { dd_surf *b = s->back; b->front = NULL; surf_destroy(b); }
    if (s->pal && !--s->pal->refs) rh_arena_free(s->pal);
    if (s->clip && !--s->clip->refs) rh_arena_free(s->clip);
    if (s == g_primary) g_primary = NULL;
    rh_media_free(s->pix);
    LOGDD("ddraw: surface #%u %ux%u destroyed (f%u)\n", s->serial, s->w, s->h, g_frame);
    rh_arena_free(s);
}
static void fill_desc(dd_surf *s, DDSURFACEDESC *d, uint8_t *at)
{
    DWORD sz = d->dwSize ? d->dwSize : sizeof *d;
    memset(d, 0, sz < sizeof *d ? sz : sizeof *d);
    d->dwSize = sz;
    d->dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | (at ? DDSD_LPSURFACE : 0);
    d->dwHeight = s->h; d->dwWidth = s->w; d->lPitch = (LONG)s->pitch;
    d->lpSurface = at;
    d->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    d->ddpfPixelFormat.dwFlags = DDPF_RGB | (s->bpp == 8 ? DDPF_PALETTEINDEXED8 : 0);
    d->ddpfPixelFormat.dwRGBBitCount = s->bpp;
    if (s->bpp == 16) { d->ddpfPixelFormat.dwRBitMask = 0xf800; d->ddpfPixelFormat.dwGBitMask = 0x07e0; d->ddpfPixelFormat.dwBBitMask = 0x001f; }
    if (s->bpp >= 24) { d->ddpfPixelFormat.dwRBitMask = 0xff0000; d->ddpfPixelFormat.dwGBitMask = 0xff00; d->ddpfPixelFormat.dwBBitMask = 0xff; }
    d->ddsCaps.dwCaps = s->caps;
    if (s->back) { d->dwFlags |= DDSD_BACKBUFFERCOUNT; d->dwBackBufferCount = 1; }
    if (s->has_ck_src) { d->dwFlags |= DDSD_CKSRCBLT; d->ddckCKSrcBlt.dwColorSpaceLowValue = s->ck_src_lo; d->ddckCKSrcBlt.dwColorSpaceHighValue = s->ck_src_hi; }
}

static HRESULT WINAPI s_QI(dd_surf *s, REFIID iid, void **out)
{
    if (!memcmp(iid, &IID_DDS1, 16) || !memcmp(iid, &IID_DDS2, 16) || !memcmp(iid, &IID_DDS3, 16) || !memcmp(iid, &IID_UNK, 16))
    { *out = s; s->refs++; return DD_OK; }
    *out = NULL; rh_log("ddraw: Surface::QueryInterface unknown IID %08lx\n", (unsigned long)iid->Data1);
    return E_NOINTERFACE;
}
static ULONG WINAPI s_Release(dd_surf *s)
{
    if (--s->refs) return s->refs;
    if (s->front) { s->refs = 1; return 0; }   /* an attached back buffer lives as long as its front */
    surf_destroy(s);
    return 0;
}
static HRESULT WINAPI s_AddAttachedSurface(dd_surf *s, dd_surf *a) { (void)s; (void)a; UNIMPL("Surface::AddAttachedSurface"); }
static HRESULT WINAPI s_AddOverlayDirtyRect(dd_surf *s, RECT *r) { (void)s; (void)r; UNIMPL("Surface::AddOverlayDirtyRect"); }

/* clip r to the surface; returns 0 if empty; *cut = 1 if it had to clip */
static int rect_of(dd_surf *s, const RECT *in, RECT *r, int *cut)
{
    if (in) *r = *in; else { r->left = r->top = 0; r->right = (LONG)s->w; r->bottom = (LONG)s->h; }
    RECT o = *r;
    if (r->left < 0) r->left = 0;
    if (r->top < 0) r->top = 0;
    if (r->right > (LONG)s->w) r->right = (LONG)s->w;
    if (r->bottom > (LONG)s->h) r->bottom = (LONG)s->h;
    *cut = memcmp(&o, r, sizeof o) != 0;
    return r->right > r->left && r->bottom > r->top;
}
static void copy_rect(dd_surf *d, LONG dx, LONG dy, dd_surf *s, const RECT *sr, int key, uint32_t klo, uint32_t khi)
{
    uint32_t bpp = d->bpp / 8, w = (uint32_t)(sr->right - sr->left), h = (uint32_t)(sr->bottom - sr->top);
    uint8_t *dp = d->pix + (uint32_t)dy * d->pitch + (uint32_t)dx * bpp;
    const uint8_t *sp = s->pix + (uint32_t)sr->top * s->pitch + (uint32_t)sr->left * bpp;
    int up = (s->pix == d->pix && dp > sp);          /* overlapping self-blit: copy bottom-up */
    for (uint32_t yy = 0; yy < h; yy++) {
        uint32_t y = up ? h - 1 - yy : yy;
        uint8_t *dl = dp + y * d->pitch; const uint8_t *sl = sp + y * s->pitch;
        if (!key) { memmove(dl, sl, w * bpp); continue; }
        if (bpp == 1) { for (uint32_t x = 0; x < w; x++) { uint8_t v = sl[x]; if (v < klo || v > khi) dl[x] = v; } }
        else if (bpp == 2) { for (uint32_t x = 0; x < w; x++) { uint16_t v = ((const uint16_t *)sl)[x]; if (v < klo || v > khi) ((uint16_t *)dl)[x] = v; } }
        else { for (uint32_t x = 0; x < w; x++) { uint32_t v = 0; memcpy(&v, sl + x * bpp, bpp); if (v < klo || v > khi) memcpy(dl + x * bpp, &v, bpp); } }
    }
}
static void fill_rect(dd_surf *d, const RECT *r, uint32_t c)
{
    uint32_t bpp = d->bpp / 8;
    for (LONG y = r->top; y < r->bottom; y++) {
        uint8_t *l = d->pix + (uint32_t)y * d->pitch + (uint32_t)r->left * bpp;
        if (bpp == 1) memset(l, (int)c, (size_t)(r->right - r->left));
        else for (LONG x = r->left; x < r->right; x++, l += bpp) memcpy(l, &c, bpp);
    }
}
static HRESULT WINAPI s_Blt(dd_surf *d, RECT *dr_in, dd_surf *s, RECT *sr_in, DWORD fl, DDBLTFX *fx)
{
    RECT dr; int cut;
    g_nblt++;
    if (!rect_of(d, dr_in, &dr, &cut)) return DD_OK;
    if (cut && !d->clip) LOGDD("ddraw: Blt dst rect clipped (no clipper) f%u\n", g_frame);
    if (fl & DDBLT_COLORFILL) {
        fill_rect(d, &dr, fx ? fx->dwFillColor : 0);
        return DD_OK;
    }
    if (!s) { if (g_unimpl++ < 50) rh_log("ddraw: Blt flags %08lx without source (f%u)\n", (unsigned long)fl, g_frame); return DDERR_INVALIDPARAMS; }
    RECT sr; int scut;
    if (!rect_of(s, sr_in, &sr, &scut)) return DD_OK;
    LONG dw = (dr_in ? dr_in->right - dr_in->left : (LONG)d->w), dh = (dr_in ? dr_in->bottom - dr_in->top : (LONG)d->h);
    LONG sw = (sr_in ? sr_in->right - sr_in->left : (LONG)s->w), sh = (sr_in ? sr_in->bottom - sr_in->top : (LONG)s->h);
    if (dw != sw || dh != sh) {
        if (g_unimpl++ < 50) rh_log("ddraw: Blt STRETCH %ldx%ld -> %ldx%ld (f%u) — copying unscaled\n", sw, sh, dw, dh, g_frame);
    }
    /* keep the source/dest correspondence when the dest rect was clipped */
    LONG ox = dr.left - (dr_in ? dr_in->left : 0), oy = dr.top - (dr_in ? dr_in->top : 0);
    sr.left += ox; sr.top += oy;
    LONG w = dr.right - dr.left, h = dr.bottom - dr.top;
    if (sr.left + w > sr.right) w = sr.right - sr.left;
    if (sr.top + h > sr.bottom) h = sr.bottom - sr.top;
    if (w <= 0 || h <= 0) return DD_OK;
    sr.right = sr.left + w; sr.bottom = sr.top + h;
    int key = 0; uint32_t klo = 0, khi = 0;
    if (fl & DDBLT_KEYSRCOVERRIDE) { key = 1; klo = fx->ddckSrcColorkey.dwColorSpaceLowValue; khi = fx->ddckSrcColorkey.dwColorSpaceHighValue; }
    else if ((fl & DDBLT_KEYSRC) && s->has_ck_src) { key = 1; klo = s->ck_src_lo; khi = s->ck_src_hi; }
    if (khi < klo) khi = klo;
    if (fl & (DDBLT_KEYDEST | DDBLT_KEYDESTOVERRIDE)) if (g_unimpl++ < 50) rh_log("ddraw: Blt dest color key unsupported (f%u)\n", g_frame);
    copy_rect(d, dr.left, dr.top, s, &sr, key, klo, khi);
    return DD_OK;
}
static HRESULT WINAPI s_BltBatch(dd_surf *s, DDBLTBATCH *b, DWORD n, DWORD fl) { (void)s; (void)b; (void)n; (void)fl; UNIMPL("Surface::BltBatch"); }
static HRESULT WINAPI s_BltFast(dd_surf *d, DWORD x, DWORD y, dd_surf *s, RECT *sr_in, DWORD trans)
{
    RECT sr; int cut;
    g_nfast++;
    if (!s || !rect_of(s, sr_in, &sr, &cut)) return DD_OK;
    LONG w = sr.right - sr.left, h = sr.bottom - sr.top;
    if ((LONG)x + w > (LONG)d->w || (LONG)y + h > (LONG)d->h || cut) {
        LOGDD("ddraw: BltFast out of bounds %lu,%lu %ldx%ld on %ux%u (f%u)\n", (unsigned long)x, (unsigned long)y, w, h, d->w, d->h, g_frame);
        return DDERR_INVALIDRECT;
    }
    int key = (trans & DDBLTFAST_SRCCOLORKEY) && s->has_ck_src;
    if (trans & DDBLTFAST_DESTCOLORKEY) if (g_unimpl++ < 50) rh_log("ddraw: BltFast dest color key unsupported (f%u)\n", g_frame);
    copy_rect(d, (LONG)x, (LONG)y, s, &sr, key, s->ck_src_lo, s->ck_src_hi > s->ck_src_lo ? s->ck_src_hi : s->ck_src_lo);
    return DD_OK;
}
static HRESULT WINAPI s_DeleteAttachedSurface(dd_surf *s, DWORD fl, dd_surf *a) { (void)s; (void)fl; (void)a; UNIMPL("Surface::DeleteAttachedSurface"); }
static HRESULT WINAPI s_EnumAttachedSurfaces(dd_surf *s, void *ctx, LPDDENUMSURFACESCALLBACK cb)
{
    if (s->back) {
        DDSURFACEDESC d; d.dwSize = sizeof d; fill_desc(s->back, &d, NULL);
        s->back->refs++;
        cb((IDirectDrawSurface *)s->back, &d, ctx);
    }
    return DD_OK;
}
static HRESULT WINAPI s_EnumOverlayZOrders(dd_surf *s, DWORD fl, void *ctx, void *cb) { (void)s; (void)fl; (void)ctx; (void)cb; UNIMPL("Surface::EnumOverlayZOrders"); }
static HRESULT WINAPI s_Flip(dd_surf *s, dd_surf *over, DWORD fl)
{
    (void)fl;
    dd_surf *b = over ? over : s->back;
    g_nflip++;
    if (!b) return DDERR_NOTFLIPPABLE;
    uint8_t *t = s->pix; s->pix = b->pix; b->pix = t;
    return DD_OK;
}
static HRESULT WINAPI s_GetAttachedSurface(dd_surf *s, DDSCAPS *caps, dd_surf **out)
{
    if (s->back && (caps->dwCaps & (DDSCAPS_BACKBUFFER | DDSCAPS_FLIP))) { *out = s->back; s->back->refs++; return DD_OK; }
    *out = NULL;
    return DDERR_NOTFOUND;
}
static HRESULT WINAPI s_GetBltStatus(dd_surf *s, DWORD fl) { (void)s; (void)fl; return DD_OK; }
static HRESULT WINAPI s_GetCaps(dd_surf *s, DDSCAPS *c) { c->dwCaps = s->caps; return DD_OK; }
static HRESULT WINAPI s_GetClipper(dd_surf *s, dd_clip **c) { *c = s->clip; if (!s->clip) return DDERR_NOCLIPPERATTACHED; s->clip->refs++; return DD_OK; }
static HRESULT WINAPI s_GetColorKey(dd_surf *s, DWORD fl, DDCOLORKEY *k)
{
    if (fl & DDCKEY_SRCBLT) { if (!s->has_ck_src) return DDERR_NOCOLORKEY; k->dwColorSpaceLowValue = s->ck_src_lo; k->dwColorSpaceHighValue = s->ck_src_hi; return DD_OK; }
    if (fl & DDCKEY_DESTBLT) { if (!s->has_ck_dst) return DDERR_NOCOLORKEY; k->dwColorSpaceLowValue = s->ck_dst_lo; k->dwColorSpaceHighValue = s->ck_dst_hi; return DD_OK; }
    return DDERR_INVALIDPARAMS;
}
/* GetDC: an 8 bpp DIB section with the surface's palette, copied back on ReleaseDC */
static HRESULT WINAPI s_GetDC(dd_surf *s, HDC *out)
{
    rh_log("ddraw: Surface::GetDC #%u (f%u)\n", s->serial, g_frame);
    struct { BITMAPINFOHEADER h; RGBQUAD c[256]; } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = (LONG)s->w; bi.h.biHeight = -(LONG)s->h; bi.h.biPlanes = 1; bi.h.biBitCount = (WORD)s->bpp;
    const PALETTEENTRY *pe = s->pal ? s->pal->e : (g_primary && g_primary->pal ? g_primary->pal->e : NULL);
    for (int i = 0; i < 256 && pe; i++) { bi.c[i].rgbRed = pe[i].peRed; bi.c[i].rgbGreen = pe[i].peGreen; bi.c[i].rgbBlue = pe[i].peBlue; }
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm) { DeleteDC(dc); return DDERR_GENERIC; }
    SelectObject(dc, bm);
    uint32_t dpitch = (s->w * (s->bpp / 8) + 3) & ~3u;
    for (uint32_t y = 0; y < s->h; y++) memcpy((uint8_t *)bits + y * dpitch, s->pix + y * s->pitch, s->w * (s->bpp / 8));
    s->dib_dc = (uint32_t)(uintptr_t)dc; s->dib_bmp = (uint32_t)(uintptr_t)bm;
    *out = dc;
    return DD_OK;
}
static HRESULT WINAPI s_ReleaseDC(dd_surf *s, HDC dc)
{
    if (!s->dib_dc || (uint32_t)(uintptr_t)dc != s->dib_dc) return DDERR_INVALIDPARAMS;
    DIBSECTION ds;
    if (GetObjectA((HBITMAP)(uintptr_t)s->dib_bmp, sizeof ds, &ds)) {
        uint32_t dpitch = (uint32_t)ds.dsBm.bmWidthBytes;
        GdiFlush();
        for (uint32_t y = 0; y < s->h; y++) memcpy(s->pix + y * s->pitch, (uint8_t *)ds.dsBm.bmBits + y * dpitch, s->w * (s->bpp / 8));
    }
    DeleteDC(dc); DeleteObject((HBITMAP)(uintptr_t)s->dib_bmp);
    s->dib_dc = s->dib_bmp = 0;
    return DD_OK;
}
static HRESULT WINAPI s_GetFlipStatus(dd_surf *s, DWORD fl) { (void)s; (void)fl; return DD_OK; }
static HRESULT WINAPI s_GetOverlayPosition(dd_surf *s, LONG *x, LONG *y) { (void)s; (void)x; (void)y; UNIMPL("Surface::GetOverlayPosition"); }
static HRESULT WINAPI s_GetPalette(dd_surf *s, dd_pal **p) { *p = s->pal; if (!s->pal) return DDERR_NOPALETTEATTACHED; s->pal->refs++; return DD_OK; }
static HRESULT WINAPI s_GetPixelFormat(dd_surf *s, DDPIXELFORMAT *pf)
{ DDSURFACEDESC d; d.dwSize = sizeof d; fill_desc(s, &d, NULL); *pf = d.ddpfPixelFormat; return DD_OK; }
static HRESULT WINAPI s_GetSurfaceDesc(dd_surf *s, DDSURFACEDESC *d) { fill_desc(s, d, NULL); return DD_OK; }
static HRESULT WINAPI s_Initialize(dd_surf *s, void *dd, DDSURFACEDESC *d) { (void)s; (void)dd; (void)d; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI s_IsLost(dd_surf *s) { (void)s; return DD_OK; }
static HRESULT WINAPI s_Lock(dd_surf *s, RECT *r, DDSURFACEDESC *d, DWORD fl, HANDLE ev)
{
    (void)fl; (void)ev;
    uint8_t *at = s->pix;
    if (r) at += (uint32_t)r->top * s->pitch + (uint32_t)r->left * (s->bpp / 8);
    fill_desc(s, d, at);
    s->locks++; g_nlock++;
    return DD_OK;
}
static HRESULT WINAPI s_Restore(dd_surf *s) { (void)s; return DD_OK; }
static HRESULT WINAPI s_SetClipper(dd_surf *s, dd_clip *c)
{
    if (c) c->refs++;
    if (s->clip && !--s->clip->refs) rh_arena_free(s->clip);
    s->clip = c;
    return DD_OK;
}
static HRESULT WINAPI s_SetColorKey(dd_surf *s, DWORD fl, DDCOLORKEY *k)
{
    if (fl & DDCKEY_SRCBLT) { s->has_ck_src = k != NULL; if (k) { s->ck_src_lo = k->dwColorSpaceLowValue; s->ck_src_hi = k->dwColorSpaceHighValue; } return DD_OK; }
    if (fl & DDCKEY_DESTBLT) { s->has_ck_dst = k != NULL; if (k) { s->ck_dst_lo = k->dwColorSpaceLowValue; s->ck_dst_hi = k->dwColorSpaceHighValue; } return DD_OK; }
    if (g_unimpl++ < 50) rh_log("ddraw: SetColorKey flags %08lx unsupported\n", (unsigned long)fl);
    return DDERR_INVALIDPARAMS;
}
static HRESULT WINAPI s_SetOverlayPosition(dd_surf *s, LONG x, LONG y) { (void)s; (void)x; (void)y; UNIMPL("Surface::SetOverlayPosition"); }
static HRESULT WINAPI s_SetPalette(dd_surf *s, dd_pal *p)
{
    if (p) p->refs++;
    if (s->pal && !--s->pal->refs) rh_arena_free(s->pal);
    s->pal = p;
    if (s->back && s->back->front == s) { if (p) p->refs++; if (s->back->pal && !--s->back->pal->refs) rh_arena_free(s->back->pal); s->back->pal = p; }
    return DD_OK;
}
static HRESULT WINAPI s_Unlock(dd_surf *s, void *p) { (void)p; if (s->locks) s->locks--; return DD_OK; }
static HRESULT WINAPI s_UpdateOverlay(dd_surf *s, RECT *a, dd_surf *d, RECT *b, DWORD fl, void *fx) { (void)s; (void)a; (void)d; (void)b; (void)fl; (void)fx; UNIMPL("Surface::UpdateOverlay"); }
static HRESULT WINAPI s_UpdateOverlayDisplay(dd_surf *s, DWORD fl) { (void)s; (void)fl; UNIMPL("Surface::UpdateOverlayDisplay"); }
static HRESULT WINAPI s_UpdateOverlayZOrder(dd_surf *s, DWORD fl, dd_surf *r) { (void)s; (void)fl; (void)r; UNIMPL("Surface::UpdateOverlayZOrder"); }
/* IDirectDrawSurface2/3 tail */
static HRESULT WINAPI s_GetDDInterface(dd_surf *s, void **out) { (void)s; *out = g_dd; if (g_dd) g_dd->refs++; return DD_OK; }
static HRESULT WINAPI s_PageLock(dd_surf *s, DWORD fl) { (void)s; (void)fl; return DD_OK; }
static HRESULT WINAPI s_PageUnlock(dd_surf *s, DWORD fl) { (void)s; (void)fl; return DD_OK; }
static HRESULT WINAPI s_SetSurfaceDesc(dd_surf *s, DDSURFACEDESC *d, DWORD fl) { (void)s; (void)d; (void)fl; UNIMPL("Surface3::SetSurfaceDesc"); }

/* ================================================================ IDirectDraw (v1 / v2) */
static HRESULT WINAPI dd_QI(dd_obj *o, REFIID iid, void **out)
{
    if (!memcmp(iid, &IID_DD1, 16) || !memcmp(iid, &IID_UNK, 16)) { *out = o; o->refs++; return DD_OK; }
    if (!memcmp(iid, &IID_DD2, 16)) {
        dd_obj *o2 = rh_arena_alloc(sizeof *o2);
        *o2 = *o; o2->vtbl = g_vt_dd2; o2->refs = 1; o2->ver = 2;
        *out = o2;
        LOGDD("ddraw: QueryInterface(IDirectDraw2)\n");
        return DD_OK;
    }
    *out = NULL;
    rh_log("ddraw: DirectDraw::QueryInterface unknown IID %08lx\n", (unsigned long)iid->Data1);
    return E_NOINTERFACE;
}
static ULONG WINAPI dd_Release(dd_obj *o) { if (--o->refs) return o->refs; if (o == g_dd) g_dd = NULL; rh_arena_free(o); return 0; }
static HRESULT WINAPI dd_Compact(dd_obj *o) { (void)o; return DD_OK; }
static HRESULT WINAPI dd_CreateClipper(dd_obj *o, DWORD fl, dd_clip **out, void *outer)
{
    (void)o; (void)fl; (void)outer;
    dd_clip *c = rh_arena_alloc(sizeof *c);
    c->vtbl = g_vt_clip; c->refs = 1;
    *out = c;
    return DD_OK;
}
static HRESULT WINAPI dd_CreatePalette(dd_obj *o, DWORD fl, PALETTEENTRY *e, dd_pal **out, void *outer)
{
    (void)o; (void)outer;
    dd_pal *p = rh_arena_alloc(sizeof *p);
    p->vtbl = g_vt_pal; p->refs = 1; p->caps = fl;
    if (e) memcpy(p->e, e, sizeof p->e); else pal_rgb_default(p->e);
    *out = p;
    LOGDD("ddraw: CreatePalette flags %08lx (f%u)\n", (unsigned long)fl, g_frame);
    return DD_OK;
}
static HRESULT WINAPI dd_CreateSurface(dd_obj *o, DDSURFACEDESC *d, dd_surf **out, void *outer)
{
    (void)outer;
    uint32_t caps = (d->dwFlags & DDSD_CAPS) ? d->ddsCaps.dwCaps : 0;
    uint32_t w, h, bpp = o->mode_bpp;
    if (caps & DDSCAPS_PRIMARYSURFACE) { w = o->mode_w; h = o->mode_h; }
    else {
        if (!(d->dwFlags & DDSD_WIDTH) || !(d->dwFlags & DDSD_HEIGHT)) return DDERR_INVALIDPARAMS;
        w = d->dwWidth; h = d->dwHeight;
    }
    if (d->dwFlags & DDSD_PIXELFORMAT) bpp = d->ddpfPixelFormat.dwRGBBitCount;
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) bpp = 8;
    uint32_t mem = (caps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY)) ? 0 : DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
    dd_surf *s = surf_new(w, h, bpp, caps | mem);
    if (caps & DDSCAPS_PRIMARYSURFACE) {
        s->caps |= DDSCAPS_VISIBLE | DDSCAPS_FRONTBUFFER * !!(caps & DDSCAPS_FLIP);
        if ((d->dwFlags & DDSD_BACKBUFFERCOUNT) && d->dwBackBufferCount) {
            if (d->dwBackBufferCount > 1) rh_log("ddraw: %lu back buffers asked, giving 1\n", (unsigned long)d->dwBackBufferCount);
            dd_surf *b = surf_new(w, h, bpp, (caps & ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE)) | DDSCAPS_BACKBUFFER | mem);
            b->front = s; s->back = b;
        }
        g_primary = s;
    }
    *out = s;
    LOGDD("ddraw: CreateSurface #%u %ux%ux%u caps %08x%s (f%u)\n", s->serial, w, h, bpp, s->caps, s->back ? " +back" : "", g_frame);
    return DD_OK;
}
static HRESULT WINAPI dd_DuplicateSurface(dd_obj *o, dd_surf *s, dd_surf **out) { (void)o; (void)s; (void)out; UNIMPL("DirectDraw::DuplicateSurface"); }
static HRESULT WINAPI dd_EnumDisplayModes(dd_obj *o, DWORD fl, DDSURFACEDESC *f, void *ctx, LPDDENUMMODESCALLBACK cb)
{
    (void)o; (void)fl; (void)f;
    static const uint32_t modes[][3] = { { 640, 480, 8 }, { 640, 480, 16 }, { 800, 600, 8 }, { 800, 600, 16 }, { 1024, 768, 8 }, { 1024, 768, 16 } };
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) {
        DDSURFACEDESC d; memset(&d, 0, sizeof d); d.dwSize = sizeof d;
        d.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;
        d.dwWidth = modes[i][0]; d.dwHeight = modes[i][1]; d.lPitch = (LONG)(modes[i][0] * modes[i][2] / 8);
        d.ddpfPixelFormat.dwSize = sizeof d.ddpfPixelFormat; d.ddpfPixelFormat.dwFlags = DDPF_RGB | (modes[i][2] == 8 ? DDPF_PALETTEINDEXED8 : 0);
        d.ddpfPixelFormat.dwRGBBitCount = modes[i][2];
        if (cb(&d, ctx) == DDENUMRET_CANCEL) break;
    }
    return DD_OK;
}
static HRESULT WINAPI dd_EnumSurfaces(dd_obj *o, DWORD fl, DDSURFACEDESC *d, void *ctx, void *cb) { (void)o; (void)fl; (void)d; (void)ctx; (void)cb; UNIMPL("DirectDraw::EnumSurfaces"); }
static HRESULT WINAPI dd_FlipToGDISurface(dd_obj *o) { (void)o; return DD_OK; }
static HRESULT WINAPI dd_GetCaps(dd_obj *o, DDCAPS *drv, DDCAPS *hel)
{
    (void)o;
    DDCAPS *c[2] = { drv, hel };
    for (int i = 0; i < 2; i++) {
        if (!c[i]) continue;
        DWORD sz = c[i]->dwSize ? c[i]->dwSize : sizeof(DDCAPS);
        memset(c[i], 0, sz < sizeof(DDCAPS) ? sz : sizeof(DDCAPS));
        c[i]->dwSize = sz;
        c[i]->dwCaps = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_COLORKEY | DDCAPS_PALETTE | DDCAPS_CANCLIP | DDCAPS_CANBLTSYSMEM;
        c[i]->dwCKeyCaps = DDCKEYCAPS_SRCBLT;
        c[i]->dwPalCaps = DDPCAPS_8BIT | DDPCAPS_PRIMARYSURFACE;
        c[i]->ddsCaps.dwCaps = DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_PALETTE |
                               DDSCAPS_PRIMARYSURFACE | DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY | DDSCAPS_COMPLEX;
        if (i == 0) { c[i]->dwVidMemTotal = 8u << 20; c[i]->dwVidMemFree = 6u << 20; }
    }
    return DD_OK;
}
static HRESULT WINAPI dd_GetDisplayMode(dd_obj *o, DDSURFACEDESC *d)
{
    dd_surf t; memset(&t, 0, sizeof t); t.w = o->mode_w; t.h = o->mode_h; t.bpp = o->mode_bpp; t.pitch = o->mode_w * o->mode_bpp / 8;
    fill_desc(&t, d, NULL);
    d->dwFlags &= ~DDSD_CAPS;
    return DD_OK;
}
static HRESULT WINAPI dd_GetFourCCCodes(dd_obj *o, DWORD *n, DWORD *codes) { (void)o; (void)codes; *n = 0; return DD_OK; }
static HRESULT WINAPI dd_GetGDISurface(dd_obj *o, dd_surf **out) { (void)o; *out = g_primary; if (!g_primary) return DDERR_NOTFOUND; g_primary->refs++; return DD_OK; }
static HRESULT WINAPI dd_GetMonitorFrequency(dd_obj *o, DWORD *f) { (void)o; *f = 60; return DD_OK; }
static HRESULT WINAPI dd_GetScanLine(dd_obj *o, DWORD *l) { (void)o; *l = 0; return DD_OK; }
static HRESULT WINAPI dd_GetVerticalBlankStatus(dd_obj *o, BOOL *b) { (void)o; *b = TRUE; return DD_OK; }
static HRESULT WINAPI dd_Initialize(dd_obj *o, GUID *g) { (void)o; (void)g; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI dd_RestoreDisplayMode(dd_obj *o) { (void)o; return DD_OK; }
static HRESULT WINAPI dd_SetCooperativeLevel(dd_obj *o, HWND h, DWORD fl)
{ (void)h; o->coop = fl; LOGDD("ddraw: SetCooperativeLevel %08lx (f%u)\n", (unsigned long)fl, g_frame); return DD_OK; }
static HRESULT WINAPI dd_SetDisplayMode1(dd_obj *o, DWORD w, DWORD h, DWORD bpp)
{
    o->mode_w = w; o->mode_h = h; o->mode_bpp = bpp;
    if (g_dd && g_dd != o) { g_dd->mode_w = w; g_dd->mode_h = h; g_dd->mode_bpp = bpp; }
    rh_log("ddraw: SetDisplayMode %lux%lux%lu (virtual) (f%u)\n", (unsigned long)w, (unsigned long)h, (unsigned long)bpp, g_frame);
    return DD_OK;
}
static HRESULT WINAPI dd_SetDisplayMode2(dd_obj *o, DWORD w, DWORD h, DWORD bpp, DWORD hz, DWORD fl) { (void)hz; (void)fl; return dd_SetDisplayMode1(o, w, h, bpp); }
static HRESULT WINAPI dd_WaitForVerticalBlank(dd_obj *o, DWORD fl, HANDLE ev) { (void)o; (void)fl; (void)ev; return DD_OK; }
static HRESULT WINAPI dd_GetAvailableVidMem(dd_obj *o, DDSCAPS *c, DWORD *tot, DWORD *fr) { (void)o; (void)c; if (tot) *tot = 8u << 20; if (fr) *fr = 6u << 20; return DD_OK; }

HRESULT WINAPI rh_DirectDrawCreate(GUID *g, void **out, void *outer)
{
    (void)g; (void)outer;
    if (!g_vt_dd1) {
        void *vdd1[] = { dd_QI, unk_AddRef, dd_Release, dd_Compact, dd_CreateClipper, dd_CreatePalette, dd_CreateSurface,
                         dd_DuplicateSurface, dd_EnumDisplayModes, dd_EnumSurfaces, dd_FlipToGDISurface, dd_GetCaps,
                         dd_GetDisplayMode, dd_GetFourCCCodes, dd_GetGDISurface, dd_GetMonitorFrequency, dd_GetScanLine,
                         dd_GetVerticalBlankStatus, dd_Initialize, dd_RestoreDisplayMode, dd_SetCooperativeLevel,
                         dd_SetDisplayMode1, dd_WaitForVerticalBlank };
        void *vdd2[24];
        memcpy(vdd2, vdd1, sizeof vdd1); vdd2[21] = dd_SetDisplayMode2; vdd2[23] = dd_GetAvailableVidMem;
        void *vs[] = { s_QI, unk_AddRef, s_Release, s_AddAttachedSurface, s_AddOverlayDirtyRect, s_Blt, s_BltBatch, s_BltFast,
                       s_DeleteAttachedSurface, s_EnumAttachedSurfaces, s_EnumOverlayZOrders, s_Flip, s_GetAttachedSurface,
                       s_GetBltStatus, s_GetCaps, s_GetClipper, s_GetColorKey, s_GetDC, s_GetFlipStatus, s_GetOverlayPosition,
                       s_GetPalette, s_GetPixelFormat, s_GetSurfaceDesc, s_Initialize, s_IsLost, s_Lock, s_ReleaseDC, s_Restore,
                       s_SetClipper, s_SetColorKey, s_SetOverlayPosition, s_SetPalette, s_Unlock, s_UpdateOverlay,
                       s_UpdateOverlayDisplay, s_UpdateOverlayZOrder, s_GetDDInterface, s_PageLock, s_PageUnlock, s_SetSurfaceDesc };
        void *vp[] = { pal_QI, unk_AddRef, pal_Release, pal_GetCaps, pal_GetEntries, pal_Initialize, pal_SetEntries };
        void *vc[] = { clip_QI, unk_AddRef, clip_Release, clip_GetClipList, clip_GetHWnd, clip_Initialize, clip_IsClipListChanged,
                       clip_SetClipList, clip_SetHWnd };
        g_vt_dd1 = fixed_vtbl(vdd1, 23); g_vt_dd2 = fixed_vtbl(vdd2, 24);
        g_vt_surf = fixed_vtbl(vs, sizeof vs / 4); g_vt_pal = fixed_vtbl(vp, 7); g_vt_clip = fixed_vtbl(vc, 9);
    }
    dd_obj *o = rh_arena_alloc(sizeof *o);
    o->vtbl = g_vt_dd1; o->refs = 1; o->ver = 1;
    o->mode_w = 640; o->mode_h = 480; o->mode_bpp = 32;   /* the "desktop" (rh_win.c GetDeviceCaps agrees) */
    if (!g_dd) g_dd = o;
    *out = o;
    rh_log("ddraw: DirectDrawCreate -> software DirectDraw %p (f%u)\n", (void *)o, g_frame);
    return DD_OK;
}

/* the displayed frame: the primary's memory + its palette */
int rh_dd_present(const uint8_t **pix, int *pitch, int *w, int *h, const PALETTEENTRY **pal)
{
    if (!g_primary) return 0;
    static PALETTEENTRY gray[256];
    if (!gray[255].peRed) pal_rgb_default(gray);
    *pix = g_primary->pix; *pitch = (int)g_primary->pitch; *w = (int)g_primary->w; *h = (int)g_primary->h;
    *pal = g_primary->pal ? g_primary->pal->e : gray;
    return (int)g_primary->bpp;
}

void rh_dd_report(void)
{
    rh_log("ddraw: surfaces created=%u blt=%u bltfast=%u flip=%u lock=%u unimpl=%ld media=%uK\n",
           g_nsurf, g_nblt, g_nfast, g_nflip, g_nlock, g_unimpl, rh_media_used() >> 10);
}
