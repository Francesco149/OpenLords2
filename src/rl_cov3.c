/* rl_cov3.c — a retail-layout port, partition 'cov3' (tools/bf/parts.py). Retail semantics on retail's
 * layout: rl.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does.
 * docs/findings/rl-cov3.md: what coverage round 3 reaches, the route per function (a: a wrapper of a linked old
 * port on retail's layout; b: a fresh port), quirks, evidence. */
#include "rl.h"
#include "rl_cov3_globals.h"
#include "rl_io_globals.h"             /* the movie (DAT_004d4d58 SMACK*, _5169bc, _5169d0, _516a4c); DAT_005530f0 */
#include "rl_ui2_globals.h"            /* DAT_00553e5c */
#include "engine_bf_input_globals.h"   /* DAT_0057c90c, the button router's param0 DAT_0059154c */
#include "rl_io_plat.h"                /* SmackClose, the CRT's open / lseek / read / write / close */
#include "gfx_globals.h"               /* g_gfx_last_error (DAT_005aeb68) */
#include "engine.h"                    /* g_sim_mp_active, the screen / redraw bridges */
#include "engine_combat.h"             /* g_siege_besieger (DAT_00568ed0) */
#include "engine_map_query.h"          /* route (a): 0x46d130 */
#include "engine_tile.h"               /* route (a): 0x4917dd */

/* rl_glue_globals.h's (its typed views of other blocks conflict with rl_io_globals.h's): the siege screen's engine
 * kind / count DAT_00544090 / DAT_00567d08 */
extern int32_t g_rl_544090;
extern int32_t g_rl_567d08;

/* route (a): engine_army_ai.c's ports of 0x49ff71 / 0x4a1b29 (linked in rhost, on the save block's layout) */
int  engine_army_ai_validate_campaign_target(int target, int player);
void ai_diplomacy_gift(int p1, int p2, uint8_t giver, int amount);

/* ---------------------------------------------------------------- retail records / globals */
#define PL8(p, va)  AT(uint8_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))   /* realm record */
#define PL32(p, va) AT(int32_t, g_player_strategic_352, 0x57bf00u, (int32_t)(va) + 352 * (p))
#define TER16(i, va) AT(int16_t, g_per_territory_420, 0x52f0b0u, (int32_t)(va) + 420 * (i))   /* army */
#define PARAM0  AT(int32_t, g_bf_59154c, 0x59154cu, 0x59154cu)   /* DAT_0059154c: the button router's param0 */
#define LOCAL   ((int)s_slot_0057c8cc)                           /* DAT_0057c8cc: the local player */
#define MP_ON   (g_sim_mp_active != 0)                           /* DAT_00553030 */
/* the AI personality table DAT_004d8a58: personality p's row at (p - 1) * 0xf0 (retail: (3p - 3) * 0x50, no bound) */
#define PERS32(p, off) AT(int32_t, g_rl_4d8a58, 0x4d8a58u, 0x4d8a58u + 240 * ((int)(p) - 1) + (off))
#define STR(sym, symva, va) ((int)(uintptr_t)&AT(uint8_t, sym, symva, va))   /* a retail string's address */

/* Port of FUN_0042df30 (178 B) — skip the playing movie (a click on screen 0x22; DAT_00516a4c set): the movie's
 * sprite / palette frees by its kind DAT_005169bc (0: 0x4af116, 2: 0x4af312, on the name DAT_005169d0), the open
 * SMACK DAT_004d4d58 closed (smackw32 #18), the base directory back (0x4af502), "OK :SMK user ends <name>" and
 * "MEM:Max alloc after = <free physical memory>" logged, then the movie's end (0x42e060(1): the return screen, or
 * the next boot movie). Callers read no return value. */
int rl_42df30(void)
{
    if (g_rl_516a4c == 0)
        return 0;
    if (g_rl_5169bc == 0)
        rl_4af116((int)(uintptr_t)g_rl_5169d0);
    else if (g_rl_5169bc == 2)
        rl_4af312((int)(uintptr_t)g_rl_5169d0);
    if (g_rl_4d4d58 != 0) {
        rl_plat_SmackClose((uint32_t)g_rl_4d4d58);
        g_rl_4d4d58 = 0;
    }
    rl_4af502();
    rl_4afab9(STR(g_rl_4d5080, 0x4d5080u, 0x4d5080u), (int)(uintptr_t)g_rl_5169d0, 0);   /* "OK :SMK user ends " */
    int m = rl_4b2575();
    rl_4afab9(STR(g_rl_4d5080, 0x4d5080u, 0x4d5094u), 0, m + 1);                         /* "MEM:Max alloc after = " */
    return rl_42e060(1);
}

