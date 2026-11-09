#pragma once
// Link preview ("status bubble") shown while a link is hovered: Paper page
// "Aperçu URL — composant", frame "Aperçu URL — contexte bas gauche" (user design).
// It replaces WebView2's own status bar (put_IsStatusBarEnabled(FALSE)); the text comes
// from ICoreWebView2_12::StatusBarText.
//
// Design values (Paper, exact): flush with the page's bottom-left corner, height 23,
// padding 6 left/right, #171717 surface, top-right radius 8 (other corners square; the
// page's own rounded bottom-left corner clips it), max width 393, URL in Inter Display
// Regular 14, tracking 0.01em, white at 50 %, one line, trailing ellipsis.
// The bubble hugs its text up to 393 DIP (and never wider than its page).
//
// The pages are windowed WebView2 children, so the bubble is a small layered popup owned
// by the main window: per-pixel alpha (antialiased corners), never activated and
// transparent to the mouse (WS_EX_TRANSPARENT), so it never steals a click from the page.
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace NativeChrome { namespace StatusBubble {
namespace Layout {
inline constexpr float height = 23, padX = 6, maxWidth = 393, radius = 8, text = 14, tracking = .14f /* 0.01em */, opacity = .5f;
inline constexpr float surface = 23 / 255.f; // #171717
}

class Bubble {
public:
    ~Bubble() { Dispose(); }
    bool Init(HINSTANCE instance, HWND owner, ID2D1Factory* factory, IDWriteFactory* write, IDWriteFontCollection* fonts) {
        owner_ = owner; factory_ = factory; write_ = write; fonts_ = fonts;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"BrowserStatusBubble"; RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
                                WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, nullptr);
        return hwnd_ != nullptr;
    }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    const std::wstring& Text() const { return text_; }
    // Width in DIPs the bubble takes for `text` inside a page `pageWidth` DIPs wide.
    float WidthFor(const std::wstring& text, float pageWidth) {
        return std::max(0.f, std::min({Layout::maxWidth, std::ceil(TextWidth(text)) + 2 * Layout::padX, pageWidth}));
    }
    // `page`: the hovered page in the owner's client pixels. Hidden when text is empty.
    void Show(const std::wstring& text, RECT page) {
        if (!hwnd_ || !factory_ || IsIconic(owner_)) { Hide(); return; }
        if (text.empty() || page.right <= page.left || page.bottom <= page.top) { Hide(); return; }
        const UINT dpi = GetDpiForWindow(owner_); const float scale = dpi / 96.f;
        const float wDip = WidthFor(text, (page.right - page.left) / scale);
        const int w = std::max(1, (int)std::lround(wDip * scale)), h = std::max(1, (int)std::lround(Layout::height * scale));
        POINT origin{page.left, page.bottom - h}; ClientToScreen(owner_, &origin);
        if (text != text_ || w != w_ || h != h_ || dpi != dpi_ || !drawn_) {
            text_ = text;
            if (!Render(w, h, dpi, wDip)) { Hide(); return; }
        }
        POINT src{0, 0}; SIZE size{w_, h_}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, nullptr, &origin, &size, dc_, &src, 0, &blend, ULW_ALPHA);
        if (!visible_) { SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW); visible_ = true; }
        shown_ = {origin.x, origin.y, origin.x + w_, origin.y + h_};
    }
    void Hide() {
        if (hwnd_ && visible_) ShowWindow(hwnd_, SW_HIDE);
        visible_ = false; text_.clear(); shown_ = {}; Release(); // nothing held at rest
    }
    // Screen rectangle while shown (tests).
    RECT ScreenRect() const { return shown_; }
    // Evidence captures: composite the current surface over a client-area image.
    void BlendInto(HDC target, POINT clientOrigin) const {
        if (!visible_ || !dc_) return;
        POINT p{shown_.left, shown_.top}; ScreenToClient(owner_, &p);
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        GdiAlphaBlend(target, clientOrigin.x + p.x, clientOrigin.y + p.y, w_, h_, dc_, 0, 0, w_, h_, blend);
    }
    void Dispose() { Hide(); if (format_) format_->Release(); format_ = nullptr; if (hwnd_) DestroyWindow(hwnd_); hwnd_ = nullptr; }
