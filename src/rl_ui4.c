/* rl_ui4.c — a retail-layout port, partition 'ui4' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 *
 * Round 3: every function here had an old port (strutil.c, text_input.c, font_metrics.c, localization.c,
 * engine_math.c, engine_path.c, engine_camera.c, engine_menu_input.c, engine.c, config.c, audio.c, gfx.c …) that was
 * not on retail's layout or not linked in rhost. Route (a): rl_<va> forwards to a linked old port that runs on
 * retail's own state (its result / EAX modelled here); route (b): a fresh port on retail's globals, the old port a
 * reading aid. Findings: docs/findings/rl-ui4.md. */
#include "rl.h"
#include "rl_ui4_globals.h"
#include "engine_bf_input_globals.h"   /* g_bf_5af8f0: the default font */
#include "rl_render_globals.h"   /* the localization blob + walker, the editor buffer / insert flag, the char tables */
#include "gfx_globals.h"         /* the measurers' cache, the editor cursor DAT_005bb4a8, the line state */
#include "engine_path.h"         /* g_path_dist_dx / _dy (DAT_00591560 / DAT_0058fea0: a mirror) */
#include "engine_math.h"
#include "engine.h"              /* engine_rng_single_in_t, g_engine_state, the screen / redraw bridges */
#include "rl_io_globals.h"       /* the DirectDraw object DAT_004eabd0, the save flag DAT_005c9274, the CD letter */
#include "engine_territory.h"    /* the bound dirty state (DAT_0053e9ac: a mirror) */
#include "engine_army_ai.h"      /* the ranking-dirty flag DAT_0056d6a0 (a mirror) */
#include "engine_map_scene.h"    /* engine_map_scene_tick_frame (0x4050c0) */
#include "engine_mapview_globals.h"   /* g_mv_rate_tier1 (DAT_0057d3c8) */
#include "gfx_panel.h"
#include "gfx_sprite_render.h"
#include "gfx_wrap.h"
#include "localization.h"
#include "rl_io_plat.h"
#include "strutil.h"

#include <string.h>
#include <stdlib.h>

#include "engine_camera.h"
#include "engine_map_query.h"    /* g_cell_cursor_col / _row (DAT_00569570 / 78: a mirror) */

/* other partitions' globals (rl_glue_globals.h conflicts with rl_render_globals.h in one unit: declared as there) */
extern int32_t g_rl_591558;          /* DAT_00591558 (rl_ui2_globals.h): the router's fourth parameter */
extern int32_t g_rl_55cea0[8385];    /* DAT_0055cea0 (rl_ui2_globals.h): the strategic cell grid, 65 a row */
extern int32_t g_rl_52afd4;          /* DAT_0052afd4 (rl_glue_globals.h): the map's columns */
extern int32_t g_rl_5533b0;          /* DAT_005533b0 (rl_glue_globals.h): the cursor is over the map */
extern int32_t g_rl_568974;          /* DAT_00568974 (rl_glue_globals.h): the hovered tile */
extern int32_t g_rl_57d338;          /* DAT_0057d338 (rl_glue_globals.h): the tile index step */

/* ---------------------------------------------------------------- retail's tables and blobs, by address */
#define PTRV(p)     ((int)(uintptr_t)(p))                       /* a pointer as retail's int argument / EAX */
#define BYTES(v)    ((uint8_t *)(uintptr_t)(uint32_t)(v))       /* a retail pointer held in an int */
#define METRICS     ((const uint8_t *)g_rl_4d71d0)              /* DAT_004d71d0: the measurers' char -> glyph table */
#define FONT        ((const uint8_t *)g_bf_5af8f0)              /* &DAT_005af8f0: the default 16 px font */
#define LOC_BLOB    g_rl_591580                                 /* &DAT_00591580: l2_eng.dat */
#define LOC_END     (g_rl_591580 + 0x1d4c0)                     /* &DAT_005aea40: the blob's end (retail's bound) */
/* the text editor (DAT_005cd550 buffer, DAT_005bb4a8 cursor, DAT_005c9280 insert flag) */
#define ED_BUF      g_rl_5cd550
#define ED_CUR      g_gfx_first_pos_target                      /* DAT_005bb4a8 */
#define ED_LEN      g_rl_58fe94                                 /* DAT_0058fe94: the length */
#define ED_MAX      AT(int32_t, g_rl_5cd4f4, 0x5cd4f4u, 0x5cd4f4u)   /* MaxCharCount DAT_005cd4f4 */
#define ED_PXMAX    g_rl_58fd68                                 /* DAT_0058fd68: the pixel width bound */
#define ED_CLAMP    g_rl_5cd408                                 /* DAT_005cd408: 0 / 1 (cursor past the end) / 2 (full) */
#define ED_OVER     g_rl_57d3b0                                 /* DAT_0057d3b0: the measured width reached the bound */
#define ED_FOLD     g_rl_5c928c                                 /* DAT_005c928c: fold capitals, no punctuation */
#define ED_ON       g_rl_5aeb78                                 /* DAT_005aeb78: the editor accepts input */
#define FONT_TALL   ((const uint8_t *)g_rl_5b2fa0)              /* &DAT_005b2fa0: the tall 24 px font */
#define CHROME      ((const uint8_t *)g_rl_57d3d0)              /* &DAT_0057d3d0: the chrome sheet */
#define GLYPHS      ((const uint8_t *)g_rl_4d71d0 + 0x20)       /* DAT_004d71f0: the renderers' char -> glyph table */
#define MISC        BYTES(g_bf_5530c8)                          /* DAT_005530c8: the misc / icon sheet */
#define STR(blk, blkva, va) ((const char *)&AT(char, blk, blkva, va))   /* a retail string */
#define STRV(blk, blkva, va) PTRV(&AT(char, blk, blkva, va))            /* … as retail's int argument */
#define SCREEN      ((unsigned)engine_current_screen())         /* byte_4EAC50 (movzx) */
#define DIRTY       engine_territory_dirty_state()              /* 0x4527a6's six globals (a mirror: rh_hooks_ai.c) */
/* the input bytes: pressed / repeat / held / released (DAT_004eafb4 / DAT_004eabc5 / DAT_004e65cc / DAT_004e65d8) */
#define IN_PRESS    g_bf_4eafb4
#define IN_REPEAT   g_bf_4eabc5
#define IN_HELD     g_bf_4e65cc
#define IN_UP       g_bf_4e65d8
#define CUR_X       g_bf_4e65fc                                 /* DAT_004e65fc: the cursor */
#define CUR_Y       g_bf_4e65f4
#define PARAM(k)    AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154c + 4 * (k))   /* DAT_0059154c..: the router's params */

/* ---------------------------------------------------------------- the gfx layer's walker (as rl_render.c) */
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

