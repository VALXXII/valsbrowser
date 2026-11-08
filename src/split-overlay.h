#pragma once
// Drag overlay of the split view: the lifted tab, the #3E67FF drop preview and the
// refusal pill (Paper boards 04-06, 11, 14, 16, 18, 19).
//
// The pages are windowed WebView2 children, so nothing drawn by the main window can
// cover them. A click-through, non-activating layered popup owned by the main window
// sits over its client area while a tab is dragged, and only then: it is created
// once, shown on lift, destroyed of all content (hidden) on release. No timer runs
// while it is hidden.
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <memory>
#include <string>
#include "favicon.h"
#include <map>
#include <vector>
#include <wincodec.h>
#include <wrl/client.h>
#include "split-view.h"

namespace NativeChrome { namespace SplitUI {

// Accent (Paper --color-browser-split-accent), reserved to split-view surfaces.
inline constexpr float accentR = 62 / 255.f, accentG = 103 / 255.f, accentB = 1.f, tintOpacity = .16f;

struct Plate { bool shown = false; Split::Box box; float opacity = 1; std::wstring title, detail; std::shared_ptr<const FaviconImage> favicon; };
struct Pill { bool shown = false; float x = 0, y = 0; std::wstring title, detail; };
struct Card { bool shown = false; Split::Box box; float opacity = 1; std::wstring title; std::shared_ptr<const FaviconImage> favicon; };
// Resize preview tile: a panel's last rendered frame shown at the panel's new rectangle
// while its page waits to reflow. `blur` (a 1/12 copy) is stretched to cover the whole
// rectangle; `full` sits at 1:1 from the top-left corner, as reflowed text would.
struct Tile { Split::Box box; std::shared_ptr<const FaviconImage> icon; std::wstring name, domain; };
// An outer bottom page corner: the page is clipped to a 6 DIP arc there. `right`: bottom-right.
struct Corner { float x = 0, y = 0; bool right = false; };
struct Frame {
    std::vector<Corner> corners;  // drawn over the resize preview tiles, as the corner masks below draw them
    Plate plate; Pill pill; Card card;
    std::vector<Tile> tiles; std::vector<Split::Box> gaps; float tileOpacity = 1;  // resize placeholders
    bool Empty() const { return !plate.shown && !pill.shown && !card.shown && (tiles.empty() || tileOpacity <= .01f); }
};

class Painter {
public:
    IDWriteFactory* write = nullptr; IDWriteFontCollection* fonts = nullptr;
    ~Painter() { Drop(regular); Drop(medium); }
    void Draw(ID2D1RenderTarget* target, const Frame& f) {
        if (!target) return;
        Formats();
        ID2D1SolidColorBrush* brush = nullptr; if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush))) return;
        if (!f.tiles.empty() && f.tileOpacity > .01f) DrawTiles(target, brush, f);
        if (f.plate.shown) DrawPlate(target, brush, f.plate);
        if (f.card.shown) DrawCard(target, brush, f.card);
        if (f.pill.shown) DrawPill(target, brush, f.pill);
        brush->Release();
    }
    // Device bitmaps for the tiles, valid for one render target; dropped with it.
    void ForgetBitmaps() { for (auto& b : bitmaps) if (b.second) b.second->Release(); bitmaps.clear(); }
    float TextWidth(const std::wstring& s, bool bold) {
        Formats(); IDWriteTextFormat* format = bold ? medium : regular; if (!write || !format || s.empty()) return 0;
        IDWriteTextLayout* layout = nullptr; if (FAILED(write->CreateTextLayout(s.c_str(), (UINT32)s.size(), format, 4096, 32, &layout))) return 0;
        DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m); layout->Release(); return m.widthIncludingTrailingWhitespace;
    }
