/*
 * OpenLords2 — strategic-map territory-info panel (see engine_hud_panel.h).
 *
 * Ports FUN_0040f7d3 + FUN_0040fec1 (resource-queue builder) + the eight
 * resource subdrawers FUN_004100af / 41023a / 4103c5 / 410502 / 410598 /
 * 41062e / 4106c4 / 4107d1 (and the trivial 41ac3e wrapper).
 *
 * All render leaves are already ported; this module is pure composition,
 * faithful to retail's draw order and the per-record field reads.
 */

#include "engine_hud_panel.h"

#include <stddef.h>
#include <string.h>

#include "gfx_sprite_render.h"   /* gfx_sprite_render_from_blob (40a21a),
                                  * gfx_sprite_render_opaque_from_blob (40a567) */
#include "gfx_text.h"            /* gfx_text_set_no_shadow / _tight_shadow /
                                  * _outline; gfx_text_render_line_centered (4025d7) */
#include "gfx_text_label.h"      /* render_dec (402f64) / _dec_centered (4030c6) /
                                  * _labeled_signed (402e0c) */
#include "gfx_wrap.h"            /* gfx_wrap_localized_line (402d37) /
                                  * _centered (402c5e) */

/* ----------------------------------------------------------------- */
/* Per-record field offsets, relative to g_per_army_768 base (retail
 * DAT_0053f9b0).  Each retail symbol &DAT_0053f9XX maps to (0x...XX -
 * 0x53f9b0); i32 reads are little-endian. */

#define REC_STRIDE      0x300

#define OFF_OWNER       0x005   /* DAT_0053f9b5  byte  owning realm        */
#define OFF_XOFF        0x008   /* DAT_0053f9b8  s8    castle x divisor    */
#define OFF_FLAGID      0x009   /* DAT_0053f9b9  s8    flag/shield index   */
#define OFF_APPROVAL    0x00c   /* DAT_0053f9bc  s8    approval %          */
#define OFF_POP         0x024   /* DAT_0053f9d4  i32   population total    */
#define OFF_RATION      0x0b9   /* DAT_0053fa69  byte  ration %            */
#define OFF_CASTLE      0x124   /* DAT_0053fad4  i32   castle-present gate  */
#define OFF_TAX         0x15d   /* DAT_0053fb0d  s8    tax rate            */
#define OFF_TAX2        0x15e   /* DAT_0053fb0e  byte  tax companion       */

/* The label/suffix strings the panel passes (retail DAT_004d3d34..84):
 * all single spaces except the ration percent sign.  The resource TYPE
 * is shown by the icon sprite, not text, so these are pure spacing. */
static const char  LBL_SPACE[] = " ";
static const char  LBL_PCT[]   = "%";

/* ----------------------------------------------------------------- */
/* Render context — mirrors the globals the retail subdrawers read
 * directly (DAT_0057c93c via `rec`, the fonts, the chrome blob, the
 * localization walker), threaded explicitly here. */
typedef struct {
    const uint8_t *rec;            /* army_block + territory*0x300 */
    const uint8_t *chrome;         /* DAT_005530c8 misc_cty */
    const uint8_t *font_name;      /* DAT_005c9a90 fntl2_9 */
    const uint8_t *font_res;       /* DAT_005aeba0 font_10 */
    const uint8_t *font_menu;      /* DAT_005af8f0 fntl2_14 */
    const uint8_t *glyph;          /* DAT_004d71f0 render map */
    const unsigned char *metrics;  /* measure map */
    localization_state_t *loc;

    /* Resource queues (retail DAT_0053f690 / DAT_00553fd0) + counts
     * (DAT_00553c60 / DAT_005440b4), row indices (DAT_00553038 /
     * DAT_0056d68c) and row heights (DAT_0053e970 / DAT_0053f04c). */
    int queue1[4], queue2[4];
    int count1, count2;
    int row1, row2;
    int height1, height2;
} panel_ctx_t;

static int32_t rec_i32(const uint8_t *rec, int off)
{
    int32_t v;
    memcpy(&v, rec + off, sizeof v);
    return v;
}
static int rec_s8(const uint8_t *rec, int off)  { return (int8_t)rec[off]; }
static int rec_u8(const uint8_t *rec, int off)  { return rec[off]; }

