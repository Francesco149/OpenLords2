/* rhost — in-process retail host for Lords of the Realm II (Lords2.exe, GOG 2.0.0.3).
 * Runs the user's own Lords2.exe inside this process, deterministically: virtual clock,
 * software DirectDraw / DirectSound / multimedia timer, a virtual window + message queue
 * fed by an input trace, fixed-address heap / handles / stack, hermetic file overlay.
 * Modeled on openrecet's tools/rhost (2026-10-13 methodology reset) and OpenSummoners'
 * DirectDraw-era port of it. See README.md. */
#pragma once
#include "retail_map.h"
#include <stdarg.h>

#define G32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define G16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define G8(va)  (*(volatile uint8_t *)(uintptr_t)(va))

/* ---- Lords2.exe (GOG 2.0.0.3, sha256 6fec043e…) ---- */
#define L2_IMAGE_SIZE   0x1e9000u
#define L2_ENTRY        0x4c3370u   /* CRT entry */
#define L2_FRAME_FN     0x4b99c0u   /* FUN_004b99c0: sim + render driver — one call = one frame */
#define L2_WNDPROC      0x4b29beu   /* FUN_004b29be: WinLords2Class procedure */
#define L2_HWND         0x4e5a00u   /* DAT_004e5a00: main HWND (rhost hands out a fixed fake) */
#define L2_ACTIVE       0x4e65a8u   /* DAT_004e65a8: window-active flag (WM_ACTIVATEAPP) */
#define L2_SIM_TICK     0x57c998u   /* DAT_0057c998: master sim tick */
#define L2_ENGINE_STATE 0x52af94u   /* DAT_0052af94: 3 ingame, 10 save, 0xb load */
#define L2_SCREEN       0x4eac50u   /* DAT_004eac50: current screen id (byte) */
#define L2_SUBSTATE     0x57c8fcu   /* DAT_0057c8fc: sub-state */
#define L2_RNG_A        0x58fe28u   /* DAT_0058fe28: LFSR A (FUN_00404a46; seeded 0x54657687 in FUN_004b2258) */
#define L2_RNG_B        0x58fe08u   /* DAT_0058fe08: LFSR B (seeded 0x72641663) */

/* retail image layout (from the PE headers at map time) */
extern uint32_t RT_TEXT_LO, RT_TEXT_SZ, RT_DATA_LO, RT_DATA_HI, RT_IMG_HI;
#define RT_IMG_LO 0x400000u

typedef struct {
    const char *exe, *gamedir, *rundir;
    uint32_t frames;            /* stop after this many frames (0 = trace end + tail, else 3000) */
    const char *trace;          /* input trace (.jsonl) */
    const char *record;         /* human play → flat trace */
    uint32_t dump_at[128]; int n_dump;
    uint32_t shot_at[64]; int n_shot; uint32_t shot_every;
    int fb_hash;                /* chain a hash of every presented frame (index plane + palette) */
    uint32_t hash_every, log_every, tail;
    int realtime;               /* pace frames at 1000/step_ms Hz wall clock */
    int show;                   /* visible window (StretchDIBits of the presented frame) */
    int scale;                  /* window scale (show/record) */
    int movies;                 /* real smackw32 (default: SmackOpen fails → retail skips movies) */
    int no_arena, no_overlay;
    int log_files, log_heap, log_input, log_dd, log_ds, log_win;
    const char *coverage;
    const char *hooks;
    uint32_t watch[16]; int n_watch;
    uint32_t step_ms;           /* virtual ms per frame */
    uint32_t creep_after;       /* clock reads without a frame before the clock creeps */
    uint32_t vbase;             /* virtual clock origin (ms) — the run's "seed" */
    uint32_t stall_wall_ms;
    uint32_t trap_max;
    uint32_t htrace_f0, htrace_f1;  /* --heap-trace A-B: every HeapAlloc / HeapFree in this frame window */
    const char *bot;           /* host:port of a decision server (bot mode) */
    const char *stop_on;        /* stop condition (e.g. "screen=0x13") */
} rh_opts_t;

