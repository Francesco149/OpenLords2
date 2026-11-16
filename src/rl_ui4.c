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
#include "rl_main.h"
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
static int panel_box(int x, int y, int cols, int rows)   /* 0x409397: the chrome panel + fill (rl_link) */
{ return rl_409397((int)x, (int)y, (int)cols, (int)rows); }
static int year_number(int v, int x, int y, int mode)   /* rl_link: county_year_number 0x41a900 */
{ return rl_41a900((int)v, (int)x, (int)y, (int)mode); }
static int territory_panel(int a) { return rl_40f7d3(a); }   /* strat_draw_territory_panel 0x40f7d3 (rl_link: engine_hud_panel) */
static int endturn_panel(int a) { return rl_41a734(a); }     /* engine_hud_panel_render_endturn 0x41a734 (rl_link) */
static int minimap_render(int x, int y) { return (int)engine_retail_call(0x410aa9u, 2, (uint32_t)x, (uint32_t)y); }   /* rl_link: engine_minimap_render 0x410aa9 */
static int title_scene(void) { return rl_41e7e1(); }          /* rl_link: engine_title_render_scene 0x41e7e1 */
static int treasury_render(void) { return rl_416925(); }      /* rl_link: engine_treasury_render 0x416925 */
static int noble_faces_render(void) { return rl_416cf3(); }   /* rl_link: engine_noble_faces_render 0x416cf3 */
static int armory_render(int a) { return rl_417ea7((int)a); }   /* rl_link: engine_armory_render 0x417ea7 */
static int armory_overlay(void) { return rl_418653(); }       /* rl_link: engine_armory_overlay_render 0x418653 */
static int trade_render(void) { return rl_416308(); }         /* rl_link: engine_trade_render 0x416308 */
static int county_panel(int (*panel)(void)) { return panel(); }   /* the county panels 0x4110b1 / 0x41152f / 0x4116fb / 0x411b72 (rl_link) */
static int workforce_render(int a) { return rl_412143((int)a); }   /* rl_link: engine_workforce_hud_render 0x412143 */
static int terrain_panel(void) { return (int)engine_retail_call(0x41b032u, 0); }        /* rl_link: 0x41b032 (engine_terrain_info) */
/* ported at the end of this file (the standalone's last seams) */
static int screen_41e5d0(void) { return rl_41e5d0(); }   /* screen 0x45 */
static int screen_425a6a(void) { return rl_425a6a(); }   /* screen 0x44 */
static int quit_4b2234(void) { return rl_4b2234(); }     /* the fatal-load exit */
__attribute__((used)) static int gfx_restore(void) { return rl_4b2730(); }   /* gfx_restore (called from asm below) */
__attribute__((used)) static int lost_4b26b8(void) { return rl_4b26b8(); }  /* the surfaces cannot be restored (asm below) */
__attribute__((used)) static int save_49792e(void) { return (int)engine_retail_call(0x49792eu, 0); }     /* TODO(rl-extern): 0x49792e (the MP save pump) */

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
__attribute__((naked)) int rl_40e890(void)   /* retail's frame (push ebp / ebx esi edi): see rl_40e8bb */
{
    __asm__("pushl %ebp\n\t movl %esp, %ebp\n\t pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
            "movl $1, _rl_frame_port_guard\n\t"                 /* the frame runs under the port's WinMain chain */
            "movl _rl_frame_driver, %eax\n\t testl %eax, %eax\n\t jz 1f\n\t call *%eax\n\t jmp 2f\n"   /* rhost: frame_wrap */
            "1:\t call _rl_4b99c0\n"
            "2:\t movl $0, _rl_frame_port_guard\n\t cmpl $9, _g_engine_state\n\t jl 3f\n\t movb $1, _g_rl_5c9274\n"
            "3:\t xorl %eax, %eax\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t leave\n\t ret");
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
/* Written as retail's frame, instruction for instruction (as WinMain, rl_main.c, and 0x40e890): the frame — all of
 * the game — runs at retail's stack depth, so stack residue that holds stack pointers (0x488793) equals pure retail's. */
__attribute__((naked)) int rl_40e8bb(void)
{
    __asm__("pushl %ebp\n\t movl %esp, %ebp\n\t pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
            "movl _g_rl_4eabd0, %eax\n\t testl %eax, %eax\n\t jz 1f\n\t"
            "pushl %eax\n\t movl (%eax), %eax\n\t call *0x60(%eax)\n\t cmpl $0x887601c2, %eax\n\t jz 2f\n"      /* IsLost */
            "1:\t movl _g_rl_4eafac, %eax\n\t testl %eax, %eax\n\t jz 3f\n\t"
            "pushl %eax\n\t movl (%eax), %eax\n\t call *0x60(%eax)\n\t cmpl $0x887601c2, %eax\n\t jnz 3f\n"
            "2:\t call _gfx_restore\n\t testl %eax, %eax\n\t jnz 3f\n\t call _lost_4b26b8\n\t xorl %eax, %eax\n\t jmp 9f\n"
            "3:\t xorl %eax, %eax\n\t movb _g_gfx_last_error, %al\n\t cmpl $3, %eax\n\t jnz 4f\n\t movb $1, _g_gfx_last_error\n"
            "4:\t xorl %eax, %eax\n\t movb _g_rl_5c9274, %al\n\t testl %eax, %eax\n\t jz 6f\n\t"
            "cmpl $0, _g_sim_mp_active\n\t jnz 5f\n\t"
            "call _rl_4978ad\n\t pushl $0\n\t call _rl_497a34\n\t addl $4, %esp\n\t jmp 6f\n"
            "5:\t call _save_49792e\n\t pushl $1\n\t call _rl_497a34\n\t addl $4, %esp\n"
            "6:\t xorl %eax, %eax\n\t movb _g_gfx_last_error, %al\n\t cmpl $1, %eax\n\t jnz 7f\n\t xorl %eax, %eax\n\t jmp 9f\n"
            "7:\t call _rl_40e890\n\t movl $1, %eax\n"
            "9:\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t leave\n\t ret");
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
    case 0x14: r = county_panel(rl_4110b1); break;
    case 0x15: r = county_panel(rl_41152f); break;
    case 0x16: r = county_panel(rl_4116fb); break;
    case 0x19: r = county_panel(rl_411b72); break;
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
        if (engine_retail_call(0x428007u, 1, (uint32_t)k) == 0 &&   /* rl_link: game_init's army_near (bit0) */
            rl_428078(k) == 0)                                      /* … (flag 0 / 2) */
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

/* ================================================================ DirectSound init, the stream stop, the cue release;
 * DirectDraw init, surfaces, the palette */

/* other partitions' globals (declared as in their headers) */
extern int32_t g_rl_53f22c;          /* DAT_0053f22c (rl_ui3_globals.h): the desktop's bits per pixel */
extern int32_t g_rl_4e65a0;          /* DAT_004e65a0 (rl_ui3_globals.h): the screen width */
extern int32_t g_rl_4e6974;          /* DAT_004e6974 (rl_glue_globals.h): the IDirectDraw */
extern uint8_t g_rl_4ea1b0[0x300];   /* DAT_004ea1b0 (rl_glue_globals.h): the screen palette (3 B a colour) */
extern uint8_t g_rl_4d50f0[0x40];    /* DAT_004d50f0 (rl_ui2_globals.h): the palette's fixed ends */
extern uint8_t g_rl_4e5a00[4];       /* hWndMain (rl_misc_globals.h) */

/* COM through the object's own vtable (as rl_io.c) */
#define VT(obj, off) (*(void **)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)(obj) + (off)))
typedef HRESULT (WINAPI *rl_com0_t)(uint32_t self);
typedef HRESULT (WINAPI *rl_com1_t)(uint32_t self, uint32_t a);
typedef HRESULT (WINAPI *rl_com2_t)(uint32_t self, uint32_t a, uint32_t b);
typedef HRESULT (WINAPI *rl_com3_t)(uint32_t self, uint32_t a, uint32_t b, uint32_t c);
typedef HRESULT (WINAPI *rl_com4_t)(uint32_t self, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef HRESULT (WINAPI *rl_com5_t)(uint32_t self, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
#define COM0(o, off)                ((rl_com0_t)VT(o, off))((uint32_t)(o))
#define COM1(o, off, a)             ((rl_com1_t)VT(o, off))((uint32_t)(o), (uint32_t)(a))
#define COM2(o, off, a, b)          ((rl_com2_t)VT(o, off))((uint32_t)(o), (uint32_t)(a), (uint32_t)(b))
#define COM3(o, off, a, b, c)       ((rl_com3_t)VT(o, off))((uint32_t)(o), (uint32_t)(a), (uint32_t)(b), (uint32_t)(c))
#define COM4(o, off, a, b, c, d)    ((rl_com4_t)VT(o, off))((uint32_t)(o), (uint32_t)(a), (uint32_t)(b), (uint32_t)(c), (uint32_t)(d))
#define COM5(o, off, a, b, c, d, e) ((rl_com5_t)VT(o, off))((uint32_t)(o), (uint32_t)(a), (uint32_t)(b), (uint32_t)(c), (uint32_t)(d), (uint32_t)(e))

#define PPDS      (*(volatile uint32_t *)g_bf_4d4760)                           /* ppDS: the IDirectSound */
#define DS_HR     g_bf_522b58                                                   /* DAT_00522b58: the last HRESULT */
#define DS_PRIM   AT(uint32_t, g_rl_522aec, 0x522aecu, 0x522af0u)               /* DAT_00522af0: the primary buffer */
#define DS_CUE    AT(uint32_t, g_rl_522aec, 0x522aecu, 0x522aecu)               /* DAT_00522aec: the one cue buffer */
#define DS_SFX(i) g_rl_522b00[i]                                                /* DAT_00522b00[i]: the effect bank (20) */
#define DS_TIMER(k) g_rl_522b00[20 + (k)]                                       /* uTimerID[k] */
#define DS_STREAM(k) AT(uint32_t, g_rl_522b60, 0x522b60u, 0x522b80u + 4u * (uint32_t)(k))   /* DAT_00522b80[k] */
#define DS_PERIOD AT(uint32_t, g_rl_4d4754, 0x4d4754u, 0x4d4758u)               /* uPeriod */
#define WFX(off)  (&AT(uint8_t, g_rl_522b90, 0x522b90u, 0x522b90u + (off)))     /* the primary's WAVEFORMATEX */
#define HWNDMAIN  AT(uint32_t, g_rl_4e5a00, 0x4e5a00u, 0x4e5a00u)               /* hWndMain */
#define S4D47(va) STRV(g_rl_4d4768, 0x4d4768u, va)
static int ds_err(int hr) { return rl_427d72(hr); }   /* the DS error's index */
static int ds_close(void) { return (int)engine_retail_call(0x425d35u, 0); }                 /* TODO(rl-extern): 0x425d35 (DirectSound off) */

/* Port of FUN_00425ded (94 B) — the primary buffer's format DAT_00522b90: PCM, 2 channels, 11025 Hz, block 2, 22050
 * B/s (the block x the rate), 8 bits, cbSize 0. EAX = the byte rate. Route (b): audio.c is not linked. */
int rl_425ded(void)
{
    memset(WFX(0), 0, 0x12);
    *(int16_t *)WFX(0) = 1;
    *(int16_t *)WFX(2) = 2;
    *(int32_t *)WFX(4) = 11025;
    *(int16_t *)WFX(0xc) = 2;
    *(int32_t *)WFX(8) = (uint16_t)*(int16_t *)WFX(0xc) * *(int32_t *)WFX(4);
    *(int16_t *)WFX(0xe) = 8;
    return *(int32_t *)WFX(8);
}

/* Port of FUN_00425b10 (549 B) — DirectSound on (once: ppDS set → 1): DirectSoundCreate (fails: its error index
 * 0x427d72 logged + 1, EAX 0), the format (0x425ded), co-operative level 2 (DSSCL_PRIORITY) on hWndMain, the primary
 * buffer (DSBUFFERDESC 20 B, DSBCAPS_PRIMARYBUFFER) into DAT_00522af0 and its format set (a failure logs the HRESULT
 * + 1, DirectSound off 0x425d35, EAX 0); the effect bank DAT_00522b00[20], the two streams DAT_00522b80 and their
 * timers zeroed; "OK :DS Installed.", EAX 1. Every HRESULT in DAT_00522b58. Route (b). */
int rl_425b10(void)
{
    if (PPDS != 0) return 1;
    DS_HR = (int32_t)rl_plat_DirectSoundCreate(NULL, (void **)g_bf_4d4760, NULL);
    if (DS_HR != 0) {
        DS_HR = ds_err(DS_HR);
        rl_4afab9(S4D47(0x4d4768), 0, DS_HR + 1);   /* "ERR:DS failed open " */
        return 0;
    }
    rl_425ded();
    uint32_t desc[5] = { 20, 1, 0, 0, 0 };   /* dwSize, DSBCAPS_PRIMARYBUFFER, 0 bytes, reserved, no format */
    DS_HR = (int32_t)COM2(PPDS, 0x18, HWNDMAIN, 2);   /* SetCooperativeLevel */
    if (DS_HR != 0) {
        rl_4afab9(S4D47(0x4d477c), 0, DS_HR + 1);   /* "ERR:DS failed set co-op " */
        ds_close();
        return 0;
    }
    DS_HR = (int32_t)COM3(PPDS, 0x0c, (uintptr_t)desc, (uintptr_t)&DS_PRIM, 0);   /* CreateSoundBuffer */
    if (DS_HR != 0) {
        rl_4afab9(S4D47(0x4d4798), 0, DS_HR + 1);   /* "ERR:DS failed setup primary buffer " */
        ds_close();
        return 0;
    }
    DS_HR = (int32_t)COM1(DS_PRIM, 0x38, (uintptr_t)WFX(0));   /* SetFormat */
    if (DS_HR != 0) {
        rl_4afab9(S4D47(0x4d47bc), 0, DS_HR + 1);   /* "ERR:DS failed to change primary buffer format " */
        ds_close();
        return 0;
    }
    for (int i = 0; i < 20; i++) DS_SFX(i) = 0;
    for (int i = 0; i < 2; i++) DS_STREAM(i) = 0;
    for (int i = 0; i < 2; i++) DS_TIMER(i) = 0;
    rl_4afab9(S4D47(0x4d47ec), 0, 0);   /* "OK :DS Installed." */
    return 1;
}

/* Port of FUN_00426d11 (196 B) — stop stream k (0 music, 1 speech) when DirectSound, its timer and its buffer are
 * all live: kill the timer (timeEndPeriod uPeriod), clear it, Stop the buffer (HRESULT in DAT_00522b58) and close the
 * stream's file (0x4b97f9 on the stream record DAT_00522bb0 + 84 k: its HMMIO at +4, its format at +0). Route (b). */
#define ST_ADDR(k, off) ((int)(uintptr_t)&AT(uint32_t, g_rl_522bb0, 0x522bb0u, 0x522bb0u + 84u * (uint32_t)(k) + (off)))
int rl_426d11(int k)
{
    if (PPDS == 0 || DS_TIMER(k) == 0 || DS_STREAM(k) == 0) return 0;
    rl_plat_timeKillEvent((UINT)DS_TIMER(k));
    rl_plat_timeEndPeriod(DS_PERIOD);
    DS_TIMER(k) = 0;
    DS_HR = (int32_t)COM0(DS_STREAM(k), 0x48);   /* Stop */
    rl_4b97f9(ST_ADDR(k, 4), ST_ADDR(k, 0));
    return 0;
}

/* Port of FUN_00427d19 (89 B) — release the cue buffer DAT_00522aec (Stop, Release, cleared). EAX: Release's (the
 * caller's when there is none). Route (b): audio.c's audio_cue_release keeps its own buffer. */
int rl_427d19(void)
{
    if (PPDS == 0 || DS_CUE == 0) return 0;
    COM0(DS_CUE, 0x48);
    int r = (int)COM0(DS_CUE, 0x08);
    DS_CUE = 0;
    return r;
}

/* ---------------------------------------------------------------- DirectDraw (gfx.c, not linked: route (b)) */
#define DD_OBJ     g_rl_4e6974                                  /* DAT_004e6974: the IDirectDraw */
#define DD_PRIMARY AT(uint32_t, g_rl_4eabd0, 0x4eabd0u, 0x4eabd0u)   /* DAT_004eabd0: the primary surface */
#define DD_BACK    g_rl_4eafac                                  /* DAT_004eafac: the composition surface */
#define DD_FULL    g_rl_53f228                                  /* DAT_0053f228: full screen (0: a window) */
#define S4D51(va)  STRV(g_rl_4d5130, 0x4d5130u, va)
#define S4D52(va)  STRV(g_rl_4d51f0, 0x4d51f0u, va)
static int dd_err(int hr) { return rl_42f60e(hr); }   /* the DD error's index */
static int pal_window(void) { return rl_42f449(); }   /* the windowed palette */

/* Port of FUN_0042f306 (323 B) — the full-screen palette's PALETTEENTRYs DAT_005165b0: entry 0 from DAT_004d50f0,
 * 1..254 from the screen palette DAT_004ea1b0 (flags 4: PC_NOCOLLAPSE), 255 from DAT_004d512b (IDA's byte_4D4E2E
 * [3 x 255]); EAX 255. Route (b). */
int rl_42f306(void)
{
    uint8_t *e = g_rl_5165b0;
    for (int i = 0; i < 1; i++) {
        e[4 * i + 0] = AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d50f0 + 3 * i);
        e[4 * i + 1] = AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d50f1 + 3 * i);
        e[4 * i + 2] = AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d50f2 + 3 * i);
        e[4 * i + 3] = 0;
    }
    for (int j = 1; j < 255; j++) {
        e[4 * j + 0] = g_rl_4ea1b0[3 * j];
        e[4 * j + 1] = g_rl_4ea1b0[3 * j + 1];
        e[4 * j + 2] = g_rl_4ea1b0[3 * j + 2];
        e[4 * j + 3] = 4;
    }
    for (int k = 255; k < 256; k++) {
        e[4 * k + 0] = AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d4e2e + 3 * k);
        e[4 * k + 1] = AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d4e2f + 3 * k);
        e[4 * k + 2] = AT(uint8_t, g_rl_4d50f0, 0x4d50f0u, 0x4d4e30 + 3 * k);
        e[4 * k + 3] = 0;
    }
    return 255;
}

