// Native harness for the annotation-mode UI (V2): no WebView2, no network, no disk I/O in the
// components (the harness itself writes PNG captures and a JSON report).
// Usage: annotations-ui-test.exe <repoRoot> <outDir> [--no-window]
//   1. zero cost before first use (no Ink, no Animator thread, no window)
//   2. pure motion samples (modal / note / card / panel enter-exit, interruption, reduced motion,
//      state cross-fade, fold, press)
//   3. configuration texts, Paper geometry (box-model heights, widths), Paper colours sampled in
//      the renders, measured contrast of every text style
//   4. one PNG per state and per planche (+ scenes and motion strips) in <outDir>
//   5. live windows: hit-testing through transparent pixels, keyboard paths, hooks, UIA nodes,
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
static std::string Utf8(const std::wstring& w) { const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr); std::string s(size_t(n), 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr); return s; }

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
static const Rgb canvasRgb = Solid(0x171717), raisedRgb = Solid(0x292929), accentRgb = Solid(0x3E67FF), pageRgb = Solid(0xF4F4F3);
struct Contrast { std::string name; double measured, expected; std::string fg, bg; };
static std::vector<Contrast> contrasts;
// Rendered text contrast: the pixel of the text rect that differs most from the background.
static void MeasureText(const Canvas& c, float scale, D2D1_RECT_F box, Rgb bg, std::uint32_t fgToken, const char* name, double minimum = 4.5) {
    double best = 1; Rgb fg = bg;
    for (int y = int(box.top * scale); y < int(box.bottom * scale); ++y) for (int x = int(box.left * scale); x < int(box.right * scale); ++x) {
        const Rgb p = Px(c.At(x, y)); const double r = Ratio(p, bg); if (r > best) { best = r; fg = p; }
    }
    const double expected = Ratio(Over(fgToken, bg), bg);
    contrasts.push_back({name, best, expected, Hex6(fg), Hex6(bg)});
    CHECKM(best >= minimum - .05, "%s measured %.2f:1 < %.1f", name, best, minimum);
    CHECKM(expected >= minimum, "%s token pair %.2f:1 < %.1f", name, expected, minimum);
}
struct Sampled { std::string what, paper, rendered; };
static std::vector<Sampled> sampled;
static void Sample(const Canvas& c, float scale, float x, float y, Rgb expected, const char* what, double tol = 2.5) {
    const Rgb p = Px(c.At(int(x * scale), int(y * scale)));
    sampled.push_back({what, Hex6(expected), Hex6(p)});
    CHECKM(Close(p, expected, tol), "%s: rendered %s, Paper %s", what, Hex6(p).c_str(), Hex6(expected).c_str());
}
struct Geometry { std::string surface; float width, paper, native; };
static std::vector<Geometry> geometry;
static void Geo(const char* surface, float width, float paper, float native, float tol = 1.5f) {
    geometry.push_back({surface, width, paper, native});
    CHECKM(Near(paper, native, tol), "%s: Paper %.2f, native %.2f", surface, paper, native);
}

// ---------------------------------------------------------------------------------------
// Fixtures
static ConnectSetup SampleSetup() {
    ConnectSetup s; s.command = L"C:\\Users\\vous\\AppData\\Local\\browser\\bin\\browser-annotations-mcp.exe";
    s.args = {L"--store", L"C:\\Users\\vous\\AppData\\Local\\browser\\annotations"}; return s;
}
static ConnectView ConnectState_(ConnectSetup s) { ConnectView v; v.setup = std::move(s); return v; }

static bool Save(Ink& ink, const Canvas& c, const wchar_t* name) {
    const std::wstring path = outDir + L"\\" + name; const HRESULT hr = SavePng(ink, c, path);
    CHECKM(SUCCEEDED(hr), "png %ls", name); if (SUCCEEDED(hr)) captures.push_back(name); return SUCCEEDED(hr);
}
static bool Render(Ink& ink, float w, float h, UINT dpi, std::uint32_t backdrop, const std::function<void(Ink&, float, float)>& paint, Canvas& out) {
    const bool ok = RenderOffscreen(ink, w, h, dpi, backdrop, paint, out); CHECK(ok); return ok;
}
template<class F> static void At_(Ink& ink, float x, float y, F&& f) {
    auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
    t->SetTransform(D2D1::Matrix3x2F::Translation(x, y) * base); f(); t->SetTransform(base);
}
// Mock page (Paper « Contenu web — Boutique »).
static void PaintPage(Ink& ink, float x, float y, float w, float h, bool selection) {
    ink.Fill({x, y, x + w, y + h}, 0, Hex(0xF4F4F3FF));
    ink.Fill({x, y, x + w, y + 56}, 0, Hex(0xFFFFFFFF)); ink.Fill({x, y + 56, x + w, y + 57}, 0, Hex(0xE2E2E0FF));
    ink.Text(L"Boutique", F(17, 24, 500, -.02f), {x + 64, y + 16, x + 300, y + 40}, Hex(0x1C1C1CFF));
    ink.Text(L"Panier · 2 articles", F(14, 20), {x + w - 264, y + 18, x + w - 64, y + 38}, Hex(0x6B6B6BFF), false, Align::Trailing);
    ink.Text(L"Paiement", F(28, 34, 500, -.02f), {x + 64, y + 97, x + 400, y + 131}, Hex(0x1C1C1CFF));
    for (int i = 0; i < 2; ++i) {
        const float fy = y + 151 + i * 88;
        ink.Text(i ? L"Carte" : L"Adresse e-mail", F(13, 18), {x + 64, fy, x + 400, fy + 18}, Hex(0x6B6B6BFF));
        ink.Fill({x + 64, fy + 24, x + 584, fy + 68}, 8, Hex(0xFFFFFFFF)); ink.Stroke({x + 64, fy + 24, x + 584, fy + 68}, 8, Hex(0xD9D9D6FF), 1);
        ink.Text(i ? L"1234 1234 1234 1234" : L"vous@exemple.fr", F(15, 20), {x + 79, fy + 36, x + 584, fy + 56}, Hex(0x9A9A97FF));
    }
    const D2D1_RECT_F card{x + 632, y + 96, x + 972, y + 275};
    ink.Fill(card, 10, Hex(0xFFFFFFFF)); ink.Stroke(card, 10, Hex(0xE2E2E0FF), 1);
    ink.Text(L"Récapitulatif", F(15, 20, 500), {card.left + 21, card.top + 21, card.right, card.top + 41}, Hex(0x1C1C1CFF));
    const wchar_t* items[2] = {L"Tasse grès, sable", L"Carnet A5 ligné"};
    for (int i = 0; i < 2; ++i) { const float iy = card.top + 55 + i * 34; ink.Text(items[i], F(14, 20), {card.left + 21, iy, card.right, iy + 20}, Hex(0x4A4A4AFF)); ink.Text(L"24,00 €", F(14, 20), {card.left, iy, card.right - 21, iy + 20}, Hex(0x4A4A4AFF), false, Align::Trailing); }
    ink.Fill({card.left + 21, card.top + 123, card.right - 21, card.top + 124}, 0, Hex(0xE2E2E0FF));
    ink.Text(L"Total", F(15, 20, 500), {card.left + 21, card.top + 138, card.right, card.top + 158}, Hex(0x1C1C1CFF));
    ink.Text(L"48,00 €", F(15, 20, 500), {card.left, card.top + 138, card.right - 21, card.top + 158}, Hex(0x1C1C1CFF), false, Align::Trailing);
    const D2D1_RECT_F b{x + 64, y + 350, x + 304, y + 394};
    ink.Fill(b, 8, Hex(0x1C1C1CFF)); ink.Text(L"Payer 48,00 €", F(15, 20, 500), b, {1, 1, 1, 1}, false, Align::Center, true);
    if (selection) { ink.Stroke(Inflate(b, 3), 10, Accent(), 2); ink.Fill({b.left - 3, b.top - 25, b.left + 205, b.top - 7}, 3, Accent());
        ink.Text(L"button.btn-primary · 240×44", Mono(11, 14), {b.left + 3, b.top - 25, b.left + 205, b.top - 7}, {1, 1, 1, 1}, false, Align::Leading, true); }
}
static void PaintChrome(Ink& ink, float w) {   // the browser chrome (placeholder: canvas, address, toolbar « Annoter »)
    ink.Fill({0, 0, w, 85}, 0, Hex(Ink_::canvas));
    ink.Fill({122, 44, w - 222, 76}, 6, Hex(0xFFFFFF14));
    ink.Text(L"localhost:5173/checkout", F(16, 20, 400, -.01f), {154, 50, 600, 70}, {1, 1, 1, 1});
    ToolbarVisual tv; tv.active = true; PaintToolbarButton(ink.Target(), ink.Factory(), ink.Brush({1, 1, 1, 1}), {w - 156, 48, w - 132, 72}, tv);
}

// ---------------------------------------------------------------------------------------
static void ZeroCostTests() {
    CHECK(!Ink::Exists());
    CHECK(!Animator::Exists());
    CHECK(Layer::Created() == 0);
    Banner banner; ConnectModal connect;
    banner.Init(GetModuleHandleW(nullptr), nullptr, L".", {}); connect.Init(GetModuleHandleW(nullptr), nullptr, L".", {});
    CHECK(!banner.Created() && !connect.Created());
    CHECK(!Ink::Exists() && !Animator::Exists() && Layer::Created() == 0);   // Init stores context only
    report << L"\"zeroCost\":{\"inkBeforeUse\":false,\"animatorBeforeUse\":false,\"windowsBeforeUse\":0},";
}

// ---------------------------------------------------------------------------------------
static std::wostringstream motionJson;
static void Row(const wchar_t* surface, const wchar_t* name, double t, const Pose& p) {
    motionJson << L"{\"surface\":\"" << surface << L"\",\"name\":\"" << name << L"\",\"t\":" << t << L",\"opacity\":" << p.opacity << L",\"scale\":" << p.scale << L",\"dx\":" << p.dx << L",\"dy\":" << p.dy << L"},";
    std::printf("  %-10ls %-22ls t=%6.1f ms  opacity=%.4f  scale=%.4f  dy=%.3f\n", surface, name, t, p.opacity, p.scale, p.dy);
}
static double EndOf(const Presence& p, double from) { for (double t = from; t < from + 2000; t += .5) if (!p.Moving(At(t))) return t - from; return -1; }
static void MotionTests() {
    motionJson.setf(std::ios::fixed); motionJson.precision(4);
    // Curves: exact CSS cubic-bezier, monotonic, ends pinned.
    CHECK(Curves::out(0) == 0 && Curves::out(1) == 1 && Curves::inOut(0) == 0 && Curves::inOut(1) == 1);
    { float last = -1; bool mono = true; for (int i = 0; i <= 100; ++i) { const float y = Curves::out(i / 100.f); if (y < last - 1e-5f) mono = false; last = y; } CHECK(mono); }
    CHECKM(Curves::out(.5f) > .95f && Curves::out(.5f) < .98f, "out(.5) = %.4f", Curves::out(.5f));   // strong ease-out: ~97 % at half time
    CHECKM(Curves::inOut(.25f) < .1f && Curves::inOut(.75f) > .9f, "inOut(.25) = %.4f", Curves::inOut(.25f));
    std::printf("Curves: out(.25/.5/.75) = %.4f %.4f %.4f; inOut(.25/.5/.75) = %.4f %.4f %.4f\n", Curves::out(.25f), Curves::out(.5f), Curves::out(.75f), Curves::inOut(.25f), Curves::inOut(.5f), Curves::inOut(.75f));
    std::printf("Motion samples (pure model)\n");
    // Modal: 200 ms in (0.96 -> 1), 150 ms out (-> 0.98).
    Presence modal{ModalTiming()}; modal.Show(At(0));
    const double modalIn = EndOf(modal, 0);
    CHECKM(Near(modalIn, 200, 1), "modal in %.1f ms", modalIn);
    for (double t : {0.0, 50.0, 100.0, 150.0, 200.0}) Row(L"modal", t == 0 ? L"enter t=0" : t == 100 ? L"enter t=1/2" : t == 200 ? L"enter t=end" : t == 50 ? L"enter t=1/4" : L"enter t=3/4", t, modal.Sample(At(t), false));
    { const Pose p0 = modal.Sample(At(0), false), p2 = modal.Sample(At(100), false), p4 = modal.Sample(At(200), false);
      CHECK(p0.opacity == 0 && Near(p0.scale, .96, 1e-6)); CHECK(p2.opacity > .9f && p2.scale > .99f); CHECK(p4.opacity == 1 && p4.scale == 1); }
    Presence out{ModalTiming()}; out.Show(At(0), false); out.Hide(At(0));
    const double modalOut = EndOf(out, 0); CHECKM(Near(modalOut, 150, 1), "modal out %.1f ms", modalOut);
    Row(L"modal", L"exit t=1/2", 75, out.Sample(At(75), false)); Row(L"modal", L"exit t=end", 150, out.Sample(At(150), false));
    CHECK(out.Sample(At(150), false).opacity == 0 && Near(out.Sample(At(150), false).scale, .98, 1e-6));
    // Interruption: the exit starts from the presented value (no jump), the re-entry too.
    Presence it{ModalTiming()}; it.Show(At(0)); const Pose before = it.Sample(At(60), false); it.Hide(At(60)); const Pose after = it.Sample(At(60), false);
    Row(L"modal", L"interrupt before", 60, before); Row(L"modal", L"interrupt after", 60, after); Row(L"modal", L"interrupt +40ms", 100, it.Sample(At(100), false));
    CHECK(Near(before.opacity, after.opacity, 1e-5) && Near(before.scale, after.scale, 1e-5));
    CHECK(it.Sample(At(100), false).opacity < before.opacity);
    it.Show(At(100)); CHECK(Near(it.Sample(At(100), false).opacity, it.Sample(At(99.9), false).opacity, .01));
    // Reduced motion: fades only, 150 ms in, 120 ms out, never scaled.
    Presence red{ModalTiming()}; red.Show(At(0), true, true); const double redIn = EndOf(red, 0);
    CHECKM(Near(redIn, 150, 1), "reduced in %.1f", redIn); Row(L"modal", L"reduced t=1/2", 75, red.Sample(At(75), true)); CHECK(red.Sample(At(75), true).scale == 1);
    red.Hide(At(400), true, true); CHECKM(Near(EndOf(red, 400), 120, 1), "reduced out");
    // State cross-fade (button « En cours » -> « Connecté »): 0.3 s critical spring, interrupt from the presented alpha.
    Crossfade fade; fade.Reset(0); fade.Go(1, At(0));
    double t98 = -1; for (double t = 0; t < 1000; t += .5) if (fade.Incoming(At(t)) >= .98f) { t98 = t; break; }
    CHECKM(t98 > 220 && t98 < 340, "cross-fade 98 %% at %.1f ms", t98);
    const float mid = fade.Incoming(At(100)); fade.Go(2, At(100)); CHECK(Near(fade.Outgoing(At(100)), mid, 1e-4));
    motionJson << L"{\"surface\":\"crossfade\",\"name\":\"98%\",\"t\":" << t98 << L",\"opacity\":0.98,\"scale\":1,\"dx\":0,\"dy\":0},";
    std::printf("  crossfade  98 %% at %.1f ms; interrupted at 100 ms from alpha %.4f\n", t98, mid);
    // Fold (réglage manuel): 200 ms cubic-bezier(.77,0,.175,1).
    Ramp fold; fold.Set(1, At(0), Spec::fold, Curves::inOut); CHECK(Near(fold.Value(At(100)), Curves::inOut(.5f), 1e-4) && fold.Value(At(200)) == 1);
    // Press 0.96, never when disabled or reduced.
    CHECK(Near(Motion::PressScale(1), .96, 1e-6) && Motion::PressScale(1, false, false) == 1 && Motion::PressScale(1, true) == 1);
    report << L"\"motion\":{\"modalInMs\":" << modalIn << L",\"modalOutMs\":" << modalOut << L",\"reducedInMs\":" << redIn << L",\"crossfade98Ms\":" << t98 << L",\"samples\":[" << motionJson.str() << L"{}]},";
}

