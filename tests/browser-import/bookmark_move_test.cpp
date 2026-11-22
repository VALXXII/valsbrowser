// One bookmark drop through the REAL save (src/bookmark-move.h + ProfileStore + Bookmarks::Merge): other windows
// deleting / replacing the item or the place, unsaved ids remapped, a refused write, then reload and the saved undo
// (review eb542bc, part 2). Synthetic files only.
// Usage: bookmark_move_test.exe <work folder>
#include "bookmark-move.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
namespace BM = Bookmarks; namespace MV = BookmarkMove; namespace BD = BookmarkDrag;
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)
static std::wstring work;
static std::string Bytes(const BM::Store& s) { std::ostringstream o; s.Write(o); return o.str(); }
static BM::Store Load(const std::wstring& file) { std::string b; ProfileStore::ReadAll(file, b); std::istringstream in(b); return BM::Store::Read(in); }
static std::string Raw(const std::wstring& file) { std::string b; ProfileStore::ReadAll(file, b); return b; }
static std::vector<std::wstring> Titles(const BM::Store& s, BM::Id f) { std::vector<std::wstring> t; for (BM::Id id : s.Find(f)->children) t.push_back(s.Find(id)->title); return t; }
struct Case { std::wstring data, file; BM::Store base, mine; };
static Case Setup(const std::wstring& name, const BM::Store& seed) {
    Case c; c.data = work + L"\\" + name; CreateDirectoryW(c.data.c_str(), nullptr); c.file = c.data + L"\\bookmarks";
    CHECK(ProfileStore::WriteAtomic(c.file, Bytes(seed))); c.base = seed; c.mine = seed; return c;
}
static ProfileStore::Result Save(Case& c) {
    std::map<BM::Id, BM::Id> remap;
    return ProfileStore::Commit<BM::Store>(c.data, c.file, c.base, c.mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) { return BM::Store::Merge(b, m, t, &remap); }, 5000, {"browser-bookmarks 1"});
}
// The host's undo step for a Moved result (main.cpp BookmarkStep::Moved), then its generic save.
static ProfileStore::Result UndoAndSave(Case& c, const MV::Result& r) { c.mine.Move(r.id, r.from, r.fromIndex); return Save(c); }

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    work = std::wstring(argv[1]) + L"\\move-run"; CreateDirectoryW(argv[1], nullptr); CreateDirectoryW(work.c_str(), nullptr);
    // Bar: a, b, Folder(x). The person drags « a » into « Folder », before « x ».
    BM::Store seed; const BM::Id a = seed.Add(BM::BarRoot, BM::End, L"a", L"https://a.example/", 1), b = seed.Add(BM::BarRoot, BM::End, L"b", L"https://b.example/", 2);
    const BM::Id folder = seed.AddFolder(BM::BarRoot, BM::End, L"Folder", 3); const BM::Id x = seed.Add(folder, BM::End, L"x", L"https://x.example/", 4);

    {   // Capture: identities of the item, the folder and the sibling it goes before; « before itself » → the next one.
        const MV::Request q = MV::Capture(seed, a, BD::Drop{folder, 0, true});
        OK(q.source.id == a && q.source.added == 1 && q.parent.id == folder && q.parent.added == 3 && q.before.id == x && q.before.added == 4);
        OK(MV::Capture(seed, a, BD::Drop{BM::BarRoot, 0, true}).before.id == b && MV::Capture(seed, a, BD::Drop{BM::BarRoot, BM::End, true}).before.Empty());
        OK(MV::Capture(seed, a, BD::Drop{BM::BarRoot, 3, true}).before.Empty());   // past the last: the end
    }
    {   // 1. Normal: moved, written, reloaded; the undo (committed ids) saved and reloaded.
        Case c = Setup(L"moved", seed);
        const auto r = MV::Commit(c.data, c.file, c.base, c.mine, MV::Capture(c.mine, a, BD::Drop{folder, 0, true}));
        const BM::Store disk = Load(c.file);
        OK(r.outcome == MV::Outcome::Moved && r.store == ProfileStore::Result::Written && r.id == a && r.from == BM::BarRoot && r.fromIndex == 0 && r.to == folder);
        OK((Titles(disk, folder) == std::vector<std::wstring>{L"a", L"x"}) && (Titles(disk, BM::BarRoot) == std::vector<std::wstring>{L"b", L"Folder"}) && disk == c.mine && c.base == c.mine);
        OK(UndoAndSave(c, r) == ProfileStore::Result::Written && Load(c.file) == seed);
        // Reorder on the bar: « a » after « b » (marker index 2 counts « a » itself).
        Case d = Setup(L"reorder", seed);
        const auto o = MV::Commit(d.data, d.file, d.base, d.mine, MV::Capture(d.mine, a, BD::Drop{BM::BarRoot, 2, true}));
        OK(o.outcome == MV::Outcome::Moved && (Titles(Load(d.file), BM::BarRoot) == std::vector<std::wstring>{L"b", L"a", L"Folder"}));
        // Dropped where it already is: no move, nothing to undo.
        Case e = Setup(L"same", seed);
        const auto u = MV::Commit(e.data, e.file, e.base, e.mine, MV::Capture(e.mine, a, BD::Drop{BM::BarRoot, 1, true}));
        OK(u.outcome == MV::Outcome::Unchanged && Load(e.file) == seed);
    }
    {   // 2. Another window deleted the item after the drag started: Gone, no crash, nothing moved, its deletion kept.
        Case c = Setup(L"source-deleted", seed);
        const MV::Request q = MV::Capture(c.mine, a, BD::Drop{folder, BM::End, true});
        BM::Store other = seed; other.Remove(a); CHECK(ProfileStore::WriteAtomic(c.file, Bytes(other)));
        const auto r = MV::Commit(c.data, c.file, c.base, c.mine, q);
        const BM::Store disk = Load(c.file);
        OK(r.outcome == MV::Outcome::Gone && r.id == BM::NoId && !disk.Find(a) && (Titles(disk, folder) == std::vector<std::wstring>{L"x"}) && !c.mine.Find(a));
        // ...or replaced it by a different item under the same id (another `added`): Gone too, that item untouched.
        Case d = Setup(L"source-replaced", seed);
        const MV::Request q2 = MV::Capture(d.mine, a, BD::Drop{folder, BM::End, true});
        BM::Store forged; const BM::Id fa = forged.Add(BM::BarRoot, BM::End, L"other", L"https://other.example/", 99); forged.Add(BM::BarRoot, BM::End, L"b", L"https://b.example/", 2);
        const BM::Id ff = forged.AddFolder(BM::BarRoot, BM::End, L"Folder", 3); forged.Add(ff, BM::End, L"x", L"https://x.example/", 4);
        CHECK(fa == a && ff == folder); CHECK(ProfileStore::WriteAtomic(d.file, Bytes(forged)));
        const auto r2 = MV::Commit(d.data, d.file, d.base, d.mine, q2);
        OK(r2.outcome == MV::Outcome::Gone && Load(d.file).Find(a)->parent == BM::BarRoot && Load(d.file).Find(a)->title == L"other");
    }
    {   // 3. The destination was deleted and its id reused by another folder (other `added`): TargetChanged, nothing
        // lands in the replacement.
        Case c = Setup(L"target-reused", seed);
        const MV::Request q = MV::Capture(c.mine, a, BD::Drop{folder, BM::End, true});
        BM::Store forged; forged.Add(BM::BarRoot, BM::End, L"a", L"https://a.example/", 1); forged.Add(BM::BarRoot, BM::End, L"b", L"https://b.example/", 2);
        const BM::Id rep = forged.AddFolder(BM::BarRoot, BM::End, L"Replacement", 77); CHECK(rep == folder);
        CHECK(ProfileStore::WriteAtomic(c.file, Bytes(forged)));
        const auto r = MV::Commit(c.data, c.file, c.base, c.mine, q);
        const BM::Store disk = Load(c.file);
        OK(r.outcome == MV::Outcome::TargetChanged && disk.Find(a)->parent == BM::BarRoot && disk.Find(rep)->children.empty() && disk.Find(rep)->title == L"Replacement");
        // The sibling it was to go before is gone: TargetChanged too (no guessed position).
        Case d = Setup(L"anchor-gone", seed);
        const MV::Request q2 = MV::Capture(d.mine, a, BD::Drop{folder, 0, true});
        BM::Store other = seed; other.Remove(x); CHECK(ProfileStore::WriteAtomic(d.file, Bytes(other)));
        const auto r2 = MV::Commit(d.data, d.file, d.base, d.mine, q2);
        OK(r2.outcome == MV::Outcome::TargetChanged && Load(d.file).Find(a)->parent == BM::BarRoot);
    }
    {   // 4. This window's UNSAVED folder whose id another window took: the drop follows it (remap), the other
        // window's folder stays untouched; undo uses the committed ids.
        Case c = Setup(L"target-remap", seed);
        const BM::Id mineFolder = c.mine.AddFolder(BM::OtherRoot, BM::End, L"New here", 50);
        BM::Store other = seed; const BM::Id theirs = other.AddFolder(BM::OtherRoot, BM::End, L"Theirs", 60); CHECK(theirs == mineFolder);
        CHECK(ProfileStore::WriteAtomic(c.file, Bytes(other)));
        const auto r = MV::Commit(c.data, c.file, c.base, c.mine, MV::Capture(c.mine, b, BD::Drop{mineFolder, BM::End, true}));
        const BM::Store disk = Load(c.file);
        OK(r.outcome == MV::Outcome::Moved && r.remap.count(mineFolder) && r.to == r.remap.at(mineFolder) && r.to != theirs);
        OK(disk.Find(theirs)->title == L"Theirs" && disk.Find(theirs)->children.empty() && disk.Find(b)->parent == r.to && disk.Find(r.to)->title == L"New here");
        OK(UndoAndSave(c, r) == ProfileStore::Result::Written && Load(c.file).Find(b)->parent == BM::BarRoot && Load(c.file).IndexOf(b) == 1);
        // An unsaved ITEM whose id another window took: it is the one moved, never theirs.
        Case d = Setup(L"source-remap", seed);
        const BM::Id fresh = d.mine.Add(BM::OtherRoot, BM::End, L"fresh", L"https://fresh.example/", 70);
        BM::Store other2 = seed; const BM::Id taken = other2.Add(BM::OtherRoot, BM::End, L"taken", L"https://taken.example/", 80); CHECK(taken == fresh);
        CHECK(ProfileStore::WriteAtomic(d.file, Bytes(other2)));
        const auto r2 = MV::Commit(d.data, d.file, d.base, d.mine, MV::Capture(d.mine, fresh, BD::Drop{folder, BM::End, true}));
        const BM::Store disk2 = Load(d.file);
        OK(r2.outcome == MV::Outcome::Moved && r2.id != taken && disk2.Find(r2.id)->title == L"fresh" && disk2.Find(r2.id)->parent == folder && disk2.Find(taken)->parent == BM::OtherRoot);
    }
    {   // 5. The write is refused (a newer format on disk): SaveFailed, nothing written, nothing changed in memory.
        Case c = Setup(L"save-failed", seed);
        std::string future = Bytes(seed); const auto nl = future.find('\n'); future = "browser-bookmarks 999" + future.substr(nl);
        CHECK(ProfileStore::WriteAtomic(c.file, future));
        const auto r = MV::Commit(c.data, c.file, c.base, c.mine, MV::Capture(c.mine, a, BD::Drop{folder, BM::End, true}));
        OK(r.outcome == MV::Outcome::SaveFailed && r.id == BM::NoId && Raw(c.file) == future && c.mine == seed && c.base == seed);
        // Read-only file: the same.
        Case d = Setup(L"read-only", seed); SetFileAttributesW(d.file.c_str(), FILE_ATTRIBUTE_READONLY);
        const auto r2 = MV::Commit(d.data, d.file, d.base, d.mine, MV::Capture(d.mine, a, BD::Drop{folder, BM::End, true}));
        SetFileAttributesW(d.file.c_str(), FILE_ATTRIBUTE_NORMAL);
        OK(r2.outcome == MV::Outcome::SaveFailed && Load(d.file) == seed && d.mine == seed);
    }
    {   // 6. Cycle / depth: refused inside the transaction too (the folder into its own child).
        BM::Store s = seed; const BM::Id inner = s.AddFolder(folder, BM::End, L"Inner", 5);
        Case c = Setup(L"cycle", s);
        MV::Request q; q.source = MV::Of(s, folder); q.parent = MV::Of(s, inner);
        const auto r = MV::Commit(c.data, c.file, c.base, c.mine, q);
        OK(r.outcome == MV::Outcome::Refused && Load(c.file) == s);
    }
    std::printf("bookmark move: %d checks passed\n", checks);
    return 0;
}
