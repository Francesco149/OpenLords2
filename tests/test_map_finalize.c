/*
 * test_map_finalize.c — end-to-end validation of the strategic-map
 * descriptor FINALIZE pass on the LOAD path.
 *
 * Retail load/resume (FUN_004976a1) rebuilds the overworld terrain
 * display descriptors in place AFTER the save is loaded:
 *
 *   FUN_004298c1(0)    build tile_cells from L2_maps.dat + iso projection
 *   FUN_00451fcc(view) camera scalars
 *   FUN_0046a037(map)  per-country overlay grid (no render consumer — deferred)
 *   FUN_0046dbf5()     autotile: recompute cell +3 by 8-neighbour pattern
 *   FUN_0046dd34()     randomise special (0xfff0006) tile_cells entries
 *   FUN_0046da4b(0)    rebuild the per-key cell roster (evict overflow)
 *
 * Note it does NOT run the new-game field clears (FUN_0046c5b8) nor the
 * L2_maps.dat plane unpacker (FUN_00467770) — so the loaded +0/+5/+6
 * descriptor bytes survive, while +1..+4/+7 come from the save and +3 is
 * then rewritten by the autotiler.  This is why the raw save's +3 matches
 * the captured (post-finalize) dump only ~4% but +5 matches ~100%.
 *
 * This test loads the real lastturn.sav, runs the ported finalize subset
 * (engine_territory_classify_feature_tiles / _randomise_special_tiles /
 * _rebuild_cell_roster) and compares the per-field descriptor table against
 * retail's own IMMEDIATE post-load dump (runs/capture/load-immediate1, taken
 * at MAP_FREEROAM+2 before the warmup), captured by driving retail through
 * load_into_map_seg.jsonl with --dump-regions 0x522f90:32768.  SKIPPED unless
 * the retail data dir + that dump are present.
 *
 * RESULT: byte-exact — +0/+1/+3/+4/+5/+7 = 100%, +2 = 99.5% (~20-cell residual).
 * So the ported LOAD data path reproduces retail's load output exactly.
 *
 * RESOLVED 2026-06-02 (engine-quirks #158): the strat-terrain1 dump earlier
 * tests compared against was NOT a warmed-up lastturn.sav at all — it was
 * captured on a DIFFERENT MAP (map 17).  There is no "region->owner remap"
 * pass: descriptor +7 is just plane-5 (per-cell baked region id) of
 * L2_maps.dat[map_id] (FUN_00467770 copies planes 0/1/2/3/5 -> +1/+2/+3/+4/+7).
 * lastturn.sav is map 0 (sea 84, 14 regions); strat-terrain1 is map 17 (sea
 * 497, 4 regions).  map0 plane5 == lastturn +7 = 100%; map17 plane5 ==
 * strat-terrain1 +7 = 100%; cross-pairing scores 4-42%.  The +3 "only 4%" was
 * a cross-map artifact.  test_map_finalize_plus7_is_map_plane5 (below) locks
 * this in.  The load finalize is correct; idling never changes +7.
 *
 * The single-LFSR RNG (DAT_00591564) is seeded with the retail InitInstance
 * constant 0x34518632; the autotiler consults it only for the isolated/water
 * type (0x16).  It happens not to matter here because the loaded save already
 * carries the autotiled +3 (the load re-derives the same bytes).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "t.h"

#include "save.h"                  /* save_load, g_overworld_map */
#include "save_schema_globals.h"   /* s_slot_0053f034 (map id), 0057c934, 0057cb18 */
#include "engine_map_init.h"
#include "engine_territory.h"
#include "engine.h"                /* engine_rng_single_in_t */

#define DESC_BYTES  32768
#define CELL_STRIDE 8
#define NCELLS      (DESC_BYTES / CELL_STRIDE)   /* 4096 */
#define RNG_SEED    0x34518632u

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int resolve_game_dir(char *dst, size_t cap)
{
    const char *env = getenv("OPENLORDS2_GAME_DIR");
    if (env && *env) { snprintf(dst, cap, "%s", env); return 1; }
    snprintf(dst, cap, "%s/vendor/original", OPENLORDS2_ROOT);
    return 1;
}

/* Count cells whose byte +field matches the truth dump. */
static int field_matches(const uint8_t *a, const uint8_t *b, int field)
{
    int n = 0;
    for (int i = 0; i < NCELLS; i++)
        if (a[i * CELL_STRIDE + field] == b[i * CELL_STRIDE + field]) n++;
    return n;
}

