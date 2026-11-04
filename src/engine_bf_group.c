/* engine_bf_group.c — the battlefield port, partition 'group' (tools/bf/parts.py). Retail semantics on retail's
 * layout: engine_bf.h (record accessors, globals). Each function: "Port of FUN_00xxxxxx" + what it does. */
#include "engine_bf.h"

/* Port of FUN_00478952 (53 B) — the battlefield's per-sub-step selection-marker countdown (the first call of
 * the frame driver's combat cluster): DAT_00553f84, the marker gate the renderer reads, falls by one to 0. */
int bf_478952(void)
{
    if (g_bf_553f84 > 0)
        g_bf_553f84--;
    return 0;
}