/* Port of FUN_0042eb7c (277 B) — clear the screen: UpdateWindow, then a colour-0 fill Blt (DDBLT_COLORFILL |
 * DDBLT_WAIT; a 100-byte DDBLTFX whose other fields are stack residue) of the composition surface and the primary
 * (full screen), or of the primary's client rect DAT_004ea180 (a window); a failure logs (1 / 2 / the HRESULT), EAX 0;
 * else 1. */
int rl_42eb7c(void)
{
    uint32_t fx[25];
    memset(fx, 0, sizeof fx);   /* retail: only dwSize and dwFillColor are set; the rest is residue no Blt reads */
    rl_plat_UpdateWindow((HWND)(uintptr_t)HWNDMAIN);
    fx[0] = 100;
    fx[20] = 0;
    if (DD_FULL != 0) {
        if (COM5(DD_BACK, 0x14, 0, 0, 0, 0x1000400u, (uintptr_t)fx) != 0) {
            rl_4afab9(S4D52(0x4d51f0), 0, 1);   /* "ERR:DD Clearing Fill failed no:" */
            return 0;
        }
        if (COM5(DD_PRIMARY, 0x14, 0, 0, 0, 0x1000400u, (uintptr_t)fx) != 0) {
            rl_4afab9(S4D52(0x4d5210), 0, 2);
            return 0;
        }
    } else {
        HRESULT hr = COM5(DD_PRIMARY, 0x14, (uintptr_t)g_rl_4ea180, 0, 0, 0x1000400u, (uintptr_t)fx);
        if (hr != 0) {
            rl_4afab9(S4D52(0x4d5230), 0, (int)hr);   /* "ERR:DD Clearing(W) Fill failed ddrval:" */
            return 0;
        }
    }
    return 1;
}

/* Port of FUN_0042e5c0 (1254 B) — DirectDraw on: the desktop's colour depth DAT_0053f22c (BITSPIXEL x PLANES; 16 when
 * NUMCOLORS is -1); not 8 bpp forces full screen DAT_0053f228 (the system palette then saved, GetPaletteEntries on a
 * NULL palette into DAT_00515d90); the screen size DAT_004e65a0 / DAT_004ea838; the IDirectDraw DAT_004e6974 (or
 * DirectDrawCreate: fails → log, EAX 0) and its co-operative level (0x13 full screen, 8 a window). Full screen: the
 * window's style made a popup, the mode xRight x yBottom x 8 set. A window: the style made overlapped, sized to the
 * canvas and centred (DAT_004eabb8 / DAT_004eabb0), "OK :DD Kept resolution.". Then the pending messages are pumped;
 * EAX 1. HOST-LEVEL (it creates the DirectDraw object, styles the window, pumps the message queue): no row. */
int rl_42e5c0(void)
{
    HWND w = (HWND)(uintptr_t)HWNDMAIN;
    HDC dc = rl_plat_GetDC(NULL);
    int numcolors = rl_plat_GetDeviceCaps(dc, 24);
    int bits = rl_plat_GetDeviceCaps(dc, 12);
    g_rl_53f22c = rl_plat_GetDeviceCaps(dc, 14) * bits;
    if (numcolors == -1) g_rl_53f22c = 16;
    rl_plat_ReleaseDC(NULL, dc);
    if (DD_FULL == 0) DD_FULL = g_rl_53f22c != 8;
    if (DD_FULL != 0) rl_plat_GetPaletteEntries(NULL, 0, 0x100, (PALETTEENTRY *)g_rl_515d90);
    g_rl_4e65a0 = rl_plat_GetSystemMetrics(0);
    g_rl_4ea838 = rl_plat_GetSystemMetrics(1);
    uint32_t dd;
    HRESULT hr;
    if (DD_OBJ != 0) {
        dd = (uint32_t)DD_OBJ;
        hr = 0;
    } else
        hr = rl_plat_DirectDrawCreate(NULL, (void **)&dd, NULL);
    if (hr != 0) {
        rl_4afab9(S4D51(0x4d5130), 0, (int)hr);   /* "ERR:DD Enable error level:" */
        return 0;
    }
    if (DD_OBJ == 0) {
        hr = COM2(dd, 0x50, (uintptr_t)w, DD_FULL != 0 ? 0x13 : 8);   /* SetCooperativeLevel */
        if (hr != 0) {
            rl_4afab9(S4D51(0x4d514c), 0, (int)hr);
            return 0;
        }
        rl_4afab9(S4D51(0x4d5170), 0, 0);
    }
    if (DD_FULL != 0) {
        LONG st = rl_plat_GetWindowLongA(w, -16) | (LONG)0x90000000u;
        rl_plat_SetWindowLongA(w, -16, st & (LONG)0xff3bffffu);
        hr = COM3(dd, 0x54, g_gfx_canvas_width, g_gfx_canvas_height, 8);   /* SetDisplayMode */
        if (hr != 0) {
            rl_4afab9(S4D51(0x4d5190), 0, (int)hr);
            return 0;
        }
        rl_4afab9(S4D51(0x4d51ac), 0, 0);
    } else {
        LONG st = (rl_plat_GetWindowLongA(w, -16) & 0x7f30ffff) | 0xcf0000;
        rl_plat_SetWindowLongA(w, -16, st);
        RECT rc, work;
        rl_plat_SetRect(&rc, 0, 0, g_gfx_canvas_width, g_gfx_canvas_height);
        LONG ex = rl_plat_GetWindowLongA(w, -20);
        BOOL menu = rl_plat_GetMenu(w) != NULL;
        rl_plat_AdjustWindowRectEx(&rc, (DWORD)rl_plat_GetWindowLongA(w, -16), menu, (DWORD)ex);
        g_rl_4eabb8 = (g_rl_4e65a0 - 640) / 2;
        int x = (g_rl_4e65a0 - 640) / 2;
        g_rl_4eabb0 = (g_rl_4ea838 - 480) / 2;
        int y = (g_rl_4ea838 - 480) / 2;
        rl_plat_SetWindowPos(w, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, 0x16);
        rl_plat_SetWindowPos(w, (HWND)(intptr_t)-2, 0, 0, 0, 0, 0x13);
        rl_plat_SystemParametersInfoA(0x30, 0, &work, 0);
        rl_plat_GetWindowRect(w, &rc);
        if (rc.left < work.left) rc.left = work.left;   /* (dead: rc is not used after) */
        if (rc.top < work.top) rc.top = work.top;
        rl_plat_SetWindowPos(w, NULL, x, y, 0, 0, 0x15);
        rl_4afab9(S4D51(0x4d51c4), 0, 0);
    }
    MSG msg;
    while (rl_plat_PeekMessageA(&msg, NULL, 0, 0, 0) && rl_plat_GetMessageA(&msg, NULL, 0, 0)) {
        rl_plat_TranslateMessage(&msg);
        rl_plat_DispatchMessageA(&msg);
    }
    DD_OBJ = (int32_t)dd;
    return 1;
}

/* Port of FUN_0042ec91 (1207 B) — the surfaces: the primary (DDSD_CAPS, DDSCAPS_PRIMARYSURFACE) into DAT_004eabd0 (a
 * failure logs, EAX 0); a test Lock / Unlock of it (DAT_004df28c = 0; a failed lock logs its index 0x42f60e + 1); the
 * composition surface DAT_004eafac = 0x42f148(xRight, yBottom, 1) (none: log, EAX 0), Locked once for its pixels Buf
 * DAT_004ea1a8 (the DDSURFACEDESC's lpSurface) and Unlocked. A window also gets a clipper DAT_004ea12c on hWndMain set
 * on the primary (each failure logs, EAX 0). Then the primary's pixel format (a 32-byte DDPIXELFORMAT, discarded);
 * EAX 1. HOST-LEVEL (it creates the surfaces and the clipper): no row. */
