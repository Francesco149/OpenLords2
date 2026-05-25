/*
 * Per-tick state hash — pure-C port of retail FUN_00440231 @ 0x440231
 * (1077 bytes) plus its inlined helpers FUN_0044015a (byte-sum hash,
 * 96 B) and FUN_0043ffc3 (clear 600 bytes, 117 B).
 *
 * Called from retail's per-tick driver as the input to the MP desync
 * detector (FUN_00440dd2 — compares bucket[0]/bucket[9] across players).
 *
 * Pure-function shape: every input explicit (mode, player_slot, sim
 * tick counter, six source-array pointers); every state mutation in
 * the output struct (10-byte bucket + 600-byte hash region + a
 * notify-flag for retail's combat snapshot tail call).
 *
 * Reference: docs/findings/save-format.md ("Two distinct gameplay-mode
 * schemas, one shared save-blob") for the cross-mode dispatch + per-
 * array stride/offset table.
 *
 * Retail global ↔ field mapping (mirrors live in the eventual wrapper):
 *
 *   DAT_0057c8fc          in->mode
 *   DAT_0057c8cc          in->player_slot
 *   DAT_0057c998          in->sim_tick_counter   (low 7 bits used)
 *
 *   DAT_0053f9b0          in->per_army_768           (17 × 768)
 *   DAT_0057bf00          in->player_strategic_352   (6  × 352)
 *   DAT_0052f0b0          in->per_territory_420      (151 × 420)
 *   DAT_00554480          in->per_battle_hex_432     (81 × 432)
 *   DAT_0057a100          in->per_combat_unit_76     (101 × 76)
 *   DAT_00566520          in->per_battle_hex_aux_52  (81 × 52)
 *
 *   DAT_00569530[slot*10..]   out->bucket[10]
 *   DAT_00565700[slot*600..]  out->hash_region[600]
 *
 * Quirks preserved:
 *
 *   (1) `bucket[7] = 1` ALWAYS fires AFTER the per-mode loops — so the
 *       combat-mode third loop's accumulated bucket[7] (the per_battle_
 *       hex_aux hash sum) is OVERWRITTEN.  Looks like a copy-paste bug
 *       in retail (the strategic-mode third loop targets bucket[3], not
 *       bucket[7]); the function reaches bucket[7] only in combat mode,
 *       and bucket[7]'s post-call value is always 1.  Preserved as a
 *       quirk — downstream MP desync comparison reads bucket[7] and
 *       expects it to always be 1, so "fixing" this would break the
 *       MP comparator.
 *
 *   (2) `bucket[0] = sum(bucket[1..8])` includes bucket[7]=1 always,
 *       and includes bucket[4]/bucket[5]/bucket[6]/bucket[8] which are
 *       always 0 in strategic mode (no loop targets them) and bucket
 *       [1..4]/bucket[8] which are always 0 in combat mode.  So:
 *
 *         strategic:  bucket[0] = b[1] + b[2] + b[3] + 1
 *         combat:     bucket[0] = b[5] + b[6]        + 1
 *         other:      bucket[0] = 1                       (only bucket[7]=1 fired)
 *
 *   (3) Byte-sum hash truncates the running total to char (8 bits) on
 *       every iteration — retail's FUN_0044015a has `local_c` as `char`
 *       so the sum wraps at 256.  Preserved.
 */

#include "engine.h"

#include <string.h>

/* Retail FUN_0044015a — sum bytes [offset..size) of `rec`, return low 8 bits.
 * The "size" arg is the END index (exclusive), not the byte count, so the
 * effective byte count is `size - offset`.  We mirror retail's semantics
 * exactly: `local_c = (char)0` then `local_c += rec[offset++]` per iter. */
