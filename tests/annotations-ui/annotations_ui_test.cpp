// Native harness for the annotation-mode UI (V1): no WebView2, no network, no disk I/O in the
// components (the harness itself writes PNG captures and a JSON report).
// Usage: annotations-ui-test.exe <repoRoot> <outDir> [--no-window]
//   1. zero cost before first use (no Ink, no Animator thread, no window)
//   2. pure motion samples (t = 0, 1/4, 1/2, end, interruption, reduced motion), press, spinner
//   3. config texts, Paper geometry (box-model heights, widths), Paper colours sampled in renders
//   4. PNG captures of every state + scenes + motion strips (build/annotations-ui-qa)
//   5. measured contrast (from rendered pixels at 2x) for every text style
//   6. live windows: hit-testing through transparent pixels, keyboard paths, hooks, UIA nodes,
//      animator frames during motion and ZERO wake-ups at rest
#include "../../src/annotation-ui.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <sstream>

using namespace NativeChrome;
using namespace NativeChrome::Annotations;
namespace M = NativeChrome::Motion;

static int failures = 0, checks = 0;
static std::wostringstream report;   // JSON fragments
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECKM(cond, ...) do { ++checks; if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
static M::Time T0() { return M::Time{} + std::chrono::hours(2); }
static M::Time At(double ms) { return T0() + std::chrono::duration_cast<M::Clock::duration>(std::chrono::duration<double, std::milli>(ms)); }
static std::wstring outDir;
static std::vector<std::wstring> captures;
static std::wstring Wide(const std::string& s) { const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0); std::wstring w(size_t(n), 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n); return w; }
static std::string Narrow(const std::wstring& w) { std::string s; for (wchar_t c : w) s += c < 128 ? char(c) : '?'; return s; }

// ---------------------------------------------------------------------------------------
// Pixels and contrast
struct Rgb { double r, g, b; };
static Rgb Px(std::uint32_t p) { return {double((p >> 16) & 255), double((p >> 8) & 255), double(p & 255)}; }
static double Lin(double c) { c /= 255; return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); }
static double Lum(Rgb c) { return .2126 * Lin(c.r) + .7152 * Lin(c.g) + .0722 * Lin(c.b); }
static double Ratio(Rgb a, Rgb b) { double x = Lum(a), y = Lum(b); if (x < y) std::swap(x, y); return (x + .05) / (y + .05); }
static Rgb Over(std::uint32_t rgba, Rgb under) { const double a = (rgba & 255) / 255.0; return {((rgba >> 24) & 255) * a + under.r * (1 - a), ((rgba >> 16) & 255) * a + under.g * (1 - a), ((rgba >> 8) & 255) * a + under.b * (1 - a)}; }
static Rgb Solid(std::uint32_t rgb) { return {double((rgb >> 16) & 255), double((rgb >> 8) & 255), double(rgb & 255)}; }
static bool Close(Rgb a, Rgb b, double tol = 2.5) { return std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol && std::fabs(a.b - b.b) <= tol; }
static std::string Hex6(Rgb c) { char b[16]; std::snprintf(b, 16, "#%02X%02X%02X", int(std::lround(c.r)), int(std::lround(c.g)), int(std::lround(c.b))); return b; }
// Rendered text contrast: the pixel of the text rect that differs most from the background.
struct Contrast { std::string name; double measured, expected; std::string fg, bg; };
static std::vector<Contrast> contrasts;
static void MeasureText(const Canvas& c, float scale, D2D1_RECT_F box, Rgb bg, double expected, const char* name, double minimum = 4.5) {
    double best = 1; Rgb fg = bg;
    for (int y = int(box.top * scale); y < int(box.bottom * scale); ++y) for (int x = int(box.left * scale); x < int(box.right * scale); ++x) {
        const Rgb p = Px(c.At(x, y)); const double r = Ratio(p, bg); if (r > best) { best = r; fg = p; }
    }
    contrasts.push_back({name, best, expected, Hex6(fg), Hex6(bg)});
    CHECKM(best >= minimum - .05, "%s measured %.2f:1 < %.1f", name, best, minimum);
    CHECKM(expected >= minimum, "%s token pair %.2f:1 < %.1f", name, expected, minimum);
}

// ---------------------------------------------------------------------------------------
// Fixtures
static NoteView SampleNote() {
    NoteView v; v.note = L"Le bouton devrait être aligné à droite comme sur la maquette, et passer en pleine largeur sur mobile.";
    v.projectLabel = L"Boutique"; v.context = {L"localhost:5173/checkout", L"form > button.btn-primary", L"« Payer 48,00 € »",
        L"display: inline-flex\njustify-content: center\nwidth: 240px · padding: 0 24px\nborder-radius: 8px", L"Recadrée · 960×420"};
    v.capture = CaptureState::Ready; v.includeShot = true; return v;
}
static Shot SampleShot() {   // a 192x112 « page » with the dark pay button (premultiplied BGRA)
    Shot s; s.width = 192; s.height = 112; s.pixels.assign(size_t(s.width) * s.height, 0xFFF4F4F3);
    for (unsigned y = 44; y < 68; ++y) for (unsigned x = 38; x < 154; ++x) s.pixels[y * s.width + x] = 0xFF1C1C1C;
    s.mark = {34, 40, 158, 72}; return s;
}
static std::vector<Project> SampleProjects() { return {{L"boutique", L"Boutique", L"localhost:5173"}, {L"docs", L"Docs Vite", L"localhost:4173"}, {L"lisiere", L"Lisière — préproduction", L"staging.lisiere.app"}}; }
static OnboardingSetup SampleSetup() { OnboardingSetup s; s.projectId = L"boutique"; s.projectLabel = L"Boutique"; s.bridgePath = L"C:\\Users\\vous\\AppData\\Local\\browser\\bin\\browser-annotations-mcp.exe"; return s; }

static bool Save(Ink& ink, const Canvas& c, const wchar_t* name) {
    const std::wstring path = outDir + L"\\" + name; const HRESULT hr = SavePng(ink, c, path);
    CHECKM(SUCCEEDED(hr), "png %ls", name); if (SUCCEEDED(hr)) captures.push_back(name); return SUCCEEDED(hr);
}
static bool Render(Ink& ink, float w, float h, UINT dpi, std::uint32_t backdrop, const std::function<void(Ink&, float, float)>& paint, Canvas& out) {
    const bool ok = RenderOffscreen(ink, w, h, dpi, backdrop, paint, out); CHECK(ok); return ok;
}
// A mock page (Paper « Contenu web — Boutique ») for scenes.
static void PaintPage(Ink& ink, float x, float y, float w, float h, bool selection) {
    ink.Fill({x, y, x + w, y + h}, 0, Hex(0xF4F4F3FF));
    ink.Fill({x, y, x + w, y + 56}, 0, Hex(0xFFFFFFFF)); ink.Fill({x, y + 56, x + w, y + 57}, 0, Hex(0x0000000F));
    ink.Text(L"Boutique", F(16, 20, 500), {x + 64, y + 18, x + 300, y + 38}, Hex(0x171717FF));
    ink.Text(L"Panier · 2 articles", F(14, 20), {x + w - 200, y + 18, x + w - 64, y + 38}, Hex(0x555555FF), false, Align::Trailing);
    ink.Text(L"Paiement", F(28, 34, 500, -.02f), {x + 64, y + 96, x + 400, y + 130}, Hex(0x171717FF));
    for (int i = 0; i < 2; ++i) {
        const float fy = y + 154 + i * 88;
        ink.Text(i ? L"Carte" : L"Adresse e-mail", F(13, 18), {x + 64, fy, x + 400, fy + 18}, Hex(0x555555FF));
        ink.Fill({x + 64, fy + 26, x + 584, fy + 70}, 8, Hex(0xFFFFFFFF)); ink.Stroke({x + 64, fy + 26, x + 584, fy + 70}, 8, Hex(0x0000001F), 1);
    }
    const D2D1_RECT_F b{x + 64, y + 350, x + 304, y + 394};
    ink.Fill(b, 8, Hex(0x1C1C1CFF)); ink.Text(L"Payer 48,00 €", F(15, 20, 500), b, {1, 1, 1, 1}, false, Align::Center, true);
    if (selection) { ink.Stroke(Inflate(b, 3), 10, Accent(), 2); ink.Fill({b.left - 3, b.top - 25, b.left + 205, b.top - 7}, 3, Accent());
        ink.Text(L"button.btn-primary · 240×44", Mono(11, 14), {b.left + 3, b.top - 25, b.left + 205, b.top - 7}, {1, 1, 1, 1}, false, Align::Leading, true); }
}
static void PaintChrome(Ink& ink, float w) {   // the browser chrome above the band (placeholder: canvas + toolbar button)
    ink.Fill({0, 0, w, 85}, 0, Hex(Ink_::canvas));
    ink.Text(L"localhost:5173/checkout", F(16, 20, 0, -.01f), {160, 49, 600, 69}, {1, 1, 1, 1});
    ToolbarVisual tv; tv.active = true; PaintToolbarButton(ink.Target(), ink.Factory(), ink.Brush({1, 1, 1, 1}), {w - 158, 47, w - 134, 71}, tv);
}

// ---------------------------------------------------------------------------------------
static void ZeroCostTests() {
    CHECK(!Ink::Exists());
    CHECK(!Animator::Exists());
    CHECK(Layer::Created() == 0);
    Banner banner; NotePopover note; Launcher launcher; Onboarding onboarding;
    banner.Init(GetModuleHandleW(nullptr), nullptr, L".", {}); note.Init(GetModuleHandleW(nullptr), nullptr, L".", {});
    launcher.Init(GetModuleHandleW(nullptr), nullptr, L".", {}); onboarding.Init(GetModuleHandleW(nullptr), nullptr, L".", {});
    CHECK(!banner.Created() && !note.Created() && !launcher.Created() && !onboarding.Created());
    CHECK(!Ink::Exists() && !Animator::Exists() && Layer::Created() == 0);   // Init stores context only
    report << L"\"zeroCost\":{\"inkBeforeUse\":false,\"animatorBeforeUse\":false,\"windowsBeforeUse\":0},";
}

