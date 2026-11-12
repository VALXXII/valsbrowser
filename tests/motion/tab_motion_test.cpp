// Deterministic checks for the tab open/close motion model (native only, no WebView2).
#include "../../src/tab-motion.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>

using namespace NativeChrome::Motion;
static int checks = 0, failures = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); } }
static Time T(double ms) { return Time{} + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms)); }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a-b) <= e; }
constexpr float Gap = 6, Tab = 128, Pitch = Tab+Gap;
static TabLayoutTarget Flat(int count, float tab = Tab) { return {tab, 0, count*(tab+Gap), false}; }
static const TabItem* Find(const TabLayoutFrame& f, unsigned key) { for (auto& i : f.items) if (i.key == key) return &i; return nullptr; }
static bool Settled(const TabStripMotion& m, double ms) { return !m.NextWake(T(ms)).delay; }
// Presented geometry equals the functional layout exactly (no residual offsets).
static bool Exact(const TabLayoutFrame& f, const std::vector<unsigned>& keys, TabLayoutTarget t) {
    if (f.items.size() != keys.size() || f.tab != t.tab || f.scroll != t.scroll || f.view != t.view) return false;
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& it = f.items[i];
        if (it.key != keys[i] || it.index != (int)i || it.presence != 1 || it.opacity != 1 || it.fill != 1 || it.exit != 0 || it.slot != i*(t.tab+Gap)) return false;
    }
    return true;
}

static void SpringChecks() {
    Spring s(.001f); s.Snap(0);
    Check(s.Value(T(0)) == 0 && !s.Moving(T(0)), "snapped spring rests exactly");
    s.Set(1, T(0), .3f);
    Check(s.Value(T(0)) == 0 && s.Moving(T(0)), "spring starts from the presented value");
    float previous = 0; bool monotonic = true, bounded = true;
    for (int ms = 1; ms <= 600; ++ms) { const float v = s.Value(T(ms)); monotonic &= v >= previous; bounded &= v <= 1; previous = v; }
    Check(monotonic && bounded, "critically damped from rest: monotonic, no overshoot (no bounce)");
    Check(s.Value(T(150)) > .5f && s.Value(T(150)) < .95f, "response .3 s is readable, not instant");
    Check(s.Value(T(700)) == 1 && !s.Moving(T(700)), "settles to the exact target in finite time");
    // Interruption: retarget mid-flight keeps value and velocity continuous.
    Spring r(.001f); r.Snap(0); r.Set(1, T(0), .3f);
    const float x = r.Value(T(80)), v = r.Velocity(T(80));
    r.Set(0, T(80), .22f);
    Check(Near(r.Value(T(80)), x, 1e-5f), "reversal starts from the presented value");
    Check(Near(r.Velocity(T(80)), v, 1e-3f), "reversal carries velocity (no brick wall)");
    Check(r.Value(T(81)) > x - .05f, "reversal decelerates instead of snapping back");
    const float before = r.Value(T(120)); r.Set(0, T(100), .22f);
    Check(r.Value(T(120)) == before, "same target never restarts the clock");
    r.Set(5, T(130), .3f, true);
    Check(r.Value(T(130)) == 5 && !r.Moving(T(130)), "reduced motion snaps");
}

static void OpenChecks() {
    TabStripMotion m;
    Check(!m.Sync({1, 2}, Flat(2), Gap, T(0)), "first sync is the initial state: no entrance");
    Check(Settled(m, 0) && Exact(m.Sample(T(0)), {1, 2}, Flat(2)), "initial geometry exact, no wake");
    Check(!m.Sync({1, 2}, Flat(2), Gap, T(5)) && Settled(m, 5), "repeated equal sync stays idle");
    // Open a tab in the middle: neighbour and new-tab button glide right.
    Check(m.Sync({1, 3, 2}, Flat(3), Gap, T(10)), "opening starts motion");
    auto f = m.Sample(T(10));
    Check(Find(f, 3)->presence == 0 && Near(Find(f, 2)->slot, Pitch) && Near(f.view, 2*Pitch), "opening starts collapsed; nothing jumps");
    float lastSlot = Find(f, 2)->slot, lastView = f.view; bool smooth = true, ordered = true;
    for (int ms = 11; ms <= 700; ++ms) {
        f = m.Sample(T(ms)); const float s = Find(f, 2)->slot;
        smooth &= s >= lastSlot - 1e-3f && s - lastSlot < 8 && f.view >= lastView - 1e-3f;
        ordered &= Find(f, 3)->slot >= Find(f, 1)->slot + Find(f, 1)->presence*Pitch - 1e-3f && Near(Find(f, 2)->slot, Find(f, 3)->slot + Find(f, 3)->presence*Pitch, 1e-3f);
        lastSlot = s; lastView = f.view;
    }
    Check(smooth, "neighbour and plus glide monotonically, no per-frame jump > 8 DIP");
    Check(ordered, "cumulative layout: neighbours stay contiguous during the opening");
    f = m.Sample(T(160));
    Check(Find(f, 3)->presence > .5f && Find(f, 3)->presence < 1, "opening is perceptible mid-way");
    f = m.Sample(T(50));
    Check(Find(f, 3)->presence > 0 && Find(f, 3)->opacity < Find(f, 3)->presence, "content trails the growing surface");
    Check(Settled(m, 700) && Exact(m.Sample(T(700)), {1, 3, 2}, Flat(3)), "opening settles on the exact layout");
    Check(!m.Sync({1, 3, 2}, Flat(3), Gap, T(800)) && m.Entries() == 3, "idle after settle");
}

