// Onboarding window (src/onboarding.h): every board rendered off screen by the production drawing code
// (software Direct2D into WIC, the shaders through Direct3D), at 96 and 120 DPI, saved as PNG for the
// side-by-side audit with the Paper screenshots (design/onboarding-evidence/), plus the checks that do not
// need eyes: Paper geometry of the interactive parts, the colours taken from the profile, keyboard order,
// the events of each button, both languages.
// Usage: onboarding_render_test.exe <repo root> <output dir>
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/onboarding.h"

static int failures = 0, checks = 0;
static void Expect(bool ok, const char* what) { ++checks; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static bool Near(float a, float b, float eps = .51f) { return std::fabs(a - b) <= eps; }

namespace UI = NativeChrome::OnboardingUI; namespace K = NativeChrome::OnboardingKit;
using Onboarding::Screen;

static std::vector<UI::BrowserSource> FakeSources() {
    UI::BrowserSource chrome; chrome.name = L"Google Chrome"; chrome.profiles = {{L"Profil 1", true, true}, {L"Travail", true, false}};
    UI::BrowserSource edge; edge.name = L"Microsoft Edge"; edge.profiles = {{L"", true, true}};
    UI::BrowserSource gone; gone.name = L"Vivaldi"; gone.available = false; gone.detail = L"Installé, pas encore pris en charge";
    UI::BrowserSource file; file.name = L"Fichier de favoris HTML…"; file.file = true;
    return {chrome, edge, gone, file};
}
// The Chrome logo of the boards (a Paper image), only to compare the renders with the Paper screenshots: the
// product shows the logo of the browser installed on the machine (src/browser-catalog.h).
static std::shared_ptr<const NativeChrome::FaviconImage> PaperLogo(const std::wstring& root) {
    IWICImagingFactory* wic = nullptr; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    Onboarding::Shaders::Image image; auto out = std::make_shared<NativeChrome::FaviconImage>();
    if (wic && Onboarding::Shaders::LoadPng(wic, root + L"\\design\\onboarding-evidence\\fixtures\\source-logo.png", image)) {
        out->width = image.width; out->height = image.height; out->stride = image.width * 4; out->pixels.resize(image.rgba.size());
        for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) { const unsigned a = image.rgba[i + 3]; out->pixels[i] = static_cast<unsigned char>(image.rgba[i + 2] * a / 255); out->pixels[i + 1] = static_cast<unsigned char>(image.rgba[i + 1] * a / 255); out->pixels[i + 2] = static_cast<unsigned char>(image.rgba[i] * a / 255); out->pixels[i + 3] = static_cast<unsigned char>(a); }
    }
    if (wic) wic->Release();
    return out->width ? out : nullptr;
}
struct Fixture {
    std::shared_ptr<const NativeChrome::FaviconImage> logo;
    UI::Window window; std::wstring root, out; int saved = 0, started = 0; unsigned token = 0; Profiles::Profile savedProfile; I18n::Locale language = I18n::Locale::Fr; bool refuseName = false;
    void Setup(bool annotation = true) {
        UI::Options o; o.seed = UI::PaperSeed(L"default", L"Alex"); o.annotation = annotation; o.reduced = true;
        o.hooks.saveProfile = [this](const Profiles::Profile& p) { if (refuseName) return Profiles::NameError::Duplicate; ++saved; savedProfile = p; return Profiles::NameError::None; };
        o.hooks.sources = [this] { auto s = FakeSources(); s[0].logo = logo; return s; };
        o.hooks.startImport = [this](unsigned t, const UI::ImportPick&, bool, bool) { ++started; token = t; return true; };
        o.hooks.language = [this](I18n::Locale l) { language = l; };
        window.SetupForTest(root, o);
    }
    bool Shot(const wchar_t* name) {
        bool ok = true;
        for (UINT dpi : {96u, 120u}) { const std::wstring path = out + L"\\" + name + (dpi == 96 ? L"@96.png" : L"@120.png"); ok = window.RenderForTest(dpi, path.c_str()) && ok; }
        return ok;
    }
};

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring root = argc > 1 ? argv[1] : L".", out = argc > 2 ? argv[2] : L".";
    I18n::SetForProcess(I18n::Locale::Fr);
    NativeChrome::Chrome probe; Expect(SUCCEEDED(probe.InitOffscreen(root, 96, 720, 580)), "offscreen surface");

    // ---- colours the boards take from the profile ----
    {
        const auto paper = UI::PaperSeed(L"default", L"Alex");
        Expect(K::Accent(paper) == K::FromHex(0x0099A0), "board accent at 200 deg / 88 %: #0099A0");
        const auto ink = K::Ink(paper); Expect(std::abs(ink.r - 0x00) <= 4 && std::abs(ink.g - 0xD1) <= 1 && std::abs(ink.b - 0xDA) <= 1, "ink at 200 deg / 88 %: #00D1DA (within one step)");
        Expect(K::Rehue(0x0AF4FF, paper) == K::FromHex(0x0AF4FF) && K::IntensityEnd(paper) == K::FromHex(0x00E1EA), "the sample profile gives the boards' literal colours");
        Profiles::Profile pink = paper; pink.hue = 0;
        const auto a = K::Accent(pink), lab = K::Accent(paper); Expect(a.r > a.g && a.r > a.b && !(a == lab), "another hue: the accent follows it");
        Expect(std::fabs(K::ToLab(a).L - .62) < .01, "accent lightness stays OKLCH .62 across hues");
        const auto moved = K::Rehue(0x0099A0, pink); Expect(std::fabs(K::ToLab(moved).L - K::ToLab(K::FromHex(0x0099A0)).L) < .02 && moved.r > moved.b, "a board colour moved to another hue keeps its lightness");
        Profiles::Profile grey = paper; grey.neutral = true;
        Expect(K::Accent(grey) == ProfileColor::NeutralFill() && K::Ink(grey) == ProfileColor::Neutral(), "neutral profile: the grey fill and ink of the chrome");
        const auto g = K::Rehue(0x0AF4FF, grey); Expect(std::abs(g.r - g.g) <= 2 && std::abs(g.g - g.b) <= 2, "neutral profile: the gradient loses its colour");
        Expect(K::MixOklab(K::FromHex(0xFF8FB4), K::FromHex(0xF8A052), 0) == K::FromHex(0xFF8FB4) && K::MixOklab(K::FromHex(0xFF8FB4), K::FromHex(0xF8A052), 1) == K::FromHex(0xF8A052), "oklab interpolation hits its ends");
    }
    // ---- geometry: the Paper boxes of the interactive parts (DIP at 100 %) ----
    {
        Fixture f; f.root = root; f.out = out; f.Setup();
        auto box = [&](int id) { for (const auto& i : f.window.ItemsForTest(probe)) if (i.first == id) return i.second; return D2D1_RECT_F{}; };
        using W = UI::Window;
        // Hug widths are the text's width rounded up + padding, as on the boards: exact.
        auto is = [](D2D1_RECT_F r, float l, float t, float w, float h) {
            const bool ok = Near(r.left, l, .01f) && Near(r.top, t, .01f) && Near(r.right - r.left, w, .01f) && Near(r.bottom - r.top, h, .01f);
            if (!ok) std::printf("     got %.2f %.2f %.2f x %.2f, Paper %.1f %.1f %.1f x %.1f\n", r.left, r.top, r.right - r.left, r.bottom - r.top, l, t, w, h);
            return ok;
        };
        Expect(is(box(W::Lang), 13, 11, 85, 28), "1: language button 85 x 28 at (13, 11)");
        Expect(is(box(W::Close), 679, 11, 28, 28), "1: close button 28 x 28 at (679, 11)");
        Expect(is(box(W::SkipAll), 20, 530, 139, 32), "1: « Passer et naviguer » 139 x 32 at (20, 530)");
        Expect(is(box(W::Primary), 598, 530, 102, 32), "1: « Commencer » 102 x 32, right edge 700");
        f.window.ActivateForTest(W::Primary);
        Expect(f.window.Flow().screen() == Screen::Profile, "Commencer -> board 2");
        Expect(is(box(W::Name), 24, 465, 83, 32), "2: name field 83 x 32 at (23.5 -> 24, 465)");
        Expect(is(box(W::IconBase), 132, 465, 32, 32) && is(box(W::IconBase + 2), 204, 465, 32, 32), "2: icon cells 32, pitch 36, from 131.5 -> 132");
        Expect(is(box(W::Hue), 371, 445, 282, 24) && is(box(W::Intensity), 371, 473, 282, 24), "2: slider lanes 282 x 24 at 370.5 -> 371, rows 445 / 473");
        Expect(is(box(W::Back), 20, 530, 67, 32) && is(box(W::Primary), 612, 530, 88, 32), "2: « Retour » 67, « Continuer » 88");
        f.window.ActivateForTest(W::Primary);
        Expect(f.window.Flow().screen() == Screen::ImportPick && f.saved == 1 && f.savedProfile.name == L"Alex" && f.savedProfile.icon == L"heart" && f.savedProfile.hue == 200, "Continuer saves the draft, then board 3");
        Expect(is(box(W::Browser), 160, 441, 228, 32) && is(box(W::Profile), 400, 441, 160, 32), "3: selects 228 and 160 x 32 at 160 / 400, y 441");
        Expect(is(box(W::CheckBookmarks), 237, 489, 139, 24) && is(box(W::CheckHistory), 396, 489, 87, 24), "3: checks 139 and 87 wide from 237, gap 20");
        Expect(is(box(W::Back), 20, 530, 67, 32) && is(box(W::SkipStep), 95, 530, 67, 32) && is(box(W::Primary), 533, 530, 167, 32), "3: « Retour » « Passer » « Importer mes données »");
        Expect(f.window.Flow().CanImport(), "3: first available browser picked, both boxes checked");
        f.window.ActivateForTest(W::Primary);
        Expect(f.window.Flow().screen() == Screen::ImportRunning && f.started == 1, "Importer starts the host's import");
        Expect(is(box(W::Cancel), 20, 530, 74, 32), "4: « Annuler » (hug)");
        f.window.ImportFinished(f.token + 7, {true, true, L""});
        Expect(f.window.Flow().screen() == Screen::ImportRunning, "an answer with another token is ignored");
        f.window.ImportFinished(f.token, {true, false, L"Google Chrome est ouvert. Fermez-le, puis réessayez."});
        Expect(f.window.Flow().screen() == Screen::ImportError && f.window.Flow().failedHistory(), "history failed -> board 5.1");
        Expect(is(box(W::SkipStep), 20, 530, 67, 32) && is(box(W::Primary), 610, 530, 90, 32), "5.1: « Passer », « Réessayer » 90");
        f.window.ActivateForTest(W::Primary);
        Expect(f.window.Flow().screen() == Screen::ImportRunning && f.started == 2 && !f.window.Flow().askedBookmarks() && f.window.Flow().askedHistory(), "Réessayer asks for the history only");
        f.window.ImportFinished(f.token, {false, true, L""});
        Expect(f.window.Flow().screen() == Screen::ImportDone, "retry succeeds -> board 5");
        Expect(is(box(W::Primary), 316, 530, 88, 32), "5: « Continuer » centred");
        f.window.ActivateForTest(W::Primary);
        Expect(f.window.Flow().screen() == Screen::Annotation, "Continuer -> board 6");
        Expect(is(box(W::Decline), 19, 531, 123, 32) && is(box(W::Primary), 633, 531, 68, 32) && is(box(W::Lang), 12, 10, 85, 28) && is(box(W::Close), 680, 10, 28, 28), "6: no border: one DIP up and left (19, 531 / 701)");
        f.window.ActivateForTest(W::Primary);
        Expect(f.window.Flow().screen() == Screen::Outro && f.window.Flow().annotate(), "Activer -> closing logo");
    }
    // ---- keyboard, language, name errors, sources ----
    {
        Fixture f; f.root = root; f.out = out; f.Setup(); using W = UI::Window;
        f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == W::SkipAll, "1: Tab starts on the first button");
        f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == W::Primary, "1: then the main button");
        f.window.KeyForTest(VK_TAB); f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == W::Close, "1: then language and close");
        f.window.KeyForTest(VK_TAB); Expect(f.window.FocusForTest() == W::SkipAll, "1: and round again");
        Fixture g; g.root = root; g.out = out; g.Setup();
        g.window.KeyForTest(VK_RETURN); Expect(g.window.Flow().screen() == Screen::Profile, "Entrée with no focus = the main button");
        g.window.KeyForTest(VK_TAB); g.window.KeyForTest(VK_TAB); Expect(g.window.FocusForTest() == W::Icons, "2: Tab: name, then the icon strip");
        g.window.KeyForTest(VK_RIGHT); Expect(g.window.Draft().icon == L"graduation-cap", "2: Right on the strip selects the next icon (board order)");
        g.window.KeyForTest(VK_HOME); Expect(g.window.Draft().icon == L"briefcase", "2: Début = first icon");
        g.window.KeyForTest(VK_END); Expect(g.window.Draft().icon == L"handshake", "2: Fin = last icon (the strip scrolls to it)");
        g.window.KeyForTest(VK_TAB); g.window.KeyForTest(VK_LEFT); Expect(g.window.Draft().hue == 199, "2: Left on Teinte: one degree");
        g.window.KeyForTest(VK_HOME); g.window.KeyForTest(VK_LEFT); Expect(g.window.Draft().hue == 359, "2: the hue wraps from the keyboard");
        g.window.KeyForTest(VK_TAB); g.window.KeyForTest(VK_HOME); Expect(K::IntensityPercent(g.window.Draft()) == 0 && g.window.Draft().neutral, "2: Intensité at 0 = the neutral profile");
        g.window.KeyForTest(VK_END); Expect(K::IntensityPercent(g.window.Draft()) == 100 && !g.window.Draft().neutral, "2: Fin = 100 %");
        g.refuseName = true; g.window.KeyForTest(VK_RETURN);
        Expect(g.window.Flow().screen() == Screen::Profile && g.saved == 0, "2: a name the host refuses keeps the board (error shown)");
        Expect(g.Shot(L"state-2-name-refused"), "render: board 2 with a refused name");
        g.refuseName = false; g.window.KeyForTest(VK_RETURN); Expect(g.window.Flow().screen() == Screen::ImportPick && g.saved == 1, "2: accepted once the host agrees");
        UI::ImportPick none = g.window.PickForTest(); none.bookmarks = none.history = false; g.window.SetPickForTest(none);
        Expect(!g.window.Flow().CanImport(), "3: nothing checked: Importer disabled");
        g.window.ActivateForTest(W::Primary); Expect(g.window.Flow().screen() == Screen::ImportPick && g.started == 0, "3: the disabled button does nothing");
        Expect(g.Shot(L"state-3-nothing-checked"), "render: board 3, nothing checked");
        {
            const auto listed = FakeSources();
            Expect(std::wstring(UI::SourceIcon(listed[3])) == L"file" && std::wstring(UI::SourceIcon(listed[0])) == L"favicon" && std::wstring(UI::SourceIcon(listed[2])) == L"favicon", "3: « Fichier de favoris HTML… » carries the file glyph, a browser without a logo the globe");
            Expect(GetFileAttributesW((root + L"\\assets\\icons\\file-96.png").c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW((root + L"\\assets\\icons\\favicon-96.png").c_str()) != INVALID_FILE_ATTRIBUTES, "3: both glyphs are bundled icons");
        }
        UI::ImportPick file = none; file.browser = 3; file.bookmarks = file.history = true; g.window.SetPickForTest(file);
        Expect(g.window.Flow().CanImport(), "3: HTML file: bookmarks only, still importable");
        Expect(g.Shot(L"state-3-html-file"), "render: board 3, HTML file (no profile select, history unavailable)");
        g.window.ActivateForTest(W::SkipStep); Expect(g.window.Flow().screen() == Screen::Annotation, "3: Passer -> board 6");
        g.window.KeyForTest(VK_ESCAPE); Expect(g.window.Flow().screen() == Screen::Closed && g.window.Flow().ending() == Onboarding::Ending::Skipped, "Échap: straight to the browser");
        // The language picker: the process language changes at once, the host is told, an untouched default name follows.
        {
            Fixture l; l.root = root; l.out = out; l.Setup(); l.window.SetDraftForTest(UI::PaperSeed(L"default", Profiles::DefaultName()));
            l.window.SetLanguageForTest(I18n::Locale::En);
            Expect(I18n::Current() == I18n::Locale::En && l.language == I18n::Locale::En, "language picker: English at once, preference handed to the host");
            Expect(l.window.Draft().name == L"Personal", "language picker: the untouched default name follows the language");
            l.window.SetLanguageForTest(I18n::Locale::En); Expect(I18n::Current() == I18n::Locale::En, "language picker: same language again changes nothing");
            Profiles::Profile named = UI::PaperSeed(L"default", L"Alex"); l.window.SetDraftForTest(named);
            l.window.SetLanguageForTest(I18n::Locale::Fr);
            Expect(I18n::Current() == I18n::Locale::Fr && l.language == I18n::Locale::Fr && l.window.Draft().name == L"Alex", "language picker: back to French, a name the person typed is kept");
        }
        {
            // Bookmarks not imported: no bar. A long bar: titles cut, the rest clipped by the window.
            Fixture b; b.root = root; b.out = out; b.Setup(); using W2 = UI::Window;
            b.window.ActivateForTest(W2::Primary); b.window.ActivateForTest(W2::Primary);
            { UI::ImportPick p = b.window.PickForTest(); p.bookmarks = false; b.window.SetPickForTest(p); }
            b.window.ActivateForTest(W2::Primary);
            b.window.ImportFinished(b.token, {false, true, L""});
            Expect(b.window.Flow().screen() == Screen::ImportDone && b.window.BarForTest().empty(), "history only: the mock keeps no bookmarks bar");
            Expect(b.Shot(L"state-5-history-only"), "render: board 5, history only (no bar)");
            b.window.SetBarForTest({{L"Un dossier au nom vraiment très long pour la barre", true}, {L"Recettes de cuisine du dimanche et autres", false}, {L"Travail", true}, {L"https://exemple.example/une/adresse", false}, {L"A", false}, {L"Documentation", false}, {L"Encore un", false}, {L"Hors champ", false}});
            Expect(b.Shot(L"state-5-long-bar"), "render: board 5 with a long bar (cut titles, overflow)");
            b.window.ActivateForTest(W2::Primary);
            // A second import from board 3 starts from an empty bar again.
        }
        {
            // The tunable 2 -> 3 timeline belongs to the window; the defaults are the shipped motion.
            Fixture t; t.root = root; t.out = out; t.Setup();
            Expect(t.window.TuningForTest() == Onboarding::Tuning::Defaults(), "tuning: a normal run plays the default 2 -> 3 transition");
            namespace T = Onboarding::Tuning;
            auto v = T::Defaults(); v[T::T23][T::Duration] = 500; v[T::Exit][T::LogoScale] = .8f; t.window.SetTuningForTest(T::Read(T::Write(v)));
            Expect(t.window.TuningForTest()[T::T23][T::Duration] == 500 && t.window.TuningForTest()[T::Exit][T::LogoScale] == .8f && t.window.TuningForTest()[T::T12] == T::Defaults(T::T12), "tuning: values read back from the panel's file drive the window, each transition its own");
            for (int s = 0; s < T::Sections; ++s) t.window.Replay(static_cast<T::Section>(s));
            Expect(t.window.Flow().screen() == Screen::Welcome && t.saved == 0 && t.started == 0, "tuning: « Rejouer » is for the eye only: the flow does not move, nothing is saved or imported");
        }
        {
            // Mid-transition frames: the board that leaves is drawn with ITS OWN boxes. (Bug seen by the designer:
            // board 3's checkboxes, labels and selects piled up in the window's top-left corner for a moment.)
            // Left of the centred title and right of the language button, the header strip holds nothing.
            IWICImagingFactory* wic = nullptr; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
            auto stray = [&](const std::wstring& name) {
                Onboarding::Shaders::Image img; if (!Onboarding::Shaders::LoadPng(wic, out + L"\\" + name + L"@96.png", img)) return -1;
                int bright = 0;
                for (unsigned y = 0; y < 60; ++y) for (unsigned x = 0; x < 262; ++x) {
                    if (x < 112 && y >= 8 && y < 42) continue;   // « Français » and its chevron
                    const auto* p = &img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4]; if (p[0] > 110 || p[1] > 110 || p[2] > 110) ++bright;
                }
                return bright;
            };
            Fixture m; m.root = root; m.out = out; m.Setup(); using W3 = UI::Window;
            struct Case { const wchar_t* name; int action; Screen from; float ms; };
            auto frame = [&](const wchar_t* name, Screen from, float ms) { return m.window.RenderTransitionForTest(from, ms, 96, (out + L"\\" + name + L"@96.png").c_str()) ? stray(name) : -1; };
            m.window.ActivateForTest(W3::Primary);
            Expect(frame(L"move-1-2-early", Screen::Welcome, 20) == 0 && frame(L"move-1-2-late", Screen::Welcome, 300) == 0, "1 -> 2: nothing stray in the corner, early or late");
            m.window.ActivateForTest(W3::Primary);
            Expect(frame(L"move-2-3-early", Screen::Profile, 20) == 0 && frame(L"move-2-3-late", Screen::Profile, 300) == 0, "2 -> 3: nothing stray in the corner");
            m.window.ActivateForTest(W3::Back);
            Expect(frame(L"move-3-2-early", Screen::ImportPick, 20) == 0, "3 -> 2: board 3's selects and checkboxes leave from their own places");
            m.window.ActivateForTest(W3::Primary); m.window.ActivateForTest(W3::Primary);
            Expect(frame(L"move-3-4-early", Screen::ImportPick, 20) == 0, "3 -> 4: same on the way to the import");
            m.window.ImportFinished(m.token, {true, false, L"Google Chrome est ouvert. Fermez-le, puis réessayez."});
            Expect(frame(L"move-4-51-early", Screen::ImportRunning, 20) == 0, "4 -> 5.1: « Annuler » leaves from its own place");
            m.window.ActivateForTest(W3::SkipStep);
            Expect(frame(L"move-51-6-early", Screen::ImportError, 20) >= 0, "5.1 -> 6: frame rendered (the light gradient fills the corner: looked at, not counted)");
            m.window.ActivateForTest(W3::Primary);
            Expect(frame(L"move-6-7-early", Screen::Annotation, 20) >= 0, "6 -> 7: frame rendered");
            if (wic) wic->Release();
        }
        {
            // Board 6's dither: the annotation band's texture (src/annotation-dither.h) laid over the gradient.
            namespace TR = Onboarding::Trame; namespace D = NativeChrome::Annotations::Dither; namespace T = Onboarding::Tuning;
            auto raster = [](float scale, double phase, const TR::Look& look, int* dots) { const int w = static_cast<int>(725 * scale), h = static_cast<int>(407 * scale); std::vector<std::uint32_t> px(static_cast<std::size_t>(w) * h, 0xDEADBEEF); *dots = TR::Raster(D::Raster{px.data(), w, w, h}, scale, phase, look); return px; };
            int n0 = 0, n1 = 0, n2 = 0, none = 0; const TR::Look look;
            const auto a = raster(1, 0, look, &n0), again = raster(1, 0, look, &n1), half = raster(1, .5, look, &n2);
            Expect(n0 > 2000 && n0 < 725 * 407 / 4 / 3 && a == again, "dither: dots on a sparse grid, the same picture for the same instant");
            {
                // The raster is the band's field, cell for cell (the reference: Field(), which calls the band's own WaveBand).
                int same = 0, cells = 0;
                for (double phase : {0.0, .37, .81}) { int n = 0; const auto px = raster(1, phase, look, &n);
                    for (int cy = 0; cy * 2 < 407; ++cy) for (int cx = 0; cx * 2 < 725; ++cx) { const float v = TR::Field((cx + .5f) * 2, (cy + .5f) * 2, 725, 407, phase, look); const bool want = v > D::Threshold(cx, cy), tie = std::fabs(v - D::Threshold(cx, cy)) < 2e-4f; ++cells; same += tie || (px[static_cast<std::size_t>(cy * 2) * 725 + cx * 2] != 0) == want; } }
                Expect(same == cells, "dither: every cell is the annotation band's own field (src/annotation-dither.h WaveBand), not a lookalike");
            }
            Expect(a != half && raster(1, Onboarding::LoopPhase(8, 8), look, &n1) == a, "dither: it moves, and the end of a pass is its start (seamless)");
            bool grid = true, ink = true; std::uint32_t top = 0;
            for (int y = 0; y < 407; ++y) for (int x = 0; x < 725; ++x) { const std::uint32_t p = a[static_cast<std::size_t>(y) * 725 + x]; if (!p) continue; grid = grid && (x % 2 == 0) && (y % 2 == 0); const std::uint32_t al = p >> 24; ink = ink && ((p >> 16) & 255) == al && ((p >> 8) & 255) == al && (p & 255) == al; top = std::max(top, al); }
            Expect(grid, "dither: the band's pitch — one dot per 2 DIP cell, the last row and column of a cell left empty");
            Expect(ink && top <= static_cast<std::uint32_t>(std::lround(.22f * 220)) + 1 && top >= 30, "dither: white ink at the band's two alphas, times its own opacity (subtle)");
            TR::Look off = look; off.opacity = 0; raster(1, 0, off, &none);
            Expect(none == 0, "dither: opacity 0 draws nothing");
            int scaled = 0; const auto big = raster(1.25f, 0, look, &scaled);
            Expect(scaled > 0 && D::CellPx(look.cell, 1.25f) == 3, "dither: cells snap to device pixels at 125 % (3 px cell, 2 px dot)");
            // On the board: there, but quiet.
            IWICImagingFactory* wic = nullptr; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
            Fixture d; d.root = root; d.out = out; d.Setup(); using W4 = UI::Window; d.window.SetShaderSecondsForTest(0);
            d.window.ActivateForTest(W4::Primary); d.window.ActivateForTest(W4::Primary); d.window.ActivateForTest(W4::SkipStep);
            auto quiet = T::Defaults(); quiet[T::Trame][T::TrameOpacity] = 0; d.window.SetTuningForTest(quiet);
            Expect(d.Shot(L"state-6-without-dither"), "render: board 6 without its dither");
            d.window.SetTuningForTest(T::Defaults()); Expect(d.Shot(L"state-6-with-dither"), "render: board 6 with its dither");
            Onboarding::Shaders::Image with, without; double sum = 0, text = 0; long changed = 0;
            if (Onboarding::Shaders::LoadPng(wic, out + L"\\state-6-with-dither@96.png", with) && Onboarding::Shaders::LoadPng(wic, out + L"\\state-6-without-dither@96.png", without)) {
                for (unsigned y = 0; y < 580; ++y) for (unsigned x = 0; x < 720; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * 720 + x) * 4; const int dv = std::abs(with.rgba[i] - without.rgba[i]) + std::abs(with.rgba[i + 1] - without.rgba[i + 1]) + std::abs(with.rgba[i + 2] - without.rgba[i + 2]);
                    if (y < 400) { sum += dv; changed += dv > 0; }
                    if (y >= 415 || (y >= 183 && y < 219 && x >= 86 && x < 316) || (y >= 181 && y < 273 && x >= 341 && x < 630)) text += dv;   // the texts, the buttons, the illustration's button and note
                }
            }
            Expect(changed > 1500 && sum / (720.0 * 400 * 3) < 3.0, "dither on board 6: present (thousands of pixels) and subtle (under 3 / 255 on average)");
            Expect(text == 0, "dither on board 6: nothing of it over the texts, the buttons or the illustration");
            if (wic) wic->Release();
        }
        {
            // The mock's tab title keeps its descenders (the Paper picture cuts the « y » of « By Val.S »): with a
            // title made of descenders, the ink goes well below the baseline and the row under it is the tab again.
            IWICImagingFactory* wic = nullptr; CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
            Fixture g; g.root = root; g.out = out; g.Setup(); using W5 = UI::Window; g.window.SetMockTitleForTest(L"gjpqy");
            g.window.ActivateForTest(W5::Primary); g.window.ActivateForTest(W5::Primary);   // board 3: the mock at 1383 wide
            Expect(g.Shot(L"state-3-tab-descenders"), "render: board 3 with a tab title made of descenders");
            for (UINT dpi : {96u, 120u}) {
                Onboarding::Shaders::Image img; const float sc = dpi / 96.f, s = 1383.f / 1180;
                const bool loaded = Onboarding::Shaders::LoadPng(wic, out + (dpi == 96 ? L"\\state-3-tab-descenders@96.png" : L"\\state-3-tab-descenders@120.png"), img);
                // The tab's title lane in the window: from the picture's title start, between the tab's top and bottom edges.
                const int x0 = static_cast<int>((18 + 78 * s) * sc), x1 = static_cast<int>((18 + 150 * s) * sc), y0 = static_cast<int>((48 + 9 * s) * sc), y1 = static_cast<int>((48 + 38 * s) * sc);
                int top = -1, bottom = -1;
                for (int y = y0; loaded && y < y1; ++y) for (int x = x0; x < x1; ++x) if (img.rgba[(static_cast<std::size_t>(y) * img.width + x) * 4] > 90) { if (top < 0) top = y; bottom = y; }
                const float font = 14 * s * sc;   // « gjpqy »: x-height 0.546 em plus a descender of about 0.21 em — a cut title would stop near 0.55
                Expect(loaded && top >= 0 && (bottom - top + 1) >= .68f * font, dpi == 96 ? "96 DPI: the descenders go their full depth below the baseline (not cut)" : "120 DPI: the descenders go their full depth below the baseline (not cut)");
                bool clear = loaded && bottom + 2 < y1;
                for (int x = x0; clear && x < x1; ++x) clear = img.rgba[(static_cast<std::size_t>(bottom + 2) * img.width + x) * 4] < 60;
                Expect(clear, dpi == 96 ? "96 DPI: under the title the tab is plain again (the ink ends by itself, inside the tab)" : "120 DPI: under the title the tab is plain again");
            }
            g.window.SetMockTitleForTest(L"By Val.S");
            if (wic) wic->Release();
        }
        Fixture h; h.root = root; h.out = out; h.Setup(false);
        h.window.ActivateForTest(W::Primary); h.window.ActivateForTest(W::Primary); h.window.ActivateForTest(W::SkipStep);
        Expect(h.window.Flow().screen() == Screen::Outro, "no annotation mode in the build: board 6 is skipped");
    }
    // ---- the boards, as Paper shows them (French, sample profile) ----
    {
        Fixture f; f.root = root; f.out = out; f.logo = PaperLogo(root); f.Setup(); using W = UI::Window;
        Expect(f.logo != nullptr, "the boards' source logo fixture");
        f.window.SetShaderSecondsForTest(0);
        Expect(f.Shot(L"1"), "render: board 1"); Expect(f.window.ShadersReadyForTest(), "the shaders ran on Direct3D (not the plain-artwork fallback)");
        f.window.ActivateForTest(W::Primary); Expect(f.Shot(L"2"), "render: board 2");
        f.window.ActivateForTest(W::Primary); Expect(f.Shot(L"3"), "render: board 3");
        f.window.ActivateForTest(W::Primary); f.window.SetProgressForTest(7 / 19.f); Expect(f.Shot(L"4"), "render: board 4 (7 ticks of 19, as the board)");
        Expect(f.window.BarForTest().empty(), "4: while the source is read the mock has no bookmarks bar yet");
        Expect(f.Shot(L"state-4-reading"), "render: board 4 before anything is imported (no bar)");
        // The bar of the boards, as an import would hand it over (the boards' sample titles).
        UI::ImportOutcome failed{true, false, L"Google Chrome est ouvert. Fermez-le, puis réessayez."};
        failed.bar = {{L"Veille", true}, {L"Paper", false}, {L"ValXXII/Browser", false}, {L"Le Monde", false}, {L"MDN Web Docs", false}, {L"Mobbin", false}, {L"France Culture", false}};
        f.window.SetBarForTest(failed.bar); Expect(f.Shot(L"4"), "render: board 4 (7 ticks of 19, the imported bar)");
        f.window.SetBarForTest({});
        f.window.ImportFinished(f.token, failed); Expect(f.window.BarForTest().size() == 7, "the saved bookmarks' bar items reach the mock"); Expect(f.Shot(L"5.1"), "render: board 5.1");
        f.window.ActivateForTest(W::Primary); f.window.ImportFinished(f.token, {false, true, L""});
        Expect(f.window.BarForTest().size() == 7, "a retry of the history keeps the bar the first pass imported"); Expect(f.Shot(L"5"), "render: board 5");
        f.window.ActivateForTest(W::Primary); Expect(f.Shot(L"6"), "render: board 6");
        f.window.ActivateForTest(W::Primary); Expect(f.Shot(L"7"), "render: board 7");
    }
    // ---- English, another profile colour, the loop's frames ----
    {
        I18n::SetForProcess(I18n::Locale::En);
        Fixture f; f.root = root; f.out = out; f.Setup(); using W = UI::Window;
        Expect(f.Shot(L"en-1"), "render: board 1 in English");
        f.window.ActivateForTest(W::Primary);
        Profiles::Profile violet = UI::PaperSeed(L"default", L"Studio"); violet.hue = 300; violet.intensity = .14f; violet.icon = L"paint-brush"; f.window.SetDraftForTest(violet);
        Expect(f.Shot(L"en-2-violet"), "render: board 2 in English, violet profile, another icon (the strip scrolled)");
        f.window.ActivateForTest(W::Primary); Expect(f.Shot(L"en-3-violet"), "render: board 3 in English");
        f.window.ActivateForTest(W::SkipStep); Expect(f.Shot(L"en-6-violet"), "render: board 6 follows the profile colour");
        I18n::SetForProcess(I18n::Locale::Fr);
        Fixture loop; loop.root = root; loop.out = out; loop.Setup();
        loop.window.SetShaderSecondsForTest(Onboarding::HalftoneLoopSeconds * .5); Expect(loop.Shot(L"1-loop-half"), "render: board 1 half a turn into the loop");
        loop.window.SetShaderSecondsForTest(Onboarding::HalftoneLoopSeconds); Expect(loop.Shot(L"1-loop-full"), "render: board 1 after one full turn");
    }
    probe.Dispose();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