int rl_42ec91(void)
{
    uint32_t sd[27];   /* DDSURFACEDESC (0x6c B) */
    uint32_t pf[8];    /* DDPIXELFORMAT (32 B) */
    int full = DD_FULL != 0;
    memset(sd, 0, 0x6c);
    sd[0] = 108;
    rl_4afab9(full ? S4D52(0x4d5258) : S4D52(0x4d5324), 0, 0);   /* "OK :DD Creating one Vram only." / "… and one system surface." */
    sd[1] = 1;
    sd[26] = 512;
    HRESULT hr = COM3(DD_OBJ, 0x18, (uintptr_t)sd, (uintptr_t)g_rl_4eabd0, 0);   /* CreateSurface */
    if (hr != 0) {
        rl_4afab9(full ? S4D52(0x4d5278) : S4D52(0x4d5358), 0, (int)hr);
        return 0;
    }
    g_rl_4df28c = 0;
    memset(sd, 0, 0x6c);
    sd[0] = 108;
    hr = COM4(DD_PRIMARY, 0x64, 0, (uintptr_t)sd, 1, 0);   /* Lock */
    if (hr != 0)
        rl_4afab9(full ? S4D52(0x4d52cc) : S4D52(0x4d53ac), 0, dd_err((int)hr) + 1);
    else {
        rl_4afab9(full ? S4D52(0x4d5298) : S4D52(0x4d5378), 0, 0);   /* "OK :DD can lock primary surface - but not going to" */
        g_rl_4df28c = 0;
        COM1(DD_PRIMARY, 0x80, 0);   /* Unlock */
    }
    DD_BACK = rl_42f148(g_gfx_canvas_width, g_gfx_canvas_height, 1);
    if (DD_BACK == 0) {
        rl_4afab9(full ? S4D52(0x4d52e8) : S4D52(0x4d53c8), 0, 0);
        return 0;
    }
    hr = COM4(DD_BACK, 0x64, 0, (uintptr_t)sd, 1, 0);
    if (hr == 0) {
        g_gfx_canvas_pixels = (uint8_t *)(uintptr_t)sd[9];
        COM1(DD_BACK, 0x80, 0);
    }
    if (full)
        rl_4afab9(S4D52(0x4d530c), 0, 0);   /* "OK :DD Surface created." */
    else {
        if (COM3(DD_OBJ, 0x10, 0, (uintptr_t)&g_rl_4ea12c, 0) != 0) {   /* CreateClipper */
            rl_4afab9(S4D52(0x4d53ec), 0, 0);
            return 0;
        }
        if (COM2(g_rl_4ea12c, 0x20, 0, HWNDMAIN) != 0) {   /* SetHWnd */
            rl_4afab9(S4D52(0x4d5408), 0, 0);
            return 0;
        }
        if (COM1(DD_PRIMARY, 0x70, g_rl_4ea12c) != 0) {   /* SetClipper */
            rl_4afab9(S4D52(0x4d5430), 0, 0);
            return 0;
        }
        rl_4afab9(S4D52(0x4d545c), 0, 0);
    }
    memset(pf, 0, sizeof pf);
    pf[0] = 32;
    COM1(DD_PRIMARY, 0x54, (uintptr_t)pf);   /* GetPixelFormat */
    return 1;
}

/* Port of FUN_0042f1df (295 B) — the palette: a window with a != 0 toggles the system palette use (SYSPAL_NOSTATIC
 * then SYSPAL_STATIC); the entries (full screen 0x42f306 → DAT_005165b0, else 0x42f449 → DAT_00515d90); the old
 * IDirectDrawPalette DAT_004df288 released; a new one (DDPCAPS_8BIT | DDPCAPS_ALLOW256) set on the primary (EAX: its
 * HRESULT), or "ERR:Palette create failed." HOST-LEVEL (it creates a DirectDraw palette object): no row. */
int rl_42f1df(int a)
{
    if (a != 0 && DD_FULL == 0) {
        HDC dc = rl_plat_GetDC(NULL);
        rl_plat_SetSystemPaletteUse(dc, 2);
        rl_plat_SetSystemPaletteUse(dc, 1);
        rl_plat_ReleaseDC(NULL, dc);
    }
    if (DD_FULL != 0)
        rl_42f306();
    else
        pal_window();
    if (g_rl_4df288 != 0) {
        COM0(g_rl_4df288, 0x08);   /* Release */
        g_rl_4df288 = 0;
    }
    if (DD_FULL != 0)
        COM4(DD_OBJ, 0x14, 0x44, (uintptr_t)g_rl_5165b0, (uintptr_t)&g_rl_4df288, 0);   /* CreatePalette */
    else
        COM4(DD_OBJ, 0x14, 0x44, (uintptr_t)g_rl_515d90, (uintptr_t)&g_rl_4df288, 0);
    if (g_rl_4df288 != 0) return (int)COM1(DD_PRIMARY, 0x7c, g_rl_4df288);   /* SetPalette */
    return rl_4afab9(STRV(g_rl_4d54a8, 0x4d54a8u, 0x4d54a8u), 0, 0);
}

/* ================================================================ the panel renders: the menu bar's chrome, the labour
 * screen, the rankings, the merchant, the diplomacy gift, the armoury, the shipment, the title backdrop / option list */
#include "gfx_line.h"
#include "gfx_text.h"
#include "gfx_text_label.h"
#include "gfx_sprite.h"
#include "engine_hud_menubar.h"
#include "engine_combat.h"             /* g_combat_flag_530d0 / _567d0c (DAT_005530d0 / DAT_00567d0c) */
#include "engine_sendsupplies.h"       /* g_send_target (DAT_00553c78) */
#include "engine_armory.h"             /* g_arm_weapon (DAT_00553f20) */
#include "engine_diplomacy_action.h"   /* g_diplo_action_mode (DAT_005651cc) */
#include "engine_title_render.h"       /* g_tr_p9_tab / g_tr_p9_count / g_tr_selected_button */

/* other partitions' globals (declared here as in their headers) */
extern int32_t g_rl_522cb4;          /* DAT_00522cb4 (rl_render_globals.h): the menu bar's hovered item */
extern int32_t g_rl_56899c;          /* DAT_0056899c (rl_skirmish_globals.h): the custom skirmish backdrop */
extern int32_t g_rl_553e5c;          /* DAT_00553e5c (rl_ui2_globals.h): the title's option page */

/* ---------------------------------------------------------------- records, blobs */
#define P5_SYSTEM    ((const uint8_t *)g_rl_5bb540)      /* &DAT_005bb540: system2.pl8 (the seal) */
#define P5_LOCAL     ((int)s_slot_0057c8cc)              /* DAT_0057c8cc: the local player */
#define P5_COUNTY    ((int)s_slot_0057c93c)              /* DAT_0057c93c: the current county */
#define P5_SUBSTATE  ((int32_t)s_slot_0057c8fc)          /* DAT_0057c8fc: the frame driver's sub-state */
#define P5_ADV       ((int32_t)s_slot_0053f25c)          /* DAT_0053f25c: the advanced readouts */
#define P5_PL8(p, va)  AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define P5_PL32(p, va) AT(int32_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define P5_CO8(c, va)  AT(uint8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))          /* county record */
#define P5_COS8(c, va) AT(int8_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define P5_CO16(c, va) AT(int16_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define P5_CO32(c, va) AT(int32_t, g_per_army_768, 0x53f9b0u, (int32_t)(va) + 768 * (c))
#define P5_T32(blk, blkva, va) AT(int32_t, blk, blkva, va)                                         /* a retail table */
#define P5_NAME(p)   (&AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d54 + 44 * (p)))             /* DAT_00553d50 + 4: a realm's name */
#define P5_PAL       g_rl_4ea8a0                         /* DAT_004ea8a0: the screen's palette (0x300 B) */
#define P5_SHEET     PTRV(g_gfx_current_blob)            /* DAT_004eabec: the current sprite sheet / scratch */
#define P5_S3E(va)   STRV(g_rl_4d3e58, 0x4d3e58u, va)    /* the labour screen's strings */
#define P5_S3EC(va)  STR(g_rl_4d3e58, 0x4d3e58u, va)

/* ---------------------------------------------------------------- the gfx layer (the walker loaded / stored) */
static void p5_wrap_c(int idx, int skip, int x, int y, int w, const uint8_t *font, int color)   /* 0x402c5e */
{
    gfx_wrap_localized_line_centered(loc_in(), idx, skip, x, y, w, font, GLYPHS, METRICS, (uint8_t)color);
    loc_out();
}
static void p5_wrap_loc(int idx, int skip, int x, int y, int w1, int n1, int x2, int w2, const uint8_t *font,
                        int color)   /* 0x40328e */
{
    gfx_wrap_localized(loc_in(), idx, skip, x, y, w1, n1, x2, w2, font, GLYPHS, METRICS, (uint8_t)color);
    loc_out();
}
static void p5_text(const uint8_t *buf, int x, int y, const uint8_t *font, int color)   /* 0x402637 */
{
    gfx_text_render_line(buf, x, y, font, GLYPHS, (uint8_t)color);
}
static void p5_label(int v, int pad, const char *suffix, int x, int y, const uint8_t *font, int color)   /* 0x402f64 */
{
    gfx_text_label_render_dec(v, (char)pad, suffix, x, y, font, GLYPHS, (uint8_t)color);
}
static void p5_label_ls(int v, int mode, const char *lab, const char *suffix, int x, int y, const uint8_t *font,
                        int cpos, int cneg)   /* 0x402e0c */
{
    gfx_text_label_render_labeled_signed(v, mode, (const uint8_t *)lab, suffix, x, y, font, GLYPHS, (uint8_t)cpos,
                                         (uint8_t)cneg);
}
static void p5_seal(int x, int y, int mode) { gfx_sprite_seal_draw(P5_SYSTEM, x, y, mode); }   /* 0x40d1bc */

/* ---------------------------------------------------------------- seams */
/* ported, not on retail's layout (a context struct) — rl_link */
static int p5_num_units(int v, int mode, int x, int y, const uint8_t *font, int color)   /* labor_num_units 0x41ab67 (rl_link) */
{
    return rl_41ab67(v, mode, x, y, (int)(uintptr_t)font, color);
}
static int p5_castle_block(int county, int x, int y, int row)   /* rl_link: labor_castle_block 0x41dedb (engine_labor: a ctx) */
{ return rl_41dedb((int)county, (int)x, (int)y, (int)row); }
static int p5_armory_racks(void) { return rl_418426(); }    /* rl_link: armory_racks 0x418426 (engine_armory: a ctx) */
static int p5_armory_counts(void) { return rl_4181eb(); }   /* rl_link: engine_armory_counts_render 0x4181eb */
static int p5_minimap_plot(int county, int x, int y, int mode)   /* rl_link: engine_minimap_plot 0x410cbd */
{ return (int)engine_retail_call(0x410cbdu, 4, (uint32_t)county, (uint32_t)x, (uint32_t)y, (uint32_t)mode); }
static int p5_title_setup(int tab) { return rl_41f6c7((int)tab); }   /* rl_link: engine_title_render_setup_screen 0x41f6c7 */
static int p5_title_41f77a(int tab) { return rl_41f77a(tab); }  /* 0x41f77a (rl mpnet) */
static int p5_title_420630(int tab) { return rl_420630(tab); }  /* the MP skirmish setup page (rl mpnet2) */

/* ================================================================ the menu bar */

/* Port of FUN_00419c78 (1262 B) — the menu bar's chrome: off the full-screen panels (screens 8..0xd, 0x17, 0x1b..0x20,
 * 0x22, 0x2c..0x2f), when the rankings are dirty (DAT_0056d6a0) or the local realm's +0x118 (its crowns) changed
 * since DAT_004e59c4: the parchment strips (25 + 2 tiles), the 640 x 24 bevel, in the sub-state 0 game the live
 * realms' shields (misc 85 + the realm's +0xa, 16 px apart from x 270), the File / Options / Help labels (the table
 * DAT_004dc428, hovered DAT_00522cb4), the year and its season string (29 / DAT_0057c934), the crowns at (500, 6);
 * the bar's centre marked dirty. (Its head — three debug readouts behind unconditional jumps — is dead code.) EAX:
 * the screen byte / a callee's (no caller reads it). Route (b): engine_hud_menubar.c's port is only the state-free
 * front and takes its sheets as arguments. */
int rl_419c78(void)
{
    unsigned s = SCREEN;
    if ((s >= 8 && s <= 0xd) || s == 0x17 || (s >= 0x1b && s <= 0x20) || s == 0x22 || (s >= 0x2c && s <= 0x2f))
        return 0;
    int redraw = 0;
    if (engine_army_ai_ranking_dirty() != 0) redraw = 1;
    if (P5_PL32(P5_LOCAL, 0x57c018) != g_rl_4e59c4) redraw = 1;
    if (!redraw) return 0;
    engine_army_ai_set_ranking_dirty(0);
    g_rl_4e59c4 = P5_PL32(P5_LOCAL, 0x57c018);
    engine_hud_menubar_tile_strip(CHROME, 0, 0, 25, 1);
    engine_hud_menubar_tile_strip(CHROME, 592, 0, 2, 1);
    gfx_line_rect_bevel_dark_tl(0, 0, 640, 24);
    if (P5_SUBSTATE == 0) {
        int n = 0;
        for (int p = 1; p < 6; p++)
            if (P5_PL8(p, 0x57bf04) != 0 && P5_PL32(p, 0x57bf00) < 999)
                gfx_sprite_render_from_blob(MISC, P5_PL8(p, 0x57bf0a) + 85, 16 * n++ + 270, 4);
    }
    engine_hud_menubar_labels((int16_t *)&g_rl_4dc428, 3, g_rl_522cb4, FONT, GLYPHS, METRICS, loc_in(), 0x3f, DIRTY);
    loc_out();
    if (P5_SUBSTATE == 0) {
        g_gfx_line_x_acc = 0;
        year_number((int32_t)s_slot_00553edc, 360, 6, 3);
        wrap_line(29, (int32_t)s_slot_0057c934, g_gfx_line_x_acc + 364, 6, FONT, 63);
    }
    p5_num_units(P5_PL32(P5_LOCAL, 0x57c018), 0, 500, 6, FONT, 63);
    rl_4527a6(235, 0, 26, 2, 1);
    return 0;
}

