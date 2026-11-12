// Back/Forward travel spring and Reload <-> Stop morph (src/nav-motion.h). Pure model test:
//   g++ -std=c++17 -Wall -Wextra -Werror -pedantic -static tests/motion/nav_motion_test.cpp -o nav_motion_test.exe -luser32
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../../src/nav-motion.h"
#include "../../src/motion-tuning.h"
#include "../../src/lottie-playback.h"
#include "../../src/download-icon.h"
using namespace NativeChrome;
using namespace std::chrono;
static int failures = 0;
static void Expect(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } }
static Motion::Time At(Motion::Time t0, int ms) { return t0 + milliseconds(ms); }

int main() {
    const Motion::Time t0 = Motion::Now();
    // ---- Travel spring ----
    {
        NavMotion::Spring s;
        Expect(!s.Moving(t0) && s.Position(t0) == 0 && !s.NextWake(t0).delay, "rest: no displacement, no frames");
        s.Kick(-1, t0, false);
        Expect(s.NextWake(t0).frames, "a kick asks for frames");
        float peak = 0; int peakAt = 0; bool crossed = false;
        for (int ms = 0; ms <= 1000; ++ms) {
            const float x = s.Position(At(t0, ms));
            if (x < peak) { peak = x; peakAt = ms; }
            if (x > 1e-4f) crossed = true;
        }
        Expect(std::fabs(-peak - NavMotion::Travel::peak) < .05f, "back travels ~3 DIP to the left");
        Expect(peakAt >= 50 && peakAt <= 80, "peak about 1/omega (64 ms) after the click: response is immediate");
        Expect(!crossed, "critically damped: never overshoots past rest");
        Expect(s.Moving(At(t0, 300)), "still settling at 300 ms");
        Expect(!s.Moving(At(t0, 700)), "settled within 700 ms");
        s.Settle(At(t0, 700));
        Expect(!s.NextWake(At(t0, 700)).delay && s.Position(At(t0, 700)) == 0, "no idle frames once settled");

        // Interruptible: a second click mid-flight continues from the live state, no jump.
        NavMotion::Spring f; f.Kick(1, t0, false);
        const auto mid = At(t0, 40);
        const float before = f.Position(mid);
        f.Kick(1, mid, false);
        Expect(std::fabs(f.Position(mid) - before) < 1e-4f, "re-kick starts from the presentation value (no jump)");
        float maxX = 0; for (int ms = 40; ms < 800; ++ms) maxX = std::max(maxX, f.Position(At(t0, ms)));
        Expect(maxX > NavMotion::Travel::peak * .9f && maxX < NavMotion::Travel::peak * 1.25f, "repeated clicks keep the travel near its peak (bounded)");
        // Reversal blends velocity: the position stays continuous.
        const auto rev = At(t0, 60); const float x = f.Position(rev);
        f.Kick(-1, rev, false);
        Expect(std::fabs(f.Position(rev) - x) < 1e-4f, "reversal is continuous");

        NavMotion::Spring r; r.Kick(-1, t0, true);
        Expect(!r.Moving(t0) && r.Position(At(t0, 50)) == 0, "reduced motion: no travel");
    }
    // ---- Reload <-> Stop ----
    {
        const auto turnEnd = At(t0, 500);
        NavMotion::StopMorph m;
        Expect(m.Value(t0) == 0 && !m.NextWake(t0).delay, "rest: reload, no frames");
        // Reload clicked (turn until 500 ms), navigation starts at 20 ms.
        m.Sync(true, turnEnd, At(t0, 20), false);
        Expect(m.Value(At(t0, 200)) == 0, "the X waits for the reload turn");
        Expect(m.NextWake(At(t0, 200)).delay && !m.NextWake(At(t0, 200)).frames, "held: a deadline at the turn end, not frames");
        m.Settle(At(t0, 500));
        Expect(m.Moving(At(t0, 520)) && m.NextWake(At(t0, 520)).frames, "turn over, still loading: morphs to X");
        m.Settle(At(t0, 800));
        Expect(m.Value(At(t0, 800)) == 1 && !m.NextWake(At(t0, 800)).delay, "X held while loading without frames");
        auto layers = m.Layers(At(t0, 800));
        Expect(layers.stop.opacity == 1 && layers.reload.opacity == 0 && layers.stopAngle == 0 && layers.stop.scale == 1, "X at rest is the plain glyph");
        // Loading ends: back to reload.
        m.Sync(false, turnEnd, At(t0, 900), false);
        Expect(m.Moving(At(t0, 950)), "load end morphs back to reload");
        const float a = m.Value(At(t0, 950));
        m.Sync(true, turnEnd, At(t0, 950), false); // a new load mid-morph
        Expect(std::fabs(m.Value(At(t0, 950)) - a) < 1e-4f, "re-target mid-morph: no jump");
        m.Sync(false, turnEnd, At(t0, 960), false);
        m.Settle(At(t0, 1300));
        Expect(m.Value(At(t0, 1300)) == 0 && !m.NextWake(At(t0, 1300)).delay, "rest again, no frames");
        layers = m.Layers(At(t0, 1300));
        Expect(layers.reload.opacity == 1 && layers.reload.scale == 1 && layers.reloadAngle == 0, "reload at rest is the plain glyph");

        // Load finishing during the turn never shows the X.
        NavMotion::StopMorph quick;
        quick.Sync(true, turnEnd, At(t0, 10), false);
        quick.Sync(false, turnEnd, At(t0, 300), false);
        quick.Settle(At(t0, 600));
        float seen = 0; for (int ms = 0; ms < 900; ms += 5) seen = std::max(seen, quick.Value(At(t0, ms)));
        Expect(seen == 0, "fast reload: no X flash");

        // A navigation without Reload (link, address): X directly.
        NavMotion::StopMorph link;
        link.Sync(true, Motion::Time{}, t0, false);
        Expect(link.Moving(At(t0, 10)) && link.Value(At(t0, 300)) == 1, "link navigation morphs straight to X");

        // Reduced motion: instant swap, no turn wait, no rotation.
        NavMotion::StopMorph reduced;
        reduced.Sync(true, turnEnd, At(t0, 20), true);
        Expect(reduced.Value(At(t0, 20)) == 1 && !reduced.NextWake(At(t0, 20)).delay, "reduced: X immediately, no frames");
        layers = reduced.Layers(At(t0, 20));
        Expect(layers.stopAngle == 0 && layers.stop.scale == 1, "reduced: no rotation or scale");

        // Snap (tab switch) lands without animating.
        NavMotion::StopMorph snap; snap.Sync(true, Motion::Time{}, t0, false);
        snap.Snap(false);
        Expect(snap.Value(At(t0, 10)) == 0 && !snap.NextWake(At(t0, 10)).delay, "snap: reload immediately");
    }
    // ---- Aggregate ----
    {
        NavMotion::State s;
        Expect(!s.NextWake(t0).delay, "idle chrome: no timer");
        s.back.Kick(-1, t0, false);
        s.Reduce(false);
        Expect(!s.NextWake(t0).delay && s.back.Position(t0) == 0, "Reduce lands everything at rest");
    }
    // ---- Motion lab tuning ----
    {
        namespace T = Tuning;
        T::Values v;
        Expect(v.IsDefault() && v[T::BackPeak] == NavMotion::Travel::peak && v[T::BackResponse] == (float)NavMotion::Travel::response, "lab defaults are the shipped travel");
        Expect(v[T::StopMorph] == (float)NavMotion::MorphDuration.count() && v[T::StopTurn] == NavMotion::MorphTurn && v[T::DownloadRipple] == (float)DownloadIcon::Ripple::duration.count(), "lab defaults are the shipped morph and ripple");
        v.Set(T::BackPeak, 99); Expect(v[T::BackPeak] == T::Spec(T::BackPeak).max, "values clamp to their bounds");
        v.Set(T::RefreshTurn, 503); Expect(v[T::RefreshTurn] == 500, "values snap to their step");
        v.Set(T::CopySpeed, 1.35f);
        T::Values w; Expect(w.Parse(v.Serialize()) == T::Count, "every value round-trips");
        bool same = true; for (int i = 0; i < T::Count; ++i) same = same && std::fabs(w[(T::Id)i] - v[(T::Id)i]) < 1e-4f;
        Expect(same, "serialised values restore exactly");
        T::Values bad; Expect(bad.Parse(std::string("# c") + '\n' + "unknown=3" + '\n' + "back.peak=abc" + '\n' + "back.peak=-5" + '\n') == 1 && bad[T::BackPeak] == 0, "unknown keys and junk are ignored, values clamp");
        // Spring knobs are real: amplitude, response and damping change the motion.
        NavMotion::Spring s; s.Kick(1, t0, false, {6, .25, 1});
        float peak = 0; int at = 0; for (int ms = 0; ms < 600; ++ms) { const float x = s.Position(At(t0, ms)); if (x > peak) { peak = x; at = ms; } }
        Expect(std::fabs(peak - 6) < .1f && at < 50, "tuned amplitude and a faster response");
        NavMotion::Spring b; b.Kick(1, t0, false, {3, .4, .6});
        float low = 0, high = 0; for (int ms = 0; ms < 1500; ++ms) { const float x = b.Position(At(t0, ms)); low = std::min(low, x); high = std::max(high, x); }
        Expect(std::fabs(high - 3) < .1f && low < -.1f, "damping below 1 overshoots rest");
        Expect(!b.Moving(At(t0, 3000)), "an underdamped spring still settles");
        // Stop morph: duration, rotation and the visual-only preview.
        NavMotion::StopMorph m; m.Configure(Motion::Milliseconds(100), 45);
        m.Preview(At(t0, 1000), At(t0, 500), t0, false);
        Expect(m.Value(At(t0, 400)) == 0 && m.NextWake(At(t0, 400)).delay, "preview waits for the turn (deadline, no frames)");
        m.Settle(At(t0, 500));
        Expect(m.Value(At(t0, 550)) > 0 && m.Value(At(t0, 550)) < 1, "then morphs to the X");
        m.Settle(At(t0, 700));
        Expect(m.Value(At(t0, 700)) == 1, "a 100 ms morph has finished at 200 ms");
        auto layers = m.Layers(At(t0, 550)); Expect(layers.stopAngle <= 45 && layers.reloadAngle >= -45, "rotation bound follows the tuned angle");
        m.Settle(At(t0, 1000)); m.Settle(At(t0, 1200));
        Expect(m.Value(At(t0, 1200)) == 0 && !m.NextWake(At(t0, 1200)).delay, "preview ends back on Reload, no idle frames");
        // Lottie timing knobs.
        Lottie::Playback p; p.Configure(19. / 30, .5, 20);
        p.Tune(1.0, 2, .5); p.RefreshBegin(t0);
        Expect(p.RefreshActive(At(t0, 900)) && !p.RefreshActive(At(t0, 1001)) && std::fabs(p.RefreshProgress(At(t0, 500)) - .5) < .01, "tuned turn length");
        p.CopySucceeded(t0);
        Expect(p.CopyActive(At(t0, 700)) && !p.CopyActive(At(t0, 900)), "copy speed x2 and a 500 ms hold");
        Lottie::Playback d; d.Configure(19. / 30, .5, 20); d.RefreshBegin(t0);
        Expect(d.RefreshActive(At(t0, 490)) && !d.RefreshActive(At(t0, 510)), "untuned playback keeps the authored 500 ms");
        // Download icon speed and ripple.
        DownloadIcon::Model dl; dl.Tune(2, Motion::Milliseconds(300));
        dl.Update(true, 0, false, t0, false);
        Expect(dl.At(At(t0, 90)).ring < DownloadIcon::Morph && dl.At(At(t0, 130)).ring >= DownloadIcon::Morph, "speed x2 finishes the 14-frame start in about 117 ms (233 ms at x1)");
        dl.Update(false, 1, true, At(t0, 200), false);
        Expect(dl.At(At(t0, 250)).ripple >= 0 && dl.At(At(t0, 510)).ripple < 0, "tuned ripple length");
    }
    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("nav motion: all checks passed");
    return 0;
}
