#pragma once
// Modal dialog (Paper « Éléments & popups », page p-D-0, artboards A1–A9) and the small
// drawing kit shared with origin-prompt.h and password-demo.h.
//
// API (final; design/missing-elements-contracts.md, adapted where Paper needed more):
//   NativeChrome::Modal::Dialog
//     bool Create(HINSTANCE, HWND owner, const std::wstring& root, Hooks{wake, choose, dismiss})
//     void Dispose()                                   // done({-1}) if still pending
//     bool Show(Spec, RECT area, std::function<void(const Result&)> done) // false if open
//     void Abort()                                     // done({-1}), then closes
//     void Finish(int button)                          // closes as if `button` were pressed
//     void SetBusy(bool, const std::wstring& label)    // A5 « Effacement… »: controls locked
//     void SetError(const std::wstring&)               // A5 failure line above the actions
//     void SetStatus(const Status&)                    // A8 status row (Rechercher → spinner…)
//     void Reposition(RECT area); bool Visible(); HWND Window(); HWND Scrim();
//     Motion::Wake NextWake(Motion::Time) const; void Frame(Motion::Time); void SetReducedMotion(bool)
//   Button ids: CancelButton (0), AcceptButton (1) — every preset's accepting action is
//   AcceptButton; -1 = cancelled/aborted (Échap without a cancel button, Abort, owner gone).
//   A button with `closes = false` keeps the dialog open and calls Spec::onButton(id)
//   (A5 hold-then-busy flow, A8 « Rechercher »); the host ends it with Finish(id).
//
// Window model. Two owned popups of `owner`, both shadowless (NativeUI::ConfigureShadowlessPopup):
//   * the scrim: black 40 % (Paper « Voile »), WS_EX_LAYERED alpha, covering the page area
//     (Scope::Page: alert/confirm/prompt/beforeunload — tabs and address stay usable) or the
//     owner's whole client area (Scope::Window: browser dialogs). It swallows pointer input,
//     so the covered surface cannot be used while the dialog is open; the host keeps its
//     keyboard accelerators off while Visible() (focus lives in the dialog anyway).
//   * the panel: #171717, 1 px #FFFFFF14 border, radius 10 (region, no DWM rounding), 400 DIP
//     (min 320), padding 16, stack gap 16 (24 in À propos). Page dialogs sit 16 DIP under the
//     top of the page area, centred horizontally; browser dialogs are centred in the window.
// No nested modal loop: Show returns at once, `done` runs exactly once.
// Keyboard: Échap = cancel button (or -1), Entrée = default button unless it is destructive
// (Role::Danger) or disabled, Tab/Maj+Tab cycle every stop, Espace activates. Focus starts on
// the safe action (Paper A1) and is restored to what had it before Show.
// Motion: 150 ms opacity (+4 DIP rise) in, 120 ms opacity out; reduced motion: opacity only.
// Hover/press 120/150 ms (Motion::State), pointer-down feedback immediate.
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "chrome.h"
#include "native-ui.h"
#include "accessibility.h"
#include "context-menu.h"
#include "site-request.h"

namespace NativeChrome {
// ---------------------------------------------------------------------------------------
// Drawing kit shared by the dialog, the origin prompts and the password demo.
namespace DialogKit {
// Paper values (A1 « Modale — composant, boutons et états », exported with get_jsx).
namespace Paper {
inline constexpr float width = 400, minWidth = 320, padding = 16, gap = 16, aboutGap = 24, radius = 10;
inline constexpr float border = 0x14 / 255.f;              // #FFFFFF14
inline constexpr float scrim = .40f;                        // black 40 %
inline constexpr float pageTop = 16;                        // page dialogs: 16 DIP under the page top
inline constexpr float button = 32, buttonPad = 12, buttonMin = 64, buttonGap = 8, actionsTop = 8, buttonRadius = 6;
inline constexpr float field = 32, fieldPad = 10, fieldRadius = 6;
inline constexpr float fieldFill = 0x14 / 255.f, fieldHover = 0x1F / 255.f, dangerFill = 0x14 / 255.f; // 8 %, 12 %, danger 8 %
inline constexpr float ringPrimary = 0xB3 / 255.f, ring = 0x80 / 255.f;   // inset 1 px focus rings
inline constexpr float check = 16, checkRadius = 3, checkGlyph = 12, checkRing = 0x4D / 255.f, checkRingHover = 0x80 / 255.f;
inline constexpr float disabled = .32f;
inline constexpr float secondaryText = 0xCC / 255.f, quietText = 0xB3 / 255.f, body = 0xB3 / 255.f, muted = .5f, origin = 0x99 / 255.f, link = 0xE6 / 255.f;
inline constexpr float infoFill = 0x0A / 255.f, listRule = 0x0D / 255.f;
inline constexpr float dangerPlate = 0x24 / 255.f, dangerHover = 0x33 / 255.f, dangerPressed = 0x42 / 255.f;
inline constexpr unsigned long primaryHover = 0x5779FF, primaryPressed = 0x3557D6, dangerLift = 0xFF9489;
inline constexpr float badge = 24, badgePad = 8, badgeTint = .16f;          // « Concept futur » chip (split tint)
inline constexpr float enterMs = 150, exitMs = 120, enterRise = 4;
inline constexpr int messageLines = 12;                                     // A2: scroll after 12 lines
}
inline D2D1_COLOR_F Rgb(unsigned long rgb, float a = 1) { return D2D1::ColorF(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a); }
inline D2D1_COLOR_F White(float a) { return D2D1::ColorF(1, 1, 1, a); }
inline D2D1_COLOR_F Gray(float v, float a = 1) { return D2D1::ColorF(v, v, v, a); }
inline D2D1_COLOR_F Danger(float a = 1) { return D2D1::ColorF(Design::Danger::r, Design::Danger::g, Design::Danger::b, a); }
inline D2D1_COLOR_F AccentColor(float a = 1) { const auto c = Design::Accent::Get(); return D2D1::ColorF(c.r, c.g, c.b, a); }
inline D2D1_COLOR_F Mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) { t = std::clamp(t, 0.f, 1.f); return D2D1::ColorF(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t); }
// A colour at alpha `a` over the opaque canvas, as an opaque colour (EDIT backgrounds).
inline COLORREF OverCanvas(D2D1_COLOR_F c, float a) {
    const float k = Design::Surface::canvas;
    auto ch = [&](float v) { return static_cast<int>(std::lround((k + (v - k) * a) * 255)); };
    return RGB(ch(c.r), ch(c.g), ch(c.b));
}
inline bool Inside(D2D1_RECT_F r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// Phosphor glyphs exactly as exported from Paper (viewBox 0 0 256 256 unless noted).
namespace Glyph {
inline constexpr const char* check = "M232.49,80.49l-128,128a12,12,0,0,1-17,0l-56-56a12,12,0,1,1,17-17L96,183,215.51,63.51a12,12,0,0,1,17,17Z";
inline constexpr const char* warning = "M128,20A108,108,0,1,0,236,128,108.12,108.12,0,0,0,128,20Zm0,192a84,84,0,1,1,84-84A84.09,84.09,0,0,1,128,212Zm-12-80V80a12,12,0,0,1,24,0v52a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,172Z";
inline constexpr const char* caretDown = "M216.49,104.49l-80,80a12,12,0,0,1-17,0l-80-80a12,12,0,0,1,17-17L128,159l71.51-71.52a12,12,0,0,1,17,17Z";
inline constexpr const char* caretRight = "M184.49,136.49l-80,80a12,12,0,0,1-17-17L159,128,87.51,56.49a12,12,0,1,1,17-17l80,80A12,12,0,0,1,184.49,136.49Z";
inline constexpr const char* close = "M208.49,191.51a12,12,0,0,1-17,17L128,145,64.49,208.49a12,12,0,0,1-17-17L111,128,47.51,64.49a12,12,0,0,1,17-17L128,111l63.51-63.52a12,12,0,0,1,17,17L145,128Z";
inline constexpr const char* download = SiteRequest::Path::download;
inline constexpr const char* checkCircle = "M176.49,95.51a12,12,0,0,1,0,17l-56,56a12,12,0,0,1-17,0l-24-24a12,12,0,1,1,17-17L112,143l47.51-47.52A12,12,0,0,1,176.49,95.51ZM236,128A108,108,0,1,1,128,20,108.12,108.12,0,0,1,236,128Zm-24,0a84,84,0,1,0-84,84A84.09,84.09,0,0,0,212,128Z";
// Brand flower (viewBox 936 0 288 288), #DFDFDF in the À propos tile.
inline constexpr const char* flower = "M1178.594 144a49.5 49.5 0 0 0-49.297-85.386 49.5 49.5 0 0 0-98.595 0A49.5 49.5 0 0 0 981.404 144a49.5 49.5 0 0 0 49.298 85.377 49.5 49.5 0 0 0 98.595 0A49.5 49.5 0 0 0 1178.594 144ZM1057.5 144a22.5 22.5 0 1 1 22.5 22.5A22.5 22.5 0 0 1 1057.5 144Zm81.393-59.984a22.5 22.5 0 1 1 22.5 38.97c-2.982 1.722-11.836 5.49-33.862 7.222a49.59 49.59 0 0 0-11.835-20.452C1128.217 91.53 1135.912 85.737 1138.893 84.016ZM1080 40.5a22.5 22.5 0 0 1 22.5 22.5c0 3.442-1.126 12.994-10.677 32.94a49.264 49.264 0 0 0-23.625 0C1058.624 75.994 1057.5 66.444 1057.5 63A22.5 22.5 0 0 1 1080 40.5ZM990.371 92.25a22.5 22.5 0 0 1 30.735-8.234c2.982 1.722 10.676 7.516 23.197 25.74a49.59 49.59 0 0 0-11.835 20.452c-22.028-1.732-30.882-5.502-33.862-7.222A22.5 22.5 0 0 1 990.371 92.25Zm30.735 111.736a22.5 22.5 0 1 1-22.5-38.97c2.982-1.722 11.836-5.49 33.862-7.222a49.59 49.59 0 0 0 11.835 20.452C1031.782 196.47 1024.087 202.264 1021.106 203.986ZM1080 247.5a22.5 22.5 0 0 1-22.5-22.5c0-3.442 1.126-12.994 10.676-32.94a49.264 49.264 0 0 0 23.625 0C1101.374 212.007 1102.5 221.558 1102.5 225A22.5 22.5 0 0 1 1080 247.5Zm89.628-51.75a22.5 22.5 0 0 1-30.735 8.236c-2.982-1.722-10.676-7.516-23.197-25.74a49.59 49.59 0 0 0 11.835-20.452c22.028 1.732 30.882 5.502 33.862 7.222A22.5 22.5 0 0 1 1169.628 195.75Z";
}

// SvgGeometry (SVG path data to a Direct2D geometry) lives in svg-path.h, shared with the toolbar.

// Text styles straight from Paper (size/line-height in px = DIP, tracking in em).
struct Style { float size = 14, line = 20; bool medium = false; float trackingEm = 0; bool tabular = false; };
namespace Type {
inline constexpr Style title{16, 20, true, -.01f}, aboutTitle{20, 24, true, -.01f};   // SemiBold 600 in Paper: Medium (only Regular/Medium ship)
inline constexpr Style body{14, 20}, detail{14, 18}, message{16, 22, false, -.01f}, field{16, 20, false, -.01f};
inline constexpr Style origin{14, 20, true, -.01f}, button{14, 18, true, -.01f}, badge{14, 18, true};
inline constexpr Style tabular{14, 20, false, 0, true}, detailTabular{14, 18, false, 0, true};
}

// Shared Direct2D/DirectWrite helpers bound to a component's Chrome.
class Kit {
    Chrome* chrome_ = nullptr;
    std::map<const char*, ID2D1PathGeometry*> glyphs_;
    ID2D1PathGeometry* spinner_ = nullptr;
    float ascent_ = .96875f, descent_ = .2421875f; bool metrics_ = false;
    void Metrics() {
        if (metrics_ || !chrome_ || !chrome_->FontCollection()) return;
        metrics_ = true; UINT32 index = 0; BOOL found = FALSE;
        if (FAILED(chrome_->FontCollection()->FindFamilyName(L"Inter Display", &index, &found)) || !found) return;
        IDWriteFontFamily* family = nullptr; IDWriteFont* font = nullptr;
        if (SUCCEEDED(chrome_->FontCollection()->GetFontFamily(index, &family)) && SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS m{}; font->GetMetrics(&m);
            if (m.designUnitsPerEm) { ascent_ = static_cast<float>(m.ascent) / m.designUnitsPerEm; descent_ = static_cast<float>(m.descent) / m.designUnitsPerEm; }
        }
        if (font) font->Release(); if (family) family->Release();
    }
public:
    ~Kit() { Release(); }
    void Bind(Chrome* chrome) { chrome_ = chrome; }
    void Release() { for (auto& g : glyphs_) if (g.second) g.second->Release(); glyphs_.clear(); if (spinner_) spinner_->Release(); spinner_ = nullptr; }
    ID2D1RenderTarget* Target() const { return chrome_ ? chrome_->RenderTarget() : nullptr; }
    ID2D1SolidColorBrush* Brush() const { return chrome_ ? chrome_->SolidBrush() : nullptr; }
    // Caller releases. `wrap` = width-bound multi-line (overflowWrap anywhere), else one trimmed line.
    IDWriteTextLayout* Layout(const std::wstring& text, Style s, float width, bool wrap) {
        if (!chrome_ || !chrome_->WriteFactory() || !chrome_->FontCollection()) return nullptr;
        Metrics();
        IDWriteTextFormat* format = nullptr; IDWriteTextLayout* layout = nullptr;
        if (FAILED(chrome_->WriteFactory()->CreateTextFormat(L"Inter Display", chrome_->FontCollection(), s.medium ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, s.size, L"fr-FR", &format))) return nullptr;
        format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_EMERGENCY_BREAK : DWRITE_WORD_WRAPPING_NO_WRAP);
        // CSS line box: the content area (ascent+descent) centred in `line`.
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, s.line, (s.line - (ascent_ + descent_) * s.size) / 2 + ascent_ * s.size);
        const HRESULT hr = chrome_->WriteFactory()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, std::max(1.f, width), 100000.f, &layout);
        format->Release(); if (FAILED(hr)) return nullptr;
        const DWRITE_TEXT_RANGE all{0, static_cast<UINT32>(text.size())};
        if (s.trackingEm != 0) { IDWriteTextLayout1* l1 = nullptr; if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(&l1)))) { l1->SetCharacterSpacing(s.trackingEm * s.size, 0, 0, all); l1->Release(); } }
        IDWriteTypography* typo = nullptr;
        if (SUCCEEDED(chrome_->WriteFactory()->CreateTypography(&typo))) {
            for (auto tag : {DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '1'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '2'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '3'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '0', '4'), DWRITE_MAKE_OPENTYPE_TAG('c', 'v', '1', '3')}) typo->AddFontFeature({static_cast<DWRITE_FONT_FEATURE_TAG>(tag), 1});
            if (s.tabular) typo->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
            layout->SetTypography(typo, all); typo->Release();
        }
        if (!wrap) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; IDWriteInlineObject* ellipsis = nullptr;
            chrome_->WriteFactory()->CreateEllipsisTrimmingSign(layout, &ellipsis); layout->SetTrimming(&trim, ellipsis); if (ellipsis) ellipsis->Release();
        }
        return layout;
    }
    float Width(const std::wstring& text, Style s) {
        auto* l = Layout(text, s, 100000, false); if (!l) return 0;
        DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release(); return std::ceil(m.widthIncludingTrailingWhitespace);
    }
    float Height(const std::wstring& text, Style s, float width) {
        if (text.empty()) return 0;
        auto* l = Layout(text, s, width, true); if (!l) return s.line;
        DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m); l->Release(); return std::max(s.line, std::round(m.lineCount * s.line));
    }
    // Draws `text` with its top at `top` (line boxes of s.line). `clip` limits the ink.
    void Text(const std::wstring& text, Style s, float left, float top, float width, D2D1_COLOR_F color, bool wrap = false, const D2D1_RECT_F* clip = nullptr) {
        auto* t = Target(); auto* b = Brush(); if (!t || !b || text.empty()) return;
        auto* l = Layout(text, s, width, wrap); if (!l) return;
        if (clip) t->PushAxisAlignedClip(*clip, D2D1_ANTIALIAS_MODE_ALIASED);
        b->SetColor(color); t->DrawTextLayout(D2D1::Point2F(left, top), l, b, D2D1_DRAW_TEXT_OPTIONS_NONE);
        if (clip) t->PopAxisAlignedClip();
        l->Release();
    }
    void Fill(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) { auto* t = Target(); auto* b = Brush(); if (!t || !b) return; b->SetColor(c); if (radius > 0) t->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b); else t->FillRectangle(r, b); }
    // 1 px inset outline (box-shadow 0 0 0 1px inset), in device pixels at any DPI.
    void Ring(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
        auto* t = Target(); auto* b = Brush(); if (!t || !b) return;
        const float px = Pixel(); b->SetColor(c);
        t->DrawRoundedRectangle(D2D1::RoundedRect({r.left + px / 2, r.top + px / 2, r.right - px / 2, r.bottom - px / 2}, std::max(0.f, radius - px / 2), std::max(0.f, radius - px / 2)), b, px);
    }
    float Pixel() const { auto* t = Target(); if (!t) return 1; return t->GetSize().width / std::max(1.f, static_cast<float>(t->GetPixelSize().width)); }
    void DrawGlyph(const char* d, D2D1_RECT_F box, float size, D2D1_COLOR_F color, float viewMin = 0, float viewSize = 256) {
        auto* t = Target(); auto* b = Brush(); if (!t || !b || !chrome_ || !chrome_->Factory()) return;
        auto& g = glyphs_[d]; if (!g) g = SvgGeometry(chrome_->Factory(), d); if (!g) return;
        D2D1_MATRIX_3X2_F old; t->GetTransform(&old);
        const float k = size / viewSize, x = (box.left + box.right - size) / 2, y = (box.top + box.bottom - size) / 2;
        t->SetTransform(D2D1::Matrix3x2F::Translation(-viewMin, 0) * D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * old);
        b->SetColor(color); t->FillGeometry(g, b); t->SetTransform(old);
    }
    // 16 px radio (B08 certificate rows): 1.5 px ring, filled 8 px dot when selected; `opacity` dims disabled rows.
    void Radio(D2D1_RECT_F box, bool on, float opacity) {
        auto* t = Target(); auto* b = Brush(); if (!t || !b) return;
        const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
        b->SetColor(White((on ? .9f : .45f) * opacity)); t->DrawEllipse(D2D1::Ellipse({cx, cy}, 7.f, 7.f), b, 1.5f);
        if (on) { b->SetColor(White(.9f * opacity)); t->FillEllipse(D2D1::Ellipse({cx, cy}, 4.f, 4.f), b); }
    }
    // Paper 16 px spinner: r 6.5 track #FFFFFF26, quarter arc #FFFFFFB3, 1.5 px, round caps.
    void Spinner(D2D1_RECT_F box, float turn) {
        auto* t = Target(); auto* b = Brush(); if (!t || !b || !chrome_ || !chrome_->Factory()) return;
        const float cx = (box.left + box.right) / 2, cy = (box.top + box.bottom) / 2;
        b->SetColor(White(0x26 / 255.f)); t->DrawEllipse(D2D1::Ellipse({cx, cy}, 6.5f, 6.5f), b, 1.5f);
        if (!spinner_) {
            if (SUCCEEDED(chrome_->Factory()->CreatePathGeometry(&spinner_))) {
                ID2D1GeometrySink* s = nullptr;
                if (SUCCEEDED(spinner_->Open(&s))) { s->BeginFigure({0, -6.5f}, D2D1_FIGURE_BEGIN_HOLLOW); s->AddArc(D2D1::ArcSegment({6.5f, 0}, {6.5f, 6.5f}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL)); s->EndFigure(D2D1_FIGURE_END_OPEN); s->Close(); s->Release(); }
            }
        }
        if (!spinner_) return;
        ID2D1StrokeStyle* round = nullptr; chrome_->Factory()->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &round);
        D2D1_MATRIX_3X2_F old; t->GetTransform(&old);
        t->SetTransform(D2D1::Matrix3x2F::Rotation(turn * 360.f) * D2D1::Matrix3x2F::Translation(cx, cy) * old);
        b->SetColor(White(0xB3 / 255.f)); t->DrawGeometry(spinner_, b, 1.5f, round); t->SetTransform(old);
        if (round) round->Release();
    }
    // Dialog buttons (A1 « Boutons de dialogue »): role, hover/press 0..1, focus, disabled.
    enum class Tone { Primary, Secondary, Danger, Quiet };
    void Button(D2D1_RECT_F r, const std::wstring& label, Tone tone, float hover, float press, bool focus, bool disabled) {
        auto* t = Target(); if (!t) return;
        if (disabled) hover = press = 0;
        D2D1_COLOR_F fill{}, ink = White(1); float ringAlpha = Paper::ring;
        const float raised = Design::Surface::raised, hov = Design::Surface::hover, pre = Design::Surface::pressed;
        switch (tone) {
        case Tone::Primary: fill = Mix(Mix(AccentColor(), Rgb(Paper::primaryHover), hover), Rgb(Paper::primaryPressed), press); ringAlpha = Paper::ringPrimary; break;
        case Tone::Secondary: fill = Mix(Mix(Gray(raised), Gray(hov), hover), Gray(pre), press); ink = White(focus ? 1 : Paper::secondaryText + (1 - Paper::secondaryText) * std::max(hover, press)); break;
        case Tone::Danger: fill = Danger(Paper::dangerPlate + (Paper::dangerHover - Paper::dangerPlate) * hover + (Paper::dangerPressed - Paper::dangerHover) * press); ink = Mix(Danger(), Rgb(Paper::dangerLift), std::max(hover, press)); break;
        case Tone::Quiet: fill = Gray(hov, hover); fill = Mix(fill, Gray(pre), press); ink = White(focus ? 1 : Paper::quietText + (1 - Paper::quietText) * std::max(hover, press)); break;
        }
        ID2D1Layer* layer = nullptr;
        if (disabled && SUCCEEDED(t->CreateLayer(nullptr, &layer))) t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), Paper::disabled), layer);
        if (fill.a > 0) Fill(r, Paper::buttonRadius, fill);
        if (focus && !disabled) Ring(r, Paper::buttonRadius, White(ringAlpha));
        const float w = Width(label, Type::button);
        Text(label, Type::button, (r.left + r.right - w) / 2, (r.top + r.bottom - Type::button.line) / 2, w + 2, ink);
        if (layer) { t->PopLayer(); layer->Release(); }
    }
    float ButtonWidth(const std::wstring& label) { return std::max(Paper::buttonMin, Width(label, Type::button) + 2 * Paper::buttonPad); }
    // Check box (A1 « Case à cocher »): ring 30 % (hover 50 %), checked fill #3E3E3E + 12 px check.
    void CheckBox(D2D1_RECT_F box, bool checked, float hover) {
        if (checked) { Fill(box, Paper::checkRadius, Gray(Design::Surface::pressed)); DrawGlyph(DialogKit::Glyph::check, box, Paper::checkGlyph, White(1)); }
        else Ring(box, Paper::checkRadius, White(Paper::checkRing + (Paper::checkRingHover - Paper::checkRing) * hover));
    }
    // « Démonstration » chip, Paper « Concept futur » badge: split tint 16 %, 24 px, 14/18 Medium.
    float BadgeWidth(const std::wstring& label) { return Width(label, Type::badge) + 2 * Paper::badgePad; }
    void Badge(float left, float top, const std::wstring& label) {
        const float w = BadgeWidth(label);
        Fill({left, top, left + w, top + Paper::badge}, Design::Radius::control, AccentColor(Paper::badgeTint));
        Text(label, Type::badge, left + Paper::badgePad, top + (Paper::badge - Type::badge.line) / 2, w, White(1));
    }
};

