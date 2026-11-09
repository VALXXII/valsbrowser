#pragma once
// C6/C7 · Indication plein écran (Paper « Browser Design », p-D-0: contexte C6 F40-0 / pastille
// F7E-0; séquence C7 F9K-0: entrée FBJ-0, visible FBV-0, sortie FC8-0, clavier verrouillé FCL-0;
// fenêtre F11 FF4-0 avec le bouton « Quitter le plein écran » FI8-0).
// A non-activating, click-through pill centred 24 DIP under the top of the monitor that says who
// took the screen and how to leave. main owns the full-screen window and the WebView2 events
// (ContainsFullScreenElementChanged, F11); this only shows and times the hint.
//
// Paper values (get_jsx / get_computed_styles):
//   pill h 40, #292929 at 92 % (#292929EB), border 1 px #FFFFFF14, radius 6, padding 16 / 8, gap 8
//   text 14/20 #FFFFFFB3, origin medium #FFFFFFE6; separator 1x16 #FFFFFF1F; key cap h 24,
//   radius 3, #FFFFFF1F, padding 8, 13/16 medium white; « pour quitter » + 8 right padding.
//   Locked keyboard (games, calls): « jeu.exemple.fr contrôle le clavier | Maintenez [Échap] pour
//   quitter », the cap filling with the accent #3E67FF over 1.5 s of hold.
//   Timing (C7 notes FIX-0..FJ0-0): slide 8 DIP + fade 160 ms in, 3 s visible, 200 ms fade out;
//   reappears when the pointer touches the top edge (main calls Reveal()). Reduced motion: fades only.
// Assumed deviations (design/missing-elements-B.md): the pill is drawn opaque #292929 (a layered
// window fades as a whole; per-pixel 92 % would need UpdateLayeredWindow, rejected for every
// popup by lot A); the F11 variant reuses the pill (« Plein écran | [F11] pour quitter »), Paper
// only drawing the revealed chrome for F11.
#include <windows.h>
#include <functional>
#include <string>
#include "tooltip.h"

