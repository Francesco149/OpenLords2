# tests/Makefile — host-gcc unit tests for the portable parsers.
#
# Built with HOST gcc (not the mingw32 cross-compiler) + ASan + UBSan
# so we can exercise the pure-C code paths under sanitizers.  Run with
#   make -C tests run
# from inside `nix develop`.
#
# Shape mirrored from /opt/src/openrecet/tests/Makefile — the
# canonical test-harness style for our sibling project.  Only the
# Win32-independent code is tested here: src/main.c, src/gfx.c,
# src/audio.c, src/io.c, src/dev_hooks.c all touch the Win32 API
# and are excluded.

CC      ?= gcc
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all \
           -fno-omit-frame-pointer
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I../src \
           -DOPENLORDS2_ROOT=\"$(abspath ..)\" \
           $(SAN) \
           -MMD -MP
LDFLAGS := $(SAN)

# Pure-C source under test.  As more portable modules land
# (e.g. localization-blob decoder, sprite-format reader), add them
# here.
SRCS_UNDER_TEST := ../src/call_trace.c ../src/strutil.c ../src/config.c \
                   ../src/engine_rate.c ../src/engine_phase.c \
                   ../src/engine_delta.c ../src/engine_anim_cycle.c \
                   ../src/engine_tick_cadence.c \
                   ../src/engine_input.c ../src/engine_fps.c \
                   ../src/engine_rng.c ../src/engine_state_hash.c \
                   ../src/engine_tile.c \
                   ../src/engine_delayed_action.c \
                   ../src/engine_dirty_rect.c \
                   ../src/engine_category_buckets.c \
                   ../src/engine_event_queue.c \
                   ../src/engine_handlers_da.c \
                   ../src/engine_math.c \
                   ../src/engine_path.c \
                   ../src/engine_path_field.c \
                   ../src/text_input.c \
                   ../src/font_metrics.c \
                   ../src/localization.c \
                   ../src/gfx_globals.c \
                   ../src/gfx_paint.c \
                   ../src/gfx_drawcap.c \
                   ../src/gfx_glyph.c \
                   ../src/gfx_glyph_table.c \
                   ../src/gfx_sprite.c \
                   ../src/gfx_sprite_rle.c \
                   ../src/gfx_dispatch.c \
                   ../src/gfx_text.c \
                   ../src/gfx_wrap.c \
                   ../src/gfx_line.c \
                   ../src/gfx_caret.c \
                   ../src/gfx_panel.c \
                   ../src/gfx_text_label.c \
                   ../src/gfx_sprite_render.c \
                   ../src/engine_map_render.c \
                   ../src/engine_map_terrain.c \
                   ../src/engine_map_scene.c \
                   ../src/engine_overlay_render.c \
                   ../src/engine_title_render.c \
                   ../src/engine_title_render_globals.c \
                   ../src/engine_button.c \
                   ../src/engine_hud_menubar.c \
                   ../src/engine_map_init.c \
                   ../src/engine_menu_input.c \
                   ../src/engine_confirm_dialog.c \
                   ../src/engine_ingame_input.c \
                   ../src/engine_terrain_info.c \
                   ../src/engine_map_query.c \
                   ../src/engine_strat_cost.c \
                   ../src/engine_territory.c \
                   ../src/engine_territory_globals.c \
                   ../src/engine_army_select.c \
                   ../src/engine_army_move_confirm.c \
                   ../src/engine_workforce_hud.c \
                   ../src/engine_county_panels.c \
                   ../src/engine_tooltip.c \
                   ../src/engine_mp_render.c \
                   ../src/engine_screen_dispatch.c \
                   ../src/engine_load_pump.c \
                   ../src/engine_minimap.c \
                   ../src/engine_hud_panel.c \
                   ../src/engine_battlefield_render.c \
                   ../src/engine_battlefield_render_globals.c \
                   ../src/engine_camera.c \
                   ../src/engine_roster.c \
                   ../src/engine_ranking.c \
                   ../src/engine_npc_schedule.c \
                   ../src/engine_army_economy.c \
                   ../src/game_init.c \
                   ../src/engine_realm.c \
                   ../src/engine_realm_globals.c \
                   ../src/engine_economy_globals.c \
                   ../src/engine_realm_events.c \
                   ../src/engine_realm_upkeep.c \
                   ../src/engine_army_ai.c \
                   ../src/engine_combat.c \
                   ../src/engine_combat_globals.c \
                   ../src/engine_diplomacy.c \
                   ../src/engine_turn.c \
                   ../src/save.c \
                   ../src/save_schema_globals.c \
                   ../src/engine_pl8.c \
                   ../src/io_path.c \
                   ../src/engine_army_state.c \
                   ../src/engine_army_move.c \
                   ../src/input_trace.c

