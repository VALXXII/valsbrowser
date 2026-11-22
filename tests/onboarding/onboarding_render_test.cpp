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
        UI::ImportPick file = none; file.browser = 3; file.bookmarks = file.history = true; g.window.SetPickForTest(file);
        Expect(g.window.Flow().CanImport(), "3: HTML file: bookmarks only, still importable");
        Expect(g.Shot(L"state-3-html-file"), "render: board 3, HTML file (no profile select, history unavailable)");
        g.window.ActivateForTest(W::SkipStep); Expect(g.window.Flow().screen() == Screen::Annotation, "3: Passer -> board 6");
        g.window.KeyForTest(VK_ESCAPE); Expect(g.window.Flow().screen() == Screen::Closed && g.window.Flow().ending() == Onboarding::Ending::Skipped, "Échap: straight to the browser");
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
        f.window.ImportFinished(f.token, {true, false, L"Google Chrome est ouvert. Fermez-le, puis réessayez."}); Expect(f.Shot(L"5.1"), "render: board 5.1");
        f.window.ActivateForTest(W::Primary); f.window.ImportFinished(f.token, {true, true, L""}); Expect(f.Shot(L"5"), "render: board 5");
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
