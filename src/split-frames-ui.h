#pragma once
// Split view hover frames: the painter and the two layered child windows. The geometry, the show / hide state machine,
// the morph's poses and the Paper values: src/split-frames.h (Val's boards « Fenêtre » 27E0-0 / 2793-0).
//
// The pages are windowed WebView2 children of another process: nothing the main window paints can cover them. Each
// frame is a small WS_EX_LAYERED child window of the main window raised above the pages (as the separator grips and the
// page corner masks), sized to the frame's final box; the morph's picture moves inside it and the window clips it, as
// the pane would. Neither window is ever activated (MA_NOACTIVATE). The actions frame takes the pointer (hover, press,
// click on « réduire » / « fermer »); a click on the site frame activates its pane. Nothing is kept while hidden but
// the two empty windows.
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include "favicon.h"
#include "svg-path.h"
#include "split-frames.h"

namespace NativeChrome { namespace SplitFrames {

// Phosphor paths, the app's own icons (assets/icons/minimize.svg, assets/icons/close.svg).
namespace Glyph {
inline constexpr const char* minimize = "M228,128a12,12,0,0,1-12,12H40a12,12,0,0,1,0-24H216A12,12,0,0,1,228,128Z";
inline constexpr const char* close = "M208.49,191.51a12,12,0,0,1-17,17L128,145,64.49,208.49a12,12,0,0,1-17-17L111,128,47.51,64.49a12,12,0,0,1,17-17L128,111l63.51-63.52a12,12,0,0,1,17,17L145,128Z";
}

struct Content {
    Box pane;                                   // owner client DIP
    std::wstring name, address;
    std::shared_ptr<const FaviconImage> favicon;
    float progress = 1; bool reduced = false;
    int hover = NoAction, press = NoAction;     // the actions frame's glyph under the pointer / held
};

class Painter {
public:
    IDWriteFactory* write = nullptr; IDWriteFontCollection* fonts = nullptr;
    ~Painter() { Forget(); if (format_) format_->Release(); }
    // Natural width of a 12 DIP line (name or address).
    float TextWidth(const std::wstring& s) {
        IDWriteTextLayout* l = Layout(s, 4096); if (!l) return 0;
        DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release(); return m.widthIncludingTrailingWhitespace;
    }
    SiteLayout Site(const Content& c) { return LayoutSite(c.pane, std::ceil(TextWidth(c.name)), std::ceil(TextWidth(c.address))); }
    // A frame's picture in owner DIP (the caller's transform places the frame's box); `pose` moves it inside its box.
    void DrawSite(ID2D1RenderTarget* t, const SiteLayout& l, const Content& c) {
        if (!t || !l.shown) return;
        const Pose pose = PoseAt(c.progress, c.reduced, false);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base); t->SetTransform(D2D1::Matrix3x2F::Translation(pose.dx, pose.dy) * base);
        Surface(t, l.frame, false, pose.surface);
        if (pose.content > 0) {
            Favicon(t, c.favicon, l.icon, pose.content);
            Text(t, c.name, l.name, Layout::nameAlpha * pose.content);
            if (l.address) Text(t, c.address, l.addressBox, Layout::addressAlpha * pose.content);
        }
        t->SetTransform(base);
    }
    void DrawActions(ID2D1RenderTarget* t, const ActionsLayout& a, const Content& c) {
        if (!t || !a.shown) return;
        const Pose pose = PoseAt(c.progress, c.reduced, true);
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base); t->SetTransform(D2D1::Matrix3x2F::Translation(pose.dx, pose.dy) * base);
        Surface(t, a.frame, true, pose.surface);
        if (pose.content > 0) {
            auto alpha = [&](int which) { return (c.press == which ? Layout::glyphPress : c.hover == which ? Layout::glyphHover : Layout::glyphAlpha) * pose.content; };
            GlyphAt(t, Glyph::minimize, minimize_, a.minimize.left, a.minimize.top + Layout::MinimizeOffset(), Layout::minimizeGlyph, alpha(Minimize));
            const float inset = (Layout::glyphBox - Layout::closeGlyph) / 2;
            GlyphAt(t, Glyph::close, close_, a.close.left + inset, a.close.top + inset, Layout::closeGlyph, alpha(Close));
        }
        t->SetTransform(base);
    }
    // Device resources follow one render target; dropped with it.
    void Forget() { if (minimize_) minimize_->Release(); if (close_) close_->Release(); minimize_ = close_ = nullptr; factory_ = nullptr; }
private:
    IDWriteTextFormat* format_ = nullptr; ID2D1Factory* factory_ = nullptr;
    ID2D1PathGeometry* minimize_ = nullptr; ID2D1PathGeometry* close_ = nullptr;
    IDWriteTextLayout* Layout(const std::wstring& s, float width) {
        if (!write || s.empty()) return nullptr;
        if (!format_) {
            if (FAILED(write->CreateTextFormat(L"Inter Display", fonts, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, Layout::font, L"fr-FR", &format_))) { format_ = nullptr; return nullptr; }
            format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr;
            if (SUCCEEDED(write->CreateEllipsisTrimmingSign(format_, &ellipsis))) { format_->SetTrimming(&trim, ellipsis); ellipsis->Release(); }
        }
        IDWriteTextLayout* l = nullptr;
        if (FAILED(write->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), format_, std::max(1.f, width), Layout::height, &l))) return nullptr;
        IDWriteTypography* typo = nullptr;   // the boards' Inter Display variants: cv01-cv04, cv13, tnum
        if (SUCCEEDED(write->CreateTypography(&typo))) {
            for (auto tag : {DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '1'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '2'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '3'),
                             DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '4'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '1', '3'), DWRITE_MAKE_OPENTYPE_TAG('t', 'n', 'u', 'm')})
                typo->AddFontFeature({static_cast<DWRITE_FONT_FEATURE_TAG>(tag), 1});
            l->SetTypography(typo, {0, static_cast<UINT32>(s.size())}); typo->Release();
        }
        return l;
    }
    void Text(ID2D1RenderTarget* t, const std::wstring& s, const Box& b, float alpha) {
        IDWriteTextLayout* l = Layout(s, b.W() + 1); if (!l) return;   // +1: a natural width never trims on rounding
        ID2D1SolidColorBrush* brush = nullptr;
        if (SUCCEEDED(t->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &brush))) {
            t->PushAxisAlignedClip(D2D1::RectF(b.left, b.top, b.right + 1, b.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
            t->DrawTextLayout({b.left, b.top}, l, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP); t->PopAxisAlignedClip(); brush->Release();
        }
        l->Release();
    }
    // The canvas cut into the page: square on the pane's edges, its inner bottom corner rounded 6.
    static void Surface(ID2D1RenderTarget* t, const Box& b, bool actions, float alpha) {
        if (alpha <= 0) return;
        ID2D1Factory* f = nullptr; t->GetFactory(&f); if (!f) return;
        ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* s = nullptr; const float r = Layout::radius;
        if (SUCCEEDED(f->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&s))) {
            s->BeginFigure({b.left, b.top}, D2D1_FIGURE_BEGIN_FILLED); s->AddLine({b.right, b.top});
            if (actions) {   // bottom-left rounded
                s->AddLine({b.right, b.bottom}); s->AddLine({b.left + r, b.bottom});
                s->AddArc(D2D1::ArcSegment({b.left, b.bottom - r}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            } else {         // bottom-right rounded
                s->AddLine({b.right, b.bottom - r});
                s->AddArc(D2D1::ArcSegment({b.right - r, b.bottom}, {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                s->AddLine({b.left, b.bottom});
            }
            s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release();
            ID2D1SolidColorBrush* brush = nullptr; const float c = Design::Surface::canvas;
            if (SUCCEEDED(t->CreateSolidColorBrush(D2D1::ColorF(c, c, c, alpha), &brush))) { t->FillGeometry(g, brush); brush->Release(); }
        }
        if (g) g->Release(); f->Release();
    }
    // The favicon fitted (aspect kept) and centred in its 12 x 12 box, corners 2; none yet: a white 90 % square.
    static void Favicon(ID2D1RenderTarget* t, const std::shared_ptr<const FaviconImage>& image, const Box& b, float alpha) {
        ID2D1SolidColorBrush* fill = nullptr;
        if (!image || !image->width || !image->height || image->pixels.empty()) {
            if (SUCCEEDED(t->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, .9f * alpha), &fill))) {
                t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(b.left, b.top, b.right, b.bottom), Layout::faviconRadius, Layout::faviconRadius), fill); fill->Release(); }
            return;
        }
        ID2D1Bitmap* bitmap = nullptr; auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(t->CreateBitmap(D2D1::SizeU(image->width, image->height), image->pixels.data(), image->stride, props, &bitmap)) || !bitmap) return;
        const float k = std::min(b.W() / image->width, b.H() / image->height), w = image->width * k, h = image->height * k;
        const D2D1_RECT_F r{b.left + (b.W() - w) / 2, b.top + (b.H() - h) / 2, b.left + (b.W() + w) / 2, b.top + (b.H() + h) / 2};
        ID2D1BitmapBrush* brush = nullptr;
        if (SUCCEEDED(t->CreateBitmapBrush(bitmap, D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR), D2D1::BrushProperties(alpha), &brush))) {
            brush->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(r.left, r.top));
            t->FillRoundedRectangle(D2D1::RoundedRect(r, Layout::faviconRadius, Layout::faviconRadius), brush); brush->Release();
        }
        bitmap->Release();
    }
    void GlyphAt(ID2D1RenderTarget* t, const char* d, ID2D1PathGeometry*& cache, float x, float y, float size, float alpha) {
        ID2D1Factory* f = nullptr; t->GetFactory(&f); if (!f) return;
        if (f != factory_) { Forget(); factory_ = f; }
        if (!cache) cache = DialogKit::SvgGeometry(f, d);
        f->Release(); if (!cache) return;
        ID2D1SolidColorBrush* brush = nullptr; if (FAILED(t->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, alpha), &brush))) return;
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base); const float k = size / 256;
        t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * base);
        t->FillGeometry(cache, brush); t->SetTransform(base); brush->Release();
    }
};

