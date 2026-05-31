/*
 * OpenLords2 — per-realm strategic turn-state updates.  See engine_realm.h.
 */

#include <stdint.h>

#include "engine_realm.h"
#include "save.h"                  /* g_per_army_768, g_player_strategic_352 */
#include "save_schema_globals.h"   /* s_slot_0056d5dc, s_slot_00553240 */

#define ARMY_REC(key)   (&g_per_army_768[(key) * 0x300])
#define STRAT_REC(slot) (&g_player_strategic_352[(slot) * 0x160])

/* Port of FUN_0044baea — see engine_realm.h for the field overview. */
void engine_realm_advance_rating(void)
{
    for (int key = 1; key <= (int)s_slot_0056d5dc; key++) {
        uint8_t *r = ARMY_REC(key);

        r[0x0d] = r[0x0c];                          /* previous = current */

        /* Fold in the three signed adjustment sources, one at a time, with
         * 8-bit wrap between steps (retail stores the byte back each add). */
        int8_t v = (int8_t)r[0x0c];
        v = (int8_t)(v + (int8_t)r[0x0e]);
        v = (int8_t)(v + (int8_t)r[0x10]);
        v = (int8_t)(v + (int8_t)r[0x11]);
        r[0x0c] = (uint8_t)v;

        r[0x12] = r[0x0e];                          /* display mirrors */
        r[0x14] = r[0x10];
        r[0x13] = r[0x11];
        r[0x15] = 0;
        r[0x17] = 0;
        r[0x219] = 0;
        *(int32_t *)(r + 0x194) = 0;

        /* Zero-population realm whose owner is not a human player → reset 50. */
        if (*(int32_t *)(r + 0x24) == 0 &&
            STRAT_REC(r[0x05])[0x05] == 0) {
            r[0x0c] = 0x32;
            r[0x17] = 0x32;
        }
        /* Unowned realm below 75 → nudge up by 5. */
        if ((int8_t)r[0x0c] < 0x4b && r[0x05] == 0) {
            r[0x0c] = (uint8_t)((int8_t)r[0x0c] + 5);
            r[0x17] = 5;
        }
        /* Decay the separate +0x2f4 countdown. */
        if (*(int32_t *)(r + 0x2f4) != 0)
            *(int32_t *)(r + 0x2f4) -= 5;

        /* Clamp 0..100 (signed). */
        if ((int8_t)r[0x0c] < 0)    r[0x0c] = 0;
        if ((int8_t)r[0x0c] > 0x64) r[0x0c] = 100;

        /* Accumulate + running average over the turn count. */
        *(int32_t *)(r + 0x1c) += (int8_t)r[0x0c];
        r[0x18] = (uint8_t)(*(int32_t *)(r + 0x1c) / (int32_t)s_slot_00553240);
    }
}
