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
#define L2_CANVAS       0x4ea1a8u   /* DAT_004ea1a8 (Buf): the back buffer the gfx primitives draw into */
#define L2_PITCH        0x4eb274u   /* DAT_004eb274 (xRight): its row pitch */
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
    const char *record;         /* human play → flat trace (= --out-trace + visible realtime window + real input) */
    const char *out_trace;      /* write every injected input event (the replayable record of the run) */
    uint32_t dump_at[128]; int n_dump;
    uint32_t shot_at[64]; int n_shot; uint32_t shot_every;
    int fb_hash;                /* chain a hash of every presented frame (index plane + palette) */
    uint32_t hash_every, log_every, tail;
    int realtime;               /* pace frames at 1000/step_ms Hz wall clock */
    int show;                   /* visible window (StretchDIBits of the presented frame) */
    int scale;                  /* window scale (show/record) */
    int movies;                 /* real smackw32 (default: SmackOpen fails → retail skips movies) */
    int audible;                /* mix DirectSound to waveOut (--audible; implied by --record) */
    int no_arena, no_overlay;
    int log_files, log_heap, log_input, log_dd, log_ds, log_win;
    const char *coverage;
    const char *hooks;
    int shadow_full;            /* --shadow-full: shadow by copying the whole state (default: page-tracked) */
    int hook_entry;             /* --hook-entry: patch every hooked function's entry, not its call sites (tests) */
    uint32_t shadow_poison;     /* --shadow-poison VA: flip a bit there after every shadowed port call (tests the diff) */
    int shadow_all;             /* --shadow-all: shadow every call of an RH_SH_SAMPLE row too */
    int shadow_media;           /* --shadow-media: every shadow also compares the surfaces' pixels (default: RH_SH_MEDIA rows) */
    int poison_px, poison_x, poison_y;   /* --shadow-poison-px X,Y: flip bit 0 of canvas pixel (X,Y) after every media-compared port call */
    uint32_t watch[16]; int n_watch;
    uint32_t step_ms;           /* virtual ms per frame */
    uint32_t creep_after;       /* clock reads without a frame before the clock creeps */
    uint32_t vbase;             /* virtual clock origin (ms) — the run's "seed" */
    uint32_t stall_wall_ms;
    uint32_t trap_max;
    uint32_t htrace_f0, htrace_f1;
    uint32_t cap_from, cap_count;   /* --cap FROM:COUNT */  /* --heap-trace A-B: every HeapAlloc / HeapFree in this frame window */
    const char *bot;           /* host:port of a decision server (bot mode) */
    const char *stop_on;        /* stop condition (e.g. "screen=0x13") */
    int live, live_port;        /* --live PORT: serve commands on 127.0.0.1:PORT (0 = pick one → <run>\\live.port); rh_live.c */
    uint32_t live_idle;         /* --live-idle S: a live session exits after S seconds without a command (1800) */
    uint32_t live_until;        /* --live-until F: with --trace, stop the prefix replay at frame F (default: its last event) */
    const char *snap_test;      /* --snap-test A:B[:K]: savestate self-check (rh_live.c) */
    int standalone;             /* --standalone: NO retail code — only the data sections are mapped (the code range is
                                   INT3 + a jmp thunk into port code at every hook row's entry); the entry is the port's
                                   rl_crt_start; <rundir>\standalone_audit.txt proves it (docs/findings/standalone-path.md) */
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
uint32_t rh_patch_entry(uint32_t va, void *hook);   /* jmp hook at va; returns the trampoline that runs the original */
void *rh_fixed_alloc(size_t n);
/* --standalone: the jmp thunks rh_hooks_install wrote into the (INT3-filled) retail code range */
typedef struct { uint32_t va; void *target; const char *name; } rh_thunk_t;
int  rh_hooks_thunks(const rh_thunk_t **out);
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
void *rh_handles_meta_save(void);          /* fake-handle sequence (free with free()) */
void rh_handles_meta_restore(const void *m);

/* rh_media.c — the fixed media pool (surfaces, sound buffers) */
int  rh_media_reserve(void);
void *rh_media_alloc(uint32_t n);
void *rh_media_alloc_surface(uint32_t n, uint32_t pitch);   /* a DirectDraw surface's pixels (tagged with its pitch) */
int  rh_media_surface_ranges(uint32_t (*r)[2], int max);   /* live surface blocks, address order (the media shadow) */
uint32_t rh_media_surface_at(uint32_t v, uint32_t *pitch);  /* base of the surface block holding v (0: none) */
void rh_media_free(void *p);
int  rh_media_in_pool(uint32_t v);
uint32_t rh_media_used(void);
void *rh_media_meta_save(void);            /* allocator state (free with free()) */
void rh_media_meta_restore(const void *m);

/* rh_ddraw.c — software DirectDraw (8 bpp) */
HRESULT WINAPI rh_DirectDrawCreate(GUID *g, void **out, void *outer);
int  rh_dd_present(const uint8_t **pix, int *pitch, int *w, int *h, const PALETTEENTRY **pal);
void rh_dd_report(void);

