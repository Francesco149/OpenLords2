"""ui.py — the bot's hands: mouse/keyboard sequences on retail's UI, through a live rhost Session.

Coordinates are 640×480 game space (docs/findings/ui-action-anchors.md, re-checked in rhost 2026-10-23).
Every action goes through rhost's virtual queue at frame boundaries, so it lands in the session's trace.
Screen ids are DAT_004eac50 (docs/findings/gameplay-and-screens.md): 0 = free-roam map, 0x23 = the turn
being processed, 0x27 = report scroll (RMB dismisses), 0x15 tax, 0x19 rations, 0x17 army, 0x13 quick battle.

What retail asks the player while the bot waits for an idle map goes through `UI.ask(kind, info) -> answer`
(play.py sets it to a Jev question; the defaults below answer without it):
  "popup"   an event scroll (0x53e910 != 0 at screen 0): notices are dismissed on their seal; the yes/no
            kinds (type 11 the AI offers an alliance, 10 an ally asks to be paid, 17 garrison capacity) → bool
  "confirm" the confirm dialog 0x1e (loc grp 10: "Besiege castle?", "Garrison castle?", ...) → bool
  "siege"   siege preparations 0x1d → {"catapults": n, "towers": n, "rams": n, "proceed": bool}
  "battle"  the pre-battle scroll 0x12 → "auto" (the right gauntlet: auto-calculate) or a battle.fight tactic
            ("attack", "mopup", "retreat", "autocalc"): take the field and play it on the battlefield
"""
import battle
import l2state
SEASON_VA, YEAR_VA = 0x57c934, 0x553edc        # 1 Spr .. 4 Win; year
END_TURN = (558, 469)
BACK = (300, 240)                               # RMB here backs out of panels / dismisses report scrolls
TAX_OPEN, TAX_UP, TAX_DOWN = (515, 218), (197, 178), (243, 178)
RATION_OPEN = (600, 218)
LABOR_LEFT, LABOR_RIGHT = (490, 258), (620, 258)   # HUD farming ◄ ► industry split
DISMISS_RMB = {0x27, 0x22, 0x2e, 0x2f}             # scrolls, movie player, battle ratings
BATTLE_PROMPT, BATTLE_RESULT = 0x12, 0x13           # "A Battle is to be fought. Will you take the field?" / "decided"
AUTO_RESOLVE = (383, 128)                           # the right gauntlet: don't take the field → auto-calc
RESULT_CONTINUE = (410, 412)                        # the hand on "The Battle is decided."
TITLE_MENU = 0x1f                                   # the title's menus (engine state 2): a custom game's defeat ends here
GAME_OVER = 0x1c                                    # victory / defeat (black while its movie is skipped): a click
                                                    # continues — after a defeat retail restarts the scenario
# event scrolls (engine_popup_scroll_input_step FUN_0047685d): yes / no buttons per popup type (button lists
# 0x4dda90 / 0x4ddac0 / 0x4ddb50: 32-px squares), else the seal dismisses
POPUP_BUTTONS = {10: ((368, 200), (404, 204)), 11: ((368, 200), (404, 204)), 17: ((336, 328), (372, 332))}
SEAL_FALLBACK = (425, 368)
CONFIRM = 0x1e                                      # engine_confirm_dialog_open: panel at (144,144), text grp 10
CONFIRM_YES, CONFIRM_NO = (240, 218), (287, 218)    # the thumbs [live: "Besiege castle?", "Garrison castle?"]
CONFIRMS = {0: "exit the game", 1: "start a new game", 2: "overwrite file", 3: "create this army",
            4: "slaughter villagers", 5: "combine armies", 6: "disband army", 7: "garrison castle",
            8: "besiege castle", 9: "autocalc battle", 10: "destroy field", 11: "surrender castle",
            12: "retreat from field", 13: "lift the siege", 14: "quit (no destination)"}
CONFIRM_DEFAULT = {3: True, 5: True, 7: True, 8: True, 9: True}   # everything else: no
SIEGE = 0x1d                                        # siege preparations (FUN_00421f14, grp 0x53) [live]
SIEGE_ARROWS = {"catapults": ((48, 190), (48, 212)), "towers": ((48, 258), (48, 280)), "rams": ((48, 326), (48, 348))}
SIEGE_LIFT, SIEGE_PROCEED = (155, 410), (312, 410)  # Proceed with no engines = assault now (FUN_0043b848 → 0x4a8aab)


def default_ask(kind, info):
    if kind == "popup":
        return False
    if kind == "confirm":
        return CONFIRM_DEFAULT.get(info.get("kind"), False)
    if kind == "siege":
        return {"catapults": 0, "towers": 0, "rams": 0, "proceed": True}
    if kind == "battle":
        return "auto"
    return None


class Stuck(RuntimeError):
    pass


class GameEnded(RuntimeError):
    """retail left the game: the title menu after a custom game's defeat (Play Now restarts the scenario instead)"""


