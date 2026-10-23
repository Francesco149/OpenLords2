/* rh_dsound.c — a silent, deterministic DirectSound + a virtual multimedia timer for retail Lords2.
 *
 * Retail streams music/sound into DirectSound buffers from a timeSetEvent callback
 * (FUN_00426dd5, armed by FUN_00426b92) that normally runs on winmm's timer thread and reads
 * the buffers' play cursors. Here:
 *   - buffers are memory in the fixed media pool; the play cursor advances with the VIRTUAL
 *     clock (rate = the buffer's format / SetFrequency), looping or stopping at the end;
 *   - timeSetEvent callbacks are fired on the MAIN thread at frame boundaries (rh_mm_frame),
 *     in id order, once per elapsed period — a deterministic serialization of the real thread.
 * Nothing is audible (yet): a mixer to a real output for --record is future work. */
#include "rhost.h"
#include <mmsystem.h>
#include <dsound.h>

#define LOGDS(...) do { if (O.log_ds) rh_log(__VA_ARGS__); } while (0)
static long g_unimpl;
#define UNIMPL(what) do { if (g_unimpl++ < 50) rh_log("dsound: UNIMPL %s (frame %u)\n", what, g_frame); return E_NOTIMPL; } while (0)

typedef struct ds_mem { uint32_t refs, size; uint8_t *p; } ds_mem;
typedef struct ds_buf {
    const void *vtbl; uint32_t refs;
    ds_mem *mem; uint32_t flags;
    WAVEFORMATEX fmt;
    uint32_t playing, looping, pos, t0, freq;
    int32_t vol, pan;
    uint32_t serial;
} ds_buf;
typedef struct ds_obj { const void *vtbl; uint32_t refs; uint32_t coop, speaker; } ds_obj;

static void *g_vt_ds, *g_vt_buf;
static uint32_t g_serial, g_nplay, g_nlock;
static ULONG WINAPI unk_AddRef(void *self) { return ++((uint32_t *)self)[1]; }

static uint32_t bytes_per_sec(ds_buf *b)
{
    uint32_t f = b->freq ? b->freq : b->fmt.nSamplesPerSec;
    return f * (b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1);
}
/* bring the play cursor up to the virtual now */
static void advance(ds_buf *b)
{
    uint32_t now = rh_vnow();
    if (!b->playing || !b->mem->size) { b->t0 = now; return; }
    uint64_t adv = (uint64_t)(now - b->t0) * bytes_per_sec(b) / 1000u;
    uint32_t ba = b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1;
    adv -= adv % ba;
    if (!adv) return;
    b->t0 = now;
    uint64_t p = b->pos + adv;
    if (b->looping) b->pos = (uint32_t)(p % b->mem->size);
    else if (p >= b->mem->size) { b->pos = 0; b->playing = 0; }
    else b->pos = (uint32_t)p;
}

