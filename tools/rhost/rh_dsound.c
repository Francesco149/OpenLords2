/* rh_dsound.c — a silent, deterministic DirectSound + a virtual multimedia timer for retail Lords2.
 *
 * Retail streams music/sound into DirectSound buffers from a timeSetEvent callback
 * (FUN_00426dd5, armed by FUN_00426b92) that normally runs on winmm's timer thread and reads
 * the buffers' play cursors. Here:
 *   - buffers are memory in the fixed media pool; the play cursor advances with the VIRTUAL
 *     clock (rate = the buffer's format / SetFrequency), looping or stopping at the end;
 *   - timeSetEvent callbacks are fired on the MAIN thread at frame boundaries (rh_mm_frame),
 *     in id order, once per elapsed period — a deterministic serialization of the real thread.
 * Silent by default; --audible (implied by --record) mixes the playing buffers to waveOut (rh_ds_mix). */
#include "rhost.h"
#include <mmsystem.h>
#include <dsound.h>
#include <math.h>

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
/* host-side registry of live buffers (the --audible mixer walks it; never part of retail state) */
#define MAXB 1024
static ds_buf *g_live[MAXB]; static int g_nlive;
static void live_add(ds_buf *b) { if (g_nlive < MAXB) g_live[g_nlive++] = b; }
static void live_del(ds_buf *b) { for (int i = 0; i < g_nlive; i++) if (g_live[i] == b) { g_live[i] = g_live[--g_nlive]; return; } }
static uint32_t g_serial, g_nplay, g_nlock;
static ULONG WINAPI unk_AddRef(void *self) { return ++((uint32_t *)self)[1]; }

static uint32_t bytes_per_sec(const ds_buf *b)
{
    uint32_t f = b->freq ? b->freq : b->fmt.nSamplesPerSec;
    return f * (b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1);
}
/* The play cursor is a pure function of the virtual clock: (pos, t0) is an ANCHOR set only by retail's
 * own state-changing calls (Play / Stop / SetCurrentPosition / SetFrequency / SetFormat); every query
 * computes the cursor from it. So extra observers (the --audible mixer) can never change what retail sees. */
static void cur(const ds_buf *b, uint32_t now, uint32_t *pos, uint32_t *playing)
{
    *pos = b->pos; *playing = b->playing;
    if (!b->playing || !b->mem->size) return;
    uint64_t adv = (uint64_t)(now - b->t0) * bytes_per_sec(b) / 1000u;
    uint32_t ba = b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1;
    adv -= adv % ba;
    uint64_t p = b->pos + adv;
    if (b->looping) *pos = (uint32_t)(p % b->mem->size);
    else if (p >= b->mem->size) { *pos = 0; *playing = 0; }
    else *pos = (uint32_t)p;
}
/* re-anchor at the virtual now (before a state-changing call) */
static void advance(ds_buf *b)
{
    uint32_t now = rh_vnow(), pos, playing;
    cur(b, now, &pos, &playing);
    b->pos = pos; b->playing = playing; b->t0 = now;
}

