#pragma once
#include "i18n.h"
// Profile menu (Paper p-I-0, P01 « Menu des profils » XMC-0 and « Aperçu des onglets » YTO-0).
// Opened by the profile button that replaced the brand flower (chrome region 900), by
// Ctrl+Maj+M, and by Entrée / Espace / ↓ on the focused button.
//
//   menu 248 wide · canvas · 1 px #FFFFFF0F · radius 12 · padding 4
//   rows 32 · radius 8 · padding 8 · gap 8 · 14/20 Regular #FFFFFFE6
//   profile rows: lanes 16 glyph (Fill, ink) / name (ellipsis) / 36 windows / 16 check
//   hover or keyboard focus: the row's profile ink at 10 % (other rows: white 8 %)
//   preview 288 wide, rows 28, favicon 16 r4 (else initial tile #3E3E3E), at most 6 tabs
//   then « + n autres onglets · m autres fenêtres » 13/16 #FFFFFF80
//   preview opens 400 ms after a row is hovered, beside it; closes 300 ms after the pointer
//   leaves; a safe corridor (triangle pointer → preview) keeps it while travelling there.
//
// Track() is modal like ContextMenu::Menu::Track: it runs its own loop and returns the choice.
// Clicking a profile row always switches profile (it never just opens the preview); → enters
// the preview, Entrée there opens that tab in its window; ← or Échap returns to the row.
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "tooltip.h"        // PopupKit (text, fills, glyph cache, offscreen render)
#include "profile-icons.h"

namespace NativeChrome {
namespace ProfileMenu {

struct Tab {
    std::wstring title, site;                          // site: host for the favicon / initial tile
    std::shared_ptr<const FaviconImage> icon;          // nullptr: initial tile
    std::wstring session;                              // window session id that owns the tab
    int index = -1;                                    // tab index inside that window
};
struct Profile {
    std::wstring id, name, icon;                       // icon: ProfileIcons key
    D2D1_COLOR_F ink{1, 1, 1, 1};                      // OKLCH L .78 profile colour
    int windows = 0;                                   // open (non-private) windows
    bool current = false;                              // this window's profile
    std::vector<Tab> tabs;                             // grouped by window, most recent window first
};
// `privateWindow`: the menu of a private window — tab list and « Nouvelle fenêtre privée » only (no profile
// rows: a private window belongs to no profile and never reads or writes the registry).
struct Model { int tabCount = 0; std::vector<Profile> profiles; bool privateWindow = false; };

enum class Action { None, TabList, Switch, Create, Private, Manage, OpenTab, RowMenu, NewWindow };
// RowMenu: right-click / Maj+F10 / Menu key on a profile row — its context menu (« Ouvrir dans une nouvelle
// fenêtre »), never the switch; `at` is where it opens (screen pixels). NewWindow: that context action
// invoked directly (UI Automation: each profile row's « Ouvrir dans une nouvelle fenêtre » child button).
struct Choice { Action action = Action::None; std::wstring profile, session; int tab = -1; bool keyboard = false; POINT at{}; };

namespace Layout {
inline constexpr float width = 248, previewWidth = 288, pad = 4, row = 32, previewRow = 28, separator = 9;
inline constexpr float radius = 12, rowRadius = 8, padX = 8, gap = 8, glyph = 16, countLane = 36, checkLane = 16, countIcon = 14, countGap = 4;
inline constexpr float tile = 16, tileRadius = 4, previewGap = 4;
inline constexpr int maxPreview = 6;
inline constexpr UINT openDelayMs = 400, closeDelayMs = 300;
// Paper P05: menu in opacity + 6 DIP down 180 ms, out the same path 150 ms; preview in/out opacity + 4 DIP
// from its edge 150 ms; reduced motion: opacity only, 100 ms. Re-showing mid-exit resumes from what is on screen.
inline constexpr float enterMs = 180, exitMs = 150, previewMs = 150, reducedMs = 100, enterSlide = 6, previewSlide = 4;
inline constexpr float tint = .10f, neutralHover = .08f, focusRing = .5f;
inline constexpr unsigned border = 0x0F;
}

// The French plural used by the windows lane and the accessible row name.
inline std::wstring WindowsPhrase(int n) { return TRP(profile_open_windows, n); }
inline std::wstring RowName(const Profile& p) {
    std::wstring s = p.current ? TRF(settings_profile_this_window, p.name) : p.name;
    return TRF(a11y_status_and_prefix, s, p.windows > 0 ? WindowsPhrase(p.windows) : std::wstring(TR(profile_no_window)));
}
// « + 8 autres onglets · 2 autres fenêtres » (only the parts that are hidden).
inline std::wstring OverflowLine(const Profile& p, int shown) {
    const int hidden = std::max(0, static_cast<int>(p.tabs.size()) - shown);
    std::vector<std::wstring> seen; for (int i = 0; i < shown && i < static_cast<int>(p.tabs.size()); ++i) if (std::find(seen.begin(), seen.end(), p.tabs[i].session) == seen.end()) seen.push_back(p.tabs[i].session);
    const int otherWindows = std::max(0, p.windows - static_cast<int>(seen.size()));
    std::wstring s;
    if (hidden > 0) s = TRP(profile_more_tabs, hidden);
    if (hidden > 0 && otherWindows > 0) s += L" · " + TRP(profile_other_windows, otherWindows);
    return s;
}
inline std::wstring Initial(const Tab& t) {
    std::wstring s = !t.site.empty() ? t.site : t.title;
    if (s.rfind(L"www.", 0) == 0) s = s.substr(4);
    for (wchar_t c : s) if (iswalnum(c)) return std::wstring(1, static_cast<wchar_t>(towupper(c)));
    return L"·";
}

class Menu {
public:
    // Row kinds of the menu, in order (built from the model).
    enum class Kind { TabList, Separator, Profile, Create, Private, Manage };
    struct Row { Kind kind; int profile = -1; float top = 0, height = 0; };
private:
    struct Surface {
        HWND hwnd = nullptr; Chrome chrome; Motion::State motion; Accessibility::Bridge uia;
        float width = 0, height = 0; UINT dpi = 96; int hover = -1, pressed = -1; bool keyboard = false;
        Motion::Time opened{}, left{}; bool entering = false, leaving = false; float enterFrom = 0, leaveFrom = 1; POINT final{}; int fromX = 0;
    };
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_;
    Surface menu_, preview_; const Model* model_ = nullptr; std::vector<Row> rows_;
    PopupKit::GlyphCache glyphs_;
    bool done_ = false, reduced_ = false, keyed_ = false, previewShown_ = false, active_ = false; Choice choice_;
    int previewRow_ = -1, pendingRow_ = -1;       // row whose tabs the preview shows / will show
    POINT lastPointer_{}; bool corridor_ = false;
    static constexpr UINT InvokeMessage = WM_APP + 41;
    static constexpr UINT_PTR FrameTimer = 1, OpenTimer = 2, CloseTimer = 3;