/* ----------------------------------------------------------------- */
/* FUN_0040fec1 — build the two resource-type queues from the record's
 * resource-present flags. */
static void panel_build_queues(panel_ctx_t *c)
{
    const uint8_t *rec = c->rec;
    for (int i = 0; i < 4; i++) {
        c->queue2[i] = 0;
        c->queue1[i] = 0;
    }
    c->count2 = 0;
    c->count1 = 0;

    /* Queue 1 (food/wealth column). */
    if (rec_u8(rec, 0x200) != 0 || rec_i32(rec, 0x250) != 0) {
        c->queue1[0] = 1;
        c->count1 = 1;
    }
    if (rec_u8(rec, 0x201) != 0 || rec_i32(rec, 0x224) != 0) {
        c->queue1[c->count1] = 0;
        c->count1++;
    }
    if (rec_u8(rec, 0x204) != 0) {
        c->queue1[c->count1] = 2;
        c->count1++;
    }

    /* Queue 2 (materials column). */
    if (rec_u8(rec, 0x297) != 0) { c->queue2[c->count2] = 6; c->count2++; }
    if (rec_u8(rec, 0x2af) != 0) { c->queue2[c->count2] = 4; c->count2++; }
    if (rec_u8(rec, 0x2df) != 0) { c->queue2[c->count2] = 5; c->count2++; }
    if (rec_u8(rec, 0x2c7) != 0) { c->queue2[c->count2] = 7; c->count2++; }
    if (rec_u8(rec, 0x1c3) != 0) { c->queue2[c->count2] = 3; c->count2++; }
}

/* ----------------------------------------------------------------- */
/* Queue-1 subdrawers (row index c->row1, height c->height1). */

/* FUN_004100af — type 1. */
static void panel_draw_q1_type1(panel_ctx_t *c)
{
    const uint8_t *rec = c->rec;
    int y = c->height1 * c->row1;
    gfx_text_set_tight_shadow(1);
    if (rec_i32(rec, 0xd8) < rec_i32(rec, 0xd0))
        gfx_sprite_render_from_blob(c->chrome, 0x4c, 0x1e2, y + 0x131);
    else if (rec_i32(rec, 0xd0) < rec_i32(rec, 0xd4))
        gfx_sprite_render_from_blob(c->chrome, 0x29, 0x1e2, y + 0x131);
    else
        gfx_sprite_render_from_blob(c->chrome, 0x26, 0x1e4, y + 0x133);
    gfx_text_label_render_labeled_signed(rec_i32(rec, 0x258), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x204, y + 0x139,
        c->font_res, c->glyph, 0xfa, 0xf9);
    gfx_text_set_tight_shadow(0);
    gfx_text_set_no_shadow(1);
    gfx_text_label_render_dec_centered(rec_i32(rec, 0x250), ' ', LBL_SPACE,
        0x1e0, y + 0x14d, 0x3c, c->font_menu, c->glyph, c->metrics, 0x3f);
    c->row1++;
    gfx_text_set_no_shadow(0);
}

/* FUN_0041023a — type 0. */
static void panel_draw_q1_type0(panel_ctx_t *c)
{
    const uint8_t *rec = c->rec;
    int y = c->height1 * c->row1;
    gfx_text_set_tight_shadow(1);
    if (rec_i32(rec, 0xcc) < rec_i32(rec, 0xc4))
        gfx_sprite_render_from_blob(c->chrome, 0x4b, 0x1e4, y + 0x12d);
    else if (rec_i32(rec, 0xc4) < rec_i32(rec, 0xc8))
        gfx_sprite_render_from_blob(c->chrome, 0x23, 0x1e5, y + 0x12e);
    else
        gfx_sprite_render_from_blob(c->chrome, 0x21, 0x1e6, y + 0x12f);
    gfx_text_label_render_labeled_signed(rec_i32(rec, 0x22c), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x204, y + 0x139,
        c->font_res, c->glyph, 0xfa, 0xf9);
    gfx_text_set_tight_shadow(0);
    gfx_text_set_no_shadow(1);
    gfx_text_label_render_dec_centered(rec_i32(rec, 0x224), ' ', LBL_SPACE,
        0x1e0, y + 0x14d, 0x3c, c->font_menu, c->glyph, c->metrics, 0x3f);
    c->row1++;
    gfx_text_set_no_shadow(0);
}

