/*
 * test_engine_terrain_info.c — coverage for the terrain-info opener
 * (engine_terrain_info_open, port of FUN_0043caf4).
 *
 * The sub-calls are bound to recording hooks: each logs an id when called,
 * and the resolve-hover / terrain-compute hooks publish programmed results
 * into the state (st->tile_index, st->county_id, st->head_link) the way
 * retail's FUN_00429ba4 / FUN_0046d5fe write the globals the later branches
 * read.  The tests pin the published cell geometry, the guard branches, and
 * the army-hover branch condition.
 */

#include <string.h>

#include "t.h"
#include "engine_terrain_info.h"
#include "localization.h"

/* ── recording hook harness ──────────────────────────────────────────── */

enum { H_RESOLVE = 1, H_COMPUTE, H_DISMISS, H_MODE2, H_ARMY, H_MAP,
       H_PANEL, H_TAIL, H_MAX };

static int g_log[32];
static int g_logn;

/* programmed compute outputs published by the recording hooks */
static int g_prog_tile;
static int g_prog_county;
static int g_prog_head;

static void rec(int id) { if (g_logn < 32) g_log[g_logn++] = id; }

static void hk_resolve(const engine_terrain_info_open_in_t *in,
                       engine_terrain_info_open_state_t *st)
{ (void)in; rec(H_RESOLVE); st->tile_index = g_prog_tile; }

static void hk_compute(const engine_terrain_info_open_in_t *in,
                       engine_terrain_info_open_state_t *st)
{ (void)in; rec(H_COMPUTE); st->county_id = g_prog_county; st->head_link = g_prog_head; }

#define DEF_HOOK(name, id) \
    static void name(const engine_terrain_info_open_in_t *in, \
                     engine_terrain_info_open_state_t *st) \
    { (void)in; (void)st; rec(id); }
DEF_HOOK(hk_dismiss, H_DISMISS)
DEF_HOOK(hk_mode2,   H_MODE2)
DEF_HOOK(hk_army,    H_ARMY)
DEF_HOOK(hk_map,     H_MAP)
DEF_HOOK(hk_panel,   H_PANEL)
DEF_HOOK(hk_tail,    H_TAIL)

static const engine_terrain_info_hooks_t REC_HOOKS = {
    .resolve_hover   = hk_resolve,
    .terrain_compute = hk_compute,
    .dismiss_scroll  = hk_dismiss,
    .mode2_jump      = hk_mode2,
    .army_select     = hk_army,
    .map_render      = hk_map,
    .panel_render    = hk_panel,
    .tail            = hk_tail,
};

static void reset(void)
{
    g_logn = 0;
    memset(g_log, 0, sizeof g_log);
    g_prog_tile = 0;
    g_prog_county = 0;
    g_prog_head = 0;
}

static int logged(int id)
{
    for (int i = 0; i < g_logn; i++) if (g_log[i] == id) return 1;
    return 0;
}

/* The retail geometry constants. */
static engine_terrain_info_open_in_t base_in(void)
{
    engine_terrain_info_open_in_t in = {0};
    in.game_mode   = 0;       /* campaign */
    in.tile_stride = 8;       /* DAT_0057d338 */
    in.map_width   = 0x40;    /* DAT_0052afd4 */
    in.view_mode   = 0;
    in.scroll_open = 0;
    in.army_active_plane = NULL;
    return in;
}

/* ── tests ───────────────────────────────────────────────────────────── */

/* game_mode != 0 (not campaign) → whole opener is a no-op. */
int test_terrain_info_non_campaign_noop(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    in.game_mode = 1;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);
    T_ASSERT_EQ_I(g_logn, 0);            /* no sub-calls at all */
    T_ASSERT_EQ_I(st.screen, 4);         /* untouched */
    T_ASSERT_EQ_I(st.compute_tile, 0);   /* nothing published */
    return 0;
}