static double SettleMs(const Presence& p, double from, double limit = 2000) { for (double t = from; t < limit; t += .5) if (!p.Moving(At(t))) return t - from; return -1; }
static double TimeTo(const Presence& p, double from, double target, bool up) { for (double t = from; t < from + 2000; t += .5) { const float v = p.Progress(At(t)); if (up ? v >= target : v <= target) return t - from; } return -1; }
static void MotionTests() {
    std::wostringstream m; m.setf(std::ios::fixed); m.precision(4);
    // Enter: critically damped, monotonic, never overshoots.
    Presence enter; enter.Show(At(0));
    const double settle = SettleMs(enter, 0), to98 = TimeTo(enter, 0, .98f, true);
    CHECKM(settle > 300 && settle < 700, "enter settle %.1f ms", settle);
    float last = -1; bool monotonic = true, overshoot = false;
    for (double t = 0; t <= settle + 50; t += 1) { const float v = enter.Raw(At(t)); if (v < last - 1e-6f) monotonic = false; if (v > 1.0001f) overshoot = true; last = v; }
    CHECK(monotonic); CHECK(!overshoot);
    auto sample = [&](const wchar_t* name, const Presence& p, double t, bool reduced, float liftY) {
        const Pose s = p.Sample(At(t), reduced, 0, liftY);
        m << L"{\"name\":\"" << name << L"\",\"t\":" << t << L",\"progress\":" << p.Progress(At(t)) << L",\"opacity\":" << s.opacity << L",\"scale\":" << s.scale << L",\"dy\":" << s.dy << L"},";
        std::printf("  %-28ls t=%7.1f ms  p=%.4f  opacity=%.4f  scale=%.4f  dy=%.3f\n", name, t, p.Progress(At(t)), s.opacity, s.scale, s.dy);
        return s;
    };
    std::printf("Motion samples (card enter: lift 10 DIP, response 0.35 s; end = settle %.1f ms, 98 %% at %.1f ms)\n", settle, to98);
    const Pose p0 = sample(L"enter t=0", enter, 0, false, Spec::panelLift);
    const Pose p1 = sample(L"enter t=1/4", enter, settle / 4, false, Spec::panelLift);
    const Pose p2 = sample(L"enter t=1/2", enter, settle / 2, false, Spec::panelLift);
    const Pose p3 = sample(L"enter t=end", enter, settle, false, Spec::panelLift);
    CHECK(p0.opacity == 0 && Near(p0.scale, .96, 1e-6) && Near(p0.dy, 10, 1e-4));
    CHECK(p1.opacity > p0.opacity && p2.opacity > p1.opacity && p3.opacity == 1 && p3.scale == 1 && p3.dy == 0);
    CHECK(p1.scale > .96f && p1.scale < 1 && p2.dy < p1.dy);
    // Exit: same path, shorter (98 % gone by ~150 ms).
    Presence exit; exit.Show(At(0), false); exit.Hide(At(0));
    const double exitSettle = SettleMs(exit, 0), exit98 = TimeTo(exit, 0, .02f, false);
    std::printf("  exit: 98 %% gone at %.1f ms, settled at %.1f ms (enter 98 %% at %.1f ms)\n", exit98, exitSettle, to98);
    CHECKM(exit98 > 120 && exit98 <= 160, "exit 98%% at %.1f ms", exit98);
    CHECK(exit98 < to98);
    sample(L"exit t=1/2", exit, exitSettle / 2, false, Spec::panelLift);
    // Interruption mid-course: the close starts from the presented value and velocity.
    Presence it; it.Show(At(0)); const double mid = settle / 2;
    const float before = it.Progress(At(mid)), vBefore = it.Velocity(At(mid));
    it.Hide(At(mid));
    const float after = it.Progress(At(mid)), vAfter = it.Velocity(At(mid)), next = it.Progress(At(mid + 8));
    std::printf("  interrupt at %.1f ms: before %.5f (v %.3f/s) after %.5f (v %.3f/s), +8 ms %.5f\n", mid, before, vBefore, after, vAfter, next);
    CHECK(Near(before, after, 1e-5)); CHECK(Near(vBefore, vAfter, 1e-3));
    CHECK(vBefore > 0);   // still moving toward « open » when grabbed: no jump, no velocity cut
    sample(L"interrupt +0", it, mid, false, Spec::panelLift); sample(L"interrupt +40ms", it, mid + 40, false, Spec::panelLift);
    // Re-open during the close: continuous again.
    const float b2 = it.Progress(At(mid + 60)); it.Show(At(mid + 60)); CHECK(Near(b2, it.Progress(At(mid + 60)), 1e-5));
    // Reduced motion: opacity only.
    Presence red; red.Show(At(0));
    const Pose r = sample(L"reduced t=1/2", red, settle / 2, true, Spec::panelLift);
    CHECK(r.scale == 1 && r.dx == 0 && r.dy == 0 && r.opacity > 0 && r.opacity < 1);
    // Press: 0.96 after 150 ms ease-out; never disabled / reduced.
    M::Interaction press; press.EnablePolish(); press.Set(true, true, At(0));
    const float s0 = Annotations::PressScale(press.Visual(At(0)), true, false), s75 = Annotations::PressScale(press.Visual(At(75)), true, false), s150 = Annotations::PressScale(press.Visual(At(150)), true, false);
    std::printf("  press scale: 0 ms %.4f, 75 ms %.4f, 150 ms %.4f\n", s0, s75, s150);
    CHECK(s0 == 1 && s75 < 1 && s75 > .96f && Near(s150, .96, 1e-6));
    CHECK(Annotations::PressScale(press.Visual(At(150)), false, false) == 1 && Annotations::PressScale(press.Visual(At(150)), true, true) == 1);
    // Step cross-fade with interruption.
    Crossfade f; f.Reset(0); f.Go(1, At(0));
    CHECK(f.Incoming(At(0)) == 0 && Near(f.Outgoing(At(0)), 1, 1e-6) && f.Incoming(At(200)) == 1 && f.Outgoing(At(200)) == 0);
    const float in100 = f.Incoming(At(100)); f.Go(2, At(100));
    CHECK(Near(f.Outgoing(At(100)), in100, 1e-6) && f.Incoming(At(100)) == 0);
    std::printf("  step cross-fade: incoming at 100 ms %.3f; interrupted -> outgoing starts at %.3f\n", in100, f.Outgoing(At(100)));
    // Spinner: one turn per 1.2 s, fixed when reduced.
    const float a0 = SpinnerAngle(At(0), false), a300 = SpinnerAngle(At(300), false);
    CHECK(Near(std::fmod(a300 - a0 + 360, 360), 90, .5)); CHECK(SpinnerAngle(At(300), true) == 0);
    report << L"\"motion\":{\"enterSettleMs\":" << settle << L",\"enter98Ms\":" << to98 << L",\"exit98Ms\":" << exit98 << L",\"exitSettleMs\":" << exitSettle
           << L",\"interrupt\":{\"atMs\":" << mid << L",\"before\":" << before << L",\"after\":" << after << L",\"velocityBefore\":" << vBefore << L",\"velocityAfter\":" << vAfter << L"}"
           << L",\"press\":{\"t0\":" << s0 << L",\"t75\":" << s75 << L",\"t150\":" << s150 << L"},\"samples\":[" << m.str() << L"{}]},";
}

static void ConfigTests() {
    const std::wstring path = L"C:\\Users\\vous\\AppData\\Local\\browser\\bin\\browser-annotations-mcp.exe";
    CHECK(ClaudeConfig(path, L"boutique") == L"\"browser-annotations\": {\n  \"command\": \"C:\\\\Users\\\\vous\\\\AppData\\\\Local\\\\browser\\\\bin\\\\browser-annotations-mcp.exe\",\n  \"args\": [\"--project\", \"boutique\"]\n}");
    CHECK(CodexConfig(path, L"boutique") == L"[mcp_servers.browser-annotations]\ncommand = 'C:\\Users\\vous\\AppData\\Local\\browser\\bin\\browser-annotations-mcp.exe'\nargs = [\"--project\", \"boutique\"]");
    CHECK(CodexConfig(L"C:\\O'Brien\\x.exe", L"") == L"[mcp_servers.browser-annotations]\ncommand = \"C:\\\\O'Brien\\\\x.exe\"\nargs = []");
    CHECK(GenericConfig(L"C:\\a\\b.exe", L"p\"q").find(L"\"args\": [\"--project\", \"p\\\"q\"]") != std::wstring::npos);
    CHECK(ArgsText(L"boutique") == L"--project boutique" && ArgsText(L"").empty());
}