/* FUN_004103c5 — type 2. */
static void panel_draw_q1_type2(panel_ctx_t *c)
{
    const uint8_t *rec = c->rec;
    int y = c->height1 * c->row1;
    gfx_text_set_tight_shadow(1);
    if (rec_i32(rec, 0xe4) < rec_i32(rec, 0xdc))
        gfx_sprite_render_from_blob(c->chrome, 0x4d, 0x1eb, y + 0x130);
    else
        gfx_sprite_render_from_blob(c->chrome, 0x3f, 0x1ed, y + 0x132);
    gfx_text_label_render_labeled_signed(rec_i32(rec, 0x20c), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x204, y + 0x133,
        c->font_res, c->glyph, 0xfa, 0xf9);
    if (rec_i32(rec, 0x214) != 0)
        gfx_text_label_render_dec(rec_i32(rec, 0x214), ' ', LBL_SPACE,
            0x20a, y + 0x143, c->font_res, c->glyph, 0xfa);
    c->row1++;
    gfx_text_set_tight_shadow(0);
}

/* ----------------------------------------------------------------- */
/* Queue-2 subdrawers (row index c->row2, height c->height2). */

/* FUN_00410502 — type 4. */
static void panel_draw_q2_type4(panel_ctx_t *c)
{
    int y = c->height2 * c->row2;
    gfx_text_set_tight_shadow(1);
    gfx_sprite_render_from_blob(c->chrome, 0x2c, 600, y + 0x133);
    gfx_text_label_render_labeled_signed(rec_i32(c->rec, 0x2c0), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x22c, y + 0x139,
        c->font_res, c->glyph, 0xfa, 0xf9);
    c->row2++;
    gfx_text_set_tight_shadow(0);
}

/* FUN_00410598 — type 5. */
static void panel_draw_q2_type5(panel_ctx_t *c)
{
    int y = c->height2 * c->row2;
    gfx_text_set_tight_shadow(1);
    gfx_sprite_render_from_blob(c->chrome, 0x2d, 0x252, y + 0x133);
    gfx_text_label_render_labeled_signed(rec_i32(c->rec, 0x2f0), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x22c, y + 0x139,
        c->font_res, c->glyph, 0xfa, 0xf9);
    c->row2++;
    gfx_text_set_tight_shadow(0);
}

/* FUN_0041062e — type 6. */
static void panel_draw_q2_type6(panel_ctx_t *c)
{
    int y = c->height2 * c->row2;
    gfx_text_set_tight_shadow(1);
    gfx_sprite_render_from_blob(c->chrome, 0x2e, 0x252, y + 0x133);
    gfx_text_label_render_labeled_signed(rec_i32(c->rec, 0x2a8), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x22c, y + 0x139,
        c->font_res, c->glyph, 0xfa, 0xf9);
    c->row2++;
    gfx_text_set_tight_shadow(0);
}

/* FUN_004106c4 — type 7. */
static void panel_draw_q2_type7(panel_ctx_t *c)
{
    const uint8_t *rec = c->rec;
    int y = c->height2 * c->row2;
    gfx_text_set_tight_shadow(1);
    if (rec_i32(rec, 0x120) < rec_i32(rec, 0x118))
        gfx_sprite_render_from_blob(c->chrome, rec_u8(rec, 0x290) + 0x4f,
                                    0x256, y + 0x131);
    else
        gfx_sprite_render_from_blob(c->chrome, rec_u8(rec, 0x290) + 0x30,
                                    600, y + 0x133);
    gfx_text_label_render_labeled_signed(rec_i32(rec, 0x2d8), 0,
        (const uint8_t *)LBL_SPACE, LBL_SPACE, 0x22c, y + 0x139,
        c->font_res, c->glyph, 0xfa, 0xf9);
    c->row2++;
    gfx_text_set_tight_shadow(0);
}

