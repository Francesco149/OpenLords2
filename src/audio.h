/*
 * OpenLords2 — DSound bring-up (header).
 *
 * Port of retail's `FUN_00425b10 @ 0x425b10` chain — DirectSoundCreate
 * + SetCooperativeLevel(PRIORITY) + CreateSoundBuffer(primary) +
 * SetFormat(11025 Hz stereo 8-bit PCM).  Called from WinMain *after*
 * FUN_004b2258 (InitInstance) returns, so wire it in `main.c` after
 * `gfx_init`.
 *
 * Today: open IDirectSound, take PRIORITY coop, create primary mixing
 * buffer, set its format to retail's 11k/stereo/8-bit template.
 *
 * NOT yet: voice slots (`DAT_00522b00[20]`), stream slots
 * (`DAT_00522b80[2]` / `DAT_00522b50[2]`), per-sound buffer creation,
 * .wav file loader.  Those land with the SFX/music playback path
 * once we know which game-state code triggers them.
 *
 * Gated behind --audio-init in main.c by default — `SetCooperativeLevel
 * (PRIORITY)` modifies global process audio state and the headless
 * smoke harness has no way to validate output audibly.  The existing
 * Frida harness already hooks DSound's `SetCooperativeLevel` to demote
 * PRIORITY→NORMAL; once gated retail-clone runs match that telemetry,
 * we can flip the default to on.
 *
 * Logged status lines mirror retail's exact strings (the
 * "OK :DS Installed." / "OK :DS Released." / "ERR:DS ..." pairs at
 * 0x4d4768..0x4d4800 in Lords2.exe) so transcripts diff cleanly
 * against a Frida-captured retail run.
 */

#ifndef OPENLORDS2_AUDIO_H
#define OPENLORDS2_AUDIO_H

#include <windows.h>

/* Init DSound against the given window.  Creates the IDirectSound
 * interface, takes PRIORITY cooperative level, creates the primary
 * IDirectSoundBuffer, and SetFormats it to 11025 Hz stereo 8-bit PCM
 * (retail's `FUN_00425ded` template).  Returns 1 on success, 0 on
 * failure.  Partial state is torn down on failure via
 * `audio_shutdown` so the caller can ignore the failure cleanly. */
int  audio_init(HWND hwnd);

/* Release the primary buffer and the IDirectSound interface (reverse
 * order of creation).  Idempotent — safe to call when audio_init was
 * never called or failed. */
void audio_shutdown(void);

/* Stop and release the one-shot WAV-cue buffer (port of retail
 * FUN_00427d19 @ 0x427d19).  Guarded on both the IDirectSound
 * interface and the cue buffer being live, so it is a no-op until the
 * cue-playback path (FUN_00427990) lands and starts creating the
 * buffer.  Called by the gameplay cue path and by game-state
 * transitions that need to silence an in-flight cue. */
void audio_cue_release(void);

#endif /* OPENLORDS2_AUDIO_H */