// Paper box-model heights (WKR-0, WVJ-0, WOL-0, XAF-0...) vs the layouts.
static void GeometryTests(Ink& ink) {
    std::wostringstream g;
    NoteView v = SampleNote();
    const NoteLayout open = LayoutNote(ink, v);
    v.expanded = false; const NoteLayout collapsed = LayoutNote(ink, v); v.expanded = true;
    CHECK(Near(collapsed.height, 272, .01));   // 1+12+16+6+84+8 | 32+12 | 1+12+18+14 | 1+10+32+12+1
    const int mentionLines = ink.Lines(NoteSpec::mention, F(12, 16), 310), styleLines = ink.Lines(v.context.styles, Mono(12, 18), 244);
    const float paperOpen = 1 + 12 + 16 + 6 + 84 + 8 + 32 + 12 + 1 + 12 + 18 + 10 + (18 + 6 + 18 + 6 + 18 + 6 + 18.f * styleLines) + 10 + 2 + 56 + 10 + 16.f * mentionLines + 14 + 1 + 10 + 32 + 12 + 1;
    CHECKM(Near(open.height, paperOpen, .01), "open %.2f vs %.2f", open.height, paperOpen);
    CHECK(styleLines == 4 && mentionLines == 2);   // as in Paper (4 CSS lines, mention on 2 lines)
    CHECK(Near(open.field.right - open.field.left, 314, .01) && Near(open.field.bottom - open.field.top, 84, .01));
    CHECK(Near(open.selector.bottom - open.selector.top, 32, .01) && Near(open.cancel.right - open.cancel.left, open.add.right - open.add.left, .01) && Near(open.add.bottom - open.add.top, 32, .01));
    CHECK(Near(open.thumb.right - open.thumb.left, 96, .01) && Near(open.thumb.bottom - open.thumb.top, 56, .01));
    CHECK(Near(open.rows[0].value.left - open.rows[0].labelBox.left, 66, .01));   // label 56 + gap 10
    CHECK(Near(ListHeight(3), 2 + 8 + 3 * 54 + 9 + 34, .01) && Near(ListRow(3, 0).bottom - ListRow(3, 0).top, 54, .01));
    g << L"{\"surface\":\"note (WKR-0)\",\"width\":340,\"paperHeight\":" << paperOpen << L",\"height\":" << open.height << L",\"collapsedPaper\":272,\"collapsed\":" << collapsed.height << L"},";
    // Card (WVJ-0 / WOL-0) and launcher (WO9-0 / WOD-0).
    CardContent noAgent; noAgent.kind = CardKind::NoAgent; noAgent.project = L"Boutique";
    const CardLayout cl = LayoutCard(ink, noAgent); const int bodyLines = ink.Lines(NoAgentBody(), F(14, 20), 294);
    const float paperNoAgent = 1 + 12 + 24 + 10 + 20.f * bodyLines + 10 + 32 + 12 + 1;
    CHECKM(Near(cl.height, paperNoAgent, .01), "card %.2f vs %.2f", cl.height, paperNoAgent);
    CHECK(bodyLines == 2 && Near(cl.connect.right - cl.connect.left, (294 - 8) * 1.4 / 2.4, .05));
    CardContent working; working.kind = CardKind::Working; working.agent = L"Opus 5.5"; working.since = L"Depuis 2 min"; working.project = L"Boutique"; working.note = v.note;
    const CardLayout wl = LayoutCard(ink, working); const int noteLines = ink.Lines(working.note, F(14, 20), 294);
    CHECK(Near(wl.height, 1 + 12 + 24 + 8 + 20.f * noteLines + 8 + 32 + 12 + 1, .01) && noteLines == 3);
    CHECK(Near(LauncherBox().right - LauncherBox().left, 40, .01));
    g << L"{\"surface\":\"card no agent (WVJ-0)\",\"width\":320,\"paperHeight\":" << paperNoAgent << L",\"height\":" << cl.height << L"},";
    g << L"{\"surface\":\"card working (WOL-0)\",\"width\":320,\"height\":" << wl.height << L"},";
    // Modal (XAF-0).
    ModalView mv; mv.setup = SampleSetup();
    const ModalLayout ml = LayoutModal(ink, mv);
    const int subLines = ink.Lines(mv.Subtitle(), F(14, 20), 478);
    const float noteH = ink.Height(L"Tout reste local, sans port réseau. Ce que l'agent lit peut ensuite être envoyé à son fournisseur de modèle.", F(12, 17), ml.note.right - ml.note.left);
    const float paperChoose = 1 + 20 + std::max(28.f, 26 + 4 + 20.f * subLines) + 14 + 18 + 16 + 4 + 3 * 64 + 3 * 8 + 8 + 18 + 16 + 1 + 14 + std::max(32.f, noteH) + 16 + 1;
    CHECKM(Near(ml.height, paperChoose, .01), "modal %.2f vs %.2f", ml.height, paperChoose);
    CHECK(Near(ml.options[0].bottom - ml.options[0].top, 64, .01) && Near(ml.primary.bottom - ml.primary.top, 32, .01));
    g << L"{\"surface\":\"onboarding choose (XAF-0)\",\"width\":560,\"paperHeight\":" << paperChoose << L",\"height\":" << ml.height << L"},";
    report << L"\"geometry\":[" << g.str() << L"{}],";
}