/* FUN_004107d1 — type 3 (troops/garrison; uses 41ac3e wrapper). */
static void panel_draw_q2_type3(panel_ctx_t *c)
{
    const uint8_t *rec = c->rec;
    int local_c = (c->row2 < 2) ? 6 : 0;
    int y = c->height2 * c->row2 + local_c;
    gfx_text_set_tight_shadow(1);
    if (rec_u8(rec, 0x1b0) != 0) {
        if (rec_i32(rec, 0xf0) < rec_i32(rec, 0xe8))
            gfx_sprite_render_from_blob(c->chrome, 0x4e, 0x255, y + 0x129);
        else
            gfx_sprite_render_from_blob(c->chrome, 0x40, 0x25b, y + 300);

        unsigned v = rec_u8(rec, 0x1a6);
        if (rec_i32(rec, 0x1d0) == 0 && rec_i32(rec, 0x1d4) == 0) {
            if (v != 0) {
                gfx_text_label_render_dec((int)v, ' ', LBL_SPACE,
                    0x23c, y + 0x13a, c->font_res, c->glyph, 0xfa);
                /* FUN_0041ac3e(v, 0x42, 0x234, y+0x146, font_name, 0xfa):
                 * loc group 8, sub = (v==1 ? 0x42 : 0x43). */
                gfx_wrap_localized_line(c->loc, 8, (v == 1) ? 0x42 : 0x43,
                    0x234, y + 0x146, c->font_name, c->glyph, 0xfa);
            }
        } else {
            if (rec_i32(rec, 0x1d0) == 0 || rec_i32(rec, 0x1d4) == 0) {
                if (rec_i32(rec, 0x1d0) == 0)
                    gfx_sprite_render_from_blob(c->chrome, 0x1a, 0x23c, y + 0x137);
                else
                    gfx_sprite_render_from_blob(c->chrome, 0x19, 0x23c, y + 0x137);
            } else {
                gfx_sprite_render_from_blob(c->chrome, 0x1b, 0x23c, y + 0x131);
            }
            gfx_wrap_localized_line(c->loc, 0x47, 0x12, 0x234, y + 0x146,
                c->font_name, c->glyph, 0xfa);
        }
    }
    c->row2++;
    gfx_text_set_tight_shadow(0);
}