    float Scale(const Surface& s) const { return s.dpi / 96.f; }
    int Px(const Surface& s, float dip) const { return static_cast<int>(std::lround(dip * Scale(s))); }
    const Profile* ProfileAt(int row) const { return row >= 0 && row < static_cast<int>(rows_.size()) && rows_[row].kind == Kind::Profile && model_ ? &model_->profiles[rows_[row].profile] : nullptr; }
    static bool Selectable(const Row& r) { return r.kind != Kind::Separator; }
    int PreviewCount() const { const Profile* p = ProfileAt(previewRow_); return p ? std::min<int>(Layout::maxPreview, static_cast<int>(p->tabs.size())) : 0; }
    bool PreviewHasOverflow() const { const Profile* p = ProfileAt(previewRow_); return p && !OverflowLine(*p, PreviewCount()).empty(); }

    void Build() {
        rows_.clear(); float y = Layout::pad + 1;
        auto add = [&](Kind k, int profile = -1) { Row r{k, profile, y, k == Kind::Separator ? Layout::separator : Layout::row}; y += r.height; rows_.push_back(r); };
        add(Kind::TabList); add(Kind::Separator);
        if (model_->privateWindow) { add(Kind::Private); menu_.width = Layout::width; menu_.height = y + Layout::pad + 1; return; }
        for (int i = 0; i < static_cast<int>(model_->profiles.size()); ++i) add(Kind::Profile, i);
        // « Nouvelle fenêtre privée »: next to « Nouveau profil… », apart from the identity rows (no profile,
        // no registry record, nothing restored: the existing --private window).
        add(Kind::Separator); add(Kind::Create); add(Kind::Private); add(Kind::Manage);
        menu_.width = Layout::width; menu_.height = y + Layout::pad + 1;
    }
    std::wstring Label(const Row& r) const {
        switch (r.kind) {
        case Kind::TabList: return TRF(profile_menu_tab_list, std::to_wstring(model_ ? model_->tabCount : 0));
        case Kind::Profile: return model_->profiles[r.profile].name;
        case Kind::Create: return TR(profile_menu_new);
        case Kind::Private: return TR(a11y_new_private_window);
        case Kind::Manage: return TR(profile_menu_manage);
        default: return L"";
        }
    }

