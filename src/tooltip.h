#pragma once
#include "i18n.h"
// C5 · Info-bulles (Paper « Browser Design », page p-D-0, artboard EP1-0 ; contexte C1 DQF-0,
// état focus C2 E11-0). Native, non-activating, click-through tooltip that replaces the comctl
// TOOLTIPS_CLASS of the main toolbar. It never takes focus or clicks (WS_EX_TRANSPARENT |
// WS_EX_NOACTIVATE) and never paints a shadow (NativeUI::ConfigureShadowlessPopup).
//
// Paper values (get_jsx, EP1-0 « Variantes » ETR-0 and « Contexte barre d'outils » ERT-0):
//   surface #171717 (--color-browser-canvas, Design::Surface::canvas; user request 2026-11-09,
//   was #3E3E3E), border 1 px #FFFFFF0F keeps the edge, radius 6 (window region)
//   single line: height 28, padding-inline 8 (4 on the right when a key cap ends the row), gap 8
//   label 14/20 Inter Display regular #FFFFFFE6 ; key cap h 20, radius 3, #FFFFFF14, padding 6,
//   13/20 medium #FFFFFFB3 ; detail lines 14/20 #FFFFFF80 ; multi-line: padding 6/8, gap 4
//   (header + detail) or 2 (truncated tab: title + domain), max width 320.
//   Behaviour notes (EW8-0..EWA-0): appears after 600 ms of still hover, then instantly between
//   neighbouring buttons for 300 ms; 120 ms fade, no movement; 6 px under the target, flips above
//   when room is short; keyboard focus shows it at once;
//   Échap hides it without moving focus (the host calls Hide()).
//   Horizontal placement (user request 2026-11-20, replaces Paper's « left aligned »): centred on
//   the target, slid only to stay 8 DIP inside the window and the screen (TooltipPlacement::Place).
//
// This header also carries NativeChrome::PopupKit, the small toolkit the B surfaces share
// (find bar, context toast, full-screen hint): popup creation without shadow, the host frame
// clock or a private timer, exact Paper (Phosphor) glyph paths and wrapped Inter text.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "chrome.h"
#include "accessibility.h"
#include "native-ui.h"
#include "tooltip-placement.h"

namespace NativeChrome {
namespace PopupKit {
inline float Dpi(HWND window) { const UINT d = window ? GetDpiForWindow(window) : 0; return d ? static_cast<float>(d) : 96.f; }
inline int Px(HWND window, float dip) { return static_cast<int>(std::lround(dip * Dpi(window) / 96.f)); }
inline D2D1_COLOR_F White(float alpha) { return D2D1::ColorF(1, 1, 1, alpha); }
inline D2D1_COLOR_F Gray(int value, float alpha = 1) { return D2D1::ColorF(value / 255.f, value / 255.f, value / 255.f, alpha); }
inline D2D1_COLOR_F Hex(unsigned rgb, float alpha = 1) { return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, alpha); }
inline D2D1_COLOR_F Danger(float alpha = 1) { return D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, alpha); }
inline D2D1_COLOR_F Accent(float alpha = 1) { const auto a = Design::Accent::Get(); return D2D1::ColorF(a.r, a.g, a.b, alpha); }
inline bool Contains(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

inline bool Register(HINSTANCE instance, const wchar_t* name, WNDPROC proc) {
    WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = name;
    return RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
// Every B popup: owned, tool window, layered (fades by window alpha), client-drawn, no
// backdrop and no shadow: NativeUI::ConfigureShadowlessPopup cuts the exact Paper `radius`
// (DIP) as a window region (no DWM rounding: it always brings a shadow). Surfaces clear to the
// canvas #171717 and paint their own antialiased rounded fill + hairline inside that region.
// `extra` adds WS_EX_NOACTIVATE / WS_EX_TRANSPARENT as needed.
inline HWND CreatePopup(HINSTANCE instance, HWND owner, const wchar_t* cls, const wchar_t* title, DWORD extra, void* self, float radius, DWORD style = WS_POPUP) {
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | extra, cls, title, style, 0, 0, 1, 1, owner, nullptr, instance, self);
    if (!h) return nullptr;
    NativeUI::ConfigureFullClientFrame(h);
    const int noBackdrop = 1; // DWMSBT_NONE
    DwmSetWindowAttribute(h, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &noBackdrop, sizeof(noBackdrop));
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    NativeUI::ConfigureShadowlessPopup(h, radius);
    return h;
}
inline void SetAlpha(HWND h, float alpha) { if (h) SetLayeredWindowAttributes(h, 0, static_cast<BYTE>(std::lround(255 * Motion::Clamp(alpha))), LWA_ALPHA); }
// 1 device pixel hairline just inside the region edge, antialiased, at the surface radius.
inline void Outline(Chrome& chrome, float w, float h, float radius, D2D1_COLOR_F color) {
    auto* t = chrome.RenderTarget(); auto* b = chrome.SolidBrush(); if (!t || !b) return;
    const float px = t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width));
    b->SetColor(color);
    t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(px / 2, px / 2, w - px / 2, h - px / 2), radius, radius), b, px);
}
inline void Fill(Chrome& chrome, D2D1_RECT_F r, float radius, D2D1_COLOR_F color) {
    auto* t = chrome.RenderTarget(); auto* b = chrome.SolidBrush(); if (!t || !b) return;
    b->SetColor(color); if (radius > 0) t->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b); else t->FillRectangle(r, b);
}
// Paper focus ring: 1 DIP inset, white at 50 % (#FFFFFF80), radius of the control.
inline void InsetRing(Chrome& chrome, D2D1_RECT_F r, float radius, float alpha = .5f) {
    auto* t = chrome.RenderTarget(); auto* b = chrome.SolidBrush(); if (!t || !b) return;
    b->SetColor(White(alpha)); r.left += .5f; r.top += .5f; r.right -= .5f; r.bottom -= .5f;
    t->DrawRoundedRectangle(D2D1::RoundedRect(r, std::max(0.f, radius - .5f), std::max(0.f, radius - .5f)), b, 1);
}