static void CloseChecks() {
    TabStripMotion m; m.Sync({1, 2, 3}, Flat(3), Gap, T(0));
    Check(m.Sync({1, 3}, Flat(2), Gap, T(10)), "closing starts motion");
    auto f = m.Sample(T(10));
    Check(f.items.size() == 3 && f.items[1].key == 2 && f.items[1].index == -1, "closed tab stays as a ghost in its old place");
    Check(f.items[2].index == 1 && Near(f.items[2].slot, 2*Pitch), "functional index updates at once; presented slot does not jump");
    float previous = 1; bool down = true;
    for (int ms = 11; ms <= 600; ++ms) { const float p = m.Sample(T(ms)).items.size() == 3 ? m.Sample(T(ms)).items[1].presence : 0; down &= p <= previous + 1e-4f; previous = p; }
    Check(down, "ghost slot collapses monotonically");
    {
        // Blur-out exit: 150 ms ease-out, monotonic, finished before the slot closes.
        TabStripMotion b; b.Sync({1, 2, 3}, Flat(3), Gap, T(0)); b.Sync({1, 3}, Flat(2), Gap, T(0));
        Check(Find(b.Sample(T(0)), 2)->exit == 0 && Find(b.Sample(T(0)), 2)->opacity == 1, "blur-out starts sharp and opaque");
        float last = 0; bool rising = true; for (int ms = 1; ms < 150; ++ms) { const float e = Find(b.Sample(T(ms)), 2)->exit; rising &= e >= last && e < 1; last = e; }
        Check(rising && Find(b.Sample(T(75)), 2)->exit > .5f, "blur-out eases out monotonically");
        Check(Find(b.Sample(T(150)), 2)->exit == 1 && Find(b.Sample(T(150)), 2)->presence > 0, "blur-out ends at 150 ms, before the slot has closed");
        Check(!Find(b.Sample(T(600)), 2) && Settled(b, 600), "ghost leaves once blurred out and collapsed");
        TabStripMotion r; r.Sync({1, 2}, Flat(2), Gap, T(0)); r.Sync({1}, Flat(1), Gap, T(0));
        const float e = Find(r.Sample(T(60)), 2)->exit; r.Sync({1, 2}, Flat(2), Gap, T(60));
        Check(Near(Find(r.Sample(T(60)), 2)->exit, e, 1e-5f) && Find(r.Sample(T(120)), 2)->exit < e, "revived tab un-blurs from where it was");
        Check(Settled(r, 700) && Exact(r.Sample(T(700)), {1, 2}, Flat(2)), "revived tab settles sharp and exact");
    }
    // Exit is shorter than entrance.
    TabStripMotion o; o.Sync({1}, Flat(1), Gap, T(0)); o.Sync({1, 2}, Flat(2), Gap, T(0));
    auto reach = [](auto sample, bool opening) { for (int ms = 0; ms < 1000; ++ms) { const float p = sample(ms); if (opening ? p >= .95f : p <= .05f) return ms; } return 1000; };
    const int open = reach([&](int ms){ return Find(o.Sample(T(ms)), 2)->presence; }, true);
    const int close = reach([&](int ms){ auto g = m.Sample(T(10+ms)); auto* i = Find(g, 2); return i ? i->presence : 0.f; }, false);
    Check(close < open && close >= 100 && open <= 320, "closing is a little shorter than opening, both visible");
    Check(Settled(m, 600) && !Find(m.Sample(T(600)), 2) && Exact(m.Sample(T(600)), {1, 3}, Flat(2)), "collapsed ghost leaves the presentation; exact layout");
    m.Sync({1, 3}, Flat(2), Gap, T(610));
    Check(m.Entries() == 2 && Exact(m.Sample(T(610)), {1, 3}, Flat(2)), "settled ghost is pruned; exact layout");
}

