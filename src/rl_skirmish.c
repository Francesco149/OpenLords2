/* rl_skirmish.c — a retail-layout port, partition 'skirmish' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * Skirmish mode (title page 12, DAT_0057a0f0 = 1): the setup pages' draws (0x42051c / 0x42150b and their parts),
 * the battle list (battles.eng) and the per-battle troop table (troops*.eng, a user .skr), the side setup, the
 * Battle Master ratings (score.dat) and the masters table. Draws go through the gfx layer on retail's globals
 * (as rl_render.c: the localization walker / caret helpers below load the gfx ports' state structs from retail's
 * globals). Findings: docs/findings/rl-skirmish.md. */
#include "rl.h"
#include "rl_skirmish_globals.h"
#include "rl_render_globals.h"
#include "rl_io_globals.h"
#include "engine_bf_input_globals.h"
#include "engine.h"
#include "engine_combat.h"
#include "engine_territory.h"
#include "engine_dirty_rect.h"
#include "engine_army_state.h"
#include "engine_math.h"
#include "gfx_globals.h"
#include "gfx_line.h"
#include "gfx_sprite.h"
#include "gfx_sprite_render.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_wrap.h"
#include "gfx_caret.h"
#include "font_metrics.h"
#include "localization.h"
#include "text_input.h"
#include "strutil.h"
#include "rl_io_plat.h"

#include <string.h>

/* the glue partition's masters table (rl_glue_globals.h cannot be included beside rl_render_globals.h: their two
 * views of g_rl_5c9270) — the same declarations */
extern uint8_t g_rl_51fbc0[0x140];   /* byte_51FBC0: the 10 masters' names, 32 B each */
extern uint8_t g_rl_51fd00[0x28];    /* dword_51FD00: the 10 masters' ratings */
extern int32_t g_rl_51fd28[10];      /* dword_51FD28: the player's 10 best ratings */

/* ---------------------------------------------------------------- retail's blobs and tables, by address */
#define FONT       ((const uint8_t *)g_bf_5af8f0)        /* &DAT_005af8f0: the default 16 px font */
#define FONT_TALL  ((const uint8_t *)g_rl_5b2fa0)        /* &DAT_005b2fa0: the tall 24 px font */
#define FONT_SMALL ((const uint8_t *)g_rl_5cbfb0)        /* &DAT_005cbfb0: the small (debug) font */
#define SYSTEM     ((const uint8_t *)g_rl_5bb540)        /* &DAT_005bb540: system2.pl8 (the seal) */
#define GLYPHS     ((const uint8_t *)g_rl_4d71d0 + 0x20) /* DAT_004d71f0: the renderers' char -> glyph table */
#define METRICS    ((const unsigned char *)g_rl_4d71d0)  /* DAT_004d71d0: the measurers' table */
#define PTR(v)     ((const uint8_t *)(uintptr_t)(uint32_t)(v))   /* a retail pointer held in a dword global */
#define MISC       PTR(g_bf_5530c8)                      /* DAT_005530c8: the misc / icon sheet (0x498dcb reloads it) */
#define STR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))
#define SK(va)     STR(g_rl_4d4374, 0x4d4374u, va)       /* the " " suffixes and score1/2 names, 0x4d4374.. */
#define SKF(va)    STR(g_rl_4d4bc8, 0x4d4bc8u, va)       /* battles.eng / troops*.eng / score.dat / "*.skr", 0x4d4cd0.. */
#define DIRTY      engine_territory_dirty_state()        /* 0x4527a6's six globals (a mirror: rh_hooks_ai.c) */
#define SKR_NAME   ((char *)g_rl_53f5fc)                 /* byte_53F1E0 + 0x41c: the .skr file chosen (lords2.inf) */
/* the player records (save.h): DAT_0057bf00 + 352 p (strategic), DAT_00553d50 + 44 p (aux: +4 the name, +0x28 the
 * difficulty) */
#define PL8(p, va)   AT(uint8_t, g_player_strategic_352, 0x57bf00u, (va) + 352 * (p))
#define PA8(p, va)   AT(uint8_t, g_player_aux_44, 0x553d50u, (va) + 44 * (p))
#define PNAME(p)     ((char *)&PA8(p, 0x553d54u))
/* the territory records (420 B; DAT_0052f0b0): territories 1 / 2 hold the two sides' armies during a skirmish
 * (DAT_00553efc / DAT_00553f34), 3 / 4 a backup (0x42c5ad), 5..8 the before / after copies the ratings compare */
#define TE8(t, va)   AT(uint8_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define TE16(t, va)  AT(int16_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define TE32(t, va)  AT(int32_t, g_per_territory_420, 0x52f0b0u, (va) + 420 * (t))
#define TROOP(t, k)  TE16(t, 0x52f21cu + 2 * (k))         /* word_52F21C[210 t + k]: troop type k's count */
#define TOTAL(t)     TE32(t, 0x52f218u)                   /* dword_52F218[105 t]: the first 7 types' total */
#define TERR(t)      (&TE8(t, 0x52f0b0u))
/* the battle troop table DAT_00516ac0: 55 battles x 220 B = [side 0..1: 110 B][tier 0..4: 22 B][type 0..10: WORD]
 * (IDA's word_516AC0 .. word_516B86 are the ten (side, tier) rows: 0x516ac0 + 110 side + 22 tier); battles 0..34
 * from troops*.eng, 35..54 from the user .skr (0x42d4aa) */
#define TR16(b, side, tier, k) \
    AT(int16_t, g_rl_516ac0, 0x516ac0u, 0x516ac0u + 220 * (b) + 110 * (side) + 22 * (tier) + 2 * (k))
#define COST(k)      g_bf_4d4b98[k]                       /* dword_4D4B98[k]: troop type k's cost */
#define MODE         ((int32_t)s_slot_0053e91c)           /* the battle set: 0 field, 1 unequal, 2 siege, 3 user .skr */
#define BATTLE       ((int32_t)s_slot_0056d590)           /* the battle chosen in the set */
#define BASE(m)      g_rl_4d49b8[m]                       /* dword_4D49B8[set]: its first battle (0 / 10 / 20 / 35) */
#define HUMAN        ((int32_t)s_slot_0057c8cc)
#define CPU          ((int32_t)s_slot_0056d5cc)
#define ATTACKER     ((int32_t)s_slot_0053ef5c)
#define SIDE_A       ((int32_t)s_slot_00553efc)           /* the human's territory (1 or 2) */
#define SIDE_B       ((int32_t)s_slot_00553f34)           /* the computer's */
/* DAT_005533e0[p]: a side's ready flag (MP; p is 1 or 2) — [2] IS DAT_005533e8, engine_army_state's
 * g_army_state_index (a mirror: rh_hooks_ai.c k_mirror_va) */
static int32_t *ready(int p)
{ return p == 2 ? (int32_t *)&g_army_state_index : &AT(int32_t, g_rl_5533e0, 0x5533e0u, 0x5533e0u + 4 * p); }