/* ---------------------------------------------------------------- seams (callees outside the partition) */
/* ported, not on retail's layout / not linked in rhost (a context struct, a file-static) — TODO(rl-link) */
static int panel_box(int x, int y, int cols, int rows)   /* TODO(rl-link): 0x409397 (the chrome panel + fill: a static in several modules) */
{ return (int)engine_retail_call(0x409397u, 4, (uint32_t)x, (uint32_t)y, (uint32_t)cols, (uint32_t)rows); }
static int year_number(int v, int x, int y, int mode)   /* TODO(rl-link): county_year_number 0x41a900 */
{ return (int)engine_retail_call(0x41a900u, 4, (uint32_t)v, (uint32_t)x, (uint32_t)y, (uint32_t)mode); }
static int territory_panel(int a) { return (int)engine_retail_call(0x40f7d3u, 1, (uint32_t)a); }   /* TODO(rl-link): strat_draw_territory_panel 0x40f7d3 (engine_hud_panel: an env) */
static int endturn_panel(int a) { return (int)engine_retail_call(0x41a734u, 1, (uint32_t)a); }     /* TODO(rl-link): engine_hud_panel_render_endturn 0x41a734 */
static int minimap_render(int x, int y) { return (int)engine_retail_call(0x410aa9u, 2, (uint32_t)x, (uint32_t)y); }   /* TODO(rl-link): engine_minimap_render 0x410aa9 */
static int title_scene(void) { return (int)engine_retail_call(0x41e7e1u, 0); }          /* TODO(rl-link): engine_title_render_scene 0x41e7e1 */
static int treasury_render(void) { return (int)engine_retail_call(0x416925u, 0); }      /* TODO(rl-link): engine_treasury_render 0x416925 */
static int noble_faces_render(void) { return (int)engine_retail_call(0x416cf3u, 0); }   /* TODO(rl-link): engine_noble_faces_render 0x416cf3 */
static int armory_render(int a) { return (int)engine_retail_call(0x417ea7u, 1, (uint32_t)a); }   /* TODO(rl-link): engine_armory_render 0x417ea7 */
static int armory_overlay(void) { return (int)engine_retail_call(0x418653u, 0); }       /* TODO(rl-link): engine_armory_overlay_render 0x418653 */
static int trade_render(void) { return (int)engine_retail_call(0x416308u, 0); }         /* TODO(rl-link): engine_trade_render 0x416308 */
static int county_panel(uint32_t va) { return (int)engine_retail_call(va, 0); }         /* TODO(rl-link): the county panels 0x4110b1 / 0x41152f / 0x4116fb / 0x411b72 */
static int workforce_render(int a) { return (int)engine_retail_call(0x412143u, 1, (uint32_t)a); }   /* TODO(rl-link): engine_workforce_hud_render 0x412143 */
static int terrain_panel(void) { return (int)engine_retail_call(0x41b032u, 0); }        /* TODO(rl-link): 0x41b032 (engine_terrain_info) */
/* unported — TODO(rl-extern) */
static int screen_41e5d0(void) { return (int)engine_retail_call(0x41e5d0u, 0); }   /* TODO(rl-extern): 0x41e5d0 (screen 0x45) */
static int screen_425a6a(void) { return (int)engine_retail_call(0x425a6au, 0); }   /* TODO(rl-extern): 0x425a6a (screen 0x44) */
static int quit_4b2234(void) { return (int)engine_retail_call(0x4b2234u, 0); }     /* TODO(rl-extern): 0x4b2234 (the fatal-load exit) */
static int gfx_restore(void) { return (int)engine_retail_call(0x4b2730u, 0); }     /* TODO(rl-extern): gfx_restore 0x4b2730 */
static int lost_4b26b8(void) { return (int)engine_retail_call(0x4b26b8u, 0); }     /* TODO(rl-extern): 0x4b26b8 (the surfaces cannot be restored) */
static int save_49792e(void) { return (int)engine_retail_call(0x49792eu, 0); }     /* TODO(rl-extern): 0x49792e (the MP save pump) */

/* ================================================================ strutil (0x401000..0x401666) */

/* Port of FUN_00401000 (133 B) — bounded substring search: the first offset < max where needle matches (its first
 * byte, then 0x4010da over strlen(needle)); 0 for none — the same as a match at 0. Route (a): strutil.c. */
int rl_401000(int hay, int needle, int max)
{
    return strutil_find_substr((const char *)BYTES(hay), (const char *)BYTES(needle), max);
}

/* Port of FUN_00401085 (85 B) — bounded char search: offset + 1 of the first c in max bytes, 0 for none. (a). */
int rl_401085(int str, int c, int max)
{
    return strutil_find_char((const char *)BYTES(str), (char)c, max);
}

/* Port of FUN_004010da (92 B) — bounded memcmp: 0 when the n bytes match, else 1 + the first mismatch's offset.
 * Route (b): strutil.c inlines it (a file-static). */
int rl_4010da(int a, int b, int n)
{
    for (int i = 0; i < n; i++)
        if (BYTES(a)[i] != BYTES(b)[i]) return i + 1;
    return 0;
}

/* Port of FUN_00401136 (64 B) — bounded copy (SRC, DST, n): no terminator; EAX = n. (a): strutil_bounded_copy takes
 * (dst, src, n). */
int rl_401136(int src, int dst, int n)
{
    strutil_bounded_copy(BYTES(dst), BYTES(src), n);
    return n;
}

/* Port of FUN_004011b0 (58 B) — one char to lower case ('A'..'Z' + 0x20, any other byte unchanged). Route (b): the
 * old port (strutil_to_lower_char) returns 0 outside 'A'..'Z' — and the host tests pin that. */
int rl_4011b0(int c)
{
    uint8_t v = (uint8_t)c;
    if (v >= 0x41 && v <= 0x5a) v = (uint8_t)(v + 0x20);
    return v;
}

/* Port of FUN_004011ea (92 B) — a string to upper case in place; EAX = its NUL. (a). */
int rl_4011ea(int s)
{
    strutil_to_upper_inplace(BYTES(s));
    return PTRV(BYTES(s) + strlen((const char *)BYTES(s)));
}

/* Port of FUN_00401246 (92 B) — a string to lower case in place; EAX = its NUL. (a). */
int rl_401246(int s)
{
    strutil_to_lower_inplace(BYTES(s));
    return PTRV(BYTES(s) + strlen((const char *)BYTES(s)));
}

/* Port of FUN_004012a2 (97 B) — cut a string at its first byte outside 0x20..0xa6; EAX = where it stopped. (a). */
int rl_4012a2(int s)
{
    strutil_strip_ctrl(BYTES(s));
    return PTRV(BYTES(s) + strlen((const char *)BYTES(s)));
}

/* Port of FUN_00401303 (48 B) — shift [p, end) one byte left (deleting *p), NUL at the end; EAX = the final p.
 * (a); the pointer compare is unsigned (jbe). */
int rl_401303(int p, int end)
{
    strutil_shift_left(BYTES(p), BYTES(end));
    return (uint32_t)end > (uint32_t)p ? end : p;
}

/* Port of FUN_00401333 (49 B) — shift (start, end] one byte right, NUL at end + 1; EAX = the final end. (a). */
int rl_401333(int start, int end)
{
    strutil_shift_right(BYTES(start), BYTES(end));
    return (uint32_t)start < (uint32_t)end ? start : end;
}

/* Port of FUN_004013bc (89 B) — trim trailing spaces in place; EAX = the index of the last byte kept (-1 for an
 * all-space string; retail then reads s[-1], and walks on if that is a space too). Route (b): the old port's walk is
 * the same, but EAX is the index. */
int rl_4013bc(int s)
{
    uint8_t *p = BYTES(s);
    int i = 0;
    while (p[i] != 0) i++;
    int j = i - 1;
    while (p[j] == 0x20) { p[j] = 0; j--; }
    return j;
}

/* Port of FUN_00401666 (141 B) — the leading decimal digits as an int, each digit times DAT_004d2030[k] (1 .. 10^7:
 * 8 entries). Route (b): retail indexes the table past its 8 entries for a 9+ digit run (the old port caps at 8,
 * and the host tests pin that). */
int rl_401666(int s)
{
    const uint8_t *p = BYTES(s);
    int n = 0, v = 0;
    while (*p >= 0x30 && *p <= 0x39) { n++; p++; }
    p = BYTES(s);
    while (n != 0) {
        n--;
        v += AT(int32_t, g_rl_4d2030, 0x4d2030u, 0x4d2030u + 4 * n) * (*p++ - 0x30);
    }
    return v;
}

/* ================================================================ font metrics (the measurers' cache DAT_005c9288 /
 * DAT_0058fd88: the last glyph index and its descriptor offset 16 i + 8) */

/* Port of FUN_004015b9 (173 B) — one char's pixel width: 0 for NUL, 4 for a space, else the font's descriptor width
 * (a 16-bit little-endian at +8 of glyph DAT_004d71d0[c] - 1) + 1; 0 for a char without a glyph. Route (b): the old
 * port (font_metrics.c) keeps the cache in a state struct. */
int rl_4015b9(int c, int font)
{
    uint8_t ch = (uint8_t)c;
    if (ch == 0) return 0;
    if (ch == 0x20) return 4;
    g_gfx_last_glyph_index = METRICS[ch];
    if (g_gfx_last_glyph_index == 0) return 0;
    g_gfx_last_glyph_index--;
    g_gfx_last_glyph_offset = 16 * g_gfx_last_glyph_index + 8;
    const uint8_t *d = BYTES(font) + g_gfx_last_glyph_offset;
    return d[0] + (d[1] << 8) + 1;
}

