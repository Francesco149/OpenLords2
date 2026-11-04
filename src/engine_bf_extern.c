/* engine_bf_extern.c — the battlefield's out-of-partition seams that turned out small enough to port (tools/bf/parts.py
 * partition 'extern'; 0x4987b7, the sprite loads, stays a retail stub in engine_bf_extern_pending.c). */
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
        AT(int32_t, &g_bf_53e8d0, 0x53e8d0u, 0x53e8d0u + 4 * i) = 0;
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
    engine_retail_call(0x426d11u, 1, 0u);   /* TODO(bf-extern): the stream stop */
    engine_retail_call(0x427d19u, 0);       /* TODO(bf-link): audio_cue_release (audio.c, not linked) */
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
