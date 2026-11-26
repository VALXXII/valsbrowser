#pragma once
// A TAB dragged onto the favorites (atelier B, « glisser un onglet dans les favoris »): where its release would ADD a
// favorite (never a move: a tab is not a bookmark yet), and how the bar looks with that place opened. Pure C++17 over
// Bookmarks::Store and BookmarksBar::Arrange (no HWND, no Direct2D): tests/tab_favorite_drop_test.cpp checks every rule.
// The host (main.cpp) owns the pointer, the overlay preview and the cancel paths; nothing here writes the store.
//
// Rules (the bar of the top layout; same rules as BookmarkDrag::BarTarget, for a new item):
//  - over the middle half of a folder: into it (at its end); elsewhere over an item: before / after it by which half;
//    in the 2 DIP gap between two items: between them; left of the first: first; right of the last shown: after it
//    (when the bar overflows, that is before the items behind « » »: the new one may itself go behind « » »).
//  - the same page (UrlKey) already DIRECTLY in the destination folder: no place opens, the release adds nothing
//    (`duplicate` names the item that already holds it, so the host can point at it).
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include "bookmark-drag.h"
#include "bookmarks-bar.h"
#include "bookmarks.h"
#include "sidebar.h"

namespace TabFavorite {
namespace BM = Bookmarks;
namespace Sidebar = NativeChrome::Sidebar;

struct Target {
    bool valid = false;                 // a release here adds the favorite at parent/index
    BM::Id parent = BM::NoId; std::size_t index = BM::End;
    int slot = -1;                      // bar: the index the new item takes among the shown items (its place opens there)
    int into = -1;                      // bar: the shown folder item it would go into (at its end)
    int duplicate = -1;                 // bar: the shown item that already holds this page (the folder itself when it
                                        // is inside a folder target); -2 = in the destination but not shown; -1 = none
    bool Same(const Target& o) const { return valid == o.valid && parent == o.parent && index == o.index && slot == o.slot && into == o.into && duplicate == o.duplicate; }
};

// A link to the same page directly in `parent`, else NoId.
inline BM::Id Existing(const BM::Store& s, BM::Id parent, const std::wstring& uri) {
    const BM::Node* p = s.Find(parent); if (!p || uri.empty()) return BM::NoId;
    const std::wstring key = BM::UrlKey(uri);
    for (BM::Id c : p->children) { const BM::Node* n = s.Find(c); if (n && n->kind == BM::Kind::Link && BM::UrlKey(n->url) == key) return c; }
    return BM::NoId;
}

// `boxes`: the shown items' left/right edges (DIP, the bar's first children in order), at rest (never the presented,
// stepped-aside places: the target stays put while the items move). `x`: the pointer.
inline Target BarTarget(const BM::Store& s, const std::wstring& uri, const std::vector<BookmarkDrag::Box>& boxes, float x) {
    Target r; const BM::Node* root = s.Find(BM::BarRoot); if (!root || !BM::Admissible(uri)) return r;
    const auto& bar = root->children;
    const std::size_t shown = std::min(boxes.size(), bar.size());
    int slot = -1;
    for (std::size_t i = 0; i < shown && slot < 0; ++i) {
        const auto& b = boxes[i];
        if (x >= b.right) { if (i + 1 < shown && x < boxes[i + 1].left) slot = static_cast<int>(i + 1); continue; }   // the gap after it
        if (x < b.left) break;
        const BM::Node* n = s.Find(bar[i]); const float w = b.right - b.left;
        if (n && n->kind == BM::Kind::Folder && x > b.left + w * .25f && x < b.right - w * .25f) {
            r.into = static_cast<int>(i); r.parent = bar[i]; r.index = BM::End;
            if (Existing(s, bar[i], uri) != BM::NoId) { r.duplicate = static_cast<int>(i); r.into = -1; return r; }
            r.valid = true; return r;
        }
        slot = static_cast<int>(x < b.left + w / 2 ? i : i + 1);
    }
    if (slot < 0) slot = shown == 0 || x < boxes[0].left ? 0 : static_cast<int>(shown);
    if (const BM::Id same = Existing(s, BM::BarRoot, uri); same != BM::NoId) {
        const std::size_t at = s.IndexOf(same); r.duplicate = at < shown ? static_cast<int>(at) : -2; return r;
    }
    r.slot = slot; r.parent = BM::BarRoot; r.index = static_cast<std::size_t>(slot); r.valid = true;
    return r;
}

// The bar as it would be with the new item at `slot`: BookmarksBar::Arrange on the items plus the new one, read back
// per existing item. `naturals`: every bar item's natural width (all of them, shown or not), `fresh`: the new item's.
struct Preview {
    std::vector<float> left, width; std::vector<bool> shown;   // per existing item
    float slotLeft = 0, slotWidth = 0; bool slotShown = false;  // the opened place (board 08's hollow slot)
    bool overflow = false; float overflowLeft = 0;              // « » » in that layout
};
inline Preview Opened(float width, const std::vector<float>& naturals, int slot, float fresh, bool forceButton) {
    Preview p; std::vector<BookmarksBar::Item> items; items.reserve(naturals.size() + 1);
    slot = std::clamp(slot, 0, static_cast<int>(naturals.size()));
    for (int i = 0; i <= static_cast<int>(naturals.size()); ++i) {
        if (i == slot) items.push_back({fresh});
        if (i < static_cast<int>(naturals.size())) items.push_back({naturals[static_cast<std::size_t>(i)]});
    }
    const auto a = BookmarksBar::Arrange(width, items, forceButton);
    auto place = [&](int k, float& l, float& w) { if (k < a.visible) { l = a.left[static_cast<std::size_t>(k)]; w = a.width[static_cast<std::size_t>(k)]; return true; } return false; };
    for (int i = 0; i < static_cast<int>(naturals.size()); ++i) {
        float l = 0, w = 0; const bool on = place(i < slot ? i : i + 1, l, w);
        p.left.push_back(l); p.width.push_back(w); p.shown.push_back(on);
    }
    p.slotShown = place(slot, p.slotLeft, p.slotWidth);
    p.overflow = a.overflow; p.overflowLeft = a.overflowLeft;
    return p;
}

// ---- The column's grid (side panel and compact layouts) -----------------------------------------------------------
// `tiles`: the shown tiles at rest (the bar's first children, in order, at the current scroll), `view`: what of the grid
// is visible (a tile outside it is no target). `vertical`: the compact column, one tile per row (before / after by the
// upper / lower half; across for the side panel's rows). A folder's middle half takes it in. Off every tile but inside
// the view: before / after the nearest tile (by its centre); under the view (« Tous les favoris », « Gérer »): after
// the last shown one (before the hidden ones: it may itself be hidden).
inline Target GridTarget(const BM::Store& s, const std::wstring& uri, const std::vector<Sidebar::Box>& tiles, const Sidebar::Box& view, bool vertical, float x, float y) {
    Target r; const BM::Node* root = s.Find(BM::BarRoot); if (!root || !BM::Admissible(uri)) return r;
    const auto& bar = root->children;
    const int shown = static_cast<int>(std::min(tiles.size(), bar.size()));
    auto seen = [&](const Sidebar::Box& t) { return t.bottom > view.top && t.top < view.bottom; };
    int slot = -1, nearest = -1; float best = 1e30f;
    for (int i = 0; i < shown; ++i) {
        const auto& t = tiles[static_cast<std::size_t>(i)]; if (!seen(t)) continue;
        const float cx = (t.left + t.right) / 2, cy = (t.top + t.bottom) / 2;
        if (!t.Contains(x, y)) { const float d = (cx - x) * (cx - x) + (cy - y) * (cy - y); if (d < best) { best = d; nearest = i; } continue; }
        const float a = vertical ? y : x, lo = vertical ? t.top : t.left, len = vertical ? t.Height() : t.Width();
        const BM::Node* n = s.Find(bar[static_cast<std::size_t>(i)]);
        if (n && n->kind == BM::Kind::Folder && a > lo + len * .25f && a < lo + len * .75f) {
            r.into = i; r.parent = bar[static_cast<std::size_t>(i)]; r.index = BM::End;
            if (Existing(s, r.parent, uri) != BM::NoId) { r.duplicate = i; r.into = -1; return r; }
            r.valid = true; return r;
        }
        slot = a < lo + len / 2 ? i : i + 1; break;
    }
    if (slot < 0) {
        if (shown == 0 || nearest < 0 || y >= view.bottom) slot = shown;
        else { const auto& t = tiles[static_cast<std::size_t>(nearest)]; const bool after = vertical ? y >= (t.top + t.bottom) / 2 : (y >= t.bottom ? true : y < t.top ? false : x >= (t.left + t.right) / 2); slot = after ? nearest + 1 : nearest; }
    }
    if (const BM::Id same = Existing(s, BM::BarRoot, uri); same != BM::NoId) {
        const std::size_t at = s.IndexOf(same); r.duplicate = at < static_cast<std::size_t>(shown) ? static_cast<int>(at) : -2; return r;
    }
    r.slot = slot; r.parent = BM::BarRoot; r.index = static_cast<std::size_t>(slot); r.valid = true;
    return r;
}
// The grid with the new tile at `slot`, in the same viewport: the side panel's rows re-share as MakeGrid lays them out
// for one more tile (a row of fewer tiles than it holds re-divides its width); the compact column moves the tiles from
// `slot` down by one pitch. The block never grows during a drag (the column below stays put): a tile or the place that
// falls outside it is clipped (`slotShown` false: past the last tile of a full grid, or of the compact column).
struct GridPreview { std::vector<Sidebar::Box> tiles; Sidebar::Box slot; bool slotShown = false; };
inline GridPreview GridOpened(const Sidebar::Grid& g, int slot) {
    namespace C = Sidebar::Complete; GridPreview p; const int m = static_cast<int>(g.tiles.size()), n = m + 1;
    slot = std::clamp(slot, 0, m);
    const float w = g.view.Width(); const int cols = std::max(1, std::min(Sidebar::Columns(w), n));
    const float tw = (w - C::tileGap * (cols - 1)) / cols;
    auto cell = [&](int k) { const int c = k % cols, r = k / cols; const float x = g.view.left + c * (tw + C::tileGap), y = g.view.top + r * (C::tile + C::tileGap) - g.scroll; return Sidebar::Box{x, y, x + tw, y + C::tile}; };
    for (int i = 0; i < m; ++i) p.tiles.push_back(cell(i < slot ? i : i + 1));
    p.slot = cell(slot); p.slotShown = p.slot.top < g.view.bottom && p.slot.bottom > g.view.top;
    return p;
}
inline GridPreview ColumnOpened(const std::vector<Sidebar::Box>& tiles, int shown, int slot) {
    namespace K = Sidebar::Compact; GridPreview p; shown = std::clamp(shown, 0, static_cast<int>(tiles.size())); slot = std::clamp(slot, 0, shown);
    const float pitch = K::tile + K::tileGap;
    for (int i = 0; i < shown; ++i) { auto t = tiles[static_cast<std::size_t>(i)]; if (i >= slot) { t.top += pitch; t.bottom += pitch; } p.tiles.push_back(t); }
    if (slot < shown) { p.slot = tiles[static_cast<std::size_t>(slot)]; p.slotShown = true; }
    else if (shown > 0) { p.slot = tiles[static_cast<std::size_t>(shown - 1)]; p.slot.top += pitch; p.slot.bottom += pitch; }
    return p;
}
} // namespace TabFavorite
