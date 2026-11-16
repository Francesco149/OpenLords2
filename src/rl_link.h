/* rl_link.h — the link layer between the retail-layout ports (rl_ / bf_: retail's globals, by symbol) and the OLD
 * ports (engine_* modules that keep some retail globals in file-static MIRRORS, take the event queue as a struct, or
 * draw through a bound environment).  One implementation for rhost (rh_port.h's rh_port_* forward here) and for a
 * build without rhost.
 *
 * MIRRORS.  rl_link_sync_in() loads every mirror from its retail global; rl_link_sync_out() stores back the mirrors
 * the port CHANGED since sync_in (store-if-changed: a port module that writes the same retail global through an
 * alias — g_bf_<va> — is not overwritten by a stale mirror; under rhost's page-tracked shadow an unchanged store
 * would also cost a page fault).
 *
 * LIVE.  rl_link_live() = 1 while old-port code runs with the mirrors authoritative.  An old-port entry from
 * retail-layout code (rl_link_port_enter) stores a live caller's mirrors first, then loads; its leave stores and
 * restores the caller's liveness.  Old-port code calling OUT to retail-layout or retail code brackets the call with
 * rl_link_retail_enter / _leave (store the mirrors and an open event queue; reload them after).
 *
 * EVENT QUEUE.  The port's queue is one struct (engine_event_queue_state_t), retail's six globals: rl_link_evq_load
 * loads it (and marks it open: a retail call inside stores / reloads it), rl_link_evq_store stores it back. */
#pragma once
#include <stdint.h>
#include "engine_event_queue.h"
#include "engine_dirty_rect.h"

void rl_link_sync_in(void);
void rl_link_sync_out(void);
int  rl_link_live(void);
int  rl_link_port_enter(void);        /* returns the caller's liveness for rl_link_port_leave */
void rl_link_port_leave(int saved);
void rl_link_retail_enter(void);
void rl_link_retail_leave(void);
engine_event_queue_state_t *rl_link_evq_load(void);
void rl_link_evq_store(void);

/* the dirty rectangle's mirror (retail: six scattered globals DAT_0050d09c / 0050a4c4 / 0050a4c8 / 0050a4bc /
 * 0050a4c0 / 0053e9ac); engine_territory_set_dirty_state() binds it */
extern engine_dirty_rect_state_t rl_link_dirty;

/* ---------------------------------------------------------------- the old ports' environments */
#include "localization.h"
/* the localized-text walker: the port's localization_state_t over retail's blob &DAT_00591580 + walker pointer
 * DAT_005c9270, loaded before an old port runs and stored after (and around its calls out) */
localization_state_t *rl_link_loc_load(void);
void rl_link_loc_store(void);
/* engine_hud_panel (the strategic county panel 0x40f7d3 and its sub-drawers, the End-turn strip 0x41a734) */
void rl_link_panel_env(void);

/* retail-layout entries of old ports (rows: tools/rhost/rh_hooks_mapview.c) */
int rl_40f7d3(int a1);   /* strat_draw_territory_panel: engine_hud_panel_draw */
int rl_41a734(int a1);   /* the End-turn strip: engine_hud_panel_endturn */

/* the screens family (src/rl_link_screens.c; rows: tools/rhost/rh_hooks_screens.c) */
void rl_link_screens_bind(void);
void rl_link_scr_callout_enter(void);   /* a call out of a screens entry into rl_ code: the walker stored */
void rl_link_scr_callout_leave(void);
int rl_476e95(void); int rl_477131(void); int rl_4770b8(void); int rl_477249(void); int rl_4772b6(void);
int rl_477320(void); int rl_4777aa(void); int rl_4b3f0a(int dest, int stride); int rl_4b3ec0(int src, int stride);
int rl_4517ca(int a0, int a1); int rl_4519ca(int a0, int a1, int a2);
int rl_451a5d(int a0, int a1, int a2, int a3, int a4); int rl_4518a5(int a0, int a1, int a2, int a3);
int rl_45161e(int county); int rl_4126c9(int a0, int a1, int a2); int rl_412666(void); int rl_412143(int full);
int rl_41f15a(void); int rl_423b4f(void); int rl_416cf3(void); int rl_4171ee(int a0, int a1); int rl_41e7e1(void);
int rl_41ea14(void); int rl_41eaa3(void); int rl_41ec8a(void); int rl_41ece6(void); int rl_41ef57(void);
int rl_41f01f(void); int rl_41f1dd(void); int rl_41f321(void); int rl_41f592(void); int rl_41f5e8(void);
int rl_4ae539(void); int rl_4ae5e2(int n); int rl_41f6c7(int tab); int rl_41f86d(int tab); int rl_41f98b(void);
int rl_41fbcb(void); int rl_4148e4(int a0); int rl_4149ec(int a0, int a1); int rl_4110b1(void); int rl_41152f(void);
int rl_4116fb(void); int rl_411b72(void); int rl_411fde(void);
int rl_41ac95(int a0, int a1, int a2, int a3, int a4, int a5); int rl_41a900(int a0, int a1, int a2, int a3);
int rl_41ab67(int a0, int a1, int a2, int a3, int a4, int a5); int rl_409397(int x, int y, int cols, int rows);
int rl_43aa83(int county); int rl_43ab25(int county); int rl_43a23f(int county); int rl_43a307(int county);
int rl_4156a7(int a0, int a1, int a2); int rl_438ceb(void); int rl_43a379(void); int rl_4bc142(int a0, int a1);
int rl_4bc8fc(void); int rl_4bcbdc(void); int rl_4bcbd1(void); int rl_4bd07f(void); int rl_4bd355(void);
int rl_4bd938(void); int rl_4bda92(void); int rl_4bdb95(void); int rl_4bdc31(int a0); int rl_4be4df(int a0);
int rl_4be7be(int a0); int rl_4beab9(int a0); int rl_4bd574(void); int rl_4bd759(int a0); int rl_4beed4(int a0);
int rl_4bd233(void); int rl_4be070(int a0); int rl_4be33f(void); int rl_4bf1f9(void); int rl_4bc51a(int a0, int a1);
int rl_4bc1d1(int a0); int rl_4bc107(int a0, int a1, int a2, int a3, int a4, int a5); int rl_4bc222(int a0, int a1);
int rl_4bc2a5(int a0); int rl_4bc2c5(int a0); int rl_4bc2e5(int a0); int rl_4bc346(int a0); int rl_4bc3ac(int a0);
int rl_4bc40d(int a0); int rl_4bc471(int a0, int a1); int rl_4bc4bb(int a0, int a1); int rl_4bc505(void);
int rl_416308(void); int rl_41635f(void); int rl_4181eb(void); int rl_418426(void); int rl_419243(void);
int rl_418fc5(void); int rl_4190db(void); int rl_4aa90a(int county, int population);
int rl_41ac3e(int a0, int a1, int a2, int a3, int a4, int a5); int rl_418653(void); int rl_435ebc(int county, int n);
int rl_435cef(void); int rl_417ea7(int arg); int rl_41dedb(int a0, int a1, int a2, int a3); int rl_416925(void);
