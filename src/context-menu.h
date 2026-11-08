#pragma once
// Native context menu: one component for every right click (web pages through
// WebView2's ContextMenuRequested, tabs, the tab list, the address field),
// painted with Direct2D from the Paper frame "Clic droit menu — poli": 14 DIP
// text, 32 DIP rows, 9 DIP separators, a 4 DIP inset shell of radius 10 (the
// rows' 6 + 4, concentric), a white 6% hairline and no drop shadow.
//
// Track() is modal like TrackPopupMenu: it runs its own message loop and
// returns the chosen id (0 = dismissed). It opens instantly (a right click is a
// high-frequency gesture: no entrance motion), hover fills fade in 120 ms on the
// shared interaction tweens, and it closes at once. Submenus open after a short
// hover, at once from Right / Enter.
#include <windowsx.h>
#include <dwmapi.h>
#include <memory>
#include <string>
#include <vector>
#include "chrome.h"

namespace NativeChrome {
namespace ContextMenu {

struct Item {
    int id = 0;                       // returned by Track(); 0 is never returned for an item
    std::wstring label, shortcut;     // label may carry '&' mnemonics ("&&" is a literal '&')
    bool enabled = true, checked = false, separator = false;
    std::vector<Item> children;       // non-empty: a submenu
};
inline Item Separator() { Item item; item.separator = true; return item; }
inline Item Command(int id, std::wstring label, std::wstring shortcut = L"", bool enabled = true, bool checked = false) {
    Item item; item.id = id; item.label = std::move(label); item.shortcut = std::move(shortcut); item.enabled = enabled; item.checked = checked; return item;
}
// Drops mnemonic markers ("&Back" -> "Back", "R&&D" -> "R&D") and reports the access key.
inline std::wstring Plain(const std::wstring& label, wchar_t* mnemonic = nullptr) {
    std::wstring out; out.reserve(label.size()); if (mnemonic) *mnemonic = 0;
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] == L'&' && i + 1 < label.size()) {
            ++i; if (label[i] != L'&' && mnemonic && !*mnemonic) *mnemonic = static_cast<wchar_t>(towlower(label[i]));
        }
        out += label[i];
    }
    return out;
}

namespace Layout {
inline constexpr float pad = 4, row = 32, separator = 9, padLeft = 8, padRight = 10, shortcutGap = 24;
inline constexpr float radius = 10, rowRadius = 6, text = 14, line = 20, trailing = 16, minWidth = 200, maxWidth = 440;
inline constexpr float submenuDelayMs = 200;
inline float Height(const Item& item) { return item.separator ? separator : row; }
}
static_assert(Layout::radius == Layout::rowRadius + Layout::pad, "concentric shell: row radius + inset");

class Menu {
    struct Level {
        HWND hwnd = nullptr; Chrome chrome; Motion::State motion;
        const std::vector<Item>* items = nullptr;
        std::vector<float> tops; float width = Layout::minWidth, height = 0;
        int hover = -1, pressed = -1, child = -1; // child: index whose submenu is open
        bool keyboard = false;
        UINT dpi = 96;
    };
    HINSTANCE instance_ = nullptr; HWND owner_ = nullptr; std::wstring root_;
    std::vector<std::unique_ptr<Level>> levels_; int depth_ = 0; // open levels
    int result_ = 0; bool done_ = false, reduced_ = false;
    static constexpr UINT_PTR FrameTimer = 1, SubmenuTimer = 2;

    static bool Selectable(const Item& item) { return !item.separator && item.enabled; }
    Level* LevelOf(HWND hwnd) { for (int i = 0; i < depth_; ++i) if (levels_[i]->hwnd == hwnd) return levels_[i].get(); return nullptr; }
    int IndexOf(HWND hwnd) const { for (int i = 0; i < depth_; ++i) if (levels_[i]->hwnd == hwnd) return i; return -1; }
    float Scale(const Level& l) const { return l.dpi / 96.f; }
    int Px(const Level& l, float dip) const { return static_cast<int>(std::lround(dip * Scale(l))); }