/* ---------------------------------------------------------------- buffers */
static HRESULT WINAPI b_QI(ds_buf *b, REFIID iid, void **out) { (void)iid; *out = b; b->refs++; return DS_OK; }
static ULONG WINAPI b_Release(ds_buf *b)
{
    if (--b->refs) return b->refs;
    if (!--b->mem->refs) { rh_media_free(b->mem->p); rh_arena_free(b->mem); }
    rh_arena_free(b);
    return 0;
}
static HRESULT WINAPI b_GetCaps(ds_buf *b, DSBCAPS *c) { c->dwFlags = b->flags; c->dwBufferBytes = b->mem->size; c->dwUnlockTransferRate = 0; c->dwPlayCpuOverhead = 0; return DS_OK; }
static HRESULT WINAPI b_GetCurrentPosition(ds_buf *b, DWORD *play, DWORD *write)
{
    advance(b);
    uint32_t lead = bytes_per_sec(b) * 15u / 1000u, ba = b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1;
    lead -= lead % ba;
    if (play) *play = b->pos;
    if (write) *write = b->mem->size ? (b->playing ? (b->pos + lead) % b->mem->size : b->pos) : 0;
    return DS_OK;
}
static HRESULT WINAPI b_GetFormat(ds_buf *b, WAVEFORMATEX *f, DWORD n, DWORD *got)
{
    DWORD need = sizeof(WAVEFORMATEX);
    if (got) *got = need;
    if (f) memcpy(f, &b->fmt, n < need ? n : need);
    return DS_OK;
}
static HRESULT WINAPI b_GetVolume(ds_buf *b, LONG *v) { *v = b->vol; return DS_OK; }
static HRESULT WINAPI b_GetPan(ds_buf *b, LONG *p) { *p = b->pan; return DS_OK; }
static HRESULT WINAPI b_GetFrequency(ds_buf *b, DWORD *f) { *f = b->freq ? b->freq : b->fmt.nSamplesPerSec; return DS_OK; }
static HRESULT WINAPI b_GetStatus(ds_buf *b, DWORD *s) { advance(b); *s = (b->playing ? DSBSTATUS_PLAYING : 0) | (b->playing && b->looping ? DSBSTATUS_LOOPING : 0); return DS_OK; }
static HRESULT WINAPI b_Initialize(ds_buf *b, void *ds, DSBUFFERDESC *d) { (void)b; (void)ds; (void)d; return DSERR_ALREADYINITIALIZED; }
static HRESULT WINAPI b_Lock(ds_buf *b, DWORD off, DWORD n, void **p1, DWORD *n1, void **p2, DWORD *n2, DWORD fl)
{
    uint32_t size = b->mem->size;
    if (fl & DSBLOCK_FROMWRITECURSOR) { DWORD w; b_GetCurrentPosition(b, NULL, &w); off = w; }
    if (fl & DSBLOCK_ENTIREBUFFER) n = size;
    if (!size || off >= size || n > size) return DSERR_INVALIDPARAM;
    g_nlock++;
    *p1 = b->mem->p + off;
    if (off + n <= size) { *n1 = n; if (p2) *p2 = NULL; if (n2) *n2 = 0; }
    else { *n1 = size - off; if (p2) *p2 = b->mem->p; if (n2) *n2 = n - (size - off); }
    return DS_OK;
}
static HRESULT WINAPI b_Play(ds_buf *b, DWORD r1, DWORD r2, DWORD fl)
{
    (void)r1; (void)r2;
    advance(b);
    b->playing = 1; b->looping = (fl & DSBPLAY_LOOPING) != 0; b->t0 = rh_vnow();
    g_nplay++;
    LOGDS("dsound: buf #%u Play%s (f%u)\n", b->serial, b->looping ? " loop" : "", g_frame);
    return DS_OK;
}
static HRESULT WINAPI b_SetCurrentPosition(ds_buf *b, DWORD p)
{ advance(b); if (b->mem->size) b->pos = p % b->mem->size; b->t0 = rh_vnow(); return DS_OK; }
static HRESULT WINAPI b_SetFormat(ds_buf *b, const WAVEFORMATEX *f) { advance(b); memcpy(&b->fmt, f, sizeof b->fmt); b->fmt.cbSize = 0; return DS_OK; }
static HRESULT WINAPI b_SetVolume(ds_buf *b, LONG v) { b->vol = v; return DS_OK; }
static HRESULT WINAPI b_SetPan(ds_buf *b, LONG p) { b->pan = p; return DS_OK; }
static HRESULT WINAPI b_SetFrequency(ds_buf *b, DWORD f) { advance(b); b->freq = f; return DS_OK; }
static HRESULT WINAPI b_Stop(ds_buf *b) { advance(b); b->playing = 0; return DS_OK; }
static HRESULT WINAPI b_Unlock(ds_buf *b, void *p1, DWORD n1, void *p2, DWORD n2) { (void)b; (void)p1; (void)n1; (void)p2; (void)n2; return DS_OK; }
static HRESULT WINAPI b_Restore(ds_buf *b) { (void)b; return DS_OK; }

