#pragma once
// Native "Réglages" drawer, painted with Direct2D through its own Chrome
// instance (shared fonts, icons, helpers). It docks to the window's right edge
// under the toolbar like the download history drawer, but always arrives with
// a slide and leaves the same way. Layout from the Paper frames "Réglages —
// panneau v2", "Autorisations des sites (sous-vue)" and "Autorisations ›
// meet.google.com".
//
// Motion (Apple / Emil): every lane retargets from the value on screen, so a
// toggle flicked twice or a drawer reopened mid-exit never jumps.
//  - Drawer: critically damped springs, exit snappier than enter.
//  - Engine choice: the selection plate and its check glide to the new row.
//  - Switches: the knob slides on a short spring and stretches while pressed.
//  - "Voir plus…": the extra engines unfold like a download section.
//  - Sub-views (Autorisations › site) push in from the right; the view below
//    slides 30% the other way and dims (iOS navigation), and pops back the same way.
//  - Value rows open the native menu as their list of choices (described
//    options for anti-tracking, sleeping tabs and site permissions).
// Reduced motion keeps every state change and drops the travel.
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <vector>
#include "chrome.h"
#include "downloads.h"
#include "settings.h"
#include "context-menu.h"

namespace NativeChrome {
namespace SettingsUI {
using Downloads::Lane; using Downloads::Spring; using Downloads::Easing; using Downloads::Settle;
using BrowserSettings::Engine;

// ---- Hit identifiers ----------------------------------------------------------
enum Hit : int {
    None = 0, Close = 1, Back = 2, EngineBase = 10, More = 20,
    RestoreTabs = 30, Folder = 31, AskWhere = 32, ReduceMotion = 33, Suggestions = 34, Passwords = 35, Autofill = 36, ClearOnExit = 37,
    ClearData = 40, CheckUpdates = 41, ResetAll = 42, ResetSite = 43,
    Tracking = 50, Sleep = 51, Zoom = 52, Permissions = 53,
    SiteBase = 100, PermissionBase = 400,
};
inline int EngineHit(int index) { return EngineBase + index; }
inline bool IsEngine(int hit) { return hit >= EngineBase && hit < EngineBase + BrowserSettings::EngineCount; }
inline bool IsSite(int hit) { return hit >= SiteBase && hit < PermissionBase; }
inline bool IsPermission(int hit) { return hit >= PermissionBase && hit < PermissionBase + 100; }
constexpr int SwitchHits[] = {RestoreTabs, AskWhere, ReduceMotion, Suggestions, Passwords, Autofill, ClearOnExit};
constexpr int SwitchCount = 7;
inline int SwitchIndex(int hit) { for (int i = 0; i < SwitchCount; ++i) if (SwitchHits[i] == hit) return i; return -1; }

// ---- Geometry (DIP, panel-local; x = 0 is the drawer's leading edge) -----------
namespace Layout {
using Design::Box;
inline constexpr float width = 332, top = Downloads::DrawerLayout::top;                          // under the toolbar
inline constexpr float inset = 12, rowLeft = inset, rowRight = width - inset;                    // rows: 12..320
inline constexpr float textLeft = rowLeft + 8;                                                    // 20: labels, names
inline constexpr float slotRight = rowRight - 4, slot = Design::Size::compact;                   // trailing 24 DIP slot: ..316
inline constexpr float iconLeft = textLeft, iconSize = Design::Size::icon;                        // favicon / caret: 20..36
inline constexpr float nameLeft = iconLeft + iconSize + Design::Space::md;                        // 44
inline constexpr float padTop = 16, headerHeight = 24, padBottom = 16, groupGap = 32;
inline constexpr float viewTop = padTop + headerHeight + groupGap;                                 // 72: content scrolls under the header
inline constexpr float label = 24, rowGap = 6, row = 32, pitch = row + rowGap;                    // label: 20 + 4 below
inline constexpr float folderRow = 54, twoLinePad = 8, descriptionLine = 18, siteRow = 54;
// Footers. Main: hairline, 12, "Effacer les données…" (32), 8, version (24), 16.
inline constexpr float footer = 1 + 12 + row + 8 + slot + padBottom;                              // 93
inline constexpr float switchWidth = 28, switchHeight = 16, knob = 12, knobInset = 2, knobStretch = 3;
inline constexpr float topFade = 16, bottomFade = 48;
inline constexpr Box Title(bool sub) { return {sub ? nameLeft : textLeft, padTop, rowRight - slot - 8, padTop + headerHeight}; }
inline constexpr Box BackButton() { return {rowLeft, padTop, rowLeft + slot, padTop + headerHeight}; }
inline constexpr Box CloseButton() { return {rowRight - slot, padTop, rowRight, padTop + headerHeight}; }
inline constexpr Box Row(float y, float h = row) { return {rowLeft, y, rowRight, y + h}; }
inline constexpr Box Trailing(float y, float h = row) { return {slotRight - slot, y + (h - slot) / 2, slotRight, y + (h + slot) / 2}; }
inline constexpr Box Switch(float y, float h = row) { return {slotRight - switchWidth, y + (h - switchHeight) / 2, slotRight, y + (h + switchHeight) / 2}; }
inline constexpr Box Icon(float y, float h = row) { return {iconLeft, y + (h - iconSize) / 2, iconLeft + iconSize, y + (h + iconSize) / 2}; }
inline constexpr Box Chevron(float y, float h = row) { return {slotRight - 16, y + (h - 24) / 2, slotRight, y + (h + 24) / 2}; }
// Two-line switch rows wrap their description up to 16 DIP before the switch.
inline constexpr float descriptionWidth = slotRight - switchWidth - 16 - textLeft;
}
static_assert(Layout::nameLeft == Layout::rowLeft + Layout::slot + 8, "sub-view title: 8 DIP after the back button");
static_assert(Layout::viewTop == 72, "content starts 32 DIP under the 24 DIP header");

namespace Timing {
inline constexpr Spring selectSpring{.26f}, switchSpring{.2f}, pageSpring{.34f};
inline constexpr Motion::Milliseconds select = Settle(.26f), toggle = Settle(.2f), press{120}, spin{900}, page = Settle(.34f);
}

enum class View { Main, Sites, Site };
// One laid-out element of a view's scrolling list (content coordinates).
enum class Kind { Label, Engine, More, Switch, Folder, Value, Nav, SiteRow, Note };
struct Slot {
    Kind kind = Kind::Label; int hit = 0, index = -1;
    float top = 0, height = Layout::row, opacity = 1;
    float clipTop = -1e6f, clipBottom = 1e6f;
    std::wstring text, value;              // label; value/summary/description text
    std::vector<std::wstring> lines;       // wrapped description (two-line switches)
};
// Per-frame inputs of the pure layout.
struct LayoutInput {
    float more = 0;                                            // fold of the extra engines (1 = shown)
    std::vector<std::wstring> passwordsLines, autofillLines;   // wrapped descriptions
    std::vector<std::wstring> noSitesLines;                     // "Autorisations des sites" with nothing to manage
    const BrowserSettings::State* state = nullptr;
    const std::vector<BrowserSettings::Site>* sites = nullptr; bool sitesLoaded = false;
    int site = -1;                                             // Site view: index into sites
};
inline std::wstring Host(const std::wstring& origin) {
    std::wstring h = origin; const auto scheme = h.find(L"://"); if (scheme != std::wstring::npos) h = h.substr(scheme + 3);
    while (!h.empty() && h.back() == L'/') h.pop_back();
    return h;
}

inline std::vector<Slot> ContentSlots(View view, const LayoutInput& in, float scroll, float* contentBottom = nullptr) {
    using namespace Layout;
    std::vector<Slot> slots; float y = viewTop - scroll;
    auto labelAt = [&](std::wstring text) { Slot s; s.kind = Kind::Label; s.top = y; s.height = label; s.text = std::move(text); slots.push_back(s); y += label + rowGap; };
    auto add = [&](Kind kind, int hit, float h, std::wstring text = L"", std::wstring value = L"") {
        Slot s; s.kind = kind; s.hit = hit; s.top = y; s.height = h; s.text = std::move(text); s.value = std::move(value); slots.push_back(s); y += h + rowGap; return &slots.back();
    };
    auto endGroup = [&] { y += groupGap - rowGap; };
    const auto* st = in.state;
    if (view == View::Main) {
        labelAt(L"Moteur de recherche");
        for (int i = 0; i < BrowserSettings::PrimaryEngines; ++i) { auto* s = add(Kind::Engine, EngineHit(i), row); s->index = i; }
        const float m = Motion::Clamp(in.more), extra = (BrowserSettings::EngineCount - BrowserSettings::PrimaryEngines) * pitch;
        if (m > .001f) {
            const float clipTop = y, clipBottom = y + m * extra;
            for (int i = BrowserSettings::PrimaryEngines; i < BrowserSettings::EngineCount; ++i) {
                Slot s; s.kind = Kind::Engine; s.hit = EngineHit(i); s.index = i; s.top = y + (i - BrowserSettings::PrimaryEngines) * pitch - (1 - m) * 8; s.opacity = m;
                if (m < 1) { s.clipTop = clipTop; s.clipBottom = clipBottom; }
                if (s.top < clipBottom) slots.push_back(s);
            }
        }
        y += m * extra;
        add(Kind::More, More, row);
        add(Kind::Switch, Suggestions, row, L"Suggestions de recherche");
        endGroup();
        labelAt(L"Démarrage");
        add(Kind::Switch, RestoreTabs, row, L"Restaurer les onglets");
        endGroup();
        labelAt(L"Téléchargements");
        add(Kind::Folder, Folder, folderRow);
        add(Kind::Switch, AskWhere, row, L"Demander où enregistrer");
        endGroup();
        labelAt(L"Confidentialité");
        add(Kind::Value, Tracking, row, L"Anti-pistage", st ? BrowserSettings::Info(st->tracking).label : L"");
        { auto* s = add(Kind::Switch, Passwords, 2 * twoLinePad + 20 + 2 + in.passwordsLines.size() * descriptionLine, L"Mots de passe"); s->lines = in.passwordsLines; }
        { auto* s = add(Kind::Switch, Autofill, 2 * twoLinePad + 20 + 2 + in.autofillLines.size() * descriptionLine, L"Saisie automatique"); s->lines = in.autofillLines; }
        add(Kind::Switch, ClearOnExit, row, L"Effacer en quittant");
        if (in.sitesLoaded && in.sites && in.sites->empty()) {
            // Nothing to manage yet: an inert row says why instead of opening an empty view.
            auto* s = add(Kind::Nav, None, 2 * twoLinePad + 20 + 2 + in.noSitesLines.size() * descriptionLine, L"Autorisations des sites"); s->lines = in.noSitesLines;
        } else add(Kind::Nav, Permissions, row, L"Autorisations des sites", in.sitesLoaded && in.sites ? std::to_wstring(in.sites->size()) : L"");
        endGroup();
        labelAt(L"Performances");
        add(Kind::Value, Sleep, row, L"Onglets en veille", st ? BrowserSettings::SleepLabel(st->sleepMinutes) : L"");
        endGroup();
        labelAt(L"Affichage");
        add(Kind::Value, Zoom, row, L"Zoom des pages", st ? BrowserSettings::ZoomLabel(st->zoomPercent) : L"");
        add(Kind::Switch, ReduceMotion, row, L"Réduire les animations");
        y -= rowGap;
    } else if (view == View::Sites) {
        const size_t n = in.sites ? in.sites->size() : 0;
        if (!in.sitesLoaded) { add(Kind::Note, None, row, L"Chargement…"); }
        else if (!n) { add(Kind::Note, None, 2 * descriptionLine, L"Aucun site n'a encore demandé d'accès."); }
        else {
            labelAt(n == 1 ? L"1 site" : std::to_wstring(n) + L" sites");
            for (size_t i = 0; i < n; ++i) {
                auto* s = add(Kind::SiteRow, SiteBase + static_cast<int>(i), siteRow, Host((*in.sites)[i].origin), BrowserSettings::SiteSummary((*in.sites)[i]));
                s->index = static_cast<int>(i);
            }
        }
        y -= rowGap;
    } else if (in.sites && in.site >= 0 && in.site < static_cast<int>(in.sites->size())) {
        labelAt(L"Autorisations");
        const auto& site = (*in.sites)[in.site];
        for (size_t i = 0; i < site.permissions.size(); ++i) {
            const auto& p = site.permissions[i];
            auto* s = add(Kind::Value, PermissionBase + static_cast<int>(i), row, BrowserSettings::PermissionName(p.kind), BrowserSettings::PermissionValue(p.kind, p.state));
            s->index = static_cast<int>(i);
        }
        y -= rowGap;
    }
    if (contentBottom) *contentBottom = y + scroll;
    return slots;
}

// "…\Downloads": the folder's own name, the rest elided; a drive root stays whole.
inline std::wstring FolderText(const std::wstring& path) {
    std::wstring p = path; while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    const auto slash = p.find_last_of(L"\\/");
    if (p.empty()) return L"Téléchargements";
    if (slash == std::wstring::npos || slash + 1 >= p.size()) return p;
    if (slash <= 2) return p; // "C:\Users" stays readable
    return L"…\\" + p.substr(slash + 1);
}
inline constexpr const wchar_t* PasswordsDescription = L"Propose d'enregistrer vos identifiants, puis les remplit à la connexion.";
inline constexpr const wchar_t* AutofillDescription = L"Remplit les formulaires avec votre nom, adresse et e-mail.";
inline constexpr const wchar_t* NoSitesDescription = L"Aucun site n'a encore demandé d'autorisation.";
inline constexpr const wchar_t* SitesNote = L"Un site demande de nouveau l'accès la prochaine fois.";

class Drawer {
public:
    struct Version { std::wstring text; bool checking = false, canCheck = false; };
    struct Hooks {
        std::function<void(Motion::Time)> schedule;                 // display-paced frames
        std::function<void(bool)> closed;                           // true when closed from the keyboard
        std::function<void(BrowserSettings::Change)> changed;       // the state changed: apply + save
        std::function<void()> pickFolder;                           // modal folder picker (may change the state)
        std::function<void()> clearData;                            // modal confirmation + clear
        std::function<void()> checkUpdates;
        std::function<Version()> version;                           // "browser 0.3.x · À jour"
        std::function<std::wstring()> downloadFolder;               // effective folder (default resolved)
        std::function<bool()> reducedMotion;                        // system || setting
        std::function<void(std::function<void(std::vector<BrowserSettings::Site>)>)> loadSites; // async, UI thread
        std::function<void(const std::wstring&, BrowserSettings::Permission, BrowserSettings::PermissionState)> setPermission;
        std::function<void(const std::wstring&)> resetSites;                                       // empty origin: all
        std::function<int(const std::vector<ContextMenu::Item>&, RECT, bool)> choose;            // modal native menu, 0 = dismissed
    };
private:
    HWND hwnd_ = nullptr, owner_ = nullptr, tip_ = nullptr;
    std::vector<UINT_PTR> tips_; std::vector<std::wstring> tipTexts_;
    Chrome chrome_; Motion::State motion_;
    BrowserSettings::State* state_ = nullptr; Hooks hooks_;
    ID2D1GradientStopCollection* stops_ = nullptr; ID2D1LinearGradientBrush* fade_ = nullptr;
    Lane slide_, more_, plate_, page_;
    std::array<Lane, 3> scrolls_;              // one per view depth
    std::array<Lane, SwitchCount> switches_, knobPress_;
    View views_[3] = {View::Main, View::Sites, View::Site}; int depth_ = 0; // navigation stack: depth 0..2
    std::vector<BrowserSettings::Site> sites_; bool sitesLoaded_ = false; int site_ = -1; unsigned loadTicket_ = 0;
    std::vector<std::wstring> passwordsLines_, autofillLines_, noSitesLines_, sitesNoteLines_;
    int hover_ = 0, pressed_ = 0, focus_ = 0;
    bool keyboard_ = false, visible_ = false, closing_ = false, tracking_ = false, modal_ = false, animatingPainted_ = false, spinning_ = false;
    int appliedVisible_ = -1; RECT appliedOwner_{}, appliedWindow_{};
    Motion::Time spinStart_{}, landStart_{};
    // Update spinner: a constant counter-clockwise turn while a check runs. When
    // it ends, the glyph keeps its speed and eases into the next full turn (cubic
    // Hermite: the spin's velocity in, zero out), so it settles on its rest pose.
    bool landing_ = false; float landFrom_ = 0, landDistance_ = 0, landSeconds_ = 0;
    static constexpr float spinSpeed = 360.f * 1000 / Timing::spin.count(); // degrees per second
    float Seconds(Motion::Time from, Motion::Time to) const { return static_cast<float>(std::chrono::duration<double>(to - from).count()); }
    bool Landing(Motion::Time now) const { return landing_ && Seconds(landStart_, now) < landSeconds_; }
    bool Spinning(Motion::Time now) const { return !Reduced() && (spinning_ || Landing(now)); }
    float SpinDegrees(Motion::Time now) const {
        if (spinning_) return std::fmod(Seconds(spinStart_, now) * spinSpeed, 360.f);
        if (!Landing(now)) return 0;
        const float u = Motion::Clamp(Seconds(landStart_, now) / landSeconds_), u2 = u * u, u3 = u2 * u;
        return std::fmod(landFrom_ + (3 * u2 - 2 * u3) * landDistance_ + (u - 2 * u2 + u3) * spinSpeed * landSeconds_, 360.f);
    }
    void SetChecking(bool checking, Motion::Time now) {
        if (checking && !spinning_) { spinStart_ = now - std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double>(SpinDegrees(now) / spinSpeed)); landing_ = false; }
        else if (!checking && spinning_ && !Reduced()) {
            landFrom_ = SpinDegrees(now); landDistance_ = 360 - landFrom_;
            if (landDistance_ < 60) landDistance_ += 360; // too close to rest to brake smoothly: one more turn
            landSeconds_ = std::clamp(2 * landDistance_ / spinSpeed, .25f, .7f); landStart_ = now; landing_ = true;
        }
        spinning_ = checking;
    }
    bool* SwitchField(int hit) const {
        if (!state_) return nullptr;
        switch (hit) {
        case RestoreTabs: return &state_->restoreTabs; case AskWhere: return &state_->askWhereToSave; case ReduceMotion: return &state_->reduceMotion;
        case Suggestions: return &state_->searchSuggestions; case Passwords: return &state_->passwords; case Autofill: return &state_->autofill;
        case ClearOnExit: return &state_->clearOnExit; default: return nullptr;
        }
    }
    static BrowserSettings::Change SwitchChange(int hit) {
        using C = BrowserSettings::Change;
        switch (hit) {
        case RestoreTabs: return C::RestoreTabs; case AskWhere: return C::AskWhereToSave; case ReduceMotion: return C::ReduceMotion;
        case Suggestions: return C::SearchSuggestions; case Passwords: return C::Passwords; case Autofill: return C::Autofill; default: return C::ClearOnExit;
        }
    }
    bool SwitchValue(int hit) const { auto* f = SwitchField(hit); return f && *f; }
    bool Reduced() const { return motion_.ReducedMotion(); }
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    int OwnerPx(float dip) const { return static_cast<int>(std::lround(dip * GetDpiForWindow(owner_) / 96.f)); }
    float Height() const { RECT r{}; if (hwnd_) GetClientRect(hwnd_, &r); return r.bottom * 96.f / Dpi(); }
    View Current() const { return views_[depth_]; }
    // Footer height per view (DIP): main = clear + version; sites = note + "Tout retirer"; site = one danger row.
    float FooterHeight(View v) const { return v == View::Main ? Layout::footer : v == View::Sites ? 1 + 12 + 40 + 8 + Layout::row + Layout::padBottom : 1 + 12 + Layout::row + Layout::padBottom; }
    float ViewBottom(View v) const { return Height() - FooterHeight(v); }
    LayoutInput Input(float more) const {
        LayoutInput in; in.more = more; in.passwordsLines = passwordsLines_; in.autofillLines = autofillLines_; in.noSitesLines = noSitesLines_;
        in.state = state_; in.sites = &sites_; in.sitesLoaded = sitesLoaded_; in.site = site_; return in;
    }
    std::vector<Slot> Slots(View v, Motion::Time now, bool target = false) const {
        const int d = v == View::Main ? 0 : v == View::Sites ? 1 : 2;
        return ContentSlots(v, Input(target ? more_.Target() : more_.Value(now)), target ? scrolls_[d].Target() : scrolls_[d].Value(now));
    }
    float MaxScroll(View v, Motion::Time now, bool target) const {
        float bottom = 0; ContentSlots(v, Input(target ? more_.Target() : more_.Value(now)), 0, &bottom);
        return std::max(0.f, bottom + Layout::groupGap - ViewBottom(v));
    }
    int SelectedIndex() const { return state_ ? static_cast<int>(state_->engine) : 0; }
    // Selection plate: content y of the selected row (animated), unscrolled.
    float EngineTop(int index, float more) const {
        for (const auto& s : ContentSlots(View::Main, Input(more), 0)) if (s.kind == Kind::Engine && s.index == index) return s.top;
        return Layout::viewTop + Layout::label + Layout::rowGap; // hidden engine: tucked under the first row
    }
    template<class T> static void Drop(T*& p) { if (p) { p->Release(); p = nullptr; } }
    void PushOpacity(float opacity) {
        ID2D1Layer* layer = nullptr; auto* t = chrome_.RenderTarget();
        if (FAILED(t->CreateLayer(&layer))) return;
        t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity), layer);
        layer->Release();
    }
    void Fill(Design::Box b, float radius, float r, float g, float bl, float a) {
        auto* brush = chrome_.SolidBrush(); brush->SetColor(D2D1::ColorF(r, g, bl, a));
        chrome_.RenderTarget()->FillRoundedRectangle(D2D1::RoundedRect({b.left, b.top, b.right, b.bottom}, radius, radius), brush);
    }
    void White(Design::Box b, float radius, float a) { if (a > .001f) Fill(b, radius, 1, 1, 1, a); }
    static D2D1_RECT_F R(Design::Box b) { return {b.left, b.top, b.right, b.bottom}; }
    Motion::InteractionVisual Interaction(int hit, Motion::Time now) const { return motion_.Visual(hit, now); }
    // Balanced wrap (CSS text-wrap: balance): the greedy line count, at the
    // narrowest width that keeps it, so lines come out even instead of a short last one.
    std::vector<std::wstring> Wrap(const std::wstring& text, float width) {
        auto lines = Greedy(text, width);
        if (lines.size() < 2) return lines;
        float lo = width / lines.size(), hi = width;
        for (int k = 0; k < 10 && hi - lo > .5f; ++k) { const float mid = (lo + hi) / 2; if (Greedy(text, mid).size() == lines.size()) hi = mid; else lo = mid; }
        return Greedy(text, hi);
    }
    std::vector<std::wstring> Greedy(const std::wstring& text, float width) {
        std::vector<std::wstring> lines; std::wstring line, word;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i < text.size() && text[i] != L' ') { word += text[i]; continue; }
            if (word.empty()) continue;
            const std::wstring candidate = line.empty() ? word : line + L" " + word;
            if (!line.empty() && chrome_.TextWidth(candidate, 14, false, false, -.14f) > width) { lines.push_back(line); line = word; } else line = candidate;
            word.clear();
        }
        if (!line.empty()) lines.push_back(line);
        return lines;
    }
    void MeasureDescriptions() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        passwordsLines_ = Wrap(PasswordsDescription, Layout::descriptionWidth);
        autofillLines_ = Wrap(AutofillDescription, Layout::descriptionWidth);
        noSitesLines_ = Wrap(NoSitesDescription, Layout::rowRight - 8 - Layout::textLeft);
        sitesNoteLines_ = Wrap(SitesNote, Layout::rowRight - Layout::textLeft - 4);
    }

    // Ghost icon button (Paper: 14 DIP glyph at 70%, radius 3): fills and brightens on hover.
    void IconButton(const wchar_t* icon, Design::Box box, int hit, Motion::Time now, float rotation = 0, bool enabled = true) {
        auto* t = chrome_.RenderTarget(); const auto v = Interaction(hit, now);
        const float h = enabled ? v.hover : 0, p = enabled ? v.press : 0;
        if (h > 0 || p > 0) {
            const float c = Design::Surface::canvas, hovered = c + (Design::Surface::hover - c) * h, fill = hovered + (Design::Surface::pressed - hovered) * p;
            chrome_.Round(R(box), Design::Radius::inset, fill);
        }
        if (focus_ == hit) chrome_.FocusRing(R(box), Design::Radius::inset);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        if (rotation != 0) t->SetTransform(D2D1::Matrix3x2F::Rotation(rotation, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
        // Full-opacity 14 px masks (the regular icons bake in 50% at 16 px): Paper's exact 70%, crisp bold strokes.
        const float size = 14 * Motion::PressScale(p, Reduced(), enabled), a = enabled ? .7f + .3f * h : Design::Text::disabled;
        chrome_.TintedIcon((std::wstring(icon) + L"-mask").c_str(), R(box), size, D2D1::ColorF(1, 1, 1, a));
        t->SetTransform(base);
    }
    // Opaque equivalent of `amount` white over the canvas: row fills never stack
    // translucent layers where the selection plate crosses a hovered row.
    static float Tint(float amount) { const float c = Design::Surface::canvas; return c + (1 - c) * amount; }
    static constexpr float hoverTint = .04f, pressTint = .03f, plateTint = .04f;
    void RowSurface(Design::Box box, int hit, Motion::Time now) {
        if (!hit) return;
        const auto v = Interaction(hit, now); const float amount = hoverTint * v.hover + pressTint * v.press;
        if (amount > .0005f) chrome_.Round(R(box), Design::Radius::control, Tint(amount));
    }
    void RowFocus(Design::Box box, int hit) { if (focus_ == hit) chrome_.FocusRing(R(box), Design::Radius::control); }
    void Label16(const std::wstring& text, float left, float top, float right, float opacity) { chrome_.Text(text, {left, top, right, top + 20}, 16, opacity, false, false, -.16f); }
    void Label14(const std::wstring& text, float left, float top, float right, float opacity, float height = 18) { chrome_.Text(text, {left, top, right, top + height}, 14, opacity, false, false, -.14f); }
    // The engine's own favicon (assets\engines\<key>.png), dimmed like its name until selected.
    void EngineTile(int index, float y, float strength) {
        const auto box = Layout::Icon(y);
        const std::string key = BrowserSettings::Info(static_cast<Engine>(index)).key;
        if (auto* favicon = chrome_.AssetBitmap(L"engines\\" + std::wstring(key.begin(), key.end()) + L".png")) {
            chrome_.RenderTarget()->DrawBitmap(favicon, R(box), .75f + .25f * strength, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
        White(box, 4, .7f + .3f * strength);
    }
    void EngineRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), s.hit);
        const float plateTop = plate_.Value(now), closeness = Motion::Clamp(1 - std::abs(plateTop - (s.top + scrolls_[0].Value(now))) / Layout::pitch);
        EngineTile(s.index, s.top, closeness);
        chrome_.Text(BrowserSettings::Info(static_cast<Engine>(s.index)).name, {Layout::nameLeft, s.top, Layout::slotRight - Layout::slot - 4, s.top + Layout::row}, 16, .7f + .3f * closeness, false, false, -.16f);
    }
    void MoreRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), More);
        auto* t = chrome_.RenderTarget(); const auto icon = Layout::Icon(s.top);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(180.f * Motion::Clamp(more_.Value(now)), {(icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2}) * base);
        // "Voir moins" cannot fold away the selected engine: it rests dimmed then.
        const float opacity = more_.Target() > .5f && state_ && !BrowserSettings::Primary(state_->engine) ? Design::Text::disabled : .7f;
        chrome_.Icon(L"caret", R(icon), 16, opacity);
        t->SetTransform(base);
        chrome_.Text(more_.Target() > .5f ? L"Voir moins" : L"Voir plus…", {Layout::nameLeft, s.top, Layout::slotRight, s.top + Layout::row}, 16, opacity, false, false, -.16f);
    }
    void SwitchRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, s.height), s.hit);
        const int i = SwitchIndex(s.hit); const float on = Motion::Clamp(switches_[i].Value(now));
        const auto track = Layout::Switch(s.top, s.height);
        if (s.lines.empty()) Label16(s.text, Layout::textLeft, s.top + (s.height - 20) / 2, track.left - 16, .7f + .3f * on);
        else {
            // Two-line row (Paper): label 16 DIP, description 14 DIP at 50%, 2 DIP apart, 8 DIP padding.
            Label16(s.text, Layout::textLeft, s.top + Layout::twoLinePad, track.left - 16, .7f + .3f * on);
            for (size_t k = 0; k < s.lines.size(); ++k) Label14(s.lines[k], Layout::textLeft, s.top + Layout::twoLinePad + 22 + k * Layout::descriptionLine, track.left - 8, .5f);
        }
        // Track: 10% white off, 70% on. Knob: 70% white off, the canvas on.
        const auto v = Interaction(s.hit, now);
        White(track, Layout::switchHeight / 2, .10f + .60f * on + .05f * v.hover * (1 - on));
        const float c = Design::Surface::canvas, offTrack = c + (1 - c) * .10f, offKnob = offTrack + (1 - offTrack) * .7f;
        const float knobGray = offKnob + (c - offKnob) * on;
        const float stretch = Reduced() ? 0 : Layout::knobStretch * Motion::Clamp(knobPress_[i].Value(now));
        const float travel = Layout::switchWidth - 2 * Layout::knobInset - Layout::knob;
        float left = track.left + Layout::knobInset + travel * on - stretch * on;
        const Design::Box knob{left, track.top + Layout::knobInset, left + Layout::knob + stretch, track.top + Layout::knobInset + Layout::knob};
        Fill(knob, Layout::knob / 2, knobGray, knobGray, knobGray, 1);
    }
    // Label + current value + chevron: opens its list of choices (native menu).
    void ValueRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top), s.hit);
        const auto v = Interaction(s.hit, now); const auto chevron = Layout::Chevron(s.top);
        const float w = chrome_.TextWidth(s.value, 14, false, false, -.14f);
        Label16(s.text, Layout::textLeft, s.top + 6, chevron.left - w - 16, .7f + .3f * v.hover);
        chrome_.Text(s.value, {chevron.left - 8 - w - 1, s.top + 6, chevron.left - 8 + 1, s.top + 26}, 14, .5f + .2f * v.hover, false, false, -.14f);
        chrome_.Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
    }
    // Label + count + right chevron: pushes a sub-view.
    void NavRow(const Slot& s, Motion::Time now) {
        if (!s.hit) { // inert: label + why, no chevron, no hover
            Label16(s.text, Layout::textLeft, s.top + Layout::twoLinePad, Layout::rowRight - 8, .5f);
            for (size_t k = 0; k < s.lines.size(); ++k) Label14(s.lines[k], Layout::textLeft, s.top + Layout::twoLinePad + 22 + k * Layout::descriptionLine, Layout::rowRight - 8, .4f);
            return;
        }
        RowFocus(Layout::Row(s.top), s.hit);
        auto* t = chrome_.RenderTarget(); const auto v = Interaction(s.hit, now); const auto chevron = Layout::Chevron(s.top);
        const float w = chrome_.TextWidth(s.value, 14, true);
        Label16(s.text, Layout::textLeft, s.top + 6, chevron.left - w - 16, .7f + .3f * v.hover);
        if (!s.value.empty()) chrome_.Text(s.value, {chevron.left - 8 - w - 1, s.top + 6, chevron.left - 8 + 1, s.top + 26}, 14, .5f + .2f * v.hover, true);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
        chrome_.Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
        t->SetTransform(base);
    }
    void FolderRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, Layout::folderRow), Folder);
        Label16(L"Enregistrer dans", Layout::textLeft, s.top + 8, Layout::Trailing(s.top).left - 8, .7f);
        const std::wstring folder = hooks_.downloadFolder ? hooks_.downloadFolder() : (state_ ? state_->downloadFolder : L"");
        chrome_.Text(FolderText(folder), {Layout::textLeft, s.top + 30, Layout::Trailing(s.top).left - 8, s.top + 46}, 14, .5f, false, false, -.14f);
        // Closed at rest, it opens on hover: cross-fade on the row's own hover tween.
        const auto v = Interaction(Folder, now); const auto box = Layout::Trailing(s.top, Layout::folderRow);
        const float h = Motion::Clamp(v.hover);
        chrome_.FolderGlyph(R(box), 14 * Motion::PressScale(v.press, Reduced()), .7f + .3f * h, h, Reduced());
    }
    void SiteRow(const Slot& s, Motion::Time now) {
        RowFocus(Layout::Row(s.top, s.height), s.hit);
        auto* t = chrome_.RenderTarget(); const auto v = Interaction(s.hit, now); const auto chevron = Layout::Chevron(s.top, s.height);
        chrome_.Icon(L"favicon", R(Layout::Icon(s.top + 8, 20)), 16, .6f + .3f * v.hover);
        Label16(s.text, Layout::nameLeft, s.top + 8, chevron.left - 8, .8f + .2f * v.hover);
        Label14(s.value, Layout::nameLeft, s.top + 30, chevron.left - 8, .5f, 16);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(chevron.left + chevron.right) / 2, (chevron.top + chevron.bottom) / 2}) * base);
        chrome_.Icon(L"caret", R(chevron), 14, .5f + .3f * v.hover);
        t->SetTransform(base);
    }
    void Plate(Motion::Time now, float scroll) {
        // One plate for the selection: it glides between rows, the check rides on it.
        const float y = plate_.Value(now) - scroll;
        chrome_.Round(R(Layout::Row(y)), Design::Radius::control, Tint(plateTint)); // opaque: covers a hover fill instead of adding to it
        chrome_.Icon(L"check", R(Layout::Trailing(y)), 14, .9f);
    }
    void DangerRow(const wchar_t* text, float top, int hit, Motion::Time now, bool enabled) {
        const auto v = Interaction(hit, now); const auto row = Layout::Row(top);
        if (enabled && (v.hover > 0 || v.press > 0)) Fill(row, Design::Radius::control, 1, .482f, .431f, .08f * v.hover + .05f * v.press);
        if (focus_ == hit) chrome_.FocusRing(R(row), Design::Radius::control);
        chrome_.ink = D2D1::ColorF(1, .482f, .431f, 1);
        Label16(text, Layout::textLeft, top + 6, Layout::rowRight, enabled ? 1.f : Design::Text::disabled);
        chrome_.ink = D2D1::ColorF(1, 1, 1, 1);
    }
    void Footer(View view, Motion::Time now) {
        const float h = Height(), top = h - FooterHeight(view);
        auto* t = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush();
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        chrome_.Color(1, .04f); t->FillRectangle(D2D1::RectF(Layout::rowLeft, top, Layout::rowRight, top + px), brush);
        if (view == View::Main) {
            DangerRow(L"Effacer les données…", top + 13, ClearData, now, true);
            // Version + check for updates. The glyph spins while a check runs.
            const auto version = hooks_.version ? hooks_.version() : Version{};
            const float y = h - Layout::padBottom - Layout::slot;
            chrome_.Text(version.text, {Layout::textLeft, y, Layout::Trailing(y, Layout::slot).left - 4, y + Layout::slot}, 14, .5f, true, false, -.14f);
            float turn = 0;
            if (Spinning(now)) turn = -SpinDegrees(now); // counter-clockwise, the way the glyph's arrow points
            IconButton(L"reload", Layout::Trailing(y, Layout::slot), CheckUpdates, now, turn, version.canCheck && !version.checking);
        } else if (view == View::Sites) {
            const auto& lines = sitesNoteLines_;
            for (size_t k = 0; k < lines.size() && k < 2; ++k) Label14(lines[k], Layout::textLeft, top + 13 + k * 20, Layout::rowRight, .5f, 20);
            DangerRow(L"Tout retirer", top + 13 + 40 + 8, ResetAll, now, !sites_.empty());
        } else DangerRow(L"Retirer les autorisations", top + 13, ResetSite, now, true);
    }
    // One view at a horizontal offset (navigation push/pop).
    void PaintView(int depth, float x, float opacity, Motion::Time now) {
        auto* target = chrome_.RenderTarget(); const View view = views_[depth];
        D2D1_MATRIX_3X2_F base; target->GetTransform(&base);
        target->SetTransform(D2D1::Matrix3x2F::Translation(x, 0) * base);
        const bool layered = opacity < .998f; if (layered) PushOpacity(opacity);
        const float c = Design::Surface::canvas, h = Height();
        chrome_.Round({0, 0, Layout::width, h}, 0, c); // opaque page: the view beneath never shows through
        // Header
        if (view == View::Main) chrome_.Text(L"Réglages", R(Layout::Title(false)), 16, .9f, false, true, -.16f);
        else {
            IconButton(L"back", Layout::BackButton(), Back, now);
            const std::wstring title = view == View::Sites ? L"Autorisations" : (site_ >= 0 && site_ < static_cast<int>(sites_.size()) ? Host(sites_[site_].origin) : L"");
            chrome_.Text(title, R(Layout::Title(true)), 16, .9f, false, true, -.16f);
        }
        IconButton(L"close", Layout::CloseButton(), Close, now);
        // Content, clipped between the header and the footer.
        const float viewTop = Layout::viewTop - Layout::groupGap / 2, viewBottom = ViewBottom(view);
        const float scroll = scrolls_[depth].Value(now);
        target->PushAxisAlignedClip(D2D1::RectF(0, viewTop, Layout::width, viewBottom), D2D1_ANTIALIAS_MODE_ALIASED);
        const auto slots = Slots(view, now);
        // Three passes: row fills, then the selection plate over them, then row contents.
        auto pass = [&](bool surfaces) { for (const auto& s : slots) {
            if (s.top + s.height < viewTop || s.top > viewBottom || s.opacity <= .002f || (surfaces && (s.kind == Kind::Label || s.kind == Kind::Note))) continue;
            const bool clipped = s.clipTop > -1e5f, fading = s.opacity < .998f;
            if (clipped) target->PushAxisAlignedClip(D2D1::RectF(0, std::max(s.clipTop, viewTop), Layout::width, std::min(s.clipBottom, viewBottom)), D2D1_ANTIALIAS_MODE_ALIASED);
            if (fading) PushOpacity(s.opacity);
            if (surfaces) RowSurface(Layout::Row(s.top, s.height), s.hit, now);
            else switch (s.kind) {
            case Kind::Label: chrome_.Text(s.text, {Layout::textLeft, s.top, Layout::rowRight, s.top + 20}, 14, .5f, false, true, -.14f); break;
            case Kind::Note: { const auto lines = Wrap(s.text, Layout::rowRight - Layout::textLeft); for (size_t k = 0; k < lines.size(); ++k) Label14(lines[k], Layout::textLeft, s.top + k * 20, Layout::rowRight, .5f, 20); break; }
            case Kind::Engine: EngineRow(s, now); break;
            case Kind::More: MoreRow(s, now); break;
            case Kind::Switch: SwitchRow(s, now); break;
            case Kind::Folder: FolderRow(s, now); break;
            case Kind::Value: ValueRow(s, now); break;
            case Kind::Nav: NavRow(s, now); break;
            case Kind::SiteRow: SiteRow(s, now); break;
            }
            if (fading) target->PopLayer();
            if (clipped) target->PopAxisAlignedClip();
        } };
        pass(true); if (view == View::Main) Plate(now, scroll); pass(false);
        // Scroll edge effects: content dissolves into the canvas where more lies beyond.
        const float max = MaxScroll(view, now, false);
        if (fade_ && scroll > 0) {
            fade_->SetStartPoint({0, viewTop + Layout::topFade}); fade_->SetEndPoint({0, viewTop}); fade_->SetOpacity(Motion::Clamp(scroll / Layout::topFade));
            target->FillRectangle(D2D1::RectF(0, viewTop, Layout::width, viewTop + Layout::topFade), fade_);
        }
        if (fade_ && max - scroll > 0) {
            fade_->SetStartPoint({0, viewBottom - Layout::bottomFade}); fade_->SetEndPoint({0, viewBottom}); fade_->SetOpacity(Motion::Clamp((max - scroll) / Layout::topFade));
            target->FillRectangle(D2D1::RectF(0, viewBottom - Layout::bottomFade, Layout::width, viewBottom), fade_);
        }
        if (fade_) fade_->SetOpacity(1);
        target->PopAxisAlignedClip();
        Footer(view, now);
        if (layered) target->PopLayer();
        target->SetTransform(base);
    }
    void Paint(Motion::Time now) {
        auto* target = chrome_.RenderTarget(); auto* brush = chrome_.SolidBrush(); if (!target || !brush) return;
        if (!fade_) {
            const float c = Design::Surface::canvas;
            D2D1_GRADIENT_STOP stops[] = {{0, D2D1::ColorF(c, c, c, 0)}, {1, D2D1::ColorF(c, c, c, 1)}};
            if (FAILED(target->CreateGradientStopCollection(stops, 2, &stops_)) ||
                FAILED(target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), stops_, &fade_))) { Drop(fade_); Drop(stops_); }
        }
        const float c = Design::Surface::canvas, h = Height();
        target->Clear(D2D1::ColorF(c, c, c, 1));
        chrome_.keyboardModality = keyboard_;
        // Navigation: views below the current position slide 30% left and dim; the
        // one above enters from the right edge (iOS push). Reduced motion: no travel.
        const float pos = page_.Value(now); const int lo = static_cast<int>(std::floor(pos)), hi = std::min(2, lo + 1);
        target->PushAxisAlignedClip(D2D1::RectF(0, 0, Layout::width, h), D2D1_ANTIALIAS_MODE_ALIASED);
        const float t = pos - lo;
        if (t < .001f || lo == hi) PaintView(std::clamp(lo, 0, 2), 0, 1, now);
        else { PaintView(lo, -.3f * Layout::width * t, 1 - .5f * t, now); PaintView(hi, Layout::width * (1 - t), 1, now); }
        target->PopAxisAlignedClip();
        // Leading edge: one physical pixel, a touch lighter than the canvas (as the download drawer).
        const float px = target->GetSize().width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
        chrome_.Color(1, .02f); target->FillRectangle(D2D1::RectF(0, 0, px, h), brush);
    }
    void PaintNow() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* target = chrome_.RenderTarget(); if (!target) return;
        target->BeginDraw(); Paint(Motion::Now());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) { Drop(fade_); Drop(stops_); chrome_.RecreateTarget(); }
    }

    // ---- Input -----------------------------------------------------------------
    int HitAt(float x, float y) const {
        const View view = Current(); const float h = Height();
        auto in = [x, y](Design::Box b) { return x >= b.left && x < b.right && y >= b.top && y < b.bottom; };
        if (page_.Moving(Motion::Now())) return None;
        if (in(Layout::CloseButton())) return Close;
        if (view != View::Main && in(Layout::BackButton())) return Back;
        const float footerTop = h - FooterHeight(view);
        if (view == View::Main) {
            if (in(Layout::Trailing(h - Layout::padBottom - Layout::slot, Layout::slot))) return CheckUpdates;
            if (in(Layout::Row(footerTop + 13))) return ClearData;
        } else if (view == View::Sites) { if (!sites_.empty() && in(Layout::Row(footerTop + 13 + 40 + 8))) return ResetAll; }
        else if (in(Layout::Row(footerTop + 13))) return ResetSite;
        if (y < Layout::viewTop - Layout::groupGap / 2 || y >= footerTop) return None;
        for (const auto& s : Slots(view, Motion::Now())) {
            if (!s.hit || s.opacity < .5f) continue;
            if (in(Layout::Row(s.top, s.height)) && y >= s.clipTop && y < s.clipBottom) return s.hit;
        }
        return None;
    }
    std::vector<int> FocusOrder() const {
        const View view = Current(); std::vector<int> order;
        if (view != View::Main) order.push_back(Back);
        order.push_back(Close);
        for (const auto& s : Slots(view, Motion::Now())) if (s.hit && s.opacity > .5f) order.push_back(s.hit);
        if (view == View::Main) { order.push_back(ClearData); if (hooks_.version && hooks_.version().canCheck) order.push_back(CheckUpdates); }
        else if (view == View::Sites) { if (!sites_.empty()) order.push_back(ResetAll); }
        else order.push_back(ResetSite);
        return order;
    }
    void Interact(int hover, int pressed) {
        hover_ = hover; pressed_ = pressed; const auto now = Motion::Now();
        motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now);
        for (int i = 0; i < SwitchCount; ++i)
            knobPress_[i].Set(pressed_ == SwitchHits[i] && hover_ == SwitchHits[i] ? 1.f : 0.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::press, Downloads::Curves::emphasized);
        Invalidate(); Schedule(now);
    }
    void Repoint() {
        if (!hwnd_ || !visible_) return;
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p); RECT client; GetClientRect(hwnd_, &client);
        const float s = 96.f / Dpi(); Interact(PtInRect(&client, p) ? HitAt(p.x * s, p.y * s) : 0, pressed_);
    }
    void Schedule(Motion::Time now) { if (hooks_.schedule) hooks_.schedule(now); }
    void ScrollTo(float target, Motion::Time now) {
        target = std::clamp(target, 0.f, MaxScroll(Current(), now, true));
        scrolls_[depth_].Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::scroll);
    }
    void Reveal(int hit) {
        const auto now = Motion::Now(); const float scroll = scrolls_[depth_].Target();
        for (const auto& s : Slots(Current(), now, true)) {
            if (s.hit != hit) continue;
            // Clear of the edge fades, so a focused row never sits half dissolved.
            const float top = Layout::viewTop + (scroll > 0 ? Layout::topFade / 2 : 0), bottom = ViewBottom(Current()) - Layout::bottomFade;
            if (s.top < top) ScrollTo(scroll - (top - s.top), now);
            else if (s.top + s.height > bottom) ScrollTo(scroll + (s.top + s.height - bottom), now);
            return;
        }
    }
    // Modal work (folder picker, confirmation, choice menus) must not close the drawer it came from.
    template<class F> void Modal(F f) { modal_ = true; f(); modal_ = false; if (visible_ && !closing_) { Activated(); Changed(); } }
    // A value row's choices, right-aligned under it (screen pixels).
    int Choose(const std::vector<ContextMenu::Item>& items, int hit, bool keyboard) {
        if (!hooks_.choose) return 0;
        for (const auto& s : Slots(Current(), Motion::Now())) {
            if (s.hit != hit) continue;
            const auto row = Layout::Row(s.top, s.height);
            RECT r{Px(row.left), Px(row.top), Px(row.right), Px(row.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&r), 2);
            int chosen = 0; Modal([&] { chosen = hooks_.choose(items, r, keyboard); });
            return chosen;
        }
        return 0;
    }
    void Navigate(int depth, Motion::Time now) {
        depth = std::clamp(depth, 0, 2);
        if (depth > depth_) scrolls_[depth].Snap(0);
        // Popping back lands keyboard focus on the row that pushed the view.
        const int from = depth_; depth_ = depth; motion_.ClearInteractions();
        focus_ = depth < from && keyboard_ ? (depth == 0 ? Permissions : SiteBase + std::max(0, site_)) : 0;
        if (focus_) { const auto order = FocusOrder(); if (std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0; }
        page_.Set(static_cast<float>(depth), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::page, Timing::pageSpring);
    }
    void LoadSites() {
        if (!hooks_.loadSites) { sitesLoaded_ = true; return; }
        const unsigned ticket = ++loadTicket_;
        hooks_.loadSites([this, ticket](std::vector<BrowserSettings::Site> sites) {
            if (ticket != loadTicket_ || !hwnd_) return; // a newer load superseded this one
            sites_ = std::move(sites); sitesLoaded_ = true;
            if (depth_ == 2 && (site_ < 0 || site_ >= static_cast<int>(sites_.size()))) Navigate(1, Motion::Now());
            if (visible_) { auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0; RefreshTips(); Invalidate(); Schedule(Motion::Now()); }
        });
    }
    void Activate(int hit, bool keyboard) {
        if (!state_) return;
        const auto now = Motion::Now(); using namespace BrowserSettings;
        if (hit == Close) { Hide(keyboard, true); return; }
        if (hit == Back) { Navigate(depth_ - 1, now); }
        else if (IsEngine(hit)) {
            const auto engine = static_cast<Engine>(hit - EngineBase);
            if (state_->engine != engine) {
                state_->engine = engine;
                plate_.Set(EngineTop(static_cast<int>(engine), more_.Target()), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::select, Timing::selectSpring);
                if (hooks_.changed) hooks_.changed(Change::Engine);
            }
        } else if (hit == More) {
            const float target = more_.Target() > .5f ? 0.f : 1.f;
            if (target == 0 && !Primary(state_->engine)) return; // folding away the selected engine would hide the plate
            more_.Set(target, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold, Downloads::DrawerMotion::foldSpring);
            plate_.Set(EngineTop(SelectedIndex(), target), now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold, Downloads::DrawerMotion::foldSpring);
            ScrollTo(scrolls_[0].Target(), now);
        } else if (SwitchIndex(hit) >= 0) {
            bool* value = SwitchField(hit); if (!value) return;
            *value = !*value;
            switches_[SwitchIndex(hit)].Set(*value ? 1.f : 0.f, now, Motion::Milliseconds(0), Reduced() ? Motion::Milliseconds(0) : Timing::toggle, Timing::switchSpring);
            if (hooks_.changed) hooks_.changed(SwitchChange(hit));
            if (hit == ReduceMotion) SyncReducedMotion();
        } else if (hit == Tracking) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < TrackingCount; ++i) {
                const auto t = static_cast<BrowserSettings::Tracking>(i);
                auto item = ContextMenu::Command(i + 1, i == 2 ? std::wstring(Info(t).label) + L" · Recommandé" : Info(t).label, L"", true, state_->tracking == t);
                item.description = Info(t).description; items.push_back(item);
            }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && static_cast<BrowserSettings::Tracking>(chosen - 1) != state_->tracking) { state_->tracking = static_cast<BrowserSettings::Tracking>(chosen - 1); if (hooks_.changed) hooks_.changed(Change::Tracking); }
        } else if (hit == Sleep) {
            static const wchar_t* notes[SleepCount] = {L"Les onglets restent actifs en arrière-plan.", L"Libère le plus de mémoire.", L"Recommandé.", L"Garde vos onglets prêts plus longtemps."};
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < SleepCount; ++i) { auto item = ContextMenu::Command(i + 1, SleepLabel(SleepChoices[i]), L"", true, state_->sleepMinutes == SleepChoices[i]); item.description = notes[i]; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && SleepChoices[chosen - 1] != state_->sleepMinutes) { state_->sleepMinutes = SleepChoices[chosen - 1]; if (hooks_.changed) hooks_.changed(Change::Sleep); }
        } else if (hit == Zoom) {
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < ZoomCount; ++i) items.push_back(ContextMenu::Command(i + 1, ZoomLabel(ZoomChoices[i]), L"", true, state_->zoomPercent == ZoomChoices[i]));
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && ZoomChoices[chosen - 1] != state_->zoomPercent) { state_->zoomPercent = ZoomChoices[chosen - 1]; if (hooks_.changed) hooks_.changed(Change::Zoom); }
        } else if (hit == Permissions) { LoadSites(); Navigate(1, now); }
        else if (IsSite(hit)) { site_ = hit - SiteBase; Navigate(2, now); }
        else if (IsPermission(hit) && site_ >= 0 && site_ < static_cast<int>(sites_.size())) {
            auto& site = sites_[site_]; const int index = hit - PermissionBase;
            if (index < 0 || index >= static_cast<int>(site.permissions.size())) return;
            auto& perm = site.permissions[index];
            static const PermissionState order[] = {PermissionState::Allow, PermissionState::Ask, PermissionState::Block};
            static const wchar_t* labels[] = {L"Autoriser", L"Demander", L"Bloquer"};
            static const wchar_t* notes[] = {L"Sans vous demander.", L"Vous choisissez sur le site.", L"Jamais d'accès."};
            std::vector<ContextMenu::Item> items;
            for (int i = 0; i < 3; ++i) { auto item = ContextMenu::Command(i + 1, labels[i], L"", true, perm.state == order[i]); item.description = notes[i]; items.push_back(item); }
            const int chosen = Choose(items, hit, keyboard);
            if (chosen > 0 && order[chosen - 1] != perm.state) {
                perm.state = order[chosen - 1];
                if (hooks_.setPermission) hooks_.setPermission(site.origin, perm.kind, perm.state);
                LoadSites();
            }
        } else if (hit == ResetSite && site_ >= 0 && site_ < static_cast<int>(sites_.size())) {
            if (hooks_.resetSites) hooks_.resetSites(sites_[site_].origin);
            Navigate(1, now); LoadSites();
        } else if (hit == ResetAll && !sites_.empty()) { if (hooks_.resetSites) hooks_.resetSites(L""); sites_.clear(); LoadSites(); }
        else if (hit == Folder) { if (hooks_.pickFolder) Modal([&] { hooks_.pickFolder(); }); }
        else if (hit == ClearData) { if (hooks_.clearData) Modal([&] { hooks_.clearData(); }); }
        else if (hit == CheckUpdates) { if (hooks_.version && hooks_.version().canCheck && hooks_.checkUpdates) hooks_.checkUpdates(); Changed(); }
        Repoint(); RefreshTips(); Invalidate(); Schedule(Motion::Now());
    }
    void RefreshTips() {
        if (!tip_) return;
        for (auto id : tips_) { TOOLINFOW t{}; t.cbSize = sizeof(t); t.hwnd = hwnd_; t.uId = id; SendMessageW(tip_, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&t)); }
        tips_.clear(); tipTexts_.clear(); tipTexts_.reserve(8);
        auto add = [&](UINT_PTR id, Design::Box b, std::wstring text) {
            tipTexts_.push_back(std::move(text));
            TOOLINFOW t{}; t.cbSize = sizeof(t); t.uFlags = TTF_SUBCLASS; t.hwnd = hwnd_; t.uId = id;
            t.rect = {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}; t.lpszText = const_cast<wchar_t*>(tipTexts_.back().c_str());
            SendMessageW(tip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&t)); tips_.push_back(id);
        };
        add(Close, Layout::CloseButton(), L"Fermer (Échap)");
        const View view = Current();
        if (view != View::Main) { add(Back, Layout::BackButton(), L"Retour"); return; }
        add(CheckUpdates, Layout::Trailing(Height() - Layout::padBottom - Layout::slot, Layout::slot), L"Rechercher une mise à jour");
        for (const auto& s : Slots(view, Motion::Now())) if (s.kind == Kind::Folder && s.top >= Layout::viewTop - 1 && s.top + s.height <= ViewBottom(view))
            add(Folder, Layout::Row(s.top, s.height), hooks_.downloadFolder ? hooks_.downloadFolder() : L"Changer de dossier");
    }
    void Invalidate() { if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void Activated() { ShowWindow(hwnd_, SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(hwnd_); }
    void SyncReducedMotion() { motion_.SetReducedMotion(hooks_.reducedMotion ? hooks_.reducedMotion() : Motion::PreferredReducedMotion()); }

    // ---- Window ----------------------------------------------------------------
    RECT OwnerClient() const { RECT r{}; GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); return r; }
    // The 332 DIP column docked to the owner's right edge under the toolbar. The
    // slide moves the window itself; the region clips it to the owner's client
    // area and rounds the corner it shares with it.
    void ApplyGeometry(Motion::Time now, bool force = false) {
        if (!hwnd_) return;
        const RECT owner = OwnerClient();
        const int column = OwnerPx(Layout::width), top = owner.top + OwnerPx(Layout::top), height = std::max(1, static_cast<int>(owner.bottom - top));
        const int shown = static_cast<int>(std::lround(column * Motion::Clamp(slide_.Value(now))));
        if (!force && shown == appliedVisible_ && EqualRect(&owner, &appliedOwner_)) return;
        const bool resized = !EqualRect(&owner, &appliedOwner_);
        appliedVisible_ = shown; appliedOwner_ = owner;
        const RECT window{owner.right - shown, top, owner.right - shown + column, top + height};
        if (force || !EqualRect(&window, &appliedWindow_)) { SetWindowPos(hwnd_, nullptr, window.left, window.top, column, height, SWP_NOACTIVATE | SWP_NOZORDER); appliedWindow_ = window; }
        HRGN region = nullptr;
        if (shown > 0) {
            const int r = OwnerPx(20);
            region = IsZoomed(owner_) ? CreateRectRgn(0, 0, shown, height) : CreateRoundRectRgn(-r * 2, -r * 2, shown + 1, height + 1, r, r);
        } else region = CreateRectRgn(0, 0, 0, 0);
        SetWindowRgn(hwnd_, region, FALSE);
        if (resized) { for (int d = 0; d < 3; ++d) scrolls_[d].Snap(std::clamp(scrolls_[d].Target(), 0.f, MaxScroll(views_[d], now, true))); RefreshTips(); Invalidate(); }
    }
    void Finish() {
        closing_ = false; visible_ = false; appliedVisible_ = -1; appliedWindow_ = {};
        ShowWindow(hwnd_, SW_HIDE);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Drawer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Drawer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); PaintNow(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); Invalidate(); return 0;
        case WM_DPICHANGED: ApplyGeometry(Motion::Now(), true); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && !modal_) Hide(false, true); return 0;
        case WM_MOUSEMOVE: {
            if (!tracking_) { TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track); tracking_ = true; }
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
            if (hit != hover_) Interact(hit, pressed_);
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; Interact(0, 0); return 0;
        case WM_LBUTTONDOWN: { const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); keyboard_ = false; if (hit) { SetCapture(h); Interact(hit, hit); } return 0; }
        case WM_LBUTTONUP: {
            const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale), was = pressed_;
            if (GetCapture() == h) ReleaseCapture();
            Interact(hit, 0); if (was && was == hit) Activate(hit, false);
            return 0;
        }
        case WM_XBUTTONUP: if (GET_XBUTTON_WPARAM(w) == XBUTTON1 && depth_ > 0) { Navigate(depth_ - 1, Motion::Now()); Repoint(); RefreshTips(); Schedule(Motion::Now()); } return TRUE;
        case WM_CAPTURECHANGED: if (pressed_) Interact(hover_, 0); return 0;
        case WM_MOUSEWHEEL: { const auto now = Motion::Now(); ScrollTo(scrolls_[depth_].Target() - GET_WHEEL_DELTA_WPARAM(w) / 120.f * Layout::pitch * 1.5f, now); Repoint(); Schedule(now); return 0; }
        case WM_KEYDOWN: {
            // Escape and Backspace step back out of a sub-view; Escape on the main view closes.
            if ((w == VK_ESCAPE || w == VK_BACK || (w == VK_LEFT && GetKeyState(VK_MENU) < 0)) && depth_ > 0) { keyboard_ = true; Navigate(depth_ - 1, Motion::Now()); Interact(hover_, pressed_); RefreshTips(); return 0; }
            if (w == VK_ESCAPE) { Hide(true, true); return 0; }
            if (w == VK_TAB || w == VK_DOWN || w == VK_UP) {
                keyboard_ = true; const auto order = FocusOrder();
                const bool back = w == VK_UP || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0);
                auto it = std::find(order.begin(), order.end(), focus_);
                int index = it == order.end() ? (back ? static_cast<int>(order.size()) - 1 : 0) : static_cast<int>(it - order.begin()) + (back ? -1 : 1);
                index = (index + static_cast<int>(order.size())) % static_cast<int>(order.size());
                focus_ = order[index]; Reveal(focus_); Interact(hover_, pressed_); return 0;
            }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { keyboard_ = true; Activate(focus_, true); return 0; }
            return 0;
        }
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    // Recently closed by deactivation: the click that caused it must not reopen it.
    ULONGLONG hiddenAt = 0;
    Drawer() { slide_.Snap(0); more_.Snap(0); page_.Snap(0); }
    ~Drawer() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, BrowserSettings::State* state, Hooks hooks) {
        owner_ = owner; state_ = state; hooks_ = std::move(hooks);
        motion_.EnablePolish(); chrome_.SetMotion(&motion_); chrome_.textCacheLimit = 384;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereSettingsDrawer";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"Réglages", WS_POPUP, 0, 0, Px(Layout::width), Px(400), owner, nullptr, instance, this);
        if (!hwnd_) return false;
        tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd_, nullptr, instance, nullptr);
        const int square = 1; DwmSetWindowAttribute(hwnd_, 33, &square, sizeof(square)); // DWMWCP_DONOTROUND: the region shapes it
        const COLORREF none = 0xFFFFFFFE; DwmSetWindowAttribute(hwnd_, 34, &none, sizeof(none));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the window.
    void Dispose() { Drop(fade_); Drop(stops_); chrome_.Dispose(); hwnd_ = tip_ = nullptr; tips_.clear(); visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    bool Closing() const { return closing_; }
    bool Open() const { return visible_ && !closing_; }
    // Slides in from the window's right edge; reopening mid-exit continues from
    // where the drawer is on screen.
    void Show() {
        if (!hwnd_ || !state_) return;
        SyncReducedMotion();
        auto now = Motion::Now(); const bool reduced = Reduced();
        if (!visible_) {
            slide_.Snap(0); for (auto& lane : scrolls_) lane.Snap(0); focus_ = 0; keyboard_ = false; hover_ = pressed_ = 0; motion_.ClearInteractions();
            depth_ = 0; page_.Snap(0); site_ = -1;
            MeasureDescriptions();
            // The selected engine is always on screen: unfold when it sits behind "Voir plus…".
            more_.Snap(BrowserSettings::Primary(state_->engine) ? 0.f : 1.f);
            plate_.Snap(EngineTop(SelectedIndex(), more_.Target()));
            for (int i = 0; i < SwitchCount; ++i) { switches_[i].Snap(SwitchValue(SwitchHits[i]) ? 1.f : 0.f); knobPress_[i].Snap(0); }
            LoadSites(); // the Autorisations row shows how many sites
        }
        { const auto version = hooks_.version ? hooks_.version() : Version{}; SetChecking(version.checking, now); }
        closing_ = false; visible_ = true;
        ApplyGeometry(now, true);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PaintNow(); ValidateRect(hwnd_, nullptr); Activated();
        now = Motion::Now();
        slide_.Set(1, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Downloads::DrawerMotion::enter, Downloads::DrawerMotion::enterSpring);
        ApplyGeometry(now); Invalidate();
        Repoint(); RefreshTips(); Schedule(now);
    }
    // Leaves along the path it came in on; `animate` false hides at once.
    void Hide(bool keyboard, bool animate) {
        if (!visible_ || closing_) { if (visible_ && !animate) Finish(); return; }
        hiddenAt = GetTickCount64();
        if (GetCapture() == hwnd_) ReleaseCapture();
        motion_.ClearInteractions(); hover_ = pressed_ = 0; tracking_ = false;
        auto now = Motion::Now(); closing_ = true;
        slide_.Set(0, now, Motion::Milliseconds(0), animate && !Reduced() ? Downloads::DrawerMotion::exit : Motion::Milliseconds(0), Downloads::DrawerMotion::exitSpring);
        if (!slide_.Moving(now)) Finish();
        if (hooks_.closed) hooks_.closed(keyboard);
        if (visible_) Schedule(now);
    }
    void Toggle() { if (Open()) Hide(false, true); else Show(); }
    void Reposition() { if (visible_) ApplyGeometry(Motion::Now()); }
    // The state or the version status changed outside the drawer: follow it.
    void Changed() {
        if (!hwnd_ || !visible_ || !state_) return;
        const auto now = Motion::Now(); const bool reduced = Reduced();
        for (int i = 0; i < SwitchCount; ++i) switches_[i].Set(SwitchValue(SwitchHits[i]) ? 1.f : 0.f, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Timing::toggle, Timing::switchSpring);
        if (!BrowserSettings::Primary(state_->engine) && more_.Target() < .5f) more_.Set(1, now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Downloads::DrawerMotion::fold, Downloads::DrawerMotion::foldSpring);
        plate_.Set(EngineTop(SelectedIndex(), more_.Target()), now, Motion::Milliseconds(0), reduced ? Motion::Milliseconds(0) : Timing::select, Timing::selectSpring);
        SetChecking(hooks_.version && hooks_.version().checking, now);
        auto order = FocusOrder(); if (focus_ && std::find(order.begin(), order.end(), focus_) == order.end()) focus_ = 0;
        RefreshTips(); Invalidate(); Schedule(now);
    }
    void SetReducedMotion() { SyncReducedMotion(); Invalidate(); }
    bool Moving(Motion::Time now) const {
        bool moving = slide_.Moving(now) || more_.Moving(now) || plate_.Moving(now) || page_.Moving(now) || Spinning(now);
        for (auto& lane : scrolls_) moving = moving || lane.Moving(now);
        for (auto& lane : switches_) moving = moving || lane.Moving(now);
        for (auto& lane : knobPress_) moving = moving || lane.Moving(now);
        return moving;
    }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        return Motion::Earlier(motion_.NextWake(now), Moving(now) ? Motion::Frame() : Motion::Wake{});
    }
    // Display-paced frame from the host clock; also paints the frame after motion ends.
    void Frame(Motion::Time now) {
        if (!visible_) return;
        ApplyGeometry(now);
        if (closing_ && !slide_.Moving(now)) { Finish(); return; }
        // A window keeps only what it is painted with while its region shows it:
        // the entering slide repaints as it uncovers; the exit moves finished pixels.
        const bool content = (Moving(now) && !(closing_ && !page_.Moving(now))) || motion_.NextWake(now).delay.has_value();
        if (content || animatingPainted_) Invalidate();
        // Rows moved under a still pointer (fold, scroll, navigation): hover follows the layout.
        if (!content && animatingPainted_) { RefreshTips(); Repoint(); }
        animatingPainted_ = content;
    }
};
} // namespace SettingsUI
} // namespace NativeChrome