/* ================================================================ the labour screen (screen 0x0f; the task DAT_00553f54
 * 1 grain, 2 cattle, 3 reclaim, 4 castle, 5..7 the mines, 8 the smithy, 9 …) */

/* Port of FUN_00412b33 (824 B) — the labour screen: base dir, all dirty; with a task: iconvill.pl8 into the sheet,
 * the glyph width 25; the smithy (8) relights the menu bar (rankings dirty) and the fire (combat flags, fire.wav, cue
 * 8); any other task cues DAT_004d2950[task], a 25 x DAT_004d29a0[task] chrome panel at (48, 96), the 50 x 50 icon box
 * with icon DAT_004d2974[task] (task 9: 16), the task's title (74 / task) and its workers (the county's +0xc4 + 12
 * (task - 1); 249 below its wanted +0xc8, 252 above its cap +0xcc, else 63); then the task's panel and the continue
 * seal; the menu-redraw byte. EAX: a callee's (no caller reads it). Route (b): engine_labor.c takes a context and
 * keeps the task in a file-static. (Its 0x4b1de0 call is an empty function: elided.) */
int rl_412b33(void)
{
    rl_4af502();
    rl_45240a();
    int t = g_rl_553f54;
    if (t == 0) return 0;
    rl_4af8d9(P5_S3E(0x4d3e58), P5_SHEET, 0x27100, 0);   /* "iconvill.pl8" */
    g_gfx_glyph_w = 25;
    if (g_rl_553f54 == 8) {
        engine_army_ai_set_ranking_dirty(1);
        rl_419c78();
        g_combat_flag_530d0 = 1;
        g_combat_flag_567d0c = 1;
        rl_427990(P5_S3E(0x4d3e68), 0, 0);                  /* "fire.wav" */
        rl_426216(8);
    } else {
        t = g_rl_553f54;
        if (P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d2950 + 4 * t) != 0)
            rl_426216(P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d2950 + 4 * g_rl_553f54));
        t = g_rl_553f54;
        g_gfx_glyph_height_short = (int16_t)P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d29a0 + 4 * t);
        g_gfx_cursor_x = 48;
        g_gfx_cursor_y = 96;
        panel_box(g_gfx_cursor_x, g_gfx_cursor_y, g_gfx_glyph_w, g_gfx_glyph_height_short);
        gfx_line_rect(64, 104, 50, 50, 63);
        int icon = P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d2974 + 4 * g_rl_553f54);
        if (g_rl_553f54 == 9) icon = 16;
        gfx_sprite_render(icon, 65, 105);
        wrap_line(74, g_rl_553f54, 128, 106, FONT_TALL, 63);
        int c = P5_COUNTY;
        t = g_rl_553f54;
        int color = 63;
        if (P5_CO32(c, 0x53fa78 + 12 * t - 12) > P5_CO32(c, 0x53fa74 + 12 * t - 12))
            color = 249;
        else if (P5_CO32(c, 0x53fa7c + 12 * t - 12) < P5_CO32(c, 0x53fa74 + 12 * t - 12))
            color = 252;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fa74 + 12 * g_rl_553f54 - 12), 2 * g_rl_553f54 + 30, 128, 136, FONT, color);
    }
    t = g_rl_553f54;
    if (t >= 5 && t <= 7)
        rl_412e6b();
    else if (t == 1)
        rl_413590();
    else if (t == 2)
        rl_413b30();
    else if (t == 3)
        rl_4140f3();
    else if (t == 4)
        rl_414220();
    else if (t == 8)
        rl_413155();
    if (g_rl_553f54 == 8)
        p5_seal(448, 448, 0);
    else
        p5_seal(420, 16 * P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d29a0 + 4 * g_rl_553f54) + 68, 0);
    g_gfx_menu_redraw_a = 1;
    return 0;
}

/* Port of FUN_00412e6b (746 B) — the mine panels (tasks 5 / 6 / 7 = the county's mineral slots 1 / 3 / 0; the smithy's
 * 8 slot 2 with the unit noun DAT_004d29c8[+0x290]): advanced mode the slot's yield % (the signed byte +0x294 + 24
 * slot); its output (+0x2a8 + 24 slot, noun 12 / 14 / 16) and string 76 / 1; then the stock lines: iron +0x284 (5),
 * stone +0x28c (6), or wood +0x280 and +0x288 (7). EAX: the last wrap's (no caller reads it). (b). The slot / noun are
 * uninitialized for a task outside 5..8 (no caller passes one). */
int rl_412e6b(void)
{
    int k = g_rl_553f54 - 1;
    int slot = 0, noun = 0;
    if (k == 4) { slot = 1; noun = 12; }
    else if (k == 5) { slot = 3; noun = 14; }
    else if (k == 6) { slot = 0; noun = 16; }
    else if (k == 7) { slot = 2; noun = P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d29c8 + 4 * P5_CO8(P5_COUNTY, 0x53fc40)); }
    if (P5_ADV == 1) {
        g_gfx_line_x_acc = 0;
        wrap_line(76, 0, g_gfx_line_x_acc + 64, 160, FONT, 63);
        p5_label(P5_COS8(P5_COUNTY, 0x53fc44 + 24 * slot), 64, P5_S3EC(0x4d3e74), g_gfx_line_x_acc + 64, 160, FONT, 63);
    }
    g_gfx_line_x_acc = 0;
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fc58 + 24 * slot), noun, 64, 176, FONT, 63);
    wrap_line(76, 1, g_gfx_line_x_acc + 64, 176, FONT, 63);
    if (k == 4) {
        g_gfx_line_x_acc = 0;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fc34), noun, 64, 192, FONT, 63);
        wrap_line(76, 2, g_gfx_line_x_acc + 64, 192, FONT, 63);
    } else if (k == 5) {
        g_gfx_line_x_acc = 0;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fc3c), noun, 64, 192, FONT, 63);
        wrap_line(76, 3, g_gfx_line_x_acc + 64, 192, FONT, 63);
    } else if (k == 6) {
        g_gfx_line_x_acc = 0;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fc30), noun, 64, 192, FONT, 63);
        wrap_line(76, 2, g_gfx_line_x_acc + 64, 192, FONT, 63);
        g_gfx_line_x_acc = 0;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fc38), noun, 64, 208, FONT, 63);
        wrap_line(76, 3, g_gfx_line_x_acc + 64, 208, FONT, 63);
    }
    return 0;
}

/* Port of FUN_00413155 (977 B) — the smithy panel: smithy.pl8 into the sheet and drawn at (0, 24); a 30 x 56-glyph
 * block cleared at (0, 424); hearth.pl8 and the hearth frame 11 + the county's smithy level +0x290 (RLE) at y 424 -
 * DAT_004d29e0[level]; a 30 x 6 chrome panel at (0, 384), the title (74 / 8, tall font) and the centred caption 75;
 * the weapon output (+0x118; advanced mode with its rate %, the signed byte +0x2c4, strings 76 / 4, 5 — else 76 / 8),
 * the stock line (76 / 6, +0x2d8 with the level's noun DAT_004d29c8, 76 / 7); the price box: bevel at (320, 390), the
 * gold / wood icons 44 / 46 and the level's DAT_004d8994 / DAT_004d8990 (8 B a level). All dirty. EAX: 0x45240a's
 * (0x43a997 discards it). (b). */
int rl_413155(void)
{
    rl_4af502();
    rl_4af8d9(P5_S3E(0x4d3e78), P5_SHEET, 0x30d40, 0);   /* "smithy.pl8" */
    gfx_sprite_render(0, 0, 24);
    g_gfx_glyph_w = 30;
    g_gfx_glyph_height_short = 56;
    gfx_sprite_fill_block(0, 424, 0);
    rl_4af8d9(P5_S3E(0x4d3e84), P5_SHEET, 0x30d40, 0);   /* "hearth.pl8" */
    int y = 424 - P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d29e0 + 4 * P5_CO8(P5_COUNTY, 0x53fc40));
    gfx_sprite_render_rle_from_blob(g_gfx_current_blob, P5_CO8(P5_COUNTY, 0x53fc40) + 11, 0, y);
    panel_box(0, 384, 30, 6);
    g_gfx_line_x_acc = 0;
    wrap_line(74, 8, 16, 390, FONT_TALL, 63);
    p5_wrap_c(75, 0, 0, 460, 460, FONT, 63);
    int noun = P5_T32(g_rl_4d2950, 0x4d2950u, 0x4d29c8 + 4 * P5_CO8(P5_COUNTY, 0x53fc40));
    if (P5_ADV == 1) {
        g_gfx_line_x_acc = 0;
        p5_label(P5_CO32(P5_COUNTY, 0x53fac8), 64, P5_S3EC(0x4d3e90), g_gfx_line_x_acc + 16, 420, FONT, 63);
        wrap_line(76, 4, g_gfx_line_x_acc + 16, 420, FONT, 63);
        p5_label(P5_COS8(P5_COUNTY, 0x53fc74), 64, P5_S3EC(0x4d3e94), g_gfx_line_x_acc + 16, 420, FONT, 63);
        wrap_line(76, 5, g_gfx_line_x_acc + 16, 420, FONT, 63);
    } else {
        g_gfx_line_x_acc = 0;
        p5_label(P5_CO32(P5_COUNTY, 0x53fac8), 64, P5_S3EC(0x4d3e98), g_gfx_line_x_acc + 16, 420, FONT, 63);
        wrap_line(76, 8, g_gfx_line_x_acc + 16, 420, FONT, 63);
    }
    g_gfx_line_x_acc = 0;
    wrap_line(76, 6, g_gfx_line_x_acc + 16, 436, FONT, 63);
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fc88), noun, g_gfx_line_x_acc + 16, 436, FONT, 63);
    wrap_line(76, 7, g_gfx_line_x_acc + 16, 436, FONT, 63);
    gfx_line_rect_bevel_dark_br(320, 390, 142, 32);
    gfx_sprite_render_from_blob(MISC, 44, 326, 391);
    p5_label(P5_T32(g_rl_4d8910, 0x4d8910u, 0x4d8994 + 8 * P5_CO8(P5_COUNTY, 0x53fc40)), 64, P5_S3EC(0x4d3e9c), 362, 398,
             FONT, 63);
    gfx_sprite_render_from_blob(MISC, 46, 393, 394);
    p5_label(P5_T32(g_rl_4d8910, 0x4d8910u, 0x4d8990 + 8 * P5_CO8(P5_COUNTY, 0x53fc40)), 64, P5_S3EC(0x4d3ea0), 436, 398,
             FONT, 63);
    return rl_45240a();
}

/* Port of FUN_00413590 (1440 B) — the grain panel: the fields' grain (+0x224) at (304, 136); advanced mode the yield
 * word 22 / (+0x208 + 100) / 29; the sown fields (+0x278, by the county's crop +0x1aa: 135 / 139 → 77 / 25, 26; none
 * → 77 / 24); advanced mode the change (+0x24c: > 0 → 77 / 16, < 0 → its magnitude + 77 / 17, 0 → 77 / 18); the
 * season DAT_0057c92c 1: the harvest (+0x230, 77 / 1) and its yield x DAT_0057c8e0 (77 / 2), else the stock (+0x248
 * in winter (4), else +0x2fc; 77 / 3), the seasons left (4 - season... 3 / 2 / 1 for 2 / 3 / other) and the
 * consumption (+0x240, 77 / 0, 4); the eaten (77 / 27: -(+0x178) signed, or "0") and the rotted (77 / 28: +0x22c).
 * EAX: the last label's (no caller reads it). (b). */