static void ConfigTests() {
    ConnectSetup s = SampleSetup();
    const std::wstring json = ConnectJson(s), toml = ConnectToml(s);
    CHECK(json.find(L"\"mcpServers\"") != std::wstring::npos && json.find(L"\"browser-annotations\"") != std::wstring::npos);
    CHECK(json.find(L"C:\\\\Users\\\\vous") != std::wstring::npos);                       // backslashes escaped in JSON
    CHECK(json.find(L"\"args\": [\"--store\", ") != std::wstring::npos);
    CHECK(toml.find(L"[mcp_servers.browser-annotations]") == 0 && toml.find(L"command = 'C:\\Users\\vous") != std::wstring::npos);
    s.serverName = L"browser annotations"; CHECK(ConnectToml(s).find(L"[mcp_servers.\"browser annotations\"]") == 0);
    CHECK(JsonQuote(L"a\"b\\c\n") == L"\"a\\\"b\\\\c\\n\"");
    CHECK(TomlQuote(L"it's") == L"\"it's\"");
    std::printf("ConnectJson:\n%s\nConnectToml:\n%s\n", Utf8(ConnectJson(SampleSetup())).c_str(), Utf8(ConnectToml(SampleSetup())).c_str());
}

// ---------------------------------------------------------------------------------------
static void GeometryTests(Ink& ink) {
    ConnectView v = ConnectState_(SampleSetup());
    Geo("1.1 modal (launch)", 440, 192, LayoutConnect(ink, v).height);           // 1+62+84+45+1 (Paper 10P2-0, revised 2026-11-14: no description, a cross)
    ConnectView w = v; w.setup.launch = false; w.setup.claude.state = ConnectState::Working;
    Geo("1.2 modal (en cours)", 440, 228.39f - 36, LayoutConnect(ink, w).height);
    ConnectView c = v; c.setup.claude.state = ConnectState::Connected; c.setup.codex = {ConnectState::Connected, L"Redémarrez Codex pour l'activer"};
    Geo("1.3 modal (connecté + Terminé)", 440, 377 - 80 - 36, LayoutConnect(ink, c).height);
    ConnectView f = v; f.setup.launch = false; f.setup.codex = {ConnectState::Failed, L"Configuration de Codex verrouillée. Réessayez ou réglez-la à la main."};
    Geo("1.4 modal (échec)", 440, 216, LayoutConnect(ink, f).height);
    ConnectView m = v; m.manual = true; Geo("1.5 modal (réglage manuel)", 440, 305, LayoutConnect(ink, m).height);
    Geo("1.5 footer unfolded", 438, 158, LayoutConnect(ink, m).footer.bottom - LayoutConnect(ink, m).footer.top);
    ConnectView r = v; r.setup.launch = false; r.setup.removableDrive = L"E:"; r.setup.claude.state = ConnectState::Reconnect;
    Geo("1.6 modal (disque amovible)", 440, 192, LayoutConnect(ink, r).height);
    Geo("1.1 pill « Reste sur cet ordinateur » width", 0, 161, LocalCollapsed(ink, v.setup).width, 2);
    Geo("1.6 pill (disque amovible) width", 0, 389, LocalCollapsed(ink, r.setup).width, 3);
    Geo("1.7 pill unfolded height", 340, 100, LocalExpandedH(ink));
    // One header for every state (1.1 to 1.7): 62 high, a 28 close button at the same place, no description.
    {   const auto ref = LayoutConnect(ink, v); const auto* rb = ref.Find(BHeader);
        CHECK(rb && Near(rb->h, 62, .01) && Near(ref.headerButton.right - ref.headerButton.left, 28, .01) && Near(ref.headerButton.bottom - ref.headerButton.top, 28, .01));
        for (const ConnectView* s : {&w, &c, &f, &m, &r}) { const auto l = LayoutConnect(ink, *s); const auto* b = l.Find(BHeader);
            CHECK(b && Near(b->h, rb->h, .01) && b->state == rb->state && Near(l.headerButton.left, ref.headerButton.left, .01) && Near(l.headerButton.top, ref.headerButton.top, .01)); }
        Geo("1.1-1.7 header", 438, 62, rb->h); }
    report << L"\"geometry\":[";
    for (auto& g : geometry) report << L"{\"surface\":\"" << Wide(g.surface) << L"\",\"width\":" << g.width << L",\"paperHeight\":" << g.paper << L",\"height\":" << g.native << L"},";
    report << L"{}],";
}

// ---------------------------------------------------------------------------------------
static void Captures(Ink& ink) {
    const UINT dpi = 192; const float k = 2;
    Canvas c;
    auto modalBoard = [&](const ConnectView& v, const wchar_t* name, float fold = 0, float desc = 0) {
        const auto l = LayoutConnect(ink, v); const float h = ConnectGroupHeight(ink, v, l) + (fold > 0 ? LocalExpandedH(ink) - 28 : 0) + 80;
        Render(ink, 520, h, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { At_(i, 40, 40, [&] { PaintConnectStatic(i, v, l, fold, desc); }); }, c); Save(ink, c, name); return l;
    };
    // ---- 1.x connect modal
    ConnectView launch = ConnectState_(SampleSetup());
    {   const auto l = modalBoard(launch, L"1.1-connect-launch.png");
        Sample(c, k, 40 + 300, 40 + 12, canvasRgb, "1.1 modal surface #171717");
        Sample(c, k, 40 + 30, 40 + l.claude.top + 16, raisedRgb, "1.1 « Connecter avec Claude » raised #292929");
        Sample(c, k, 40 + 0.25f, 40 + 30, Over(0xFFFFFF14, canvasRgb), "1.1 modal border #FFFFFF14", 6);
        MeasureText(c, k, {40 + 47, 40 + 21, 40 + 300, 40 + 47}, canvasRgb, 0xFFFFFFFF, "modal title 18/24 white");
        MeasureText(c, k, {40 + l.headerButton.left + 6, 40 + l.headerButton.top + 6, 40 + l.headerButton.right - 6, 40 + l.headerButton.bottom - 6}, canvasRgb, 0xFFFFFFB3, "close cross 16 at 70 % (non-text, 3:1)", 3);
        {   // Nothing under the title any more (the description is gone): the band 47..65 is the bare surface.
            bool bare = true; for (int y = int((40 + 49) * k); y < int((40 + 62) * k); ++y) for (int x = int((40 + 47) * k); x < int((40 + 380) * k); ++x) if (!Close(Px(c.At(x, y)), canvasRgb, 3)) bare = false;
            CHECKM(bare, "1.1: no description under the title"); }
        MeasureText(c, k, {40 + 150, 40 + l.claude.top + 8, 40 + 260, 40 + l.claude.bottom - 8}, raisedRgb, 0xFFFFFFFF, "« Connecter avec Claude » white on #292929");
        MeasureText(c, k, {40 + 150, 40 + l.manual.top + 3, 40 + 300, 40 + l.manual.bottom - 3}, canvasRgb, 0xFFFFFFB3, "« Autre app ou réglage manuel » 13/18 #FFFFFFB3");
        MeasureText(c, k, {40 + 160, 40 + l.height + 8 + 6, 40 + 300, 40 + l.height + 8 + 22}, canvasRgb, 0xFFFFFF99, "pill « Reste sur cet ordinateur » 12/16 #FFFFFF99");
        // Claude logo colour.
        double best = 1e9; Rgb found{}; for (int y = int((40 + l.claude.top) * k); y < int((40 + l.claude.bottom) * k); ++y) for (int x = int((40 + 270) * k); x < int((40 + 300) * k); ++x) { const Rgb p = Px(c.At(x, y)); const double d = std::fabs(p.r - 0xD9) + std::fabs(p.g - 0x77) + std::fabs(p.b - 0x57); if (d < best) { best = d; found = p; } }
        sampled.push_back({"Claude logo #D97757", "#D97757", Hex6(found)}); CHECKM(best < 12, "Claude logo colour %s", Hex6(found).c_str());
    }
    ConnectView working = launch; working.setup.launch = false; working.setup.claude.state = ConnectState::Working; working.spinner = 30;
    {   const auto l = modalBoard(working, L"1.2-connect-working.png");
        Sample(c, k, 40 + 30, 40 + l.claude.top + 16, Over(0xFFFFFF0A, canvasRgb), "1.2 en cours #FFFFFF0A");
        MeasureText(c, k, {40 + 170, 40 + l.claude.top + 8, 40 + 330, 40 + l.claude.bottom - 8}, Over(0xFFFFFF0A, canvasRgb), 0xFFFFFFCC, "« Cliquez sur Installer dans Claude » #FFFFFFCC"); }
    ConnectView connected = launch; connected.setup.claude.state = ConnectState::Connected; connected.setup.codex = {ConnectState::Connected, L"Redémarrez Codex pour l'activer"};
    {   const auto l = modalBoard(connected, L"1.3-connect-connected.png");
        Sample(c, k, 40 + 30, 40 + l.claude.top + 16, canvasRgb, "1.3 connecté: no fill (contour only)");
        Sample(c, k, 40 + 30, 40 + l.done.top + 16, accentRgb, "1.3 « Terminé » accent");
        MeasureText(c, k, {40 + 150, 40 + l.codexMsg.top, 40 + 300, 40 + l.codexMsg.bottom}, canvasRgb, 0xFFFFFF99, "« Redémarrez Codex pour l'activer » 12/16 #FFFFFF99");
        MeasureText(c, k, {40 + 170, 40 + l.done.top + 8, 40 + 230, 40 + l.done.bottom - 8}, accentRgb, 0xFFFFFFFF, "« Terminé » white on accent"); }
    ConnectView failed = launch; failed.setup.launch = false; failed.setup.codex = {ConnectState::Failed, L"Configuration de Codex verrouillée. Réessayez ou réglez-la à la main."};
    {   const auto l = modalBoard(failed, L"1.4-connect-failed.png");
        Sample(c, k, 40 + l.codex.left + .25f, 40 + l.codex.top + 16, Over(0xFF7B6E66, raisedRgb), "1.4 échec border #FF7B6E66", 8);
        MeasureText(c, k, {40 + 60, 40 + l.codexMsg.top, 40 + 400, 40 + l.codexMsg.bottom}, canvasRgb, 0xFF7B6EFF, "échec message 12/16 #FF7B6E"); }
    ConnectView manual = launch; manual.setup.launch = false; manual.manual = true;
    {   const auto l = modalBoard(manual, L"1.5-connect-manual.png");
        Sample(c, k, 40 + l.command.left + 4, 40 + l.command.top + 16, raisedRgb, "1.5 command box raised");
        MeasureText(c, k, {40 + l.command.left + 10, 40 + l.command.top + 6, 40 + l.command.right - 10, 40 + l.command.bottom - 6}, raisedRgb, 0xFFFFFFCC, "command JetBrains Mono 12/16 #FFFFFFCC");
        MeasureText(c, k, {40 + l.formatsLabel.left, 40 + l.formatsLabel.top + 2, 40 + l.formatsLabel.right, 40 + l.formatsLabel.bottom - 2}, canvasRgb, 0xFFFFFF99, "« Copier au format : » 12/16 #FFFFFF99");
        MeasureText(c, k, {40 + l.manualText.left, 40 + l.manualText.top, 40 + l.manualText.right, 40 + l.manualText.bottom}, canvasRgb, 0xFFFFFFB3, "manual text 13/19 #FFFFFFB3"); }
    ConnectView copied = manual; copied.copied = CCopyJson; modalBoard(copied, L"1.5b-connect-manual-copied.png");
    ConnectView removable = launch; removable.setup.launch = false; removable.setup.removableDrive = L"E:"; removable.setup.claude.state = ConnectState::Reconnect;
    {   const auto l = modalBoard(removable, L"1.6-connect-removable.png");
        MeasureText(c, k, {40 + 60, 40 + l.height + 8 + 6, 40 + 400, 40 + l.height + 8 + 24}, canvasRgb, 0xFFFFFFCC, "removable pill 12/16 #FFFFFFCC"); }
    {   const auto l = modalBoard(launch, L"1.7-connect-local-unfolded.png", 1, 1);
        MeasureText(c, k, {40 + 62, 40 + l.height + 8 + 34, 40 + 380, 40 + l.height + 8 + 88}, canvasRgb, 0xFFFFFFB3, "local pill description 12/18 #FFFFFFB3"); }
    ConnectView focus = launch; focus.focus = CClaude; focus.keyboard = true; modalBoard(focus, L"1.1b-connect-focus-keyboard.png");
    // ---- 1.1 scene (veil over the page, band, launcher) and 1.8 band reminder
    auto scene = [&](const std::function<void(Ink&)>& extra, bool band, bool reminder, bool beta, bool selection, const wchar_t* name) {
        Render(ink, 1180, 815, dpi, 0x171717FF, [&](Ink& i, float w, float) {
            PaintChrome(i, w);
            const float top = band ? 117.f : 85.f;
            if (band) { BannerVisual bv; bv.beta = beta; bv.reminder = reminder ? 1.f : 0.f; At_(i, 0, 85, [&] { PaintBanner(i, w, 32, bv); }); }
            PaintPage(i, 0, top, w, 815 - top, selection);
            extra(i);
        }, c);
        return Save(ink, c, name);
    };
    scene([&](Ink& i) {
        i.Fill({0, 0, 1180, 815}, 0, Hex(0x0000008C));
        const auto l = LayoutConnect(i, launch); const float gh = ConnectGroupHeight(i, launch, l);
        At_(i, (1180 - 440) / 2.f, (815 - gh) / 2.f, [&] { PaintConnectStatic(i, launch, l); });
    }, true, false, true, false, L"1.1-scene-launch.png");
    Sample(c, k, 600, 760, Over(0x0000008C, pageRgb), "veil #0000008C over the page");
    scene([](Ink&) {}, true, true, false, false, L"1.8-scene-band-reminder.png");
    {   const auto l = LayoutBanner(ink, 1180, false);
        const float cx = l.remind.left + 3, cy = 85 + 16;
        const Rgb chip = Px(c.At(int(cx * k), int(cy * k)));
        sampled.push_back({"1.8 reminder chip #FFFFFF14 over the band", "(composited)", Hex6(chip)});
        MeasureText(c, k, {l.remind.left + 8, 85 + l.remind.top + 3, l.remind.left + 125, 85 + l.remind.bottom - 3}, chip, 0xFFFFFFCC, "band « Aucun agent connecté » 12/16 #FFFFFFCC");
        MeasureText(c, k, {l.remind.right - 62, 85 + l.remind.top + 3, l.remind.right - 6, 85 + l.remind.bottom - 3}, chip, 0xFFFFFFFF, "band « Connecter » 12/16 medium white");
    }
    scene([](Ink&) {}, true, false, true, false, L"1.1c-band-beta.png");
    {   const auto l = LayoutBanner(ink, 1180, true); const Rgb chip = Px(c.At(int((l.beta.left + 1.5f) * k), int((85 + 16) * k)));
        sampled.push_back({"BETA chip accent 16 % over the band", "(composited)", Hex6(chip)});
        MeasureText(c, k, {l.beta.left + 3, 85 + l.beta.top + 2, l.beta.right - 3, 85 + l.beta.bottom - 2}, chip, 0x6A8FFFFF, "band « BETA » 12 medium #6A8FFF (Paper #5B84FF: 4.14:1)"); }
    {   Render(ink, 1180, 32, dpi, 0x171717FF, [&](Ink& i, float w, float) { BannerVisual bv; bv.beta = true; bv.chatShown = true; bv.chatActive = true; PaintBanner(i, w, 32, bv); }, c);
        Save(ink, c, L"6.b-band-layout-switch-comment.png");
        const auto l = LayoutBanner(ink, 1180, true, true), plain = LayoutBanner(ink, 1180, true, false);
        Geo("6.b band layout switch (Paper 1AYP-0) — gap to « Quitter »", 1180, 12, l.quit.left - l.sw.right);
        Geo("6.b band layout switch — 24 px segments, 2 apart", 1180, 24 + 2 + 24, l.chat.right - l.comment.left);
        Geo("6.b band « Quitter » does not move", 1180, plain.quit.left, l.quit.left);
        Geo("6.b band reminder moves left of the switch", 1180, 12, l.sw.left - l.remind.right);
        Geo("6.b band without the chat: no switch", 1180, 0, plain.sw.right - plain.sw.left);
        Render(ink, 1180, 32, dpi, 0x171717FF, [&](Ink& i, float w, float) { BannerVisual bv; bv.beta = true; bv.chatShown = true; bv.reminder = 1; bv.focus = true; bv.focusItem = BannerChat; bv.keyboard = true; PaintBanner(i, w, 32, bv); }, c);
        Save(ink, c, L"6.b2-band-layout-switch-reminder-focus.png"); }
}