// Native EDIT for a dialog field: Inter Display 16, colours from the field state (fill 8 %,
// hover 12 %, invalid danger 8 %), accent caret, placeholder drawn when empty, ES_PASSWORD.
struct EditField {
    HWND edit = nullptr; HBRUSH brush = nullptr; COLORREF color = 0; std::wstring placeholder; bool password = false;
    NativeUI::AccentCaret caret;
    void Destroy() { if (edit && IsWindow(edit)) DestroyWindow(edit); edit = nullptr; if (brush) DeleteObject(brush); brush = nullptr; }
    void SetColor(COLORREF c) { if (brush && c == color) return; if (brush) DeleteObject(brush); brush = CreateSolidBrush(c); color = c; if (edit) InvalidateRect(edit, nullptr, TRUE); }
    std::wstring Text() const { if (!edit) return {}; const int n = GetWindowTextLengthW(edit); std::wstring s(static_cast<size_t>(n) + 1, L'\0'); GetWindowTextW(edit, s.data(), n + 1); s.resize(static_cast<size_t>(n)); return s; }
    // Best effort for secrets: overwrite the EDIT buffer before clearing it.
    void Wipe() { if (!edit) return; const int n = GetWindowTextLengthW(edit); if (n > 0) { std::wstring fill(static_cast<size_t>(n), L'\x2022'); SetWindowTextW(edit, fill.c_str()); } SetWindowTextW(edit, L""); }
};
inline LRESULT CALLBACK PlaceholderProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
    auto* f = reinterpret_cast<EditField*>(data);
    if (msg == WM_NCDESTROY) { RemoveWindowSubclass(h, PlaceholderProc, 0x5048); return DefSubclassProc(h, msg, w, l); }
    if (msg == WM_PAINT && f && !f->placeholder.empty() && GetWindowTextLengthW(h) == 0) {
        const LRESULT r = DefSubclassProc(h, msg, w, l);
        HDC dc = GetDC(h); RECT rc{}; GetClientRect(h, &rc);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)); auto old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, NativeUI::AccentCaret::Mix(f->color, RGB(255, 255, 255), .5f)); // #FFFFFF80
        DrawTextW(dc, f->placeholder.c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, old); ReleaseDC(h, dc);
        if (GetFocus() == h) { HideCaret(h); ShowCaret(h); }
        return r;
    }
    if (msg == WM_SETTEXT || msg == WM_CHAR || msg == WM_KEYDOWN || msg == WM_CUT || msg == WM_PASTE || msg == WM_CLEAR || msg == WM_UNDO) { const LRESULT r = DefSubclassProc(h, msg, w, l); InvalidateRect(h, nullptr, TRUE); return r; }
    return DefSubclassProc(h, msg, w, l);
}
} // namespace DialogKit

// ---------------------------------------------------------------------------------------
// Glyphs of the origin popovers (B01–B08), Paper exports, viewBox 0 0 256 256.
namespace DialogKit { namespace Glyph {
inline constexpr const char* camera = SiteRequest::Path::camera;
inline constexpr const char* microphone = SiteRequest::Path::microphone;
inline constexpr const char* position = SiteRequest::Path::position;
inline constexpr const char* bell = SiteRequest::Path::bell;
inline constexpr const char* clipboard = SiteRequest::Path::clipboard;
inline constexpr const char* info = SiteRequest::Path::info;
inline constexpr const char* lockOpen = "M208,76H100V56a28,28,0,0,1,28-28c13.51,0,25.65,9.62,28.24,22.39a12,12,0,1,0,23.52-4.78C174.87,21.5,153.1,4,128,4A52.06,52.06,0,0,0,76,56V76H48A20,20,0,0,0,28,96V208a20,20,0,0,0,20,20H208a20,20,0,0,0,20-20V96A20,20,0,0,0,208,76Zm-4,128H52V100H204Z";
inline constexpr const char* external = "M228,104a12,12,0,0,1-24,0V69l-59.51,59.51a12,12,0,0,1-17-17L187,52H152a12,12,0,0,1,0-24h64a12,12,0,0,1,12,12Zm-44,24a12,12,0,0,0-12,12v64H52V84h64a12,12,0,0,0,0-24H48A20,20,0,0,0,28,80V208a20,20,0,0,0,20,20H176a20,20,0,0,0,20-20V140A12,12,0,0,0,184,128Z";
inline constexpr const char* triangle = "M240.26,186.1,152.81,34.23h0a28.74,28.74,0,0,0-49.62,0L15.74,186.1a27.45,27.45,0,0,0,0,27.71A28.31,28.31,0,0,0,40.55,228h174.9a28.31,28.31,0,0,0,24.79-14.19A27.45,27.45,0,0,0,240.26,186.1Zm-20.8,15.7a4.46,4.46,0,0,1-4,2.2H40.55a4.46,4.46,0,0,1-4-2.2,3.56,3.56,0,0,1,0-3.73L124,46.2a4.77,4.77,0,0,1,8,0l87.44,151.87A3.56,3.56,0,0,1,219.46,201.8ZM116,136V104a12,12,0,0,1,24,0v32a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,176Z";
inline constexpr const char* file = SiteRequest::Path::file;
inline constexpr const char* key = "M160,12A84.09,84.09,0,0,0,79.43,119.53L23.51,175.51A12,12,0,0,0,20,184v40a12,12,0,0,0,12,12H72a12,12,0,0,0,12-12V212H96a12,12,0,0,0,12-12V188h12a12,12,0,0,0,8.49-3.51l7.93-7.94A84,84,0,1,0,160,12Zm0,144a59.66,59.66,0,0,1-24.43-5.18,12,12,0,0,0-13.48,2.45L115,164H96a12,12,0,0,0-12,12v12H72a12,12,0,0,0-12,12v12H44V189l58.73-58.73a12,12,0,0,0,2.45-13.48A60,60,0,1,1,160,156Zm36-76a16,16,0,1,1-16-16A16,16,0,0,1,196,80Z";
inline constexpr const char* eye = "M251,123.13c-.37-.81-9.13-20.26-28.48-39.61C196.63,57.67,164,44,128,44S59.37,57.67,33.51,83.52C14.16,102.87,5.4,122.32,5,123.13a12.08,12.08,0,0,0,0,9.75c.37.82,9.13,20.26,28.49,39.61C59.37,198.34,92,212,128,212s68.63-13.66,94.48-39.51c19.36-19.35,28.12-38.79,28.49-39.61A12.08,12.08,0,0,0,251,123.13Zm-46.06,33C183.47,177.27,157.59,188,128,188s-55.47-10.73-76.91-31.88A130.36,130.36,0,0,1,29.52,128,130.45,130.45,0,0,1,51.09,99.89C72.54,78.73,98.41,68,128,68s55.46,10.73,76.91,31.89A130.36,130.36,0,0,1,226.48,128,130.45,130.45,0,0,1,204.91,156.12ZM128,84a44,44,0,1,0,44,44A44.05,44.05,0,0,0,128,84Zm0,64a20,20,0,1,1,20-20A20,20,0,0,1,128,148Z";
// Not exported in Paper's B02 grid (MIDI, screen, certificate, generic): closest Phosphor shapes drawn from the same set.
inline constexpr const char* screen = SiteRequest::Path::screen;
}}

namespace Modal {
namespace P = DialogKit::Paper;
enum class Kind { Generic, Alert, Confirm, Prompt, BeforeUnload, ClearData, CloseWindow, OpenManyTabs, DeleteBookmarks, About, Private, Origin, Passwords };
enum class Role { Primary, Secondary, Danger, Quiet };
// Page: scrim on `area`, 16 DIP under its top. Window: scrim on the owner's client area,
// centred. Anchor: no scrim, under Spec::anchor (light dismiss).
enum class Scope { Page, Window, Anchor };
inline constexpr int CancelButton = 0, AcceptButton = 1;
inline constexpr int PrivateClearDataButton = 2;                        // A9 « Effacer les données… »
inline constexpr int AboutCheckButton = 3, AboutRestartButton = 4, AboutDownloadButton = 5, AboutCancelButton = 7;   // A8 status actions (7: engine Cancel while downloading)
inline constexpr int AboutNotesLink = 101, AboutLicensesLink = 102, AboutReportLink = 103;
struct Button {
    int id = CancelButton; std::wstring label; Role role = Role::Secondary;
    bool isDefault = false, isCancel = false, disabled = false, initialFocus = false;
    bool closes = true;            // false: Spec::onButton(id), the dialog stays open
    bool leading = false;          // placed at the start of the row (B04 « Jamais pour ce site »)
    std::wstring automationId;
};
struct Field {
    std::wstring label, value, placeholder, error; bool password = false, required = false, reveal = false;
    std::wstring automationId; int maxLength = 4096;
};
struct Check { std::wstring label, detail; bool value = false, disabled = false; };
struct Select {
    std::wstring label; std::vector<std::wstring> options; int index = 0;
    std::function<std::vector<std::wstring>(int)> details;   // new check details for an option (A5 counts)
};
enum class StatusIcon { None, Check, Download, Warning, Busy };
struct Status { StatusIcon icon = StatusIcon::None; std::wstring title, detail; Button button; bool hasButton = false; };
struct Info { std::wstring text, trailing; };                 // A6 download in progress row
struct Link { int id = 0; std::wstring label; };
struct Notice { const char* glyph = nullptr; std::wstring text; bool boxed = false; }; // B06 lock box / info line
// B01/B02 request rows. `toggle` (B02 « Clic sur la puce », EAC-0): -1 none, 1 on, 0 off. A toggle
// only ever switches off from here (revoking); an off toggle is inert, re-granting needs the site
// to ask again. Its row calls Spec::onButton(Dialog::RowHit(i)) and the dialog stays open.
struct Row { const char* glyph = nullptr; std::wstring label, trailing; int toggle = -1; std::wstring detail; };
// B03 fill list: `masked` is the password as dots (tracked 0.08em, Paper EK1-0) unless `plain`;
// a plain second line (certificate issuer, FW8-0) is ordinary 14 px secondary text, tracking 0.
struct Account { std::wstring user, masked; bool plain = false, disabled = false; };   // disabled: shown at 32 %, not selectable (expired certificate)
struct DangerCard { std::wstring title, subtitle, text; bool shown = false; };       // B07
// B08 screen share picker (Démonstration): categories in a segmented control (Paper FX2-0), the
// sources of the current one as thumbnails (FX9-0) drawn locally from `art` (fictitious content,
// never a capture). Selecting only marks a source; the accept button stays disabled until one is.
struct Source { std::wstring title; int category = 0, art = 0; };
struct SourcePicker {
    std::vector<std::wstring> categories; std::vector<Source> items; int category = 0, selected = -1;
    std::wstring empty = L"Aucune source disponible dans cette catégorie.";
    bool shown = false; int acceptId = 1;   // Modal::AcceptButton
};
struct Spec {
    Kind kind = Kind::Generic; Scope scope = Scope::Window;
    std::wstring automationId, title, subtitle, body, origin /* « exemple.com indique » */, message /* page text */, footnote;
    std::shared_ptr<const FaviconImage> favicon;               // page dialogs: the site icon (else a neutral glyph)
    bool appTile = false, closeButton = false, largeTitle = false;
    const char* headGlyph = nullptr;                           // 16 DIP glyph left of the title (B02)
    const char* tileGlyph = nullptr;                           // 32 DIP raised tile left of the title (B06 app)
    float width = 0, padding = 0, gap = 0;                     // 0: Paper defaults (400 / 16 / 16)
    RECT anchor{}; bool anchorRight = false;                   // Scope::Anchor
    std::vector<Field> fields;
    std::wstring checkLabel; bool checkValue = false, originCheck = false; // single row check (A2/A6; B0x style)
    // A6 420 (Paper EX1-0 / EY9-0, comment c-npdt3g): key caps after the body (KKC-0: padding-left 4,
    // gap 4; cap KKD-0 #FFFFFF0F radius 4, padding 2/6, 14/16 tabular at 50 %), and the single check
    // on the actions row, left (EYG-0 space-between) when it fits beside the buttons.
    std::vector<std::wstring> bodyKeys; bool checkInActions = false;
    // B06 (user override of Paper FJ5-0): the check row shares the head's lanes — box centred in the
    // 32 DIP tile lane, label on the description's left edge, same 14/18 regular role, 70 % white.
    bool checkInHeadLane = false;
    std::vector<Check> checks;                                 // detailed list (A5)
    Select select; bool hasSelect = false;
    std::vector<Notice> notices; std::vector<Row> rows; std::vector<Info> infos;
    Status status; bool hasStatus = false;
    std::vector<Link> links;                                   // links close with done(link.id)
    std::wstring accountsTitle; std::vector<Account> accounts; std::wstring manageLabel; int manageId = -1; // B03
    DangerCard danger;
    SourcePicker sources;
    bool accountsNavigate = false;                             // B05: account rows call onButton(Dialog::AccountHit(i)) and stay open
    // B08 certificate (E1): rows are radios. A click/arrow/Espace only selects; the AcceptButton
    // (« Envoyer ») stays disabled until a valid row is selected and alone transmits the choice.
    bool accountsSelect = false; int accountSelected = -1;
    std::vector<Button> buttons; bool stretchButtons = false, softButtons = false;
    bool demo = false;                                         // badge « Démonstration »
    std::function<std::wstring(const std::vector<std::wstring>&)> validate; // inline error or ""
    std::function<void(int)> onButton;                         // buttons with closes = false
    // Input that opened the dialog. Focus always lands on the initial control (Entrée / Échap work);
    // only the RING depends on it: Keyboard shows it at once, Pointer hides it until Tab / arrows /
    // Espace, Host asks Hooks::keyboard (the host's last chrome input). Page-originated requests
    // (permissions, JavaScript dialogs) say Pointer: the page's own input is not visible to us.
    enum class Modality { Host, Pointer, Keyboard } modality = Modality::Host;
};
struct Result { int button = -1; std::vector<std::wstring> values; bool checked = false; std::vector<bool> checks; int select = -1; };

// The select's list: the shared native menu (Paper « Clic droit menu » rows, region-rounded and
// shadowless), dropped under the field with the right edges aligned, the current option checked
// and, from the keyboard, highlighted. Returns the chosen index, -1 when dismissed.
// Second line of an account row: password dots keep Paper's 0.08em; any other text is not tracked.
inline DialogKit::Style AccountHintStyle(const Account& a) { return {14, 16, false, a.plain ? 0.f : .08f}; }

inline int ChooseFromMenu(ContextMenu::Menu& menu, const std::vector<std::wstring>& options, int current, RECT anchor, bool keyboard, bool reduced) {
    std::vector<ContextMenu::Item> items;
    for (size_t i = 0; i < options.size(); ++i) {
        std::wstring label; for (wchar_t c : options[i]) { label += c; if (c == L'&') label += L'&'; }   // no mnemonics
        items.push_back(ContextMenu::Command(static_cast<int>(i) + 1, std::move(label), L"", true, static_cast<int>(i) == current));
    }
    const int id = menu.Track(items, {anchor.right, anchor.bottom}, &anchor, keyboard, reduced, true, true);
    return id > 0 && id <= static_cast<int>(options.size()) ? id - 1 : -1;
}

class Dialog {
public:
    struct Hooks {
        std::function<void()> wake;   // ask the host clock for a frame (NextWake changed)
        // Native list for the Période select (B07 « Plus d'options »): chosen index or -1. Hosts with a
        // shared ContextMenu::Menu pass ChooseFromMenu on it; without a hook the dialog opens its own
        // (same shadowless menu). Choosing only changes the value: it never triggers the action.
        std::function<int(const std::vector<std::wstring>&, int current, RECT anchorScreen, bool keyboard)> choose;
        std::function<void()> dismiss;   // with `choose` / `menu`: end that list now (the dialog closed while it was open)
        // Command list (B07 « Plus d'options »): chosen id, 0 when dismissed. Hosts pass their shared
        // ContextMenu::Menu; without it the dialog opens its own (same shadowless menu).
        std::function<int(const std::vector<ContextMenu::Item>&, RECT anchorScreen, bool keyboard)> menu;
        std::function<bool()> keyboard;   // Spec::Modality::Host: was the host's last input the keyboard?
    };
    // Focus/hit ids (stable; exposed for tests and UIA).
    static int ButtonHit(size_t i) { return 1000 + static_cast<int>(i); }
    static int CheckHit(size_t i) { return 2000 + static_cast<int>(i); }
    static constexpr int SimpleCheckHit = 2999, SelectHit = 3000, StatusHit = 5000, CloseHit = 6000, ManageHit = 6100;
    static int LinkHit(size_t i) { return 4000 + static_cast<int>(i); }
    static int AccountHit(size_t i) { return 6200 + static_cast<int>(i); }
    static int RevealHit(size_t i) { return 6900 + static_cast<int>(i); }
    static int FieldStop(size_t i) { return 7000 + static_cast<int>(i); }
    static int RowHit(size_t i) { return 9000 + static_cast<int>(i); }
    static bool IsRow(int hit) { return hit >= 9000 && hit < 9100; }
    static int CategoryHit(size_t i) { return 8000 + static_cast<int>(i); }
    static int SourceHit(size_t i) { return 8100 + static_cast<int>(i); }   // index into Spec::sources.items
    static bool IsCategory(int hit) { return hit >= 8000 && hit < 8100; }
    static bool IsSource(int hit) { return hit >= 8100 && hit < 8900; }
private:
    using Kit = DialogKit::Kit;
    enum class B { Badge, Head, Message, Select, Segments, Sources, Checks, Field, Notice, Rows, Info, Status, Links, Footnote, Check, Accounts, Danger, Error, Actions };
    struct Block { B type; size_t index; D2D1_RECT_F r; };
    struct Box { int hit = 0; D2D1_RECT_F r{}; };
    HWND hwnd_ = nullptr, scrim_ = nullptr, owner_ = nullptr, restore_ = nullptr; HINSTANCE instance_ = nullptr;
    Chrome chrome_; Kit kit_; Motion::State motion_; Hooks hooks_; Accessibility::Bridge uia_;
    Chrome* pc_ = &chrome_;   // paint target (the window, or an offscreen bitmap for RenderForTest)
    std::wstring root_;
    Spec spec_; std::function<void(const Result&)> done_; RECT area_{};
    std::unique_ptr<ContextMenu::Menu> menu_;   // the select's list when no Hooks::choose
    unsigned generation_ = 0;                   // bumped by Show/Update: a list answer for an older spec is dropped
    bool hostMenu_ = false;                     // the open list belongs to the host (Hooks::choose / menu)
    std::vector<DialogKit::EditField> edits_; std::vector<bool> revealed_; HFONT font_ = nullptr;
    std::vector<Block> blocks_; std::vector<Box> boxes_; std::vector<float> fieldTops_;
    float width_ = P::width, height_ = 0;
    float messageHeight_ = 0, messageFull_ = 0, scroll_ = 0; int messageLines_ = P::messageLines;
    bool open_ = false, closing_ = false, keyboard_ = true, busy_ = false, submitted_ = false, reduced_ = false, choosing_ = false;
    std::wstring busyLabel_, error_, fieldError_;
    int hover_ = 0, pressed_ = 0, focus_ = 0;
    float thumbScale_ = 1; int sourceColumns_ = 2;   // B08 grid: shrinks thumbnails before overflowing
    Motion::Tween fade_; Motion::Time spinStart_{};
    static constexpr long long GroupKey = 1, DangerKey = 7500;   // DangerKey: B07 card group (UIA only, not a hit)