int rl_413590(void)
{
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fbd4), 2, 304, 136, FONT, 63);
    if (P5_ADV == 1) wrap_line(22, (P5_CO32(P5_COUNTY, 0x53fbb8) + 100) / 29, 128, 152, FONT, 63);
    g_gfx_line_x_acc = 0;
    if (P5_CO32(P5_COUNTY, 0x53fc28) == 0)
        wrap_line(77, 24, 64, 176, FONT, 63);
    else {
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fc28), 2, 64, 176, FONT, 63);
        if (P5_CO16(P5_COUNTY, 0x53fb5a) == 135)
            wrap_line(77, 25, g_gfx_line_x_acc + 64, 176, FONT, 63);
        else if (P5_CO16(P5_COUNTY, 0x53fb5a) == 139)
            wrap_line(77, 26, g_gfx_line_x_acc + 64, 176, FONT, 63);
    }
    if (P5_ADV == 1) {
        g_gfx_line_x_acc = 0;
        if (P5_CO32(P5_COUNTY, 0x53fbfc) > 0) {
            p5_num_units(P5_CO32(P5_COUNTY, 0x53fbfc), 2, g_gfx_line_x_acc + 64, 192, FONT, 63);
            wrap_line(77, 16, g_gfx_line_x_acc + 64, 192, FONT, 63);
        } else if (P5_CO32(P5_COUNTY, 0x53fbfc) < 0) {
            p5_num_units(-P5_CO32(P5_COUNTY, 0x53fbfc), 2, g_gfx_line_x_acc + 64, 192, FONT, 63);
            wrap_line(77, 17, g_gfx_line_x_acc + 64, 192, FONT, 63);
        } else
            wrap_line(77, 18, g_gfx_line_x_acc + 64, 192, FONT, 63);
    }
    if ((int32_t)s_slot_0057c92c == 1) {
        g_gfx_line_x_acc = 0;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fbe0), 2, 64, 216, FONT, 63);
        wrap_line(77, 1, g_gfx_line_x_acc + 64, 216, FONT, 63);
        g_gfx_line_x_acc = 0;
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fbe0) * g_rl_57c8e0, 2, 64, 232, FONT, 63);
        wrap_line(77, 2, g_gfx_line_x_acc + 64, 232, FONT, 63);
    } else {
        int left = (int32_t)s_slot_0057c92c == 2 ? 3 : (int32_t)s_slot_0057c92c == 3 ? 2 : 1;
        int stock = (int32_t)s_slot_0057c92c == 4 ? P5_CO32(P5_COUNTY, 0x53fbf8) : P5_CO32(P5_COUNTY, 0x53fcac);
        g_gfx_line_x_acc = 0;
        p5_num_units(stock, 2, 64, 216, FONT, 63);
        wrap_line(77, 3, g_gfx_line_x_acc + 64, 216, FONT, 63);
        p5_num_units(left, 66, g_gfx_line_x_acc + 64, 216, FONT, 63);
        g_gfx_line_x_acc = 0;
        wrap_line(77, 0, 64, 232, FONT, 63);
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fbf0), 2, g_gfx_line_x_acc + 64, 232, FONT, 63);
        wrap_line(77, 4, g_gfx_line_x_acc + 64, 232, FONT, 63);
    }
    wrap_line(77, 27, 64, 264, FONT, 63);
    if (P5_CO32(P5_COUNTY, 0x53fb28) == 0)
        p5_label(0, 64, P5_S3EC(0x4d3ea4), 304, 264, FONT, 63);
    else
        p5_label_ls(-P5_CO32(P5_COUNTY, 0x53fb28), 0, P5_S3EC(0x4d3eac), P5_S3EC(0x4d3ea8), 296, 264, FONT, 63, 249);
    wrap_line(77, 28, 64, 280, FONT, 63);
    if (P5_CO32(P5_COUNTY, 0x53fbdc) == 0)
        p5_label(0, 64, P5_S3EC(0x4d3eb0), 304, 280, FONT, 63);
    else
        p5_label_ls(P5_CO32(P5_COUNTY, 0x53fbdc), 0, P5_S3EC(0x4d3eb8), P5_S3EC(0x4d3eb4), 296, 280, FONT, 63, 249);
    return 0;
}

/* Port of FUN_00413b30 (1475 B) — the cattle panel: the herd (+0x250) at (304, 136); the pasture quality (+0x25c: 10 /
 * 20 / 30 → 77 / 8, 9, 10, else 11); the grazing fields (+0x274, by the crop +0x1aa: 136 / 137 / 140 / 141 → 77 / 20
 * .. 23; none → 77 / 19); advanced mode the change (+0x270, as the grain's); the births (77 / 5, +0x268), the deaths
 * (77 / 6, +0x26c) and their balance (77 / 7: signed, or "0"); the eaten (77 / 27, -(+0x17c)) and the slaughtered
 * (77 / 28, +0x258). EAX: the last label's (no caller reads it). (b). */
int rl_413b30(void)
{
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fc00), 4, 304, 136, FONT, 63);
    int q = P5_CO32(P5_COUNTY, 0x53fc0c);
    wrap_line(77, q == 10 ? 8 : q == 20 ? 9 : q == 30 ? 10 : 11, 64, 160, FONT, 63);
    g_gfx_line_x_acc = 0;
    if (P5_CO32(P5_COUNTY, 0x53fc24) == 0)
        wrap_line(77, 19, 64, 176, FONT, 63);
    else {
        p5_num_units(P5_CO32(P5_COUNTY, 0x53fc24), 4, 64, 176, FONT, 63);
        int crop = P5_CO16(P5_COUNTY, 0x53fb5a);
        if (crop == 136) wrap_line(77, 20, g_gfx_line_x_acc + 64, 176, FONT, 63);
        else if (P5_CO16(P5_COUNTY, 0x53fb5a) == 137) wrap_line(77, 21, g_gfx_line_x_acc + 64, 176, FONT, 63);
        else if (P5_CO16(P5_COUNTY, 0x53fb5a) == 140) wrap_line(77, 22, g_gfx_line_x_acc + 64, 176, FONT, 63);
        else if (P5_CO16(P5_COUNTY, 0x53fb5a) == 141) wrap_line(77, 23, g_gfx_line_x_acc + 64, 176, FONT, 63);
    }
    if (P5_ADV == 1) {
        g_gfx_line_x_acc = 0;
        if (P5_CO32(P5_COUNTY, 0x53fc20) > 0) {
            p5_num_units(P5_CO32(P5_COUNTY, 0x53fc20), 4, g_gfx_line_x_acc + 64, 192, FONT, 63);
            wrap_line(77, 16, g_gfx_line_x_acc + 64, 192, FONT, 63);
        } else if (P5_CO32(P5_COUNTY, 0x53fc20) < 0) {
            p5_num_units(-P5_CO32(P5_COUNTY, 0x53fc20), 4, g_gfx_line_x_acc + 64, 192, FONT, 63);
            wrap_line(77, 17, g_gfx_line_x_acc + 64, 192, FONT, 63);
        } else
            wrap_line(77, 18, g_gfx_line_x_acc + 64, 192, FONT, 63);
    }
    wrap_line(77, 5, 64, 216, FONT, 63);
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fc18), 4, 304, 216, FONT, 63);
    wrap_line(77, 6, 64, 232, FONT, 63);
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fc1c), 4, 304, 232, FONT, 63);
    wrap_line(77, 7, 64, 248, FONT, 63);
    if (P5_CO32(P5_COUNTY, 0x53fc18) == P5_CO32(P5_COUNTY, 0x53fc1c))
        p5_label(0, 64, P5_S3EC(0x4d3ebc), 304, 248, FONT, 63);
    else
        p5_label_ls(P5_CO32(P5_COUNTY, 0x53fc18) - P5_CO32(P5_COUNTY, 0x53fc1c), 0, P5_S3EC(0x4d3ec4), P5_S3EC(0x4d3ec0),
                    296, 248, FONT, 63, 249);
    wrap_line(77, 27, 64, 264, FONT, 63);
    if (P5_CO32(P5_COUNTY, 0x53fb2c) == 0)
        p5_label(0, 64, P5_S3EC(0x4d3ec8), 304, 264, FONT, 63);
    else
        p5_label_ls(-P5_CO32(P5_COUNTY, 0x53fb2c), 0, P5_S3EC(0x4d3ed0), P5_S3EC(0x4d3ecc), 296, 264, FONT, 63, 249);
    wrap_line(77, 28, 64, 280, FONT, 63);
    if (P5_CO32(P5_COUNTY, 0x53fc08) == 0)
        p5_label(0, 64, P5_S3EC(0x4d3ed4), 304, 280, FONT, 63);
    else
        p5_label_ls(P5_CO32(P5_COUNTY, 0x53fc08), 0, P5_S3EC(0x4d3edc), P5_S3EC(0x4d3ed8), 296, 280, FONT, 63, 249);
    return 0;
}

/* Port of FUN_004140f3 (301 B) — the reclaim panel: the swamp count (the byte +0x204; 1 → 77 / 12, else 13); the
 * reclaimed so far (+0x214: 0 → 77 / 15, else 77 / 14 + the count with noun 66). EAX: the last call's (no caller
 * reads it). (b). */
int rl_4140f3(void)
{
    g_gfx_line_x_acc = 0;
    p5_label(P5_CO8(P5_COUNTY, 0x53fbb4), 64, P5_S3EC(0x4d3ee0), 64, 184, FONT, 63);
    if (P5_CO8(P5_COUNTY, 0x53fbb4) == 1)
        wrap_line(77, 12, g_gfx_line_x_acc + 64, 184, FONT, 63);
    else
        wrap_line(77, 13, g_gfx_line_x_acc + 64, 184, FONT, 63);
    if (P5_CO32(P5_COUNTY, 0x53fbc4) == 0) {
        wrap_line(77, 15, 64, 200, FONT, 63);
        return 0;
    }
    g_gfx_line_x_acc = 0;
    wrap_line(77, 14, 64, 200, FONT, 63);
    p5_num_units(P5_CO32(P5_COUNTY, 0x53fbc4), 66, g_gfx_line_x_acc + 64, 200, FONT, 63);
    return 0;
}

/* Port of FUN_00414220 (31 B) — the castle panel: the county's castle block 0x41dedb(county, -32, 64, 0). EAX: its
 * (no caller reads it). (b). */
int rl_414220(void) { return p5_castle_block(P5_COUNTY, -32, 64, 0); }

/* ================================================================ the rankings (screen 0x20) */

/* Port of FUN_00415e42 (373 B) — a realm's score on the ranking DAT_0055ce7c: 0 for a dead realm (+4 clear); 0 the
 * byte +0x29, 1 the byte +0x4c, 2 +0x54, 3 +0x118, 4 the byte +0xc, 5 +0x10; any other ranking 2 before 1270, else 6 -
 * the byte +0x2b. (b): engine_rankings.c is not linked. */
int rl_415e42(int p)
{
    if (P5_PL8(p, 0x57bf04) == 0) return 0;
    switch ((int32_t)s_slot_0055ce7c) {
    case 0: return P5_PL8(p, 0x57bf29);
    case 1: return P5_PL8(p, 0x57bf4c);
    case 2: return P5_PL32(p, 0x57bf54);
    case 3: return P5_PL32(p, 0x57c018);
    case 4: return P5_PL8(p, 0x57bf0c);
    case 5: return P5_PL32(p, 0x57bf10);
    default: break;
    }
    if ((int32_t)s_slot_00553edc > 1269) return 6 - P5_PL8(p, 0x57bf2b);
    return 2;
}

/* Port of FUN_00415bdc (614 B) — the rankings: the leader (the LAST realm 1..5 with the highest score), each realm's
 * percentage of the leader DAT_00522c60[p] (0x404dc1), clamped 0..100; all live realms equal → DAT_00522c78 = 1 and
 * every live realm 50; else DAT_00522c94 = 1 when another live realm ties the leader. EAX: the leader. (b). */
int rl_415bdc(void)
{
    int lead = 1, best = 0;
    for (int i = 1; i < 6; i++) {
        int v = rl_415e42(i);
        if (v >= best) { best = v; lead = i; }
    }
    for (int j = 1; j < 6; j++) g_rl_522c60[j] = rl_404dc1(rl_415e42(j), best);
    int first = 999;
    g_rl_522c78 = 1;
    for (int k = 1; k < 6; k++) {
        if (g_rl_522c60[k] < 0) g_rl_522c60[k] = 0;
        if (g_rl_522c60[k] > 100) g_rl_522c60[k] = 100;
        if (P5_PL8(k, 0x57bf04) != 0) {
            if (first == 999)
                first = g_rl_522c60[k];
            else if (g_rl_522c60[k] != first)
                g_rl_522c78 = 0;
        }
    }
    if (g_rl_522c78 != 0) {
        for (int m = 1; m < 6; m++)
            if (P5_PL8(m, 0x57bf04) != 0) g_rl_522c60[m] = 50;
        return lead;
    }
    int top = g_rl_522c60[lead];
    g_rl_522c94 = 0;
    for (int n = 1; n < 6; n++)
        if (P5_PL8(n, 0x57bf04) != 0 && lead != n && g_rl_522c60[n] == top) g_rl_522c94 = 1;
    return lead;
}

/* Port of FUN_0041593b (673 B) — the rankings screen: base dir, the workforce panel off (DAT_0053f050 = 0), mode 1
 * 0x4b11ce; grtnoble.256 / grtnoble.pl8 / flags.pl8; the rankings (0x415bdc); per slot i 1..5 the realm
 * DAT_004d2b58[i]'s flag (its +0xa - 1) at (DAT_004d2b40[i], 2 (100 - pct) + 45) on a four-line pole to y 344; the
 * ranking's marker (sprite 5 at DAT_004d2b20[ranking], 371), its name (35 / ranking) and the leader's name — or 35 /
 * 7 when all equal / tied; the seal, all dirty, presented (0x452160(1)), the palette applied. EAX: 0x45240a's (no
 * caller reads it). (b). */
