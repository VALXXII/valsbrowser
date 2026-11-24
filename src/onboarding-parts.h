#pragma once
#include "i18n.h"
// Pieces of the first-run onboarding window (src/onboarding.h) that the annotation mode's own onboarding
// (src/annotation-onboarding.h, #125) is drawn with too: the same window family (720 × 580, the turquoise
// GrainGradient of board 6, centred title, the header's language and close buttons, the footer's buttons). They
// were members of OnboardingUI::Window; both windows call them now, with the very same values.
//
//   TextLook          the text weight of the boards (DirectWrite brought to the web engine's ink mass)
//   HeaderControls    « Français ⌄ » and the close cross (board 6: #474747 over the light gradient)
//   FooterButton      the 32-high buttons (raised #292929 / accent, hover and press, keyboard ring)
//   Shade             one shader node into its Direct2D layer (still frames kept until the target changes)
//   GrainUniforms     board 6's GrainGradient at a profile's hue (the boards are drawn at 200° / 88 %)
//   TrameLayer        board 6's dither over that gradient (src/onboarding-dither.h), 15 pictures a second
//   GrainBackdrop     the gradient's node: #DEFDFF, the shader (or its plain fallback), the dither
//   AnnotationDemo    board 6's illustration (the picked button, its frame and tags, the crosshair, the note)
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include "chrome.h"
#include "tooltip.h"
#include "ui-language.h"
#include "onboarding-kit.h"
#include "onboarding-shaders.h"
#include "shader-layer.h"
#include "onboarding-dither.h"
#include "onboarding-tuning.h"

