#pragma once
// Undo toast for bookmark removals (Paper "09 · Supprimer et annuler"): bottom-left of
// the window, 14 DIP inset, #292929 card, radius 8 (DWM-rounded; Paper 10), 1 DIP #FFFFFF0F border, padding
// 8/12, gap 16: trash glyph 16 (50%), the message (14 medium, white), "Annuler", and a
// 24x24 close. One at a time (the newest replaces the previous one, which stays
// undoable with Ctrl+Z). Lives 6 s, paused while hovered or focused. Enters in 240 ms
// (12 DIP rise + fade), leaves in 150 ms; reduced motion: fade 100 ms.
// Non-activating: showing it never steals focus from the page.
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <functional>
#include <string>
#include "chrome.h"
#include "accessibility.h"
#include "native-ui.h"

namespace NativeChrome {
namespace BookmarkToast {
namespace Layout {
inline constexpr float inset = 14, height = 42, padX = 12, gap = 16, radius = NativeUI::nativeCornerRadius, close = 24, maxText = 420;
inline constexpr float lifeMs = 6000, enterMs = 240, exitMs = 150, reducedMs = 100, rise = 12;
}
class Toast {
    enum Hit { None = 0, UndoHit = 1, CloseHit = 2 };
    HWND hwnd_ = nullptr, owner_ = nullptr; Chrome chrome_; Motion::State motion_; Accessibility::Bridge uia_;
    std::wstring text_, action_ = L"Annuler"; std::function<void()> undo_, closed_; bool sticky_ = false;
    bool visible_ = false, leaving_ = false, reduced_ = false, hovered_ = false, keyboard_ = false;
    int hover_ = 0, pressed_ = 0, focus_ = 0; float bottomReserve_ = 0;
    Motion::Time phase_{}; ULONGLONG remaining_ = 0, resumed_ = 0;
    static constexpr UINT_PTR FrameTimer = 1, LifeTimer = 2;
    float Dpi() const { return static_cast<float>(GetDpiForWindow(owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    float TextWidth() { return std::min(Layout::maxText, std::ceil(chrome_.TextWidth(text_, 14, true, false, -.28f))); }
    float UndoWidth() { return std::ceil(chrome_.TextWidth(action_, 14, true, false, -.28f)) + 16; }
    float Width() { return Layout::padX + 16 + 8 + TextWidth() + Layout::gap + UndoWidth() + 8 + Layout::close + Layout::padX - 4; }
    D2D1_RECT_F UndoBox() { const float x = Layout::padX + 24 + TextWidth() + Layout::gap; return {x, 9, x + UndoWidth(), 33}; }
    D2D1_RECT_F CloseBox() { const float w = Width(); return {w - Layout::padX + 4 - Layout::close, 9, w - Layout::padX + 4, 33}; }
    int HitAt(float x, float y) { auto in = [&](D2D1_RECT_F r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }; return in(UndoBox()) ? UndoHit : in(CloseBox()) ? CloseHit : None; }
    POINT Rest() {
        RECT client{}; GetClientRect(owner_, &client); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&client), 2);
        return {client.left + Px(Layout::inset), client.bottom - Px(Layout::inset + bottomReserve_) - Px(Layout::height)};
    }
    void Place(float progress, bool entering) {
        const POINT rest = Rest(); const int w = Px(Width()), h = Px(Layout::height);
        const float rise = reduced_ ? 0 : (entering ? Layout::rise * (1 - progress) : 0);
        SetWindowPos(hwnd_, HWND_TOP, rest.x, rest.y + Px(rise), w, h, SWP_NOACTIVATE);
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * Motion::Clamp(progress))), LWA_ALPHA);
    }
    float Elapsed() const { return static_cast<float>(std::chrono::duration<double, std::milli>(Motion::Now() - phase_).count()); }
    void Step() {
        if (!visible_) { KillTimer(hwnd_, FrameTimer); return; }
        if (leaving_) {
            const float u = Motion::Clamp(Elapsed() / (reduced_ ? Layout::reducedMs : Layout::exitMs));
            Place(1 - u, false); if (u >= 1) Finish(); return;
        }
        const float u = Motion::Clamp(Elapsed() / (reduced_ ? Layout::reducedMs : Layout::enterMs));
        Place(1 - (1 - u) * (1 - u) * (1 - u), true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (u >= 1 && !motion_.NextWake(Motion::Now()).delay) KillTimer(hwnd_, FrameTimer);
    }
    void Finish() { KillTimer(hwnd_, FrameTimer); KillTimer(hwnd_, LifeTimer); visible_ = leaving_ = false; ShowWindow(hwnd_, SW_HIDE); auto done = std::move(closed_); closed_ = nullptr; if (done) done(); }
    // The 6 s countdown runs only while neither hovered nor focused.
    void Pause() { if (!remaining_) return; KillTimer(hwnd_, LifeTimer); const ULONGLONG spent = GetTickCount64() - resumed_; remaining_ = remaining_ > spent ? remaining_ - spent : 1; resumed_ = 0; }
    void Resume() { if (!visible_ || leaving_ || resumed_ || !remaining_) return; resumed_ = GetTickCount64(); SetTimer(hwnd_, LifeTimer, static_cast<UINT>(remaining_), nullptr); }
    void Interact() { motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, Motion::Now()); InvalidateRect(hwnd_, nullptr, FALSE); if (motion_.NextWake(Motion::Now()).delay) SetTimer(hwnd_, FrameTimer, 10, nullptr); }
    void Activate(int hit) { if (hit == UndoHit) { auto undo = undo_; Dismiss(); if (undo) undo(); } else if (hit == CloseHit) Dismiss(); }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); const float w = Width(), c = Design::Surface::canvas, r = Design::Surface::raised;
        t->BeginDraw(); t->Clear(D2D1::ColorF(c, c, c, 1)); chrome_.keyboardModality = keyboard_;
        chrome_.Round({0, 0, w, Layout::height}, Layout::radius, r);
        chrome_.Icon(L"trash", {Layout::padX, 13, Layout::padX + 16, 29}, 16, 1);
        t->PushAxisAlignedClip(D2D1::RectF(Layout::padX + 24, 0, Layout::padX + 24 + TextWidth(), Layout::height), D2D1_ANTIALIAS_MODE_ALIASED);
        chrome_.Text(text_, {Layout::padX + 24, 0, Layout::padX + 24 + TextWidth() + 400, Layout::height}, 14, 1, false, true, -.28f);
        t->PopAxisAlignedClip();
        const auto undo = UndoBox(); const auto uv = motion_.Visual(UndoHit, now);
        chrome_.Round(undo, Design::Radius::control, r + (Design::Surface::hover - r) * (uv.hover * .7f + uv.press * .3f));
        if (focus_ == UndoHit) chrome_.FocusRing(undo, Design::Radius::control);
        chrome_.Text(action_, {undo.left + 8, undo.top, undo.right, undo.bottom}, 14, 1, false, true, -.28f);
        const auto cv = motion_.Visual(CloseHit, now);
        chrome_.DrawButton(L"close", CloseBox(), Design::ButtonVariant::Embedded, {cv.hover, cv.press, false, focus_ == CloseHit});
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        auto* b = chrome_.SolidBrush(); b->SetColor(D2D1::ColorF(1, 1, 1, 15 / 255.f));
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, w - px / 2, Layout::height - px / 2), Layout::radius, Layout::radius), b, px);
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Toast*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Toast*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float s = 96.f / Dpi();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER: if (w == FrameTimer) Step(); else if (w == LifeTimer) { KillTimer(h, LifeTimer); remaining_ = 0; Dismiss(); } return 0;
        case WM_MOUSEMOVE: { if (!hovered_) { hovered_ = true; Pause(); TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); } const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (hit != hover_) { hover_ = hit; Interact(); } return 0; }
        case WM_MOUSELEAVE: hovered_ = false; hover_ = 0; Interact(); if (!focus_) Resume(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; pressed_ = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s); if (pressed_) SetCapture(h); Interact(); return 0; }
        case WM_LBUTTONUP: { const int hit = HitAt(GET_X_LPARAM(l) * s, GET_Y_LPARAM(l) * s), was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was && was == hit) Activate(hit); return 0; }
        case WM_KEYDOWN:
            if (w == VK_TAB || w == VK_LEFT || w == VK_RIGHT) { keyboard_ = true; focus_ = focus_ == UndoHit ? CloseHit : UndoHit; InvalidateRect(h, nullptr, FALSE); if (uia_.Listening()) uia_.Raise(focus_, UIA_AutomationFocusChangedEventId); return 0; }
            if ((w == VK_RETURN || w == VK_SPACE) && focus_) { Activate(focus_); return 0; }
            if (w == VK_ESCAPE || w == VK_F6) { const bool back = true; focus_ = 0; Pause(); Resume(); if (back && returnFocus_) returnFocus_(); return 0; }
            break;
        case WM_SETFOCUS: Pause(); if (!focus_) focus_ = UndoHit; InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_KILLFOCUS: focus_ = 0; InvalidateRect(h, nullptr, FALSE); if (!hovered_) Resume(); return 0;
        case WM_APP + 1: Activate(static_cast<int>(w)); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    std::function<void()> returnFocus_; // Escape / F6 from the toast: back to the page
    ~Toast() { Dispose(); }
    void Dispose() { uia_.Detach(); chrome_.Dispose(); hwnd_ = nullptr; visible_ = false; }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root) {
        owner_ = owner; motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereBookmarkToast";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, wc.lpszClassName, L"Favoris", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_) return false;
        // Our Direct2D card owns the opaque surface and border; DWM only rounds the
        // window (antialiased, RoundNatively). Never acrylic/Mica or its non-client frame.
        NativeUI::ConfigureFullClientFrame(hwnd_);
        const int noBackdrop = 1; // DWMSBT_NONE (Windows 11; harmless on Windows 10).
        DwmSetWindowAttribute(hwnd_, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &noBackdrop, sizeof(noBackdrop));
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        NativeUI::RoundNatively(hwnd_);
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] {
            std::vector<Accessibility::Node> nodes; if (!visible_) return nodes;
            auto screen = [&](D2D1_RECT_F r) { RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; };
            Accessibility::Node g; g.key = 10; g.role = Accessibility::Role::Group; g.name = text_; g.automationId = L"BookmarkUndoToast"; g.focusable = false; RECT wr{}; GetWindowRect(hwnd_, &wr); g.bounds = wr; nodes.push_back(g);
            Accessibility::Node u; u.key = UndoHit; u.parent = 10; u.name = action_; if (action_ == L"Annuler") u.accelerator = L"Ctrl+Z"; u.automationId = L"BookmarkUndo"; u.bounds = screen(UndoBox()); nodes.push_back(u);
            Accessibility::Node c; c.key = CloseHit; c.parent = 10; c.name = L"Fermer"; c.automationId = L"BookmarkUndoClose"; c.bounds = screen(CloseBox()); nodes.push_back(c);
            return nodes; };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? focus_ : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_ && !leaving_; }
    const std::wstring& Message() const { return text_; }
    const std::wstring& ActionLabel() const { return action_; }
    // Keeps clear of another bottom-left card (the update toast) by `reserve` DIP.
    void SetBottomReserve(float reserve) { bottomReserve_ = reserve; if (visible_ && !leaving_) Place(1, false); }
    // `sticky`: stays until acted on (a failed save offering "Réessayer").
    void Show(const std::wstring& text, std::function<void()> undo, bool reduced, const std::wstring& action = L"Annuler", bool sticky = false) {
        if (!hwnd_) return;
        text_ = text; undo_ = std::move(undo); action_ = action; sticky_ = sticky; reduced_ = reduced; leaving_ = false; focus_ = 0; hover_ = pressed_ = 0; motion_.ClearInteractions(); motion_.SetReducedMotion(reduced);
        const bool was = visible_; visible_ = true; phase_ = Motion::Now();
        chrome_.Resize();
        if (!was) Place(0, true); else Place(1, false);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE); chrome_.Resize(); InvalidateRect(hwnd_, nullptr, FALSE);
        SetTimer(hwnd_, FrameTimer, 10, nullptr);
        KillTimer(hwnd_, LifeTimer); remaining_ = sticky ? 0 : static_cast<ULONGLONG>(Layout::lifeMs); resumed_ = 0; if (!hovered_) Resume();
        if (uia_.Listening()) uia_.ChildrenChanged(10);
    }
    void Dismiss() {
        if (!visible_ || leaving_) return;
        KillTimer(hwnd_, LifeTimer); remaining_ = 0; leaving_ = true; phase_ = Motion::Now(); undo_ = nullptr;
        if (GetFocus() == hwnd_ && returnFocus_) returnFocus_();
        SetTimer(hwnd_, FrameTimer, 10, nullptr);
    }
    void HideNow() { if (visible_) Finish(); }
    void Reposition() { if (visible_ && !leaving_) Place(1, false); }
    bool Focus() { if (!Visible()) return false; keyboard_ = true; focus_ = UndoHit; SetFocus(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    bool HasFocus() const { return hwnd_ && GetFocus() == hwnd_; }
    void SetClosed(std::function<void()> closed) { closed_ = std::move(closed); }
    void PaintForTest() { Paint(); }
};
} // namespace BookmarkToast
} // namespace NativeChrome