/* ---------------------------------------------------------------- the gfx layer's state structs (as rl_render.c) */
static localization_state_t s_loc;
static localization_state_t *loc_in(void)
{
    s_loc.blob = g_rl_591580;
    s_loc.walker_offset = (size_t)(g_rl_5c9270 - g_rl_591580);
    return &s_loc;
}
static void loc_out(void)
{
    uint8_t *w = g_rl_591580 + s_loc.walker_offset;
    if (g_rl_5c9270 != w) g_rl_5c9270 = w;
}
static void wrap_line(int idx, int skip, int x, int y, const uint8_t *font, int color)   /* 0x402d37 */
{
    gfx_wrap_localized_line(loc_in(), idx, skip, x, y, font, GLYPHS, (uint8_t)color);
    loc_out();
}
static void wrap_line_c(int idx, int skip, int x, int y, int w, const uint8_t *font, int color)   /* 0x402c5e */
{
    gfx_wrap_localized_line_centered(loc_in(), idx, skip, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
    loc_out();
}
static void wrap_buf(const uint8_t *buf, int x, int y, int w, const uint8_t *font, int color)   /* 0x40352f */
{
    gfx_wrap_buffer(buf, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
}
static void text_line(const void *buf, int x, int y, const uint8_t *font, int color)   /* 0x402637 */
{
    gfx_text_render_line((const uint8_t *)buf, x, y, font, GLYPHS, (uint8_t)color);
}
static void text_line_c(const void *buf, int x, int y, int w, const uint8_t *font, int color)   /* 0x4025d7 */
{
    gfx_text_render_line_centered((const uint8_t *)buf, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
}
static void label(int v, int pad, const char *suffix, int x, int y, const uint8_t *font, int color)   /* 0x402f64 */
{
    gfx_text_label_render_dec(v, (char)pad, suffix, x, y, font, GLYPHS, (uint8_t)color);
}
static void label_hex(int v, int pad, const char *suffix, int x, int y, const uint8_t *font, int color)   /* 0x403015 */
{
    gfx_text_label_render_hex(v, (char)pad, suffix, x, y, font, GLYPHS, (uint8_t)color);
}
static void label_c(int v, int pad, const char *suffix, int x, int y, int w, const uint8_t *font,
                    int color)   /* 0x4030c6 */
{
    gfx_text_label_render_dec_centered(v, (char)pad, suffix, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
}
static void sprite(int idx, int x, int y) { gfx_sprite_render_from_blob(MISC, idx, x, y); }   /* 0x40a21a */
/* the caret: the port's editor + measurer cache over retail's (buf DAT_005cd550, cursor DAT_005bb4a8, insert
 * DAT_005c9280; cache DAT_005c9288 / DAT_0058fd88) */
static void caret(const uint8_t *font, int color)   /* 0x40acce */
{
    static text_input_state_t ed;
    int cur = g_gfx_first_pos_target;   /* DAT_005bb4a8: also the editor's cursor */
    ed.cursor = cur;
    ed.insert_mode = g_rl_5c9280;
    if (cur >= 0 && cur < TEXT_INPUT_BUF_SIZE) ed.buf[cur] = g_rl_5cd550[cur];
    font_metrics_state_t fm = { .char_glyph_index = METRICS, .last_glyph_index = g_gfx_last_glyph_index,
                                .last_glyph_offset = g_gfx_last_glyph_offset };
    gfx_caret_render(&ed, &fm, font, (uint8_t)color);
    if (g_gfx_last_glyph_index != fm.last_glyph_index) g_gfx_last_glyph_index = fm.last_glyph_index;
    if (g_gfx_last_glyph_offset != fm.last_glyph_offset) g_gfx_last_glyph_offset = fm.last_glyph_offset;
}
static void mark_box(int x, int y, int w, int h, int force)   /* 0x4527a6 */
{
    engine_dirty_rect_mark_box_tiled(DIRTY, x, y, w, h, force);
}
static void mark_full(void) { engine_territory_mark_full_screen_dirty(); }   /* 0x45240a */

/* ---------------------------------------------------------------- seams (callees outside the partition) */
/* ported, not on retail's layout / not linked in rhost — TODO(rl-link) */
static void chdir_base(void) { engine_retail_call(0x4af502u, 0); }   /* TODO(rl-link): io_path_chdir_base */
static void read_file_at(const void *name, const void *dst, uint32_t n, uint32_t off)   /* TODO(rl-link): io_read_file_at 0x4af8d9 */
{ engine_retail_call(0x4af8d9u, 4, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)dst, n, off); }
static void load_background(const void *name, int h)   /* gfx_load_background_image 0x408fcb (rl ui4) */
{ rl_408fcb((int)(uintptr_t)name, h); }
static void load_panel_sheet(int n) { engine_retail_call(0x4b25b0u, 1, (uint32_t)n); }   /* TODO(rl-link): gfx_load_panel_sheet */
static void set_palette(const void *pal) { engine_retail_call(0x4b0ab5u, 1, (uint32_t)(uintptr_t)pal); }   /* TODO(rl-link): gfx.c's palette set 0x4b0ab5 */
static void panel_box(int x, int y, int cols, int rows)   /* TODO(rl-link): 0x409397 = gfx_panel_draw_border_chrome(mode 0) + gfx_panel_fill_interior_chrome (a static in several modules) */
{ engine_retail_call(0x409397u, 4, (uint32_t)x, (uint32_t)y, (uint32_t)cols, (uint32_t)rows); }
static void bf_minimap_scroll(int a) { engine_retail_call(0x4bc1d1u, 1, (uint32_t)a); }   /* TODO(rl-link): engine_battlefield_render_minimap_scroll */
static void load_system_sheet(int a) { engine_retail_call(0x499a1cu, 1, (uint32_t)a); }   /* TODO(rl-link): gfx_load_system_sheet_select */
static int  file_size(const char *name) { return rl_4af70c((int)(uintptr_t)name); }   /* 0x4af70c: the size, 0 when absent (ui2) */
/* 0x46ead0 is memset(record, 0, 0x1a4) (no other effect; EAX unused): inlined */

/* ================================================================ the setup page's parts */

/* Port of FUN_00420147 (463 B) — the MP chat line (DAT_00553030 set): a 22-row strip cleared (at (168, 150) 303
 * wide on the setup page a1 == 0, else (11, 56) 433 wide), the typed line byte_553E80 in the human's colour (realm
 * +9), the first-glyph position latched (DAT_0058fd58 / 54 = where the line starts), the caret, the box dirty. */
int rl_420147(int a1)
{
    if (g_sim_mp_active == 0) return 0;
    g_gfx_no_shadow = 1;
    int x = a1 == 0 ? 174 : 15, y = a1 == 0 ? 155 : 60;
    if (a1 == 0) gfx_line_fill_rect(168, 150, 303, 22, 63);
    else gfx_line_fill_rect(11, 56, 433, 22, 63);
    g_gfx_first_pos_latch = 0;
    g_gfx_first_pos_x = 0;
    g_gfx_char_counter = 0;
    g_gfx_collapse_active = 1;
    g_gfx_line_x_acc = 0;
    text_line(g_rl_553e80, x, y, FONT, PL8(HUMAN, 0x57bf09u));
    if (g_gfx_first_pos_latch == 0) {
        g_gfx_first_pos_x = g_gfx_line_x_acc;
        g_gfx_first_pos_latch = 1;
    }
    g_gfx_first_pos_x += x;
    g_gfx_first_pos_y = y;
    caret(FONT, 32);
    mark_box(168, 150, 22, 3, 1);
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_004207c3 (510 B) — the battle-set buttons (strings 20..22 of group 11 at x 508, y 299 / 331 / 363:
 * colour 32 on the chosen set DAT_0053e91c — 2 siege, 0 field, 1 unequal — else 63), the user-map button (string 35
 * in 249 when no .skr exists DAT_0055cd34, else the .skr's name, 32 when set 3), and the chosen set's sprite. */
int rl_4207c3(void)
{
    wrap_line_c(11, 20, 508, 299, 120, FONT, MODE == 2 ? 32 : 63);
    wrap_line_c(11, 21, 508, 331, 120, FONT, MODE == 0 ? 32 : 63);
    wrap_line_c(11, 22, 508, 363, 120, FONT, MODE == 1 ? 32 : 63);
    int c = MODE == 3 ? 32 : 63;
    if (g_rl_55cd34 != 0) wrap_line_c(11, 35, 495, 392, 141, FONT, 249);
    else text_line_c(SKR_NAME, 495, 392, 141, FONT, c);
    switch (MODE) {
    case 2: sprite(24, 464, 292); break;
    case 0: sprite(25, 464, 323); break;
    case 1: sprite(26, 464, 354); break;
    case 3: sprite(27, 464, 385); break;
    default: break;
    }
    return 0;
}

/* Port of FUN_004209c1 (895 B) — the SP setup page's sides: both names (aux +4) at y 150, "Attacker" / "Defender"
 * (strings 23 / 24) and their sprites 29 / 28 by who attacks (DAT_0053ef5c == 1: the human), both banners (realm
 * +0xa, sprite + 8), the advantage label (string 40, 41 at level 2) and its arrow (sprites 30 / 32 / 31 for the
 * human's difficulty byte_553DA4 3 / 2 / 1), both army values (string 28 + DAT_0051fbbc the human's / DAT_0051fad0
 * the computer's). */
int rl_4209c1(void)
{
    text_line_c(PNAME(1), 14, 320, 150, FONT, 63);
    text_line_c(PNAME(2), 280, 320, 150, FONT, 63);
    if (ATTACKER == 1) {
        wrap_line_c(11, 23, 11, 274, 120, FONT, 63);
        wrap_line_c(11, 24, 278, 274, 120, FONT, 63);
        sprite(29, 127, 268);
        sprite(28, 394, 268);
    } else {
        wrap_line_c(11, 24, 11, 274, 120, FONT, 63);
        wrap_line_c(11, 23, 278, 274, 120, FONT, 63);
        sprite(28, 127, 268);
        sprite(29, 394, 268);
    }
    sprite(PL8(1, 0x57bf0au) + 8, 56, 358);
    sprite(PL8(2, 0x57bf0au) + 8, 324, 358);
    uint8_t lvl = PA8(1, 0x553d78u);
    wrap_line_c(11, lvl == 2 ? 41 : 40, 160, 456, 140, FONT, 63);
    switch (lvl) {
    case 3: sprite(30, 198, 364); break;
    case 2: sprite(32, 214, 350); break;
    case 1: sprite(31, 191, 363); break;
    default: break;
    }
    g_gfx_line_x_acc = 0;
    wrap_line(11, 28, 52, 454, FONT, 63);
    if (HUMAN == 1) label(g_rl_51fbbc, 32, SK(0x4d4374u), g_gfx_line_x_acc + 52, 454, FONT, 63);
    else label(g_rl_51fad0, 32, SK(0x4d4378u), g_gfx_line_x_acc + 52, 454, FONT, 63);
    g_gfx_line_x_acc = 0;
    wrap_line(11, 28, 328, 454, FONT, 63);
    if (HUMAN == 2) label(g_rl_51fbbc, 32, SK(0x4d437cu), g_gfx_line_x_acc + 328, 454, FONT, 63);
    else label(g_rl_51fad0, 32, SK(0x4d4380u), g_gfx_line_x_acc + 328, 454, FONT, 63);
    return 0;
}

/* Port of FUN_00420d40 (164 B) — the animated globe (sprites 33..44): every 100 ms of timeGetTime (signed delta)
 * the frame DAT_004d31e8 steps (wraps at 12) and the screen is marked dirty; drawn at (178, 244), or (270, 206) on
 * the MP page DAT_0056899c. */
int rl_420d40(void)
{
    int x = g_rl_56899c != 0 ? 270 : 178, y = g_rl_56899c != 0 ? 206 : 244;
    DWORD t = rl_plat_timeGetTime();
    if ((int)(t - (DWORD)g_rl_4d31ec) > 100) {
        g_rl_4d31ec = (int32_t)t;
        if (++g_rl_4d31e8 >= 12) g_rl_4d31e8 = 0;
        mark_full();
    }
    sprite(g_rl_4d31e8 + 33, x, y);
    return 0;
}

/* Port of FUN_00420de4 (545 B) — the MP setup page's sides: each side's banner (realm +0xa, sprite + 13), name,
 * army value (string 28 + DAT_0051fbbc for the human's side, else DAT_0051fad0), "Attacker" (string 23) and each
 * side's difficulty word (string 42 + aux +0x28), the attacker's shield (realm +0xa of DAT_0053ef5c, sprite + 18). */
int rl_420de4(void)
{
    sprite(PL8(1, 0x57bf0au) + 13, 230, 115);
    text_line(PNAME(1), 275, 110, FONT, 63);
    int v = HUMAN == 1 ? g_rl_51fbbc : g_rl_51fad0;
    g_gfx_line_x_acc = 0;
    wrap_line(11, 28, 275, 130, FONT, 63);
    label(v, 32, SK(0x4d4384u), g_gfx_line_x_acc + 275, 130, FONT, 63);
    sprite(PL8(2, 0x57bf0au) + 13, 230, 338);
    text_line(PNAME(2), 275, 333, FONT, 63);
    v = HUMAN == 2 ? g_rl_51fbbc : g_rl_51fad0;
    g_gfx_line_x_acc = 0;
    wrap_line(11, 28, 275, 353, FONT, 63);
    label(v, 32, SK(0x4d4388u), g_gfx_line_x_acc + 275, 353, FONT, 63);
    wrap_line_c(11, 23, 300, 430, 120, FONT, 63);
    wrap_line_c(11, PA8(1, 0x553d78u) + 42, 264, 156, 129, FONT, 63);
    wrap_line_c(11, PA8(2, 0x553d78u) + 42, 264, 379, 129, FONT, 63);
    sprite(PL8(ATTACKER, 0x57bf0au) + 18, 276, 431);
    return 0;
}

/* Port of FUN_00421005 (556 B) — the battle list: six rows from the scroll top DAT_0053f64c (while below the
 * count DAT_0053f0d4), the selected row DAT_0053e9a8 inverted (102 on 63), each row's name (set 3: the .skr
 * record's name 0x42ab82(n, 0); else battles.eng string 3 (n + 10 set) 0x42ab15); then the scroll bar at (604,
 * 203): 60 px split into the part above (percent of the top), the thumb (6 rows) and the part below. */
int rl_421005(void)
{
    g_gfx_no_shadow = 1;
    for (int i = 0; i < 6; i++) {
        int n = i + (int32_t)s_slot_0053f64c;
        if ((int32_t)s_slot_0053f0d4 <= n) continue;
        int c;
        if ((int32_t)s_slot_0053e9a8 == i) {
            c = 102;
            gfx_line_fill_rect(468, 16 * i + 185, 133, 16, 63);
        } else {
            c = 63;
            gfx_line_fill_rect(468, 16 * i + 185, 133, 16, 102);
        }
        if (MODE == 3) rl_42ab82(n, 0);
        else rl_42ab15(3 * (n + 10 * MODE));
        text_line_c(g_rl_5c9270, 468, 16 * i + 186, 133, FONT, c);
    }
    int top = (int32_t)s_slot_0053f64c, cnt = (int32_t)s_slot_0053f0d4;
    int p_top = engine_math_percent_div(top, cnt);
    int p_thumb = engine_math_percent_div(6, cnt);
    int p_rest = engine_math_percent_div(cnt - top - 6, cnt);
    int h_top = engine_math_percent_of(60, p_top);
    int h_thumb = engine_math_percent_of(60, p_thumb);
    int h_rest = engine_math_percent_of(60, p_rest);
    (void)h_thumb;
    int h_mid = 60 - h_top - h_rest;   /* the thumb takes what the two ends leave */
    if (h_top != 0) gfx_line_fill_rect(604, 203, 20, h_top, 102);
    if (h_mid != 0) gfx_line_fill_rect(604, h_top + 203, 20, h_mid, 63);
    if (h_rest != 0) gfx_line_fill_rect(604, 60 - h_rest + 203, 20, h_rest, 102);
    return 0;
}

/* Port of FUN_00421231 (222 B) — the chosen battle's title and description: string 1 of the battle (the .skr
 * record +13, or battles.eng string 3 n + 1) centered in the tall font, upper-cased (DAT_0058fe2c), and string 2
 * (record +42, or 3 n + 2) word-wrapped at (40, 152) 370 wide. */
int rl_421231(void)
{
    g_gfx_uppercase_highlight = 1;
    if (MODE == 3) rl_42ab82(BATTLE, 1);
    else rl_42ab15(3 * (BATTLE + 10 * MODE) + 1);
    text_line_c(g_rl_5c9270, 37, 120, 377, FONT_TALL, 63);
    g_gfx_uppercase_highlight = 0;
    if (MODE == 3) rl_42ab82(BATTLE, 2);
    else rl_42ab15(3 * (BATTLE + 10 * MODE) + 2);
    wrap_buf(g_rl_5c9270, 40, 152, 370, FONT, 63);
    return 0;
}

/* Port of FUN_0042130f (508 B) — the custom-armies page's numbers: the gold left DAT_00521c90 (string 28), the
 * human's army size (territory DAT_00553efc's total, "unit" / "units" strings 33 / 27), and per troop type (7, 11 on
 * a siege: the engines) its count (32 when non-zero, else 63) and its cost DAT_004d4b98[i]. Its two arguments are
 * unused (the only caller passes 6, 162). */
int rl_42130f(int a0, int a1)
{
    (void)a0; (void)a1;
    g_gfx_line_x_acc = 0;
    wrap_line(11, 28, 60, 432, FONT, 63);
    label(g_rl_521c90, 32, SK(0x4d438cu), g_gfx_line_x_acc + 60, 432, FONT, 63);
    g_gfx_line_x_acc = 0;
    label(TOTAL(SIDE_A), 32, SK(0x4d4390u), 43, 453, FONT, 63);
    if (TOTAL(SIDE_A) == 1) wrap_line(11, 33, g_gfx_line_x_acc + 53, 453, FONT, 63);
    else wrap_line(11, 27, g_gfx_line_x_acc + 53, 453, FONT, 63);
    int n = MODE == 2 ? 11 : 7;
    for (int i = 0; i < n; i++) {
        int v = TROOP(SIDE_A, i);
        label_c(v, 32, SK(0x4d4394u), 166, 30 * i + 108, 20, FONT, v != 0 ? 32 : 63);
        label_c(COST(i), 32, SK(0x4d4398u), 55, 30 * i + 108, 20, FONT, 63);
    }
    return 0;
}

/* Port of FUN_0042051c (276 B) — the MP skirmish setup page (title page 12 in MP): the three buttons (strings 37,
 * 39 / 38 on the page state DAT_0056899c, 36), the redraw latches (DAT_0057c948, DAT_0055ce94, the below-status
 * redraw DAT_0053e9ac), the battlefield minimap strip 0x4bc1d1(80), the battle list, the set buttons, the globe;
 * then the custom armies (page state set: 0x42130f + 0x420de4) or the title / description + sides. */
int rl_42051c(int a0)
{
    (void)a0;
    g_gfx_no_shadow = 1;
    wrap_line_c(11, 37, 461, 440, 56, FONT, 63);
    if (g_rl_56899c != 0) wrap_line_c(11, 39, 519, 440, 56, FONT, 63);
    else wrap_line_c(11, 38, 519, 440, 56, FONT, 63);
    wrap_line_c(11, 36, 577, 440, 56, FONT, 63);
    g_rl_57c948 = 1;
    g_rl_55ce94 = 1;
    DIRTY->below_status_redraw = 1;   /* DAT_0053e9ac */
    bf_minimap_scroll(80);
    rl_421005();
    rl_4207c3();
    rl_420d40();
    if (g_rl_56899c != 0) {
        rl_42130f(6, 162);
        rl_420de4();
    } else {
        rl_421231();
        rl_4209c1();
    }
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_0042150b (508 B) — the .skr file page (title page 13): "Go" (string 36; in MP 63 when the opponent
 * is ready DAT_005533e0[computer] == 1, 249 at 2, absent otherwise), strings 37 / 38, the redraw latches, the
 * minimap strip, the battle list, the title / description, the set buttons; a 28x18 chrome panel at (96, 100), its
 * title (group 40 string 6, tall font), a frame, string 8, the file list from the top (DAT_004e65dc = 999,
 * 0x414e06) and the seal at (504, 356). */
int rl_42150b(void)
{
    g_gfx_no_shadow = 1;
    if (g_sim_mp_active == 0 || *ready(CPU) == 1) wrap_line_c(11, 36, 461, 440, 56, FONT, 63);
    else if (*ready(CPU) == 2) wrap_line_c(11, 36, 461, 440, 56, FONT, 249);
    wrap_line_c(11, 37, 519, 440, 56, FONT, 63);
    wrap_line_c(11, 38, 577, 440, 56, FONT, 63);
    g_rl_57c948 = 1;
    g_rl_55ce94 = 1;
    DIRTY->below_status_redraw = 1;   /* DAT_0053e9ac */
    bf_minimap_scroll(80);
    rl_421005();
    rl_421231();
    rl_4207c3();
    int x = 96, y = 68;   /* retail keeps them in two locals */
    panel_box(x, y + 32, 28, 18);
    mark_full();
    wrap_line_c(40, 6, x, y + 64, 448, FONT_TALL, 63);
    gfx_line_rect(x + 24, y + 104, 352, 165, 63);
    wrap_line_c(40, 8, x, y + 288, 448, FONT, 63);
    g_rl_4e65dc = 999;
    rl_414e06(g_rl_4e65dc);
    gfx_sprite_seal_draw(SYSTEM, x + 408, y + 288, 0);
    g_gfx_no_shadow = 0;
    return 0;
}

/* ================================================================ the Battle Master screens (0x2e / 0x2f) */

/* Port of FUN_00421707 (1538 B) — the Battle Master ratings (screen 0x2e): the battle sheets reloaded (0x498dcb),
 * score1.256 / score1.pl8 as palette + background, the seal, the title (group 37 string 0, tall); per side (the
 * human's on top: banner, name + string 4, the rating DAT_0053e9b4 / DAT_0057c95c in the tall font) the troop
 * types' counts before the battle (territory + 6; 32 when some survived, territory + 4), the losses (before - after)
 * and the enemy's (the other side's losses, 249). */
int rl_421707(void)
{
    rl_498dcb();
    read_file_at(SK(0x4d439cu), g_rl_4ea8a0, 0x300u, 0);   /* "score1.256" */
    load_background(SK(0x4d43a8u), 480);                    /* "score1.pl8" */
    set_palette(g_rl_4ea8a0);
    mark_full();
    /* territories 5 / 6 = the armies after the battle, 7 / 8 = before it (0x42c64f's copies of 1..4) */
    int a_now = SIDE_A + 4, b_now = SIDE_B + 4, a_was = SIDE_A + 6, b_was = SIDE_B + 6;
    gfx_sprite_seal_draw(SYSTEM, 516, 390, 0);
    wrap_line_c(37, 0, 96, 68, 446, FONT_TALL, 63);
    sprite(PL8(HUMAN, 0x57bf0au) + 8, 112, 100);
    g_gfx_line_x_acc = 0;
    text_line(PNAME(HUMAN), 216, 110, FONT, 63);
    wrap_line(37, 4, g_gfx_line_x_acc + 216, 110, FONT, 63);
    label(g_rl_53e9b4, 32, SK(0x4d43b4u), g_gfx_line_x_acc + 236, 105, FONT_TALL, 63);
    wrap_line(37, 2, 104, 200, FONT, 63);
    wrap_line(37, 3, 104, 220, FONT, 63);
    for (int i = 0; i < 7; i++) {
        int c = TROOP(a_now, i) != 0 ? 32 : 63;
        label_c(TROOP(a_was, i), 32, SK(0x4d43b8u), 50 * i + 173, 180, 60, FONT, c);
        label_c(TROOP(a_was, i) - TROOP(a_now, i), 32, SK(0x4d43bcu), 50 * i + 173, 200, 60, FONT, c);
        label_c(TROOP(b_was, i) - TROOP(b_now, i), 32, SK(0x4d43c0u), 50 * i + 173, 220, 60, FONT, 249);
    }
    sprite(PL8(CPU, 0x57bf0au) + 8, 112, 260);
    g_gfx_line_x_acc = 0;
    text_line(PNAME(CPU), 216, 270, FONT, 63);
    wrap_line(37, 4, g_gfx_line_x_acc + 216, 270, FONT, 63);
    label(g_rl_57c95c, 32, SK(0x4d43c4u), g_gfx_line_x_acc + 236, 265, FONT_TALL, 63);
    wrap_line(37, 2, 104, 360, FONT, 63);
    wrap_line(37, 3, 104, 380, FONT, 63);
    for (int j = 0; j < 7; j++) {
        int c = TROOP(b_now, j) != 0 ? 32 : 63;
        label_c(TROOP(b_was, j), 32, SK(0x4d43c8u), 50 * j + 173, 340, 60, FONT, c);
        label_c(TROOP(b_was, j) - TROOP(b_now, j), 32, SK(0x4d43ccu), 50 * j + 173, 360, 60, FONT, c);
        label_c(TROOP(a_was, j) - TROOP(a_now, j), 32, SK(0x4d43d0u), 50 * j + 173, 380, 60, FONT, 249);
    }
    return 0;
}

/* Port of FUN_00421d09 (523 B) — the skirmish masters (screen 0x2f): score2.256 / score2.pl8, a frame, the title
 * (group 37 string 5, tall, upper-cased), the 10 masters (name + rating DAT_0051fd00); a second frame, the human's
 * rank title (group 38 string DAT_0053e9e0) and his 5 best ratings (DAT_0051fd28) under his name. */
int rl_421d09(void)
{
    read_file_at(SK(0x4d43d4u), g_rl_4ea8a0, 0x300u, 0);   /* "score2.256" */
    load_background(SK(0x4d43e0u), 480);                    /* "score2.pl8" */
    set_palette(g_rl_4ea8a0);
    mark_full();
    g_gfx_uppercase_highlight = 1;
    g_gfx_no_shadow = 1;
    gfx_line_rect(130, 10, 380, 270, 63);
    wrap_line_c(37, 5, 0, 22, 640, FONT_TALL, 63);
    g_gfx_uppercase_highlight = 0;
    for (int i = 0; i < 10; i++) {
        text_line(g_rl_51fbc0 + 32 * i, 200, 20 * i + 70, FONT, 63);
        label(AT(int32_t, g_rl_51fd00, 0x51fd00u, 0x51fd00u + 4 * i), 32, SK(0x4d43ecu), 400, 20 * i + 70, FONT, 63);
    }
    gfx_line_rect(130, 300, 380, 170, 63);
    g_gfx_uppercase_highlight = 1;
    wrap_line_c(38, g_rl_53e9e0, 0, 312, 640, FONT_TALL, 63);
    g_gfx_uppercase_highlight = 0;
    for (int j = 0; j < 5; j++) {
        text_line(PNAME(HUMAN), 200, 20 * j + 360, FONT, 63);
        label(g_rl_51fd28[j], 32, SK(0x4d43f0u), 400, 20 * j + 360, FONT, 63);
    }
    g_gfx_no_shadow = 0;
    return 0;
}

/* ================================================================ the battlefield debug inspectors */

/* Port of FUN_00425314 (371 B) — the unit inspector (debug DAT_0055402c, on the battlefield): a 160x384 block at
 * (480, 24), "FIGURE" + the unit DAT_00553e68, its 432-byte record (DAT_0055324c = its address, DAT_005681e8 = 432)
 * as hex, ten bytes a row, in the small font. */
int rl_425314(void)
{
    if (g_rl_55402c == 0 || (int32_t)s_slot_0057c8fc != 2) return 0;
    g_gfx_glyph_w = 10;
    g_gfx_glyph_height_short = 384;
    gfx_sprite_fill_block(480, 24, 34);
    mark_box(480, 24, 10, 24, 1);
    g_gfx_no_shadow = 1;
    g_gfx_line_x_acc = 0;
    text_line(STR(g_rl_4d46ec, 0x4d46ecu, 0x4d46ecu), 484, 26, FONT_SMALL, 63);   /* "FIGURE" */
    label(g_bf_553e68, 32, STR(g_rl_4d46ec, 0x4d46ecu, 0x4d46f4u), g_gfx_line_x_acc + 484, 26, FONT_SMALL, 63);
    g_rl_55324c = (int32_t)(uintptr_t)&BU8(g_bf_553e68, BF_UNIT_VA);
    g_rl_5681e8 = 432;
    for (int i = 0; i < g_rl_5681e8; i++)
        label_hex(PTR(g_rl_55324c)[i], 32, STR(g_rl_4d46ec, 0x4d46ecu, 0x4d46f8u), 16 * (i % 10) + 480,
                  8 * (i / 10) + 36, FONT_SMALL, 63);
    return 0;
}

/* Port of FUN_00425487 (437 B) — the group inspector: as 0x425314 for the unit's group (unit +0x178): "GROUP" + the
 * group + DAT_0057c8dc, the 52-byte group record as hex. */
int rl_425487(void)
{
    if (g_rl_55402c == 0 || (int32_t)s_slot_0057c8fc != 2) return 0;
    g_gfx_glyph_w = 10;
    g_gfx_glyph_height_short = 384;
    gfx_sprite_fill_block(480, 24, 34);
    mark_box(480, 24, 10, 24, 1);
    int g = BS16(g_bf_553e68, 0x5545f8u);
    g_gfx_no_shadow = 1;
    g_gfx_line_x_acc = 0;
    text_line(STR(g_rl_4d46ec, 0x4d46ecu, 0x4d46fcu), 484, 26, FONT_SMALL, 63);   /* "GROUP" */
    label(g, 32, STR(g_rl_4d46ec, 0x4d46ecu, 0x4d4704u), g_gfx_line_x_acc + 484, 26, FONT_SMALL, 63);
    label(g_bf_57c8dc, 40, STR(g_rl_4d46ec, 0x4d46ecu, 0x4d4708u), g_gfx_line_x_acc + 484, 26, FONT_SMALL, 63);
    g_rl_55324c = (int32_t)(uintptr_t)&BG8(g, BF_GROUP_VA);
    g_rl_5681e8 = 52;
    for (int i = 0; i < g_rl_5681e8; i++)
        label_hex(PTR(g_rl_55324c)[i], 32, STR(g_rl_4d46ec, 0x4d46ecu, 0x4d470cu), 16 * (i % 10) + 480,
                  8 * (i / 10) + 36, FONT_SMALL, 63);
    return 0;
}

/* ================================================================ the battle list and the troop table */

/* Port of FUN_0042aa90 (133 B) — battles.eng into DAT_0051fd50 (its size from 0x4af70c; absent → 0), every control
 * byte (< 0x20) made a NUL. Returns the size. */
int rl_42aa90(void)
{
    int n = file_size(SKF(0x4d4cd0u));
    if (n == 0) return 0;
    read_file_at(SKF(0x4d4cdcu), g_rl_51fd50, (uint32_t)n, 0);
    for (int i = 0; i < n; i++)
        if (AT(uint8_t, g_rl_51fd50, 0x51fd50u, 0x51fd50u + i) < 0x20) AT(uint8_t, g_rl_51fd50, 0x51fd50u, 0x51fd50u + i) = 0;
    return n;
}

/* Port of FUN_0042ab15 (109 B) — the walker DAT_005c9270 to string a1 of battles.eng (a NUL after a non-NUL ends a
 * string), past leading control bytes. Returns the walker. */
int rl_42ab15(int a1)
{
    uint8_t *p = g_rl_51fd50;
    while (a1 > 0) {
        /* the first step reads the byte below the text: DAT_0051fd4f, the best ratings' last byte */
        uint8_t prev = p == g_rl_51fd50 ? ((const uint8_t *)g_rl_51fd28)[39] : p[-1];
        if (p[0] == 0 && prev != 0) --a1;
        ++p;
    }
    while (*p < 0x20) ++p;
    g_rl_5c9270 = p;
    return (int)(uintptr_t)p;
}

/* Port of FUN_0042ab82 (138 B) — the walker DAT_005c9270 into the .skr battle record a1 (183 B at DAT_00521ca0):
 * a2 0 its name, 1 (+13) its title, 2 (+42) its description; another a2 leaves it. Returns the walker (else the
 * caller's EAX: no caller reads it). */
int rl_42ab82(int a1, int a2)
{
    static const int off[3] = { 0, 13, 42 };
    if (a2 < 0 || a2 > 2) return 0;
    uint8_t *p = &AT(uint8_t, g_rl_521ca0, 0x521ca0u, 0x521ca0u + 183 * a1 + off[a2]);
    g_rl_5c9270 = p;
    return (int)(uintptr_t)p;
}

/* Port of FUN_0042ac0c (2932 B) — the troop table DAT_00516ac0 for the 35 built-in battles: zeroed; troops.eng in MP,
 * else troops2.eng when the human attacks (DAT_0053ef5c == DAT_0057c8cc), troops3.eng otherwise, into DAT_00519a10
 * (absent → stop). From the first '*', every decimal number in turn: a battle's first is its advantage (clamped
 * 0..10, DAT_0051fae0[battle]), then 2 x 5 x 11 counts [tier][side][type] (side varies fastest); 35 battles. Then
 * the types 7..10 (engines) capped at 9; every tier := tier 2 (the file's "normal"); and the first 7 types get the
 * difficulty spread: tiers 0 / 1 + 16 % / 8 %, tiers 3 / 4 - 8 % / 16 % of tier 2. */
int rl_42ac0c(void)
{
    for (int b = 0; b < 35; b++)
        for (int k = 0; k < 11; k++)
            for (int s = 0; s < 2; s++)
                for (int t = 0; t < 5; t++)
                    TR16(b, s, t, k) = 0;
    const char *name;
    if (g_sim_mp_active != 0) name = SKF(0x4d4cf4u);          /* troops.eng */
    else if (ATTACKER == HUMAN) name = SKF(0x4d4d0cu);         /* troops2.eng */
    else name = SKF(0x4d4d24u);                                /* troops3.eng */
    int n = file_size(g_sim_mp_active != 0 ? SKF(0x4d4ce8u) : ATTACKER == HUMAN ? SKF(0x4d4d00u) : SKF(0x4d4d18u));
    if (n == 0) return 0;
    read_file_at(name, g_rl_519a10, (uint32_t)n, 0);
    const uint8_t *f = g_rl_519a10;
    int k = 0;
    while (k < n && f[k] != '*') k++;
    int head = 0, tier = 0, side = 0, type = 0, b = 0;
    while (k < n) {
        if (f[k] < '0' || f[k] > '9') { k++; continue; }
        int v = strutil_decimal_atoi((const char *)&f[k]);
        if (type != 0 || side != 0 || tier != 0 || head != 0) {
            TR16(b, side, tier, type) = (int16_t)v;
            if (++type >= 11) {
                type = 0;
                if (++side >= 2) {
                    side = 0;
                    if (++tier >= 5) {
                        tier = 0;
                        ++b;
                    }
                }
            }
            head = 0;
        } else {
            if (v < 0) v = 0;
            if (v > 10) v = 10;
            g_rl_51fae0[b] = v;
            head = 1;
        }
        if (b >= 35) break;
        while (f[k] >= '0' && f[k] <= '9') k++;
    }
    for (int bb = 0; bb < 35; bb++)
        for (int t = 7; t < 11; t++)
            for (int s = 0; s < 2; s++)
                for (int tt = 0; tt < 5; tt++)
                    if (TR16(bb, s, tt, t) > 9) TR16(bb, s, tt, t) = 9;
    for (int bb = 0; bb < 35; bb++)
        for (int t = 0; t < 11; t++)
            for (int s = 0; s < 2; s++) {
                int16_t v = TR16(bb, s, 2, t);
                TR16(bb, s, 0, t) = v;
                TR16(bb, s, 1, t) = v;
                TR16(bb, s, 3, t) = v;
                TR16(bb, s, 4, t) = v;
            }
    for (int bb = 0; bb < 35; bb++)
        for (int t = 0; t < 7; t++) {
            int a8 = engine_math_percent_of(TR16(bb, 0, 2, t), 8);
            int b8 = engine_math_percent_of(TR16(bb, 1, 2, t), 8);
            int a16 = engine_math_percent_of(TR16(bb, 0, 2, t), 16);
            int b16 = engine_math_percent_of(TR16(bb, 1, 2, t), 16);
            TR16(bb, 0, 0, t) = (int16_t)(TR16(bb, 0, 0, t) + a16);
            TR16(bb, 1, 0, t) = (int16_t)(TR16(bb, 1, 0, t) + b16);
            TR16(bb, 0, 1, t) = (int16_t)(TR16(bb, 0, 1, t) + a8);
            TR16(bb, 1, 1, t) = (int16_t)(TR16(bb, 1, 1, t) + b8);
            TR16(bb, 0, 3, t) = (int16_t)(TR16(bb, 0, 3, t) - a8);
            TR16(bb, 1, 3, t) = (int16_t)(TR16(bb, 1, 3, t) - b8);
            TR16(bb, 0, 4, t) = (int16_t)(TR16(bb, 0, 4, t) - a16);
            TR16(bb, 1, 4, t) = (int16_t)(TR16(bb, 1, 4, t) - b16);
        }
    return 0;
}

/* ================================================================ entering / leaving the skirmish */

/* Port of FUN_0042b780 (119 B) — Skirmish! from the title: the popup cleared (0x4360f2), the MP chat buffers
 * cleared (0x43f24b), screen 0x1f, the title page DAT_005530f0 = 12 (11 in MP), the skirmish set up (0x42b7f7), the
 * human ready (DAT_005533e0[human] = 1) and announced (0x43eda0(85, 0)), the armies restored from the backup
 * (0x42c5fe) and valued (0x42bf46); redraw 1. */
int rl_42b780(void)
{
    rl_4360f2();
    rl_43f24b();
    engine_set_current_screen(0x1f);
    g_rl_5530f0 = g_sim_mp_active != 0 ? 11 : 12;
    rl_42b7f7();
    *ready(HUMAN) = 1;
    rl_43eda0(85, 0);
    rl_42c5fe();
    rl_42bf46();
    engine_set_demand_redraw(1);
    return 0;
}

/* Port of FUN_0042b7f7 (290 B) — set the skirmish up: the two sides (DAT_005681ec = 1, DAT_005681d8 = 2; the
 * computer DAT_0056d5cc is the side the human is not), the sides' records (0x42ba40), both names trimmed, the two
 * army territories cleared (0x46ead0), DAT_00568210 / 18 = 1 / 2, battles.eng and the troop table read, the system
 * sheet 1, skirmish mode (DAT_0057a0f0), sub-state 2, the battle sheets loaded (0x498dcb), the battlefield built
 * (0x42b9c4), the armies valued (0x42bf46), the records again (0x42ba40). */
int rl_42b7f7(void)
{
    s_slot_005681ec = 1;
    s_slot_005681d8 = 2;
    if (HUMAN == (int32_t)s_slot_005681ec) s_slot_0056d5cc = s_slot_005681d8;
    else s_slot_0056d5cc = s_slot_005681ec;
    rl_42ba40();
    strutil_trim_trailing_spaces(PNAME(HUMAN));
    strutil_trim_trailing_spaces(PNAME(CPU));
    memset(TERR(1), 0, 0x1a4);   /* 0x46ead0 */
    memset(TERR(2), 0, 0x1a4);
    s_slot_00568210 = 1;
    s_slot_00568218 = 2;
    rl_42aa90();
    rl_42ac0c();
    load_system_sheet(1);
    s_slot_0057a0f0 = 1;
    s_slot_0057c8fc = 2;
    s_slot_00568964 = 1;
    s_slot_0055ce70 = 32;
    s_slot_005678a0 = 33;
    rl_498dcb();
    rl_42b9c4();
    rl_42bf46();
    rl_42ba40();
    return 0;
}

/* Port of FUN_0042b919 (171 B) — the skirmish setup's defaults: both difficulties 2, the human ready, the field
 * set 0, skirmish mode, DAT_0053e8fc = 0, the human attacks (DAT_0053ef5c = 1), DAT_00522f60 / DAT_0057c910 = 0, the
 * first battle, the SP page, 10 battles in the list from the top (DAT_0056d5c0 = 28); the .skr list (0x42d389) and
 * the chosen .skr's checksum (0x42d857). */
int rl_42b919(void)
{
    PA8(1, 0x553d78u) = 2;
    PA8(2, 0x553d78u) = 2;
    *ready(HUMAN) = 1;
    s_slot_0053e91c = 0;
    s_slot_0057a0f0 = 1;
    s_slot_0053e8fc = 0;
    s_slot_0053ef5c = 1;
    s_slot_00522f60 = 0;
    s_slot_0057c910 = 0;
    s_slot_0056d590 = 0;
    g_rl_56899c = 0;
    s_slot_0053f0d4 = 10;
    s_slot_0056d5c0 = 28;
    s_slot_0053f64c = 0;
    s_slot_0053e9a8 = 0;
    rl_42d389();
    rl_42d857();
    return 0;
}

/* Port of FUN_0042b9c4 (124 B) — build the chosen battle's battlefield: the field set 0x47aaa3(battle), the unequal
 * set 0x47aaa3(battle + 10), a siege 0x47c4ba(battle), a user battle 0x47b8b2(battle). */
int rl_42b9c4(void)
{
    switch (MODE) {
    case 0: return bf_47aaa3(BATTLE);
    case 1: return bf_47aaa3(BATTLE + 10);
    case 2: return bf_47c4ba(BATTLE);
    default: return bf_47b8b2(BATTLE);
    }
}

/* Port of FUN_0042ba40 (1286 B) — the two sides' records: in MP both realms human (+5 = 1, +4 = 1, aux +0x26 = 0);
 * in SP the computer (realm +5 = 0, +4 = 1) takes the battle's lord DAT_004d4bc8[...] (+7; its name from lords2
 * string group 7 into aux +4, 16 B; +0x29 / +0x2a = 1) and his banner DAT_004d4ca8 (the second choice when the
 * human wears that one), its colours (DAT_004dc1ce pair → +8 / +9). Then the two army territories (DAT_00568210 /
 * 18: +8 = 1, owner +0, +1 the owner's human flag, +2 the banner) — the attacker DAT_0053ef5c's army in the first —
 * the human's / computer's territory (DAT_00553efc / 34) and both banners (DAT_00568934 / 38). */
int rl_42ba40(void)
{
    if (g_sim_mp_active != 0) {
        PL8(HUMAN, 0x57bf05u) = 1;
        PL8(HUMAN, 0x57bf04u) = 1;
        PA8(HUMAN, 0x553d76u) = 0;
        PL8(CPU, 0x57bf05u) = 1;
        PL8(CPU, 0x57bf04u) = 1;
        PA8(CPU, 0x553d76u) = 0;
    } else {
        int lord = AT(int32_t, g_rl_4d4bc8, 0x4d4bc8u, 0x4d4bc8u + 4 * (BATTLE + BASE(MODE)));
        PL8(CPU, 0x57bf07u) = (uint8_t)lord;
        PL8(CPU, 0x57bf05u) = 0;
        PL8(CPU, 0x57bf04u) = 1;
        localization_seek_skip(loc_in(), 7, PL8(CPU, 0x57bf07u));   /* 0x4018d7 */
        loc_out();
        strutil_bounded_copy(PNAME(CPU), g_rl_5c9270, 16);
        PL8(CPU, 0x57bf2au) = 1;
        PL8(CPU, 0x57bf29u) = PL8(CPU, 0x57bf2au);
        int32_t ban = AT(int32_t, g_rl_4d4bc8, 0x4d4bc8u, 0x4d4ca8u + 8 * lord);
        if ((int32_t)PL8(HUMAN, 0x57bf0au) == ban)
            PL8(CPU, 0x57bf0au) = AT(uint8_t, g_rl_4d4bc8, 0x4d4bc8u, 0x4d4cacu + 8 * lord);
        else
            PL8(CPU, 0x57bf0au) = (uint8_t)ban;
        PL8(CPU, 0x57bf08u) = AT(uint8_t, g_rl_4dc1ce, 0x4dc1ceu, 0x4dc1ceu + 2 * PL8(CPU, 0x57bf0au));
        PL8(CPU, 0x57bf09u) = AT(uint8_t, g_rl_4dc1ce, 0x4dc1ceu, 0x4dc1cfu + 2 * PL8(CPU, 0x57bf0au));
    }
    int t1 = (int32_t)s_slot_00568210, t2 = (int32_t)s_slot_00568218;
    TE8(t1, 0x52f0b8u) = 1;
    TE8(t2, 0x52f0b8u) = 1;
    if (ATTACKER == HUMAN) {
        TE8(t1, 0x52f0b0u) = (uint8_t)CPU;
        TE8(t2, 0x52f0b0u) = (uint8_t)HUMAN;
        TE8(t1, 0x52f0b1u) = PL8(CPU, 0x57bf05u);
        TE8(t2, 0x52f0b1u) = PL8(HUMAN, 0x57bf05u);
        TE8(t1, 0x52f0b2u) = PL8(CPU, 0x57bf0au);
        TE8(t2, 0x52f0b2u) = PL8(HUMAN, 0x57bf0au);
        s_slot_00553efc = 2;
        s_slot_00553f34 = 1;
    } else {
        TE8(t1, 0x52f0b0u) = (uint8_t)HUMAN;
        TE8(t2, 0x52f0b0u) = (uint8_t)CPU;
        TE8(t1, 0x52f0b1u) = PL8(HUMAN, 0x57bf05u);
        TE8(t2, 0x52f0b1u) = PL8(CPU, 0x57bf05u);
        TE8(t1, 0x52f0b2u) = PL8(HUMAN, 0x57bf0au);
        TE8(t2, 0x52f0b2u) = PL8(CPU, 0x57bf0au);
        s_slot_00553efc = 1;
        s_slot_00553f34 = 2;
    }
    s_slot_00568934 = TE8(t1, 0x52f0b2u);
    s_slot_00568938 = TE8(t2, 0x52f0b2u);
    return 84 * t2;
}

/* Port of FUN_0042bf46 (935 B) — the two armies from the troop table: the battle (DAT_0056d590 + the set's base),
 * each side's row by its difficulty (aux +0x28) and by whether it attacks (side 1 = the attacker's), the 11 counts
 * into its territory (DAT_00553efc / 34) and the first 7's total (+0x168); then both armies' values (sum of count x
 * cost DAT_004d4b98: DAT_0051fbbc the human's, DAT_0051fad0 the computer's) and the gold left DAT_00521c90 = 0. */
int rl_42bf46(void)
{
    int b = BATTLE + BASE(MODE);
    int s = ATTACKER == HUMAN;
    int d = PA8(HUMAN, 0x553d78u);
    for (int i = 0; i < 11; i++) TROOP(SIDE_A, i) = TR16(b, s, d, i);
    int sum = 0;
    for (int j = 0; j < 7; j++) sum += TR16(b, s, d, j);
    TOTAL(SIDE_A) = sum;
    s = ATTACKER == CPU;
    d = PA8(CPU, 0x553d78u);
    for (int k = 0; k < 11; k++) TROOP(SIDE_B, k) = TR16(b, s, d, k);
    sum = 0;
    for (int m = 0; m < 7; m++) sum += TR16(b, s, d, m);
    TOTAL(SIDE_B) = sum;
    int va = 0, vb = 0;
    g_rl_521c90 = 0;
    for (int n = 0; n < 11; n++) va += COST(n) * TROOP(SIDE_A, n);
    for (int n = 0; n < 11; n++) vb += COST(n) * TROOP(SIDE_B, n);
    g_rl_51fbbc = va;
    g_rl_51fad0 = vb;
    return COST(10) * TROOP(SIDE_B, 10);
}

/* Port of FUN_0042c476 (108 B) — the gold left on the custom-armies page: DAT_00521c90 = the human's army value
 * DAT_0051fbbc - his current army's cost. Returns it. */
int rl_42c476(void)
{
    int v = 0;
    for (int i = 0; i < 11; i++) v += COST(i) * TROOP(SIDE_A, i);
    g_rl_521c90 = g_rl_51fbbc - v;
    return g_rl_521c90;
}

/* Port of FUN_0042c4e2 (203 B) — can the human buy one more of type a1: the first 7 when the gold left covers its
 * cost; the engines (7..10) only on a siege — the battering ram (10) only for the attacker, the others only for
 * the defender — fewer than 9 and affordable. */
int rl_42c4e2(int a1)
{
    if (a1 >= 7) {
        if (MODE != 2) return 0;
        if (ATTACKER == HUMAN && a1 != 10) return 0;
        if (ATTACKER == CPU && a1 == 10) return 0;
        if (TROOP(SIDE_A, a1) >= 9) return 0;
    }
    return COST(a1) <= g_rl_521c90 ? 1 : 0;
}

/* Port of FUN_0042c5ad (81 B) — back up the two armies: territory 1 → 3, 2 → 4 (420 B each). */
int rl_42c5ad(void)
{
    strutil_bounded_copy(TERR(3), TERR(1), 420);
    strutil_bounded_copy(TERR(4), TERR(2), 420);
    return 0;
}

/* Port of FUN_0042c5fe (81 B) — restore the two armies: territory 3 → 1, 4 → 2. */
int rl_42c5fe(void)
{
    strutil_bounded_copy(TERR(1), TERR(3), 420);
    strutil_bounded_copy(TERR(2), TERR(4), 420);
    return 0;
}

/* ================================================================ the Battle Master ratings */

/* Port of FUN_0042c64f (1531 B) — rate the skirmish just fought: the armies' values before (territory + 2) and after,
 * the losses (DAT_005533b4 the computer's units lost, DAT_00544024 the human's), each side's share of the value
 * destroyed and of its units kept; the ratings DAT_0053e9b4 (human) / DAT_0057c95c (computer) = 600 x the share
 * destroyed + 300 x the share kept, + 500 for the winner — a surrender (DAT_0056d5c8, by DAT_005656f8) rates the
 * side that gave up 0 and gives the other no bonus; a siege's / lost battle's winner by DAT_00553f3c and the
 * attacker, else the side whose army survived (the human's total > 0). Then score.dat read (0x42d216), the
 * masters updated (0x42cc4a), written back (0x42d2dc), and territories 1..4 copied to 5..8. */
int rl_42c64f(void)
{
    int a0 = SIDE_A + 2, b0 = SIDE_B + 2;
    int va0 = 0, va = 0;
    for (int i = 0; i < 7; i++) va0 += COST(i) * TROOP(a0, i);
    for (int j = 0; j < 7; j++) va += COST(j) * TROOP(SIDE_A, j);
    int a_lost = va0 - va;
    int na0 = TOTAL(a0), na = TOTAL(SIDE_A);
    int vb0 = 0, vb = 0;
    for (int k = 0; k < 7; k++) vb0 += COST(k) * TROOP(b0, k);
    for (int m = 0; m < 7; m++) vb += COST(m) * TROOP(SIDE_B, m);
    int nb0 = TOTAL(b0), nb = TOTAL(SIDE_B);
    g_rl_5533b4 = nb0 - nb;
    g_rl_544024 = na0 - na;
    int b_lost = vb0 - vb;
    int p_a = engine_math_percent_div(b_lost, b_lost + a_lost);   /* the human's share of the value destroyed */
    int p_b = engine_math_percent_div(a_lost, b_lost + a_lost);
    int k_a = engine_math_percent_div(na, na0);                   /* the share of his units kept */
    int k_b = engine_math_percent_div(nb, nb0);
    /* retail computes an advantage-weighted bonus here (DAT_0051fae0 of the battle, 300 x (100 - 10 adv) % …) and
     * then overwrites both with 500: dead code (pure calls) */
    const int bonus = 500;
    if (g_combat_render_active != 0 && HUMAN == g_combat_owner_5656f8) {
        g_rl_53e9b4 = 0;
        g_rl_57c95c = engine_math_percent_of(600, p_b);
        g_rl_57c95c += engine_math_percent_of(300, k_b);
    } else if (g_combat_render_active != 0 && CPU == g_combat_owner_5656f8) {
        g_rl_57c95c = 0;
        g_rl_53e9b4 = engine_math_percent_of(600, p_a);
        g_rl_53e9b4 += engine_math_percent_of(300, k_a);
    } else if (s_slot_00553f3c != 0 && ATTACKER == HUMAN) {
        g_rl_53e9b4 = engine_math_percent_of(600, p_a);
        g_rl_57c95c = engine_math_percent_of(600, p_b);
        g_rl_57c95c += engine_math_percent_of(300, k_b);
        g_rl_57c95c += bonus;
    } else if (s_slot_00553f3c != 0 && ATTACKER != HUMAN) {
        g_rl_57c95c = engine_math_percent_of(600, p_b);
        g_rl_53e9b4 = engine_math_percent_of(600, p_a);
        g_rl_53e9b4 += engine_math_percent_of(300, k_a);
        g_rl_53e9b4 += bonus;
    } else if (na > 0) {
        g_rl_57c95c = engine_math_percent_of(600, p_b);
        g_rl_53e9b4 = engine_math_percent_of(600, p_a);
        g_rl_53e9b4 += engine_math_percent_of(300, k_a);
        g_rl_53e9b4 += bonus;
    } else {
        g_rl_53e9b4 = engine_math_percent_of(600, p_a);
        g_rl_57c95c = engine_math_percent_of(600, p_b);
        g_rl_57c95c += engine_math_percent_of(300, k_b);
        g_rl_57c95c += bonus;
    }
    rl_42d216();
    rl_42cc4a();
    rl_42d2dc();
    strutil_bounded_copy(TERR(5), TERR(1), 420);
    strutil_bounded_copy(TERR(6), TERR(2), 420);
    strutil_bounded_copy(TERR(7), TERR(3), 420);
    strutil_bounded_copy(TERR(8), TERR(4), 420);
    return 0;
}

/* Port of FUN_0042cc4a (1327 B) — the masters table after a rating: the games played DAT_00568988 + 1, the ratings'
 * sum DAT_00568214, the average DAT_0055ce8c, the rank DAT_0053e9e0 = average / 100 + a bonus for experience (0 up
 * to 20 games, 1 to 50, 2 to 150, 3 past), at least 0, at most a cap by games played (0 up to 2 ... 11 past 100).
 * The higher of the two new ratings already in the table under that side's name → done. Else the human's rating
 * into his best ten DAT_0051fd28 (the masters screen shows five) and both ratings into the masters (rating DAT_0051fd00 + name byte_51FBC0, the
 * rows below shifted down). */
int rl_42cc4a(void)
{
#define MR(i)  AT(int32_t, g_rl_51fd00, 0x51fd00u, 0x51fd00u + 4 * (i))
#define MN(i)  (g_rl_51fbc0 + 32 * (i))
    ++g_rl_568988;
    g_rl_568214 += g_rl_53e9b4;
    g_rl_55ce8c = g_rl_568214 / g_rl_568988;
    g_rl_53e9e0 = g_rl_55ce8c / 100;
    int g = g_rl_568988, cap, bonus;
    if (g <= 2) { cap = 0; bonus = 0; }
    else if (g <= 4) { cap = 1; bonus = 0; }
    else if (g <= 6) { cap = 2; bonus = 0; }
    else if (g <= 8) { cap = 3; bonus = 0; }
    else if (g <= 10) { cap = 4; bonus = 0; }
    else if (g <= 20) { cap = 5; bonus = 0; }
    else if (g <= 30) { cap = 6; bonus = 1; }
    else if (g <= 40) { cap = 7; bonus = 1; }
    else if (g <= 50) { cap = 8; bonus = 1; }
    else if (g <= 75) { cap = 9; bonus = 1; }
    else if (g <= 100) { cap = 10; bonus = 2; }
    else if (g <= 150) { cap = 11; bonus = 2; }
    else { cap = 11; bonus = 3; }
    g_rl_53e9e0 += bonus;
    if (g_rl_53e9e0 < 0) g_rl_53e9e0 = 0;
    if (cap < g_rl_53e9e0) g_rl_53e9e0 = cap;
    if (g_rl_57c95c < g_rl_53e9b4) {
        for (int i = 0; i < 10; i++)
            if (MR(i) == g_rl_53e9b4 && strcmp(PNAME(HUMAN), (const char *)MN(i)) == 0) return 0;
    } else {
        for (int j = 0; j < 10; j++)
            if (MR(j) == g_rl_57c95c && strcmp(PNAME(CPU), (const char *)MN(j)) == 0) return 0;
    }
    for (int k = 0; k < 10; k++)
        if (g_rl_51fd28[k] < g_rl_53e9b4) {
            for (int m = 9; m > k; --m) g_rl_51fd28[m] = g_rl_51fd28[m - 1];
            g_rl_51fd28[k] = g_rl_53e9b4;
            break;
        }
    for (int n = 0; n < 10; n++)
        if (MR(n) < g_rl_53e9b4) {
            for (int ii = 9; ii > n; --ii) MR(ii) = MR(ii - 1);
            MR(n) = g_rl_53e9b4;
            for (int jj = 9; jj > n; --jj) strutil_bounded_copy(MN(jj), MN(jj - 1), 32);
            strutil_bounded_copy(MN(n), PNAME(HUMAN), 32);
            break;
        }
    for (int kk = 0; kk < 10; kk++)
        if (MR(kk) < g_rl_57c95c) {
            for (int mm = 9; mm > kk; --mm) MR(mm) = MR(mm - 1);
            MR(kk) = g_rl_57c95c;
            for (int nn = 9; nn > kk; --nn) strutil_bounded_copy(MN(nn), MN(nn - 1), 32);
            strutil_bounded_copy(MN(kk), PNAME(CPU), 32);
            break;
        }
    return 0;
#undef MR
#undef MN
}

/* score.dat's first field is 0xa0 B from DAT_0051fd00 — the masters' ratings (0x28), the human's best ten DAT_0051fd28
 * (0x28) and the first 0x50 B of battles.eng's text DAT_0051fd50: three port symbols (contiguous under rhost, not in
 * the standalone), so the field goes through a buffer */
static void span_put(const uint8_t *buf, int n)
{
    for (int i = 0; i < n && i < 0xa0; i++) {
        if (i < 0x28) g_rl_51fd00[i] = buf[i];
        else if (i < 0x50) ((uint8_t *)g_rl_51fd28)[i - 0x28] = buf[i];
        else g_rl_51fd50[i - 0x50] = buf[i];
    }
}
static void span_get(uint8_t *buf)
{
    memcpy(buf, g_rl_51fd00, 0x28);
    memcpy(buf + 0x28, g_rl_51fd28, 0x28);
    memcpy(buf + 0x50, g_rl_51fd50, 0x50);
}

/* Port of FUN_0042d216 (198 B) — read score.dat (base directory): the masters' ratings (0xa0 B), names (0x140 B),
 * the rank, games played and ratings' sum; absent → those three 0. */
int rl_42d216(void)
{
    chdir_base();
    int fd = rl_plat_open(SKF(0x4d4d30u), 0x8000, 0);
    if (fd == -1) {
        g_rl_53e9e0 = 0;
        g_rl_568988 = 0;
        g_rl_568214 = 0;
        return fd;
    }
    uint8_t buf[0xa0];   /* 0xa0 B at DAT_0051fd00: the ratings, the best ten and 80 B of battles.eng's text (quirk) */
    int r = rl_plat_read(fd, buf, 0xa0u);
    span_put(buf, r);
    rl_plat_read(fd, g_rl_51fbc0, 0x140u);
    rl_plat_read(fd, &g_rl_53e9e0, 4u);
    rl_plat_read(fd, &g_rl_568988, 4u);
    rl_plat_read(fd, &g_rl_568214, 4u);
    return rl_plat_close(fd);
}

/* Port of FUN_0042d2dc (173 B) — write score.dat (base directory; O_BINARY | O_TRUNC | O_CREAT | O_WRONLY, 0600):
 * the same five fields. */
int rl_42d2dc(void)
{
    chdir_base();
    int fd = rl_plat_open(SKF(0x4d4d3cu), 0x8301, 0x180);
    if (fd == -1) return fd;
    uint8_t buf[0xa0];
    span_get(buf);
    rl_plat_write(fd, buf, 0xa0u);
    rl_plat_write(fd, g_rl_51fbc0, 0x140u);
    rl_plat_write(fd, &g_rl_53e9e0, 4u);
    rl_plat_write(fd, &g_rl_568988, 4u);
    rl_plat_write(fd, &g_rl_568214, 4u);
    return rl_plat_close(fd);
}

/* ================================================================ the user battles (.skr) */

/* Port of FUN_0042d389 (130 B) — list the *.skr files (base directory; 0x4aecf0, extensions kept): their count
 * DAT_005653e8; no .skr → DAT_0055cd34 = 1; the chosen one (lords2.inf +0x41c) still there → 0, else the first
 * listed is chosen and DAT_0055cd34 = 1. */
int rl_42d389(void)
{
    chdir_base();
    rl_4aecf0((int)(uintptr_t)SKF(0x4d4d48u), 1);
    g_rl_5653e8 = g_rl_4eb25c;
    g_rl_55cd34 = 0;
    if (g_rl_5653e8 != 0) {
        if (rl_4af767((int)(uintptr_t)SKR_NAME) != 0) return 0;
        strcpy(SKR_NAME, (const char *)g_rl_4e8790);
    }
    g_rl_55cd34 = 1;
    return 0;
}

/* Port of FUN_0042d4aa (860 B) — load the chosen .skr: its 20 battles' armies (20 x 2 x 11 dwords at 0) into
 * DAT_0051f3f0 and their names / titles / descriptions (20 x 183 B at 0x6e0) into DAT_00521ca0; the troop table's
 * battles 35..54 from the armies: tier 2 the count c, tiers 1 / 0 c + c/3 / c + 2c/3, tiers 3 / 4 2c/3 / c/3. */
int rl_42d4aa(void)
{
    chdir_base();
    read_file_at(SKR_NAME, g_rl_51f3f0, 0x6e0u, 0);
    read_file_at(SKR_NAME, g_rl_521ca0, 0xe4cu, 0x6e0u);
    for (int i = 0; i < 20; i++)
        for (int j = 0; j < 11; j++) {
            int b = i + 35;
            for (int s = 0; s < 2; s++) {
                int32_t c = AT(int32_t, g_rl_51f3f0, 0x51f3f0u, 0x51f3f0u + 88 * i + 44 * s + 4 * j);
                TR16(b, s, 4, j) = (int16_t)(c / 3);
                TR16(b, s, 3, j) = (int16_t)(2 * c / 3);
                TR16(b, s, 2, j) = (int16_t)c;
                TR16(b, s, 1, j) = (int16_t)(c / 3 + c);
                TR16(b, s, 0, j) = (int16_t)(2 * c / 3 + c);
            }
        }
    return 0;
}

/* Port of FUN_0042d857 (179 B) — the chosen .skr's checksum DAT_005440bc (the sum of its bytes; base directory): the
 * panel sheet grown to 220000 B first when smaller (Size DAT_004eabcc, signed), the file read into the bound sheet
 * DAT_004eabec. An absent file leaves 0. (EAX: nobody reads it.) */
int rl_42d857(void)
{
    int k = 0;
    chdir_base();
    g_rl_5440bc = 0;
    int n = file_size(SKR_NAME);
    if (n == 0) return 0;
    if (AT(int32_t, g_rl_4eabcc, 0x4eabccu, 0x4eabccu) < 220000) load_panel_sheet(220000);
    read_file_at(SKR_NAME, g_gfx_current_blob, (uint32_t)n, 0);
    while (n-- > 0) g_rl_5440bc += g_gfx_current_blob[k++];
    return 0;
}
