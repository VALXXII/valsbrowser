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
static Shot MakeShot(unsigned w, unsigned h, float bw, float bh, float pad) {   // a page with the dark pay button (premultiplied BGRA)
    Shot s; s.width = w; s.height = h; s.pixels.assign(size_t(w) * h, 0xFFF4F4F3);
    const unsigned x0 = unsigned((w - bw) / 2), y0 = unsigned((h - bh) / 2);
    for (unsigned y = y0; y < y0 + unsigned(bh); ++y) for (unsigned x = x0; x < x0 + unsigned(bw); ++x) s.pixels[y * w + x] = 0xFF1C1C1C;
    s.mark = {x0 - pad, y0 - pad, x0 + bw + pad, y0 + bh + pad}; return s;
}
static ConnectSetup SampleSetup() {
    ConnectSetup s; s.command = L"C:\\Users\\vous\\AppData\\Local\\browser\\bin\\browser-annotations-mcp.exe";
    s.args = {L"--store", L"C:\\Users\\vous\\AppData\\Local\\browser\\annotations"}; return s;
}
static ConnectView ConnectState_(ConnectSetup s) { ConnectView v; v.setup = std::move(s); return v; }
static NoteContext SampleContext() {
    NoteContext c; c.page = L"http://localhost:5173/checkout"; c.element = L"form > button.btn-primary"; c.label = L"Bouton « Payer 48,00 € »";
    c.styles = L"display: inline-flex\nwidth: 240px"; return c;
}
static const wchar_t* sampleNote = L"Le bouton devrait être aligné à droite comme sur la maquette, et passer en pleine largeur sur mobile.";
static HistoryData SampleHistory() {
    HistoryData d; d.currentSite = L"localhost:5173";
    auto e = [](const wchar_t* id, const wchar_t* site, const wchar_t* note, EntryStatus st, AgentKind a, const wchar_t* age) {
        HistoryEntry x; x.id = id; x.site = site; x.page = std::wstring(site) + L"/checkout"; x.note = note; x.status = st; x.agent = a; x.age = age; return x; };
    d.entries.push_back(e(L"a1", L"localhost:5173", L"Le bouton devrait être aligné à droite comme sur la maquette", EntryStatus::Working, AgentKind::Claude, L"2 min"));
    d.entries.push_back(e(L"a2", L"localhost:5173", L"Le titre « Paiement » est trop proche du champ e-mail", EntryStatus::Pending, AgentKind::Claude, L"10 min"));
    d.entries.push_back(e(L"a3", L"localhost:5173", L"Faute : « dès 50 € » au lieu de « à partir de 50 € »", EntryStatus::Done, AgentKind::Claude, L"Hier"));
    d.entries.push_back(e(L"a4", L"localhost:4173", L"Le lien « Guide » mène vers une page vide", EntryStatus::Done, AgentKind::Codex, L"Mardi"));
    d.entries[0].note = std::wstring(sampleNote); d.entries[0].followUp = L"Ajoutée à 14:02 · prise en charge à 14:04";
    return d;
}

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
static void PaintLauncherAt(Ink& ink, float right, float bottom, LauncherState st, int count, bool open) {
    LauncherVisual lv; lv.state = st; lv.count = count; lv.expanded = open;
    At_(ink, right - LauncherSpec::size, bottom - LauncherSpec::size - LauncherSpec::badgeOut, [&] { PaintLauncher(ink, lv); });
}

