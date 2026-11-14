---
start: knight-garrison@19051   # the battle prompt of a strategic siege (the Baron's Germany: swords + archers garrison) — or a skirmish siege (Cust. page: add archers); say which and why
steps:
  - on the battlefield of a siege: screen >= 40 and screen <= 43
  - still on the battlefield: screen >= 40 and screen <= 43
forbid:
  - quit to the title: state == 2
---
# Make the AI's siege-defender swords sally at a breach — sub_48C3FE (0x48c3fe) / sub_48B7FC (0x48b7fc), never reached

`rd covered 0x48c3fe 0x48b7fc` → neither reached (0x48a9d2, 0x48b7fc's caller, IS reached by 14 scenarios). Two earlier
attempts (round 2, docs/human-traces.md B7) reached nothing; the conditions were never named in state. From the IDA export:

- sub_48E4CC (an AI defender group's behaviour, every 100 of its ticks, group records 0x566520 stride 52, current group
  dword_568458): v1 = the group's target group (+0xa word, valid when +0xc byte set); v3 = 1 when the target stands on a
  cell whose byte_5440E7[640·row + 8·col] < 4 (height? read it), 2 when ≥ 4; v2 = sub_48ED95(group) (a breach record —
  read it). Then, when dword_4EEA98 ≤ dword_552FF0: not (dword_542CD4 && dword_553FB0), dword_553E64 < 4 (≥ 6 → 0x48bff9),
  and v2 != 0 and v3 == 1 → **sub_48C3FE(v2)**.
- sub_48B7FC: from sub_48A9D2 (read the branch that calls it; coverage-round3 notes dword_56D89C set by another AI group).

So likely: a wall section down (the siege-castles header: catapults on a wall cell beside a kind-5 cell, 16 hits → the
section falls), our units standing OUTSIDE the walls near the breach as the defenders' target, and few of ours inside
(dword_553E64 may count attackers inside — find out). Name the state first (`rd ram R define`), watch it on the field
(`rd scan` / `--watch`), then search on battlefield orders (DRIVER.md: battlefield orders are LEFT clicks) with the
state in the cell. `rd cov R --blocks --fn 0x48e4cc` shows which branches you take.

Report frames, the state named, rd cov (functions + blocks), and if blocked, exactly which condition and why.

**Campaign 5's attempt (run `aisally`, docs/findings/probes/ai-sally.md) named the blocking condition:** the swords group
(stance 3) takes a target (+0xc) ONLY from a ranged hit — a projectile of type ≤ 2 (arrow / bolt; records 0x57a104,
stride 76) from our side marks the unit hit and records the attacker; melee and catapult stones do not. So 0x48c3fe needs
OUR archers / crossbowmen standing on ground of kind < 4 (byte 0x5440e7 + 640·row + 8·col: 1 ground, 4 wall, 5 courtyard,
9 rubble) shooting the stance-3 swords while a breach record exists (0x553c80: 20 pairs [cell offset, claiming group];
a fallen wall section — catapults, 16 hits — or a castles.dat breach carried from an earlier assault, C(k)+0x1c3 == 2,
repaired at C(k)+0x1c4 == 100) and the strength ratio 0x4eea98 (AI over ours, % − 100, ±random every ~100 ticks) ≤
dword 0x552ff0 = 260, with 0x553e64 (our units inside) < 4. siege-castles has no archers on our side. Bring an army with
archers (+ a catapult, or attack a castle already breached) of more than ~1/3.6 of the garrison's strength.