/* rh_dsound.c — fake DirectSound + virtual multimedia timer */
HRESULT WINAPI rh_DirectSoundCreate(GUID *g, void **out, void *outer);
void rh_mm_install(void);
void rh_mm_frame(void);        /* fire due timeSetEvent callbacks (main thread, frame boundary) */
void rh_ds_report(void);
void rh_ds_mix(void);          /* --audible: this frame's audio to waveOut (reads only) */
void *rh_ds_meta_save(void);               /* serials, live registry, timers (free with free()) */
void rh_ds_meta_restore(const void *m);
uint32_t rh_smack_heap_top(void);          /* smackw32's heap bump pointer (a movie row's shadow rolls it: rh_hook_smk.h) */
void rh_smack_heap_top_set(uint32_t top);
void *rh_dd_meta_save(void);               /* DirectDraw surface serial / count / primary (free with free()) */
void rh_dd_meta_restore(const void *m);
void *rh_win_meta_save(void);              /* virtual queue + window state (free with free()) */
int rh_is_addr_taken(uint32_t va);         /* IDA: the function's address is taken (rh_hooks.c: entry-patch it too) */
void *rh_files_meta_save(void);            /* the run dir's files a shadowed call touched (rh_files_meta_free) */
void rh_files_meta_restore(const void *m);
void rh_files_meta_free(void *m);
void rh_win_meta_restore(const void *m);

/* rh_win.c — virtual window, message queue, input trace, recorder, visible presenter */
#define RH_FAKE_HWND 0x00020c2eu
void rh_win_install(void);
void rh_win_frame(void);       /* frame boundary: inject this frame's trace events / record / present */
void rh_win_present(void);
HWND rh_win_real(HWND h);
uint32_t rh_trace_last_frame(void);
const char *rh_trace_status(void);
void rh_trace_finish(void);
void rh_win_post(UINT m, WPARAM w, LPARAM l);   /* append to the virtual queue (not recorded) */
void rh_win_inject(UINT m, uint32_t w, uint32_t l);
void rh_win_snap_register(void);   /* input event at this frame boundary (recorded) */

/* rh_state.c */
typedef struct { uint64_t full, semantic; uint32_t masked, ranges, bytes; } rh_hash_t;
void rh_state_hash(rh_hash_t *h);
int  rh_state_dump(const char *path);
void rh_state_volatile_add(uint32_t va);
int  rh_shadow_ex(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn);
/* … with flags: RH_SH_MEDIA also compares every DirectDraw surface's pixels (a render row; O.shadow_media: every row) */
#define RH_SH_MEDIA 1
#define RH_SH_SAMPLE 2   /* (hook rows) a hot leaf: shadow a sample of its calls (rh_hooks.c rh_hook_sample; --shadow-all: every call) */
#define RH_SH_HOST 4     /* (hook rows) a host-level function (it creates host objects: the window class, the DirectDraw
                            * object …): the shadow cannot run it twice. Shadow passes leave every call to retail (counted
                            * in `sampled`); the hybrid pass replaces it — corpus --check (state + every frame) proves it */
#define RH_SH_FULL 8     /* (hook rows) shadow this row with the full state copy, not the page-tracked one: its retail side
                            * reads stack residue below its own frame, where the paged shadow's write faults push exception
                            * frames (retail's own run perturbed: rl_46521f). Costly: pair with RH_SH_SAMPLE on a hot row */
int  rh_shadow_ex_f(const char *name, void (*retail)(void *), void (*port)(void *), void *ctx, char *why, size_t whyn, int flags);
void rh_shadow_prewrite(const void *p, size_t n);   /* before a KERNEL write into retail memory (ReadFile, ...) */
void rh_shadow_report(void);                         /* shadow-prof: per-phase cost per call */
void rh_log_occupants(uint32_t lo, uint32_t hi);     /* what holds a fixed range (reservation failures) */

/* rh_shot.c */
void rh_shot(const char *tag);
void rh_cap_frame(void);       /* --cap FROM:COUNT → capture.olv3 (Trace Studio v3) */
void rh_cap_finish(void);
int  rh_fb_hash(uint64_t *out);

/* rh_smack.c — deterministic Smacker (--movies) */
void rh_smack_install(void);
void rh_smack_frame(void);      /* waveOut completions on the virtual clock */
int  rh_smack_in(uint32_t v);

/* rh_hooks.c */
void rh_hooks_install(void);
void rh_hooks_report(void);

/* rhost.c — the game coroutine (--live / --snap-test): retail runs on its fixed stack, the host on its own */
void rh_step(void);             /* host: run one whole frame, boundary to boundary */
uint32_t rh_game_esp(void);     /* retail's stack pointer at the boundary */

/* rh_snap.c — in-process savestates */
void rh_snap_register(const char *name, void *p, size_t n);
int  rh_snap_take(void);
int  rh_snap_restore(int id);
void rh_snap_free(int id);
void rh_snap_free_all(void);
int  rh_snap_frame(int id);
int  rh_snap_live(void);
long rh_snap_diff(int id, uint32_t lo, uint32_t hi, void (*cb)(uint32_t va, uint32_t was, uint32_t now, void *u), void *u);
void rh_snap_stats(char *buf, size_t n);
void rh_heap_meta_restore_snap(void *m);   /* rh_heap.c: + the regions' commit marks */
void *rh_handles_snap(void);               /* rh_handles.c */
void rh_handles_unsnap(void *s);
void *rh_files_snap(void);                 /* rh_files.c */
void rh_files_unsnap(void *s);
void rh_files_snap_free(void *s);

/* rh_live.c */
void rh_live_install(void);
void rh_live_register(void);
void rh_live_main(void);
void rh_live_frame_begin(void);
void rh_live_note_event(uint32_t frame, uint32_t m, uint32_t w, uint32_t l);
void rh_snap_selftest(void);
const char *rh_snap_test_status(void);

/* rh_bot.c — decision-server link (bot mode) */
void rh_bot_install(void);
void rh_bot_frame(void);
uint32_t rh_bot_poked(void);