namespace NativeChrome {
namespace FullscreenHint {
namespace Layout {
inline constexpr float top = 24, height = 40, radius = 6, padLeft = 16, padRight = 8, gap = 8, separator = 16;
inline constexpr float keyHeight = 24, keyPad = 8, keyRadius = 3, trailing = 8;
inline constexpr float slide = 8, enterMs = 160, visibleMs = 3000, exitMs = 200, holdMs = 1500;
inline constexpr unsigned surface = 0x292929;
inline constexpr float border = 0x14 / 255.f, text = 0xB3 / 255.f, strong = 0xE6 / 255.f, line = 0x1F / 255.f, keyPlate = 0x1F / 255.f;
}
// « Quitter le plein écran » chrome button (F11 reveal, FI8-0), for main's chrome painter:
// h 32, #333333, radius 6, padding 8 / 4, gap 8, corners-in glyph 16 @80 %, label 14/20
// #FFFFFFE6, key cap « F11 » h 24 radius 3 #FFFFFF14 padding 6 13/16 medium #FFFFFFB3,
// 6 DIP from the window edge; reveal after 300 ms at the top edge, folds 600 ms after leaving.
namespace ExitButton {
inline constexpr float height = 32, radius = 6, padLeft = 8, padRight = 4, gap = 8, glyphOpacity = .8f, keyHeight = 24, keyPad = 6, margin = 6;
inline constexpr int revealDelayMs = 300, foldDelayMs = 600;
inline constexpr const wchar_t* label = L"Quitter le plein écran";
inline constexpr const char* glyph = PopupKit::Glyphs::cornersIn;
}
// What the pill reads: `strong` is emphasised (the origin), `key` sits in the cap.
struct Copy { std::wstring strong, lead, prefix, key, trailing; };
inline Copy CopyFor(bool page, const std::wstring& origin, bool keyboardLocked) {
    if (!page) return {L"Plein écran", L"", L"", L"F11", L"pour quitter"};
    const std::wstring who = origin.empty() ? L"Cette page" : origin;
    if (keyboardLocked) return {who, L" contrôle le clavier", L"Maintenez", L"Échap", L"pour quitter"};
    return {who, L" est en plein écran", L"", L"Échap", L"pour quitter"};
}
inline std::wstring Spoken(const Copy& c) { return c.strong + c.lead + L", " + (c.prefix.empty() ? L"" : c.prefix + L" ") + c.key + L" " + c.trailing; }

class Hint {
    HWND hwnd_ = nullptr, owner_ = nullptr; Chrome chrome_; Chrome* surface_ = &chrome_; PopupKit::Pump pump_; Accessibility::Bridge uia_;
    Copy copy_; std::wstring origin_; RECT monitor_{}; bool page_ = true, locked_ = false, visible_ = false, leaving_ = false, reduced_ = false;
    Motion::Time phase_{}, until_{}; float width_ = 0, hold_ = 0;
    int Px(float dip) const { return PopupKit::Px(owner_, dip); }
    PopupKit::Type Body() const { return {14, 20}; }
    float W(const std::wstring& s, const PopupKit::Type& t) { return s.empty() ? 0 : std::ceil(PopupKit::Measure(chrome_, s, 8192, t, false).width); }
    float KeyWidth() { return W(copy_.key, {13, 16, 0, true}) + 2 * Layout::keyPad; }
    float LeadWidth() { PopupKit::Type strong = Body(); strong.medium = true; return W(copy_.strong, strong) + W(copy_.lead, Body()) + 1; }
    void Measure() {
        using namespace Layout;
        width_ = 2 + padLeft + LeadWidth() + gap + 1 + gap + (copy_.prefix.empty() ? 0 : W(copy_.prefix, Body()) + gap) + KeyWidth() + gap + W(copy_.trailing, Body()) + trailing + padRight;
        width_ = std::ceil(width_);
    }
    void Paint() {
        Chrome& chrome_ = *surface_; // offscreen in RenderForTest
        using namespace Layout;
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        t->BeginDraw(); t->Clear(PopupKit::Gray(0x17));
        PopupKit::Fill(chrome_, {0, 0, width_, height}, radius, PopupKit::Hex(surface));
        float x = 1 + padLeft; const float y = 10;
        const std::wstring all = copy_.strong + copy_.lead; const float lead = LeadWidth();
        PopupKit::DrawRich(chrome_, all, 0, copy_.strong.size(), {x, y, x + lead + 4, y + 20}, Body(), PopupKit::White(text), PopupKit::White(strong));
        x += lead + gap; PopupKit::Fill(chrome_, {x, 12, x + 1, 28}, 0, PopupKit::White(line)); x += 1 + gap;
        if (!copy_.prefix.empty()) { const float w = W(copy_.prefix, Body()); PopupKit::DrawText(chrome_, copy_.prefix, {x, y, x + w + 1, y + 20}, Body(), PopupKit::White(text)); x += w + gap; }
        const float kw = KeyWidth(); const D2D1_RECT_F cap{x, 8, x + kw, 32};
        PopupKit::Fill(chrome_, cap, keyRadius, PopupKit::White(keyPlate));
        if (locked_ && hold_ > 0) {
            auto* b = chrome_.SolidBrush(); b->SetColor(PopupKit::Accent());
            t->PushAxisAlignedClip({cap.left, cap.top, cap.left + kw * Motion::Clamp(hold_), cap.bottom}, D2D1_ANTIALIAS_MODE_ALIASED);
            t->FillRoundedRectangle(D2D1::RoundedRect(cap, keyRadius, keyRadius), b); t->PopAxisAlignedClip();
        }
        PopupKit::DrawText(chrome_, copy_.key, {cap.left + keyPad, 12, cap.right, 28}, {13, 16, 0, true}, PopupKit::White(1));
        x += kw + gap; PopupKit::DrawText(chrome_, copy_.trailing, {x, y, width_, y + 20}, Body(), PopupKit::White(text));
        PopupKit::Outline(chrome_, width_, height, radius, PopupKit::White(border));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    float Elapsed(Motion::Time now) const { return static_cast<float>(std::chrono::duration<double, std::milli>(now - phase_).count()); }
    void Place(float progress, float slide) {
        const int w = Px(width_), x = (monitor_.left + monitor_.right - w) / 2, y = monitor_.top + Px(Layout::top) - Px(slide);
        SetWindowPos(hwnd_, HWND_TOP, x, y, w, Px(Layout::height), SWP_NOACTIVATE);
        PopupKit::SetAlpha(hwnd_, progress);
    }
    void Wake() { pump_.Request(NextWake(Motion::Now())); }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Hint*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Hint*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        if (!self) return DefWindowProcW(h, msg, w, l);
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (self->uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); self->Paint(); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_TIMER: if (w == PopupKit::Pump::TimerId) { KillTimer(h, w); const auto now = Motion::Now(); self->Frame(now); self->pump_.Request(self->NextWake(now)); } return 0;
        case WM_NCDESTROY: self->uia_.Detach(); self->hwnd_ = nullptr; self->visible_ = false; break;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Hint() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, std::function<void()> wake = nullptr) {
        owner_ = owner; PopupKit::Register(instance, L"LisiereFullscreenHint", Proc);
        if (!PopupKit::CreatePopup(instance, owner, L"LisiereFullscreenHint", L"Plein écran", WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, this, Layout::radius)) return false;
        pump_.Bind(hwnd_, std::move(wake));
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { std::vector<Accessibility::Node> nodes; if (!visible_) return nodes; Accessibility::Node n; n.key = 1; n.role = Accessibility::Role::Group; n.name = Spoken(copy_); n.automationId = L"FullscreenHint"; n.focusable = false; GetWindowRect(hwnd_, &n.bounds); nodes.push_back(n); return nodes; };
        host.focused = []() -> long long { return 0; };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() { pump_.Stop(); uia_.Detach(); chrome_.Dispose(); if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); hwnd_ = nullptr; visible_ = false; }
    // `monitorScreen`: the full-screen monitor rectangle (px). `page`: element full screen
    // (requestFullscreen → « Échap ») vs browser F11. `origin`: host that asked (never the page
    // title). `keyboardLocked`: the site holds Échap (hold 1.5 s, see SetHoldProgress).
    void Show(RECT monitorScreen, bool page, const std::wstring& origin = L"", bool keyboardLocked = false) {
        if (!hwnd_) return;
        monitor_ = monitorScreen; page_ = page; origin_ = origin; locked_ = page && keyboardLocked; hold_ = 0; copy_ = CopyFor(page, origin, locked_);
        const bool was = visible_ && !leaving_; visible_ = true; leaving_ = false;
        Measure(); chrome_.Resize();
        if (!was) { phase_ = Motion::Now(); Place(0, reduced_ ? 0 : Layout::slide); ShowWindow(hwnd_, SW_SHOWNOACTIVATE); }
        else Place(1, 0);
        chrome_.Resize(); Paint(); ValidateRect(hwnd_, nullptr);
        until_ = Motion::Now() + Motion::Milliseconds(static_cast<int>(Layout::enterMs + Layout::visibleMs));
        if (uia_.Listening()) { uia_.ChildrenChanged(1); const auto said = Spoken(copy_); uia_.Notify(1, said.c_str(), L"FullscreenHint"); }
        Wake();
    }
    // Pointer at the top edge (main): shows the same hint again for 3 s.
    void Reveal() { if (!hwnd_ || copy_.key.empty()) return; const std::wstring origin = origin_; Show(monitor_, page_, origin, locked_); }
    // Hold-to-exit progress 0..1 (main measures how long Échap is held; 1 = leave).
    void SetHoldProgress(float progress) { hold_ = Motion::Clamp(progress); if (visible_) { until_ = std::max(until_, Motion::Now() + Motion::Milliseconds(static_cast<int>(Layout::exitMs))); Paint(); ValidateRect(hwnd_, nullptr); } }
    void Hide() { if (!visible_ || leaving_) return; leaving_ = true; phase_ = Motion::Now(); Wake(); }
    void HideNow() { if (!hwnd_) return; visible_ = leaving_ = false; ShowWindow(hwnd_, SW_HIDE); }
    bool Visible() const { return visible_ && !leaving_; }
    HWND Window() const { return hwnd_; }
    const Copy& CurrentCopy() const { return copy_; }
    float WidthDip() const { return width_; }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!visible_) return {};
        if (leaving_ || Elapsed(now) < Layout::enterMs) return Motion::Frame();
        auto d = Motion::Deadline(now, until_); return d.delay ? d : Motion::Frame();
    }
    void Frame(Motion::Time now) {
        if (!hwnd_ || !visible_) return;
        if (leaving_) { const float u = Motion::Clamp(Elapsed(now) / Layout::exitMs); Place(1 - u, 0); if (u >= 1) HideNow(); return; }
        if (now >= until_ && !(locked_ && hold_ > 0)) { leaving_ = true; phase_ = now; return; }
        const float u = Motion::Clamp(Elapsed(now) / Layout::enterMs), e = 1 - (1 - u) * (1 - u) * (1 - u);
        Place(e, reduced_ ? 0 : Layout::slide * (1 - e));
    }
    void PaintForTest() { Paint(); }
    bool RenderForTest(const std::wstring& root, const wchar_t* png, UINT dpi = 96) { return PopupKit::RenderPng(root, width_, Layout::height, dpi, png, surface_, [this] { Paint(); }); }
};
} // namespace FullscreenHint
} // namespace NativeChrome
