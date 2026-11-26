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
    // ---- Keyboard moves (Val 2026-11-26): F6 / Maj+F6 & co. flash the frames on the pane receiving the focus
    {   const int hold = static_cast<int>(Timing::flash.count());
        auto away = [&](bool active = true) { Input i = in(300, 500, active); return i; };   // the pointer low in a page (no zone)
        Machine m;
        Check(m.Flash(1, away(), t0) && m.Pane() == 1 && m.Showing() && m.Flashing(), "F6 to the right pane: the frames arrive there at once (no show delay)");
        Check(m.Moving(At(t0, 30)) && m.Progress(At(t0, 30)) > 0 && m.Progress(At(t0, 30)) < 1, "the same corner morph (in motion)");
        m.Update(away(), At(t0, 600)); m.Update(away(), At(t0, hold - 20)); Check(m.Showing(), "still shown just before Timing::flash (the pointer elsewhere)");
        m.Update(away(), At(t0, hold + 10)); Check(m.Pane() == 1 && !m.Showing() && !m.Flashing(), "after Timing::flash: they leave as after a hover (exit spring)");
        Check(m.Progress(At(t0, hold + 60)) > 0 && m.Progress(At(t0, hold + 60)) < 1, "leaving: the morph runs back");
        m.Update(away(), At(t0, hold + 10 + static_cast<int>(Timing::exit.count()) + 5)); Check(m.Pane() == -1, "gone");
        // Maj+F6 back to the left pane while they show on the right: they move there and arrive again.
        m.Flash(1, away(), At(t0, 5000)); m.Update(away(), At(t0, 5400));
        Check(m.Flash(0, away(), At(t0, 5500)) && m.Pane() == 0 && m.Progress(At(t0, 5500)) < .01f, "Maj+F6 to the left pane: the frames move there and arrive again");
        m.Update(away(), At(t0, 5500 + hold - 20)); Check(m.Showing(), "its own moment starts again");
        m.Update(away(), At(t0, 5500 + hold + 10)); Check(!m.Showing(), "then out");
    }
    {   // Three panes, F6 to the third.
        Machine m; Input i3; i3.active = true; i3.pointer = true; i3.x = 100; i3.y = 600; i3.panes = {{4, 81, 392, 811}, {396, 81, 784, 811}, {788, 81, 1176, 811}};
        Check(m.Flash(2, i3, t0) && m.Pane() == 2, "three panes: F6 to the third, its frames");
        Check(!m.Flash(3, i3, t0) && !m.Flash(-1, i3, t0) && m.Pane() == 2, "no pane there (a rail or none): nothing changes");
    }
    {   // Hover during the keyboard moment: the pointer takes over.
        Machine m; auto away = in(300, 500);
        m.Flash(1, away, t0); m.Update(in(900, 90), At(t0, 300));
        Check(m.Pane() == 1 && m.Showing() && !m.Flashing(), "the pointer reaches the flashed pane's top: hover rules from now on");
        m.Update(in(900, 90), At(t0, 2500)); Check(m.Showing(), "kept past the keyboard moment while the pointer is there");
        m.Update(away, At(t0, 2600)); m.Update(away, At(t0, 3010)); Check(!m.Showing(), "pointer gone: out after the hover's 400 ms");
        Machine n; n.Flash(1, away, t0); n.Update(in(300, 90), At(t0, 200));
        Check(n.Pane() == 0 && !n.Flashing(), "the pointer at the other pane's top during the moment: the frames follow the pointer");
        Machine o; o.Update(in(300, 90), t0);   // the pointer already rests at the left pane's top
        Check(!o.Flash(1, in(300, 90), At(t0, 50)) && o.Candidate() == 0, "F6 while the pointer is over a pane's top: hover wins, no flash");
        Machine q; Check(!q.Flash(1, in(300, 500, false), t0) && q.Pane() == -1, "window not in front: no flash");
        Machine r; r.Flash(1, away, t0); r.Update(in(300, 500, false), At(t0, 300)); Check(!r.Showing() && !r.Flashing(), "the window leaves the front during the moment: out at once");
        Check(r.NextWake(At(t0, 300)).delay.has_value(), "frames requested while it leaves");
    }
    {   // Reduced motion: the 120 ms fade, no travel.
        Machine m; m.SetReduced(true); const auto away = in(300, 500);
        m.Flash(0, away, t0); Check(Near(m.Progress(At(t0, 125)), 1) && Near(PoseAt(m.Progress(At(t0, 60)), true, false).dx, 0), "reduced motion: F6 fades the frames in (120 ms), no travel");
        m.Update(away, At(t0, static_cast<int>(Timing::flash.count()) + 10)); Check(!m.Showing() && Near(m.Progress(At(t0, static_cast<int>(Timing::flash.count()) + 140)), 0), "and fades them out");
    }
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