static ds_buf *buf_new(ds_mem *m, const WAVEFORMATEX *f, uint32_t flags)
{
    ds_buf *b = rh_arena_alloc(sizeof *b);
    b->vtbl = g_vt_buf; b->refs = 1; b->mem = m; m->refs++; b->flags = flags;
    if (f) memcpy(&b->fmt, f, sizeof b->fmt);
    else { b->fmt.wFormatTag = WAVE_FORMAT_PCM; b->fmt.nChannels = 2; b->fmt.nSamplesPerSec = 22050; b->fmt.wBitsPerSample = 8; b->fmt.nBlockAlign = 2; b->fmt.nAvgBytesPerSec = 44100; }
    b->fmt.cbSize = 0;
    b->t0 = rh_vnow(); b->serial = ++g_serial;
    return b;
}

/* ---------------------------------------------------------------- IDirectSound */
static HRESULT WINAPI d_QI(ds_obj *d, REFIID iid, void **out) { (void)iid; *out = d; d->refs++; return DS_OK; }
static ULONG WINAPI d_Release(ds_obj *d) { if (--d->refs) return d->refs; rh_arena_free(d); return 0; }
static HRESULT WINAPI d_CreateSoundBuffer(ds_obj *d, DSBUFFERDESC *desc, ds_buf **out, void *outer)
{
    (void)d; (void)outer;
    uint32_t n = desc->dwBufferBytes;
    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) n = 8192;
    if (!n) return DSERR_INVALIDPARAM;
    ds_mem *m = rh_arena_alloc(sizeof *m);
    m->size = n; m->p = rh_media_alloc(n);
    ds_buf *b = buf_new(m, desc->lpwfxFormat, desc->dwFlags);
    m->refs = 1;
    *out = b;
    LOGDS("dsound: CreateSoundBuffer #%u %u bytes flags %08lx %luHz (f%u)\n", b->serial, n, (unsigned long)desc->dwFlags,
          (unsigned long)b->fmt.nSamplesPerSec, g_frame);
    return DS_OK;
}
static HRESULT WINAPI d_GetCaps(ds_obj *d, DSCAPS *c)
{
    (void)d;
    DWORD sz = c->dwSize ? c->dwSize : sizeof *c;
    memset(c, 0, sz < sizeof *c ? sz : sizeof *c);
    c->dwSize = sz;
    c->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
                 DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;
    c->dwMinSecondarySampleRate = 100; c->dwMaxSecondarySampleRate = 100000;
    c->dwMaxHwMixingAllBuffers = c->dwMaxHwMixingStaticBuffers = c->dwMaxHwMixingStreamingBuffers = 1;
    return DS_OK;
}
static HRESULT WINAPI d_DuplicateSoundBuffer(ds_obj *d, ds_buf *src, ds_buf **out)
{
    (void)d;
    ds_buf *b = buf_new(src->mem, &src->fmt, src->flags);
    b->vol = src->vol; b->pan = src->pan; b->freq = src->freq;
    *out = b;
    return DS_OK;
}
static HRESULT WINAPI d_SetCooperativeLevel(ds_obj *d, HWND h, DWORD l) { (void)h; d->coop = l; return DS_OK; }
static HRESULT WINAPI d_Compact(ds_obj *d) { (void)d; return DS_OK; }
static HRESULT WINAPI d_GetSpeakerConfig(ds_obj *d, DWORD *c) { *c = d->speaker ? d->speaker : DSSPEAKER_STEREO; return DS_OK; }
static HRESULT WINAPI d_SetSpeakerConfig(ds_obj *d, DWORD c) { d->speaker = c; return DS_OK; }
static HRESULT WINAPI d_Initialize(ds_obj *d, GUID *g) { (void)d; (void)g; return DSERR_ALREADYINITIALIZED; }

