#pragma once
// Bookmark drag and drop: where a dragged bookmark would land, for the bookmarks bar and for the drawer's tree, and the
// one move that commits it. Pure C++17 over Bookmarks::Store (no HWND), so tests/bookmark_drag_test.cpp checks every
// rule. The host (main.cpp) owns the pointer, the capture, the markers and the cancel paths; nothing here mutates the
// store before Commit, which is called only on drop.
#include <algorithm>
#include <cstddef>
#include <vector>
#include "bookmarks.h"

namespace BookmarkDrag {
namespace BM = Bookmarks;

// A place in the tree: the folder and the final index among its children (BM::End: last), or `into` a folder (end).
struct Drop {
    BM::Id parent = BM::NoId; std::size_t index = BM::End;
    bool valid = false;
    bool Same(const Drop& o) const { return valid == o.valid && parent == o.parent && index == o.index; }
};
// Can `dragged` go into `parent` at all: not itself, not one of its own descendants, not past the depth limit.
inline bool Accepts(const BM::Store& s, BM::Id dragged, BM::Id parent) {
    const BM::Node* p = s.Find(parent); const BM::Node* d = s.Find(dragged);
    if (!p || !d || p->kind != BM::Kind::Folder || dragged == parent || BM::Store::IsRoot(dragged) || s.IsAncestor(dragged, parent)) return false;
    if (d->kind == BM::Kind::Folder && static_cast<std::size_t>(s.Depth(parent) + s.FolderHeight(dragged)) > BM::Store::MaxDepth) return false;
    return true;
}

// ---- Bookmarks bar ---------------------------------------------------------------------------------------------
// `boxes`: the visible items' left/right edges (DIP, same order as the bar's first children). Over the middle half of a
// folder (not the dragged one, not one it contains) the drop goes INTO it; otherwise before / after the item under the
// pointer, by which half it is in. Left of the first item: first; right of the last visible: after it.
struct Box { float left = 0, right = 0; };
struct BarDrop { Drop drop; int into = -1; int slot = -1; float marker = -1; };   // into: visible index; marker: x of the insertion line
inline BarDrop BarTarget(const BM::Store& s, BM::Id dragged, const std::vector<Box>& boxes, float x) {
    BarDrop r; const auto& bar = s.Find(BM::BarRoot)->children;
    const std::size_t visible = std::min(boxes.size(), bar.size());
    if (!s.Find(dragged)) return r;
    for (std::size_t i = 0; i < visible; ++i) {
        const Box& b = boxes[i]; if (x < b.left || x >= b.right) continue;
        const BM::Node* n = s.Find(bar[i]); const float w = b.right - b.left;
        if (n && n->kind == BM::Kind::Folder && bar[i] != dragged && x > b.left + w * .25f && x < b.right - w * .25f) {
            if (Accepts(s, dragged, bar[i])) { r.into = static_cast<int>(i); r.drop = {bar[i], BM::End, true}; }
            return r;   // over a folder it cannot enter: no drop there
        }
        r.slot = static_cast<int>(x < b.left + w / 2 ? i : i + 1);
        break;
    }
    if (r.slot < 0) {
        if (visible == 0 || x < boxes[0].left) r.slot = 0;
        else if (x >= boxes[visible - 1].right) r.slot = static_cast<int>(visible);
        else return r;   // in a gap between items: nearest edge is fine but keep it simple
    }
    if (!Accepts(s, dragged, BM::BarRoot)) return r;
    r.drop = {BM::BarRoot, static_cast<std::size_t>(r.slot), true};
    r.marker = r.slot < static_cast<int>(visible) ? boxes[static_cast<std::size_t>(r.slot)].left - 3 : (visible ? boxes[visible - 1].right + 3 : boxes.empty() ? 12 : boxes[0].left);
    return r;
}

// ---- Drawer tree ------------------------------------------------------------------------------------------------
// `rows`: what the drawer shows (id, level, folder, open), top to bottom. `row` is under the pointer, `fraction` is the
// pointer's height inside it (0 top .. 1 bottom). A root label (level 0) takes the drop as its first child. A folder's
// middle half takes it inside (end); otherwise before the row (upper half) or after it (lower half) in its folder; after
// an OPEN folder means its first child.
struct Row { BM::Id id = 0; int level = 0; bool folder = false, open = false; };
enum class Mark { None, Before, After, Into };
struct ListDrop { Drop drop; int row = -1; Mark mark = Mark::None; };
inline ListDrop ListTarget(const BM::Store& s, BM::Id dragged, const std::vector<Row>& rows, int row, float fraction) {
    ListDrop r; if (row < 0 || row >= static_cast<int>(rows.size()) || !s.Find(dragged)) return r;
    const Row& w = rows[static_cast<std::size_t>(row)]; const BM::Node* n = s.Find(w.id);
    if (!n) return r;
    auto set = [&](BM::Id parent, std::size_t index, Mark mark) { if (Accepts(s, dragged, parent)) { r.drop = {parent, index, true}; r.row = row; r.mark = mark; } return r; };
    if (w.level == 0) return set(w.id, 0, Mark::After);                                            // the root itself: first in it
    if (w.id == dragged) return r;
    if (w.folder && fraction > .25f && fraction < .75f) return set(w.id, BM::End, Mark::Into);
    if (fraction >= .5f && w.folder && w.open) return set(w.id, 0, Mark::After);                    // just under an open folder: its first child
    const std::size_t at = s.IndexOf(w.id);
    return set(n->parent, fraction < .5f ? at : at + 1, fraction < .5f ? Mark::Before : Mark::After);
}

// ---- Commit (on drop only) --------------------------------------------------------------------------------------
// `index` is where it should appear counting the current children (the marker's place); Store::Move wants the final
// index, which is one less when moving down inside the same folder. Refused (cycle, depth, gone): nothing moved.
enum class Result { Moved, Unchanged, Refused };
inline Result Commit(BM::Store& s, BM::Id dragged, const Drop& d) {
    if (!d.valid || !Accepts(s, dragged, d.parent)) return Result::Refused;
    const BM::Node* n = s.Find(dragged);
    std::size_t index = d.index;
    if (n->parent == d.parent) {
        const std::size_t from = s.IndexOf(dragged), count = s.Find(d.parent)->children.size();
        if (index == BM::End || index > count) index = count;
        if (from < index) --index;
        if (from == index) return Result::Unchanged;
    }
    return s.Move(dragged, d.parent, index) ? Result::Moved : Result::Refused;
}
} // namespace BookmarkDrag
