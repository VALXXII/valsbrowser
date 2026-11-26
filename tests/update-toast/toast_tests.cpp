// Native-only tests for the update card (Val's « Mise à jour » card, 2026-11-26): model and texts, Paper geometry,
// motion (the three states, the drawer icon's timeline, cancel, interruptions, reduced motion), « Plus tard », click
// arbitration and dwell, Direct2D renders (the real GrainGradient, PNG snapshots and transition strips, the card-only
// renders compare.py checks against the validated preview), and the popup window (placement in the layouts, input,
// keyboard, and no cost at all while the card is hidden). No WebView2, no network.
// Usage: toast-tests.exe <repoRoot> <snapshotDir> [--no-window]
#include "../../src/update-toast.cpp"
#include <wincodec.h>
#include <cstdio>
#include <vector>

using namespace BrowserUpdate;
using namespace BrowserUpdate::ToastModel;
namespace M = NativeChrome::Motion;

static int failures = 0, checks = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }
static M::Time T0() { return M::Time{} + std::chrono::hours(1); }
static M::Time At(double ms) { return T0() + std::chrono::duration_cast<M::Clock::duration>(std::chrono::duration<double, std::milli>(ms)); }

static View V(Phase p, std::uint64_t received = 0, std::uint64_t total = 0, double eta = -1, std::wstring error = L"", std::wstring version = L"0.3.2") {
    View v; v.phase = p; v.received = received; v.total = total; v.secondsRemaining = eta; v.error = std::move(error); v.version = std::move(version); return v;
}
static const std::uint64_t PaperTotal = 209715200, PaperReceived = 52848230;   // 200,0 Mo / 50,4 Mo
struct ProfileInk { const char* name; ProfileColor::Rgb8 ink; };
static const ProfileInk Profiles6[6] = {{"gris", {0xB3, 0xB3, 0xB3}}, {"vert", {0x6F, 0xD0, 0x87}}, {"cyan", {0x4F, 0xC3, 0xD9}},
                                        {"violet", {0xB4, 0x9B, 0xFF}}, {"rose", {0xF0, 0x8B, 0xC4}}, {"ambre", {0xE6, 0xAC, 0x3D}}};
static unsigned long RgbOf(ProfileColor::Rgb8 c) { return (static_cast<unsigned long>(c.r) << 16) | (static_cast<unsigned long>(c.g) << 8) | c.b; }

// French interface (the default): Val's texts (with Chief's two corrections) and the French number format.
static void FrenchModelTests() {
    CHECK(I18n::Current() == I18n::Locale::Fr);
    auto a = Describe(V(Phase::Available));
    CHECK(a.title == L"Mise à jour disponible" && a.body == L"Une nouvelle mise à jour est prête à être téléchargée.");
    CHECK(a.buttons.size() == 2 && a.buttons[0].label == L"Télécharger" && a.buttons[0].action == Action::Download);
    CHECK(a.buttons[1].label == L"Plus tard" && a.buttons[1].action == Action::Later);
    CHECK(a.icon == Glyph::Tray);
    auto d = Describe(V(Phase::Downloading, PaperReceived, PaperTotal, 14));
    CHECK(d.title == L"Téléchargement en cours…" && d.size == L"50,4/200,0 Mo" && d.eta == L"00:14" && d.icon == Glyph::Drawer);
    CHECK(d.buttons.size() == 1 && d.buttons[0].label == L"Annuler" && d.buttons[0].action == Action::Cancel && d.body.empty());
    auto r = Describe(V(Phase::Ready));
    CHECK(r.title == L"Prête à installer" && r.body == L"La mise à jour est prête à être installée." && r.icon == Glyph::Check);
    CHECK(r.buttons.size() == 1 && r.buttons[0].label == L"Redémarrer et installer" && r.buttons[0].action == Action::RestartInstall);
    CHECK(Describe(V(Phase::Downloading, 3 * 1048576 + 300000, 0, -1)).size == L"3,3 Mo");
    CHECK(FormatSize(1500ull * 1048576ull, 2000ull * 1048576ull) == L"1,5/2,0 Go");
    auto e = Describe(V(Phase::Error));
    CHECK(e.kind == Kind::Error && !e.body.empty() && e.buttons.size() == 2 && e.buttons[0].action == Action::Retry && e.buttons[1].action == Action::Dismiss);
}

static void ModelTests() {
    auto a = Describe(V(Phase::Available));
    CHECK(a.title == L"New update available" && a.body == L"A new update is ready to be downloaded." && a.buttons[0].label == L"Download" && a.buttons[1].label == L"Later");
    auto d = Describe(V(Phase::Downloading, PaperReceived, PaperTotal, 14));
    CHECK(d.title == L"Downloading…" && d.buttons[0].label == L"Cancel" && d.size == L"50.4/200.0MB" && d.eta == L"00:14");
    auto i = Describe(V(Phase::Installed));
    CHECK(i.title == L"Update installed" && i.buttons.empty() && i.icon == Glyph::Check);
    CHECK(Describe(V(Phase::Installing)).buttons.empty() && Describe(V(Phase::Installing)).icon == Glyph::Tray);
    CHECK(Describe(V(Phase::Hidden)).kind == Kind::None && Describe(V(Phase::Checking)).kind == Kind::None);
    auto e = Describe(V(Phase::Error, 0, 0, -1, L"Échec du téléchargement"));
    CHECK(e.kind == Kind::Error && e.body == L"Échec du téléchargement" && e.icon == Glyph::Info);
    CHECK(Describe(V(Phase::Error)).body == L"The update could not be completed.");
    // Never invented: unknown total and remaining time.
    auto u = Describe(V(Phase::Downloading, 3 * 1048576 + 300000, 0, -1));
    CHECK(u.size == L"3.3MB" && u.eta.empty() && u.progress < 0);
    CHECK(FormatEta(-1).empty() && FormatEta(NAN).empty() && FormatEta(0) == L"00:00" && FormatEta(13.2) == L"00:14" && FormatEta(59.2) == L"01:00" && FormatEta(3725) == L"1:02:05");
    CHECK(FormatSize(0, PaperTotal) == L"0.0/200.0MB" && FormatSize(300 * 1048576ull, 200 * 1048576ull) == L"200.0/200.0MB");
    CHECK(FormatSize(512ull * 1048576, 1536ull * 1048576) == L"0.5/1.5GB");
    CHECK(Describe(V(Phase::Downloading, 50, 100)).progress == .5f);
    // Paper geometry (Layout with Paper's line counts and « Plus tard » 85.609375 wide).
    auto ga = Layout(a, 269, 2, 85.609375f), gd = Layout(d, 269, 0), gr = Layout(Describe(V(Phase::Ready)), 269, 2);
    CHECK(Near(ga.height, Paper::available) && Near(gd.height, Paper::downloading) && Near(gr.height, Paper::ready));
    CHECK(Near(ga.icon.left, 108) && Near(ga.icon.top, 21) && Near(ga.icon.Width(), 53));
    CHECK(Near(ga.title.top, 87) && Near(ga.body.top, 109) && Near(ga.body.Height(), 36));
    CHECK(Near(ga.buttons[0].left, 17) && Near(ga.buttons[0].top, 161) && Near(ga.buttons[1].right, 252) && Near(ga.buttons[1].Width(), 85.609375f));
    CHECK(Near(ga.buttons[1].left - ga.buttons[0].right, 8) && Near(ga.buttons[0].Width(), 141.390625f));
    CHECK(Near(gd.stats.top, 109) && Near(gd.buttons[0].top, 139) && Near(gd.buttons[0].Width(), 235));
    // Bottom padding: 16 - 0.40625 (Paper's text trim) on every state with buttons.
    CHECK(Near(ga.height - ga.buttons[0].bottom, 1 + 16 - Paper::textTrim) && Near(gd.height - gd.buttons[0].bottom, 1 + 16 - Paper::textTrim));
    CHECK(Near(ContentWidth(269), 235));
    CHECK(FitWidth(1180, 14, 0) == 269 && FitWidth(260, 14, 0) == 232 && FitWidth(250, 14, 0) == 0 && FitWidth(600, 14, 300) == 269 && FitWidth(520, 14, 270) == 0);
    CHECK(FitsHeight(815, 84, Paper::available) && !FitsHeight(300, 84, Paper::available));
}

