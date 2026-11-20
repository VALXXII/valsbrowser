#pragma once
#include "i18n.h"
// Suggestion list under the address bar (issue #20, Paper "Adresse · Édition").
//
// An owned, never-activated popup (WS_EX_NOACTIVATE + MA_NOACTIVATE): keyboard
// focus stays in the address EDIT, which drives the active row with the arrows
// (combobox / aria-activedescendant model). The host owns the data and every
// action; this window only draws rows and reports pointer intent.
//
// Motion: none. The list is opened, retargeted and dismissed dozens of times a
// day from the keyboard, so it appears and updates instantly (Raycast-style);
// row hover and the active row change without transition.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <functional>
#include <string>
#include "chrome.h"
#include "address.h"
#include "native-ui.h"
#include "tooltip.h"

namespace NativeChrome::AddressUI {
namespace Layout {
inline constexpr float pad = Design::Space::xs, row = Design::Size::control;          // 4, 32
inline constexpr float radius = Design::Radius::control + pad;                          // concentric: 6 + 4
inline constexpr float indent = Design::Space::xs;  // active/hovered row content shifts 4 DIP (Paper)
inline constexpr float slot = Design::Size::compact, gap = Design::Space::xs, noteGap = Design::Space::md;
inline constexpr float remove = Design::Size::compact, removeInset = Design::Space::xs;
inline constexpr float active = 20 / 255.f, hover = 10 / 255.f;                         // #FFFFFF14 / #FFFFFF0A
inline constexpr float anchorGap = Design::Space::sm;                                    // 6 DIP under the bar
inline constexpr float border = .02f;                                                    // hairline, as the popovers
inline float Height(size_t rows) { return pad * 2 + row * static_cast<float>(rows); }
}

struct Hooks {
    std::function<void(int)> open;   // row clicked
    std::function<void(int)> remove; // "Retirer de l'historique" clicked
    // Remembered favicon of a site (no network: only sites already visited).
    std::function<std::shared_ptr<const FaviconImage>(const std::wstring&)> icon;
};

class Suggestions {
    HWND hwnd_ = nullptr; Tooltip::Tools tip_;
    Chrome chrome_;
    Hooks hooks_;
    AddressBar::Suggestions data_;
    std::wstring engine_ = L"DuckDuckGo";
    int active_ = 0, hover_ = -1, pressed_ = -1;
    bool hoverRemove_ = false, pressRemove_ = false, tracking_ = false, visible_ = false;
    UINT dpi_ = 96;
    float width_ = 0; // DIP width of the last paint (hit tests use the same geometry)