/* Port of FUN_004014f0 (201 B) — a string's pixel width (at most 10000 chars; a space 4), as 0x4015b9 inlined.
 * Route (b) (font_metrics.c: a state struct). */
int rl_4014f0(int buf, int font)
{
    const uint8_t *p = BYTES(buf);
    int w = 0;
    for (int k = 10000; k > 0; k--) {
        uint8_t ch = *p++;
        if (ch == 0) return w;
        if (ch == 0x20) { w += 4; continue; }
        g_gfx_last_glyph_index = METRICS[ch];
        if (g_gfx_last_glyph_index != 0) {
            g_gfx_last_glyph_index--;
            g_gfx_last_glyph_offset = 16 * g_gfx_last_glyph_index + 8;
            const uint8_t *d = BYTES(font) + g_gfx_last_glyph_offset;
            w += d[0] + (d[1] << 8) + 1;
        }
    }
    return w;
}

/* Port of FUN_00401f8f (122 B) — the editor buffer's pixel width (0x4015b9 per char); DAT_0057d3b0 = width >=
 * DAT_0058fd68. Route (b) (font_metrics.c / text_input.c: state structs). */
int rl_401f8f(int font)
{
    int w = 0, k = 0;
    for (;;) {
        uint8_t ch = ED_BUF[k++];
        if (ch == 0) break;
        w += rl_4015b9(ch, font);
    }
    ED_OVER = 0;
    if (w >= ED_PXMAX) ED_OVER = 1;
    return w;
}

/* Port of FUN_004036f9 (245 B) — the pixel width of the next word (at most 1999 bytes): leading spaces count 4 and
 * leading '$' nothing; the word ends at NUL, or at a space / '$' after a printable; control bytes are skipped.
 * DAT_005c9a7c counts the bytes consumed (the wrap renderers copy that many). Route (b) (font_metrics.c). */
int rl_4036f9(int buf, int font)
{
    const uint8_t *p = BYTES(buf);
    int w = 0, k = 0, seen = 0;
    g_gfx_word_chars = 0;
    while (++k < 2000) {
        uint8_t ch = *p++;
        if (ch == 0) return w;
        if (ch == 0x20) {
            if (seen) return w;
            w += 4;
        } else if (ch == 0x24) {
            if (seen) return w;
        } else if (ch >= 0x20) {
            w += rl_4015b9(ch, font);
            seen = 1;
        }
        g_gfx_word_chars++;
    }
    return w;
}

/* ================================================================ localization (the blob DAT_00591580: a 24-bit
 * offset table at +8, NUL-separated strings; the walker DAT_005c9270) */

/* Port of FUN_0040187a (93 B) — string index i's offset into the blob: bytes +8 / +9 / +10 of entry 4 i. Route (b):
 * the old port (localization.c) takes the blob as an argument; this is the same read on retail's. */
int rl_40187a(int i)
{
    const uint8_t *e = &AT(uint8_t, LOC_BLOB, 0x591580u, 0x591588 + 4 * i);
    return (e[2] << 16) + (e[1] << 8) + e[0];
}

/* Port of FUN_004017bf (187 B) — copy n bytes of localized string `idx`, `skip` strings in, to dst: a string ends at
 * a NUL after a printable (>= 0x20) byte; control bytes before the text are skipped. A LOCAL walk (DAT_005c9270 is
 * left alone). An offset past the blob's end copies nothing: EAX = the end (&DAT_005aea40); else EAX = n. Route (b)
 * (localization.c: a state struct whose walker it moves — rl-render.md). */
int rl_4017bf(int dst, int idx, int skip, int n)
{
    const uint8_t *w = LOC_BLOB + rl_40187a(idx);
    if ((uintptr_t)LOC_END < (uintptr_t)w) return PTRV(LOC_END);
    while (skip > 0) {
        if (w[0] == 0 && w[-1] >= 0x20) skip--;
        w++;
    }
    while (*w < 0x20) w++;
    for (int i = 0; i < n; i++) BYTES(dst)[i] = w[i];
    return n;
}

/* Port of FUN_004018d7 (173 B) — point the walker DAT_005c9270 at localized string `idx`, `skip` strings in (a string
 * ends at a NUL after a printable or after another NUL), past the control bytes; EAX = the walker (the blob's end
 * &DAT_005aea40 for an offset past it, the walker then left there). Route (b) (localization.c: a state struct). */
int rl_4018d7(int idx, int skip)
{
    g_rl_5c9270 = LOC_BLOB + rl_40187a(idx);
    if ((uintptr_t)LOC_END < (uintptr_t)g_rl_5c9270) return PTRV(LOC_END);
    while (skip > 0) {
        if (g_rl_5c9270[0] == 0 && (g_rl_5c9270[-1] >= 0x20 || g_rl_5c9270[-1] == 0)) skip--;
        g_rl_5c9270++;
    }
    while (*g_rl_5c9270 < 0x20) g_rl_5c9270++;
    return PTRV(g_rl_5c9270);
}

/* ================================================================ the text editor (0x401984..0x40210c).
 * Route (b) throughout: text_input.c / main.c keep the editor in a state struct (text_input_state_t). */

/* Port of FUN_00401984 (156 B) — clamp the editor cursor: measure (0x401f8f, the default font); past the length + 1
 * → there and DAT_005cd408 = 1; at or past MaxCharCount → there and 2; the width at the bound → 2; below 0 → 0.
 * EAX = MaxCharCount. */
int rl_401984(void)
{
    ED_CLAMP = 0;
    rl_401f8f(PTRV(FONT));
    if (ED_LEN + 1 <= ED_CUR) { ED_CUR = ED_LEN + 1; ED_CLAMP = 1; }
    if (ED_CUR >= ED_MAX) { ED_CUR = ED_MAX; ED_CLAMP = 2; }
    if (ED_OVER != 0) ED_CLAMP = 2;
    if (ED_CUR < 0) ED_CUR = 0;
    return ED_MAX;
}

/* Port of FUN_00401d26 (162 B) — insert / overwrite char c at the cursor (the editor on, not full): insert mode
 * shifts the tail right (0x401333) while the length < MaxCharCount. EAX: AL residue (no caller reads it). */
int rl_401d26(int c)
{
    if (ED_ON == 0) return 0;
    rl_401984();
    if (ED_CLAMP == 2) return 0;
    if (g_rl_5c9280 != 0) {
        if (ED_MAX > ED_LEN) {
            rl_401333(PTRV(&ED_BUF[ED_CUR]), PTRV(&ED_BUF[ED_LEN + 1]));
            ED_BUF[ED_CUR] = (uint8_t)c;
            ED_CUR++;
        }
    } else {
        ED_BUF[ED_CUR] = (uint8_t)c;
        ED_CUR++;
    }
    return 0;
}

/* Port of FUN_00401a20 (543 B) — a typed char through the editor's filter: space, '-', digits, lower case, the
 * accented 0x80..0x9a / 0xa0..0xa7 / 0xe1 always; ',' '.' '?' '!' only when DAT_005c928c is clear; capitals
 * folded to lower case (0x4011b0) when it is set. Then clamp; EAX = MaxCharCount. */
int rl_401a20(int c)
{
    uint8_t ch = (uint8_t)c;
    int ins = 0;
    if (ch == 0x20 || ch == 0x2d || (ch >= 0x30 && ch <= 0x39) || (ch >= 0x61 && ch <= 0x7a))
        ins = 1;
    else if ((ch == 0x2c || ch == 0x2e || ch == 0x3f || ch == 0x21) && ED_FOLD == 0)
        ins = 1;
    else if (ch >= 0x41 && ch <= 0x5a) {
        if (ED_FOLD != 0) ch = (uint8_t)rl_4011b0(ch);
        ins = 1;
    } else if ((ch >= 0x80 && ch <= 0x9a) || (ch >= 0xa0 && ch <= 0xa7) || ch == 0xe1)
        ins = 1;
    if (ins) rl_401d26(ch);
    return rl_401984();
}

/* Port of FUN_00401dc8 (64 B) — delete the char at the cursor (the editor on): shift left (0x401303) up to the
 * length, then clamp. EAX: clamp's, else the caller's. */
int rl_401dc8(void)
{
    if (ED_ON == 0) return 0;
    rl_401303(PTRV(&ED_BUF[ED_CUR]), PTRV(&ED_BUF[ED_LEN]));
    return rl_401984();
}

