/* engine_bf_extern.c — the battlefield's out-of-partition seams that turned out small enough to port (tools/bf/parts.py
 * partition 'extern'): the tick reset, the click cue, the battle music, the sprite loads. */
#include "engine_bf.h"
#include "rl.h"
#include "engine_combat.h"
#include "engine.h"

/* Port of FUN_0043f304 (78 B) — reset the per-player tick counters DAT_0053e8d0[10], the sim tick DAT_0057c998 and
 * raise the render-dirty mode DAT_0052afcc = 2. Returns the last index (9). */
int bf_43f304(void)
{
    int i;
    for (i = 0; i < 10; i++)
        g_bf_53e8d0[i] = 0;
    g_bf_57c998[0] = 0;
    g_combat_render_dirty = 2;
    return i - 1;
}

/* Port of FUN_00499d97 (26 B) — the click cue: click3.wav through the cue table (0x425e4b, slot 17). */
int bf_499d97(void) { return rl_425e4b((int)(uintptr_t)g_bf_4dafc0, 17); }

/* Port of FUN_00477b2f (346 B) — start the battle music: stop the stream (0x426d11(0)) and the cue (0x427d19); with
 * battle2.wav present (0x4af841) or in single player, alternate the two siege-assault tracks (a multi-wave siege
 * DAT_0057a0f0: battle4 / battle5; a siege DAT_0053e8fc: battle3 / battle4) or the two field tracks (battle1 / 2) —
 * the alternation counters DAT_00553540 / DAT_00553d28; else (an MP game without the music) battle3 for a siege,
 * battle1 otherwise. The music plays through the stream loader 0x4263ad(name, 0, 1). */
int bf_477b2f(void)
{
    const uint8_t *tracks = g_bf_4d9228;   /* "battle1.wav" … "battle5.wav", 16 B each (0x4d9228) */
    g_combat_audio_latch = 0;
    int have = rl_4af841((int)(uintptr_t)(g_bf_4d9c90 + (0x4d9ea4u - 0x4d9c90u)));   /* "battle2.wav" */
    rl_426d11(0);   /* the stream stop (rl ui4) */
    rl_427d19();       /* audio_cue_release (rl ui4) */
    if (have != 0 || g_sim_mp_active == 0) {
        if (s_slot_0057a0f0 != 0) {
            if ((int)++s_slot_00553540 > 1)
                s_slot_00553540 = 0;
            return rl_4263ad((int)(uintptr_t)(tracks + 16 * ((int)s_slot_00553540 + 3)), 0, 1);
        }
        if (s_slot_0053e8fc != 0) {
            if ((int)++s_slot_00553540 > 1)
                s_slot_00553540 = 0;
            return rl_4263ad((int)(uintptr_t)(tracks + 16 * ((int)s_slot_00553540 + 2)), 0, 1);
        }
        if ((int)++s_slot_00553d28 > 1)
            s_slot_00553d28 = 0;
        return rl_4263ad((int)(uintptr_t)(tracks + 16 * (int)s_slot_00553d28), 0, 1);
    }
    if (s_slot_0053e8fc != 0)
        return rl_4263ad((int)(uintptr_t)(tracks + 32), 0, 1);
    return rl_4263ad((int)(uintptr_t)tracks, 0, 1);
}

#include "rl_io_plat.h"
#include "engine_battlefield_render.h"
#include "engine_mapview_globals.h"
#include "engine_workforce_hud.h"
#include "engine_title_render.h"
#include "save.h"
#include "save_schema_globals.h"
#include "engine_bf_extern_globals.h"

static int read_file_at(const void *name, void *buf, uint32_t n)   /* 0x4af8d9 — TODO(rl-link): io_read_file_at */
{ return (int)engine_retail_call(0x4af8d9u, 4, (uint32_t)(uintptr_t)name, (uint32_t)(uintptr_t)buf, n, 0u); }

/* Port of FUN_004987b7 (1556 B) — load the battlefield's sprite sheets: the base dir (0x4af502), the panel sheet
 * (0x4b25b0, 75000 B), the strip / palette / name loads (0x49915e, 0x499366, 0x49964c), county screen mode 2
 * (DAT_0053f050). Then 32 slots, each mapped to an entry of the 20-B file table at 0x4da550 (name, size at +0x10):
 * slots 0 / 1 the terrain banks (a siege: by the castle class DAT_0057c910, 2 / 3 or 4 / 5; a field battle: 0 only),
 * 6..10 themselves, 11 / 12 the siege / field minimap banks (13 / 14, 15 / 16, or 11 alone), 17, 18..24 the attacker's
 * nation (county DAT_00568210's +2) and 25..31 the defender's (DAT_00568218): each loaded into a fresh block (retail's
 * malloc) after the stream poll (0x4b8c3a, 0x426529), its pointer into the slot's global; a failed alloc / read is
 * logged (0x4afab9). Then misc_bat.pl8 → DAT_005530c8, vill_gd8.pl8 → 0x542ce0 (0x12d8 B), the renderer's hex-grid
 * and minimap contexts bound (0x4bc020 / 0x4bc107), "OK: battle data loaded". Returns 1, 0 on the misc sheet's failure. */
