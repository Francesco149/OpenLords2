/*
 * Strategic-map movement-cost grid — pure-C port of FUN_0046ff43.
 * See engine_strat_cost.h for the format/topology notes.
 */

#include "engine_strat_cost.h"

/* Canonical home for retail DAT_004f4080. */
int16_t g_strat_move_cost[ENGINE_STRAT_COST_CELLS];

void engine_strat_cost_rebuild(const uint8_t *overworld_map)
{
    int desc = 0;   /* byte offset into the descriptor grid (retail local_10) */
    int cell = 0;   /* linear cost-grid index 0..4095   (retail local_c)     */

    for (int row = 0; row < ENGINE_STRAT_COST_GRID_H; row++) {
        for (int col = 0; col < ENGINE_STRAT_COST_GRID_W; col++) {
            uint8_t flags = overworld_map[desc + 1];   /* DAT_00522f91 */
            uint8_t type  = overworld_map[desc + 0];   /* DAT_00522f90 */
            int16_t cost;

            /* Branch ladder mirrors FUN_0046ff43 verbatim — the test
             * order is load-bearing (earlier predicates win). */
            if ((flags & 0xc) != 0) {
                cost = 0;                               /* water / blocked */
            } else if (overworld_map[desc + 7] >= 0x11) {
                cost = 0;                               /* DAT_00522f97 gate */
            } else if ((flags & 1) != 0) {
                cost = 1;                               /* road */
            } else if ((flags & 0x20) != 0) {
                if (type < 2)         cost = 3;
                else if (type < 0x17) cost = 6;
                else                  cost = 3;
            } else if ((flags & 0x40) != 0) {
                cost = 100;
            } else if ((flags & 0x80) != 0) {
                if (type == 0x14)     cost = 3;
                else if (type == 3)   cost = 0;
                else if (type == 6)   cost = 0;
                else if (type == 0xc) cost = 0;
                else if (type == 9)   cost = 0;
                else                  cost = 100;
            } else if ((flags & 0x10) != 0) {
                cost = (type == 0x10) ? 100 : 0;
            } else {
                cost = 3;
            }

            g_strat_move_cost[cell] = cost;
            desc += ENGINE_STRAT_COST_DESC_STRIDE;
            cell++;
        }
    }
}
