# Call-graph diff — port↔retail structural parity per frame

> Landed 2026-05-29 (Phase G of `docs/plans/structural-parity-harness.md`).
> **Validated end-to-end against live retail** (frida-server
> `cutestation.soy:27042`): 424/424 VAs hooked, a boot-frame retail↔port diff
> ran clean — retail 60 unique VAs/tick vs the port's scaffold 1 (engine_tick),
> 59 named retail-only gap candidates.

## What it does

For a given sim frame, reports which engine functions each side called:

- **retail-only** — retail called it, the port didn't → an unported function
  the port's scene-state code skips today (the work-list), OR a ported
  function we forgot to annotate.
- **port-only** — port called it, retail didn't → structural divergence (the
  port enters a code path retail doesn't) or a wrong VA tag.
- **overlap** — both called it. `=` full parity, `≈` count-parity but the port
  body is a stub (`CALL_TRACE_ENTER_STUB`), `≠` call-count mismatch.

Frame numbering is identical on both sides: `g_engine_frame_counter` (port,
bumped in `engine_tick()`) and `g_tick_count` (agent, bumped in
`engine_tick.onEnter` @ `FUN_004b99c0`) both increment once per per-tick
driver invocation, so frame N means the same tick on both.

## Pieces

| Piece | File | Role |
|-------|------|------|
| Port tracer | `src/call_trace.{c,h}` | `CALL_TRACE_ENTER(0xVA)` probes → per-frame JSONL |
| Port wiring | `src/engine.c`, `src/main.c` | `--call-trace PATH [--call-trace-frames i,j,k]`; begin/end frame |
| Retail emitter | `tools/frida/openlords2-agent.js` | `installCallTrace` hooks N VAs, batches per tick |
| Retail driver | `tools/frida_capture.py` | `--call-trace --call-trace-vas @file` → `call_trace.jsonl` |
| Probe-gen | `tools/gen_call_trace_probes.py` | annotation worklist + retail hook-VA set |
| Diff | `tools/call_trace_diff.py` | aligns frames, reports the three sets |

## Annotation convention

Each ported function gets ONE line at the top of its body:

```c
void engine_map_render_frame(void)
{
    CALL_TRACE_ENTER(0x00405487);   /* the engine FUN_ this implements */
    ...
}
```

Use `CALL_TRACE_ENTER_STUB(0xVA)` instead when the body is a stub / partially
ported and the missing part is load-bearing for the current scene — the diff
then marks count-parity rows `≈` so a stubbed body can't masquerade as parity.

The probe is a single null-check when `--call-trace` is off (cost-free in
normal runs). It's no-op in host tests too (`g_f` stays NULL).

Find what's left to annotate:

```
nix develop -c python3 tools/gen_call_trace_probes.py          # 5/276 done
```

## End-to-end run

```sh
# 1. Build the port hook-VA set (ported ∪ their callees = gap candidates).
nix develop -c python3 tools/gen_call_trace_probes.py --hook-vas runs/hookvas.json

# 2. Capture retail's trace for the target frames (drive to the scene via
#    --input-trace; pick frames inside the render path you care about).
#    NB: --remote cutestation.soy:27042 is REQUIRED — WSL2's Hyper-V firewall
#    blocks the 127.0.0.1 default (see memory reference_frida_remote).
nix develop -c python3 tools/frida_capture.py \
    --remote cutestation.soy:27042 \
    --run-id calltrace-retail --turbo \
    --input-trace tests/scenarios/<scene>/trace.jsonl \
    --call-trace --call-trace-vas @runs/hookvas.json \
    --call-trace-frames 240,241,242

# 3. Capture the port's trace for the same frames.
nix develop -c tools/run-openlords2.sh --frames 300 --gfx-init \
    --call-trace /opt/src/OpenLords2/runs/port-calltrace.jsonl \
    --call-trace-frames 240,241,242

# 4. Diff a frame (or event-anchor with --align-on-first 0xVA if the wall-
#    clock frames don't line up between the two runs).
nix develop -c python3 tools/call_trace_diff.py \
    --retail runs/capture/calltrace-retail/call_trace.jsonl \
    --port   runs/port-calltrace.jsonl \
    --retail-frame 240 --port-frame 240 --verbose
```

The **retail-only** list from step 4 is the prioritised port work-list for that
frame: port the deepest leaf first, re-run, watch it drop out of retail-only.
That's the Phase J leaf-first loop.

## Validation status

- Port tracer: `--frames 40 --gfx-init --call-trace-frames 5,10,20` emits
  exactly 3 rows `{va:0x4b99c0, stub:true}` at the filtered frames. ✓
- Diff tool: synthetic retail trace vs real port trace → correctly flags the
  `0x4b99c0` `≠` count mismatch + stub marker and names the retail-only
  `0x404a46`. ✓
- Probe-gen: worklist 5/276 annotated; hook-VA set 424 (276 ported + 148
  callee-only). ✓
- Retail emitter: **live retail run validated** — 424/424 VAs hooked (0 failed),
  frames 100/101/102 = 71/76/71 calls (per-event frame stamp, no straggler
  frame). ✓
- Full pipeline: boot-frame retail↔port diff named 59 retail-only gaps,
  including 4 already-ported-but-unannotated (40e890 engine_tick_dispatch,
  40e8bb engine_tick_guard, 43de70 scenario_hint, 429b1d camera_clamp_scroll)
  — exactly the "annotate these" + "port these" split the tool is for. ✓

## Gotchas (carried from openrecet's scars)

- **Per-tick batch the retail emitter.** Per-call `send()` saturates; the
  pre-bring-up CRT-init window can fire hundreds of thousands of hooks. The
  agent flushes one `call_trace_batch` per tick and force-flushes at
  `CALL_TRACE_FLUSH_AT` (20000) events to stay under GLib DBus's 128MiB blob
  limit (exceeding it kills frida-server + the target silently).
- **Hook the callees, not just the ported set.** If you only hook functions
  you've ported, retail-only is empty by construction — hook their unported
  neighbours (`gen_call_trace_probes.py` does this) to surface gaps.
- **Pair `--call-trace-frames` with the scene.** Hot functions saturate the
  port JSONL otherwise.
- `ret_va` is module-relative on both sides (add `0x00400000` for a Ghidra VA).
</content>