static uint8_t hash_record(const uint8_t *rec, int size, int offset)
{
    uint8_t sum = 0;
    int n = size - offset;
    const uint8_t *p = rec + offset;
    for (int i = 0; i < n; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

/* Hash one strided array, writing per-slot hash bytes into the
 * caller-supplied region and accumulating the running sum.
 *
 * `array` points to slot 0; the function reads slots 1..max_slot.
 * Each slot is `stride` bytes; the hash skips the first `offset`
 * bytes (matches retail's per-array offset constants — 5 for the
 * per-army block, 6 for per-player strategic, etc.).
 *
 * `region` is the 200-byte chunk; the per-slot hash byte lands at
 * region[slot_idx].  Slot 0 and slots above max_slot stay at their
 * pre-call value (which is 0 after the FUN_0043ffc3 clear at function
 * entry).
 *
 * Returns the accumulated hash-sum so the caller can drop it into
 * the matching bucket slot. */
static uint8_t hash_strided_loop(const uint8_t *array, int stride, int offset,
                                 int max_slot, uint8_t *region)
{
    uint8_t bucket = 0;
    for (int i = 1; i <= max_slot; i++) {
        uint8_t h = hash_record(array + i * stride, stride, offset);
        region[i] = h;
        bucket = (uint8_t)(bucket + h);
    }
    return bucket;
}

void engine_compute_state_hash(const engine_state_hash_in_t *in,
                               engine_state_hash_out_t *out)
{
    /* Step 1 — clear the 600-byte hash region (retail FUN_0043ffc3). */
    memset(out->hash_region, 0, sizeof out->hash_region);

    /* Step 2 — clear the 10-byte bucket region.  Retail's first loop
     * clears [0..10) at function entry; the per-mode branches then
     * write into specific bucket indices.  Indices not written by any
     * branch (4, 8 always; 1..3 in combat, 5..7 in strategic) stay 0. */
    memset(out->bucket, 0, sizeof out->bucket);

    /* Step 3 — per-mode hash loops.  Each strided source array is
     * hashed slot-by-slot; the per-slot hash byte lands in the matching
     * 200-byte chunk of hash_region; the running sum lands in a fixed
     * bucket index. */
    if (in->mode == 0) {
        /* Strategic.  Loop indices and bucket targets are retail-side
         * constants from FUN_00440231's strategic branch. */
        out->bucket[1] = hash_strided_loop(in->per_army_768,
                                           0x300, 5, 16,
                                           &out->hash_region[0]);
        out->bucket[2] = hash_strided_loop(in->player_strategic_352,
                                           0x160, 6, 5,
                                           &out->hash_region[200]);
        out->bucket[3] = hash_strided_loop(in->per_territory_420,
                                           0x1a4, 0, 150,
                                           &out->hash_region[400]);
    } else if (in->mode == 2) {
        /* Combat.  Same shape, different arrays/strides/bounds. */
        out->bucket[5] = hash_strided_loop(in->per_battle_hex_432,
                                           0x1b0, 0x12, 80,
                                           &out->hash_region[0]);
        out->bucket[6] = hash_strided_loop(in->per_combat_unit_76,
                                           0x4c, 4, 100,
                                           &out->hash_region[200]);
        /* Quirk #1 — this bucket[7] write is overwritten by step 4.
         * Mirror retail anyway so the hash_region byte writes still
         * happen (downstream readers consume the region bytes
         * independently of bucket[7]'s value). */
        out->bucket[7] = hash_strided_loop(in->per_battle_hex_aux_52,
                                           0x34, 0, 80,
                                           &out->hash_region[400]);
    }
    /* Any other mode — both branches skipped, hash_region stays 0,
     * bucket[1..8] stay 0.  Steps 4-6 still fire. */

    /* Step 4 — bucket[7] = 1.  Always.  See quirk #1 above. */
    out->bucket[7] = 1;

    /* Step 5 — bucket[0] = sum of bucket[1..8].  Retail clears bucket
     * [0] explicitly (already 0 from our memset) then loops 1..9
     * accumulating.  The loop bound in retail is `i < 9` so it reads
     * bucket[1..8].  bucket[9] is set AFTER this sum (step 6), so the
     * pre-existing bucket[9] (which is 0 from the memset) doesn't
     * contribute. */
    uint8_t s = 0;
    for (int i = 1; i < 9; i++) {
        s = (uint8_t)(s + out->bucket[i]);
    }
    out->bucket[0] = s;

    /* Step 6 — bucket[9] = (sim_tick_counter & 0x7f) + 1.  Retail
     * casts to byte first (`(byte)DAT_0057c998 & 0x7f` — low byte's
     * low 7 bits + 1).  Truncating to u8 then masking 0x7f is the
     * same as masking 0x7f on the u32 directly, but mirror the cast
     * sequence anyway. */
    out->bucket[9] = (uint8_t)((((uint8_t)in->sim_tick_counter) & 0x7f) + 1u);

    /* Combat-mode tail — retail calls FUN_0043f4b7 which conditionally
     * memcpys three combat byte arrays into a malloc'd scratch (the
     * snapshot-for-MP-rollback machinery).  We surface this as a flag
     * rather than perform the snapshot — the scratch pointers aren't
     * ported yet, and the diff orchestrator pins them null retail-side
     * so the memcpy branch stays dormant. */
    out->notify_combat_snapshot = (in->mode == 2) ? 1 : 0;
}