// « Plus tard » (Val, choice B): hidden for this session for that version; a newer version or any other view passes.
static void LaterTests() {
    CHECK(CardView(V(Phase::Available, 0, 0, -1, L"", L"0.3.2"), L"0.3.2").phase == Phase::Hidden);
    CHECK(CardView(V(Phase::Available, 0, 0, -1, L"", L"0.3.2"), L"0.3.2").phase == Phase::Hidden);   // re-published: still hidden
    CHECK(CardView(V(Phase::Available, 0, 0, -1, L"", L"0.3.3"), L"0.3.2").phase == Phase::Available); // a newer version
    CHECK(CardView(V(Phase::Available, 0, 0, -1, L"", L"0.3.2"), L"").phase == Phase::Available);      // forgotten (manual check, new start)
    CHECK(CardView(V(Phase::Downloading, 1, 2, -1, L"", L"0.3.2"), L"0.3.2").phase == Phase::Downloading);
    CHECK(CardView(V(Phase::Ready, 0, 0, -1, L"", L"0.3.2"), L"0.3.2").phase == Phase::Ready);
}

// The background follows the profile (Val, 2026-11-26): violet exactly Val's, grey neutral, the rest kept in sRGB
// with the board's lightness and hue offsets (chroma only reduced).
static void ProfileColorTests() {
    const auto violet = Grain::Colors(Grain::VioletInk);
    CHECK(RgbOf(violet[0]) == 0x8A66FF && RgbOf(violet[1]) == 0xECAEFF && RgbOf(violet[2]) == 0xF4F4FF);
    // The formula itself (not the shortcut) also gives Val's colours from the violet ink: one unit off at most.
    {
        const Grain::Lch v = Grain::ToLch(Grain::VioletInk);
        for (int i = 0; i < 3; ++i) {
            const Grain::Lch b = Grain::ToLch(Grain::Board[i]);
            const auto o = ProfileColor::Detail::Pack(ProfileColor::Detail::FromOklch(b.L, ProfileColor::Detail::FitChroma(b.L, b.C * (v.C / v.C), b.h), b.h));
            CHECK(std::abs(o.r - Grain::Board[i].r) <= 1 && std::abs(o.g - Grain::Board[i].g) <= 1 && std::abs(o.b - Grain::Board[i].b) <= 1);
        }
    }
    const Grain::Lch v = Grain::ToLch(Grain::VioletInk);
    for (const auto& p : Profiles6) {
        const auto c = Grain::Colors(p.ink);
        std::printf("  profile %-6s ink #%06lX -> #%06lX #%06lX #%06lX\n", p.name, RgbOf(p.ink), RgbOf(c[0]), RgbOf(c[1]), RgbOf(c[2]));
        const Grain::Lch q = Grain::ToLch(p.ink);
        for (int i = 0; i < 3; ++i) {
            const Grain::Lch b = Grain::ToLch(Grain::Board[i]), o = Grain::ToLch(c[i]);
            CHECK(std::fabs(o.L - b.L) < .01);                                     // the board's lightness
            if (q.C < Grain::GreyChroma) { CHECK(c[i].r == c[i].g && c[i].g == c[i].b); continue; }   // grey profile: neutral greys
            CHECK(o.C <= b.C * q.C / v.C + .004);                                  // chroma only ever reduced (into sRGB)
            if (o.C > .03) { double d = std::fabs(std::fmod(o.h - (b.h + q.h - v.h) + 720, 360)); if (d > 180) d = 360 - d; CHECK(d < 2.5); }   // the hue offset kept
        }
    }
    CHECK(RgbOf(Grain::Colors({0xB3, 0xB3, 0xB3})[0]) == 0x888888 && RgbOf(Grain::Colors({0xB3, 0xB3, 0xB3})[2]) == 0xF5F5F5);
}

static Presenter Fresh(bool reduced = false) { Presenter p; p.SetReduced(reduced, T0()); return p; }
static void Show(Presenter& p, const View& v, M::Time at, float secondary = 85.609375f) {
    auto c = Describe(v); p.Set(c, Layout(c, 269, c.body.empty() ? 0 : 2, secondary), at);
}
static Frame Step(Presenter& p, M::Time at) { p.Settle(at); return p.Sample(at); }

