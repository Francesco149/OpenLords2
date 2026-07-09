# AI-POV visualization — video idea + RE lead (2026-07-09, owner)

**Idea (for the LoTR II "AI cheats" video):** render the running game FROM AN AI
PLAYER'S POINT OF VIEW — their terrain, their troops moving, their fog-of-war
revealed — with a slopstudio animated overlay showing the *hidden cheated
resources* (the free-gold grant / cheap upkeep) as they're applied. Makes the
cheat undeniable: you watch the AI's economy from the inside.

**Two implementation modes (owner):**
1. Play back a captured game trace with "hacked visuals" (swap the viewed player).
2. Force the human's actions via state (no input) while the render shows the AI POV.

**RE lead — the hook to find:** LoTR2 hotseat MP switches the *viewed/active
player*, which drives (a) which counties render as "owned", (b) fog-of-war reveal,
(c) the HUD treasury/army shown. Find the **current-viewing-player global** (near
the player tables `g_player_strategic_352 @ 0x0057bf00`, or the MP turn-owner
`DAT_00553030` var_sim_mp_active seen in the agent) and poke it to an AI slot
(2..4) to make the strat-map + HUD render that AI's perspective. Combine with the
per-realm STRAT reads (treasury +0x118, army counts) for the overlay data. Verify
with the probe tool: poke the candidate, `probe.py shot`, see if the map redraws
as the AI's territory.

## Progress 2026-07-09 — the resource perspective is a one-liner; the map needs hooks

**`DAT_0057c8cc` = the "local player" the HUD reads.** Poking it 1→2 (human→AI
slot 2) flipped the on-screen treasury **1000 → 692 = the AI's real cash**, and
restored cleanly (no crash). So the *stats/resource* half of the AI-POV — the
hidden cheated-treasury reveal — is just: `poke 0x57c8cc <ai_slot>` (or read the
STRAT records directly: cash `0x57bf00 + slot*0x160 + 0x118`, and overlay in
slopstudio). Players are slots 1..4 (1 = human).

**The MAP did NOT switch** (same fog / county ownership / camera) — only ~0.6% of
pixels changed (the HUD treasury). So rendering the AI's *terrain/troops/fog*
needs hooking the map-render + fog/ownership path to substitute the AI slot,
NOT just the HUD global. Use the engine-thread substrate (drainCallQueue /
Interceptor on the engine thread — see [[lotr2-retail-driving-poc]]): find the
fog-of-war buffer + the per-owner map-render selector and force the AI slot for a
capture, then restore. Candidate anchors: the map composite (screen 0 render
FUN_0040f5fd), the fog/visibility globals, and `s_slot_0057c93c` (selected
county). Owner: "we'll likely need to hook a bunch" — yes, for the map.

Prereq for driving into a filmable state: the basic-action recipe (economic
actions done via clicks + callq). See [[lotr2-retail-driving-poc]] + tools/probe_daemon.py.