// ---------------------------------------------------------------------------------------
// Motion strips: frames of the real paint code at sampled instants (offscreen, 2x).
static void MotionStrips(Ink& ink) {
    const UINT dpi = 192; Canvas c;
    ConnectView v = ConnectState_(SampleSetup()); const auto l = LayoutConnect(ink, v);
    auto strip = [&](const wchar_t* name, int n, float cellW, float cellH, const std::function<void(Ink&, int)>& frame) {
        Render(ink, cellW * n, cellH, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { for (int f = 0; f < n; ++f) At_(i, f * cellW, 0, [&] { frame(i, f); }); }, c);
        Save(ink, c, name);
    };
    const float mh = ConnectGroupHeight(ink, v, l) + 40;
    // Modal enter 0 / 50 / 100 / 150 / 200 ms, then exit, interruption, reduced.
    auto modalAt = [&](Ink& i, const Pose& p) { At_(i, 20, 20, [&] { Posed(i, p, {220, l.height / 2}, [&] { PaintConnectStatic(i, v, l); }); }); };
    { Presence p{ModalTiming()}; p.Show(At(0)); const double ts[5] = {0, 50, 100, 150, 200};
      strip(L"m1-modal-enter-0-50-100-150-200ms.png", 5, 480, mh, [&](Ink& i, int f) { modalAt(i, p.Sample(At(ts[f]), false)); }); }
    { Presence p{ModalTiming()}; p.Show(At(0), false); p.Hide(At(0)); const double ts[4] = {0, 50, 100, 150};
      strip(L"m2-modal-exit-0-50-100-150ms.png", 4, 480, mh, [&](Ink& i, int f) { modalAt(i, p.Sample(At(ts[f]), false)); }); }
    { Presence p{ModalTiming()}; p.Show(At(0)); p.Hide(At(60)); const double ts[4] = {59, 60, 90, 120};
      strip(L"m3-modal-interrupt-at-60ms.png", 4, 480, mh, [&](Ink& i, int f) { modalAt(i, p.Sample(At(ts[f]), false)); }); }
    { Presence p{ModalTiming()}; p.Show(At(0), true, true); const double ts[3] = {0, 75, 150};
      strip(L"m4-modal-reduced-fade.png", 3, 480, mh, [&](Ink& i, int f) { modalAt(i, p.Sample(At(ts[f]), true)); }); }
    // Button cross-fade « En cours » -> « Connecté » (0.3 s spring): 0 / 60 / 120 / 300 ms.
    { ConnectView a = v; a.setup.launch = false; a.setup.claude.state = ConnectState::Working; a.spinner = 40; ConnectView b = a; b.setup.claude.state = ConnectState::Connected;
      const auto la = LayoutConnect(ink, a), lb = LayoutConnect(ink, b); Crossfade x; x.Reset(0); x.Go(1, At(0)); const double ts[4] = {0, 60, 120, 300};
      strip(L"m5-button-crossfade-working-to-connected.png", 4, 440, 72, [&](Ink& i, int f) {
          const float in = x.Incoming(At(ts[f])), out = x.Outgoing(At(ts[f]));
          At_(i, 0, 20 - la.claude.top, [&] {
              i.Fill({0, la.claude.top - 8, 440, la.claude.bottom + 8}, 0, Hex(Ink_::canvas));
              ID2D1Layer* g = nullptr; bool pushed = i.PushOpacity(out, g); PaintConnectBlock(i, a, la, BClaude, 0); if (pushed) i.PopOpacity(g);
              pushed = i.PushOpacity(in, g); PaintConnectBlock(i, b, lb, BClaude, 0); if (pushed) i.PopOpacity(g); });
      }); }
    // Local pill unfold (spring 0.3 s, description 60 ms later).
    { Motion::Spring s{.002f}; s.Set(1, At(0), Spec::stateResponse); Ramp d; d.Set(1, At(0), 200, Curves::out, Spec::localDescDelay); const double ts[4] = {0, 80, 160, 400};
      strip(L"m6-local-pill-unfold.png", 4, 440, 150, [&](Ink& i, int f) {
          const float fold = Motion::Clamp(s.Value(At(ts[f]))), desc = Motion::Clamp(d.Value(At(ts[f])));
          PaintLocal(i, v, LayoutLocal(i, v.setup, 220, 20, fold), fold, desc, false); }); }
}

