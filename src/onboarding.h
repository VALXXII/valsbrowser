#pragma once
#include "i18n.h"
// First-run onboarding window (Paper p-R-0 « Onboarding — fenêtre de lancement », boards « 1 » … « 7 »,
// 720 × 580, radius 12, canvas #171717, 1 px #FFFFFF1A hairline; board 6 is black and has no hairline).
// Every position, size, colour and text below is the Paper value (design/onboarding-values.md lists them with
// their node); coordinates are DIP in the window, origin at its top-left corner.
//
//   1 Bienvenue           HalftoneDots logo (animated: the dot lattice slides, src/onboarding-shaders.h)
//   2 Créer votre profil  name · icon strip · Teinte / Intensité, over the zoomed browser mock (live profile chip)
//   3 Importer vos données  browser and profile selects, « Favoris et dossiers » / « Historique »
//   4 Import en cours…    source logo → 19 ticks → GemSmoke logo; the mock shows the bookmarks bar
//   5 Import terminé !    5.1 the error state (what failed, why, « Réessayer »)
//   6 Mode Annotation     GrainGradient + the annotation illustration (only when the mode is compiled in)
//   7 the logo alone      closing transition: the window fades and the browser opens
//
// The flow is src/onboarding-flow.h (pure, unit-tested); this window draws it and turns input into its events.
// The host (src/onboarding-integration.h) does the real work behind Hooks: save the profile, list and read the
// import sources, write the language preference.
//
// Motion (Paper is silent; design skills): screen changes 220 ms — the old content fades out, the browser mock
// travels between its two framings (ease-out), the new content fades in; reduced motion: 120 ms, opacity only,
// shaders still. Hover 120 ms. Frames come from the display's clock (src/frame-clock.h), only while something
// moves and the window is visible.
// Keyboard: Tab / Maj+Tab through the controls then the buttons, Entrée = the focused control, else the main
// button; Espace toggles; ← → on the icon strip and the sliders (Maj 10, Page 30, Début / Fin); Échap leaves.
#include <windows.h>
#include <windowsx.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "tooltip.h"
#include "context-menu.h"
#include "frame-clock.h"
#include "profile-icons.h"
#include "profiles.h"
#include "ui-language.h"
#include "onboarding-flow.h"
#include "onboarding-model.h"
#include "onboarding-shaders.h"
#include "onboarding-kit.h"
#include "onboarding-tuning.h"
#include "onboarding-debug.h"
#include "onboarding-handoff.h"
#include "onboarding-dither.h"

namespace NativeChrome {
namespace OnboardingUI {

namespace K = OnboardingKit;
using Onboarding::Screen;

namespace Layout {
inline constexpr float width = 720, height = 580, radius = 12;
inline constexpr float barTop = 530, barLeft = 20, barRight = 700, button = 32, buttonPad = 12, buttonRadius = 6;   // boards 1 – 5.1
inline constexpr float headerTop = 11, langLeft = 13, closeLeft = 679, control = 28;
inline constexpr float titleCentre = 360, titleTop = 17.4f;
// Board 2 (1C5U-0: the row overflows its box by 17, centred: starts at 23.5).
inline constexpr float rowLeft = 23.5f, labelTop = 441, fieldTop = 465, fieldW = 83, fieldH = 32;
inline constexpr float stripLeft = 131.5f, stripW = 140, cell = 32, cellPitch = 36, stripFade = 53;
inline constexpr float laneLabel = 296.5f, track = 370.5f, trackW = 282, trackH = 10, thumbW = 4, thumbH = 20, valueLeft = 660.5f, valueW = 36;
inline constexpr float hueRow = 445, intensityRow = 473, row = 24;
// Boards 3 – 5.1.
inline constexpr float selectsTop = 417, selectH = 32, browserW = 228, profileW = 160, selectGap = 12, checksTop = 489;
inline constexpr float flowTop = 456, flowH = 57, flowLeft = 166.5f, logoBox = 51, logo = 42, tick = 2, tickH = 16, tickPitch = 14, gemW = 58, gemH = 57;
inline constexpr int ticks = 19;
// Browser mock (1180-unit layout): board 2 at (72, 99) 2521 wide, boards 3 – 5.1 at (18, 48) 1383 wide.
inline constexpr float mockUnits = 1180, mockPlainUnits = 600, mockPlainH = 440, mockWideUnits = 1383, mockWideShown = 703, mockWideH = 516;
}

// BrowserSource, BrowserProfile, ImportPick, ImportOutcome: src/onboarding-model.h.

struct Hooks {
    std::function<Profiles::NameError(const Profiles::Profile&)> saveProfile;     // « Continuer » on board 2: None = saved
    // The import sources of board 3. Called on a WORKER thread (listing browsers, profiles and logos takes time and
    // must never hold the window): as soon as the window opens, and again after a language change.
    std::function<std::vector<BrowserSource>()> sources;
    // Starts the read of `pick` (worker thread); answers later with Window::ImportFinished(token, …) on the UI
    // thread. False: nothing started (file dialog cancelled, source gone).
    std::function<bool(unsigned token, const ImportPick& pick, bool bookmarks, bool history)> startImport;
    std::function<void(I18n::Locale)> language;                                   // persist the choice
    std::function<void(WPARAM, LPARAM)> message;                                  // Window::HostMessage posted by the host's worker
    std::function<void(HWND)> created;                                            // the window exists (where the worker posts)
};
struct Options {
    Profiles::Profile seed;                       // the draft the profile board starts from
    bool annotation = true, reduced = false;
    bool debug = false;                           // « --onboarding-debug »: the tuning panel of the 2 → 3 transition
    std::wstring tuningFile;                      // debug only: where the panel keeps its values
    OnboardingHandoff::Stage* stage = nullptr;    // the host's stage for the hand-off to the browser (null: the window just fades)
    Hooks hooks;
};
struct Result { Onboarding::Ending ending = Onboarding::Ending::Skipped; bool annotate = false, profileSaved = false, handedOff = false; };   // handedOff: the stage is playing the window's exit, the browser takes its turn on it

// The strip shows the icons in the board's order (briefcase, house, heart, graduation cap), then the others.
inline const std::vector<int>& IconOrder() {
    static const std::vector<int> order = [] {
        std::vector<int> v;
        for (const wchar_t* key : {L"briefcase", L"house", L"heart", L"graduation-cap"}) v.push_back(ProfileIcons::IndexOf(key));
        for (int i = 0; i < ProfileIcons::count; ++i) if (std::find(v.begin(), v.end(), i) == v.end()) v.push_back(i);
        return v;
    }();
    return order;
}
// The glyph of a source that has no logo of its own (assets\\icons): the HTML bookmarks file is a file, anything
// else the globe — the same two names Réglages › « Importer depuis un navigateur… » gives its list.
inline const wchar_t* SourceIcon(const BrowserSource& s) { return s.file ? L"file" : L"favicon"; }
// The draft of a first run: the board's sample look (heart, 200°, 88 %) under the default profile's name.
inline Profiles::Profile PaperSeed(const std::wstring& id, const std::wstring& name) {
    Profiles::Profile p; p.id = id; p.name = name; p.icon = L"heart"; p.hue = K::PaperHue; p.intensity = K::PaperChroma; p.neutral = false; return p;
}

class Window {
public:
    enum Id : int { None = 0, Lang = 1, Close = 2, Primary = 10, Back = 11, SkipAll = 12, SkipStep = 13, Cancel = 14, Decline = 15,
                    Name = 20, Hue = 21, Intensity = 22, Icons = 23, Browser = 30, Profile = 31, CheckBookmarks = 32, CheckHistory = 33, IconBase = 100 };
    static constexpr UINT FrameMessage = WM_APP + 0x4F0, HostMessage = WM_APP + 0x4F1, ReplayDoneMessage = WM_APP + 0x4F2, SourcesMessage = WM_APP + 0x4F3;
    static constexpr float TextGamma = 2.2f, TextContrast = .5f;
    static constexpr float WelcomeRaise = 35.5f / 721;   // (579 - 508) / 2 over the picture box
private:
    struct Item { int id; D2D1_RECT_F r; };
    struct Layer {
        ID2D1Bitmap* bitmap = nullptr; ID2D1RenderTarget* owner = nullptr; Onboarding::Shaders::Frame frame; bool fresh = false; Motion::Time shaded{}; float key = 0;
        void Drop() { if (bitmap) { bitmap->Release(); bitmap = nullptr; } owner = nullptr; fresh = false; shaded = {}; }
        // `part`: the pixels of `frame` that changed (the whole frame when the bitmap is new).
        bool Upload(ID2D1RenderTarget* t, const RECT* part = nullptr) {
            if (!t || !frame.width || !frame.height) return false;
            if (bitmap && owner == t) { const auto size = bitmap->GetPixelSize(); if (size.width != frame.width || size.height != frame.height) Drop(); }
            if (!bitmap || owner != t) {
                Drop();
                const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
                if (FAILED(t->CreateBitmap(D2D1::SizeU(frame.width, frame.height), frame.bgra.data(), frame.width * 4, props, &bitmap))) { bitmap = nullptr; return false; }
                owner = t;
            } else if (part) {
                const D2D1_RECT_U to{static_cast<UINT32>(part->left), static_cast<UINT32>(part->top), static_cast<UINT32>(part->right), static_cast<UINT32>(part->bottom)};
                bitmap->CopyFromMemory(&to, frame.bgra.data() + (static_cast<std::size_t>(part->top) * frame.width + part->left) * 4, frame.width * 4);
            } else bitmap->CopyFromMemory(nullptr, frame.bgra.data(), frame.width * 4);
            fresh = true; return true;
        }
    };
    enum LayerId { HalftoneWelcome = 0, HalftoneOutro = 1, Gem = 2, Grain = 3, LayerCount = 4 };

