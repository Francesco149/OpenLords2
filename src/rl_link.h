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