namespace NativeChrome {
namespace OnboardingParts {

namespace K = OnboardingKit;

// ---- text ----------------------------------------------------------------------------------------------
// Text weight: the boards were drawn by a web engine, whose light-on-dark text reads heavier than
// DirectWrite's default grayscale rendering (ink mass about 0.8 of the board's). These parameters bring the
// same glyphs to the board's weight (measured by tests/onboarding/compare.py).
inline constexpr float TextGamma = 2.2f, TextContrast = .5f;
inline void TextLook(Chrome& c, ID2D1RenderTarget* t) {
    IDWriteRenderingParams* params = nullptr; float gamma = TextGamma, contrast = TextContrast;
    if (const wchar_t* g = _wgetenv(L"ONBOARDING_TEXT_GAMMA")) gamma = static_cast<float>(_wtof(g));
    if (const wchar_t* k = _wgetenv(L"ONBOARDING_TEXT_CONTRAST")) contrast = static_cast<float>(_wtof(k));
    if (c.WriteFactory() && SUCCEEDED(c.WriteFactory()->CreateCustomRenderingParams(gamma, contrast, 0, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, &params)) && params) { t->SetTextRenderingParams(params); params->Release(); }
    t->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}
inline K::Font Label() { return {14, 20, 500, 0}; }

// ---- header: language + close ---------------------------------------------------------------------------
// h 28, padding-inline 6, gap 4, radius 6: the autonym 14/20 500 + chevron 16; the close button 28 × 28, cross 16.
inline constexpr float HeaderSize = 28, HeaderRadius = 6;
inline float LanguageWidth(Chrome& c) { return 6 + K::Width(c, UiLanguage::Autonym(I18n::Current()), Label()) + 4 + 16 + 6; }
struct Control { D2D1_RECT_F r; float lift; bool ring; };
// `tone` 0: #B3B3B3 (the canvas boards), 1: #474747 (board 6, over the light gradient). `lift`: hover / press /
// open menu as white over the control (0.08 hover, 0.04 more pressed, 0.08 while the menu is open).
inline void HeaderControls(Chrome& c, K::Glyphs& glyphs, const Control& lang, const Control& close, float tone, float alpha) {
    if (alpha <= 0) return;
    const auto color = K::Col(K::Lerp(K::FromHex(K::Token::secondary), K::FromHex(0x474747), tone), alpha);
    for (const Control* x : {&lang, &close}) {
        if (x->lift > 0) K::Fill(c, x->r, HeaderRadius, PopupKit::White(x->lift * alpha));
        if (x->ring) PopupKit::InsetRing(c, x->r, HeaderRadius);
    }
    const std::wstring name = UiLanguage::Autonym(I18n::Current());
    K::Text(c, name, {lang.r.left + 6, lang.r.top + 4, lang.r.right, lang.r.bottom}, Label(), color);
    glyphs.Stroke(c, K::Path::chevron, {lang.r.right - 6 - 16, lang.r.top + 6, lang.r.right - 6, lang.r.top + 22}, 24, 1.8f, color);
    glyphs.Stroke(c, K::Path::close, {close.r.left + 6, close.r.top + 6, close.r.left + 22, close.r.top + 22}, 24, 1.8f, color);
}

// ---- footer buttons ---------------------------------------------------------------------------------------
// h 32, radius 6, padding-inline 12, label 14/20 white (500 on boards 1 and 6, 600 on the others).
inline constexpr float ButtonHeight = 32, ButtonPad = 12, ButtonRadius = 6;
inline float ButtonWidth(Chrome& c, const std::wstring& label, int weight) { return K::Width(c, label, {14, 20, weight, 0}) + 2 * ButtonPad; }
// `fill`: the accent's colour (accent) — the raised token otherwise. `glow` hover 0..1, `press` 0 / 1.
inline void FooterButton(Chrome& c, D2D1_RECT_F r, const std::wstring& label, unsigned fill, bool accent, int weight, float glow, float press, bool enabled, bool ring, float alpha) {
    const float a = alpha * (enabled ? 1.f : K::Token::disabled);
    if (accent) { const auto base = K::FromHex(fill); const auto col = Design::Accent::StateColor({base.r / 255.f, base.g / 255.f, base.b / 255.f}, glow, press); K::Fill(c, r, ButtonRadius, D2D1::ColorF(col.r, col.g, col.b, a)); }
    else { const auto col = K::Lerp(K::Lerp(K::FromHex(K::Token::raised), K::FromHex(K::Token::hover), glow), K::FromHex(K::Token::pressed), press); K::Fill(c, r, ButtonRadius, K::Col(col, a)); }
    K::Text(c, label, {r.left, r.top + 6, r.right, r.bottom}, {14, 20, weight, 0}, K::Col(K::Token::text, a), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (ring) PopupKit::InsetRing(c, r, ButtonRadius);
}

// ---- shader nodes -----------------------------------------------------------------------------------------
// Renders `layer` for the target's pixel density (`w` × `h` CSS pixels of the node). A still frame is kept until
// the target, its size or `key` changes; a loop draws again when `due`.
inline void Shade(Onboarding::Shaders::Renderer& gpu, Onboarding::Shaders::Layer& layer, ID2D1RenderTarget* t, Onboarding::Shaders::Kind kind, const Onboarding::Shaders::Uniforms& u,
                  float w, float h, bool loops, bool due, float key, Motion::Time now) {
    const auto px = t->GetPixelSize(); const auto dip = t->GetSize(); const float scale = px.width / std::max(1.f, dip.width);
    const unsigned ow = static_cast<unsigned>(std::lround(w * scale)), oh = static_cast<unsigned>(std::lround(h * scale));
    const bool current = layer.bitmap && layer.owner == t && layer.frame.width == ow && layer.frame.height == oh;
    if (current && key == layer.key && (!loops || !due)) return;
    layer.key = key;
    if (gpu.Render(kind, u, w, h, ow, oh, layer.frame)) { layer.Upload(t); layer.shaded = now; } else if (!current) layer.Drop();   // the whole field slides: every pixel is new
}

// ---- board 6's gradient -----------------------------------------------------------------------------------
// GrainGradient 725 × 407 at (−2, −2) over #DEFDFF (Paper 1CJ9-0; the annotation onboarding's 1MNL-0 is the same node).
inline constexpr float GrainW = 725, GrainH = 407;
inline D2D1_RECT_F GrainBox() { return {-2, -2, -2 + GrainW, -2 + GrainH}; }
inline Onboarding::Shaders::Uniforms GrainUniforms(double seconds, const Profiles::Profile& p) {
    namespace S = Onboarding::Shaders;
    S::Uniforms u = S::GrainGradient(S::GrainStillSeconds + static_cast<float>(std::fmod(seconds, S::GrainPeriod)));
    const unsigned paper[3] = {0x0AF4FF, 0x0099A0, 0x01292C};   // the board's colours, at the profile's hue
    for (int i = 0; i < 3; ++i) { const auto c = K::Rehue(paper[i], p); u.colors[i][0] = c.r / 255.f; u.colors[i][1] = c.g / 255.f; u.colors[i][2] = c.b / 255.f; }
    return u;
}
// The veil under the content (1BZ7-0 / 1MNK-0): bottom 1, height 330 → y 249 … 579, black 0 % at 0.3 % to black at 47.2 %.
inline constexpr float GrainVeilTop = 249, GrainVeilFrom = .003f, GrainVeilTo = .472f;

// Board 6's dither (src/onboarding-dither.h): rastered on the CPU at the device's pixels, the wave's period from
// the tuning row (8 s a pass); 15 pictures a second are enough for it.
class TrameLayer {
    ID2D1Bitmap* bitmap_ = nullptr; ID2D1RenderTarget* owner_ = nullptr; std::vector<std::uint32_t> bits_; Motion::Time at_{}; double key_ = -1;
    static float Ms(Motion::Time from, Motion::Time to) { return static_cast<float>(std::chrono::duration<double, std::milli>(to - from).count()); }
public:
    TrameLayer() = default; TrameLayer(const TrameLayer&) = delete; TrameLayer& operator=(const TrameLayer&) = delete;
    ~TrameLayer() { Drop(); }
    void Drop() { if (bitmap_) { bitmap_->Release(); bitmap_ = nullptr; } owner_ = nullptr; key_ = -1; }
    static Onboarding::Trame::Look LookOf(const Onboarding::Tuning::Row& r) { namespace T = Onboarding::Tuning; Onboarding::Trame::Look l; l.opacity = r[T::TrameOpacity]; l.cell = r[T::TramePitch]; l.base = r[T::TrameBase]; l.wave = r[T::TrameWave]; return l; }
    void Shade(ID2D1RenderTarget* t, double seconds, const Onboarding::Tuning::Row& row, Motion::Time now) {
        namespace T = Onboarding::Tuning; namespace D = NativeChrome::Annotations::Dither;
        const auto px = t->GetPixelSize(); const auto dip = t->GetSize(); const float scale = px.width / std::max(1.f, dip.width);
        const int w = static_cast<int>(std::lround(GrainW * scale)), h = static_cast<int>(std::lround(GrainH * scale));
        const auto look = LookOf(row); const double phase = Onboarding::LoopPhase(seconds, row[T::TramePeriod]);
        const double key = phase * 1000 + look.opacity * 7 + look.cell * 13 + look.base * 17 + look.wave * 19 + w;
        const bool current = bitmap_ && owner_ == t && static_cast<int>(bitmap_->GetPixelSize().width) == w && static_cast<int>(bitmap_->GetPixelSize().height) == h;
        if (current && (key == key_ || (at_ != Motion::Time{} && Ms(at_, now) < 1000.f / 15 - 3))) return;   // the wave takes 8 s to pass: 15 pictures a second
        bits_.resize(static_cast<std::size_t>(w) * h);
        Onboarding::Trame::Raster(D::Raster{bits_.data(), w, w, h}, scale, phase, look);
        if (!current) { Drop(); if (FAILED(t->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)), bits_.data(), static_cast<UINT32>(w) * 4, D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96), &bitmap_))) { bitmap_ = nullptr; return; } owner_ = t; }
        else bitmap_->CopyFromMemory(nullptr, bits_.data(), static_cast<UINT32>(w) * 4);
        key_ = key; at_ = now;
    }
    ID2D1Bitmap* Bitmap(ID2D1RenderTarget* t) const { return bitmap_ && owner_ == t ? bitmap_ : nullptr; }
};
// The node: its #DEFDFF background, the shader's frame (or the plain three-stop gradient while there is none), the
// dither on top. Everything else of the board is drawn over it.
inline void GrainBackdrop(Chrome& c, const Onboarding::Shaders::Layer& grain, const TrameLayer& trame, const Profiles::Profile& p, float alpha) {
    auto* t = c.RenderTarget(); if (!t || alpha <= 0) return;
    const D2D1_RECT_F box = GrainBox();
    K::Fill(c, box, 0, K::Col(K::Rehue(0xDEFDFF, p), alpha));
    if (grain.bitmap && grain.owner == t) t->DrawBitmap(grain.bitmap, box, alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else K::Gradient(c, box, 0, {0, box.top}, {0, box.bottom}, {{.2f, K::Rehue(0x0AF4FF, p), 0}, {.6f, K::Rehue(0x0099A0, p), 1}, {1, K::Rehue(0x01292C, p), 1}}, alpha);
    if (auto* b = trame.Bitmap(t)) t->DrawBitmap(b, box, alpha, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);   // the band's dither over the gradient, under everything else
}

// ---- the note card of the illustrations ------------------------------------------------------------------
// 305 wide at (nx, ny), #171717, radius 16, padding 12, gap 12: a text block `textH` high (drawn by the caller at
// (nx + 12, ny + 12), 281 wide), then the footer row (28): plus 14 at 50 %, shield 13, Claude 14 + « Opus 5.5 » 12/16 500 +
// « Moyen » 12/16 400 at 50 %, send 28 × 28 in the accent (radius 8, arrow 14). `sendAlpha`: the send button's own
// opacity (the tutorial's A3 dims it while nothing is typed). `ring`: the send button's highlight, under it (the tutorial's
// A3, Paper 1S33-0 « box-shadow: 0 0 0 3px #0AF4FF59 »: 3 outside the button, radius 11); none by default (board 6).
// Returns the card's height.
inline float DemoNote(Chrome& c, K::Glyphs& glyphs, float nx, float ny, float textH, K::Rgb8 accent, float alpha, float sendAlpha = 1, D2D1_COLOR_F ring = {0, 0, 0, 0}) {
    auto box = [](float x, float y, float w, float h) { return D2D1_RECT_F{x, y, x + w, y + h}; };
    const float height = 12 + textH + 12 + 28 + 12;
    K::Fill(c, box(nx, ny, 305, height), 16, K::Col(K::Token::canvas, alpha));
    const float fy = ny + 12 + textH + 12;   // the footer row, 28 high
    glyphs.Fill(c, K::Path::plus, box(nx + 12 + 5, fy + 2 + 5, 14, 14), 256, PopupKit::White(.5f * alpha));
    glyphs.Fill(c, K::Path::shield, box(nx + 12 + 24 + 6, fy + 2 + 5.5f, 13, 13), 256, PopupKit::White(.45f * alpha));
    const K::Font model{12, 16, 500, 0}, effort{12, 16, 400, 0}; const std::wstring name = L"Opus 5.5", level = TR(onboarding_demo_effort);   // i18n-exempt: a model name in the illustration
    const float wn = K::Width(c, name, model), wl = K::Width(c, level, effort), chipW = 6 + 14 + 4 + wn + 4 + wl + 6;
    const float send = nx + 305 - 12 - 28, chip = send - 4 - chipW;
    glyphs.Fill(c, K::Path::claude, box(chip + 6, fy + 2 + 5, 14, 14), 18.666f, K::Col(0xD97757, alpha), .667f, 1.6f);
    K::Text(c, name, {chip + 24, fy + 5, chip + 24 + wn + 8, fy + 21}, model, PopupKit::White(.9f * alpha));       // 1CIF-0 / 1CIH-0: measured one row above the 12/16 rule
    K::Text(c, level, {chip + 28 + wn, fy + 5, chip + 28 + wn + wl + 8, fy + 21}, effort, PopupKit::White(.5f * alpha));
    if (ring.a > 0) K::Border(c, box(send - 3, fy - 3, 34, 34), 11, 3, ring);
    K::Fill(c, box(send, fy, 28, 28), 8, K::Col(accent, alpha * sendAlpha));
    glyphs.Fill(c, K::Path::arrowUp, box(send + 7, fy + 7, 14, 14), 256, K::Col(K::Token::text, alpha * sendAlpha));
    return height;
}
// Where DemoNote's send button is (hit testing).
inline D2D1_RECT_F DemoNoteSend(float nx, float ny, float textH) { const float fy = ny + 12 + textH + 12, send = nx + 305 - 12 - 28; return {send, fy, send + 28, fy + 28}; }

// ---- board 6's illustration -------------------------------------------------------------------------------
// 1CHL-0 at (81, 155) — the annotation onboarding's A1 1MLY-0 is the same group at the same place: the picked
// button of a site, its selection frame and labels, the crosshair, the note with its footer row.
inline void AnnotationDemo(Chrome& c, K::Glyphs& glyphs, K::Rgb8 accent, float alpha) {
    auto* t = c.RenderTarget(); auto* b = c.SolidBrush(); if (!t || !b || alpha <= 0) return;
    auto box = [](float x, float y, float w, float h) { return D2D1_RECT_F{x, y, x + w, y + h}; };
    const float x = 81, y = 155;
    K::Fill(c, box(x, y + 24, 240, 44), 8, K::Col(0x1C1C1C, alpha));
    K::Text(c, TR(onboarding_demo_pay), {x, y + 36, x + 240, y + 56}, {15, 20, 600, 0}, K::Col(K::Token::text, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    const D2D1_RECT_F frame = box(x - 4, y + 20, 248, 52);
    K::Fill(c, frame, 0, K::Col(0x3E67FF, (0x14 / 255.f) * alpha)); K::Border(c, frame, 0, 2, K::Col(accent, alpha));
    auto tag = [&](const std::wstring& text, float left, bool fromRight) {
        const K::Font f{13, 18, 400, 0}; const float w = K::Width(c, text, f) + 12, l = fromRight ? left - w : left;
        K::Fill(c, box(l, y, w, 20), 3, K::Col(accent, alpha)); K::Fill(c, box(l, y + 10, w, 10), 0, K::Col(accent, alpha));   // radius 3 on the top corners only
        K::Text(c, text, {l + 6, y + 1, l + w + 8, y + 19}, f, PopupKit::White(.9f * alpha));
    };
    tag(L"button.btn-primary", x - 4, false);   // i18n-exempt: a CSS selector in the illustration
    tag(L"240x64", x + 240 + 4, true);          // i18n-exempt: a size in the illustration
    const D2D1_RECT_F cross = box(x + 196, y + 30, 32, 32);
    glyphs.Stroke(c, K::Path::cross, cross, 32, 4, K::Col(K::Token::text, alpha)); glyphs.Stroke(c, K::Path::cross, cross, 32, 1.6f, K::Col(0x1C1C1C, alpha));
    b->SetColor(K::Col(accent, alpha)); t->FillEllipse(D2D1::Ellipse({cross.left + 16, cross.top + 16}, 2.6f, 2.6f), b);
    b->SetColor(K::Col(K::Token::text, alpha)); t->DrawEllipse(D2D1::Ellipse({cross.left + 16, cross.top + 16}, 2.6f, 2.6f), b, 1.2f);
    const float nx = x + 252, ny = y + 20;
    DemoNote(c, glyphs, nx, ny, 40, accent, alpha);
    K::Text(c, TR(onboarding_demo_note), {nx + 12, ny + 12, nx + 293, ny + 52}, {14, 20, 400, 0}, PopupKit::White(.9f * alpha), DWRITE_TEXT_ALIGNMENT_LEADING, true);
}

} // namespace OnboardingParts
} // namespace NativeChrome