// ---------------------------------------------------------------------------------------
// Live windows
static int hostWheel = 0;
static void Pump(int ms) {
    const DWORD end = GetTickCount() + DWORD(ms); MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    }
}
static bool PumpUntil(const std::function<bool()>& done, int ms) { const DWORD end = GetTickCount() + DWORD(ms); while (GetTickCount() < end) { if (done()) return true; Pump(10); } return done(); }
static LRESULT CALLBACK HostProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEWHEEL) ++hostWheel;
    if (m == WM_ERASEBKGND) { RECT r; GetClientRect(h, &r); FillRect(reinterpret_cast<HDC>(w), &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); return 1; }
    return DefWindowProcW(h, m, w, l);
}
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
static bool Settle(int ms = 3000) { return PumpUntil([] { return !Animator::Get().Animating(); }, ms); }
static bool HasNode(const std::vector<Accessibility::Node>& n, const std::wstring& name) { for (auto& x : n) if (x.name == name) return true; return false; }
static void IdleCheck(const char* label) {
    const auto* st = Animator::Get().Stats();
    const auto frames = Animator::Get().Frames(); const auto requests = st ? st->requests.load() : 0, wakes = st ? st->wakes.load() : 0, posted = st ? st->posted.load() : 0, ticks = st ? st->ticks.load() : 0;
    unsigned timers = 0, foreign = 0; const DWORD end = GetTickCount() + 1500; MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_TIMER) { wchar_t cls[64] = L""; GetClassNameW(msg.hwnd, cls, 64); std::printf("  WM_TIMER hwnd %p class %ls id %llu\n", (void*)msg.hwnd, cls, (unsigned long long)msg.wParam);
                if (!wcsncmp(cls, L"BrowserAnnotation", 17)) ++timers; else ++foreign; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    const auto frames2 = Animator::Get().Frames(); const auto requests2 = st ? st->requests.load() : 0, wakes2 = st ? st->wakes.load() : 0, posted2 = st ? st->posted.load() : 0, ticks2 = st ? st->ticks.load() : 0;
    std::printf("Idle 1500 ms (%s): frames %llu, clock requests %llu, wakes %llu, ticks %llu, posted %llu, WM_TIMER %u (foreign %u), fallback armed %d\n", label,
        frames2 - frames, (unsigned long long)(requests2 - requests), (unsigned long long)(wakes2 - wakes), (unsigned long long)(ticks2 - ticks), (unsigned long long)(posted2 - posted), timers, foreign, Animator::Get().FallbackTimerArmed());
    CHECK(frames2 == frames && requests2 == requests && wakes2 == wakes && posted2 == posted && ticks2 == ticks);
    CHECK(timers == 0 && !Animator::Get().FallbackTimerArmed() && !Animator::Get().Animating());
    report << L"\"idle_" << Wide(label) << L"\":{\"ms\":1500,\"frames\":" << (frames2 - frames) << L",\"clockRequests\":" << (requests2 - requests) << L",\"clockWakes\":" << (wakes2 - wakes)
           << L",\"clockTicks\":" << (ticks2 - ticks) << L",\"posted\":" << (posted2 - posted) << L",\"wmTimer\":" << timers << L",\"foreignWmTimer\":" << foreign << L"},";
}
static void WindowTests(Ink& ink, const std::wstring& root) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc = HostProc; wc.hInstance = inst; wc.lpszClassName = L"AnnotationHarnessHost"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    const UINT sysDpi = GetDpiForSystem(); const float s = sysDpi / 96.f;
    RECT want{0, 0, LONG(1180 * s), LONG(815 * s)}; AdjustWindowRectExForDpi(&want, WS_OVERLAPPEDWINDOW, FALSE, 0, sysDpi);
    HWND host = CreateWindowExW(0, wc.lpszClassName, L"Annotations harness", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 40, 40, want.right - want.left, want.bottom - want.top, nullptr, nullptr, inst, nullptr);
    CHECK(host != nullptr); if (!host) return;
    ShowWindow(host, SW_SHOW); UpdateWindow(host);
    SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ForceForeground(host); Pump(150);
    std::printf("Host foreground: %d\n", GetForegroundWindow() == host ? 1 : 0);
    const float dpiScale = GetDpiForWindow(host) / 96.f;
    RECT client{}; GetClientRect(host, &client);
    const int pageTop = int(std::lround(117 * dpiScale));
    RECT page{client.left, pageTop, client.right, client.bottom};
    HWND web = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_WHITERECT, page.left, page.top, page.right - page.left, page.bottom - page.top, host, nullptr, inst, nullptr);
    CHECK(web != nullptr);
    std::vector<std::wstring> said; auto announce = [&](const std::wstring& t) { said.push_back(t); };
    auto lastSaid = [&](const std::wstring& prefix) { for (auto it = said.rbegin(); it != said.rend(); ++it) if (it->rfind(prefix, 0) == 0) return true; return false; };

    // ---- Banner: reminder « Aucun agent connecté · Connecter »
    Banner banner; int exits = 0, escapes = 0, bannerConnects = 0;
    BannerHooks bh; bh.exit = [&] { ++exits; }; bh.escape = [&] { ++escapes; }; bh.connect = [&] { ++bannerConnects; }; bh.announce = announce;
    banner.Init(inst, host, root, bh); banner.SetLiveForTest(0);   // V3-B: the wave is tested in V3B::Windows; here the band must settle
    CHECK(banner.Show({0, int(std::lround(85 * dpiScale)), client.right, pageTop}, false));
    CHECK(lastSaid(L"Mode Annotation activé"));
    banner.SetReminder(true);
    CHECK(Settle());
    { auto n = banner.NodesForTest(); CHECK(HasNode(n, L"Connecter un agent") && HasNode(n, L"Quitter le mode Annotation")); }
    SetFocus(banner.Window()); CHECK(banner.KeyForTest(VK_TAB) && banner.FocusItemForTest() == BannerRemind);
    banner.KeyForTest(VK_RETURN); CHECK(bannerConnects == 1);
    banner.KeyForTest(VK_ESCAPE); CHECK(escapes == 1 && exits == 0);   // Escape in the band: its own hook (an open bubble closes first), « Quitter » stays exit
    SaveLayer(ink, banner.LayerForTest(), L"w1-window-band-reminder.png");
    banner.SetReminder(false); CHECK(!banner.ReminderVisible()); Settle();

    // ---- Connect modal
    ConnectModal modal; std::vector<AgentKind> connects; std::vector<ConnectEnd> ends; std::wstring copied;
    ConnectHooks ch; ch.connect = [&](AgentKind a) { connects.push_back(a); }; ch.copy = [&](const std::wstring& t) { copied = t; }; ch.closed = [&](ConnectEnd e) { ends.push_back(e); }; ch.announce = announce;
    modal.Init(inst, host, root, ch);
    RECT clientScreen = client; MapWindowPoints(host, nullptr, reinterpret_cast<POINT*>(&clientScreen), 2);
    const DWORD t0 = GetTickCount(); const auto f0 = Animator::Get().Frames();
    CHECK(modal.Show(SampleSetup(), clientScreen, false));
    CHECK(!IsWindowEnabled(host) && IsWindowVisible(modal.Window()) && IsWindowVisible(modal.Veil()));
    Pump(100); SaveLayer(ink, modal.LayerForTest(), L"w2-window-modal-entering.png");
    CHECK(Settle()); const DWORD enterMs = GetTickCount() - t0; const auto enterFrames = Animator::Get().Frames() - f0;
    std::printf("Modal enter in a real window: %lu ms, %llu frames, clock %s\n", enterMs, enterFrames, Animator::Get().ClockRunning() ? "compositor/DWM" : "fallback timer");
    CHECK(enterFrames >= 5 && enterMs < 600);
    report << L"\"liveModal\":{\"enterMs\":" << enterMs << L",\"frames\":" << enterFrames << L"},";
    SaveLayer(ink, modal.LayerForTest(), L"w3-window-modal-launch.png");
    {   auto n = modal.NodesForTest(); CHECK(HasNode(n, L"Fermer, connecter un agent plus tard") && !HasNode(n, L"Plus tard") && HasNode(n, L"Connecter avec Claude") && HasNode(n, L"Connecter avec Codex") && HasNode(n, L"Autre app ou réglage manuel") && HasNode(n, L"Reste sur cet ordinateur")); }
    {   auto n = modal.NodesForTest(); bool later = false; for (auto& x : n) if (x.key == CHeader) later = x.automationId == L"AnnotationConnectLater" && x.accelerator == L"Échap"; CHECK(later); }
    // Tab order: close (« plus tard ») -> Claude -> Codex -> manual -> pill.
    modal.KeyForTest(VK_TAB); CHECK(modal.FocusForTest() == CHeader);
    modal.KeyForTest(VK_TAB); CHECK(modal.FocusForTest() == CClaude);
    modal.KeyForTest(VK_RETURN); CHECK(connects.size() == 1 && connects[0] == AgentKind::Claude);
    modal.SetStatus(AgentKind::Claude, {ConnectState::Working, L""});
    CHECK(modal.ViewForTest().AnyWorking() && !modal.ViewForTest().ClosesAsLater());
    {   auto n = modal.NodesForTest(); CHECK(HasNode(n, L"Fermer") && !HasNode(n, L"Fermer, connecter un agent plus tard")); }
    CHECK(lastSaid(L"Cliquez sur Installer dans Claude"));
    Pump(400); CHECK(Animator::Get().Animating());   // the ring turns while working
    modal.SetStatus(AgentKind::Claude, {ConnectState::Connected, L""});
    Pump(70); SaveLayer(ink, modal.LayerForTest(), L"w4-window-modal-crossfade-working-to-connected.png");
    CHECK(Settle()); CHECK(modal.ViewForTest().DoneVisible() && lastSaid(L"Claude Desktop connecté"));
    SaveLayer(ink, modal.LayerForTest(), L"w5-window-modal-connected.png");
    modal.SetStatus(AgentKind::Codex, {ConnectState::Failed, L"Configuration de Codex verrouillée. Réessayez ou réglez-la à la main."});
    CHECK(lastSaid(L"Échec : Configuration de Codex verrouillée")); CHECK(Settle());
    SaveLayer(ink, modal.LayerForTest(), L"w6-window-modal-failed.png");
    {   auto n = modal.NodesForTest(); CHECK(HasNode(n, L"Réessayer avec Codex") && HasNode(n, L"Terminé") && HasNode(n, L"Fermer")); }
    // Manual fold from the keyboard: instant; copy JSON.
    while (modal.FocusForTest() != CManual) modal.KeyForTest(VK_TAB);
    modal.KeyForTest(VK_RETURN); CHECK(modal.ViewForTest().manual);
    CHECK(Near(modal.ModalHeightForTest(Motion::Now()), modal.LayoutForTest().height, .01));   // keyboard = instant
    modal.KeyForTest(VK_TAB); modal.KeyForTest(VK_TAB); CHECK(modal.FocusForTest() == CCopyJson);
    modal.KeyForTest(VK_RETURN); CHECK(copied.find(L"\"mcpServers\"") != std::wstring::npos && modal.ViewForTest().copied == CCopyJson && lastSaid(L"Copié"));
    CHECK(Settle()); SaveLayer(ink, modal.LayerForTest(), L"w7-window-modal-manual.png");
    // Local pill: 150 ms intent under the pointer, stays open, Échap folds it first.
    modal.KeyForTest(VK_TAB); CHECK(modal.FocusForTest() == CCopyToml); modal.KeyForTest(VK_TAB); CHECK(modal.FocusForTest() == CLocal); CHECK(!modal.LocalOpenForTest());
    CHECK(PumpUntil([&] { return modal.LocalOpenForTest(); }, 600));
    CHECK(Settle()); SaveLayer(ink, modal.LayerForTest(), L"w8-window-modal-local-unfolded.png");
    modal.KeyForTest(VK_ESCAPE); CHECK(!modal.LocalOpenForTest() && modal.Visible());
    // Terminé (Entrée) closes with « Done » and gives the browser back.
    modal.ActivateForTest(CDone);
    CHECK(!ends.empty() && ends.back() == ConnectEnd::Done && IsWindowEnabled(host));
    CHECK(Settle()); CHECK(!IsWindowVisible(modal.Window()) && !IsWindowVisible(modal.Veil()));
    // Launch again: Échap = « Plus tard ».
    modal.Show(SampleSetup(), clientScreen, false); Settle(); modal.KeyForTest(VK_ESCAPE);
    CHECK(ends.back() == ConnectEnd::Later && IsWindowEnabled(host)); CHECK(Settle());
    // The cross at launch is the same « Plus tard » (same hook, same decline counter on the host side).
    modal.Show(SampleSetup(), clientScreen, false); Settle(); SaveLayer(ink, modal.LayerForTest(), L"w3b-window-modal-launch-cross.png");
    {   const size_t before = ends.size(); modal.ActivateForTest(CHeader); CHECK(ends.size() == before + 1 && ends.back() == ConnectEnd::Later && IsWindowEnabled(host)); CHECK(Settle()); }
    // Opened later (not at launch): the cross is a plain « Fermer » (Closed).
    {   ConnectSetup later = SampleSetup(); later.launch = false; modal.Show(later, clientScreen, false); Settle();
        auto n = modal.NodesForTest(); CHECK(HasNode(n, L"Fermer") && !HasNode(n, L"Fermer, connecter un agent plus tard"));
        modal.KeyForTest(VK_ESCAPE); CHECK(ends.back() == ConnectEnd::Closed); CHECK(Settle()); }
    // Reduced motion: a fade, never scaled.
    modal.Show(SampleSetup(), clientScreen, true); CHECK(Near(modal.PresenceForTest().Sample(Motion::Now(), true).scale, 1, 1e-6)); Settle(); modal.Close(true, ConnectEnd::Closed); Settle();

    {   // Smooth corners: the band and the modal are per-pixel-alpha layers; neither is cut by a window region.
        HRGN probe = CreateRectRgn(0, 0, 0, 0); int surfaces = 0, regions = 0, plain = 0;
        for (HWND w : {banner.Window(), modal.Window()}) {
            if (!w) continue;
            ++surfaces; if (GetWindowRgn(w, probe) != ERROR) ++regions; if (!(GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_LAYERED)) ++plain; }
        DeleteObject(probe);
        CHECKM(surfaces >= 2 && regions == 0 && plain == 0, "annotation surfaces: %d checked, %d with a window region, %d not layered", surfaces, regions, plain);
    }
    // All surfaces at rest: zero wake-ups (band).
    Settle();
    IdleCheck("rest");
    DestroyWindow(host); Pump(50);
}