/* Port of FUN_00401c70 (35 B) — backspace: the cursor > 0 → back one, delete. */
int rl_401c70(void)
{
    if (ED_CUR > 0) { ED_CUR--; return rl_401dc8(); }
    return 0;
}

/* Port of FUN_00401c93 (16 B) — Delete: 0x401dc8. */
int rl_401c93(void) { return rl_401dc8(); }

/* Port of FUN_00401ca3 (25 B) — toggle the insert flag DAT_005c9280; EAX = the new value. */
int rl_401ca3(void)
{
    g_rl_5c9280 ^= 1;
    return g_rl_5c9280;
}

/* Port of FUN_00401cbc (30 B) — cursor left (not below 0). */
int rl_401cbc(void)
{
    if (ED_CUR > 0) ED_CUR--;
    return 0;
}

/* Port of FUN_00401cda (34 B) — cursor right (not past the length); EAX = the length. */
int rl_401cda(void)
{
    if (ED_CUR < ED_LEN) ED_CUR++;
    return ED_LEN;
}

/* Port of FUN_00401cfc (21 B) — cursor home. */
int rl_401cfc(void)
{
    ED_CUR = 0;
    return 0;
}

/* Port of FUN_00401d11 (21 B) — cursor end; EAX = the length. */
int rl_401d11(void)
{
    ED_CUR = ED_LEN;
    return ED_LEN;
}

/* Port of FUN_00401f49 (70 B) — the length DAT_0058fe94 = the buffer's NUL index; EAX = that index. */
int rl_401f49(void)
{
    int k = 0;
    ED_LEN = 0;
    while (ED_BUF[k++] != 0) ED_LEN++;
    return k - 1;
}

/* Port of FUN_00402009 (165 B) — open the editor on string `init`: clear the 2000-byte buffer, copy it in, length
 * = its length, MaxCharCount = max, the pixel bound = px, DAT_005c928c = DAT_005cd408 = flag; the overflow flag, the
 * caret's y DAT_0058fd54 and the cursor 0. EAX = flag. */
int rl_402009(int init, int max, int px, int flag)
{
    int i;
    for (i = 1999; i >= 0; i--) ED_BUF[i] = 0;
    for (const uint8_t *s = BYTES(init); *s != 0; s++) ED_BUF[++i] = *s;
    ED_FOLD = flag;
    ED_LEN = i + 1;
    ED_MAX = max;
    ED_PXMAX = px;
    ED_CLAMP = flag;
    ED_OVER = 0;
    g_gfx_first_pos_y = 0;
    ED_CUR = 0;
    return flag;
}

/* Port of FUN_004020ae (94 B) — clear the editor: the buffer zeroed, length 1 (sic), overflow / caret y / cursor 0.
 * EAX = 0 (the clear loop's last index). */
int rl_4020ae(void)
{
    for (int i = 1999; i >= 0; i--) ED_BUF[i] = 0;
    ED_LEN = 1;
    ED_OVER = 0;
    g_gfx_first_pos_y = 0;
    ED_CUR = 0;
    return 0;
}

/* Port of FUN_0040210c (90 B) — copy the editor's text out (at most n chars) + NUL; EAX = the NUL's address. */
int rl_40210c(int dst, int n)
{
    uint8_t *d = BYTES(dst);
    for (int i = 0; ED_BUF[i] != 0; i++) {
        int k = n--;
        if (k <= 0) break;
        *d++ = ED_BUF[i];
    }
    *d = 0;
    return PTRV(d);
}

/* ================================================================ the line setup, cadence, rng, math */

/* Port of FUN_004037ee (347 B) — Bresenham setup: |dx| / |dy| DAT_005aeb40 / 44, the endpoints ordered along the
 * dominant axis (start DAT_005aeb5c / 60, end DAT_005aeb48 / 4c), the steps DAT_005aeb54 / 58 = -1 where the end lies
 * below the start. EAX = y start. Route (b): gfx_line.c's copy is a file-static (verified through the line rows). */
int rl_4037ee(int x1, int y1, int x2, int y2)
{
    g_gfx_y_step = 1;
    g_gfx_x_step = 1;
    g_gfx_abs_dx = x1 <= x2 ? x2 - x1 : x1 - x2;
    g_gfx_abs_dy = y2 >= y1 ? y2 - y1 : y1 - y2;
    int fwd = g_gfx_abs_dx >= g_gfx_abs_dy ? x1 <= x2 : y2 >= y1;
    if (fwd) { g_gfx_y_start = y1; g_gfx_y_end = y2; g_gfx_x_start = x1; g_gfx_x_end = x2; }
    else     { g_gfx_y_start = y2; g_gfx_y_end = y1; g_gfx_x_start = x2; g_gfx_x_end = x1; }
    if (g_gfx_x_end < g_gfx_x_start) g_gfx_x_step = -1;
    if (g_gfx_y_end < g_gfx_y_start) g_gfx_y_step = -1;
    return g_gfx_y_start;
}

/* Port of FUN_0040489e (111 B) — zero the ten frame-cadence counters (0x40490d). Route (b): engine_tick_cadence has
 * no linked module. */
int rl_40489e(void)
{
    g_bf_5c9268 = 0; g_rl_5c926c = 0; g_rl_5aeb98 = 0; g_rl_5af8e8 = 0; g_rl_5bb470 = 0;
    g_rl_5bb49c = 0; g_rl_5c9284 = 0; g_rl_5c9290 = 0; g_rl_5c9a78 = 0; g_rl_5cd418 = 0;
    return 0;
}

/* Port of FUN_0040490d (313 B) — step the frame-cadence counters: modulo 4 / 8 / 16 / 24 / 32 / 64 / 128 / 256 /
 * 384 / 768. (b). */
int rl_40490d(void)
{
#define STEP(g, n) do { if (++(g) >= (n)) (g) = 0; } while (0)
    STEP(g_bf_5c9268, 4); STEP(g_rl_5c926c, 8); STEP(g_rl_5aeb98, 16); STEP(g_rl_5af8e8, 24); STEP(g_rl_5bb470, 32);
    STEP(g_rl_5bb49c, 64); STEP(g_rl_5c9284, 128); STEP(g_rl_5c9290, 256); STEP(g_rl_5c9a78, 384); STEP(g_rl_5cd418, 768);
#undef STEP
    return 0;
}

/* Port of FUN_00404b2c (109 B) — the single LFSR DAT_00591564 stepped 31 times; DAT_005c9a84 = its low 7 bits = EAX.
 * Route (a): engine_rng_step_single (pure in / out). */
extern int32_t g_bf_591564;   /* DAT_00591564 (engine_bf_setup_globals.c; rhost: aliased) */
int rl_404b2c(void)
{
    engine_rng_single_in_t in = { (uint32_t)g_bf_591564 };
    engine_rng_single_out_t o;
    engine_rng_step_single(&in, &o);
    g_bf_591564 = (int32_t)o.new_state;
    g_bf_5c9a84 = (int32_t)o.rng_7bit;
    return (int)o.rng_7bit;
}

/* Port of FUN_00404d6b (43 B) — a b / 100 (signed). (a): engine_math.c. */
int rl_404d6b(int a, int b) { return engine_math_percent_of(a, b); }

/* Port of FUN_00404d96 (43 B) — a b / 10000. (a). */
int rl_404d96(int a, int b) { return engine_math_bp_of(a, b); }

/* Port of FUN_00404dc1 (66 B) — 100 a / b, 0 for b == 0. (a). */
int rl_404dc1(int a, int b) { return engine_math_percent_div(a, b); }

/* Port of FUN_00404e03 (72 B) — a / b rounded up (a remainder adds one), 0 for b == 0. (a). */
int rl_404e03(int a, int b) { return engine_math_div_ceil(a, b); }

/* Port of FUN_00404e4b (98 B) — the value of the first (key, value) pair of n whose key > q, else the fallback. (a). */
int rl_404e4b(int q, int table, int n, int fallback)
{
    return engine_math_lookup_threshold(q, (const int *)BYTES(table), n, fallback);
}

/* Port of FUN_00404ead (159 B) — |dx| + |dy|, storing |dx| / |dy| in DAT_00591560 / DAT_0058fea0. Route (a):
 * engine_path_dist_manhattan (on the mirror g_path_dist_dx / _dy). */