// ---- Exact Paper glyphs: Phosphor (bold) path data from get_jsx, 256 unit view box. ----
namespace Glyphs {
inline constexpr char search[] = "M232.49,215.51,185,168a92.12,92.12,0,1,0-17,17l47.53,47.54a12,12,0,0,0,17-17ZM44,112a68,68,0,1,1,68,68A68.07,68.07,0,0,1,44,112Z";
inline constexpr char caretUp[] = "M216.49,168.49a12,12,0,0,1-17,0L128,97,56.49,168.49a12,12,0,0,1-17-17l80-80a12,12,0,0,1,17,0l80,80A12,12,0,0,1,216.49,168.49Z";
inline constexpr char caretDown[] = "M216.49,104.49l-80,80a12,12,0,0,1-17,0l-80-80a12,12,0,0,1,17-17L128,159l71.51-71.52a12,12,0,0,1,17,17Z";
inline constexpr char close[] = "M208.49,191.51a12,12,0,0,1-17,17L128,145,64.49,208.49a12,12,0,0,1-17-17L111,128,47.51,64.49a12,12,0,0,1,17-17L128,111l63.51-63.52a12,12,0,0,1,17,17L145,128Z";
inline constexpr char info[] = "M108,84a16,16,0,1,1,16,16A16,16,0,0,1,108,84Zm128,44A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Zm-72,36.68V132a20,20,0,0,0-20-20,12,12,0,0,0-4,23.32V168a20,20,0,0,0,20,20,12,12,0,0,0,4-23.32Z";
inline constexpr char checkCircle[] = "M176.49,95.51a12,12,0,0,1,0,17l-56,56a12,12,0,0,1-17,0l-24-24a12,12,0,1,1,17-17L112,143l47.51-47.52A12,12,0,0,1,176.49,95.51ZM236,128A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Z";
inline constexpr char warningCircle[] = "M128,20A108,108,0,1,0,236,128,108.12,108.12,0,0,0,128,20Zm0,192a84,84,0,1,1,84-84A84.09,84.09,0,0,1,128,212Zm-12-80V80a12,12,0,0,1,24,0v52a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,172Z";
inline constexpr char link[] = "M117.18,188.74a12,12,0,0,1,0,17l-5.12,5.12A58.26,58.26,0,0,1,70.6,228h0A58.62,58.62,0,0,1,29.14,127.92L63.89,93.17a58.64,58.64,0,0,1,98.56,28.11,12,12,0,1,1-23.37,5.44,34.65,34.65,0,0,0-58.22-16.58L46.11,144.89A34.62,34.62,0,0,0,70.57,204h0a34.41,34.41,0,0,0,24.49-10.14l5.11-5.12A12,12,0,0,1,117.18,188.74ZM226.83,45.17a58.65,58.65,0,0,0-82.93,0l-5.11,5.11a12,12,0,0,0,17,17l5.12-5.12a34.63,34.63,0,1,1,49,49L175.1,145.86A34.39,34.39,0,0,1,150.61,156h0a34.63,34.63,0,0,1-33.69-26.72,12,12,0,0,0-23.38,5.44A58.64,58.64,0,0,0,150.56,180h.05a58.28,58.28,0,0,0,41.47-17.17l34.75-34.75a58.62,58.62,0,0,0,0-82.91Z";
inline constexpr char cornersIn[] = "M152,96V56a12,12,0,0,1,24,0V84h28a12,12,0,0,1,0,24H164A12,12,0,0,1,152,96ZM92,148H52a12,12,0,0,0,0,24H80v28a12,12,0,0,0,24,0V160A12,12,0,0,0,92,148Zm112,0H164a12,12,0,0,0-12,12v40a12,12,0,0,0,24,0V172h28a12,12,0,0,0,0-24ZM92,44A12,12,0,0,0,80,56V84H52a12,12,0,0,0,0,24H92a12,12,0,0,0,12-12V56A12,12,0,0,0,92,44Z";
}
// Minimal SVG path reader (M L H V C S Q A Z, absolute and relative, implicit repeats),
// enough for the Phosphor paths above. Arcs map directly to D2D1_ARC_SEGMENT.
inline ID2D1PathGeometry* ParsePath(ID2D1Factory* factory, const char* d) {
    ID2D1PathGeometry* g = nullptr; if (!factory || FAILED(factory->CreatePathGeometry(&g))) return nullptr;
    ID2D1GeometrySink* s = nullptr; if (FAILED(g->Open(&s))) { g->Release(); return nullptr; }
    s->SetFillMode(D2D1_FILL_MODE_WINDING);
    const char* p = d; char cmd = 0; float x = 0, y = 0, sx = 0, sy = 0, cx = 0, cy = 0; bool open = false;
    auto skip = [&] { while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t') ++p; };
    auto num = [&](float& v) { skip(); char* e = nullptr; v = std::strtof(p, &e); if (e == p) return false; p = e; return true; };
    auto flag = [&](bool& v) { skip(); if (*p != '0' && *p != '1') return false; v = *p == '1'; ++p; return true; };
    auto begin = [&](float bx, float by) { if (open) s->EndFigure(D2D1_FIGURE_END_OPEN); s->BeginFigure(D2D1::Point2F(bx, by), D2D1_FIGURE_BEGIN_FILLED); open = true; sx = bx; sy = by; };
    for (;;) {
        skip(); if (!*p) break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) cmd = *p++;
        const bool rel = cmd >= 'a'; const char c = static_cast<char>(rel ? cmd - 32 : cmd);
        const float ox = rel ? x : 0, oy = rel ? y : 0; float a[7]{};
        if (c == 'Z') { if (open) { s->EndFigure(D2D1_FIGURE_END_CLOSED); open = false; } x = sx; y = sy; cx = x; cy = y; continue; }
        if (c == 'M') { if (!num(a[0]) || !num(a[1])) break; x = ox + a[0]; y = oy + a[1]; begin(x, y); cx = x; cy = y; cmd = rel ? 'l' : 'L'; continue; }
        if (!open) begin(x, y);
        if (c == 'L') { if (!num(a[0]) || !num(a[1])) break; x = ox + a[0]; y = oy + a[1]; s->AddLine(D2D1::Point2F(x, y)); cx = x; cy = y; }
        else if (c == 'H') { if (!num(a[0])) break; x = ox + a[0]; s->AddLine(D2D1::Point2F(x, y)); cx = x; cy = y; }
        else if (c == 'V') { if (!num(a[0])) break; y = oy + a[0]; s->AddLine(D2D1::Point2F(x, y)); cx = x; cy = y; }
        else if (c == 'C' || c == 'S') {
            int n = c == 'C' ? 6 : 4; bool ok = true; for (int i = 0; i < n; ++i) ok = ok && num(a[i]); if (!ok) break;
            float x1, y1, x2, y2, ex, ey;
            if (c == 'C') { x1 = ox + a[0]; y1 = oy + a[1]; x2 = ox + a[2]; y2 = oy + a[3]; ex = ox + a[4]; ey = oy + a[5]; }
            else { x1 = 2 * x - cx; y1 = 2 * y - cy; x2 = ox + a[0]; y2 = oy + a[1]; ex = ox + a[2]; ey = oy + a[3]; }
            s->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), D2D1::Point2F(ex, ey))); cx = x2; cy = y2; x = ex; y = ey;
        } else if (c == 'Q') {
            bool ok = true; for (int i = 0; i < 4; ++i) ok = ok && num(a[i]); if (!ok) break;
            const float x1 = ox + a[0], y1 = oy + a[1]; x = ox + a[2]; y = oy + a[3];
            s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(x1, y1), D2D1::Point2F(x, y))); cx = x1; cy = y1;
        } else if (c == 'A') {
            bool large = false, sweep = false;
            if (!num(a[0]) || !num(a[1]) || !num(a[2]) || !flag(large) || !flag(sweep) || !num(a[5]) || !num(a[6])) break;
            x = ox + a[5]; y = oy + a[6];
            s->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(a[0], a[1]), a[2], sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
            cx = x; cy = y;
        } else break;
    }
    if (open) s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close(); s->Release(); return g;
}
// Geometry cache (device independent: survives render-target recreation).
class GlyphCache {
    std::map<const char*, ID2D1PathGeometry*> paths_; ID2D1Factory* factory_ = nullptr; // geometries belong to one factory
public:
    ~GlyphCache() { Clear(); }
    void Clear() { for (auto& p : paths_) if (p.second) p.second->Release(); paths_.clear(); }
    // Fills `path` (256 view box) into a `size` DIP square centred in `box`.
    void Draw(Chrome& chrome, const char* path, D2D1_RECT_F box, float size, D2D1_COLOR_F color) {
        auto* t = chrome.RenderTarget(); auto* b = chrome.SolidBrush(); if (!t || !b || color.a <= 0) return;
        if (chrome.Factory() != factory_) { Clear(); factory_ = chrome.Factory(); }
        auto& g = paths_[path]; if (!g) g = ParsePath(factory_, path); if (!g) return;
        const float x = (box.left + box.right - size) / 2, y = (box.top + box.bottom - size) / 2, k = size / 256.f;
        D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
        t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * base);
        b->SetColor(color); t->FillGeometry(g, b); t->SetTransform(base);
    }
};

