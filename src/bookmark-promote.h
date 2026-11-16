#pragma once
// « Placer le contenu dans la barre de favoris » as ONE profile-store transaction (review 9ef33c5, P1).
// The operation runs inside the save, on the merged tree under the profile lock: every child the folder has THERE
// (another window's additions included) moves to the end of the bar with its id and order, then the folder is removed
// only if it is empty there AND `owned` says an import created it. Doing it outside (move + remove in memory, then the
// generic three-way merge) lost the moved children: the merge applies the folder's removal to the file first, which
// takes the whole subtree, and then skips the moves of nodes it no longer finds.
// The result says exactly what was committed, for one undo step; on any failure nothing was written and `base` /
// `mine` are untouched.
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "bookmarks.h"
#include "profile-store.h"

namespace BookmarkPromote {
namespace BM = Bookmarks;
struct Moved { BM::Id id = 0, parent = 0; std::size_t index = 0; };   // where it was (undo: back there, in reverse)
struct Result {
    ProfileStore::Result store = ProfileStore::Result::Failed;
    std::vector<Moved> moved; std::optional<BM::Removed> removed;
    std::map<BM::Id, BM::Id> remap;
    bool Saved() const { return store == ProfileStore::Result::Written || store == ProfileStore::Result::Unchanged; }
};
// The operation on a store (pure; also used by tests on their own stores).
inline void Apply(BM::Store& s, BM::Id folder, const std::function<bool(const BM::Store&, BM::Id)>& owned, Result& out) {
    out.moved.clear(); out.removed.reset();
    const BM::Node* f = s.Find(folder);
    if (!f || f->kind != BM::Kind::Folder || BM::Store::IsRoot(folder)) return;
    const std::vector<BM::Id> kids = f->children;
    for (std::size_t i = 0; i < kids.size(); ++i) if (s.Move(kids[i], BM::BarRoot, BM::End)) out.moved.push_back({kids[i], folder, i});
    if (s.Find(folder)->children.empty() && owned && owned(s, folder)) out.removed = s.Remove(folder);
}
inline Result Commit(const std::wstring& dataDir, const std::wstring& file, BM::Store& base, BM::Store& mine, BM::Id folder,
                     const std::function<bool(const BM::Store&, BM::Id)>& owned, DWORD timeoutMs = 5000) {
    Result r;
    r.store = ProfileStore::Commit<BM::Store>(dataDir, file, base, mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) {
        BM::Store merged = BM::Store::Merge(b, m, t, &r.remap);
        Apply(merged, folder, owned, r);   // the merge may run again on a retry: Apply resets `r` each time
        return merged;
    }, timeoutMs, {"browser-bookmarks 1"});
    if (!r.Saved()) { r.moved.clear(); r.removed.reset(); }
    return r;
}
// Undo of a committed promotion on `s` (then saved by the caller): the folder back first (empty, same id), then each
// child back to its place, in reverse. Children that another window has since moved or deleted are left alone.
inline void Undo(BM::Store& s, const Result& r) {
    if (r.removed) s.Restore(*r.removed);
    for (auto it = r.moved.rbegin(); it != r.moved.rend(); ++it) {
        const BM::Node* n = s.Find(it->id); if (!n || n->parent != BM::BarRoot || !s.Find(it->parent)) continue;
        s.Move(it->id, it->parent, it->index);
    }
}
} // namespace BookmarkPromote