int rl_404ead(int x1, int y1, int x2, int y2) { return engine_path_dist_manhattan(x1, y1, x2, y2); }

/* Port of FUN_00404f4c (180 B) — max(|dx|, |dy|), storing |dx| / |dy| as 0x404ead. (a): engine_path_dist_chebyshev. */
int rl_404f4c(int x1, int y1, int x2, int y2) { return engine_path_dist_chebyshev(x1, y1, x2, y2); }

/* ================================================================ menus, the confirm dialog, the tick guard */

/* the hot-spot records the routers walk: 24 B each — +0 x, +2 y, +4 x2 / size, +6 y2 / size (int16), +8 the action
 * (a retail code pointer), +12 the toggle / lit byte, +13 the press countdown, +14 the repeat count, +15 the kind,
 * +16 / +20 the router's param0 / param1 (DAT_0059154c / DAT_00591550) */
#define HS16(r, off) (*(const int16_t *)((r) + (off)))
#define HS32(r, off) (*(const int32_t *)((r) + (off)))
static void hs_fire(const uint8_t *r)   /* the record's action, through retail (a router action: hooked / retail) */
{
    engine_retail_call(*(const uint32_t *)(r + 8), 0);
}

/* Port of FUN_00408fcb (96 B) — load a raw .pl8 backdrop straight into the back buffer (h rows at the file's
 * offset 24); a failed read logs "OK :Couldn't load:" and quits (0x4b2234). Route (b): gfx.c is not linked. */
int rl_408fcb(int name, int h)
{
    if (g_gfx_canvas_pixels == NULL) return 0;
    if (rl_4af8d9(name, PTRV(g_gfx_canvas_pixels), g_gfx_canvas_width * h, 24) == 0) {
        rl_4afab9(STRV(g_rl_4d21cc, 0x4d21ccu, 0x4d21ccu), name, 0);
        quit_4b2234();
    }
    return 0;
}

/* Port of FUN_0040ccfa (94 B) — the confirm dialog's box: a 14 x 8 bevel panel at (DAT_0052af90 - 16, DAT_0052af98 -
 * 16) and its localized line (string 10, entry DAT_0057cb14) at +16. Route (b): engine_confirm_dialog.c is not linked.
 * (Its first call, 0x4b1de0, is an empty function: elided.) */
int rl_40ccfa(void)
{
    g_gfx_menu_redraw_a = 1;
    gfx_panel_draw_bevel(CHROME, g_rl_52af90 - 16, g_rl_52af98 - 16, 14, 8);
    wrap_line(10, g_rl_57cb14, g_rl_52af90 + 16, g_rl_52af98 + 16, FONT, 63);
    return 0;
}

/* Port of FUN_0040da1e (884 B) — the auto-repeat button router (n records at `table`, offset (x, y); a record is a
 * SQUARE of side +6). First every record's countdown +13 ticks down: at 0 it lights (+12 = 1, +14 = 0) and a kind-5
 * record fires its action. Kind 3 is unlit each call. Then the first record under the cursor: kind 4 (repeat) fires
 * on a press / repeat (the alert cue 0x426216(1), countdown 3) or, held with DAT_004ea128, every frame from its 8th
 * held frame where the repeat table DAT_004d2748[count] allows it (the count stops at 47; param2 = the count); kind 2
 * (toggle) flips +12 and fires unless already repeating; kind 5 (delayed) cues and arms a 20-frame countdown.
 * EAX: 1 + the record's index, else 0. Route (b): engine_menu_input.c is not linked. */
int rl_40da1e(int x, int y, int table, int n)
{
    uint8_t *r = BYTES(table);
    for (int i = 0; i < n; i++, r += 24) {
        if (r[13] != 0) {
            r[13]--;
            if (r[13] == 0) {
                r[12] = 1;
                r[14] = 0;
                if (r[15] == 5) {
                    PARAM(0) = HS32(r, 16);
                    PARAM(1) = HS32(r, 20);
                    hs_fire(r);
                }
            }
        } else
            r[14] = 0;
        if (r[15] == 3) r[12] = 0;
    }
    r = BYTES(table);
    for (int j = 0; j < n; j++, r += 24) {
        int s = HS16(r, 6);
        if (x + HS16(r, 0) > CUR_X || x + s + HS16(r, 0) <= CUR_X || y + HS16(r, 2) > CUR_Y ||
            y + s + HS16(r, 2) <= CUR_Y)
            continue;
        if (r[15] == 4) {
            if (IN_PRESS != 0 || IN_REPEAT != 0) {
                rl_426216(1);
                r[13] = 3;
                r[14] = 0;
                r[12] = 1;
                PARAM(0) = HS32(r, 16);
                PARAM(1) = HS32(r, 20);
                PARAM(2) = 0;
                g_rl_591558 = 0;
                hs_fire(r);
                return j + 1;
            }
            if (IN_HELD == 0) return 0;
            r[13] = 3;
            if (g_rl_4ea128 == 0) return 0;
            r[14]++;
            if (r[14] >= 0x30)
                r[14] = 47;
            else {
                if (r[14] < 8) return 0;
                if (g_rl_4d2748[r[14]] == 0) return 0;
            }
            PARAM(0) = HS32(r, 16);
            PARAM(1) = HS32(r, 20);
            PARAM(2) = r[14];
            g_rl_591558 = 0;
            hs_fire(r);
            return j + 1;
        }
        if (r[15] == 2) {
            r[13] = 1;
            if (r[14] == 0) {
                r[12] ^= 1;
                PARAM(0) = HS32(r, 16);
                PARAM(1) = HS32(r, 20);
                g_rl_591558 = 0;
                hs_fire(r);
            }
            return j + 1;
        }
        if (r[15] == 5) {
            if (IN_PRESS == 0 && IN_REPEAT == 0) return 0;
            rl_426216(1);
            r[13] = 20;
            r[12] = 1;
            return j + 1;
        }
    }
    return 0;
}

/* Port of FUN_0040e3ee (505 B) — the click router (n records at `table`, offset (x, y); a record spans x..x2,
 * y..y2): the first under the cursor whose kind fires — 1 on a press, 3 on a release, 2 on a press / repeat (which
 * clears DAT_0058feb0) or held with DAT_0057d3c8 — lights +13, sets the router's params and runs the action. EAX: 1 +
 * its index, else 0. QUIRK: a held kind-2 record without DAT_0057d3c8 does not advance the record pointer — the
 * remaining iterations re-test the same record, so records after it are never reached. Route (b)
 * (engine_menu_input.c is not linked). */
int rl_40e3ee(int x, int y, int table, int n)
{
    uint8_t *r = BYTES(table);
    for (int i = 0; i < n; i++) {
        if (x + HS16(r, 0) <= CUR_X && x + HS16(r, 4) > CUR_X && y + HS16(r, 2) <= CUR_Y && y + HS16(r, 6) > CUR_Y) {
            uint8_t k = r[15];
            int fire = 0;
            if (k == 1)
                fire = IN_PRESS != 0;
            else if (k == 3)
                fire = IN_UP != 0;
            else if (k == 2 && (IN_HELD != 0 || IN_PRESS != 0 || IN_REPEAT != 0)) {
                if (IN_PRESS != 0 || IN_REPEAT != 0) {
                    g_rl_58feb0 = 0;
                    fire = 1;
                } else if (g_mv_rate_tier1 != 0)
                    fire = 1;
                else
                    continue;   /* (the quirk: r stays) */
            }
            if (fire) {
                r[13] = 1;
                PARAM(0) = HS32(r, 16);
                PARAM(1) = HS32(r, 20);
                g_rl_591558 = 0;
                hs_fire(r);
                return i + 1;
            }
        }
        r += 24;
    }
    return 0;
}

/* Port of FUN_0040e5e7 (153 B) — the hover test: 1 + the index of the first record (x..x2, y..y2, offset (x, y))
 * under the cursor, else 0. Route (b). */
int rl_40e5e7(int x, int y, int table, int n)
{
    const uint8_t *r = BYTES(table);
    for (int i = 0; i < n; i++, r += 24)
        if (x + HS16(r, 0) <= CUR_X && x + HS16(r, 4) > CUR_X && y + HS16(r, 2) <= CUR_Y && y + HS16(r, 6) > CUR_Y)
            return i + 1;
    return 0;
}