    int Px(float dip) const { return static_cast<int>(std::lround(dip * dpi_ / 96.f)); }
    float Width() const { RECT r{}; if (hwnd_ && GetClientRect(hwnd_, &r) && r.right > 0) return r.right * 96.f / dpi_; return width_; }
    D2D1_RECT_F Row(int i) const { const float top = Layout::pad + Layout::row * i; return {Layout::pad, top, Width() - Layout::pad, top + Layout::row}; }
    bool Removable(int i) const { return i >= 0 && i < static_cast<int>(data_.rows.size()) && data_.rows[i].kind == AddressBar::Kind::Site && !data_.rows[i].popular; }
    D2D1_RECT_F RemoveBox(int i) const { auto r = Row(i); return {r.right - Layout::removeInset - Layout::remove, r.top + Layout::removeInset, r.right - Layout::removeInset, r.bottom - Layout::removeInset}; }
    int HitRow(float y) const { const int i = static_cast<int>(std::floor((y - Layout::pad) / Layout::row)); return y >= Layout::pad && i >= 0 && i < static_cast<int>(data_.rows.size()) ? i : -1; }
    static bool Inside(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

public:
    static const wchar_t* IconFor(const AddressBar::Suggestion& row) { return row.kind == AddressBar::Kind::Search ? L"search" : L"favicon"; }
    std::wstring Note(const AddressBar::Suggestion& row) const {
        switch (row.kind) {
        case AddressBar::Kind::Site: return row.title;
        case AddressBar::Kind::Address: return TR(suggest_open_address);
        default: return TRF(suggest_search_with, engine_);
        }
    }
    // Spoken form of a row for UI Automation.
    std::wstring Spoken(int i) const {
        if (i < 0 || i >= static_cast<int>(data_.rows.size())) return L"";
        const auto& row = data_.rows[i]; const auto note = Note(row);
        return row.text + (note.empty() ? L"" : L", " + note);
    }

    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        hooks_ = std::move(hooks);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserAddressSuggestions";
        RegisterClassW(&wc);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, TR(suggest_window_name), WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_) return false;
        // No DWM rounding or DWM border (rounding always casts a shadow): a window region
        // rounds it at Layout::radius and PaintRows draws the hairline DWM used to draw.
        NativeUI::ConfigureShadowlessPopup(hwnd_, Layout::radius);
        tip_.Create(instance, hwnd_, root);
        chrome_.textCacheLimit = 256;
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() { tip_.Dispose(); chrome_.Dispose(); hwnd_ = nullptr; visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    int Active() const { return active_; }
    const AddressBar::Suggestions& Data() const { return data_; }
    UINT RenderDpiForTest() const { return chrome_.dpi; }   // the DPI the rows are drawn at (tests/ui/address_popup_dpi_test.cpp)
    // Screen pixels of row `i` (UI Automation bounds).
    RECT RowScreen(int i) const {
        auto r = Row(i); RECT p{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)};
        if (hwnd_) MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&p), 2);
        return p;
    }
    RECT Bounds() const { RECT r{}; if (hwnd_) GetWindowRect(hwnd_, &r); return r; }

    // `left`, `right`: screen pixels of the address field; `top`: its bottom edge.
    void Show(AddressBar::Suggestions data, int active, std::wstring engine, int left, int top, int right, UINT dpi) {
        if (!hwnd_) return;
        if (data.rows.empty()) { Hide(); return; }
        const bool same = visible_ && data.rows.size() == data_.rows.size();
        data_ = std::move(data); engine_ = std::move(engine); dpi_ = dpi;
        active_ = std::clamp(active, 0, static_cast<int>(data_.rows.size()) - 1);
        if (!same) { hover_ = -1; hoverRemove_ = false; }
        if (hover_ >= static_cast<int>(data_.rows.size())) hover_ = -1;
        SetWindowPos(hwnd_, HWND_TOP, left, top + Px(Layout::anchorGap), right - left, Px(Layout::Height(data_.rows.size())), SWP_NOACTIVATE | SWP_SHOWWINDOW);
        visible_ = true; RefreshTip(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void Hide() {
        if (!hwnd_ || !visible_) return;
        visible_ = false; hover_ = pressed_ = -1; hoverRemove_ = pressRemove_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        ShowWindow(hwnd_, SW_HIDE);
    }
    void SetActive(int i) { if (i == active_) return; active_ = i; if (visible_) { InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_); } }

    // Draws into the current target in popup DIPs (also used by the native fixture).
    void PaintRows(Chrome& c, float width) {
        width_ = width;
        auto* target = c.RenderTarget(); auto* brush = c.SolidBrush();
        const float canvas = Design::Surface::canvas;
        target->Clear(D2D1::ColorF(canvas, canvas, canvas));
        for (int i = 0; i < static_cast<int>(data_.rows.size()); ++i) {
            const auto& row = data_.rows[i];
            const float top = Layout::pad + Layout::row * i;
            const D2D1_RECT_F r{Layout::pad, top, width - Layout::pad, top + Layout::row};
            const bool isActive = i == active_, isHover = i == hover_;
            if (isActive || isHover) {
                brush->SetColor(D2D1::ColorF(1, 1, 1, isActive ? Layout::active : Layout::hover));
                target->FillRoundedRectangle(D2D1::RoundedRect(r, Design::Radius::control, Design::Radius::control), brush);
            }
            const float shift = isActive || isHover ? Layout::indent : 0;
            const D2D1_RECT_F slot{r.left + shift, r.top + (Layout::row - Layout::slot) / 2, r.left + shift + Layout::slot, r.top + (Layout::row + Layout::slot) / 2};
            const auto image = hooks_.icon && !row.site.empty() && row.kind != AddressBar::Kind::Search ? hooks_.icon(row.site) : nullptr;
            if (!c.DrawSiteImage(image, slot)) c.Icon(IconFor(row), slot, Design::Size::icon, Design::Text::secondary);
            const bool removable = isHover && Removable(i);
            const auto removeBox = RemoveBox(i);
            const float right = (removable ? r.right - Layout::removeInset - Layout::remove - Layout::gap : r.right - Design::Space::md);
            float x = slot.right + Layout::gap;
            const auto& type = Design::Type::address;
            const auto note = Note(row);
            target->PushAxisAlignedClip(D2D1::RectF(x, r.top, right, r.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
            // Main label: matched characters in primary, the rest a step down (.7).
            const float rest = row.kind == AddressBar::Kind::Site ? .7f : 1.f;
            auto run = [&](const std::wstring& s, float opacity, bool medium) {
                if (s.empty() || x >= right) return;
                const float w = c.TextWidth(s, type.size, medium, false, type.tracking);
                c.Text(s, {x, r.top, x + w + 1, r.bottom}, type.size, opacity, false, medium, type.tracking);
                x += w;
            };
            const float cap = note.empty() ? right : x + (right - x) * .72f; // the note keeps room
            target->PushAxisAlignedClip(D2D1::RectF(x, r.top, cap, r.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
            if (row.match != std::wstring::npos && row.match + row.matchLength <= row.text.size()) {
                run(row.text.substr(0, row.match), rest, false);
                run(row.text.substr(row.match, row.matchLength), 1.f, true);
                run(row.text.substr(row.match + row.matchLength), rest, false);
            } else run(row.text, row.kind == AddressBar::Kind::Site ? rest : 1.f, false);
            target->PopAxisAlignedClip();
            x = std::min(x, cap);
            if (!note.empty() && x + Layout::noteGap < right) {
                x += Layout::noteGap;
                c.Text(note, {x, r.top, right, r.bottom}, type.size, Design::Text::secondary, false, false, type.tracking);
            }
            target->PopAxisAlignedClip();
            if (removable) {
                const auto box = removeBox;
                brush->SetColor(D2D1::ColorF(1, 1, 1, hoverRemove_ ? (pressRemove_ ? .16f : .1f) : .06f));
                target->FillRoundedRectangle(D2D1::RoundedRect(box, Design::Radius::button, Design::Radius::button), brush);
                c.Icon(L"detail-close", box, Design::Size::windowIcon * (pressRemove_ ? .96f : 1.f), .7f);
            }
        }
        // Hairline (Paper border 2 % white over the canvas), on the region's rounded outline.
        const auto size = target->GetSize(); const float px = size.width / std::max(1.f, static_cast<float>(target->GetPixelSize().width));
        brush->SetColor(D2D1::ColorF(1, 1, 1, Layout::border));
        target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, size.width - px / 2, size.height - px / 2), Layout::radius, Layout::radius), brush, px);
    }
    // Native fixture: paint `data` into an offscreen chrome of the given width.
    void SetIconSource(std::function<std::shared_ptr<const FaviconImage>(const std::wstring&)> icon) { hooks_.icon = std::move(icon); }
    void Preview(Chrome& c, AddressBar::Suggestions data, int active, int hover, std::wstring engine, float width) {
        data_ = std::move(data); active_ = active; hover_ = hover; engine_ = std::move(engine); PaintRows(c, width);
    }

private:
    void Paint() {
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* target = chrome_.RenderTarget(); if (!target) return;
        target->BeginDraw(); PaintRows(chrome_, Width());
        if (target->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void RefreshTip() {
        if (!tip_) return;
        tip_.Clear();
        if (!Removable(hover_)) return;
        const auto b = RemoveBox(hover_);
        tip_.Add(1, {Px(b.left), Px(b.top), Px(b.right), Px(b.bottom)}, TR(suggest_remove_tip));
    }
    void Pointer(float x, float y) {
        const int row = HitRow(y);
        const bool onRemove = Removable(row) && Inside(RemoveBox(row), x, y);
        if (row == hover_ && onRemove == hoverRemove_) return;
        const bool rowChanged = row != hover_;
        hover_ = row; hoverRemove_ = onRemove;
        if (rowChanged) RefreshTip();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Suggestions*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Suggestions*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / dpi_;
        const float x = GET_X_LPARAM(l) * scale, y = GET_Y_LPARAM(l) * scale;
        switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE; // the EDIT keeps focus
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_SIZE: chrome_.Resize(); InvalidateRect(h, nullptr, FALSE); return 0;
        // Shown on a monitor at another DPI: the move sizes the window first (WM_SIZE still reads the old DPI), then
        // the DPI changes. Redraw at the new one, keeping the size Show computed for it.
        case WM_DPICHANGED: chrome_.Resize(); InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_MOUSEMOVE:
            if (!tracking_) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); tracking_ = true; }
            Pointer(x, y); return 0;
        case WM_MOUSELEAVE: tracking_ = false; if (GetCapture() != h) { hover_ = -1; hoverRemove_ = false; RefreshTip(); InvalidateRect(h, nullptr, FALSE); } return 0;
        case WM_LBUTTONDOWN: {
            // Feedback on press, commit on release over the same target.
            Pointer(x, y); pressed_ = hover_; pressRemove_ = hoverRemove_;
            if (pressed_ >= 0) SetCapture(h);
            InvalidateRect(h, nullptr, FALSE); return 0;
        }
        case WM_LBUTTONUP: {
            const int was = pressed_; const bool wasRemove = pressRemove_;
            pressed_ = -1; pressRemove_ = false; if (GetCapture() == h) ReleaseCapture();
            Pointer(x, y);
            if (was >= 0 && was == hover_ && wasRemove == hoverRemove_) {
                if (wasRemove) { if (hooks_.remove) hooks_.remove(was); }
                else if (hooks_.open) hooks_.open(was);
            }
            if (visible_) InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED: pressed_ = -1; pressRemove_ = false; if (visible_) InvalidateRect(h, nullptr, FALSE); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
};
} // namespace NativeChrome::AddressUI