/* ----------------------------------------------------------------- */
/* FUN_0040f7d3 (param_1 == 1 forced-redraw path). */
void engine_hud_panel_render(const engine_hud_panel_state_t *st,
                             const uint8_t *chrome,
                             const uint8_t *font_name,
                             const uint8_t *font_res,
                             const uint8_t *font_menu,
                             const uint8_t *glyph_table,
                             const unsigned char *glyph_metrics,
                             localization_state_t *loc)
{
    if (!st || !st->army_block) {
        return;
    }

    int terr = st->current_territory;
    const uint8_t *rec = st->army_block + terr * REC_STRIDE;
    int name_sub = st->map_id * 0x14 + terr;

    panel_ctx_t c;
    c.rec = rec;
    c.chrome = chrome;
    c.font_name = font_name;
    c.font_res = font_res;
    c.font_menu = font_menu;
    c.glyph = glyph_table;
    c.metrics = glyph_metrics;
    c.loc = loc;

    if (rec_u8(rec, OFF_OWNER) == st->current_realm) {
        /* ---- OWNED branch ---- */
        gfx_sprite_render_opaque_from_blob(chrome, 0x37, 0x1de, 0x9c);
        gfx_sprite_render_opaque_from_blob(chrome, 0x38, 0x1de, 0x12e);

        /* Territory name (loc group 100). */
        gfx_wrap_localized_line_centered(loc, 100, name_sub, 0x1e0, 0xa5, 0xa0,
            font_menu, glyph_table, glyph_metrics, 0x3f);

        gfx_text_set_no_shadow(1);
        /* Population (+0x24). */
        gfx_text_label_render_dec(rec_i32(rec, OFF_POP), ' ', LBL_SPACE,
            0x1fc, 0xbd, font_name, glyph_table, 0x3f);
        /* Approval (+0x0c, signed byte). */
        gfx_text_label_render_dec(rec_s8(rec, OFF_APPROVAL), ' ', LBL_SPACE,
            0x25a, 0xbd, font_name, glyph_table, 0x3f);
        /* "Pop"/"Wealth" labels (loc 0x3d sub 0/1). */
        gfx_wrap_localized_line_centered(loc, 0x3d, 0, 0x1e0, 0xd5, 0x4c,
            font_name, glyph_table, glyph_metrics, 0x3f);
        /* Ration (+0xb9, "%"). */
        gfx_text_label_render_dec(rec_u8(rec, OFF_RATION), ' ', LBL_PCT,
            0x1fa, 0xe2, font_name, glyph_table, 0x3f);
        gfx_wrap_localized_line_centered(loc, 0x3d, 1, 0x234, 0xd5, 0x4c,
            font_name, glyph_table, glyph_metrics, 0x3f);
        /* Tax rate (loc 0x15 sub = +0x15d), red when changed. */
        uint8_t tax_col = (rec_u8(rec, OFF_TAX) == rec_u8(rec, OFF_TAX2))
                          ? 0x3f : 0xf9;
        gfx_wrap_localized_line_centered(loc, 0x15, rec_s8(rec, OFF_TAX),
            0x234, 0xe2, 0x4c, font_name, glyph_table, glyph_metrics, tax_col);
        gfx_text_set_no_shadow(0);

        /* Realm flag shield (+0x09). */
        gfx_sprite_render_from_blob(chrome, rec_s8(rec, OFF_FLAGID) + 0x46,
                                    0x228, 0xb5);
        /* Resource-panel divider sprite. */
        gfx_sprite_render_opaque_from_blob(chrome, 0x42, 0x1de, 0xfa);
        /* Castle/town indicator (+0x124 gate, +0x08 x-offset). */
        if (rec_i32(rec, OFF_CASTLE) == 0)
            gfx_sprite_render_from_blob(chrome, 0x3d,
                rec_s8(rec, OFF_XOFF) / 2 + 0x214, 0x106);
        else
            gfx_sprite_render_from_blob(chrome, 0x55,
                rec_s8(rec, OFF_XOFF) / 2 + 0x212, 0x104);

        /* Resource readouts. */
        panel_build_queues(&c);
        c.height1 = (c.count1 < 3) ? 0x3c : 0x2d;
        if (c.count2 < 3)      c.height2 = 0x3c;
        else if (c.count2 < 4) c.height2 = 0x2d;
        else                   c.height2 = 0x1e;
        c.row1 = 0;
        c.row2 = 0;

        for (int i = 0; i < c.count1; i++) {
            if (c.queue1[i] == 1)      panel_draw_q1_type1(&c);
            else if (c.queue1[i] == 0) panel_draw_q1_type0(&c);
            else if (c.queue1[i] == 2) panel_draw_q1_type2(&c);
        }
        for (int i = 0; i < c.count2; i++) {
            if (c.queue2[i] == 4)      panel_draw_q2_type4(&c);
            else if (c.queue2[i] == 5) panel_draw_q2_type5(&c);
            else if (c.queue2[i] == 6) panel_draw_q2_type6(&c);
            else if (c.queue2[i] == 7) panel_draw_q2_type7(&c);
            else if (c.queue2[i] == 3) panel_draw_q2_type3(&c);
        }
    } else {
        /* ---- NOT-OWNED branch ---- */
        gfx_sprite_render_opaque_from_blob(chrome, 0x3a, 0x1de, 0x9c);
        gfx_wrap_localized_line_centered(loc, 100, name_sub, 0x1e0, 0xb4, 0xa0,
            font_menu, glyph_table, glyph_metrics, 0x3f);

        int owner = rec_u8(rec, OFF_OWNER);
        if (owner != 0) {
            gfx_text_set_outline(1);
            uint8_t col = st->realm_block
                          ? st->realm_block[owner * 0x160 + 8]
                          : 0x3f;
            gfx_wrap_localized_line_centered(loc, 0xf, 0, 0x1e0, 0xf0, 0xa0,
                font_menu, glyph_table, glyph_metrics, col);
            gfx_wrap_localized_line_centered(loc, 0xf, 1, 0x1e0, 0x104, 0xa0,
                font_menu, glyph_table, glyph_metrics, col);
            /* Owning realm name (DAT_00553d54 + owner*0x2c).  Skipped when
             * the name table is unbound. */
            if (st->realm_names) {
                gfx_text_render_line_centered(
                    st->realm_names + owner * 0x2c, 0x1e0, 0x118, 0xa0,
                    font_menu, glyph_table, glyph_metrics, col);
            }
            gfx_text_set_outline(0);
        }
    }
}
