/* rh_hooks_tile.c — engine_tile's ports (src/engine_tile.c: the cell grid's path / stage scratch refresh, the
 * footprint list, the 3×3 footprint's neighbour flags) vs retail, on identical state — through the battlefield's
 * retail-layout wrappers (src/engine_bf_common.c bf_tile_*: the arrays are the save block's / the scratches,
 * aliased at retail's VAs; retail's EAX modelled). */
#include "rh_port.h"
#include "engine_bf.h"

#define RH_TABLE RH_HOOKS_TILE
extern hook_t RH_HOOKS_TILE[];
enum { X_tile_refresh_path, X_tile_refresh_stage, X_tile_push_footprint, X_tile_neighbor_flags };

HOOK_R(tile_refresh_path,   0, bf_tile_refresh_path())
HOOK_R(tile_refresh_stage,  0, bf_tile_refresh_stage())
HOOK_R(tile_push_footprint, 1, bf_tile_push_footprint(I(0)))
HOOK_V(tile_neighbor_flags, 3, bf_tile_neighbor_flags(I(0), I(1), I(2)))

hook_t RH_HOOKS_TILE[] = {
    { "tile_refresh_path", 0x471c1fu, (void *)w_tile_refresh_path, 0, 0, 0, 0, "" },
    { "tile_refresh_stage", 0x471d30u, (void *)w_tile_refresh_stage, 0, 0, 0, 0, "" },
    { "tile_push_footprint", 0x48ee46u, (void *)w_tile_push_footprint, 0, 0, 0, 0, "" },
    { "tile_neighbor_flags", 0x4921e5u, (void *)w_tile_neighbor_flags, 0, 0, 0, 0, "" },
};
const int RH_HOOKS_TILE_N = sizeof RH_HOOKS_TILE / sizeof *RH_HOOKS_TILE;
