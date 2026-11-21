#pragma once
#include "i18n.h"
// « Mode Annotation » band under the toolbar + the toolbar « Annoter » button state.
// Paper (file 01M6Q7YT14ZKMGET54ASPNGQEC, p-H-0, « Bandeau — Mode Annotation », 1.1 / 1.8):
//   height 32; background linear-gradient(180deg, #3E67FF00 -> #233FD6 at 32 %) over the
//   canvas #171717; bottom border 1 #3E67FF3D; padding 0 8 0 12; gap 12.
//   Left: the mark (Paper renders it through a « Heatmap » shader 31x29: natively the same
//   glyph, 18x17, filled with the shader's #C0DAFF -> #2F63E7 ramp) + 6 + « Mode Annotation »
//   Inter Display 14/20 #5B84FF + 6 + « BETA » chip (h 18, radius 3, accent 16 %, padding 5,
//   12/20 medium #5B84FF -> #6A8FFF for 4.5:1, see AccentTone::Paper::beta).
//   Right: [1.8 reminder: h 22, radius 6, #FFFFFF14, padding 8, gap 6: « Aucun agent
//   connecté » 12/16 #FFFFFFCC + « Connecter » 12/16 medium white, underline 1 px offset 3]
//   + 12 + « Quitter » button h 24, radius 3, padding 8/4, gap 8: 13/16 medium #FFFFFFE6 and
//   key cap « Échap » (h 18, radius 3, padding 5, #FFFFFF14, 11/14 regular #FFFFFFB3).
// Toolbar « Annoter » (Paper W09-0, active): 24x24 radius 6, split tint (accent 16 %), mark
// 13x12.25 accent, an accent bar 10x2 radius 1 at x+7, 2 DIP under the button.
// The band is a child layered window of the browser window (above the WebView), created
// at the first Show. V3 (« Spéc · Entrée du mode », Paper 5.2): it slides from -32 DIP to 0 with a fade in
// 220 ms cubic-bezier(.32,.72,0,1) and goes back up in 160 ms; every move starts from the presented value
// (interruptible); reduced motion: a 150 ms fade only. The page moves by 32 DIP at once; what the sliding band
// uncovers is the chrome's own canvas (the chrome paints down to ContentTop), the same #171717 as the band.
// V3-B (Paper 5.2 « Bandeau vivant », validated): a dithered wave lives in the band's background
// (src/annotation-dither.h), drawn under the content: the base texture everywhere, the wave (band + travelling
// envelope, 4 s linear loop) faded to 0 over 120 DIP before « Mode Annotation », before « Quitter / Échap » and under
// the reminder while it shows (zones from the real label rectangles). It starts 220 ms after the entry (the band has
// landed) and grows in over 400 ms; 30 frames/s at most; its clock stops while the window is minimised, hidden or in
// the background, and at the exit: zero wake-up outside the mode. Reduced motion: the base texture, still.
// Profile colour (design/annotation-profile-color-plan.md): every blue above is a tone of the reference accent #3E67FF;
// the band paints the tones of the window's accent instead (src/annotation-accent.h: same OKLCH move, text tones
// floored at 4.5:1 on their measured backdrop). The neutral « Personnel » profile keeps the exact Paper values.
#include "annotation-kit.h"
#include "annotation-dither.h"
#include "annotation-accent.h"