// ---------------------------------------------------------------------------------------
static void Captures(Ink& ink) {
    const UINT dpi = 192; const float k = 2;   // 2x captures; sample at DIP * 2
    Canvas c; Shot shot = SampleShot();
    std::wostringstream colours;
    auto colour = [&](const char* name, const Canvas& cv, float x, float y, Rgb expected) {
        const Rgb got = Px(cv.At(int(x * k), int(y * k))); const bool ok = Close(got, expected);
        CHECKM(ok, "%s: got %s, Paper %s", name, Hex6(got).c_str(), Hex6(expected).c_str());
        colours << L"{\"what\":\"" << Wide(name) << L"\",\"paper\":\"" << Wide(Hex6(expected)) << L"\",\"rendered\":\"" << Wide(Hex6(got)) << L"\"},";
    };
    const Rgb canvas = Solid(0x171717), raised = Solid(0x292929);
    // ---- Banner and toolbar button
    BannerVisual bv;
    if (Render(ink, 1180, 32, dpi, Ink_::canvas, [&](Ink& i, float w, float h) { PaintBanner(i, w, h, bv); }, c)) {
        Save(ink, c, L"01-banner.png");
        const auto l = LayoutBanner(ink, 1180);
        colour("banner bottom border (#3E67FF3D over band)", c, 600, 31.5f, Over(0x3E67FF3D, Over(0x233FD652, canvas)));
        const Rgb labelBg = Px(c.At(int((l.label.right + 20) * k), int(16 * k)));
        MeasureText(c, k, {l.label.left, 6, l.label.right, 26}, labelBg, Ratio(Solid(0x5B84FF), Over(0x233FD629, canvas)), "banner « Mode Annotation » #5B84FF");
        MeasureText(c, k, {l.quit.left + 8, l.quit.top, l.keycapX, l.quit.bottom}, labelBg, Ratio(Over(0xFFFFFFE6, canvas), canvas), "banner « Quitter » #FFFFFFE6");
        MeasureText(c, k, {l.keycapX + 4, l.quit.top + 4, l.quit.right - 4, l.quit.bottom - 4}, Px(c.At(int((l.keycapX + 2) * k), int(9 * k))), Ratio(Over(0xFFFFFFB3, Over(0xFFFFFF14, canvas)), Over(0xFFFFFF14, canvas)), "banner key cap « Échap »");
    }
    bv.quit.hover = 1; bv.focus = true; bv.keyboard = true;
    if (Render(ink, 1180, 32, dpi, Ink_::canvas, [&](Ink& i, float w, float h) { PaintBanner(i, w, h, bv); }, c)) Save(ink, c, L"01b-banner-quit-focus.png");
    for (int active = 0; active < 2; ++active) {
        if (Render(ink, 40, 40, dpi, Ink_::canvas, [&](Ink& i, float, float) { ToolbarVisual tv; tv.active = active; PaintToolbarButton(i.Target(), i.Factory(), i.Brush({1, 1, 1, 1}), {8, 6, 32, 30}, tv); }, c)) {
            Save(ink, c, active ? L"02-toolbar-annotate-active.png" : L"02b-toolbar-annotate-idle.png");
            if (active) { colour("toolbar active plate (accent 16 %)", c, 9.5f, 18, Over(0x3E67FF29, canvas)); colour("toolbar active bar (accent)", c, 20, 33, Solid(0x3E67FF)); }
        }
    }
    // ---- Note popover states
    auto note = [&](NoteView v, const wchar_t* name, bool check) {
        const NoteLayout l = LayoutNote(ink, v);
        if (!Render(ink, NoteSpec::width, l.height, dpi, Ink_::canvas, [&](Ink& i, float, float) { PaintNote(i, v, l); }, c)) return;
        Save(ink, c, name);
        if (!check) return;
        colour("note surface #292929", c, 330, l.selector.bottom + 6, raised);
        colour("note field #FFFFFF0A on raised", c, l.field.right - 6, l.field.bottom - 6, Over(0xFFFFFF0A, raised));
        colour("project selector #FFFFFF0A", c, l.selector.left + 230, (l.selector.top + l.selector.bottom) / 2, Over(0xFFFFFF0A, raised));
        colour("context section #FFFFFF05", c, 330, l.lineC + 6, Over(0xFFFFFF05, raised));
        colour("« Annuler » #333333", c, l.cancel.left + 5, l.cancel.top + 16, Solid(0x333333));
        colour("« Ajouter » accent #3E67FF", c, l.add.left + 5, l.add.top + 16, Solid(0x3E67FF));
        MeasureText(c, k, l.label, raised, Ratio(Over(0xFFFFFF99, raised), raised), "note label 12px #FFFFFF99");
        MeasureText(c, k, {l.field.left + 12, l.field.top + 10, l.field.right - 12, l.field.top + 30}, Over(0xFFFFFF0A, raised), Ratio(Over(0xFFFFFFE6, Over(0xFFFFFF0A, raised)), Over(0xFFFFFF0A, raised)), "note text 14px #FFFFFFE6");
        const Rgb sec = Over(0xFFFFFF05, raised);
        MeasureText(c, k, l.rows[0].labelBox, sec, Ratio(Over(0xFFFFFF99, sec), sec), "context row label 12px #FFFFFF99");
        MeasureText(c, k, l.rows[1].value, sec, Ratio(Over(0xFFFFFFCC, sec), sec), "context value mono 12px #FFFFFFCC");
        MeasureText(c, k, l.mention, sec, Ratio(Over(0xFFFFFF99, sec), sec), "provider mention 12px #FFFFFF99");
        MeasureText(c, k, l.sub, sec, Ratio(Over(0xFFFFFF99, sec), sec), "capture label 12px #FFFFFF99");
        MeasureText(c, k, {l.add.left + 20, l.add.top + 6, l.add.left + 80, l.add.bottom - 6}, Solid(0x3E67FF), Ratio(Solid(0xFFFFFF), Solid(0x3E67FF)), "« Ajouter » white on accent");
        MeasureText(c, k, {l.cancel.left + 20, l.cancel.top + 6, l.cancel.left + 80, l.cancel.bottom - 6}, Solid(0x333333), Ratio(Solid(0xFFFFFF), Solid(0x333333)), "« Annuler » white on #333333");
    };
    NoteView v = SampleNote(); v.shot = &shot;
    note(v, L"03-note-editing.png", true);
    { NoteView x = v; x.expanded = false; note(x, L"04-note-collapsed.png", false); }
    { NoteView x = v; x.listOpen = true; const NoteLayout l = LayoutNote(ink, x); ListView lv; lv.projects = SampleProjects(); lv.selected = L"boutique"; lv.hot = 1;
      const float lh = ListHeight(3);
      if (Render(ink, 360, std::max(l.height, l.selector.bottom + 6 + lh) + 10, dpi, Ink_::canvas, [&](Ink& i, float, float) {
            auto* t = i.Target(); t->SetTransform(D2D1::Matrix3x2F::Translation(10, 0)); PaintNote(i, x, l);
            t->SetTransform(D2D1::Matrix3x2F::Translation(10 + NoteSpec::listX, l.selector.bottom + 6)); PaintProjectList(i, lv); t->SetTransform(D2D1::IdentityMatrix()); }, c)) {
          Save(ink, c, L"05-note-project-list.png");
          colour("list hot row #FFFFFF14", c, 10 + NoteSpec::listX + 200, l.selector.bottom + 6 + ListRow(3, 1).top + 4, Over(0xFFFFFF14, raised));
          const auto r0 = ListRow(3, 0); const float oy = l.selector.bottom + 6;
          MeasureText(c, k, {10 + NoteSpec::listX + r0.left + 8, oy + r0.top + 28, 10 + NoteSpec::listX + r0.left + 120, oy + r0.top + 46}, raised, Ratio(Over(0xFFFFFF99, raised), raised), "project origin 13px #FFFFFF99");
      } }
    { NoteView x = v; x.tooltip = true; x.focus = NoteInfo; x.keyboard = true; const NoteLayout l = LayoutNote(ink, x); const float th = TooltipHeight(ink);
      if (Render(ink, NoteSpec::width, l.height, dpi, Ink_::canvas, [&](Ink& i, float, float) {
            PaintNote(i, x, l); i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(1 + NoteSpec::tipX, l.lineC + 1 + NoteSpec::tipY)); PaintTooltip(i, NoteSpec::tipW, th); i.Target()->SetTransform(D2D1::IdentityMatrix()); }, c)) {
          Save(ink, c, L"06-note-tooltip.png");
          MeasureText(c, k, {1 + NoteSpec::tipX + 11, l.lineC + 1 + NoteSpec::tipY + 9, 1 + NoteSpec::tipX + 225, l.lineC + 1 + NoteSpec::tipY + 27}, canvas, Ratio(Over(0xFFFFFFE6, canvas), canvas), "tooltip 13px #FFFFFFE6 on #171717");
      } }
    { NoteView x = v; x.capture = CaptureState::Pending; x.shot = nullptr; x.spinner = 40; note(x, L"07-note-capturing.png", false); }
    { NoteView x = v; x.status = NoteStatus::Failed; x.message = L"Impossible d'enregistrer l'annotation. Votre note est conservée ; réessayez."; x.focus = NoteAdd; x.keyboard = true; x.fieldFocused = false;
      const NoteLayout l = LayoutNote(ink, x); note(x, L"08-note-write-failed.png", false);
      Render(ink, NoteSpec::width, l.height, dpi, Ink_::canvas, [&](Ink& i, float, float) { PaintNote(i, x, l); }, c);
      MeasureText(c, k, {l.message.left + 22, l.message.top, l.message.right, l.message.top + 16}, raised, Ratio(Solid(0xFF7B6E), raised), "write error 12px #FF7B6E"); }
    { NoteView x = v; x.note.clear(); x.fieldError = true; x.message = L"Écrivez une remarque pour l'agent."; note(x, L"09-note-empty-validation.png", false); }
    { NoteView x = v; x.capture = CaptureState::Unavailable; x.includeShot = false; x.shot = nullptr; note(x, L"09b-note-capture-unavailable.png", false); }
    // ---- Launcher and card
    for (int working = 0; working < 2; ++working) {
        LauncherVisual lv; lv.state = working ? LauncherState::Working : LauncherState::Rest; lv.count = working; lv.spinner = 30;
        if (Render(ink, 46, 46, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { PaintLauncher(i, lv); }, c)) {
            Save(ink, c, working ? L"11-launcher-working.png" : L"10-launcher-rest.png");
            colour("launcher surface #171717", c, 4, 30, canvas);
            colour(working ? "launcher border #3E67FF66" : "launcher border #FFFFFF1F", c, 20, 6.5f, working ? Over(0x3E67FF66, canvas) : Over(0xFFFFFF1F, canvas));
            if (working) colour("badge accent", c, 37, 9, Solid(0x3E67FF));
        }
    }
    auto card = [&](const CardContent& cc, const wchar_t* name) {
        const CardLayout l = LayoutCard(ink, cc); CardVisual cv; cv.spinner = 30;
        if (!Render(ink, 320, l.height, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { PaintCard(i, cc, l, cv); }, c)) return;
        Save(ink, c, name);
        colour("card surface #292929", c, 160, l.header.bottom + 4, raised);
        if (cc.kind == CardKind::NoAgent) {
            colour("« Plus tard » #333333", c, l.later.left + 4, l.later.top + 16, Solid(0x333333));
            colour("« Connecter un agent » accent", c, l.connect.left + 4, l.connect.top + 16, Solid(0x3E67FF));
            MeasureText(c, k, {l.header.left + 24 + ink.Width(L"Annotation ajoutée", F(13, 18, 500)) + 2, l.header.top, l.close.left, l.header.bottom}, raised, Ratio(Over(0xFFFFFF99, raised), raised), "card « · Boutique » 13px #FFFFFF99", 4.5);
            MeasureText(c, k, l.body, raised, Ratio(Over(0xFFFFFFE6, raised), raised), "card body 14px #FFFFFFE6");
        }
        if (cc.kind == CardKind::Working) {
            const float sinceW = ink.Width(cc.since, F(14, 18));
            MeasureText(c, k, {l.close.left - 8 - sinceW, l.header.top, l.close.left - 8, l.header.bottom}, raised, Ratio(Over(0xFFFFFF99, raised), raised), "card « Depuis 2 min » 14px #FFFFFF99");
            MeasureText(c, k, l.projectLabel, raised, Ratio(Over(0xFFFFFF99, raised), raised), "card project 12px #FFFFFF99");
            colour("« Ouvrir » #FFFFFF0F", c, l.open.left + 3, l.open.top + 16, Over(0xFFFFFF0F, raised));
        }
    };
    CardContent working; working.kind = CardKind::Working; working.agent = L"Opus 5.5"; working.monogram = L"C"; working.since = L"Depuis 2 min"; working.project = L"Boutique"; working.note = v.note;
    CardContent noAgent; noAgent.kind = CardKind::NoAgent; noAgent.project = L"Boutique";
    CardContent added; added.kind = CardKind::Added; added.project = L"Boutique";
    card(working, L"12-card-working.png"); card(noAgent, L"13-card-no-agent.png"); card(added, L"14-card-added.png");
    // ---- Onboarding
    auto modal = [&](ModalView mv, const wchar_t* name, bool check) {
        const ModalLayout l = LayoutModal(ink, mv);
        if (!Render(ink, ModalSpec::width, l.height, dpi, Ink_::raised, [&](Ink& i, float, float) { PaintModal(i, l.height, mv, l, 1, nullptr, nullptr, 0); }, c)) return;
        Save(ink, c, name);
        if (!check) return;
        MeasureText(c, k, l.subtitle, raised, Ratio(Over(0xFFFFFFB3, raised), raised), "modal subtitle 14px #FFFFFFB3");
        MeasureText(c, k, l.note, Over(0xFFFFFF05, raised), Ratio(Over(0xFFFFFF99, Over(0xFFFFFF05, raised)), Over(0xFFFFFF05, raised)), "footer note 12px #FFFFFF99");
    };
    ModalView mv; mv.setup = SampleSetup();
    { const ModalLayout l = LayoutModal(ink, mv);
      modal(mv, L"15-onboarding-choose.png", true);
      Render(ink, ModalSpec::width, l.height, dpi, Ink_::raised, [&](Ink& i, float, float) { PaintModal(i, l.height, mv, l, 1, nullptr, nullptr, 0); }, c);
      colour("chosen option #3E67FF1A", c, l.options[0].right - 60, l.options[0].top + 8, Over(0x3E67FF1A, raised));
      colour("other option #FFFFFF08", c, l.options[1].right - 60, l.options[1].top + 8, Over(0xFFFFFF08, raised));
      colour("footer #FFFFFF05", c, 30, l.height - 6, Over(0xFFFFFF05, raised));
      const Rgb opt = Over(0xFFFFFF08, raised);
      MeasureText(c, k, {l.options[1].left + 60, l.options[1].top + 34, l.options[1].right - 42, l.options[1].top + 52}, opt, Ratio(Over(0xFFFFFF99, opt), opt), "option description 13px #FFFFFF99");
      float sx = l.steps.left; for (int s = 0; s < 2; ++s) { sx += 18 + 6 + ink.Width(s ? L"Installer" : L"Choisir l'app", F(12, 16, s ? 400 : 500)) + 8 + 24 + 8; }
      MeasureText(c, k, {sx + 24, l.steps.top + 1, sx + 80, l.steps.bottom - 1}, raised, Ratio(Over(0xFFFFFF99, raised), raised), "todo step label 12px #FFFFFF99"); }
    { ModalView x = mv; x.step = OnboardingStep::Install; modal(x, L"16-onboarding-claude.png", false); }
    { ModalView x = mv; x.step = OnboardingStep::Install; x.manual = true; x.copied = MCopyBlock; const ModalLayout l = LayoutModal(ink, x); modal(x, L"17-onboarding-claude-manual.png", false);
      Render(ink, ModalSpec::width, l.height, dpi, Ink_::raised, [&](Ink& i, float, float) { PaintModal(i, l.height, x, l, 1, nullptr, nullptr, 0); }, c);
      colour("code block #171717", c, l.block.right - 4, l.block.bottom - 4, canvas);
      MeasureText(c, k, {l.blockText.left, l.blockText.top, l.blockText.right, l.blockText.top + 18}, canvas, Ratio(Over(0xFFFFFFCC, canvas), canvas), "config mono 12px #FFFFFFCC");
      MeasureText(c, k, l.fileLabel, Over(0xFFFFFF08, raised), Ratio(Over(0xFFFFFF99, Over(0xFFFFFF08, raised)), Over(0xFFFFFF08, raised)), ".mcpb label 12px #FFFFFF99"); }
    { ModalView x = mv; x.step = OnboardingStep::Install; x.app = AgentApp::Codex; modal(x, L"18-onboarding-codex.png", false);
      x.manual = true; modal(x, L"18b-onboarding-codex-toml.png", false); }
    { ModalView x = mv; x.step = OnboardingStep::Install; x.app = AgentApp::Other; modal(x, L"19-onboarding-other.png", false); }
    { ModalView x = mv; x.step = OnboardingStep::Verify; x.spinner = 60; modal(x, L"20-onboarding-verify-waiting.png", false);
      x.help = true; const ModalLayout l = LayoutModal(ink, x); modal(x, L"21-onboarding-verify-help.png", false);
      Render(ink, ModalSpec::width, l.height, dpi, Ink_::raised, [&](Ink& i, float, float) { PaintModal(i, l.height, x, l, 1, nullptr, nullptr, 0); }, c);
      const Rgb st = Over(0xFFFFFF0A, Over(0xFFFFFF08, raised));
      MeasureText(c, k, l.help, st, Ratio(Solid(0x9DB3FF), st), "« Ça ne marche pas ? » #9DB3FF");
      MeasureText(c, k, {l.status.left + 42, l.status.top + 11, l.status.left + 200, l.status.bottom - 11}, st, Ratio(Over(0xFFFFFFB3, st), st), "waiting status 13px #FFFFFFB3"); }
    { ModalView x = mv; x.step = OnboardingStep::Verify; x.status.read = true; x.status.reader = L"Claude"; x.status.detail = L"1 ouverte";
      x.status.agents = {{L"C", L"Claude Desktop", L"Projet Boutique · lu il y a quelques secondes", true}, {L"Cx", L"Codex", L"Non connecté", false}};
      const ModalLayout l = LayoutModal(ink, x); modal(x, L"22-onboarding-verify-connected.png", false);
      Render(ink, ModalSpec::width, l.height, dpi, Ink_::raised, [&](Ink& i, float, float) { PaintModal(i, l.height, x, l, 1, nullptr, nullptr, 0); }, c);
      const Rgb st = Over(0x3E67FF1F, Over(0xFFFFFF08, raised));
      colour("read status #3E67FF1F", c, l.status.right - 4, l.status.top + 4, st);
      MeasureText(c, k, {l.status.left + 230, l.status.top + 11, l.status.right - 12, l.status.bottom - 11}, st, Ratio(Over(0xFFFFFFB3, st), st), "read detail 13px #FFFFFFB3 on accent 12 %");
      MeasureText(c, k, {l.agentRows[0].left + 40, (l.agentRows[0].top + l.agentRows[0].bottom) / 2 + 2, l.agentRows[0].left + 250, (l.agentRows[0].top + l.agentRows[0].bottom) / 2 + 18}, raised, Ratio(Over(0xFFFFFF99, raised), raised), "agent detail 12px #FFFFFF99"); }
    // ---- Scenes (1180 x 815, Paper artboard size)
    const float W = 1180, H = 815, pageTop = 117;
    { NoteView x = v; const NoteLayout l = LayoutNote(ink, x);
      if (Render(ink, W, H, 96, Ink_::canvas, [&](Ink& i, float, float) {
            PaintChrome(i, W); i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(0, 85)); PaintBanner(i, W, 32, BannerVisual{}); i.Target()->SetTransform(D2D1::IdentityMatrix());
            PaintPage(i, 0, pageTop, W, H - pageTop, true);
            i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(324, pageTop + 96)); PaintNote(i, x, l); i.Target()->SetTransform(D2D1::IdentityMatrix()); }, c)) Save(ink, c, L"30-scene-v2-note.png"); }
    { const CardLayout l = LayoutCard(ink, working); LauncherVisual lv; lv.state = LauncherState::Working; lv.count = 1; lv.spinner = 30; CardVisual cv; cv.spinner = 30;
      if (Render(ink, W, H, 96, Ink_::canvas, [&](Ink& i, float, float) {
            PaintChrome(i, W); i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(0, 85)); PaintBanner(i, W, 32, BannerVisual{}); i.Target()->SetTransform(D2D1::IdentityMatrix());
            PaintPage(i, 0, pageTop, W, H - pageTop, true);
            i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(W - 16 - 320, H - 64 - l.height)); PaintCard(i, working, l, cv);
            i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(W - 16 - 40, H - 16 - 46)); PaintLauncher(i, lv); i.Target()->SetTransform(D2D1::IdentityMatrix()); }, c)) Save(ink, c, L"31-scene-v5-working.png"); }
    { const CardLayout l = LayoutCard(ink, noAgent); LauncherVisual lv;
      if (Render(ink, W, H, 96, Ink_::canvas, [&](Ink& i, float, float) {
            PaintChrome(i, W); i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(0, 85)); PaintBanner(i, W, 32, BannerVisual{}); i.Target()->SetTransform(D2D1::IdentityMatrix());
            PaintPage(i, 0, pageTop, W, H - pageTop, false);
            i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(W - 16 - 320, H - 64 - l.height)); PaintCard(i, noAgent, l, CardVisual{});
            i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(W - 16 - 40, H - 16 - 46)); PaintLauncher(i, lv); i.Target()->SetTransform(D2D1::IdentityMatrix()); }, c)) Save(ink, c, L"32-scene-onboarding-1-no-agent.png"); }
    for (int s = 0; s < 3; ++s) {
        ModalView x = mv; if (s == 1) x.step = OnboardingStep::Install; if (s == 2) { x.step = OnboardingStep::Verify; x.status.read = true; x.status.reader = L"Claude"; x.status.detail = L"1 ouverte"; x.status.agents = {{L"C", L"Claude Desktop", L"Projet Boutique · lu il y a quelques secondes", true}, {L"Cx", L"Codex", L"Non connecté", false}}; }
        const ModalLayout l = LayoutModal(ink, x);
        if (Render(ink, W, H, 96, Ink_::canvas, [&](Ink& i, float, float) {
              PaintChrome(i, W); i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(0, 85)); PaintBanner(i, W, 32, BannerVisual{}); i.Target()->SetTransform(D2D1::IdentityMatrix());
              PaintPage(i, 0, pageTop, W, H - pageTop, false); i.Fill({0, 0, W, H}, 0, {0, 0, 0, Spec::veilOpacity});
              i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(310, 128)); PaintModal(i, l.height, x, l, 1, nullptr, nullptr, 0); i.Target()->SetTransform(D2D1::IdentityMatrix()); }, c)) {
            Save(ink, c, s == 0 ? L"33-scene-onboarding-2-choose.png" : s == 1 ? L"34-scene-onboarding-3a-claude.png" : L"35-scene-onboarding-4-connected.png");
            if (s == 0) colour("veil #0000008C over the page", c, 1100 / 2.f, 790 / 2.f, Over(0x0000008C, Solid(0xF4F4F3)));
        }
    }
    report << L"\"paperColours\":[" << colours.str() << L"{}],";
}

