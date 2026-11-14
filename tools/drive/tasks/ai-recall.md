---
start: fresh                  # an Equal FIELD skirmish (not a siege): Single → Skirmish → a field battle
steps:
  - on a field battlefield: screen >= 40 and screen <= 43 and u32(0x53e8fc) == 0
  - the AI recall flag set: u32(0x56d89c) != 0
  - still in the game: state == 3
forbid:
  - quit to the title: state == 2
---
# Make an AI field group recall to the attacker — sub_48B7FC (0x48b7fc), never reached

`rd covered 0x48b7fc` → not reached (its caller sub_48A9D2, AI field stance 1 in table 0x4d91b8, is reached by 14
scenarios). docs/findings/probes/ai-sally.md ("Why sub_48B7FC is out of reach here") named the condition from the code
(not tried):

- an AI stance-2/3/4 group (sub_48ACD2 / sub_48B02B) is hit by our arrows, so it has a target; then if
  ++u32(0x553f28) ≤ byte 0x553080[u32(0x5653f8)] and u32(0x553f74)/8 ≤ u32(0x56d6ac) it sets u32(0x56d89c) = 1 and the
  attacker's centre DAT_00522f70 / DAT_00522f74;
- on its next 200-tick step an AI stance-1 group with u32(0x4eea98) ≤ u32(0x57c8b4), u32(0x553070) == 0 and no target of
  its own (+0xc == 0) calls sub_48B7FC (bf-group.md: it steps TOWARD that remembered point).
- So: an Equal skirmish field battle; our archers shoot an AI stance-2/3/4 group and never hit its stance-1 group.
  0x553f74 / 0x56d6ac (sub_481B9A all_ida.c:69750): AI men of class < 7 / AI ranged men (classes 1, 5) — the AI needs
  ≥ 1/8 ranged. Group records 0x566520 stride 52 (stance +8, target +0xc, clock +0x1c); read the AI groups' stances
  at the start (`rd ram`, `rd eval … --each`).

Battlefield orders are LEFT clicks (DRIVER.md); `rd cell` does not work on the battlefield — ai-sally's probe gives
the minimap / view geometry. Search on order timing with the predicates above in the cell.

Report frames per step, the state named, rd cov (functions + blocks); if blocked, exactly which condition and why.
