/* engine_path_field.h — strategic-map cost-field pathfinder (Dijkstra flood +
 * gradient-descent trace).  Pure-C port of the FUN_0046f700 group, the engine's
 * *second* pathfinder (distinct from the greedy FUN_00472392 group in
 * engine_path.c).  Reached from the army-movement arrival handler
 * (retail 0x467196, not yet ported): when a marching stack reaches a waypoint it
 * floods a cost
 * field outward from the destination, then traces the cheapest descent back to
 * the stack and copies that route into the stack's waypoint list.
 *
 *   FUN_0046f700 → engine_path_field_fill         cost-field flood (Dijkstra)
 *   FUN_004701ac → engine_path_field_trace        gradient-descent path trace
 *   FUN_004708d0 → engine_path_field_clear_slot   zero a path scratch slot
 *   FUN_004707be → engine_path_field_copy_record  scratch slot → army record
 *
 * The per-cell, per-direction move-cost tables (retail base 0x4f3ffe) are a
 * map-derived INPUT, populated elsewhere (retail 0x46ee31); bind them with
 * engine_path_field_bind_terrain.  Everything else (the two cost fields, the two
 * BFS ring queues, the per-slot waypoint scratch) is internal recompute scratch.
 */
#ifndef OPENLORDS2_ENGINE_PATH_FIELD_H
#define OPENLORDS2_ENGINE_PATH_FIELD_H

#include <stdint.h>

/* Grid is 0x40×0x40; cell index = row*0x40 + col.  A path scratch slot holds up
 * to 0x96 (150) waypoints. */
#define ENGINE_PATH_FIELD_DIM       0x40
#define ENGINE_PATH_FIELD_CELLS     (ENGINE_PATH_FIELD_DIM * ENGINE_PATH_FIELD_DIM)
#define ENGINE_PATH_FIELD_MAX_WP    0x96
#define ENGINE_PATH_FIELD_SLOTS     8     /* scratch slots (0 = transient path) */

/* Bind the move-cost input region (retail 0x4f3ffe): a 16-bit-per-entry grid of
 * 8-directional step costs + a diagonal-forbid flag, indexed by cell.  The base
 * is the *NW* sub-table (0x4f3ffe); the port reads each direction at its fixed
 * cell offset from this base.  Must point at a buffer ≥ (ENGINE_PATH_FIELD_CELLS
 * + 0x82) int16 entries. */
void engine_path_field_bind_terrain(const int16_t *terrain_4f3ffe);

/* FUN_0046f700: flood a cost field outward from (col,row).  `slot` selects the
 * field/queue pair (the local player's id → the persistent field 0x500c30,
 * anything else → the transient field 0x4f0380).  When `blocked_mode` is set,
 * any step cost > 1 is clamped to 100 (heavily penalise costly-but-passable
 * cells).  The source cell gets cost 1; each relaxed neighbour gets
 * cost(cell)+step. */
void engine_path_field_fill(int slot, int col, int row, int blocked_mode);

/* FUN_004701ac: trace the cheapest descent of the filled field from (col,row)
 * down to the cost-1 source, recording each cell into scratch slot `slot`.
 * Returns 1 if the descent reached the source (cost < 2), 0 if it stalled. */
int engine_path_field_trace(int slot, int col, int row);

/* FUN_004708d0: zero scratch slot `slot` (count + waypoint list). */
void engine_path_field_clear_slot(int slot);

/* FUN_004707be: copy scratch slot `slot` into army record `army_idx`'s waypoint
 * list (+0x1c count, +0x1d/+0x1e col/row pairs), zero-filling the tail. */
void engine_path_field_copy_record(int slot, int army_idx);

/* Test / retail-diff accessors. */
int16_t engine_path_field_cost(int slot, int cell);   /* field[cell] for slot's field */
int     engine_path_field_wp_count(int slot);
void    engine_path_field_wp(int slot, int i, int *col, int *row);

#endif /* OPENLORDS2_ENGINE_PATH_FIELD_H */