// Motion strips: frames of the real paint path at t = 0, 1/4, 1/2, end, + interruption + reduced.
static void MotionStrips(Ink& ink) {
    Presence enter; enter.Show(At(0)); const double settle = SettleMs(enter, 0);
    const double times[4] = {0, settle / 4, settle / 2, settle};
    CardContent cc; cc.kind = CardKind::NoAgent; cc.project = L"Boutique"; const CardLayout cl = LayoutCard(ink, cc);
    const float cellW = 360, cellH = cl.height + 40 + 56;
    auto cardFrame = [&](Ink& i, float ox, Pose p) {
        auto* t = i.Target();
        LauncherVisual lv; i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(ox + 20 + 320 - 40, cl.height + 40 + 8 - LauncherSpec::badgeOut)); PaintLauncher(i, lv);
        const D2D1_POINT_2F origin{320 - 20, cl.height};
        t->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale, origin) * D2D1::Matrix3x2F::Translation(ox + 20 + p.dx, 30 + p.dy));
        ID2D1Layer* g = nullptr; const bool pushed = i.PushOpacity(p.opacity, g); PaintCard(i, cc, cl, CardVisual{}); if (pushed) i.PopOpacity(g);
        t->SetTransform(D2D1::IdentityMatrix());
    };
    Canvas c;
    auto strip = [&](const wchar_t* name, const std::vector<Pose>& poses) {
        if (RenderOffscreen(ink, cellW * poses.size(), cellH, 96, 0xF4F4F3FF, [&](Ink& i, float, float) { for (size_t n = 0; n < poses.size(); ++n) cardFrame(i, cellW * n, poses[n]); }, c)) Save(ink, c, name);
    };
    std::vector<Pose> poses; for (double t : times) poses.push_back(enter.Sample(At(t), false, 0, Spec::panelLift));
    strip(L"40-motion-card-enter-0-quarter-half-end.png", poses);
    Presence exit; exit.Show(At(0), false); exit.Hide(At(0)); const double es = SettleMs(exit, 0);
    poses.clear(); for (double t : {0.0, es / 4, es / 2, es}) poses.push_back(exit.Sample(At(t), false, 0, Spec::panelLift));
    strip(L"41-motion-card-exit-0-quarter-half-end.png", poses);
    Presence it; it.Show(At(0)); const double mid = settle / 2; poses.clear();
    poses.push_back(it.Sample(At(mid - 16), false, 0, Spec::panelLift)); poses.push_back(it.Sample(At(mid), false, 0, Spec::panelLift)); it.Hide(At(mid));
    poses.push_back(it.Sample(At(mid), false, 0, Spec::panelLift)); poses.push_back(it.Sample(At(mid + 16), false, 0, Spec::panelLift)); poses.push_back(it.Sample(At(mid + 60), false, 0, Spec::panelLift));
    strip(L"42-motion-card-interrupt-mid.png", poses);
    Presence red; red.Show(At(0)); poses.clear(); for (double t : times) poses.push_back(red.Sample(At(t), true, 0, Spec::panelLift));
    strip(L"43-motion-card-reduced.png", poses);
    // Note popover: born from the target above it (origin at the top edge, lift -8).
    NoteView v = SampleNote(); v.expanded = false; const NoteLayout nl = LayoutNote(ink, v);
    if (RenderOffscreen(ink, 380 * 4, nl.height + 80, 96, 0xF4F4F3FF, [&](Ink& i, float, float) {
            for (int n = 0; n < 4; ++n) {
                const Pose p = enter.Sample(At(times[n]), false, 0, -Spec::popoverLift); auto* t = i.Target();
                const float ox = 380.f * n + 20; t->SetTransform(D2D1::IdentityMatrix());
                i.Fill({ox + 40, 8, ox + 280, 52}, 8, Hex(0x1C1C1CFF)); i.Stroke(Inflate({ox + 40, 8, ox + 280, 52}, 3), 10, Accent(), 2);
                t->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale, {160, 0}) * D2D1::Matrix3x2F::Translation(ox + p.dx, 64 + p.dy));
                ID2D1Layer* g = nullptr; const bool pushed = i.PushOpacity(p.opacity, g); PaintNote(i, v, nl); if (pushed) i.PopOpacity(g);
                t->SetTransform(D2D1::IdentityMatrix());
            } }, c)) Save(ink, c, L"44-motion-note-enter-from-target.png");
    // Modal: from the centre, with the veil.
    ModalView mv; mv.setup = SampleSetup(); const ModalLayout ml = LayoutModal(ink, mv);
    if (RenderOffscreen(ink, 600 * 4, ml.height + 40, 96, 0xF4F4F3FF, [&](Ink& i, float, float) {
            for (int n = 0; n < 4; ++n) {
                const Pose p = enter.Sample(At(times[n]), false); auto* t = i.Target(); const float ox = 600.f * n;
                i.Fill({ox, 0, ox + 600, ml.height + 40}, 0, {0, 0, 0, Spec::veilOpacity * p.opacity});
                t->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale, {280, ml.height / 2}) * D2D1::Matrix3x2F::Translation(ox + 20, 20));
                ID2D1Layer* g = nullptr; const bool pushed = i.PushOpacity(p.opacity, g); PaintModal(i, ml.height, mv, ml, 1, nullptr, nullptr, 0); if (pushed) i.PopOpacity(g);
                t->SetTransform(D2D1::IdentityMatrix());
            } }, c)) Save(ink, c, L"45-motion-modal-enter.png");
    // Step cross-fade at its midpoint (Choose -> Install, 100 ms).
    ModalView a = mv, b = mv; b.step = OnboardingStep::Install; const ModalLayout al = LayoutModal(ink, a), bl = LayoutModal(ink, b);
    Crossfade f; f.Reset(0); f.Go(1, At(0)); M::Spring h; h.Snap(al.height); h.Set(bl.height, At(0), Spec::stepHeightResponse);
    const float hh = h.Value(At(100));
    if (RenderOffscreen(ink, 600, std::max(al.height, bl.height) + 40, 96, Ink_::canvas, [&](Ink& i, float, float) {
            i.Target()->SetTransform(D2D1::Matrix3x2F::Translation(20, 20)); PaintModal(i, hh, b, bl, f.Incoming(At(100)), &a, &al, f.Outgoing(At(100))); i.Target()->SetTransform(D2D1::IdentityMatrix()); }, c))
        Save(ink, c, L"46-motion-onboarding-step-crossfade-mid.png");
}