// ---------------------------------------------------------------------------------------
// V3-A (retours du premier test réel): band slide, one restart line for both apps, the native frame copy.
static void V3Tests(Ink& ink) {
    const UINT dpi = 192; const float k = 2; Canvas c;
    // ---- band: -32 -> 0 + fade, 220 ms (0.32, 0.72, 0, 1); back up in 160 ms; reduced: a 150 ms fade
    {   const Timing t = BannerTiming();
        CHECK(t.enter == 220 && t.exit == 160 && t.reducedEnter == 150 && t.reducedExit == 150 && t.from == 1 && t.to == 1);
        CHECK(Near(BannerOffset(0, false), -32, 1e-4) && Near(BannerOffset(1, false), 0, 1e-4) && Near(BannerOffset(0, true), 0, 1e-4));
        Presence p(t); p.Show(At(0), true, false);
        CHECK(Near(p.Progress(At(0)), 0, 1e-4) && Near(p.Progress(At(220)), 1, 1e-4) && p.Progress(At(60)) > .55f);   // (0.32, 0.72, 0, 1): fast start
        p.Hide(At(100), true, false); const float shown = p.Progress(At(100));
        CHECK(Near(p.Progress(At(100.5)), shown, .02) && Near(p.Progress(At(260)), 0, 1e-4));   // interrupted from the presented value, out in 160 ms
        Presence r(t); r.Show(At(0), true, true); CHECK(Near(r.Progress(At(150)), 1, 1e-4) && Near(BannerOffset(r.Progress(At(40)), true), 0, 1e-4));
        // A strip of the real paint at 0 / 40 / 90 / 220 ms (slides down from under the toolbar).
        const float times[4] = {0, 40, 90, 220}; const float w = 600, h = 4 * (32 + 12) + 12;
        Render(ink, w, h, dpi, 0x171717FF, [&](Ink& i, float, float) {
            for (int f = 0; f < 4; ++f) {
                Presence q(t); q.Show(At(0), true, false); const float pr = q.Progress(At(times[f])), dy = BannerOffset(pr, false);
                const float top = 12 + f * 44.f; i.Target()->PushAxisAlignedClip({0, top, w, top + 32}, D2D1_ANTIALIAS_MODE_ALIASED);
                At_(i, 0, top + dy, [&] { ID2D1Layer* g = nullptr; const bool pushed = i.PushOpacity(pr, g); BannerVisual bv; PaintBanner(i, w, 32, bv); if (pushed) i.PopOpacity(g); });
                i.Target()->PopAxisAlignedClip();
            }
        }, c);
        Save(ink, c, L"v3-6-band-slide-0-40-90-220ms.png");
    }
    // ---- 5.6 one restart line for both apps
    {   ConnectView v = ConnectState_(SampleSetup());
        v.setup.claude = {ConnectState::Connected, L"Redémarrez Claude pour l'activer"}; v.setup.codex = {ConnectState::Connected, L"Redémarrez Codex pour l'activer"};
        CHECK(v.BothRestart() && v.Message(AgentKind::Claude).empty() && v.Message(AgentKind::Codex) == L"Redémarrez Claude et Codex pour les activer");
        const auto l = LayoutConnect(ink, v); CHECK(l.claudeMsg.bottom == l.claudeMsg.top && l.codexMsg.bottom > l.codexMsg.top);
        const float h = ConnectGroupHeight(ink, v, l) + 80;
        Render(ink, 520, h, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { At_(i, 40, 40, [&] { PaintConnectStatic(i, v, l, 0, 0); }); }, c); Save(ink, c, L"v3-5.6-connect-restart-both.png");
        MeasureText(c, k, {40 + 120, 40 + l.codexMsg.top, 40 + 400, 40 + l.codexMsg.bottom}, canvasRgb, 0xFFFFFF99, "5.6 « Redémarrez Claude et Codex pour les activer » 12/16 #FFFFFF99");
        ConnectView one = v; one.setup.codex = {ConnectState::Idle, L""};
        CHECK(!one.BothRestart() && one.Message(AgentKind::Claude) == L"Redémarrez Claude pour l'activer");
    }
    // ---- 2 the frame's native copy: same pixels as the page's edges and label, nothing else
    {   FrameMark m; m.page = {0, 0, 1200, 600}; m.dpr = 2; m.x = 100.25; m.y = 80.5; m.w = 240; m.h = 44; m.lx = 100.25; m.ly = 58.5; m.lw = 152; m.lh = 20;
        m.text = L"button.btn-primary · 240×44"; m.accent = 0x3E67FF;
        const RECT b = SelectionFrame::Bounds(m);
        CHECK(b.left <= LONG(100.25 * 2) && b.top <= LONG(58.5 * 2) && b.right >= LONG(340.25 * 2) && b.bottom >= LONG(124.5 * 2));
        { FrameMark off = m; off.x = off.lx = 2000; const RECT e = SelectionFrame::Bounds(off); CHECK(IsRectEmpty(&e)); }
        const float w = float(b.right - b.left) / 2, h = float(b.bottom - b.top) / 2;
        Render(ink, w, h, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { SelectionFrame::Paint(i, m, {b.left, b.top}, 2); }, c);
        Save(ink, c, L"v3-2-native-frame-copy.png");
        auto at = [&](double cssX, double cssY) { return Px(c.At(int(std::lround(cssX * 2 - b.left)), int(std::lround(cssY * 2 - b.top)))); };
        CHECKM(Close(at(220, 81.5), accentRgb, 4), "top edge accent (%s)", Hex6(at(220, 81.5)).c_str());
        CHECKM(Close(at(101.25, 100), accentRgb, 4), "left edge accent");
        CHECKM(Close(at(220, 100), pageRgb, 1), "inside the edges: nothing (the fill stays in the page)");
        CHECKM(Close(at(103, 60), accentRgb, 4), "label box accent");
        // final pass 1, defect 3 (R45): given the page's label pixels (taken from the screen), the copy shows exactly them
        FrameMark g = m;
        auto pattern = [](RECT r, std::uint32_t seed) { FrameMark::Pixels p; p.at = r; for (LONG y = r.top; y < r.bottom; ++y) for (LONG x = r.left; x < r.right; ++x)
            p.bits.push_back(0xFF000000u | ((std::uint32_t(x * 37 + y * 91) * seed) & 0x00FFFFFFu)); return p; };
        g.leftPx = pattern({LONG(std::lround(g.lx * 2)), LONG(std::lround(g.ly * 2)), LONG(std::lround((g.lx + g.lw) * 2)), LONG(std::lround((g.ly + g.lh) * 2))}, 2654435761u);
        Canvas cg; Render(ink, w, h, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { SelectionFrame::Paint(i, g, {b.left, b.top}, 2); }, cg);
        int off = 0, n = 0; const RECT& r = g.leftPx.at;
        for (LONG y = r.top; y < r.bottom; ++y) for (LONG x = r.left; x < r.right; ++x, ++n)
            if ((cg.At(int(x - b.left), int(y - b.top)) | 0xFF000000u) != g.leftPx.bits[size_t(n)]) ++off;
        CHECKM(off == 0, "R45 native copy: the page's own label pixels, pixel for pixel (%d of %d differ)", off, n);
        // final pass 1: the WHOLE box as on screen (edges, hatches, value labels too): the copy is exactly those pixels
        FrameMark wm = m; wm.whole = pattern(b, 40503u);
        Canvas cw; Render(ink, w, h, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { SelectionFrame::Paint(i, wm, {b.left, b.top}, 2); }, cw);
        int woff = 0, wn = 0;
        for (LONG y = b.top; y < b.bottom; ++y) for (LONG x = b.left; x < b.right; ++x, ++wn)
            if ((cw.At(int(x - b.left), int(y - b.top)) | 0xFF000000u) != wm.whole.bits[size_t(wn)]) ++woff;
        CHECKM(woff == 0, "R45 native copy: the whole box as on screen, pixel for pixel (%d of %d differ)", woff, wn);
    }
}
// Live: the band and the native frame in a real window.
static void V3Windows(Ink& ink, const std::wstring& root) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND host = CreateWindowExW(0, L"AnnotationHarnessHost", L"Annotations harness V3", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 60, 60, 1200, 820, nullptr, nullptr, inst, nullptr);
    CHECK(host != nullptr); if (!host) return;
    ShowWindow(host, SW_SHOW); UpdateWindow(host); Pump(80);
    RECT page{}; GetClientRect(host, &page); page.top = 120;
    const float s = GetDpiForWindow(host) / 96.f;
    // Band in a real window: mid-slide frame, then at rest.
    Banner band; BannerHooks bh; band.Init(inst, host, root, bh); band.SetLiveForTest(0);
    CHECK(band.Show({0, 88, page.right, 120}, false)); Pump(40); SaveLayer(ink, band.LayerForTest(), L"v3-6-window-band-entering.png");
    CHECK(band.PresenceForTest().Moving(Motion::Now()) || band.PresenceForTest().Progress(Motion::Now()) > 0);
    CHECK(Settle()); band.Hide(true); CHECK(Settle()); CHECK(!IsWindowVisible(band.Window()));
    // The native frame: click-through child, above the page, gone when hidden.
    SelectionFrame frame; frame.Init(inst, host, root);
    FrameMark m; m.page = page; m.dpr = s; m.x = 100; m.y = 80; m.w = 240; m.h = 44; m.lx = 100; m.ly = 58; m.lw = 152; m.lh = 20; m.text = L"button.btn-primary · 240×44";
    CHECK(frame.Show(m) && frame.Visible() && (GetWindowLongW(frame.Window(), GWL_EXSTYLE) & (WS_EX_TRANSPARENT | WS_EX_LAYERED)) == (WS_EX_TRANSPARENT | WS_EX_LAYERED));
    SaveLayer(ink, frame.LayerForTest(), L"v3-2-window-native-frame.png");
    frame.Hide(); CHECK(!frame.Visible()); frame.Destroy(); CHECK(!frame.Window());
    DestroyWindow(host); Pump(50);
}