/* Normal open: publishes cell geometry, fires the sub-calls in order, and
 * leaves screen at 4 (the panel stays up). */
int test_terrain_info_normal_open_publishes_and_renders(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    /* tile_index 0x208 = cell 0x41 = (col 1, row 1) at width 0x40 */
    g_prog_tile = 0x208;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT_EQ_I(st.tile_index, 0x208);
    T_ASSERT_EQ_I(st.copy_tile, 0x208);
    T_ASSERT_EQ_I(st.cell_index, 0x41);  /* 0x208 / 8 */
    T_ASSERT_EQ_I(st.col, 1);            /* 0x41 % 0x40 */
    T_ASSERT_EQ_I(st.row, 1);            /* 0x41 / 0x40 */
    T_ASSERT_EQ_I(st.compute_tile, 0x208);
    T_ASSERT_EQ_I(st.state_5681cc, 3);
    T_ASSERT_EQ_I(st.screen, 4);         /* panel stays open */

    /* order: resolve, compute, panel, tail (no army branch, no scroll) */
    T_ASSERT_EQ_I(g_logn, 4);
    T_ASSERT_EQ_I(g_log[0], H_RESOLVE);
    T_ASSERT_EQ_I(g_log[1], H_COMPUTE);
    T_ASSERT_EQ_I(g_log[2], H_PANEL);
    T_ASSERT_EQ_I(g_log[3], H_TAIL);
    return 0;
}

/* pending_flag set → snap to screen 0, clear the guard, skip compute+panel. */
int test_terrain_info_pending_flag_snaps_back(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    g_prog_tile = 0x100;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    st.pending_flag = 1;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT_EQ_I(st.pending_flag, 0);   /* cleared */
    T_ASSERT_EQ_I(st.compute_tile, 0x100); /* geometry still published first */
    /* only resolve_hover ran (geometry); no compute / panel */
    T_ASSERT_EQ_I(g_logn, 1);
    T_ASSERT_EQ_I(g_log[0], H_RESOLVE);
    return 0;
}

/* view_mode == 2 → snap to screen 0 + mode2 jump, no panel. */
int test_terrain_info_viewmode2_jumps(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    in.view_mode = 2;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT_EQ_I(st.screen, 0);
    T_ASSERT(logged(H_MODE2));
    T_ASSERT(!logged(H_COMPUTE));
    T_ASSERT(!logged(H_PANEL));
    return 0;
}

/* county under cursor → county_store set, army branch suppressed even with a
 * head-link present. */
int test_terrain_info_county_sets_store_no_army(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    static const uint8_t army_plane[0x600] = { [0x300] = 0x11 }; /* head 1 active */
    in.army_active_plane = army_plane;
    g_prog_county = 7;
    g_prog_head = 1;       /* present, but county != 0 suppresses the branch */
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT_EQ_I(st.county_store, 7);
    T_ASSERT(!logged(H_ARMY));
    T_ASSERT(!logged(H_MAP));    /* army branch (incl. map render) gated on county==0 */
    T_ASSERT(logged(H_PANEL));
    return 0;
}

/* army-hover branch: head present, not the current army, no county, and the
 * army "active" field is non-zero → select it + re-render the map. */
int test_terrain_info_army_hover_selects(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    /* army record stride 0x300; head_link 2 → active field at 2*0x300. */
    static uint8_t army_plane[0x900];
    memset(army_plane, 0, sizeof army_plane);
    army_plane[2 * 0x300 + 0] = 0x55;   /* LE int32 != 0 */
    in.army_active_plane = army_plane;
    g_prog_head = 2;
    g_prog_county = 0;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    st.cur_army = 0;       /* head 2 != cur 0 */
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT_EQ_I(st.army_ptr, 0x55);
    T_ASSERT_EQ_I(st.cur_army, 2);       /* DAT_0057c93c = head_link */
    T_ASSERT_EQ_I(st.army_selected, 1);
    T_ASSERT_EQ_I(st.scene_flag, 1);
    T_ASSERT(logged(H_ARMY));
    T_ASSERT(logged(H_MAP));
    T_ASSERT(logged(H_PANEL));
    return 0;
}