// ---------------------------------------------------------------------------------------
// Live windows
static void Pump(int ms) {
    const DWORD end = GetTickCount() + DWORD(ms); MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}
static bool PumpUntil(const std::function<bool()>& done, int ms) { const DWORD end = GetTickCount() + DWORD(ms); while (GetTickCount() < end) { if (done()) return true; Pump(10); } return done(); }
static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) { RECT r; GetClientRect(h, &r); FillRect(reinterpret_cast<HDC>(w), &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); return 1; }
    return DefWindowProcW(h, m, w, l);
}
static bool TimerHook(const MSG& msg) { return msg.message == WM_TIMER; }
static void SaveLayer(Ink& ink, Layer& layer, const wchar_t* name) {
    auto px = layer.Pixels(); const RECT r = layer.Rect(); Canvas c; c.width = unsigned(r.right - r.left); c.height = unsigned(r.bottom - r.top); c.pixels = std::move(px);
    if (c.width && c.height && c.pixels.size() == size_t(c.width) * c.height) Save(ink, c, name);
}
static void ForceForeground(HWND h) {   // tests only: a console-launched harness is rarely the foreground app
    HWND fg = GetForegroundWindow(); const DWORD other = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, self = GetCurrentThreadId();
    if (other && other != self) AttachThreadInput(other, self, TRUE);
    SetForegroundWindow(h); BringWindowToTop(h); SetActiveWindow(h);
    if (other && other != self) AttachThreadInput(other, self, FALSE);
}
static void WindowTests(Ink& ink, const std::wstring& root) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = inst; wc.lpszClassName = L"AnnotationHarnessHost"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    const UINT sysDpi = GetDpiForSystem(); const float s = sysDpi / 96.f;
    RECT want{0, 0, LONG(1180 * s), LONG(815 * s)}; AdjustWindowRectExForDpi(&want, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    HWND host = CreateWindowExW(0, wc.lpszClassName, L"Annotations harness", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 40, 40, want.right - want.left, want.bottom - want.top, nullptr, nullptr, inst, nullptr);
    CHECK(host != nullptr); if (!host) return;
    ShowWindow(host, SW_SHOW); UpdateWindow(host);
    // Hit tests read the real desktop: keep the host above other apps and take the foreground
    // (the Alt tap is the documented way a background process may call SetForegroundWindow).
    SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ForceForeground(host); Pump(150);
    std::printf("Host foreground: %d%s", GetForegroundWindow() == host ? 1 : 0, "\n");
    const float dpiScale = GetDpiForWindow(host) / 96.f;
    RECT client{}; GetClientRect(host, &client);
    const int pageTop = int(std::lround(117 * dpiScale));
    RECT page{client.left, pageTop, client.right, client.bottom};
    // WebView stand-in: a plain child window under our surfaces.
    HWND web = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_WHITERECT, page.left, page.top, page.right - page.left, page.bottom - page.top, host, nullptr, inst, nullptr);
    CHECK(web != nullptr);
    std::vector<std::wstring> announcements;
    auto announce = [&](const std::wstring& t) { announcements.push_back(t); };

    // ---- Banner
    Banner banner; int exits = 0;
    banner.Init(inst, host, root, {[&] { ++exits; }, announce});
    CHECK(banner.Show({0, int(std::lround(85 * dpiScale)), client.right, int(std::lround(117 * dpiScale))}, false));
    CHECK(banner.Created() && GetParent(banner.Window()) == host && IsWindowVisible(banner.Window()));
    CHECK(!announcements.empty() && announcements.back() == L"Mode Annotation activé");
    CHECK(PumpUntil([&] { return !Animator::Get().Animating(); }, 2000));
    { auto n = banner.NodesForTest(); CHECK(n.size() == 2 && n[1].name == L"Quitter le mode Annotation" && n[1].accelerator == L"Échap" && n[0].status == L"Activé"); }
    SaveLayer(ink, banner.LayerForTest(), L"50-window-banner.png");

    // ---- Launcher: shape-exact hit testing over the WebView
    Launcher launcher; int toggles = 0, dismissedKeyboard = -1, connects = 0, laters = 0;
    LauncherHooks lh; lh.toggle = [&] { ++toggles; }; lh.connect = [&] { ++connects; }; lh.later = [&] { ++laters; };
    lh.dismissed = [&](bool k) { dismissedKeyboard = k ? 1 : 0; }; lh.announce = announce;
    launcher.Init(inst, host, root, lh); launcher.Place(page);
    CHECK(launcher.Show(LauncherState::Rest, 0, false));
    CHECK(PumpUntil([&] { return !Animator::Get().Animating(); }, 2000));
    {
        RECT lw{}; GetWindowRect(launcher.LauncherWindow(), &lw);
        const int b6 = int(std::lround(6 * dpiScale));
        POINT center{(lw.left + lw.right - b6) / 2, (lw.top + b6 + lw.bottom) / 2};
        POINT cornerOut{lw.right - 2, lw.top + 2};                       // badge overhang area, no badge at rest
        POINT roundCorner{lw.left + 1, lw.bottom - 1};                    // outside the 10 DIP rounded corner
        CHECK(WindowFromPoint(center) == launcher.LauncherWindow());
        CHECKM(WindowFromPoint(cornerOut) != launcher.LauncherWindow(), "transparent overhang must not take the click");
        CHECKM(WindowFromPoint(roundCorner) != launcher.LauncherWindow(), "rounded corner pixel must not take the click");
        // UIA name + keyboard.
        auto n = launcher.ButtonNodesForTest(); CHECK(n.size() == 1 && n[0].name == L"Annotations" && n[0].expand == 1);
        SendMessageW(launcher.LauncherWindow(), WM_KEYDOWN, VK_RETURN, 0); CHECK(toggles == 1);
        SaveLayer(ink, launcher.ButtonLayerForTest(), L"51-window-launcher-rest.png");
        launcher.SetStateForTest(LauncherState::Working, 1);
        POINT badge{lw.right - int(std::lround(9 * dpiScale)), lw.top + int(std::lround(9 * dpiScale))};
        CHECKM(WindowFromPoint(badge) == launcher.LauncherWindow(), "badge pixels are part of the launcher");
        SaveLayer(ink, launcher.ButtonLayerForTest(), L"52-window-launcher-working.png");
        launcher.SetStateForTest(LauncherState::Rest, 0);
    }
    // ---- Card: born from the launcher, then zero wake at rest.
    CardContent noAgent; noAgent.kind = CardKind::NoAgent; noAgent.project = L"Boutique";
    const auto framesBefore = Animator::Get().Frames();
    launcher.ShowCard(noAgent);
    CHECK(!announcements.empty() && announcements.back().find(L"Annotation ajoutée · Boutique") == 0);
    // mid-flight capture of the real layered surface
    Pump(110); SaveLayer(ink, launcher.CardLayerForTest(), L"53-window-card-entering.png");
    const DWORD settleStart = GetTickCount();
    CHECK(PumpUntil([&] { return !Animator::Get().Animating(); }, 3000));
    const DWORD settleMs = GetTickCount() - settleStart + 110;
    const auto framesDuring = Animator::Get().Frames() - framesBefore;
    std::printf("Card enter in a real window: %lu ms, %llu frames, clock %s\n", settleMs, framesDuring, Animator::Get().ClockRunning() ? "compositor/DWM" : "fallback timer");
    CHECK(framesDuring >= 5);
    CHECK(Animator::Get().ClockRunning() || Animator::Get().FallbackStarts() > 0);
    SaveLayer(ink, launcher.CardLayerForTest(), L"54-window-card-no-agent.png");
    // ZERO wake-ups at rest: nothing moves -> no frame, no request, no clock wake, no WM_TIMER.
    {
        const auto* st = Animator::Get().Stats();
        const auto frames = Animator::Get().Frames(); const auto requests = st ? st->requests.load() : 0, wakes = st ? st->wakes.load() : 0, posted = st ? st->posted.load() : 0, ticks = st ? st->ticks.load() : 0;
        unsigned timers = 0, foreignTimers = 0; const DWORD end = GetTickCount() + 1500; MSG msg;
        while (GetTickCount() < end) { while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { if (TimerHook(msg)) { wchar_t cls[64] = L""; GetClassNameW(msg.hwnd, cls, 64); std::printf("  WM_TIMER hwnd %p class %ls id %llu%s", (void*)msg.hwnd, cls, (unsigned long long)msg.wParam, "\n"); if (!wcscmp(cls, L"BrowserAnnotationLayer") || !wcscmp(cls, L"BrowserAnnotationAnimator") || !wcscmp(cls, L"BrowserAnnotationNote")) ++timers; else ++foreignTimers; } TranslateMessage(&msg); DispatchMessageW(&msg); } MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT); }
        const auto frames2 = Animator::Get().Frames(); const auto requests2 = st ? st->requests.load() : 0, wakes2 = st ? st->wakes.load() : 0, posted2 = st ? st->posted.load() : 0, ticks2 = st ? st->ticks.load() : 0;
        std::printf("Idle 1500 ms: frames %llu->%llu, requests %llu->%llu, clock wakes %llu->%llu, ticks %llu->%llu, posted %llu->%llu, WM_TIMER %u, fallback armed %d\n",
            frames, frames2, requests, requests2, wakes, wakes2, ticks, ticks2, posted, posted2, timers, Animator::Get().FallbackTimerArmed());
        CHECK(frames2 == frames && requests2 == requests && wakes2 == wakes && posted2 == posted && ticks2 == ticks);
        CHECK(timers == 0 && !Animator::Get().FallbackTimerArmed() && !Animator::Get().Animating());
        report << L"\"idle\":{\"ms\":1500,\"frames\":" << (frames2 - frames) << L",\"clockRequests\":" << (requests2 - requests) << L",\"clockWakes\":" << (wakes2 - wakes)
               << L",\"clockTicks\":" << (ticks2 - ticks) << L",\"posted\":" << (posted2 - posted) << L",\"wmTimer\":" << timers << L",\"foreignWmTimer\":" << foreignTimers << L",\"cardEnterFrames\":" << framesDuring << L",\"cardEnterMs\":" << settleMs << L"},";
    }
    // Card keyboard path and UIA.
    { auto n = launcher.CardNodesForTest(); bool later = false, connect = false, close = false; for (auto& x : n) { later |= x.name == L"Plus tard"; connect |= x.name == L"Connecter un agent"; close |= x.name == L"Fermer"; } CHECK(later && connect && close); }
    launcher.FocusCard(); CHECK(GetFocus() == launcher.CardWindow());
    SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_TAB, 0); SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(connects == 1);
    SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK(!launcher.CardVisible() && dismissedKeyboard == 1 && GetFocus() == launcher.LauncherWindow());
    PumpUntil([&] { return !Animator::Get().Animating(); }, 2000);
    CHECK(!IsWindowVisible(launcher.CardWindow()));
    // « Annotation ajoutée »: one-shot dwell, no action, leaves by itself.
    CardContent added; added.kind = CardKind::Added; added.project = L"Boutique";
    launcher.ShowCard(added); CHECK(launcher.DwellArmedForTest());
    SendMessageW(launcher.CardWindow(), WM_TIMER, 7, 0);
    CHECK(!launcher.CardVisible() && dismissedKeyboard == 0 && !launcher.DwellArmedForTest());
    PumpUntil([&] { return !Animator::Get().Animating(); }, 2000);

    // ---- Note popover: born from the target, EDIT focused at once, submit / fail / retry / cancel.
    NotePopover note; std::vector<NoteSubmission> submits; std::wstring cancelled; int cancelledKeyboard = -1; std::wstring changed;
    NoteHooks nh; nh.submit = [&](const NoteSubmission& x) { submits.push_back(x); }; nh.cancel = [&](const std::wstring& d, bool k) { cancelled = d; cancelledKeyboard = k; };
    nh.projectChanged = [&](const std::wstring& id) { changed = id; }; nh.announce = announce;
    note.Init(inst, host, root, nh);
    POINT origin{0, 0}; ClientToScreen(host, &origin);
    RECT target{origin.x + LONG(64 * dpiScale), origin.y + LONG((117 + 40) * dpiScale), origin.x + LONG(304 * dpiScale), origin.y + LONG((117 + 84) * dpiScale)};
    RECT bounds{origin.x + page.left, origin.y + page.top, origin.x + page.right, origin.y + page.bottom};
    NoteRequest req; req.projects = SampleProjects(); req.projectId = L"boutique"; req.capture = CaptureState::Pending;
    req.context = {L"localhost:5173/checkout", L"form > button.btn-primary", L"« Payer 48,00 € »", L"display: inline-flex\njustify-content: center", L"Recadrée · 960×420"};
    CHECK(note.Show(req, target, bounds, false, false));
    ForceForeground(note.Window());
    CHECK(GetFocus() == note.Edit());                                     // keystrokes accepted from frame 0
    { BYTE a = 255; DWORD flags = 0; GetLayeredWindowAttributes(note.Window(), nullptr, &a, &flags); CHECK(a == 0); }   // the ghost shows the motion
    CHECKM(note.LiftYForTest() < 0 && note.OriginForTest().y == 0, "lift %.1f origin %.1f", note.LiftYForTest(), note.OriginForTest().y);       // below the target: born from its top edge
    Pump(100); CHECK(note.GhostForTest().Shown()); SaveLayer(ink, note.GhostForTest(), L"55-window-note-ghost-entering.png");
    CHECK(PumpUntil([&] { return note.SettledForTest(); }, 3000));
    { BYTE a = 0; DWORD flags = 0; GetLayeredWindowAttributes(note.Window(), nullptr, &a, &flags); CHECK(a == 255); CHECK(!note.GhostForTest().Shown()); }
    Shot shot = SampleShot(); note.SetShot(CaptureState::Ready, &shot);
    ForceForeground(note.Window()); Pump(20); CHECK(GetFocus() == note.Edit());   // reactivation restores the field
    // Tab order: field -> project -> toggle -> info -> include -> cancel -> add -> field.
    std::vector<int> order; for (int i = 0; i < 7; ++i) { note.KeyForTest(VK_TAB, i == 0); order.push_back(note.FocusForTest()); }
    for (int o : order) std::printf("order %d\n", o); std::printf("visible %d\n", note.Visible());
    CHECKM((order == std::vector<int>{NoteProject, NoteToggle, NoteInfo, NoteInclude, NoteCancel, NoteAdd, NoteField}), "order %d %d %d %d %d %d %d", order[0], order[1], order[2], order[3], order[4], order[5], order[6]);
    CHECK(GetFocus() == note.Edit());
    // Project list by keyboard.
    note.KeyForTest(VK_TAB, true); note.KeyForTest(VK_DOWN, false); CHECK(note.ListForTest().Shown());
    { auto n = note.ListNodesForTest(); CHECK(n.size() == 5 && n[1].selected && n[4].name == L"Nouveau projet pour cette origine…"); }
    note.KeyForTest(VK_DOWN, false); note.KeyForTest(VK_RETURN, false); CHECK(changed == L"docs" && !note.ListForTest().Shown());
    // Validation: empty note -> inline error, focus back in the field.
    note.TypeForTest(L""); note.KeyForTest(VK_RETURN, true);
    CHECKM(submits.empty() && note.ViewForTest().fieldError && !note.ViewForTest().message.empty() && GetFocus() == note.Edit(), "submits %zu error %d message %d focus-edit %d status %d visible %d", submits.size(), note.ViewForTest().fieldError, int(note.ViewForTest().message.size()), GetFocus() == note.Edit(), int(note.ViewForTest().status), note.Visible());
    note.TypeForTest(L"Le bouton devrait être aligné à droite.");
    CHECK(!note.ViewForTest().fieldError);
    note.KeyForTest(VK_RETURN, true);
    CHECK(submits.size() == 1 && submits[0].note == L"Le bouton devrait être aligné à droite." && submits[0].projectId == L"docs" && submits[0].includeShot);
    CHECK(note.ViewForTest().status == NoteStatus::Saving);
    note.Failed();
    CHECK(note.ViewForTest().status == NoteStatus::Failed && AddLabel(note.ViewForTest().status) == L"Réessayer" && note.FocusForTest() == NoteAdd);
    { auto n = note.NodesForTest(); bool retry = false; for (auto& x : n) retry |= x.name == L"Réessayer" && x.accelerator == L"Entrée"; CHECK(retry); CHECK(n[0].status.find(L"Impossible") == 0); }
    Pump(50); { RECT wr; GetWindowRect(note.Window(), &wr); (void)wr; }
    note.KeyForTest(VK_RETURN, false); CHECK(submits.size() == 2);        // « Réessayer » with Entrée
    note.Saved(); CHECK(!note.Visible());
    CHECK(PumpUntil([&] { return !Animator::Get().Animating(); }, 2000)); CHECK(!IsWindowVisible(note.Window()) && !note.GhostForTest().Shown());
    // Échap keeps the draft for « Reprendre la note ».
    { NoteRequest tall = req; tall.draft = L"1"; for (int n = 2; n <= 6; ++n) { tall.draft += wchar_t(13); tall.draft += wchar_t(10); tall.draft += std::to_wstring(n); } CHECK(note.Show(tall, target, bounds, false, true)); CHECK(note.ViewForTest().fieldHeight > NoteSpec::fieldMin); note.Close(false); }
    req.draft = L"Brouillon gardé"; CHECK(note.Show(req, target, bounds, true, true));   // reduced motion: alpha only, no ghost
    CHECK(!note.GhostForTest().Shown());
    PumpUntil([&] { return note.SettledForTest(); }, 2000);
    if (note.ViewForTest().tooltip) { note.KeyForTest(VK_ESCAPE, true); CHECK(!note.ViewForTest().tooltip && note.Visible()); }   // first Échap closes the tip only
    note.KeyForTest(VK_ESCAPE, true);
    CHECKM(cancelled == L"Brouillon gardé" && cancelledKeyboard == 1 && !note.Visible(), "cancelled [%ls] keyboard %d visible %d tooltip %d list %d status %d", cancelled.c_str(), cancelledKeyboard, note.Visible(), note.ViewForTest().tooltip, note.ViewForTest().listOpen, int(note.ViewForTest().status));
    PumpUntil([&] { return !Animator::Get().Animating(); }, 2000);
    { auto n = note.NodesForTest(); CHECK(n.empty()); }

    // ---- Onboarding: veil + modal, keyboard all the way, no app launched, no config written.
    Onboarding onboarding; std::vector<std::wstring> copies; int started = 0, stopped = 0, closedCompleted = -1, opens = 0;
    OnboardingHooks oh; oh.copy = [&](const std::wstring& t) { copies.push_back(t); }; oh.verifyStarted = [&](AgentApp) { ++started; }; oh.verifyStopped = [&](AgentApp) { ++stopped; };
    oh.closed = [&](bool c) { closedCompleted = c; }; oh.openMcpb = [&] { ++opens; }; oh.announce = announce;
    onboarding.Init(inst, host, root, oh);
    RECT hostClient = client; MapWindowPoints(host, nullptr, reinterpret_cast<POINT*>(&hostClient), 2);
    CHECK(onboarding.Show(SampleSetup(), hostClient, false));
    CHECK(!IsWindowEnabled(host) && IsWindowVisible(onboarding.Veil()));
    CHECK(PumpUntil([&] { return !Animator::Get().Animating(); }, 3000));
    { BYTE a = 0; DWORD flags = 0; GetLayeredWindowAttributes(onboarding.Veil(), nullptr, &a, &flags); CHECK(a == BYTE(std::lround(255 * Spec::veilOpacity))); }
    SaveLayer(ink, onboarding.LayerForTest(), L"56-window-onboarding-choose.png");
    { auto n = onboarding.NodesForTest(); CHECK(n[0].name == L"Connecter un agent"); int items = 0; for (auto& x : n) if (x.role == Accessibility::Role::ListItem) ++items; CHECK(items == 3); }
    CHECK(onboarding.ViewForTest().focus == MOption);
    onboarding.KeyForTest(VK_DOWN); CHECK(onboarding.ViewForTest().app == AgentApp::Codex);
    onboarding.KeyForTest(VK_RETURN); CHECK(onboarding.ViewForTest().step == OnboardingStep::Install);
    Pump(100); SaveLayer(ink, onboarding.LayerForTest(), L"57-window-onboarding-crossfade.png");
    CHECK(onboarding.ViewForTest().focus == MCopyCommand);
    onboarding.KeyForTest(VK_RETURN); CHECK(!copies.empty() && copies.back() == SampleSetup().bridgePath);
    onboarding.ActivateForTest(MDisclosure); onboarding.ActivateForTest(MCopyBlock);
    CHECK(copies.back() == CodexConfig(SampleSetup().bridgePath, L"boutique"));
    onboarding.ActivateForTest(MPrimary); CHECK(onboarding.ViewForTest().step == OnboardingStep::Verify && started == 1);
    onboarding.HelpTimeoutForTest(); CHECK(onboarding.ViewForTest().help);
    VerifyStatus st; st.read = true; st.reader = L"Codex"; st.detail = L"1 ouverte"; st.agents = {{L"Cx", L"Codex", L"Projet Boutique · lu il y a quelques secondes", true}};
    onboarding.SetStatus(st);
    CHECK(onboarding.ViewForTest().Title() == L"Codex est connecté");
    CHECK(!announcements.empty() && announcements.back() == L"Codex a lu vos annotations");
    PumpUntil([&] { return !Animator::Get().Animating(); }, 3000);
    SaveLayer(ink, onboarding.LayerForTest(), L"58-window-onboarding-connected.png");
    onboarding.KeyForTest(VK_ESCAPE);
    CHECK(closedCompleted == 0 && stopped == 1 && IsWindowEnabled(host) && opens == 0);
    PumpUntil([&] { return !Animator::Get().Animating(); }, 2000);
    CHECK(!IsWindowVisible(onboarding.Window()) && !IsWindowVisible(onboarding.Veil()));

    // ---- Banner exit + launcher hide, then final idle check.
    banner.Hide(); launcher.Hide();
    CHECK(PumpUntil([&] { return !Animator::Get().Animating(); }, 2000));
    CHECK(!IsWindowVisible(banner.Window()) && !IsWindowVisible(launcher.LauncherWindow()));
    report << L"\"announcements\":[";
    for (auto& a : announcements) report << L"\"" << a << L"\",";
    report << L"\"\"],";
    note.Dispose(); DestroyWindow(host);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: annotations-ui-test.exe <repoRoot> <outDir> [--no-window]\n"); return 2; }
    const std::wstring root = argv[1]; outDir = argv[2];
    const bool windows = !(argc > 3 && std::wstring(argv[3]) == L"--no-window");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CreateDirectoryW(outDir.c_str(), nullptr);
    report << L"{";
    ZeroCostTests();
    MotionTests();
    ConfigTests();
    Ink* ink = Ink::Shared(root);
    CHECK(ink != nullptr); if (!ink) { std::fprintf(stderr, "Ink init failed (fonts under %ls\\assets\\fonts?)\n", root.c_str()); return 1; }
    std::printf("Monospace family: %s\n", Narrow(ink->MonoFamily()).c_str());
    report << L"\"monoFamily\":\"" << ink->MonoFamily() << L"\",";
    GeometryTests(*ink);
    Captures(*ink);
    MotionStrips(*ink);
    if (windows) WindowTests(*ink, root);
    // Contrast table.
    std::printf("\nContrast (measured on 2x renders / token pair):\n");
    report << L"\"contrast\":[";
    for (auto& c : contrasts) {
        std::printf("  %-48s %6.2f:1  (tokens %5.2f:1)  fg %s on %s\n", c.name.c_str(), c.measured, c.expected, c.fg.c_str(), c.bg.c_str());
        report << L"{\"what\":\"" << Wide(c.name) << L"\",\"measured\":" << c.measured << L",\"tokens\":" << c.expected << L",\"fg\":\"" << Wide(c.fg) << L"\",\"bg\":\"" << Wide(c.bg) << L"\"},";
    }
    report << L"{}],\"captures\":[";
    for (auto& c : captures) report << L"\"" << c << L"\",";
    report << L"\"\"],\"checks\":" << checks << L",\"failures\":" << failures << L"}";
    {   // UTF-8 report (names and labels are French)
        const std::wstring w = report.str(); const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
        std::string u(size_t(n), 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), u.data(), n, nullptr, nullptr);
        std::ofstream f((outDir + L"/report.json").c_str(), std::ios::binary); f << u; }
    std::printf("\n%d checks, %d failures; %zu captures in %ls\n", checks, failures, captures.size(), outDir.c_str());
    return failures ? 1 : 0;
}