// ---- Inter Display text with CSS line boxes (wrapping, colour, tracking in DIP). ----
struct Type { float size = 14, lineHeight = 20, tracking = 0; bool medium = false; bool tabular = false; };
// CSS half-leading baseline for Inter (ascender .96875 em, descender .2412 em).
inline float Baseline(const Type& type) { return (type.lineHeight - (0.96875f + 0.2412f) * type.size) / 2 + 0.96875f * type.size; }
// `emoji` (optional) receives how to draw it (src/emoji-text.h).
inline IDWriteTextLayout* MakeLayout(Chrome& chrome, const std::wstring& text, float width, const Type& type, bool wrap, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, EmojiText::Style* emoji = nullptr) {
    auto* write = chrome.WriteFactory(); if (!write || text.empty()) return nullptr;
    IDWriteTextFormat* format = nullptr;
    if (FAILED(write->CreateTextFormat(L"Inter Display", chrome.FontCollection(), type.medium ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, type.size, L"fr-FR", &format))) return nullptr;
    format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(align);
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, type.lineHeight, Baseline(type));
    IDWriteTextLayout* layout = nullptr;
    write->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, std::max(1.f, width), 4096, &layout); format->Release();
    if (!layout) return nullptr;
    const DWRITE_TEXT_RANGE all{0, static_cast<UINT32>(text.size())};
    IDWriteTextLayout1* l1 = nullptr;
    if (type.tracking != 0 && SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&l1)))) { l1->SetCharacterSpacing(type.tracking, 0, 0, all); l1->Release(); }
    IDWriteTypography* typo = nullptr; write->CreateTypography(&typo);
    if (typo) {
        for (auto tag : {DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '1'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '2'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '3'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '4'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '1', '3')}) typo->AddFontFeature({static_cast<DWRITE_FONT_FEATURE_TAG>(tag), 1});
        if (type.tabular) typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
        layout->SetTypography(typo, all); typo->Release();
    }
    const auto style = EmojiText::Prepare(write, layout, text, chrome.FontCollection(), L"Inter Display"); if (emoji) *emoji = style;
    if (!wrap) { DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr; write->CreateEllipsisTrimmingSign(layout, &ellipsis); layout->SetTrimming(&trim, ellipsis); if (ellipsis) ellipsis->Release(); }
    return layout;
}
struct TextSize { float width = 0, height = 0; int lines = 0; };
inline TextSize Measure(Chrome& chrome, const std::wstring& text, float width, const Type& type, bool wrap) {
    TextSize r; if (text.empty()) return r;
    auto* layout = MakeLayout(chrome, text, wrap ? width : 8192.f, type, wrap); if (!layout) return r;
    DWRITE_TEXT_METRICS m{}; layout->GetMetrics(&m); layout->Release();
    r.width = std::ceil(m.widthIncludingTrailingWhitespace); r.lines = static_cast<int>(m.lineCount); r.height = r.lines * type.lineHeight; return r;
}
// Draws `text` with its first line box at `box.top`; one line (ellipsis) unless `wrap`.
inline void DrawText(Chrome& chrome, const std::wstring& text, D2D1_RECT_F box, const Type& type, D2D1_COLOR_F color, bool wrap = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    auto* t = chrome.RenderTarget(); auto* b = chrome.SolidBrush(); if (!t || !b || text.empty()) return;
    EmojiText::Style emoji; auto* layout = MakeLayout(chrome, text, box.right - box.left, type, wrap, align, &emoji); if (!layout) return;
    EmojiText::Draw(t, D2D1::Point2F(box.left, box.top), layout, b, color, emoji); layout->Release();
}
// A text run with one emphasised range (Paper: « <b>youtube.com</b> est en plein écran »).
inline void DrawRich(Chrome& chrome, const std::wstring& text, size_t strongBegin, size_t strongLength, D2D1_RECT_F box, const Type& type, D2D1_COLOR_F color, D2D1_COLOR_F strong, bool wrap = false, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING) {
    auto* t = chrome.RenderTarget(); auto* b = chrome.SolidBrush(); if (!t || !b || text.empty()) return;
    EmojiText::Style emoji; auto* layout = MakeLayout(chrome, text, box.right - box.left, type, wrap, align, &emoji); if (!layout) return;
    // Colour glyphs ignore brush alpha: one layer at the larger alpha, both brushes scaled to it,
    // so each run keeps its exact alpha (emoji take the layer's).
    float layer = 1;
    if (emoji.layered) { emoji.layered = false; layer = std::max(color.a, strongLength ? strong.a : 0.f); if (layer <= 0) { layout->Release(); return; } if (layer < 1) { color.a /= layer; strong.a /= layer; } }
    ID2D1SolidColorBrush* emphasis = nullptr;
    if (strongLength && SUCCEEDED(t->CreateSolidColorBrush(strong, &emphasis))) {
        const DWRITE_TEXT_RANGE r{static_cast<UINT32>(strongBegin), static_cast<UINT32>(strongLength)};
        layout->SetFontWeight(DWRITE_FONT_WEIGHT_MEDIUM, r); layout->SetDrawingEffect(emphasis, r);
    }
    if (layer < 1) t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), layer), nullptr);
    EmojiText::Draw(t, D2D1::Point2F(box.left, box.top), layout, b, color, emoji);
    if (layer < 1) t->PopLayer();
    if (emphasis) emphasis->Release();
    layout->Release();
}