/* Port of FUN_0040e6f2 (111 B) — open the confirm dialog (message entry `msg` at (x, y), answer callback `cb` in
 * DAT_00552ff4): the screen saved in DAT_0057c90c, screen 0x1e, DAT_0059151c = 0, the box drawn (0x40ccfa), the whole
 * screen dirty, the map gate DAT_0053e9ac and redraw 2; the message's cue 0x4b36c0(msg) (its EAX). Route (b). */
int rl_40e6f2(int msg, int x, int y, int cb)
{
    g_bf_57c90c = (int32_t)SCREEN;
    engine_set_current_screen(0x1e);
    AT(uint8_t, g_bf_59151c, 0x59151cu, 0x59151cu) = 0;
    g_rl_52af90 = x;
    g_rl_52af98 = y;
    g_rl_552ff4 = cb;
    g_rl_57cb14 = msg;
    rl_40ccfa();
    rl_45240a();
    DIRTY->below_status_redraw = 1;
    engine_set_demand_redraw(2);
    return rl_4b36c0(msg);
}

/* Port of FUN_0040e7e4 (87 B) — the continue seal: on a release over the 24 x 24 seal at (DAT_0055ce78,
 * DAT_0057c8a0), 0x4b18e3 and 1; else 0. Route (b) (engine_ingame_input.c is not linked). */
int rl_40e7e4(void)
{
    if (IN_UP == 0) return 0;
    if (rl_4b1deb(g_gfx_seal_x, g_gfx_seal_y, 24, 24) == 0) return 0;
    rl_4b18e3();
    return 1;
}

/* Port of FUN_0040e890 (43 B) — one frame (0x4b99c0); then engine state >= 9 sets the save-pump flag DAT_005c9274.
 * EAX 0. HOST-LEVEL: rhost's frame boundary is every call of 0x4b99c0 (rhost.c frame_wrap) — no row
 * (docs/findings/rl-ui4.md). */
int rl_40e890(void)
{
    rl_4b99c0();
    if (g_engine_state >= 9) g_rl_5c9274 = 1;
    return 0;
}

/* Port of FUN_0040e8bb (240 B) — the message loop's frame guard: a lost primary DAT_004eabd0 or composition surface
 * DAT_004eafac (IsLost == DDERR_SURFACELOST) that gfx_restore cannot restore → 0x4b26b8, EAX 0. Else the error latch
 * 3 → 1; the save-pump flag DAT_005c9274 runs the pump (MP 0x49792e + 0x497a34(1), else 0x4978ad + 0x497a34(0));
 * latch 1 → EAX 0, else a frame (0x40e890) and EAX 1. HOST-LEVEL (the frame): no row. */
typedef HRESULT (__stdcall *rl_islost_fn)(void *self);
static HRESULT surface_islost(uint32_t obj)   /* IDirectDrawSurface::IsLost (vtable +0x60) */
{
    void *o = (void *)(uintptr_t)obj;
    return ((rl_islost_fn)(*(void ***)o)[0x60 / 4])(o);
}
int rl_40e8bb(void)
{
    uint32_t primary = (uint32_t)AT(int32_t, g_rl_4eabd0, 0x4eabd0u, 0x4eabd0u);
    int lost = (primary != 0 && surface_islost(primary) == (HRESULT)0x887601c2u) ||
               (g_rl_4eafac != 0 && surface_islost((uint32_t)g_rl_4eafac) == (HRESULT)0x887601c2u);
    if (lost && gfx_restore() == 0) {
        lost_4b26b8();
        return 0;
    }
    if (g_gfx_last_error == 3) g_gfx_last_error = 1;
    if (g_rl_5c9274 != 0) {
        if (g_sim_mp_active == 0) {
            rl_4978ad();
            rl_497a34(0);
        } else {
            save_49792e();
            rl_497a34(1);
        }
    }
    if (g_gfx_last_error == 1) return 0;
    rl_40e890();
    return 1;
}

/* Port of FUN_0040ecc4 (1099 B) — the hardware defaults: CD letter 'D', CDROM speed 2, 8000 KB physical memory, CPU
 * 486 / 66 MHz, video speed 0, 13 sound channels; then read l2.ini (else sierra.ini in the WindowsDir it names, else
 * sierra.ini here) into a 20000-byte block and parse "CDRomDrives=" (the letter), "CDROM=", "SoundChannels=",
 * "PhysicalMem=", "VideoSpeed=" (atoi), "CPU=" ('>' / 'p' / 'P' first → 586, else 486; the MHz after the '-').
 * CDROM >= 4 with > 70 MHz sets the fast flag DAT_0053f5f4. A key is found when find_substr + find_char('=') > 0
 * (find_substr's 0 is "absent"). Route (b): config.c is not linked. */
int rl_40ecc4(void)
{
#define S4D(va) STRV(g_rl_4d2838, 0x4d2838u, va)
    char dest[40];
    g_rl_53f200 = 0x44;
    g_rl_53f5f8 = 2;
    g_rl_53f5e8 = 8000;
    g_rl_53f5dc = 486;
    g_rl_53f5e0 = 66;
    g_rl_53f5e4 = 0;
    g_rl_53f5f0 = 13;
    g_rl_53f5f4 = 0;
    uint8_t *buf = (uint8_t *)rl_plat_malloc(0x4e20);
    if (buf == NULL) return 0;
    int at;
    int ok = 0;   /* 1: a block to parse */
    if (rl_4af8d9(S4D(0x4d2838), PTRV(buf), 0x4e20, 0) != 0) {   /* "l2.ini" */
        at = rl_401000(PTRV(buf), S4D(0x4d2840), 0x4e20);          /* "WindowsDir" */
        at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
        if (at <= 0) {
            rl_4afab9(S4D(0x4d284c), 0, 0);                         /* "Err:Could not read string WindowsDir= ..." */
            goto fail;
        }
        rl_401136(PTRV(buf + at), PTRV(dest), 20);
        rl_4012a2(PTRV(dest));
        strcat(dest, STR(g_rl_4d2838, 0x4d2838u, 0x4d287c));        /* "\sierra.ini" */
        ok = rl_4af8d9(PTRV(dest), PTRV(buf), 0x4e20, 0) != 0;
    }
    if (!ok && rl_4af8d9(S4D(0x4d2888), PTRV(buf), 0x4e20, 0) == 0)   /* "sierra.ini" */
        goto fail;
    at = rl_401000(PTRV(buf), S4D(0x4d2894), 0x4e20);                  /* "CDRomDrives" */
    at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
    if (at > 0) g_rl_53f200 = buf[at];
    at = rl_401000(PTRV(buf), S4D(0x4d28a0), 0x4e20);                  /* "CDROM" */
    at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
    if (at > 0) g_rl_53f5f8 = atoi((const char *)buf + at);
    at = rl_401000(PTRV(buf), S4D(0x4d28a8), 0x4e20);                  /* "SoundChannels" */
    at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
    if (at > 0) g_rl_53f5f0 = atoi((const char *)buf + at);
    at = rl_401000(PTRV(buf), S4D(0x4d28b8), 0x4e20);                  /* "PhysicalMem" */
    at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
    if (at > 0) g_rl_53f5e8 = atoi((const char *)buf + at);
    at = rl_401000(PTRV(buf), S4D(0x4d28c4), 0x4e20);                  /* "VideoSpeed" */
    at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
    if (at > 0) g_rl_53f5e4 = atoi((const char *)buf + at);
    at = rl_401000(PTRV(buf), S4D(0x4d28d0), 0x4e20);                  /* "CPU" */
    at += rl_401085(PTRV(buf + at), 0x3d, 0x4e20);
    if (at > 0) {
        uint8_t c = buf[at];
        g_rl_53f5dc = (c == 0x3e || c == 0x70 || c == 0x50) ? 586 : 486;
        int i = 0;
        while (buf[at + i] != 0x2d && i < 20) i++;
        g_rl_53f5e0 = atoi((const char *)buf + at + i + 1);
    }
    if (g_rl_53f5f8 >= 4 && g_rl_53f5e0 > 70) g_rl_53f5f4 = 1;
    rl_4c0580(PTRV(buf));
    return 0;
fail:
    rl_4afab9(S4D(0x4d28d4), 0, 0);                                   /* "Err:Could not read sierra.ini" */
    rl_4c0580(PTRV(buf));
    return 0;
#undef S4D
}