static void InterruptChecks() {
    // Close while still opening: reverses from the presented presence.
    TabStripMotion m; m.Sync({1}, Flat(1), Gap, T(0)); m.Sync({1, 2}, Flat(2), Gap, T(0));
    const float p = Find(m.Sample(T(90)), 2)->presence;
    m.Sync({1}, Flat(1), Gap, T(90));
    auto f = m.Sample(T(90));
    Check(Near(Find(f, 2)->presence, p, 1e-5f) && Find(f, 2)->index == -1, "close during opening starts from the displayed width");
    float peak = 0; for (int ms = 90; ms <= 200; ++ms) peak = std::max(peak, Find(m.Sample(T(ms)), 2)->presence);
    Check(peak <= p + .06f && Find(m.Sample(T(200)), 2)->presence < p, "reversal carries momentum briefly, then collapses");
    Check(Settled(m, 700), "interrupted motion still ends");
    // Rapid opens every 30 ms: continuous at every event, monotonic, exact end.
    TabStripMotion r; std::vector<unsigned> keys{1}; r.Sync(keys, Flat(1), Gap, T(0));
    bool continuous = true, monotonic = true; float last = r.Sample(T(0)).content;
    for (int i = 0; i < 6; ++i) {
        const double at = 30.0*i+1;
        const float before = r.Sample(T(at)).content;
        keys.push_back(10+i); r.Sync(keys, Flat((int)keys.size()), Gap, T(at));
        continuous &= Near(r.Sample(T(at)).content, before, 1e-3f);
    }
    for (int ms = 1; ms <= 900; ++ms) { const float c = r.Sample(T(ms)).content; monotonic &= c >= last - 1e-3f; last = c; }
    Check(continuous, "rapid opens: each event continues from the displayed strip");
    Check(monotonic, "rapid opens: the strip only grows, never stutters back");
    Check(Settled(r, 900) && Exact(r.Sample(T(900)), keys, Flat((int)keys.size())), "rapid opens settle exactly");
    // Rapid closes with a locked width: neighbours converge under the pointer.
    TabStripMotion c; std::vector<unsigned> all{1, 2, 3, 4, 5}; c.Sync(all, Flat(5, 100), Gap, T(0));
    for (int i = 0; i < 3; ++i) {
        const double at = 40.0*i+1; const float before = c.Sample(T(at)).content;
        all.erase(all.begin()+1); TabLayoutTarget t{100, 0, all.size()*106.f, false}; c.Sync(all, t, Gap, T(at));
        continuous &= Near(c.Sample(T(at)).content, before, 1e-3f) && c.Sample(T(at)).tab == 100;
    }
    Check(continuous, "rapid closes keep the locked width and continuity");
    Check(Settled(c, 900) && Exact(c.Sample(T(900)), all, {100, 0, all.size()*106.f, false}), "rapid closes settle exactly");
    // A key re-added while its ghost collapses grows back from where it is.
    TabStripMotion g; g.Sync({1, 2}, Flat(2), Gap, T(0)); g.Sync({1}, Flat(1), Gap, T(0));
    const float q = Find(g.Sample(T(60)), 2)->presence; g.Sync({1, 2}, Flat(2), Gap, T(60));
    Check(Find(g.Sample(T(60)), 2)->index == 1 && Near(Find(g.Sample(T(60)), 2)->presence, q, 1e-5f), "revived tab continues from its presented width");
}

