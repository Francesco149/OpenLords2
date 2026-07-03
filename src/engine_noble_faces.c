/*
 * OpenLords2 — Noble Faces / diplomacy partner-select (screen 0x0b).
 * See engine_noble_faces.h for the retail address map and the entry path.
 *
 * Two-tier render, the same split as every interactive strat panel:
 *   - engine_noble_faces_render  (FUN_00416cf3)  — the open/demand panel,
 *     drawn over a freshly recomposited map (the strat composite is the
 *     caller's, exactly as the Court / Send Supplies);
 *   - engine_noble_faces_refresh (FUN_004ba26e case 0x0b) — the per-frame
 *     action-button sprite strip (FUN_0040cfd2 over DAT_004dd940).
 */

#include <stdint.h>
#include <stdlib.h>

#include "engine_noble_faces.h"

#include "engine_roster.h"         /* player_first_active_nonlocal (FUN_004a1e6c) */
#include "save_schema_globals.h"   /* s_slot_0057c8cc / _00553fbc / _00553f38 */

#include "gfx_panel.h"             /* gfx_panel_draw_bevel (FUN_004093e0) */
#include "gfx_sprite_render.h"     /* gfx_sprite_render[_from_blob] + seal (a682/a21a/d1bc) */
#include "gfx_text.h"              /* gfx_text_render_line (FUN_00402637) */
#include "gfx_line.h"              /* rect / fill / bevel (403cf4 / 40437d / 403deb) */
#include "gfx_wrap.h"              /* gfx_wrap_localized (FUN_0040328e) */
#include "io.h"                    /* io_read_file_at (FUN_004af8d9) */
#include "io_path.h"               /* io_path_chdir_base (FUN_004af502) */

/* Per-player strategic record (g_player_strategic_352, retail DAT_0057bf00). */
#define STRAT_STRIDE 0x160
#define SREC(s)      (ctx->strategic_block + (size_t)(s) * STRAT_STRIDE)

/* Diplomacy offsets within the 0x160-byte record. */
#define F_ACTIVE   0x04   /* alive / in-game                                  */
#define F_AI       0x05   /* 0 = AI-controlled → personality face + meter     */
#define F_FACE     0x07   /* personality → portrait index (personality*3 - 3) */
#define F_MOOD     0x0a   /* mood 1..5 → misc_cty sprite mood+0x55            */
#define F_LORD     0x81   /* this player's lord/vassal id                     */
#define F_REL      0x84   /* relation i8 toward other (+ other*0x10)          */
#define F_TREATY   0x85   /* treaty flag toward other (+ other*0x10)          */
#define F_WARFLAG  0x88   /* war-declared flag toward other (+ other*0x10)    */
#define F_ALLY     0x90   /* alliance/met flag toward other (+ other*0x10)    */

/* DAT_00553ee8 — action-button count (transient UI, not save-tracked). */
static int g_noble_action_count;

/* faces.pl8 sprite sheet.  Retail loads it into the shared scratch DAT_004eabec
 * at 100000 bytes and binds it as the global sprite blob (portraits + the
 * war/alliance status icons 0xc..0xf); we keep a persistent buffer, reloaded
 * per render — the same pattern as engine_rankings' flags.pl8. */
#define FACES_SCRATCH_LEN 100000
static uint8_t *g_faces_scratch;

static int faces_scratch_ready(void)
{
    if (!g_faces_scratch) {
        g_faces_scratch = malloc(FACES_SCRATCH_LEN);
    }
    return g_faces_scratch != NULL;
}

/* ── The 6 diplomacy-action buttons (retail DAT_004dd940, r2-extracted) ──
 * x=0x190, y=0x66+i*0x32, sprite 0x40, kind 0x20 (system sheet), type 5
 * (press-latch); the actions are the 0x1a-mode entry functions FUN_00436141
 * … FUN_0043631e (wired when the diplomacy-action screen is ported). */
static engine_button_rec_t k_noble_buttons[6] = {
    { 0x190, 0x066, 0x40, 0x20, 0x436141, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x098, 0x40, 0x20, 0x43618b, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x0ca, 0x40, 0x20, 0x4361da, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x0fc, 0x40, 0x20, 0x436229, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x12e, 0x40, 0x20, 0x4362ca, 0, 0, 0, 5, 0, 0 },
    { 0x190, 0x160, 0x40, 0x20, 0x43631e, 0, 0, 0, 5, 0, 0 },
};