    UINT Dpi() const { return GetDpiForWindow(hwnd_ ? hwnd_ : owner_); }
    int Px(float dip) const { return static_cast<int>(std::lround(dip * Dpi() / 96.f)); }
    float Dip(int px) const { return px * 96.f / Dpi(); }
    bool Anchored() const { return spec_.scope == Scope::Anchor; }
    float Pad() const { return spec_.padding > 0 ? spec_.padding : P::padding; }
    float Gap() const { return spec_.gap > 0 ? spec_.gap : spec_.appTile ? P::aboutGap : P::gap; }
    float ContentWidth() const { return width_ - 2 * Pad(); }
    bool Invalid() const {
        if (!fieldError_.empty()) return true;
        for (size_t i = 0; i < spec_.fields.size() && i < edits_.size(); ++i) if (spec_.fields[i].required && edits_[i].Text().empty()) return true;
        return false;
    }
    bool ButtonDisabled(const Button& b) const { return busy_ || b.disabled || (b.isDefault && submitted_ && Invalid()); }
    D2D1_RECT_F Find(int hit) const { for (auto& b : boxes_) if (b.hit == hit) return b.r; return {}; }
    const Block* BlockOf(B type, size_t index = 0) const { for (auto& b : blocks_) if (b.type == type && b.index == index) return &b; return nullptr; }
    int HitAt(float x, float y) const { for (auto it = boxes_.rbegin(); it != boxes_.rend(); ++it) if (DialogKit::Inside(it->r, x, y)) return it->hit; return 0; }
    bool Enabled(int hit) const {
        if (busy_ || hit == 0) return false;
        if (hit >= 1000 && hit < 2000) return !ButtonDisabled(spec_.buttons[static_cast<size_t>(hit - 1000)]);
        if (hit >= 2000 && hit < 2999) return !spec_.checks[static_cast<size_t>(hit - 2000)].disabled;
        if (hit == StatusHit) return spec_.hasStatus && spec_.status.hasButton && !spec_.status.button.disabled;
        if (hit >= 6200 && hit < 6900) { const size_t i = static_cast<size_t>(hit - 6200); return i < spec_.accounts.size() && !spec_.accounts[i].disabled; }
        if (IsRow(hit)) { const size_t i = static_cast<size_t>(hit - 9000); return i < spec_.rows.size() && spec_.rows[i].toggle == 1; }
        if (IsSource(hit)) return spec_.sources.shown && static_cast<size_t>(hit - 8100) < spec_.sources.items.size() && spec_.sources.items[static_cast<size_t>(hit - 8100)].category == spec_.sources.category;
        return true;
    }
    // ---- B08 source picker ----------------------------------------------------------------------
    std::vector<size_t> VisibleSources() const {
        std::vector<size_t> v; const auto& sp = spec_.sources;
        for (size_t i = 0; i < sp.items.size(); ++i) if (sp.items[i].category == sp.category) v.push_back(i);
        return v;
    }
    int SourceStop() const {   // the grid's single Tab stop: the selected source, else the first one
        const auto v = VisibleSources(); if (v.empty()) return 0;
        for (size_t i : v) if (static_cast<int>(i) == spec_.sources.selected) return SourceHit(i);
        return SourceHit(v.front());
    }
    void SyncAccept() {
        if (spec_.accountsSelect) { for (auto& b : spec_.buttons) if (b.id == AcceptButton) b.disabled = spec_.accountSelected < 0; return; }
        for (auto& b : spec_.buttons) if (b.id == spec_.sources.acceptId) b.disabled = spec_.sources.selected < 0;
    }
    void SelectAccount(size_t i, bool keyboard) {
        if (!spec_.accountsSelect || i >= spec_.accounts.size() || spec_.accounts[i].disabled) return;
        spec_.accountSelected = static_cast<int>(i); SyncAccept();
        keyboard_ = keyboard; FocusStop(AccountHit(i)); InvalidateRect(hwnd_, nullptr, FALSE);
        if (uia_.Listening()) { uia_.Raise(AccountHit(i), UIA_SelectionItem_ElementSelectedEventId); uia_.ChildrenChanged(GroupKey); }
    }
    void SetCategory(int c, bool keyboard) {
        auto& sp = spec_.sources; if (!sp.shown || sp.categories.empty()) return;
        c = std::clamp(c, 0, static_cast<int>(sp.categories.size()) - 1);
        if (c != sp.category) {
            sp.category = c;
            // A source of another category is never shared by accident: the choice starts over.
            if (sp.selected >= 0 && (sp.selected >= static_cast<int>(sp.items.size()) || sp.items[static_cast<size_t>(sp.selected)].category != c)) sp.selected = -1;
            SyncAccept(); Relayout();
        }
        keyboard_ = keyboard; FocusStop(CategoryHit(static_cast<size_t>(c)));
        if (uia_.Listening()) uia_.Raise(CategoryHit(static_cast<size_t>(c)), UIA_SelectionItem_ElementSelectedEventId);
    }
    void SelectSource(size_t i, bool keyboard) {
        auto& sp = spec_.sources; if (!sp.shown || i >= sp.items.size() || sp.items[i].category != sp.category) return;
        sp.selected = static_cast<int>(i); SyncAccept();
        keyboard_ = keyboard; FocusStop(SourceHit(i));
        if (uia_.Listening()) { uia_.Raise(SourceHit(i), UIA_SelectionItem_ElementSelectedEventId); uia_.ChildrenChanged(GroupKey); }
    }
    // Arrows move the selection inside the grid (left/right by one, up/down by a row), like a list box.
    bool MoveSource(int step) {
        const auto v = VisibleSources(); if (v.empty()) return true;
        auto it = std::find(v.begin(), v.end(), static_cast<size_t>(focus_ - 8100));
        int i = it == v.end() ? 0 : static_cast<int>(it - v.begin()) + step;
        i = std::clamp(i, 0, static_cast<int>(v.size()) - 1);
        SelectSource(v[static_cast<size_t>(i)], true); return true;
    }
    // ---- layout (DIP) -------------------------------------------------------------------------
    float HeadTextWidth() const {
        float w = ContentWidth();
        if (spec_.tileGlyph) w -= 32 + 12; else if (spec_.headGlyph) w -= 24 + 8;
        if (spec_.closeButton) w -= 24 + 8;
        return w;
    }
    float HeadLeft() const { return Pad() + (spec_.tileGlyph ? 44.f : spec_.headGlyph ? 32.f : 0.f); }
    // ---- Popover lanes (B06 rule, generalised 2026-11-09) ----------------------------------
    // A popover's head has a leading lane (32 DIP tile, 24 DIP glyph, or the 24 DIP glyph lane of the
    // request rows, inset 4) and a text column. Secondary lines — the remembering check, notices — put
    // their box / glyph centred in that lane and their text on the description's column, in the
    // description's role (14/18 regular); interactive text is brighter (70 %) than the description (60 %).
    struct Lane { float left = 0, width = 24, text = 32; };   // DIP from the content's left edge
    Lane SecondaryLane() const {
        if (spec_.tileGlyph) return {0, 32, 44};
        if (spec_.headGlyph) return {0, 24, 32};
        for (const auto& r : spec_.rows) if (r.glyph) return {4, 24, 36};
        return {0, 24, 32};
    }
    // The check sits in the lane when the popover has one, or when it is the origin check of a fieldless
    // popover (B01/B02 « Mémoriser pour ce site », B06). Form checks under fields keep their own row.
    bool LaneCheck() const { return spec_.checkInHeadLane || (spec_.originCheck && spec_.fields.empty()); }
    // With a 32 DIP tile and a regular (not large) title, the title's first line is centred on the tile.
    float TileTitleDrop() const { return spec_.tileGlyph && !spec_.largeTitle && !spec_.title.empty() ? (32 - TitleStyle().line) / 2 : 0; }
    DialogKit::Style TitleStyle() const { auto s = spec_.largeTitle ? DialogKit::Type::aboutTitle : DialogKit::Type::title; if (spec_.largeTitle) s.trackingEm = -.02f; return s; }
    static constexpr DialogKit::Style KeyCapStyle{14, 16, false, 0, true};
    float KeyCapWidth(const std::wstring& k) { return std::ceil(kit_.Width(k, KeyCapStyle)) + 12; }
    float BodyKeysWidth() { if (spec_.bodyKeys.empty()) return 0; float w = 4; for (size_t i = 0; i < spec_.bodyKeys.size(); ++i) w += (i ? 4 : 0) + KeyCapWidth(spec_.bodyKeys[i]); return w; }
    // The caps follow the body on its line when it is one line and they fit, else start the next line.
    bool BodyKeysInline(float tw) { return kit_.Height(spec_.body, DialogKit::Type::body, tw) <= DialogKit::Type::body.line && std::ceil(kit_.Width(spec_.body, DialogKit::Type::body)) + BodyKeysWidth() <= tw; }
    void DrawBodyKeys(float x, float y) {
        x += 4;
        for (size_t i = 0; i < spec_.bodyKeys.size(); ++i) {
            const float w = KeyCapWidth(spec_.bodyKeys[i]);
            kit_.Fill({x, y, x + w, y + 20}, 4, DialogKit::White(0x0F / 255.f));
            kit_.Text(spec_.bodyKeys[i], KeyCapStyle, x + 6, y + 2, w - 12 + 1, DialogKit::White(.5f));
            x += w + 4;
        }
    }
    float CheckInActionsWidth() { return 16 + 8 + std::ceil(kit_.Width(spec_.checkLabel, DialogKit::Type::detail)) + 8; }
    bool CheckOnActionsRow() {
        if (!spec_.checkInActions || spec_.checkLabel.empty() || spec_.buttons.empty() || spec_.stretchButtons || spec_.danger.shown) return false;
        float buttons = 0; for (size_t i = 0; i < spec_.buttons.size(); ++i) buttons += (i ? P::buttonGap : 0) + ButtonWidth(spec_.buttons[i]);
        return CheckInActionsWidth() + 8 + buttons <= ContentWidth();
    }
    float StatusButtonWidth() { return spec_.status.hasButton ? kit_.Width(spec_.status.button.label, DialogKit::Type::button) + 2 * P::buttonPad : 0; }
    float StatusTextWidth() { return ContentWidth() - 34 - 4 - (spec_.status.hasButton ? StatusButtonWidth() + 8 : 0); }
    float StatusTextHeight() {   // whole 18 DIP detail lines (the measured height can be a little short)
        if (spec_.status.detail.empty()) return 20;
        const float line = DialogKit::Type::detail.line, lines = std::max(1.f, std::ceil(kit_.Height(spec_.status.detail, DialogKit::Type::detail, StatusTextWidth()) / line - .2f));
        return 20 + lines * line;
    }
    float StatusHeight() { const float t = StatusTextHeight(); return t <= 20 + DialogKit::Type::detail.line ? 48.f : t + 12; }   // title + one detail line fit 48; more lines: padding 6
    float HeadHeight() {
        const float cw = ContentWidth();
        if (spec_.appTile) return std::max(48.f, 2 + DialogKit::Type::aboutTitle.line + 4 + kit_.Height(spec_.body, DialogKit::Type::tabular, cw - 48 - 16 - 24 - 16));
        if (!spec_.origin.empty()) return DialogKit::Type::origin.line;
        const float tw = HeadTextWidth(); float h = 0;
        if (!spec_.title.empty()) h += kit_.Height(spec_.title, TitleStyle(), tw);
        if (!spec_.subtitle.empty()) h += (h > 0 ? (spec_.largeTitle ? 8 : 4) : 0) + kit_.Height(spec_.subtitle, DialogKit::Type::detail, tw);
        if (!spec_.body.empty()) h += (h > 0 ? 4 : 0) + kit_.Height(spec_.body, DialogKit::Type::body, tw);
        if (!spec_.bodyKeys.empty() && !BodyKeysInline(tw)) h += 4 + 20;
        if (!spec_.footnote.empty() && !spec_.appTile) h += 8 + kit_.Height(spec_.footnote, DialogKit::Type::body, tw);
        if (spec_.tileGlyph) h = std::max(h + TileTitleDrop(), 32.f);
        if (spec_.closeButton) h = std::max(h, 24.f);
        return h;
    }
    float ButtonWidth(const Button& b) { return spec_.softButtons ? kit_.Width(b.label, DialogKit::Type::button) + 28 : kit_.ButtonWidth(b.label); }
    void LayoutActions(float top, float left, float right) {
        if (spec_.stretchButtons) {
            const float n = static_cast<float>(spec_.buttons.size()), w = (right - left - P::buttonGap * (n - 1)) / std::max(1.f, n);
            for (size_t i = 0; i < spec_.buttons.size(); ++i) boxes_.push_back({ButtonHit(i), {left + (w + P::buttonGap) * i, top, left + (w + P::buttonGap) * i + w, top + P::button}});
            return;
        }
        float x = right;
        for (size_t i = spec_.buttons.size(); i-- > 0;) { if (spec_.buttons[i].leading) continue; const float w = ButtonWidth(spec_.buttons[i]); boxes_.push_back({ButtonHit(i), {x - w, top, x, top + P::button}}); x -= w + P::buttonGap; }
        float l = left;
        for (size_t i = 0; i < spec_.buttons.size(); ++i) { if (!spec_.buttons[i].leading) continue; const float w = kit_.Width(spec_.buttons[i].label, DialogKit::Type::button) + 8; boxes_.push_back({ButtonHit(i), {l, top, l + w, top + P::button}}); l += w + P::buttonGap; }
    }
    void Build() {
        blocks_.clear(); boxes_.clear(); fieldTops_.assign(spec_.fields.size(), 0); messageHeight_ = messageFull_ = 0;
        const float left = Pad(), cw = ContentWidth(), right = left + cw;
        float y = Pad(); bool first = true; bool actionsLift = true;
        auto next = [&](B type, size_t index, float h, float gap = -1) { if (!first) y += gap >= 0 ? gap : Gap(); first = false; blocks_.push_back({type, index, {left, y, right, y + h}}); y += h; return blocks_.back().r; };
        if (spec_.demo) next(B::Badge, 0, P::badge);
        if (spec_.appTile || !spec_.origin.empty() || !spec_.title.empty() || !spec_.subtitle.empty() || !spec_.body.empty()) {
            const auto r = next(B::Head, 0, HeadHeight());
            if (spec_.closeButton) boxes_.push_back({CloseHit, {right - 24, r.top - (spec_.appTile ? 0 : 2), right, r.top - (spec_.appTile ? 0 : 2) + 24}});
            if (!spec_.origin.empty() && !spec_.message.empty()) {
                messageFull_ = kit_.Height(spec_.message, DialogKit::Type::message, cw);
                messageHeight_ = std::min(messageFull_, messageLines_ * DialogKit::Type::message.line);
                next(B::Message, 0, messageHeight_);
            }
        }
        for (size_t i = 0; i < spec_.notices.size(); ++i) {
            const auto& n = spec_.notices[i];
            const float h = n.boxed ? 16 + kit_.Height(n.text, DialogKit::Type::detail, cw - 20 - 22) : kit_.Height(n.text, DialogKit::Type::detail, cw - SecondaryLane().text);
            next(B::Notice, i, h, n.boxed ? 8.f : -1.f);
        }
        if (!spec_.rows.empty()) {
            const auto r = next(B::Rows, 0, 32 * spec_.rows.size() + 4 * (spec_.rows.size() - 1));
            for (size_t i = 0; i < spec_.rows.size(); ++i) if (spec_.rows[i].toggle >= 0) boxes_.push_back({RowHit(i), {left, r.top + 36.f * i, right, r.top + 36.f * i + 32}});
        }
        if (spec_.hasSelect) { const auto r = next(B::Select, 0, P::field); boxes_.push_back({SelectHit, {right - 184, r.top, right, r.bottom}}); }
        if (spec_.sources.shown) {
            // Paper FX2-0: #FFFFFF0D track, padding 2, gap 2, segments 28 high.
            const auto& sp = spec_.sources; const size_t n = std::max<size_t>(1, sp.categories.size());
            const auto seg = next(B::Segments, 0, 32);
            const float sw = (cw - 4 - 2.f * (n - 1)) / n;
            for (size_t i = 0; i < sp.categories.size(); ++i) { const float x = left + 2 + i * (sw + 2); boxes_.push_back({CategoryHit(i), {x, seg.top + 2, x + sw, seg.top + 30}}); }
            // Paper FX9-0: tiles 8 apart, padding 6, thumbnail radius 4 then a 14/18 caption 6 below.
            sourceColumns_ = cw >= 480 ? 3 : 2;
            const int cols = sourceColumns_; const float tw = (cw - 8.f * (cols - 1)) / cols;
            const float th = std::max(40.f, std::round((tw - 12) * .55f * thumbScale_));
            const float tile = 6 + th + 6 + 18 + 6;
            const auto vis = VisibleSources();
            const size_t rows = (vis.size() + cols - 1) / cols;
            const auto grid = next(B::Sources, 0, vis.empty() ? 96.f : rows * tile + 8.f * (rows - 1));
            for (size_t k = 0; k < vis.size(); ++k) {
                const float x = left + (k % cols) * (tw + 8), y = grid.top + (k / cols) * (tile + 8);
                boxes_.push_back({SourceHit(vis[k]), {x, y, x + tw, y + tile}});
            }
        }
        if (!spec_.checks.empty()) {
            float h = 12; std::vector<float> tops, heights;
            for (size_t i = 0; i < spec_.checks.size(); ++i) { if (i) h += 12; tops.push_back(h); const float rh = 20 + (spec_.checks[i].detail.empty() ? 0 : 2 + DialogKit::Type::detail.line); heights.push_back(rh); h += rh; }
            const auto r = next(B::Checks, 0, h + 12);
            for (size_t i = 0; i < spec_.checks.size(); ++i) boxes_.push_back({CheckHit(i), {left, r.top + tops[i], right, r.top + tops[i] + heights[i]}});
        }
        for (size_t i = 0; i < spec_.fields.size(); ++i) {
            const auto& f = spec_.fields[i];
            const std::wstring err = i == 0 && !fieldError_.empty() ? fieldError_ : f.error;
            const float label = f.label.empty() ? 0 : DialogKit::Type::detail.line + 6;
            float h = label + P::field;
            if (!err.empty()) h += (f.label.empty() ? 8 : 6) + kit_.Height(err, f.label.empty() ? DialogKit::Type::body : DialogKit::Type::detail, cw - 22);
            const auto r = next(B::Field, i, h, i > 0 && !f.label.empty() ? 8.f : -1.f);
            fieldTops_[i] = r.top + label;
            if (f.reveal) boxes_.push_back({RevealHit(i), {right - 28, fieldTops_[i] + 4, right - 4, fieldTops_[i] + 28}});
        }
        for (size_t i = 0; i < spec_.infos.size(); ++i) { next(B::Info, i, 32); actionsLift = false; }
        if (spec_.hasStatus) {
            // A8 contract: the row is 48 minimum and grows (padding 6) when the detail needs 2 lines;
            // icon, text block and button stay vertically centred.
            const float h = StatusHeight();
            const auto r = next(B::Status, 0, h);
            if (spec_.status.hasButton) { const float w = StatusButtonWidth(); const float by = r.top + (h - 32) / 2; boxes_.push_back({StatusHit, {right - 4 - w, by, right - 4, by + 32}}); }
        }
        if (!spec_.links.empty()) { const auto r = next(B::Links, 0, 32.f * spec_.links.size()); for (size_t i = 0; i < spec_.links.size(); ++i) boxes_.push_back({LinkHit(i), {left, r.top + 32 * i, right, r.top + 32 * (i + 1)}}); }
        if (spec_.appTile && !spec_.footnote.empty()) next(B::Footnote, 0, DialogKit::Type::body.line);
        if (!spec_.accountsTitle.empty() || !spec_.accounts.empty()) {
            float h = 6 + DialogKit::Type::detail.line + 4 + 2;
            h += spec_.accounts.size() * (48 + 2.f);
            if (spec_.accounts.empty()) h += 0;
            const bool manage = !spec_.manageLabel.empty();
            if (manage) h += 1 + 2 + 32;
            const auto r = next(B::Accounts, 0, h);
            float ry = r.top + 6 + DialogKit::Type::detail.line + 4 + 2;
            for (size_t i = 0; i < spec_.accounts.size(); ++i) { boxes_.push_back({AccountHit(i), {left, ry, right, ry + 48}}); ry += 50; }
            if (manage) boxes_.push_back({ManageHit, {left, ry + 3, right, ry + 35}});
            actionsLift = false;
        }
        const bool checkOnActions = CheckOnActionsRow();
        if (!spec_.checkLabel.empty() && !checkOnActions) {
            if (LaneCheck()) {
                const float lane = SecondaryLane().text; const float h = std::max(DialogKit::Type::detail.line, kit_.Height(spec_.checkLabel, DialogKit::Type::detail, cw - lane));
                const auto r = next(B::Check, 0, h);
                boxes_.push_back({SimpleCheckHit, {left, r.top, right, r.bottom}});
                actionsLift = false;
            } else {
            const bool origin = spec_.originCheck; const auto style = origin && spec_.fields.empty() ? DialogKit::Type::message : DialogKit::Type::detail;
            const float h = origin ? (spec_.fields.empty() ? 20.f : 18.f) : 32.f;
            const auto r = next(B::Check, 0, h);
            const float lead = origin && spec_.fields.empty() ? 4 + 24 + 8 : 16 + 8;
            const float w = std::min(cw, lead + kit_.Width(spec_.checkLabel, origin && spec_.fields.empty() ? DialogKit::Type::field : style) + 8);
            boxes_.push_back({SimpleCheckHit, {left, r.top, left + w, r.bottom}});
            actionsLift = false;
            }
        }
        if (!error_.empty()) next(B::Error, 0, kit_.Height(error_, DialogKit::Type::body, cw - 22));
        if (spec_.danger.shown) {
            // B07: the card holds its header, text and the actions (padding 12, gap 12).
            const float inner = cw - 24;
            const float text = kit_.Height(spec_.danger.text, DialogKit::Type::detail, inner);
            const float h = 12 + 40 + 12 + text + (spec_.buttons.empty() ? 0 : 12 + P::button) + 12;
            const auto r = next(B::Danger, 0, h, 12);
            if (!spec_.buttons.empty()) LayoutActions(r.bottom - 12 - P::button, r.left + 12, r.right - 12);
        } else if (!spec_.buttons.empty()) {
            const bool lift = actionsLift && !spec_.appTile && !Anchored() && !spec_.stretchButtons && !spec_.softButtons;
            const auto r = next(B::Actions, 0, (lift && !checkOnActions ? P::actionsTop : 0) + P::button);
            LayoutActions(r.bottom - P::button, left, right);
            if (checkOnActions) {   // EYG-0: the check sits left on the buttons' row (KK7-0 h 32, gap 8)
                const D2D1_RECT_F c{left, r.bottom - P::button, left + CheckInActionsWidth(), r.bottom};
                blocks_.push_back({B::Check, 0, c}); boxes_.push_back({SimpleCheckHit, c});
            }
        }
        height_ = y + Pad();
    }
    // Fit into the available height: the message viewport shrinks first (min 3 lines).
    void Fit(float available) {
        messageLines_ = P::messageLines; thumbScale_ = 1; Build();
        while (height_ > available && spec_.sources.shown && thumbScale_ > .5f) { thumbScale_ -= .1f; Build(); }
        while (height_ > available && messageLines_ > 3 && messageFull_ > messageLines_ * DialogKit::Type::message.line - 1) { --messageLines_; Build(); }
        scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, messageFull_ - messageHeight_));
    }
    RECT OwnerClient() const { RECT r{}; if (owner_) { GetClientRect(owner_, &r); MapWindowPoints(owner_, nullptr, reinterpret_cast<POINT*>(&r), 2); } return r; }
    RECT ScopeRect() const { return spec_.scope == Scope::Page ? area_ : OwnerClient(); }
    void Place(bool entering) {
        const float rise = entering && !reduced_ ? P::enterRise * (1 - Fade()) : 0;
        if (Anchored()) {
            RECT work{}; MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromRect(&spec_.anchor, MONITOR_DEFAULTTONEAREST), &mi)) work = mi.rcWork; else work = OwnerClient();
            width_ = spec_.width > 0 ? spec_.width : P::width;
            Fit(std::max(120.f, Dip(work.bottom - spec_.anchor.bottom) - 32));
            const int w = Px(width_), h = Px(height_);
            int x = spec_.anchorRight ? spec_.anchor.right - w : spec_.anchor.left - Px(Design::Address::slotInset);
            int y = spec_.anchor.bottom + Px(16) - Px(rise);
            x = std::max<int>(work.left + Px(6), std::min<int>(x, work.right - Px(6) - w));
            if (y + h > work.bottom - Px(6)) y = std::max<int>(work.top + Px(6), work.bottom - Px(6) - h);
            SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
        } else {
            const RECT scope = ScopeRect();
            const float availW = Dip(scope.right - scope.left), availH = Dip(scope.bottom - scope.top);
            width_ = std::clamp(availW - 2 * P::padding, P::minWidth, spec_.width > 0 ? spec_.width : P::width);
            Fit(std::max(120.f, availH - (spec_.scope == Scope::Page ? P::pageTop : 0) - 2 * P::padding));
            const int w = Px(width_), h = Px(height_);
            const int x = scope.left + (scope.right - scope.left - w) / 2;
            int y = spec_.scope == Scope::Page ? scope.top + Px(P::pageTop) : scope.top + (scope.bottom - scope.top - h) / 2;
            y += Px(rise);
            SetWindowPos(scrim_, HWND_TOP, scope.left, scope.top, std::max<int>(1, scope.right - scope.left), std::max<int>(1, scope.bottom - scope.top), SWP_NOACTIVATE);
            SetWindowPos(hwnd_, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);
        }
        chrome_.Resize(); PlaceEdits(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
    float Fade() const { return fade_.Value(Motion::Now()); }
    void ApplyAlpha() {
        const float a = Fade();
        if (!Anchored()) SetLayeredWindowAttributes(scrim_, 0, static_cast<BYTE>(std::lround(255 * P::scrim * a)), LWA_ALPHA);
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(std::lround(255 * a)), LWA_ALPHA);
    }
    // ---- fields ---------------------------------------------------------------------------------
    bool FieldDanger(size_t i) const { return (i == 0 && !fieldError_.empty()) || !spec_.fields[i].error.empty(); }
    COLORREF FieldColor(size_t i) const {
        if (FieldDanger(i) && spec_.fields[i].label.empty()) return DialogKit::OverCanvas(DialogKit::Danger(), P::dangerFill);
        if (!spec_.fields[i].label.empty()) return Design::NativeGray(Design::Surface::raised); // B04/B06 fields: raised, ring only
        const float h = motion_.Visual(FieldStop(i), Motion::Now()).hover;
        return DialogKit::OverCanvas(DialogKit::White(1), P::fieldFill + (P::fieldHover - P::fieldFill) * h);
    }
    void PlaceEdits() {
        if (!font_) return;
        for (size_t i = 0; i < edits_.size(); ++i) {
            HWND e = edits_[i].edit; if (!e) continue;
            HDC dc = GetDC(e); auto old = SelectObject(dc, font_); TEXTMETRICW m{}; GetTextMetricsW(dc, &m); SelectObject(dc, old); ReleaseDC(e, dc);
            const int lineH = m.tmHeight + m.tmExternalLeading;
            const int top = Px(fieldTops_[i]) + (Px(P::field) - lineH) / 2;
            const float inset = spec_.fields[i].reveal ? 28 + 6 - P::fieldPad + P::fieldPad : P::fieldPad;
            MoveWindow(e, Px(Pad() + P::fieldPad), top, Px(ContentWidth() - P::fieldPad - inset), lineH, TRUE);
            edits_[i].SetColor(FieldColor(i));
        }
    }
    void MakeFont() {
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-Px(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Inter Display");
        for (auto& e : edits_) if (e.edit) SendMessageW(e.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    void DestroyEdits() { for (auto& e : edits_) { if (e.password) e.Wipe(); e.Destroy(); } edits_.clear(); revealed_.clear(); }
    void CreateEdits() {
        DestroyEdits();
        edits_.resize(spec_.fields.size()); revealed_.assign(spec_.fields.size(), false);
        for (size_t i = 0; i < spec_.fields.size(); ++i) {
            const auto& f = spec_.fields[i]; auto& e = edits_[i];
            e.password = f.password; e.placeholder = f.placeholder;
            e.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (f.password ? ES_PASSWORD : 0), 0, 0, 1, 1, hwnd_, nullptr, instance_, nullptr);
            if (!e.edit) continue;
            SendMessageW(e.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
            SendMessageW(e.edit, EM_SETLIMITTEXT, static_cast<WPARAM>(std::max(1, f.maxLength)), 0);
            if (f.password) SendMessageW(e.edit, EM_SETPASSWORDCHAR, 0x2022, 0);
            SetWindowTextW(e.edit, f.value.c_str());
            SetWindowSubclass(e.edit, DialogKit::PlaceholderProc, 0x5048, reinterpret_cast<DWORD_PTR>(&e));
            SetWindowSubclass(e.edit, EditProc, 0x4D44, reinterpret_cast<DWORD_PTR>(this));
            e.SetColor(FieldColor(i));
        }
        MakeFont();
    }
    int EditIndex(HWND h) const { if (!h) return -1; for (size_t i = 0; i < edits_.size(); ++i) if (edits_[i].edit == h) return static_cast<int>(i); return -1; }
    void RefreshCaret() {
        const int i = EditIndex(GetFocus()); if (i < 0) return;
        const auto c = FieldDanger(static_cast<size_t>(i)) ? Design::DangerRgb() : Design::Accent::Get();
        auto& e = edits_[static_cast<size_t>(i)];
        e.caret.Apply(e.edit, e.color, NativeUI::AccentCaret::Mix(e.color, RGB(std::lround(c.r * 255), std::lround(c.g * 255), std::lround(c.b * 255)), Design::Accent::caretOpacity), std::max(1, Px(1)), Px(18));
    }
    std::vector<std::wstring> Values() const { std::vector<std::wstring> v; for (auto& e : edits_) v.push_back(e.Text()); return v; }
    void Revalidate() {
        if (!spec_.validate || !submitted_) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        const std::wstring err = spec_.validate(Values());
        if (err != fieldError_) { fieldError_ = err; Relayout(); }
        else InvalidateRect(hwnd_, nullptr, FALSE);
        RefreshCaret();
    }
    void Relayout() { if (!open_) return; Place(false); if (uia_.Listening()) uia_.ChildrenChanged(GroupKey); }
    void ToggleReveal(size_t i) {
        if (i >= edits_.size() || !edits_[i].edit) return;
        revealed_[i] = !revealed_[i];
        SendMessageW(edits_[i].edit, EM_SETPASSWORDCHAR, revealed_[i] ? 0 : 0x2022, 0); InvalidateRect(edits_[i].edit, nullptr, TRUE); InvalidateRect(hwnd_, nullptr, FALSE);
    }
    // ---- focus ----------------------------------------------------------------------------------
    std::vector<int> Order() const {
        std::vector<int> o;
        for (size_t i = 0; i < spec_.fields.size(); ++i) { o.push_back(FieldStop(i)); if (spec_.fields[i].reveal) o.push_back(RevealHit(i)); }
        if (spec_.hasSelect) o.push_back(SelectHit);
        for (size_t i = 0; i < spec_.rows.size(); ++i) if (spec_.rows[i].toggle == 1) o.push_back(RowHit(i));
        if (spec_.sources.shown) { if (!spec_.sources.categories.empty()) o.push_back(CategoryHit(static_cast<size_t>(spec_.sources.category))); if (const int s = SourceStop()) o.push_back(s); }
        for (size_t i = 0; i < spec_.checks.size(); ++i) if (!spec_.checks[i].disabled) o.push_back(CheckHit(i));
        if (spec_.hasStatus && spec_.status.hasButton && !spec_.status.button.disabled) o.push_back(StatusHit);
        for (size_t i = 0; i < spec_.links.size(); ++i) o.push_back(LinkHit(i));
        for (size_t i = 0; i < spec_.accounts.size(); ++i) if (!spec_.accounts[i].disabled) o.push_back(AccountHit(i));
        if (!spec_.manageLabel.empty()) o.push_back(ManageHit);
        if (!spec_.checkLabel.empty()) o.push_back(SimpleCheckHit);
        for (size_t i = 0; i < spec_.buttons.size(); ++i) if (spec_.buttons[i].leading && !ButtonDisabled(spec_.buttons[i])) o.push_back(ButtonHit(i));
        for (size_t i = 0; i < spec_.buttons.size(); ++i) if (!spec_.buttons[i].leading && !ButtonDisabled(spec_.buttons[i])) o.push_back(ButtonHit(i));
        if (spec_.closeButton) o.push_back(CloseHit);
        return o;
    }
    int CurrentStop() const { const int i = EditIndex(GetFocus()); return i >= 0 ? FieldStop(static_cast<size_t>(i)) : focus_; }
    void FocusStop(int stop) {
        if (stop >= 7000 && stop < 8000) {   // field stops (7000..7999); 8000+ are B08 picker ids
            focus_ = 0; const size_t i = static_cast<size_t>(stop - 7000);
            if (i < edits_.size() && edits_[i].edit) { SetFocus(edits_[i].edit); SendMessageW(edits_[i].edit, EM_SETSEL, 0, -1); }
        } else { focus_ = stop; SetFocus(hwnd_); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (uia_.Listening() && stop > 0 && (stop < 7000 || stop >= 8000)) uia_.Raise(stop, UIA_AutomationFocusChangedEventId);
    }
    void Cycle(bool back) {
        const auto o = Order(); if (o.empty()) return;
        const auto it = std::find(o.begin(), o.end(), CurrentStop());
        int i = it == o.end() ? (back ? -1 : 0) : static_cast<int>(it - o.begin()) + (back ? -1 : 1);
        i = (i + static_cast<int>(o.size())) % static_cast<int>(o.size());
        keyboard_ = true; FocusStop(o[static_cast<size_t>(i)]);
    }
    int InitialStop() const {
        for (size_t i = 0; i < spec_.fields.size(); ++i) if (spec_.fields[i].value.empty() || i + 1 == spec_.fields.size()) return FieldStop(i);
        for (size_t i = 0; i < spec_.accounts.size(); ++i) if (!spec_.accounts[i].disabled) return AccountHit(i);
        for (size_t i = 0; i < spec_.rows.size(); ++i) if (spec_.rows[i].toggle == 1) return RowHit(i);
        if (spec_.sources.shown) { if (const int s = SourceStop()) return s; if (!spec_.sources.categories.empty()) return CategoryHit(static_cast<size_t>(spec_.sources.category)); }
        for (size_t i = 0; i < spec_.buttons.size(); ++i) if (spec_.buttons[i].initialFocus && !ButtonDisabled(spec_.buttons[i])) return ButtonHit(i);
        for (size_t i = 0; i < spec_.buttons.size(); ++i) if (spec_.buttons[i].isCancel && !ButtonDisabled(spec_.buttons[i])) return ButtonHit(i);
        for (size_t i = 0; i < spec_.buttons.size(); ++i) if (!ButtonDisabled(spec_.buttons[i])) return ButtonHit(i);
        if (spec_.hasStatus && spec_.status.hasButton && !spec_.status.button.disabled) return StatusHit;   // A8: focus on the status action
        if (spec_.closeButton) return CloseHit;
        return 0;
    }
    // ---- actions --------------------------------------------------------------------------------
    Result Collect(int button) const {
        Result r; r.button = button; r.values = Values(); r.checked = spec_.checkValue;
        for (auto& c : spec_.checks) r.checks.push_back(c.value);
        r.select = spec_.accountsSelect ? spec_.accountSelected : spec_.sources.shown ? spec_.sources.selected : spec_.hasSelect ? spec_.select.index : -1;
        return r;
    }
    void Close(Result result) {
        if (!open_) return;
        if (choosing_) { if (hostMenu_) { if (hooks_.dismiss) hooks_.dismiss(); } else if (menu_) menu_->Cancel(); }
        open_ = false; busy_ = false; choosing_ = false;
        auto done = std::move(done_); done_ = nullptr;
        // Secrets never outlive the answer: the EDIT buffers are overwritten now.
        for (size_t i = 0; i < edits_.size(); ++i) if (spec_.fields[i].password) edits_[i].Wipe();
        if (uia_.Listening()) uia_.Raise(GroupKey, UIA_Window_WindowClosedEventId);
        closing_ = true; fade_.Set(0, Motion::Now(), Motion::Milliseconds(static_cast<int>(P::exitMs)), false, Motion::Curve::EaseOut);
        EnableWindow(hwnd_, FALSE);
        HWND back = restore_ && IsWindow(restore_) ? restore_ : owner_;
        if (owner_) SetForegroundWindow(owner_);
        if (back) SetFocus(back);
        if (hooks_.wake) hooks_.wake();
        if (done) done(result);
        for (auto& v : result.values) SecureZeroMemory(v.data(), v.size() * sizeof(wchar_t));
    }
    void FinishClosing() {
        closing_ = false; fade_.Snap(0);
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
        if (scrim_) ShowWindow(scrim_, SW_HIDE);
        if (hwnd_) EnableWindow(hwnd_, TRUE);
        DestroyEdits();
    }
    void Press(size_t index) {
        if (index >= spec_.buttons.size()) return;
        const Button b = spec_.buttons[index];
        if (ButtonDisabled(b)) return;
        if (b.isDefault && (spec_.validate || !spec_.fields.empty())) {
            submitted_ = true;
            if (spec_.validate) { fieldError_ = spec_.validate(Values()); if (!fieldError_.empty()) { Relayout(); FocusStop(FieldStop(0)); RefreshCaret(); return; } }
            if (Invalid()) { Relayout(); return; }
        }
        if (!b.closes) { if (spec_.onButton) spec_.onButton(b.id); return; }
        Close(Collect(b.id));
    }
    void Cancel() {
        if (busy_) return;
        for (auto& b : spec_.buttons) if (b.isCancel) { Close(Collect(b.id)); return; }
        Close(Collect(-1));
    }
    RECT ScreenOf(D2D1_RECT_F r) const { RECT s{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; MapWindowPoints(hwnd_, nullptr, reinterpret_cast<POINT*>(&s), 2); return s; }
    // Opens the list under the field (modal, like TrackPopupMenu). Échap / a click outside leave the
    // value as it was; the dialog may be closed or replaced meanwhile, then the answer is dropped.
    void ChooseSelect(bool keyboard) {
        if (!open_ || busy_ || choosing_ || !spec_.hasSelect || spec_.select.options.empty()) return;
        const auto options = spec_.select.options; const int current = spec_.select.index; const unsigned generation = generation_;
        const RECT anchor = ScreenOf(Find(SelectHit));
        keyboard_ = keyboard; focus_ = SelectHit; InvalidateRect(hwnd_, nullptr, FALSE);
        choosing_ = true; hostMenu_ = static_cast<bool>(hooks_.choose);
        int next = -1;
        if (hooks_.choose) next = hooks_.choose(options, current, anchor, keyboard);
        else {
            if (!menu_) { menu_ = std::make_unique<ContextMenu::Menu>(); menu_->Init(instance_, hwnd_, root_); }
            next = ChooseFromMenu(*menu_, options, current, anchor, keyboard, reduced_);
        }
        choosing_ = false;
        if (!open_ || generation != generation_ || !hwnd_ || !IsWindowVisible(hwnd_)) return;
        SetForegroundWindow(hwnd_); keyboard_ = keyboard; FocusStop(SelectHit);
        SetSelect(next);
    }
    void SetSelect(int index) {
        if (index < 0 || index >= static_cast<int>(spec_.select.options.size()) || index == spec_.select.index) return;
        spec_.select.index = index;
        if (spec_.select.details) { const auto d = spec_.select.details(index); for (size_t i = 0; i < d.size() && i < spec_.checks.size(); ++i) spec_.checks[i].detail = d[i]; }
        Relayout();
    }
    void Activate(int hit, bool keyboard) {
        keyboard_ = keyboard;
        if (!open_ || !Enabled(hit)) return;
        if (hit >= 1000 && hit < 2000) Press(static_cast<size_t>(hit - 1000));
        else if (hit >= 2000 && hit < 2999) { auto& c = spec_.checks[static_cast<size_t>(hit - 2000)]; c.value = !c.value; focus_ = hit; InvalidateRect(hwnd_, nullptr, FALSE); if (uia_.Listening()) uia_.Raise(hit, UIA_ToggleToggleStatePropertyId); }
        else if (hit == SimpleCheckHit) { spec_.checkValue = !spec_.checkValue; focus_ = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        else if (hit == SelectHit) ChooseSelect(keyboard);
        else if (hit == StatusHit) { const Button b = spec_.status.button; if (b.closes) Close(Collect(b.id)); else if (spec_.onButton) spec_.onButton(b.id); }
        else if (hit >= 4000 && hit < 5000) Close(Collect(spec_.links[static_cast<size_t>(hit - 4000)].id));
        else if (hit >= 6200 && hit < 6900 && spec_.accountsSelect) {
            // Entrée on the selected row sends it (as the default button would); a click, ↑/↓ or Espace only select.
            const size_t i = static_cast<size_t>(hit - 6200);
            if (keyboard && static_cast<int>(i) == spec_.accountSelected && GetKeyState(VK_SPACE) >= 0) { for (size_t b = 0; b < spec_.buttons.size(); ++b) if (spec_.buttons[b].id == AcceptButton && !spec_.buttons[b].disabled) { Press(b); return; } }
            SelectAccount(i, keyboard);
        }
        else if (hit >= 6200 && hit < 6900) { if (spec_.accountsNavigate) { if (spec_.onButton) spec_.onButton(hit); } else { Result r = Collect(AcceptButton); r.select = hit - 6200; Close(r); } }
        else if (hit == ManageHit) Close(Collect(spec_.manageId));
        else if (hit >= 6900 && hit < 7000) ToggleReveal(static_cast<size_t>(hit - 6900));
        else if (hit == CloseHit) Cancel();
        else if (IsRow(hit)) { if (spec_.onButton) spec_.onButton(hit); }
        else if (IsCategory(hit)) SetCategory(hit - 8000, keyboard);
        else if (IsSource(hit)) {
            const size_t i = static_cast<size_t>(hit - 8100);
            // Entrée / Espace on the selected source shares it (as the default button would); a click only selects.
            if (keyboard && static_cast<int>(i) == spec_.sources.selected) { for (size_t b = 0; b < spec_.buttons.size(); ++b) if (spec_.buttons[b].id == spec_.sources.acceptId) { Press(b); return; } }
            SelectSource(i, keyboard);
        }
    }
    bool Key(WPARAM key) {
        if (!open_) return false;
        const bool inEdit = EditIndex(GetFocus()) >= 0;
        switch (key) {
        case VK_ESCAPE: Cancel(); return true;
        case VK_TAB: Cycle(GetKeyState(VK_SHIFT) < 0); return true;
        case VK_RETURN: {
            keyboard_ = true;
            if (!inEdit && focus_ == SelectHit) { Activate(SelectHit, true); return true; }   // opens the list
            if (!inEdit && focus_ && !(focus_ >= 2000 && focus_ <= 2999)) { Activate(focus_, true); return true; }
            for (size_t i = 0; i < spec_.buttons.size(); ++i) if (spec_.buttons[i].isDefault && spec_.buttons[i].role != Role::Danger) { Press(i); return true; }
            return true;
        }
        case VK_SPACE: if (!inEdit && focus_) { Activate(focus_, true); return true; } return false;
        case VK_DOWN: case VK_UP: {
            const int step = key == VK_DOWN ? 1 : -1;
            if (!inEdit && IsSource(focus_)) return MoveSource(step * sourceColumns_);
            if (!inEdit && focus_ == SelectHit) { Activate(SelectHit, true); return true; }   // ↑/↓ open the list, never change silently
            if (!inEdit && ((focus_ >= 6200 && focus_ < 6900) || focus_ == ManageHit)) {   // B03: ↓ then Entrée
                std::vector<int> list; for (size_t i = 0; i < spec_.accounts.size(); ++i) if (!spec_.accounts[i].disabled) list.push_back(AccountHit(i)); if (!spec_.manageLabel.empty()) list.push_back(ManageHit);
                auto it = std::find(list.begin(), list.end(), focus_); int i = static_cast<int>(it - list.begin()) + step; i = std::clamp(i, 0, static_cast<int>(list.size()) - 1);
                if (spec_.accountsSelect && list[static_cast<size_t>(i)] != ManageHit) { SelectAccount(static_cast<size_t>(list[static_cast<size_t>(i)] - 6200), true); return true; }   // radio group: arrows move the selection, never send
                keyboard_ = true; FocusStop(list[static_cast<size_t>(i)]); return true;
            }
            if (!inEdit) { Scroll(step * DialogKit::Type::message.line); return true; }
            return false;
        }
        case VK_F4: if (!inEdit && focus_ == SelectHit) { Activate(SelectHit, true); return true; } return false;
        case VK_NEXT: case VK_PRIOR: if (!inEdit) { Scroll((key == VK_NEXT ? 1 : -1) * std::max(DialogKit::Type::message.line, messageHeight_ - DialogKit::Type::message.line)); return true; } return false;
        case VK_LEFT: case VK_RIGHT:
            if (!inEdit && IsCategory(focus_)) { SetCategory(focus_ - 8000 + (key == VK_LEFT ? -1 : 1), true); return true; }
            if (!inEdit && IsSource(focus_)) return MoveSource(key == VK_LEFT ? -1 : 1);
            if (!inEdit && focus_ >= 1000 && focus_ < 2000) { Cycle(key == VK_LEFT); return true; } return false;
        case VK_HOME: case VK_END: if (!inEdit && IsSource(focus_)) return MoveSource(key == VK_HOME ? -1000 : 1000); return false;
        }
        return false;
    }
    // Alt+↓ / Alt+↑ on the select open its list (combo box convention).
    bool AltKey(WPARAM key) {
        if (!open_ || (key != VK_DOWN && key != VK_UP) || focus_ != SelectHit || EditIndex(GetFocus()) >= 0) return false;
        Activate(SelectHit, true); return true;
    }
    void Scroll(float dy) { const float max = std::max(0.f, messageFull_ - messageHeight_); const float s = std::clamp(scroll_ + dy, 0.f, max); if (s != scroll_) { scroll_ = s; InvalidateRect(hwnd_, nullptr, FALSE); } }
    void Interact() { motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, Motion::Now()); for (size_t i = 0; i < edits_.size(); ++i) edits_[i].SetColor(FieldColor(i)); InvalidateRect(hwnd_, nullptr, FALSE); if (hooks_.wake) hooks_.wake(); }
    // ---- window procedures ----------------------------------------------------------------------
    static LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<Dialog*>(data);
        switch (msg) {
        case WM_NCDESTROY: RemoveWindowSubclass(h, EditProc, 0x4D44); break;
        case WM_GETDLGCODE: return DefSubclassProc(h, msg, w, l) | DLGC_WANTALLKEYS;
        case WM_KEYDOWN: if ((w == VK_ESCAPE || w == VK_TAB || w == VK_RETURN) && self->Key(w)) return 0; break;
        case WM_CHAR: if (w == L'\t' || w == L'\r' || w == 27) return 0; break;
        case WM_SETFOCUS: { const LRESULT r = DefSubclassProc(h, msg, w, l); self->focus_ = 0; self->RefreshCaret(); InvalidateRect(self->hwnd_, nullptr, FALSE); return r; }
        case WM_KILLFOCUS: InvalidateRect(self->hwnd_, nullptr, FALSE); break;
        case WM_LBUTTONDOWN: self->keyboard_ = false; break;
        case WM_MOUSEMOVE: { const int i = self->EditIndex(h); if (i >= 0 && self->hover_ != FieldStop(static_cast<size_t>(i))) { self->hover_ = FieldStop(static_cast<size_t>(i)); TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); self->Interact(); } break; }
        case WM_MOUSELEAVE: self->hover_ = 0; self->Interact(); break;
        }
        return DefSubclassProc(h, msg, w, l);
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); self->hwnd_ = h; }
        return self ? self->Handle(h, msg, w, l) : DefWindowProcW(h, msg, w, l);
    }
    static LRESULT CALLBACK ScrimProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); FillRect(dc, &ps.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))); EndPaint(h, &ps); return 0; }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATEANDEAT;
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            if (self && self->open_) { SetForegroundWindow(self->hwnd_); if (!self->focus_ && self->EditIndex(GetFocus()) < 0) self->FocusStop(self->InitialStop()); }
            return 0;
        case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    LRESULT Handle(HWND h, UINT msg, WPARAM w, LPARAM l) {
        const float scale = 96.f / Dpi();
        switch (msg) {
        case WM_GETOBJECT: { LRESULT result; if (uia_.OnGetObject(h, w, l, result)) return result; break; }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); Paint(); EndPaint(h, &ps); return 0; }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(w); const int i = EditIndex(reinterpret_cast<HWND>(l));
            if (i < 0) break;
            SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, edits_[static_cast<size_t>(i)].color); SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(edits_[static_cast<size_t>(i)].brush);
        }
        case WM_COMMAND: if (HIWORD(w) == EN_CHANGE) Revalidate(); if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_MOUSEACTIVATE: return MA_ACTIVATE;
        case WM_ACTIVATE:
            // Anchored popovers dismiss when the user goes elsewhere (not for our own menu).
            if (LOWORD(w) == WA_INACTIVE && open_ && Anchored() && !choosing_) PostMessageW(h, WM_APP + 2, 0, 0);
            return 0;
        case WM_APP + 2: if (open_ && Anchored() && !choosing_ && GetForegroundWindow() != hwnd_) Close(Collect(-1)); return 0;
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); if (!Enabled(hit)) hit = 0; if (hit != hover_) { hover_ = hit; Interact(); } return 0; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); return 0;
        case WM_LBUTTONDOWN: { keyboard_ = false; int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); if (!Enabled(hit)) hit = 0; pressed_ = hit; if (hit) SetCapture(h); Interact(); return 0; }
        case WM_LBUTTONUP: { const int hit = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale); const int was = pressed_; pressed_ = 0; if (GetCapture() == h) ReleaseCapture(); Interact(); if (was && was == hit) Activate(hit, false); return 0; }
        case WM_MOUSEWHEEL: Scroll(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA * 3 * DialogKit::Type::message.line); return 0;
        case WM_KEYDOWN: if (Key(w)) return 0; break;
        case WM_SYSKEYDOWN: if (AltKey(w)) return 0; break;
        case WM_SETFOCUS: InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_DPICHANGED: if (open_) { MakeFont(); Place(false); } return 0;
        case WM_APP + 1: Activate(static_cast<int>(w), true); return 0;
        case WM_APP + 3: { const int k = static_cast<int>(w); if (IsCategory(k)) SetCategory(k - 8000, true); else if (IsSource(k)) SelectSource(static_cast<size_t>(k - 8100), true); return 0; }   // UIA Select: never shares
        case WM_DESTROY: if (open_) { open_ = false; auto done = std::move(done_); done_ = nullptr; if (done) done(Result{}); } uia_.Detach(); return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }
    // ---- paint ----------------------------------------------------------------------------------
    static Kit::Tone ToneOf(Role r) { switch (r) { case Role::Primary: return Kit::Tone::Primary; case Role::Danger: return Kit::Tone::Danger; case Role::Quiet: return Kit::Tone::Quiet; default: return Kit::Tone::Secondary; } }
    float SpinTurn(Motion::Time now) const { return static_cast<float>(std::fmod(std::chrono::duration<double>(now - spinStart_).count(), 1.0)); }
    bool Focused(int hit) const { return keyboard_ && focus_ == hit && GetFocus() == hwnd_; }
    D2D1_COLOR_F RowFill(int hit, Motion::Time now) const { const auto v = motion_.Visual(hit, now); return DialogKit::Mix(DialogKit::Gray(Design::Surface::hover, v.hover), DialogKit::Gray(Design::Surface::pressed), v.press); }
    void PaintHead(const Block& b, Motion::Time now) {
        const float left = b.r.left, cw = b.r.right - b.r.left; float y = b.r.top;
        if (spec_.appTile) {
            kit_.Fill({left, y, left + 48, y + 48}, 10, DialogKit::Gray(Design::Surface::raised));
            kit_.DrawGlyph(DialogKit::Glyph::flower, {left, y, left + 48, y + 48}, 28, DialogKit::Rgb(0xDFDFDF), 936, 288);
            const float tx = left + 48 + 16, tw = cw - 48 - 16 - 24 - 16;
            kit_.Text(spec_.title, DialogKit::Type::aboutTitle, tx, y + 2, tw, DialogKit::White(1));
            kit_.Text(spec_.body, DialogKit::Type::tabular, tx, y + 2 + 24 + 4, tw, DialogKit::White(P::body), true);
        } else if (!spec_.origin.empty()) {
            const D2D1_RECT_F icon{left, y + 2, left + 16, y + 18};
            if (!pc_->DrawSiteImage(spec_.favicon, icon)) pc_->Icon(L"favicon", icon, 16, P::body);
            kit_.Text(spec_.origin, DialogKit::Type::origin, left + 24, y, cw - 24 - (spec_.closeButton ? 32 : 0), DialogKit::White(P::origin));
        } else {
            if (spec_.tileGlyph) { kit_.Fill({left, y, left + 32, y + 32}, Design::Radius::control, DialogKit::Gray(Design::Surface::raised)); kit_.DrawGlyph(spec_.tileGlyph, {left, y, left + 32, y + 32}, 16, DialogKit::White(.8f)); }
            else if (spec_.headGlyph) kit_.DrawGlyph(spec_.headGlyph, {left, y + 2, left + 24, y + 18}, 16, DialogKit::White(P::body));
            const float tx = HeadLeft(), tw = HeadTextWidth(); y += TileTitleDrop();
            if (!spec_.title.empty()) { kit_.Text(spec_.title, TitleStyle(), tx, y, tw, DialogKit::White(1), true); y += kit_.Height(spec_.title, TitleStyle(), tw); }
            if (!spec_.subtitle.empty()) { y += spec_.largeTitle ? 8 : 4; kit_.Text(spec_.subtitle, DialogKit::Type::detail, tx, y, tw, DialogKit::White(P::origin), true); y += kit_.Height(spec_.subtitle, DialogKit::Type::detail, tw); }
            if (!spec_.body.empty()) { y += 4; kit_.Text(spec_.body, DialogKit::Type::body, tx, y, tw, DialogKit::White(P::body), true);
                if (!spec_.bodyKeys.empty()) { if (BodyKeysInline(tw)) DrawBodyKeys(tx + std::ceil(kit_.Width(spec_.body, DialogKit::Type::body)), y); else DrawBodyKeys(tx - 4, y + kit_.Height(spec_.body, DialogKit::Type::body, tw) + 4); }
                y += kit_.Height(spec_.body, DialogKit::Type::body, tw); if (!spec_.bodyKeys.empty() && !BodyKeysInline(tw)) y += 4 + 20; }
            if (!spec_.footnote.empty()) { y += 8; kit_.Text(spec_.footnote, DialogKit::Type::body, tx, y, tw, DialogKit::White(P::muted), true); }
        }
        if (spec_.closeButton) {
            const auto r = Find(CloseHit); const auto v = motion_.Visual(CloseHit, now);
            if (v.hover > 0 || v.press > 0) kit_.Fill(r, Design::Radius::inset, RowFill(CloseHit, now));
            kit_.DrawGlyph(DialogKit::Glyph::close, r, 14, DialogKit::White((spec_.appTile ? P::body : 0x82 / 255.f) + (1 - P::body) * v.hover));
            if (Focused(CloseHit)) kit_.Ring(r, Design::Radius::inset, DialogKit::White(P::ring));
        }
    }
    void PaintActions(Motion::Time now) {
        for (size_t i = 0; i < spec_.buttons.size(); ++i) {
            const auto& b = spec_.buttons[i]; const auto v = motion_.Visual(ButtonHit(i), now); const auto r = Find(ButtonHit(i));
            if (b.leading) {   // text action (B04 « Jamais pour ce site »): 14 Medium 70 %, white on hover
                if (Focused(ButtonHit(i))) kit_.Ring(r, P::buttonRadius, DialogKit::White(P::ring));
                kit_.Text(b.label, DialogKit::Type::button, r.left + 4, r.top + 7, r.right - r.left, b.role == Role::Danger ? DialogKit::Mix(DialogKit::Danger(), DialogKit::Rgb(P::dangerLift), v.hover) : DialogKit::White(P::quietText + (1 - P::quietText) * v.hover));
                continue;
            }
            kit_.Button(r, b.label, ToneOf(b.role), v.hover, v.press, Focused(ButtonHit(i)), ButtonDisabled(b));
        }
        if (busy_ && !spec_.buttons.empty()) {
            float leftmost = 1e9f, top = 0; for (size_t i = 0; i < spec_.buttons.size(); ++i) { const auto r = Find(ButtonHit(i)); leftmost = std::min(leftmost, r.left); top = r.top; }
            const float left = Pad();
            kit_.Spinner({left, top + 8, left + 16, top + 24}, reduced_ ? .125f : SpinTurn(now));
            kit_.Text(busyLabel_, DialogKit::Type::body, left + 24, top + 6, leftmost - left - 24 - 8, DialogKit::White(P::body));
        }
    }
    // Fictitious thumbnails (B08 demo), drawn locally: no capture of the desktop or of any window.
    // 0 document (Paper FXB-0), 1 dark app (FXH-0), 2 desktop, 3 spreadsheet, 4 browser, 5 call.
    void PaintThumbnail(int art, D2D1_RECT_F t) {
        auto hex = [](unsigned v) { return DialogKit::Rgb(v); };
        const float w = t.right - t.left, h = t.bottom - t.top;
        auto bar = [&](float x, float y, float bw, float bh, unsigned c) { if (y + bh > h - 2) return; kit_.Fill({t.left + x, t.top + y, t.left + x + bw, t.top + y + bh}, 2, hex(c)); };
        switch (art % 6) {
        case 0: kit_.Fill(t, 4, hex(0xE9ECEF)); bar(8, 8, std::round(w * .7f) - 16, 8, 0xC5CBD3); bar(8, 22, std::round(w * .9f) - 16, 6, 0xD6DBE1); bar(8, 34, std::round(w * .8f) - 16, 6, 0xD6DBE1); bar(8, 46, std::round(w * .6f) - 16, 6, 0xD6DBE1); break;
        case 1: kit_.Fill(t, 4, hex(0x2B2D31)); bar(8, 8, std::round(w * .5f) - 16, 8, 0x4A4D55); bar(8, 22, std::round(w * .85f) - 16, 6, 0x3A3D44); break;
        case 2: kit_.Fill(t, 4, hex(0x1F3A5F)); kit_.Fill({t.left, t.bottom - 10, t.right, t.bottom}, 0, hex(0x14243B)); bar(8, 8, 14, 14, 0x3D5A80); bar(8, 28, 14, 14, 0x3D5A80); bar(w / 2 - 20, h * .25f, 40, h * .35f, 0x2E4B73); break;
        case 3: { kit_.Fill(t, 4, hex(0xF4F6F8)); kit_.Fill({t.left, t.top, t.right, t.top + 10}, 0, hex(0x2F6B4F)); for (float y = 16; y < h - 4; y += 9) kit_.Fill({t.left + 4, t.top + y, t.right - 4, t.top + y + 1}, 0, hex(0xD6DBE1)); for (float x = w / 4; x < w - 4; x += w / 4) kit_.Fill({t.left + x, t.top + 12, t.left + x + 1, t.bottom - 4}, 0, hex(0xD6DBE1)); break; }
        case 4: kit_.Fill(t, 4, hex(0x171717)); kit_.Fill({t.left, t.top, t.right, t.top + 12}, 0, hex(0x292929)); bar(6, 3, 30, 6, 0x3E3E3E); bar(8, 20, std::round(w * .55f), 8, 0x3E3E3E); bar(8, 34, w - 16, std::max(8.f, h - 42), 0x232323); break;
        default: { kit_.Fill(t, 4, hex(0x202124)); const float cwh = (w - 18) / 2, chh = (h - 18) / 2; bar(6, 6, cwh, chh, 0x3C4043); bar(12 + cwh, 6, cwh, chh, 0x35363A); bar(6, 12 + chh, cwh, chh, 0x35363A); bar(12 + cwh, 12 + chh, cwh, chh, 0x3C4043); break; }
        }
    }
    void Paint() {
        if (pc_ == &chrome_ && !chrome_.RenderTarget()) chrome_.Resize();
        auto* t = pc_->RenderTarget(); if (!t || !pc_->SolidBrush()) return;
        const auto now = Motion::Now(); const float c = Design::Surface::canvas;
        t->BeginDraw(); t->Clear(D2D1::ColorF(c, c, c, 1));
        for (const auto& b : blocks_) {
            const float left = b.r.left, right = b.r.right, cw = right - left;
            switch (b.type) {
            case B::Badge: kit_.Badge(left, b.r.top, L"Démonstration"); break;
            case B::Head: PaintHead(b, now); break;
            case B::Message: {
                const D2D1_RECT_F clip = b.r;
                kit_.Text(spec_.message, DialogKit::Type::message, left, b.r.top - scroll_, cw, DialogKit::White(1), true, &clip);
                if (messageFull_ > messageHeight_) {   // discreet scroll position (not in Paper: says more text exists)
                    const float track = messageHeight_, thumb = std::max(16.f, track * messageHeight_ / messageFull_);
                    const float at = (track - thumb) * scroll_ / std::max(1.f, messageFull_ - messageHeight_);
                    kit_.Fill({right + 6, b.r.top + at, right + 8, b.r.top + at + thumb}, 1, DialogKit::White(.2f));
                }
                break;
            }
            case B::Notice: {
                const auto& n = spec_.notices[b.index];
                if (n.boxed) {
                    kit_.Fill(b.r, P::fieldRadius, DialogKit::White(P::infoFill));
                    if (n.glyph) kit_.DrawGlyph(n.glyph, {left + 10, b.r.top + 8 + 2, left + 24, b.r.top + 8 + 16}, 14, DialogKit::White(P::body));
                    kit_.Text(n.text, DialogKit::Type::detail, left + 32, b.r.top + 8, cw - 42, DialogKit::White(P::secondaryText), true);
                } else {
                    const auto ln = SecondaryLane();
                    kit_.DrawGlyph(n.glyph ? n.glyph : DialogKit::Glyph::info, {left + ln.left, b.r.top + 2, left + ln.left + ln.width, b.r.top + 16}, 14, DialogKit::White(P::muted));
                    kit_.Text(n.text, DialogKit::Type::detail, left + ln.text, b.r.top, cw - ln.text, DialogKit::White(P::origin), true);
                }
                break;
            }
            case B::Rows: {
                float ry = b.r.top;
                for (size_t i = 0; i < spec_.rows.size(); ++i) {
                    const auto& row = spec_.rows[i];
                    if (row.toggle >= 0) {   // EAC-0 row: hover / focus on the whole row, 28 x 16 switch at its end
                        const D2D1_RECT_F rr{left, ry, right, ry + 32}; const auto v = motion_.Visual(RowHit(i), now);
                        if (row.toggle == 1 && (v.hover > 0 || v.press > 0)) kit_.Fill(rr, P::fieldRadius, RowFill(RowHit(i), now));
                        const D2D1_RECT_F track{right - 4 - 28, ry + 8, right - 4, ry + 24};
                        kit_.Fill(track, 8, row.toggle == 1 ? DialogKit::White(0xB3 / 255.f) : DialogKit::White(.12f));
                        const float kx = row.toggle == 1 ? track.right - 2 - 12 : track.left + 2;
                        kit_.Fill({kx, track.top + 2, kx + 12, track.top + 14}, 6, row.toggle == 1 ? DialogKit::Gray(Design::Surface::canvas) : DialogKit::White(.5f));
                        if (Focused(RowHit(i))) kit_.Ring(rr, P::fieldRadius, DialogKit::White(P::ring));
                    }
                    if (row.glyph) kit_.DrawGlyph(row.glyph, {left + 4, ry + 4, left + 28, ry + 28}, 16, DialogKit::White(row.toggle >= 0 ? 0xB3 / 255.f : P::body));
                    const float tw = row.trailing.empty() ? 0 : kit_.Width(row.trailing, DialogKit::Type::detail);
                    const float tail = (tw > 0 ? tw + 16 : 0) + (row.toggle >= 0 ? 28 + 12 : 0);
                    kit_.Text(row.label, DialogKit::Type::field, left + 36, ry + 6, cw - 36 - tail, DialogKit::White(row.toggle == 0 ? P::muted : 1.f));
                    if (tw > 0) kit_.Text(row.trailing, DialogKit::Type::detail, right - 8 - tw - (row.toggle >= 0 ? 28 + 8 : 0), ry + 7, tw + 1, row.trailing == L"En cours" ? DialogKit::AccentColor() : DialogKit::White(P::origin));
                    ry += 36;
                }
                break;
            }
            case B::Segments: {
                const auto& sp = spec_.sources;
                kit_.Fill(b.r, 6, DialogKit::White(0x0D / 255.f));
                for (size_t i = 0; i < sp.categories.size(); ++i) {
                    const auto r = Find(CategoryHit(i)); const auto v = motion_.Visual(CategoryHit(i), now); const bool on = static_cast<int>(i) == sp.category;
                    if (on) kit_.Fill(r, 4, DialogKit::Gray(Design::Surface::pressed));
                    else if (v.hover > 0) kit_.Fill(r, 4, DialogKit::White(.04f * v.hover));
                    const float tw = kit_.Width(sp.categories[i], DialogKit::Type::button);
                    kit_.Text(sp.categories[i], DialogKit::Type::button, (r.left + r.right - tw) / 2, r.top + 5, tw + 2, DialogKit::White(on ? 1.f : 0x99 / 255.f));
                    if (Focused(CategoryHit(i))) kit_.Ring(r, 4, DialogKit::White(P::ring));
                }
                break;
            }
            case B::Sources: {
                const auto& sp = spec_.sources; const auto vis = VisibleSources();
                if (vis.empty()) {
                    kit_.Fill(b.r, 6, DialogKit::White(.03f));
                    const float tw = std::min(cw - 32, kit_.Width(sp.empty, DialogKit::Type::detail));
                    kit_.Text(sp.empty, DialogKit::Type::detail, left + (cw - tw) / 2, b.r.top + (b.r.bottom - b.r.top - 18) / 2, tw + 2, DialogKit::White(P::muted));
                    break;
                }
                for (size_t i : vis) {
                    const auto r = Find(SourceHit(i)); const auto v = motion_.Visual(SourceHit(i), now); const bool on = static_cast<int>(i) == sp.selected;
                    if (on) { kit_.Fill(r, 6, DialogKit::AccentColor(P::badgeTint)); kit_.Ring(r, 6, DialogKit::AccentColor()); }
                    else if (v.hover > 0 || v.press > 0) kit_.Fill(r, 6, DialogKit::White(.05f * std::max(v.hover, v.press)));
                    const D2D1_RECT_F thumb{r.left + 6, r.top + 6, r.right - 6, r.bottom - 6 - 18 - 6};
                    PaintThumbnail(sp.items[i].art, thumb);
                    kit_.Text(sp.items[i].title, DialogKit::Type::detail, r.left + 6, thumb.bottom + 6, r.right - r.left - 12, DialogKit::White(on ? 1.f : 0xB3 / 255.f), false, &r);
                    if (Focused(SourceHit(i))) kit_.Ring({r.left - 2, r.top - 2, r.right + 2, r.bottom + 2}, 8, DialogKit::White(P::ring));
                }
                break;
            }
            case B::Select: {
                const auto r = Find(SelectHit); const auto v = motion_.Visual(SelectHit, now);
                kit_.Text(spec_.select.label, DialogKit::Type::body, left, r.top + 6, r.left - left - 8, DialogKit::White(P::body));
                kit_.Fill(r, P::fieldRadius, DialogKit::Mix(DialogKit::Mix(DialogKit::Gray(Design::Surface::raised), DialogKit::Gray(Design::Surface::hover), v.hover), DialogKit::Gray(Design::Surface::pressed), v.press));
                const std::wstring value = spec_.select.index >= 0 && spec_.select.index < static_cast<int>(spec_.select.options.size()) ? spec_.select.options[static_cast<size_t>(spec_.select.index)] : L"";
                kit_.Text(value, DialogKit::Type::body, r.left + 10, r.top + 6, r.right - r.left - 10 - 8 - 14 - 8, DialogKit::White(busy_ ? P::disabled : 1));
                kit_.DrawGlyph(DialogKit::Glyph::caretDown, {r.right - 8 - 14, r.top + 9, r.right - 8, r.top + 23}, 14, DialogKit::White(P::muted));
                if (Focused(SelectHit)) kit_.Ring(r, P::fieldRadius, DialogKit::White(P::ring));
                break;
            }
            case B::Checks: {
                const float px = kit_.Pixel();
                kit_.Fill({left, b.r.top, right, b.r.top + px}, 0, DialogKit::White(P::listRule));
                kit_.Fill({left, b.r.bottom - px, right, b.r.bottom}, 0, DialogKit::White(P::listRule));
                for (size_t i = 0; i < spec_.checks.size(); ++i) {
                    const auto& ck = spec_.checks[i]; const auto r = Find(CheckHit(i)); const auto v = motion_.Visual(CheckHit(i), now);
                    ID2D1Layer* layer = nullptr;
                    if ((ck.disabled || busy_) && SUCCEEDED(t->CreateLayer(nullptr, &layer))) t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), P::disabled), layer);
                    kit_.CheckBox({left, r.top + 2, left + 16, r.top + 18}, ck.value, v.hover);
                    kit_.Text(ck.label, DialogKit::Type::body, left + 26, r.top, cw - 26, DialogKit::White(ck.value || v.hover > 0 ? 1 : P::body));
                    if (!ck.detail.empty()) kit_.Text(ck.detail, DialogKit::Type::detail, left + 26, r.top + 22, cw - 26, DialogKit::White(P::muted));
                    if (Focused(CheckHit(i))) kit_.Ring({r.left - 6, r.top - 4, r.right + 6, r.bottom + 4}, P::fieldRadius, DialogKit::White(P::ring));
                    if (layer) { t->PopLayer(); layer->Release(); }
                }
                break;
            }
            case B::Field: {
                const size_t i = b.index; const auto& f = spec_.fields[i]; const float top = fieldTops_[i];
                const bool labeled = !f.label.empty();
                if (labeled) kit_.Text(f.label, DialogKit::Type::detail, left, b.r.top, cw, DialogKit::White(P::body));
                const D2D1_RECT_F box{left, top, right, top + P::field};
                const std::wstring err = i == 0 && !fieldError_.empty() ? fieldError_ : f.error;
                const auto v = motion_.Visual(FieldStop(i), now);
                const bool focus = i < edits_.size() && GetFocus() == edits_[i].edit;
                if (labeled) { kit_.Fill(box, P::fieldRadius, DialogKit::Gray(Design::Surface::raised)); if (!err.empty()) kit_.Ring(box, P::fieldRadius, DialogKit::Danger()); else if (focus) kit_.Ring(box, P::fieldRadius, DialogKit::White(P::ring)); }
                else if (!err.empty()) { kit_.Fill(box, P::fieldRadius, DialogKit::Danger(P::dangerFill)); kit_.Ring(box, P::fieldRadius, DialogKit::Danger()); }
                else { kit_.Fill(box, P::fieldRadius, DialogKit::White(P::fieldFill + (P::fieldHover - P::fieldFill) * v.hover)); if (focus) kit_.Ring(box, P::fieldRadius, DialogKit::White(P::ring)); }
                if (f.reveal) {
                    const auto r = Find(RevealHit(i)); const auto rv = motion_.Visual(RevealHit(i), now);
                    if (rv.hover > 0 || rv.press > 0) kit_.Fill(r, Design::Radius::inset, RowFill(RevealHit(i), now));
                    kit_.DrawGlyph(DialogKit::Glyph::eye, r, 16, DialogKit::White(i < revealed_.size() && revealed_[i] ? 1 : P::origin));
                    if (Focused(RevealHit(i))) kit_.Ring(r, Design::Radius::inset, DialogKit::White(P::ring));
                }
                if (!err.empty()) {
                    const auto style = labeled ? DialogKit::Type::detail : DialogKit::Type::body; const float g = labeled ? 14.f : 16.f;
                    const float ey = top + P::field + (labeled ? 6 : 8);
                    kit_.DrawGlyph(DialogKit::Glyph::warning, {left, ey + (style.line - g) / 2, left + g, ey + (style.line + g) / 2}, g, DialogKit::Danger());
                    kit_.Text(err, style, left + g + 6, ey, cw - g - 6, DialogKit::Danger(), true);
                }
                break;
            }
            case B::Info: {
                const auto& info = spec_.infos[b.index];
                kit_.Fill(b.r, P::fieldRadius, DialogKit::White(P::infoFill));
                kit_.DrawGlyph(DialogKit::Glyph::download, {left + 8, b.r.top + 8, left + 24, b.r.top + 24}, 16, DialogKit::White(P::muted));
                const float tw = info.trailing.empty() ? 0 : kit_.Width(info.trailing, DialogKit::Type::detailTabular);
                kit_.Text(info.text, DialogKit::Type::detail, left + 32, b.r.top + 7, cw - 40 - tw - 8, DialogKit::White(P::link));
                if (tw > 0) kit_.Text(info.trailing, DialogKit::Type::detailTabular, right - 8 - tw, b.r.top + 7, tw + 1, DialogKit::White(P::muted));
                break;
            }
            case B::Status: {
                const float sy = b.r.top;
                kit_.Fill(b.r, P::fieldRadius, DialogKit::White(P::infoFill));
                const float sh = b.r.bottom - b.r.top, iy = sy + (sh - 16) / 2;
                const D2D1_RECT_F icon{left + 10, iy, left + 26, iy + 16};
                switch (spec_.status.icon) {
                case StatusIcon::Check: kit_.DrawGlyph(DialogKit::Glyph::checkCircle, icon, 16, DialogKit::White(.7f)); break;
                case StatusIcon::Download: kit_.DrawGlyph(DialogKit::Glyph::download, icon, 16, DialogKit::White(.7f)); break;
                case StatusIcon::Warning: kit_.DrawGlyph(DialogKit::Glyph::warning, icon, 16, DialogKit::Danger()); break;
                case StatusIcon::Busy: kit_.Spinner(icon, reduced_ ? .125f : SpinTurn(now)); break;
                case StatusIcon::None: break;
                }
                const float tw = StatusTextWidth(), textH = spec_.status.detail.empty() ? 20 : StatusTextHeight(), ty = sy + std::round((sh - textH) / 2);
                kit_.Text(spec_.status.title, DialogKit::Type::body, left + 34, ty, tw, DialogKit::White(1));
                if (!spec_.status.detail.empty()) kit_.Text(spec_.status.detail, DialogKit::Type::detail, left + 34, ty + 20, tw, DialogKit::White(P::muted), true);
                if (spec_.status.hasButton) { const auto v = motion_.Visual(StatusHit, now); const auto& sb = spec_.status.button; kit_.Button(Find(StatusHit), sb.label, ToneOf(sb.role), v.hover, v.press, Focused(StatusHit), sb.disabled || busy_); }
                break;
            }
            case B::Links:
                for (size_t i = 0; i < spec_.links.size(); ++i) {
                    const auto r = Find(LinkHit(i)); const auto v = motion_.Visual(LinkHit(i), now);
                    if (v.hover > 0 || v.press > 0) kit_.Fill(r, P::fieldRadius, RowFill(LinkHit(i), now));
                    kit_.Text(spec_.links[i].label, DialogKit::Type::body, r.left + 8, r.top + 6, cw - 8 - 4 - 14 - 8, DialogKit::White(P::link + (1 - P::link) * v.hover));
                    kit_.DrawGlyph(DialogKit::Glyph::caretRight, {r.right - 4 - 14, r.top + 9, r.right - 4, r.top + 23}, 14, DialogKit::White(P::muted + (P::body - P::muted) * v.hover));
                    if (Focused(LinkHit(i))) kit_.Ring(r, P::fieldRadius, DialogKit::White(P::ring));
                }
                break;
            case B::Footnote: kit_.Text(spec_.footnote, DialogKit::Type::body, left + 8, b.r.top, cw - 8, DialogKit::White(P::muted)); break;
            case B::Accounts: {
                kit_.Text(spec_.accountsTitle, {14, 18, true}, left + 8, b.r.top + 6, cw - 16, DialogKit::White(P::muted));
                for (size_t i = 0; i < spec_.accounts.size(); ++i) {
                    const auto r = Find(AccountHit(i)); const auto v = motion_.Visual(AccountHit(i), now); const bool f = Focused(AccountHit(i));
                    if (v.hover > 0 || v.press > 0 || f) kit_.Fill(r, P::fieldRadius, f && v.hover == 0 ? DialogKit::Gray(Design::Surface::hover) : RowFill(AccountHit(i), now));
                    if (f) kit_.Ring(r, P::fieldRadius, DialogKit::White(P::ring));
                    const D2D1_RECT_F avatar{r.left + 8, r.top + 12, r.left + 32, r.top + 36};
                    kit_.Fill(avatar, 12, DialogKit::Gray(i == 0 ? Design::Surface::pressed : Design::Surface::raised));
                    const std::wstring initial = spec_.accounts[i].user.empty() ? L"?" : std::wstring(1, static_cast<wchar_t>(towupper(spec_.accounts[i].user[0])));
                    const DialogKit::Style letter{12, 12, true};
                    const float lw = kit_.Width(initial, letter);
                    kit_.Text(initial, letter, (avatar.left + avatar.right - lw) / 2, avatar.top + 6, lw + 1, DialogKit::White(P::secondaryText));
                    const float dim = spec_.accounts[i].disabled ? .32f : 1.f;   // Paper FW8-0: expired certificate at 32 %
                    kit_.Text(spec_.accounts[i].user, DialogKit::Type::field, r.left + 42, r.top + 5, cw - 50, DialogKit::White(dim));
                    kit_.Text(spec_.accounts[i].masked, AccountHintStyle(spec_.accounts[i]), r.left + 42, r.top + 27, cw - (spec_.accountsSelect ? 74 : 50), DialogKit::White(P::muted * dim));
                    if (spec_.accountsSelect) kit_.Radio({r.right - 28, r.top + 16, r.right - 12, r.top + 32}, static_cast<int>(i) == spec_.accountSelected, dim);
                }
                if (!spec_.manageLabel.empty()) {
                    const auto r = Find(ManageHit); const auto v = motion_.Visual(ManageHit, now);
                    kit_.Fill({r.left + 8, r.top - 3, r.right - 8, r.top - 3 + kit_.Pixel()}, 0, DialogKit::White(P::listRule));
                    if (v.hover > 0 || v.press > 0) kit_.Fill(r, P::fieldRadius, RowFill(ManageHit, now));
                    if (Focused(ManageHit)) kit_.Ring(r, P::fieldRadius, DialogKit::White(P::ring));
                    kit_.DrawGlyph(DialogKit::Glyph::key, {r.left + 8, r.top + 8, r.left + 32, r.top + 24}, 16, DialogKit::White(P::origin));
                    kit_.Text(spec_.manageLabel, DialogKit::Type::detail, r.left + 42, r.top + 7, cw - 50, DialogKit::White(P::body + (1 - P::body) * v.hover));
                }
                break;
            }
            case B::Check: {
                const auto r = Find(SimpleCheckHit); const auto v = motion_.Visual(SimpleCheckHit, now);
                if (LaneCheck()) {
                    const auto ln = SecondaryLane(); const float lane = ln.text, line = DialogKit::Type::detail.line, bx = r.left + ln.left + (ln.width - 16) / 2;
                    const D2D1_RECT_F box{bx, r.top + (line - 16) / 2, bx + 16, r.top + (line + 16) / 2};
                    if (spec_.checkValue) { kit_.Fill(box, 4, DialogKit::AccentColor()); kit_.DrawGlyph(DialogKit::Glyph::check, box, 12, DialogKit::White(1)); }
                    else { auto* br = pc_->SolidBrush(); br->SetColor(DialogKit::White(0x66 / 255.f + .2f * v.hover)); t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .75f, box.top + .75f, box.right - .75f, box.bottom - .75f}, 3.25f, 3.25f), br, 1.5f); }
                    kit_.Text(spec_.checkLabel, DialogKit::Type::detail, r.left + lane, r.top, r.right - r.left - lane, DialogKit::White(0xB3 / 255.f + (0xE6 - 0xB3) / 255.f * v.hover), true);
                    if (Focused(SimpleCheckHit)) kit_.Ring({r.left - 4, r.top - 4, r.right + 4, r.bottom + 4}, P::fieldRadius, DialogKit::White(P::ring));
                } else if (spec_.originCheck) {
                    const bool big = spec_.fields.empty(); const float bx = big ? r.left + 8 : r.left;
                    const D2D1_RECT_F box{bx, r.top + (r.bottom - r.top - 16) / 2, bx + 16, r.top + (r.bottom - r.top + 16) / 2};
                    if (spec_.checkValue) { kit_.Fill(box, 4, DialogKit::AccentColor()); kit_.DrawGlyph(DialogKit::Glyph::check, box, 12, DialogKit::White(1)); }
                    else { auto* br = pc_->SolidBrush(); br->SetColor(DialogKit::White(0x66 / 255.f + .2f * v.hover)); t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .75f, box.top + .75f, box.right - .75f, box.bottom - .75f}, 3.25f, 3.25f), br, 1.5f); }
                    if (big) kit_.Text(spec_.checkLabel, DialogKit::Type::field, r.left + 36, r.top, r.right - r.left - 36, DialogKit::White(1));
                    else kit_.Text(spec_.checkLabel, DialogKit::Type::detail, r.left + 24, r.top, r.right - r.left - 24, DialogKit::White(spec_.checkValue || v.hover > 0 ? 1 : P::body));
                    if (Focused(SimpleCheckHit)) kit_.Ring({r.left - 4, r.top - 6, r.right + 4, r.bottom + 6}, P::fieldRadius, DialogKit::White(P::ring));
                } else {
                    kit_.CheckBox({r.left, r.top + 8, r.left + 16, r.top + 24}, spec_.checkValue, v.hover);
                    kit_.Text(spec_.checkLabel, DialogKit::Type::detail, r.left + 24, r.top + 7, r.right - r.left - 24, DialogKit::White(spec_.checkValue || v.hover > 0 ? 1 : P::body));
                    if (Focused(SimpleCheckHit)) kit_.Ring({r.left - 8, r.top, r.right, r.bottom}, P::fieldRadius, DialogKit::White(P::ring));
                }
                break;
            }
            case B::Error:
                kit_.DrawGlyph(DialogKit::Glyph::warning, {left, b.r.top + 2, left + 16, b.r.top + 18}, 16, DialogKit::Danger());
                kit_.Text(error_, DialogKit::Type::body, left + 22, b.r.top, cw - 22, DialogKit::Danger(), true);
                break;
            case B::Danger: {
                kit_.Fill(b.r, P::fieldRadius, DialogKit::Danger(P::dangerFill)); kit_.Ring(b.r, P::fieldRadius, DialogKit::Danger(P::dangerHover));
                const float x = left + 12, y = b.r.top + 12, iw = cw - 24;
                kit_.Fill({x, y + 4, x + 32, y + 36}, P::fieldRadius, DialogKit::Danger(0x26 / 255.f));
                kit_.DrawGlyph(DialogKit::Glyph::triangle, {x, y + 4, x + 32, y + 36}, 16, DialogKit::Danger());
                kit_.Text(spec_.danger.title, DialogKit::Type::title, x + 42, y, iw - 42, DialogKit::White(1));
                kit_.Text(spec_.danger.subtitle, DialogKit::Type::detail, x + 42, y + 22, iw - 42, DialogKit::Danger());
                kit_.Text(spec_.danger.text, DialogKit::Type::detail, x, y + 40 + 12, iw, DialogKit::White(P::body), true);
                break;
            }
            case B::Actions: break;
            }
        }
        PaintActions(now);
        kit_.Ring({0, 0, width_, height_}, P::radius, DialogKit::White(P::border));
        if (t->EndDraw() == D2DERR_RECREATE_TARGET && pc_ == &chrome_) { kit_.Release(); chrome_.RecreateTarget(); }
    }
    // ---- UI Automation --------------------------------------------------------------------------
    // B07 card for UIA (R1): file, real reason title and full text, from the spec (not the drawn, possibly
    // ellipsised lines). Given as the dialog's description, the card group's name/help and each card
    // button's help, so it is read on open and again on every keyboard stop.
    std::wstring DangerSpoken() const {
        std::wstring s = spec_.danger.title;
        if (!spec_.danger.subtitle.empty()) s += (s.empty() ? L"" : L" — ") + spec_.danger.subtitle;
        if (!spec_.danger.text.empty()) s += (s.empty() ? L"" : L". ") + spec_.danger.text;
        return s;
    }
    std::wstring AccessibleName() const { if (!spec_.title.empty()) return spec_.title; if (!spec_.origin.empty()) return spec_.origin; if (spec_.danger.shown) return spec_.danger.title; if (!spec_.accountsTitle.empty()) return spec_.accountsTitle; return L"Dialogue"; }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> nodes; if (!open_) return nodes;
        Accessibility::Node g; g.key = GroupKey; g.role = Accessibility::Role::Group; g.name = AccessibleName();
        g.help = !spec_.message.empty() ? spec_.message : !spec_.body.empty() ? spec_.body : spec_.subtitle;
        if (spec_.message.empty() && !spec_.body.empty() && !spec_.bodyKeys.empty()) { g.help += L" "; for (size_t i = 0; i < spec_.bodyKeys.size(); ++i) g.help += (i ? L" + " : L"") + spec_.bodyKeys[i]; g.help += L"."; } if (!error_.empty()) g.help = error_;
        if (spec_.danger.shown && error_.empty()) g.help = DangerSpoken();   // R1: the full reason, never the ellipsised line
        g.automationId = spec_.automationId; g.focusable = false; RECT wr{}; GetWindowRect(hwnd_, &wr); g.bounds = wr; nodes.push_back(g);
        auto add = [&](int key, const std::wstring& name, D2D1_RECT_F r, bool enabled, const std::wstring& id = L"") { Accessibility::Node n; n.key = key; n.parent = GroupKey; n.name = name; n.bounds = ScreenOf(r); n.enabled = enabled; n.automationId = id; nodes.push_back(n); return nodes.size() - 1; };
        for (size_t i = 0; i < spec_.fields.size(); ++i) if (spec_.fields[i].reveal) { const auto at = add(RevealHit(i), L"Afficher le mot de passe", Find(RevealHit(i)), !busy_); nodes[at].toggle = i < revealed_.size() && revealed_[i] ? 2 : 1; }
        if (spec_.sources.shown) {
            const auto& sp = spec_.sources;
            for (size_t i = 0; i < sp.categories.size(); ++i) { const auto at = add(CategoryHit(i), sp.categories[i], Find(CategoryHit(i)), !busy_, L"ShareCategory"); nodes[at].role = Accessibility::Role::TabItem; nodes[at].selected = static_cast<int>(i) == sp.category; nodes[at].position = static_cast<int>(i) + 1; nodes[at].count = static_cast<int>(sp.categories.size()); }
            const auto vis = VisibleSources();
            for (size_t k = 0; k < vis.size(); ++k) { const size_t i = vis[k]; const auto at = add(SourceHit(i), sp.items[i].title, Find(SourceHit(i)), !busy_, L"ShareSource"); nodes[at].role = Accessibility::Role::ListItem; nodes[at].selected = static_cast<int>(i) == sp.selected; nodes[at].position = static_cast<int>(k) + 1; nodes[at].count = static_cast<int>(vis.size()); nodes[at].help = L"Aperçu fictif (démonstration)"; }
        }
        for (size_t i = 0; i < spec_.rows.size(); ++i) if (spec_.rows[i].toggle >= 0) {
            const auto at = add(RowHit(i), spec_.rows[i].label, Find(RowHit(i)), spec_.rows[i].toggle == 1 && !busy_, L"SitePermissionToggle");
            nodes[at].toggle = spec_.rows[i].toggle == 1 ? 2 : 1; nodes[at].help = spec_.rows[i].trailing.empty() ? spec_.rows[i].detail : spec_.rows[i].trailing + L" · " + spec_.rows[i].detail;
        }
        if (spec_.hasSelect) { const auto at = add(SelectHit, spec_.select.label, Find(SelectHit), !busy_); nodes[at].expand = 1; if (spec_.select.index >= 0 && spec_.select.index < static_cast<int>(spec_.select.options.size())) nodes[at].help = spec_.select.options[static_cast<size_t>(spec_.select.index)]; }
        for (size_t i = 0; i < spec_.checks.size(); ++i) { const auto at = add(CheckHit(i), spec_.checks[i].label, Find(CheckHit(i)), !spec_.checks[i].disabled && !busy_); nodes[at].toggle = spec_.checks[i].value ? 2 : 1; nodes[at].help = spec_.checks[i].detail; }
        if (spec_.hasStatus && spec_.status.hasButton) { const auto at = add(StatusHit, spec_.status.button.label, Find(StatusHit), Enabled(StatusHit)); nodes[at].help = spec_.status.title + L" · " + spec_.status.detail; }
        for (size_t i = 0; i < spec_.links.size(); ++i) add(LinkHit(i), spec_.links[i].label, Find(LinkHit(i)), !busy_);
        for (size_t i = 0; i < spec_.accounts.size(); ++i) { const auto at = add(AccountHit(i), spec_.accounts[i].user, Find(AccountHit(i)), !spec_.accounts[i].disabled); nodes[at].selected = spec_.accountsSelect && static_cast<int>(i) == spec_.accountSelected; nodes[at].help = spec_.accounts[i].masked; nodes[at].role = Accessibility::Role::ListItem; nodes[at].position = static_cast<int>(i) + 1; nodes[at].count = static_cast<int>(spec_.accounts.size()); }
        if (!spec_.manageLabel.empty()) add(ManageHit, spec_.manageLabel, Find(ManageHit), true);
        if (!spec_.checkLabel.empty()) { const auto at = add(SimpleCheckHit, spec_.checkLabel, Find(SimpleCheckHit), !busy_); nodes[at].toggle = spec_.checkValue ? 2 : 1; }
        if (spec_.danger.shown) { Accessibility::Node c; c.key = DangerKey; c.parent = GroupKey; c.role = Accessibility::Role::Group; c.focusable = false; c.name = spec_.danger.title + (spec_.danger.subtitle.empty() ? L"" : L" — " + spec_.danger.subtitle); c.help = spec_.danger.text; c.automationId = L"DangerousDownloadCard"; for (const auto& bl : blocks_) if (bl.type == B::Danger) c.bounds = ScreenOf(bl.r); nodes.push_back(c); }
        for (size_t i = 0; i < spec_.buttons.size(); ++i) { const auto at = add(ButtonHit(i), spec_.buttons[i].label, Find(ButtonHit(i)), !ButtonDisabled(spec_.buttons[i]), spec_.buttons[i].automationId); if (spec_.danger.shown) nodes[at].help = DangerSpoken(); }
        if (spec_.closeButton) add(CloseHit, L"Fermer", Find(CloseHit), true, L"DialogClose");
        return nodes;
    }