// The two windows. `Show` places and paints them for one content; `Hide` hides them.
class Frames {
public:
    Painter painter;
    std::function<void(int action)> action;   // « réduire » (Minimize) / « fermer » (Close) clicked
    std::function<void()> activate;           // the site frame clicked
    std::function<void()> changed;            // the pointer's hover / press on the actions changed (repaint)
    ~Frames() { Dispose(); }
    bool Init(HINSTANCE instance, HWND owner, ID2D1Factory* factory, IDWriteFactory* write, IDWriteFontCollection* fonts) {
        owner_ = owner; factory_ = factory; painter.write = write; painter.fonts = fonts;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"BrowserSplitFrame"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
        for (Surface* s : {&site_, &actions_}) {
            s->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_CHILD, 0, 0, 1, 1, owner, nullptr, instance, nullptr);
            if (!s->hwnd) return false;
            SetWindowLongPtrW(s->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        }
        return true;
    }
    // Owner client DIP. A progress of 0 hides both windows.
    void Show(const Content& c, const std::wstring& siteName, const std::wstring& actionsName) {
        if (!owner_ || !factory_ || c.progress <= 0) { Hide(); return; }
        content_ = c; content_.hover = hover_; content_.press = press_;
        site_.layout = painter.Site(content_); actions_.layoutActions = LayoutActions(content_.pane);
        Place(site_, site_.layout.shown ? site_.layout.frame : Box{}, siteName);
        Place(actions_, actions_.layoutActions.shown ? actions_.layoutActions.frame : Box{}, actionsName);
    }
    void Hide() {
        for (Surface* s : {&site_, &actions_}) { if (s->hwnd && s->visible) ShowWindow(s->hwnd, SW_HIDE); s->visible = false; s->key.clear(); Release(*s); }
        if (GetCapture() && (GetCapture() == actions_.hwnd || GetCapture() == site_.hwnd)) ReleaseCapture();
        hover_ = press_ = NoAction; tracking_ = false;
    }
    // Above the pages again (a page window created or re-placed lands on top).
    void Raise() { for (Surface* s : {&site_, &actions_}) if (s->hwnd && s->visible) SetWindowPos(s->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
    bool Visible() const { return site_.visible || actions_.visible; }
    int Hover() const { return hover_; }
    int Press() const { return press_; }
    HWND SiteWindow() const { return site_.hwnd; }
    HWND ActionsWindow() const { return actions_.hwnd; }
    const ActionsLayout& Actions() const { return actions_.layoutActions; }
    const SiteLayout& Site() const { return site_.layout; }
    // Tests: the two frames' pictures in owner DIP into the caller's target, as the windows show them.
    void PaintForTest(ID2D1RenderTarget* t, const Content& c) {
        auto s = painter.Site(c); painter.DrawSite(t, s, c); painter.DrawActions(t, LayoutActions(c.pane), c);
    }
    // Tests: the pointer on the actions frame (owner client DIP), as the window procedure sees it.
    void PointerForTest(UINT msg, float x, float y) { Pointer(actions_, msg, x, y); }
    void Dispose() {
        Hide();
        for (Surface* s : {&site_, &actions_}) { if (s->hwnd) DestroyWindow(s->hwnd); s->hwnd = nullptr; }
    }
private:
    struct Surface {
        HWND hwnd = nullptr; HDC dc = nullptr; HBITMAP bitmap = nullptr; HGDIOBJ old = nullptr; int w = 0, h = 0, x = 0, y = 0;
        ID2D1DCRenderTarget* target = nullptr; UINT dpi = 0; bool visible = false; std::wstring key, name;
        SiteLayout layout; ActionsLayout layoutActions;
    };
    Surface site_, actions_; Content content_;
    HWND owner_ = nullptr; ID2D1Factory* factory_ = nullptr;
    int hover_ = NoAction, press_ = NoAction; bool tracking_ = false;
    void Release(Surface& s) {
        if (s.target) s.target->Release(); s.target = nullptr; painter.Forget();
        if (s.dc && s.old) SelectObject(s.dc, s.old);
        if (s.bitmap) DeleteObject(s.bitmap);
        if (s.dc) DeleteDC(s.dc);
        s.dc = nullptr; s.bitmap = nullptr; s.old = nullptr; s.w = s.h = 0;
    }
    bool Bitmap(Surface& s, int w, int h) {
        if (s.dc && w == s.w && h == s.h) return true;
        Release(s);
        HDC screen = GetDC(nullptr); s.dc = CreateCompatibleDC(screen); ReleaseDC(nullptr, screen); if (!s.dc) return false;
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr; s.bitmap = CreateDIBSection(s.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!s.bitmap) { Release(s); return false; }
        s.old = SelectObject(s.dc, s.bitmap); s.w = w; s.h = h; return true;
    }
    void Place(Surface& s, const Box& frame, const std::wstring& name) {
        if (!s.hwnd || frame.W() <= 0) { if (s.visible) ShowWindow(s.hwnd, SW_HIDE); s.visible = false; s.key.clear(); return; }
        const UINT dpi = GetDpiForWindow(owner_); const float k = dpi / 96.f;
        const int x = static_cast<int>(std::lround(frame.left * k)), y = static_cast<int>(std::lround(frame.top * k));
        const int w = std::max(1, static_cast<int>(std::lround(frame.right * k)) - x), h = std::max(1, static_cast<int>(std::lround(frame.bottom * k)) - y);
        // Repaint only when the picture changes (the morph's frames, a hover, another page or size).
        wchar_t key[160]; swprintf(key, 160, L"%d %d %d %d %u %.4f %d %d %d %p", x, y, w, h, dpi, content_.progress, content_.hover, content_.press, content_.reduced ? 1 : 0, static_cast<const void*>(content_.favicon.get()));
        const std::wstring full = std::wstring(key) + L"|" + content_.name + L"|" + content_.address;
        if (name != s.name) { SetWindowTextW(s.hwnd, name.c_str()); s.name = name; }   // what screen readers read for the window
        if (s.visible && full == s.key) return;
        if (!Bitmap(s, w, h)) return;
        if (!s.target || s.dpi != dpi) {
            if (s.target) s.target->Release(); s.target = nullptr; painter.Forget(); s.dpi = dpi;
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), static_cast<float>(dpi), static_cast<float>(dpi));
            if (FAILED(factory_->CreateDCRenderTarget(&props, &s.target))) { s.target = nullptr; return; }
        }
        RECT all{0, 0, w, h};
        if (SUCCEEDED(s.target->BindDC(s.dc, &all))) {
            s.target->BeginDraw(); s.target->Clear(D2D1::ColorF(0, 0, 0, 0));
            s.target->SetTransform(D2D1::Matrix3x2F::Translation(-x / k, -y / k));   // owner DIP -> this window (device pixels stay whole)
            if (&s == &site_) painter.DrawSite(s.target, s.layout, content_); else painter.DrawActions(s.target, s.layoutActions, content_);
            if (s.target->EndDraw() == D2DERR_RECREATE_TARGET) { s.target->Release(); s.target = nullptr; painter.Forget(); }
        }
        if (!s.visible || x != s.x || y != s.y || w != s.w || h != s.h) SetWindowPos(s.hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        POINT src{0, 0}; SIZE size{w, h}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        HDC screen = GetDC(nullptr); UpdateLayeredWindow(s.hwnd, screen, nullptr, &size, s.dc, &src, 0, &blend, ULW_ALPHA); ReleaseDC(nullptr, screen);
        s.x = x; s.y = y; s.visible = true; s.key = full;
    }
    // Pointer on the actions frame, owner client DIP.
    void Pointer(Surface& s, UINT msg, float x, float y) {
        const int at = &s == &actions_ ? ActionAt(s.layoutActions, x, y) : NoAction; const int was = hover_ * 4 + press_;
        switch (msg) {
        case WM_MOUSEMOVE: hover_ = at; break;
        case WM_MOUSELEAVE: hover_ = NoAction; if (!(GetCapture() == s.hwnd)) press_ = NoAction; break;
        case WM_LBUTTONDOWN: hover_ = at; press_ = at; break;
        case WM_LBUTTONUP: { const int pressed = press_; press_ = NoAction; hover_ = at;
            if (pressed != NoAction && pressed == at && action) { if (changed) changed(); action(at); return; }
            break; }
        }
        if (hover_ * 4 + press_ != was && changed) changed();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Frames*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!self) return DefWindowProcW(h, msg, w, l);
        Surface& s = h == self->actions_.hwnd ? self->actions_ : self->site_;
        switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTCLIENT;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
        case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MOUSELEAVE: {
            if (msg == WM_MOUSEMOVE && !self->tracking_) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; self->tracking_ = TrackMouseEvent(&t) != FALSE; }
            if (msg == WM_MOUSELEAVE) self->tracking_ = false;
            if (msg == WM_LBUTTONDOWN) SetCapture(h);
            const float k = 96.f / GetDpiForWindow(self->owner_);
            const float x = (s.x + GET_X_LPARAM(l)) * k, y = (s.y + GET_Y_LPARAM(l)) * k;
            if (&s == &self->site_) { if (msg == WM_LBUTTONUP) { if (GetCapture() == h) ReleaseCapture(); if (self->activate) self->activate(); } return 0; }
            const bool up = msg == WM_LBUTTONUP;
            self->Pointer(s, msg, x, y);
            if (up && GetCapture() == h) ReleaseCapture();
            return 0;
        }
        case WM_CAPTURECHANGED: if (self->press_ != NoAction) { self->press_ = NoAction; if (self->changed) self->changed(); } return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
};

}}  // namespace NativeChrome::SplitFrames