// =======================================================================================
// V3-B (Paper 5.1 – 5.4): entry veil, living band, cursor, spacing inspector.
namespace V3B {
namespace D = NativeChrome::Annotations::Dither;
// The veil's pixels as a bitmap drawn at (x, y) DIP of the current target (same raster as the live window).
static void DrawVeil(Ink& ink, float x, float y, float w, float h, double ms, const D::VeilTimeline& tl) {
    auto* t = ink.Target(); float dx = 96, dy = 96; t->GetDpi(&dx, &dy); const float s = dx / 96.f;
    const int pw = int(std::lround(w * s)), ph = int(std::lround(h * s)); std::vector<std::uint32_t> px(size_t(pw) * ph, 0);
    D::RasterVeil({px.data(), pw, pw, ph}, s, ms / 1000.0, tl.Value(ms), !tl.Reduced());
    ID2D1Bitmap* b = nullptr; const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);
    if (SUCCEEDED(t->CreateBitmap(D2D1::SizeU(UINT32(pw), UINT32(ph)), px.data(), UINT32(pw * 4), props, &b))) t->DrawBitmap(b, D2D1::RectF(x, y, x + pw / s, y + ph / s), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    Release(b);
}
static void Band(Ink& i, float w, double seconds, float amp, float reminder = 0, bool beta = false) {
    BannerVisual bv; bv.texture = true; bv.waveSeconds = seconds; bv.waveAmp = amp; bv.reminder = reminder; bv.beta = beta; PaintBanner(i, w, 32, bv);
}
// Pixels of a band render that differ between the wave and the base texture alone, per column range (device px).
struct Diff { int inText = 0, inMiddle = 0; };
static Diff WaveDiff(Ink& ink, float w, double seconds, float reminder, bool beta, UINT dpi) {
    Canvas a, b; const float k = dpi / 96.f;
    Render(ink, w, 32, dpi, 0x171717FF, [&](Ink& i, float, float) { Band(i, w, seconds, 1, reminder, beta); }, a);
    Render(ink, w, 32, dpi, 0x171717FF, [&](Ink& i, float, float) { Band(i, w, seconds, 0, reminder, beta); }, b);
    const auto l = LayoutBanner(ink, w, beta); const auto z = WaveZones(l, beta, reminder);
    Diff d;
    for (unsigned y = 0; y < a.height; ++y) for (unsigned x = 0; x < a.width; ++x) {
        if (a.At(int(x), int(y)) == b.At(int(x), int(y))) continue;
        const float xd = (x + .5f) / k;
        if (xd <= z.left || xd >= z.right) ++d.inText; else ++d.inMiddle;
    }
    return d;
}
static void Static(Ink& ink) {
    const UINT dpi = 192; const float k = 2; Canvas c;
    // ---- 5.2 three instants of the same loop (t = 0, 1.3, 2.7 s), and the reminder variant
    {   const double ts[3] = {0, 1.3, 2.7}; const float w = 1180, h = 3 * (32 + 20) + 20;
        Render(ink, w, h, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { for (int f = 0; f < 3; ++f) At_(i, 0, 20 + f * 52.f, [&] { Band(i, w, ts[f], 1); }); }, c);
        Save(ink, c, L"v3b-5.2-band-wave-0-1.3-2.7s.png");
        Render(ink, w, 32 + 40, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { At_(i, 0, 20, [&] { Band(i, w, 1.3, 1, 1); }); }, c);
        Save(ink, c, L"v3b-5.2-band-wave-reminder.png");
        Render(ink, w, 32 + 40, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { At_(i, 0, 20, [&] { Band(i, w, 1.3, 0); }); }, c);
        Save(ink, c, L"v3b-5.2-band-reduced-still-texture.png");
    }
    // ---- 5.2 proof: the wave is ZERO under every label (pixels identical to the base texture there), at 12 instants
    //      of the loop, without and with the reminder, with BETA, at 2x and 1.25x; and present in the middle.
    {   int text = 0, middle = 0, renders = 0;
        for (UINT dpi2 : {UINT(192), UINT(120)}) for (int r = 0; r < 2; ++r) for (int b = 0; b < 2; ++b) for (int f = 0; f < 12; ++f) {
            const Diff d = WaveDiff(ink, 1180, f * D::Wave::period / 12, float(r), b == 1, dpi2); text += d.inText; middle += d.inMiddle; ++renders;
            CHECKM(d.inText == 0, "wave under a label: %d pixels (dpi %u, reminder %d, beta %d, t %.2f s)", d.inText, dpi2, r, b, f * D::Wave::period / 12);
        }
        CHECKM(middle > renders * 50, "the wave shows between the labels (%d pixels over %d renders)", middle, renders);
        std::printf("V3-B wave under the labels: %d differing pixels in the text zones, %d in the middle (%d renders)\n", text, middle, renders);
        report << L"\"v3b_waveUnderLabels\":{\"renders\":" << renders << L",\"pixelsInTextZones\":" << text << L",\"pixelsInMiddle\":" << middle << L"},";
        // The zones come from the label rectangles: wider labels move them.
        const auto l = LayoutBanner(ink, 1180, false); const auto z = WaveZones(l, false, 0), zr = WaveZones(l, false, 1), zb = WaveZones(LayoutBanner(ink, 1180, true), true, 0);
        CHECK(Near(z.left, l.label.right, 1e-3) && Near(z.right, l.quit.left, 1e-3) && Near(zr.right, l.remind.left, 1e-3) && zb.left > z.left);
    }
    // ---- 5.2 the wave frames' fast path (pixels written in place, no Direct2D) = the full Direct2D frame there
    for (UINT dpi2 : {UINT(96), UINT(120), UINT(144), UINT(192)}) for (int rem = 0; rem < 2; ++rem) for (double t : {0.4, 1.7, 3.3}) {
        Canvas full; const float w = 1180, kk = dpi2 / 96.f;
        Render(ink, w, 32, dpi2, 0x171717FF, [&](Ink& i, float, float) { Band(i, w, t, 1, float(rem), true); }, full);
        const auto l = LayoutBanner(ink, w, true); const auto z = WaveZones(l, true, float(rem));
        const RECT span{LONG(std::ceil(z.left * kk)), 0, LONG(std::floor(z.right * kk)), LONG(full.height)};
        std::vector<std::uint32_t> px(full.pixels); RasterBandSpan(px.data(), int(full.width), span, w, 32, kk, t, 1, z);
        int worst = 0, over2 = 0;
        for (LONG y = span.top; y < span.bottom; ++y) for (LONG x = std::max(0L, span.left); x < std::min(LONG(full.width), span.right); ++x) {
            const std::uint32_t a = full.pixels[size_t(y) * full.width + x], b = px[size_t(y) * full.width + x];
            for (int sh = 0; sh < 32; sh += 8) { const int dd = std::abs(int((a >> sh) & 255) - int((b >> sh) & 255)); worst = std::max(worst, dd); if (dd > 2) ++over2; }
        }
        CHECKM(over2 == 0, "wave span fast path vs Direct2D at %u dpi, reminder %d, t %.1f: worst %d levels, %d channel values over 2", dpi2, rem, t, worst, over2);
        if (dpi2 == 120 && rem == 0 && t == 1.7) { std::printf("V3-B wave span fast path vs Direct2D (120 dpi): worst %d levels\n", worst); report << L"\"v3b_spanVsD2dWorst\":" << worst << L","; }
    }
    // ---- 5.2 entry timeline strip: band + veil at 0 / 60 / 140 / 220 / 360 / 500 ms (scene, page reduced to 400 DIP)
    {   const double ts[6] = {0, 60, 140, 220, 360, 500}; const float cw = 590, ch = 117 + 300;
        D::VeilTimeline tl; tl.Start(false);
        Render(ink, cw * 3, ch * 2, 96 * 1, 0x0E0E0EFF, [&](Ink& i, float, float) {
            for (int f = 0; f < 6; ++f) At_(i, (f % 3) * cw, (f / 3) * ch, [&] {
                i.Target()->PushAxisAlignedClip({0, 0, cw - 6, ch - 6}, D2D1_ANTIALIAS_MODE_ALIASED);
                PaintChrome(i, 1180); PaintPage(i, 0, 117, 1180, 300, false);
                Presence q(BannerTiming()); q.Show(At(0), true, false); const float pr = q.Progress(At(ts[f]));
                At_(i, 0, 85 + BannerOffset(pr, false), [&] { ID2D1Layer* g = nullptr; const bool pushed = i.PushOpacity(pr, g); Band(i, 1180, ts[f] / 1000.0, D::Amplitude(ts[f], false)); if (pushed) i.PopOpacity(g); });
                DrawVeil(i, 0, 117, 1180, 300, ts[f], tl);
                i.Target()->PopAxisAlignedClip(); });
        }, c);
        Save(ink, c, L"v3b-5.2-entry-strip-0-60-140-220-360-500ms.png");
    }
    // ---- 5.1 the peak (~180 ms): scene at 1180 x 815, and the reduced variant (no texture, 50 %)
    {   D::VeilTimeline tl; tl.Start(false);
        Render(ink, 1180, 815, dpi, 0x171717FF, [&](Ink& i, float w, float h) {
            PaintChrome(i, w); At_(i, 0, 85, [&] { Band(i, w, .18, D::Amplitude(180, false)); }); PaintPage(i, 0, 117, w, h - 117, false);
            DrawVeil(i, 0, 117, w, h - 117, 180, tl); }, c);
        Save(ink, c, L"v3b-5.1-entry-peak-180ms.png");
        const Rgb top = Px(c.At(int(600 * k), int((117 + 2) * k))), mid = Px(c.At(int(400 * k), int(600 * k)));
        CHECKM(top.b > top.r + 8, "5.1 the wash at the top of the page is blue (%s)", Hex6(top).c_str());
        CHECKM(Close(mid, pageRgb, 2), "5.1 nothing in the middle of the page (%s)", Hex6(mid).c_str());
        D::VeilTimeline rt; rt.Start(true);
        Render(ink, 1180, 815, dpi, 0x171717FF, [&](Ink& i, float w, float h) {
            PaintChrome(i, w); At_(i, 0, 85, [&] { Band(i, w, 0, 0); }); PaintPage(i, 0, 117, w, h - 117, false);
            DrawVeil(i, 0, 117, w, h - 117, 150, rt); }, c);
        Save(ink, c, L"v3b-5.1-entry-reduced-150ms.png");
        CHECK(Near(rt.Value(150), .5, 1e-4));
        // Reduced: no dot anywhere (the wash only): the top rows are smooth (no pixel darker than its neighbours by a dot).
        int dots = 0; for (int x = 0; x < 2000; x += 1) { const Rgb p = Px(c.At(x, int((117 + 48) * k))), q = Px(c.At(x + 1, int((117 + 48) * k))); if (std::fabs(p.r - q.r) > 6) ++dots; }   // the header's empty row
        CHECKM(dots == 0, "reduced motion: no texture (%d dot edges)", dots);
        // Interruption at 60 ms: from the presented value, out in 160 ms; the values on a strip.
        D::VeilTimeline it; it.Start(false); const float v60 = it.Value(60); it.Interrupt(60);
        CHECK(Near(it.Value(60), v60, 1e-5) && it.Value(140) < v60 && it.Value(220) == 0 && Near(it.EndMs(), 220, 1e-6));
        const double ts[4] = {30, 60, 120, 200};
        Render(ink, 4 * 300, 200, 192, 0xF4F4F3FF, [&](Ink& i, float, float) { for (int f = 0; f < 4; ++f) { i.Fill({f * 300.f, 0, f * 300.f + 296, 200}, 0, Hex(0xF4F4F3FF)); DrawVeil(i, f * 300.f, 0, 296, 200, ts[f], it); } }, c);
        Save(ink, c, L"v3b-5.2-entry-interrupted-at-60ms-30-60-120-200.png");
        std::printf("V3-B veil: natural 60 ms %.3f, 140 ms %.3f, 220 ms %.3f, 360 ms %.3f, 500 ms %.3f; interrupted at 60 ms: 120 ms %.3f, 220 ms %.3f\n",
                    tl.Value(60), tl.Value(140), tl.Value(220), tl.Value(360), tl.Value(500), it.Value(120), it.Value(220));
    }
    // ---- 5.3 the inspector as the native copy draws it (same geometry as the page): Paper's summary card.
    //      ONE padding ring + ONE gap union, hatched in the viewport's phase; order hatches -> edges -> element label ->
    //      value labels; no seam at the ring's four inner corners.
    {   FrameMark m; m.page = {0, 0, 1240, 680}; m.dpr = 2; m.accent = 0x3E67FF;
        m.x = 140; m.y = 96; m.w = 340; m.h = 179; m.lx = 138; m.ly = 74; m.lw = 120; m.lh = 20; m.text = L"div.summary · 340×179";
        m.hasPad = true; const double pad[8] = {141, 97, 338, 177, 161, 117, 298, 137}; std::copy(pad, pad + 8, m.pad); m.radius = 9; m.vw = 620; m.vh = 340;
        m.gaps = {{161, 137, 298, 14}, {161, 171, 298, 14}, {161, 205, 298, 14}, {161, 220, 298, 14}};
        m.labels = {{0, 300, 100, 20, 14, L"20"}, {0, 142, 178, 18, 14, L"20"}, {1, 300, 137, 20, 14, L"14"}};
        auto card = [&](Ink& i) {
            const D2D1_RECT_F cardBox{140, 96, 480, 275}; i.Fill(cardBox, 10, Hex(0xFFFFFFFF)); i.Stroke(cardBox, 10, Hex(0xE2E2E0FF), 1);
            i.Text(L"Récapitulatif", F(15, 20, 600), {161, 117, 480, 137}, Hex(0x1C1C1CFF));
            const wchar_t* items[2] = {L"Tasse grès, sable", L"Carnet A5 ligné"};
            for (int r = 0; r < 2; ++r) { const float y = 151 + r * 34.f; i.Text(items[r], F(14, 20), {161, y, 459, y + 20}, Hex(0x4A4A4AFF)); i.Text(L"24,00 €", F(14, 20), {161, y, 459, y + 20}, Hex(0x4A4A4AFF), false, Align::Trailing); }
            i.Fill({161, 219, 459, 220}, 0, Hex(0xE2E2E0FF));
            i.Text(L"Total", F(15, 20, 600), {161, 234, 459, 254}, Hex(0x1C1C1CFF)); i.Text(L"48,00 €", F(15, 20, 600), {161, 234, 459, 254}, Hex(0x1C1C1CFF), false, Align::Trailing);
        };
        Render(ink, 620, 340, dpi, 0xF3F3F2FF, [&](Ink& i, float, float) { card(i); SelectionFrame::Paint(i, m, {0, 0}, 2); }, c);
        Save(ink, c, L"v3b-5.3-spacing-inspector.png");
        int blue = 0, pink = 0;
        for (int y = int(98 * k); y < int(116 * k); ++y) for (int x = int(160 * k); x < int(280 * k); ++x) { const Rgb p = Px(c.At(x, y)); if (p.b > 230 && p.r < 200 && p.g < 210) ++blue; }
        for (int y = int(138 * k); y < int(150 * k); ++y) for (int x = int(330 * k); x < int(450 * k); ++x) { const Rgb p = Px(c.At(x, y)); if (p.r > 220 && p.g < 200 && p.b > 200 && p.r > p.g + 30) ++pink; }
        CHECKM(blue > 300, "5.3 blue hatches in the top padding (%d px)", blue); CHECKM(pink > 200, "5.3 pink hatches in the gap (%d px)", pink);
        Sample(c, k, 303, 101, accentRgb, "5.3 padding label --color-browser-accent", 4);
        Sample(c, k, 303, 138, Solid(0xD63E92), "5.3 gap label #D63E92", 4);
        // No seam: the hatch pattern is invariant along its stripes (direction (-1, 1) for 135 deg); around each inner
        // corner of the ring (where the old top / side bands met), every pixel equals the pixel 4 device px down-left.
        struct Corner { const char* name; float x0, x1, y0, y1; } corners[4] = {{"top-left", 150, 158, 110, 124}, {"top-right", 462, 470, 110, 124},
                                                                              {"bottom-left", 150, 158, 247, 261}, {"bottom-right", 462, 470, 247, 261}};
        int seamWorst = 0;
        for (const auto& cn : corners) {
            int worst = 0, over = 0, n = 0;
            for (int y = int(cn.y0 * k); y < int(cn.y1 * k); ++y) for (int x = int(cn.x0 * k); x < int(cn.x1 * k); ++x) {
                const Rgb a = Px(c.At(x, y)), b = Px(c.At(x - 4, y + 4)); ++n;
                const int d = int(std::max({std::fabs(a.r - b.r), std::fabs(a.g - b.g), std::fabs(a.b - b.b)}));
                worst = std::max(worst, d); if (d > 3) ++over;
            }
            seamWorst = std::max(seamWorst, worst);
            CHECKM(over == 0, "5.3 seam at the %s inner corner: %d of %d pixels differ by > 3 levels along the stripes (worst %d)", cn.name, over, n, worst);
        }
        std::printf("V3-B padding ring corners: worst difference along the stripes %d levels (4 corners)\n", seamWorst);
        report << L"\"v3b_cornerSeamWorst\":" << seamWorst << L",";
        // Order: a value label over the frame's top edge and over the element label stays whole on top of both.
        FrameMark o = m; o.labels = {{1, 300, 90, 20, 14, L"10"}, {1, 150, 78, 20, 14, L"4"}};
        Render(ink, 620, 340, dpi, 0xF3F3F2FF, [&](Ink& i, float, float) { card(i); SelectionFrame::Paint(i, o, {0, 0}, 2); }, c);
        Save(ink, c, L"v3b-5.3-value-labels-on-top.png");
        Sample(c, k, 302, 97, Solid(0xD63E92), "5.3 value label above the frame's edge", 6);
        Sample(c, k, 152, 84, Solid(0xD63E92), "5.3 value label above the element label", 6);
        // Thin sides: a 4 px padding keeps a whole 14 px label (the page puts it flush with the inner edge: inward).
        FrameMark t = m; const double thin[8] = {141, 97, 338, 177, 151, 101, 318, 169}; std::copy(thin, thin + 8, t.pad); t.gaps.clear();
        t.labels = {{0, 300, 101 - 4, 20, 14, L"4"}}; t.labels[0].y = 97;   // flush with the band's top, overflowing inward
        Render(ink, 620, 340, dpi, 0xF3F3F2FF, [&](Ink& i, float, float) { card(i); SelectionFrame::Paint(i, t, {0, 0}, 2); }, c);
        Save(ink, c, L"v3b-5.3-thin-side-label-inward.png");
        Sample(c, k, 302, 109, accentRgb, "5.3 thin side: the label's lower part inside the element", 6);
    }
    // ---- 5.4 the cursor « viseur » (direction B): reference render of the SVG at 32 / 48 / 64 on light and dark;
    //      the hot spot (16, 16) is the blue dot's centre
    {   auto* f = ink.Factory();
        ID2D1PathGeometry* cross = DialogKit::SvgGeometry(f, D::Cursor::strokes);
        CHECK(cross != nullptr && D::Cursor::hotX == 16 && D::Cursor::hotY == 16);
        ID2D1StrokeStyle* cap = nullptr; f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &cap);
        Render(ink, 3 * 80 + 20, 2 * 80 + 20, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) {
            auto* t = i.Target(); const int sizes[3] = {32, 48, 64};
            for (int row = 0; row < 2; ++row) for (int n = 0; n < 3; ++n) {
                const float x0 = 10 + n * 80.f, y0 = 10 + row * 80.f; i.Fill({x0, y0, x0 + 76, y0 + 76}, 8, Hex(row ? 0x171717FF : 0xF3F3F2FF));
                const float sc = sizes[n] / 32.f / 2;   // DIP at 2x = device px / 2
                D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
                t->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * D2D1::Matrix3x2F::Translation(x0 + 6, y0 + 6) * base);
                if (cross) { t->DrawGeometry(cross, i.Brush({1, 1, 1, 1}), 4, cap); t->DrawGeometry(cross, i.Brush(Hex(0x1C1C1CFF)), 1.6f, cap); }
                ID2D1EllipseGeometry* dot = nullptr; f->CreateEllipseGeometry(D2D1::Ellipse({16, 16}, 2.6f, 2.6f), &dot);
                if (dot) { t->FillGeometry(dot, i.Brush(Hex(0x3E67FFFF))); t->DrawGeometry(dot, i.Brush({1, 1, 1, 1}), 1.2f); dot->Release(); }
                t->SetTransform(base);
            }
        }, c);
        Save(ink, c, L"v3b-5.4-cursor-viseur-32-48-64.png");
        // the centre is blue, the empty ring around it shows the background, the strokes are dark on a white halo
        const float cx = 10 + 80 + 6 + 16 * 48 / 32.f / 2, cy = 10 + 6 + 16 * 48 / 32.f / 2;
        Sample(c, k, cx, cy, accentRgb, "5.4 viseur centre #3E67FF (48 px)", 6);
        Sample(c, k, cx + 4 * 48 / 32.f / 2, cy + 4 * 48 / 32.f / 2, Solid(0xF3F3F2), "5.4 viseur: background between the dot and the strokes (48 px)", 6);
        if (cross) cross->Release(); if (cap) cap->Release();
    }
}