/* army branch entered but the active field is 0 → no select / no cur_army
 * update, but scene_flag + map render still fire (retail does them
 * unconditionally inside the branch). */
int test_terrain_info_army_hover_inactive_no_select(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    static const uint8_t army_plane[0x900] = {0};   /* all-zero → field 0 */
    in.army_active_plane = army_plane;
    g_prog_head = 2;
    g_prog_county = 0;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    st.cur_army = 0;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT_EQ_I(st.army_ptr, 0);
    T_ASSERT_EQ_I(st.army_selected, 0);
    T_ASSERT_EQ_I(st.cur_army, 0);       /* unchanged */
    T_ASSERT_EQ_I(st.scene_flag, 1);     /* still set */
    T_ASSERT(!logged(H_ARMY));
    T_ASSERT(logged(H_MAP));
    return 0;
}

/* army branch suppressed when the hovered army is already selected. */
int test_terrain_info_army_already_selected_skips(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    static uint8_t army_plane[0x900];
    memset(army_plane, 0, sizeof army_plane);
    army_plane[2 * 0x300] = 0x55;
    in.army_active_plane = army_plane;
    g_prog_head = 2;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    st.cur_army = 2;        /* head == cur → branch skipped */
    engine_terrain_info_open(&in, &st, &REC_HOOKS);

    T_ASSERT(!logged(H_ARMY));
    T_ASSERT(!logged(H_MAP));
    T_ASSERT_EQ_I(st.scene_flag, 0);
    T_ASSERT(logged(H_PANEL));
    return 0;
}

/* an open event scroll is dismissed before the panel renders. */
int test_terrain_info_open_scroll_dismissed(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    in.scroll_open = 1;
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    engine_terrain_info_open(&in, &st, &REC_HOOKS);
    T_ASSERT(logged(H_DISMISS));
    return 0;
}

/* NULL hooks must not crash (pure skeleton). */
int test_terrain_info_null_hooks_safe(void)
{
    reset();
    engine_terrain_info_open_in_t in = base_in();
    engine_terrain_info_open_state_t st = {0};
    st.screen = 4;
    engine_terrain_info_open(&in, &st, NULL);
    /* tile_index never set (no resolve hook) → geometry from 0 */
    T_ASSERT_EQ_I(st.state_5681cc, 3);
    T_ASSERT_EQ_I(st.screen, 4);
    return 0;
}

/* ── panel/text classifier coverage (FUN_0041befe / FUN_0041c208) ─────── */

/* head_link 1 army record with bytes set at the given offsets. */
static engine_terrain_info_attr_t attr_with(int flags, int type, int player,
                                            int owner, int derived)
{
    engine_terrain_info_attr_t a = {0};
    a.flags = flags; a.terrain_type = type; a.head_link = 1;
    a.player_id = player; a.owner = owner; a.derived_bit = derived;
    return a;
}

/* Empty terrain (flags 0): the "Here Be Dragons" panel. */
int test_terrain_info_class_empty(void)
{
    engine_terrain_info_attr_t a = attr_with(0, 0, 0, 0, 0);
    engine_terrain_info_panel_class_t pc = engine_terrain_info_classify_panel(&a);
    engine_terrain_info_text_class_t  tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(pc.row, 0x11);
    T_ASSERT_EQ_I(pc.off, 2);
    T_ASSERT_EQ_I(tc.idx20, 0);
    T_ASSERT_EQ_I(tc.idx1c, 0x16);
    T_ASSERT_EQ_I(tc.idx8, 0x17);
    T_ASSERT_EQ_I(tc.idxc, 0);
    T_ASSERT_EQ_I(tc.alert_arg, 0);
    return 0;
}