namespace NativeChrome { namespace Annotations {
namespace BannerSpec {
inline constexpr float height = 32, padLeft = 12, padRight = 8, logoW = 31, logoH = 29, logoGap = 6, gap = 12;
inline constexpr float markW = 18, markH = 17;          // Paper WO9-0 mark size inside the 31x29 slot
inline constexpr float quitH = 24, quitPadL = 8, quitPadR = 4, quitGap = 8, quitRadius = 3;
inline constexpr float remindH = 22, remindPad = 8, remindGap = 6, remindRadius = 6;   // Paper 1.8 (10SY-0)
// Colours: AccentTone::Paper (label #5B84FF, border #3E67FF3D, gradient end #233FD6) carried to the window's accent.
// « BETA » on its accent-16 % chip: Paper #5B84FF measures 4.14:1 there (12 px needs 4.5:1), so it is
// lifted to #6A8FFF (4.66:1); « Mode Annotation » keeps #5B84FF on the band (4.87:1).
inline constexpr float gradientAlpha = AccentTone::Paper::gradientAlpha;
// R10 (Paper 1AYJ-0 « Masque » in 1AYH-0): over the dither and the band's gradient, under the content, a layer of the
// canvas (#171717) at opacity 0.85 whose alpha ramps from 0 at 60.66 % of the band's width to full at 82.29 % (oklab
// between the same colour = a linear alpha ramp): the wave fades out before the switch and « Quitter ».
inline constexpr float maskFrom = .6066f, maskTo = .8229f, maskOpacity = .85f;
inline float MaskAlpha(float x, float w) { if (w <= 0) return 0; const float t = (x / w - maskFrom) / (maskTo - maskFrom); return maskOpacity * (t <= 0 ? 0.f : t >= 1 ? 1.f : t); }
}
// BannerChat: the « Panneau » half of the layout switch, BannerComment its « Commentaire » half (chat intégré, local sites
// only; Paper 1AYP-0: they replace the former « Demander à l'agent » button).
enum BannerHit { BannerNone = 0, BannerRemind = 1, BannerQuit = 2, BannerChat = 3, BannerComment = 4 };
namespace SwitchSpec { inline constexpr float pad = 4, seg = 24, gap = 2, radius = 6, icon = 14; }   // Paper 1AYP-0
struct BannerVisual {
    Motion::InteractionVisual quit, remind, chat, comment; bool focus = false, keyboard = false, reduced = false;
    bool chatShown = false, chatActive = false;   // chat intégré: the layout switch exists / « Commentaire » is the chosen layout
    int focusItem = BannerQuit; bool beta = true; float reminder = 0;   // reminder: 0 hidden .. 1 shown (fade)
    bool texture = false; double waveSeconds = 0; float waveAmp = 0;     // V3-B: the dithered background (off in the static boards)
    std::uint32_t accent = AccentTone::reference;                        // the window's accent (0xRRGGBB) as presented
};
struct BannerLayout { D2D1_RECT_F logo{}, label{}, beta{}, quit{}, remind{}, chat{}, comment{}, sw{}; float keycapX = 0; };
inline BannerLayout LayoutBanner(Ink& ink, float w, bool beta = true, bool chat = false) {
    using namespace BannerSpec; BannerLayout l;
    l.logo = {padLeft, (height - logoH) / 2, padLeft + logoW, (height + logoH) / 2};
    l.label = {l.logo.right + logoGap, 6, l.logo.right + logoGap + ink.Width(TR(settings_group_annotation_mode), F(14, 20)), 26};
    if (beta) { const float bw = ink.Width(L"BETA", F(12, 20, 500)) + 10; l.beta = {l.label.right + logoGap, (height - 18) / 2, l.label.right + logoGap + bw, (height + 18) / 2}; }
    const float quitW = quitPadL + ink.Width(TR(engine_quit), F(13, 16, 500)) + quitGap + KeycapWidth(ink, TR(key_escape), 400) + quitPadR;
    l.quit = {w - padRight - quitW, (height - quitH) / 2, w - padRight, (height + quitH) / 2};
    l.keycapX = l.quit.right - quitPadR - KeycapWidth(ink, TR(key_escape), 400);
    const float rw = remindPad + ink.Width(TR(annotation_no_agent_connected), F(12, 16)) + remindGap + ink.Width(TR(agent_connect), F(12, 16, 500)) + remindPad;
    float edge = l.quit.left;   // the reminder sits left of « Quitter », or of the chat button when there is one
    if (chat) {   // the switch: padding 4, two 24 px segments 2 apart (Commentaire, Panneau), left of « Quitter »
        namespace W = SwitchSpec; const float sw = W::pad + W::seg + W::gap + W::seg + W::pad, top = (height - W::seg) / 2;
        l.sw = {l.quit.left - BannerSpec::gap - sw, 0, l.quit.left - BannerSpec::gap, height};
        l.comment = {l.sw.left + W::pad, top, l.sw.left + W::pad + W::seg, top + W::seg}; l.chat = {l.comment.right + W::gap, top, l.comment.right + W::gap + W::seg, top + W::seg};
        edge = l.sw.left;
    }
    l.remind = {edge - gap - rw, (height - remindH) / 2, edge - gap, (height + remindH) / 2};
    return l;
}
// The text-free span of the band for the wave (V3-B): from the end of « Mode Annotation » (+ BETA) to the start of
// « Quitter » — or of the reminder, as it fades in (reminder 0..1).
inline Dither::Zones WaveZones(const BannerLayout& l, bool beta, float reminder) {
    Dither::Zones z; z.left = std::max(l.label.right, beta ? l.beta.right : 0.f);
    const float edge = l.sw.right > l.sw.left ? l.sw.left : l.quit.left;
    z.right = edge + (l.remind.left - edge) * Motion::Clamp(reminder);
    return z;
}
// The texture as a bitmap at the target's pixel density (dots snapped to device pixels), drawn at (0, 0).
inline int PaintBandTexture(Ink& ink, float w, float h, double seconds, float amp, const Dither::Zones& z, const AccentTone::Band& tones = AccentTone::Band{}) {
    auto* t = ink.Target(); float dx = 96, dy = 96; t->GetDpi(&dx, &dy); const float scale = dx / 96.f;
    const int pw = std::max(1, int(std::lround(w * scale))), ph = std::max(1, int(std::lround(h * scale)));
    static thread_local std::vector<std::uint32_t> buffer; buffer.assign(size_t(pw) * ph, 0);
    const int dots = Dither::RasterBand({buffer.data(), pw, pw, ph}, scale, seconds, amp, z, tones.waveDim, tones.waveBright);
    ID2D1Bitmap* bitmap = nullptr;
    const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);
    if (SUCCEEDED(t->CreateBitmap(D2D1::SizeU(UINT32(pw), UINT32(ph)), buffer.data(), UINT32(pw * 4), props, &bitmap)))
        t->DrawBitmap(bitmap, D2D1::RectF(0, 0, pw / scale, ph / scale), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    Release(bitmap); return dots;
}
// V3-B: the wave span's pixels without Direct2D, the same composition as PaintBanner there: the canvas, the band's
// vertical gradient, the dots (the bitmap PaintBandTexture draws 1:1), the bottom border over them. `d` = the device
// rect to write (inside the text-free span: no text, no chip, no focus ring can be there); w / h = the band (DIP),
// k = px per DIP. Equal to the Direct2D render within 2 levels (harness: v3b span vs full frame).
inline void RasterBandSpan(std::uint32_t* px, int stride, RECT d, float w, float h, float k, double seconds, float amp, const Dither::Zones& z, const AccentTone::Band& tones = AccentTone::Band{}) {
    using namespace BannerSpec;
    const float cr = 0x17 / 255.f, cg = 0x17 / 255.f, cb = 0x17 / 255.f;
    const float er = ((tones.gradientEnd >> 16) & 255) / 255.f, eg = ((tones.gradientEnd >> 8) & 255) / 255.f, eb = (tones.gradientEnd & 255) / 255.f;
    const std::uint32_t border = tones.border;
    const int pw = std::max(1, int(std::lround(w * k))), ph = std::max(1, int(std::lround((h - 1) * k)));   // the texture bitmap (PaintBandTexture)
    const float texW = pw / k, texH = ph / k; const double phase = Dither::Phase(seconds);
    const int cp = Dither::CellPx(Dither::Wave::cell, k), dot = cp - Dither::GapPx(k);
    const std::uint32_t dim = Dither::Premul(tones.waveDim, Dither::Wave::opacity), bright = Dither::Premul(tones.waveBright, Dither::Wave::opacity);
    const float borderTop = (h - 1) * k, borderBottom = h * k;
    // one field evaluation per cell (not per dot pixel): the dot colours of the current cell row, 0 = no dot
    const int c0 = int(d.left) / cp, c1 = std::min(pw - 1, int(d.right) - 1) / cp; static thread_local std::vector<std::uint32_t> cells; int cellRow = -1;
    cells.assign(size_t(std::max(0, c1 - c0 + 1)), 0u);
    for (int y = d.top; y < d.bottom; ++y) {
        const float a = gradientAlpha * Motion::Clamp(((y + .5f) / k) / h);
        const float r = cr + (er - cr) * a, g = cg + (eg - cg) * a, b = cb + (eb - cb) * a;
        const std::uint32_t bg = 0xFF000000u | (std::uint32_t(std::lround(r * 255)) << 16) | (std::uint32_t(std::lround(g * 255)) << 8) | std::uint32_t(std::lround(b * 255));
        const float cov = std::max(0.f, std::min(float(y + 1), borderBottom) - std::max(float(y), borderTop));
        const std::uint32_t edge = cov > 0 ? Dither::Premul(border, cov) : 0;
        std::uint32_t* row = px + size_t(y) * stride; const int cy = y / cp; const bool dotRow = y < ph && (y - cy * cp) < dot;
        if (dotRow && cy != cellRow) {
            cellRow = cy;
            for (int cx = c0; cx <= c1; ++cx) {
                const float v = Dither::BandField((cx + .5f) * cp / k, (cy + .5f) * cp / k, texW, texH, phase, amp, z);
                cells[size_t(cx - c0)] = v > Dither::Threshold(cx, cy) ? (v < .75f ? dim : bright) : 0u;
            }
        }
        for (int x = d.left; x < d.right; ++x) {
            std::uint32_t c = bg;
            if (dotRow && x < pw) {
                const int cx = x / cp;
                if (x - cx * cp < dot && cells[size_t(cx - c0)]) c = Dither::Over(cells[size_t(cx - c0)], c);
            }
            if (edge) c = Dither::Over(edge, c);
            if (const float m = MaskAlpha((x + .5f) / k, w); m > 0) c = Dither::Over(Dither::Premul(0x171717FFu, m), c);   // R10: the mask over all of it
            row[x] = c;
        }
    }
}
inline int BannerHitAt(const BannerLayout& l, bool reminder, float x, float y) {
    if (Inside(l.quit, x, y)) return BannerQuit;
    if (l.chat.right > l.chat.left && Inside(l.chat, x, y)) return BannerChat;
    if (l.comment.right > l.comment.left && Inside(l.comment, x, y)) return BannerComment;
    if (reminder && Inside(l.remind, x, y)) return BannerRemind;
    return BannerNone;
}
inline void PaintBanner(Ink& ink, float w, float h, const BannerVisual& v, const std::function<void()>& backdrop = {}) {
    using namespace BannerSpec;
    auto* t = ink.Target(); const auto l = LayoutBanner(ink, w, v.beta, v.chatShown);
    const auto& tone = AccentTone::BandCached(v.accent);
    auto rgb = [](std::uint32_t c, float a = 1) { return D2D1_COLOR_F{((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, a}; };
    ink.Fill({0, 0, w, h}, 0, Hex(Ink_::canvas));
    const auto end = rgb(tone.gradientEnd);
    ID2D1GradientStopCollection* stops = nullptr; ID2D1LinearGradientBrush* gradient = nullptr;
    const D2D1_GRADIENT_STOP s[2] = {{0, {end.r, end.g, end.b, 0}}, {1, {end.r, end.g, end.b, gradientAlpha}}};
    if (SUCCEEDED(t->CreateGradientStopCollection(s, 2, &stops)) && SUCCEEDED(t->CreateLinearGradientBrush({{0, 0}, {0, h}}, stops, &gradient))) t->FillRectangle({0, 0, w, h}, gradient);
    Release(gradient); Release(stops);
    if (v.texture) PaintBandTexture(ink, w, h - 1, v.waveSeconds, v.waveAmp, WaveZones(l, v.beta, v.reminder), tone);
    if (backdrop) { t->PushAxisAlignedClip({0, 0, w, h}, D2D1_ANTIALIAS_MODE_ALIASED); backdrop(); t->PopAxisAlignedClip(); }
    ink.Fill({0, h - 1, w, h}, 0, Hex(tone.border));
    {   // R10: Paper's mask (1AYJ-0) over the dither, the gradient and the border, under the content. One row of device
        // pixels from MaskAlpha (the same values RasterBandSpan writes), stretched to the band's height.
        float dx = 96, dy = 96; t->GetDpi(&dx, &dy); const float s = dx / 96.f; const int pw = std::max(1, int(std::lround(w * s)));
        static thread_local std::vector<std::uint32_t> row; row.assign(size_t(pw), 0u);
        for (int x = 0; x < pw; ++x) if (const float m = MaskAlpha((x + .5f) / s, w); m > 0) row[size_t(x)] = Dither::Premul(0x171717FFu, m);
        ID2D1Bitmap* mask = nullptr; const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dx, dy);
        if (SUCCEEDED(t->CreateBitmap(D2D1::SizeU(UINT32(pw), 1), row.data(), UINT32(pw * 4), props, &mask))) t->DrawBitmap(mask, D2D1::RectF(0, 0, pw / s, h), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        Release(mask);
    }
    {   // Mark with the shader's ramp.
        const D2D1_RECT_F m{(l.logo.left + l.logo.right - markW) / 2, (l.logo.top + l.logo.bottom - markH) / 2, (l.logo.left + l.logo.right + markW) / 2, (l.logo.top + l.logo.bottom + markH) / 2};
        const D2D1_GRADIENT_STOP ramp[3] = {{0, rgb(tone.markLight)}, {.55f, rgb(tone.markMid)}, {1, rgb(tone.markDeep)}};
        if (SUCCEEDED(t->CreateGradientStopCollection(ramp, 3, &stops)) && SUCCEEDED(t->CreateLinearGradientBrush({{m.left, m.top}, {m.right, m.bottom}}, stops, &gradient))) ink.IconBrush(Glyphs::mark, m, gradient);
        Release(gradient); Release(stops);
    }
    ink.Text(TR(settings_group_annotation_mode), F(14, 20), {l.label.left, 0, l.label.right + 40, h}, rgb(tone.label), false, Align::Leading, true);
    if (v.beta) { ink.Fill(l.beta, 3, rgb(tone.fill, AccentTone::Paper::betaChip)); ink.Text(L"BETA", F(12, 20, 500), l.beta, rgb(tone.beta), false, Align::Center, true); }
    if (v.reminder > 0) {   // « Aucun agent connecté · Connecter » (1.8): fades in, press 0.96
        ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(v.reminder, group);
        {
            ScaleScope scale(t, l.remind, Motion::PressScale(v.remind.press, v.reduced));
            ink.Fill(l.remind, remindRadius, {1, 1, 1, (20 / 255.f) + .04f * v.remind.hover + .03f * v.remind.press});
            Piece connect = PText(TR(agent_connect), F(12, 16, 500), {1, 1, 1, 1}); connect.underline = true;
            Pieces(ink, {PText(TR(annotation_no_agent_connected), F(12, 16), Hex(0xFFFFFFCC), remindGap), connect}, l.remind);
            if (v.focus && v.focusItem == BannerRemind) FocusRing(ink, l.remind, remindRadius, v.keyboard);
        }
        if (pushed) ink.PopOpacity(group);
    }
    if (v.chatShown) {   // Paper 1AYP-0: the chosen half on white 10 % (radius 6), its icon at 80 %; the other at 50 %
        namespace W = SwitchSpec;
        auto seg = [&](const D2D1_RECT_F& r, const Glyph& g, float degrees, bool chosen, const Motion::InteractionVisual& iv, int item) {
            ScaleScope scale(t, r, Motion::PressScale(iv.press, v.reduced));
            const float plate = chosen ? .10f : .06f * iv.hover + .04f * iv.press;
            if (plate > 0) ink.Fill(r, W::radius, {1, 1, 1, plate});
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            ink.Icon(g, {cx - W::icon / 2, cy - W::icon / 2, cx + W::icon / 2, cy + W::icon / 2}, {1, 1, 1, chosen ? .8f : .5f + .3f * iv.hover}, degrees);
            if (v.focus && v.focusItem == item) FocusRing(ink, r, W::radius, v.keyboard);
        };
        seg(l.comment, Glyphs::commentBubble, 0, v.chatActive, v.comment, BannerComment);
        seg(l.chat, Glyphs::sidePanel, 180, !v.chatActive, v.chat, BannerChat);
    }
    {   // Quitter + Échap: hover plate, press 0.96.
        ScaleScope scale(t, l.quit, Motion::PressScale(v.quit.press, v.reduced));
        const float plate = .08f * v.quit.hover + .04f * v.quit.press;
        ink.Fill(l.quit, quitRadius, {1, 1, 1, plate});
        ink.Text(TR(engine_quit), F(13, 16, 500), {l.quit.left + quitPadL, l.quit.top, l.keycapX, l.quit.bottom}, Hex(0xFFFFFFE6), false, Align::Leading, true);
        Keycap(ink, TR(key_escape), l.keycapX, (l.quit.top + l.quit.bottom) / 2, Hex(0xFFFFFF14), Hex(0xFFFFFFB3), 400);
        if (v.focus && v.focusItem == BannerQuit) FocusRing(ink, l.quit, quitRadius, v.keyboard);
    }
}

// Toolbar « Annoter » for the browser's own chrome renderer (T3 calls it from its paint pass
// with the Chrome's target, factory and brush). box = the 24x24 button. No allocation per
// frame after the first (geometry cached per factory).
struct ToolbarVisual { bool active = false; float hover = 0, press = 0; bool focus = false, keyboard = false, reduced = false; };
inline void PaintToolbarButton(ID2D1RenderTarget* t, ID2D1Factory* factory, ID2D1SolidColorBrush* brush, D2D1_RECT_F box, const ToolbarVisual& v) {
    if (!t || !factory || !brush) return;
    static std::map<ID2D1Factory*, ID2D1PathGeometry*> cache;
    auto& g = cache[factory]; if (!g) g = DialogKit::SvgGeometry(factory, Glyphs::mark.d);
    D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
    const float s = Motion::PressScale(v.press, v.reduced);
    if (s != 1) t->SetTransform(D2D1::Matrix3x2F::Scale(s, s, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}) * base);
    const auto a = Design::Accent::Get();
    if (v.active) { brush->SetColor(D2D1::ColorF(a.r, a.g, a.b, .16f)); t->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), brush); }
    else if (v.hover > 0 || v.press > 0) { brush->SetColor(D2D1::ColorF(1, 1, 1, (20 / 255.f) * std::max(v.hover, v.press))); t->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), brush); }
    if (g) {
        const float w = 13, h = 12.25f, x = (box.left + box.right - w) / 2, y = (box.top + box.bottom - h) / 2;
        D2D1_MATRIX_3X2_F now; t->GetTransform(&now);
        t->SetTransform(D2D1::Matrix3x2F::Translation(-Glyphs::mark.x, -Glyphs::mark.y) * D2D1::Matrix3x2F::Scale(w / Glyphs::mark.w, h / Glyphs::mark.h) * D2D1::Matrix3x2F::Translation(x, y) * now);
        brush->SetColor(v.active ? D2D1::ColorF(a.r, a.g, a.b, 1) : D2D1::ColorF(1, 1, 1, .5f));
        t->FillGeometry(g, brush); t->SetTransform(now);
    }
    if (v.active) { brush->SetColor(D2D1::ColorF(a.r, a.g, a.b, 1)); t->FillRoundedRectangle(D2D1::RoundedRect({box.left + 7, box.bottom + 2, box.left + 17, box.bottom + 4}, 1, 1), brush); }
    if (v.focus) { brush->SetColor(D2D1::ColorF(1, 1, 1, Design::Focus::Opacity(v.keyboard))); t->DrawRoundedRectangle(D2D1::RoundedRect({box.left + .5f, box.top + .5f, box.right - .5f, box.bottom - .5f}, 5.5f, 5.5f), brush, 1); }
    t->SetTransform(base);
}
// UIA for the toolbar button (T3 adds it to its toolbar nodes).
inline Accessibility::Node ToolbarNode(long long key, long long parent, RECT bounds, bool active) {
    Accessibility::Node n; n.key = key; n.parent = parent; n.role = Accessibility::Role::Button; n.name = TR(a11y_annotate);
    n.accelerator = TR(key_ctrl_shift_a); n.toggle = active ? 2 : 1; n.automationId = L"AnnotateButton"; n.bounds = bounds; return n;
}

