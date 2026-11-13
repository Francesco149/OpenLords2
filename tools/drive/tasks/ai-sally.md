---
start: siege-castles        # a strategic siege of an AI castle fought on the field — read its header; any start you prefer, say which
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