/* flag-bit priority: bit0 (flags&1) wins over every other bit. */
int test_terrain_info_class_bit0_priority(void)
{
    engine_terrain_info_attr_t a = attr_with(0xff, 5, 0, 0, 0);  /* all bits */
    engine_terrain_info_text_class_t tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 1);    /* the flags&1 arm */
    T_ASSERT_EQ_I(tc.idx1c, 0x17);
    T_ASSERT_EQ_I(tc.idx8, 0x18);
    return 0;
}

/* flags&0x10 panel row + the type-keyed text indices. */
int test_terrain_info_class_bit4(void)
{
    engine_terrain_info_attr_t a = attr_with(0x10, 0x10, 0, 0, 0);
    engine_terrain_info_panel_class_t pc = engine_terrain_info_classify_panel(&a);
    engine_terrain_info_text_class_t  tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(pc.row, 0x10);
    T_ASSERT_EQ_I(pc.off, 0);
    T_ASSERT_EQ_I(tc.idx20, 0x30);  /* type == 0x10 */
    T_ASSERT_EQ_I(tc.idx1c, 0x31);
    T_ASSERT_EQ_I(tc.idx8, 0x1b);
    a.terrain_type = 0x11;
    tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 0x32);  /* type != 0x10 */
    T_ASSERT_EQ_I(tc.idx1c, 0x33);
    return 0;
}

/* flags&8 derived-bit split. */
int test_terrain_info_class_bit3_derived(void)
{
    engine_terrain_info_attr_t a = attr_with(8, 0, 0, 0, 0);
    engine_terrain_info_text_class_t tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 5); T_ASSERT_EQ_I(tc.idx1c, 0x1a); T_ASSERT_EQ_I(tc.idx8, 0x1a);
    a.derived_bit = 1;
    tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 4); T_ASSERT_EQ_I(tc.idx1c, 0x19); T_ASSERT_EQ_I(tc.idx8, 0x19);
    return 0;
}

/* flags&0x40 panel row depends on the army record byte +0x1ad. */
int test_terrain_info_class_bit6_army_byte(void)
{
    static uint8_t blk[0x600];
    memset(blk, 0, sizeof blk);
    engine_terrain_info_attr_t a = attr_with(0x40, 0, 0, 0, 0);
    a.army_block = blk;
    /* +0x1ad zero → row 0x11 */
    engine_terrain_info_panel_class_t pc = engine_terrain_info_classify_panel(&a);
    T_ASSERT_EQ_I(pc.row, 0x11);
    /* +0x1ad set → row 0xf */
    blk[1 * 0x300 + 0x1ad] = 1;
    pc = engine_terrain_info_classify_panel(&a);
    T_ASSERT_EQ_I(pc.row, 0xf);
    /* text arm is the flat flags&0x40 case */
    engine_terrain_info_text_class_t tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 7); T_ASSERT_EQ_I(tc.idx1c, 0x1b); T_ASSERT_EQ_I(tc.idx8, 0x1b);
    return 0;
}

/* flags&0x80 named terrain (type 0xd..0x19): idx20 from army byte +0x1c3,
 * idx1c = type+7, panel row from the player==owner / army-byte branches. */
int test_terrain_info_class_bit7_named(void)
{
    static uint8_t blk[0x600];
    memset(blk, 0, sizeof blk);
    engine_terrain_info_attr_t a = attr_with(0x80, 0x14, 3, 3, 0);  /* owned */
    a.army_block = blk;

    /* army +0x1c3 == 0, +0x1c2 == 0 → panel row 0xe */
    engine_terrain_info_panel_class_t pc = engine_terrain_info_classify_panel(&a);
    T_ASSERT_EQ_I(pc.row, 0xe);
    engine_terrain_info_text_class_t tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 8);          /* byte +0x1c3 != 1 && != 2 */
    T_ASSERT_EQ_I(tc.idx1c, 0x14 + 7);   /* type + 7 */
    T_ASSERT_EQ_I(tc.idx8, 0x1c);

    /* army +0x1c3 == 1 → idx20 0xe; +0x1c3 set non-zero → panel uses
     * player==owner branch → row 10 */
    blk[1 * 0x300 + 0x1c3] = 1;
    pc = engine_terrain_info_classify_panel(&a);
    T_ASSERT_EQ_I(pc.row, 10);
    tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 0xe);
    return 0;
}

