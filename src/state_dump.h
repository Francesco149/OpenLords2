/*
 * src/state_dump.h — save-schema state snapshot for trace_studio.
 *
 * At each segtrace {capture} instant the port dumps the 225-entry save
 * schema (save_schema_table.h) to <label>.state.json: scalar entries
 * (size ≤ 4) as unsigned ints, big blocks as 32-bit FNV-1a hashes
 * ("h:xxxxxxxx").  The retail agent reads the SAME slots by VA at the
 * same ticks (tools/frida/openlords2-agent.js stateDumpOnce — the hash
 * must match bit-for-bit), so the studio can field-diff sim state per
 * captured label.  Format:
 *
 *     {"slots":[123, "h:a1b2c3d4", …]}        (slot order = schema order)
 */

#ifndef OPENLORDS2_STATE_DUMP_H
#define OPENLORDS2_STATE_DUMP_H

#include <stdint.h>

/* 32-bit FNV-1a over n bytes (the block-hash both sides share). */
uint32_t state_dump_fnv1a(const void *p, uint32_t n);

/* Write the schema snapshot.  Returns 0 on success. */
int state_dump_write(const char *path);

#endif /* OPENLORDS2_STATE_DUMP_H */