int rl_41593b(int a)
{
#define S3F(va) STRV(g_rl_4d3efc, 0x4d3efcu, va)
    rl_4af502();
    g_bf_53f050 = 0;
    if (a == 1) rl_4b11ce();
    rl_4af8d9(S3F(0x4d3efc), PTRV(P5_PAL), 0x300, 0);   /* "grtnoble.256" */
    rl_408fcb(S3F(0x4d3f0c), 480);                      /* "grtnoble.pl8" */
    rl_4af8d9(S3F(0x4d3f1c), P5_SHEET, 0x27100, 0);     /* "flags.pl8" */
#undef S3F
    int lead = rl_415bdc();
    for (int i = 1; i < 6; i++) {
        int p = P5_T32(g_rl_4d2b20, 0x4d2b20u, 0x4d2b58 + 4 * i);
        if (P5_PL8(p, 0x57bf04) == 0) continue;
        int x = P5_T32(g_rl_4d2b20, 0x4d2b20u, 0x4d2b40 + 4 * i);
        int y = 2 * (100 - g_rl_522c60[p]) + 45;
        gfx_sprite_render(P5_PL8(p, 0x57bf0a) - 1, x, y);
        int y2 = y + 92;
        gfx_line_draw(x + 23, y2, x + 23, 344, 16);
        gfx_line_draw(x + 24, y2, x + 24, 344, 18);
        gfx_line_draw(x + 25, y2, x + 25, 344, 20);
        gfx_line_draw(x + 26, y2, x + 26, 344, 18);
    }
    gfx_sprite_render(5, P5_T32(g_rl_4d2b20, 0x4d2b20u, 0x4d2b20 + 4 * (int32_t)s_slot_0055ce7c), 371);
    g_gfx_line_x_acc = 0;
    wrap_line(35, (int32_t)s_slot_0055ce7c, 328, 446, FONT, 63);
    if (g_rl_522c78 != 0 || g_rl_522c94 != 0)
        wrap_line(35, 7, g_gfx_line_x_acc + 330, 446, FONT, 63);
    else
        p5_text(P5_NAME(lead), g_gfx_line_x_acc + 330, 446, FONT, 63);
    p5_seal(g_gfx_canvas_width - 28, g_gfx_canvas_height - 28, 1);
    rl_45240a();
    rl_452160(1);
    rl_4b0ab5(PTRV(P5_PAL));
    return rl_45240a();
}

/* ================================================================ the merchant, the diplomacy gift */

/* Port of FUN_00415fb7 (212 B) — the merchant screen: mode 1 0x4b11ce; base dir, the workforce panel off;
 * merchant.256 / merchant.pl8 and the click grid mercgrid.pl8 (0x12d8 B into DAT_00542ce0); the seal; the deal state
 * DAT_00569500 / DAT_0055cd68 / the good DAT_0056d8ac cleared, the price step DAT_0053ef54 = 500; all dirty,
 * presented, the palette applied, the menu-redraw byte. EAX: 0x4b0ab5's (no caller reads it). (b): engine_merchant.c
 * is not linked. */
int rl_415fb7(int a)
{
#define S3F(va) STRV(g_rl_4d3efc, 0x4d3efcu, va)
    if (a == 1) rl_4b11ce();
    rl_4af502();
    g_bf_53f050 = 0;
    rl_4af8d9(S3F(0x4d3f28), PTRV(P5_PAL), 0x300, 0);          /* "merchant.256" */
    rl_408fcb(S3F(0x4d3f38), 480);                             /* "merchant.pl8" */
    rl_4af8d9(S3F(0x4d3f48), PTRV(g_bf_542ce0), 0x12d8, 0);     /* "mercgrid.pl8" */
#undef S3F
    p5_seal(g_gfx_canvas_width - 28, g_gfx_canvas_height - 28, 1);
    g_rl_569500 = 0;
    g_rl_55cd68 = 0;
    s_slot_0056d8ac = 0;
    g_rl_53ef54 = 500;
    rl_45240a();
    rl_452160(1);
    int r = rl_4b0ab5(PTRV(P5_PAL));
    g_gfx_menu_redraw_a = 1;
    return r;
}

/* Port of FUN_0041789b (197 B) — the diplomacy screen by its mode DAT_005651cc: 0 the gift (0x417960), 1..4 the
 * message pages 0x417aef(0..3), 5 / 6 0x417cef(0 / 1). EAX: the page's (no caller reads it). (b):
 * engine_diplomacy_action.c is not linked. */
int rl_41789b(void)
{
    switch (g_diplo_action_mode) {
    case 0: return rl_417960();
    case 1: return rl_417aef(0);
    case 2: return rl_417aef(1);
    case 3: return rl_417aef(2);
    case 4: return rl_417aef(3);
    case 5: return rl_417cef(0);
    case 6: return rl_417cef(1);
    default: return 0;
    }
}

/* Port of FUN_00417960 (399 B) — the gift box: the menu-redraw bytes (0, then 2; DAT_004e65f8 = 1), the uppercase
 * highlight on; a 22 x 11 bevel at (64, 160) marked dirty (23 x 12, mode 2), the seal at (376, 294), all dirty; "to"
 * (72 / 10) + the target realm DAT_00553fbc's name; 72 / 23 + the local player's gold held by the target (its record
 * +0x8c + 16 local); 72 / 18 + the gift DAT_0057a0f8 at 256; 72 / 17 at (160, 288); the highlight off. EAX: the last
 * wrap's (no caller reads it). (b). (Its 0x4b1de0 call is an empty function: elided.) */
int rl_417960(void)
{
    g_gfx_menu_redraw_a = 0;
    g_gfx_menu_redraw_a = 2;
    g_gfx_menu_redraw_b = 1;
    g_gfx_uppercase_highlight = 1;
    gfx_panel_draw_bevel(CHROME, 64, 160, 22, 11);
    rl_4527a6(64, 160, 23, 12, 2);
    p5_seal(376, 294, 0);
    rl_45240a();
    g_gfx_line_x_acc = 0;
    wrap_line(72, 10, 96, 184, FONT, 63);
    p5_text(P5_NAME((int32_t)s_slot_00553fbc), g_gfx_line_x_acc + 96, 184, FONT, 63);
    g_gfx_line_x_acc = 0;
    wrap_line(72, 23, 96, 216, FONT, 63);
    p5_num_units(P5_PL32((int32_t)s_slot_00553fbc, 0x57bf8c + 16 * P5_LOCAL), 0, g_gfx_line_x_acc + 96, 216, FONT, 63);
    wrap_line(72, 18, 96, 248, FONT, 63);
    p5_num_units((int32_t)s_slot_0057a0f8, 0, 256, 248, FONT, 63);
    wrap_line(72, 17, 160, 288, FONT, 63);
    g_gfx_uppercase_highlight = 0;
    return 0;
}

/* ================================================================ the armoury */

/* Port of FUN_004180f6 (245 B) — the armoury screen: base dir, armoury.256 / armoury.pl8, the seal at (w - 28, h -
 * 112), the racks (0x418426) and the counts (0x4181eb); outlined, the three centred captions 69 / 6, 7, 8 at x 542 (y
 * 404 / 430 / 456, width 100, colour 55); all dirty, presented, the palette applied, the menu-redraw byte. EAX:
 * 0x4b0ab5's (no caller reads it). (b): engine_ingame_input.c is not linked; engine_armory.c takes a context. */
int rl_4180f6(void)
{
    rl_4af502();
    rl_4af8d9(STRV(g_rl_4d4024, 0x4d4024u, 0x4d4024), PTRV(P5_PAL), 0x300, 0);   /* "armoury.256" */
    rl_408fcb(STRV(g_rl_4d4024, 0x4d4024u, 0x4d4030), 480);                     /* "armoury.pl8" */
    p5_seal(g_gfx_canvas_width - 28, g_gfx_canvas_height - 112, 1);
    p5_armory_racks();
    p5_armory_counts();
    g_gfx_outline = 1;
    p5_wrap_c(69, 6, 542, 404, 100, FONT, 55);
    p5_wrap_c(69, 7, 542, 430, 100, FONT, 55);
    p5_wrap_c(69, 8, 542, 456, 100, FONT, 55);
    g_gfx_outline = 0;
    rl_45240a();
    rl_452160(1);
    int r = rl_4b0ab5(PTRV(P5_PAL));
    g_gfx_menu_redraw_a = 1;
    return r;
}

/* Port of FUN_00418e2d (408 B) — the weapon assignment's numbers: the weapon's frame DAT_005aea48 of the sheet
 * DAT_0053e918 at (102, 10) (dirty 7 x 7); the local player's muster rows DAT_0053f6a0 (128 B a player, 16 B a row):
 * the weapon's stock (row w +0) in the tall font at (236, 13) with its noun 2 w + 52 over a 15 x 2 chrome fill; the
 * men who can still take it, min(row w +8 - row w +0, row 0 +0), "+ 69 / 5" at (216, 84) over a 16 x 2 fill; both
 * marked dirty. EAX: the last mark's (no caller reads it). (b): engine_armory.c takes a context. */
#define P5_MUSTER(p, w, off) AT(int32_t, g_rl_53f690, 0x53f690u, 0x53f6a0 + 128 * (p) + 16 * (w) + (off))
int rl_418e2d(void)
{
    gfx_sprite_render_from_blob(BYTES(g_rl_53e918), g_rl_5aea48, 102, 10);
    rl_4527a6(102, 10, 7, 7, 1);
    int free_ = P5_MUSTER(P5_LOCAL, g_arm_weapon, 8) - P5_MUSTER(P5_LOCAL, g_arm_weapon, 0);
    int men = P5_MUSTER(P5_LOCAL, 0, 0);
    if (men < free_) free_ = men;
    g_gfx_line_x_acc = 0;
    gfx_panel_fill_interior_chrome(CHROME, 236, 12, 15, 2);
    p5_num_units(P5_MUSTER(P5_LOCAL, g_arm_weapon, 0), 2 * g_arm_weapon + 52, 236, 13, FONT_TALL, 63);
    rl_4527a6(236, 12, 20, 3, 1);
    g_gfx_line_x_acc = 0;
    gfx_panel_fill_interior_chrome(CHROME, 216, 76, 16, 2);
    p5_label(free_, 64, STR(g_rl_4d40d4, 0x4d40d4u, 0x4d40d4), 216, 84, FONT, 63);
    wrap_line(69, 5, g_gfx_line_x_acc + 216, 84, FONT, 63);
    return rl_4527a6(216, 76, 20, 3, 1);
}

/* Port of FUN_004184c6 (397 B) — the weapon assignment screen: base dir, all dirty; the weapon DAT_00553f20's sheet
 * (1 crossbow, 2 mace, 3 sword, 4 pike, 5 bow, 6 mail, else crossbow) into DAT_0053e918; a 26 x 7 chrome panel at (96,
 * 4), the seal at (484, 88), the bevel and frame 0 at (102, 10); the numbers (0x418e2d); the menu-redraw byte. EAX:
 * 0x418e2d's (no caller reads it). (b). (Its 0x4b1de0 call is an empty function: elided.) */
int rl_4184c6(void)
{
    static const uint32_t k_sheet[7] = { 0x4d40a0u, 0x4d4044u, 0x4d4054u, 0x4d4064u, 0x4d4074u, 0x4d4084u, 0x4d4090u };
    rl_4af502();
    rl_45240a();
    int w = g_arm_weapon;
    uint32_t name = (w >= 1 && w <= 6) ? k_sheet[w] : k_sheet[0];
    rl_4af8d9(STRV(g_rl_4d4044, 0x4d4044u, name), g_rl_53e918, 0x3d090, 0);
    panel_box(96, 4, 26, 7);
    p5_seal(484, 88, 0);
    gfx_line_rect_bevel_dark_br(101, 9, 102, 102);
    gfx_sprite_render_from_blob(BYTES(g_rl_53e918), 0, 102, 10);
    int r = rl_418e2d();
    g_gfx_menu_redraw_a = 1;
    return r;
}

/* ================================================================ the shipment (screen 0x18) */

/* Port of FUN_00410a5d (76 B) — the county map at (x - 2, y + 3): the 128 x 128 raw map DAT_00569590 and county c's
 * outline (0x410cbd(c, x - 2, y + 3, 0)). EAX: 0x410cbd's (no caller reads it). (b): engine_sendsupplies.c is not
 * linked. */
int rl_410a5d(int c, int x, int y)
{
    gfx_sprite_render_raw(g_rl_569590, x - 2, y + 3, 128, 128);
    return p5_minimap_plot(c, x - 2, y + 3, 0);
}

/* Port of FUN_0041ad5d (325 B) — the shipment box: the map scene ticked, all dirty; a 22 x 22 bevel at (64, 48); the
 * target county DAT_00553c78's map (else the current county's) at (96, 104); the title (33 / 0, tall), the wrapped
 * instructions (33 / 7 at (240, 112), 176 wide, 100 lines), "from" (33 / 1) + the county's name (100 / county + 20
 * map), "to" (33 / 2) + the target's, 33 / 6 at (96, 352). EAX: the last wrap's (no caller reads it). (b). */
int rl_41ad5d(void)
{
    engine_map_scene_tick_frame();
    rl_45240a();
    gfx_panel_draw_bevel(CHROME, 64, 48, 22, 22);
    if (g_send_target != 0)
        rl_410a5d(g_send_target, 96, 104);
    else
        rl_410a5d(P5_COUNTY, 96, 104);
    wrap_line(33, 0, 80, 68, FONT_TALL, 63);
    p5_wrap_loc(33, 7, 240, 112, 176, 100, 0, 0, FONT, 63);
    wrap_line(33, 1, 240, 158, FONT, 63);
    wrap_line(100, P5_COUNTY + 20 * (int32_t)s_slot_0053f034, 240, 176, FONT, 63);
    wrap_line(33, 2, 240, 194, FONT, 63);
    wrap_line(100, g_send_target + 20 * (int32_t)s_slot_0053f034, 240, 212, FONT, 63);
    wrap_line(33, 6, 96, 352, FONT, 63);
    return 0;
}