/* ---------------------------------------------------------------- buffers */
static HRESULT WINAPI b_QI(ds_buf *b, REFIID iid, void **out) { (void)iid; *out = b; b->refs++; return DS_OK; }
static ULONG WINAPI b_Release(ds_buf *b)
{
    if (--b->refs) return b->refs;
    live_del(b);
    if (!--b->mem->refs) { rh_media_free(b->mem->p); rh_arena_free(b->mem); }
    rh_arena_free(b);
    return 0;
}
static HRESULT WINAPI b_GetCaps(ds_buf *b, DSBCAPS *c) { c->dwFlags = b->flags; c->dwBufferBytes = b->mem->size; c->dwUnlockTransferRate = 0; c->dwPlayCpuOverhead = 0; return DS_OK; }
static long g_ngcp;
static HRESULT WINAPI b_GetCurrentPosition(ds_buf *b, DWORD *play, DWORD *write)
{
    uint32_t pos, playing;
    cur(b, rh_vnow(), &pos, &playing);
    if (O.log_ds && g_ngcp++ < 400) rh_log("dsound: buf #%u GetCurrentPosition -> %u playing=%u t=%u (f%u)\n", b->serial, pos, playing, rh_vnow(), g_frame);
    uint32_t lead = bytes_per_sec(b) * 15u / 1000u, ba = b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1;
    lead -= lead % ba;
    if (play) *play = pos;
    if (write) *write = b->mem->size ? (playing ? (pos + lead) % b->mem->size : pos) : 0;
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
static HRESULT WINAPI b_GetStatus(ds_buf *b, DWORD *s)
{ uint32_t pos, playing; cur(b, rh_vnow(), &pos, &playing); *s = (playing ? DSBSTATUS_PLAYING : 0) | (playing && b->looping ? DSBSTATUS_LOOPING : 0); return DS_OK; }
static HRESULT WINAPI b_Initialize(ds_buf *b, void *ds, DSBUFFERDESC *d) { (void)b; (void)ds; (void)d; return DSERR_ALREADYINITIALIZED; }
static HRESULT WINAPI b_Lock(ds_buf *b, DWORD off, DWORD n, void **p1, DWORD *n1, void **p2, DWORD *n2, DWORD fl)
{
    uint32_t size = b->mem->size;
    if (fl & DSBLOCK_FROMWRITECURSOR) { DWORD w; b_GetCurrentPosition(b, NULL, &w); off = w; }   /* pure: no re-anchor */
    if (fl & DSBLOCK_ENTIREBUFFER) n = size;
    if (!size || off >= size || n > size) return DSERR_INVALIDPARAM;
    g_nlock++;
    LOGDS("dsound: buf #%u Lock off %lu n %lu fl %lx (f%u)\n", b->serial, (unsigned long)off, (unsigned long)n, (unsigned long)fl, g_frame);
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
    live_add(b);
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
/* ---------------------------------------------------------------- --audible: mix to waveOut (record mode)
 * Reads each playing buffer between its cursor at the previous mix and now (cur(): pure), resamples to
 * 22050 Hz stereo, applies volume / pan, queues the chunk. Retail state is never touched. */
#define OUT_HZ 22050
#define NHDR 32
static HWAVEOUT g_wo; static WAVEHDR g_hdr[NHDR]; static int16_t *g_obuf[NHDR]; static int g_ohead;
static uint32_t g_mix_last; static uint32_t g_mix_frac;
static float db_gain(int32_t v) { return v <= -10000 ? 0.0f : powf(10.0f, (float)v / 2000.0f); }
static int32_t sample(const ds_buf *b, uint32_t frame_idx, int ch)
{
    uint32_t ba = b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1, nch = b->fmt.nChannels ? b->fmt.nChannels : 1;
    const uint8_t *p = b->mem->p + (frame_idx * ba) % b->mem->size;
    int c = nch > 1 ? ch : 0;
    if (b->fmt.wBitsPerSample == 16) return ((const int16_t *)p)[c];
    return ((int32_t)p[c] - 128) << 8;
}
void rh_ds_mix(void)
{
    if (!O.audible) return;
    uint32_t now = rh_vnow();
    if (!g_wo) {
        WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, OUT_HZ, OUT_HZ * 4, 4, 16, 0 };
        if (waveOutOpen(&g_wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { rh_log("audible: no waveOut device\n"); O.audible = 0; return; }
        for (int i = 0; i < NHDR; i++) { g_obuf[i] = calloc(8192, 4); }
        g_mix_last = now;
        rh_log("audible: mixing to waveOut %u Hz\n", OUT_HZ);
        return;
    }
    uint32_t dt = now - g_mix_last;
    if (!dt) return;
    if (dt > 400) { g_mix_last = now; return; }                  /* a stall: drop, don't burst */
    uint32_t total = dt * OUT_HZ + g_mix_frac, n = total / 1000;
    g_mix_frac = total % 1000;
    if (!n || n > 8192) { g_mix_last = now; return; }
    static int32_t acc[8192 * 2];
    memset(acc, 0, n * 8);
    for (int k = 0; k < g_nlive; k++) {
        const ds_buf *b = g_live[k];
        if (!b->playing || (b->flags & DSBCAPS_PRIMARYBUFFER) || !b->mem->size) continue;
        uint32_t ba = b->fmt.nBlockAlign ? b->fmt.nBlockAlign : 1, nframes = b->mem->size / ba;
        uint32_t freq = b->freq ? b->freq : b->fmt.nSamplesPerSec;
        if (!freq || !nframes) continue;
        uint32_t start_t = (int32_t)(b->t0 - g_mix_last) > 0 ? b->t0 : g_mix_last;   /* Play inside this interval */
        uint32_t p0, playing;
        cur(b, start_t, &p0, &playing);
        if (!playing) continue;
        uint32_t first_out = (uint32_t)((uint64_t)(start_t - g_mix_last) * OUT_HZ / 1000u);
        uint32_t f0 = p0 / ba;
        float g = db_gain(b->vol), gl = g * (b->pan > 0 ? db_gain(-b->pan) : 1.0f), gr = g * (b->pan < 0 ? db_gain(b->pan) : 1.0f);
        for (uint32_t i = first_out; i < n; i++) {
            uint64_t src = (uint64_t)(i - first_out) * freq / OUT_HZ;     /* frames since start_t */
            if (!b->looping && f0 + src >= nframes) break;
            uint32_t fi = (uint32_t)((f0 + src) % nframes);
            acc[2 * i] += (int32_t)(sample(b, fi, 0) * gl);
            acc[2 * i + 1] += (int32_t)(sample(b, fi, 1) * gr);
        }
    }
    g_mix_last = now;
    WAVEHDR *h = &g_hdr[g_ohead];
    if (h->dwFlags & WHDR_PREPARED) {
        if (!(h->dwFlags & WHDR_DONE)) return;                       /* the device is behind: drop this chunk */
        waveOutUnprepareHeader(g_wo, h, sizeof *h);
    }
    int16_t *o = g_obuf[g_ohead];
    for (uint32_t i = 0; i < 2 * n; i++) { int32_t v = acc[i]; o[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
    memset(h, 0, sizeof *h);
    h->lpData = (LPSTR)o; h->dwBufferLength = n * 4;
    waveOutPrepareHeader(g_wo, h, sizeof *h);
    waveOutWrite(g_wo, h, sizeof *h);
    g_ohead = (g_ohead + 1) % NHDR;
}

void rh_ds_report(void)
{
    rh_log("dsound: buffers=%u plays=%u locks=%u mmtimer-fires=%u unimpl=%ld\n", g_serial, g_nplay, g_nlock, g_nfire, g_unimpl);
}

/* host-side meta (buffer serials, the live-buffer registry, the virtual timers): see rh_media_meta_save */
typedef struct { ds_buf *live[MAXB]; int nlive; uint32_t serial, nplay, nlock, tid, nfire; long ngcp, unimpl;
                 __typeof__(g_t) t; } ds_meta_t;
void *rh_ds_meta_save(void)
{
    ds_meta_t *m = malloc(sizeof *m);
    memcpy(m->live, g_live, g_nlive * sizeof *g_live); m->nlive = g_nlive;
    m->serial = g_serial; m->nplay = g_nplay; m->nlock = g_nlock; m->tid = g_tid; m->nfire = g_nfire;
    m->ngcp = g_ngcp; m->unimpl = g_unimpl; memcpy(m->t, g_t, sizeof g_t);
    return m;
}
void rh_ds_meta_restore(const void *p)
{
    const ds_meta_t *m = p;
    memcpy(g_live, m->live, m->nlive * sizeof *g_live); g_nlive = m->nlive;
    g_serial = m->serial; g_nplay = m->nplay; g_nlock = m->nlock; g_tid = m->tid; g_nfire = m->nfire;
    g_ngcp = m->ngcp; g_unimpl = m->unimpl; memcpy(g_t, m->t, sizeof g_t);
}