/* ================================================================ the screen dispatcher and the strategic screen */

/* Port of FUN_0040f1a0 (1117 B) — the screen dispatcher: the tooltip down (0x47703a), then the screen byte's
 * renderer (a = the redraw mode, passed to the arms that take one); screen 0 the strategic map (0x40f5fd); the
 * menu-redraw byte DAT_004eb260 cleared. EAX: the arm's (no caller reads it). Route (b): engine_screen_dispatch.c is
 * not linked. */
int rl_40f1a0(int a)
{
    int r;
    rl_47703a();
    switch (SCREEN) {
    case 0x35: r = rl_414819(0); break;
    case 0x36: r = rl_414819(1); break;
    case 0x1c: r = rl_41e1dd(); break;
    case 0x1f: r = title_scene(); break;
    case 0x45: r = screen_41e5d0(); break;
    case 0x2e: r = rl_421707(); break;
    case 0x2f: r = rl_421d09(); break;
    case 0x39: r = rl_414f68(); break;
    case 0x44: r = screen_425a6a(); break;
    case 0x42: r = rl_41515c(); break;
    case 0x43: r = rl_4152ea(); break;
    case 0x25: r = rl_41543f(); break;
    case 0x31: r = rl_4154ea(); break;
    case 0x32: r = rl_40c928(); break;
    case 0x1d: r = rl_421f14(); break;
    case 0x12: r = rl_422d22(); break;
    case 0x13: r = rl_422ff1(); break;
    case 0x28:
    case 0x29: r = rl_4233f7(a); break;
    case 0x2b: r = rl_423241(); break;
    case 0x20: r = rl_41593b(a); break;
    case 0x08: r = rl_415fb7(a); break;
    case 0x09: r = treasury_render(); break;
    case 0x0b: r = noble_faces_render(); break;
    case 0x0a: r = armory_render(a); break;
    case 0x17:
        if (a == 1) armory_render(a);
        r = armory_overlay();
        break;
    case 0x11: r = rl_4192b1(); break;
    case 0x1b: r = rl_419789(a); break;
    case 0x1a: r = rl_41789b(); break;
    case 0x0c: r = trade_render(); break;
    case 0x18: r = rl_41ad5d(); break;
    case 0x14: r = county_panel(0x4110b1u); break;
    case 0x15: r = county_panel(0x41152fu); break;
    case 0x16: r = county_panel(0x4116fbu); break;
    case 0x19: r = county_panel(0x411b72u); break;
    case 0x0f: r = rl_412b33(); break;
    case 0x02: r = workforce_render(1); break;
    case 0x04: r = terrain_panel(); break;
    default:
        r = (int)SCREEN;
        if (SCREEN == 0) r = rl_40f5fd(a);
        break;
    }
    g_gfx_menu_redraw_a = 0;
    return r;
}

/* Port of FUN_0040f5fd (470 B) — the strategic screen (workforce panel mode DAT_0053f050 == 1 only): mode a == 1
 * first 0x4b1897 / 0x4b11ce and the menu-redraw byte; the map cleared (0x4b1867), all dirty; the three side strips of
 * the misc sheet (54 / 57 / 59 at x 478; "ERR:screens no misc data" logged without one); the map scene ticked (the map
 * gate set around it, the rankings dirty); the menu bar (0x419c78); in view mode 2 the overview's caption box
 * (string 101 / the map, "year" + the year, string 34 / 1 at the foot); the territory and end-turn panels, the
 * minimap at (480, 25); a == 1 presents (0x452160(1)); a < 3 applies the palette DAT_005691f0 (0x4b0ab5). Route (b):
 * engine_screen_strat.c is not linked. */
int rl_40f5fd(int a)
{
    int r = 0;
    if (g_bf_53f050 != 1) return 0;
    if (a == 1) {
        rl_4b1897();
        rl_4b11ce();
        g_gfx_menu_redraw_a = 1;
    }
    rl_4b1867();
    rl_45240a();
    if (g_bf_5530c8 == 0) rl_4afab9(STRV(g_rl_4d3d18, 0x4d3d18u, 0x4d3d18u), 0, 0);
    gfx_sprite_render_opaque_from_blob(MISC, 54, 478, 24);
    gfx_sprite_render_opaque_from_blob(MISC, 57, 478, 430);
    gfx_sprite_render_opaque_from_blob(MISC, 59, 478, 460);
    DIRTY->below_status_redraw = 1;
    engine_map_scene_tick_frame();
    engine_army_ai_set_ranking_dirty(1);
    DIRTY->below_status_redraw = 1;
    rl_419c78();
    if ((int32_t)s_slot_0057cb18 == 2) {
        panel_box(0, 412, 30, 4);
        g_gfx_uppercase_highlight = 1;
        g_gfx_line_x_acc = 0;
        wrap_line(101, (int32_t)s_slot_0053f034, 64, 424, FONT_TALL, 63);
        wrap_line(34, 0, g_gfx_line_x_acc + 80, 424, FONT_TALL, 63);
        year_number((int32_t)s_slot_00553edc, g_gfx_line_x_acc + 96, 424, 1);
        g_gfx_uppercase_highlight = 0;
        wrap_line(34, 1, 80, 454, FONT, 63);
    }
    territory_panel(1);
    endturn_panel(1);
    r = minimap_render(480, 25);
    if (a == 1) r = rl_452160(1);
    if (a < 3) r = rl_4b0ab5(PTRV(g_rl_5691f0));
    return r;
}

/* ================================================================ the strategic camera, the category buckets, the map
 * cells, the army starts */

/* Port of FUN_00429b1d (135 B) — clamp the cell scroll DAT_005651b4 / b8 to [0, 64 - cols] x [0, 128 - rows] (lower
 * bounds first). EAX: 128 - rows when the row clamped high, else 129 - rows. Route (a): engine_camera_clamp_scroll. */
int rl_429b1d(void)
{
    int row_lo = (int32_t)s_slot_005651b8 < 0 ? 0 : (int32_t)s_slot_005651b8;
    int high = 129 - g_mv_row_count <= row_lo;
    engine_camera_clamp_scroll((int *)&s_slot_005651b4, (int *)&s_slot_005651b8, g_mv_col_count, g_mv_row_count);
    return high ? 128 - g_mv_row_count : 129 - g_mv_row_count;
}

/* Port of FUN_00429ba4 (878 B) — the cell under the cursor: inside the map band (x from DAT_0052afdc over cols x
 * DAT_00568220, y from DAT_00553244 + one row over rows rows of DAT_0053f65c; mode 2 biases y by 16) pick the
 * staggered cell (DAT_00553f04 col, DAT_005681f4 row; its pixel DAT_00553ff8 / DAT_00553fe8) and its tile
 * DAT_00568974 from the grid DAT_0055cea0 (65 per row, at the scroll); a tile below 0x0fff0000 also sets the row /
 * column edge flags DAT_0056d654 / DAT_0057cab8 and EAX 1, else 0. The argument is unused (its branches all bias 0).
 * Route (a): engine_camera_cursor_to_cell (pure in / out). */
#define PUTI(g, v) do { int32_t v_ = (int32_t)(v); if ((g) != v_) (g) = v_; } while (0)
int rl_429ba4(int unused)
{
    (void)unused;
    engine_camera_hover_in_t in = {
        CUR_X, CUR_Y, g_gfx_alt_clip_left, g_mv_x_step, g_mv_x_half, g_mv_cam_y0, g_mv_y_step,
        g_mv_col_count, g_mv_row_count, (int32_t)s_slot_005651b4, (int32_t)s_slot_005651b8,
        (int32_t)s_slot_0057c8fc, (const int32_t *)g_rl_55cea0,
    };
    engine_camera_hover_out_t o = { g_rl_553f04, g_rl_5681f4, g_rl_553ff8, g_rl_553fe8, g_rl_568974, g_rl_56d654,
                                    g_rl_57cab8 };
    int r = engine_camera_cursor_to_cell(&in, &o);
    PUTI(g_rl_553f04, o.cell_col);
    PUTI(g_rl_5681f4, o.cell_row);
    PUTI(g_rl_553ff8, o.cell_px_x);
    PUTI(g_rl_553fe8, o.cell_px_y);
    PUTI(g_rl_568974, o.tile_index);
    PUTI(g_rl_56d654, o.edge_row);
    PUTI(g_rl_57cab8, o.edge_col);
    return r;
}

