# Probe: ai-surrender

- **Run:** `aisurrender` · **task:** `tools/drive/tasks/ai-surrender.md` · not filed (blocked)
- **Driver:** API harness (`tools/drive/agent.py`, claude-opus-5-5, effort high), campaign 6, 2026-11-14

The driver's report, as written (retail facts observed live through `rd`; claims read from code only are leads). It
corrects the task's premise (the orchestrator read sub_481B9A's count as allowing an engines-only AI army):

---

The surrender flag was never armed, so the task is not done. Run `aisurrender` is 1997 frames long, from `fresh`. `rd check` gives FAIL: step 1 holds at f767 (a skirmish siege on the battlefield, screen 41, u32(0x53e8fc) = 1), but step 2 (u32(0x56d5c8) == 1) is never reached. `rd cov`: 604 retail functions, 0 new to the corpus. I did not run `rd cov --blocks` or `rd verify`. Nothing was poked, but there is nothing worth filing.

**Setup clicks**
- Single (320,103) → Skirmish (320,175) → shield (144,175) → Continue (432,227) → Siege (480,305). This gives page 12, and on The Palisade we are already the Defender.
- Map list rows are at x=540, y 192 + 16·i. The arrow (613,272) scrolls the list.
- Advantage arrows are at (153,412) / (290,412). Cust. is (547,448), Go is (600,448).
- On the Cust. page each unit row is at y ≈ 113 + 29.6·row. The arrows are found by code, not by `rd hot`: (101,y) is +1, (113,y) is −1, (143,y) sets the most you can afford, (157,y) sets 0.
- The final try was Norman Keep, Advantage arrow left (AI 4118 gold, us 2377). Our army was edited to 4 crossbows, about 126 archers, swords added with the remaining gold (I did not confirm how many) and 1 boiling oil. Go was at f767.

**What I checked in the decompile (the orchestrator's reading holds, with three extra conditions)**
- Stance 4 is only ever given to a class-6 (knights) group (all_ida.c:69239). So 0x48d9ce runs only for the AI attacker's knight groups, and the AI army needs knights.
- sub_477DFC ends the battle as soon as either side has 0 men of class < 7 (u32(0x53f028) is side 0x568210, the AI here; u32(0x553c58) is our side). So "only engines left" (0 ≤ 0) can never arm the flag. Every AI non-knight must be dead while at least one AI knight is alive.
- In a siege, if u32(0x53e990) == 0 and u32(0x553ff0) == 0, the battle either ends or 0x53e990 is set to 4. u32(0x553ff0) is the number of class 7..9 units on the field (siege engines, either side), counted in sub_4822ED. So an engine must stay alive. I could not buy our own catapult: the +1 click on the catapult row did nothing for the defender.
- **u32(0x53e990) is the breach count.** It goes:
  - +1 per wall cell next to the courtyard turned to rubble (sub_47DFE0);
  - +4 when the gate counter u32(0x568da4) passes 20000 (sub_484A0C at 0x484a0c adds 20 a tick while the ram is battering; sub_483A88 adds 1 per hit), and that also sets the gate-broken flag u32(0x569588) = 1;
  - +4 from a docked tower (all_ida.c:77515) and +3 at all_ida.c:75454.

**Survey of AI attackers (u32(0x553f74) all AI men / u32(0x57c980) AI knights)**
- No skirmish siege map has a knights-only attacker. Maps with knights:
  - Castle Acre 650/75
  - Norman Keep 400/25 by default, 368/23 with Advantage arrow left (2 towers + 1 ram)
  - Leeds 575/25
  - Curtain Wall 2000/500
  - Chambers 1500/50
  - Expanded Keep 1450/50
- Maps with no knights: Palisade 315/0, River Island 545/0, Wooden Palace 550/0, Bodiam 1500/0, Royal Castle 1700/0, The Ell 1650/0, Island Castle 1800/0, Goodrich 1600/0, Portchester 1800/0.
- The Cust. page edits only the human's army. I swapped to Attacker, made that army 106 knights + 2 towers, and swapped back: the attacker army reverted to the map's preset (315/0 at Go).

**What happened on Norman Keep**
- Left alone, the AI ram reaches the gate at about f1340 (battle opened at f402). It hits the gate until u32(0x568da4) reaches 20000, and u32(0x53e990) = 4 at about f2351.
- All other AI groups (u32(0x553f74) stays 368) stay in the bottom-left corner around (5..14, 70..77) and never come near the walls, and the knights sit at (5,72).
- Our archers ordered onto the ram from the gatehouse do almost nothing to it: about 1 crew man per 200–250 frames (crew 8). A melee unit sent out the gate kills the crew faster: 5 swords took it from 6 to 3 in 300 frames, still too slow.
- The final attempt (f1571–1997: everyone sent to the gatehouse, attack ordered on the ram) got only one soldier outside, and the ram crew was 6 at f1997. I stopped there.

**Why it is blocked**
- Even with the ram killed in time, about 345 AI non-knights (archers, swords, pikes, peasants) would have to die while they wait out of bow range, about 30 cells away.
- Meanwhile no breach may happen, no AI tower may dock, an engine must survive, and the knights' group must be untargeted when its clock (word 0x56653c + 52·g) reaches 200.
- With our roughly 170 men from 2377 gold, that field battle is not winnable, and the micro it needs was beyond my budget.

**Next attempt**
- Look for a start where the AI attacker's non-knights die first: a strategic AI siege of our castle (siege-defence's River Island attacker had knights).
- Or bait AI groups with arrows from walls near their camp (AI groups take a target only from ranged hits) and kill the ram with a strong melee sally before about 950 frames at the gate.

**Tool gaps**
- `rd eval --each` with `{I}` fails as "unsupported syntax … Set" unless there are spaces around the operators.
- An early `--until` on unit fields fires on the title screen, because the fields are 0 there.
- `rd hot` does not show the Cust. page unit arrows.

---
model claude-opus-5-5, effort high, 122 turns, $2.93, 36 min
