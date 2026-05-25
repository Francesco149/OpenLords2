/*
 * OpenLords2 — UI / screen-message event queue.
 *
 * Producer-side port of the FUN_00472bc5 cluster — a small ring buffer
 * of 50 fixed-size (24 B) event records that the engine populates from
 * many places (143 call sites in retail) and a consumer (FUN_00472e46
 * — NOT ported in this module) drains during the in-game phase.
 *
 * Each record carries: an audience player id, a target player id, three
 * i32 payload slots, three i8 discriminators, and one more i32.  The
 * publish call gates on a small filter — only enqueue if the calling
 * audience matches the local player (or is broadcast = 0).
 *
 *   FUN_00472ad0 @ 0x472ad0  →  engine_event_queue_init_wrapper
 *   FUN_00472ae0 @ 0x472ae0  →  engine_event_queue_init
 *   FUN_00472b40 @ 0x472b40  →  engine_event_queue_slot_zero
 *   FUN_00472bc5 @ 0x472bc5  →  engine_event_queue_post
 *   FUN_00472df5 @ 0x472df5  →  engine_event_queue_publish_scratch
 *
 * Pure C; the only retail dependency was a 24-byte memcpy
 * (FUN_00401136) which we inline with memcpy().  Consumer-side reads
 * from this state remain the responsibility of whoever ports
 * FUN_00472e46 — its callees (FUN_00476768, FUN_0047309e) are still
 * unported subsystems.
 */

#ifndef OPENLORDS2_ENGINE_EVENT_QUEUE_H
#define OPENLORDS2_ENGINE_EVENT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

/* Ring-buffer capacity.  Retail spelling: 0x32.  Wrap-around: the write
 * and read indexes are incremented past 0x31, then reset to 0. */
#define ENGINE_EVENT_QUEUE_CAPACITY  0x32   /* 50 slots */

/* Wire-size of one record — matches retail's `0x18` memcpy literal. */
#define ENGINE_EVENT_QUEUE_RECORD_BYTES  0x18

/* One event record.  Field offsets match the 24-byte retail layout
 * exactly so a memcpy from/to a Frida-side mirror is byte-equivalent.
 *
 *   +0  i32 audience    — `param_2` of post (filter input + copied
 *                          into the record but never read back by the
 *                          consumer; preserved for byte parity)
 *   +4  i32 target      — `param_1` of post (target / source player)
 *   +8  i32 code        — `param_3`.  Consumer treats this as the
 *                          "is slot live" flag (non-zero = pending)
 *                          AND the dispatch discriminator.
 *   +12 i32 payload32a  — `param_4`
 *   +16 u8  pad16       — never written by the producer; sits between
 *                          the +12 i32 and the +17 byte.  Set to 0 on
 *                          slot init; persists otherwise.
 *   +17 u8  flag5       — `param_5`
 *   +18 u8  flag6       — `param_6`
 *   +19 u8  flag7       — `param_7`
 *   +20 i32 payload32b  — `param_8`
 */
typedef struct {
    int32_t audience;
    int32_t target;
    int32_t code;
    int32_t payload32a;
    uint8_t pad16;
    uint8_t flag5;
    uint8_t flag6;
    uint8_t flag7;
    int32_t payload32b;
} engine_event_queue_record_t;

/* All state read or written by the producer cluster.  Mirrors the
 * adjacent retail globals so a host-side test can construct the same
 * post-state the engine would see.
 *
 *   scratch          ← DAT_00553510..DAT_00553527 (24 B staging area)
 *   ring             ← DAT_00568480..  (50 × 24 B = 1200 B)
 *   write_idx        ← DAT_00543fd0 (next slot the producer writes)
 *   read_idx         ← DAT_00553ec4 (next slot the consumer reads)
 *   notify_flag      ← DAT_00565484 (1 = at least one publish since
 *                      last reset; consumer-side latch)
 *   consumer_cooldown← DAT_0057c954 (the consumer-side rate limiter
 *                      ticks; included because init clears it and a
 *                      faithful init() must wipe the same set of
 *                      globals retail does)
 */
typedef struct {
    engine_event_queue_record_t scratch;
    engine_event_queue_record_t ring[ENGINE_EVENT_QUEUE_CAPACITY];
    int write_idx;
    int read_idx;
    int notify_flag;
    int consumer_cooldown;
} engine_event_queue_state_t;

