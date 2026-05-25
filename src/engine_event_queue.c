/*
 * UI / screen-message event-queue producer cluster — pure-C port of
 * the FUN_00472bc5 family.  See engine_event_queue.h for the address
 * map, semantics, and quirks (#12).
 */

#include "engine_event_queue.h"

#include <stddef.h>
#include <string.h>

/* The wire size has to be exactly 24 bytes — the retail memcpy literal
 * is 0x18 and the consumer reads fixed offsets into the slot.  Catch a
 * layout shift at compile time. */
_Static_assert(sizeof(engine_event_queue_record_t)
               == ENGINE_EVENT_QUEUE_RECORD_BYTES,
               "record must be 24 bytes to match retail layout");
_Static_assert(offsetof(engine_event_queue_record_t, audience)   == 0,
               "audience at +0");
_Static_assert(offsetof(engine_event_queue_record_t, target)     == 4,
               "target at +4");
_Static_assert(offsetof(engine_event_queue_record_t, code)       == 8,
               "code at +8");
_Static_assert(offsetof(engine_event_queue_record_t, payload32a) == 12,
               "payload32a at +12");
_Static_assert(offsetof(engine_event_queue_record_t, pad16)      == 16,
               "pad16 at +16");
_Static_assert(offsetof(engine_event_queue_record_t, flag5)      == 17,
               "flag5 at +17");
_Static_assert(offsetof(engine_event_queue_record_t, flag6)      == 18,
               "flag6 at +18");
_Static_assert(offsetof(engine_event_queue_record_t, flag7)      == 19,
               "flag7 at +19");
_Static_assert(offsetof(engine_event_queue_record_t, payload32b) == 20,
               "payload32b at +20");

void engine_event_queue_slot_zero(engine_event_queue_state_t *s, int slot)
{
    /* Verbatim retail behavior — see quirk #12.  Only the `code` write
     * uses the caller-supplied `slot` index; every other field zeros
     * slot `state->read_idx`.  At runtime the two indexes always agree
     * (the only non-init caller is the consumer, which passes its own
     * read_idx), but the init loop sweeps slot=0..49 with read_idx
     * pinned to 0 — so slots 1..49 keep their pre-existing non-code
     * bytes. */
    s->ring[slot].code             = 0;

    s->ring[s->read_idx].payload32a = 0;
    s->ring[s->read_idx].payload32b = 0;
    s->ring[s->read_idx].flag6      = 0;
    s->ring[s->read_idx].flag7      = 0;
    s->ring[s->read_idx].flag5      = 0;
    s->ring[s->read_idx].target     = 0;
}

void engine_event_queue_init(engine_event_queue_state_t *s)
{
    s->write_idx         = 0;
    s->read_idx          = 0;
    s->notify_flag       = 0;
    s->consumer_cooldown = 0;

    /* Iterate over the full ring — retail's loop body is identical to
     * a single FUN_00472b40 call per i. */
    for (int i = 0; i < ENGINE_EVENT_QUEUE_CAPACITY; i++) {
        engine_event_queue_slot_zero(s, i);
    }
}

void engine_event_queue_init_wrapper(engine_event_queue_state_t *s)
{
    /* FUN_00472ad0 — 16-byte trampoline calling FUN_00472ae0 with no
     * extra prologue / epilogue work.  Preserved as a separate entry
     * point for grep parity with retail call sites. */
    engine_event_queue_init(s);
}

/* Tail half of FUN_00472bc5 (and the entire body of FUN_00472df5):
 * memcpy 24 bytes scratch → ring[write_idx], bump write_idx with
 * wrap-around, latch the notify flag. */
static void publish_scratch_impl(engine_event_queue_state_t *s)
{
    /* Retail uses a byte-by-byte memcpy (FUN_00401136).  libc memcpy
     * is byte-equivalent for non-overlapping buffers of this size. */
    memcpy(&s->ring[s->write_idx], &s->scratch,
           ENGINE_EVENT_QUEUE_RECORD_BYTES);

    s->write_idx += 1;
    if (s->write_idx > 0x31) {
        s->write_idx = 0;
    }
    s->notify_flag = 1;
}

void engine_event_queue_publish_scratch(engine_event_queue_state_t *s)
{
    publish_scratch_impl(s);
}

bool engine_event_queue_post(engine_event_queue_state_t *s,
                             int32_t target,
                             int32_t audience,
                             int32_t code,
                             int32_t payload32a,
                             uint8_t flag5,
                             uint8_t flag6,
                             uint8_t flag7,
                             int32_t payload32b,
                             const uint8_t player_has_data[7],
                             int local_player)
{
    bool publish = false;

    /* Retail's filter has three structurally distinct branches that
     * all reduce to "publish iff audience == 0 OR audience ==
     * local_player".  Mirroring the dead arms verbatim so a future
     * byte-equivalence diff of the codegen still lines up; commentary
     * inline explains each arm. */
    if (target == 0) {
        /* Broadcast-target arm. */
        if (audience == 0) {
            publish = true;
        } else if (local_player == audience) {
            publish = true;
        } else {
            publish = false;
        }
    } else if (player_has_data[target] == 0) {
        /* Target slot is empty.  Same filter as the broadcast arm. */
        if (audience == 0) {
            publish = true;
        } else if (local_player == audience) {
            publish = true;
        } else {
            publish = false;
        }
    } else {
        /* Target slot is populated.  Retail's source has a comma-
         * expression here:  `((has_data != 0) && (bVar1 = audience ==
         * local, audience == 0))`.  Evaluation order:
         *   1) set publish = (audience == local)
         *   2) test (audience == 0); if true, fall into body to
         *      overwrite publish = true.
         * Either way the outcome matches arms 1 and 2. */
        publish = (local_player == audience);
        if (audience == 0) {
            publish = true;
        }
    }

    /* Scratch is ALWAYS updated, regardless of the filter outcome.
     * This is what lets the save/load path's "scratched but not yet
     * published" path work — call post() to set scratch (filter may
     * fail), then call publish_scratch() to force a commit. */
    s->scratch.audience   = audience;
    s->scratch.target     = target;
    s->scratch.code       = code;
    s->scratch.payload32a = payload32a;
    s->scratch.flag5      = flag5;
    s->scratch.flag6      = flag6;
    s->scratch.flag7      = flag7;
    s->scratch.payload32b = payload32b;
    /* pad16 is left untouched — retail never writes it through the
     * scratch path either. */

    if (publish) {
        publish_scratch_impl(s);
    }

    return publish;
}