// Host frame clock (Hooks::wake, the contract) or, without one, a private window timer.
class Pump {
    HWND window_ = nullptr; std::function<void()> wake_;
public:
    static constexpr UINT_PTR TimerId = 0xB5;
    void Bind(HWND window, std::function<void()> wake) { window_ = window; wake_ = std::move(wake); }
    bool External() const { return static_cast<bool>(wake_); }
    void Request(Motion::Wake next) {
        if (wake_) { if (next.delay) wake_(); return; }
        if (!window_) return;
        if (!next.delay) { KillTimer(window_, TimerId); return; }
        SetTimer(window_, TimerId, static_cast<UINT>(std::max<long long>(1, next.delay->count())), nullptr);
    }
    void Stop() { if (window_) KillTimer(window_, TimerId); }
};
inline RECT WorkArea(RECT around) { return TooltipPlacement::WorkArea(around); }

// Tests: runs a surface's Paint() into an offscreen WIC target (same code path, software
// Direct2D) and saves a PNG: the GPU HWND target cannot be read back (PrintWindow / BitBlt).
inline bool RenderPng(const std::wstring& root, float width, float height, UINT dpi, const wchar_t* path, Chrome*& surface, const std::function<void()>& paint) {
    Chrome off; if (FAILED(off.InitOffscreen(root, dpi, width, height))) return false;
    Chrome* keep = surface; surface = &off; paint(); surface = keep;
    const bool ok = SUCCEEDED(off.SavePng(path)); off.Dispose(); return ok;
}
} // namespace PopupKit

