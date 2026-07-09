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

Prereq: the basic-action recipe (play a proper game first). See
[[lotr2-retail-driving-poc]] + tools/probe_daemon.py.
