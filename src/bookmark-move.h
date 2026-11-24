#pragma once
// One bookmark drop (bar, drawer, folder menus) as ONE profile-store transaction (review eb542bc, part 2).
// The place chosen on screen is captured as IDENTITIES, never raw ids: the dragged node, the destination folder and the
// sibling it goes before (none: the end), each by id + kind + `added`. Inside the save, under the profile lock, on the
// tree merged with the other windows' changes, each is resolved again (following `remap` for this window's unsaved
// nodes) and must still be that node; the destination must still hold the anchor. Then the one move is applied there.
// Anything else — the item deleted or replaced elsewhere, the folder deleted or another one reusing its id, the anchor
// gone — is an honest outcome with nothing moved; a failed write changes nothing at all. Only `Moved` gets an undo
// step and a success announcement, from what was actually committed (ids after remap, the place it left).
#include <cstdint>
#include <map>
#include <string>
#include "bookmark-drag.h"
#include "bookmarks.h"
#include "profile-store.h"

namespace BookmarkMove {
namespace BM = Bookmarks;
struct Identity {
    BM::Id id = BM::NoId; BM::Kind kind = BM::Kind::Link; std::int64_t added = 0;
    bool Empty() const { return id == BM::NoId; }
};
inline Identity Of(const BM::Store& s, BM::Id id) { const BM::Node* n = s.Find(id); return n ? Identity{id, n->kind, n->added} : Identity{}; }
// The node `who` names in `s` (after `remap` for an unsaved one), or NoId if it is not there as itself.
inline BM::Id Resolve(const BM::Store& s, const Identity& who, const std::map<BM::Id, BM::Id>& remap) {
    if (who.Empty()) return BM::NoId;
    if (BM::Store::IsRoot(who.id)) return who.id;   // the fixed roots cannot be replaced
    const auto it = remap.find(who.id); const BM::Id id = it == remap.end() ? who.id : it->second;
    const BM::Node* n = s.Find(id);
    return n && n->kind == who.kind && n->added == who.added ? id : BM::NoId;
}
// What the person chose: drag `source` to `parent`, before `before` (empty: at the end).
struct Request { Identity source, parent, before; };
// `drop.index` counts the displayed children (the marker's place, the dragged one included if it is there).
inline Request Capture(const BM::Store& s, BM::Id dragged, const BookmarkDrag::Drop& drop) {
    Request r; r.source = Of(s, dragged); r.parent = Of(s, drop.parent);
    const BM::Node* p = s.Find(drop.parent);
    if (p && drop.index != BM::End) {
        std::size_t i = drop.index;
        if (i < p->children.size() && p->children[i] == dragged) ++i;   // « before itself »: before the next one
        if (i < p->children.size()) r.before = Of(s, p->children[i]);
    }
    return r;
}
enum class Outcome { Moved, Unchanged, Refused /* cycle / depth */, Gone /* item deleted or replaced */, TargetChanged, SaveFailed };
struct Result {
    Outcome outcome = Outcome::SaveFailed;
    ProfileStore::Result store = ProfileStore::Result::Failed;
    BM::Id id = BM::NoId, from = BM::NoId, to = BM::NoId;   // committed ids: the item, the folder it left and the one it entered
    std::size_t fromIndex = 0;
    std::map<BM::Id, BM::Id> remap;
};
// The move on a store (pure: also run by tests on their own stores).
inline void Apply(BM::Store& s, const Request& q, const std::map<BM::Id, BM::Id>& remap, Result& r) {
    r.id = r.from = r.to = BM::NoId; r.fromIndex = 0;
    const BM::Id id = Resolve(s, q.source, remap);
    if (!id) { r.outcome = Outcome::Gone; return; }
    const BM::Id parent = Resolve(s, q.parent, remap);
    const BM::Node* p = s.Find(parent);
    if (!parent || !p || p->kind != BM::Kind::Folder) { r.outcome = Outcome::TargetChanged; return; }
    std::size_t index = p->children.size();
    if (!q.before.Empty()) {
        const BM::Id before = Resolve(s, q.before, remap);
        const BM::Node* b = s.Find(before);
        if (!before || !b || b->parent != parent) { r.outcome = Outcome::TargetChanged; return; }
        index = s.IndexOf(before);
    }
    if (!BookmarkDrag::Accepts(s, id, parent)) { r.outcome = Outcome::Refused; return; }
    const BM::Id from = s.Find(id)->parent; const std::size_t at = s.IndexOf(id);
    if (from == parent) { if (at < index) --index; if (at == index) { r.outcome = Outcome::Unchanged; r.id = id; return; } }
    if (!s.Move(id, parent, index)) { r.outcome = Outcome::Refused; return; }
    r.outcome = Outcome::Moved; r.id = id; r.from = from; r.fromIndex = at; r.to = parent;
}
inline Result Commit(const std::wstring& dataDir, const std::wstring& file, BM::Store& base, BM::Store& mine, const Request& q, DWORD timeoutMs = 5000) {
    Result r;
    r.store = ProfileStore::Commit<BM::Store>(dataDir, file, base, mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) {
        BM::Store merged = BM::Store::Merge(b, m, t, &r.remap);
        Apply(merged, q, r.remap, r);   // a retry runs this again: Apply resets the ids
        return merged;
    }, timeoutMs, {"browser-bookmarks 1"});
    if (r.store != ProfileStore::Result::Written && r.store != ProfileStore::Result::Unchanged) { r.outcome = Outcome::SaveFailed; r.id = r.from = r.to = BM::NoId; }
    return r;
}
} // namespace BookmarkMove