extern rh_opts_t O;
extern volatile uint32_t g_frame;      /* completed frame-driver calls */
extern FILE *g_log;
void rh_log(const char *fmt, ...);
void rh_fatal(const char *fmt, ...);
void rh_finish(const char *why);
uint32_t rh_vnow(void);
void rh_clock_save(uint32_t s[3]);
void rh_clock_restore(const uint32_t s[3]);
void iat_override(const char *name, void *fn);
int  iat_try(const char *name, void *fn);
void *iat_lookup(const char *name);
int  redirect_calls(uint32_t va, void *hook);
void *rh_fixed_alloc(size_t n);
int  rh_in_shadow(void);

/* rh_heap.c */
int  rh_heap_init(void);
void rh_heap_install(void);
extern int rh_heap_zero_alloc;
int  rh_heap_ranges(uint32_t (*out)[2], int max);
void rh_heap_stats(char *buf, size_t n);
int  rh_is_det_addr(uint32_t v);
void *rh_heap_meta_save(void);
void rh_heap_meta_restore(void *m);
void rh_heap_meta_free(void *m);
void *rh_arena_alloc(uint32_t n);
void rh_arena_free(void *p);

/* rh_hostalloc.c */
int rh_host_in(unsigned v);

/* rh_files.c */
void rh_files_install(void);
const char *rh_files_resolve(const char *path, int want_write, char *out, size_t n);

/* rh_handles.c */
void rh_handles_install(void);
long rh_handles_live(void);
uint32_t rh_handles_issued(void);
HANDLE rh_handle_fake(HANDLE real);
HANDLE rh_handle_real(HANDLE h);
void rh_handle_forget(HANDLE h);

/* rh_media.c — the fixed media pool (surfaces, sound buffers) */
int  rh_media_reserve(void);
void *rh_media_alloc(uint32_t n);
void rh_media_free(void *p);
int  rh_media_in_pool(uint32_t v);
uint32_t rh_media_used(void);

/* rh_ddraw.c — software DirectDraw (8 bpp) */
HRESULT WINAPI rh_DirectDrawCreate(GUID *g, void **out, void *outer);
int  rh_dd_present(const uint8_t **pix, int *pitch, int *w, int *h, const PALETTEENTRY **pal);
void rh_dd_report(void);

/* rh_dsound.c — fake DirectSound + virtual multimedia timer */
HRESULT WINAPI rh_DirectSoundCreate(GUID *g, void **out, void *outer);
void rh_mm_install(void);
void rh_mm_frame(void);        /* fire due timeSetEvent callbacks (main thread, frame boundary) */
void rh_ds_report(void);

/* rh_win.c — virtual window, message queue, input trace, recorder, visible presenter */
#define RH_FAKE_HWND 0x00020c2eu
void rh_win_install(void);
void rh_win_frame(void);       /* frame boundary: inject this frame's trace events / record / present */
void rh_win_present(void);
HWND rh_win_real(HWND h);
uint32_t rh_trace_last_frame(void);
const char *rh_trace_status(void);
void rh_trace_finish(void);
void rh_win_post(UINT m, WPARAM w, LPARAM l);   /* append to the virtual queue (bot, tests) */

/* rh_state.c */
typedef struct { uint64_t full, semantic; uint32_t masked, ranges, bytes; } rh_hash_t;
void rh_state_hash(rh_hash_t *h);
int  rh_state_dump(const char *path);
void rh_state_volatile_add(uint32_t va);
int  rh_shadow_ex(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn);

/* rh_shot.c */
void rh_shot(const char *tag);
int  rh_fb_hash(uint64_t *out);

/* rh_hooks.c */
void rh_hooks_install(void);
void rh_hooks_report(void);

/* rh_bot.c — decision-server link (bot mode) */
void rh_bot_install(void);
void rh_bot_frame(void);