/* Non-const: the widget-router 0x0b arm dispatches over this table, and
 * engine_menu_button_dispatch animates each button's press-flash in place
 * (the per-frame refresh then reads it back — same as every router widget). */
engine_button_rec_t *engine_noble_faces_buttons(void) { return k_noble_buttons; }
int engine_noble_faces_button_count(void) { return g_noble_action_count; }

/* ── one action-label line (FUN_0040328e(0x48,sub,0xe0,y,0xa0,100,0,0,…)) ── */
static void nf_action_label(const engine_noble_faces_ctx_t *ctx, int sub, int y)
{
    gfx_wrap_localized(ctx->loc, 0x48, sub, 0xe0, y, 0xa0, 100, 0, 0,
                       ctx->menu_font, ctx->glyph_table, ctx->glyph_metrics, 0x3f);
}

/* ── FUN_004171ee — one noble's portrait cell (player p, grid row) ──────── */
static void noble_face_draw(const engine_noble_faces_ctx_t *ctx, int p, int row)
{
    uint8_t *pr   = SREC(p);
    int      base = row * 100;
    int      loc  = ctx->local_player;

    /* Portrait frame bevel. */
    gfx_line_rect_bevel_dark_br(0x30, base + 0x31, 0x52, 0x4e);

    /* Portrait: AI slots show their personality face, others the generic
     * face 0xc.  From the global faces.pl8 blob. */
    if (pr[F_AI] == 0) {
        gfx_sprite_render((int)pr[F_FACE] * 3 - 3, 0x31, base + 0x32);
    } else {
        gfx_sprite_render(0xc, 0x31, base + 0x32);
    }

    /* Mood byte lazy-init (0 → 1) + clamp (>5 → 5), persisted in place exactly
     * as retail; the icon is misc_cty sprite mood+0x55. */
    if (pr[F_MOOD] == 0) pr[F_MOOD] = 1;
    if (pr[F_MOOD] > 5)  pr[F_MOOD] = 5;
    gfx_sprite_render_from_blob(ctx->misc_sheet, (int)pr[F_MOOD] + 0x55,
                               0x20, base + 0x37);

    /* Realm name (menu font). */
    gfx_text_render_line(ctx->realm_names + (size_t)p * 0x2c, 0x20, base + 0x83,
                         ctx->menu_font, ctx->glyph_table, 0x3f);

    /* Selection highlight if this is the current partner (two nested frames). */
    if (p == (int)s_slot_00553fbc) {
        gfx_line_rect(0x2f, base + 0x30, 0x54, 0x50, 0xf9);
        gfx_line_rect(0x2e, base + 0x2f, 0x56, 0x52, 0x3f);
    }

    /* Status icons toward the local player (from the faces.pl8 blob): alliance
     * (0xf), then treaty (0xd) OR — with no treaty — war-declared (0xe). */
    if (pr[F_ALLY + loc * 0x10] != 0) {
        gfx_sprite_render(0xf, 0x1e, base + 0x50);
    }
    if (pr[F_TREATY + loc * 0x10] == 0) {
        if (pr[F_WARFLAG + loc * 0x10] != 0) {
            gfx_sprite_render(0xe, 0x92, base + 0x41);
        }
    } else {
        gfx_sprite_render(0xd, 0x9e, base + 0x41);
    }

    /* Relation meter (AI slots only): a 61-segment vertical bar filled from the
     * top down to the relation value; one colour by overall relation (red <-10,
     * yellow <11, else green). */
    if (pr[F_AI] == 0) {
        int rel = (int8_t)pr[F_REL + loc * 0x10];
        gfx_line_rect_bevel_dark_br(0x88, base + 0x40, 10, 0x3f);
        gfx_line_fill_rect(0x89, base + 0x41, 8, 0x3d, 0x3f);
        int seg = 0;
        for (int t = 0x1e; t > -0x1f; t--) {
            if (t <= rel) {
                uint8_t color = (rel < 0xb) ? ((rel < -10) ? 0xf9 : 0xfc) : 0xfa;
                gfx_line_fill_rect(0x89, base + seg + 0x41, 8, 1, color);
            }
            seg++;
        }
    }
}