/* Port of FUN_0042a15f (206 B) — the hovered cell's scroll origin DAT_0050a4a8 / ac (its pixel, x less a half step,
 * odd rows another half step left), when the map is ready (DAT_005533b0) and the absolute cell lies on the 65 x 129
 * map. EAX: the last bound tested (no caller reads it). Route (a): engine_camera_compute_scroll_pixels. */
int rl_42a15f(void)
{
    int x = g_rl_50a4a8, y = g_rl_50a4ac;
    engine_camera_compute_scroll_pixels(g_rl_5533b0, (int32_t)s_slot_005651b4, (int32_t)s_slot_005651b8, g_rl_553f04,
                                        g_rl_5681f4, g_mv_x_step, g_mv_x_half, g_gfx_alt_clip_left, g_mv_y_step,
                                        g_mv_cam_y0, &x, &y);
    PUTI(g_rl_50a4a8, x);
    PUTI(g_rl_50a4ac, y);
    return 0;
}

/* the category buckets: six realms' 16 county kinds DAT_00567970 (16 B a realm) and the digest DAT_00569518 (6 B).
 * Route (b) throughout: engine_category_buckets.c keeps them in a state struct. */
#define BUCKET(i, j) s_slot_00567970[16 * (i) + (j)]

/* Port of FUN_004290d0 (131 B) — clear the buckets and the digest; EAX 5 (the last index). */
int rl_4290d0(void)
{
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 16; j++) BUCKET(i, j) = 0;
    for (int k = 0; k < 6; k++) s_slot_00569518[k] = 0;
    return 5;
}

/* Port of FUN_00429153 (96 B) — append kind c to bucket b (1-based) in its first empty slot (none: nothing). EAX:
 * 16 (b - 1), its low byte c when appended. */
int rl_429153(int c, int b)
{
    uint32_t e = (uint32_t)(16 * (b - 1));
    for (int i = 0; i < 16; i++) {
        if (BUCKET(b - 1, i) == 0) {
            BUCKET(b - 1, i) = (uint8_t)c;
            return (int)((e & ~0xffu) | (uint8_t)c);
        }
    }
    return (int)e;
}

/* Port of FUN_0042925e (81 B) — 1 when kind k is in the digest, else 0. */
int rl_42925e(int k)
{
    for (int i = 0; i < 6; i++)
        if (s_slot_00569518[i] == k) return 1;
    return 0;
}

/* Port of FUN_004291b3 (171 B) — the digest: per bucket its first kind not already in the digest, trying slots 0,
 * 2, 4, … (an empty slot restarts at 1; at most 6 tries; the last tried kind stands). */
int rl_4291b3(void)
{
    for (int i = 0; i < 6; i++) {
        int tries = 0, at = 0;
        int k = BUCKET(i, at);
        while (rl_42925e(k) != 0) {
            if (++tries > 5) break;
            at += 2;
            k = BUCKET(i, at);
            if (k <= 0) at = 1;
        }
        s_slot_00569518[i] = (uint8_t)k;
    }
    return 0;
}

/* Port of FUN_004298c1 (604 B) — build the strategic cell grid DAT_0055cea0 (129 rows x 65) for rotation r (0 / 2 /
 * 4 / 6): every cell 0x0fff0000 (no tile), the terrain layer seeded (0x46797d, the map DAT_0053f034), the rotation
 * kept in DAT_00522f7c; then the map's DAT_005694f8 rows x DAT_0052afd4 columns of tile indices DAT_0057d338 (k
 * cols + m) laid diagonally from the rotation's corner (the extents DAT_0053e980 / DAT_0052afc8). EAX: the row count
 * walked. Route (b): engine_map_init.c is not linked. (Another rotation would walk uninitialized locals; every caller
 * passes 0, or the rotation stepped by 2 mod 8.) */
int rl_4298c1(int rot)
{
    for (int i = 0; i < 129; i++)
        for (int j = 0; j < 65; j++) g_rl_55cea0[65 * i + j] = 0x0fff0000;
    rl_46797d((int32_t)s_slot_0053f034);
    g_mv_rotation = rot;
    int row0 = 0, row_k = 0, row_m = 0, col0 = 0, col_k = 0, col_m = 0;
    switch (rot) {
    case 0: row0 = 2 * g_rl_53e980 + 1; row_k = 1; row_m = 1; col0 = 64; col_k = -1; col_m = 1; break;
    case 2: row0 = 66; row_k = 1; row_m = -1; col0 = 2 * g_rl_52afc8 + 1; col_k = 1; col_m = 1; break;
    case 4: row0 = 129 - 2 * g_rl_53e980 - 2; row_k = -1; row_m = -1; col0 = 66; col_k = 1; col_m = -1; break;
    case 6: row0 = 66; row_k = -1; row_m = 1; col0 = 129 - 2 * g_rl_53e980 - 2; col_k = -1; col_m = -1; break;
    default: break;
    }
    int k;
    for (k = 0; k < g_rl_5694f8; k++) {
        int row = row0, col2 = col0;
        for (int m = 0; m < g_rl_52afd4; m++) {
            g_rl_55cea0[65 * row + col2 / 2] = g_rl_57d338 * (m + k * g_rl_52afd4);
            row += row_m;
            col2 += col_m;
        }
        row0 += row_k;
        col0 += col_k;
    }
    return k;
}

/* Port of FUN_00427ed0 (311 B) — the army starts: for each realm kind in the digest DAT_00569518 (to the first 0)
 * with a start cell near it (0x428007: a rawflag-bit0 cell, else 0x428078: flag 0 / 2 — they leave the cell in the
 * cursor DAT_00569570 / 78), place an army (0x46e1b0(3, col, row, 6) → the new slot DAT_0052afe0) and stamp it: +0x150
 * = 1, +0x10 = the kind, +0x166 = 100, +0x167 = the kind, +0x164 (low byte) = 1, +0x14f = the bucket index; count
 * them in DAT_005530b4. Route (b): game_init.c is not linked. */
#define TER8(i, va) AT(uint8_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))
int rl_427ed0(void)
{
    s_slot_005530b4 = 0;
    for (int i = 0; i < 6; i++) {
        int k = s_slot_00569518[i];
        if (k == 0) break;
        if (engine_retail_call(0x428007u, 1, (uint32_t)k) == 0 &&   /* TODO(rl-link): game_init's army_near (bit0) */
            engine_retail_call(0x428078u, 1, (uint32_t)k) == 0)     /* TODO(rl-link): … (flag 0 / 2) */
            continue;
        rl_46e1b0(3, g_cell_cursor_col, g_cell_cursor_row, 6);
        int t = engine_territory_current_slot();
        TER8(t, 0x52f200u) = 1;
        TER8(t, 0x52f0c0u) = (uint8_t)k;
        TER8(t, 0x52f216u) = 100;
        TER8(t, 0x52f217u) = (uint8_t)k;
        TER8(t, 0x52f214u) = 1;
        TER8(t, 0x52f1ffu) = (uint8_t)i;
        s_slot_005530b4++;
    }
    return 0;
}

/* Port of FUN_00428471 (11 B) — an empty function. */
int rl_428471(void) { return 0; }

/* Port of FUN_0042847c (82 B) — the merchant offers DAT_005532a0 (16 B each, 16 of them): +0 the default price
 * DAT_004d8910[i], +4 = 0. EAX 0xf0 (the last row's offset). Route (b): game_init.c is not linked. */
int rl_42847c(void)
{
    for (int i = 0; i < 16; i++) {
        AT(int32_t, s_slot_005532a0, 0x5532a0u, 0x5532a0 + 16 * i) = AT(int32_t, g_rl_4d8910, 0x4d8910u, 0x4d8910 + 4 * i);
        AT(int32_t, s_slot_005532a0, 0x5532a0u, 0x5532a4 + 16 * i) = 0;
    }
    return 0xf0;
}