# Test files — each defines one or more `int test_<name>(void)`
# functions registered in test_main.c.
SRCS_TESTS      := test_main.c test_strutil.c test_config.c \
                   test_engine_rate.c test_engine_phase.c \
                   test_engine_delta.c test_engine_anim_cycle.c \
                   test_engine_tick_cadence.c \
                   test_engine_input.c test_engine_fps.c \
                   test_engine_rng.c test_engine_state_hash.c \
                   test_engine_tile.c \
                   test_engine_delayed_action.c \
                   test_engine_dirty_rect.c \
                   test_engine_category_buckets.c \
                   test_engine_event_queue.c \
                   test_engine_handlers_da.c \
                   test_engine_math.c \
                   test_engine_path.c \
                   test_engine_path_field.c \
                   test_text_input.c \
                   test_font_metrics.c \
                   test_localization.c \
                   test_gfx_paint.c \
                   test_gfx_glyph.c \
                   test_gfx_glyph_table.c \
                   test_gfx_sprite.c \
                   test_gfx_sprite_rle.c \
                   test_gfx_dispatch.c \
                   test_gfx_text.c \
                   test_gfx_wrap.c \
                   test_gfx_line.c \
                   test_gfx_caret.c \
                   test_gfx_panel.c \
                   test_gfx_text_label.c \
                   test_gfx_sprite_render.c \
                   test_engine_map_render.c \
                   test_engine_map_scene.c \
                   test_engine_overlay_render.c \
                   test_engine_title_render.c \
                   test_engine_button.c \
                   test_engine_hud_menubar.c \
                   test_engine_map_init.c \
                   test_map_finalize.c \
                   test_engine_minimap.c \
                   test_engine_hud_panel.c \
                   test_engine_menu_input.c \
                   test_engine_confirm_dialog.c \
                   test_engine_ingame_input.c \
                   test_engine_terrain_info.c \
                   test_engine_map_query.c \
                   test_engine_strat_cost.c \
                   test_engine_territory.c \
                   test_engine_army_select.c \
                   test_engine_army_move_confirm.c \
                   test_engine_workforce_hud.c \
                   test_engine_county_panels.c \
                   test_engine_tooltip.c \
                   test_engine_mp_render.c \
                   test_engine_screen_dispatch.c \
                   test_engine_load_pump.c \
                   test_engine_battlefield_render.c \
                   test_engine_camera.c \
                   test_engine_roster.c \
                   test_engine_ranking.c \
                   test_engine_npc_schedule.c \
                   test_engine_army_economy.c \
                   test_game_init.c \
                   test_engine_realm.c \
                   test_engine_realm_events.c \
                   test_engine_realm_upkeep.c \
                   test_engine_army_ai.c \
                   test_engine_combat.c \
                   test_engine_army_state.c \
                   test_engine_army_move.c \
                   test_engine_diplomacy.c \
                   test_engine_turn.c \
                   test_save.c \
                   test_engine_pl8.c \
                   test_io_path.c \
                   test_input_trace.c