    // ---- windows ------------------------------------------------------------------------
    void Create(Surface& s) {
        if (s.hwnd) return;
        s.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED, L"LisiereProfileMenu", L"", WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, this);
        if (!s.hwnd) return;
        SetLayeredWindowAttributes(s.hwnd, 0, 255, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(s.hwnd, Layout::radius);
        s.motion.EnablePolish(); s.chrome.SetMotion(&s.motion); s.chrome.textCacheLimit = 128; s.chrome.Init(s.hwnd, root_);
        Attach(s);
    }
    static RECT WorkArea(POINT p) { MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &info); return info.rcWork; }
    void Show(Surface& s, int x, int y, bool activate, int fromX) {
        s.dpi = GetDpiForWindow(owner_); s.motion.SetReducedMotion(reduced_); s.motion.ClearInteractions();
        const int w = Px(s, s.width), h = Px(s, s.height);
        const auto now0 = Motion::Now(); const float shown = s.leaving && IsWindowVisible(s.hwnd) ? Presence(s, now0) : 0.f;
        s.final = {x, y}; s.fromX = fromX; s.leaving = false; s.entering = true; s.enterFrom = shown; s.opened = now0;
        SetWindowPos(s.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        Step(s, s.opened); s.chrome.Resize();
        ShowWindow(s.hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
        Paint(s); ValidateRect(s.hwnd, nullptr);
        SetTimer(s.hwnd, FrameTimer, 10, nullptr);
        if (activate) { SetForegroundWindow(s.hwnd); SetFocus(s.hwnd); }
        if (s.uia.Listening()) { s.uia.ChildrenChanged(MenuKey); s.uia.Raise(MenuKey, UIA_MenuOpenedEventId); }
    }
    float Ms(Motion::Time a, Motion::Time b) const { return static_cast<float>(std::chrono::duration<double, std::milli>(b - a).count()); }
    float InMs(const Surface& s) const { return reduced_ ? Layout::reducedMs : &s == &preview_ ? Layout::previewMs : Layout::enterMs; }
    float OutMs(const Surface& s) const { return reduced_ ? Layout::reducedMs : &s == &preview_ ? Layout::previewMs : Layout::exitMs; }
    // 0 = gone, 1 = at rest: in on ease-out cubic from what was shown, out on the mirrored curve from what is shown.
    float Presence(const Surface& s, Motion::Time now) const {
        if (s.leaving) { const float u = Motion::Clamp(Ms(s.left, now) / OutMs(s)); return s.leaveFrom * (1 - u * u * u); }
        if (s.entering) { const float u = Motion::Clamp(Ms(s.opened, now) / InMs(s)); const float e = 1 - (1 - u) * (1 - u) * (1 - u); return s.enterFrom + (1 - s.enterFrom) * e; }
        return 1;
    }
    void Step(Surface& s, Motion::Time now) {
        const float p = Presence(s, now);
        if (s.entering && Ms(s.opened, now) >= InMs(s)) s.entering = false;
        SetLayeredWindowAttributes(s.hwnd, 0, static_cast<BYTE>(std::lround(255 * p)), LWA_ALPHA);
        const float slide = reduced_ ? 0.f : &s == &preview_ ? Layout::previewSlide : Layout::enterSlide;
        const int offset = Px(s, slide * (1 - p));
        SetWindowPos(s.hwnd, nullptr, s.final.x + s.fromX * offset, s.final.y + (s.fromX ? 0 : -offset), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (s.leaving && Ms(s.left, now) >= OutMs(s)) { s.leaving = false; KillTimer(s.hwnd, FrameTimer); ShowWindow(s.hwnd, SW_HIDE); }
    }
    bool Leaving() const { return menu_.leaving || preview_.leaving; }
    // Leaves along the path it came in on (P05); `animate` false hides at once.
    void Leave(Surface& s, bool animate) {
        if (!s.hwnd) return; KillTimer(s.hwnd, OpenTimer); KillTimer(s.hwnd, CloseTimer);
        if (s.uia.Listening() && IsWindowVisible(s.hwnd)) s.uia.Raise(MenuKey, UIA_MenuClosedEventId);
        s.hover = s.pressed = -1;
        if (!animate || !IsWindowVisible(s.hwnd)) { KillTimer(s.hwnd, FrameTimer); ShowWindow(s.hwnd, SW_HIDE); s.entering = s.leaving = false; return; }
        const auto now = Motion::Now(); s.leaveFrom = Presence(s, now); s.entering = false; s.leaving = true; s.left = now; SetTimer(s.hwnd, FrameTimer, 10, nullptr);
    }
    void Hide(Surface& s) { Leave(s, false); }

    // ---- preview -----------------------------------------------------------------------
    void MeasurePreview() {
        const int n = PreviewCount(); preview_.width = Layout::previewWidth;
        preview_.height = 2 * (Layout::pad + 1) + (n + (PreviewHasOverflow() ? 1 : 0)) * Layout::previewRow;
    }
    void OpenPreview(int row, bool keyboard) {
        KillTimer(menu_.hwnd, OpenTimer); KillTimer(menu_.hwnd, CloseTimer); pendingRow_ = -1;
        const Profile* p = ProfileAt(row);
        if (!p || p->tabs.empty()) { ClosePreview(); return; }        // « Sans fenêtre · Sans aperçu »
        if (previewShown_ && previewRow_ == row) return;
        Create(preview_); if (!preview_.hwnd) return;
        previewRow_ = row; preview_.hover = -1; preview_.keyboard = keyboard; MeasurePreview();
        preview_.dpi = menu_.dpi;
        RECT r{menu_.final.x, menu_.final.y, menu_.final.x + Px(menu_, menu_.width), menu_.final.y + Px(menu_, menu_.height)};
        const RECT work = WorkArea({r.right, r.top});
        const int w = Px(preview_, preview_.width), h = Px(preview_, preview_.height), g = Px(menu_, Layout::previewGap);
        int x = r.right + g, fromX = -1;
        if (x + w > work.right) { x = r.left - g - w; fromX = 1; }
        int y = r.top + Px(menu_, rows_[row].top) - Px(menu_, Layout::pad + 1);
        y = std::max<int>(work.top, std::min<int>(y, work.bottom - h));
        Show(preview_, std::max<int>(x, work.left), y, false, fromX); previewShown_ = true;
        Invalidate(menu_);
    }
    void ClosePreview() {
        KillTimer(menu_.hwnd, OpenTimer); KillTimer(menu_.hwnd, CloseTimer); pendingRow_ = -1;
        if (!previewShown_) return; Leave(preview_, true); previewShown_ = false; previewRow_ = -1; corridor_ = false; Invalidate(menu_);
    }
    // The pointer is travelling from the menu towards the preview: keep the preview.
    bool InCorridor(POINT p) const {
        if (!previewShown_) return false;
        RECT pr; GetWindowRect(preview_.hwnd, &pr);
        const bool right = pr.left >= lastPointer_.x;
        const POINT a = lastPointer_, b{right ? pr.left : pr.right, pr.top}, c{right ? pr.left : pr.right, pr.bottom};
        auto side = [](POINT p1, POINT p2, POINT p3) { return (p1.x - p3.x) * static_cast<long long>(p2.y - p3.y) - (p2.x - p3.x) * static_cast<long long>(p1.y - p3.y); };
        const long long d1 = side(p, a, b), d2 = side(p, b, c), d3 = side(p, c, a);
        const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        return !(neg && pos);
    }

    // ---- interaction ------------------------------------------------------------------------
    void Invalidate(Surface& s) { if (s.hwnd) { InvalidateRect(s.hwnd, nullptr, FALSE); if (s.motion.NextWake(Motion::Now()).delay) SetTimer(s.hwnd, FrameTimer, 10, nullptr); } }
    void Interact(Surface& s) { s.motion.SetInteraction(s.hover + 1, s.pressed >= 0 && s.pressed == s.hover ? s.pressed + 1 : 0, Motion::Now()); Invalidate(s); }
    void SetHover(int i, bool keyboard) {
        menu_.keyboard = keyboard; if (menu_.hover == i) return;
        menu_.hover = i; Interact(menu_);
        if (keyboard && i >= 0 && menu_.uia.Listening()) menu_.uia.Raise(RowKey(i), UIA_AutomationFocusChangedEventId);
        KillTimer(menu_.hwnd, OpenTimer);
        if (keyboard) { ClosePreview(); return; }                    // → opens it from the keyboard
        if (ProfileAt(i) && !ProfileAt(i)->tabs.empty() && i != previewRow_) { pendingRow_ = i; SetTimer(menu_.hwnd, OpenTimer, Layout::openDelayMs, nullptr); }
        if (previewShown_ && i != previewRow_) SetTimer(menu_.hwnd, CloseTimer, Layout::closeDelayMs, nullptr);
        else KillTimer(menu_.hwnd, CloseTimer);
    }
    void SetPreviewHover(int i, bool keyboard) {
        preview_.keyboard = keyboard; if (preview_.hover == i) return; preview_.hover = i; Interact(preview_);
        if (keyboard && i >= 0 && preview_.uia.Listening()) preview_.uia.Raise(RowKey(i), UIA_AutomationFocusChangedEventId);
    }
    int HitRow(float y) const { for (size_t i = 0; i < rows_.size(); ++i) if (y >= rows_[i].top && y < rows_[i].top + rows_[i].height) return Selectable(rows_[i]) ? static_cast<int>(i) : -1; return -1; }
    int HitPreview(float y) const { const int i = static_cast<int>(std::floor((y - Layout::pad - 1) / Layout::previewRow)); return i >= 0 && i < PreviewCount() ? i : -1; }
    void Move(int step, bool fromStart = false) {
        const int n = static_cast<int>(rows_.size()); int i = fromStart ? (step > 0 ? -1 : n) : menu_.hover;
        for (int k = 0; k < n; ++k) { i = (i + step + n) % n; if (Selectable(rows_[i])) { SetHover(i, true); return; } }
    }
    void MovePreview(int step, bool fromStart = false) {
        const int n = PreviewCount(); if (!n) return; int i = fromStart ? (step > 0 ? 0 : n - 1) : (preview_.hover < 0 ? 0 : (preview_.hover + step + n) % n);
        SetPreviewHover(i, true);
    }
    void Finish(Choice c) { choice_ = c; choice_.keyboard = keyed_ && c.action != Action::None; done_ = true; PostMessageW(owner_, WM_NULL, 0, 0); }
    void Activate(int i) {
        if (i < 0 || i >= static_cast<int>(rows_.size())) return; const Row& r = rows_[i]; Choice c;
        switch (r.kind) {
        case Kind::TabList: c.action = Action::TabList; break;
        case Kind::Profile: c.action = Action::Switch; c.profile = model_->profiles[r.profile].id; break;
        case Kind::Create: c.action = Action::Create; break;
        case Kind::Private: c.action = Action::Private; break;
        case Kind::Manage: c.action = Action::Manage; break;
        default: return;
        }
        Finish(c);
    }
    void RowMenu(int i, POINT at) {
        if (i < 0 || i >= static_cast<int>(rows_.size()) || rows_[i].kind != Kind::Profile) return;
        Choice c; c.action = Action::RowMenu; c.profile = model_->profiles[rows_[i].profile].id; c.at = at; Finish(c);
    }
    void NewWindowRow(int i) {
        if (i < 0 || i >= static_cast<int>(rows_.size()) || rows_[i].kind != Kind::Profile) return;
        Choice c; c.action = Action::NewWindow; c.profile = model_->profiles[rows_[i].profile].id; c.keyboard = true; Finish(c);
    }
    POINT RowAnchor(int i) const { RECT r{}; GetWindowRect(menu_.hwnd, &r); const float k = menu_.dpi / 96.f; return {r.left + static_cast<LONG>((Layout::pad + Layout::padX) * k), r.top + static_cast<LONG>((rows_[i].top + rows_[i].height) * k)}; }
    void ActivateTab(int i) {
        const Profile* p = ProfileAt(previewRow_); if (!p || i < 0 || i >= PreviewCount()) return;
        Choice c; c.action = Action::OpenTab; c.profile = p->id; c.session = p->tabs[i].session; c.tab = p->tabs[i].index; Finish(c);
    }
    bool Key(WPARAM key) {
        keyed_ = true;
        const bool inPreview = previewShown_ && preview_.keyboard && preview_.hover >= 0;
        if (inPreview) {
            switch (key) {
            case VK_DOWN: MovePreview(1); return true;
            case VK_UP: MovePreview(-1); return true;
            case VK_HOME: MovePreview(1, true); return true;
            case VK_END: MovePreview(-1, true); return true;
            case VK_RETURN: case VK_SPACE: ActivateTab(preview_.hover); return true;
            case VK_LEFT: case VK_ESCAPE: { const int row = previewRow_; ClosePreview(); SetHover(-1, true); SetHover(row, true); return true; }
            case VK_TAB: case VK_MENU: case VK_F10: Finish({}); return true;
            default: return true;
            }
        }
        switch (key) {
        case VK_DOWN: Move(1); return true;
        case VK_UP: Move(-1); return true;
        case VK_HOME: case VK_PRIOR: Move(1, true); return true;
        case VK_END: case VK_NEXT: Move(-1, true); return true;
        case VK_TAB: Move(GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return true;
        case VK_RIGHT: if (const Profile* p = ProfileAt(menu_.hover)) if (!p->tabs.empty()) { OpenPreview(menu_.hover, true); if (previewShown_) MovePreview(1, true); } return true;
        case VK_RETURN: case VK_SPACE: if (menu_.hover >= 0) Activate(menu_.hover); return true;
        case VK_APPS: if (ProfileAt(menu_.hover)) RowMenu(menu_.hover, RowAnchor(menu_.hover)); return true;
        case VK_F10: if (GetKeyState(VK_SHIFT) < 0 && ProfileAt(menu_.hover)) { RowMenu(menu_.hover, RowAnchor(menu_.hover)); return true; } Finish({}); return true;
        case VK_ESCAPE: case VK_MENU: Finish({}); return true;
        default: return true;
        }
    }

    // ---- paint ---------------------------------------------------------------------------
    void Paint(Surface& s) {
        if (!s.chrome.RenderTarget()) s.chrome.Resize();
        auto* t = s.chrome.RenderTarget(); if (!t || !s.chrome.SolidBrush() || !model_) return;
        const auto now = Motion::Now();
        t->BeginDraw(); if (&s == &menu_) PaintMenu(s.chrome, now); else PaintPreview(s.chrome, now);
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) s.chrome.RecreateTarget();
        if (!s.motion.NextWake(now).delay && !s.entering && !s.leaving) KillTimer(s.hwnd, FrameTimer);
    }
    void Glyph(Chrome& c, const char* path, D2D1_RECT_F box, float size, D2D1_COLOR_F color) { glyphs_.Draw(c, path, box, size, color); }
    void PaintMenu(Chrome& c, Motion::Time now) {
        auto* t = c.RenderTarget(); const float k = Design::Surface::canvas; t->Clear(D2D1::ColorF(k, k, k, 1));
        c.keyboardModality = menu_.keyboard;
        const float left = Layout::pad + 1, right = menu_.width - Layout::pad - 1;
        PopupKit::Type label{14, 20};
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& r = rows_[i];
            if (r.kind == Kind::Separator) { PopupKit::Fill(c, {left + Layout::padX, r.top + 4, right - Layout::padX, r.top + 5}, 0, PopupKit::White(Layout::border / 255.f)); continue; }
            const D2D1_RECT_F box{left, r.top, right, r.top + r.height};
            const Profile* p = r.kind == Kind::Profile ? &model_->profiles[r.profile] : nullptr;
            const auto v = menu_.motion.Visual(static_cast<int>(i) + 1, now);
            // Hover / keyboard focus / the row whose preview is open: profile ink at 10 % (white 8 % elsewhere).
            const float h = std::max(v.hover, previewShown_ && previewRow_ == static_cast<int>(i) ? 1.f : 0.f);
            if (h > 0 || v.press > 0) {
                D2D1_COLOR_F fill = p ? D2D1::ColorF(p->ink.r, p->ink.g, p->ink.b, Layout::tint * h + .06f * v.press) : PopupKit::White(Layout::neutralHover * h + .04f * v.press);
                PopupKit::Fill(c, box, Layout::rowRadius, fill);
            }
            if (menu_.keyboard && static_cast<int>(i) == menu_.hover && !(previewShown_ && preview_.keyboard)) {
                c.SolidBrush()->SetColor(PopupKit::White(Layout::focusRing));
                t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .5f, box.top + .5f, box.right - .5f, box.bottom - .5f}, Layout::rowRadius - .5f, Layout::rowRadius - .5f), c.SolidBrush(), 1);
            }
            const float cy = (box.top + box.bottom) / 2; float x = box.left + Layout::padX;
            const D2D1_RECT_F gbox{x, cy - Layout::glyph / 2, x + Layout::glyph, cy + Layout::glyph / 2};
            const char* path = r.kind == Kind::TabList ? ProfileIcons::Bold::tabs : r.kind == Kind::Create ? ProfileIcons::Bold::plus : r.kind == Kind::Private ? Design::PrivateChip::eyeSlash : r.kind == Kind::Manage ? ProfileIcons::Bold::gear : ProfileIcons::Fill(p->icon.c_str());
            Glyph(c, path, gbox, Layout::glyph, p ? D2D1::ColorF(p->ink.r, p->ink.g, p->ink.b, 1) : PopupKit::White(.7f));
            x = gbox.right + Layout::gap;
            float tail = box.right - Layout::padX;
            if (p) {
                const D2D1_RECT_F check{tail - Layout::checkLane, cy - 8, tail, cy + 8};
                if (p->current) Glyph(c, ContextGlyph::check, check, 14, PopupKit::White(.75f));
                tail = check.left - Layout::gap;
                const D2D1_RECT_F lane{tail - Layout::countLane, cy - 8, tail, cy + 8};
                if (p->windows > 0) {
                    PopupKit::Type count{13, 16, 0, true, true};
                    const std::wstring n = std::to_wstring(p->windows);
                    const float nw = PopupKit::Measure(c, n, 100, count, false).width;
                    PopupKit::DrawText(c, n, {lane.right - nw, cy - 8, lane.right, cy + 8}, count, PopupKit::White(.6f));
                    const float ix = lane.right - nw - Layout::countGap - Layout::countIcon;
                    Glyph(c, ProfileIcons::Bold::browser, {ix, cy - 7, ix + Layout::countIcon, cy + 7}, Layout::countIcon, PopupKit::White(.7f));
                }
                tail = lane.left - Layout::gap;
            }
            PopupKit::DrawText(c, Label(r), {x, cy - 10, tail, cy + 10}, label, PopupKit::White(.9f + .1f * h));
        }
        PopupKit::Outline(c, menu_.width, menu_.height, Layout::radius, PopupKit::White(Layout::border / 255.f));
    }
    void PaintPreview(Chrome& c, Motion::Time now) {
        auto* t = c.RenderTarget(); const float k = Design::Surface::canvas; t->Clear(D2D1::ColorF(k, k, k, 1));
        const Profile* p = ProfileAt(previewRow_); if (!p) return;
        const float left = Layout::pad + 1, right = preview_.width - Layout::pad - 1; const int n = PreviewCount();
        PopupKit::Type title{14, 18};
        for (int i = 0; i < n; ++i) {
            const Tab& tab = p->tabs[i]; const float top = Layout::pad + 1 + i * Layout::previewRow;
            const D2D1_RECT_F box{left, top, right, top + Layout::previewRow};
            const auto v = preview_.motion.Visual(i + 1, now);
            if (v.hover > 0) PopupKit::Fill(c, box, Layout::rowRadius, PopupKit::White(.08f * v.hover + .04f * v.press));
            if (preview_.keyboard && i == preview_.hover) {
                c.SolidBrush()->SetColor(PopupKit::White(Layout::focusRing));
                t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .5f, box.top + .5f, box.right - .5f, box.bottom - .5f}, Layout::rowRadius - .5f, Layout::rowRadius - .5f), c.SolidBrush(), 1);
            }
            const float cy = (box.top + box.bottom) / 2, x = box.left + Layout::padX;
            const D2D1_RECT_F icon{x, cy - Layout::tile / 2, x + Layout::tile, cy + Layout::tile / 2};
            if (!(tab.icon && c.DrawSiteImage(tab.icon, icon))) {
                PopupKit::Fill(c, icon, Layout::tileRadius, PopupKit::Gray(0x3E));
                PopupKit::Type letter{11, 12, 0, true};
                PopupKit::DrawText(c, Initial(tab), {icon.left, cy - 6, icon.right, cy + 6}, letter, PopupKit::White(.8f), false, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            PopupKit::DrawText(c, tab.title.empty() ? tab.site : tab.title, {icon.right + Layout::gap, cy - 9, box.right - Layout::padX, cy + 9}, title, PopupKit::White(.9f));
        }
        const std::wstring more = OverflowLine(*p, n);
        if (!more.empty()) {
            const float top = Layout::pad + 1 + n * Layout::previewRow, cy = top + Layout::previewRow / 2;
            PopupKit::DrawText(c, more, {left + Layout::padX, cy - 8, right - Layout::padX, cy + 8}, PopupKit::Type{13, 16, 0, false, true}, PopupKit::White(.5f));
        }
        PopupKit::Outline(c, preview_.width, preview_.height, Layout::radius, PopupKit::White(Layout::border / 255.f));
    }
    // Bold check from the shared vocabulary (Paper XPA-0 = Phosphor check bold).
    struct ContextGlyph { static constexpr const char* check = "M232.49,80.49l-128,128a12,12,0,0,1-17,0l-56-56a12,12,0,1,1,17-17L96,183,215.51,63.51a12,12,0,0,1,17,17Z"; };

    // ---- window procedure ----------------------------------------------------------------------
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM lp) {
        auto* self = reinterpret_cast<Menu*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self ? self->Handle(h, msg, w, lp) : DefWindowProcW(h, msg, w, lp);
    }
    Surface* Of(HWND h) { return h == menu_.hwnd ? &menu_ : h == preview_.hwnd ? &preview_ : nullptr; }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM lp) {
        Surface* s = Of(h); const bool isMenu = s == &menu_;
        switch (msg) {
        case WM_GETOBJECT: if (s) { LRESULT result; if (s->uia.OnGetObject(h, w, lp, result)) return result; } break;
        case InvokeMessage: keyed_ = true; if (isMenu) { if (lp == 1) OpenPreview(static_cast<int>(w), true); else if (lp == 2) ClosePreview(); else if (lp == 3) NewWindowRow(static_cast<int>(w)); else Activate(static_cast<int>(w)); } else if (s) ActivateTab(static_cast<int>(w)); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); if (s) Paint(*s); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        // Another window took activation (Alt+Tab, a click elsewhere): dismiss — once the menu has been active
        // (opened from UI Automation while the browser is in the background, it never was: a press outside still closes it).
        case WM_ACTIVATE: if (LOWORD(w) != WA_INACTIVE) active_ = true; else if (active_ && !done_ && !Of(reinterpret_cast<HWND>(lp))) Finish({}); return 0;
        case WM_TIMER:
            if (!s) return 0;
            if (w == FrameTimer) { if (s->entering || s->leaving) Step(*s, Motion::Now()); if (IsWindowVisible(h)) InvalidateRect(h, nullptr, FALSE); return 0; }
            if (w == OpenTimer) { KillTimer(h, OpenTimer); if (pendingRow_ >= 0 && pendingRow_ == menu_.hover) OpenPreview(pendingRow_, false); return 0; }
            if (w == CloseTimer) { KillTimer(h, CloseTimer); if (preview_.hover < 0 && menu_.hover != previewRow_) { ClosePreview(); if (menu_.hover >= 0) { const int row = menu_.hover; menu_.hover = -1; SetHover(row, false); } } return 0; }
            return 0;
        case WM_MOUSEMOVE: {
            if (!s || ignorePointer) return 0;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track);
            POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ClientToScreen(h, &screen);
            keyed_ = false;
            if (isMenu) {
                const int row = HitRow(GET_Y_LPARAM(lp) / Scale(menu_));
                // Safe corridor: while heading for the open preview, other rows do not steal it.
                if (previewShown_ && row != previewRow_ && InCorridor(screen)) { corridor_ = true; SetTimer(menu_.hwnd, CloseTimer, Layout::closeDelayMs, nullptr); lastPointer_ = screen; return 0; }
                corridor_ = false; lastPointer_ = screen; SetHover(row, false);
            } else { KillTimer(menu_.hwnd, CloseTimer); SetPreviewHover(HitPreview(GET_Y_LPARAM(lp) / Scale(preview_)), false); }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (!s || ignorePointer) return 0;
            if (isMenu) { if (!menu_.keyboard && !corridor_) SetHover(-1, false); if (previewShown_) SetTimer(menu_.hwnd, CloseTimer, Layout::closeDelayMs, nullptr); }
            else { SetPreviewHover(-1, false); SetTimer(menu_.hwnd, CloseTimer, Layout::closeDelayMs, nullptr); }
            return 0;
        case WM_RBUTTONUP: if (s && isMenu) { keyed_ = false; const int row = HitRow(GET_Y_LPARAM(lp) / Scale(*s)); POINT at{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ClientToScreen(h, &at); RowMenu(row, at); } return 0;
        case WM_LBUTTONDOWN: if (s) { s->pressed = isMenu ? HitRow(GET_Y_LPARAM(lp) / Scale(*s)) : HitPreview(GET_Y_LPARAM(lp) / Scale(*s)); Interact(*s); } return 0;
        case WM_LBUTTONUP: {
            if (!s) return 0;
            const int row = isMenu ? HitRow(GET_Y_LPARAM(lp) / Scale(*s)) : HitPreview(GET_Y_LPARAM(lp) / Scale(*s)); s->pressed = -1; Interact(*s);
            keyed_ = false; if (row >= 0) { if (isMenu) Activate(row); else ActivateTab(row); }
            return 0;
        }
        }
        return DefWindowProcW(h, msg, w, lp);
    }

    // ---- UI Automation -------------------------------------------------------------------------