HRESULT WINAPI rh_DirectSoundCreate(GUID *g, void **out, void *outer)
{
    (void)g; (void)outer;
    if (!g_vt_ds) {
        void *vd[] = { d_QI, unk_AddRef, d_Release, d_CreateSoundBuffer, d_GetCaps, d_DuplicateSoundBuffer,
                       d_SetCooperativeLevel, d_Compact, d_GetSpeakerConfig, d_SetSpeakerConfig, d_Initialize };
        void *vb[] = { b_QI, unk_AddRef, b_Release, b_GetCaps, b_GetCurrentPosition, b_GetFormat, b_GetVolume, b_GetPan,
                       b_GetFrequency, b_GetStatus, b_Initialize, b_Lock, b_Play, b_SetCurrentPosition, b_SetFormat,
                       b_SetVolume, b_SetPan, b_SetFrequency, b_Stop, b_Unlock, b_Restore };
        g_vt_ds = rh_fixed_alloc(sizeof vd); memcpy(g_vt_ds, vd, sizeof vd);
        g_vt_buf = rh_fixed_alloc(sizeof vb); memcpy(g_vt_buf, vb, sizeof vb);
    }
    ds_obj *d = rh_arena_alloc(sizeof *d);
    d->vtbl = g_vt_ds; d->refs = 1;
    *out = d;
    rh_log("dsound: DirectSoundCreate -> silent virtual DirectSound %p (f%u)\n", (void *)d, g_frame);
    return DS_OK;
}

/* ---------------------------------------------------------------- virtual multimedia timer */
#define MAXT 16
static struct { UINT id, delay, periodic; LPTIMECALLBACK cb; DWORD_PTR user; uint32_t next; int live; } g_t[MAXT];
static UINT g_tid;
static uint32_t g_nfire;
static MMRESULT WINAPI my_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT fl)
{
    (void)res;
    if (fl & ~(UINT)(TIME_PERIODIC | TIME_CALLBACK_FUNCTION)) rh_log("timeSetEvent: flags %x unsupported\n", fl);
    for (int i = 0; i < MAXT; i++)
        if (!g_t[i].live) {
            g_t[i].live = 1; g_t[i].id = ++g_tid; g_t[i].delay = delay ? delay : 1; g_t[i].periodic = fl & TIME_PERIODIC;
            g_t[i].cb = cb; g_t[i].user = user; g_t[i].next = rh_vnow() + g_t[i].delay;
            rh_log("mmtimer: timeSetEvent #%u every %ums%s cb %p (f%u)\n", g_t[i].id, delay, g_t[i].periodic ? "" : " once", (void *)cb, g_frame);
            return g_t[i].id;
        }
    return 0;
}
static MMRESULT WINAPI my_timeKillEvent(UINT id)
{
    for (int i = 0; i < MAXT; i++) if (g_t[i].live && g_t[i].id == id) { g_t[i].live = 0; return TIMERR_NOERROR; }
    return MMSYSERR_INVALPARAM;
}
static MMRESULT WINAPI my_timeBeginPeriod(UINT p) { (void)p; return TIMERR_NOERROR; }
static MMRESULT WINAPI my_timeEndPeriod(UINT p) { (void)p; return TIMERR_NOERROR; }
void rh_mm_frame(void)
{
    uint32_t now = rh_vnow();
    for (int i = 0; i < MAXT; i++) {
        int n = 0;
        while (g_t[i].live && (int32_t)(now - g_t[i].next) >= 0) {
            UINT id = g_t[i].id;
            if (g_t[i].periodic) g_t[i].next += g_t[i].delay; else g_t[i].live = 0;
            g_nfire++;
            g_t[i].cb(id, 0, g_t[i].user, 0, 0);
            if (++n >= 8 && g_t[i].live) { g_t[i].next = now + g_t[i].delay; break; }   /* a long frame: no catch-up spiral */
        }
    }
}
void rh_mm_install(void)
{
    iat_override("timeSetEvent", my_timeSetEvent);
    iat_override("timeKillEvent", my_timeKillEvent);
    iat_override("timeBeginPeriod", my_timeBeginPeriod);
    iat_override("timeEndPeriod", my_timeEndPeriod);
}
void rh_ds_report(void)
{
    rh_log("dsound: buffers=%u plays=%u locks=%u mmtimer-fires=%u unimpl=%ld\n", g_serial, g_nplay, g_nlock, g_nfire, g_unimpl);
}