/* Reset the entire queue to "empty + idle".  Sets write_idx = read_idx
 * = notify_flag = consumer_cooldown = 0, then calls engine_event_queue_
 * slot_zero(state, i) for i in 0..ENGINE_EVENT_QUEUE_CAPACITY.  Retail
 * FUN_00472ae0.
 *
 * NOTE: only slot 0 ends up fully zeroed — see engine_event_queue_slot_
 * zero's quirk note.  Other slots retain whatever non-`+8` bytes they
 * had pre-init.  In retail this is benign at boot (BSS is loader-zeroed)
 * but matters on runtime re-init from a non-zero state. */
void engine_event_queue_init(engine_event_queue_state_t *state);

/* Trivial wrapper around engine_event_queue_init.  Retail
 * FUN_00472ad0; exists as a separate symbol because retail had two
 * call sites that took different code paths to reach the same body.
 * Preserved as its own function for grep-ability. */
void engine_event_queue_init_wrapper(engine_event_queue_state_t *state);

/* Zero a slot's per-field contents.  Used by init() (called for each
 * slot in 0..49) AND by the consumer (FUN_00472e46) to clear a slot
 * after draining it.
 *
 * QUIRK (#12): retail's FUN_00472b40 zeros slot[param_1].code (+8) but
 * the other six writes target slot[state->read_idx] regardless of
 * param_1.  At runtime the consumer always passes param_1 == read_idx
 * so this is a no-op divergence.  During init's 0..49 sweep, however,
 * read_idx is pinned at 0, so:
 *   - every slot's `code` (+8) IS zeroed (because that uses param_1),
 *   - only slot[0]'s remaining fields are zeroed.
 * Slots 1..49 keep their pre-existing non-`code` bytes through init.
 * See docs/findings/engine-quirks.md. */
void engine_event_queue_slot_zero(engine_event_queue_state_t *state, int slot);

/* Stage scratch from the 8 caller args, optionally publish to the ring.
 * Retail FUN_00472bc5.  Returns true iff the record was committed to
 * the ring (filter-pass + write_idx bump + notify_flag = 1).
 *
 * Argument naming follows retail's positional order:
 *   target          (param_1) — target / source player slot
 *   audience        (param_2) — audience player; 0 = broadcast
 *   code            (param_3) — event code (also the live-flag)
 *   payload32a      (param_4)
 *   flag5..flag7    (param_5..7) — byte discriminators
 *   payload32b      (param_8)
 *
 * The filter (under all three branches in retail) reduces to:
 *   publish iff (audience == 0) OR (audience == local_player)
 *
 * Retail's two extra branches (gated on target == 0 vs the per-player
 * has-data byte) are preserved verbatim — they all reach the same
 * conclusion, but the byte sequence differs in the dead arms and the
 * port faithfully traces every branch.  See the comment in the body.
 *
 * Filter inputs:
 *   player_has_data[7] — retail (&DAT_0057bf05)[i * 0x160], one byte
 *                        per slot 0..6; non-zero = slot is active.
 *   local_player       — retail DAT_0057c8cc (current player slot).
 *
 * Scratch is ALWAYS written even when the filter fails — that mirrors
 * retail and is observable through engine_event_queue_publish_scratch
 * (which a save-restore path can use to commit the unrelated previous
 * scratch contents).
 */
bool engine_event_queue_post(engine_event_queue_state_t *state,
                             int32_t target,
                             int32_t audience,
                             int32_t code,
                             int32_t payload32a,
                             uint8_t flag5,
                             uint8_t flag6,
                             uint8_t flag7,
                             int32_t payload32b,
                             const uint8_t player_has_data[7],
                             int local_player);

/* Commit the current scratch record to the ring buffer unconditionally
 * (no filter check).  Retail FUN_00472df5.  Bumps write_idx, sets
 * notify_flag = 1.  Used by the save/load path's "restore a pending
 * scratched-but-not-yet-published event" call (FUN_00443024). */
void engine_event_queue_publish_scratch(engine_event_queue_state_t *state);

#endif /* OPENLORDS2_ENGINE_EVENT_QUEUE_H */