public:
    ~Dialog() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root, Hooks hooks) {
        instance_ = instance; owner_ = owner; hooks_ = std::move(hooks); motion_.EnablePolish(); chrome_.SetMotion(&motion_); kit_.Bind(&chrome_);
        reduced_ = Motion::PreferredReducedMotion(); motion_.SetReducedMotion(reduced_);
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.lpszClassName = L"BrowserModalDialog"; RegisterClassW(&wc);
        WNDCLASSW sc{}; sc.lpfnWndProc = ScrimProc; sc.hInstance = instance; sc.hCursor = LoadCursor(nullptr, IDC_ARROW); sc.lpszClassName = L"BrowserModalScrim"; RegisterClassW(&sc);
        scrim_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, sc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"Dialogue", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, nullptr, instance, this);
        if (!hwnd_ || !scrim_) return false;
        SetLayeredWindowAttributes(scrim_, 0, 0, LWA_ALPHA); SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        NativeUI::ConfigureShadowlessPopup(scrim_, 0);
        NativeUI::ConfigureShadowlessPopup(hwnd_, P::radius);
        Accessibility::Host host; host.window = hwnd_;
        host.nodes = [this] { return Nodes(); };
        host.focused = [this]() -> long long { return GetFocus() == hwnd_ ? focus_ : 0; };
        host.invoke = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.toggle = [this](long long key) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.select = [this](long long key) { if (IsCategory(static_cast<int>(key)) || IsSource(static_cast<int>(key))) PostMessageW(hwnd_, WM_APP + 3, static_cast<WPARAM>(key), 0); };
        host.expand = [this](long long key, bool) { PostMessageW(hwnd_, WM_APP + 1, static_cast<WPARAM>(key), 0); };
        host.label = [this](HWND child, Accessibility::Node& n) {
            const int i = EditIndex(child); if (i < 0) return false;
            const auto& f = spec_.fields[static_cast<size_t>(i)];
            n.name = !f.label.empty() ? f.label : !spec_.message.empty() ? spec_.message.substr(0, 200) : spec_.title;
            n.automationId = f.automationId; n.help = i == 0 && !fieldError_.empty() ? fieldError_ : f.error; return true;
        };
        uia_.Attach(std::move(host));
        root_ = root;
        return SUCCEEDED(chrome_.Init(hwnd_, root));
    }
    void Dispose() {
        if (open_) { open_ = false; auto done = std::move(done_); done_ = nullptr; if (done) done(Result{}); }
        // A list still tracking (Dispose from inside its loop) is only dismissed: its Track() is on the stack.
        if (choosing_) { if (hostMenu_) { if (hooks_.dismiss) hooks_.dismiss(); } else if (menu_) menu_->Cancel(); }
        else if (menu_) menu_->Dispose();   // its windows go with hwnd_ (their owner) below
        uia_.Detach(); DestroyEdits(); kit_.Release(); chrome_.Dispose();
        if (font_) DeleteObject(font_);
        font_ = nullptr;
        if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
        if (scrim_ && IsWindow(scrim_)) DestroyWindow(scrim_);
        hwnd_ = scrim_ = nullptr; closing_ = false;
        if (!choosing_) menu_.reset();
    }
    bool Show(Spec spec, RECT area, std::function<void(const Result&)> done) {
        if (!hwnd_ || open_) return false;
        if (closing_) FinishClosing();
        spec_ = std::move(spec); done_ = std::move(done); area_ = area; ++generation_;
        busy_ = submitted_ = false; error_.clear(); fieldError_.clear(); busyLabel_.clear(); scroll_ = 0; hover_ = pressed_ = 0; focus_ = 0; motion_.ClearInteractions();
        keyboard_ = OpenedFromKeyboard(); open_ = true; spinStart_ = Motion::Now();
        HWND f = GetFocus(); restore_ = f && f != hwnd_ && !IsChild(hwnd_, f) ? f : nullptr;
        SetWindowTextW(hwnd_, AccessibleName().c_str());
        EnableWindow(hwnd_, TRUE);
        CreateEdits();
        fade_.Snap(0); fade_.Set(1, Motion::Now(), Motion::Milliseconds(static_cast<int>(P::enterMs)), false, Motion::Curve::EaseOut);
        Place(true); ApplyAlpha();
        if (!Anchored()) ShowWindow(scrim_, SW_SHOWNOACTIVATE); else ShowWindow(scrim_, SW_HIDE);
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        FocusStop(InitialStop());
        if (uia_.Listening()) { uia_.ChildrenChanged(GroupKey); uia_.Raise(GroupKey, UIA_Window_WindowOpenedEventId); }
        if (hooks_.wake) hooks_.wake();
        return true;
    }
    // Replace the content of an open dialog (B05 list ↔ detail ↔ confirmation); keeps `done`.
    void Update(Spec spec) {
        if (!open_) return;
        spec_ = std::move(spec); ++generation_; busy_ = submitted_ = false; error_.clear(); fieldError_.clear(); scroll_ = 0; hover_ = pressed_ = 0; motion_.ClearInteractions();
        SetWindowTextW(hwnd_, AccessibleName().c_str()); CreateEdits(); Place(false); FocusStop(InitialStop());
        if (uia_.Listening()) uia_.ChildrenChanged(GroupKey);
    }
    void Abort() { if (open_) Close(Result{}); }
    // Current answers (fields, checks, select, single check) with button = -1, dialog untouched.
    Result Snapshot() const { return Collect(-1); }
    void Finish(int button) { if (open_) Close(Collect(button)); }
    // A command list dropped under the control `hit` (right edges aligned), modal like TrackPopupMenu.
    // The dialog stays open while the list owns activation (no light dismiss towards our own menu);
    // Échap / a click outside close only the list. Returns the chosen id, 0 when dismissed or when
    // the dialog was closed or replaced meanwhile. Focus comes back to `hit`.
    int TrackMenu(const std::vector<ContextMenu::Item>& items, int hit, bool keyboard) {
        if (!open_ || busy_ || choosing_ || items.empty() || !hwnd_) return 0;
        const unsigned generation = generation_; const RECT anchor = ScreenOf(Find(hit));
        keyboard_ = keyboard; focus_ = hit; InvalidateRect(hwnd_, nullptr, FALSE);
        choosing_ = true; hostMenu_ = static_cast<bool>(hooks_.menu);
        int id = 0;
        if (hooks_.menu) id = hooks_.menu(items, anchor, keyboard);
        else {
            if (!menu_) { menu_ = std::make_unique<ContextMenu::Menu>(); menu_->Init(instance_, hwnd_, root_); }
            id = menu_->Track(items, {anchor.right, anchor.bottom}, &anchor, keyboard, reduced_, true);
        }
        choosing_ = false;
        if (!open_ || generation != generation_ || !hwnd_ || !IsWindowVisible(hwnd_)) return 0;
        SetForegroundWindow(hwnd_); keyboard_ = keyboard; FocusStop(hit);
        return id;
    }
    bool KeyboardModality() const { return keyboard_; }
    bool OpenedFromKeyboard() const { return spec_.modality == Spec::Modality::Keyboard || (spec_.modality == Spec::Modality::Host && hooks_.keyboard && hooks_.keyboard()); }
    // Focus a control of the current content (after Update, e.g. back on « Plus d'options »).
    void FocusControl(int hit) { if (open_ && Enabled(hit)) FocusStop(hit); }
    void SetBusy(bool busy, const std::wstring& label = L"") {
        if (!open_) return;
        busy_ = busy; busyLabel_ = label; if (busy) { error_.clear(); spinStart_ = Motion::Now(); hover_ = pressed_ = 0; motion_.ClearInteractions(); }
        for (auto& e : edits_) if (e.edit) EnableWindow(e.edit, busy ? FALSE : TRUE);
        if (!busy && GetFocus() == hwnd_ && !Enabled(focus_)) FocusStop(InitialStop());
        Relayout(); if (hooks_.wake) hooks_.wake();
        if (uia_.Listening() && busy) uia_.Notify(GroupKey, label.c_str(), L"DialogBusy");
    }
    void SetError(const std::wstring& message) { if (!open_) return; error_ = message; Relayout(); if (uia_.Listening() && !message.empty()) uia_.Notify(GroupKey, message.c_str(), L"DialogError"); }
    // A field error set by the host (B06 « Identifiant ou mot de passe refusé »): focus moves there.
    void SetFieldError(size_t field, const std::wstring& message) { if (!open_ || field >= spec_.fields.size()) return; spec_.fields[field].error = message; Relayout(); FocusStop(FieldStop(field)); RefreshCaret(); }
    void SetStatus(const Status& status) { if (!open_) return; spec_.status = status; spec_.hasStatus = true; spinStart_ = Motion::Now(); Relayout(); if (hooks_.wake) hooks_.wake(); }
    void Reposition(RECT area) { area_ = area; if (open_) Place(false); }
    void SetAnchor(RECT anchor) { spec_.anchor = anchor; if (open_) Place(false); }
    bool Visible() const { return open_; }
    HWND Window() const { return hwnd_; }
    HWND Scrim() const { return scrim_; }
    // Covered surface: the page area (Scope::Page), the whole owner window (Scope::Window), none (Anchor).
    bool BlocksWindow() const { return open_ && spec_.scope == Scope::Window; }
    const Spec& CurrentSpec() const { return spec_; }
    Motion::Wake NextWake(Motion::Time now) const {
        if (!open_ && !closing_) return {};
        Motion::Wake w = motion_.NextWake(now);
        if (fade_.Moving(now) || closing_) w = Motion::Earlier(w, Motion::Frame());
        if (open_ && !reduced_ && (busy_ || (spec_.hasStatus && spec_.status.icon == StatusIcon::Busy))) w = Motion::Earlier(w, Motion::Frame());
        return w;
    }
    void Frame(Motion::Time now) {
        if (!hwnd_) return;
        if (closing_ && !fade_.Moving(now)) { FinishClosing(); return; }
        if ((open_ || closing_) && fade_.Moving(now)) Place(true);   // exit retraces the 4 DIP entrance, shorter
        ApplyAlpha();
        if (open_ || closing_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; motion_.SetReducedMotion(reduced); if (open_) InvalidateRect(hwnd_, nullptr, FALSE); }
    // Test helpers (native harness).
    bool FocusVisibleForTest() const { return keyboard_ && focus_ != 0; }   // the ring is drawn (window focus aside)
    int FocusForTest() const { return CurrentStop(); }
    float HeadLeftForTest() const { return HeadLeft(); }
    float LaneTextForTest() const { return SecondaryLane().text; }
    float LaneBoxLeftForTest() const { const auto l = SecondaryLane(); return l.left + (l.width - 16) / 2; }
    bool LaneCheckForTest() const { return LaneCheck(); }
    DialogKit::Style TitleStyleForTest() const { return TitleStyle(); }
    float TitleDropForTest() const { return TileTitleDrop(); }
    void KeyForTest(WPARAM key) { Key(key); }
    void AltKeyForTest(WPARAM key) { AltKey(key); }
    bool ChoosingForTest() const { return choosing_; }
    ContextMenu::Menu* MenuForTest() const { return menu_.get(); }
    void ActivateForTest(int hit) { Activate(hit, false); }
    void SetFieldForTest(size_t i, const std::wstring& text) { if (i < edits_.size() && edits_[i].edit) { SetWindowTextW(edits_[i].edit, text.c_str()); Revalidate(); } }
    std::wstring FieldForTest(size_t i) const { return i < edits_.size() ? edits_[i].Text() : L""; }
    HWND EditForTest(size_t i) const { return i < edits_.size() ? edits_[i].edit : nullptr; }
    D2D1_RECT_F BoxForTest(int hit) const { return Find(hit); }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    const SourcePicker& SourcesForTest() const { return spec_.sources; }
    bool EnabledForTest(int hit) const { return Enabled(hit); }
    int ColumnsForTest() const { return sourceColumns_; }
    float HeightForTest() const { return height_; }
    float WidthForTest() const { return width_; }
    bool BusyForTest() const { return busy_; }
    bool ClosingForTest() const { return closing_; }
    std::wstring FieldErrorForTest() const { return fieldError_; }
    void FinishAnimationsForTest() { fade_.Settle(); if (closing_) FinishClosing(); else if (open_) { Place(false); ApplyAlpha(); } }
    // Paint the current content into a PNG (offscreen Direct2D, works on a locked desktop).
    bool RenderForTest(const std::wstring& png, UINT dpi = 0) {
        if (!open_) return false;
        Chrome off; if (FAILED(off.InitOffscreen(root_, dpi ? dpi : Dpi(), width_, height_))) return false;
        kit_.Release(); kit_.Bind(&off); pc_ = &off;
        Paint();
        const bool ok = SUCCEEDED(off.SavePng(png.c_str()));
        kit_.Release(); kit_.Bind(&chrome_); pc_ = &chrome_; off.Dispose();
        return ok;
    }
    void PaintForTest() { Paint(); }
};