/* Port of FUN_0041aea2 (369 B) — the shipment's goods: a 20 x 5 chrome fill at (80, 256) marked dirty (21 x 6), the
 * bevel 320 x 80; the local player's caravan order DAT_005678c0 (24 B a player): 33 / 3 + its +0 at 160 and +4 at 336
 * (the gold icon 33 at (250, 264)); 33 / 5 + its +0x10 / +0x14 (the grain icon 38 at (248, 300)). EAX: the last
 * label's (0x4ba26e discards it). (b). */
int rl_41aea2(void)
{
#define S42(va) STR(g_rl_4d4208, 0x4d4208u, va)
#define ORDER(off) AT(int32_t, g_caravan_order, 0x5678c0u, 0x5678c0 + 24 * P5_LOCAL + (off))
    gfx_panel_fill_interior_chrome(CHROME, 80, 256, 20, 5);
    rl_4527a6(80, 256, 21, 6, 1);
    gfx_line_rect_bevel_dark_br(80, 256, 320, 80);
    wrap_line(33, 3, 88, 268, FONT, 63);
    p5_label(ORDER(0), 32, S42(0x4d4208), 160, 268, FONT, 63);
    gfx_sprite_render_from_blob(MISC, 33, 250, 264);
    p5_label(ORDER(4), 32, S42(0x4d420c), 336, 268, FONT, 63);
    wrap_line(33, 5, 88, 304, FONT, 63);
    p5_label(ORDER(0x10), 32, S42(0x4d4210), 160, 304, FONT, 63);
    gfx_sprite_render_from_blob(MISC, 38, 248, 300);
    p5_label(ORDER(0x14), 32, S42(0x4d4214), 336, 304, FONT, 63);
    return 0;
#undef ORDER
#undef S42
}

/* ================================================================ the title screens */

/* Port of FUN_0041e61d (452 B) — the title backdrop by page DAT_005530f0: base dir (page 1 first 0x4b1897 / 0x4b11ce);
 * page >= 11 skirmish.256 + skircust.pl8 (DAT_0056899c) / skirmish.pl8; page 10 or < 7 gateway.256 / gateway.pl8, the
 * panel sheet grown to 220000 (0x4b25b0) when Size DAT_004eabcc is smaller, panels2.pl8 into it; 7..9 custom.256 /
 * custom.pl8; page 1 presents (0x452160(1)); the palette applied, all dirty. EAX: 0x45240a's (no caller reads it).
 * (b): the linked engine_title_render binds it as a seam (retail's), gfx.c is not linked. */
int rl_41e61d(void)
{
#define S43(va) STRV(g_rl_4d42e4, 0x4d42e4u, va)
#define SIZE AT(int32_t, g_rl_4eabcc, 0x4eabccu, 0x4eabccu)
    rl_4af502();
    if (g_rl_5530f0 == 1) {
        rl_4b1897();
        rl_4b11ce();
    }
    if (g_rl_5530f0 >= 11) {
        rl_4af8d9(S43(0x4d42e4), PTRV(P5_PAL), 0x300, 0);                    /* "skirmish.256" */
        rl_408fcb(g_rl_56899c != 0 ? S43(0x4d4304) : S43(0x4d42f4), 480);    /* "skircust.pl8" / "skirmish.pl8" */
    } else if (g_rl_5530f0 == 10) {
        rl_4af8d9(S43(0x4d4314), PTRV(P5_PAL), 0x300, 0);                    /* "gateway.256" */
        rl_408fcb(S43(0x4d4320), 480);                                       /* "gateway.pl8" */
        if (SIZE < 220000) rl_4b25b0(220000);
        rl_4af8d9(S43(0x4d432c), P5_SHEET, 0x27100, 0);                      /* "panels2.pl8" */
    } else if (g_rl_5530f0 >= 7) {
        rl_4af8d9(S43(0x4d4338), PTRV(P5_PAL), 0x300, 0);                    /* "custom.256" */
        rl_408fcb(S43(0x4d4344), 480);                                       /* "custom.pl8" */
    } else {
        rl_4af8d9(S43(0x4d4350), PTRV(P5_PAL), 0x300, 0);                    /* "gateway.256" */
        rl_408fcb(S43(0x4d435c), 480);                                       /* "gateway.pl8" */
        if (SIZE < 220000) rl_4b25b0(220000);
        rl_4af8d9(S43(0x4d4368), P5_SHEET, 0x27100, 0);                      /* "panels2.pl8" */
    }
    if (g_rl_5530f0 == 1) rl_452160(1);
    rl_4b0ab5(PTRV(P5_PAL));
    return rl_45240a();
#undef SIZE
#undef S43
}

/* Port of FUN_0041fdd6 (415 B) — an option page's drop-down list: the page DAT_00553e5c redraws first (7 the setup
 * 0x41f6c7, 8 0x41f77a, 12 0x42051c, 11 0x420630, each with the tab DAT_0055306c); then at the tab's anchor
 * (DAT_004d3158 / DAT_004d315c[3 tab]; tab 2 moves up 16 (count - 1)) a 6 x (count + 2) bevel without shadow and the
 * DAT_00553fb4 entries (103 / DAT_004d3128[tab] + i, centred in 96 at 16 px a row; the hovered DAT_00553114 in 249);
 * marked dirty. EAX: the mark's (no caller reads it). (b): engine_title_render.c draws it through a context. */
int rl_41fdd6(void)
{
    if (g_rl_553e5c == 7) p5_title_setup(g_tr_p9_tab);
    else if (g_rl_553e5c == 8) p5_title_41f77a(g_tr_p9_tab);
    else if (g_rl_553e5c == 12) rl_42051c(g_tr_p9_tab);
    else if (g_rl_553e5c == 11) p5_title_420630(g_tr_p9_tab);
    int tab = g_tr_p9_tab;
    int x = P5_T32(g_rl_4d3128, 0x4d3128u, 0x4d3158 + 12 * tab);
    int y = P5_T32(g_rl_4d3128, 0x4d3128u, 0x4d315c + 12 * g_tr_p9_tab);
    if (g_tr_p9_tab == 2) y -= 16 * (g_tr_p9_count - 1);
    int rows = g_tr_p9_count + 2;
    g_gfx_no_shadow = 1;
    gfx_panel_draw_bevel(CHROME, x, y, 6, rows);
    for (int i = 0; i < g_tr_p9_count; i++) {
        int k = P5_T32(g_rl_4d3128, 0x4d3128u, 0x4d3128 + 4 * g_tr_p9_tab) + i;
        p5_wrap_c(103, k, x + 1, 16 * i + y + 16, 96, FONT, i + 1 == g_tr_selected_button ? 249 : 63);
    }
    g_gfx_no_shadow = 0;
    return rl_4527a6(x, y, 6, rows, 1);
}

/* ================================================================ the standalone's last seams (2026-11-16)
 * Retail entries the port reached only through engine_retail_call / an address (prove_no_retail_code --gaps-only):
 * the DirectDraw restore / paint / error paths, the debug overlays, two screens, the demo sheet, two confirm
 * callbacks, the MP error / leave paths. None is reached by the corpus (docs/findings/rl-ui4.md §Gap ports). */

/* other partitions' globals (declared here as in their headers) */
extern int32_t g_rl_4e65d0;          /* DAT_004e65d0 (rl_misc_globals.h): the app is active */
extern int32_t g_rl_52effc;          /* DAT_0052effc (rl_turn_globals.h): the year index */
extern int32_t g_rl_553008;          /* DAT_00553008 (rl_ctl_globals.h): the "dumb view" banner's countdown */
extern int32_t g_rl_55324c;          /* DAT_0055324c (rl_skirmish_globals.h): a record pointer */
extern int32_t g_rl_5681e8;          /* DAT_005681e8 (rl_skirmish_globals.h): a loop bound */

#define GAP_PAL   g_rl_4ea8a0                                   /* DAT_004ea8a0: the screen's palette (0x300 B) */
#define GAP_SMALL ((const uint8_t *)g_rl_5cbfb0)                /* &DAT_005cbfb0: the small (debug) font */
#define GAP_SMK   ((uint32_t)g_rl_4d4d58)                       /* DAT_004d4d58: the open Smack* */
#define GAP_SMKF(o) (*(volatile uint32_t *)(uintptr_t)(GAP_SMK + (o)))
typedef HRESULT (WINAPI *gap_restore_t)(uint32_t s);
typedef HRESULT (WINAPI *gap_bltfast_t)(uint32_t s, int x, int y, uint32_t src, void *rect, uint32_t fl);
typedef HRESULT (WINAPI *gap_blt_t)(uint32_t s, void *dst, uint32_t src, void *rect, uint32_t fl, void *fx);

/* Port of FUN_0040902b (148 B) — the demo screen's sheet: FileName (a .pl8) into the back buffer Buf at offset 24
 * (0x4b000 B), the palette file into DAT_004ea8a0 (0x300 B); then the screen dirty, 0x452160(1), the palette
 * applied (0x4b0ab5). 0 when Buf is null or a read fails; else 1. */
int rl_40902b(int name, int pal)
{
    if (g_gfx_canvas_pixels == NULL)
        return 0;
    if (rl_4af8d9(name, PTRV(g_gfx_canvas_pixels), 0x4b000, 24) == 0)
        return 0;
    if (rl_4af8d9(pal, PTRV(GAP_PAL), 0x300, 0) == 0)
        return 0;
    rl_45240a();
    rl_452160(1);
    rl_4b0ab5(PTRV(GAP_PAL));
    return 1;
}

/* Port of FUN_0041e5d0 (77 B) — screen 0x45's backdrop: chdir to the base, lom.256 → DAT_004ea8a0, lom.pl8 as the
 * 480-row background (0x408fcb), the palette applied, the screen dirty (its EAX). */
int rl_41e5d0(void)
{
    rl_4af502();
    rl_4af8d9(STRV(g_rl_4d42d4, 0x4d42d4u, 0x4d42d4u), PTRV(GAP_PAL), 0x300, 0);
    rl_408fcb(STRV(g_rl_4d42d4, 0x4d42d4u, 0x4d42dcu), 480);
    rl_4b0ab5(PTRV(GAP_PAL));
    return rl_45240a();
}

/* Port of FUN_00420428 (244 B) — title page 10: the stone panel (sheet DAT_004eabec, 80, 10, 30 x 19), string
 * (11, 16) centred in the tall font (capitals highlighted: DAT_0058fe2c), then without shadows (DAT_005aea40) the
 * wrapped strings (11, 17) / (11, 18), the line (11, 48) in colour 1 and the wrapped (11, 49). */
int rl_420428(void)
{
    g_gfx_uppercase_highlight = 1;
    gfx_panel_draw(g_gfx_current_blob, 80, 10, 30, 19);
    p5_wrap_c(11, 16, 80, 36, 480, FONT_TALL, 63);
    g_gfx_uppercase_highlight = 0;
    g_gfx_no_shadow = 1;
    p5_wrap_loc(11, 17, 128, 0x48, 0x180, 100, 0, 0, FONT, 63);
    p5_wrap_loc(11, 18, 128, 0x78, 0x180, 100, 0, 0, FONT, 63);
    wrap_line(11, 48, 128, 220, FONT, 1);
    p5_wrap_loc(11, 49, 128, 0xf0, 0x180, 100, 0, 0, FONT, 63);
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_004247f5 (174 B) — the "DUMB VIEW ON." banner (the frame driver, DAT_0056d650 set): DAT_00553008
 * counts down; at <= 0 (reset to 100 below 0) a 10 x 22 block of colour 24 at (176, 220), its 249 frame, the text. */
int rl_4247f5(void)
{
    if (--g_rl_553008 > 0)
        return 0;
    if (g_rl_553008 < 0)
        g_rl_553008 = 100;
    g_gfx_glyph_w = 10;
    g_gfx_glyph_height_short = 22;
    gfx_sprite_fill_block(176, 220, 24);
    gfx_line_rect(175, 219, 16 * g_gfx_glyph_w + 2, g_gfx_glyph_height_short + 2, 249);
    p5_text((const uint8_t *)STR(g_rl_4d4544, 0x4d4544u, 0x4d4544u), 192, 226, FONT, 63);
    return 0;
}

/* Port of FUN_0042563c (349 B) — the territory hex dump (debug): a 10 x 384 block of colour 34 at (480, 24) marked
 * dirty, "piece" + territory DAT_00553e68 in decimal after it (the line's x advance DAT_005cd404), then its 420-byte
 * record as 2-digit hex, 10 per row (DAT_0055324c the record, DAT_005681e8 = 420 the bound, both retail globals),
 * small font, no shadows. */
int rl_42563c(void)
{
    g_gfx_glyph_w = 10;
    g_gfx_glyph_height_short = 384;
    gfx_sprite_fill_block(480, 24, 34);
    rl_4527a6(480, 24, 10, 24, 1);
    g_gfx_no_shadow = 1;
    g_gfx_line_x_acc = 0;
    p5_text((const uint8_t *)STR(g_rl_4d4710, 0x4d4710u, 0x4d4710u), 484, 26, GAP_SMALL, 63);
    p5_label(g_bf_553e68, 32, STR(g_rl_4d4710, 0x4d4710u, 0x4d4718u), g_gfx_line_x_acc + 484, 26, GAP_SMALL, 63);
    g_rl_55324c = PTRV(&AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b0 + 420 * g_bf_553e68));
    g_rl_5681e8 = 420;
    for (int i = 0; i < g_rl_5681e8; i++)
        gfx_text_label_render_hex(*(const uint8_t *)(uintptr_t)(uint32_t)(i + g_rl_55324c), 32,
                                  STR(g_rl_4d4710, 0x4d4710u, 0x4d471cu), 16 * (i % 10) + 480, 8 * (i / 10) + 36,
                                  GAP_SMALL, GLYPHS, 63);
    g_gfx_no_shadow = 0;
    return 0;
}