// Live windows: the band's wave clock (30 frames/s at most, paused in the background, zero wake-up outside the
// mode), the entry veil (click-through, destroyed after 500 ms, interruptible, reduced), « Rouvrir » end to end.
static void Windows(Ink& ink, const std::wstring& root) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND host = CreateWindowExW(0, L"AnnotationHarnessHost", L"Annotations harness V3-B", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 60, 60, 1200, 820, nullptr, nullptr, inst, nullptr);
    CHECK(host != nullptr); if (!host) return;
    ShowWindow(host, SW_SHOW); UpdateWindow(host); SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); ForceForeground(host); Pump(100);
    RECT client{}; GetClientRect(host, &client); const float s = GetDpiForWindow(host) / 96.f;
    RECT page{0, LONG(std::lround(117 * s)), client.right, client.bottom};
    HWND web = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_WHITERECT, page.left, page.top, page.right - page.left, page.bottom - page.top, host, nullptr, inst, nullptr);
    CHECK(web != nullptr);
    // ---- band: the wave at <= 30 frames/s once landed
    Banner band; BannerHooks bh; band.Init(inst, host, root, bh); band.SetLiveForTest(1);
    const RECT bandRect{0, LONG(std::lround(85 * s)), client.right, LONG(std::lround(117 * s))};
    const unsigned long long f0 = Animator::Get().Frames();
    CHECK(band.Show(bandRect, false));
    const DWORD t0 = GetTickCount(); Pump(300); const auto w0 = band.WaveFramesForTest(); const DWORD t1 = GetTickCount();
    band.ResetRenderStatsForTest(); const auto ticks0 = Animator::Get().Frames();
    Pump(2000); const auto w1 = band.WaveFramesForTest(); const DWORD t2 = GetTickCount();
    std::printf("V3-B band frame cost: %.2f ms per presented frame (paint + texture + layered update)\n", band.AverageRenderMsForTest());
    report << L"\"v3b_bandFrameMs\":" << band.AverageRenderMsForTest() << L",";
    const double rate = double(w1 - w0) * 1000.0 / double(t2 - t1);
    std::printf("V3-B band wave: %llu frames in %lu ms => %.1f frames/s (animator ticks %llu over %lu ms, wave timers %llu)\n", (unsigned long long)(w1 - w0), (unsigned long)(t2 - t1), rate, Animator::Get().Frames() - f0, (unsigned long)(t2 - t0), band.WaveTimersForTest());
    CHECKM(rate <= 31.5 && rate >= 20, "wave at %.1f frames/s (30 at most)", rate);
    {   const double tickRate = double(Animator::Get().Frames() - ticks0) * 1000.0 / double(t2 - t1);
        std::printf("V3-B display-clock ticks while only the wave runs: %.1f per second\n", tickRate);
        CHECKM(tickRate <= 2.5 * 31.5, "the clock is not kept running between wave frames (%.1f ticks/s)", tickRate);
        report << L"\"v3b_waveClockTicksPerSecond\":" << tickRate << L","; }
    report << L"\"v3b_waveFps\":" << rate << L",";
    SaveLayer(ink, band.LayerForTest(), L"v3b-5.2-window-band-wave-a.png"); Pump(700); SaveLayer(ink, band.LayerForTest(), L"v3b-5.2-window-band-wave-b.png");
    {   // the live band's pixels: identical between two instants in the text zones, different in the middle
        const auto l = band.LayoutForTest(); const auto z = WaveZones(l, true, 0);
        auto a = band.LayerForTest().Pixels(); Pump(500); auto b = band.LayerForTest().Pixels(); const RECT r = band.LayerForTest().Rect(); const int w = r.right - r.left, h = r.bottom - r.top;
        int text = 0, middle = 0;
        if (a.size() == b.size() && a.size() == size_t(w) * h) for (int y = 0; y < h - 1; ++y) for (int x = 0; x < w; ++x) { if (a[size_t(y) * w + x] == b[size_t(y) * w + x]) continue; const float xd = (x + .5f) / s; if (xd <= z.left || xd >= z.right) ++text; else ++middle; }
        std::printf("V3-B live band, two instants 500 ms apart: %d pixels changed in the text zones, %d in the middle\n", text, middle);
        CHECKM(text == 0 && middle > 100, "live band: %d changed pixels under the labels, %d between", text, middle);
        report << L"\"v3b_liveBandChanges\":{\"textZones\":" << text << L",\"middle\":" << middle << L"},";
    }
    // background / minimised: the clock stops (no frame, no wake-up), the wave time does not run
    band.SetLiveForTest(0); Pump(100); const double paused = band.WaveSecondsForTest(Motion::Now());
    IdleCheck("v3b band in the background");
    CHECK(Near(band.WaveSecondsForTest(Motion::Now()), paused, 1e-6));
    band.SetLiveForTest(1); Pump(200); CHECK(band.WaveFramesForTest() > w1 && band.WaveSecondsForTest(Motion::Now()) < paused + .5);
    // the real window state: minimised => paused
    band.SetLiveForTest(-1); ShowWindow(host, SW_MINIMIZE); band.WindowStateChanged(); Pump(150);
    IdleCheck("v3b band, window minimised");
    ShowWindow(host, SW_RESTORE); ForceForeground(host); band.WindowStateChanged(); Pump(200);
    std::printf("V3-B band restored, foreground %d: wave frames %llu\n", GetForegroundWindow() == host ? 1 : 0, band.WaveFramesForTest());
    band.SetLiveForTest(1);
    // exit: zero wake-up outside the mode
    band.Hide(true); CHECK(Settle()); IdleCheck("v3b outside the mode after the wave");
    // reduced motion: still texture, no frame once landed
    CHECK(band.Show(bandRect, true)); CHECK(Settle()); IdleCheck("v3b band in reduced motion (still texture)");
    band.Hide(false); CHECK(Settle());
    // ---- entry veil: click-through, never activated, destroyed after 500 ms
    EntryVeil veil; veil.Init(inst, host, root);
    const unsigned created = Layer::Created();
    double ended = -1; unsigned long long frames = 0; veil.OnEnded([&](double ms, unsigned long long n, double) { ended = ms; frames = n; });
    const auto v0 = Motion::Now(); CHECK(veil.Start(page, false));
    CHECK(Layer::Created() == created + 1 && veil.Window() && (GetWindowLongW(veil.Window(), GWL_EXSTYLE) & (WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE)) == (WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE));
    PumpUntil([&] { return veil.LastMs() >= 170; }, 1000); SaveLayer(ink, veil.LayerForTest(), L"v3b-5.1-window-veil-near-peak.png");
    {   POINT pt{(page.left + page.right) / 2, page.top + 40}; ClientToScreen(host, &pt);
        CHECKM(WindowFromPoint(pt) != veil.Window(), "the veil takes no click (WindowFromPoint skips it)"); }
    PumpUntil([&] { return !veil.Running(); }, 2000);
    const double real = std::chrono::duration<double, std::milli>(Motion::Now() - v0).count();
    std::printf("V3-B entry veil: %llu frames, ended %.1f ms after the start (timeline 500), peak %.3f, average frame %.2f ms (raster %.2f), worst %.2f ms, observed %.1f ms\n",
                frames, ended, veil.Peak(), veil.AverageFrameMs(), veil.AverageRasterMs(), veil.WorstFrameMs(), real);
    CHECKM(ended >= 500 && ended < 560, "veil destroyed %.1f ms after the start", ended);
    CHECK(!veil.Running() && !veil.Window() && veil.Peak() > .97f);
    report << L"\"v3b_veil\":{\"frames\":" << frames << L",\"endedMs\":" << ended << L",\"peak\":" << veil.Peak() << L",\"avgFrameMs\":" << veil.AverageFrameMs() << L",\"worstFrameMs\":" << veil.WorstFrameMs() << L"},";
    // interruption at ~60 ms: out from the value on screen, ended ~160 ms later
    CHECK(veil.Start(page, false)); PumpUntil([&] { return veil.LastMs() >= 55; }, 1000);
    const float atInterrupt = veil.LastValue(); veil.Interrupt();
    PumpUntil([&] { return !veil.Running(); }, 2000);
    std::printf("V3-B veil interrupted at %.3f: ended %.1f ms after the start, peak %.3f\n", atInterrupt, ended, veil.Peak());
    CHECKM(ended < 300 && veil.Peak() < .999f && !veil.Window(), "interrupted veil: ended %.1f ms, peak %.3f", ended, veil.Peak());
    // reduced: 50 %, no texture, 300 ms
    CHECK(veil.Start(page, true)); PumpUntil([&] { return veil.LastMs() >= 140; }, 1000); SaveLayer(ink, veil.LayerForTest(), L"v3b-5.1-window-veil-reduced.png");
    PumpUntil([&] { return !veil.Running(); }, 2000);
    CHECKM(veil.Peak() <= .5001f && veil.Peak() > .45f && ended >= 300 && ended < 360, "reduced veil: peak %.3f, ended %.1f ms", veil.Peak(), ended);
    IdleCheck("v3b after the entry veil");
    DestroyWindow(host); Pump(50);
}
} // namespace V3B