static void LayoutChecks() {
    // Resize / DPI (non-structural width change) follows immediately.
    TabStripMotion m; m.Sync({1, 2}, Flat(2), Gap, T(0));
    Check(!m.Sync({1, 2}, Flat(2, 90), Gap, T(10)) && m.Sample(T(10)).tab == 90 && Settled(m, 10), "resize snaps: no lag behind the window edge");
    // Explicit reflow (width unlock after pointer closes) glides.
    Check(m.Sync({1, 2}, Flat(2, 128), Gap, T(20), true), "explicit reflow animates");
    const float w = m.Sample(T(80)).tab; Check(w > 90 && w < 128, "width unlock glides between widths");
    Check(Settled(m, 800) && Exact(m.Sample(T(800)), {1, 2}, Flat(2)), "reflow settles exactly");
    // Reorder (drag, Ctrl+Shift+PgUp) is not an open/close: presented order follows at once.
    Check(!m.Sync({2, 1}, Flat(2), Gap, T(900)) && Settled(m, 900) && Exact(m.Sample(T(900)), {2, 1}, Flat(2)), "reorder is immediate, no ghost");
    // Overflow: the visible width never exceeds the content and moves continuously.
    TabStripMotion o; std::vector<unsigned> keys{1, 2, 3, 4};
    o.Sync(keys, {60, 0, 4*66.f, false}, Gap, T(0));
    const float before = o.Sample(T(5)).view;
    keys.push_back(5); o.Sync(keys, {60, 40, 4*66.f-20, true}, Gap, T(5));
    Check(Near(o.Sample(T(5)).view, before, 1e-3f), "entering overflow keeps the presented view");
    bool bounded = true; for (int ms = 5; ms < 800; ++ms) { auto f = o.Sample(T(ms)); bounded &= f.view <= f.content + 1e-3f && f.scroll >= 0 && f.scroll <= f.content - f.view + 1e-3f; }
    Check(bounded, "overflow: view within content, scroll within range");
    Check(Settled(o, 800) && o.Sample(T(800)).view == 4*66.f-20 && o.Sample(T(800)).scroll == 40, "overflow settles on the layout");
    const float shown = o.Sample(T(810)).view;
    keys.pop_back(); o.Sync(keys, {60, 0, 4*66.f, false}, Gap, T(810));
    Check(Near(o.Sample(T(810)).view, shown, 1e-3f), "leaving overflow keeps the presented view");
    Check(Settled(o, 1600) && Exact(o.Sample(T(1600)), keys, {60, 0, 4*66.f, false}), "leaving overflow settles exactly");
}

static void ReducedChecks() {
    TabStripMotion m; m.SetReducedMotion(true); m.Sync({1}, Flat(1), Gap, T(0));
    m.Sync({1, 2}, Flat(2), Gap, T(10));
    auto f = m.Sample(T(10));
    Check(Find(f, 2)->presence == 1 && Near(Find(f, 2)->slot, Pitch) && Near(f.view, 2*Pitch), "reduced: geometry is final at once (no slide)");
    Check(Find(f, 2)->fill == 0 && Find(f, 2)->opacity == 0, "reduced: the new tab cross-fades in");
    Check(Find(m.Sample(T(85)), 2)->opacity > 0 && Find(m.Sample(T(85)), 2)->opacity < 1, "reduced: opacity-only fade is visible");
    Check(Settled(m, 160) && Exact(m.Sample(T(160)), {1, 2}, Flat(2)), "reduced: 150 ms fade then exact");
    m.Sync({2}, Flat(1), Gap, T(200));
    Check(m.Entries() == 1 && Settled(m, 200) && Exact(m.Sample(T(200)), {2}, Flat(1)), "reduced: closing removes at once");
    // Turning reduced motion on mid-flight settles everything immediately.
    TabStripMotion r; r.Sync({1, 2}, Flat(2), Gap, T(0)); r.Sync({1, 3}, Flat(2), Gap, T(0));
    r.SetReducedMotion(true);
    Check(Settled(r, 1) && Exact(r.Sample(T(1)), {1, 3}, Flat(2)), "preference change settles opens and closes");
    // Restored session: the host settles the burst of opens.
    TabStripMotion s; s.Sync({}, Flat(0), Gap, T(0));
    std::vector<unsigned> keys; for (unsigned k = 1; k <= 6; ++k) { keys.push_back(k); s.Sync(keys, Flat((int)k), Gap, T(1)); }
    s.Settle();
    Check(Settled(s, 1) && Exact(s.Sample(T(1)), keys, Flat(6)), "restored tabs appear settled, no entrance cascade");
}

int main() {
    SpringChecks(); OpenChecks(); CloseChecks(); InterruptChecks(); LayoutChecks(); ReducedChecks();
    if (failures) { std::fprintf(stderr, "%d of %d tab motion checks failed\n", failures, checks); return 1; }
    std::printf("PASS: %d deterministic tab motion checks; native only, no WebView2.\n", checks);
}
