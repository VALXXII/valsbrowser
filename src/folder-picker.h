#pragma once
#include "i18n.h"
// #138 « Personnaliser… » from a folder's right-click menu (the bar, the side panel's grid, the compact column, a folder
// menu): the folder picker as a popover (Paper « #138 — Dossiers de favoris », board « C finale (validée) » 23SD-0).
// 288 DIP, #1E1E1E, a 1 DIP #2E2E2E border, radius 8 (window region, no shadow), padding 10, 8 between its parts:
// a header (the folder in a 32 DIP #202020 tile, radius 6; its name 14/18 semibold white, 8 after it), the swatches
// (« Sans couleur » then the profiles' palette) and the 8 x 3 grid of icons (src/bookmark-folder-style.h, painted by
// Chrome::PaintFolderPicker). A choice applies at once and the popover stays; Escape or a click elsewhere closes it.
// Keyboard: Tab and the arrows move (FolderStyle::Picker::Model), Enter or Space chooses.
// Not in the board, the app's own values: it opens like the bookmark popover (8 DIP under its anchor, kept 6 DIP inside
// the window, flipped above when room is short; 180 ms fade + 6 DIP slide, none with reduced motion); hover is the
// ghost buttons' #333333; the keyboard focus ring is the board's 2 DIP #E6E6E6.
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "bookmark-folder-style.h"