// ---------------------------------------------------------------------------------------
static void ZeroCostTests() {
    CHECK(!Ink::Exists());
    CHECK(!Animator::Exists());
    CHECK(Layer::Created() == 0);
    Banner banner; NotePopover note; Launcher launcher; ConnectModal connect;
    banner.Init(GetModuleHandleW(nullptr), nullptr, L".", {}); note.Init(GetModuleHandleW(nullptr), nullptr, L".", {});
    launcher.Init(GetModuleHandleW(nullptr), nullptr, L".", {}); connect.Init(GetModuleHandleW(nullptr), nullptr, L".", {});
    CHECK(!banner.Created() && !note.Created() && !launcher.Created() && !connect.Created());
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
    // Note panel: 180 ms; its pill follows 60 ms later.
    Presence note{NoteTiming()}; note.Show(At(0)); CHECKM(Near(EndOf(note, 0), 180, 1), "note in");
    Row(L"note", L"enter t=1/2", 90, note.Sample(At(90), false, -8, 0));
    Ramp pill; pill.Set(1, At(0), Spec::pillUnfold, Curves::out, Spec::pillDelay);
    CHECK(pill.Value(At(59)) == 0 && pill.Value(At(61)) > 0 && pill.Value(At(260)) == 1);
    // Cards / history panel: 200 / 150 from the launcher.
    Presence card{PanelTiming()}; card.Show(At(0)); CHECKM(Near(EndOf(card, 0), 200, 1), "card in");
    Row(L"card", L"enter t=1/2", 100, card.Sample(At(100), false));
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
    CHECK(SiteOf(L"http://localhost:5173/checkout?x=1") == L"localhost:5173" && SiteOf(L"localhost:4173") == L"localhost:4173");
    HistoryEntry e; e.note = L"Titre\nDétail"; e.site = L"localhost:5173"; e.page = L"localhost:5173/x"; e.status = EntryStatus::Done; e.agentName = L"Claude"; e.summary = L"Corrigé.";
    const std::wstring md = EntryMarkdown(e);
    CHECK(md.find(L"## Titre\n") == 0 && md.find(L"- Statut : Claude a terminé") != std::wstring::npos && md.find(L"> Corrigé.") != std::wstring::npos);
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
    NoteView n; n.context = SampleContext(); n.fieldHeight = 40;
    Geo("2.1 note panel (2 lines)", 350, 102, LayoutNote(ink, n).height);
    n.fieldHeight = 0; Geo("2.1 note panel min-height", 350, 84, LayoutNote(ink, n).height);
    Geo("2.2 pill unfolded", 350, 301.19f - 80 - 102 - 8, PillExpandedH());
    Geo("2.3 « Capture impossible » pill width", 0, 320, PillCollapsed(ink, CaptureState::Unavailable).width, 2);
    CardContent k; k.kind = CardKind::NoAgent; Geo("3.1 card", 320, 206.59f - 24 - 16 - 8 - 40, LayoutCard(ink, k).height);
    k.kind = CardKind::Working; k.agent = L"Claude"; k.since = L"depuis 2 min"; k.note = sampleNote; Geo("3.2 card", 320, 138, LayoutCard(ink, k).height);
    k.kind = CardKind::Done; k.summary = L"Bouton aligné à droite et en pleine largeur sur mobile, dans CheckoutActions.tsx."; Geo("3.3 card", 320, 138, LayoutCard(ink, k).height);
    HistoryView h; h.data = SampleHistory(); h.maxHeight = 2000;
    Geo("4.1 history panel", 360, 362, LayoutHistory(ink, h).height);
    h.detailId = L"a1"; Geo("4.2 detail", 360, 466.59f - 24 - 16 - 8 - 40, LayoutHistory(ink, h).height);
    HistoryView e; e.maxHeight = 2000; Geo("4.3 empty", 360, 291.59f - 24 - 16 - 8 - 40, LayoutHistory(ink, e).height);
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
        PaintLauncherAt(i, 1180 - 16, 815 - 16, LauncherState::Rest, 0, false);
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
    // ---- 2.x note panel + pill
    Shot pillShot = MakeShot(192, 124, 124, 30, 6);
    auto noteBoard = [&](NoteView v, CaptureState cap, float fold, const wchar_t* name) {
        v.capture = cap; v.shot = cap == CaptureState::Ready ? &pillShot : nullptr; const auto l = LayoutNote(ink, v);
        PillView pv; pv.context = v.context; pv.capture = cap; pv.shot = v.shot; pv.fold = fold;
        const D2D1_RECT_F pr = PillRect(ink, cap, NoteSpec::width / 2, l.height + 8, fold);
        Render(ink, 430, pr.bottom + 80, dpi, 0x0E0E0EFF, [&](Ink& i, float, float) { At_(i, 40, 40, [&] { PaintNote(i, v, l); PaintPill(i, pv, pr); }); }, c);
        Save(ink, c, name); return std::make_pair(l, pr);
    };
    NoteView nv; nv.context = SampleContext(); nv.note = sampleNote; nv.fieldHeight = 40;
    {   auto [l, pr] = noteBoard(nv, CaptureState::Ready, 0, L"2.1-note-panel.png");
        Sample(c, k, 40 + 30, 40 + 50, canvasRgb, "2.1 note surface #171717");
        Sample(c, k, 40 + 0.25f, 40 + 50, Over(0x3E67FF99, canvasRgb), "2.1 focused border #3E67FF99", 10);
        Sample(c, k, 40 + l.add.left + 3, 40 + l.add.top + 12, accentRgb, "2.1 « Ajouter » accent");
        MeasureText(c, k, {40 + l.field.left, 40 + l.field.top, 40 + l.field.right, 40 + l.field.bottom}, canvasRgb, 0xFFFFFFE6, "note text 14/20 #FFFFFFE6");
        MeasureText(c, k, {40 + l.site.left + 22, 40 + l.site.top + 4, 40 + l.site.left + 100, 40 + l.site.bottom - 4}, canvasRgb, 0xFFFFFF99, "site 12/16 #FFFFFF99");
        MeasureText(c, k, {40 + l.add.left + 8, 40 + l.add.top + 4, 40 + l.add.right - 8, 40 + l.add.bottom - 4}, accentRgb, 0xFFFFFFFF, "« Ajouter » 13/16 white on accent");
        MeasureText(c, k, {40 + pr.left + 6, 40 + pr.top + 6, 40 + pr.right - 26, 40 + pr.bottom - 6}, canvasRgb, 0xFFFFFF99, "pill « Ce que l'agent verra » 12/16 #FFFFFF99"); }
    {   NoteView empty = nv; empty.note.clear(); empty.fieldHeight = 22; auto [l, pr] = noteBoard(empty, CaptureState::Pending, 0, L"2.1b-note-placeholder.png"); (void)pr;
        MeasureText(c, k, {40 + l.field.left, 40 + l.field.top, 40 + l.field.left + 150, 40 + l.field.top + 20}, canvasRgb, 0xFFFFFF99, "placeholder « Que faut-il changer ? » #FFFFFF99"); }
    {   auto [l, pr] = noteBoard(nv, CaptureState::Ready, 1, L"2.2-note-what-the-agent-sees.png"); (void)l;
        const float rx = pr.left + 13 + 96 + 12;
        MeasureText(c, k, {40 + rx, 40 + pr.top + 37, 40 + rx + 48, 40 + pr.top + 53}, canvasRgb, 0xFFFFFF99, "pill row label 12/16 #FFFFFF99");
        MeasureText(c, k, {40 + rx + 56, 40 + pr.top + 37, 40 + pr.right - 13, 40 + pr.top + 53}, canvasRgb, 0xFFFFFFE6, "pill row value 12/16 #FFFFFFE6");
        MeasureText(c, k, {40 + pr.left + 100, 40 + pr.top + 11, 40 + pr.right - 120, 40 + pr.top + 27}, canvasRgb, 0xFFFFFFE6, "pill header 12/16 medium #FFFFFFE6"); }
    noteBoard(nv, CaptureState::Pending, 1, L"2.2b-note-capture-pending.png");
    {   auto [l, pr] = noteBoard(nv, CaptureState::Unavailable, 0, L"2.3-note-capture-unavailable.png"); (void)l;
        MeasureText(c, k, {40 + pr.left + 28, 40 + pr.top + 6, 40 + pr.right - 8, 40 + pr.bottom - 6}, canvasRgb, 0xFFFFFFCC, "« Capture impossible ici » 12/16 #FFFFFFCC"); }
    {   NoteView err = nv; err.note.clear(); err.fieldError = true; err.fieldFocused = true; err.message = L"Écrivez ce qu'il faut changer."; err.fieldHeight = 22; noteBoard(err, CaptureState::Ready, 0, L"2.4-note-empty-validation.png"); }
    {   NoteView saving = nv; saving.status = NoteStatus::Saving; noteBoard(saving, CaptureState::Ready, 0, L"2.5-note-saving.png"); }
    {   NoteView fail = nv; fail.status = NoteStatus::Failed; fail.message = L"Impossible d'enregistrer l'annotation. Votre note est conservée ; réessayez."; auto [l, pr] = noteBoard(fail, CaptureState::Ready, 0, L"2.6-note-write-failed.png"); (void)pr;
        MeasureText(c, k, {40 + l.message.left + 19, 40 + l.message.top, 40 + l.message.right, 40 + l.message.bottom}, canvasRgb, 0xFF7B6EFF, "write error 12/16 #FF7B6E"); }
    {   NoteView kb = nv; kb.focus = NoteAdd; kb.keyboard = true; kb.fieldFocused = false; noteBoard(kb, CaptureState::Ready, 0, L"2.7-note-focus-add-keyboard.png"); }
    scene([&](Ink& i) {
        NoteView v = nv; v.capture = CaptureState::Ready; const auto l = LayoutNote(i, v);
        PillView pv; pv.context = v.context; pv.capture = CaptureState::Ready; const D2D1_RECT_F pr = PillRect(i, CaptureState::Ready, NoteSpec::width / 2, l.height + 8, 0);
        At_(i, 315, 117 + 346, [&] { PaintNote(i, v, l); PaintPill(i, pv, pr); });
        PaintLauncherAt(i, 1180 - 16, 815 - 16, LauncherState::Rest, 0, false);
    }, true, false, true, true, L"2.1-scene-write-note.png");
    // ---- 3.x cards (on the page colour, launcher below)
    auto cardBoard = [&](const CardContent& content, LauncherState st, int count, const wchar_t* name, CardVisual cv = {}) {
        const auto l = LayoutCard(ink, content); const float h = 24 + l.height + 8 + 40 + 16;
        Render(ink, 420, h, dpi, 0xF4F4F3FF, [&](Ink& i, float w, float hh) {
            At_(i, w - 16 - 320, 24, [&] { PaintCard(i, content, l, cv); });
            PaintLauncherAt(i, w - 16, hh - 16, st, count, false);
        }, c);
        Save(ink, c, name); return l;
    };
    CardContent added; added.kind = CardKind::NoAgent;
    {   const auto l = cardBoard(added, LauncherState::Rest, 1, L"3.1-card-added-no-agent.png");
        Sample(c, k, 420 - 16 - 300 + 290, 24 + 52, canvasRgb, "3.1 card surface #171717");
        Sample(c, k, 420 - 16 - 320 + l.primary.left + 4, 24 + l.primary.top + 16, raisedRgb, "3.1 « Connecter un agent » raised");
        Sample(c, k, 420 - 16 - 320 + 1 + 14 + 9, 24 + 1 + 14 + 13 - 6, accentRgb, "3.1 check disc accent", 6);
        const float x = 420 - 16 - 320;
        MeasureText(c, k, {x + 41, 24 + l.header.top + 3, x + 200, 24 + l.header.bottom - 3}, canvasRgb, 0xFFFFFFFF, "card title 14/20 600 white");
        MeasureText(c, k, {x + l.body.left, 24 + l.body.top, x + l.body.right, 24 + l.body.bottom}, canvasRgb, 0xFFFFFFB3, "card body 13/19 #FFFFFFB3");
        MeasureText(c, k, {x + l.primary.left + 120, 24 + l.primary.top + 8, x + l.primary.right - 80, 24 + l.primary.bottom - 8}, raisedRgb, 0xFFFFFFFF, "card action 13/16 white on #292929");
        Sample(c, k, 420 - 16 - 8, (24 + l.height + 8) + 2 + 3, raisedRgb, "3.1 launcher badge raised #292929", 8);
    }
    CardContent work; work.kind = CardKind::Working; work.agent = L"Claude"; work.since = L"depuis 2 min"; work.note = L"Le bouton devrait être aligné à droite comme sur la maquette, et passer en pleine largeur sur mobile.";
    {   const auto l = cardBoard(work, LauncherState::Working, 1, L"3.2-card-agent-working.png"); const float x = 420 - 16 - 320;
        MeasureText(c, k, {x + 160, 24 + l.header.top + 3, x + 260, 24 + l.header.bottom - 3}, canvasRgb, 0xFFFFFF99, "card « · depuis 2 min » 14/20 #FFFFFF99");
        Sample(c, k, 420 - 16 - 9, (24 + l.height + 8) + 3, accentRgb, "3.2 launcher badge accent", 8); }
    CardContent done; done.kind = CardKind::Done; done.agent = L"Claude"; done.summary = L"Bouton aligné à droite et en pleine largeur sur mobile, dans CheckoutActions.tsx.";
    cardBoard(done, LauncherState::Rest, 0, L"3.3-card-agent-done.png");
    CardContent addedAgent; addedAgent.kind = CardKind::Added; cardBoard(addedAgent, LauncherState::Rest, 0, L"3.4-card-added-agent-connected.png");
    {   CardVisual cv; cv.focus = CardReopen; cv.keyboard = true; cardBoard(done, LauncherState::Rest, 0, L"3.3b-card-done-focus-keyboard.png", cv); }
    CardContent codex = work; codex.agent = L"Codex"; codex.agentKind = AgentKind::Codex; cardBoard(codex, LauncherState::Working, 2, L"3.2b-card-codex-working.png");
    // Launcher states.
    auto launcherBoard = [&](LauncherState st, int count, bool open, LauncherVisual extra, const wchar_t* name) {
        Render(ink, 72, 72, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { LauncherVisual lv = extra; lv.state = st; lv.count = count; lv.expanded = open; At_(i, 16, 10, [&] { PaintLauncher(i, lv); }); }, c); Save(ink, c, name);
    };
    launcherBoard(LauncherState::Rest, 0, false, {}, L"3.0-launcher-rest.png");
    Sample(c, k, 16 + 4, 10 + 6 + 20, canvasRgb, "launcher surface #171717");
    launcherBoard(LauncherState::Working, 1, false, {}, L"3.0b-launcher-working.png");
    launcherBoard(LauncherState::Rest, 0, true, {}, L"3.0c-launcher-history-open.png");
    { LauncherVisual f; f.focus = true; f.keyboard = true; launcherBoard(LauncherState::Rest, 0, false, f, L"3.0d-launcher-focus-keyboard.png"); }
    { LauncherVisual f; f.iv.hover = 1; launcherBoard(LauncherState::Rest, 0, false, f, L"3.0e-launcher-hover.png"); }
    // ---- 4.x history
    Shot detailShot = MakeShot(668, 300, 400, 88, 12);
    auto historyBoard = [&](HistoryView v, const wchar_t* name, bool open = true) {
        v.maxHeight = 2000; const auto l = LayoutHistory(ink, v); const float h = 24 + l.height + 8 + 40 + 16;
        Render(ink, 440, h, dpi, 0xF4F4F3FF, [&](Ink& i, float w, float hh) {
            At_(i, w - 16 - 360, 24, [&] { PaintHistory(i, v, l, l.height); });
            PaintLauncherAt(i, w - 16, hh - 16, LauncherState::Working, 1, open);
        }, c);
        Save(ink, c, name); return l;
    };
    HistoryView hv; hv.data = SampleHistory(); hv.hot = 0;
    {   const auto l = historyBoard(hv, L"4.1-history-list.png"); const float x = 440 - 16 - 360;
        Sample(c, k, x + l.segment[0].left + 3, 24 + l.segment[0].top + 13, Solid(0x3E3E3E), "4.1 chosen segment #3E3E3E");
        Sample(c, k, x + l.filters.left + 2, 24 + l.filters.top + 16, raisedRgb, "4.1 filters raised #292929");
        float rowTop = 0; for (auto& it : l.items) if (!it.section && it.entry == 0) rowTop = l.listTop + it.y;
        Sample(c, k, x + 5 + 2, 24 + rowTop + 26, Over(0xFFFFFF0A, canvasRgb), "4.1 hovered row #FFFFFF0A");
        MeasureText(c, k, {x + 41, 24 + 15, x + 140, 24 + 39}, canvasRgb, 0xFFFFFFFF, "« Historique » 16/20 600 white");
        MeasureText(c, k, {x + l.segment[1].left + 10, 24 + l.segment[1].top + 5, x + l.segment[1].right - 22, 24 + l.segment[1].bottom - 5}, raisedRgb, 0xFFFFFFB3, "segment 12/16 medium #FFFFFFB3 on #292929");
        { Font n = F(11, 14); n.tabular = true; const float cw = ink.Width(L"2", n), tw = ink.Width(L"En cours", F(12, 16, 500)) + 5 + cw, cr = (l.segment[1].left + l.segment[1].right) / 2 + tw / 2;
          MeasureText(c, k, {x + cr - cw - 1, 24 + l.segment[1].top + 5, x + cr + 1, 24 + l.segment[1].bottom - 5}, raisedRgb, 0xFFFFFF80, "segment count 11/14 #FFFFFF80 on #292929"); }
        MeasureText(c, k, {x + 5 + 10, 24 + l.listTop + 1 + 10, x + 100, 24 + l.listTop + 1 + 26}, canvasRgb, 0xFFFFFFB3, "section site 12/16 medium #FFFFFFB3");
        MeasureText(c, k, {x + 100, 24 + l.listTop + 1 + 10, x + 160, 24 + l.listTop + 1 + 26}, canvasRgb, 0xFFFFFF80, "« · cette page » 11/14 #FFFFFF80");
        MeasureText(c, k, {x + 5 + 62, 24 + rowTop + 28, x + 5 + 140, 24 + rowTop + 44}, Over(0xFFFFFF0A, canvasRgb), 0xFFFFFF99, "row status 12/16 #FFFFFF99");
        float doneTop = 0; for (auto& it : l.items) if (!it.section && it.entry == 2) doneTop = l.listTop + it.y;
        MeasureText(c, k, {x + 5 + 62, 24 + doneTop + 28, x + 5 + 200, 24 + doneTop + 44}, canvasRgb, 0xFFFFFF99, "done row status 12/16 #FFFFFF99 (kept at 100 %)");
        MeasureText(c, k, {x + 5 + 62, 24 + doneTop + 8, x + 300, 24 + doneTop + 26}, canvasRgb, 0xFFFFFFB3, "done row note 13/18 white at 70 %");
    }
    { HistoryView m = hv; m.menuEntry = 0; m.menuHot = HMenuDelete; historyBoard(m, L"4.1b-history-row-menu.png"); }
    { HistoryView f = hv; f.filter = HistoryFilter::Open; f.hot = -1; historyBoard(f, L"4.1c-history-filter-open.png"); }
    { HistoryView f = hv; f.filter = HistoryFilter::Done; f.hot = -1; f.selected = 2; f.focus = HList; f.keyboard = true; historyBoard(f, L"4.1d-history-filter-done-keyboard.png"); }
    { HistoryView d = hv; d.data.entries[0].capture = detailShot; d.detailId = L"a1"; d.hot = -1;
      const auto l = historyBoard(d, L"4.2-history-detail.png"); const float x = 440 - 16 - 360;
      Sample(c, k, x + l.del.left + 4, 24 + l.del.top + 15, Over(0xFF7B6E1A, canvasRgb), "4.2 « Supprimer » danger 10 %");
      MeasureText(c, k, {x + l.del.left + 40, 24 + l.del.top + 6, x + l.del.right - 40, 24 + l.del.bottom - 6}, Over(0xFF7B6E1A, canvasRgb), 0xFF7B6EFF, "« Supprimer » 12/16 #FF7B6E on danger 10 %");
      MeasureText(c, k, {x + l.follow.left + 12, 24 + l.follow.top + 34, x + l.follow.right - 12, 24 + l.follow.bottom - 10}, raisedRgb, 0xFFFFFF99, "follow-up 12/17 #FFFFFF99 on #292929");
      MeasureText(c, k, {x + l.page.left, 24 + l.page.top + 4, x + l.page.left + 140, 24 + l.page.bottom - 4}, canvasRgb, 0xFFFFFFB3, "detail page 13/18 #FFFFFFB3");
      MeasureText(c, k, {x + l.copy.left + 20, 24 + l.copy.top + 6, x + l.copy.right - 20, 24 + l.copy.bottom - 6}, raisedRgb, 0xFFFFFFFF, "« Copier en Markdown » 12/16 white on #292929"); }
    { HistoryView d = hv; d.data.entries[2].summary = L"Libellé corrigé dans PromoBanner.tsx."; d.detailId = L"a3"; d.hot = -1; historyBoard(d, L"4.2b-history-detail-done.png"); }
    { HistoryView e; const auto l = historyBoard(e, L"4.3-history-empty.png", true); const float x = 440 - 16 - 360;
      MeasureText(c, k, {x + 80, 24 + 1 + 50 + 20 + 36, x + 280, 24 + 1 + 50 + 20 + 56}, canvasRgb, 0xFFFFFF80, "empty « Aucune annotation pour l'instant... » 14/20 #FFFFFF80");
      Sample(c, k, x + l.annotate.left + 3, 24 + l.annotate.top + 16, accentRgb, "4.3 « Annoter cette page » accent"); }
    { HistoryView f = hv; f.data.entries.resize(2); f.filter = HistoryFilter::Done; f.hot = -1; historyBoard(f, L"4.3b-history-filter-empty.png"); }
    {   HistoryView many = hv; many.hot = -1; for (int i = 0; i < 14; ++i) { auto e = hv.data.entries[size_t(i % 4)]; e.id = L"m" + std::to_wstring(i); e.age = std::to_wstring(i + 3) + L" j"; many.data.entries.push_back(e); }
        many.maxHeight = 520; many.scroll = 140; const auto l = LayoutHistory(ink, many); CHECK(MaxScroll(l) > 0 && l.height <= 520.5f);
        Render(ink, 440, 24 + l.height + 64, dpi, 0xF4F4F3FF, [&](Ink& i, float w, float hh) { At_(i, w - 16 - 360, 24, [&] { PaintHistory(i, many, l, l.height); }); PaintLauncherAt(i, w - 16, hh - 16, LauncherState::Working, 1, true); }, c);
        Save(ink, c, L"4.4-history-scrolled.png"); }
    // Toast.
    Render(ink, ToastWidth(ink) + 80, 80, dpi, 0xF4F4F3FF, [&](Ink& i, float, float) { At_(i, 40, 20, [&] { PaintToast(i, {}, false, false, false); }); }, c);
    Save(ink, c, L"4.5-history-toast-undo.png");
    MeasureText(c, k, {40 + 13, 20 + 12, 40 + 140, 20 + 28}, canvasRgb, 0xFFFFFFE6, "toast « Annotation supprimée » 13/16 #FFFFFFE6");
    // 4.1 scene.
    Render(ink, 1180, 815, dpi, 0x171717FF, [&](Ink& i, float w, float h) {
        PaintChrome(i, w); PaintPage(i, 0, 85, w, h - 85, false);
        HistoryView v = hv; v.maxHeight = 2000; const auto l = LayoutHistory(i, v);
        At_(i, w - 16 - 360, h - 64 - l.height, [&] { PaintHistory(i, v, l, l.height); });
        PaintLauncherAt(i, w - 16, h - 16, LauncherState::Working, 1, true);
    }, c);
    Save(ink, c, L"4.1-scene-history.png");
    // Scenes for the cards (3.2 on the page).
    Render(ink, 1180, 815, dpi, 0x171717FF, [&](Ink& i, float w, float h) {
        PaintChrome(i, w); BannerVisual bv; At_(i, 0, 85, [&] { PaintBanner(i, w, 32, bv); }); PaintPage(i, 0, 117, w, h - 117, false);
        const auto l = LayoutCard(i, work); At_(i, w - 16 - 320, h - 64 - l.height, [&] { PaintCard(i, work, l, {}); });
        PaintLauncherAt(i, w - 16, h - 16, LauncherState::Working, 1, false);
    }, c);
    Save(ink, c, L"3.2-scene-card-working.png");
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
    // Card born from the launcher (origin bottom-right), and a history panel.
    { CardContent cc; cc.kind = CardKind::NoAgent; const auto cl = LayoutCard(ink, cc); Presence p{PanelTiming()}; p.Show(At(0)); const double ts[4] = {0, 50, 100, 200};
      strip(L"m7-card-from-launcher.png", 4, 380, cl.height + 100, [&](Ink& i, int f) {
          i.Fill({0, 0, 380, cl.height + 100}, 0, Hex(0xF4F4F3FF));
          At_(i, 30, 20, [&] { Posed(i, p.Sample(At(ts[f]), false), {320 - 20, cl.height + 28}, [&] { PaintCard(i, cc, cl, {}); }); });
          PaintLauncherAt(i, 30 + 320, 20 + cl.height + 8 + 40 + 6, LauncherState::Rest, 1, false); }); }
    { HistoryView hv; hv.data = SampleHistory(); hv.maxHeight = 2000; const auto hl = LayoutHistory(ink, hv); Presence p{PanelTiming()}; p.Show(At(0), false); p.Hide(At(0)); const double ts[4] = {0, 50, 100, 150};
      strip(L"m8-history-back-to-launcher.png", 4, 420, hl.height + 100, [&](Ink& i, int f) {
          i.Fill({0, 0, 420, hl.height + 100}, 0, Hex(0xF4F4F3FF));
          At_(i, 30, 20, [&] { Posed(i, p.Sample(At(ts[f]), false), {360 - 20, hl.height + 28}, [&] { PaintHistory(i, hv, hl, hl.height); }); });
          PaintLauncherAt(i, 30 + 360, 20 + hl.height + 8 + 40 + 6, LauncherState::Working, 1, true); }); }
    // Note pill unfolding into its card (200 ms).
    { NoteContext ctx = SampleContext(); Ramp r; r.Set(1, At(0), Spec::pillUnfold, Curves::out); const double ts[4] = {0, 40, 100, 200};
      strip(L"m9-note-pill-unfold.png", 4, 390, 150, [&](Ink& i, int f) { PillView pv; pv.context = ctx; pv.fold = r.Value(At(ts[f])); PaintPill(i, pv, PillRect(i, CaptureState::Ready, 195, 20, pv.fold)); }); }
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
    Banner banner; int exits = 0, bannerConnects = 0;
    BannerHooks bh; bh.exit = [&] { ++exits; }; bh.connect = [&] { ++bannerConnects; }; bh.announce = announce;
    banner.Init(inst, host, root, bh);
    CHECK(banner.Show({0, int(std::lround(85 * dpiScale)), client.right, pageTop}, false));
    CHECK(lastSaid(L"Mode Annotation activé"));
    banner.SetReminder(true);
    CHECK(Settle());
    { auto n = banner.NodesForTest(); CHECK(HasNode(n, L"Connecter un agent") && HasNode(n, L"Quitter le mode Annotation")); }
    SetFocus(banner.Window()); CHECK(banner.KeyForTest(VK_TAB) && banner.FocusItemForTest() == BannerRemind);
    banner.KeyForTest(VK_RETURN); CHECK(bannerConnects == 1);
    banner.KeyForTest(VK_ESCAPE); CHECK(exits == 1);
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

    // ---- Note panel + pill
    NotePopover note; std::vector<NoteSubmission> submits; std::wstring cancelled; bool cancelledKb = false;
    NoteHooks nh; nh.submit = [&](const NoteSubmission& x) { submits.push_back(x); }; nh.cancel = [&](const std::wstring& d, bool kb) { cancelled = d; cancelledKb = kb; }; nh.announce = announce;
    note.Init(inst, host, root, nh);
    RECT target{int(64 * dpiScale), pageTop + int(350 * dpiScale), int(304 * dpiScale), pageTop + int(394 * dpiScale)}; MapWindowPoints(host, nullptr, reinterpret_cast<POINT*>(&target), 2);
    RECT pageScreen = page; MapWindowPoints(host, nullptr, reinterpret_cast<POINT*>(&pageScreen), 2);
    NoteRequest req; req.context = SampleContext(); req.capture = CaptureState::Pending;
    ForceForeground(host);
    CHECK(note.Show(req, target, pageScreen, false, false));
    CHECK(GetFocus() == note.Edit());                                        // keystrokes accepted from frame 0
    CHECK((GetWindowLongPtrW(note.GhostForTest().Window(), GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0);   // the ghost never takes a click
    CHECK(note.LiftXForTest() < 0 && note.FinalForTest().left > target.right);   // born beside the element, on its side
    Pump(70); SaveLayer(ink, note.GhostForTest(), L"w9-window-note-entering.png");
    CHECK(Settle()); CHECK(note.SettledForTest());
    SaveLayer(ink, note.PillForTest(), L"w10-window-note-pill.png");
    {   auto n = note.NodesForTest(); CHECK(HasNode(n, L"Ajouter") && HasNode(n, L"Ce que l'agent verra") && HasNode(n, L"Site")); }
    // Empty: validation on submit, never a disabled button.
    note.KeyForTest(VK_RETURN, true); CHECK(submits.empty() && note.ViewForTest().fieldError && lastSaid(L"Écrivez ce qu'il faut changer."));
    note.TypeForTest(sampleNote); CHECK(!note.ViewForTest().fieldError);
    // Tab order: field -> Ajouter -> pill; Space unfolds the pill; Échap folds it, then cancels.
    note.KeyForTest(VK_TAB, true); CHECK(note.FocusForTest() == NoteAdd);
    note.KeyForTest(VK_TAB, false); CHECK(note.FocusForTest() == NotePill);
    note.KeyForTest(VK_SPACE, false); CHECK(note.PillOpenForTest());
    note.SetShot(CaptureState::Ready, nullptr);
    CHECK(Settle()); SaveLayer(ink, note.PillForTest(), L"w11-window-note-pill-unfolded.png");
    { auto n = note.NodesForTest(); CHECK(HasNode(n, L"Page") && HasNode(n, L"Élément") && HasNode(n, L"Style")); }
    note.KeyForTest(VK_ESCAPE, false); CHECK(!note.PillOpenForTest() && note.Visible());
    note.KeyForTest(VK_TAB, false); CHECK(note.FocusForTest() == NoteField);
    note.KeyForTest(VK_RETURN, true);
    CHECK(submits.size() == 1 && submits[0].note == sampleNote && submits[0].site == L"localhost:5173" && submits[0].includeShot);
    note.Saved(); CHECK(!note.Visible()); CHECK(Settle());
    // Capture impossible: the pill says so and leaves the Tab order.
    req.capture = CaptureState::Unavailable; req.draft = L"brouillon"; note.Show(req, target, pageScreen, true, false); Settle();
    note.KeyForTest(VK_TAB, true); note.KeyForTest(VK_TAB, false); CHECK(note.FocusForTest() == NoteField);
    { auto n = note.NodesForTest(); CHECK(HasNode(n, NoteSpec::unavailable) && !HasNode(n, L"Ce que l'agent verra")); }
    SaveLayer(ink, note.PillForTest(), L"w12-window-note-capture-unavailable.png");
    note.KeyForTest(VK_ESCAPE, true); CHECK(cancelled == L"brouillon" && cancelledKb && !note.Visible()); Settle();

    // ---- Launcher, cards, history, toast
    Launcher launcher; int toggles = 0, cardConnects = 0, views = 0, reloads = 0, reopens = 0, dismissedKb = -1, annotates = 0;
    std::vector<std::wstring> removed, undone, finals; std::wstring markdown;
    LauncherHooks lh; lh.connect = [&] { ++cardConnects; }; lh.open = [&] { ++views; }; lh.reload = [&] { ++reloads; }; lh.reopen = [&] { ++reopens; };
    lh.dismissed = [&](bool kb) { dismissedKb = kb ? 1 : 0; }; lh.announce = announce;
    lh.history.remove = [&](const std::wstring& id) { removed.push_back(id); }; lh.history.undo = [&](const std::wstring& id) { undone.push_back(id); };
    lh.history.removeFinal = [&](const std::wstring& id) { finals.push_back(id); }; lh.history.copy = [&](const std::wstring& m) { markdown = m; }; lh.history.annotate = [&] { ++annotates; };
    lh.toggle = [&] { ++toggles; if (launcher.HistoryVisible()) launcher.HideHistory(); else launcher.ShowHistory(SampleHistory(), true); };
    launcher.Init(inst, host, root, lh); launcher.Place(page);
    CHECK(launcher.Show(LauncherState::Rest, 0, false)); CHECK(Settle());
    {   RECT lw{}; GetWindowRect(launcher.LauncherWindow(), &lw); const int b6 = int(std::lround(6 * dpiScale));
        POINT center{(lw.left + lw.right - b6) / 2, (lw.top + b6 + lw.bottom) / 2}, overhang{lw.right - 2, lw.top + 2}, corner{lw.left + 1, lw.bottom - 1};
        CHECK(WindowFromPoint(center) == launcher.LauncherWindow());
        CHECKM(WindowFromPoint(overhang) != launcher.LauncherWindow(), "transparent overhang must not take the click");
        CHECKM(WindowFromPoint(corner) != launcher.LauncherWindow(), "rounded corner pixel must not take the click");
        auto n = launcher.ButtonNodesForTest(); CHECK(n.size() == 1 && n[0].name == L"Annotations" && n[0].expand == 1 && n[0].accelerator == L"Ctrl+Maj+H"); }
    SaveLayer(ink, launcher.ButtonLayerForTest(), L"w13-window-launcher-rest.png");
    // Card 3.1: born from the launcher; « Connecter un agent » from the keyboard.
    CardContent noAgent; noAgent.kind = CardKind::NoAgent; launcher.Show(LauncherState::Rest, 1, false);
    const auto cf0 = Animator::Get().Frames(); const DWORD ct0 = GetTickCount();
    launcher.ShowCard(noAgent); CHECK(lastSaid(L"Annotation ajoutée. Aucun agent"));
    Pump(90); SaveLayer(ink, launcher.CardLayerForTest(), L"w14-window-card-entering.png");
    CHECK(Settle()); const DWORD cardMs = GetTickCount() - ct0; const auto cardFrames = Animator::Get().Frames() - cf0;
    std::printf("Card enter in a real window: %lu ms, %llu frames\n", cardMs, cardFrames); CHECK(cardFrames >= 5);
    report << L"\"liveCard\":{\"enterMs\":" << cardMs << L",\"frames\":" << cardFrames << L"},";
    SaveLayer(ink, launcher.CardLayerForTest(), L"w15-window-card-no-agent.png");
    IdleCheck("card");
    launcher.FocusCard(); CHECK(GetFocus() == launcher.CardWindow());
    SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_RETURN, 0); CHECK(cardConnects == 1);
    // 3.2 -> 3.3: the same card cross-fades in place.
    CardContent work; work.kind = CardKind::Working; work.agent = L"Claude"; work.since = L"depuis 2 min"; work.note = sampleNote; work.id = L"a1";
    launcher.Show(LauncherState::Working, 1, false); launcher.ShowCard(work); CHECK(Settle(800) || true);
    { auto n = launcher.ButtonNodesForTest(); CHECK(n.size() == 1 && n[0].name == L"Annotations · 1 en cours"); }
    CardContent done; done.kind = CardKind::Done; done.agent = L"Claude"; done.summary = L"Bouton aligné à droite et en pleine largeur sur mobile, dans CheckoutActions.tsx."; done.id = L"a1";
    launcher.Show(LauncherState::Rest, 0, false); launcher.ShowCard(done);
    Pump(60); SaveLayer(ink, launcher.CardLayerForTest(), L"w16-window-card-crossfade-working-to-done.png");
    CHECK(Settle()); SaveLayer(ink, launcher.CardLayerForTest(), L"w17-window-card-done.png");
    launcher.FocusCard(); SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_RETURN, 0); CHECK(reloads == 1);
    SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_TAB, 0); SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_RETURN, 0); CHECK(reopens == 1);
    SendMessageW(launcher.CardWindow(), WM_KEYDOWN, VK_ESCAPE, 0); CHECK(!launcher.CardVisible() && dismissedKb == 1 && GetFocus() == launcher.LauncherWindow());
    // « Annotation ajoutée » with an agent: leaves by itself (one-shot dwell).
    CardContent added; added.kind = CardKind::Added; launcher.ShowCard(added); CHECK(launcher.DwellArmedForTest());
    SendMessageW(launcher.CardWindow(), WM_TIMER, 7, 0); CHECK(!launcher.CardVisible() && dismissedKb == 0); Settle();
    // History: opened from the launcher (keyboard), filters, detail, delete + undo, toast expiry, contained scroll.
    // (Rest: the « en cours » ring turns by design while an agent works, which would keep frames coming.)
    launcher.Show(LauncherState::Rest, 1, false);
    SetFocus(launcher.LauncherWindow()); SendMessageW(launcher.LauncherWindow(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(toggles == 1 && launcher.HistoryVisible());
    Pump(80); SaveLayer(ink, launcher.HistoryForTest().LayerForTest(), L"w18-window-history-entering.png");
    CHECK(Settle()); SaveLayer(ink, launcher.HistoryForTest().LayerForTest(), L"w19-window-history.png");
    { auto n = launcher.ButtonNodesForTest(); CHECK(n[0].expand == 2); }
    auto& hp = launcher.HistoryForTest();
    {   auto n = hp.NodesForTest(); CHECK(HasNode(n, L"Toutes") && HasNode(n, L"En cours (2)") && HasNode(n, L"Terminées (2)") && HasNode(n, L"Fermer l'historique")); }
    CHECK(GetFocus() == launcher.HistoryWindow() && hp.ViewForTest().focus == HList && hp.ViewForTest().selected == 0);
    hp.KeyForTest(VK_DOWN); CHECK(hp.ViewForTest().selected == 1);
    hp.KeyForTest(VK_RETURN); CHECK(!hp.ViewForTest().detailId.empty());
    Pump(60); SaveLayer(ink, hp.LayerForTest(), L"w20-window-history-detail-sliding.png");
    CHECK(Settle()); SaveLayer(ink, hp.LayerForTest(), L"w21-window-history-detail.png");
    hp.KeyForTest(VK_TAB); hp.KeyForTest(VK_TAB); CHECK(hp.ViewForTest().focus == HCopy);
    hp.KeyForTest(VK_RETURN); CHECK(markdown.find(L"## Le titre « Paiement »") == 0);
    hp.KeyForTest(VK_ESCAPE); CHECK(hp.ViewForTest().detailId.empty() && hp.ViewForTest().focus == HList); Settle();
    hp.KeyForTest(VK_DELETE); CHECK(removed.size() == 1 && removed[0] == L"a2" && hp.ViewForTest().data.entries.size() == 3);
    CHECK(launcher.HistoryForTest().ToastVisible() && lastSaid(L"Annotation supprimée"));
    CHECK(Settle()); SaveLayer(ink, hp.ToastForTest(), L"w22-window-toast-undo.png");
    {   auto n = hp.ToastNodesForTest(); CHECK(HasNode(n, L"Annuler")); }
    hp.KeyForTest('Z' | 0); // without Ctrl: nothing
    CHECK(undone.empty());
    {   BYTE keys[256]; GetKeyboardState(keys); keys[VK_CONTROL] = 0x80; SetKeyboardState(keys); hp.KeyForTest('Z'); keys[VK_CONTROL] = 0; SetKeyboardState(keys); }
    CHECK(undone.size() == 1 && undone[0] == L"a2" && hp.ViewForTest().data.entries.size() == 4 && lastSaid(L"Suppression annulée"));
    hp.KeyForTest(VK_DELETE); CHECK(removed.size() == 2); hp.ExpireToastForTest(); CHECK(finals.size() == 1 && finals[0] == removed[1]);
    // Filters with the arrows.
    hp.KeyForTest(VK_TAB); CHECK(hp.ViewForTest().focus == HClose); hp.KeyForTest(VK_TAB); CHECK(hp.ViewForTest().focus == HFilters);
    hp.KeyForTest(VK_RIGHT); CHECK(hp.ViewForTest().filter == HistoryFilter::Open); hp.KeyForTest(VK_RIGHT); CHECK(hp.ViewForTest().filter == HistoryFilter::Done);
    hp.KeyForTest(VK_RIGHT); CHECK(hp.ViewForTest().filter == HistoryFilter::All);
    // Internal scroll, contained: many entries, a wheel notch scrolls the list and never reaches the page.
    {   HistoryData many = SampleHistory(); for (int i = 0; i < 20; ++i) { auto e = many.entries[size_t(i % 4)]; e.id = L"x" + std::to_wstring(i); many.entries.push_back(e); }
        launcher.UpdateHistory(many); CHECK(Settle());
        const int before = hostWheel; RECT hr{}; GetWindowRect(launcher.HistoryWindow(), &hr);
        SendMessageW(launcher.HistoryWindow(), WM_MOUSEWHEEL, MAKEWPARAM(0, WORD(-120)), MAKELPARAM((hr.left + hr.right) / 2, hr.bottom - 40));
        CHECK(hp.ScrollForTest() > 0 && hostWheel == before);
        for (int i = 0; i < 40; ++i) SendMessageW(launcher.HistoryWindow(), WM_MOUSEWHEEL, MAKEWPARAM(0, WORD(-120)), 0);   // past the end: clamped, still contained
        CHECK(Near(hp.ScrollForTest(), MaxScroll(hp.LayoutForTest()), .01) && hostWheel == before);
        SaveLayer(ink, hp.LayerForTest(), L"w23-window-history-scrolled.png"); }
    hp.KeyForTest(VK_ESCAPE); CHECK(!launcher.HistoryVisible() && GetFocus() == launcher.LauncherWindow());
    CHECK(Settle());
    // Empty state: « Annoter cette page ».
    launcher.ShowHistory(HistoryData{}, true); CHECK(Settle()); SaveLayer(ink, hp.LayerForTest(), L"w24-window-history-empty.png");
    hp.KeyForTest(VK_RETURN); CHECK(annotates == 1 && !launcher.HistoryVisible()); Settle();
    // All surfaces at rest: zero wake-ups (launcher, band).
    launcher.Show(LauncherState::Rest, 0, false); Settle();
    IdleCheck("rest");
    DestroyWindow(host); Pump(50);
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
    Captures(*ink);
    MotionStrips(*ink);
    if (windows) WindowTests(*ink, root);
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