static void MotionTests() {
    // Entrance: rises 8 DIP and fades in on the 1.0 / 0.3 s spring, no overshoot.
    {
        Presenter p = Fresh();
        CHECK(!p.Moving(T0()) && !p.Sample(T0()).visible);
        Show(p, V(Phase::Available), T0());
        auto f0 = p.Sample(T0());
        CHECK(f0.visible && Near(f0.opacity, 0) && Near(f0.offset, Timing::enterOffset) && Near(f0.height, Paper::available));
        CHECK(f0.icon.mode == IconFrame::Mode::Static && f0.icon.glyph == Glyph::Tray && Near(f0.secondary, 1) && Near(f0.actions, 1));
        float lastOpacity = -1, lastOffset = 100; bool monotonic = true;
        for (int ms = 0; ms <= 800; ++ms) {
            auto f = p.Sample(At(ms));
            if (f.opacity < lastOpacity - 1e-6f || f.offset > lastOffset + 1e-6f || f.offset < -1e-6f || f.opacity > 1) monotonic = false;
            lastOpacity = f.opacity; lastOffset = f.offset;
        }
        CHECK(monotonic && p.Sample(At(450)).opacity > .99f && p.Sample(At(450)).offset < .1f);
        CHECK(p.Moving(At(200)));
        p.Settle(At(800));
        CHECK(!p.Moving(At(800)) && !p.Moving(At(100000)));   // idle: zero frame requests
    }
    // Disponible -> En cours (Télécharger): texts cross-fade in 120 ms, « Plus tard » folds, the card shrinks 209.6 -> 187.6
    // anchored at the bottom, the fade moves to -2 / -100, and the icon IS the drawer's: its start morph from the tray.
    {
        Presenter p = Fresh();
        Show(p, V(Phase::Available), T0()); p.Settle(At(1000));
        const unsigned gen = p.Generation();
        Show(p, V(Phase::Downloading, 0, PaperTotal, -1), At(1000));
        CHECK(p.Generation() != gen);
        auto s0 = Step(p, At(1000));
        CHECK(s0.layers[0].content && s0.layers[0].content->kind == Kind::Available && Near(s0.layers[0].opacity, 1) && Near(s0.layers[1].opacity, 0));
        CHECK(s0.icon.mode == IconFrame::Mode::Drawer && s0.icon.pose.ring == 0 && s0.icon.pose.arrow == 0);   // the tray itself
        CHECK(Near(s0.icon.opacity, 1) && s0.ghost.opacity == 0);                                             // no cross-fade
        bool clean = true, folds = true, shrinks = true; float lastSecondary = 2, lastHeight = 1000;
        for (int ms = 0; ms <= 900; ++ms) {
            auto f = p.Sample(At(1000 + ms));
            if (f.layers[0].content && std::min(f.layers[0].opacity, f.layers[1].opacity) > .3f) clean = false;   // never two legible texts
            if (f.secondary > lastSecondary + 1e-6f) folds = false;
            if (f.height > lastHeight + 1e-4f || f.height < Paper::downloading - 1e-3f) shrinks = false;
            lastSecondary = f.secondary; lastHeight = f.height;
        }
        CHECK(clean && folds && shrinks);
        CHECK(!p.Sample(At(1121)).layers[0].content || p.Sample(At(1121)).layers[0].opacity < .01f);
        auto s120 = Step(p, At(1240));
        CHECK(s120.icon.mode == IconFrame::Mode::Drawer && s120.icon.pose.ring >= DI::Morph);   // ring formed after 233 ms
        auto s600 = Step(p, At(1600));
        CHECK(std::fabs(s600.height - Paper::downloading) < .3f && s600.secondary < .02f && Near(s600.fadeTop, -100, .2) && Near(s600.fadeLeft, -2, .01));
        // Progress feeds the ring: 14 + round(fraction × 40), the drawer's 40 steps; the arrow keeps bobbing.
        Show(p, V(Phase::Downloading, PaperReceived, PaperTotal, 14), At(2000));
        auto s = Step(p, At(2000));
        CHECK(s.icon.pose.ring == DI::Morph + 10 && Near(s.icon.dim, 1) && s.layers[1].content->size == L"50.4/200.0MB");
        CHECK(p.Moving(At(2000)));   // the arrow's loop needs frames while downloading
    }
    // En cours -> Prête: the ripple at once, the card waits for the arrow to come back to rest (loop boundary), then
    // texts, button and height change while the arrow morphs into the check; the check then grows to Val's (450 ms).
    {
        Presenter p = Fresh();
        Show(p, V(Phase::Downloading, PaperReceived, PaperTotal, 14), T0());
        Step(p, At(2000));
        Show(p, V(Phase::Ready, PaperTotal, PaperTotal), At(2000));
        auto r0 = Step(p, At(2000));
        CHECK(p.PendingReady() && r0.layers[1].content->kind == Kind::Downloading && r0.ripple >= 0 && r0.ripple < .05f);
        M::Time switched{};
        for (int ms = 0; ms <= 1400; ms += 1) {
            Step(p, At(2000 + ms));
            if (!p.PendingReady()) { switched = At(2000 + ms); break; }
        }
        CHECK(switched != M::Time{});
        const double waited = std::chrono::duration<double, std::milli>(switched - At(2000)).count();
        CHECK(waited >= 0 && waited <= 1300 + 17);   // at most one 1.3 s loop
        auto r1 = p.Sample(switched);
        CHECK(r1.layers[1].content->kind == Kind::Ready && r1.icon.mode == IconFrame::Mode::Drawer && r1.icon.pose.success >= 0 && r1.icon.pose.success <= 1);
        const double base = std::chrono::duration<double, std::milli>(switched - T0()).count();
        auto r2 = Step(p, At(base + 405));
        CHECK(r2.icon.mode == IconFrame::Mode::Check && r2.icon.settle < .2f);
        auto r3 = Step(p, At(base + 400 + 225));
        CHECK(r3.icon.mode == IconFrame::Mode::Check && r3.icon.settle > .3f && r3.icon.settle < .9f);
        auto r4 = Step(p, At(base + 400 + 500));
        CHECK(r4.icon.mode == IconFrame::Mode::Static && r4.icon.glyph == Glyph::Check);   // Val's check-circle, exact path
        CHECK(std::fabs(r4.height - Paper::ready) < .3f && r4.secondary < .01f && r4.ripple < 0);
        p.Settle(At(base + 2000));
        CHECK(!p.Moving(At(base + 2000)));   // the 3 s hold / morph back of the toolbar never runs on the card
        CHECK(p.Sample(At(base + 6000)).icon.glyph == Glyph::Check);
    }
    // Annuler: texts and buttons at once; the icon finishes its loop, then the ring unrolls into the tray.
    {
        Presenter p = Fresh();
        Show(p, V(Phase::Downloading, PaperReceived, PaperTotal, 14), T0());
        Step(p, At(1000));
        Show(p, V(Phase::Available), At(1000));
        auto c0 = Step(p, At(1000));
        CHECK(c0.layers[1].content->kind == Kind::Available && c0.icon.mode == IconFrame::Mode::Drawer);
        bool tray = false;
        for (int ms = 0; ms <= 2000 && !tray; ms += 5) { auto f = Step(p, At(1000 + ms)); tray = f.icon.mode == IconFrame::Mode::Static && f.icon.glyph == Glyph::Tray; }
        CHECK(tray);
        auto c1 = Step(p, At(4000));
        CHECK(Near(c1.secondary, 1, .01) && std::fabs(c1.height - Paper::available) < .3f && !p.Moving(At(4000)));
    }
    // Interruption: Télécharger then Annuler 100 ms later — continuous from what is displayed, no jump.
    {
        Presenter p = Fresh();
        Show(p, V(Phase::Available), T0()); p.Settle(At(1000));
        Show(p, V(Phase::Downloading, 0, PaperTotal), At(1000));
        const float hMid = Step(p, At(1100)).height, sMid = p.Sample(At(1100)).secondary;
        Show(p, V(Phase::Available), At(1100));
        CHECK(Near(p.Sample(At(1100)).height, hMid, 1e-3) && Near(p.Sample(At(1100)).secondary, sMid, 1e-3));
        CHECK(std::fabs(p.Sample(At(1101)).height - hMid) < .5f);
        bool bounded = true;
        for (int ms = 0; ms <= 800; ++ms) { const float h = p.Sample(At(1100 + ms)).height; if (h > Paper::available + 1e-3f || h < Paper::downloading - 1e-3f) bounded = false; }
        CHECK(bounded);
    }
    // Errors, installing, installed: no board — cross-fades, the error's two buttons, no row for the other two.
    {
        Presenter p = Fresh();
        Show(p, V(Phase::Downloading, PaperReceived, PaperTotal, 14), T0()); Step(p, At(1000));
        Show(p, V(Phase::Error, 0, 0, -1, L"Réseau indisponible"), At(1000));
        auto e0 = Step(p, At(1000));
        CHECK(e0.ghost.opacity > .9f && e0.ghost.mode == IconFrame::Mode::Drawer && Near(e0.icon.opacity, 0) && e0.icon.glyph == Glyph::Info);
        auto e1 = Step(p, At(1500));
        CHECK(e1.secondary > .99f && e1.ghost.opacity == 0 && Near(e1.icon.opacity, 1));
        Show(p, V(Phase::Installed), At(2000));
        auto i1 = Step(p, At(2600));
        CHECK(i1.actions < .01f && i1.icon.glyph == Glyph::Check);
    }
    // Exit: a 160 ms fade, no movement; not interactive.
    {
        Presenter p = Fresh();
        Show(p, V(Phase::Ready), T0()); p.Settle(At(1000));
        p.Set({}, {}, At(1000));
        CHECK(!p.Interactive() && p.Sample(At(1000)).visible);
        auto x80 = p.Sample(At(1080));
        CHECK(x80.opacity < .6f && x80.opacity > 0 && Near(x80.offset, 0));
        CHECK(!p.Sample(At(1160)).visible);
    }
    // Reduced motion: 120 ms cross-fades only — no entrance rise, no size or fold animation, no morph, no ripple.
    {
        Presenter p = Fresh(true);
        Show(p, V(Phase::Available), T0());
        bool still = true;
        for (int ms = 0; ms <= 400; ms += 5) if (!Near(p.Sample(At(ms)).offset, 0)) still = false;
        CHECK(still && p.Sample(At(60)).opacity > 0 && p.Sample(At(60)).opacity < 1 && Near(p.Sample(At(120)).opacity, 1));
        p.Settle(At(300));
        Show(p, V(Phase::Downloading, PaperReceived, PaperTotal, 14), At(400));
        auto d0 = Step(p, At(400));
        CHECK(Near(d0.height, Paper::downloading) && Near(d0.secondary, 0) && d0.ghost.opacity > .9f);
        auto d1 = Step(p, At(530));
        CHECK(d1.icon.mode == IconFrame::Mode::Drawer && d1.icon.pose.ring == DI::Morph + 10 && d1.icon.pose.arrow == DI::Morph && Near(d1.icon.opacity, 1));
        CHECK(!p.Moving(At(1000)));   // static ring (repainted on progress only)
        Show(p, V(Phase::Ready), At(1000));
        auto r0 = Step(p, At(1000));
        CHECK(!p.PendingReady() && r0.layers[1].content->kind == Kind::Ready && r0.ripple < 0 && r0.ghost.opacity > .9f);
        auto r1 = Step(p, At(1200));
        CHECK(r1.icon.glyph == Glyph::Check && r1.icon.mode == IconFrame::Mode::Static && Near(r1.height, Paper::ready));
        p.Settle(At(1300)); p.Set({}, {}, At(1300));
        CHECK(Near(p.Sample(At(1380)).offset, 0) && !p.Sample(At(1460)).visible);
    }
    // Channel: retarget carries velocity yet never crosses the target.
    {
        Channel c; c.Snap(0); c.Spring(100, T0(), .3);
        const float v = c.Velocity(At(40));
        c.Spring(60, At(40), .3);
        CHECK(Near(c.Velocity(At(40)), v, 1e-2));
        bool noCross = true; for (int ms = 40; ms < 800; ++ms) if (c.Value(At(ms)) > 60 + 1e-3f && c.Value(At(40)) <= 60) noCross = false;
        CHECK(noCross);
    }
    // White icon composition: the track at half the ink while the fill means progress, everything else at the ink.
    {
        const uint32_t on = 0xFF000000u, off = 0;
        std::vector<uint32_t> out;
        ComposeWhite(&on, &off, &off, &off, 1, out, .85f, 1);   // bare track, dimmed
        CHECK(std::abs((int)(out[0] >> 24) - (int)std::lround(.85f * .5f * 255)) <= 1);
        ComposeWhite(&on, &on, &off, &off, 1, out, .85f, 1);    // filled
        CHECK(std::abs((int)(out[0] >> 24) - (int)std::lround(.85f * 255)) <= 1);
        ComposeWhite(&on, &on, &on, &off, 1, out, .85f, 1);     // cut out
        CHECK((out[0] >> 24) == 0);
        ComposeWhite(&off, &off, &on, &on, 1, out, .85f, 0);    // the arrow over its own band
        CHECK(std::abs((int)(out[0] >> 24) - (int)std::lround(.85f * 255)) <= 1 && (out[0] & 0xFF) == (out[0] >> 24));
        CHECK(Near(TrackDim(0), 0) && Near(TrackDim(DI::Morph), 1) && Near(TrackDim(DI::FillEnd), 1) && Near(TrackDim(DI::RingDone), 0, 1e-3));
    }
}

