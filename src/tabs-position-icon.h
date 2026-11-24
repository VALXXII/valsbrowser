#pragma once
// « Position des onglets » icons (#127; Paper « Proposition — Clic droit · 3 icônes segmentées » 1XQ7-0 / 1XQB-0 / 1XQG-0
// and « Réglages › Onglets · 3 icônes » 1XPT-0 / 1XPX-0 / 1XQ2-0). A window (192 x 160 at 32,48 in a 256 viewBox, radius
// 20) with a solid band: across the top (En haut), a wide one on the left with its edge at x 104 (Panneau latéral), a
// narrow one at x 72 (Panneau compact). Solid colours, no transparency (Val): the band is drawn under the outline.
// Stroke 22 at 16 DIP, 20 at 20 DIP. Active: outline #FFFFFF, band #9D9D9D; rest: #BCBCBC / #6E6E6E; hover: #BFBFBF /
// #747474. Shared by the tab menu's segmented row (src/context-menu.h) and Réglages › Onglets (src/settings-panel.h).
#include <d2d1.h>
#include <algorithm>
#include "svg-path.h"

namespace NativeChrome { namespace TabsPositionIcon {
enum class State { Rest, Hover, Active };
inline constexpr const char* Band[3] = {
    "M52 48h152a20 20 0 0 1 20 20v34H32V68a20 20 0 0 1 20-20z",   // En haut
    "M52 48h52v160H52a20 20 0 0 1-20-20V68a20 20 0 0 1 20-20z",   // Panneau latéral
    "M52 48h20v160H52a20 20 0 0 1-20-20V68a20 20 0 0 1 20-20z",   // Panneau compact
};
inline constexpr float Divider[3] = {0, 104, 72};
inline unsigned Outline(State s) { return s == State::Active ? 0xFFFFFF : s == State::Hover ? 0xBFBFBF : 0xBCBCBC; }
inline unsigned Fill(State s) { return s == State::Active ? 0x9D9D9D : s == State::Hover ? 0x747474 : 0x6E6E6E; }
inline float Stroke(float size) { return size >= 20 ? 20.f : 22.f; }
inline D2D1_COLOR_F Rgb(unsigned c) { return D2D1::ColorF(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f, 1); }
// Draws icon `position` (0 top, 1 side, 2 compact) `size` DIP wide centred in `box`.
inline void Paint(ID2D1RenderTarget* t, ID2D1Factory* factory, ID2D1SolidColorBrush* brush, D2D1_RECT_F box, float size, int position, State state) {
    if (!t || !factory || !brush) return;
    position = std::clamp(position, 0, 2);
    static ID2D1Factory* owner = nullptr; static ID2D1PathGeometry* bands[3] = {};
    if (owner != factory) { for (auto*& g : bands) if (g) { g->Release(); g = nullptr; } owner = factory; }
    if (!bands[position]) bands[position] = DialogKit::SvgGeometry(factory, Band[position]);
    D2D1_MATRIX_3X2_F base; t->GetTransform(&base);
    const float k = size / 256, x = (box.left + box.right - size) / 2, y = (box.top + box.bottom - size) / 2;
    t->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(x, y) * base);
    if (bands[position]) { brush->SetColor(Rgb(Fill(state))); t->FillGeometry(bands[position], brush); }
    brush->SetColor(Rgb(Outline(state)));
    const float w = Stroke(size);
    t->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(32, 48, 224, 208), 20, 20), brush, w);
    if (Divider[position] > 0) t->DrawLine({Divider[position], 48}, {Divider[position], 208}, brush, w);
    t->SetTransform(base);
}
}}  // namespace NativeChrome::TabsPositionIcon