int bf_4987b7(void)
{
    engine_retail_call(0x4af502u, 0);                 /* TODO(rl-link): io_path_chdir_base */
    engine_retail_call(0x4b25b0u, 1, 75000u);         /* TODO(rl-link): gfx_load_panel_sheet */
    rl_49915e();
    rl_499366();
    rl_49964c();
    g_wf_panel_mode = 2;
    const uint8_t *tab = g_bf_4da550;                 /* 20-B records: the file name, its size at +0x10 */
    for (int i = 0; i < 32; i++) {
        int k;
        if (i >= 25)
            k = i - 24 + 7 * AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b2u + 420 * s_slot_00568218) + 17;
        else if (i >= 18)
            k = i + 7 * AT(uint8_t, g_per_territory_420, 0x52f0b0u, 0x52f0b2u + 420 * s_slot_00568210);
        else if (i == 17)
            k = 17;
        else if (i >= 13)
            continue;
        else if (i >= 11) {
            if (s_slot_0053e8fc != 0)
                k = s_slot_0057c910 != 0 ? (i == 12 ? 14 : 13) : (i == 12 ? 16 : 15);
            else if (i == 12)
                continue;
            else
                k = 11;
        } else if (i >= 6)
            k = i;
        else if (i >= 2)
            continue;
        else if (s_slot_0053e8fc != 0)
            k = s_slot_0057c910 != 0 ? (i == 1 ? 3 : 2) : (i == 1 ? 5 : 4);
        else if (i == 1)
            continue;
        else
            k = 0;
        uint32_t size = *(const uint32_t *)(tab + 20 * k + 0x10);
        const uint8_t *name = tab + 20 * k;
        if (size == 0)
            continue;
        rl_4b8c3a();
        rl_426529();
        uint8_t *buf = rl_plat_malloc(size);
        if (!buf) {
            rl_4afab9((int)(uintptr_t)(g_bf_4dbd3c + 0), (int)(uintptr_t)name, 0);          /* "ERR:BATTLE Data alloc …" */
            continue;
        }
        if (read_file_at(name, buf, size) == 0) {
            rl_4afab9((int)(uintptr_t)(g_bf_4dbd3c + 40), (int)(uintptr_t)name, 0);         /* "ERR:BATTLE Data load …" */
            continue;
        }
        int32_t p = (int32_t)(uintptr_t)buf;
        if (i == 0)
            g_mv_terrain_blob0 = buf;
        switch (i) {
        case 1:  g_bf_57c928 = p; break;
        case 6:  g_bfr_marker_blob = buf; break;
        case 7:  g_bf_55ce58 = p; break;
        case 8:  g_bfr_unit_key_blob = buf; break;
        case 9:  g_bfr_type7_blob_low = buf; break;
        case 10: g_bfr_type7_blob_high = buf; break;
        case 11: g_bf_53f044 = p; break;
        case 12: g_bf_56d680 = p; break;
        case 17: g_bf_56d5a0 = p; break;
        case 18: g_bf_53ef50 = p; break;
        case 19: g_bf_53f0d0 = p; break;
        case 20: g_bf_56d640 = p; break;
        case 21: g_bf_56d684 = p; break;
        case 22: g_bf_57c8d4 = p; break;
        case 23: g_bf_52afb0 = p; break;
        case 24: g_bf_53f650 = p; break;
        case 25: g_bf_553f2c = p; break;
        case 26: g_bf_55cd30 = p; break;
        case 27: g_bf_56d890 = p; break;
        case 28: g_bf_57c8bc = p; break;
        case 29: g_bf_57cb28 = p; break;
        case 30: g_bf_53f0bc = p; break;
        case 31: g_bf_553258 = p; break;
        default: break;
        }
    }
    uint32_t n = (uint32_t)g_bf_4daed4;
    uint8_t *misc = rl_plat_malloc(n);
    g_tr_misc_sheet = misc;
    if (!misc) {
        rl_4afab9((int)(uintptr_t)(g_bf_4dbd3c + 164), (int)(uintptr_t)g_bf_4daec4, 0);     /* "ERR:BATTLE Data M…" */
        return 0;
    }
    if (read_file_at(g_bf_4daec4, misc, n) == 0) {                                          /* misc_bat.pl8 */
        rl_4afab9((int)(uintptr_t)(g_bf_4dbd3c + 124), (int)(uintptr_t)g_bf_4daec4, 0);
        return 0;
    }
    read_file_at(g_bf_4dbd3c + 80, g_bf_542ce0, 0x12d8u);                                  /* vill_gd8.pl8 */
    engine_battlefield_render_bind_hex_grid_context(g_mv_terrain_blob0, (const uint8_t *)(uintptr_t)(uint32_t)g_bf_57c928,
                                                    g_castles_state, 80, 80, 8, 0, 24, 15, 14, 32);
    engine_battlefield_render_bind_minimap_context((const uint8_t *)(uintptr_t)(uint32_t)g_bf_53f044,
                                                   (const uint8_t *)(uintptr_t)(uint32_t)g_bf_56d680,
                                                   (const uint8_t *)(uintptr_t)(uint32_t)g_bf_56d5a0, 480, 24, 2);
    rl_4afab9((int)(uintptr_t)(g_bf_4dbd3c + 96), 0, 0);                                    /* "OK: battle data loaded" */
    return 1;
}