static void ClickAndDwellTests() {
    auto a = Describe(V(Phase::Available)); auto d = Describe(V(Phase::Downloading, 1, 2));
    Clicks c;
    c.Down(0, 1);
    CHECK(c.Up(0, 2, d) == std::nullopt);        // phase changed under the finger: no Cancel
    c.Down(0, 1);
    CHECK(c.Up(-1, 1, a) == std::nullopt);       // released outside
    c.Down(0, 1);
    CHECK(c.Up(0, 1, a) == Action::Download);
    c.Down(0, 1);
    CHECK(c.Up(0, 1, a) == std::nullopt);        // repeat ignored for the same generation
    c.Down(1, 3);
    CHECK(c.Up(1, 3, a) == Action::Later);
    CHECK(c.Activate(0, 2, d) == Action::Cancel);
    Dwell w; w.Start(T0());
    CHECK(!w.Expired(At(5999)) && w.Expired(At(6000)));
    w.Hold(true, At(5000));
    CHECK(!w.Expired(At(9000)) && !w.Deadline());
    w.Hold(false, At(9000));
    CHECK(!w.Expired(At(10999)) && w.Expired(At(11000)));
}

// ---- Direct2D / DirectWrite --------------------------------------------------------
static std::wstring repo, snapshots;
static IWICImagingFactory* wic = nullptr;
static Onboarding::Shaders::Renderer gpu;

struct Canvas {
    IWICBitmap* bitmap = nullptr; ID2D1RenderTarget* rt = nullptr; UINT w = 0, h = 0; float dpi = 96;
    bool Create(Painter& painter, float wDip, float hDip, float dpiValue) {
        dpi = dpiValue; w = (UINT)std::lround(wDip * dpi / 96); h = (UINT)std::lround(hDip * dpi / 96);
        if (FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap))) return false;
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
        if (FAILED(painter.factory->CreateWicBitmapRenderTarget(bitmap, props, &rt))) return false;
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return SUCCEEDED(painter.Bind(rt));
    }
    ~Canvas() { Drop(rt); Drop(bitmap); }
    std::vector<BYTE> Pixels() {
        std::vector<BYTE> px(w * h * 4);
        WICRect r{0, 0, (INT)w, (INT)h};
        bitmap->CopyPixels(&r, w * 4, (UINT)px.size(), px.data());
        return px;
    }
    bool Save(const std::wstring& name) {
        IWICStream* stream = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* frame = nullptr;
        HRESULT hr = wic->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename((snapshots + L"\\" + name).c_str(), GENERIC_WRITE);
        if (SUCCEEDED(hr)) hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
        if (SUCCEEDED(hr)) hr = enc->Initialize(stream, WICBitmapEncoderNoCache);
        if (SUCCEEDED(hr)) hr = enc->CreateNewFrame(&frame, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Initialize(nullptr);
        if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
        if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap, nullptr);
        if (SUCCEEDED(hr)) hr = frame->Commit();
        if (SUCCEEDED(hr)) hr = enc->Commit();
        Drop(frame); Drop(enc); Drop(stream);
        return SUCCEEDED(hr);
    }
};

