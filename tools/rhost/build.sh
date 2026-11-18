#!/usr/bin/env bash
# Build rhost.exe into build/rhost/. The host links at 0x200000 without ASLR so retail's 0x400000..
# range is ours (a .bss hole reserves it; rhost.x puts .bss FIRST, so code growth never pushes it).
# Port code for hooks is compiled with the port's own -std (src/Makefile CFLAGS).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$ROOT/src; H=$ROOT/tools/rhost; B=${RHOST_BUILD_DIR:-$ROOT/build/rhost}   # (an experiment builds aside)
CC=${RHOST_CC:-i686-w64-mingw32-gcc}
# the pinned devshell compiler only (tools/dev): /usr/bin's mingw is another GCC, and rhost's own code leaves stack
# residue some retail readers see (field-orders' state differs between GCC 15.2 and 16.2 builds). RHOST_CC overrides.
if [ -z "${RHOST_CC:-}" ] && [[ "$(command -v "$CC")" != /nix/store/* ]]; then
    echo "build.sh: $CC is $(command -v "$CC" || echo missing), not the pinned devshell's — run it through tools/dev" >&2; exit 1
fi
LDF=(-Wl,--image-base,0x200000 -Wl,--large-address-aware -Wl,--disable-dynamicbase -Wl,-T,"$H/rhost.x" -static-libgcc -static)
PORT_STD=${PORT_STD:-$(sed -n 's/^CFLAGS *?= *\(-std=[a-z0-9]*\).*/\1/p' "$S/Makefile")}
mkdir -p "$B"
# one build at a time per tree (parallel agents): the objects, the pad relink and SizeOfImage pinning share $B
exec 9>"$B/.build.lock"; flock 9
OUT=$B/rhost.$$.exe   # linked aside, renamed over rhost.exe when complete: stagers never copy a half-built exe
shopt -s nullglob
SUB_HOOKS=("$H"/rh_hooks_*.c)   # subsystem hook tables (rh_hooks.h)
# port modules linked in for --hook (only pure / retail-layout ones: no Win32 side effects at load)
# engine_rng: rh_hooks.c. The hybrid closure (rh_hooks_ai / _combat / _state / _events / _battle / _economy.c):
# engine_army_ai/_economy/realm/combat/army_move/army_state/realm_events, the screens' models (engine_trade_model,
# engine_diplomacy_action_model, engine_sendsupplies_model) and what they reach, CUT at game_init / engine / main /
# engine_turn / engine_menu_input / config (bridged in the tables). Modules that only DEFINE retail globals —
# save.c, save_schema_globals.c, engine_combat_globals.c, engine_realm_globals.c, engine_economy_globals.c — are NOT
# linked: the tables alias those globals to retail's VAs (and engine.c's g_engine_state → 0x52af94,
# rh_hooks_battle.c; engine_minimap.c's g_minimap_region_grid → 0x52aff0, rh_hooks_economy.c; gfx_globals.c's render
# globals — the canvas, the staged glyph, the text state — rh_hooks_gfx.c). The gfx closure (gfx_*, font_metrics,
# localization) is pure: it draws into the canvas retail's own globals point at.
PORT_OBJS=(); PIDS=()
for f in engine_rng \
         engine_army_ai engine_army_economy engine_realm engine_combat engine_army_move call_trace engine_army_state engine_camera \
         engine_trade_model engine_diplomacy_action_model engine_sendsupplies_model text_input \
         engine_category_buckets engine_diplomacy engine_dirty_rect engine_event_queue engine_map_query engine_map_render \
         engine_math engine_path engine_path_field engine_ranking engine_realm_events engine_realm_upkeep engine_roster engine_territory \
         gfx_dispatch gfx_drawcap gfx_glyph gfx_paint gfx_sprite gfx_sprite_rle strutil \
         engine_retail_call engine_tile $(cd "$S" && ls engine_bf_*.c rl_*.c | grep -v '_globals\.c$' | sed 's/\.c$//') \
         engine_tooltip engine_workforce_hud engine_mp_render engine_noble_faces engine_title_render engine_county_panels \
         engine_battlefield_render engine_trade engine_armory engine_labor engine_treasury \
         gfx_line gfx_text gfx_text_label gfx_wrap gfx_sprite_render gfx_panel gfx_caret font_metrics localization engine_hud_menubar engine_button \
         engine_map_terrain engine_overlay_render engine_map_scene engine_minimap engine_hud_panel engine_terrain_info; do
    $CC $PORT_STD -O2 -fno-omit-frame-pointer -I"$S" -include "$H/rh_hostalloc.h" -DUNICODE=0 -D_UNICODE=0 -c -o "$B/$f.o" "$S/$f.c" &
    PIDS+=($!); PORT_OBJS+=("$B/$f.o")
done
for p in "${PIDS[@]}"; do wait "$p"; done
$CC -std=gnu11 -O2 -Wall -I"$H" -c -o "$B/rh_hostalloc.o" "$H/rh_hostalloc.c"
python3 "$H/gen_callsites.py" "$B/rh_callsites.h" >/dev/null   # real call instructions (redirect_calls)
LINK=($CC -std=gnu11 -O2 -g -Wall -Wno-unused-function -Wno-format-truncation -Wno-cast-function-type -Wno-misleading-indentation
      -I"$S" -I"$H" -I"$B" -include "$H/rh_hostalloc.h" "${LDF[@]}" -o "$OUT"
      "$H"/rhost.c "$H"/rh_heap.c "$H"/rh_media.c "$H"/rh_files.c "$H"/rh_handles.c "$H"/rh_state.c "$H"/rh_shot.c
      "$H"/rh_ddraw.c "$H"/rh_dsound.c "$H"/rh_win.c "$H"/rh_hooks.c "$H"/rh_bot.c "$H"/rh_smack.c "$H"/rh_snap.c "$H"/rh_live.c "$H"/rh_net.c "$H"/rh_real.c "${SUB_HOOKS[@]}"
      "$B/rh_hostalloc.o" "${PORT_OBJS[@]}" -luser32 -lgdi32 -lwinmm -lws2_32)
"${LINK[@]}"
# a FIXED SizeOfImage: the system allocations Windows / wine place right after the image must not
# move when rhost grows — pad .bss up to IMG_SIZE, relink
IMG_SIZE=0x1000000
img_size() { python3 -c "import struct,sys; b=open(sys.argv[1],'rb').read(4096); pe=struct.unpack_from('<I',b,0x3c)[0]; print(struct.unpack_from('<I',b,pe+80)[0])" "$1"; }
trap 'rm -f "$OUT"' EXIT
CUR=$(img_size "$OUT")
PAD=$(( IMG_SIZE - CUR ))
if (( PAD < 0 )); then echo "rhost.exe SizeOfImage $(printf 0x%x $CUR) > $IMG_SIZE: raise IMG_SIZE" >&2; exit 1; fi
if (( PAD > 0 )); then
    for try in 1 2 3; do
        echo "unsigned char rh_img_pad[$PAD] __attribute__((used, aligned(4096)));" > "$B/rh_pad.c"
        $CC -c -o "$B/rh_pad.o" "$B/rh_pad.c"
        "${LINK[@]}" "$B/rh_pad.o"
        NOW=$(img_size "$OUT")
        (( NOW == IMG_SIZE )) && break
        PAD=$(( PAD + IMG_SIZE - NOW ))
    done
    (( NOW == IMG_SIZE )) || { echo "rhost.exe: cannot pin SizeOfImage ($(printf 0x%x $NOW))" >&2; exit 1; }
fi
HOLE=$(i686-w64-mingw32-nm "$OUT" | awk '$3=="_g_retail_hole"{print $1}')
if (( 0x$HOLE > 0x400000 )); then echo "rhost.exe: .bss starts past 0x400000 (hole at 0x$HOLE): retail cannot map" >&2; exit 1; fi
if (( 0x$HOLE + 0x480000 < 0x5e9000 )); then echo "rhost.exe: hole 0x$HOLE+0x480000 does not cover retail's image" >&2; exit 1; fi
mv -f "$OUT" "$B/rhost.exe"
echo "built $B/rhost.exe (port std: $PORT_STD, SizeOfImage pinned $IMG_SIZE, retail hole from 0x$HOLE)"

# --product: openlords2.exe — rhost.c -DRH_PRODUCT (always --standalone --real: the port alone on the real platform; no
# path that maps the exe), a GUI exe with the port's composed data image linked in (build/rl_data.img, regenerated
# here: tools/data_selfcheck.py --compose --strict). It needs only the game's asset files next to it.
if [[ " $* " == *" --product "* ]]; then
    DI=$ROOT/build/rl_data.img
    python3 "$ROOT/tools/data_selfcheck.py" --compose "$DI" --strict --no-md --jobs 8 >/dev/null
    $CC -c -DDI_PATH="\"$DI\"" -o "$B/rh_product_di.o" "$H/rh_product_di.S"
    PROD=("${LINK[@]}")
    for i in "${!PROD[@]}"; do
        [[ "${PROD[$i]}" == "$OUT" ]] && PROD[$i]="$B/openlords2.$$.exe"
    done
    trap 'rm -f "$OUT" "$B/openlords2.$$.exe"' EXIT
    "${PROD[@]}" -DRH_PRODUCT "$B/rh_product_di.o" -mwindows   # (rhost.c stays first: its .bss hole lands at 0x202000)
    PH=$(i686-w64-mingw32-nm "$B/openlords2.$$.exe" | awk '$3=="_g_retail_hole"{print $1}')
    if (( 0x$PH > 0x400000 || 0x$PH + 0x480000 < 0x5e9000 )); then echo "openlords2.exe: the hole 0x$PH does not cover retail's image" >&2; exit 1; fi
    mv -f "$B/openlords2.$$.exe" "$B/openlords2.exe"
    echo "built $B/openlords2.exe (the product: standalone + real platform, data image $(stat -c %s "$DI") B embedded)"
fi