/* Port of FUN_00434308 (62 B) — the load box's Cancel (title page 3's thumbs-down, list 0x4ddd78): on screen 0x1f
 * DAT_005530f0 = DAT_00553e5c, else the screen goes back to DAT_0057c90c's low byte; redraw 2. A router action
 * (entry-patched): EAX (the value moved) is not read. */
int rl_434308(void)
{
    int r;
    if (engine_current_screen() == 31) {
        r = g_rl_553e5c;
        g_rl_5530f0 = r;
    } else {
        r = (uint8_t)g_bf_57c90c;
        engine_set_current_screen(r);
    }
    engine_set_demand_redraw(2);
    return r;
}

/* Port of FUN_004367ff (115 B) — the type-10 popup's answer ("pay the price?"): the popup dismissed (0x476768);
 * on yes (param0) the local player pays his ally (realm +0x81, mutual) the asked price DAT_00567958 (ai_diplomacy_gift
 * 0x4a1b29, the giver byte DAT_0055ce50); MP: message 0x46 instead. A router action: EAX is not read. */
int rl_4367ff(void)
{
    int r = rl_476768();
    if (PARAM0 == 0)
        return r;
    if (MP_ON)
        return rl_43eda0(70, 0);
    return rl_4a1b29(PL8(LOCAL, 0x57bf81), LOCAL, (int)s_slot_0055ce50, (int)s_slot_00567958);
}

/* Port of FUN_0043b741 (131 B) — the siege preparations' "−" (param0 = the engine kind → DAT_00544090;
 * DAT_00567d08 = −1): when the besieger DAT_00568ed0 has one of that kind (the int16 +0x182 + 6 k, signed > 0) it
 * loses one (0x43b7c4; MP: message 0x37). A router action: EAX (the count when there is none) is not read. */
int rl_43b741(void)
{
    g_rl_544090 = PARAM0;
    g_rl_567d08 = -1;
    int r = TER16(g_siege_besieger, 0x52f232 + 6 * g_rl_544090);
    if (r <= 0)
        return r;
    if (MP_ON)
        return rl_43eda0(55, 0);
    return rl_43b7c4(g_siege_besieger, g_rl_544090, g_rl_567d08);
}

/* Port of FUN_0046d130 (371 B) — 1 iff the (2 radius + 1)-box at (col, row), clamped to the 64 × 64 map, holds a
 * cell with no army (+5 == 0) and raw flags 0 or 2 (+1 & 0xfd == 0); the loops count in the shared cell cursor
 * DAT_00569570 / DAT_00569578 (left at the hit). Route (a): engine_map_query.c's port (the cursor is its
 * g_cell_cursor_col / _row; the grid g_overworld_map). */
int rl_46d130(int col, int row, int radius)
{
    return engine_map_query_box_any_rawflag_only_bit1(col, row, radius, NULL, NULL);
}

/* Port of FUN_004917dd (599 B) — a castle cell's building footprint (castles_read_scenario 0x4aea70, cell flag +1
 * bit 0): an anchor glyph '@' / 'C' / 'X' / '[' at +3 (orientation 0 / 2 / 4 / 6) lays the 3 × 3 footprint
 * (0x4921e5's neighbour flags, the glyphs DAT_004d9dd0, the door cell), refreshes the path / stage scratches
 * (0x471c1f, 0x471d30) and records the cell (0x48ee46). Route (a): engine_tile.c's port on retail's arrays.
 * Returns the offset (retail's EAX on both paths: the default case's, and 0x48ee46's argument). */
int rl_4917dd(int off)
{
    engine_tile_expand_footprint_at(off, g_castles_state, g_path_scratch, g_stage_scratch, g_footprint_list,
                                    &g_last_footprint_tile);
    return off;
}