// The card's real background for a canvas: the GrainGradient at `seconds` from Paper's frame, as the popup draws it.
static ID2D1Bitmap* GrainBitmap(Canvas& k, Onboarding::Shaders::Layer& layer, float width, double seconds = 0, ProfileColor::Rgb8 ink = Grain::VioletInk) {
    if (!gpu.Ready()) return nullptr;
    const unsigned ow = (unsigned)std::lround((width - 1) * k.dpi / 96), oh = (unsigned)std::lround(Paper::grainHeight * k.dpi / 96);
    const auto c = Grain::Colors(ink); const unsigned long rgb[3] = {RgbOf(c[0]), RgbOf(c[1]), RgbOf(c[2])};
    if (!gpu.Render(Onboarding::Shaders::Kind::GrainGradient, Onboarding::Shaders::UpdateCardGrain(seconds, rgb), width - 1, Paper::grainHeight, ow, oh, layer.frame)) return nullptr;
    layer.Drop();
    return layer.Upload(k.rt) ? layer.bitmap : nullptr;
}
static Frame Settled(Painter& painter, const View& v, float width, bool reduced = false) {
    static Presenter pr; pr = Presenter(); pr.SetReduced(reduced, T0());
    auto c = Describe(v); pr.Set(c, painter.Measure(c, width), T0()); pr.Settle(At(3000));
    return pr.Sample(At(3000));
}
// En cours as the preview shows it (Paper's 50,4 Mo, the drawer's arrow at its loop start).
static Frame PaperDownloading(Painter& painter, float width) {
    Frame f = Settled(painter, V(Phase::Downloading, PaperReceived, PaperTotal, 14), width);
    f.icon.mode = IconFrame::Mode::Drawer; f.icon.pose = DI::Pose{}; f.icon.pose.idle = false;
    f.icon.pose.ring = DI::Morph + 10; f.icon.pose.arrow = DI::Morph; f.icon.dim = 1; f.icon.opacity = 1; f.ripple = -1;
    return f;
}
static void PaintAt(Painter& p, const Frame& f, float x, float bottom, float width, const ButtonVisual vis[2], bool reduced, ID2D1Bitmap* grain) {
    D2D1_MATRIX_3X2_F old; p.target->GetTransform(&old);
    p.target->SetTransform(D2D1::Matrix3x2F::Translation(x, bottom - f.height + f.offset) * old);
    p.Paint(f, width, f.height, vis, reduced, grain);
    p.target->SetTransform(old);
}

static void PainterTests(Painter& painter) {
    CHECK(painter.drawerReady);   // the drawer's six Lottie files load at the card's size
    // Balanced wrapping reproduces Paper's line breaks: 2 lines at 235 DIP.
    for (auto locale : {I18n::Locale::Fr, I18n::Locale::En}) {
        using I18n::Id;
        for (Id id : {Id::update_toast_available_body, Id::update_toast_ready_body}) {
            int lines = 0; const float w = painter.BalancedWidth(I18n::In(locale, id), 235, &lines);
            CHECK(locale == I18n::Locale::Fr ? lines == 2 && w < 235 : lines <= 2);   // Paper's two lines are French
        }
        // Every title and label fits on one line down to the minimum card width.
        for (Id id : {Id::update_toast_available, Id::update_toast_downloading, Id::update_toast_ready, Id::update_toast_installed, Id::update_toast_installing, Id::update_toast_failed}) {
            const float w = painter.NaturalWidth(Role::Title, I18n::In(locale, id));
            if (w > ContentWidth(MinWidth)) std::fprintf(stderr, "  title too wide: %.1f > %.1f DIP (id %d)\n", w, ContentWidth(MinWidth), (int)id);
            CHECK(w <= ContentWidth(MinWidth));
        }
        CHECK(painter.NaturalWidth(Role::Button, I18n::In(locale, Id::update_toast_restart_install)) <= ContentWidth(MinWidth) - 2 * Paper::primaryPadX);
        const float later = painter.NaturalWidth(Role::Button, I18n::In(locale, Id::common_later)) + 32;
        CHECK(painter.NaturalWidth(Role::Button, I18n::In(locale, Id::updates_download)) + 16 <= ContentWidth(MinWidth) - 8 - later);
    }
    // Measured heights equal Paper's (Val's French texts); « Plus tard » measures Paper's 85.609 within half a DIP.
    I18n::SetForProcess(I18n::Locale::Fr);
    CHECK(Near(painter.Measure(Describe(V(Phase::Available)), 269).height, Paper::available));
    CHECK(Near(painter.Measure(Describe(V(Phase::Ready)), 269).height, Paper::ready));
    CHECK(Near(painter.Measure(Describe(V(Phase::Downloading, 1, 2, 3)), 269).height, Paper::downloading));
    const auto ga = painter.Measure(Describe(V(Phase::Available)), 269);
    CHECK(ga.buttons.size() == 2 && std::fabs(ga.buttons[1].Width() - 85.609375f) < .5f);
    I18n::SetForProcess(I18n::Locale::En);
    for (int i = 0; i < 200; ++i) painter.Text(Role::Body, std::to_wstring(i), 100);
    CHECK(painter.cache.size() <= Painter::CacheLimit);
}