namespace NativeChrome {
namespace FolderPicker {
namespace P = FolderStyle::Picker;
namespace Layout {
inline constexpr float width = P::popoverWidth, inset = P::border + P::padding;            // content from 11
inline constexpr float headerTop = inset, nameLeft = inset + P::headerTile + P::headerGap;   // 51
inline constexpr float stopsTop = headerTop + P::headerTile + P::popoverGap;                 // 51
inline constexpr float gridTop = stopsTop + P::swatch + P::popoverGap;                       // 83
inline constexpr float enterMs = 180, enterSlide = 6;                                        // the bookmark popover's entrance
inline float Height() { return P::PopoverHeight(); }
}
struct Hooks {
    std::function<bool(unsigned id, const std::string& icon, const std::string& color)> restyle;   // false: the folder is gone (closes)
    std::function<void(bool keyboard)> closed;
    std::function<void(const std::wstring&)> announce;
};
class Popover {
    static constexpr UINT_PTR FrameTimer = 1;
    static constexpr long long GroupKey = 10, StopKey = 100;   // stops: StopKey + index
    HWND hwnd_ = nullptr, owner_ = nullptr;
    Chrome chrome_; Motion::State motion_; Hooks hooks_; Accessibility::Bridge uia_;
    unsigned id_ = 0; std::wstring name_; P::Model model_;
    bool visible_ = false, keyboard_ = false, entering_ = false; Motion::Time opened_{}; POINT final_{};
    int hover_ = -1, pressed_ = -1;
    float Dpi() const { return static_cast<float>(GetDpiForWindow(hwnd_ ? hwnd_ : owner_)); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    static std::wstring StopName(int stop) {
        if (stop < 0 || stop >= P::count) return L"";
        if (stop < P::swatches) return stop == 0 ? std::wstring(TR(folder_color_none)) : std::wstring(FolderStyle::colors[stop - 1].label);
        return std::wstring(FolderStyle::icons[stop - P::swatches].label);
    }
    int HitAt(float x, float y) const { return P::HitStop(x, y, Layout::inset, Layout::stopsTop, Layout::gridTop); }
    FolderStyle::Look HeaderLook() const { return FolderStyle::Resolve(model_.color, model_.icon); }
    void Interact() {
        motion_.SetInteraction(hover_ >= 0 ? static_cast<int>(StopKey) + hover_ : 0, pressed_ >= 0 && pressed_ == hover_ ? static_cast<int>(StopKey) + pressed_ : 0, Motion::Now());
        InvalidateRect(hwnd_, nullptr, FALSE); if (motion_.NextWake(Motion::Now()).delay) SetTimer(hwnd_, FrameTimer, 10, nullptr);
    }
    void Step() {
        const auto now = Motion::Now();
        if (entering_) {
            const float u = Motion::Clamp(static_cast<float>(std::chrono::duration<double, std::milli>(now - opened_).count()) / Layout::enterMs);
            const float e = 1 - (1 - u) * (1 - u) * (1 - u);
            SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * Motion::Clamp(e * 1.4f))), LWA_ALPHA);
            SetWindowPos(hwnd_, nullptr, final_.x, final_.y - Px(Layout::enterSlide * (1 - e)), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (u >= 1) entering_ = false;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (!entering_ && !motion_.NextWake(now).delay) KillTimer(hwnd_, FrameTimer);
    }
    void Choose(int stop, bool keyboard) {
        const auto out = model_.Choose(stop); if (!keyboard) model_.focus = -1; else keyboard_ = true;
        if (out == P::Outcome::Chosen) {
            if (hooks_.restyle && !hooks_.restyle(id_, FolderStyle::IconKey(model_.icon), FolderStyle::ColorKey(model_.color))) { Close(keyboard); return; }
            if (hooks_.announce) hooks_.announce(stop < P::swatches ? TRF(folder_announce_color, StopName(stop)) : TRF(folder_announce_icon, StopName(stop)));
        }
        if (uia_.Listening()) uia_.Raise(StopKey + stop, UIA_SelectionItemIsSelectedPropertyId);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    bool Key(WPARAM w) {
        using K = P::Key; K k;
        switch (w) {
        case VK_TAB: k = GetKeyState(VK_SHIFT) < 0 ? K::BackTab : K::Tab; break;
        case VK_LEFT: k = K::Left; break; case VK_RIGHT: k = K::Right; break; case VK_UP: k = K::Up; break; case VK_DOWN: k = K::Down; break;
        case VK_HOME: k = K::Home; break; case VK_END: k = K::End; break;
        case VK_RETURN: case VK_SPACE: k = K::Choose; break;
        case VK_ESCAPE: k = K::Close; break;
        default: return false;
        }
        keyboard_ = true;
        if (k == K::Close) { Close(true); return true; }
        if (k == K::Choose && model_.focus >= 0) { Choose(model_.focus, true); return true; }
        if (model_.Press(k) != P::Outcome::None) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            if (uia_.Listening()) uia_.Raise(StopKey + model_.focus, UIA_AutomationFocusChangedEventId);
            if (hooks_.announce) hooks_.announce(StopName(model_.focus));
        }
        return true;
    }
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        const auto now = Motion::Now(); const float h = Layout::Height(), w = Layout::width;
        t->BeginDraw(); t->Clear(Chrome::Rgb(P::surface)); chrome_.keyboardModality = keyboard_;
        {   // Header (23SE-0): the folder as it looks now, its name.
            const D2D1_RECT_F tile{Layout::inset, Layout::headerTop, Layout::inset + P::headerTile, Layout::headerTop + P::headerTile};
            chrome_.FillBox(tile, Design::Radius::control, Chrome::Rgb(P::headerTileFill));
            const auto look = HeaderLook();
            if (look.styled) chrome_.FolderLook(look, tile, FolderStyle::Size::For(FolderStyle::Size::cell, FolderStyle::Size::cellPlain, look));
            else chrome_.FolderGlyph(tile, Design::Size::icon, 1, 0, true);   // the grey folder of today (the drawer's)
            chrome_.semibold = true;
            chrome_.FadedText(name_, {Layout::nameLeft, Layout::headerTop, w - Layout::inset, Layout::headerTop + P::headerTile}, P::nameSize, 1, false, false, 0, FolderStyle::nameFade);
            chrome_.semibold = false;
        }
        const int focus = keyboard_ && GetFocus() == hwnd_ ? model_.focus : -1;
        chrome_.PaintFolderPicker(Layout::inset, Layout::stopsTop, P::popoverGap, model_, [&](int stop) { return motion_.Visual(static_cast<int>(StopKey) + stop, now).hover; }, focus);
        // Border (#2E2E2E, 1 DIP) on the rounded outline.
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        auto* b = chrome_.SolidBrush(); b->SetColor(Chrome::Rgb(P::popoverBorder));
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(P::border / 2, P::border / 2, w - P::border / 2, h - P::border / 2), P::popoverRadius - P::border / 2, P::popoverRadius - P::border / 2), b, P::border);
        (void)px;
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> nodes; if (!visible_) return nodes;
        auto screen = [&](P::Box r) { RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2); return p; };
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = TRF(folder_customize_named, name_); g.help = TR(folder_picker_hint_keys);
        g.automationId = L"FolderPickerPopover"; g.focusable = false; RECT wr{}; GetWindowRect(hwnd_, &wr); g.bounds = wr; nodes.push_back(g);
        for (int s = 0; s < P::count; ++s) {
            const bool swatch = s < P::swatches; Accessibility::Node n; n.key = StopKey + s; n.parent = GroupKey; n.role = Accessibility::Role::Button; n.name = StopName(s);
            n.help = swatch ? TR(folder_picker_colors) : TR(folder_picker_icons); n.selected = swatch ? s - 1 == model_.color : s - P::swatches == model_.icon;
            n.automationId = swatch ? L"FolderPicker.Color." + std::wstring(s == 0 ? L"none" : FolderStyle::WideKey(FolderStyle::colors[s - 1].key)) : L"FolderPicker.Icon." + FolderStyle::WideKey(FolderStyle::icons[s - P::swatches].key);
            n.position = swatch ? s + 1 : s - P::swatches + 1; n.count = swatch ? P::swatches : P::cells; n.bounds = screen(P::Stop(s, Layout::inset, Layout::stopsTop, Layout::gridTop)); nodes.push_back(n);
        }
        return nodes;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Popover*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Popover*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        if (msg == WM_NCDESTROY) { SetWindowLongPtrW(h, GWLP_USERDATA, 0); if (self && self->hwnd_ == h) self->hwnd_ = nullptr; return DefWindowProcW(h, msg, w, l); }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE && visible_) PostMessageW(h, WM_APP + 1, 0, 0); return 0;   // a click elsewhere closes it
        case WM_APP + 1: if (visible_ && GetForegroundWindow() != hwnd_) Close(false); return 0;
        case WM_APP + 2: keyboard_ = true; Choose(static_cast<int>(w), true); return 0;    // UIA invoke
        case WM_APP + 3: keyboard_ = true; model_.focus = static_cast<int>(w); InvalidateRect(h, nullptr, FALSE); return 0;   // UIA focus
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_TIMER: if (w == FrameTimer) Step(); return 0;
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT e{sizeof(e), TME_LEAVE, h, 0}; TrackMouseEvent(&e); const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); if (hit != hover_) { hover_ = hit; Interact(); } return 0; }
        case WM_MOUSELEAVE: hover_ = -1; Interact(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); pressed_ = hit; if (hit >= 0) SetCapture(h); Interact(); return 0; }
        case WM_LBUTTONUP: { const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale), was = pressed_; pressed_ = -1; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was >= 0 && was == hit) Choose(hit, false); return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: if (Key(w)) return 0; break;
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
        case WM_SETFOCUS: case WM_KILLFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_DPICHANGED: if (visible_) { SetWindowPos(hwnd_, nullptr, 0, 0, Px(Layout::width), Px(Layout::Height()), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); chrome_.Resize(); InvalidateRect(h, nullptr, FALSE); } return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Popover() { Dispose(); }
    void Dispose() {
        if (hwnd_) { KillTimer(hwnd_, FrameTimer); SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0); }
        uia_.Detach(); chrome_.Dispose();
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
        hwnd_ = nullptr; visible_ = false;
    }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        owner_ = owner; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereFolderPicker";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, TR(folder_customize), WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_) return false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(hwnd_, P::popoverRadius);   // region-rounded (8), never a shadow
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ && model_.focus >= 0 ? StopKey + model_.focus : 0; };
        host.invoke = [this](long long key) { if (key >= StopKey && key < StopKey + P::count) PostMessageW(hwnd_, WM_APP + 2, static_cast<WPARAM>(key - StopKey), 0); };
        host.focus = [this](long long key) { if (key >= StopKey && key < StopKey + P::count) PostMessageW(hwnd_, WM_APP + 3, static_cast<WPARAM>(key - StopKey), 0); };
        host.select = host.focus;
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    unsigned Folder() const { return visible_ ? id_ : 0; }
    // Opens for folder `id` (its name and stored look) under `anchor` (screen px), left edges aligned, kept 6 DIP inside `bounds`.
    void Show(unsigned id, const std::wstring& name, const std::string& icon, const std::string& color, RECT anchor, RECT bounds, bool keyboard, bool reduced) {
        if (!hwnd_) return;
        id_ = id; name_ = name; model_ = {}; model_.color = FolderStyle::ColorIndex(color); model_.icon = FolderStyle::IconIndex(icon);
        keyboard_ = keyboard; if (keyboard) model_.FocusChosen();
        hover_ = pressed_ = -1; motion_.ClearInteractions(); motion_.SetReducedMotion(reduced);
        SetWindowTextW(hwnd_, TRF(folder_customize_named, name).c_str());
        const int w = Px(Layout::width), h = Px(Layout::Height()), margin = Px(Design::Space::sm);
        int x = anchor.left, y = anchor.bottom + Px(Design::Space::md);
        x = std::max<int>(bounds.left + margin, std::min<int>(x, bounds.right - margin - w));
        if (y + h > bounds.bottom - margin) y = std::max<int>(bounds.top + margin, anchor.top - Px(Design::Space::md) - h);
        final_ = {x, y}; visible_ = true; opened_ = Motion::Now(); entering_ = !reduced;
        SetWindowPos(hwnd_, HWND_TOP, x, entering_ ? y - Px(Layout::enterSlide) : y, w, h, SWP_NOACTIVATE);
        SetLayeredWindowAttributes(hwnd_, 0, entering_ ? 0 : 255, LWA_ALPHA);
        chrome_.Resize(); ShowWindow(hwnd_, SW_SHOW); Paint(); ValidateRect(hwnd_, nullptr);
        SetForegroundWindow(hwnd_); SetFocus(hwnd_);
        if (entering_) SetTimer(hwnd_, FrameTimer, 10, nullptr);
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); if (keyboard) uia_.Raise(StopKey + model_.focus, UIA_AutomationFocusChangedEventId); }
        if (hooks_.announce) hooks_.announce(TRF(folder_customize_named, name));
    }
    void Close(bool keyboard) {
        if (!visible_) return;
        visible_ = false; KillTimer(hwnd_, FrameTimer);
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        ShowWindow(hwnd_, SW_HIDE);
        if (hooks_.closed) hooks_.closed(keyboard);
    }
    // The folder changed elsewhere (another window, an undo): its name and look as stored now.
    void Sync(const std::wstring& name, const std::string& icon, const std::string& color) { if (!visible_) return; name_ = name; model_.color = FolderStyle::ColorIndex(color); model_.icon = FolderStyle::IconIndex(icon); InvalidateRect(hwnd_, nullptr, FALSE); }
    // Test hooks.
    void KeyForTest(WPARAM key) { Key(key); }
    void ChooseForTest(int stop) { Choose(stop, false); }
    const P::Model& ModelForTest() const { return model_; }
    void PaintForTest() { Paint(); }
    std::vector<Accessibility::Node> AccessibleNodesForTest() { return Nodes(); }
};
} // namespace FolderPicker
} // namespace NativeChrome