public:   // node keys (tests read the tree)
    static constexpr long long MenuKey = 1;
    static long long RowKey(int i) { return 100 + i; }
    static constexpr long long NewWindowBase = 10000;   // a profile row's context action, child of RowKey(i)
    static long long NewWindowKey(int i) { return NewWindowBase + i; }
private:
    void Attach(Surface& s) {
        Accessibility::Host host; host.window = s.hwnd; Surface* sp = &s;
        host.nodes = [this, sp] { return sp == &menu_ ? MenuNodes() : PreviewNodes(); };
        host.focused = [this, sp]() -> long long { if (!IsWindowVisible(sp->hwnd)) return 0; if (sp == &menu_) return !(previewShown_ && preview_.keyboard) && menu_.hover >= 0 ? RowKey(menu_.hover) : 0; return preview_.keyboard && preview_.hover >= 0 ? RowKey(preview_.hover) : 0; };
        host.invoke = [this, sp](long long key) {
            if (sp == &menu_ && key >= NewWindowBase) PostMessageW(sp->hwnd, InvokeMessage, static_cast<WPARAM>(key - NewWindowBase), 3);
            else if (key >= 100 && key < NewWindowBase) PostMessageW(sp->hwnd, InvokeMessage, static_cast<WPARAM>(key - 100), 0); };
        host.expand = [this, sp](long long key, bool open) { if (sp == &menu_ && key >= 100 && key < NewWindowBase) PostMessageW(sp->hwnd, InvokeMessage, static_cast<WPARAM>(key - 100), open ? 1 : 2); };
        host.focus = [this, sp](long long key) { if (key < 100 || key >= NewWindowBase) return; if (sp == &menu_) SetHover(static_cast<int>(key - 100), true); else SetPreviewHover(static_cast<int>(key - 100), true); };
        s.uia.Attach(std::move(host));
    }