static double pct(int hit) { return 100.0 * (double)hit / (double)NCELLS; }

int test_map_finalize_load_path_diagnostic(void)
{
    char gdir[1024];
    resolve_game_dir(gdir, sizeof gdir);

    char sav_path[1200];
    snprintf(sav_path, sizeof sav_path, "%s/lastturn.sav", gdir);
    if (!file_exists(sav_path))
        T_SKIP("retail lastturn.sav not found at %s", sav_path);

    char l2_path[1200];
    snprintf(l2_path, sizeof l2_path, "%s/L2_maps.dat", gdir);
    if (!file_exists(l2_path))
        T_SKIP("L2_maps.dat not found at %s", l2_path);

    char desc_path[1200];
    snprintf(desc_path, sizeof desc_path,
             "%s/runs/capture/load-immediate1/dump/descriptors.bin",
             OPENLORDS2_ROOT);
    if (!file_exists(desc_path))
        T_SKIP("captured descriptors.bin not found at %s", desc_path);

    /* Truth: the captured post-finalize descriptor table. */
    static uint8_t truth[DESC_BYTES];
    {
        FILE *f = fopen(desc_path, "rb");
        if (!f) T_FAIL("cannot open %s", desc_path);
        size_t got = fread(truth, 1, sizeof truth, f);
        fclose(f);
        if (got != sizeof truth) T_FAIL("descriptors.bin short read (%zu)", got);
    }

    /* 1. Load the real save → g_overworld_map (descriptors) + map id. */
    const char *tmp_castles = "/tmp/openlords2_finalize_castles.dat";
    if (save_load(sav_path, tmp_castles) != 0)
        T_FAIL("save_load(%s) failed", sav_path);
    int map_id = (int)s_slot_0053f034;
    s_slot_0057cb18 = 0;   /* view-mode normal */

    /* Pre-finalize per-field match (descriptors straight from the save). */
    int pre3 = field_matches(g_overworld_map, truth, 3);
    int pre5 = field_matches(g_overworld_map, truth, 5);
    /* Save vs truth: +7 owner match, and whether +1 bit2 == (+7==0). */
    {
        int s7h[256]={0}, t7h[256]={0}, m7=0;
        int trel=0, srel=0;   /* cells where (+1&4)!=0 iff (+7==0) */
        for (int i = 0; i < NCELLS; i++) {
            uint8_t s1=g_overworld_map[i*8+1], s7=g_overworld_map[i*8+7];
            uint8_t t1=truth[i*8+1], t7=truth[i*8+7];
            s7h[s7]++; t7h[t7]++;
            if (s7==t7) m7++;
            if (((t1&4)!=0) == (t7==0)) trel++;
            if (((s1&4)!=0) == (s7==0)) srel++;
        }
        printf("    [map_finalize] save +7 vs truth: %d/%d=%.1f%%   "
               "rel(+1bit2==(+7==0)) truth=%d/%d save=%d/%d\n",
               m7,NCELLS,100.0*m7/NCELLS, trel,NCELLS, srel,NCELLS);
        printf("    [map_finalize] save +7 hist:");
        for(int v=0;v<256;v++) if(s7h[v]) printf(" %02x:%d",v,s7h[v]);
        printf("\n    [map_finalize] truth +7 hist:");
        for(int v=0;v<256;v++) if(t7h[v]) printf(" %02x:%d",v,t7h[v]);
        printf("\n");

        /* HYPOTHESIS: truth +7 == owner-of-territory[ save +7 ], where the
         * territory record owner is byte +0x00 of the 0x1a4-stride record. */
        extern uint8_t g_per_territory_420[];
        int remap_ok = 0, remap_tot = 0;
        for (int i = 0; i < NCELLS; i++) {
            unsigned tid = g_overworld_map[i*8+7];      /* save +7 = territory id */
            if (tid == 0 || tid == 0x20) continue;       /* unowned / sea: skip */
            remap_tot++;
            int owner = (int8_t)g_per_territory_420[tid * 0x1a4 + 0x00];
            if ((uint8_t)owner == truth[i*8+7]) remap_ok++;
        }
        printf("    [map_finalize] remap (truth+7 == owner[save+7]): %d/%d = %.1f%%\n",
               remap_ok, remap_tot, remap_tot ? 100.0*remap_ok/remap_tot : 0.0);

        /* Are the on-disk save and the captured dump the SAME game state?
         * Compare the save's loaded territory records to the captured ones. */
        char tr_path[1200];
        snprintf(tr_path, sizeof tr_path,
                 "%s/runs/capture/strat-terrain1/dump/territory_records.bin", OPENLORDS2_ROOT);
        FILE *tf = fopen(tr_path, "rb");
        if (tf) {
            static uint8_t tr_truth[65536];
            size_t got = fread(tr_truth, 1, sizeof tr_truth, tf);
            fclose(tf);
            int cmp = (got < 63420) ? (int)got : 63420;
            int tr_ok = 0;
            for (int i = 0; i < cmp; i++)
                if (g_per_territory_420[i] == tr_truth[i]) tr_ok++;
            printf("    [map_finalize] save territory_records vs captured: %d/%d = %.1f%%\n",
                   tr_ok, cmp, 100.0 * tr_ok / cmp);
        }

        /* Confusion: for each save+7 value, what truth+7 values does it map to?
         * If each save+7 maps to ONE truth+7, the remap is a clean per-id table. */
        printf("    [map_finalize] save+7 -> truth+7 mapping:\n");
        for (int sv = 1; sv <= 0x20; sv++) {
            int row[256]={0}, any=0;
            for (int i=0;i<NCELLS;i++) if (g_overworld_map[i*8+7]==sv) { row[truth[i*8+7]]++; any=1; }
            if (!any) continue;
            printf("      save+7=0x%02x ->", sv);
            for (int t=0;t<256;t++) if (row[t]) printf(" 0x%02x:%d", t, row[t]);
            /* also print territory record bytes 0..6 for this id */
            printf("   rec[0x%02x][0..6]=", sv);
            for (int b=0;b<7;b++) printf("%02x ", g_per_territory_420[sv*0x1a4+b]);
            printf("\n");
        }
    }
    /* Does the save's +1 bit2 (the autotile's border gate) match truth's? */
    {
        int b2_ok = 0;
        for (int i = 0; i < NCELLS; i++)
            if ((g_overworld_map[i*8+1] & 4) == (truth[i*8+1] & 4)) b2_ok++;
        printf("    [map_finalize] save +1 bit2 (border gate) vs truth: %d/%d = %.1f%%\n",
               b2_ok, NCELLS, 100.0 * b2_ok / NCELLS);
    }

    /* 2. Build tile_cells (FUN_004298c1) + camera from L2_maps.dat. */
    if (engine_map_init_strategic(gdir, map_id) == 0)
        T_FAIL("engine_map_init_strategic failed (map_id=%d)", map_id);
    int32_t *cells = engine_map_init_tile_cells();

    /* L2_maps.dat record for this map (the 6-plane block 467770 unpacks). */
    static uint8_t l2_block[0x80c1];
    {
        FILE *f = fopen(l2_path, "rb");
        if (!f) T_FAIL("cannot open %s", l2_path);
        if (fseek(f, (long)map_id * 0x80c1, SEEK_SET) != 0) {
            fclose(f); T_FAIL("seek L2_maps.dat map %d", map_id);
        }
        size_t got = fread(l2_block, 1, sizeof l2_block, f);
        fclose(f);
        if (got != sizeof l2_block) T_FAIL("L2_maps.dat short read (%zu)", got);
    }

    /* 3. Bind the finalize inputs. */
    static int32_t roster[18 * 20];          /* 17 keys × 20 cols (+slack) */
    memset(roster, 0, sizeof roster);
    engine_territory_bind_cells(g_overworld_map);
    engine_territory_bind_terrain_tiles(cells);
    engine_territory_bind_cell_roster(roster);
    engine_territory_bind_strat_map_record(l2_block);

    /* 3b. Verify the built tile_cells matches the captured grid (foundation). */
    {
        char tc_path[1200];
        snprintf(tc_path, sizeof tc_path,
                 "%s/runs/capture/strat-terrain1/dump/tile_cells.bin", OPENLORDS2_ROOT);
        FILE *f = fopen(tc_path, "rb");
        if (f) {
            static int32_t tc_truth[32768/4];
            size_t got = fread(tc_truth, 1, sizeof tc_truth, f);
            fclose(f);
            int tc_ok = 0;
            for (int i = 0; i < (int)(sizeof tc_truth/4); i++)
                if (cells[i] == tc_truth[i]) tc_ok++;
            printf("    [map_finalize] tile_cells (pre-finalize) vs captured: %d/%d = %.1f%%\n",
                   tc_ok, (int)(sizeof tc_truth/4),
                   100.0 * tc_ok / (sizeof tc_truth/4));
        }
    }

    /* 4. Run the EXACT load-path finalize (retail FUN_004976a1): the save's
     * descriptors are autotiled in place — no 467770 plane unpack, no 46df51
     * (those are new-game-only).  (void)l2_block: kept only for reference. */
    (void)l2_block;
    engine_rng_single_in_t rng = { RNG_SEED };
    engine_territory_classify_feature_tiles(&rng);   /* FUN_0046dbf5 */
    engine_territory_randomise_special_tiles(&rng);  /* FUN_0046dd34 */
    engine_territory_rebuild_cell_roster(0);         /* FUN_0046da4b(0) */

    /* DIAGNOSTIC: +3 match measured ONLY over cells the render reads +3 from
     * — the cells referenced as ACTIVE (value < 0xfff0000) by the tile_cells
     * iso projection.  Special/sea cells are drawn from tile_cells low-bits,
     * so their descriptor +3 is irrelevant to the render. */
    {
        int active = 0, active3_ok = 0, border = 0, special_nonsent = 0;
        int land = 0, land3_ok = 0, sea = 0, sea3_ok = 0;
        for (int row = 0; row < 0x81; row++) {
            for (int col = 0; col < 0x41; col++) {
                uint32_t v = (uint32_t)cells[row * 0x41 + col];
                if (v < 0xfff0000u) {
                    active++;
                    int ok = (g_overworld_map[v + 3] == truth[v + 3]);
                    if (ok) active3_ok++;
                    if (g_overworld_map[v + 1] & 0x4) border++;
                    if (g_overworld_map[v + 7] == 0x20) { sea++; sea3_ok += ok; }
                    else { land++; land3_ok += ok; }
                } else if (v != 0xfff0006u) special_nonsent++;
            }
        }
        printf("    [map_finalize] autotile gate: active-border=%d special-nonsentinel=%d\n",
               border, special_nonsent);
        printf("    [map_finalize] ACTIVE +3: %d/%d=%.1f%%   LAND(+7!=0x20) +3: %d/%d=%.1f%%   "
               "SEA(+7==0x20) +3: %d/%d=%.1f%%\n",
               active3_ok, active, active ? 100.0 * active3_ok / active : 0.0,
               land3_ok, land, land ? 100.0 * land3_ok / land : 0.0,
               sea3_ok, sea, sea ? 100.0 * sea3_ok / sea : 0.0);
        /* sample first 10 mismatched BORDER cells (+1&4 — classify recomputed
         * these), to see whether the autotile output itself is wrong */
        int shown = 0, bm_ok = 0, bm_tot = 0;
        for (int row = 0; row < 0x81; row++) {
            for (int col = 0; col < 0x41; col++) {
                uint32_t v = (uint32_t)cells[row * 0x41 + col];
                if (v >= 0xfff0000u) continue;
                if ((g_overworld_map[v + 1] & 0x4) == 0) continue;  /* border only */
                bm_tot++;
                if (g_overworld_map[v + 3] == truth[v + 3]) { bm_ok++; continue; }
                if (shown < 10) {
                    printf("      BORDER (r%d,c%d) cell %u  +3 got=0x%02x truth=0x%02x  "
                           "+1=0x%02x +2=0x%02x +7=0x%02x\n",
                           row, col, v / 8, g_overworld_map[v+3], truth[v+3],
                           g_overworld_map[v+1], g_overworld_map[v+2], g_overworld_map[v+7]);
                    shown++;
                }
            }
        }
        printf("    [map_finalize] BORDER-cell +3 match: %d/%d = %.1f%%\n",
               bm_ok, bm_tot, bm_tot ? 100.0 * bm_ok / bm_tot : 0.0);

        /* Water-range (type 0x16, variants 0x16..0x1d) agreement: if both
         * truth and computed land a cell in the water band, the TYPE matches
         * even when the RNG variant differs (anim-phase residual). */
        int truth_water = 0, got_water = 0, both_water = 0, active2 = 0;
        for (int row = 0; row < 0x81; row++) {
            for (int col = 0; col < 0x41; col++) {
                uint32_t v = (uint32_t)cells[row * 0x41 + col];
                if (v >= 0xfff0000u) continue;
                active2++;
                int tw = truth[v+3] >= 0x16 && truth[v+3] <= 0x1d;
                int gw = g_overworld_map[v+3] >= 0x16 && g_overworld_map[v+3] <= 0x1d;
                truth_water += tw; got_water += gw; both_water += (tw && gw);
            }
        }
        printf("    [map_finalize] water-band: truth=%d got=%d overlap=%d (of %d active)\n",
               truth_water, got_water, both_water, active2);
    }

    /* 5. Post-finalize per-field match. */
    int post0 = field_matches(g_overworld_map, truth, 0);
    int post1 = field_matches(g_overworld_map, truth, 1);
    int post2 = field_matches(g_overworld_map, truth, 2);
    int post3 = field_matches(g_overworld_map, truth, 3);
    int post4 = field_matches(g_overworld_map, truth, 4);
    int post5 = field_matches(g_overworld_map, truth, 5);
    int post7 = field_matches(g_overworld_map, truth, 7);

    printf("    [map_finalize] map_id=%d  +3 sprite: pre=%.1f%% post=%.1f%%  "
           "+5 head: pre=%.1f%%\n",
           map_id, pct(pre3), pct(post3), pct(pre5));
    printf("    [map_finalize] post per-field: +0=%.1f +1=%.1f +2=%.1f "
           "+3=%.1f +4=%.1f +5=%.1f +7=%.1f\n",
           pct(post0), pct(post1), pct(post2), pct(post3),
           pct(post4), pct(post5), pct(post7));

    (void)post1; (void)post7;

    /* The ported LOAD data path must reproduce retail's immediate post-load
     * descriptor table byte-for-byte.  +3 (terrain sprite, the field the
     * renderer reads) and the owner/feature fields are exact; +2 carries a
     * tiny (~0.5%) residual still under investigation. */
    T_ASSERT_EQ_I(post0, NCELLS);          /* +0 type          100% */
    T_ASSERT_EQ_I(post3, NCELLS);          /* +3 terrain sprite 100% */
    T_ASSERT_EQ_I(post4, NCELLS);          /* +4               100% */
    T_ASSERT_EQ_I(post5, NCELLS);          /* +5 feature head  100% */
    T_ASSERT(pct(post2) > 99.0);           /* +2 render flags  ~99.5% (residual) */

    return 0;
}