struct BannerHooks {
    std::function<void()> exit;                               // « Quitter » / Échap via UIA
    std::function<void()> escape;                             // Échap typed in the band (unset: exit) — an open bubble closes first
    std::function<void()> connect;                            // V2: « Aucun agent connecté · Connecter » (1.8)
    std::function<void()> chat;                               // chat intégré: « Panneau » (the chat panel at the right; opens it)
    std::function<void()> comment;                            // chat intégré: « Commentaire » (a bubble on the element clicked)
    std::function<void(const std::wstring&)> announce;        // optional: the host's announcer
};
namespace BannerMotion { inline constexpr int enter = 220, exit = 160, reduced = 150; inline constexpr float lift = 32; }
inline Timing BannerTiming() { Timing t; t.enter = BannerMotion::enter; t.exit = BannerMotion::exit; t.reducedEnter = t.reducedExit = BannerMotion::reduced; t.from = t.to = 1; t.curve = Curves::band; return t; }
// The band's offset for a presence progress (0 hidden .. 1 in place): slides with its opacity, none when reduced.
// R25: the band's visible height while it slides in (progress 0 -> 1) or out (1 -> 0); the page and the chat panel
// are laid out under it at every frame: they follow without a jump. Reduced motion: no slide (a step).
inline float BandSlideHeight(float progress, float full, bool reduced) { return reduced ? (progress >= .5f ? full : 0.f) : full * Motion::Clamp(progress); }
inline float BannerOffset(float progress, bool reduced) { return reduced ? 0.f : -(1 - Motion::Clamp(progress)) * BannerMotion::lift; }
class Banner : public Animator::Client {
    static constexpr long long RootKey = 1, QuitKey = 2, RemindKey = 3, ChatKey = 4, CommentKey = 5;
    bool chat_ = false, chatActive_ = false;   // the switch is shown / « Commentaire » is chosen
    float hidden_ = 0;                          // R25: DIP of the band still above its visible part while it slides
    static long long KeyOf(int item) { return item == BannerQuit ? QuitKey : item == BannerChat ? ChatKey : item == BannerComment ? CommentKey : RemindKey; }
    static int ItemOf(long long key) { return key == QuitKey ? BannerQuit : key == ChatKey ? BannerChat : key == CommentKey ? BannerComment : BannerRemind; }
    HINSTANCE instance_ = nullptr; HWND parent_ = nullptr; std::wstring root_; BannerHooks hooks_;
    Layer layer_; Presence presence_{BannerTiming()};
    Motion::State motion_; Ramp reminder_; bool reminderOn_ = false, beta_ = true;
    RECT rect_{}; int hover_ = 0, pressed_ = 0, focusItem_ = BannerQuit; bool focus_ = false, keyboard_ = false, reduced_ = false;
    float Width() const { return (rect_.right - rect_.left) / layer_.Scale(); }
    // Cached per width / BETA / scale (V3-B: the wave frames need the label rectangles 30 times a second).
    BannerLayout layoutCache_{}; float layoutW_ = -1, layoutScale_ = 0; bool layoutBeta_ = true, layoutChat_ = false;
    BannerLayout Layout() {
        auto* ink = Ink::Instance(); if (!ink) return BannerLayout{};
        const float w = Width(), sc = layer_.Scale();
        if (w != layoutW_ || beta_ != layoutBeta_ || sc != layoutScale_ || chat_ != layoutChat_) { layoutCache_ = LayoutBanner(*ink, w, beta_, chat_); layoutW_ = w; layoutBeta_ = beta_; layoutScale_ = sc; layoutChat_ = chat_; }
        return layoutCache_;
    }
    void Render(Motion::Time now, bool waveOnly = false) {
        if (!layer_.Window()) return;
        const float opacity = presence_.Progress(now), dy = BannerOffset(opacity, reduced_);
        BannerVisual v; v.quit = motion_.Visual(BannerQuit, now); v.remind = motion_.Visual(BannerRemind, now); v.focus = focus_; v.keyboard = keyboard_;
        v.chat = motion_.Visual(BannerChat, now); v.comment = motion_.Visual(BannerComment, now); v.chatShown = chat_; v.chatActive = chatActive_;
        v.accent = AccentRgb24();   // the window accent as presented (it follows the profile fade)
        v.reduced = reduced_; v.focusItem = focusItem_; v.beta = beta_; v.reminder = Motion::Clamp(reminder_.Value(now));
        v.texture = true; v.waveSeconds = WaveSeconds(now); v.waveAmp = Dither::Amplitude(std::chrono::duration<double, std::milli>(now - enteredAt_).count(), reduced_);
        if (v.waveAmp > 0 && presence_.IsOpen()) ++waveFrames_;
        lastRender_ = now;
        const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count(); auto backdrop = backdrop_; const float hidden = hidden_;
        layer_.paint = [v, opacity, dy, seconds, backdrop, hidden](Ink& ink, float w, float hv) {
            const float h = hv + hidden;   // R25: the band's full height; only its lower part is in the window while it slides
            auto* t = ink.Target(); D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
            t->SetTransform(D2D1::Matrix3x2F::Translation(0, dy - hidden) * base);
            ID2D1Layer* group = nullptr; const bool pushed = ink.PushOpacity(opacity, group);
            PaintBanner(ink, w, h, v, backdrop ? [&] { backdrop(ink, w, h, seconds); } : std::function<void()>());
            if (pushed) ink.PopOpacity(group);
            t->SetTransform(base);
        };
        // V3-B: raised above the page only when shown / moved, never per wave frame (a z-order call 30 times a
        // second made every frame cost a window-position round trip).
        LARGE_INTEGER a{}, b{}, f{}; QueryPerformanceCounter(&a);
        // A wave-only frame (nothing else moves): only the span between the labels changes; its pixels are written in
        // place (no Direct2D) and only they go to the compositor.
        bool done = false;
        if (waveOnly && !raiseNext_ && opacity >= 1 && dy == 0 && hidden_ == 0) {
            const auto z = WaveZones(Layout(), beta_, v.reminder); const float k = layer_.Scale(), w = Width(), h = BannerSpec::height;
            const RECT span{LONG(std::ceil(z.left * k)), 0, LONG(std::floor(z.right * k)), LONG(std::lround(h * k))};
            const auto& tones = AccentTone::BandCached(v.accent);
            if (z.right > z.left) done = layer_.PresentRegion(rect_, span, [&](std::uint32_t* px, int stride, RECT d) { RasterBandSpan(px, stride, d, w, h, k, v.waveSeconds, v.waveAmp, z, tones); });
        }
        if (!done) layer_.Present(rect_, raiseNext_);
        raiseNext_ = false;
        QueryPerformanceCounter(&b); QueryPerformanceFrequency(&f); renderMs_ += double(b.QuadPart - a.QuadPart) * 1000.0 / double(f.QuadPart); ++renders_;
    }
    bool raiseNext_ = true; double renderMs_ = 0; unsigned long long renders_ = 0;
    std::function<void(Ink&, float, float, double)> backdrop_; bool backdropAnimates_ = false;
    // V3-B wave clock: seconds of wave time (paused while the window cannot be seen), 30 frames/s at most.
    Motion::Time enteredAt_{}, waveOrigin_{}, pausedAt_{}, lastRender_{}; bool paused_ = false; unsigned long long waveFrames_ = 0;
    int liveForTest_ = -1;   // -1: the real window state; 0 / 1: forced (harness)
    double WaveSeconds(Motion::Time now) const { return std::chrono::duration<double>((paused_ ? pausedAt_ : now) - waveOrigin_).count(); }
    // Can the wave be seen? The browser window visible, not minimised, and its app in the foreground.
    bool Live() const {
        if (liveForTest_ >= 0) return liveForTest_ == 1;
        HWND root = parent_ ? GetAncestor(parent_, GA_ROOT) : nullptr;
        if (!root || IsIconic(root) || !IsWindowVisible(root) || !layer_.Shown()) return false;
        HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId();
    }
    // The wave wants frames: open, not reduced, past its start, visible.
    bool WaveWanted(Motion::Time now) const { return presence_.IsOpen() && !reduced_ && now >= enteredAt_ + std::chrono::milliseconds(Dither::Wave::startMs) && Live(); }
    void Pause(Motion::Time now) { if (!paused_) { paused_ = true; pausedAt_ = now; } }
    void Unpause(Motion::Time now) { if (paused_) { paused_ = false; waveOrigin_ += now - pausedAt_; } }
    void Interact() { const auto now = Motion::Now(); motion_.SetInteraction(hover_, pressed_ == hover_ ? pressed_ : 0, now); Render(now); Animator::Get().Request(this); }
    void Activate(int item) {
        if (item == BannerQuit) { if (hooks_.exit) hooks_.exit(); }
        else if (item == BannerChat) { if (chat_ && hooks_.chat) hooks_.chat(); }
        else if (item == BannerComment) { if (chat_ && hooks_.comment) hooks_.comment(); }
        else if (item == BannerRemind && reminderOn_ && hooks_.connect) hooks_.connect();
    }
    bool Handle(UINT m, WPARAM w, LPARAM l, LRESULT& r) {
        const float s = 1 / layer_.Scale(); const float x = GET_X_LPARAM(l) * s, y = GET_Y_LPARAM(l) * s;
        switch (m) {
        case WM_MOUSEMOVE: { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, layer_.Window(), 0}; TrackMouseEvent(&t);
            const int hit = BannerHitAt(Layout(), reminderOn_, x, y); if (hit != hover_) { hover_ = hit; Interact(); } r = 0; return true; }
        case WM_MOUSELEAVE: hover_ = 0; Interact(); r = 0; return true;
        case WM_LBUTTONDOWN: pressed_ = BannerHitAt(Layout(), reminderOn_, x, y); if (pressed_) { SetCapture(layer_.Window()); keyboard_ = false; } Interact(); r = 0; return true;
        case WM_LBUTTONUP: { const int was = pressed_, hit = BannerHitAt(Layout(), reminderOn_, x, y); pressed_ = 0; if (GetCapture() == layer_.Window()) ReleaseCapture();
            hover_ = hit; Interact(); if (was && was == hit) Activate(hit); r = 0; return true; }
        case WM_SETFOCUS: focus_ = true; Render(Motion::Now()); return false;
        case WM_KILLFOCUS: focus_ = false; Render(Motion::Now()); return false;
        case WM_GETDLGCODE: r = DLGC_WANTALLKEYS; return true;
        case WM_KEYDOWN:
            keyboard_ = true;
            if (w == VK_TAB && (reminderOn_ || chat_)) {   // reminder -> chat -> Quitter (reading order), Maj+Tab backwards
                std::vector<int> order; if (reminderOn_) order.push_back(BannerRemind); if (chat_) { order.push_back(BannerComment); order.push_back(BannerChat); } order.push_back(BannerQuit);
                auto it = std::find(order.begin(), order.end(), focusItem_); int i = it == order.end() ? 0 : int(it - order.begin()) + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
                focusItem_ = order[size_t((i + int(order.size())) % int(order.size()))]; Render(Motion::Now());
                if (layer_.uia.Listening()) layer_.uia.Raise(KeyOf(focusItem_), UIA_AutomationFocusChangedEventId);
                r = 0; return true;
            }
            if (w == VK_RETURN || w == VK_SPACE) { Activate(focusItem_); r = 0; return true; }
            if (w == VK_ESCAPE) { if (hooks_.escape) hooks_.escape(); else if (hooks_.exit) hooks_.exit(); r = 0; return true; }
            return false;
        case WM_APP + 1: Activate(int(w)); r = 0; return true;
        case WM_TIMER: if (w != WaveTimer) return false;   // V3-B: the next wave frame is due: back to the display clock
            DisarmWave(); if (WaveWanted(Motion::Now())) Animator::Get().Request(this); r = 0; return true;
        }
        return false;
    }
    std::vector<Accessibility::Node> Nodes() {
        std::vector<Accessibility::Node> n; if (!presence_.IsOpen()) return n;
        Accessibility::Node g; g.key = RootKey; g.role = Accessibility::Role::Group; g.name = TR(settings_group_annotation_mode); g.status = reminderOn_ ? TR(annotation_banner_on_no_agent) : TR(annotation_banner_on);
        g.help = TR(annotation_banner_help); g.automationId = L"AnnotationBanner"; g.focusable = false;
        GetWindowRect(layer_.Window(), &g.bounds); n.push_back(g);
        const auto l = Layout();
        if (reminderOn_) {
            Accessibility::Node c; c.key = RemindKey; c.parent = RootKey; c.name = TR(annotation_connect_agent); c.help = TR(annotation_no_agent_connected);
            c.automationId = L"AnnotationConnectReminder"; c.bounds = layer_.Screen(l.remind); n.push_back(c);
        }
        if (chat_) {   // the switch: two toggle buttons, exactly one on
            Accessibility::Node m; m.key = CommentKey; m.parent = RootKey; m.name = TR(chat_layout_comment); m.help = TR(chat_layout_comment_help); m.toggle = chatActive_ ? 2 : 1;
            m.automationId = L"AnnotationLayoutComment"; m.bounds = layer_.Screen(l.comment); n.push_back(m);
            Accessibility::Node c; c.key = ChatKey; c.parent = RootKey; c.name = TR(chat_layout_panel); c.help = TR(chat_layout_panel_help); c.accelerator = TR(key_ctrl_shift_d); c.toggle = chatActive_ ? 1 : 2;
            c.automationId = L"AnnotationAskAgent"; c.bounds = layer_.Screen(l.chat); n.push_back(c);
        }
        Accessibility::Node q; q.key = QuitKey; q.parent = RootKey; q.name = TR(annotation_quit_mode); q.accelerator = TR(key_escape);
        q.automationId = L"AnnotationQuit"; q.bounds = layer_.Screen(l.quit); n.push_back(q);
        return n;
    }