private:
    IDWriteTextFormat* regular = nullptr; IDWriteTextFormat* medium = nullptr;
    std::map<IWICBitmap*, ID2D1Bitmap*> bitmaps;
    ID2D1Bitmap* Device(ID2D1RenderTarget* t, IWICBitmap* source) {
        if (!source) return nullptr; auto it = bitmaps.find(source); if (it != bitmaps.end()) return it->second;
        ID2D1Bitmap* b = nullptr; t->CreateBitmapFromWicBitmap(source, nullptr, &b); bitmaps[source] = b; return b;
    }
    // Rectangle with its own radius per corner (top-left, top-right, bottom-right, bottom-left).
    static ID2D1PathGeometry* Box(ID2D1RenderTarget* t, D2D1_RECT_F r, float tl, float tr, float br, float bl) {
        ID2D1Factory* factory = nullptr; t->GetFactory(&factory); if (!factory) return nullptr;
        ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* s = nullptr; const bool ok = SUCCEEDED(factory->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&s)); factory->Release();
        if (!ok) { if (g) g->Release(); return nullptr; }
        auto arc = [&](D2D1_POINT_2F to, float radius) { if (radius > 0) s->AddArc(D2D1::ArcSegment(to, {radius, radius}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL)); else s->AddLine(to); };
        s->BeginFigure({r.left + tl, r.top}, D2D1_FIGURE_BEGIN_HOLLOW); s->AddLine({r.right - tr, r.top}); arc({r.right, r.top + tr}, tr); s->AddLine({r.right, r.bottom - br}); arc({r.right - br, r.bottom}, br);
        s->AddLine({r.left + bl, r.bottom}); arc({r.left, r.bottom - bl}, bl); s->AddLine({r.left, r.top + tl}); arc({r.left + tl, r.top}, tl); s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release(); return g;
    }
    // Corner wedge: from the corner point along one edge, then the arc back to the other edge.
    static ID2D1PathGeometry* Shape(ID2D1RenderTarget* t, std::vector<D2D1_POINT_2F> lines, D2D1_POINT_2F arcTo, float r, bool counter) {
        ID2D1Factory* factory = nullptr; t->GetFactory(&factory); if (!factory) return nullptr;
        ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* s = nullptr; const bool ok = SUCCEEDED(factory->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&s)); factory->Release();
        if (!ok) { if (g) g->Release(); return nullptr; }
        s->BeginFigure(lines[0], D2D1_FIGURE_BEGIN_FILLED); for (size_t i = 1; i < lines.size(); ++i) s->AddLine(lines[i]);
        s->AddArc(D2D1::ArcSegment(arcTo, {r, r}, 0, counter ? D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE : D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
        s->EndFigure(D2D1_FIGURE_END_CLOSED); s->Close(); s->Release(); return g;
    }
    // Resize placeholders (Paper 09, user direction): the two resized panels are opaque
    // #171717 surfaces following their new rectangles 1:1, the site's identity centred:
    // favicon 36 (radius 4), 12, short name 16 medium, 4, bare domain 14, both white 50 %.
    void DrawTiles(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const Frame& f) {
        const float o = f.tileOpacity;
        // One opaque surface for both tiles and the gap, 1 DIP wider and aliased: an antialiased
        // edge at a fractional pixel would let the frozen page show through as a grey hairline.
        if (!f.tiles.empty()) {
            D2D1_RECT_F u{1e9f, 1e9f, -1e9f, -1e9f};
            auto grow = [&](const Split::Box& x) { u = {std::min(u.left, x.left), std::min(u.top, x.top), std::max(u.right, x.right), std::max(u.bottom, x.bottom)}; };
            for (auto& tile : f.tiles) grow(tile.box); for (auto& g : f.gaps) grow(g);
            t->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED); b->SetColor(D2D1::ColorF(23 / 255.f, 23 / 255.f, 23 / 255.f, o));
            t->FillRectangle({u.left - 1, u.top - 1, u.right + 1, u.bottom + 1}, b); t->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        for (auto& tile : f.tiles) {
            const D2D1_RECT_F r{tile.box.left, tile.box.top, tile.box.right, tile.box.bottom};
            t->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
            const float cx = (r.left + r.right) / 2, block = 36 + 12 + 14 + 4 + 14, top = (r.top + r.bottom - block) / 2, width = std::max(0.f, tile.box.W() - 32);
            const D2D1_RECT_F icon{cx - 18, top, cx + 18, top + 36};
            ID2D1Factory* factory = nullptr; t->GetFactory(&factory); ID2D1RoundedRectangleGeometry* mask = nullptr; ID2D1Layer* layer = nullptr;
            if (factory) { factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(icon, 4, 4), &mask); factory->Release(); }
            if (mask && SUCCEEDED(t->CreateLayer(&layer))) t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask), layer);
            if (tile.icon && tile.icon->width) Fav(t, b, tile.icon, icon, o);
            else { b->SetColor(D2D1::ColorF(1, 1, 1, .08f * o)); t->FillRectangle(icon, b); }  // no icon yet: a quiet square, never a stand-in site
            if (layer) { t->PopLayer(); layer->Release(); } if (mask) mask->Release();
            Line(t, b, tile.name, {cx - width / 2, top + 48 - 3, cx + width / 2, top + 48 + 17}, true, 16, .5f * o);
            if (!tile.domain.empty()) Line(t, b, tile.domain, {cx - width / 2, top + 66 - 3, cx + width / 2, top + 66 + 17}, false, 14, .5f * o);
            t->PopAxisAlignedClip();
        }
        for (auto& g : f.gaps) {  // the moving gap, which the frozen pages would otherwise cover
            b->SetColor(D2D1::ColorF(23 / 255.f, 23 / 255.f, 23 / 255.f, o)); t->FillRectangle({g.left, g.top, g.right, g.bottom}, b);
        }
        for (auto& c : f.corners) {  // canvas wedge outside the 6 DIP arc: the tiles are pages, clipped like them
            const float r = Split::pageRadius, sx = c.right ? -1.f : 1.f;
            if (auto g = Shape(t, {{c.x, c.y}, {c.x, c.y - r}}, {c.x + sx * r, c.y}, r, !c.right)) { b->SetColor(D2D1::ColorF(23 / 255.f, 23 / 255.f, 23 / 255.f, o)); t->FillGeometry(g, b); g->Release(); }
        }
        for (auto& g : f.gaps) {  // the dragged separator turns accent: a 2 DIP line along it and its 8 x 36 handle
            const bool row = g.H() > g.W(); const float cx = (g.left + g.right) / 2, cy = (g.top + g.bottom) / 2;
            b->SetColor(D2D1::ColorF(accentR, accentG, accentB, o));
            t->FillRectangle(row ? D2D1_RECT_F{cx - 1, g.top, cx + 1, g.bottom} : D2D1_RECT_F{g.left, cy - 1, g.right, cy + 1}, b);
            const D2D1_RECT_F pill = row ? D2D1_RECT_F{cx - 4, cy - 18, cx + 4, cy + 18} : D2D1_RECT_F{cx - 18, cy - 4, cx + 18, cy + 4};
            t->FillRoundedRectangle(D2D1::RoundedRect(pill, 4, 4), b);
        }
    }
    template<class T> static void Drop(T*& p) { if (p) p->Release(); p = nullptr; }
    void Formats() {
        if (!write || regular) return;
        for (int bold = 0; bold < 2; ++bold) {
            IDWriteTextFormat* format = nullptr;
            if (SUCCEEDED(write->CreateTextFormat(L"Inter Display", fonts, bold ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL,
                                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14, L"fr-FR", &format))) {
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr;
                if (SUCCEEDED(write->CreateEllipsisTrimmingSign(format, &ellipsis))) { format->SetTrimming(&trim, ellipsis); ellipsis->Release(); }
            }
            (bold ? medium : regular) = format;
        }
    }
    void Text(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const std::wstring& s, D2D1_RECT_F r, bool bold, float opacity) {
        IDWriteTextFormat* format = bold ? medium : regular; if (!format || s.empty()) return;
        b->SetColor(D2D1::ColorF(1, 1, 1, opacity)); t->DrawText(s.c_str(), (UINT32)s.size(), format, r, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    // One centred line at `size` DIP, cut with an ellipsis when it does not fit.
    void Line(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const std::wstring& s, D2D1_RECT_F r, bool bold, float size, float opacity) {
        Formats(); IDWriteTextFormat* format = bold ? medium : regular; if (!write || !format || s.empty() || r.right <= r.left) return;
        IDWriteTextLayout* layout = nullptr; if (FAILED(write->CreateTextLayout(s.c_str(), (UINT32)s.size(), format, r.right - r.left, r.bottom - r.top, &layout))) return;
        const DWRITE_TEXT_RANGE all{0, (UINT32)s.size()}; layout->SetFontSize(size, all); layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        IDWriteInlineObject* sign = nullptr; DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        if (SUCCEEDED(write->CreateEllipsisTrimmingSign(layout, &sign))) layout->SetTrimming(&trim, sign);
        b->SetColor(D2D1::ColorF(1, 1, 1, opacity)); t->DrawTextLayout({r.left, r.top}, layout, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (sign) sign->Release(); layout->Release();
    }
    static void Fav(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const std::shared_ptr<const FaviconImage>& image, D2D1_RECT_F r, float opacity) {
        ID2D1Bitmap* bitmap = nullptr;
        if (image && image->width && image->height) {
            auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            t->CreateBitmap(D2D1::SizeU(image->width, image->height), image->pixels.data(), image->stride, props, &bitmap);
        }
        if (bitmap) { t->DrawBitmap(bitmap, r, opacity); bitmap->Release(); return; }
        b->SetColor(D2D1::ColorF(1, 1, 1, .9f * opacity)); t->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4), b);
    }
    void DrawPlate(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const Plate& p) {
        const D2D1_RECT_F r{p.box.left, p.box.top, p.box.right, p.box.bottom};
        b->SetColor(D2D1::ColorF(accentR, accentG, accentB, tintOpacity * p.opacity)); t->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), b);
        b->SetColor(D2D1::ColorF(accentR, accentG, accentB, p.opacity));
        t->DrawRoundedRectangle(D2D1::RoundedRect({r.left + 1, r.top + 1, r.right - 1, r.bottom - 1}, 5, 5), b, 2);
        // Label: favicon, site (medium) and where it lands, on a solid accent chip (4.58:1).
        const float title = TextWidth(p.title, true), detail = TextWidth(p.detail, false);
        float w = 8 + 16 + 8 + title + (p.detail.empty() ? 0 : 6 + detail) + 12;
        w = std::min(w, std::min(240.f, std::max(0.f, p.box.W() - 24)));  // long names end in an ellipsis
        if (w < 48) return;
        const float x = std::round((r.left + r.right - w) / 2), y = r.top + 24;
        b->SetColor(D2D1::ColorF(accentR, accentG, accentB, p.opacity)); t->FillRoundedRectangle(D2D1::RoundedRect({x, y, x + w, y + 32}, 6, 6), b);
        Fav(t, b, p.favicon, {x + 8, y + 8, x + 24, y + 24}, p.opacity);
        const float room = w - 8 - 16 - 8 - 12, tw = std::min(title, room);
        Text(t, b, p.title, {x + 32, y, x + 32 + tw, y + 32}, true, p.opacity);
        if (!p.detail.empty() && room - tw > 24) Text(t, b, p.detail, {x + 32 + tw + 6, y, x + w - 12, y + 32}, false, p.opacity);
    }
    void DrawCard(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const Card& c) {
        const D2D1_RECT_F r{c.box.left, c.box.top, c.box.right, c.box.bottom};
        // Elevation: a soft stack of transparent shadows (no blur effect needed).
        for (int i = 8; i >= 1; --i) {  // 8 faint layers read as one soft 0 4 12 shadow, no visible steps
            const float s = (float)i * 1.5f;
            b->SetColor(D2D1::ColorF(0, 0, 0, .028f * c.opacity));
            t->FillRoundedRectangle(D2D1::RoundedRect({r.left - s, r.top - s + 4, r.right + s, r.bottom + s + 4}, 6 + s, 6 + s), b);
        }
        b->SetColor(D2D1::ColorF(41 / 255.f, 41 / 255.f, 41 / 255.f, c.opacity)); t->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), b);
        b->SetColor(D2D1::ColorF(1, 1, 1, .10f * c.opacity)); t->DrawRoundedRectangle(D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, 5.5f, 5.5f), b, 1);
        Fav(t, b, c.favicon, {r.left + 8, r.top + 8, r.left + 24, r.top + 24}, c.opacity);
        Text(t, b, c.title, {r.left + 32, r.top, r.right - 8, r.bottom}, true, c.opacity);
    }
    void DrawPill(ID2D1RenderTarget* t, ID2D1SolidColorBrush* b, const Pill& p) {
        const float title = TextWidth(p.title, true), detail = TextWidth(p.detail, false);
        const float w = 8 + 16 + 8 + title + (p.detail.empty() ? 0 : 6 + detail) + 12;
        const D2D1_RECT_F r{p.x, p.y, p.x + w, p.y + 32};
        b->SetColor(D2D1::ColorF(41 / 255.f, 41 / 255.f, 41 / 255.f)); t->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), b);
        b->SetColor(D2D1::ColorF(1, 1, 1, .10f)); t->DrawRoundedRectangle(D2D1::RoundedRect({r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f}, 5.5f, 5.5f), b, 1);
        // Neutral "not allowed" glyph: never the accent, never the danger tone.
        b->SetColor(D2D1::ColorF(1, 1, 1)); const float cx = r.left + 16, cy = r.top + 16;
        t->DrawEllipse(D2D1::Ellipse({cx, cy}, 6.5f, 6.5f), b, 1.5f); t->DrawLine({cx - 4.6f, cy + 4.6f}, {cx + 4.6f, cy - 4.6f}, b, 1.5f);
        Text(t, b, p.title, {r.left + 32, r.top, r.left + 32 + title, r.bottom}, true, 1);
        if (!p.detail.empty()) Text(t, b, p.detail, {r.left + 38 + title, r.top, r.right - 12, r.bottom}, false, .6f);
    }
};