    Level& Ensure(int index) {
        while (static_cast<int>(levels_.size()) <= index) {
            auto level = std::make_unique<Level>();
            level->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LisiereContextMenu", L"", WS_POPUP, 0, 0, 1, 1, owner_, nullptr, instance_, this);
            if (level->hwnd) {
                const int square = 1; DwmSetWindowAttribute(level->hwnd, 33, &square, sizeof(square)); // no DWM rounding: it brings a shadow
                const COLORREF none = 0xFFFFFFFE; DwmSetWindowAttribute(level->hwnd, 34, &none, sizeof(none));
                level->motion.EnablePolish(); level->chrome.SetMotion(&level->motion); level->chrome.textCacheLimit = 192;
                level->chrome.Init(level->hwnd, root_);
            }
            levels_.push_back(std::move(level));
        }
        return *levels_[index];
    }
    // Content-driven width: the widest label, plus the shortcut column or a trailing glyph.
    void Measure(Level& l) {
        float label = 0, shortcut = 0; bool glyph = false;
        for (const auto& item : *l.items) {
            if (item.separator) continue;
            label = std::max(label, l.chrome.TextWidth(Plain(item.label), Layout::text));
            if (!item.shortcut.empty()) shortcut = std::max(shortcut, l.chrome.TextWidth(item.shortcut, Layout::text, false, true));
            glyph = glyph || item.checked || !item.children.empty();
        }
        float tail = shortcut > 0 ? Layout::shortcutGap + shortcut : 0;
        if (glyph) tail = std::max(tail, Layout::shortcutGap + Layout::trailing);
        l.width = std::clamp(std::ceil(2 * Layout::pad + 2 + Layout::padLeft + label + tail + Layout::padRight), Layout::minWidth, Layout::maxWidth);
        l.tops.clear(); float y = Layout::pad + 1;
        for (const auto& item : *l.items) { l.tops.push_back(y); y += Layout::Height(item); }
        l.height = y + Layout::pad + 1;
    }
    static RECT WorkArea(POINT p) { MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &info); return info.rcWork; }
    // Opens `items` as level `index`. at: screen point for the top-left corner;
    // flipX: the x the menu ends at when it must open leftward instead.
    void Open(int index, const std::vector<Item>& items, POINT at, int flipX, int flipY, bool activate, bool fromRight = false) {
        Level& l = Ensure(index); if (!l.hwnd) return;
        l.items = &items; l.hover = l.pressed = l.child = -1; l.keyboard = false; l.motion.ClearInteractions(); l.motion.SetReducedMotion(reduced_);
        l.dpi = GetDpiForWindow(owner_); Measure(l);
        const int w = Px(l, l.width), h = Px(l, l.height); const RECT work = WorkArea(at);
        int x = fromRight ? at.x - w : at.x, y = at.y;
        if (x + w > work.right) x = std::max<int>(work.left, flipX - w);
        if (y + h > work.bottom) y = std::max<int>(work.top, std::min<int>(flipY, work.bottom) - h);
        x = std::max<int>(x, work.left); y = std::max<int>(y, work.top);
        SetWindowPos(l.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        SetWindowRgn(l.hwnd, CreateRoundRectRgn(0, 0, w + 1, h + 1, Px(l, Layout::radius * 2), Px(l, Layout::radius * 2)), FALSE);
        l.chrome.Resize();
        depth_ = index + 1;
        ShowWindow(l.hwnd, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
        Paint(l); ValidateRect(l.hwnd, nullptr);
        if (activate) { SetForegroundWindow(l.hwnd); SetFocus(l.hwnd); }
    }
    void CloseFrom(int index) {
        for (int i = index; i < depth_; ++i) { KillTimer(levels_[i]->hwnd, FrameTimer); KillTimer(levels_[i]->hwnd, SubmenuTimer); ShowWindow(levels_[i]->hwnd, SW_HIDE); }
        if (index < depth_) depth_ = index;
        if (index > 0 && index - 1 < depth_) { levels_[index - 1]->child = -1; Invalidate(*levels_[index - 1]); }
    }
    void OpenChild(int index) {
        Level& l = *levels_[index]; const int i = l.hover;
        if (i < 0 || (*l.items)[i].children.empty() || !(*l.items)[i].enabled) return;
        if (l.child == i && depth_ > index + 1) return;
        CloseFrom(index + 1); l.child = i;
        RECT r; GetWindowRect(l.hwnd, &r);
        const POINT at{r.right - Px(l, Layout::pad), r.top + Px(l, l.tops[i]) - Px(l, Layout::pad + 1)};
        Open(index + 1, (*l.items)[i].children, at, r.left + Px(l, Layout::pad), r.top + Px(l, l.tops[i] + Layout::row) + Px(l, Layout::pad + 1), false);
        Invalidate(l);
    }
    void Finish(int id) { result_ = id; done_ = true; PostMessageW(owner_, WM_NULL, 0, 0); }
    void Activate(int index, int i) {
        Level& l = *levels_[index]; if (i < 0 || i >= static_cast<int>(l.items->size())) return;
        const Item& item = (*l.items)[i]; if (!Selectable(item)) return;
        if (!item.children.empty()) { l.hover = i; OpenChild(index); if (depth_ > index + 1) Move(index + 1, 1, true); return; }
        Finish(item.id);
    }
    void SetHover(int index, int i, bool keyboard) {
        Level& l = *levels_[index]; l.keyboard = keyboard;
        if (l.hover == i) return;
        l.hover = i; Interact(l);
        // Hovering another row of a parent closes its open submenu after the same delay.
        KillTimer(l.hwnd, SubmenuTimer);
        if (!keyboard && (i != l.child)) SetTimer(l.hwnd, SubmenuTimer, static_cast<UINT>(Layout::submenuDelayMs), nullptr);
    }
    void Interact(Level& l) {
        l.motion.SetInteraction(l.hover + 1, l.pressed >= 0 && l.pressed == l.hover ? l.pressed + 1 : 0, Motion::Now());
        Invalidate(l);
    }
    void Move(int index, int step, bool fromStart = false) {
        Level& l = *levels_[index]; const int n = static_cast<int>(l.items->size()); if (!n) return;
        int i = fromStart ? (step > 0 ? -1 : n) : l.hover;
        for (int k = 0; k < n; ++k) { i = (i + step + n) % n; if (Selectable((*l.items)[i])) { SetHover(index, i, true); return; } }
    }
    int HitRow(const Level& l, float y) const {
        for (size_t i = 0; i < l.tops.size(); ++i) if (y >= l.tops[i] && y < l.tops[i] + Layout::Height((*l.items)[i])) return (*l.items)[i].separator ? -1 : static_cast<int>(i);
        return -1;
    }
    void Invalidate(Level& l) {
        if (!l.hwnd) return; InvalidateRect(l.hwnd, nullptr, FALSE);
        if (l.motion.NextWake(Motion::Now()).delay) SetTimer(l.hwnd, FrameTimer, 10, nullptr);
    }
    void Paint(Level& l) {
        if (!l.chrome.RenderTarget()) l.chrome.Resize();
        auto* t = l.chrome.RenderTarget(); auto* brush = l.chrome.SolidBrush(); if (!t || !brush || !l.items) return;
        const auto now = Motion::Now(); const float c = Design::Surface::canvas;
        t->BeginDraw(); t->Clear(D2D1::ColorF(c, c, c, 1));
        l.chrome.keyboardModality = l.keyboard;
        const float left = Layout::pad + 1, right = l.width - Layout::pad - 1;
        for (size_t i = 0; i < l.items->size(); ++i) {
            const Item& item = (*l.items)[i]; const float top = l.tops[i];
            if (item.separator) { brush->SetColor(D2D1::ColorF(1, 1, 1, .06f)); const float y = top + Layout::separator / 2 - .5f; t->FillRectangle(D2D1::RectF(left + Layout::padLeft, y, right - Layout::padLeft, y + 1), brush); continue; }
            const auto v = l.motion.Visual(static_cast<int>(i) + 1, now);
            // The open submenu's row keeps its fill while the pointer is in the submenu.
            const float h = item.enabled ? std::max(v.hover, static_cast<int>(i) == l.child ? 1.f : 0.f) : 0;
            const D2D1_RECT_F row{left, top, right, top + Layout::row};
            if (h > 0 || v.press > 0) { brush->SetColor(D2D1::ColorF(1, 1, 1, .08f * h + .04f * v.press)); t->FillRoundedRectangle(D2D1::RoundedRect(row, Layout::rowRadius, Layout::rowRadius), brush); }
            const float dim = item.enabled ? 1.f : Design::Text::disabled;
            float tail = right - Layout::padRight;
            if (!item.children.empty()) {
                const D2D1_RECT_F box{tail - Layout::trailing, top + (Layout::row - Layout::trailing) / 2, tail, top + (Layout::row + Layout::trailing) / 2};
                D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
                t->SetTransform(D2D1::Matrix3x2F::Rotation(-90, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
                l.chrome.Icon(L"caret", box, 16, .7f * dim); t->SetTransform(base); tail -= Layout::trailing + Layout::shortcutGap;
            } else if (item.checked) {
                const D2D1_RECT_F box{tail - Layout::trailing, top + (Layout::row - Layout::trailing) / 2, tail, top + (Layout::row + Layout::trailing) / 2};
                // Icons ship at 50%: two passes give the check its ~75% weight.
                l.chrome.Icon(L"check", box, 14, dim); l.chrome.Icon(L"check", box, 14, dim); tail -= Layout::trailing + Layout::shortcutGap;
            }
            if (!item.shortcut.empty()) {
                const float w = l.chrome.TextWidth(item.shortcut, Layout::text, false, true);
                l.chrome.Text(item.shortcut, {tail - w - 1, top, tail + 1, top + Layout::row}, Layout::text, Design::Text::disabled * dim, true);
                tail -= w + Layout::shortcutGap;
            }
            l.chrome.Text(Plain(item.label), {left + Layout::padLeft, top, std::max(left + Layout::padLeft + 8, std::min(tail + Layout::shortcutGap - 8, right - Layout::padRight)), top + Layout::row}, Layout::text, (.9f + .1f * h) * dim);
        }
        // Hairline on the region's rounded outline (DWM no longer draws one).
        const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
        brush->SetColor(D2D1::ColorF(1, 1, 1, .06f));
        t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, l.width - px / 2, l.height - px / 2), Layout::radius, Layout::radius), brush, px);
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) l.chrome.RecreateTarget();
        if (!l.motion.NextWake(now).delay) KillTimer(l.hwnd, FrameTimer);
    }
    // Keyboard for the whole stack: the deepest open level owns it.
    bool Key(WPARAM key) {
        if (!depth_) return false;
        const int index = depth_ - 1; Level& l = *levels_[index];
        switch (key) {
        case VK_DOWN: Move(index, 1); return true;
        case VK_UP: Move(index, -1); return true;
        case VK_HOME: Move(index, 1, true); return true;
        case VK_END: Move(index, -1, true); return true;
        case VK_RIGHT: if (l.hover >= 0 && !(*l.items)[l.hover].children.empty()) Activate(index, l.hover); return true;
        case VK_LEFT: if (index > 0) CloseFrom(index); return true;
        case VK_ESCAPE: if (index > 0) CloseFrom(index); else Finish(0); return true;
        case VK_RETURN: case VK_SPACE: if (l.hover >= 0) Activate(index, l.hover); return true;
        case VK_TAB: Move(index, GetKeyState(VK_SHIFT) < 0 ? -1 : 1); return true;
        case VK_MENU: case VK_F10: Finish(0); return true;
        default: break;
        }
        // Access keys: a unique match runs, several cycle through.
        const UINT ch = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_CHAR); if (!ch) return true;
        const wchar_t letter = static_cast<wchar_t>(towlower(static_cast<wchar_t>(ch)));
        std::vector<int> matches;
        for (size_t i = 0; i < l.items->size(); ++i) { wchar_t m = 0; Plain((*l.items)[i].label, &m); if (m == letter && Selectable((*l.items)[i])) matches.push_back(static_cast<int>(i)); }
        if (matches.size() == 1) Activate(index, matches[0]);
        else if (!matches.empty()) { auto it = std::upper_bound(matches.begin(), matches.end(), l.hover); SetHover(index, it == matches.end() ? matches[0] : *it, true); }
        return true;
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM lp) {
        auto* self = reinterpret_cast<Menu*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Menu*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        return self ? self->Handle(h, msg, w, lp) : DefWindowProcW(h, msg, w, lp);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM lp) {
        const int index = IndexOf(h); Level* l = index >= 0 ? levels_[index].get() : nullptr;
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); if (l) Paint(*l); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ACTIVATE:
            // Another window took activation (Alt+Tab, a click elsewhere): dismiss.
            if (LOWORD(w) == WA_INACTIVE && !done_ && depth_ && !LevelOf(reinterpret_cast<HWND>(lp))) Finish(0);
            return 0;
        case WM_TIMER:
            if (!l) return 0;
            if (w == FrameTimer) { InvalidateRect(h, nullptr, FALSE); return 0; }
            if (w == SubmenuTimer) { KillTimer(h, SubmenuTimer); if (l->hover >= 0 && !(*l->items)[l->hover].children.empty()) OpenChild(index); else if (l->hover >= 0 && l->child >= 0 && l->hover != l->child) CloseFrom(index + 1); }
            return 0;
        case WM_MOUSEMOVE: {
            if (!l) return 0;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track);
            const int row = HitRow(*l, GET_Y_LPARAM(lp) / Scale(*l));
            if (row >= 0 && !(*l->items)[row].enabled) { SetHover(index, -1, false); return 0; }
            SetHover(index, row, false); return 0;
        }
        case WM_MOUSELEAVE: if (l && !l->keyboard && l->child < 0) SetHover(index, -1, false); return 0;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: if (l) { l->pressed = HitRow(*l, GET_Y_LPARAM(lp) / Scale(*l)); Interact(*l); } return 0;
        case WM_LBUTTONUP: case WM_RBUTTONUP: {
            if (!l) return 0;
            const int row = HitRow(*l, GET_Y_LPARAM(lp) / Scale(*l)); l->pressed = -1; Interact(*l);
            if (row >= 0) Activate(index, row);
            return 0;
        }
        }
        return DefWindowProcW(h, msg, w, lp);
    }