namespace Tooltip {
namespace Layout {
inline constexpr float radius = 6, height = 28, padX = 8, padKey = 4, gap = 8, textSize = 14, line = 20;
inline constexpr float keyHeight = 20, keyPad = 6, keySize = 13, maxWidth = 320, padY = 6, headerGap = 4, titleGap = 2;
inline constexpr float offset = 6;                                  // px under the target (Paper EW9-0)
inline constexpr int delayMs = 600, neighbourMs = 300, fadeMs = 120;  // Paper EW8-0
inline constexpr unsigned surface = 0x171717;                       // --color-browser-canvas (Design::Surface::canvas)
inline constexpr float border = 0x0F / 255.f, label = 0xE6 / 255.f, keyPlate = 0x14 / 255.f, keyText = 0xB3 / 255.f, detail = 0x80 / 255.f;
}
// `label` names the action, `shortcut` (optional) its keys (« Ctrl+R »), `detail` an optional
// second line (« Clic droit ou maintien pour l'historique ») or, for a truncated tab, the
// domain line. A label longer than one line wraps (tab title variant, 320 DIP max).
struct Content { std::wstring label, shortcut, detail; bool operator==(const Content& o) const { return label == o.label && shortcut == o.shortcut && detail == o.detail; } };
struct Hooks { std::function<void()> wake; };

class Tip {
    HWND hwnd_ = nullptr, owner_ = nullptr; Chrome chrome_; Chrome* surface_ = &chrome_; PopupKit::Pump pump_; Accessibility::Bridge uia_;
    Content content_; RECT target_{}, bounds_{}; bool hasBounds_ = false; bool shown_ = false, hiding_ = false, pending_ = false, reduced_ = false, blocked_ = false;
    Motion::Tween alpha_; Motion::Time showAt_{}; ULONGLONG hiddenTick_ = 0;
    float width_ = 0, height_ = 0;
    bool Multi() const { return !content_.detail.empty() || wrapped_; }
    bool wrapped_ = false;
    void SetBounds(const RECT* b) { hasBounds_ = b != nullptr; if (b) bounds_ = *b; }
    // Separate caps, one per key (« Ctrl » « Maj » « M », like the shortcuts panel; Paper p-I-0 tooltip keycaps):
    // each at least 24 wide, 4 apart. A literal « + » key stays a key.
    static std::vector<std::wstring> Keys(const std::wstring& shortcut) {
        std::vector<std::wstring> keys; std::wstring cur;
        for (size_t i = 0; i < shortcut.size(); ++i) { if (shortcut[i] == L'+' && !cur.empty()) { keys.push_back(cur); cur.clear(); } else cur += shortcut[i]; }
        if (!cur.empty()) keys.push_back(cur);
        return keys;
    }
    static constexpr float keyMin = 24, keyGap = 4;
    float CapWidth(const std::wstring& key) { return std::max(keyMin, std::ceil(chrome_.TextWidth(key, Layout::keySize, true)) + 2 * Layout::keyPad); }
    float KeyWidth() { if (content_.shortcut.empty()) return 0; float w = 0; for (const auto& k : Keys(content_.shortcut)) w += (w > 0 ? keyGap : 0) + CapWidth(k); return w; }
    // Draws the caps from `left` in a 20 DIP row at `top`.
    void DrawCaps(Chrome& chrome, float left, float top) {
        using namespace Layout; PopupKit::Type keys{keySize, 16, 0, true}; float x = left; constexpr float capText = 0xCC / 255.f;
        for (const auto& k : Keys(content_.shortcut)) {
            const float w = CapWidth(k); const D2D1_RECT_F cap{x, top, x + w, top + keyHeight};
            // Paper p-I-0 XUX-0: cap #FFFFFF14, radius 4 (--radius-browser-4), 13/16 Medium #FFFFFFCC, centred in 20.
            PopupKit::Fill(chrome, cap, Design::Radius::r4, PopupKit::White(keyPlate));
            PopupKit::DrawText(chrome, k, {cap.left, cap.top + 2, cap.right, cap.bottom}, keys, PopupKit::White(capText), false, DWRITE_TEXT_ALIGNMENT_CENTER);
            x += w + keyGap;
        }
    }
    void Measure() {
        using namespace Layout; PopupKit::Type text{textSize, line};
        const float key = KeyWidth(), labelW = PopupKit::Measure(chrome_, content_.label, 8192, text, false).width;
        const float single = 1 + padX + labelW + (key ? gap + key + padKey : padX) + 1;
        wrapped_ = single > maxWidth && content_.shortcut.empty();
        if (content_.detail.empty() && !wrapped_) { width_ = std::min(maxWidth, single); height_ = height; return; }
        const float inner = maxWidth - 2 - 2 * padX;
        float w = std::min(inner, labelW + (key ? gap + key : 0));
        const auto title = PopupKit::Measure(chrome_, content_.label, key ? inner - gap - key : inner, text, true);
        const auto more = PopupKit::Measure(chrome_, content_.detail, inner, text, true);
        w = std::max({w, std::min(inner, more.width), wrapped_ ? inner : 0.f});
        width_ = std::ceil(w + 2 * padX + 2); height_ = 2 + 2 * padY + std::max(line, title.height) + (content_.detail.empty() ? 0 : (key ? headerGap : titleGap) + more.height);
    }
    void Paint() {
        Chrome& chrome_ = *surface_; // offscreen in RenderForTest
        using namespace Layout;
        if (!chrome_.RenderTarget()) chrome_.Resize();
        auto* t = chrome_.RenderTarget(); if (!t || !chrome_.SolidBrush()) return;
        t->BeginDraw(); t->Clear(PopupKit::Gray(0x17)); PopupKit::Fill(chrome_, {0, 0, width_, height_}, radius, PopupKit::Hex(surface));
        PopupKit::Type text{textSize, line};
        const float key = KeyWidth();
        if (!Multi()) {
            const float right = width_ - 1 - (key ? padKey : padX);
            PopupKit::DrawText(chrome_, content_.label, {1 + padX, 4, right - (key ? key + gap : 0), 24}, text, PopupKit::White(label));
            if (key) DrawCaps(chrome_, right - key, 4);
        } else {
            const float left = 1 + padX, right = width_ - 1 - padX; float y = 1 + padY;
            const float titleRight = key ? right - key - gap : right;
            PopupKit::DrawText(chrome_, content_.label, {left, y, titleRight, height_}, text, PopupKit::White(label), true);
            const float titleH = std::max(line, PopupKit::Measure(chrome_, content_.label, titleRight - left, text, true).height);
            const float labelW = PopupKit::Measure(chrome_, content_.label, 8192, text, false).width; // Paper EST-0: cap right after the label
            const float capLeft = std::min(titleRight + gap, left + labelW + gap);
            if (key) DrawCaps(chrome_, capLeft, y);
            y += titleH + (key ? headerGap : titleGap);
            PopupKit::DrawText(chrome_, content_.detail, {left, y, right, height_}, text, PopupKit::White(detail), true);
        }
        PopupKit::Outline(chrome_, width_, height_, radius, PopupKit::White(border));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET) chrome_.RecreateTarget();
    }
    void Place() {
        const SIZE size{PopupKit::Px(owner_, width_), PopupKit::Px(owner_, height_)};
        const RECT area = TooltipPlacement::Bounds(PopupKit::WorkArea(target_), hasBounds_ ? &bounds_ : nullptr);
        const POINT p = TooltipPlacement::Place(target_, size, area, PopupKit::Px(owner_, Layout::offset), PopupKit::Px(owner_, TooltipPlacement::marginDip)).at;
        SetWindowPos(hwnd_, HWND_TOP, p.x, p.y, size.cx, size.cy, SWP_NOACTIVATE);
        chrome_.Resize();
    }
    void Appear(bool fade) {
        pending_ = false; hiding_ = false; Measure(); Place();
        const auto now = Motion::Now();
        if (!shown_) { alpha_.Snap(0); PopupKit::SetAlpha(hwnd_, fade ? 0.f : 1.f); ShowWindow(hwnd_, SW_SHOWNOACTIVATE); }
        shown_ = true;
        if (fade) alpha_.Set(1, now, Motion::Milliseconds(Layout::fadeMs)); else { alpha_.Snap(1); PopupKit::SetAlpha(hwnd_, 1); }
        Paint(); ValidateRect(hwnd_, nullptr);
        if (uia_.Listening()) uia_.ChildrenChanged(1);
        pump_.Request(NextWake(now));
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Tip*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Tip*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        if (!self) return DefWindowProcW(h, msg, w, l);
        switch (msg) {
        case WM_GETOBJECT: { LRESULT r; if (self->uia_.OnGetObject(h, w, l, r)) return r; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); self->Paint(); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_TIMER: if (w == PopupKit::Pump::TimerId) { KillTimer(h, w); const auto now = Motion::Now(); self->Frame(now); self->pump_.Request(self->NextWake(now)); } return 0;
        case WM_DPICHANGED: if (self->shown_) { self->Measure(); self->Place(); } return 0;
        case WM_NCDESTROY: self->uia_.Detach(); self->hwnd_ = nullptr; self->shown_ = false; break;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Tip() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks = {}) {
        owner_ = owner; PopupKit::Register(instance, L"LisiereTooltip", Proc);
        if (!PopupKit::CreatePopup(instance, owner, L"LisiereTooltip", TR(tooltip_window_name), WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, this, Layout::radius)) return false;
        pump_.Bind(hwnd_, std::move(hooks.wake));
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { std::vector<Accessibility::Node> nodes; if (!shown_) return nodes; Accessibility::Node n; n.key = 1; n.role = Accessibility::Role::Group; n.name = content_.label; n.accelerator = content_.shortcut; n.help = content_.detail; n.automationId = L"Tooltip"; n.focusable = false; GetWindowRect(hwnd_, &n.bounds); nodes.push_back(n); return nodes; };
        host.focused = []() -> long long { return 0; };
        uia_.Attach(std::move(host));
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() { pump_.Stop(); uia_.Detach(); chrome_.Dispose(); if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); hwnd_ = nullptr; shown_ = pending_ = false; }
    // Pointer rest on a target: after 600 ms, or at once if a tip showed less than 300 ms ago.
    // `bounds` (optional, screen px) narrows the monitor work area the tip is clamped to.
    void Hover(const Content& content, RECT targetScreen, const RECT* bounds = nullptr) {
        if (!hwnd_ || blocked_ || content.label.empty()) return;
        SetBounds(bounds);
        const bool same = shown_ && !hiding_ && content == content_ && EqualRect(&target_, &targetScreen);
        content_ = content; target_ = targetScreen; if (same) return;
        const bool neighbour = (shown_ && !hiding_) || (hiddenTick_ && GetTickCount64() - hiddenTick_ < static_cast<ULONGLONG>(Layout::neighbourMs));
        if (neighbour) { Appear(!shown_ || hiding_); return; }
        pending_ = true; showAt_ = Motion::Now() + Motion::Milliseconds(Layout::delayMs); pump_.Request(NextWake(Motion::Now()));
    }
    // Keyboard focus (or an explicit request): shows at once, with the 120 ms fade.
    void Show(const std::wstring& text, const std::wstring& shortcut, RECT targetScreen) { Show(Content{text, shortcut, L""}, targetScreen); }
    void Show(const Content& content, RECT targetScreen, const RECT* bounds = nullptr) { if (!hwnd_ || blocked_ || content.label.empty()) return; SetBounds(bounds); content_ = content; target_ = targetScreen; Appear(!shown_ || hiding_); }
    // A popover hangs from the toolbar (#125 E1b card): the tip showing (or about to) goes at once, no other one shows
    // over it until it is gone — as under the modal menus, which get every pointer move while they are open.
    void Block(bool on) { blocked_ = on; if (on) HideNow(); }
    bool Blocked() const { return blocked_; }
    // Pointer left before the delay, a click, Échap, a menu opening: never shows / fades out.
    void Hide() {
        pending_ = false; if (!hwnd_ || !shown_ || hiding_) return;
        hiding_ = true; hiddenTick_ = GetTickCount64(); alpha_.Set(0, Motion::Now(), Motion::Milliseconds(Layout::fadeMs));
        pump_.Request(NextWake(Motion::Now()));
    }
    void HideNow() { pending_ = false; if (!hwnd_) return; if (shown_) hiddenTick_ = GetTickCount64(); shown_ = hiding_ = false; alpha_.Snap(0); ShowWindow(hwnd_, SW_HIDE); }
    bool Visible() const { return shown_ && !hiding_; }
    bool Pending() const { return pending_; }
    HWND Window() const { return hwnd_; }
    const Content& Current() const { return content_; }
    SIZE SizeDip() const { return {static_cast<LONG>(width_), static_cast<LONG>(height_)}; }
    // Fades stay opacity only; reduced motion keeps the same 120 ms opacity fade (no movement exists).
    void SetReducedMotion(bool reduced) { reduced_ = reduced; }
    Motion::Wake NextWake(Motion::Time now) const {
        Motion::Wake w; if (pending_) w = Motion::Deadline(now, showAt_); if (pending_ && !w.delay) w = Motion::Frame();
        if (alpha_.Moving(now)) w = Motion::Earlier(w, Motion::Frame());
        return w;
    }
    void Frame(Motion::Time now) {
        if (!hwnd_) return;
        if (pending_ && now >= showAt_) Appear(true);
        if (!shown_) return;
        const float a = alpha_.Value(now); PopupKit::SetAlpha(hwnd_, a);
        if (hiding_ && !alpha_.Moving(now)) { shown_ = hiding_ = false; ShowWindow(hwnd_, SW_HIDE); }
    }
    void PaintForTest() { Paint(); }
    bool RenderForTest(const std::wstring& root, const wchar_t* png, UINT dpi = 96) { return PopupKit::RenderPng(root, width_, height_, dpi, png, surface_, [this] { Paint(); }); }
};

// A button's own tooltip never shows over the menu it opened (owner's capture 2026-11-25: « Téléchargements » over the
// open downloads popover). Muted while that menu (popover, drawer, menu) is open, and after it closes until the pointer
// has left the button: coming back shows it again with the usual delay. Other buttons' tips are untouched. Pure.
struct MenuMute {
    int id = 0;          // the button whose menu is (or was) open; 0 none
    bool away = false;   // the pointer left that button since
    // A menu of `button` is open (called whenever one may have opened). True when it is a new one: the tip goes at once.
    bool Opened(int button) { if (!button || button == id) return false; id = button; away = false; return true; }
    // The pointer is over `under` (0: no button); `open`: whether `id`'s menu is still open. True: `under`'s tip stays hidden.
    bool Muted(int under, bool open) {
        if (!id) return false;
        if (under != id) { away = true; if (!open) id = 0; return false; }
        if (open) { away = false; return true; }
        if (away) { id = 0; return false; }   // back on the button after its menu closed: its tip again
        return true;                          // still on it since its menu closed
    }
    void Reset() { id = 0; away = false; }
};

// Hover tips for a window's icon buttons (history, downloads, settings, address suggestions).
// They used comctl TOOLTIPS_CLASS tools, which never showed: without common-controls v6 in the
// manifest, TTM_ADDTOOL rejects the v6 TOOLINFO size (0 tools registered, measured 2026-11-20).
// Now the same C5 tip as the toolbar: 600 ms delay, instant between neighbours, centred under
// the button, inside the browser window. The window is subclassed to follow the pointer; a
// click, the wheel, a key, leaving or hiding the window dismisses the tip.
class Tools {
    struct Tool { UINT_PTR id; RECT rect; std::wstring text; };
    Tip tip_; HWND hwnd_ = nullptr; std::vector<Tool> tools_; UINT_PTR current_ = 0; bool dismissed_ = false, tracking_ = false;
    const Tool* At(POINT p) const { for (const auto& t : tools_) if (PtInRect(&t.rect, p)) return &t; return nullptr; }
    void Pointer(POINT p) {
        if (!tracking_) { TRACKMOUSEEVENT e{sizeof(e), TME_LEAVE, hwnd_, 0}; tracking_ = TrackMouseEvent(&e) != FALSE; }
        const Tool* t = At(p); const UINT_PTR id = t ? t->id : 0;
        if (id == current_) return;
        current_ = id; dismissed_ = false;
        if (!t) { tip_.Hide(); return; }
        RECT target = t->rect; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&target), 2);
        const RECT bounds = TooltipPlacement::VisibleRect(GetAncestor(hwnd_, GA_ROOTOWNER));
        tip_.Hover(Content{t->text, L"", L""}, target, bounds.right > bounds.left ? &bounds : nullptr);
    }
    // Stays dismissed until the pointer reaches another tool (like the toolbar after a click).
    void Dismiss() { if (current_) dismissed_ = true; tip_.Hide(); }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Tools*>(data);
        const LRESULT result = DefSubclassProc(h, msg, w, l);   // after the window: its hover / tools are current
        switch (msg) {
        case WM_MOUSEMOVE: { const POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; const Tool* t = self->At(p); if (self->dismissed_ && t && t->id == self->current_) break; self->Pointer(p); break; }
        case WM_MOUSELEAVE: self->tracking_ = false; self->current_ = 0; self->dismissed_ = false; self->tip_.Hide(); break;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_CAPTURECHANGED: self->Dismiss(); break;
        case WM_SHOWWINDOW: if (!w) { self->current_ = 0; self->tip_.HideNow(); } break;
        case WM_WINDOWPOSCHANGED: if (reinterpret_cast<const WINDOWPOS*>(l)->flags & SWP_HIDEWINDOW) { self->current_ = 0; self->tip_.HideNow(); } break;
        case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) { self->current_ = 0; self->tip_.Hide(); } break;
        case WM_NCDESTROY: RemoveWindowSubclass(h, Proc, 1); self->hwnd_ = nullptr; break;
        default: break;
        }
        return result;
    }
public:
    bool Create(HINSTANCE instance, HWND window, const std::wstring& root) {
        if (!window || !tip_.Create(instance, window, root)) return false;
        hwnd_ = window; return SetWindowSubclass(window, Proc, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE;
    }
    void Dispose() { if (hwnd_) RemoveWindowSubclass(hwnd_, Proc, 1); hwnd_ = nullptr; tools_.clear(); current_ = 0; tip_.Dispose(); }
    explicit operator bool() const { return hwnd_ != nullptr; }
    // Tools are client rects in px; RefreshTips rebuilds them as the layout moves.
    void Clear() { tools_.clear(); }
    void Add(UINT_PTR id, RECT clientPx, std::wstring text) { tools_.push_back({id, clientPx, std::move(text)}); }
    Tip& TipForTest() { return tip_; }
    UINT_PTR CurrentForTest() const { return current_; }
    size_t CountForTest() const { return tools_.size(); }
    const Tool* ToolForTest(UINT_PTR id) const { for (const auto& t : tools_) if (t.id == id) return &t; return nullptr; }
};
} // namespace Tooltip
} // namespace NativeChrome
