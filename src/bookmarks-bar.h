#pragma once
// Bookmarks bar geometry (Paper "Favoris — flow", band under the toolbar). Pure C++17:
// the host measures each item's natural width (Chrome::TextWidth) and this decides,
// in two steps, what the band shows: titles shrink from 160 DIP down to a 96 DIP
// floor (widest first), then whatever still does not fit goes behind one "»" button
// pinned to the right edge, whose room is reserved before counting visible items.
// The band never scrolls and never shows favicon-only items. Tested without
// Direct2D by tests/bookmarks/bookmarks_ui_test.cpp.
#include <algorithm>
#include <cmath>
#include <vector>
#include "design-system.h"

namespace BookmarksBar {
namespace Layout {
inline constexpr float padding = NativeChrome::Design::Space::sm;   // 6: first item aligns with Back
inline constexpr float gap = 2;                                      // Paper band: 2 DIP between items
inline constexpr float itemPad = NativeChrome::Design::Space::md;   // 8 inside an item
inline constexpr float iconGap = NativeChrome::Design::Space::sm;   // 6 icon -> title
inline constexpr float chevron = 12, chevronGap = 6;                 // folders: 12 DIP caret at 32%
inline constexpr float maxItem = 160, minItem = 96;                  // title cap, then the floor
inline constexpr float overflow = NativeChrome::Design::Size::control; // "»" button, 32x32
inline constexpr float top = NativeChrome::Design::Vertical::bandTop, bottom = NativeChrome::Design::Vertical::bandBottom;
// Natural width of an item whose title measures `title` DIP (0 = untitled: icon only).
inline float Natural(float title, bool folder) {
    float w = itemPad + NativeChrome::Design::Size::icon + itemPad;
    if (title > 0) w += iconGap + std::ceil(title);
    if (folder) w += chevronGap + chevron;
    return std::min(w, maxItem);
}
}
struct Item { float natural = 0; };
struct Result {
    int visible = 0;                 // items[0..visible) are on the band, the rest overflow
    bool overflow = false;           // the "»" button exists
    std::vector<float> left, width;  // per visible item (DIP, client)
    float overflowLeft = 0;          // the button's left edge when shown
};
// `width`: client width (DIP). `forceButton`: the button shows even when everything fits
// (Autres favoris has content, reachable only from it).
inline Result Arrange(float width, const std::vector<Item>& items, bool forceButton = false) {
    using namespace Layout;
    Result r; const int n = static_cast<int>(items.size());
    const float right = width - padding;
    auto total = [&](int count, float cap) {
        float sum = 0; for (int i = 0; i < count; ++i) sum += std::min(items[i].natural, std::max(cap, std::min(items[i].natural, minItem)));
        return sum + gap * std::max(0, count - 1);
    };
    float room = right - padding;
    int count = n;
    if (forceButton || total(n, minItem) > room) {
        // Reserve the button first, so its arrival never moves an item that stays.
        r.overflow = true; room -= overflow + gap;
    }
    if (r.overflow) { count = 0; while (count < n && total(count + 1, minItem) <= room) ++count; }
    // Titles shrink only as far as needed: the largest cap in [floor, 160] that fits.
    float lo = minItem, hi = maxItem;
    if (total(count, hi) <= room) lo = hi;
    else for (int k = 0; k < 24; ++k) { const float mid = (lo + hi) / 2; if (total(count, mid) <= room) lo = mid; else hi = mid; }
    const float cap = std::floor(lo);
    float x = padding;
    for (int i = 0; i < count; ++i) {
        const float w = std::floor(std::min(items[i].natural, std::max(cap, std::min(items[i].natural, minItem))));
        r.left.push_back(x); r.width.push_back(w); x += w + gap;
    }
    r.visible = count;
    if (count < n) r.overflow = true;
    r.overflowLeft = right - overflow;
    return r;
}
} // namespace BookmarksBar