public:
    ~Menu() { Dispose(); }
    void Init(HINSTANCE instance, HWND owner, std::wstring root) {
        instance_ = instance; owner_ = owner; root_ = std::move(root);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"LisiereContextMenu";
        RegisterClassW(&wc);
    }
    // Release Direct2D/WIC before CoUninitialize; the owner destroys the windows.
    void Dispose() { for (auto& l : levels_) l->chrome.Dispose(); levels_.clear(); depth_ = 0; }
    bool Open() const { return depth_ > 0; }
    // Modal: returns the chosen item's id, 0 when dismissed. `at` is a screen
    // point; `exclude` (keyboard-opened menus) is the control to drop below.
    // alignRight: a keyboard-opened menu ends at the control's right edge (gear-side controls).
    int Track(const std::vector<Item>& items, POINT at, const RECT* exclude = nullptr, bool keyboard = false, bool reduced = false, bool alignRight = false) {
        if (!owner_ || items.empty() || depth_) return 0;
        result_ = 0; done_ = false; reduced_ = reduced;
        int flipX = at.x, flipY = at.y;
        const bool fromRight = exclude && alignRight;
        if (exclude) { at = {fromRight ? exclude->right : exclude->left, exclude->bottom}; flipX = fromRight ? exclude->right : exclude->right; flipY = exclude->top; }
        Open(0, items, at, flipX, flipY, true, fromRight);
        if (!depth_) return 0;
        if (keyboard) Move(0, 1, true);
        MSG msg;
        while (!done_ && depth_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
            // A press outside every level dismisses the menu and is swallowed, like Windows menus.
            const bool press = msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_MBUTTONDOWN ||
                               msg.message == WM_NCLBUTTONDOWN || msg.message == WM_NCRBUTTONDOWN || msg.message == WM_NCMBUTTONDOWN;
            if (press && !LevelOf(msg.hwnd)) { Finish(0); break; }
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) { if (Key(msg.wParam)) continue; }
            if (msg.message == WM_CHAR || msg.message == WM_SYSCHAR || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { PostQuitMessage(static_cast<int>(msg.wParam)); break; }
        }
        CloseFrom(0);
        return result_;
    }
};
} // namespace ContextMenu
} // namespace NativeChrome