public:
    ~Banner() { DisarmWave(); if (Animator::Exists()) Animator::Get().Forget(this); }
    // Stores the context only: no window, no thread, no allocation of D2D resources.
    void Init(HINSTANCE instance, HWND parent, std::wstring root, BannerHooks hooks) { instance_ = instance; parent_ = parent; root_ = std::move(root); hooks_ = std::move(hooks); }
    bool Created() const { return layer_.Window() != nullptr; }
    HWND Window() const { return layer_.Window(); }
    bool Visible() const { return presence_.IsOpen(); }
    bool ReminderVisible() const { return reminderOn_; }
    // The window's accent changed (profile colour edit, its 120 ms fade, a profile switch): one full repaint with the
    // accent as presented now. No clock: the caller (the profile fade's own timer) calls it once per step.
    void AccentChanged() { if (layer_.Window() && presence_.Visible(Motion::Now())) Render(Motion::Now()); }
    // rect: the band in the parent's client pixels (full width, 32 DIP high, under the toolbar).
    bool Show(RECT rect, bool reduced) {
        if (!Ink::Shared(root_)) return false;
        if (!layer_.Window()) {
            Layer::Options o; o.child = true; o.title = TR(settings_group_annotation_mode); o.tabStop = true;
            if (!layer_.Create(instance_, parent_, o)) return false;
            layer_.handle = [this](UINT m, WPARAM w, LPARAM l, LRESULT& r) { return Handle(m, w, l, r); };
            Accessibility::Host host; host.window = layer_.Window();
            host.nodes = [this] { return Nodes(); };
            host.focused = [this]() -> long long { return focus_ ? KeyOf(focusItem_) : 0; };
            host.invoke = [this](long long key) { PostMessageW(layer_.Window(), WM_APP + 1, WPARAM(ItemOf(key)), 0); };
            layer_.uia.Attach(std::move(host));
            motion_.EnablePolish();
        }
        rect_ = rect; reduced_ = reduced; motion_.SetReducedMotion(reduced); const auto now = Motion::Now();
        const bool was = presence_.IsOpen();
        if (!was) { enteredAt_ = waveOrigin_ = now; paused_ = false; }
        raiseNext_ = true;
        presence_.Show(now, true, reduced); Render(now); Animator::Get().Request(this);
        if (!was) {
            if (hooks_.announce) hooks_.announce(TR(annotation_mode_on));
            else layer_.uia.Notify(RootKey, TR(annotation_mode_on), L"AnnotationModeOn");
        }
        return true;
    }
    void Move(RECT rect) { rect_ = rect; raiseNext_ = true; if (presence_.Visible(Motion::Now())) Render(Motion::Now()); }
    // R25: the band slides under the toolbar: `rect` is its visible part, `hiddenDip` what is still above it (drawn
    // shifted up, laid out at its full height). 0 = in place.
    void Slide(RECT rect, float hiddenDip) { hidden_ = std::max(0.f, hiddenDip); Move(rect); }
    // Chat intégré: the layout switch (shown on local sites only; active = « Commentaire » is the chosen layout).
    void SetChat(bool shown, bool active) {
        if (shown == chat_ && active == chatActive_) return;
        const bool layout = shown != chat_; chat_ = shown; chatActive_ = active;
        if (!chat_ && focusItem_ == BannerChat) focusItem_ = BannerQuit;
        if (layer_.Window()) { Render(Motion::Now()); if (layout && layer_.uia.Listening()) layer_.uia.ChildrenChanged(RootKey); }
    }
    bool ChatShown() const { return chat_; }
    // V2 (1.8): « Aucun agent connecté · Connecter » after « Plus tard »; fades in/out in 120 ms.
    void SetReminder(bool on, bool animate = true) {
        if (on == reminderOn_) return;
        reminderOn_ = on; const auto now = Motion::Now();
        reminder_.Set(on ? 1.f : 0.f, now, animate ? Spec::bannerFade : 0, Curves::out);
        if (!on && focusItem_ == BannerRemind) focusItem_ = BannerQuit;
        if (layer_.Window()) { Render(now); Animator::Get().Request(this); if (layer_.uia.Listening()) layer_.uia.ChildrenChanged(RootKey); }
    }
    void SetBeta(bool beta) { beta_ = beta; if (layer_.Window() && presence_.Visible(Motion::Now())) Render(Motion::Now()); }
    void Hide(bool animate = true) {
        if (!layer_.Window()) return;
        DisarmWave();
        const auto now = Motion::Now();
        presence_.Hide(now, animate, reduced_); hover_ = pressed_ = 0; focus_ = false; motion_.ClearInteractions();
        if (!presence_.Visible(now)) layer_.Hide(); else Animator::Get().Request(this);
    }
    bool Frame(Motion::Time now) override {
        if (!presence_.Visible(now)) { layer_.Hide(); return false; }
        const bool moving = presence_.Moving(now) || reminder_.Moving(now) || motion_.NextWake(now).delay.has_value() || (backdropAnimates_ && presence_.IsOpen());
        const bool waveDue = presence_.IsOpen() && !reduced_ && now < enteredAt_ + std::chrono::milliseconds(Dither::Wave::startMs);   // waiting for 220 ms
        const bool wave = WaveWanted(now);
        if (wave) Unpause(now); else { Pause(now); DisarmWave(); }
        if (moving || !wave) { Render(now); return moving || (waveDue && Live()); }
        // The wave alone: one frame per 1/30 s at most. Between two wave frames the display clock (60-144 Hz) is not
        // kept running: a one-shot timer hands back to it just before the next frame is due (~2 clock ticks per frame).
        const auto period = std::chrono::microseconds(1000000 / Dither::Wave::fps - 1500);
        if (now - lastRender_ >= period) Render(now, true);
        const double untilMs = std::chrono::duration<double, std::milli>(lastRender_ + period - now).count();
        if (untilMs <= 10) return true;   // due within a clock tick or so
        ArmWave(UINT(untilMs - 8)); return false;
    }
    static constexpr UINT_PTR WaveTimer = 41;
    bool waveTimer_ = false; unsigned long long waveTimers_ = 0;
    void ArmWave(UINT ms) { if (layer_.Window() && SetTimer(layer_.Window(), WaveTimer, std::max(1u, ms), nullptr)) { waveTimer_ = true; ++waveTimers_; } }
    void DisarmWave() { if (waveTimer_ && layer_.Window()) KillTimer(layer_.Window(), WaveTimer); waveTimer_ = false; }
    // V3-B: the browser window was minimised / restored / shown / hidden, or its app lost / took the foreground.
    void WindowStateChanged() {
        if (!layer_.Window()) return;
        if (presence_.IsOpen() && !reduced_ && Live()) Animator::Get().Request(this); else { DisarmWave(); Pause(Motion::Now()); }
    }
    unsigned long long WaveTimersForTest() const { return waveTimers_; }
    unsigned long long WaveFramesForTest() const { return waveFrames_; }
    double AverageRenderMsForTest() const { return renders_ ? renderMs_ / double(renders_) : 0; }
    void ResetRenderStatsForTest() { renderMs_ = 0; renders_ = 0; }
    double WaveSecondsForTest(Motion::Time now) const { return WaveSeconds(now); }
    void SetLiveForTest(int live) { liveForTest_ = live; WindowStateChanged(); }
    BannerLayout LayoutForTest() { return Layout(); }
    // Future animated background (see the header comment): paint(ink, w, h, seconds), above the band's
    // gradient and under its content; animates = keep requesting frames while the band is open.
    void SetBackdrop(std::function<void(Ink&, float, float, double)> paint, bool animates) {
        backdrop_ = std::move(paint); backdropAnimates_ = animates && backdrop_;
        if (layer_.Window() && presence_.Visible(Motion::Now())) { Render(Motion::Now()); if (backdropAnimates_) Animator::Get().Request(this); }
    }
    Presence& PresenceForTest() { return presence_; }
    // Tests.
    Layer& LayerForTest() { return layer_; }
    std::vector<Accessibility::Node> NodesForTest() { return Nodes(); }
    bool KeyForTest(WPARAM key) { LRESULT r; return Handle(WM_KEYDOWN, key, 0, r); }
    int FocusItemForTest() const { return focusItem_; }
};
}} // namespace NativeChrome::Annotations