static void SnapshotTests(Painter& painter) {
    const ButtonVisual rest[2]{};
    // Card-only renders for compare.py (the validated preview's references at 100 % and 150 %), and per-state checks.
    struct Case { const wchar_t* name; Frame (*make)(Painter&); };
    const Case cards[] = {
        {L"A", [](Painter& p) { return Settled(p, V(Phase::Available), 269); }},
        {L"B", [](Painter& p) { return PaperDownloading(p, 269); }},
        {L"C", [](Painter& p) { return Settled(p, V(Phase::Ready), 269); }},
    };
    for (float dpi : {96.f, 144.f}) for (const Case& c : cards) {
        const Frame f = c.make(painter);
        // As the popup: its height is whole device pixels and the card fills it (src/update-toast.cpp Present).
        const float k = dpi / 96, h = std::lround(f.height * k) / k;
        Canvas t; CHECK(t.Create(painter, 269, h, dpi));
        Onboarding::Shaders::Layer layer; ID2D1Bitmap* grain = GrainBitmap(t, layer, 269);
        t.rt->BeginDraw(); t.rt->Clear(D2D1::ColorF(0, 0, 0, 0)); painter.Paint(f, 269, h, rest, false, grain); CHECK(SUCCEEDED(t.rt->EndDraw()));
        auto px = t.Pixels();
        CHECK(px[3] == 0 && px[(t.w - 1) * 4 + 3] == 0 && px[((t.h - 1) * t.w) * 4 + 3] == 0);   // rounded corners: click-through
        CHECK(px[((t.h / 2) * t.w + t.w / 2) * 4 + 3] == 255);
        wchar_t name[64]; std::swprintf(name, 64, L"native-%ls-%d.png", c.name, (int)std::lround(dpi / 96 * 100)); CHECK(t.Save(name));
        layer.Drop(); painter.Unbind();
    }
    // Every state at 96 / 120 / 144 / 192 DPI on the app's canvas: bright pixels stay inside the card.
    struct State { const wchar_t* name; View view; };
    const State states[] = {
        {L"available", V(Phase::Available)}, {L"downloading", V(Phase::Downloading, PaperReceived, PaperTotal, 14)}, {L"ready", V(Phase::Ready)},
        {L"installed", V(Phase::Installed)}, {L"installing", V(Phase::Installing)},
        {L"error", V(Phase::Error, 0, 0, -1, L"Impossible de vérifier le paquet de mise à jour.")},
        {L"downloading-unknown-total", V(Phase::Downloading, 3 * 1048576 + 300000, 0, -1)},
    };
    for (float dpi : {96.f, 120.f, 144.f, 192.f}) for (const State& s : states) {
        const Frame f = Settled(painter, s.view, 269);
        Canvas k; const float W = 297, H = f.height + 28;
        if (!k.Create(painter, W, H, dpi)) { CHECK(false); continue; }
        Onboarding::Shaders::Layer layer; ID2D1Bitmap* grain = GrainBitmap(k, layer, 269);
        k.rt->BeginDraw(); k.rt->Clear(D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1));
        PaintAt(painter, f, 14, H - 14, 269, rest, false, grain);
        CHECK(SUCCEEDED(k.rt->EndDraw()));
        auto px = k.Pixels(); const int s14 = (int)std::lround(14 * dpi / 96) - 1; int outside = 0;
        for (UINT y = 0; y < k.h; ++y) for (UINT x = 0; x < k.w; ++x)
            if ((int)x < s14 || (int)y < s14 || (int)x >= (int)k.w - s14 || (int)y >= (int)k.h - s14) if (px[(y * k.w + x) * 4 + 1] > 40) ++outside;
        CHECK(outside == 0);
        if (dpi == 96 || dpi == 144) { wchar_t name[96]; std::swprintf(name, 96, L"%ls-%d.png", s.name, (int)dpi); CHECK(k.Save(name)); }
        layer.Drop(); painter.Unbind();
    }
    // Transition strips (real presenter frames, the real icon frames), 144 DPI: 8 frames each.
    auto strip = [&](const wchar_t* name, bool reduced, const View& from, const View& to, const double* times, double settleFrom, const View* after = nullptr, double afterAt = 0) {
        Canvas k; const float W = 14 + 8 * 283.f, H = 209.6f + 44;
        if (!k.Create(painter, W, H, 144)) { CHECK(false); return; }
        Onboarding::Shaders::Layer layer; ID2D1Bitmap* grain = GrainBitmap(k, layer, 269);
        k.rt->BeginDraw(); k.rt->Clear(D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1));
        Presenter pr = Fresh(reduced);
        auto c0 = Describe(from); pr.Set(c0, painter.Measure(c0, 269), T0()); pr.Settle(At(settleFrom));
        auto c1 = Describe(to); pr.Set(c1, painter.Measure(c1, 269), At(settleFrom));
        bool afterDone = false; double clock = settleFrom;   // one monotonic clock, settled every 8 ms like frames
        auto advance = [&](double to) { while (clock + 8 < to) { clock += 8; pr.Settle(At(clock)); } clock = to; pr.Settle(At(clock)); };
        for (int i = 0; i < 8; ++i) {
            const double t = settleFrom + times[i];
            if (after && !afterDone && times[i] >= afterAt) { advance(settleFrom + afterAt); auto c2 = Describe(*after); pr.Set(c2, painter.Measure(c2, 269), At(clock)); afterDone = true; }
            advance(t);
            const Frame f = pr.Sample(At(t));
            PaintAt(painter, f, 14 + i * 283.f, H - 14, 269, rest, reduced, grain);
        }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(name));
        layer.Drop(); painter.Unbind();
    };
    const double ab[] = {0, 40, 80, 120, 180, 240, 330, 500};
    strip(L"transition-disponible-en-cours-144.png", false, V(Phase::Available), V(Phase::Downloading, 0, PaperTotal), ab, 1000);
    const View ready = V(Phase::Ready, PaperTotal, PaperTotal);
    const double bc[] = {0, 300, 700, 1000, 1150, 1300, 1500, 1900};
    strip(L"transition-en-cours-prete-144.png", false, V(Phase::Downloading, PaperReceived, PaperTotal, 14), V(Phase::Downloading, 199 * 1048576ull, PaperTotal, 1), bc, 1000, &ready, 100);
    const double ba[] = {0, 60, 120, 400, 800, 1100, 1300, 1600};
    strip(L"transition-annuler-144.png", false, V(Phase::Downloading, PaperReceived, PaperTotal, 14), V(Phase::Available), ba, 1000);
    const double rm[] = {0, 30, 60, 90, 120, 200, 300, 500};
    strip(L"transition-reduite-144.png", true, V(Phase::Available), V(Phase::Downloading, PaperReceived, PaperTotal, 14), rm, 1000);
    const double en[] = {0, 40, 80, 120, 160, 220, 300, 450};
    {
        Canvas k; const float W = 14 + 8 * 283.f, H = 209.6f + 44;
        CHECK(k.Create(painter, W, H, 144));
        Onboarding::Shaders::Layer layer; ID2D1Bitmap* grain = GrainBitmap(k, layer, 269);
        k.rt->BeginDraw(); k.rt->Clear(D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1));
        Presenter pr = Fresh(); auto a = Describe(V(Phase::Available)); pr.Set(a, painter.Measure(a, 269), T0());
        for (int i = 0; i < 8; ++i) { Frame f = pr.Sample(At(en[i])); PaintAt(painter, f, 14 + i * 283.f, H - 14, 269, rest, false, grain); }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"apparition-144.png"));
        layer.Drop(); painter.Unbind();
    }
    // The six profiles side by side (state A), 144 DPI: the background follows each profile ink.
    {
        Canvas k; const float W = 14 + 6 * 283.f, H = 209.6f + 28;
        CHECK(k.Create(painter, W, H, 144));
        k.rt->BeginDraw(); k.rt->Clear(D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1));
        for (int i = 0; i < 6; ++i) {
            Onboarding::Shaders::Layer layer; ID2D1Bitmap* grain = GrainBitmap(k, layer, 269, 0, Profiles6[i].ink);
            Frame f = Settled(painter, V(Phase::Available), 269); PaintAt(painter, f, 14 + i * 283.f, H - 14, 269, rest, false, grain);
            layer.Drop();
        }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"profils-144.png"));
        painter.Unbind();
    }
    // Button states on the primary: hover, pressed (0.96), keyboard focus; and « Plus tard » hovered.
    {
        Canvas k; const float W = 14 + 4 * 283.f, H = 209.6f + 28;
        CHECK(k.Create(painter, W, H, 192));
        Onboarding::Shaders::Layer layer; ID2D1Bitmap* grain = GrainBitmap(k, layer, 269);
        k.rt->BeginDraw(); k.rt->Clear(D2D1::ColorF(Paper::surface, Paper::surface, Paper::surface, 1));
        ButtonVisual states[4][2] = {{{1, 0, false, false}, {}}, {{1, 1, false, false}, {}}, {{0, 0, true, true}, {}}, {{}, {1, 0, false, false}}};
        for (int i = 0; i < 4; ++i) { Frame f = Settled(painter, V(Phase::Available), 269); PaintAt(painter, f, 14 + i * 283.f, H - 14, 269, states[i], false, grain); }
        CHECK(SUCCEEDED(k.rt->EndDraw())); CHECK(k.Save(L"button-states-192.png"));
        layer.Drop(); painter.Unbind();
    }
    // Narrow card at 200 % and 250 %: nothing past the padding.
    for (float dpi : {192.f, 240.f}) {
        const Frame f = Settled(painter, V(Phase::Error, 0, 0, -1, L"Le serveur de mises à jour est injoignable."), MinWidth);
        Canvas t; CHECK(t.Create(painter, MinWidth, f.height, dpi));
        t.rt->BeginDraw(); t.rt->Clear(D2D1::ColorF(0, 0, 0, 0)); painter.Paint(f, MinWidth, f.height, rest, false, nullptr); CHECK(SUCCEEDED(t.rt->EndDraw()));
        wchar_t name[64]; std::swprintf(name, 64, L"narrow-error-%d.png", (int)dpi); CHECK(t.Save(name));
        painter.Unbind();
    }
}