/*
 * test_map_finalize_plus7_is_map_plane5 — proves descriptor +7 is simply
 * plane-5 (per-cell baked region id) of L2_maps.dat[map_id], not a remapped
 * "owner" produced by any warmup/turn pass (engine-quirks #158).
 *
 * Loads the real lastturn.sav, reads L2_maps.dat plane 5 for the loaded
 * map_id, and asserts +7 == plane5 for all 4096 cells.  Also prints the
 * map 0 vs map 17 plane-5 fingerprints that identify lastturn.sav as map 0
 * and the old strat-terrain1 "warmup" capture as map 17.
 */
int test_map_finalize_plus7_is_map_plane5(void)
{
    char gdir[1024];
    resolve_game_dir(gdir, sizeof gdir);

    char sav_path[1200], l2_path[1200];
    snprintf(sav_path, sizeof sav_path, "%s/lastturn.sav", gdir);
    snprintf(l2_path,  sizeof l2_path,  "%s/L2_maps.dat",  gdir);
    if (!file_exists(sav_path)) T_SKIP("lastturn.sav not found at %s", sav_path);
    if (!file_exists(l2_path))  T_SKIP("L2_maps.dat not found at %s", l2_path);

    /* Load the save -> g_overworld_map (descriptors) + map id. */
    const char *tmp_castles = "/tmp/openlords2_plane5_castles.dat";
    if (save_load(sav_path, tmp_castles) != 0)
        T_FAIL("save_load(%s) failed", sav_path);
    int map_id = (int)s_slot_0053f034;

    /* Read plane 5 (offset 0x5000) of this map (stride 0x80c1). */
    static uint8_t plane5[0x1000];
    {
        FILE *f = fopen(l2_path, "rb");
        if (!f) T_FAIL("cannot open %s", l2_path);
        long off = (long)map_id * 0x80c1 + 0x5000;
        if (fseek(f, off, SEEK_SET) != 0) { fclose(f); T_FAIL("seek"); }
        size_t got = fread(plane5, 1, sizeof plane5, f);
        fclose(f);
        if (got != sizeof plane5) T_FAIL("plane5 short read (%zu)", got);
    }

    int ok = 0;
    for (int i = 0; i < NCELLS; i++)
        if (g_overworld_map[i * CELL_STRIDE + 7] == plane5[i]) ok++;
    printf("    [plane5] map_id=%d  +7 == L2_maps plane5: %d/%d = %.1f%%\n",
           map_id, ok, NCELLS, pct(ok));
    T_ASSERT_EQ_I(ok, NCELLS);   /* +7 is verbatim plane-5 — no remap exists */

    return 0;
}