/* ── FUN_00416cf3 — the open render (over the recomposited map) ─────────── */
void engine_noble_faces_render(const engine_noble_faces_ctx_t *ctx)
{
    int loc = ctx->local_player;

    /* Default the partner to the first living non-local player if none is set
     * or the current one has left (FUN_004a1e6c writes s_slot_00553fbc). */
    if (s_slot_00553fbc == 0 ||
        ctx->strategic_block[(size_t)s_slot_00553fbc * STRAT_STRIDE + F_ACTIVE] == 0) {
        player_first_active_nonlocal();
    }
    int partner = (int)s_slot_00553fbc;

    /* Standing relation toward the partner (drives the action-button set):
     * 3 allied, else 1 partner-is-my-lord / 0 I-have-no-lord / 2 other-lord. */
    if (SREC(partner)[F_ALLY + loc * 0x10] == 0) {
        uint8_t my_lord = SREC(loc)[F_LORD];
        if (my_lord == partner)      s_slot_00553f38 = 1;
        else if (my_lord == 0)       s_slot_00553f38 = 0;
        else                         s_slot_00553f38 = 2;
    } else {
        s_slot_00553f38 = 3;
    }

    /* Panel parchment + continue seal (over the retained map). */
    if (ctx->chrome) {
        gfx_panel_draw_bevel(ctx->chrome, 0x10, 0x20, 0x1c, 0x1b);
    }
    gfx_sprite_seal_draw(ctx->system_sheet, 0x1a8, 0x1a6, 0);

    /* Load faces.pl8 into the global sprite blob (portraits + status icons). */
    if (faces_scratch_ready()) {
        io_path_chdir_base();
        io_read_file_at("faces.pl8", g_faces_scratch, FACES_SCRATCH_LEN, 0);
        gfx_sprite_render_set_blob(g_faces_scratch);
    }

    /* One portrait cell per living non-local player. */
    int row = 0;
    for (int p = 1; p < 6; p++) {
        if (p != loc &&
            ctx->strategic_block[(size_t)p * STRAT_STRIDE + F_ACTIVE] != 0) {
            noble_face_draw(ctx, p, row);
            row++;
        }
    }

    /* Partner realm name (title font). */
    gfx_text_render_line(ctx->realm_names + (size_t)partner * 0x2c, 0xd0, 0x3d,
                         ctx->title_font, ctx->glyph_table, 0x3f);

    /* Action-label set + button count by relationship state (grp 0x48).  The
     * misc_cty decor icon 0x1d and the label backdrop bevel match retail. */
    if (s_slot_00553f38 == 0) {
        gfx_line_rect_bevel_dark_br(0xd0, 0x60, 0xe8, 0xd0);
        g_noble_action_count = 4;
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x1d, 0x140, 0x140);
        nf_action_label(ctx, 2, 0x70);
        nf_action_label(ctx, 3, 0xa2);
        nf_action_label(ctx, 4, 0xd4);
        nf_action_label(ctx, 5, 0x106);
    } else if (s_slot_00553f38 == 1) {
        gfx_line_rect_bevel_dark_br(0xd0, 0x60, 0xe8, 0x130);
        g_noble_action_count = 6;
        nf_action_label(ctx, 2, 0x70);
        nf_action_label(ctx, 3, 0xa2);
        nf_action_label(ctx, 4, 0xd4);
        nf_action_label(ctx, 6, 0x106);
        nf_action_label(ctx, 7, 0x138);
        nf_action_label(ctx, 8, 0x16a);
    } else if (s_slot_00553f38 == 2) {
        gfx_line_rect_bevel_dark_br(0xd0, 0x60, 0xe8, 0xa0);
        g_noble_action_count = 3;
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x1d, 0x140, 0x140);
        nf_action_label(ctx, 2, 0x70);
        nf_action_label(ctx, 3, 0xa2);
        nf_action_label(ctx, 4, 0xd4);
    } else if (s_slot_00553f38 == 3) {
        g_noble_action_count = 0;
        nf_action_label(ctx, 0x18, 0xa2);
        gfx_sprite_render_from_blob(ctx->misc_sheet, 0x1d, 0x140, 0x140);
    }
    /* FUN_0045240a (dirty mark) is seam-owned; omitted like every panel. */
}

/* ── FUN_004ba26e case 0x0b — the per-frame action-button sprite strip ─── */
void engine_noble_faces_refresh(const engine_noble_faces_ctx_t *ctx)
{
    /* FUN_0040cfd2(0,0,&DAT_004dd940, DAT_00553ee8). */
    engine_button_render_list(0, 0, k_noble_buttons, g_noble_action_count,
                              ctx->misc_sheet, ctx->system_sheet, NULL);
}