/* flags&0x80 army-stats arm (type<0xd): records the SFX-cue arg + bumps
 * the body sub-index from the per-unit stat fields. */
int test_terrain_info_class_bit7_army_stats(void)
{
    static uint8_t blk[0x600];
    memset(blk, 0, sizeof blk);
    engine_terrain_info_attr_t a = attr_with(0x80, 2, 0, 0, 0);  /* type < 4 */
    a.army_block = blk;
    /* type<4 → idx18=1, base 0x18; stat diff = int32[0x2a0+0x18] - [0x2a4+0x18].
     * Set a diff of 0x20 (in [0x19,0x32) → +2), readiness byte +0x296+0x18 == 0. */
    blk[1 * 0x300 + 0x2a0 + 0x18] = 0x20;   /* low byte of the first int32 */
    engine_terrain_info_text_class_t tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 9);
    T_ASSERT_EQ_I(tc.idx18, 1);
    T_ASSERT_EQ_I(tc.idx8, 0x1e);
    T_ASSERT_EQ_I(tc.alert_arg, 10);
    T_ASSERT_EQ_I(tc.idx1c, 0x35 + 2);      /* base 0x35, diff 0x20 → +2 */
    /* readiness byte non-zero → flat +4 instead */
    blk[1 * 0x300 + 0x296 + 0x18] = 1;
    tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx1c, 0x35 + 4);
    return 0;
}

/* flags&0x20 special arm: indices from the 4-int table; NULL table → the
 * inert default (a deferred special feature). */
int test_terrain_info_class_bit5_table(void)
{
    static const int32_t table[0x20 * 4];   /* zero-filled */
    static int32_t tbl[0x20 * 4];
    memset(tbl, 0, sizeof tbl);
    tbl[5 * 4 + 0] = 0x40; tbl[5 * 4 + 1] = 0x41; tbl[5 * 4 + 2] = 0x42; tbl[5 * 4 + 3] = 7;
    engine_terrain_info_attr_t a = attr_with(0x20, 5, 3, 3, 0);
    a.terr_table = tbl;
    engine_terrain_info_text_class_t tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 0x40);
    T_ASSERT_EQ_I(tc.idx1c, 0x41);
    T_ASSERT_EQ_I(tc.idx8, 0x42);
    T_ASSERT_EQ_I(tc.idxc, 7);
    /* panel row: flags&0x20, owned, type 5 (<0x19, !=0) → row 5 */
    engine_terrain_info_panel_class_t pc = engine_terrain_info_classify_panel(&a);
    T_ASSERT_EQ_I(pc.row, 5);
    /* NULL table → inert default (idxc 0 so no 0x12 fixup) */
    a.terr_table = NULL;
    tc = engine_terrain_info_classify_text(&a);
    T_ASSERT_EQ_I(tc.idx20, 0);
    T_ASSERT_EQ_I(tc.idx1c, 0x16);
    (void)table;
    return 0;
}

/* The render driver must not crash with NULL assets (drop-in guards). */
int test_terrain_info_render_null_assets_safe(void)
{
    engine_terrain_info_attr_t a = attr_with(0, 0, 0, 0, 0);
    engine_terrain_info_render_ctx_t ctx = {0};
    /* loc NULL would deref in the text path; the localized renderers guard
     * a NULL blob, but loc itself must be valid — pass a zeroed loc. */
    localization_state_t loc = {0};
    ctx.loc = &loc;
    engine_terrain_info_render(&a, &ctx);   /* all sheets NULL → draws skipped */
    return 0;
}