/* Port of FUN_0049ff71 (164 B) — validate an AI realm's campaign target county `target` for `player`: 0 when the
 * player has no ally (+0x81), when the county is the ally's and its +0x19c is 0, or when it is the player's own;
 * else the county. Route (a): engine_army_ai.c's port. */
int rl_49ff71(int target, int player)
{
    return engine_army_ai_validate_campaign_target(target, player);
}

/* Port of FUN_004a1b29 (298 B) — realm `from` gives `amount` to realm `to` if it can (cash +0x118, signed): the
 * pair's gift counter (+0x91 + 16 from) up, the relation (+0x84 + 16 from) down 4 (clamped at −30), the giver
 * byte +0xe8 = `giver`. Route (a): engine_army_ai.c's port (ai_diplomacy_gift). Retail's EAX, modelled from the
 * pre-state: 352 from when it cannot pay; else `giver` in AL over the relation's movsx after the −4 (0xffffff
 * when it is negative and not clamped; 16 from's upper bytes, i.e. 0, when clamped). */
int rl_4a1b29(int to, int from, int giver, int amount)
{
    if (PL32(from, 0x57c018) < amount)
        return 352 * from;
    int after = (int8_t)(uint8_t)(PL8(to, 0x57bf84 + 16 * from) - 4);
    ai_diplomacy_gift(to, from, (uint8_t)giver, amount);
    uint32_t hi = after < -30 ? ((uint32_t)(16 * from) & 0xffffff00u) : after < 0 ? 0xffffff00u : 0u;
    return (int)(hi | (uint8_t)giver);
}

/* Port of FUN_004a3551 (906 B) — AI realm `ai` answers `asker`'s plea for help (inbox type 5; `arg` = the county):
 * help needs the asker to be its ally (+0x81), an average county population (+0x14) of at least
 * the personality's bar (+0x30: 750 / 800 / 900 / 1000), and a relation (+0x84 + 16 asker) of 10+; then the
 * cached roll DAT_0058fd60 against 4 × relation: below → 184 (help: a message and a gift of 0, 0x4a1b29), else
 * 185 (a price: DAT_0055ce50 = arg, DAT_00567958 = the pair's gift count × the personality's unit +0xc, a type-10
 * message). Otherwise 183 (refused: the pair's +0x86 counter up). Every answer posts the message (0x472bc5) with
 * the variant = the mood cycle +0x159 + 4 × personality − 4, and steps the cycle (0..3). Returns retail's EAX
 * (352 ai, or the gift's). */
int rl_4a3551(int ai, int asker, int arg)
{
    int rel4 = 4 * (int8_t)PL8(ai, 0x57bf84 + 16 * asker);
    int bar = PERS32(PL8(ai, 0x57bf07), 0x30);
    int code;
    if ((int)PL8(ai, 0x57bf81) != asker)
        code = 183;
    else if (PL32(ai, 0x57bf14) < bar)
        code = 183;
    else if ((int8_t)PL8(ai, 0x57bf84 + 16 * asker) < 10)
        code = 183;
    else if (rel4 > g_bf_58fd60)
        code = 184;
    else
        code = 185;
#define VARIANT ((int)PL8(ai, 0x57c059) + 4 * (int)PL8(ai, 0x57bf07) - 4)
#define CYCLE() do { PL8(ai, 0x57c059)++; if (PL8(ai, 0x57c059) >= 4) PL8(ai, 0x57c059) = 0; } while (0)
    if (code == 183) {
        PL8(ai, 0x57bf86 + 16 * asker)++;
        rl_472bc5(ai, asker, 183, VARIANT, 1, arg, 0, 0);
        CYCLE();
        return 352 * ai;
    }
    if (code == 184) {
        rl_472bc5(ai, asker, 184, VARIANT, 1, arg, 0, 0);
        CYCLE();
        return rl_4a1b29(ai, asker, arg, 0);
    }
    s_slot_0055ce50 = (uint32_t)arg;
    s_slot_00567958 = (uint32_t)(PERS32(PL8(ai, 0x57bf07), 0xc) * (int)PL8(ai, 0x57bf91 + 16 * asker));
    rl_472bc5(ai, asker, code, VARIANT, 10, arg, 0, 0);
    CYCLE();
    return 352 * ai;
#undef VARIANT
#undef CYCLE
}

