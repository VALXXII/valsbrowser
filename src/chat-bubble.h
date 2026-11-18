#pragma once
// Chat intégré — Mode Commentaire: where the bubble goes (pure, no Win32 window; tests/chat/chat_units_test.cpp).
// design/chat-integration.md « Mode Commentaire ». Paper 18NS-0 / 1AWI-0: the bubble sits in the free space next to the
// element it talks about — at its right if it fits, else at its left, else under it, else above it — and never leaves
// the page area. All rectangles in the same unit (screen pixels of the host).
#include <algorithm>
#include <string>

namespace ChatBubble {
struct Rect { int left = 0, top = 0, right = 0, bottom = 0; int W() const { return right - left; } int H() const { return bottom - top; } };
enum class Side { Right, Left, Below, Above, Inside };
struct Placement { Rect rect; Side side = Side::Right; };

inline int Clamp(int v, int lo, int hi) { return hi < lo ? lo : std::max(lo, std::min(v, hi)); }
// The bubble's size, bounded by the page area (minus the margins) and by the panel page's own maxima.
inline void Fit(int& w, int& h, const Rect& area, int margin, int maxW, int maxH) {
    w = std::max(1, std::min({w, maxW, area.W() - 2 * margin}));
    h = std::max(1, std::min({h, maxH, area.H() - 2 * margin}));
}
// element: the element's box; size: the bubble's (w, h); area: where it may go (the page's visible area); gap: between
// the element and the bubble; margin: from the area's edges. The bubble is aligned on the element's top (sides) or left
// (below / above), then slid to stay inside. When no side has room, it goes inside the area at the element's right edge
// (Inside: it may cover the element, never the window's edge).
// `prefer`: the side the open bubble is on — kept while it still fits, so a new height never makes it jump sides.
inline Placement Place(const Rect& element, int w, int h, const Rect& area, int gap, int margin, Side prefer = Side::Right) {
    const int minX = area.left + margin, maxX = area.right - margin, minY = area.top + margin, maxY = area.bottom - margin;
    auto at = [&](int x, int y, Side s) { Placement p; p.side = s; x = Clamp(x, minX, maxX - w); y = Clamp(y, minY, maxY - h); p.rect = {x, y, x + w, y + h}; return p; };
    auto fits = [&](Side s) {
        switch (s) {
        case Side::Right: return element.right + gap + w <= maxX;
        case Side::Left: return element.left - gap - w >= minX;
        case Side::Below: return element.bottom + gap + h <= maxY;
        case Side::Above: return element.top - gap - h >= minY;
        default: return false;
        }
    };
    auto put = [&](Side s) {
        switch (s) {
        case Side::Right: return at(element.right + gap, element.top, s);
        case Side::Left: return at(element.left - gap - w, element.top, s);
        case Side::Below: return at(element.left, element.bottom + gap, s);
        default: return at(element.left, element.top - gap - h, Side::Above);   // above: its bottom stays on the element's top
        }
    };
    if (prefer != Side::Inside && fits(prefer)) return put(prefer);
    for (Side s : {Side::Right, Side::Left, Side::Below, Side::Above}) if (fits(s)) return put(s);
    return at(element.right - w, element.top, Side::Inside);
}
// The page an anchor belongs to: the URL without its fragment (a « #section » link does not change the page).
inline std::string PageKey(const std::string& url) { const auto hash = url.find('#'); return hash == std::string::npos ? url : url.substr(0, hash); }
} // namespace ChatBubble