/* Port of FUN_00425a6a (158 B) — screen 0x44: the bevel panel (80, 176, 20 x 10), the screen dirty, strings
 * (89, 0) in the tall font, (89, 1) + the year DAT_0052effc + 1, (89, 2). */
int rl_425a6a(void)
{
    gfx_panel_draw_bevel(CHROME, 80, 176, 20, 10);
    rl_45240a();
    wrap_line(89, 0, 112, 200, FONT_TALL, 63);
    wrap_line(89, 1, 128, 244, FONT, 63);
    p5_label(g_rl_52effc + 1, 32, STR(g_rl_4d4750, 0x4d4750u, 0x4d4750u), 288, 242, FONT, 63);
    wrap_line(89, 2, 144, 292, FONT, 63);
    return 0;
}

static const uint32_t k_dd_err[99] = {   /* [i] = the HRESULT whose index is i + 1 */
    0x88760005u, 0x8876000au, 0x88760014u, 0x88760028u, 0x88760037u, 0x80004005u, 0x8876005au, 0x8876005fu,
    0x88760064u, 0x8876006eu, 0x88760078u, 0x88760082u, 0x80070057u, 0x88760091u, 0x88760096u, 0x887600a0u,
    0x887600aau, 0x887600b4u, 0x887600cdu, 0x887600d2u, 0x887600d4u, 0x887600d7u, 0x887600dcu, 0x887600deu,
    0x887600e1u, 0x887600e6u, 0x887600f0u, 0x887600fau, 0x887600ffu, 0x88760104u, 0x88760118u, 0x88760122u,
    0x88760136u, 0x8876013cu, 0x8876013du, 0x88760140u, 0x8876014au, 0x8876014fu, 0x88760154u, 0x8876015eu,
    0x88760168u, 0x8007000eu, 0x8876017cu, 0x8876017eu, 0x88760180u, 0x88760183u, 0x88760190u, 0x8876019au,
    0x887601a4u, 0x887601aeu, 0x887601b3u, 0x887601b8u, 0x887601c2u, 0x887601ccu, 0x887601d6u, 0x887601e0u,
    0x887601eau, 0x80004001u, 0x887601feu, 0x88760208u, 0x88760219u, 0x8876021cu, 0x88760230u, 0x88760231u,
    0x88760232u, 0x88760233u, 0x88760234u, 0x88760235u, 0x88760236u, 0x88760237u, 0x88760238u, 0x88760239u,
    0x8876023au, 0x8876023bu, 0x8876023cu, 0x8876023du, 0x8876023eu, 0x8876023fu, 0x88760240u, 0x88760241u,
    0x88760242u, 0x88760243u, 0x88760244u, 0x88760245u, 0x88760246u, 0x88760247u, 0x88760248u, 0x88760249u,
    0x8876024au, 0x8876024bu, 0x8876024cu, 0x8876024du, 0x8876024eu, 0x8876024fu, 0x88760250u, 0x8876026cu,
    0x88760280u, 0x88760294u, 0x887602a8u,
};
static const uint32_t k_ds_err[14] = {   /* [i] = the HRESULT whose index is i + 1 */
    0x8878000au, 0x8878001eu, 0x80070057u, 0x88780032u, 0x80004005u, 0x88780046u, 0x8007000eu, 0x88780064u,
    0x80004001u, 0x88780078u, 0x88780082u, 0x80040110u, 0x88780096u, 0x887800a0u,
};

/* Port of FUN_00427d72 (340 B) — a DirectSound HRESULT's index into the error names (1..14; 0 unknown). */
int rl_427d72(int hr)
{
    for (int i = 0; i < (int)(sizeof k_ds_err / sizeof *k_ds_err); i++)
        if ((uint32_t)hr == k_ds_err[i]) return i + 1;
    return 0;
}

/* Port of FUN_0042f60e (2298 B) — a DirectDraw HRESULT's index into the error names (1..99; 0 unknown). */
int rl_42f60e(int hr)
{
    for (int i = 0; i < (int)(sizeof k_dd_err / sizeof *k_dd_err); i++)
        if ((uint32_t)hr == k_dd_err[i]) return i + 1;
    return 0;
}

/* Port of FUN_00428078 (113 B) — county k has a cell flagged only bit 1 within radius 1..3 of its (col, row)
 * (+0x6c / +0x6d: 0x46d130). 1 / 0. */
int rl_428078(int k)
{
    for (int i = 1; i < 4; i++)
        if (rl_46d130(P5_CO8(k, 0x53fa1cu), P5_CO8(k, 0x53fa1du), i) != 0)
            return 1;
    return 0;
}

/* Port of FUN_0042e3ba (164 B) — the movie's WM_PAINT: BeginPaint, a BLACKNESS PatBlt of the movie's size (Smack +4
 * / +8) while one plays (DAT_004df28c), EndPaint; with a frame shown (DAT_005169b8) Smack #17(0), #27(that frame),
 * #17(1) (its EAX), else EndPaint's. */
int rl_42e3ba(int hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = rl_plat_BeginPaint((HWND)(uintptr_t)hwnd, &ps);
    if (g_rl_4df28c != 0)
        rl_plat_PatBlt(dc, 0, 0, (int)GAP_SMKF(4), (int)GAP_SMKF(8), 0x42);
    int r = rl_plat_EndPaint((HWND)(uintptr_t)hwnd, &ps);
    if (g_rl_4df28c != 0 && g_rl_5169b8 != 0) {
        rl_plat_Smack17(GAP_SMK, 0);
        rl_plat_Smack27(GAP_SMK, (uint32_t)g_rl_5169b8);
        return (int)rl_plat_Smack17(GAP_SMK, 1);
    }
    return r;
}

/* the movie text box / the whole frame onto the primary: BltFast (DDBLTFAST_WAIT) at the client origin
 * (DAT_004ea180) full screen, else Blt (0x1000200) to the client rect; the HRESULT */
static int gap_present(int x, int y, int w, int h)
{
    int32_t src[4] = { x, y, w + x, h + y };
    int32_t ox = AT(int32_t, g_rl_4ea180, 0x4ea180u, 0x4ea180u), oy = AT(int32_t, g_rl_4ea180, 0x4ea180u, 0x4ea184u);
    int32_t dst[4] = { x + ox, y + oy, w + x + ox, h + y + oy };
    if (DD_FULL != 0)
        return (int)((gap_bltfast_t)VT(DD_PRIMARY, 0x1c))(DD_PRIMARY, dst[0], dst[1], (uint32_t)DD_BACK, src, 16);
    return (int)((gap_blt_t)VT(DD_PRIMARY, 0x14))(DD_PRIMARY, dst, (uint32_t)DD_BACK, src, 0x1000200u, NULL);
}
/* Port of FUN_0042e45e (175 B) — present the rect (x, y, w, h) of the composition surface (gap_present). */
int rl_42e45e(int x, int y, int w, int h) { return gap_present(x, y, w, h); }
/* Port of FUN_0042e50d (179 B) — present the whole 640 x 480 frame (gap_present). */
int rl_42e50d(void) { return gap_present(0, 0, 640, 480); }

/* Port of FUN_0042f449 (323 B) — the windowed palette pPalEntries DAT_00515d90: the 10 static colours at each end
 * (0..9, 246..255) copied back into the screen palette DAT_004ea1b0 (flags 0), 10..245 from it (flags 4:
 * PC_NOCOLLAPSE). EAX 255. */
int rl_42f449(void)
{
    uint8_t *e = g_rl_515d90;
    for (int i = 0; i < 10; i++) {
        g_rl_4ea1b0[3 * i] = e[4 * i]; g_rl_4ea1b0[3 * i + 1] = e[4 * i + 1]; g_rl_4ea1b0[3 * i + 2] = e[4 * i + 2];
        e[4 * i + 3] = 0;
    }
    for (int j = 10; j < 246; j++) {
        e[4 * j] = g_rl_4ea1b0[3 * j]; e[4 * j + 1] = g_rl_4ea1b0[3 * j + 1]; e[4 * j + 2] = g_rl_4ea1b0[3 * j + 2];
        e[4 * j + 3] = 4;
    }
    for (int k = 246; k < 256; k++) {
        g_rl_4ea1b0[3 * k] = e[4 * k]; g_rl_4ea1b0[3 * k + 1] = e[4 * k + 1]; g_rl_4ea1b0[3 * k + 2] = e[4 * k + 2];
        e[4 * k + 3] = 0;
    }
    return 255;
}

/* Port of FUN_0043b101 (68 B) — a confirm popup's yes (0x43b06d → 0x40e6f2): the screen dirty, redraw 2; the map
 * (screen 0) + the below-status gate DAT_0053e9ac when the router's param DAT_0059154c is 1, else screen 24. */
int rl_43b101(void)
{
    int r = rl_45240a();
    g_bf_57d340 = 2;
    if (PARAM(0) == 1) {
        engine_set_current_screen(0);
        DIRTY->below_status_redraw = 1;
    } else {
        engine_set_current_screen(24);
    }
    return r;
}

/* Port of FUN_0043baf9 (111 B) — the battle confirm's yes (0x43ba8f, confirm 11): back to screen DAT_0057c90c,
 * DAT_00553ed8 = 1; with a battle pending (DAT_0059151c) DAT_005653f4 = 2 and message 62 to the peers (MP) or
 * 0x43bbd7(local player, 2). */
int rl_43baf9(void)
{
    engine_set_current_screen((uint8_t)g_bf_57c90c);
    g_bf_553ed8 = 1;
    uint8_t b = AT(uint8_t, g_bf_59151c, 0x59151cu, 0x59151cu);
    if (b == 0)
        return b;
    s_slot_005653f4 = 2;
    if (g_sim_mp_active != 0)
        return rl_43eda0(62, 0);
    return bf_43bbd7((int)s_slot_0057c8cc, (int)s_slot_005653f4);
}

/* Port of FUN_0043e668 (58 B) — a network error: log "ERR:Network:" + a1, 0x4b7180(1), WM_CLOSE to the window. */
int rl_43e668(int a1)
{
    rl_4afab9(STRV(g_rl_4d5d60, 0x4d5d60u, 0x4d5d60u), a1, 0);
    rl_4b7180(1);
    return rl_plat_PostMessageA((HWND)(uintptr_t)HWNDMAIN, 0x10, 0, 0);
}

/* Port of FUN_0049bb9d (75 B) — a seat left: realm p's +5 byte (DAT_0057bf05), its aux +0x27 byte and +0 dword
 * cleared. EAX 44 p. */
int rl_49bb9d(int p)
{
    AT(uint8_t, g_player_strategic_352, 0x57bf00u, 0x57bf05 + 352 * p) = 0;
    AT(uint8_t, g_player_aux_44, 0x553d50u, 0x553d77 + 44 * p) = 0;
    AT(int32_t, g_player_aux_44, 0x553d50u, 0x553d50 + 44 * p) = 0;
    return 44 * p;
}

/* Port of FUN_004b2234 (36 B) — five calls of the empty 0x4b21cf / 0x4b21da / 0x4b21e5 (retail's leftovers): nothing. */
int rl_4b2234(void) { return 0; }

/* Port of FUN_004b26b8 (64 B) — minimized: log "OK :Paused.", DAT_004e65d0 = 1 then 0 (retail's two stores), the
 * window invalidated. */
int rl_4b26b8(void)
{
    rl_4afab9(STRV(g_rl_4df648, 0x4df648u, 0x4df648u), 0, 0);
    *(volatile int32_t *)&g_rl_4e65d0 = 1;
    *(volatile int32_t *)&g_rl_4e65d0 = 0;
    return rl_plat_InvalidateRect((HWND)(uintptr_t)HWNDMAIN, NULL, TRUE);
}

/* Port of gfx_restore FUN_004b2730 (143 B) — Restore the primary DAT_004eabd0, then the composition surface
 * DAT_004eafac: 1 when both succeed; else the failure logged (front / back), 0. A null surface counts as a failure. */
int rl_4b2730(void)
{
    if (DD_PRIMARY == 0 || ((gap_restore_t)VT(DD_PRIMARY, 0x6c))(DD_PRIMARY) != 0) {
        rl_4afab9(STRV(g_rl_4df668, 0x4df668u, 0x4df668u), 0, 0);
        return 0;
    }
    if (DD_BACK == 0 || ((gap_restore_t)VT(DD_BACK, 0x6c))((uint32_t)DD_BACK) != 0) {
        rl_4afab9(STRV(g_rl_4df668, 0x4df668u, 0x4df68cu), 0, 0);
        return 0;
    }
    return 1;
}

/* Port of FUN_004b74ce (70 B) — leave the MP session: DebugBreak when DAT_00568464 is clear (retail's assert), then
 * the transport's leave 0x4b6ab6 (this = DAT_004e2a30), or DebugBreak when there is none. */
int rl_4b74ce(void)
{
    if (g_rl_568464 == 0)
        rl_plat_DebugBreak();
    if (g_rl_4e2a30 != 0)
        rl_4b6ab6((uint32_t)g_rl_4e2a30);
    else
        rl_plat_DebugBreak();
    return 0;
}
