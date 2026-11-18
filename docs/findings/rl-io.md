# rl io — the platform layer on retail's layout (2026-11-04)

Partition `io` of family `rl` (`tools/bf/parts.py`; design: `battlefield-sim-port.md`): the asset / file loaders,
the WAV cues and the effect bank, the music streamer, the movies, the debug log status.txt, the sheet / buffer
frees, the boot / new-game resets. 70 functions, 21910 B, all in `src/rl_io.c`; rows in `tools/rhost/rh_hooks_rl_io.c`.

## Status (2026-11-04)

All 70 ported. `rhost.py verify --target wine -j 3 --hooks '^rl_'` over the whole corpus (51 scenarios, after master
7965fc7): 70/70 reached, 13.77 M shadowed calls, 0 divergences, 0 failed runs; hybrid PASS (every row replaced,
`corpus --check`). 0x497500's 51 calls are all "skipped" (its sheet set 0x4984dc grows the arena): the hybrid pass
covers it. Least-reached: 0x4aee3e / 0x4aecf0 (1 scenario: the save list), 0x4b3acd (5), 0x42e298 / 0x42e362 /
0x4b2575 (6: the movie palette / end). Never exercised (no MP in the corpus): 0x4b8c3a past its `DAT_00553030 == 0`
exit; 0x4996c8's failure path; 0x4c05a0's report paths.

## The platform shim (`src/rl_io_plat.{c,h}`)

A port running inside rhost must reach the platform the way retail does, or it bypasses rhost's virtual layers.

| retail reaches | the shim, bound (rhost) | standalone |
|---|---|---|
| Win32 / winmm / smackw32 imports (IAT) | a call through retail's own IAT slot, read at call time (rhost's overrides: file overlay, fake handles, heap arena, DirectSound fakes, virtual clock, the mapped smackw32) | the real API; smackw32 loaded lazily by ordinal (absent: SmackOpen fails, as retail tolerates) |
| stateful CRT (fd table `__pioinfo`, errno, `time`'s tm / `asctime`'s buffer, the debug heap) | retail's own CRT function (cdecl, no mirror bracket: the CRT touches no port mirror) | the host CRT; `malloc` builds the debug CRT's block (header + 0xfd no-man's-land, linked into `_pFirstBlock`) so the ported `_free_dbg` frees it |
| pure CRT (strcmp, strcat, memset, memcpy) | not shimmed: the host's (`rl_strcmp` keeps retail's -1 / 0 / +1) | same |
| COM (DirectSound, DirectDraw, DirectPlay) | through the object's own vtable (whatever object retail created) | same |
| the stream timer's callback pointer | retail's entry 0x426dd5 (rhost patches it: shadow / replace by mode) | a `CALLBACK` thunk into `rl_426dd5` |

rhost binds it from the table file (`rl_plat_bind_retail()`, a constructor); unbound = the standalone. The X-macro
lists in `rl_io_plat.h` hold every import's IAT slot VA and every CRT VA.

## Three host layers the shadow does not roll back (worked around in the table file)

`rh_shadow_ex` rolls retail memory, the heap meta, the media pool, DirectSound, the handle sequence and the window
back between the retail and the port side. Three more things matter to this partition; `rh_hooks_rl_io.c` handles
them per row (a shared fix belongs in rh_state / rh_handles / rh_smack):