/* Port of FUN_004ae968 (264 B) — castles_write_scenario: castles.dat (O_WRONLY | O_CREAT | O_BINARY, 0600) gets the
 * battlefield grid's persistent bytes (+3, +1 of each 8-B cell, packed into DAT_004eb2b0) at slot `n` (12800 B at
 * (n − 1) × 12800). A failed open / seek logs "ERR:Exited, couldn't open / save castles.dat" and latches
 * DAT_005aeb68 = 1 (a failed seek leaves the file open). Returns close's result, else the log's. */
int rl_4ae968(int n)
{
    int fd = rl_plat_open((const char *)(uintptr_t)STR(g_rl_4df1c8, 0x4df1c8u, 0x4df1c8u), 0x8101, 0x180);
    if (fd == -1) {
        int r = rl_4afab9(STR(g_rl_4df1c8, 0x4df1c8u, 0x4df1d4u), 0, 0);
        g_gfx_last_error = 1;
        return r;
    }
    int len = 0x3200;
    for (int i = 0, j = 0; i < len; i += 2, j += 8) {
        g_castles_scratch[i] = g_castles_state[j + 3];
        g_castles_scratch[i + 1] = g_castles_state[j + 1];
    }
    if (rl_plat_lseek(fd, (n - 1) * len, 0) == -1) {
        int r = rl_4afab9(STR(g_rl_4df1c8, 0x4df1c8u, 0x4df1fcu), 0, 0);
        g_gfx_last_error = 1;
        return r;
    }
    rl_plat_write(fd, g_castles_scratch, (unsigned)len);
    return rl_plat_close(fd);
}

/* Port of FUN_004aea70 (506 B) — castles_read_scenario: slot `n` of castles.dat (O_RDONLY | O_BINARY) into
 * DAT_004eb2b0 (the read's count unchecked), unpacked into each cell's +3 / +1; per cell (flags = the new +1):
 * bit 1 → +7 = 9, +4 = 0, +2 = (+2 & 0xe3) | 4; else bit 0 → the building footprint (0x4917dd); flags 0: the
 * old +7 2 → 1, 7 → 5 (+4 = 0, +2 = (+2 & 0xe3) | 4). A failed open / seek logs and latches DAT_005aeb68 = 1
 * (a failed seek leaves the file open). Returns close's result, else the log's. */
int rl_4aea70(int n)
{
    int fd = rl_plat_open((const char *)(uintptr_t)STR(g_rl_4df1c8, 0x4df1c8u, 0x4df224u), 0x8000, 0);
    if (fd == -1) {
        int r = rl_4afab9(STR(g_rl_4df1c8, 0x4df1c8u, 0x4df230u), 0, 0);
        g_gfx_last_error = 1;
        return r;
    }
    int len = 0x3200;
    if (rl_plat_lseek(fd, (n - 1) * len, 0) == -1) {
        int r = rl_4afab9(STR(g_rl_4df1c8, 0x4df1c8u, 0x4df260u), 0, 0);
        g_gfx_last_error = 1;
        return r;
    }
    rl_plat_read(fd, g_castles_scratch, (unsigned)len);
    for (int i = 0, j = 0; i < len; i += 2, j += 8) {
        g_castles_state[j + 3] = g_castles_scratch[i];
        g_castles_state[j + 1] = g_castles_scratch[i + 1];
        int old7 = g_castles_state[j + 7];
        int fl = g_castles_state[j + 1];
        if (fl & 2) {
            g_castles_state[j + 7] = 9;
            g_castles_state[j + 4] = 0;
            g_castles_state[j + 2] &= 0xe3;
            g_castles_state[j + 2] |= 4;
        } else if (fl & 1) {
            rl_4917dd(j);
        }
        if (fl == 0) {
            if (old7 == 2) {
                g_castles_state[j + 7] = 1;
            } else if (old7 == 7) {
                g_castles_state[j + 7] = 5;
                g_castles_state[j + 4] = 0;
                g_castles_state[j + 2] &= 0xe3;
                g_castles_state[j + 2] |= 4;
            }
        }
    }
    return rl_plat_close(fd);
}