class UI:
    def __init__(self, s, log=print, ask=None):
        self.s, self.log = s, log
        self.ask = ask or default_ask
        self.unknown = {}          # screen id → first frame seen (for the run report)
        self.battles = 0
        self.game_overs = 0
        self.events = []           # popups / confirms / sieges answered (play.py logs them per turn)
        self.sieges = 0
        self.quick = 0             # battles shown as the quick animation 0x13 (no "take the field?" prompt)
        self.fields = 0            # battles taken to the field (battle.fight)

    def modal(self):
        return self.s.i32(l2state.MODAL)

    def popup(self):
        """answer / dismiss the event scroll on screen → its record"""
        s = self.s
        ev = {"frame": s.frame, "what": "popup", "id": s.i32(l2state.MODAL), "type": s.i32(l2state.POPUP_TYPE),
              "from": s.i32(l2state.POPUP_FROM)}
        ev["title"] = l2state.loc(s.read, ev["id"], 0)
        if ev["type"] in POPUP_BUTTONS:          # the question: an AI's message line (grp id, sub arg+1), or the
            arg = s.i32(l2state.POPUP_ARG)       # capacity popup's own lines (grp 166: 1 + 4)
            ev["text"] = (" ".join(l2state.loc(s.read, ev["id"], i) for i in (1, 4)) if ev["type"] == 17
                          else l2state.loc(s.read, ev["id"], arg + 1))
            ev["answer"] = bool(self.ask("popup", ev))
            pt = POPUP_BUTTONS[ev["type"]][0 if ev["answer"] else 1]
        else:
            x, y = s.i32(l2state.SEAL_X), s.i32(l2state.SEAL_Y)
            pt = (x + 12, y + 12) if 16 <= x < 600 and 16 <= y < 460 else SEAL_FALLBACK
        s.move(*pt, frames=4)
        s.click(*pt)
        s.run(20)
        if s.i32(l2state.MODAL) == ev["id"]:          # still up: retail's popups also close on a map click
            s.click(*SEAL_FALLBACK)
            s.run(20)
        self.events.append(ev)
        self.log(f"  ui: popup {ev['id']} type {ev['type']} \"{ev['title']}\"" + (f" → {ev['answer']}" if "answer" in ev else ""))
        return ev

    def confirm(self, want=None):
        """answer the confirm dialog 0x1e (`want` overrides the asker) → its record"""
        s = self.s
        kind = s.i32(l2state.CONFIRM_KIND)
        ev = {"frame": s.frame, "what": "confirm", "kind": kind, "text": CONFIRMS.get(kind, str(kind))}
        ev["answer"] = bool(self.ask("confirm", ev) if want is None else want)
        pt = CONFIRM_YES if ev["answer"] else CONFIRM_NO
        s.move(*pt, frames=6)
        s.click(*pt)
        s.run(30)
        self.events.append(ev)
        self.log(f"  ui: confirm \"{ev['text']}?\" → {ev['answer']}")
        return ev

    def siege(self):
        """siege preparations 0x1d: engines, then Proceed or Lift siege"""
        s = self.s
        st = l2state.snapshot(s.read)
        mine = [a for a in st["armies"] if a["owner"] == st["me"] and a.get("besieging")]
        ev = {"frame": s.frame, "what": "siege", "army": mine[0]["id"] if mine else None,
              "county": mine[0]["besieging"] if mine else None}
        ans = self.ask("siege", dict(ev, state=st)) or default_ask("siege", ev)
        for k, (up, _) in SIEGE_ARROWS.items():
            for _ in range(max(0, min(4, int(ans.get(k, 0))))):
                s.click(*up, frames=3)
        s.run(10)
        pt = SIEGE_PROCEED if ans.get("proceed", True) else SIEGE_LIFT
        s.move(*pt, frames=6)
        s.click(*pt)
        s.run(30)
        ev["answer"] = {k: ans.get(k) for k in ("catapults", "towers", "rams", "proceed")}
        a = next((x for x in l2state.snapshot(s.read)["armies"] if x["id"] == ev["army"]), None)
        ev["seasons"] = a and a.get("siege_seasons")
        self.sieges += 1
        self.events.append(ev)
        self.log(f"  ui: siege of county {ev['county']} → {ev['answer']} (seasons {ev['seasons']})")
        return ev

    def battle_prompt(self):
        """the pre-battle scroll 0x12: ask, then auto-resolve or take the field and fight it out"""
        s = self.s
        st = l2state.snapshot(s.read)
        by_id = {a["id"]: a for a in st["armies"]}
        atk, dfn = by_id.get(s.i32(battle.ATK_PIECE), {}), by_id.get(s.i32(battle.DEF_PIECE), {})
        we_attack = atk.get("owner") == st["me"]
        ours, theirs = (atk, dfn) if we_attack else (dfn, atk)
        info = {"frame": s.frame, "what": "battle", "we_attack": we_attack, "siege": bool(s.i32(0x53E8FC)),
                "ours": {k: ours.get(k) for k in ("id", "men", "troops", "strength")},
                "theirs": {k: theirs.get(k) for k in ("id", "owner", "men", "troops", "strength")},
                "county": (atk or dfn).get("county")}
        tactic = self.ask("battle", dict(info, state=st)) or "auto"
        if tactic == "auto":
            s.move(*AUTO_RESOLVE, frames=6)
            s.click(*AUTO_RESOLVE)
            s.run(20)
            self.events.append(dict(info, tactic="auto"))
            return
        s.move(*battle.TAKE_FIELD, frames=6)
        s.click(*battle.TAKE_FIELD)
        s.run(30)
        rec = battle.fight(s, self, tactic, log=self.log)
        self.fields += 1
        self.events.append(dict(info, **rec))

    def season(self):
        return self.s.u32(SEASON_VA)

    def year(self):
        return self.s.u32(YEAR_VA)

    def settle(self, max_frames=6000, step=20):
        """run until the map is idle (screen 0 twice in a row), dismissing report scrolls / replays"""
        idle, seen = 0, []
        for _ in range(max_frames // step):
            self.s.run(step)
            sc = self.s.screen
            if not seen or seen[-1] != sc:
                if sc == BATTLE_RESULT and (not seen or seen[-1] != BATTLE_PROMPT):
                    self.quick += 1
                seen.append(sc)
            if sc == 0 and self.modal():
                idle = 0
                self.popup()
                continue
            if sc == 0:
                idle += 1
                if idle >= 2:
                    return seen
                continue
            idle = 0
            if sc == CONFIRM:
                self.confirm()
            elif sc == SIEGE:
                self.siege()
            elif sc in DISMISS_RMB:
                self.s.click(*BACK, right=True)
            elif sc == BATTLE_PROMPT:          # take the field (battle.fight) or auto-resolve
                self.battles += 1
                self.battle_prompt()
            elif sc == TITLE_MENU and self.s.i32(l2state.ENGINE_STATE) == 2:
                raise GameEnded(f"title menu at frame {self.s.frame}")
            elif sc == GAME_OVER:
                self.game_overs += 1
                self.s.click(320, 240)
                self.s.run(40)
            elif sc == BATTLE_RESULT:
                self.s.click(*RESULT_CONTINUE)
                self.s.run(20)
                if self.s.screen == BATTLE_RESULT:
                    self.s.click(*BACK, right=True)
            elif sc != 0x23:
                if sc not in self.unknown:
                    self.unknown[sc] = self.s.frame
                    self.s.shot(f"unknown_{sc:02x}_f{self.s.frame}")
                    self.log(f"  ui: unknown screen {sc:#x} at frame {self.s.frame} — trying RMB")
                self.s.click(*BACK, right=True)
        raise Stuck(f"no idle map after {max_frames} frames; screens {[hex(x) for x in seen]}")

    def end_turn(self, max_frames=20000, clicks=4):
        """End turn; returns once the new season's map is idle. Re-clicks when retail swallowed the click
        (right after a map order the first press only clears the selection)."""
        s0, y0 = self.season(), self.year()
        seen = []
        for _ in range(clicks):
            self.s.move(*END_TURN, frames=4)
            self.s.click(*END_TURN)
            self.s.run(80)                     # turn processing (0x23) starts within a few frames when taken
            for _ in range(max_frames // 4000):
                seen += self.settle(max_frames=4000)
                if (self.season(), self.year()) != (s0, y0):
                    seen += self.settle(max_frames=2000)
                    return seen
                if self.s.screen == 0 and 0x23 not in seen[-3:]:
                    break                      # idle on the old season: the click did not take
        raise Stuck(f"season did not advance after End turn ({s0}/{y0}); screens {[hex(x) for x in seen[-12:]]}")

    def _panel(self, opener, want_screen, frames=20):
        for attempt in range(2):
            self.s.click(*opener)
            self.s.run(frames)
            if self.s.screen == 0x27:        # a first-time hint scroll over the panel
                self.s.click(*BACK, right=True)
                self.s.run(frames)
            if self.s.screen == want_screen:
                return
            if attempt == 0 and self.s.screen != 0:   # something came up meanwhile (a battle prompt as an army
                self.settle(max_frames=4000)          # arrives mid-season [v3 s207]): deal with it, try again
        raise Stuck(f"panel {want_screen:#x} did not open (screen {self.s.screen:#x})")

    def close(self):
        self.s.click(*BACK, right=True)
        self.s.run(10)
        self.settle(max_frames=600)

    def set_tax(self, target, read_tax, max_clicks=60):
        """tax panel arrows until read_tax() == target (percent); read_tax reads the county's tax field"""
        cur = read_tax()
        if cur == target:
            return cur
        self._panel(TAX_OPEN, 0x15)
        for _ in range(max_clicks):
            cur = read_tax()
            if cur == target:
                break
            self.s.click(*(TAX_UP if cur < target else TAX_DOWN), frames=3)
        self.close()
        return read_tax()

    def nudge_labor(self, steps):
        """move the HUD farming/industry split: steps < 0 toward farming, > 0 toward industry"""
        for _ in range(abs(steps)):
            self.s.click(*(LABOR_RIGHT if steps > 0 else LABOR_LEFT), frames=3)
        self.s.run(6)
