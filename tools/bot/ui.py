"""ui.py — the bot's hands: mouse/keyboard sequences on retail's UI, through a live rhost Session.

Coordinates are 640×480 game space (docs/findings/ui-action-anchors.md, re-checked in rhost 2026-10-23).
Every action goes through rhost's virtual queue at frame boundaries, so it lands in the session's trace.
Screen ids are DAT_004eac50 (docs/findings/gameplay-and-screens.md): 0 = free-roam map, 0x23 = the turn
being processed, 0x27 = report scroll (RMB dismisses), 0x15 tax, 0x19 rations, 0x17 army, 0x13 quick battle.
"""
SEASON_VA, YEAR_VA = 0x57c934, 0x553edc        # 1 Spr .. 4 Win; year
END_TURN = (558, 469)
BACK = (300, 240)                               # RMB here backs out of panels / dismisses report scrolls
TAX_OPEN, TAX_UP, TAX_DOWN = (515, 218), (197, 178), (243, 178)
RATION_OPEN = (600, 218)
LABOR_LEFT, LABOR_RIGHT = (490, 258), (620, 258)   # HUD farming ◄ ► industry split
DISMISS_RMB = {0x27, 0x13, 0x22, 0x2e, 0x2f}       # scrolls, quick-battle replay, movie player, battle ratings


class Stuck(RuntimeError):
    pass


class UI:
    def __init__(self, s, log=print):
        self.s, self.log = s, log
        self.unknown = {}          # screen id → first frame seen (for the run report)

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
                seen.append(sc)
            if sc == 0:
                idle += 1
                if idle >= 2:
                    return seen
                continue
            idle = 0
            if sc in DISMISS_RMB:
                self.s.click(*BACK, right=True)
            elif sc != 0x23:
                if sc not in self.unknown:
                    self.unknown[sc] = self.s.frame
                    self.s.shot(f"unknown_{sc:02x}_f{self.s.frame}")
                    self.log(f"  ui: unknown screen {sc:#x} at frame {self.s.frame} — trying RMB")
                self.s.click(*BACK, right=True)
        raise Stuck(f"no idle map after {max_frames} frames; screens {[hex(x) for x in seen]}")

    def end_turn(self, max_frames=20000):
        """End turn; returns once the new season's map is idle"""
        s0, y0 = self.season(), self.year()
        self.s.click(*END_TURN)
        seen = []
        for _ in range(max_frames // 200):
            seen += self.settle(max_frames=4000)
            if (self.season(), self.year()) != (s0, y0):
                seen += self.settle(max_frames=2000)
                return seen
        raise Stuck(f"season did not advance after End turn ({s0}/{y0}); screens {[hex(x) for x in seen]}")

    def _panel(self, opener, want_screen, frames=20):
        self.s.click(*opener)
        self.s.run(frames)
        if self.s.screen != want_screen:
            if self.s.screen == 0x27:        # a first-time hint scroll over the panel
                self.s.click(*BACK, right=True)
                self.s.run(frames)
            if self.s.screen != want_screen:
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