// The layered popup. Owned by the main window: always above it, never activated,
// transparent to the mouse (the main window keeps the capture during the drag).
class Overlay {
public:
    Painter painter;
    bool Init(HINSTANCE instance, HWND owner, ID2D1Factory* factory, IDWriteFactory* write, IDWriteFontCollection* fonts) {
        owner_ = owner; factory_ = factory; painter.write = write; painter.fonts = fonts;
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = instance; wc.lpszClassName = L"BrowserSplitOverlay"; RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"",
                                WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, nullptr);
        return hwnd_ != nullptr;
    }
    HWND Window() const { return hwnd_; }
    bool Visible() const { return visible_; }
    // Paint `frame` (client DIPs) over the owner's client area at its current DPI.
    void Show(const Frame& frame) {
        if (!hwnd_ || !factory_) return;
        if (frame.Empty()) { Hide(); return; }
        RECT client; GetClientRect(owner_, &client); POINT origin{0, 0}; ClientToScreen(owner_, &origin);
        const int w = std::max<LONG>(1, client.right), h = std::max<LONG>(1, client.bottom); const UINT dpi = GetDpiForWindow(owner_);
        if (!Surface(w, h)) return;
        if (!target_ || dpi != dpi_) {
            DropTarget(); dpi_ = dpi;
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), (float)dpi, (float)dpi);
            if (FAILED(factory_->CreateDCRenderTarget(&props, &target_))) { target_ = nullptr; return; }
        }
        RECT all{0, 0, w, h};
        if (SUCCEEDED(target_->BindDC(dc_, &all))) {
            target_->BeginDraw(); target_->Clear(D2D1::ColorF(0, 0, 0, 0)); painter.Draw(target_, frame);
            if (target_->EndDraw() == D2DERR_RECREATE_TARGET) DropTarget();
        }
        POINT src{0, 0}; SIZE size{w, h}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, nullptr, &origin, &size, dc_, &src, 0, &blend, ULW_ALPHA);
        if (!visible_) { SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW); visible_ = true; }
    }
    void Hide() {
        if (hwnd_ && visible_) ShowWindow(hwnd_, SW_HIDE);
        visible_ = false; Release();
    }
    void Dispose() { Hide(); if (hwnd_) DestroyWindow(hwnd_); hwnd_ = nullptr; }
    // Evidence captures (--split-test): composite the current surface over a client-area image.
    void BlendInto(HDC target, POINT clientOrigin) const {
        if (!visible_ || !dc_) return; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        GdiAlphaBlend(target, clientOrigin.x, clientOrigin.y, w_, h_, dc_, 0, 0, w_, h_, blend);
    }
private:
    HWND hwnd_ = nullptr, owner_ = nullptr; ID2D1Factory* factory_ = nullptr; bool visible_ = false;
    HDC dc_ = nullptr; HBITMAP bitmap_ = nullptr; HGDIOBJ old_ = nullptr; int w_ = 0, h_ = 0;
    ID2D1DCRenderTarget* target_ = nullptr; UINT dpi_ = 0;
    void DropTarget() { painter.ForgetBitmaps(); if (target_) target_->Release(); target_ = nullptr; }
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
    // The surface is only kept while dragging: no memory held at rest.
    void Release() {
        DropTarget();
        if (dc_ && old_) SelectObject(dc_, old_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nullptr; bitmap_ = nullptr; old_ = nullptr; w_ = h_ = 0;
    }
};
}}