OBJS := $(addprefix build/,$(notdir $(SRCS_UNDER_TEST:.c=.o)) \
                            $(SRCS_TESTS:.c=.o))
DEPS := $(OBJS:.o=.d)
OUT  := build/run_tests

# Differential-test shared library — host-built, loaded via ctypes by
# tools/diff_test.py.  Same source as $(SRCS_UNDER_TEST) for the diff
# targets currently wired in, but compiled WITHOUT sanitizers (ASan-
# instrumented .so can't be dlopen'd from a non-ASan Python).
# The diff lib links the full host-clean source set (the same closure the test
# binary links) so stateful diff targets that touch the shared save globals —
# engine_realm passes, etc. — resolve their whole dependency graph (engine_realm
# → engine_army_economy → engine_territory → map_render/camera/…).  Built -fPIC,
# ASan-free, so it can be dlopen'd from a non-ASan Python (load_port_lib).
DIFF_SRCS := $(SRCS_UNDER_TEST)
DIFF_OBJS := $(addprefix build/diff_,$(notdir $(DIFF_SRCS:.c=.o))) \
             build/diff_diff_stubs.o
DIFF_OUT  := build/libengine_diff.so
DIFF_CFLAGS := -std=c11 -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -I../src

# Map-render parity harness — drives engine_map_render with retail's
# captured live map state into an offscreen canvas (see
# tools/map_render_parity.py).  Links the under-test closure (same as the
# diff lib) plus its own main(); ASan stays on so reads against real
# captured data are bounds-checked.
HARNESS_OBJS := $(addprefix build/,$(notdir $(SRCS_UNDER_TEST:.c=.o))) \
                build/map_render_harness.o build/stubs.o
HARNESS_OUT  := build/map_render_harness

# Pass-1 base-terrain parity harness — drives engine_map_terrain with the
# captured terrain dataset (see tools/terrain_render_parity.py).
TERRAIN_HARNESS_OBJS := $(addprefix build/,$(notdir $(SRCS_UNDER_TEST:.c=.o))) \
                        build/terrain_render_harness.o build/stubs.o
TERRAIN_HARNESS_OUT  := build/terrain_render_harness

# Composed-scene parity harness — pass-1 terrain + feature walker in one
# canvas (see tools/scene_render_parity.py).
SCENE_HARNESS_OBJS := $(addprefix build/,$(notdir $(SRCS_UNDER_TEST:.c=.o))) \
                      build/scene_render_harness.o build/stubs.o
SCENE_HARNESS_OUT  := build/scene_render_harness

.PHONY: all run clean diff harness terrain-harness scene-harness
all: $(OUT) $(DIFF_OUT)

harness: $(HARNESS_OUT)

terrain-harness: $(TERRAIN_HARNESS_OUT)

scene-harness: $(SCENE_HARNESS_OUT)

$(TERRAIN_HARNESS_OUT): $(TERRAIN_HARNESS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(TERRAIN_HARNESS_OBJS)

$(SCENE_HARNESS_OUT): $(SCENE_HARNESS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(SCENE_HARNESS_OBJS)

# ASan-built copy of the diff-lib stubs (log_line / io_read_file_at /
# g_sim_* globals) so the standalone harness resolves the Win32-side
# symbols the under-test closure references.
build/stubs.o: diff_stubs.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(HARNESS_OUT): $(HARNESS_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(HARNESS_OBJS)

run: $(OUT)
	./$(OUT)

diff: $(DIFF_OUT)

build/%.o: ../src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/diff_%.o: ../src/%.c | build
	$(CC) $(DIFF_CFLAGS) -c -o $@ $<

build/diff_diff_stubs.o: diff_stubs.c | build
	$(CC) $(DIFF_CFLAGS) -c -o $@ $<

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(DIFF_OUT): $(DIFF_OBJS)
	$(CC) -shared -fPIC -o $@ $(DIFF_OBJS)

build:
	@mkdir -p $@

clean:
	rm -rf build/

-include $(DEPS)
