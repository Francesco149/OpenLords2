/*
 * OpenLords2 — DSound bring-up (port of retail FUN_00425b10).
 *
 * Call shape mirrored from `docs/findings/ddraw-dsound-bringup.md`:
 *
 *     audio_init(hwnd)
 *        ├─ DirectSoundCreate(NULL, &lpDS, NULL)
 *        ├─ FUN_00425ded     (fills DAT_00522b90 WAVEFORMATEX template)
 *        ├─ IDirectSound::SetCooperativeLevel(hwnd, DSSCL_PRIORITY)
 *        ├─ IDirectSound::CreateSoundBuffer(primaryDesc, &primary)
 *        ├─ IDirectSoundBuffer::SetFormat(primary, &wfx)
 *        └─ zero out voice/stream tracking slots
 *
 * The retail template at `FUN_00425ded` lays out a 18-byte
 * WAVEFORMATEX as:
 *
 *     wFormatTag       = 1        (WAVE_FORMAT_PCM)
 *     nChannels        = 2        (stereo)
 *     nSamplesPerSec   = 11025    (0x2b11)
 *     nAvgBytesPerSec  = 22050    (0x5622 = 11025 * 2 * 8/8)
 *     nBlockAlign      = 2        (nChannels * wBitsPerSample/8)
 *     wBitsPerSample   = 8
 *     cbSize           = 0        (implied — memset zeros)
 *
 * The 2-step `CreateSoundBuffer(primary) → SetFormat` is the standard
 * DX5-era pattern for taking a usable primary mixing buffer instead
 * of accepting the system's default (which on a stock Win9x install
 * was 11kHz mono 8-bit — fine for retail's content, but the explicit
 * SetFormat decouples us from system defaults that might have
 * downgraded since).
 *
 * Differences from retail held to a minimum:
 *
 *   1. Retail's `FUN_00425b10` also calls `FUN_00425ded` BEFORE the
 *      SetCooperativeLevel — it's fine to call later as long as we
 *      hand the format buffer to `SetFormat`.  We call it inline here
 *      for readability.
 *
 *   2. Retail zeroes 20 voice slots (`DAT_00522b00`), 2 stream slots
 *      (`DAT_00522b80`), and 2 unknown slots (`DAT_00522b50`) at the
 *      end of the success arm.  We don't have those slots yet — they
 *      come with the SFX/music playback path.  Mirrored as TODOs.
 *
 *   3. Retail's error log uses an HRESULT→small-code mapping
 *      (`FUN_00427d72`) and logs the small code.  We log the raw
 *      HRESULT instead — easier to look up.  Log prefix strings
 *      ("ERR:DS failed open <code>", etc.) still match retail's
 *      so diff'd transcripts line up modulo the trailing number.
 */

#define WIN32_LEAN_AND_MEAN
#define DIRECTSOUND_VERSION 0x0500       /* match DX5 era — same as retail */
#define COBJMACROS

#include <windows.h>
#include <mmsystem.h>           /* WAVEFORMATEX (pulls in mmreg.h) */
#include <dsound.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"

/* Logger lives in main.c — declared here as a weak local prototype
 * so we don't drag dev_hooks.h into this TU.  Matches gfx.c. */
extern void log_line(const char *fmt, ...);

/* ---------------------------------------------------------------- *
 *  Global DSound state mirrors retail's DAT_* globals one-for-one   *
 *  so future ports of code that reads them is a global-replace.     *
 * ---------------------------------------------------------------- */

static LPDIRECTSOUND       g_ds;        /* DAT_004d4760 */
static LPDIRECTSOUNDBUFFER g_primary;   /* DAT_00522af0 */

int audio_init(HWND hwnd)
{
    if (!hwnd) return 0;
    if (g_ds) {
        /* Already initialised — retail's bring-up returns 1 on a
         * second call (the `if (DAT_004d4760 == NULL)` early-out
         * branch).  We mirror the no-op success behaviour. */
        return 1;
    }

    HRESULT hr = DirectSoundCreate(NULL, &g_ds, NULL);
    if (FAILED(hr) || !g_ds) {
        log_line("ERR:DS failed open 0x%08lx", (unsigned long)hr);
        g_ds = NULL;
        return 0;
    }

    /* Retail's FUN_00425ded — fill the WAVEFORMATEX template that
     * SetFormat will read.  Inlined here. */
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = 11025;
    wfx.nAvgBytesPerSec = 22050;        /* 11025 * 2 * 8/8 */
    wfx.nBlockAlign     = 2;            /* nChannels * wBitsPerSample/8 */
    wfx.wBitsPerSample  = 8;
    wfx.cbSize          = 0;

    hr = IDirectSound_SetCooperativeLevel(g_ds, hwnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        log_line("ERR:DS failed set co-op 0x%08lx", (unsigned long)hr);
        audio_shutdown();
        return 0;
    }

    /* Primary mixing buffer.  DSBUFFERDESC.dwSize is 0x14 in DX3+
     * (this matches the `local_1c = 0x14` literal in retail).  Only
     * dwFlags = DSBCAPS_PRIMARYBUFFER is set; lpwfxFormat must be
     * NULL for primary buffers per DX docs (the SetFormat call
     * below installs the format). */
    DSBUFFERDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize        = sizeof(desc);  /* 0x14 */
    desc.dwFlags       = DSBCAPS_PRIMARYBUFFER;
    desc.dwBufferBytes = 0;
    desc.lpwfxFormat   = NULL;

    hr = IDirectSound_CreateSoundBuffer(g_ds, &desc, &g_primary, NULL);
    if (FAILED(hr) || !g_primary) {
        log_line("ERR:DS failed setup primary buffer 0x%08lx",
                 (unsigned long)hr);
        audio_shutdown();
        return 0;
    }

    hr = IDirectSoundBuffer_SetFormat(g_primary, &wfx);
    if (FAILED(hr)) {
        log_line("ERR:DS failed to change primary buffer format 0x%08lx",
                 (unsigned long)hr);
        audio_shutdown();
        return 0;
    }

    /* TODO(sfx): zero `g_voices[20]` / `g_streams[2]` / `g_???[2]`
     * tracking arrays once the SFX/music subsystem lands.  Mirrors
     * the three trailing loops in FUN_00425b10's success arm. */

    log_line("OK :DS Installed.");
    return 1;
}

void audio_shutdown(void)
{
    if (!g_ds) return;

    /* TODO(sfx): call the equivalent of FUN_00426d11 (stream stop,
     * slots 0..1) and 0x4262eb (voice stop, slots 1..20) here
     * once those subsystems exist.  0x427d19 (final cleanup)
     * also belongs here. */

    if (g_primary) {
        IDirectSoundBuffer_Release(g_primary);
        g_primary = NULL;
    }
    IDirectSound_Release(g_ds);
    g_ds = NULL;
    log_line("OK :DS Released.");
}