// ---------------------------------------------------------------------------------------
// Profile colour (design/annotation-profile-color-plan.md): the mode follows the window's profile accent.
// Static: band, note, launcher, toolbar button, page frame (light / dark page), veil — Personnel (Paper values) and
// real profile fills; pixels sampled against the expected tone, rendered contrast measured. Windows: a live band and
// launcher repaint in the new accent on AccentChanged() alone (no input), with zero wake-ups afterwards.
namespace ProfileAccent {
struct Profile { const char* name; std::uint32_t fill; };
static std::uint32_t FillOf(int hue, float chroma) { const auto f = ProfileColor::AccentFill(hue, chroma); return (std::uint32_t(f.r) << 16) | (std::uint32_t(f.g) << 8) | f.b; }
static std::vector<Profile> Profiles() {
    return {{"personnel", AccentTone::reference}, {"vert", FillOf(150, .14f)}, {"ambre", FillOf(55, .14f)}, {"rose", FillOf(0, .14f)},
            {"violet-max", FillOf(300, .16f)}, {"bleu-gris-faible", FillOf(240, .02f)}, {"gris-intensite-0", FillOf(0, 0)},
            {"violet-288-10-review", FillOf(288, .10f)}};   // the independent review's reproducer (BETA 4.32:1 on e204fd6)
}
// Contrast row by row (review of e204fd6): the band's background changes from row to row (its gradient lightens
// downwards), so one background sample and the best text pixel prove nothing. For every device row of `box`, the
// backdrop is the rendered pixel of that row at `bgX` (a glyph-free column), and every OPAQUE glyph pixel of the row
// (within 6 levels of the token: its core, not its antialiased edge) is measured against it. Every such row must pass.
static void MeasureRows(const Canvas& c, float scale, D2D1_RECT_F box, float bgX, std::uint32_t token, const std::string& name, double minimum = 4.5) {
    const Rgb fg = Solid(token); double worst = 99; int rows = 0, worstRow = -1; Rgb worstBg{}, worstFg{};
    for (int y = int(box.top * scale); y < int(box.bottom * scale); ++y) {
        const Rgb bg = Px(c.At(int(bgX * scale), y)); bool any = false; double rowWorst = 99; Rgb rowFg{};
        for (int x = int(box.left * scale); x < int(box.right * scale); ++x) {
            const Rgb q = Px(c.At(x, y)); if (!Close(q, fg, 6)) continue;
            any = true; const double r = Ratio(q, bg); if (r < rowWorst) { rowWorst = r; rowFg = q; }
        }
        if (!any) continue;
        ++rows; if (rowWorst < worst) { worst = rowWorst; worstRow = y; worstBg = bg; worstFg = rowFg; }
    }
    contrasts.push_back({name + " — worst glyph row (px " + std::to_string(worstRow) + ")", worst, worst, Hex6(worstFg), Hex6(worstBg)});
    CHECKM(rows >= 6, "%s: only %d glyph rows found", name.c_str(), rows);
    CHECKM(worst >= minimum, "%s: row %d glyph %s on %s = %.3f:1 < %.1f", name.c_str(), worstRow, Hex6(worstFg).c_str(), Hex6(worstBg).c_str(), worst, minimum);
}
static void SetAccent(std::uint32_t rgb) { Design::Accent::Set(rgb); }
static Rgb Of(std::uint32_t rgb) { return Solid(rgb); }
static std::string Name(const char* what, const Profile& p) { return std::string(what) + " [" + p.name + " " + Hex6(Of(p.fill)) + "]"; }
static void Static(Ink& ink) {
    const UINT dpi = 192; const float k = 2; Canvas c;
    for (const auto& p : Profiles()) {
        SetAccent(p.fill); const auto tone = AccentTone::BandFor(p.fill);
        const std::wstring tag = Wide(p.name);
        // ---- band (texture on, wave at full amplitude)
        BannerVisual bv; bv.beta = true; bv.texture = true; bv.waveSeconds = 1.3; bv.waveAmp = 1; bv.accent = p.fill;
        Render(ink, 1180, 32, dpi, 0x171717FF, [&](Ink& i, float w, float h) { PaintBanner(i, w, h, bv); }, c);
        Save(ink, c, (L"pc-band-" + tag + L".png").c_str());   // the live look (texture + wave)
        bv.texture = false;   // measured without the dither dots (as the V2 boards): the solid backdrop under the texts
        Render(ink, 1180, 32, dpi, 0x171717FF, [&](Ink& i, float w, float h) { PaintBanner(i, w, h, bv); }, c);
        {   const auto l = LayoutBanner(ink, 1180, true);
            const Rgb mid = Of(AccentTone::Over(tone.gradientEnd, AccentTone::Paper::gradientAlpha * .5, 0x171717));
            MeasureText(c, k, {l.label.left, 8, l.label.right, 24}, mid, (tone.label << 8) | 0xFF, Name("band « Mode Annotation » 14/20", p).c_str());
            const Rgb chip = Px(c.At(int((l.beta.left + 1.5f) * k), int(16 * k)));
            Sample(c, k, l.beta.left + 1.5f, 16, Of(AccentTone::Over(p.fill, .16, AccentTone::Over(tone.gradientEnd, AccentTone::Paper::gradientAlpha * .5, 0x171717))), Name("band BETA chip accent 16 %", p).c_str(), 4);
            MeasureText(c, k, {l.beta.left + 3, l.beta.top + 2, l.beta.right - 3, l.beta.bottom - 2}, chip, (tone.beta << 8) | 0xFF, Name("band « BETA » 12 medium", p).c_str());
            MeasureRows(c, k, {l.label.left, 0, l.label.right, 32}, l.label.left - 3, tone.label, Name("band « Mode Annotation » per row", p));
            MeasureRows(c, k, {l.beta.left + 3, l.beta.top, l.beta.right - 3, l.beta.bottom}, l.beta.left + 1.5f, tone.beta, Name("band « BETA » per row", p));
            Save(ink, c, (L"pc-band-solid-" + tag + L".png").c_str());   // the measured render (texture off)
        }
        // ---- the toolbar « Annoter » (active)
        {   ToolbarVisual tv; tv.active = true;
            Render(ink, 40, 40, dpi, 0x171717FF, [&](Ink& i, float, float) { PaintToolbarButton(i.Target(), i.Factory(), i.Brush({1, 1, 1, 1}), {8, 6, 32, 30}, tv); }, c);
            Save(ink, c, (L"pc-toolbar-" + tag + L".png").c_str());
            const Rgb plate = Of(AccentTone::Over(p.fill, .16, 0x171717));
            Sample(c, k, 10, 18, plate, Name("toolbar « Annoter » active plate accent 16 %", p).c_str(), 3);
            Sample(c, k, 20, 33, Of(p.fill), Name("toolbar « Annoter » active bar = fill", p).c_str(), 3);
            MeasureText(c, k, {13, 11, 27, 25}, plate, (p.fill << 8) | 0xFF, Name("toolbar « Annoter » mark on its plate (1.4.11)", p).c_str(), 3);
        }
        // ---- the page frame (native copy = what the page script paints) on light and dark pages
        for (std::uint32_t page : {0xFFFFFFu, 0xF4F4F3u, 0x000000u, 0x1C1C1Cu}) {
            FrameMark m; m.page = {0, 0, 800, 400}; m.dpr = 2; m.accent = p.fill;
            m.x = 100; m.y = 80; m.w = 240; m.h = 44; m.lx = 100; m.ly = 58; m.lw = 152; m.lh = 20; m.text = L"button.btn-primary · 240×44";
            Render(ink, 400, 200, dpi, (page << 8) | 0xFF, [&](Ink& i, float, float) { SelectionFrame::Paint(i, m, {0, 0}, 2); }, c);
            char what[96]; std::snprintf(what, sizeof what, "page frame edge 2 px on page %s (1.4.11)", Hex6(Of(page)).c_str());
            Sample(c, k, 100 + 120, 80 + .5f, Of(p.fill), Name(what, p).c_str(), 3);
            const double edge = Ratio(Px(c.At(int((100 + 120) * k), int((80 + .5f) * k))), Of(page));
            contrasts.push_back({Name(what, p), edge, Ratio(Of(p.fill), Of(page)), Hex6(Of(p.fill)), Hex6(Of(page))});
            MeasureText(c, k, {100 + 6, 58 + 2, 100 + 150, 58 + 18}, Of(p.fill), 0xFFFFFFFF, Name("page frame label white on accent", p).c_str());
            if (page == 0xFFFFFFu || page == 0x000000u) Save(ink, c, (L"pc-frame-" + tag + (page ? L"-light.png" : L"-dark.png")).c_str());
        }
        // ---- the entry veil at its peak over a light page
        {   const float w = 600, h = 300; std::vector<std::uint32_t> px(size_t(w * k) * size_t(h * k), 0);
            Dither::RasterVeil({px.data(), int(w * k), int(w * k), int(h * k)}, k, .1, 1, true, p.fill);
            Canvas v; v.width = unsigned(w * k); v.height = unsigned(h * k); v.pixels.resize(px.size());
            for (size_t i = 0; i < px.size(); ++i) v.pixels[i] = Dither::Over(px[i], 0xFFF4F4F3);
            Save(ink, v, (L"pc-veil-" + tag + L".png").c_str());
        }
    }
    SetAccent(AccentTone::reference);
}
static void Windows(Ink& ink, const std::wstring& root) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND host = CreateWindowExW(0, L"AnnotationHarnessHost", L"Annotations harness - profile colour", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 60, 60, 1200, 820, nullptr, nullptr, inst, nullptr);
    CHECK(host != nullptr); if (!host) return;
    ShowWindow(host, SW_SHOW); UpdateWindow(host); SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); ForceForeground(host); Pump(100);
    RECT client{}; GetClientRect(host, &client); const float s = GetDpiForWindow(host) / 96.f;
    SetAccent(AccentTone::reference);
    Banner band; BannerHooks bh; band.Init(inst, host, root, bh); band.SetLiveForTest(0);   // the wave clock stopped: only AccentChanged repaints
    CHECK(band.Show({0, LONG(std::lround(85 * s)), client.right, LONG(std::lround(117 * s))}, true));
    CHECK(Settle());
    auto chipPixel = [&] {   // the BETA chip, left edge, middle row (the layer's premultiplied BGRA, opaque there)
        const auto l = band.LayoutForTest(); const auto px = band.LayerForTest().Pixels(); const RECT r = band.LayerForTest().Rect(); const int w = r.right - r.left;
        const int x = int((l.beta.left + 1.5f) * s), y = int(16 * s); return px.size() > size_t(y) * w + x ? Px(px[size_t(y) * w + x]) : Rgb{0, 0, 0}; };
    const Rgb before = chipPixel();
    SaveLayer(ink, band.LayerForTest(), L"pc-window-band-personnel.png");
    // A profile colour edit (green), reduced motion: SetAccents sets the accent at once and calls AnnotationAccentChanged
    // => each visible surface's AccentChanged(). No input, no clock.
    const std::uint32_t green = FillOf(150, .14f); SetAccent(green);
    const auto frames0 = Animator::Get().Frames();
    band.AccentChanged();
    const Rgb after = chipPixel();
    SaveLayer(ink, band.LayerForTest(), L"pc-window-band-vert.png");
    const Rgb expect = Of(AccentTone::Over(green, .16, AccentTone::Over(AccentTone::BandFor(green).gradientEnd, AccentTone::Paper::gradientAlpha * .5, 0x171717)));
    sampled.push_back({"live band BETA chip after AccentChanged (vert)", Hex6(expect), Hex6(after)});
    CHECKM(Close(after, expect, 5) && !Close(before, after, 5), "live band repainted in the new accent: before %s, after %s, expected %s", Hex6(before).c_str(), Hex6(after).c_str(), Hex6(expect).c_str());
    CHECKM(Animator::Get().Frames() == frames0, "AccentChanged paints synchronously, no animator frame requested");
    IdleCheck("profile colour after AccentChanged");
    band.Hide(false); CHECK(Settle());
    SetAccent(AccentTone::reference);
    DestroyWindow(host); Pump(50);
}
} // namespace ProfileAccent

// ---------------------------------------------------------------------------------------
// Text fit: no glyph past the padding of its container, French and English, 96 and 120 DPI.
// `Bare`: every pixel of the DIP band [x0,x1)x[y0,y1) matches `ref` (no ink there).
namespace TextFit {
static bool Bare(const Canvas& c, float k, float x0, float y0, float x1, float y1, Rgb ref, double tol = 4) {
    for (int y = int(std::ceil(y0 * k)); y < int(std::floor(y1 * k)); ++y)
        for (int x = int(std::ceil(x0 * k)); x < int(std::floor(x1 * k)); ++x) if (!Close(Px(c.At(x, y)), ref, tol)) return false;
    return true;
}
static const wchar_t* LangName() { return I18n::Current() == I18n::Locale::En ? L"en" : L"fr"; }
static void Run(Ink& ink) {
    Canvas c;
    // 1. Connect modal: a failed app's message stays between the row's 12 DIP paddings (it wraps when longer).
    const I18n::Id agentErrors[] = {I18n::Id::agent_bridge_missing, I18n::Id::agent_bridge_not_pe, I18n::Id::agent_bridge_linked, I18n::Id::agent_bridge_not_regular,
        I18n::Id::agent_claude_config_invalid, I18n::Id::agent_claude_servers_not_object, I18n::Id::agent_claude_not_installed_folder, I18n::Id::agent_claude_uninstall_extension,
        I18n::Id::agent_codex_not_installed_folder, I18n::Id::agent_codex_config_unreadable, I18n::Id::agent_connect_failed};
    for (auto lang : {I18n::Locale::Fr, I18n::Locale::En}) {
        I18n::SetForProcess(lang);
        for (auto id : agentErrors) for (UINT dpi : {96u, 120u}) {
            const float k = dpi / 96.f;
            ConnectView v = ConnectState_(SampleSetup()); v.setup.launch = false; v.setup.codex = {ConnectState::Failed, I18n::T(id)};
            const auto l = LayoutConnect(ink, v);
            Render(ink, 520, l.height + 80, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { At_(i, 40, 40, [&] { PaintConnectStatic(i, v, l, 0, 0); }); }, c);
            const auto& m = l.codexMsg;
            const bool in = Bare(c, k, 40 + 2, 40 + m.top, 40 + m.left - .5f, 40 + m.bottom, canvasRgb) && Bare(c, k, 40 + m.right + .5f, 40 + m.top, 40 + 438, 40 + m.bottom, canvasRgb);
            CHECKM(in, "connect failure message inside the row padding (%ls, %u dpi, %ls)", LangName(), dpi, I18n::T(id));
            const std::wstring msg = I18n::T(id); const float row = m.right - m.left;
            const int lines = 13 + 6 + ink.Width(msg, F(12, 16)) <= row ? 1 : ink.Lines(msg, F(12, 16), row - 13 - 6);
            CHECKM(Near(m.bottom - m.top, 16.0 * lines, .01), "connect failure message: one 16 DIP line per wrapped line (%ls)", I18n::T(id));
            if (id == I18n::Id::agent_claude_not_installed_folder) { std::wstring n = L"textfit-connect-failed-" + std::wstring(LangName()) + L"-" + std::to_wstring(dpi) + L".png"; Save(ink, c, n.c_str()); }
        }
    }
    I18n::SetForProcess(I18n::Locale::Fr);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: annotations-ui-test.exe <repoRoot> <outDir> [--no-window]\n"); return 2; }
    const std::wstring root = argv[1]; outDir = argv[2];
    const bool windows = !(argc > 3 && std::wstring(argv[3]) == L"--no-window");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CreateDirectoryW(outDir.c_str(), nullptr);
    report << L"{\"version\":2,";
    ZeroCostTests();
    MotionTests();
    ConfigTests();
    Ink* ink = Ink::Shared(root);
    CHECK(ink != nullptr); if (!ink) { std::fprintf(stderr, "Ink init failed (fonts under %ls\\assets\\fonts?)\n", root.c_str()); return 1; }
    std::printf("Monospace family: %s\n", Utf8(ink->MonoFamily()).c_str());
    report << L"\"monoFamily\":\"" << ink->MonoFamily() << L"\",";
    GeometryTests(*ink);
    TextFit::Run(*ink);
    Captures(*ink);
    V3Tests(*ink);
    V3B::Static(*ink);
    ProfileAccent::Static(*ink);
    MotionStrips(*ink);
    if (windows) { WindowTests(*ink, root); V3Windows(*ink, root); V3B::Windows(*ink, root); ProfileAccent::Windows(*ink, root); }
    std::printf("\nGeometry (Paper box model vs native):\n");
    for (auto& g : geometry) std::printf("  %-44s Paper %7.2f  native %7.2f\n", g.surface.c_str(), g.paper, g.native);
    std::printf("\nPaper colours sampled in the renders (2x):\n");
    for (auto& s : sampled) std::printf("  %-48s Paper %-14s rendered %s\n", s.what.c_str(), s.paper.c_str(), s.rendered.c_str());
    std::printf("\nContrast (measured on 2x renders / token pair):\n");
    report << L"\"paperColours\":[";
    for (auto& s : sampled) report << L"{\"what\":\"" << Wide(s.what) << L"\",\"paper\":\"" << Wide(s.paper) << L"\",\"rendered\":\"" << Wide(s.rendered) << L"\"},";
    report << L"{}],\"contrast\":[";
    for (auto& c : contrasts) {
        std::printf("  %-56s %6.2f:1  (tokens %5.2f:1)  fg %s on %s\n", c.name.c_str(), c.measured, c.expected, c.fg.c_str(), c.bg.c_str());
        report << L"{\"what\":\"" << Wide(c.name) << L"\",\"measured\":" << c.measured << L",\"tokens\":" << c.expected << L",\"fg\":\"" << Wide(c.fg) << L"\",\"bg\":\"" << Wide(c.bg) << L"\"},";
    }
    report << L"{}],\"captures\":[";
    for (auto& c : captures) report << L"\"" << c << L"\",";
    report << L"\"\"],\"checks\":" << checks << L",\"failures\":" << failures << L"}";
    {   const std::string u = Utf8(report.str()); std::ofstream f((outDir + L"/report.json").c_str(), std::ios::binary); f << u; }
    std::printf("\n%d checks, %d failures; %zu captures in %ls\n", checks, failures, captures.size(), outDir.c_str());
    return failures ? 1 : 0;
}
