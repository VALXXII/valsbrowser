#pragma once
#include "i18n.h"
// #121 « Bulle de vérification · A » (Paper 1LJW-0, page « #121 — Fenêtres du moteur à notre design »): our bubble in
// place of the engine's « Insérez un symbole @ ». Security contract P14: design/security-review-autofill-under-field.md.
// Native, never activated, click-through (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, HTTRANSPARENT), no button: nothing in
// it can be chosen. Under the field, no arrow, inside the tab's page area only (Forms::Place).
//
// Paper values (get_jsx): #171717, border 1 #FFFFFF0F, radius 10, padding 6 / 8, gap 8, max width 320;
//   icon slot 16×20, warning-circle (fill) 16 in --color-browser-danger (#FF7B6E);
//   text 14/20 Regular #FFFFFFE6 (it wraps inside the 320 maximum).
// Placement: 6 DIP under the field (board: y 288 under 249.6 + 32), left edges aligned.
// Not drawn by the board (listed in the #121 report): the grey example line the #121 comment mentions (absent from
// the board, so not shown), the duration (the engine's: 5 s, or at once when the person types, leaves the field,
// scrolls, navigates or switches tab), the other validity states (same bubble, our words: Forms::InvalidText).
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "tooltip.h"

namespace NativeChrome::ValidationBubble {
namespace Glyph {
inline constexpr char warningCircle[] = "M128,24A104,104,0,1,0,232,128,104.11,104.11,0,0,0,128,24Zm-8,56a8,8,0,0,1,16,0v56a8,8,0,0,1-16,0Zm8,104a12,12,0,1,1,12-12A12,12,0,0,1,128,184Z";
}
namespace Layout {
inline constexpr float maxWidth = 320, border = 1, padY = 6, padX = 8, gap = 8, radius = 10, iconSlot = 16, iconLine = 20, icon = 16;
inline constexpr float textSize = 14, line = 20, anchorGap = 6;
inline constexpr unsigned surface = 0x171717;
inline constexpr float borderAlpha = 0x0F / 255.f, textAlpha = 0xE6 / 255.f;
inline constexpr UINT hideMs = 5000;
}
class Bubble {
    HWND hwnd_ = nullptr; Chrome chrome_; Chrome* surface_ = &chrome_; PopupKit::GlyphCache glyphs_;
    std::wstring text_; float width_ = 0, height_ = 0; bool visible_ = false;
    static constexpr UINT_PTR HideTimer = 0x121;
    void Measure(Chrome& c) {
        using namespace Layout; const PopupKit::Type type{textSize, line};
        const float inner = maxWidth - 2 * border - 2 * padX - iconSlot - gap;
        const auto one = PopupKit::Measure(c, text_, 8192, type, false);
        const auto wrapped = one.width > inner ? PopupKit::Measure(c, text_, inner, type, true) : one;
        const float textW = std::min(inner, one.width), textH = std::max(line, wrapped.lines > 1 ? wrapped.height : line);
        width_ = 2 * border + 2 * padX + iconSlot + gap + textW;   // CSS keeps the fraction (Paper 266.6): the window rounds it
        height_ = 2 * border + 2 * padY + std::max(iconLine, textH);
    }
    void Paint() {
        Chrome& c = *surface_; using namespace Layout;
        if (!c.RenderTarget()) c.Resize();
        auto* t = c.RenderTarget(); if (!t || !c.SolidBrush()) return;
        t->BeginDraw(); t->Clear(PopupKit::Hex(surface)); PopupKit::Fill(c, {0, 0, width_, height_}, radius, PopupKit::Hex(surface));
        const float left = border + padX, top = border + padY;
        glyphs_.Draw(c, Glyph::warningCircle, {left, top, left + iconSlot, top + iconLine}, icon, PopupKit::Danger());
        PopupKit::DrawText(c, text_, {left + iconSlot + gap, top, width_ - border - padX, height_}, PopupKit::Type{textSize, line}, PopupKit::White(textAlpha), true);
        PopupKit::Outline(c, width_, height_, radius, PopupKit::White(borderAlpha));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) c.RecreateTarget();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Bubble*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Bubble*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        if (!self) return DefWindowProcW(h, msg, w, l);
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); self->Paint(); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;   // click-through: it holds no action
        case WM_SIZE: self->chrome_.Resize(); return 0;
        case WM_TIMER: if (w == HideTimer) { KillTimer(h, w); self->Hide(); } return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Bubble() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root) {
        if (!PopupKit::Register(instance, L"BrowserValidationBubble", Proc)) return false;
        if (!PopupKit::CreatePopup(instance, owner, L"BrowserValidationBubble", TR(forms_bubble_window_name), WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, this, Layout::radius)) return false;
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() { glyphs_.Clear(); chrome_.Dispose(); if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); hwnd_ = nullptr; visible_ = false; }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    const std::wstring& Text() const { return text_; }
    // Screen pixels of the bubble for `text` at `dpi`.
    SIZE SizeFor(const std::wstring& text, UINT dpi) {
        text_ = text; Measure(chrome_);
        return {static_cast<LONG>(std::lround(width_ * dpi / 96.f)), static_cast<LONG>(std::lround(height_ * dpi / 96.f))};
    }
    void Show(const std::wstring& text, const RECT& at) {
        if (!hwnd_) return;
        text_ = text; Measure(chrome_);
        SetWindowPos(hwnd_, HWND_TOP, at.left, at.top, at.right - at.left, at.bottom - at.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PopupKit::SetAlpha(hwnd_, 1); chrome_.Resize();
        visible_ = true; InvalidateRect(hwnd_, nullptr, FALSE); UpdateWindow(hwnd_);
        SetTimer(hwnd_, HideTimer, Layout::hideMs, nullptr);
    }
    void Hide() {
        if (!hwnd_ || !visible_) return;
        visible_ = false; KillTimer(hwnd_, HideTimer); ShowWindow(hwnd_, SW_HIDE);
    }
    // Tests: the same Paint() into an offscreen PNG.
    bool RenderForTest(const std::wstring& root, const std::wstring& text, UINT dpi, const wchar_t* path) {
        text_ = text; Chrome measure; if (FAILED(measure.InitOffscreen(root, dpi, 400, 100))) return false; Measure(measure); measure.Dispose();
        return PopupKit::RenderPng(root, width_, height_, dpi, path, surface_, [this] { Paint(); });
    }
    float WidthForTest() const { return width_; }
    float HeightForTest() const { return height_; }
};
} // namespace NativeChrome::ValidationBubble
