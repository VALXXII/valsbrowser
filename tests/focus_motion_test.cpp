// Mode Focus entry / exit motion model (src/focus-motion.h; Designer's spec 2026-11-25, Paper « Mode Focus — mouvement
// d'entrée et de sortie »): the critically damped spring (response 0.3 s, damping 1.0), its interruption from the
// position and velocity on screen, the bar's fade over 60 % / 40 % of the path, and the in-between layout (the page's
// edge glued to the bar's, the canvas only in the inset). Pure: no Win32, no window.
#include "../src/focus-motion.h"
#include <cstdio>
#include <cstdlib>

namespace FX = NativeChrome::FocusMotion;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static bool Near(double a, double b, double e = 1e-6) { return std::fabs(a - b) <= e; }

int main() {
    // ---- the spec's values ----
    Require(FX::responseS == 0.3 && FX::dampingRatio == 1.0 && FX::enterFadeShare == 0.6 && FX::exitFadeShare == 0.4 && FX::crossFadeMs == 120, "spec: spring 1.0 / 0.3 s, fades 60 % / 40 %, cross-fade 120 ms");

    // ---- the spring from rest: no bounce, monotonic, at the analytic curve whatever the frame rate ----
    {   FX::Spring s; s.Snap(0); s.Retarget(1); double prev = 0, t = 0; bool monotonic = true, overshoot = false;
        while (t < 1.0) { s.Step(1.0 / 60); t += 1.0 / 60; if (s.x < prev - 1e-12) monotonic = false; if (s.x > 1 + 1e-9) overshoot = true; prev = s.x; }
        Require(monotonic && !overshoot && s.x == 1 && s.v == 0, "from rest: monotonic, never past the target (no bounce), settled within 1 s");
        auto exact = [](double t) { const double w = FX::omega; return 1 - (1 + w * t) * std::exp(-w * t); };
        FX::Spring a, b; a.Snap(0); a.Retarget(1); b.Snap(0); b.Retarget(1);
        for (int i = 0; i < 18; ++i) a.Step(1.0 / 60);    // 0.3 s at 60 Hz
        for (int i = 0; i < 45; ++i) b.Step(1.0 / 150);   // 0.3 s at 150 Hz
        Require(Near(a.x, exact(.3), 1e-9) && Near(b.x, exact(.3), 1e-9), "analytic: the same place at 0.3 s at 60 or 150 frames a second");
        Require(exact(.3) > .98 && exact(.15) > .8 && exact(.05) < .5, "response 0.3 s: 98.6 % at 0.3 s, past 80 % at 0.15 s");
        FX::Spring c; c.Snap(0); c.Retarget(1); double end = 0; for (int i = 1; i <= 120 && !c.Settled(); ++i) { c.Step(1.0 / 120); end = i / 120.0; }
        Require(end > .35 && end < .6, "it settles in about half a second (no long tail)");
    }
    // ---- interruptible: a second press restarts from the position on screen, keeping the velocity ----
    {   FX::Spring s; s.Snap(0); s.Retarget(1); for (int i = 0; i < 6; ++i) s.Step(1.0 / 60);   // 100 ms into the entry
        const double x = s.x, v = s.v; Require(x > .2 && x < .8 && v > 0, "mid-entry: on the way, moving toward Focus");
        s.Retarget(0); Require(s.x == x && s.v == v, "the second press keeps the position and the velocity on screen (never jumps to a target)");
        s.Step(1.0 / 240); Require(s.x > x - 1e-3 && std::fabs(s.x - x) < .02, "the very next frame continues from there (no jump), the velocity carrying it a little further first");
        double peak = s.x; for (int i = 0; i < 120; ++i) { s.Step(1.0 / 120); peak = std::max(peak, s.x); }
        Require(s.x == 0 && peak < x + .1, "it then comes back to the normal layout, settled");
    }
    // ---- the bar's fade along the path ----
    {   const auto in = FX::Fade::Toward(1, 0, 1);
        Require(Near(in.At(0), 1) && Near(in.At(.3), .5) && Near(in.At(.6), 0) && Near(in.At(.9), 0), "entry from rest: 1 -> 0 over the first 60 % of the path");
        const auto out = FX::Fade::Toward(0, 1, 0);
        Require(Near(out.At(1), 0) && Near(out.At(.8), .5) && Near(out.At(.6), 1) && Near(out.At(.1), 1), "exit from rest: 0 -> 1 over the first 40 % of the path");
        const double shown = in.At(.25); const auto back = FX::Fade::Toward(0, .25, shown);
        Require(Near(back.At(.25), shown) && Near(back.At(.25 - .25 * .4), 1) && back.At(.2) > shown, "a reversal mid-way starts from the opacity on screen (no jump) and reaches 1 over 40 % of the way back");
    }
    // ---- the in-between layout: the page glued to the bar, the canvas only in the inset ----
    {   const float W = 1180, H = 815;
        FX::Layout n; n.barHeight = 81; n.page = {2, 81, W - 2, H - 2}; n.topRadius = 0; n.bottomRadius = 8; n.chat = {}; n.drawerTop = 81;
        FX::Layout f; f.barHeight = 0; f.page = {4, 4, W - 4, H - 4}; f.topRadius = 6; f.bottomRadius = 6; f.chat = {}; f.drawerTop = 92;
        const auto a = FX::At(n, f, 0, 1), b = FX::At(n, f, 1, 0);
        Require(a.barOffset == 0 && a.page.top == 81 && a.page.left == 2 && a.bottomRadius == 8 && a.topRadius == 0 && a.drawerTop == 81, "x 0: the normal layout exactly");
        Require(b.barOffset == -81 && b.page.top == 4 && b.page.left == 4 && b.page.right == W - 4 && b.page.bottom == H - 4 && b.topRadius == 6 && b.bottomRadius == 6 && b.drawerTop == 92, "x 1: Mode Focus exactly (today's layout: inset 4, radius 6)");
        bool glued = true, monotonic = true; float prevTop = 82;
        for (int i = 0; i <= 100; ++i) {
            const double x = i / 100.0; const auto fr = FX::At(n, f, x, 1);
            const float gap = FX::Gap(n, fr);
            if (gap < -1e-4f || gap > 4 * static_cast<float>(x) + 1e-4f) glued = false;   // the gap is the inset growing 0 -> 4
            if (fr.page.top > prevTop + 1e-4f) monotonic = false;
            prevTop = fr.page.top;
        }
        Require(glued, "every frame: the page's top edge on the bar's bottom edge plus the growing inset (0 -> 4), never a hole");
        Require(monotonic, "the page grows on the same curve (its top never goes back down on the way in)");
        const auto over = FX::At(n, f, 1.04, 0), under = FX::At(n, f, -.03, 1);
        Require(over.page.top == 4 && under.page.top == 81, "a reversal's small carry past an end is clamped (never past either layout)");
        // With the annotation banner (Paper 203J-0): the page under it in both layouts, the banner glued to the page's top.
        FX::Layout na = n; na.page.top = 81 + 32; FX::Layout fa = f; fa.page.top = 32; bool bannerGlued = true;
        for (int i = 0; i <= 20; ++i) { const auto fr = FX::At(na, fa, i / 20.0, 1); if (FX::Gap(na, fr) < 32 - 1e-3f) bannerGlued = false; }
        Require(bannerGlued, "with the annotation banner: room for it (32) between the bar and the page at every frame");
    }
    std::printf("PASS focus motion: %d checks (spring 1.0 / 0.3 s, interruption, fades 60 / 40 %%, glued layout)\n", checks);
    return 0;
}