public:
    std::vector<Accessibility::Node> MenuNodes() const {
        std::vector<Accessibility::Node> nodes; if (!model_) return nodes;
        RECT window{}; if (menu_.hwnd) GetWindowRect(menu_.hwnd, &window); const float k = (menu_.dpi ? menu_.dpi : 96) / 96.f;   // no window: the test tree
        Accessibility::Node m; m.key = MenuKey; m.role = Accessibility::Role::Menu; m.name = TR(settings_profiles); m.automationId = L"ProfileMenu"; m.focusable = false; m.bounds = window; nodes.push_back(m);
        int count = 0; for (const auto& r : rows_) if (Selectable(r)) ++count;
        int position = 0;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& r = rows_[i]; if (!Selectable(r)) continue;
            Accessibility::Node n; n.key = RowKey(static_cast<int>(i)); n.parent = MenuKey; n.role = Accessibility::Role::MenuItem; n.position = ++position; n.count = count;
            n.name = r.kind == Kind::Profile ? RowName(model_->profiles[r.profile]) : Label(r);
            if (r.kind == Kind::TabList) n.automationId = L"ProfileMenuTabList";
            else if (r.kind == Kind::Create) n.automationId = L"ProfileMenuCreate";
            else if (r.kind == Kind::Private) { n.automationId = L"ProfileMenuPrivate"; n.accelerator = TR(key_ctrl_shift_n); n.help = TR(profile_menu_private_help); }
            else if (r.kind == Kind::Manage) n.automationId = L"ProfileMenuManage";
            else {
                const Profile& p = model_->profiles[r.profile]; n.automationId = L"ProfileMenuProfile"; if (p.current) n.status = TR(menu_item_selected);
                n.accelerator = TR(key_shift_f10);   // its context menu: « Ouvrir dans une nouvelle fenêtre »
                if (!p.tabs.empty()) {   // the preview, also exposed as the row's description
                    std::wstring help; const int shown = std::min<int>(Layout::maxPreview, static_cast<int>(p.tabs.size()));
                    for (int t = 0; t < shown; ++t) help += (t ? L" ; " : L"") + (p.tabs[t].title.empty() ? p.tabs[t].site : p.tabs[t].title);
                    const auto more = OverflowLine(p, shown); if (!more.empty()) help += L" ; " + more;
                    n.help = help;   // no ExpandCollapse: a MenuItem with it loses Invoke (UIA bridge), and the row must switch
                }
            }
            n.bounds = {window.left + static_cast<LONG>(Layout::pad * k), window.top + static_cast<LONG>(r.top * k), window.right - static_cast<LONG>(Layout::pad * k), window.top + static_cast<LONG>((r.top + r.height) * k)};
            const RECT rowBounds = n.bounds; const bool profile = r.kind == Kind::Profile;
            nodes.push_back(std::move(n));
            if (profile) {   // the row's context action as a real Invoke target (Shift+F10 / Menu / right-click open the same)
                Accessibility::Node a; a.key = NewWindowKey(static_cast<int>(i)); a.parent = RowKey(static_cast<int>(i)); a.role = Accessibility::Role::Button;
                a.name = TR(toast_open_in_new_window); a.automationId = L"ProfileMenuNewWindow"; a.help = TR(profile_menu_new_window_help); a.focusable = false; a.bounds = rowBounds;
                nodes.push_back(std::move(a));
            }
        }
        return nodes;
    }
    std::vector<Accessibility::Node> PreviewNodes() const {
        std::vector<Accessibility::Node> nodes; const Profile* p = ProfileAt(previewRow_); if (!preview_.hwnd || !p) return nodes;
        RECT window{}; GetWindowRect(preview_.hwnd, &window); const float k = preview_.dpi / 96.f;
        Accessibility::Node m; m.key = MenuKey; m.role = Accessibility::Role::Menu; m.name = TRF(profile_tabs_of, p->name); m.automationId = L"ProfileTabPreview"; m.focusable = false; m.bounds = window; nodes.push_back(m);
        const int n = PreviewCount();
        for (int i = 0; i < n; ++i) {
            Accessibility::Node t; t.key = RowKey(i); t.parent = MenuKey; t.role = Accessibility::Role::MenuItem; t.position = i + 1; t.count = n;
            t.name = p->tabs[i].title.empty() ? p->tabs[i].site : p->tabs[i].title; t.help = p->tabs[i].site; t.automationId = L"ProfileTabPreviewItem";
            const float top = Layout::pad + 1 + i * Layout::previewRow;
            t.bounds = {window.left + static_cast<LONG>(Layout::pad * k), window.top + static_cast<LONG>(top * k), window.right - static_cast<LONG>(Layout::pad * k), window.top + static_cast<LONG>((top + Layout::previewRow) * k)};
            nodes.push_back(std::move(t));
        }
        return nodes;
    }
    bool ignorePointer = false;
    ~Menu() { Dispose(); }
    void Init(HINSTANCE instance, HWND owner, std::wstring root) {
        instance_ = instance; owner_ = owner; root_ = std::move(root);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereProfileMenu";
        RegisterClassW(&wc);
    }
    void Dispose() {
        for (Surface* s : {&menu_, &preview_}) { if (s->hwnd && IsWindow(s->hwnd)) SetWindowLongPtrW(s->hwnd, GWLP_USERDATA, 0); s->uia.Detach(); s->chrome.Dispose(); s->hwnd = nullptr; }
        glyphs_.Clear();
    }
    bool Open() const { return menu_.hwnd && IsWindowVisible(menu_.hwnd); }
    void Cancel() { if (Open() && !done_) Finish({}); }
    // Modal. `anchor`: the profile button in screen pixels; the menu drops 4 DIP under it,
    // left-aligned (flipped above / clamped to the work area when needed).
    Choice Track(const Model& model, RECT anchor, bool keyboard, bool reduced) {
        if (!owner_ || Open()) return {};
        model_ = &model; done_ = false; active_ = false; reduced_ = reduced; keyed_ = keyboard; choice_ = {}; previewShown_ = false; previewRow_ = pendingRow_ = -1; corridor_ = false;
        Create(menu_); if (!menu_.hwnd) return {};
        menu_.dpi = GetDpiForWindow(owner_); Build(); menu_.hover = menu_.pressed = -1; menu_.keyboard = false;
        const RECT work = WorkArea({anchor.left, anchor.bottom});
        const int w = Px(menu_, menu_.width), h = Px(menu_, menu_.height), g = Px(menu_, 4);
        int x = std::max<int>(work.left, std::min<int>(anchor.left, work.right - w)), y = anchor.bottom + g;
        if (y + h > work.bottom) y = std::max<int>(work.top, anchor.top - g - h);
        Show(menu_, x, y, true, 0);
        if (keyboard) Move(1, true);
        MSG msg;
        while (!done_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            const bool press = msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN || msg.message == WM_NCMBUTTONDOWN;
            if (press && !Of(msg.hwnd)) { Finish({}); break; }
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) { if (Key(msg.wParam)) continue; }
            if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        }
        // The exit plays out before Track returns (150 ms; reduced 100 ms): the model stays valid while painted.
        ClosePreview(); Leave(menu_, true);
        const ULONGLONG until = GetTickCount64() + 400;
        while (Leaving() && GetTickCount64() < until) { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) { PostQuitMessage(static_cast<int>(m.wParam)); break; } TranslateMessage(&m); DispatchMessageW(&m); } MsgWaitForMultipleObjects(0, nullptr, FALSE, 8, QS_ALLINPUT); }
        Leave(menu_, false); Leave(preview_, false); model_ = nullptr;
        return choice_;
    }
    // ---- test hooks (offscreen render, state) ----
    const std::vector<Row>& Rows() const { return rows_; }
    int HoverRow() const { return menu_.hover; }
    // Motion test hooks: presence of the menu surface entering / leaving at `ms` after the gesture.
    float TestEnterPresence(float ms, bool reduced) { reduced_ = reduced; menu_.entering = true; menu_.leaving = false; menu_.enterFrom = 0; const auto t = Motion::Now(); menu_.opened = t; return Presence(menu_, t + std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double, std::milli>(ms))); }
    float TestLeavePresence(float ms, bool reduced, float from = 1) { reduced_ = reduced; menu_.entering = false; menu_.leaving = true; menu_.leaveFrom = from; const auto t = Motion::Now(); menu_.left = t; return Presence(menu_, t + std::chrono::duration_cast<Motion::Clock::duration>(std::chrono::duration<double, std::milli>(ms))); }
    int PreviewRow() const { return previewShown_ ? previewRow_ : -1; }
    bool TestKey(WPARAM key) { return Key(key); }
    Choice TestNewWindow(const Model& m, int row) { model_ = &m; Build(); done_ = false; choice_ = {}; NewWindowRow(row); model_ = nullptr; return choice_; }
    std::vector<Accessibility::Node> TestMenuNodes(const Model& m) { model_ = &m; Build(); auto n = MenuNodes(); model_ = nullptr; return n; }
    Choice TestRowMenu(const Model& m, int row) { model_ = &m; Build(); done_ = false; choice_ = {}; RowMenu(row, {10, 20}); model_ = nullptr; return choice_; }
    // Renders the menu (and, with `previewRow` >= 0, the preview of that row) to PNGs without windows.
    bool RenderForTest(const Model& model, const std::wstring& root, int hoverRow, int previewRow, bool keyboard, const wchar_t* menuPng, const wchar_t* previewPng) {
        model_ = &model; Build(); menu_.hover = hoverRow; menu_.keyboard = keyboard; previewRow_ = previewRow; previewShown_ = previewRow >= 0;
        const auto later = Motion::Now() + std::chrono::seconds(1);
        if (hoverRow >= 0) { menu_.motion.SetReducedMotion(true); menu_.motion.SetInteraction(hoverRow + 1, 0, later); }
        Chrome* dummy = nullptr; bool ok = PopupKit::RenderPng(root, menu_.width, menu_.height, 96, menuPng, dummy, [&] { Chrome off; (void)off; });
        // RenderPng swaps a pointer we do not use: paint straight into an offscreen Chrome instead.
        Chrome a; ok = SUCCEEDED(a.InitOffscreen(root, 96, menu_.width, menu_.height));
        if (ok) { a.RenderTarget()->BeginDraw(); PaintMenu(a, later); a.RenderTarget()->EndDraw(); ok = SUCCEEDED(a.SavePng(menuPng)); } a.Dispose();
        if (ok && previewRow >= 0 && previewPng) {
            MeasurePreview(); Chrome b; ok = SUCCEEDED(b.InitOffscreen(root, 96, preview_.width, preview_.height));
            if (ok) { b.RenderTarget()->BeginDraw(); PaintPreview(b, later); b.RenderTarget()->EndDraw(); ok = SUCCEEDED(b.SavePng(previewPng)); } b.Dispose();
        }
        glyphs_.Clear(); model_ = nullptr; previewShown_ = false; previewRow_ = -1; menu_.hover = -1;
        return ok;
    }
};

} // namespace ProfileMenu
} // namespace NativeChrome