// ---------------------------------------------------------------------------------------
// Presets (Paper text, verbatim). `origin` is the caller frame's host (eTLD+1 for a cross-
// origin subframe), never the page title.
inline Button Make(int id, std::wstring label, Role role, bool isDefault, bool isCancel, bool initial, std::wstring automationId = L"") {
    Button b; b.id = id; b.label = std::move(label); b.role = role; b.isDefault = isDefault; b.isCancel = isCancel; b.initialFocus = initial; b.automationId = std::move(automationId); return b;
}
// A2 alert(): OK only (Entrée and Échap both answer OK); the blocking check appears from the
// 2nd dialog of the same site in the same tab session (`repeated`).
inline Spec AlertSpec(const std::wstring& origin, const std::wstring& message, bool repeated = false) {
    Spec s; s.kind = Kind::Alert; s.scope = Scope::Page; s.automationId = L"ScriptAlert"; s.origin = origin + L" indique"; s.message = message;
    if (repeated) s.checkLabel = L"Empêcher ce site d'ouvrir d'autres dialogues";
    s.buttons = {Make(AcceptButton, L"OK", Role::Primary, true, true, true, L"DialogOk")};
    return s;
}
// A2 confirm(): OK = default (Entrée), Annuler = Échap, focus on OK.
inline Spec ConfirmSpec(const std::wstring& origin, const std::wstring& message, bool repeated = false) {
    Spec s = AlertSpec(origin, message, repeated); s.kind = Kind::Confirm; s.automationId = L"ScriptConfirm";
    s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, false, L"DialogCancel"), Make(AcceptButton, L"OK", Role::Primary, true, false, true, L"DialogOk")};
    return s;
}
// A3 prompt(): value selected at opening, no browser validation (empty is a valid answer).
inline Spec PromptSpec(const std::wstring& origin, const std::wstring& message, const std::wstring& defaultText, bool repeated = false) {
    Spec s = ConfirmSpec(origin, message, repeated); s.kind = Kind::Prompt; s.automationId = L"ScriptPrompt";
    Field f; f.value = defaultText; f.automationId = L"DialogPromptField"; f.maxLength = 32767; s.fields.push_back(f);
    return s;
}
// A3 « Variante Browser » (folder, rename, bookmark address): validated at first submit, then
// on every keystroke; while invalid the action is disabled and Entrée does nothing.
inline Spec InputSpec(const std::wstring& title, const std::wstring& subtitle, const std::wstring& value, const std::wstring& action, std::function<std::wstring(const std::vector<std::wstring>&)> validate) {
    Spec s; s.kind = Kind::Generic; s.scope = Scope::Window; s.automationId = L"BrowserInput"; s.title = title; s.body = subtitle;
    Field f; f.value = value; f.automationId = L"DialogInputField"; s.fields.push_back(f); s.validate = std::move(validate);
    s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, false, L"DialogCancel"), Make(AcceptButton, action, Role::Primary, true, false, false, L"DialogAccept")};
    return s;
}
// A4/A6 beforeunload: the browser's text, never the site's. Focus on « Rester sur la page »;
// the destructive action is never the Entrée default.
inline Spec BeforeUnloadSpec(const std::wstring& origin, bool reload) {
    Spec s; s.kind = Kind::BeforeUnload; s.scope = Scope::Page; s.automationId = L"BeforeUnload";
    s.title = reload ? L"Actualiser ce site ?" : L"Quitter ce site ?";
    s.body = L"Les modifications non enregistrées sur " + origin + L" pourraient être perdues.";
    s.buttons = {Make(CancelButton, L"Rester sur la page", Role::Secondary, false, true, true, L"DialogStay"), Make(AcceptButton, reload ? L"Actualiser" : L"Quitter", Role::Danger, true, false, false, L"DialogLeave")};
    return s;
}
// A5. `details`: per category (history, cookies, cache, downloads, permissions); empty keeps
// Paper's static lines. Accepting is AcceptButton; to show the busy footer, set
// buttons[1].closes = false + onButton, call SetBusy(true, L"Effacement…") then Finish().
inline const std::vector<std::wstring>& ClearDataPeriods() { static const std::vector<std::wstring> p{L"Dernière heure", L"Dernières 24 heures", L"7 derniers jours", L"4 dernières semaines", L"Depuis toujours"}; return p; }
inline Spec ClearDataSpec(std::vector<std::wstring> details = {}, int period = 1, std::vector<bool> chosen = {true, true, true, false, false}) {
    Spec s; s.kind = Kind::ClearData; s.scope = Scope::Window; s.automationId = L"ClearBrowsingData";
    s.title = L"Effacer les données"; s.body = L"Sur cet appareil uniquement. Les onglets ouverts sont conservés.";
    s.hasSelect = true; s.select.label = L"Période"; s.select.options = ClearDataPeriods(); s.select.index = std::clamp(period, 0, 4);
    const wchar_t* labels[] = {L"Historique de navigation", L"Cookies et données des sites", L"Images et fichiers en cache", L"Historique des téléchargements", L"Autorisations des sites"};
    const wchar_t* fixed[] = {L"", L"Vous serez déconnecté de la plupart des sites.", L"", L"Les fichiers restent sur le disque.", L"Caméra, micro, notifications…"};
    for (size_t i = 0; i < 5; ++i) { Check c; c.label = labels[i]; c.detail = i < details.size() && !details[i].empty() ? details[i] : fixed[i]; c.value = i < chosen.size() && chosen[i]; s.checks.push_back(c); }
    s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, true, L"DialogCancel"), Make(AcceptButton, L"Effacer les données", Role::Danger, true, false, false, L"DialogClearData")};
    return s;
}
// A6 « Fermer la fenêtre » (from 2 tabs). With a download in progress the check disappears and
// the action turns destructive.
inline Spec CloseWindowSpec(int tabs, const std::wstring& download = L"", const std::wstring& progress = L"") {
    Spec s; s.kind = Kind::CloseWindow; s.scope = Scope::Window; s.automationId = L"CloseWindow";
    s.width = 420;   // Paper EY9-0 (comment c-npdt3g): this confirmation only; other dialogs keep 400
    s.title = L"Fermer " + std::to_wstring(tabs) + L" onglets ?";
    if (download.empty()) {
        s.body = L"Rouvrez-les depuis l'historique ou avec"; s.bodyKeys = {L"Ctrl", L"Maj", L"T"}; s.checkLabel = L"Ne plus demander"; s.checkInActions = true;
        s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, true, L"DialogCancel"), Make(AcceptButton, L"Fermer la fenêtre", Role::Primary, true, false, false, L"DialogCloseWindow")};
    } else {
        s.body = L"Un téléchargement est en cours et sera annulé."; s.infos.push_back({download, progress});
        s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, true, L"DialogCancel"), Make(AcceptButton, L"Fermer et annuler le téléchargement", Role::Danger, true, false, false, L"DialogCloseWindow")};
    }
    return s;
}
// A6 « Ouvrir tous les favoris » (from 15). `open` < `count` when the 128-tab limit cuts it.
inline Spec OpenManyTabsSpec(int count, const std::wstring& folder = L"", int open = -1, int existing = 0, int limit = 128) {
    if (open < 0 || open > count) open = count;
    Spec s; s.kind = Kind::OpenManyTabs; s.scope = Scope::Window; s.automationId = L"OpenManyTabs";
    const std::wstring name = folder.empty() ? L"" : L" « " + folder + L" »";
    if (open < count) {
        s.title = L"Ouvrir " + std::to_wstring(open) + L" des " + std::to_wstring(count) + L" favoris ?";
        s.body = L"Cette fenêtre compte déjà " + std::to_wstring(existing) + L" onglets et en accepte " + std::to_wstring(limit) + L" au maximum. Les " + std::to_wstring(count - open) + L" derniers favoris" + (folder.empty() ? L"" : L" de" + name) + L" ne seront pas ouverts.";
    } else {
        s.title = L"Ouvrir " + std::to_wstring(count) + L" favoris ?";
        s.body = (folder.empty() ? std::wstring(L"Ces favoris s'ouvriront") : L"Le dossier" + name + L" s'ouvrira") + L" dans " + std::to_wstring(count) + L" nouveaux onglets. Les pages se chargent quand vous les affichez.";
    }
    s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, true, L"DialogCancel"), Make(AcceptButton, L"Ouvrir " + std::to_wstring(open) + L" onglets", Role::Primary, true, false, false, L"DialogOpenTabs")};
    return s;
}
// Not drawn in Paper (task A6 « supprimer favoris »): A6 vocabulary, destructive action.
inline Spec DeleteBookmarksSpec(int count) {
    Spec s; s.kind = Kind::DeleteBookmarks; s.scope = Scope::Window; s.automationId = L"DeleteBookmarks";
    s.title = count > 1 ? L"Supprimer " + std::to_wstring(count) + L" favoris ?" : L"Supprimer ce favori ?";
    s.body = L"Vous pourrez annuler juste après depuis la notification.";
    s.buttons = {Make(CancelButton, L"Annuler", Role::Secondary, false, true, true, L"DialogCancel"), Make(AcceptButton, L"Supprimer", Role::Danger, true, false, false, L"DialogDelete")};
    return s;
}
// A8 states (same vocabulary as the update toast).
enum AboutState { AboutUpToDate = 0, AboutReady = 1, AboutOffline = 2, AboutChecking = 3 };
// `version`: running version (« 0.3.113 »); `detail`: second status line (check time,
// « S'installe au redémarrage », « Aucune connexion internet »); `next`: ready version.
inline Status AboutStatus(int state, const std::wstring& detail = L"", const std::wstring& next = L"") {
    Status st;
    switch (state) {
    case AboutReady: st.icon = StatusIcon::Download; st.title = L"Version " + next + L" prête"; st.detail = detail.empty() ? L"S'installe au redémarrage" : detail; st.button = Make(AboutRestartButton, L"Redémarrer", Role::Primary, false, false, false, L"AboutRestart"); st.hasButton = true; break;
    case AboutOffline: st.icon = StatusIcon::Warning; st.title = L"Vérification impossible"; st.detail = detail.empty() ? L"Aucune connexion internet" : detail; st.button = Make(AboutCheckButton, L"Réessayer", Role::Quiet, false, false, false, L"AboutRetry"); st.button.closes = false; st.hasButton = true; break;
    case AboutChecking: st.icon = StatusIcon::Busy; st.title = L"Recherche de mises à jour…"; st.detail = detail; st.button = Make(AboutCheckButton, L"Rechercher", Role::Quiet, false, false, false, L"AboutCheck"); st.button.closes = false; st.button.disabled = true; st.hasButton = true; break;
    default: st.icon = StatusIcon::Check; st.title = L"browser est à jour"; st.detail = detail; st.button = Make(AboutCheckButton, L"Rechercher", Role::Quiet, false, false, false, L"AboutCheck"); st.button.closes = false; st.hasButton = true; break;
    }
    return st;
}
inline Spec AboutSpec(const std::wstring& version, int state, const std::wstring& detail = L"", const std::wstring& next = L"") {
    Spec s; s.kind = Kind::About; s.scope = Scope::Window; s.automationId = L"AboutBrowser";
    s.appTile = true; s.closeButton = true; s.title = L"browser";
    s.body = (version.rfind(L"Version ", 0) == 0 ? version : L"Version " + version) + L" · Windows 64 bits";
    s.status = AboutStatus(state, detail, next); s.hasStatus = true;
    s.links = {{AboutNotesLink, L"Notes de version"}, {AboutLicensesLink, L"Licences des composants tiers"}, {AboutReportLink, L"Signaler un problème"}};
    s.footnote = L"Moteur Microsoft Edge WebView2 · runtime système";
    return s;
}
// A9: the shipped « indisponible » modal; `future` (the concept) marks the future idea as Démonstration.
inline Spec PrivateSpec(bool future = false) {
    Spec s; s.kind = Kind::Private; s.scope = Scope::Window; s.automationId = L"PrivateBrowsing";
    if (!future) {
        s.title = L"Navigation privée indisponible";
        s.body = L"Cette version n'a pas encore de fenêtre privée. Vos onglets utilisent le profil habituel : cookies, cache et historique sont conservés.";
        s.footnote = L"Aucun onglet n'a été ouvert.";
    } else {
        s.demo = true; s.title = L"Fenêtre privée — concept";
        s.body = L"Une fenêtre privée utiliserait un environnement WebView2 éphémère (dossier temporaire supprimé à la fermeture), sans cookies partagés avec le profil habituel, repérée par la puce « Privée » dans la barre d'onglets.";
        s.footnote = L"Concept futur, non implémenté : aucune fenêtre n'a été ouverte.";
    }
    s.buttons = {Make(PrivateClearDataButton, L"Effacer les données…", Role::Quiet, false, false, false, L"PrivateClearData"), Make(AcceptButton, L"Compris", Role::Primary, true, true, true, L"DialogOk")};
    return s;
}
} // namespace Modal
} // namespace NativeChrome