// ---- Popup window behaviour (run on a desktop that is never displayed: run.py --desktop) -----------------------
static LRESULT CALLBACK OwnerProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static void Pump(int ms) {
    const DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((int)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}

static void WindowTests() {
    WNDCLASSW wc{}; wc.lpfnWndProc = OwnerProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"ToastTestOwner";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); RegisterClassW(&wc);
    const UINT sys = GetDpiForSystem();
    HWND owner = CreateWindowExW(0, wc.lpszClassName, L"toast test owner (native, mock)", WS_OVERLAPPEDWINDOW,
                                 40, 40, MulDiv(900, sys, 96), MulDiv(640, sys, 96), nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(owner != nullptr); if (!owner) return;
    ShowWindow(owner, SW_SHOWNORMAL); UpdateWindow(owner); SetForegroundWindow(owner); SetFocus(owner);
    Pump(100);
    HWND foreground = GetForegroundWindow();
    std::vector<Action> actions; int releases = 0;
    Toast toast;
    ToastOptions opt; opt.forceReducedMotion = 0;
    CHECK(toast.Init(owner, repo + L"\\assets", [&](Action a) { actions.push_back(a); }, opt));
    toast.SetFocusReleaseHandler([&] { ++releases; SetActiveWindow(owner); SetFocus(owner); });
    HWND popup = toast.Window();
    CHECK(popup && GetWindow(popup, GW_OWNER) == owner);
    const LONG_PTR ex = GetWindowLongPtrW(popup, GWL_EXSTYLE);
    CHECK(!(ex & WS_EX_LAYERED) && (ex & WS_EX_NOACTIVATE) && (ex & WS_EX_TOOLWINDOW) && !(ex & WS_EX_TOPMOST));
    CHECK(!IsWindowVisible(popup) && !toast.Shown());
    auto* impl = reinterpret_cast<Toast::Impl*>(GetWindowLongPtrW(popup, GWLP_USERDATA));
    // Nothing exists for the background until the card shows.
    CHECK(!impl->gpu.Ready() && !impl->grainTimer && impl->grainRenders == 0);
    const auto statsBefore = impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0;
    toast.SetView(V(Phase::Available));
    Pump(700);
    CHECK(toast.Shown() && IsWindowVisible(popup));
    CHECK(GetForegroundWindow() == foreground && GetFocus() == owner); // never steals focus
    CHECK(impl->grainRenders >= 1);   // the real GrainGradient (or its still frame without a GPU)
    // Bottom-left anchoring against the owner client, Paper's size.
    RECT client{}; GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, (POINT*)&client, 2);
    RECT pr{}; GetWindowRect(popup, &pr);
    const float s = GetDpiForWindow(owner) / 96.f;
    CHECK(pr.left == client.left + (LONG)std::lround(14 * s));
    CHECK(pr.bottom == client.bottom - (LONG)std::lround(14 * s));
    CHECK(pr.right - pr.left == (LONG)std::lround(269 * s));
    CHECK(std::abs((pr.bottom - pr.top) - (LONG)std::lround(Paper::available * s)) <= 1);
    CHECK(NativeUI::IsShadowless(popup));
    // At rest: no vsync frame; the background alone moves at 30 a second on a timer (with a hardware device).
    const auto posted = impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0;
    CHECK(posted > statsBefore);
    const auto rendersBefore = impl->grainRenders;
    Pump(500);
    CHECK((impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0) == posted && !impl->fallbackFrames);
    if (impl->gpu.Animated()) CHECK(impl->grainTimer && impl->grainRenders >= rendersBefore + 8 && impl->grainRenders <= rendersBefore + 20);
    else CHECK(!impl->grainTimer && impl->grainRenders == rendersBefore);
    // Hit testing: card inside = client, rounded corner / outside = transparent.
    auto hit = [&](int x, int y) { return SendMessageW(popup, WM_NCHITTEST, 0, MAKELPARAM(x, y)); };
    CHECK(hit((pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2) == HTCLIENT);
    CHECK(hit(pr.left, pr.top) == HTTRANSPARENT && hit(pr.right - 1, pr.bottom - 1) == HTTRANSPARENT);
    CHECK(hit(pr.left - 5, pr.bottom - 20) == HTTRANSPARENT);
    // Click « Plus tard » then « Télécharger » (popup client coordinates); actions arrive after a posted message.
    const Geometry& g = impl->presenter.CurrentGeometry();
    CHECK(g.buttons.size() == 2);
    auto click = [&](const Rect& b) {
        const int bx = (int)std::lround((b.left + b.right) / 2 * s), by = (int)std::lround((b.top + b.bottom) / 2 * s);
        SendMessageW(popup, WM_MOUSEMOVE, 0, MAKELPARAM(bx, by));
        SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bx, by));
        SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(bx, by));
        return std::make_pair(bx, by);
    };
    click(g.buttons[1]); Pump(50);
    CHECK(actions.size() == 1 && actions[0] == Action::Later);
    const auto at = click(g.buttons[0]);
    CHECK(actions.size() == 1);
    Pump(50);
    CHECK(actions.size() == 2 && actions[1] == Action::Download);
    CHECK(GetForegroundWindow() == foreground && GetFocus() == owner);
    // Phase flips between press and release: Télécharger never becomes Annuler.
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(at.first, at.second));
    toast.SetView(V(Phase::Downloading, PaperReceived, PaperTotal, 14));
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(at.first, at.second));
    Pump(700);
    CHECK(actions.size() == 2);
    GetWindowRect(popup, &pr);
    CHECK(pr.bottom == client.bottom - (LONG)std::lround(14 * s));   // bottom edge anchored
    CHECK(std::abs((pr.bottom - pr.top) - (LONG)std::lround(Paper::downloading * s)) <= 1);
    CHECK(impl->painter.composes > 0);   // the drawer's frames drew the icon
    // Keyboard: explicit Focus(), Enter activates the primary, Esc returns focus.
    CHECK(toast.Focus() && GetFocus() == popup);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0); Pump(50);
    CHECK(actions.size() == 3 && actions[2] == Action::Cancel);
    toast.SetView(V(Phase::Error, 0, 0, -1, L"Réseau indisponible"));
    Pump(400);
    CHECK(impl->focus == 0);   // primary (Réessayer) focused after the swap
    SendMessageW(popup, WM_KEYDOWN, VK_TAB, 0);
    CHECK(impl->focus == 1);
    SendMessageW(popup, WM_KEYDOWN, VK_SPACE, 0); SendMessageW(popup, WM_KEYUP, VK_SPACE, 0); Pump(50);
    CHECK(actions.size() == 4 && actions[3] == Action::Dismiss);
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0); Pump(50);
    CHECK(releases == 1 && GetFocus() == owner);
    toast.SetView(V(Phase::Available)); Pump(500);
    // Layouts (as main.cpp drives them): a drawer open on the right, the bookmarks band, a narrow and a short window.
    toast.SetReservedRight(268 + 8);
    toast.Layout(); Pump(300);
    GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, (POINT*)&client, 2); GetWindowRect(popup, &pr);
    CHECK(IsWindowVisible(popup) && pr.left == client.left + (LONG)std::lround(14 * s) && pr.right <= client.right - (LONG)std::lround((268 + 8 + 14) * s));
    toast.SetReservedRight(0);
    toast.SetTopReserve(121); toast.Layout(); Pump(200);
    CHECK(IsWindowVisible(popup));
    SetWindowPos(owner, nullptr, 0, 0, MulDiv(900, sys, 96), MulDiv(380, sys, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    toast.Layout(); Pump(200);
    CHECK(!toast.Fits() && !IsWindowVisible(popup));   // card taller than the room under the chrome: hidden, state kept
    toast.SetTopReserve(81);
    SetWindowPos(owner, nullptr, 0, 0, MulDiv(240, sys, 96), MulDiv(640, sys, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    toast.Layout(); Pump(200);
    CHECK(!toast.Fits() && !IsWindowVisible(popup));
    SetWindowPos(owner, nullptr, 0, 0, MulDiv(700, sys, 96), MulDiv(520, sys, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    toast.Layout(); Pump(400);
    GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, (POINT*)&client, 2); GetWindowRect(popup, &pr);
    CHECK(toast.Fits() && IsWindowVisible(popup) && pr.left == client.left + (LONG)std::lround(14 * s) && pr.bottom == client.bottom - (LONG)std::lround(14 * s));
    // Minimised: hidden, no frame, no background timer, no background render.
    ShowWindow(owner, SW_MINIMIZE); toast.SetOwnerVisible(false); Pump(100);
    const auto hiddenPosted = impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0, hiddenRenders = impl->grainRenders, hiddenPresents = impl->presents;
    Pump(600);
    CHECK(!IsWindowVisible(popup) && !impl->grainTimer && impl->grainRenders == hiddenRenders && impl->presents == hiddenPresents);
    CHECK((impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0) == hiddenPosted);
    ShowWindow(owner, SW_RESTORE); toast.SetOwnerVisible(true); Pump(400);
    CHECK(IsWindowVisible(popup));
    // Reduced motion: the background freezes (no timer, no new frame).
    impl->options.forceReducedMotion = 1; toast.SyncReducedMotion(); Pump(300);
    const auto frozen = impl->grainRenders; Pump(500);
    CHECK(!impl->grainTimer && impl->grainRenders == frozen);
    // The profile changes while the card is up (still frozen by reduced motion): the background follows at once.
    {
        auto& ink = NativeChrome::Design::Accent::InkCurrent(); const auto saved = ink;
        ink = {0x6F / 255.f, 0xD0 / 255.f, 0x87 / 255.f};   // « vert »
        const auto before = impl->grainRenders; toast.AccentChanged(); Pump(300);
        CHECK(impl->grainRenders == before + 1 && impl->grainColors == Grain::Colors({0x6F, 0xD0, 0x87}));
        ink = saved; toast.AccentChanged(); Pump(300);
        CHECK(impl->grainColors == Grain::Colors(impl->Ink()));
    }
    impl->options.forceReducedMotion = 0; toast.SyncReducedMotion(); Pump(200);
    // Installed: only by the real event; finite dwell then a single Dismiss and exit.
    toast.SetView(V(Phase::Installed));
    Pump(300);
    CHECK(impl->presenter.Current().kind == Kind::Installed && impl->dwell.Active());
    impl->dwell.Start(M::Now() - Timing::installedDwell + Ms(100)); impl->ArmDwell(M::Now());
    Pump(600);
    CHECK(actions.size() == 5 && actions[4] == Action::Dismiss && !IsWindowVisible(popup));
    // Hidden card: no frame, no timer, and the background's GPU resources are released.
    toast.SetView(V(Phase::Available)); Pump(500);
    toast.SetView(V(Phase::Hidden)); Pump(500);
    CHECK(!IsWindowVisible(popup) && !impl->presenter.Shown() && !impl->gpu.Ready() && !impl->grainTimer);
    const auto idlePosted = impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0, idlePresents = impl->presents;
    Pump(600);
    CHECK((impl->clock.Stats() ? impl->clock.Stats()->posted.load() : 0) == idlePosted && impl->presents == idlePresents);
    toast.Close(); toast.Close();
    CHECK(!IsWindow(popup));
    DestroyWindow(owner);
    Pump(50);
}

int wmain(int argc, wchar_t** argv) {
    // Every window of this run lives on a desktop of its own that is never displayed (before any window exists).
    wchar_t deskName[64]; std::swprintf(deskName, 64, L"update-card-tests-%lu", GetCurrentProcessId());
    HDESK desk = CreateDesktopW(deskName, nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desk || !SetThreadDesktop(desk)) { std::fprintf(stderr, "hidden desktop unavailable (%lu): refusing to show windows\n", GetLastError()); return 2; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    repo = argc > 1 ? argv[1] : L".";
    snapshots = argc > 2 ? argv[2] : repo + L"\\build\\update-toast\\snapshots";
    CreateDirectoryW(snapshots.c_str(), nullptr);
    FrenchModelTests();
    LaterTests();
    ProfileColorTests();
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) { std::fprintf(stderr, "WIC unavailable\n"); return 2; }
    {
        Onboarding::Shaders::Image noise;
        if (gpu.Init() && Onboarding::Shaders::LoadPng(wic, repo + L"\\assets\\onboarding\\noise.png", noise)) gpu.SetInput(Onboarding::Shaders::Kind::GrainGradient, noise);
        std::printf("background shader: %s\n", gpu.Animated() ? "hardware" : gpu.Ready() ? "software (still frame)" : "unavailable");
        Painter painter;
        if (FAILED(painter.Init(repo + L"\\assets"))) { std::fprintf(stderr, "painter init failed\n"); return 2; }
        SnapshotTests(painter);   // French renders (Val's texts), compared with the preview by compare.py
        // Model, motion and the remaining checks in English: pinned explicitly, never the OS language.
        I18n::SetForProcess(I18n::Locale::En);
        ModelTests();
        MotionTests();
        ClickAndDwellTests();
        painter.ClearCache();
        PainterTests(painter);
    }
    gpu.Dispose();
    bool windows = true;
    for (int i = 3; i < argc; ++i) if (std::wstring(argv[i]) == L"--no-window") windows = false;
    if (windows) WindowTests();
    Drop(wic);
    CoUninitialize();
    std::printf("%d checks, %d failures\n", checks, failures);
    CloseDesktop(desk);
    return failures ? 1 : 0;
}