private:
    HWND hwnd_ = nullptr, owner_ = nullptr; ID2D1Factory* factory_ = nullptr; IDWriteFactory* write_ = nullptr; IDWriteFontCollection* fonts_ = nullptr;
    IDWriteTextFormat* format_ = nullptr; std::wstring text_; bool visible_ = false, drawn_ = false; RECT shown_{};
    HDC dc_ = nullptr; HBITMAP bitmap_ = nullptr; HGDIOBJ old_ = nullptr; int w_ = 0, h_ = 0; UINT dpi_ = 0;
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_NCHITTEST) return HTTRANSPARENT;
        if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(h, msg, w, l);
    }
    IDWriteTextFormat* Format() {
        if (format_ || !write_) return format_;
        if (FAILED(write_->CreateTextFormat(L"Inter Display", fonts_, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Layout::text, L"fr-FR", &format_))) format_ = nullptr;
        if (format_) { format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
        return format_;
    }
    IDWriteTextLayout* MakeLayout(const std::wstring& s, float width) {
        auto* format = Format(); if (!format || s.empty()) return nullptr;
        IDWriteTextLayout* layout = nullptr; if (FAILED(write_->CreateTextLayout(s.c_str(), (UINT32)s.size(), format, std::max(1.f, width), Layout::height, &layout))) return nullptr;
        IDWriteTextLayout1* l1 = nullptr;
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), (void**)&l1)) && l1) { l1->SetCharacterSpacing(0, Layout::tracking, 0, {0, (UINT32)s.size()}); l1->Release(); }
        return layout;
    }
    float TextWidth(const std::wstring& s) {
        auto* layout = MakeLayout(s, 4096); if (!layout) return 0;
        DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m); layout->Release(); return m.widthIncludingTrailingWhitespace;
    }
    bool Surface(int w, int h) {
        if (dc_ && w == w_ && h == h_) return true;
        Release();
        HDC screen = GetDC(nullptr); dc_ = CreateCompatibleDC(screen); ReleaseDC(nullptr, screen); if (!dc_) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr; bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap_) { Release(); return false; }
        old_ = SelectObject(dc_, bitmap_); w_ = w; h_ = h; return true;
    }
    void Release() {
        if (dc_ && old_) SelectObject(dc_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nullptr; bitmap_ = nullptr; old_ = nullptr; w_ = h_ = 0; drawn_ = false;
    }
    bool Render(int w, int h, UINT dpi, float wDip) {
        if (!Surface(w, h)) return false;
        dpi_ = dpi;
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), (float)dpi, (float)dpi);
        ID2D1DCRenderTarget* target = nullptr; if (FAILED(factory_->CreateDCRenderTarget(&props, &target)) || !target) return false;
        RECT all{0, 0, w, h}; bool ok = false;
        if (SUCCEEDED(target->BindDC(dc_, &all))) {
            ID2D1SolidColorBrush* brush = nullptr;
            target->BeginDraw(); target->Clear(D2D1::ColorF(0, 0, 0, 0)); target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            if (SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(Layout::surface, Layout::surface, Layout::surface), &brush)) && brush) {
                const float hDip = h * 96.f / dpi, r = std::min(Layout::radius, std::min(wDip, hDip));
                // Square everywhere but the top-right corner: a rectangle plus a rounded square there.
                target->FillRectangle(D2D1::RectF(0, r, wDip, hDip), brush);
                target->FillRectangle(D2D1::RectF(0, 0, wDip - r, r), brush);
                target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(std::max(0.f, wDip - 2 * r), 0, wDip, 2 * r), r, r), brush);
                if (auto* layout = MakeLayout(text_, std::max(1.f, wDip - 2 * Layout::padX))) {
                    DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m);
                    if (m.widthIncludingTrailingWhitespace > wDip - 2 * Layout::padX) {
                        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr;
                        write_->CreateEllipsisTrimmingSign(layout, &ellipsis); layout->SetTrimming(&trim, ellipsis); if (ellipsis) ellipsis->Release();
                    }
                    brush->SetColor(D2D1::ColorF(1, 1, 1, Layout::opacity));
                    target->PushAxisAlignedClip(D2D1::RectF(Layout::padX, 0, wDip - Layout::padX, hDip), D2D1_ANTIALIAS_MODE_ALIASED);
                    target->DrawTextLayout(D2D1::Point2F(Layout::padX, 0), layout, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
                    target->PopAxisAlignedClip(); layout->Release();
                }
                brush->Release();
            }
            ok = SUCCEEDED(target->EndDraw());
        }
        target->Release(); drawn_ = ok; return ok;
    }
};
}}