1. **An mmio file's read position** (`HOOK_IO`). The music stream and a cue's file inside 0x4b984f are read across the
   call: unrolled, the port side read on where retail stopped (EOF → error 3 in 0x4b9652) and the file was read twice
   (the stream ended early: every shadow pass left the corpus). The row notes the logical position (mmioSeek
   SEEK_CUR through retail's import) and seeks back before the port side.
2. **The caller's stack behind a pointer argument** (`HOOK_IO`). The stack is not state: 0x4b9652 counts the caller's
   MMCKINFO down — the port side saw retail's count and read nothing — and 0x4b984f / 0x4b92e0 / 0x4b95ba / 0x4b97f9
   write the caller's locals. The row saves the listed arguments' bytes, restores them before the port side and
   COMPARES the port's results with retail's (a mismatch flips the compared return value and logs
   `DIVERGE-STACK`): these outputs are verified like state.
3. **smackw32 itself** (`--movies`; `HOOK_SMK` on 0x42d91b / 0x42da18 / 0x42dbc7 / 0x42e320). Its image and CRT heap
   (0x18000000..0x1b000000) and its .smk file are outside the state: unrolled, the port's SmackOpen got another heap
   block (the SMACK* differed), the next step found the frame already decoded (SmackWait), and a doubled SmackClose
   crashed smackw32 (boot-movies, pass 1). The row snapshots smackw32's committed writable pages and the position of
   every live faked disk handle, and puts both back before the port side. Not covered: rh_smack's waveOut queue and
   span bump pointer (statics) — no divergence in the corpus.

Also: the timer callback 0x426dd5 is `__stdcall` (winmm TIMECALLBACK, 5 arguments, no call site): its row has an
entry stub ending `ret $0x14` (`HOOK_IO_CB`). Its generated 0-argument signature in `rl_io.h` was wrong; it is now
`rl_426dd5(id, msg, dwUser, dw1, dw2)` (no code calls it).

## Return values

Retail's EAX is modelled wherever a path defines it (HOOK_R: compared). HOOK_V where a path passes a callee's or the
caller's EAX through untouched — the hook wrapper does not preserve that EAX either (0x4ae1b8 returned 0x4ae2ab's
"EAX" = the wrapper's): 0x41b013, the empty functions, 0x4983b7, the forget-stores, 0x4ae1b8, 0x4ae2bd, 0x4262eb,
0x426360, 0x4c05a0 / 0x4c0580, the sheet frees, the cue wrappers with an out-of-range argument, 0x46a101.

## Quirks (retail ground truth)

1. 0x4af116 / 0x4af312: the SMK subdirectory is inverted between the two trees — DAT_0053f5f4 == 1 picks
   "smk_high" on the CD (0x4af116) but "\smk_low" in the install tree (0x4af312).
2. 0x4af116: the 30-byte copy from the string table runs past each literal into its neighbours (DAT_005cd490 holds
   string-table residue after the NUL; cf. io_path.c's quirk for 0x4aef7e).
3. 0x4af312: only the first 40 bytes of the launch directory DAT_0058fe30 are copied before `strcat` — a launch path
   of 40+ characters leaves no NUL: the install-tree retry chdirs to garbage (GOG default paths are shorter).
4. 0x4aee3e: the save list is bubble-sorted by the FIRST BYTE only; a swap moves 64 of the 65 bytes.
5. 0x427c9b: "a cue is playing" is GetStatus == DSBSTATUS_PLAYING exactly: a looping cue (5) reads as not playing.
6. 0x427990: a looping cue asks for DSBCAPS 0xe3 (DSBCAPS_PRIMARYBUFFER with a size and a format): real DirectSound
   refuses it ("ERR:Load and play WAV"), rhost's fake accepts it. Returns 1 for played / null.wav / not found alike.
7. 0x426534: the Lock HRESULT is not checked (`jz $+6` to the next instruction).
8. 0x426dd5: GetStatus is called through DAT_00522af0's vtable with the stream's buffer as `this` (one vtable for
   all buffers; NULL DAT_00522af0 would crash). The bit tested is DSBSTATUS_BUFFERLOST.
9. 0x42dbc7: a lost surface that Restore() recovers is drawn WITHOUT a new Lock (Restore's 0 is taken as the lock
   result; IDA's pseudocode shows an endless loop instead — the asm does not loop).
10. 0x46a101: each of map01..map15.pl8 is probed four times (name i / 4, i < 60): DAT_00554018 counts four per map.
11. 0x4c05a0 / 0x4c0580: Lords2.exe links the MSVC debug CRT — every free() is `_free_dbg` (checks, 0xdd fill).
12. 0x46f21f: walks 7 realm records (i <= 6); the save block holds 6 (the 7th lies past g_player_strategic_352).
13. 0x4afab9: the run's first status.txt line is "msg + date" then 60 dashes; its `arg` / `num` are not written.
14. 0x4b049b: copies 40 bytes of asctime's 26-byte buffer (the CRT's next static bytes ride along to DAT_004eb280).
15. 0x4ae1b8: lords2.inf (0x468 B over DAT_0053f1e0) replaces every setting except the CD drive letter.
16. 0x4ae2ab: an empty function that zeroes its own argument slot.

## Seams left

- TODO(rl-link) (a port exists, not on retail's layout or not linked in rhost): strutil_ext_save 0x4af54a,
  io_path_chdir_base 0x4af502, engine_realm_personality_init 0x49caaa, engine_map_init_set_camera 0x451fcc,
  audio_cue_release 0x427d19, gfx_load_system_sheet_select 0x499a1c, engine_minimap_load_grids 0x46a037,
  engine_territory_load_strat_map 0x467770, engine_map_init_build_cells 0x4298c1,
  engine_title_render_setup_clamp_player_count 0x4ae5e2, engine_map_init_strategic 0x498270, gfx_load_misc_sheet
  0x499034, text_input_init 0x402009, text_input_recompute_length 0x401f49, engine_title_render_scene 0x41e7e1.
- TODO(rl-extern) (unported): 0x46ea61, 0x452160, 0x4b0ab5, 0x4b1867, 0x4984dc, 0x499859, 0x4ae310, 0x4b2636,
  0x426d11 (stream stop), 0x41a166, 0x42e060, 0x42f60e, 0x42f306, 0x42f449, 0x42b780, 0x4b756b, 0x43e6a2, 0x43eac1,
  0x43f43e, the SierraNW `__thiscall` methods 0x4b6d44 / 0x4b6a15 / 0x4b6e1b / 0x4b6b71 / 0x4b92a0 (through a cdecl
  adapter, so engine_retail_call's bracket still applies).
- Now direct (master's gfx layer on retail's layout): gfx_text_label_format_dec 0x4022bd (g_gfx_format_buffer),
  the back buffer Buf (g_gfx_canvas_pixels), DAT_005aeb68 (g_gfx_last_error).

## Globals

`src/rl_io_globals.{h,c}` + `tools/rhost/rh_rl_io_alias.h`: generated (`gen_skeleton.py --part-globals io`, run with
`rh_rl_io_alias.h` moved aside — the generator otherwise counts the partition's own aliases as owners and emits
nothing), then hand-edited for the standalone's sizes (marked "hand"): Buffer (MEMORYSTATUS, 0x20 B), the extension
scratch (4 B), the default save name (16 B), the movie name (16 B), the movie palette (0x300 B), the two DirectDraw
palette tables (0x400 B each), the DDSURFACEDESC (0x6c B; lPitch / lpSurface through it), the setup choices (8 B), and
the effect bank DAT_00522b00[20] + uTimerID[2] (added).
