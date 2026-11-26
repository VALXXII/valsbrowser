// Split view hover frames, the pure part (src/split-frames.h; Val's « Atelier » boards « Fenêtre » 27E0-0 / 2793-0):
// the frames' geometry against the boards, the trigger zone, the show / hide state machine (delays, follow, leave,
// held buttons, the window leaving the front, a pane gone), the morph's poses and reduced motion (no travel, a fade),
// and the actions' pointer targets. No window, no drawing: driven with synthetic inputs and clock values.
#include "../../src/split-frames.h"
#include <cstdio>
#include <cmath>
using namespace NativeChrome;
using namespace NativeChrome::SplitFrames;

static int failures = 0, checks = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a - b) <= e; }
static Motion::Time At(Motion::Time t0, int ms) { return t0 + Motion::Milliseconds(ms); }

int main() {
    // The boards' window: 1180 x 815, the panes under the 81 DIP chrome, 4 DIP inside the window, 4 apart.
    const Box left{4, 81, 588, 811}, right{592, 81, 1176, 811};

    // ---- Geometry (Paper 27GD-0 / 27GI-0 / 27DC-0 / 27DU-0)
    {   const auto a = LayoutActions(right);
        Check(a.shown && Near(a.frame.W(), 58) && Near(a.frame.H(), 24) && Near(a.frame.right, right.right) && Near(a.frame.top, right.top), "actions frame: 58 x 24, flush with the pane's top-right corner");
        Check(Near(a.minimize.left, a.frame.left + 8) && Near(a.minimize.W(), 16) && Near(a.minimize.top, right.top + 4) && Near(a.close.left, a.minimize.right + 8) && Near(a.close.right, a.frame.right - 10),
              "actions: padding 8 / 10, the 16 DIP minimize then 8 then the 16 DIP close box, centred on the 24 DIP height");
        Check(Near(a.minimizeHit.left, a.frame.left) && Near(a.closeHit.right, a.frame.right) && Near(a.minimizeHit.right, a.closeHit.left) && Near(a.minimizeHit.H(), 24),
              "actions: the frame splits between the two targets (no dead strip)");
        Check(ActionAt(a, a.minimize.left + 8, a.frame.top + 12) == Minimize && ActionAt(a, a.close.left + 8, a.frame.top + 12) == Close && ActionAt(a, a.frame.left - 1, a.frame.top + 12) == NoAction,
              "actions: the pointer finds « réduire » and « fermer »");
        Check(Near(Layout::MinimizeOffset(), -.2f, .002f), "minimize glyph: 0.2 DIP higher than the app's (the board's view box starts at -12.8)");
    }
    {   const auto s = LayoutSite(right, 32.5f, 66.4f);   // « Paper », « paper.design » at 12 DIP
        Check(s.shown && s.address && Near(s.frame.left, right.left) && Near(s.frame.top, right.top) && Near(s.frame.H(), 24), "site frame: flush with the pane's top-left corner, 24 high");
        Check(Near(s.icon.left, right.left + 8) && Near(s.icon.W(), 12) && Near(s.icon.top, right.top + 6), "site frame: padding 8, the 12 x 12 favicon centred");
        Check(Near(s.name.left, s.icon.right + 6) && Near(s.name.W(), 32.5f) && Near(s.addressBox.left, s.name.right + 8) && Near(s.addressBox.W(), 66.4f) && Near(s.frame.right, s.addressBox.right + 10),
              "site frame: 6, the name, 8, the address, padding 10 (it hugs its content)");
    }
    {   // Narrow pane (not drawn, provisional): the address trims, then goes, then the name trims; the actions keep their room.
        const Box narrow{0, 0, 200, 600};
        const auto a = LayoutActions(narrow); auto s = LayoutSite(narrow, 60, 120);
        Check(s.shown && s.address && s.frame.right <= a.frame.left - Layout::sideGap + .01f && s.addressBox.W() < 120, "narrow: the address trims first, 8 DIP before the actions");
        s = LayoutSite(narrow, 100, 120);
        Check(s.shown && !s.address && s.frame.right <= a.frame.left - Layout::sideGap + .01f && s.name.W() < 100, "narrow: under 24 DIP the address goes, the name trims");
        s = LayoutSite({0, 0, 90, 600}, 50, 50);
        Check(!s.shown && LayoutActions({0, 0, 90, 600}).shown, "very narrow: the site frame steps aside, the actions stay");
    }

    // ---- Trigger zone (provisional: the pane's top 32 DIP)
    {   std::vector<Box> panes{left, right};
        Check(ZoneAt(panes, 300, 81) == 0 && ZoneAt(panes, 300, 112.9f) == 0 && ZoneAt(panes, 300, 113.1f) == -1, "zone: the left pane's top 32 DIP");
        Check(ZoneAt(panes, 900, 90) == 1 && ZoneAt(panes, 590, 90) == -1 && ZoneAt(panes, 300, 70) == -1, "zone: the right pane; not the 4 DIP gap, not the toolbar");
    }

    // ---- State machine
    const auto t0 = Motion::Now();
    auto in = [&](float x, float y, bool active = true, bool buttons = false) { Input i; i.active = active; i.pointer = true; i.x = x; i.y = y; i.buttons = buttons; i.panes = {left, right}; return i; };
    {   Machine m;
        m.Update(in(900, 400), t0); Check(m.Pane() == -1 && m.Candidate() == -1, "pointer low in the page: nothing");
        m.Update(in(900, 90), At(t0, 10)); Check(m.Pane() == -1 && m.Candidate() == 1, "pointer reaches the right pane's top: waiting (show delay)");
        m.Update(in(910, 92), At(t0, 100)); Check(m.Pane() == -1, "still waiting at 90 ms");
        m.Update(in(912, 92), At(t0, 170)); Check(m.Pane() == 1 && m.Showing(), "150 ms in the zone: the frames arrive on the right pane");
        Check(m.Moving(At(t0, 200)) && m.Progress(At(t0, 200)) > 0 && m.Progress(At(t0, 200)) < 1, "they arrive on the morph spring (in motion)");
        Check(Near(m.Progress(At(t0, 170 + static_cast<int>(Timing::enter.count()) + 5)), 1), "settled after the morph spring's settle time");
        Check(m.NextWake(At(t0, 200)).delay.has_value(), "frames requested while it moves");
        // Leaving: a grace period, then out on the exit spring.
        m.Update(in(900, 400), At(t0, 1000)); Check(m.Pane() == 1 && m.Showing(), "pointer leaves the zone: still shown (hide delay)");
        m.Update(in(900, 90), At(t0, 1300)); Check(m.Pane() == 1 && m.Showing(), "back within the delay: they stay");
        m.Update(in(900, 400), At(t0, 1400)); m.Update(in(900, 400), At(t0, 1700)); Check(m.Showing(), "300 ms away: still shown");
        m.Update(in(900, 400), At(t0, 1810)); Check(m.Pane() == 1 && !m.Showing(), "400 ms away: leaving (exit spring)");
        Check(m.Progress(At(t0, 1850)) < 1 && m.Progress(At(t0, 1850)) > 0, "leaving: the morph runs back");
        m.Update(in(900, 400), At(t0, 1810 + static_cast<int>(Timing::exit.count()) + 5)); Check(m.Pane() == -1 && Near(m.Progress(At(t0, 3000)), 0), "gone once the exit settles");
    }
    {   Machine m;   // follow: from one pane's top to the other's, at once
        m.Update(in(900, 90), t0); m.Update(in(900, 90), At(t0, 160)); Check(m.Pane() == 1, "shown on the right pane");
        m.Update(in(300, 90), At(t0, 600)); Check(m.Pane() == 0 && m.Showing() && m.Progress(At(t0, 600)) < .01f, "pointer moves to the left pane's top: the frames follow at once and arrive again");
        // Re-entering while they leave: they come back from where they are, no second wait.
        m.Update(in(300, 400), At(t0, 1000)); m.Update(in(300, 400), At(t0, 1500)); Check(!m.Showing(), "leaving");
        const float mid = m.Progress(At(t0, 1550)); m.Update(in(300, 90), At(t0, 1550));
        Check(m.Showing() && m.Pane() == 0 && Near(m.Progress(At(t0, 1550)), mid, .02f), "back in the zone mid-exit: retargets from the value on screen");
    }
    {   Machine m;   // a held button never starts the frames (a selection dragged up in a page)
        m.Update(in(900, 90, true, true), t0); m.Update(in(900, 90, true, true), At(t0, 300)); Check(m.Pane() == -1 && m.Candidate() == -1, "button held: no frames");
        m.Update(in(900, 90), At(t0, 400)); m.Update(in(900, 90), At(t0, 560)); Check(m.Pane() == 1, "released in the zone: they come after the delay");
        m.Update(in(900, 90, true, true), At(t0, 700)); Check(m.Showing(), "a press on the frames (in the zone) keeps them");
    }
    {   Machine m;   // the window leaves the front (or a drag, Mode Focus…): out, without the grace period
        m.Update(in(900, 90), t0); m.Update(in(900, 90), At(t0, 160));
        m.Update(in(900, 90, false), At(t0, 500)); Check(m.Pane() == 1 && !m.Showing(), "inactive: leaving at once");
        Input none; none.active = true; none.pointer = true; none.x = 900; none.y = 90;   // the split view ends: no panes
        m.Update(in(900, 90), At(t0, 520)); Check(m.Showing(), "active again in the zone: back");
        m.Update(none, At(t0, 540)); Check(m.Pane() == -1, "its pane is gone: dropped at once");
        m.Update(in(900, 90), At(t0, 600)); m.Update(in(900, 90), At(t0, 800)); m.Dismiss(); Check(m.Pane() == -1 && !m.Showing(), "Dismiss (a click on « réduire » / « fermer »): gone at once");
    }

    // ---- Morph poses (motion) and reduced motion
    {   const auto p0 = PoseAt(0, false, false), p1 = PoseAt(1, false, false), a0 = PoseAt(0, false, true);
        Check(Near(p0.dx, -24) && Near(p0.dy, -24) && Near(p1.dx, 0) && Near(p1.dy, 0), "site frame: arrives from behind the top-left corner, one frame height each way");
        Check(Near(a0.dx, 24) && Near(a0.dy, -24), "actions frame: from behind the top-right corner");
        Check(Near(p0.surface, 1) && Near(p0.content, 0) && Near(PoseAt(.25f, false, false).content, 0) && Near(PoseAt(.625f, false, false).content, .5f) && Near(p1.content, 1),
              "the surface stays opaque (clipped by the pane), the content fades in over the morph's last 75 %");
        const auto r = PoseAt(.5f, true, true);
        Check(Near(r.dx, 0) && Near(r.dy, 0) && Near(r.surface, .5f) && Near(r.content, .5f), "reduced motion: no travel, a plain fade");
        Machine m; m.SetReduced(true); m.Update(in(900, 90), t0); m.Update(in(900, 90), At(t0, 160));
        Check(Near(m.Progress(At(t0, 160 + 125)), 1), "reduced motion: the fade lasts 120 ms");
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