    HINSTANCE instance_ = nullptr; HWND hwnd_ = nullptr, edit_ = nullptr; HFONT editFont_ = nullptr; HBRUSH editBrush_ = nullptr; UINT editDpi_ = 0;
    std::wstring root_; Chrome chrome_; Chrome* c_ = &chrome_; Accessibility::Bridge uia_; ContextMenu::Menu menu_; FrameClock::Clock clock_; bool clockOk_ = false;
    K::Glyphs glyphs_; Onboarding::Shaders::Renderer gpu_; bool gpuTried_ = false; Layer layers_[LayerCount];
    Onboarding::Flow flow_; Options options_; Profiles::Profile draft_; Profiles::NameError nameError_ = Profiles::NameError::None;
    std::vector<BrowserSource> sources_; bool sourcesLoaded_ = false; ImportPick pick_; std::wstring errorDetail_;
    std::vector<Item> items_; Screen arranged_ = Screen::Welcome;   // whose boxes items_ holds
    UINT dpi_ = 96; bool done_ = false, keyboard_ = false, offscreen_ = false, syncingEdit_ = false, menuOpen_ = false;
    int focus_ = None, hover_ = None, pressed_ = None, dragging_ = None;
    std::map<int, float> glow_; float iconScroll_ = 0, iconScrollTarget_ = 0;
    Motion::Time opened_{}, lastStep_{}, changed_{}, left_{}, outroStart_{}, importStarted_{}, importAnsweredAt_{};
    Screen shown_ = Screen::Welcome, from_ = Screen::Welcome; bool changing_ = false, entering_ = false, leaving_ = false;
    unsigned importToken_ = 0; bool importAnswered_ = false; float importAtAnswer_ = 0, progressForTest_ = -1; ImportOutcome pending_;
    double secondsForTest_ = -1;
    Onboarding::Tuning::Values tuning_ = Onboarding::Tuning::Defaults(); OnboardingDebug::Panel panel_;
    std::vector<BarItem> bar_; Motion::Time barAt_{};   // the imported bookmarks bar shown in the mock (boards 4 – 5.1)
    // The end of the flow (board 7): the logo's exit inside the dot field, then the hand-off of the window.
    Motion::Time exitAt_{}; bool exitRunning_ = false, handedOff_ = false; float logoScale_ = 1, logoAlpha_ = 1;
    // Board 6's dither (src/onboarding-dither.h): rastered on the CPU at the device's pixels, 30 pictures a second.
    ID2D1Bitmap* trame_ = nullptr; ID2D1RenderTarget* trameOwner_ = nullptr; std::vector<std::uint32_t> trameBits_; Motion::Time trameAt_{}; double trameKey_ = -1;
    void DropTrame() { if (trame_) { trame_->Release(); trame_ = nullptr; } trameOwner_ = nullptr; trameKey_ = -1; }
    Onboarding::Trame::Look TrameLook() const { namespace T = Onboarding::Tuning; const auto& r = tuning_[T::Trame]; Onboarding::Trame::Look l; l.opacity = r[T::TrameOpacity]; l.cell = r[T::TramePitch]; l.base = r[T::TrameBase]; l.wave = r[T::TrameWave]; return l; }
    void ShadeTrame(ID2D1RenderTarget* t, Motion::Time now) {
        namespace T = Onboarding::Tuning; namespace D = NativeChrome::Annotations::Dither;
        const auto px = t->GetPixelSize(); const auto dip = t->GetSize(); const float scale = px.width / std::max(1.f, dip.width);
        const int w = static_cast<int>(std::lround(725 * scale)), h = static_cast<int>(std::lround(407 * scale));
        const auto look = TrameLook(); const double phase = Onboarding::LoopPhase(ShaderSeconds(now), tuning_[T::Trame][T::TramePeriod]);
        const double key = phase * 1000 + look.opacity * 7 + look.cell * 13 + look.base * 17 + look.wave * 19 + w;
        const bool current = trame_ && trameOwner_ == t && static_cast<int>(trame_->GetPixelSize().width) == w && static_cast<int>(trame_->GetPixelSize().height) == h;
        if (current && (key == trameKey_ || (trameAt_ != Motion::Time{} && Ms(trameAt_, now) < 1000.f / 15 - 3))) return;   // the wave takes 8 s to pass: 15 pictures a second
        trameBits_.resize(static_cast<std::size_t>(w) * h);
        Onboarding::Trame::Raster(D::Raster{trameBits_.data(), w, w, h}, scale, phase, look);
        if (!current) { DropTrame(); if (FAILED(t->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)), trameBits_.data(), static_cast<UINT32>(w) * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), &trame_))) { trame_ = nullptr; return; } trameOwner_ = t; }
        else trame_->CopyFromMemory(nullptr, trameBits_.data(), static_cast<UINT32>(w) * 4);
        trameKey_ = key; trameAt_ = now;
    }
    // Review panel: a transition replayed for the eye only (the flow does not move), or the hand-off on a stand-in.
    bool replaying_ = false, exitReplay_ = false; Motion::Time replayUntil_{}; OnboardingHandoff::Stage previewStage_;

    // ---- small helpers ----
    float Scale() const { return dpi_ / 96.f; }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Scale())); }
    static float Ms(Motion::Time from, Motion::Time to) { return static_cast<float>(std::chrono::duration<double, std::milli>(to - from).count()); }
    static float EaseOut(float u) { u = Motion::Clamp(u); const float v = 1 - u; return 1 - v * v * v * v; }   // ≈ cubic-bezier(.23, 1, .32, 1)
    static bool In(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
    static D2D1_RECT_F Box(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
    // The boards' flex layout lands on half pixels (23.5, 131.5 …); like the engine that drew them, boxes start
    // on a device pixel (half up: 23.5 is 24 at 100 %).
    float Sx(float dip) const { const float s = Scale(); return std::floor(dip * s + .5f) / s; }
    void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    bool Annotation(Screen s) const { return s == Screen::Annotation; }
    // Board 6 has no border: its content sits one DIP up and left of the bordered boards.
    D2D1_POINT_2F Nudge(Screen s) const { return Annotation(s) ? D2D1::Point2F(-1, -1) : D2D1::Point2F(0, 0); }
    const BrowserSource* Source() const { return pick_.browser >= 0 && pick_.browser < static_cast<int>(sources_.size()) ? &sources_[static_cast<std::size_t>(pick_.browser)] : nullptr; }
    const BrowserProfile* SourceProfile() const { const auto* s = Source(); return s && pick_.profile >= 0 && pick_.profile < static_cast<int>(s->profiles.size()) ? &s->profiles[static_cast<std::size_t>(pick_.profile)] : nullptr; }
    bool HasBookmarks() const { const auto* s = Source(); if (!s || !s->available) return false; if (s->file) return true; const auto* p = SourceProfile(); return p && p->bookmarks; }
    bool HasHistory() const { const auto* s = Source(); if (!s || !s->available || s->file) return false; const auto* p = SourceProfile(); return p && p->history; }
    bool ProfileSelectShown() const { const auto* s = Source(); return s && s->profiles.size() > 1; }
    void SyncImport() { const auto* s = Source(); flow_.SetSourceAvailable(s && s->available); flow_.SetCategories(pick_.bookmarks && HasBookmarks(), pick_.history && HasHistory()); }
    // The sources are listed off the UI thread and adopted when they arrive (SourcesMessage). Until then board 3
    // shows its select with « Recherche des navigateurs… » and nothing to import. Tests (no window): at once.
    unsigned sourcesAsked_ = 0; bool sourcesLoading_ = false; float slowestChangeMs_ = 0, slowestFrameMs_ = 0; unsigned frames_ = 0;
    struct SourcesJob { std::function<std::vector<BrowserSource>()> list; HWND window; unsigned asked; };
    void RequestSources() {
        if (offscreen_ || !hwnd_ || !options_.hooks.sources) { LoadSources(); return; }
        auto* job = new SourcesJob{options_.hooks.sources, hwnd_, ++sourcesAsked_}; sourcesLoading_ = true;
        HANDLE thread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            std::unique_ptr<SourcesJob> j(static_cast<SourcesJob*>(p));
            auto* found = new std::vector<BrowserSource>(j->list());
            if (!PostMessageW(j->window, SourcesMessage, j->asked, reinterpret_cast<LPARAM>(found))) delete found;   // the window is gone
            return 0;
        }, job, 0, nullptr);
        if (!thread) { delete job; sourcesLoading_ = false; LoadSources(); return; }
        CloseHandle(thread);
    }
    void LoadSources() { AdoptSources(options_.hooks.sources ? options_.hooks.sources() : std::vector<BrowserSource>{}); }
    void AdoptSources(std::vector<BrowserSource> found) {
        sources_ = std::move(found); sourcesLoaded_ = true; sourcesLoading_ = false;
        if (!Source() || !Source()->available) { pick_.browser = -1; pick_.profile = 0; for (std::size_t i = 0; i < sources_.size(); ++i) if (sources_[i].available) { pick_.browser = static_cast<int>(i); break; } }
        if (!SourceProfile()) pick_.profile = 0;
        SyncImport();
    }
    int IconSlot() const { const int index = ProfileIcons::IndexOf(draft_.icon.c_str()); const auto& o = IconOrder(); for (std::size_t i = 0; i < o.size(); ++i) if (o[i] == index) return static_cast<int>(i); return 0; }
    float IconScrollMax() const { return ProfileIcons::count * Layout::cellPitch - 4 - Layout::stripW; }
    void RevealIcon(bool animate) {
        // The selected cell stays clear of the fades: one cell from either end, unless it is the first or the last.
        const int slot = IconSlot(); const float x = slot * Layout::cellPitch, lo = slot == 0 ? 0 : Layout::cellPitch, hi = Layout::stripW - Layout::cell - (slot == ProfileIcons::count - 1 ? 0 : Layout::cellPitch);
        float target = iconScrollTarget_;
        if (x - target < lo) target = x - lo; else if (x - target > hi) target = x - hi;
        iconScrollTarget_ = std::clamp(target, 0.f, IconScrollMax());
        if (!animate || options_.reduced || offscreen_) iconScroll_ = iconScrollTarget_; else RequestFrame();
    }

    // ---- geometry of the interactive parts (items_) ----
    K::Font Label() const { return {14, 20, 500, 0}; }
    float ButtonWidth(const std::wstring& label, int weight) { return K::Width(*c_, label, {14, 20, weight, 0}) + 2 * Layout::buttonPad; }
    D2D1_RECT_F LangBox(Screen s) { const auto n = Nudge(s); const float w = 6 + K::Width(*c_, UiLanguage::Autonym(I18n::Current()), Label()) + 4 + 16 + 6; return Box(Layout::langLeft + n.x, Layout::headerTop + n.y, w, Layout::control); }
    D2D1_RECT_F CloseBox(Screen s) const { const auto n = Nudge(s); return Box(Layout::closeLeft + n.x - n.x * 2, Layout::headerTop + n.y, Layout::control, Layout::control); }
    D2D1_RECT_F NameField() const { return Box(Sx(Layout::rowLeft), Layout::fieldTop, Layout::fieldW, Layout::fieldH); }
    D2D1_RECT_F Strip() const { return Box(Sx(Layout::stripLeft), Layout::fieldTop, Layout::stripW, Layout::cell); }
    D2D1_RECT_F Cell(int slot) const { return Box(Sx(Layout::stripLeft) + slot * Layout::cellPitch - iconScroll_, Layout::fieldTop, Layout::cell, Layout::cell); }
    D2D1_RECT_F Track(int which) const { const float top = which == Hue ? Layout::hueRow : Layout::intensityRow; return Box(Sx(Layout::track), top + (Layout::row - Layout::trackH) / 2, Layout::trackW, Layout::trackH); }
    D2D1_RECT_F Lane(int which) const { const float top = which == Hue ? Layout::hueRow : Layout::intensityRow; return Box(Sx(Layout::track), top, Layout::trackW, Layout::row); }
    float Fraction(int which) const { return which == Hue ? draft_.hue / 359.f : K::IntensityPercent(draft_) / 100.f; }
    D2D1_RECT_F Thumb(int which) const { const auto t = Track(which); const float x = std::round(Fraction(which) * (Layout::trackW - Layout::thumbW)), cy = (t.top + t.bottom) / 2; return Box(t.left + x, cy - Layout::thumbH / 2, Layout::thumbW, Layout::thumbH); }
    D2D1_RECT_F BrowserSelect() const { const float total = ProfileSelectShown() ? Layout::browserW + Layout::selectGap + Layout::profileW : Layout::browserW; return Box(360 - total / 2, Layout::selectsTop + 24, Layout::browserW, Layout::selectH); }
    D2D1_RECT_F ProfileSelect() const { const auto b = BrowserSelect(); return Box(b.right + Layout::selectGap, b.top, Layout::profileW, Layout::selectH); }
    struct Checks { D2D1_RECT_F bookmarks, history; float labelB, labelH; };
    Checks CheckBoxes() {
        const float w1 = K::Width(*c_, TR(onboarding_import_bookmarks), Label()), w2 = K::Width(*c_, TR(import_item_history), Label());
        const float total = 16 + 7 + w1 + 20 + 16 + 7 + w2, left = 360 - total / 2, top = Layout::checksTop;
        Checks c; c.bookmarks = Box(left, top, 16 + 7 + w1, 24); c.history = Box(left + 16 + 7 + w1 + 20, top, 16 + 7 + w2, 24); c.labelB = w1; c.labelH = w2; return c;
    }
    struct ButtonSpec { int id; std::wstring label; bool accent; };
    std::vector<ButtonSpec> LeftButtons(Screen s) {
        switch (s) {
        case Screen::Welcome: return {{SkipAll, TR(onboarding_skip_browse), false}};
        case Screen::Profile: return {{Back, TR(common_back), false}};
        case Screen::ImportPick: return {{Back, TR(common_back), false}, {SkipStep, TR(onboarding_skip), false}};
        case Screen::ImportRunning: return {{Cancel, TR(common_cancel), false}};
        case Screen::ImportError: return {{SkipStep, TR(onboarding_skip), false}};
        case Screen::Annotation: return {{Decline, TR(onboarding_not_interested), false}};
        default: return {};
        }
    }
    std::wstring PrimaryLabel(Screen s) {
        switch (s) {
        case Screen::Welcome: return TR(onboarding_start);
        case Screen::Profile: case Screen::ImportDone: return TR(onboarding_continue);
        case Screen::ImportPick: return TR(onboarding_import_action);
        case Screen::ImportError: return TR(onboarding_retry);
        case Screen::Annotation: return TR(onboarding_activate);
        default: return L"";
        }
    }
    // Boards 1 and 6 set their buttons in Medium, the others in SemiBold.
    static int ButtonWeight(Screen s) { return s == Screen::Welcome || s == Screen::Annotation ? 500 : 600; }
    std::vector<Item> ButtonBoxes(Screen s) {
        std::vector<Item> out; const auto n = Nudge(s); const float top = Layout::barTop - n.y; float x = Layout::barLeft + n.x; const int weight = ButtonWeight(s);
        for (const auto& b : LeftButtons(s)) { const float w = ButtonWidth(b.label, weight); out.push_back({b.id, Box(x, top, w, Layout::button)}); x += w + 8; }
        const std::wstring primary = PrimaryLabel(s);
        if (!primary.empty()) {
            const float w = ButtonWidth(primary, weight);
            out.push_back({Primary, s == Screen::ImportDone ? Box(360 - w / 2, top, w, Layout::button) : Box(Layout::barRight - n.x - w, top, w, Layout::button)});
        }
        return out;
    }
    void Arrange() { Arrange(flow_.screen()); }
    void Arrange(Screen s) {
        items_.clear(); arranged_ = s;
        if (s == Screen::Closed || s == Screen::Outro) return;
        if (s == Screen::Profile) {
            items_.push_back({Name, NameField()});
            for (int i = 0; i < ProfileIcons::count; ++i) items_.push_back({IconBase + i, Cell(i)});
            items_.push_back({Hue, Lane(Hue)}); items_.push_back({Intensity, Lane(Intensity)});
        }
        if (s == Screen::ImportPick) {
            items_.push_back({Browser, BrowserSelect()});
            if (ProfileSelectShown()) items_.push_back({Profile, ProfileSelect()});
            const auto c = CheckBoxes(); items_.push_back({CheckBookmarks, c.bookmarks}); items_.push_back({CheckHistory, c.history});
        }
        for (const auto& b : ButtonBoxes(s)) items_.push_back(b);
        items_.push_back({Lang, LangBox(s)}); items_.push_back({Close, CloseBox(s)});
    }
    D2D1_RECT_F Rect(int id) const { for (const auto& i : items_) if (i.id == id) return i.r; return {}; }
    bool Enabled(int id) const {
        if (replaying_) return true;   // a replayed board is shown as it looks, whatever the flow's state
        if (flow_.screen() == Screen::ImportPick) {
            if (id == Primary) return flow_.CanImport();
            if (id == CheckBookmarks) return HasBookmarks();
            if (id == CheckHistory) return HasHistory();
            if (id == Browser) return !sources_.empty();
        }
        if (flow_.screen() == Screen::ImportRunning && id == Cancel) return !importAnswered_;
        return true;
    }
    int Hit(float x, float y) const {
        if (replaying_) return None;
        for (auto it = items_.rbegin(); it != items_.rend(); ++it) {
            if (!In(it->r, x, y)) continue;
            if (it->id >= IconBase && !In(Strip(), x, y)) continue;
            return Enabled(it->id) ? it->id : None;
        }
        return None;
    }
    std::vector<int> TabOrder() const {
        std::vector<int> o; const Screen s = flow_.screen();
        if (s == Screen::Profile) o = {Name, Icons, Hue, Intensity};
        if (s == Screen::ImportPick) { o.push_back(Browser); if (ProfileSelectShown()) o.push_back(Profile); o.push_back(CheckBookmarks); o.push_back(CheckHistory); }
        for (const auto& i : items_) if (i.id >= Primary && i.id <= Decline) o.push_back(i.id);
        if (s != Screen::Outro && s != Screen::Closed) { o.push_back(Lang); o.push_back(Close); }
        o.erase(std::remove_if(o.begin(), o.end(), [this](int id) { return !Enabled(id); }), o.end());
        return o;
    }

    // ---- time ----
    void RequestFrame() {
        if (!hwnd_ || offscreen_) return;
        if (clockOk_) clock_.Request(); else SetTimer(hwnd_, 1, 16, nullptr);
    }
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_); }
    bool ShaderOn(Screen s) const { return s == Screen::Welcome || s == Screen::Outro || s == Screen::Annotation || s == Screen::ImportRunning || s == Screen::ImportDone || s == Screen::ImportError; }
    bool ShadersLoop() const { return !offscreen_ && Onboarding::ShaderLoops(options_.reduced, gpu_.Animated(), Visible(), hwnd_ && IsIconic(hwnd_)) && (ShaderOn(shown_) || (changing_ && ShaderOn(from_))); }
    // Board 2 <-> board 3: the tunable timeline (src/onboarding-tuning.h). Reduced motion keeps the plain fade.
    bool Tuned() const { return changing_ && !options_.reduced; }
    const Onboarding::Tuning::Row& Tune() const { return tuning_[Onboarding::Tuning::Between(from_, shown_)]; }
    float Change(Motion::Time now) const {
        if (!changing_) return 1.f;
        return Motion::Clamp(Ms(changed_, now) / (Tuned() ? Onboarding::Tuning::TotalMs(Tune()) : static_cast<float>(Onboarding::TransitionMs(options_.reduced))));
    }
    float BarShown(Motion::Time now) const { return bar_.empty() ? 0.f : offscreen_ || options_.reduced ? 1.f : Motion::Clamp(Ms(barAt_, now) / 300.f); }
    float BarItemShown(std::size_t i, Motion::Time now) const { return offscreen_ || options_.reduced ? 1.f : Motion::Clamp((Ms(barAt_, now) - 120 - 40.f * i) / 220.f); }
    bool BarArriving() const { return !bar_.empty() && !offscreen_ && Ms(barAt_, Motion::Now()) < 400 + 40.f * bar_.size(); }
    double ShaderSeconds(Motion::Time now) const {
        if (secondsForTest_ >= 0) return secondsForTest_;
        if (offscreen_ || options_.reduced || !gpu_.Animated()) return 0;
        return std::chrono::duration<double>(now - opened_).count();
    }
    float Progress(Motion::Time now) const {
        if (progressForTest_ >= 0) return progressForTest_;
        const float run = .9f * (1 - std::exp(-Ms(importStarted_, importAnswered_ ? importAnsweredAt_ : now) / 1200.f));
        if (!importAnswered_) return run;
        return importAtAnswer_ + (1 - importAtAnswer_) * EaseOut(Ms(importAnsweredAt_, now) / 250.f);
    }
    // Something other than a shader loop is in motion (every tick is a new picture).
    bool Animating() const {
        if (changing_ || entering_ || leaving_ || replaying_ || exitRunning_) return true;
        if (std::fabs(iconScroll_ - iconScrollTarget_) > .01f) return true;
        if (flow_.screen() == Screen::ImportRunning || BarArriving()) return true;
        for (const auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f; if (std::fabs(g.second - want) > .001f) return true; }
        return false;
    }
    bool Moving() const { return Animating() || ShadersLoop() || flow_.screen() == Screen::Outro; }
    void Step() {
        const auto now = Motion::Now(); const float dt = lastStep_ == Motion::Time{} ? 16 : std::min(100.f, Ms(lastStep_, now)); lastStep_ = now;
        if (entering_) { const float u = Motion::Clamp(Ms(opened_, now) / 150.f); PopupKit::SetAlpha(hwnd_, u); if (u >= 1) entering_ = false; }
        if (leaving_) {
            const float u = Motion::Clamp(Ms(left_, now) / (flow_.ending() == Onboarding::Ending::Completed ? Onboarding::OutroFadeMs : 120.f));
            PopupKit::SetAlpha(hwnd_, 1 - u);
            if (u >= 1) { done_ = true; leaving_ = false; PostMessageW(hwnd_, WM_NULL, 0, 0); return; }
        }
        if (changing_ && Change(now) >= 1) {
            changing_ = false; from_ = shown_; Invalidate();
            if (flow_.screen() == Screen::Outro && shown_ == Screen::Outro && !exitReplay_) { exitAt_ = now; exitRunning_ = true; }   // board 7 is in place: the exit begins
        }
        if (replaying_ && !exitReplay_ && !changing_ && now >= replayUntil_) EndReplay();
        for (auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f, step = dt / 120.f; g.second = want > g.second ? std::min(want, g.second + step) : std::max(want, g.second - step); }
        if (std::fabs(iconScroll_ - iconScrollTarget_) > .01f) { iconScroll_ += (iconScrollTarget_ - iconScroll_) * std::min(1.f, dt / 60.f); if (std::fabs(iconScroll_ - iconScrollTarget_) < .3f) iconScroll_ = iconScrollTarget_; Arrange(); Invalidate(); }
        bool settling = false; for (const auto& g : glow_) { const float want = g.first == hover_ ? 1.f : 0.f; if (std::fabs(g.second - want) <= .001f && g.second != want) settling = true; }
        if (settling) Invalidate();
        if (flow_.screen() == Screen::ImportRunning && importAnswered_ && Ms(importAnsweredAt_, now) >= 250 && Ms(importStarted_, now) >= (options_.reduced ? 300 : 900)) ApplyImport();
        if (exitRunning_ && !leaving_ && !changing_) StepExit(now);
        if (Animating() || (ShadersLoop() && ShadeDue(now))) Invalidate();   // a tick with nothing new to show paints nothing
        if (Moving()) RequestFrame(); else if (!clockOk_) KillTimer(hwnd_, 1);
    }

    // ---- flow events ----
    void Changed(Screen before) {
        const Screen now = flow_.screen();
        if (now == before) { Arrange(); Invalidate(); return; }
        if (now == Screen::Closed) { Leave(); return; }
        from_ = shown_; shown_ = now; changed_ = Motion::Now(); changing_ = !offscreen_;
        focus_ = None; hover_ = None; pressed_ = None; dragging_ = None; ShowEdit(false);
        if (now == Screen::ImportPick && !sourcesLoaded_ && !sourcesLoading_) RequestSources();
        if (now == Screen::Outro) outroStart_ = changed_;
        if (now == Screen::Profile) RevealIcon(false);
        Arrange(); Invalidate(); RequestFrame();
        if (uia_.Attached()) uia_.ChildrenChanged(0);
    }
    // Board 7, once in place: a pause, the logo shrinks and fades inside the dot field (the dots go on), then the
    // window itself leaves on the stage (src/onboarding-handoff.h) while the browser comes in.
    void StepExit(Motion::Time now) {
        const auto plan = Onboarding::Tuning::ExitPlanFor(tuning_[Onboarding::Tuning::Exit], options_.reduced);
        const auto f = Onboarding::Tuning::ExitAt(plan, Ms(exitAt_, now));
        logoScale_ = f.logoScale; logoAlpha_ = f.logoAlpha; Invalidate();
        if (f.windowLeaving) { exitRunning_ = false; HandOff(plan); }
    }
    void HandOff(const Onboarding::Tuning::ExitPlan& plan) {
        const bool replay = exitReplay_; OnboardingHandoff::Stage* stage = replay ? &previewStage_ : options_.stage; bool started = false;
        if (stage && hwnd_ && !offscreen_) {
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); DwmFlush();
            auto shot = OnboardingHandoff::Capture(hwnd_, Layout::radius * Scale());
            if (shot.Valid()) {
                if (replay) { const auto standIn = OnboardingHandoff::Placeholder(hwnd_); started = stage->Start(std::move(shot), plan, &standIn, hwnd_, ReplayDoneMessage); }
                else started = stage->Start(std::move(shot), plan);
            }
        }
        if (replay) { if (started) PopupKit::SetAlpha(hwnd_, 0); else EndReplay(); return; }
        if (started) { handedOff_ = true; PopupKit::SetAlpha(hwnd_, 0); flow_.OutroDone(); done_ = true; PostMessageW(hwnd_, WM_NULL, 0, 0); }   // its picture is on the stage: the real window goes at once
        else { const Screen before = flow_.screen(); flow_.OutroDone(); Changed(before); }   // no stage: the plain fade
    }
    void EndReplay() {
        replaying_ = false; exitReplay_ = false; exitRunning_ = flow_.screen() == Screen::Outro; changing_ = false; logoScale_ = logoAlpha_ = 1;
        shown_ = from_ = flow_.screen() == Screen::Closed ? shown_ : flow_.screen(); Arrange(); if (hwnd_) PopupKit::SetAlpha(hwnd_, 1); Invalidate(); RequestFrame();
    }
    void Leave() { if (leaving_ || done_) return; ShowEdit(false); leaving_ = true; left_ = Motion::Now(); if (offscreen_ || !hwnd_) { done_ = true; return; } RequestFrame(); }
    void SaveProfile() {
        draft_.name = Profiles::TrimName(draft_.name);
        if (draft_.name.empty()) { nameError_ = Profiles::NameError::Empty; Invalidate(); return; }
        nameError_ = options_.hooks.saveProfile ? options_.hooks.saveProfile(draft_) : Profiles::NameError::None;
        if (nameError_ == Profiles::NameError::None) { layers_[Grain].Drop(); flow_.ProfileAccepted(); }
    }
    void BeginImport(bool retry) {
        SyncImport();
        const unsigned token = retry ? flow_.Retry() : flow_.BeginImport(); if (!token) return;
        importToken_ = token; importStarted_ = Motion::Now(); importAnswered_ = false; pending_ = {};
        if (!retry) bar_.clear();
        const bool started = options_.hooks.startImport && options_.hooks.startImport(token, pick_, flow_.askedBookmarks(), flow_.askedHistory());
        if (!started) { importToken_ = 0; flow_.CancelImport(); }
    }
    void ApplyImport() {
        const Screen before = flow_.screen(); errorDetail_ = pending_.detail; importAnswered_ = false;
        flow_.ImportFinished(importToken_, pending_.bookmarksSaved, pending_.historySaved); importToken_ = 0; Changed(before);
    }
    std::wstring ErrorTitle() const {
        if (flow_.failedBookmarks() && flow_.failedHistory()) return TR(onboarding_import_failed_both);
        return flow_.failedBookmarks() ? TR(onboarding_import_failed_bookmarks) : TR(onboarding_import_failed_history);
    }
    void SetLanguage(I18n::Locale l) {
        if (l == I18n::Current()) return;
        const bool defaultName = draft_.name == Profiles::DefaultName();
        I18n::SetForProcess(l);
        if (options_.hooks.language) options_.hooks.language(l);
        if (defaultName) { draft_.name = Profiles::DefaultName(); SyncEdit(); }
        if (hwnd_) SetWindowTextW(hwnd_, TR(onboarding_welcome_title));
        if (sourcesLoaded_ || sourcesLoading_) RequestSources();   // the labels follow the language
        Arrange(); Invalidate();
        if (uia_.Attached()) uia_.ChildrenChanged(0);
    }
    POINT Below(D2D1_RECT_F r) const { POINT p{Px(r.left), Px(r.bottom + 4)}; ClientToScreen(hwnd_, &p); return p; }
    int Menu(const std::vector<ContextMenu::Item>& items, D2D1_RECT_F anchor, bool keyboard) {
        if (!hwnd_ || offscreen_) return 0;
        menuOpen_ = true; Invalidate();
        const int chosen = menu_.Track(items, Below(anchor), nullptr, keyboard, options_.reduced, false, true);
        menuOpen_ = false; Invalidate(); return chosen;
    }
    void LanguageMenu(bool keyboard) {
        std::vector<ContextMenu::Item> items;
        for (I18n::Locale l : {I18n::Locale::Fr, I18n::Locale::En}) { ContextMenu::Item i; i.id = 1 + static_cast<int>(l); i.label = UiLanguage::Autonym(l); i.checked = l == I18n::Current(); items.push_back(i); }
        const int chosen = Menu(items, Rect(Lang), keyboard);
        if (chosen) SetLanguage(chosen == 2 ? I18n::Locale::En : I18n::Locale::Fr);
    }
    void BrowserMenu(bool keyboard) {
        std::vector<ContextMenu::Item> items;
        for (std::size_t n = 0; n < sources_.size(); ++n) { ContextMenu::Item i; i.id = static_cast<int>(n) + 1; i.label = NativeUI::EscapeMenuText(sources_[n].name); i.description = sources_[n].detail; i.enabled = sources_[n].available; i.checked = static_cast<int>(n) == pick_.browser; i.image = sources_[n].logo; if (!i.image) i.icon = SourceIcon(sources_[n]); items.push_back(i); }
        const int chosen = Menu(items, Rect(Browser), keyboard);
        if (chosen && chosen - 1 != pick_.browser) { pick_.browser = chosen - 1; pick_.profile = 0; SyncImport(); Arrange(); Invalidate(); }
    }
    void ProfileMenu(bool keyboard) {
        const auto* s = Source(); if (!s) return;
        std::vector<ContextMenu::Item> items;
        for (std::size_t n = 0; n < s->profiles.size(); ++n) { ContextMenu::Item i; i.id = static_cast<int>(n) + 1; i.label = NativeUI::EscapeMenuText(s->profiles[n].label); i.checked = static_cast<int>(n) == pick_.profile; items.push_back(i); }
        const int chosen = Menu(items, Rect(Profile), keyboard);
        if (chosen) { pick_.profile = chosen - 1; SyncImport(); Invalidate(); }
    }
    void SelectIcon(int slot) {
        slot = std::clamp(slot, 0, ProfileIcons::count - 1); draft_.icon = ProfileIcons::all[IconOrder()[static_cast<std::size_t>(slot)]].key;
        RevealIcon(true); Arrange(); Invalidate();
    }
    void SetSlider(int which, int value) {
        if (which == Hue) draft_.hue = ((value % 360) + 360) % 360; else K::SetIntensityPercent(draft_, value);
        Invalidate();
        if (uia_.Attached()) uia_.RangeChanged(which, 0, which == Hue ? draft_.hue : K::IntensityPercent(draft_));
    }
    void DragSlider(int which, float x) {
        const float u = Motion::Clamp((x - Sx(Layout::track) - Layout::thumbW / 2) / (Layout::trackW - Layout::thumbW));
        SetSlider(which, static_cast<int>(std::lround(u * (which == Hue ? 359 : 100))));
    }
    // How long the UI thread was held by the longest board change (for the log: a change must never block).
    struct ChangeTimer { Window& w; Motion::Time start = Motion::Now(); Screen before; ~ChangeTimer() { if (w.flow_.screen() != before) w.slowestChangeMs_ = std::max(w.slowestChangeMs_, Ms(start, Motion::Now())); } };
    void Activate(int id, bool keyboard) {
        if (!Enabled(id)) return;
        const Screen before = flow_.screen(); const ChangeTimer timer{*this, Motion::Now(), before};
        switch (id) {
        case Lang: LanguageMenu(keyboard); return;
        case Close: case SkipAll: flow_.Skip(); break;
        case Back: flow_.Back(); break;
        case SkipStep: flow_.SkipImport(); break;
        case Cancel: importToken_ = 0; flow_.CancelImport(); break;
        case Decline: flow_.AnswerAnnotation(false); break;
        case Browser: BrowserMenu(keyboard); return;
        case Profile: ProfileMenu(keyboard); return;
        case CheckBookmarks: pick_.bookmarks = !pick_.bookmarks; SyncImport(); Invalidate(); return;
        case CheckHistory: pick_.history = !pick_.history; SyncImport(); Invalidate(); return;
        case Name: FocusName(true); return;
        case Primary:
            if (before == Screen::Welcome) flow_.Start();
            else if (before == Screen::Profile) SaveProfile();
            else if (before == Screen::ImportPick) BeginImport(false);
            else if (before == Screen::ImportDone) flow_.Continue();
            else if (before == Screen::ImportError) BeginImport(true);
            else if (before == Screen::Annotation) flow_.AnswerAnnotation(true);
            break;
        default: return;
        }
        Changed(before);
    }
    void Focus(int id, bool keyboard) {
        keyboard_ = keyboard;
        if (id == Name) { FocusName(true); return; }
        ShowEdit(false); focus_ = id; if (hwnd_ && GetFocus() != hwnd_ && !offscreen_) SetFocus(hwnd_);
        if (id == Icons) RevealIcon(true);
        Invalidate();
    }
    void TabTo(bool back) {
        const auto order = TabOrder(); if (order.empty()) return;
        auto it = std::find(order.begin(), order.end(), focus_);
        int next;
        if (it == order.end()) next = back ? order.back() : order.front();
        else if (back) next = it == order.begin() ? order.back() : *(it - 1);
        else next = it + 1 == order.end() ? order.front() : *(it + 1);
        Focus(next, true);
    }
    bool Key(WPARAM key) {
        keyboard_ = true; const bool shift = GetKeyState(VK_SHIFT) < 0;
        if (key == VK_ESCAPE) { Activate(Close, true); return true; }
        if (key == VK_TAB) { TabTo(shift); return true; }
        if (flow_.screen() == Screen::Outro) return false;
        if (key == VK_RETURN) {
            if (focus_ == None || focus_ == Icons || focus_ == Hue || focus_ == Intensity || focus_ == Name) Activate(Primary, true); else Activate(focus_, true);
            return true;
        }
        if (key == VK_SPACE && focus_ != None && focus_ != Icons && focus_ != Hue && focus_ != Intensity) { Activate(focus_, true); return true; }
        if ((key == VK_DOWN || key == VK_F4) && (focus_ == Browser || focus_ == Profile || focus_ == Lang)) { Activate(focus_, true); return true; }
        if (focus_ == Icons) {
            int slot = IconSlot();
            switch (key) { case VK_LEFT: case VK_UP: --slot; break; case VK_RIGHT: case VK_DOWN: ++slot; break; case VK_HOME: slot = 0; break; case VK_END: slot = ProfileIcons::count - 1; break; default: return false; }
            SelectIcon(std::clamp(slot, 0, ProfileIcons::count - 1)); return true;
        }
        if (focus_ == Hue || focus_ == Intensity) {
            const bool hue = focus_ == Hue; const int v = hue ? draft_.hue : K::IntensityPercent(draft_), step = shift ? 10 : 1; int next = v;
            switch (key) {
            case VK_LEFT: case VK_DOWN: next = v - step; break; case VK_RIGHT: case VK_UP: next = v + step; break;
            case VK_PRIOR: next = v + 30; break; case VK_NEXT: next = v - 30; break;
            case VK_HOME: next = 0; break; case VK_END: next = hue ? 359 : 100; break;
            default: return false;
            }
            SetSlider(focus_, hue ? next : std::clamp(next, 0, 100)); return true;
        }
        return false;
    }

    // ---- the name field: a native EDIT while it has the focus, Direct2D text otherwise ----
    std::wstring EditText() const { if (!edit_) return draft_.name; const int n = GetWindowTextLengthW(edit_); std::wstring s(static_cast<std::size_t>(n) + 1, L'\0'); GetWindowTextW(edit_, s.data(), n + 1); s.resize(static_cast<std::size_t>(n)); return s; }
    void SyncEdit() { if (!edit_) return; syncingEdit_ = true; SetWindowTextW(edit_, draft_.name.c_str()); syncingEdit_ = false; }
    void PlaceEdit() {
        if (!edit_) return;
        if (editDpi_ != dpi_) {
            if (editFont_) DeleteObject(editFont_);
            editFont_ = CreateFontW(-Px(14), 0, 0, 0, FW_MEDIUM, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
            SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE); editDpi_ = dpi_;
        }
        const auto f = NameField(); const int x0 = Px(f.left + 10), y0 = Px(f.top + 6), x1 = Px(f.right - 10), y1 = Px(f.top + 26);
        SetWindowPos(edit_, HWND_TOP, x0, y0, x1 - x0, y1 - y0, SWP_NOACTIVATE);
    }
    void ShowEdit(bool show) {
        if (!edit_) return;
        if (show) { PlaceEdit(); ShowWindow(edit_, SW_SHOWNA); }
        else if (IsWindowVisible(edit_)) { const bool had = GetFocus() == edit_; ShowWindow(edit_, SW_HIDE); if (had && hwnd_) SetFocus(hwnd_); }
    }
    void FocusName(bool selectAll) {
        if (flow_.screen() != Screen::Profile) return;
        focus_ = Name; Invalidate(); if (!edit_) return;
        SyncEdit(); ShowEdit(true); SetFocus(edit_);
        if (selectAll) SendMessageW(edit_, EM_SETSEL, 0, -1);
        Invalidate();
    }
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Window*>(data);
        switch (msg) {
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DefSubclassProc(h, msg, w, l);
        case WM_KEYDOWN:
            if (w == VK_TAB || w == VK_RETURN || w == VK_ESCAPE) { self->Key(w); return 0; }
            break;
        case WM_CHAR:
            if (w == L'\r' || w == 27 || w == L'\t') return 0;
            // 32 user-perceived characters at most (a surrogate pair counts once), as everywhere a profile is named.
            if (w >= 0x20 && !IS_LOW_SURROGATE(static_cast<wchar_t>(w))) { DWORD a = 0, b = 0; SendMessageW(h, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b)); if (a == b && Profiles::NameLength(self->EditText()) >= Profiles::MaxNameLength) { MessageBeep(MB_OK); return 0; } }
            break;
        case WM_KILLFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); self->draft_.name = Profiles::TrimName(self->EditText()); ShowWindow(h, SW_HIDE); if (self->focus_ == Name) self->focus_ = None; self->Invalidate(); return r; }
        case WM_NCDESTROY: RemoveWindowSubclass(h, EditProc, 0); break;
        }
        return DefSubclassProc(h, msg, w, l);
    }

    // ---- shaders ----
    void EnsureGpu() {
        if (gpuTried_) return; gpuTried_ = true;
        namespace S = Onboarding::Shaders;
        if (!gpu_.Init()) return;
        const std::wstring dir = root_ + L"\\assets\\onboarding\\"; S::Image logo, gem, noise;
        if (S::LoadPng(c_->Imaging(), dir + L"halftone-logo.png", logo)) gpu_.SetInput(S::Kind::Halftone, logo);
        if (S::LoadPng(c_->Imaging(), dir + L"gem-shape.png", gem, true)) gpu_.SetInput(S::Kind::GemSmoke, gem);
        if (S::LoadPng(c_->Imaging(), dir + L"noise.png", noise)) gpu_.SetInput(S::Kind::GrainGradient, noise);
    }
    void DropLayers() { for (auto& l : layers_) l.Drop(); DropLogos(); DropTrame(); }
    // A source's logo at the size the board gives it (14 in the select, 42 next to the ticks).
    std::map<const FaviconImage*, ID2D1Bitmap*> logos_; ID2D1RenderTarget* logosOwner_ = nullptr;
    void DropLogos() { for (auto& l : logos_) if (l.second) l.second->Release(); logos_.clear(); logosOwner_ = nullptr; }
    bool DrawLogo(const std::shared_ptr<const FaviconImage>& image, D2D1_RECT_F box, float alpha) {
        auto* t = c_->RenderTarget(); if (!t || !image || !image->width || !image->height || image->pixels.size() < static_cast<std::size_t>(image->stride) * image->height) return false;
        if (logosOwner_ != t) { DropLogos(); logosOwner_ = t; }
        auto& bitmap = logos_[image.get()];
        if (!bitmap && FAILED(t->CreateBitmap(D2D1::SizeU(image->width, image->height), image->pixels.data(), image->stride, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &bitmap))) bitmap = nullptr;
        if (!bitmap) return false;
        K::DrawBitmap(t, bitmap, box, alpha); return true;
    }
    // Renders the layer for the target's pixel density; still frames are kept until the target or size changes.
    void Shade(LayerId id, ID2D1RenderTarget* t, Motion::Time now, bool loops) {
        namespace S = Onboarding::Shaders;
        Layer& layer = layers_[id]; EnsureGpu(); if (!gpu_.Ready()) return;
        const auto px = t->GetPixelSize(); const auto dip = t->GetSize(); const float scale = px.width / std::max(1.f, dip.width);
        float w = 721, h = 508; S::Kind kind = S::Kind::Halftone; S::Uniforms u;
        const double seconds = ShaderSeconds(now);
        switch (id) {
        // The dot field covers the whole window on both boards (the designer's request: no square of dots around
        // the logo). Board 1's logo stays where Paper has it: the centre of a 508-high node, 35.5 above the window's.
        case HalftoneWelcome: h = 579; u = S::Halftone(static_cast<float>(Onboarding::LoopPhase(seconds, Onboarding::HalftoneLoopSeconds)), true, WelcomeRaise); break;
        case HalftoneOutro: h = 579; u = S::Halftone(static_cast<float>(Onboarding::LoopPhase(seconds, Onboarding::HalftoneLoopSeconds)), true, 0, logoScale_, logoAlpha_); break;
        case Gem: w = Layout::gemW; h = Layout::gemH; kind = S::Kind::GemSmoke; u = S::GemSmoke(S::GemStillSeconds + static_cast<float>(std::fmod(seconds, S::GemPeriod))); break;
        default: {
            w = 725; h = 407; kind = S::Kind::GrainGradient; u = S::GrainGradient(S::GrainStillSeconds + static_cast<float>(std::fmod(seconds, S::GrainPeriod)));
            const unsigned paper[3] = {0x0AF4FF, 0x0099A0, 0x01292C};   // the board's colours, at the profile's hue
            for (int i = 0; i < 3; ++i) { const auto c = K::Rehue(paper[i], draft_); u.colors[i][0] = c.r / 255.f; u.colors[i][1] = c.g / 255.f; u.colors[i][2] = c.b / 255.f; }
            break;
        }
        }
        const unsigned ow = static_cast<unsigned>(std::lround(w * scale)), oh = static_cast<unsigned>(std::lround(h * scale));
        const bool current = layer.bitmap && layer.owner == t && layer.frame.width == ow && layer.frame.height == oh;
        const float key = id == HalftoneOutro ? logoScale_ * 16 + logoAlpha_ : 0;   // the logo's exit is a new picture even when nothing loops
        if (current && key == layer.key && (!loops || !Due(id, now))) return;
        layer.key = key;
        if (gpu_.Render(kind, u, w, h, ow, oh, layer.frame)) { layer.Upload(t); layer.shaded = now; } else if (!current) layer.Drop();   // the whole field slides: every pixel is new
    }
    // Loop pacing: frames come at the display's rate (src/frame-clock.h); a layer is drawn again on the first
    // tick after its own period. The halftone lattice travels about 1.3 px per second and the gradient's wave
    // takes half a minute: 30 pictures a second show both without a visible step; the small gem follows the display.
    static float PeriodMs(LayerId id) { return id == Gem ? 0.f : 1000.f / 30; }
    bool Due(LayerId id, Motion::Time now) const { const Layer& l = layers_[id]; return l.shaded == Motion::Time{} || Ms(l.shaded, now) >= PeriodMs(id) - 3; }
    bool ShadeDue(Motion::Time now) const {
        for (Screen s : {shown_, changing_ ? from_ : shown_}) {
            if (s == Screen::Welcome && Due(HalftoneWelcome, now)) return true;
            if (s == Screen::Outro && Due(HalftoneOutro, now)) return true;
            if (s == Screen::Annotation && Due(Grain, now)) return true;
            if ((s == Screen::ImportRunning || s == Screen::ImportDone || s == Screen::ImportError) && Due(Gem, now)) return true;
        }
        return false;
    }
    void DrawLayer(LayerId id, D2D1_RECT_F to, float alpha) {
        const Layer& l = layers_[id]; auto* t = c_->RenderTarget();
        if (l.bitmap && l.owner == t) t->DrawBitmap(l.bitmap, to, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    // ---- drawing ----
    struct Backdrop { int kind = 0; float x = 0, y = 0, w = 0, bookmarks = 0; };   // kind: 1 halftone (board 1), 2 halftone (board 7), 3 browser mock, 4 grain
    static Backdrop BackOf(Screen s) {
        switch (s) {
        case Screen::Welcome: return {1};
        case Screen::Outro: return {2};
        case Screen::Profile: return {3, 72, 99, 2521, 0};
        case Screen::ImportPick: return {3, 18, 48, 1383, 0};
        case Screen::ImportRunning: case Screen::ImportDone: case Screen::ImportError: return {3, 18, 48, 1383, 1};
        case Screen::Annotation: return {4};
        default: return {};
        }
    }
    struct Veil { float top, from, to, alpha; unsigned color; };   // the bottom gradient of each board
    static Veil VeilOf(Screen s) {
        switch (s) {
        case Screen::Welcome: return {312, .003f, .6982f, 1, K::Token::canvas};
        case Screen::Profile: return {303, .0029f, .4125f, 1, K::Token::canvas};
        case Screen::Annotation: return {249, .003f, .472f, 1, 0x000000};
        case Screen::Outro: return {312, .003f, .6982f, 0, K::Token::canvas};
        default: return {327, .0029f, .4125f, 1, K::Token::canvas};
        }
    }
    void DrawChip(D2D1_RECT_F plate, float alpha) {
        // The mock's profile button, redrawn with the draft: the chrome's own look (plate = ink 15 % on the canvas).
        const auto ink = K::Ink(draft_); const float size = plate.right - plate.left, k = size / 32;
        K::Fill(*c_, {plate.left - k, plate.top - k, plate.right + k, plate.bottom + k}, 0, K::Col(K::Token::canvas, alpha));
        K::Fill(*c_, plate, 8 * k, K::Col(ProfileColor::Over(ink, .153), alpha));
        const float g = 16 * k, gx = (plate.left + plate.right - g) / 2, gy = (plate.top + plate.bottom - g) / 2;
        glyphs_.Fill(*c_, ProfileIcons::Fill(draft_.icon.c_str()), Box(gx, gy, g, g), 256, K::Col(ink, alpha));
    }
    // The mock's tab title. In the Paper picture the bottom of the « y » of « By Val.S » is cut (the screenshot
    // was taken from a build whose tab titles were clipped to a short box); the title is drawn again as text, in
    // its full line box, ascender to descender, over the tab's own colour. `left`, `capTop`: where the picture's
    // title starts and where its capitals begin; `k`: DIP of the 1180-wide layout per unit drawn.
    std::wstring mockTitle_ = L"By Val.S";   // i18n-exempt: the sample page of the mock (a name)
    void DrawMockTitle(float left, float capTop, float k, float alpha) {
        const K::Font f{14 * k, 20 * k, 500, -.28f * k}; const float top = capTop - (K::Baseline({14, 20, 500, 0}) - .727f * 14) * k;   // Inter's capitals are 0.727 em tall
        const float width = std::min(K::Width(*c_, mockTitle_, f), 88 * k);   // the tab's title lane: cut on the right, never vertically
        K::Fill(*c_, {left - 2 * k, top - 1 * k, left + 90 * k, top + 21 * k}, 0, K::Col(K::Token::raised, alpha));
        K::Text(*c_, mockTitle_, {left, top, left + width + 1, top + 20 * k}, f, K::Col(0x949494, alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
    }
    void DrawMock(float x, float y, float w, float bookmarks, float alpha) {
        auto* t = c_->RenderTarget(); if (alpha <= 0) return;
        if (bookmarks < 1) {
            const float s = w / Layout::mockUnits;
            if (auto* b = c_->AssetBitmap(L"onboarding\\mock-plain.png")) K::DrawBitmap(t, b, Box(x, y, Layout::mockPlainUnits * s, Layout::mockPlainH * s), alpha);
            DrawChip({x + 7.75f * s, y + 7.25f * s, x + 39.5f * s, y + 39 * s}, alpha);
            DrawMockTitle(x + 78 * s, y + 17.75f * s, s, alpha);
        }
        if (bookmarks > 0) {
            const float s = w / Layout::mockWideUnits, a = alpha * bookmarks;
            if (auto* b = c_->AssetBitmap(L"onboarding\\mock-bookmarks.png")) K::DrawBitmap(t, b, Box(x, y, Layout::mockWideShown * s, Layout::mockWideH * s), a);
            DrawChip({x + 8.25f * s, y + 9 * s, x + 45.75f * s, y + 46.25f * s}, a);
            DrawMockTitle(x + 91 * s, y + 21 * s, s * Layout::mockWideUnits / Layout::mockUnits, a);
            DrawBar(x, y, s, a);
        }
    }
    // The mock's bookmarks bar, with what was really imported: the picture's sample items are covered and the
    // source's bar items are laid out as the browser's own bar does it (src/bookmarks-bar.h: padding 8, icon 16,
    // gap 6, folders with a 12 caret, 2 between items, an item never wider than 160, the title cut with « … »),
    // at the mock's scale (its layout is 1180 wide, shown 1383 wide). Links carry the globe: the import brings no
    // site icon. What does not fit is cut by the window's edge, where the real bar would put its « » » button.
    void DrawBar(float x, float y, float s, float alpha) {
        if (alpha <= 0) return;
        const auto now = Motion::Now(); const float k = s * Layout::mockWideUnits / Layout::mockUnits;
        K::Fill(*c_, {x + 2 * s, y + 100 * s, x + Layout::mockWideShown * s, y + 134 * s}, 0, K::Col(K::Token::canvas, alpha));
        const K::Font label{14 * k, 20 * k, 500, -.28f * k}; const float cy = y + 117.25f * s; float left = x + 19.5f * s - 8 * k;
        for (std::size_t i = 0; i < bar_.size() && left < Layout::width; ++i) {
            const BarItem& item = bar_[i]; const float a = alpha * BarItemShown(i, now);
            const float fixed = 8 * k + 16 * k + 6 * k + (item.folder ? 18 * k : 0) + 8 * k, title = std::min(K::Width(*c_, item.title, label), 160 * k - fixed);
            if (a > 0) {
                const D2D1_RECT_F icon = Box(left + 8 * k, cy - 8 * k, 16 * k, 16 * k);
                c_->Icon(item.folder ? L"folder-closed" : L"favicon", icon, 16 * k, .6f * a);
                K::Text(*c_, item.title, {left + 30 * k, cy - 10 * k, left + 30 * k + title + 1, cy + 10 * k}, label, K::Col(0x979797, a), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
                if (item.folder) glyphs_.Stroke(*c_, K::Path::chevron, Box(left + 30 * k + title + 6 * k, cy - 6 * k, 12 * k, 12 * k), 24, 2, K::Col(0x979797, .7f * a));
            }
            left += fixed + title + 2 * k;
        }
    }
    void DrawBack(const Backdrop& b, float alpha) {
        if (alpha <= 0) return;
        auto* t = c_->RenderTarget();
        if (b.kind == 1 || b.kind == 2) {
            const LayerId id = b.kind == 1 ? HalftoneWelcome : HalftoneOutro; const float centre = b.kind == 1 ? 255.f : 290.5f;
            if (layers_[id].bitmap && layers_[id].owner == t) DrawLayer(id, Box(-1, 1, 721, 579), alpha);
            else if (auto* logo = c_->AssetBitmap(L"onboarding\\halftone-logo.png")) { const float side = 721 * .46f; K::DrawBitmap(t, logo, Box(-1 + (721 - side) / 2, centre - side / 2, side, side), alpha); }
        } else if (b.kind == 3) DrawMock(b.x, b.y, b.w, b.bookmarks, alpha);
        else if (b.kind == 4) {
            const D2D1_RECT_F box = Box(-2, -2, 725, 407);
            K::Fill(*c_, box, 0, K::Col(K::Rehue(0xDEFDFF, draft_), alpha));
            if (layers_[Grain].bitmap && layers_[Grain].owner == t) DrawLayer(Grain, box, alpha);
            else K::Gradient(*c_, box, 0, {0, box.top}, {0, box.bottom}, {{.2f, K::Rehue(0x0AF4FF, draft_), 0}, {.6f, K::Rehue(0x0099A0, draft_), 1}, {1, K::Rehue(0x01292C, draft_), 1}}, alpha);
            if (trame_ && trameOwner_ == t) t->DrawBitmap(trame_, box, alpha, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);   // the band's dither over the gradient, under everything else
        }
    }
    static bool Placed(D2D1_RECT_F r) { return r.right > r.left && r.bottom > r.top; }   // a control without a box is never drawn
    void DrawButton(int id, const std::wstring& label, unsigned raisedOrAccent, bool accent, int weight, float alpha) {
        const auto r = Rect(id); if (!Placed(r)) return; const bool enabled = Enabled(id); const float a = alpha * (enabled ? 1.f : K::Token::disabled);
        const float glow = enabled ? glow_[id] : 0, press = enabled && pressed_ == id && hover_ == id ? 1.f : 0.f;
        if (accent) { const auto base = K::FromHex(raisedOrAccent); const auto c = Design::Accent::StateColor({base.r / 255.f, base.g / 255.f, base.b / 255.f}, glow, press); K::Fill(*c_, r, Layout::buttonRadius, D2D1::ColorF(c.r, c.g, c.b, a)); }
        else { const auto c = K::Lerp(K::Lerp(K::FromHex(K::Token::raised), K::FromHex(K::Token::hover), glow), K::FromHex(K::Token::pressed), press); K::Fill(*c_, r, Layout::buttonRadius, K::Col(c, a)); }
        K::Text(*c_, label, {r.left, r.top + 6, r.right, r.bottom}, {14, 20, weight, 0}, K::Col(K::Token::text, a), DWRITE_TEXT_ALIGNMENT_CENTER);
        if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, r, Layout::buttonRadius);
    }
    void DrawButtons(Screen s, float alpha) {
        const unsigned accent = s == Screen::Welcome ? K::Token::accent : 0;
        const auto fill = K::Accent(draft_); const unsigned profile = (static_cast<unsigned>(fill.r) << 16) | (static_cast<unsigned>(fill.g) << 8) | fill.b;
        for (const auto& b : LeftButtons(s)) DrawButton(b.id, b.label, K::Token::raised, false, ButtonWeight(s), alpha);
        const std::wstring primary = PrimaryLabel(s);
        if (!primary.empty()) DrawButton(Primary, primary, accent ? accent : profile, true, ButtonWeight(s), alpha);
    }
    void DrawHeader(Screen s, float tone, float alpha) {
        if (alpha <= 0) return;
        const auto color = K::Col(K::Lerp(K::FromHex(K::Token::secondary), K::FromHex(0x474747), tone), alpha);
        const auto lang = LangBox(s), close = CloseBox(s);
        for (int id : {Lang, Close}) {
            const auto r = id == Lang ? lang : close; const float lift = .08f * glow_[id] + (pressed_ == id && hover_ == id ? .04f : 0) + (id == Lang && menuOpen_ ? .08f : 0);
            if (lift > 0) K::Fill(*c_, r, 6, PopupKit::White(lift * alpha));
            if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, r, 6);
        }
        const std::wstring name = UiLanguage::Autonym(I18n::Current());
        K::Text(*c_, name, {lang.left + 6, lang.top + 4, lang.right, lang.bottom}, Label(), color);
        glyphs_.Stroke(*c_, K::Path::chevron, Box(lang.right - 6 - 16, lang.top + 6, 16, 16), 24, 1.8f, color);
        glyphs_.Stroke(*c_, K::Path::close, Box(close.left + 6, close.top + 6, 16, 16), 24, 1.8f, color);
    }
    void DrawTitle(const std::wstring& text, const char* icon, float side, float vx, float vy, float alpha) {
        const K::Font f{16, 16, 600, -.56f}; const float w = K::Width(*c_, text, f), total = 16 + 4 + w, left = Sx(Layout::titleCentre - total / 2), top = Layout::titleTop;
        // 1C7C-0 sits at top 16.4 + the 1 px border, moved by a transform: the engine keeps the fraction (the
        // glyph is antialiased across two rows) and the text lands one row lower than a box at 17 would put it.
        glyphs_.Fill(*c_, icon, Box(left, top, 16, 16), side, PopupKit::White(.5f * alpha), vx, vy);
        K::Text(*c_, text, {left + 20, std::ceil(top), left + 20 + w + 8, std::ceil(top) + 16}, f, K::Col(K::Token::text, alpha));
    }
    void DrawSelect(int id, const std::wstring& caption, const std::wstring& value, const BrowserSource* logo, float alpha) {
        const auto r = Rect(id); if (!Placed(r)) return;
        const float a = alpha * (Enabled(id) ? 1.f : K::Token::disabled);
        K::Text(*c_, caption, {r.left, r.top - 24, r.right + 40, r.top - 4}, Label(), K::Col(K::Token::secondary, alpha));
        K::Fill(*c_, r, 6, K::Col(K::Lerp(K::FromHex(K::Token::raised), K::FromHex(K::Token::hover), glow_[id]), a));
        float x = r.left + 10;
        if (logo) {
            const D2D1_RECT_F box = Box(x, r.top + 9, 14, 14);
            if (!DrawLogo(logo->logo, box, a)) c_->Icon(SourceIcon(*logo), box, 14, .7f * a);
            x += 14 + 6;
        }
        K::Text(*c_, value, {x, r.top + 6, r.right - 10 - 16 - 12, r.bottom}, Label(), K::Col(K::Token::text, a), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        glyphs_.Stroke(*c_, K::Path::chevron, Box(r.right - 10 - 16, r.top + 8, 16, 16), 24, 1.8f, K::Col(K::Token::secondary, a));
        if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, r, 6);
    }
    void DrawCheck(int id, const std::wstring& label, bool on, float alpha) {
        const auto r = Rect(id); if (!Placed(r)) return;
        const bool enabled = Enabled(id); const float a = alpha * (enabled ? 1.f : K::Token::disabled);
        const D2D1_RECT_F box = Box(r.left, r.top + 4, 16, 16);
        if (on && enabled) { K::Fill(*c_, box, 3, K::Col(K::Accent(draft_), a)); glyphs_.Stroke(*c_, K::Path::check, Box(box.left + 1, box.top + 1, 14, 14), 24, 1.8f, K::Col(K::Token::text, a)); }
        else K::Border(*c_, box, 3, 1.5f, PopupKit::White(.5f * a));
        K::Text(*c_, label, {box.right + 7, r.top + 2, r.right + 8, r.bottom}, Label(), K::Col(K::Token::text, a));
        if (focus_ == id && keyboard_) PopupKit::InsetRing(*c_, {r.left - 4, r.top, r.right + 4, r.bottom}, 6);
    }
    void DrawSlider(int which, float alpha) {
        auto* t = c_->RenderTarget(); auto* b = c_->SolidBrush(); const auto tr = Track(which); const float top = which == Hue ? Layout::hueRow : Layout::intensityRow;
        K::Text(*c_, which == Hue ? TR(profile_hue) : TR(profile_intensity), {Sx(Layout::laneLabel), top + 2, Sx(Layout::laneLabel) + 66, top + 22}, Label(), K::Col(K::Token::secondary, alpha));
        std::vector<K::Stop> stops;
        if (which == Hue) {
            // 1C6N-0: linear-gradient(in oklab 90deg, …) — the board's nine stops.
            static const unsigned hues[9] = {0xFF8FB4, 0xF8A052, 0xDAB33A, 0x6FD087, 0x0FD4BB, 0x61BFFF, 0xA59AFF, 0xE995E2, 0xFF8FB4};
            std::vector<std::pair<float, K::Rgb8>> s; for (int i = 0; i < 9; ++i) s.push_back({i / 8.f, K::FromHex(hues[i])});
            stops = K::OklabStops(s);
        } else stops = K::OklabStops({{0, K::FromHex(0xB7B7B7)}, {1, K::IntensityEnd(draft_)}}, 12);
        K::Gradient(*c_, tr, 5, {tr.left, 0}, {tr.right, 0}, stops, alpha);
        const auto th = Thumb(which);
        b->SetColor(K::Col(0x101010, alpha)); t->FillRoundedRectangle(D2D1::RoundedRect({th.left - 1, th.top - 1, th.right + 1, th.bottom + 1}, 3, 3), b);   // box-shadow 0 0 0 1px #101010
        K::Fill(*c_, th, 2, K::Col(K::Token::text, alpha));
        if (focus_ == which && keyboard_) { b->SetColor(PopupKit::White(.5f * alpha)); t->DrawRoundedRectangle(D2D1::RoundedRect({th.left - 3.5f, th.top - 3.5f, th.right + 3.5f, th.bottom + 3.5f}, 5, 5), b, 1.5f); }
        const std::wstring value = which == Hue ? std::to_wstring(draft_.hue) + L"°" : std::to_wstring(K::IntensityPercent(draft_)) + L" %";
        K::Text(*c_, value, {Sx(Layout::valueLeft) - 20, top + 2, Sx(Layout::valueLeft) + Layout::valueW, top + 22}, Label(), K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    void DrawProfile(float alpha) {
        auto* t = c_->RenderTarget();
        DrawTitle(TR(onboarding_profile_title), K::Path::user, 128, 4, 61.6f, alpha);
        // Name
        K::Text(*c_, TR(bookmark_field_name), {Sx(Layout::rowLeft), Layout::labelTop, Sx(Layout::rowLeft) + Layout::fieldW, Layout::labelTop + 20}, Label(), K::Col(K::Token::secondary, alpha));
        const auto field = NameField();
        K::Fill(*c_, field, 6, K::Col(K::Lerp(K::FromHex(K::Token::raised), K::FromHex(K::Token::hover), focus_ == Name ? 0 : glow_[Name]), alpha));
        if (focus_ == Name) K::Border(*c_, field, 6, 1, PopupKit::White((keyboard_ ? .5f : .2f) * alpha));
        if (nameError_ != Profiles::NameError::None) K::Border(*c_, field, 6, 1, K::Col(K::Token::danger, alpha));
        if (!edit_ || !IsWindowVisible(edit_)) K::Text(*c_, draft_.name, {field.left + 10, field.top + 6, field.right - 10, field.bottom}, Label(), K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        K::Fill(*c_, Box(Sx(Layout::rowLeft + 95), Layout::labelTop, 1, 56), 0, PopupKit::White(K::Token::focus * alpha));
        // Icons
        K::Text(*c_, TR(profile_icon), {Sx(Layout::stripLeft), Layout::labelTop, Sx(Layout::stripLeft) + Layout::stripW, Layout::labelTop + 20}, Label(), K::Col(K::Token::secondary, alpha));
        const auto strip = Strip(); const auto ink = K::Ink(draft_); const int selected = IconSlot();
        t->PushAxisAlignedClip(strip, D2D1_ANTIALIAS_MODE_ALIASED);
        for (int slot = 0; slot < ProfileIcons::count; ++slot) {
            const auto r = Cell(slot); if (r.right <= strip.left || r.left >= strip.right) continue;
            const bool on = slot == selected;
            if (on) { K::Fill(*c_, r, 8, K::Col(ProfileColor::Over(ink, .16), alpha)); K::Border(*c_, r, 8, 1, K::Col(ink, alpha)); }
            else if (glow_[IconBase + slot] > 0) K::Fill(*c_, r, 8, PopupKit::White(.06f * glow_[IconBase + slot] * alpha));
            glyphs_.Fill(*c_, ProfileIcons::all[IconOrder()[static_cast<std::size_t>(slot)]].fill, Box(r.left + 8, r.top + 8, 16, 16), 256, on ? K::Col(ink, alpha) : K::Col(K::Token::secondary, alpha));
            if (on && focus_ == Icons && keyboard_) K::Border(*c_, r, 8, 2, PopupKit::White(.5f * alpha));
        }
        // 1C6H-0: linear-gradient(in oklab 89.68deg, #17171700 19.92%, #171717 80.09%), 53 wide; mirrored once scrolled.
        const K::Rgb8 canvas = K::FromHex(K::Token::canvas);
        const float more = Motion::Clamp((IconScrollMax() - iconScroll_) / 12), less = Motion::Clamp(iconScroll_ / 12);
        if (more > 0) K::Gradient(*c_, {strip.right - Layout::stripFade, strip.top, strip.right, strip.bottom}, 0, {strip.right - Layout::stripFade, 0}, {strip.right, 0}, {{.1992f, canvas, 0}, {.8009f, canvas, 1}}, alpha * more);
        if (less > 0) K::Gradient(*c_, {strip.left, strip.top, strip.left + Layout::stripFade, strip.bottom}, 0, {strip.left + Layout::stripFade, 0}, {strip.left, 0}, {{.1992f, canvas, 0}, {.8009f, canvas, 1}}, alpha * less);
        t->PopAxisAlignedClip();
        K::Fill(*c_, Box(Sx(283.5f), Layout::labelTop, 1, 56), 0, PopupKit::White(K::Token::focus * alpha));
        DrawSlider(Hue, alpha); DrawSlider(Intensity, alpha);
        DrawButtons(Screen::Profile, alpha);
        if (nameError_ != Profiles::NameError::None) {
            const auto back = Rect(Back); const std::wstring why = nameError_ == Profiles::NameError::Duplicate ? TR(profile_name_duplicate) : nameError_ == Profiles::NameError::TooLong ? TR(profile_name_max) : TR(profile_name_empty);
            K::Text(*c_, why, {back.right + 12, back.top + 8, Rect(Primary).left - 12, back.bottom}, {12, 16, 400, 0}, K::Col(K::Token::danger, alpha), DWRITE_TEXT_ALIGNMENT_LEADING, false, true);
        }
    }
    void DrawImportPick(float alpha) {
        DrawTitle(TR(onboarding_import_title), K::Path::cloudDown, 128, 0, 0, alpha);
        const auto* s = Source();
        DrawSelect(Browser, TR(onboarding_import_browser), s ? s->name : std::wstring(sourcesLoaded_ ? TR(onboarding_import_none) : TR(onboarding_import_searching)), s, alpha);
        if (ProfileSelectShown()) { const auto* p = SourceProfile(); DrawSelect(Profile, TRF(onboarding_import_profile, s->name), p ? p->label : std::wstring(), nullptr, alpha); }
        DrawCheck(CheckBookmarks, TR(onboarding_import_bookmarks), pick_.bookmarks && HasBookmarks(), alpha);
        DrawCheck(CheckHistory, TR(import_item_history), pick_.history && HasHistory(), alpha);
        DrawButtons(Screen::ImportPick, alpha);
    }
    // Boards 4, 5 and 5.1: source logo, 19 ticks, GemSmoke logo; `state` 0 running, 1 done, 2 failed.
    void DrawImportFlow(int state, Motion::Time now, float alpha) {
        const auto* s = Source(); const float top = Layout::flowTop, flowLeft = Sx(Layout::flowLeft);
        const D2D1_RECT_F logo = Box(flowLeft, Sx(top + (Layout::flowH - Layout::logo) / 2), Layout::logo, Layout::logo);
        if (!s || !DrawLogo(s->logo, logo, alpha)) c_->Icon(s ? SourceIcon(*s) : L"favicon", logo, Layout::logo, .7f * alpha);
        std::vector<D2D1_RECT_F> bars; const float first = flowLeft + Layout::logoBox + 12, y = Sx(top + (Layout::flowH - Layout::tickH) / 2);
        for (int i = 0; i < Layout::ticks; ++i) bars.push_back(Box(first + i * Layout::tickPitch, y, Layout::tick, Layout::tickH));
        const auto ink = K::Ink(draft_);
        if (state == 0) {
            const int lit = Onboarding::ProgressBars(Progress(now), Layout::ticks);
            for (int i = 0; i < Layout::ticks; ++i) K::Fill(*c_, bars[static_cast<std::size_t>(i)], 1, i < lit ? K::Col(ink, alpha) : K::Col(0xDDDDDD, K::Token::focus * alpha));
        } else K::BlurredBars(*c_, bars, 1, state == 1 ? K::Col(ink, .2f * alpha) : K::Col(K::Token::danger, .2f * alpha), 4);
        const D2D1_RECT_F gem = Box(first + Layout::ticks * Layout::tickPitch, top, Layout::gemW, Layout::gemH);
        if (layers_[Gem].bitmap && layers_[Gem].owner == c_->RenderTarget()) DrawLayer(Gem, gem, alpha);
        const D2D1_RECT_F mark = Box(360 - 16, Sx(top + 12.5f), 32, 32);
        if (state == 1) glyphs_.Fill(*c_, K::Path::checkBold, mark, 256, K::Col(ink, alpha));
        if (state == 2) glyphs_.Fill(*c_, K::Path::warning, mark, 256, K::Col(K::Token::danger, alpha));
    }
    void DrawImportRunning(Motion::Time now, float alpha) {
        DrawTitle(TR(onboarding_import_title), K::Path::cloudDown, 128, 0, 0, alpha);
        K::Text(*c_, TR(onboarding_import_running), {1, 436, 719, 456}, Label(), K::Col(K::Token::secondary, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawImportFlow(0, now, alpha); DrawButtons(Screen::ImportRunning, alpha);
    }
    void DrawImportDone(Motion::Time now, float alpha) {
        DrawTitle(TR(onboarding_import_title), K::Path::cloudDown, 128, 0, 0, alpha);
        K::Text(*c_, TR(onboarding_import_done), {1, 436, 719, 456}, Label(), K::Col(K::Token::secondary, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        DrawImportFlow(1, now, alpha); DrawButtons(Screen::ImportDone, alpha);
    }
    void DrawImportError(Motion::Time now, float alpha) {
        DrawTitle(TR(onboarding_import_title), K::Path::cloudDown, 128, 0, 0, alpha);
        K::Text(*c_, ErrorTitle(), {1, 418, 719, 438}, Label(), K::Col(K::Token::danger, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        K::Text(*c_, errorDetail_, {32, 439, 688, 455}, {12, 16, 400, 0}, K::Col(K::Token::danger, alpha), DWRITE_TEXT_ALIGNMENT_CENTER, false, true);   // 1CEQ-0: measured one row above 440 on the board
        DrawImportFlow(2, now, alpha); DrawButtons(Screen::ImportError, alpha);
        // 1CES-0: canvas chip, radius 8, padding 6 / 8, gap 4, 4 after « Passer ».
        const auto skip = Rect(SkipStep); const K::Font small{12, 16, 400, 0}; const std::wstring later = TR(onboarding_import_later);
        const float w = 8 + 14 + 4 + K::Width(*c_, later, small) + 8; const D2D1_RECT_F chip = Box(skip.right + 4, skip.top + 2, w, 28);
        K::Fill(*c_, chip, 8, K::Col(K::Token::canvas, alpha));
        glyphs_.Fill(*c_, K::Path::info, Box(chip.left + 8, chip.top + 7, 14, 14), 112, PopupKit::White(.6f * alpha));
        K::Text(*c_, later, {chip.left + 26, chip.top + 6, chip.right + 8, chip.bottom}, small, PopupKit::White(.6f * alpha));
    }
    void DrawAnnotation(float alpha) {
        auto* t = c_->RenderTarget(); auto* b = c_->SolidBrush();
        const auto accent = K::Accent(draft_);
        K::Text(*c_, TR(onboarding_annotation_title), {0, 415, 720, 453}, {32, 38, 500, -1.12f}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        K::Text(*c_, TR(onboarding_annotation_line1), {0, 458, 720, 478}, {14, 20, 400, 0}, K::Col(K::Token::secondary, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        K::Text(*c_, TR(onboarding_annotation_line2), {0, 478, 720, 498}, {14, 20, 400, 0}, K::Col(K::Token::secondary, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        // 1CHL-0 at (81, 155): the picked button of a site, its selection frame and labels, the crosshair, the note.
        const float x = 81, y = 155;
        K::Fill(*c_, Box(x, y + 24, 240, 44), 8, K::Col(0x1C1C1C, alpha));
        K::Text(*c_, TR(onboarding_demo_pay), {x, y + 36, x + 240, y + 56}, {15, 20, 600, 0}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        const D2D1_RECT_F frame = Box(x - 4, y + 20, 248, 52);
        K::Fill(*c_, frame, 0, K::Col(0x3E67FF, (0x14 / 255.f) * alpha)); K::Border(*c_, frame, 0, 2, K::Col(accent, alpha));
        auto tag = [&](const std::wstring& text, float left, bool fromRight) {
            const K::Font f{13, 18, 400, 0}; const float w = K::Width(*c_, text, f) + 12, l = fromRight ? left - w : left;
            K::Fill(*c_, Box(l, y, w, 20), 3, K::Col(accent, alpha)); K::Fill(*c_, Box(l, y + 10, w, 10), 0, K::Col(accent, alpha));   // radius 3 on the top corners only
            K::Text(*c_, text, {l + 6, y + 1, l + w + 8, y + 19}, f, PopupKit::White(.9f * alpha));
        };
        tag(L"button.btn-primary", x - 4, false);   // i18n-exempt: a CSS selector in the illustration
        tag(L"240x64", x + 240 + 4, true);          // i18n-exempt: a size in the illustration
        const D2D1_RECT_F cross = Box(x + 196, y + 30, 32, 32);
        glyphs_.Stroke(*c_, K::Path::cross, cross, 32, 4, K::Col(K::Token::text, alpha)); glyphs_.Stroke(*c_, K::Path::cross, cross, 32, 1.6f, K::Col(0x1C1C1C, alpha));
        b->SetColor(K::Col(accent, alpha)); t->FillEllipse(D2D1::Ellipse({cross.left + 16, cross.top + 16}, 2.6f, 2.6f), b);
        b->SetColor(K::Col(K::Token::text, alpha)); t->DrawEllipse(D2D1::Ellipse({cross.left + 16, cross.top + 16}, 2.6f, 2.6f), b, 1.2f);
        const float nx = x + 252, ny = y + 20;
        K::Fill(*c_, Box(nx, ny, 305, 104), 16, K::Col(K::Token::canvas, alpha));
        K::Text(*c_, TR(onboarding_demo_note), {nx + 12, ny + 12, nx + 293, ny + 52}, {14, 20, 400, 0}, PopupKit::White(.9f * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, true);
        const float fy = ny + 64;   // the footer row, 28 high
        glyphs_.Fill(*c_, K::Path::plus, Box(nx + 12 + 5, fy + 2 + 5, 14, 14), 256, PopupKit::White(.5f * alpha));
        glyphs_.Fill(*c_, K::Path::shield, Box(nx + 12 + 24 + 6, fy + 2 + 5.5f, 13, 13), 256, PopupKit::White(.45f * alpha));
        const K::Font model{12, 16, 500, 0}, effort{12, 16, 400, 0}; const std::wstring name = L"Opus 5.5", level = TR(onboarding_demo_effort);   // i18n-exempt: a model name in the illustration
        const float wn = K::Width(*c_, name, model), wl = K::Width(*c_, level, effort), chipW = 6 + 14 + 4 + wn + 4 + wl + 6;
        const float send = nx + 305 - 12 - 28, chip = send - 4 - chipW;
        glyphs_.Fill(*c_, K::Path::claude, Box(chip + 6, fy + 2 + 5, 14, 14), 18.666f, K::Col(0xD97757, alpha), .667f, 1.6f);
        K::Text(*c_, name, {chip + 24, fy + 5, chip + 24 + wn + 8, fy + 21}, model, PopupKit::White(.9f * alpha));       // 1CIF-0 / 1CIH-0: measured one row above the 12/16 rule
        K::Text(*c_, level, {chip + 28 + wn, fy + 5, chip + 28 + wn + wl + 8, fy + 21}, effort, PopupKit::White(.5f * alpha));
        K::Fill(*c_, Box(send, fy, 28, 28), 8, K::Col(accent, alpha));
        glyphs_.Fill(*c_, K::Path::arrowUp, Box(send + 7, fy + 7, 14, 14), 256, K::Col(K::Token::text, alpha));
        DrawButtons(Screen::Annotation, alpha);
    }
    void DrawContent(Screen s, Motion::Time now, float alpha) {
        if (alpha <= 0) return;
        switch (s) {
        case Screen::Welcome:
            K::Text(*c_, TR(onboarding_welcome_title), {1, 426, 719, 464}, {32, 38, 500, -1.12f}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
            K::Text(*c_, TR(onboarding_welcome_body), {1, 469, 719, 489}, {14, 20, 400, 0}, K::Col(K::Token::secondary, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
            DrawButtons(s, alpha); break;
        case Screen::Profile: DrawProfile(alpha); break;
        case Screen::ImportPick: DrawImportPick(alpha); break;
        case Screen::ImportRunning: DrawImportRunning(now, alpha); break;
        case Screen::ImportDone: DrawImportDone(now, alpha); break;
        case Screen::ImportError: DrawImportError(now, alpha); break;
        case Screen::Annotation: DrawAnnotation(alpha); break;
        default: break;
        }
    }
    // Text weight: the boards were drawn by a web engine, whose light-on-dark text reads heavier than
    // DirectWrite's default grayscale rendering (ink mass about 0.8 of the board's). These parameters bring the
    // same glyphs to the board's weight (measured by tests/onboarding/compare.py).
    void TextLook(ID2D1RenderTarget* t) {
        IDWriteRenderingParams* params = nullptr; float gamma = TextGamma, contrast = TextContrast;
        if (const wchar_t* g = _wgetenv(L"ONBOARDING_TEXT_GAMMA")) gamma = static_cast<float>(_wtof(g));
        if (const wchar_t* k = _wgetenv(L"ONBOARDING_TEXT_CONTRAST")) contrast = static_cast<float>(_wtof(k));
        if (c_->WriteFactory() && SUCCEEDED(c_->WriteFactory()->CreateCustomRenderingParams(gamma, contrast, 0, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &params)) && params) { t->SetTextRenderingParams(params); params->Release(); }
        t->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }
    void Draw(Motion::Time now) {
        auto* t = c_->RenderTarget(); if (!t) return;
        TextLook(t);
        const Screen to = shown_, from = changing_ ? from_ : shown_; const float raw = Change(now); float u = EaseOut(raw);
        const bool tuned = Tuned(); Onboarding::Tuning::Frame tf;
        if (tuned) { tf = Onboarding::Tuning::At(Tune(), Ms(changed_, now)); u = tf.mock; }
        // Shader layers of the boards on screen.
        const bool loops = ShadersLoop();
        for (Screen s : {from, to}) {
            if (s == Screen::Welcome) Shade(HalftoneWelcome, t, now, loops);
            if (s == Screen::Outro) Shade(HalftoneOutro, t, now, loops);
            if (s == Screen::Annotation) { Shade(Grain, t, now, loops); ShadeTrame(t, now); }
            if (s == Screen::ImportRunning || s == Screen::ImportDone || s == Screen::ImportError) Shade(Gem, t, now, loops);
        }
        const float toneFrom = Annotation(from) ? 1.f : 0.f, toneTo = Annotation(to) ? 1.f : 0.f, tone = toneFrom + (toneTo - toneFrom) * u;
        t->Clear(K::Col(K::Lerp(K::FromHex(K::Token::canvas), K::FromHex(0x000000), tone)));
        Backdrop a = BackOf(from), b = BackOf(to);
        // The bookmarks bar only exists once bookmarks were imported: until then the mock has none.
        for (Backdrop* k : {&a, &b}) if (k->kind == 3 && k->bookmarks > 0) k->bookmarks = BarShown(now);
        if (a.kind == 3 && b.kind == 3) {
            // One mock: it travels between its two framings and gains the bookmarks bar.
            const float m = Onboarding::Moves(options_.reduced) ? u : (raw < .4f ? 0.f : 1.f);
            DrawMock(a.x + (b.x - a.x) * m, a.y + (b.y - a.y) * m, a.w + (b.w - a.w) * m, a.bookmarks + (b.bookmarks - a.bookmarks) * (from == to ? 1.f : u), 1 - tf.dim);
        } else if (from == to) DrawBack(b, 1);
        else { DrawBack(a, 1 - u); DrawBack(b, u); }
        const Veil va = VeilOf(from), vb = VeilOf(to);
        const float top = va.top + (vb.top - va.top) * u, alpha = va.alpha + (vb.alpha - va.alpha) * u;
        if (alpha > 0) {
            const K::Rgb8 color = K::Lerp(K::FromHex(va.color), K::FromHex(vb.color), u);
            K::Gradient(*c_, {-2, top, 727, 579}, 0, {0, top}, {0, 579}, {{va.from + (vb.from - va.from) * u, color, 0}, {va.to + (vb.to - va.to) * u, color, 1}}, alpha);
            if (tone > 0) K::Fill(*c_, {0, 578.5f, 720, 580}, 0, K::Col(color, tone));
        }
        const bool swapped = !changing_ || (tuned ? tf.swapped : raw >= .4f); const Screen content = swapped ? to : from;
        const float contentAlpha = !changing_ ? 1.f : tuned ? (swapped ? tf.newAlpha : tf.oldAlpha) : raw < .4f ? 1 - raw / .4f : (raw - .4f) / .6f;
        const float headerAlpha = (from == Screen::Outro ? 0.f : 1.f) + ((to == Screen::Outro ? 0.f : 1.f) - (from == Screen::Outro ? 0.f : 1.f)) * u;
        // The board being drawn gets ITS boxes: during a change the one that leaves is not the flow's any more, and
        // a control looked up in another board's boxes would be drawn at the window's corner. Input keeps the
        // flow's board: its boxes are put back right after.
        const Screen live = arranged_;
        if (content != arranged_) Arrange(content);
        DrawHeader(content == Screen::Outro ? from : content, tone, headerAlpha);
        if (tuned && tf.rise > 0) {   // the new content climbs into place; the header stays put
            D2D1_MATRIX_3X2_F base; t->GetTransform(&base); t->SetTransform(D2D1::Matrix3x2F::Translation(0, tf.rise) * base);
            DrawContent(content, now, contentAlpha); t->SetTransform(base);
        } else DrawContent(content, now, contentAlpha);
        if (arranged_ != live) Arrange(live);
        const float hairline = 1 - tone;
        if (hairline > 0) PopupKit::Outline(*c_, Layout::width, Layout::height, Layout::radius, PopupKit::White((0x1A / 255.f) * hairline));
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t) return;
        const auto began = Motion::Now();
        t->BeginDraw(); Draw(began);
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) { DropLayers(); chrome_.RecreateTarget(); }
        if (++frames_ > 1) slowestFrameMs_ = std::max(slowestFrameMs_, Ms(began, Motion::Now()));   // the first frame builds the shaders: before the window shows
    }

    // ---- UI Automation ----
    void AttachUia() {
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; RECT win{}; GetWindowRect(hwnd_, &win); const Screen s = flow_.screen();
            auto bounds = [&](D2D1_RECT_F r) { return RECT{win.left + Px(r.left), win.top + Px(r.top), win.left + Px(r.right), win.top + Px(r.bottom)}; };
            auto text = [&](long long key, const std::wstring& value) { Accessibility::Node n; n.key = key; n.parent = 500; n.role = Accessibility::Role::Text; n.name = value; n.focusable = false; nodes.push_back(n); };
            auto button = [&](int id, const std::wstring& name, const wchar_t* automation) { Accessibility::Node n; n.key = id; n.parent = 500; n.role = Accessibility::Role::Button; n.name = name; n.automationId = automation; n.bounds = bounds(Rect(id)); n.enabled = Enabled(id); nodes.push_back(n); return nodes.size() - 1; };
            Accessibility::Node g; g.key = 500; g.role = Accessibility::Role::Group; g.focusable = false; g.bounds = win; g.automationId = L"Onboarding";
            switch (s) {
            case Screen::Profile: g.name = TR(onboarding_profile_title); break;
            case Screen::ImportPick: case Screen::ImportRunning: case Screen::ImportDone: case Screen::ImportError: g.name = TR(onboarding_import_title); break;
            case Screen::Annotation: g.name = TR(onboarding_annotation_title); g.help = std::wstring(TR(onboarding_annotation_line1)) + L" " + TR(onboarding_annotation_line2); break;
            default: g.name = TR(onboarding_welcome_title); g.help = TR(onboarding_welcome_body); break;
            }
            nodes.push_back(g);
            if (s == Screen::Outro || s == Screen::Closed) return nodes;
            if (s == Screen::Profile) {
                Accessibility::Node name; name.key = Name; name.parent = 500; name.role = Accessibility::Role::Edit; name.name = TR(bookmark_field_name); name.value = draft_.name; name.automationId = L"OnboardingName"; name.bounds = bounds(NameField());
                if (nameError_ != Profiles::NameError::None) name.help = nameError_ == Profiles::NameError::Duplicate ? TR(profile_name_duplicate) : TR(profile_name_empty);
                nodes.push_back(name);
                Accessibility::Node list; list.key = Icons; list.parent = 500; list.role = Accessibility::Role::List; list.name = TR(profile_icon); list.automationId = L"OnboardingIcons"; list.focusable = false; list.bounds = bounds(Strip()); nodes.push_back(list);
                const int selected = IconSlot(); const auto strip = Strip();
                for (int slot = 0; slot < ProfileIcons::count; ++slot) {
                    Accessibility::Node n; n.key = IconBase + slot; n.parent = Icons; n.role = Accessibility::Role::ListItem; n.name = I18n::T(ProfileIcons::all[IconOrder()[static_cast<std::size_t>(slot)]].label.id);
                    n.selected = slot == selected; n.position = slot + 1; n.count = ProfileIcons::count; n.automationId = L"OnboardingIcon"; const auto r = Cell(slot); n.bounds = bounds(r); n.offscreen = r.right <= strip.left || r.left >= strip.right; nodes.push_back(n);
                }
                for (int which : {Hue, Intensity}) {
                    Accessibility::Node n; n.key = which; n.parent = 500; n.role = Accessibility::Role::Slider; n.automationId = which == Hue ? L"OnboardingHue" : L"OnboardingIntensity"; n.name = which == Hue ? TR(profile_hue) : TR(profile_intensity);
                    n.rangeMin = 0; n.rangeMax = which == Hue ? 359 : 100; n.smallChange = 1; n.largeChange = 10; n.rangeValue = which == Hue ? draft_.hue : K::IntensityPercent(draft_);
                    n.value = which == Hue ? std::to_wstring(draft_.hue) + L"°, " + ProfileColor::HueName(draft_.hue) : I18n::Percent(K::IntensityPercent(draft_)); n.bounds = bounds(Lane(which)); nodes.push_back(n);
                }
            }
            if (s == Screen::ImportPick) {
                const auto* src = Source();
                const auto b = button(Browser, TR(onboarding_import_browser), L"OnboardingBrowser"); nodes[b].value = src ? src->name : std::wstring(TR(onboarding_import_none)); nodes[b].expand = 1;
                if (ProfileSelectShown()) { const auto p = button(Profile, TRF(onboarding_import_profile, src->name), L"OnboardingSourceProfile"); nodes[p].value = SourceProfile() ? SourceProfile()->label : std::wstring(); nodes[p].expand = 1; }
                const auto cb = button(CheckBookmarks, TR(onboarding_import_bookmarks), L"OnboardingBookmarks"); nodes[cb].toggle = pick_.bookmarks && HasBookmarks() ? 2 : 1;
                const auto ch = button(CheckHistory, TR(import_item_history), L"OnboardingHistory"); nodes[ch].toggle = pick_.history && HasHistory() ? 2 : 1;
            }
            if (s == Screen::ImportRunning) text(600, TR(onboarding_import_running));
            if (s == Screen::ImportDone) text(600, TR(onboarding_import_done));
            if (s == Screen::ImportError) { text(600, ErrorTitle()); text(601, errorDetail_); text(602, TR(onboarding_import_later)); }
            static const wchar_t* ids[] = {L"OnboardingPrimary", L"OnboardingBack", L"OnboardingSkip", L"OnboardingSkipStep", L"OnboardingCancel", L"OnboardingDecline"};
            for (const auto& b : const_cast<Window*>(this)->LeftButtons(s)) button(b.id, b.label, ids[b.id - Primary]);
            const std::wstring primary = const_cast<Window*>(this)->PrimaryLabel(s);
            if (!primary.empty()) button(Primary, primary, ids[0]);
            const auto lang = button(Lang, TR(language_label), L"OnboardingLanguage"); nodes[lang].value = UiLanguage::Autonym(I18n::Current()); nodes[lang].expand = 1;
            button(Close, TR(common_close), L"OnboardingClose");
            return nodes;
        };
        host.focused = [this]() -> long long { return focus_ == Icons ? IconBase + IconSlot() : focus_; };
        host.invoke = [this](long long key) { if (key >= IconBase) SelectIcon(static_cast<int>(key - IconBase)); else Activate(static_cast<int>(key), true); };
        host.toggle = [this](long long key) { Activate(static_cast<int>(key), true); };
        host.expand = [this](long long key, bool open) { if (open) Activate(static_cast<int>(key), true); };
        host.select = [this](long long key) { if (key >= IconBase) SelectIcon(static_cast<int>(key - IconBase)); };
        host.setRange = [this](long long key, double v) { if (key == Hue || key == Intensity) SetSlider(static_cast<int>(key), static_cast<int>(std::lround(v))); };
        host.setValue = [this](long long key, const std::wstring& v) { if (key == Name) { draft_.name = v; SyncEdit(); nameError_ = Profiles::NameError::None; Invalidate(); } };
        host.focus = [this](long long key) { Focus(key >= IconBase ? static_cast<int>(Icons) : static_cast<int>(key), true); };
        uia_.Attach(std::move(host));
    }

    // ---- window ----
    void Resize(bool center) {
        const int w = Px(Layout::width), h = Px(Layout::height);
        if (center) {
            POINT cursor{}; GetCursorPos(&cursor); MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi);
            SetWindowPos(hwnd_, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2, mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        } else SetWindowPos(hwnd_, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius); chrome_.Resize(); DropLayers();
        if (edit_ && IsWindowVisible(edit_)) PlaceEdit();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self && self->hwnd_ == h ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = Scale();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); NativeUI::EditBuffer::Flush(edit_); return 0; }
        case FrameMessage: if (l == FrameClock::ClockLost) { clock_.Stop(); clockOk_ = false; SetTimer(h, 1, 16, nullptr); return 0; } clock_.Acknowledge(); Step(); return 0;
        case WM_TIMER: if (w == 1) Step(); return 0;
        case HostMessage: if (options_.hooks.message) options_.hooks.message(w, l); return 0;
        case ReplayDoneMessage: if (exitReplay_) EndReplay(); return 0;
        case SourcesMessage: {
            std::unique_ptr<std::vector<BrowserSource>> found(reinterpret_cast<std::vector<BrowserSource>*>(l));
            if (found && static_cast<unsigned>(w) == sourcesAsked_) { AdoptSources(std::move(*found)); Arrange(); Invalidate(); if (uia_.Attached()) uia_.ChildrenChanged(0); }
            return 0;
        }
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(l) == edit_) { const HDC dc = reinterpret_cast<HDC>(w); NativeUI::EditBuffer::Clip(edit_, dc); SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, RGB(0x29, 0x29, 0x29)); return reinterpret_cast<LRESULT>(editBrush_); }
            break;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(l) == edit_ && HIWORD(w) == EN_CHANGE && !syncingEdit_) { draft_.name = EditText(); nameError_ = Profiles::NameError::None; Invalidate(); }
            return 0;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p); const float x = p.x / s, y = p.y / s;
            if (y < 48 && Hit(x, y) == None && flow_.screen() != Screen::Closed) return HTCAPTION;   // the header moves the window
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tr{sizeof(tr), TME_LEAVE, h, 0}; TrackMouseEvent(&tr);
            const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s;
            if (dragging_) { DragSlider(dragging_, x); return 0; }
            const int id = Hit(x, y);
            if (id != hover_) { hover_ = id; if (id) glow_[id]; Invalidate(); RequestFrame(); }
            return 0;
        }
        case WM_MOUSELEAVE: if (hover_ && !dragging_) { hover_ = None; Invalidate(); RequestFrame(); } return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
            const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s; const int id = Hit(x, y); keyboard_ = false;
            if (id == Name) { FocusName(false); POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; MapWindowPoints(h, edit_, &p, 1); SendMessageW(edit_, WM_LBUTTONDOWN, w, MAKELPARAM(std::max<LONG>(0, p.x), std::max<LONG>(0, p.y))); return 0; }
            ShowEdit(false); SetFocus(h); SetCapture(h);
            if (id == Hue || id == Intensity) { focus_ = id; dragging_ = id; DragSlider(id, x); return 0; }
            if (id >= IconBase) { focus_ = Icons; SelectIcon(id - IconBase); return 0; }
            pressed_ = id; focus_ = id; Invalidate(); return 0;
        }
        case WM_LBUTTONUP: {
            // What was pressed is read first: releasing the capture sends WM_CAPTURECHANGED, which forgets it.
            const int was = pressed_, slider = dragging_; pressed_ = None; dragging_ = None;
            if (GetCapture() == h) ReleaseCapture();
            const float x = GET_X_LPARAM(l) / s, y = GET_Y_LPARAM(l) / s; Invalidate();
            if (slider) return 0;
            const int id = Hit(x, y);
            if (id && id == was) Activate(id, false);
            return 0;
        }
        case WM_CAPTURECHANGED: dragging_ = None; pressed_ = None; Invalidate(); return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ScreenToClient(h, &p);
            if (flow_.screen() == Screen::Profile && In({Strip().left, Layout::labelTop, Strip().right, Strip().bottom}, p.x / s, p.y / s)) {
                iconScrollTarget_ = std::clamp(iconScrollTarget_ - GET_WHEEL_DELTA_WPARAM(w) / static_cast<float>(WHEEL_DELTA) * Layout::cellPitch, 0.f, IconScrollMax());
                if (options_.reduced) { iconScroll_ = iconScrollTarget_; Arrange(); Invalidate(); } else RequestFrame();
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT && hover_ == Name && reinterpret_cast<HWND>(w) == h) { SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE; }   // the name is a text field
            break;
        case WM_KEYDOWN: Key(w); return 0;
        case WM_SYSKEYDOWN: if (w == VK_F4) { Activate(Close, true); return 0; } break;
        case WM_SIZE: if (w != SIZE_MINIMIZED) { lastStep_ = {}; RequestFrame(); } return 0;
        case WM_SHOWWINDOW: if (w) RequestFrame(); break;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(w); const RECT* r = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, Px(Layout::width), Px(Layout::height), SWP_NOZORDER | SWP_NOACTIVATE);
            Resize(false); Arrange(); Invalidate(); return 0;
        }
        case WM_CLOSE: Activate(Close, false); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    Window() = default; Window(const Window&) = delete; Window& operator=(const Window&) = delete;
    HWND Handle() const { return hwnd_; }
    const Onboarding::Flow& Flow() const { return flow_; }
    const Profiles::Profile& Draft() const { return draft_; }
    // The entry of board 3's « Navigateur » list that is chosen, as the host handed it (null: none).
    const BrowserSource* PickedSource() const { return Source(); }
    float SlowestChangeMs() const { return slowestChangeMs_; }
    float SlowestFrameMs() const { return slowestFrameMs_; }
    // The import the window is waiting for (0: none). The host writes nothing for any other token.
    unsigned ImportToken() const { return flow_.screen() == Screen::ImportRunning && !importAnswered_ ? importToken_ : 0; }
    // The host's answer to Hooks::startImport (UI thread). Results of a cancelled or older import are ignored.
    void ImportFinished(unsigned token, const ImportOutcome& outcome) {
        if (!token || token != importToken_ || flow_.screen() != Screen::ImportRunning || importAnswered_) return;
        pending_ = outcome; importAtAnswer_ = Progress(Motion::Now()); importAnswered_ = true; importAnsweredAt_ = Motion::Now();
        if (outcome.bookmarksSaved) { bar_ = outcome.bar; barAt_ = importAnsweredAt_; }   // the bar fills as the bookmarks land
        if (offscreen_) ApplyImport(); else RequestFrame();
    }
    // Shows the window and runs until the flow ends. `root`: the folder that holds assets\.
    Result Run(HINSTANCE instance, const std::wstring& root, Options options) {
        instance_ = instance; root_ = root; options_ = std::move(options); draft_ = options_.seed;
        flow_ = Onboarding::Flow(Onboarding::Flow::Config{options_.annotation});
        Result result;
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereOnboarding"; wc.style = CS_DBLCLKS;
        wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
        wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        RegisterClassExW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED, wc.lpszClassName, TR(onboarding_welcome_title), WS_POPUP | WS_CLIPCHILDREN | WS_MINIMIZEBOX | WS_SYSMENU, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!hwnd_) return result;
        if (options_.hooks.created) options_.hooks.created(hwnd_);
        NativeUI::ConfigureFullClientFrame(hwnd_);
        const int noBackdrop = 1; DwmSetWindowAttribute(hwnd_, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &noBackdrop, sizeof(noBackdrop));
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        { POINT cursor{}; GetCursorPos(&cursor); MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &mi); SetWindowPos(hwnd_, nullptr, mi.rcWork.left, mi.rcWork.top, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE); }
        dpi_ = GetDpiForWindow(hwnd_); if (!dpi_) dpi_ = 96;
        if (FAILED(chrome_.Init(hwnd_, root_))) { DestroyWindow(hwnd_); hwnd_ = nullptr; return result; }
        Resize(true); dpi_ = GetDpiForWindow(hwnd_); Resize(true);
        edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_, nullptr, instance, nullptr);
        if (edit_) { NativeUI::SelectAllOnCtrlA(edit_); SetWindowSubclass(edit_, EditProc, 0, reinterpret_cast<DWORD_PTR>(this)); SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0); SendMessageW(edit_, EM_LIMITTEXT, 2 * Profiles::MaxNameLength, 0); NativeUI::EditBuffer::Attach(edit_); }
        editBrush_ = CreateSolidBrush(RGB(0x29, 0x29, 0x29));
        menu_.Init(instance, hwnd_, root_);
        clockOk_ = clock_.Start(hwnd_, FrameMessage);
        if (options_.debug) {
            if (!options_.tuningFile.empty()) tuning_ = Onboarding::Tuning::Read(OnboardingDebug::Load(options_.tuningFile));
            panel_.Open(instance, hwnd_, root_, options_.tuningFile, &tuning_, [this] { Invalidate(); }, [this](Onboarding::Tuning::Section s) { Replay(s); });
        }
        shown_ = from_ = Screen::Welcome; RevealIcon(false); Arrange(); AttachUia();
        RequestSources();   // listed in the background while the first boards are read: ready before board 3
        // Everything a later board needs that takes time is made ready now, before the window shows: the two
        // pictures of the browser mock are decoded (tens of ms each, once), the shaders are built by the first
        // frame. No board change then waits for anything.
        chrome_.AssetBitmap(L"onboarding\\mock-plain.png"); chrome_.AssetBitmap(L"onboarding\\mock-bookmarks.png");
        Paint();
        opened_ = Motion::Now(); entering_ = true; done_ = false;
        ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); RequestFrame();
        MSG msg;
        while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (!done_) { PostQuitMessage(static_cast<int>(msg.wParam)); if (flow_.screen() != Screen::Closed) flow_.Skip(); }   // WM_QUIT while the window was open
        result.ending = flow_.ending() == Onboarding::Ending::None ? Onboarding::Ending::Skipped : flow_.ending();
        result.annotate = result.ending == Onboarding::Ending::Completed && flow_.annotate(); result.profileSaved = flow_.profileApplied(); result.handedOff = handedOff_;
        previewStage_.Stop();
        panel_.Close(); clock_.Stop(); KillTimer(hwnd_, 1); uia_.Detach(); menu_.Dispose(); DropLayers(); gpu_.Dispose(); glyphs_.Clear();
        if (edit_) { RemoveWindowSubclass(edit_, EditProc, 0); DestroyWindow(edit_); edit_ = nullptr; }
        if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
        if (editBrush_) { DeleteObject(editBrush_); editBrush_ = nullptr; }
        chrome_.Dispose(); DestroyWindow(hwnd_); hwnd_ = nullptr;
        return result;
    }

    // Debug panel « Rejouer 2 → 3 »: board 2 is shown again for a moment, then the transition plays. The flow is
    // taken to board 3 if it was before it (nothing is saved: review only).
    void Replay(Onboarding::Tuning::Section section) {
        namespace T = Onboarding::Tuning;
        if (offscreen_ || !hwnd_ || exitReplay_ || previewStage_.Active() || flow_.screen() == Screen::Closed || flow_.screen() == Screen::Outro) return;
        static const Screen boards[T::Sections][2] = {{Screen::Welcome, Screen::Profile}, {Screen::Profile, Screen::ImportPick}, {Screen::ImportPick, Screen::ImportRunning}, {Screen::ImportRunning, Screen::ImportDone},
                                                      {Screen::ImportDone, Screen::Annotation}, {Screen::Annotation, Screen::Outro}, {Screen::Outro, Screen::Outro}, {Screen::Annotation, Screen::Annotation}};
        if (!sourcesLoaded_ && !sourcesLoading_) RequestSources();
        ShowEdit(false); focus_ = hover_ = pressed_ = None; replaying_ = true; logoScale_ = logoAlpha_ = 1; const auto now = Motion::Now();
        if (section == T::Trame) { from_ = shown_ = Screen::Annotation; changing_ = false; replayUntil_ = now + std::chrono::milliseconds(6000); }   // board 6, to look at its dither
        else if (section == T::Exit) { exitReplay_ = true; from_ = shown_ = Screen::Outro; changing_ = false; exitAt_ = now; exitRunning_ = true; }   // « 7 → navigateur » on a stand-in for the browser
        else {
            from_ = boards[section][0]; shown_ = boards[section][1]; changing_ = true; changed_ = now + std::chrono::milliseconds(450);   // the first board is shown a moment, then the transition plays
            replayUntil_ = changed_ + std::chrono::milliseconds(static_cast<int>(T::TotalMs(tuning_[section])) + 800);
        }
        Invalidate(); RequestFrame();
    }
    const Onboarding::Tuning::Values& TuningForTest() const { return tuning_; }
    void SetTuningForTest(const Onboarding::Tuning::Values& v) { tuning_ = v; }
    void SetMockTitleForTest(const std::wstring& title) { mockTitle_ = title; }
    void SetBarForTest(std::vector<BarItem> items) { bar_ = std::move(items); barAt_ = Motion::Now(); }
    const std::vector<BarItem>& BarForTest() const { return bar_; }
    // ---- tests: the same drawing code into an offscreen target, and the events without a window ----
    void SetupForTest(const std::wstring& root, Options options) {
        root_ = root; options_ = std::move(options); draft_ = options_.seed; offscreen_ = true; flow_ = Onboarding::Flow(Onboarding::Flow::Config{options_.annotation});
        shown_ = from_ = Screen::Welcome; RevealIcon(false);
    }
    void ActivateForTest(int id) { Arrange(); Activate(id, true); }
    void SetLanguageForTest(I18n::Locale l) { SetLanguage(l); }   // what the language menu does with its answer
    bool KeyForTest(WPARAM key) { Arrange(); return Key(key); }
    int FocusForTest() const { return focus_; }
    void SetProgressForTest(float fraction) { progressForTest_ = fraction; }
    void SetShaderSecondsForTest(double seconds) { secondsForTest_ = seconds; }
    void SetDraftForTest(const Profiles::Profile& p) { draft_ = p; RevealIcon(false); }
    void SetPickForTest(const ImportPick& pick) { pick_ = pick; SyncImport(); }
    const ImportPick& PickForTest() const { return pick_; }
    std::vector<std::pair<int, D2D1_RECT_F>> ItemsForTest(Chrome& chrome) { Chrome* keep = c_; c_ = &chrome; Arrange(); c_ = keep; std::vector<std::pair<int, D2D1_RECT_F>> out; for (const auto& i : items_) out.push_back({i.id, i.r}); return out; }
    bool ShadersReadyForTest() const { return gpu_.Ready(); }
    // One frame of a transition, `ms` after it began (the flow must already be on `to`).
    bool RenderTransitionForTest(Screen from, float ms, UINT dpi, const wchar_t* path) {
        from_ = from; shown_ = flow_.screen(); changing_ = true; changed_ = Motion::Now() - std::chrono::microseconds(static_cast<long long>(ms * 1000));
        const bool ok = RenderForTest(dpi, path); changing_ = false; from_ = shown_; return ok;
    }
    bool RenderForTest(UINT dpi, const wchar_t* path) {
        offscreen_ = true; dpi_ = dpi;
        const bool ok = PopupKit::RenderPng(root_, Layout::width, Layout::height, dpi, path, c_, [this] {
            auto* t = c_->RenderTarget(); if (!t) return;
            gpuTried_ = gpuTried_ && gpu_.Ready();   // the first render loads the shader inputs with this target's factory
            Arrange(); t->BeginDraw(); Draw(Motion::Now()); t->EndDraw(); DropLayers(); glyphs_.Clear();
        });
        return ok;
    }
};

} // namespace OnboardingUI
} // namespace NativeChrome
