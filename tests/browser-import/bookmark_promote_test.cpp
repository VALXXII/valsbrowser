// « Placer le contenu dans la barre de favoris » through the REAL save (src/bookmark-promote.h + ProfileStore +
// Bookmarks::Merge), reloads from disk and the undo saved the same way (review 9ef33c5 P1/P2). Synthetic files only.
// Usage: bookmark_promote_test.exe <work folder>
#include "bookmark-promote.h"
#include "browser-import.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)
namespace BM = Bookmarks; namespace BP = BookmarkPromote; namespace BI = BrowserImport;
static int checks = 0;
#define OK(x) do { CHECK(x); ++checks; } while (0)
static std::wstring work;
static std::string Bytes(const BM::Store& s) { std::ostringstream o; s.Write(o); return o.str(); }
static BM::Store Load(const std::wstring& file) { std::string b; ProfileStore::ReadAll(file, b); std::istringstream in(b); return BM::Store::Read(in); }
static std::vector<std::wstring> Titles(const BM::Store& s, BM::Id f) { std::vector<std::wstring> t; for (BM::Id id : s.Find(f)->children) t.push_back(s.Find(id)->title); return t; }
// The production predicate (src/browser-import-integration.h ImportOwnedFolder): the map names the folder itself.
static std::function<bool(const BM::Store&, BM::Id)> OwnedBy(const std::wstring& mapFile) {
    return [mapFile](const BM::Store& s, BM::Id id) { std::string b; if (!ProfileStore::ReadAll(mapFile, b)) return false; bool k = false, c = false; const auto m = BI::FolderMap::Read(b, &k, &c); return k && c && m.Names(s, id); };
}
struct Case { std::wstring data, file, map; BM::Store base, mine; };
static Case Setup(const std::wstring& name, const BM::Store& seed, const BI::FolderMap& map) {
    Case c; c.data = work + L"\\" + name; CreateDirectoryW(c.data.c_str(), nullptr); c.file = c.data + L"\\bookmarks"; c.map = c.data + L"\\bookmark-imports";
    CHECK(ProfileStore::WriteAtomic(c.file, Bytes(seed))); CHECK(ProfileStore::WriteAtomic(c.map, map.Write())); c.base = seed; c.mine = seed; return c;
}
// Undo exactly as main.cpp replays it (Removed undone first, then the moves back in reverse), then the generic save.
static ProfileStore::Result UndoAndSave(Case& c, const BP::Result& r) {
    BP::Undo(c.mine, r);
    std::map<BM::Id, BM::Id> remap;
    return ProfileStore::Commit<BM::Store>(c.data, c.file, c.base, c.mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) { return BM::Store::Merge(b, m, t, &remap); }, 5000, {"browser-bookmarks 1"});
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    work = std::wstring(argv[1]) + L"\\promote-run"; CreateDirectoryW(argv[1], nullptr); CreateDirectoryW(work.c_str(), nullptr);
    // A v0.3.377 import: « Work » (guid-mapped) holding a link and a folder the person created inside it.
    BM::Store seed; const BM::Id mine0 = seed.Add(BM::BarRoot, BM::End, L"mine", L"https://mine.example/", 1);
    const BM::Id work1 = seed.AddFolder(BM::OtherRoot, BM::End, L"Work", 500);
    const BM::Id link = seed.Add(work1, BM::End, L"Doc", L"https://doc.example/", 501);
    const BM::Id manual = seed.AddFolder(work1, BM::End, L"My own", 502);
    const BM::Id inside = seed.Add(manual, BM::End, L"Inside", L"https://inside.example/", 503);
    BI::FolderMap map; map.Prefer("chromium:Chrome/Default|g:work", {work1, 500});

    {   // 0. Red reference: the 9ef33c5 way (move + remove in memory, then the generic three-way save) loses the link.
        Case c = Setup(L"p1-red", seed, map);
        for (BM::Id k : std::vector<BM::Id>(c.mine.Find(work1)->children)) c.mine.Move(k, BM::BarRoot, BM::End);
        c.mine.Remove(work1);
        std::map<BM::Id, BM::Id> remap;
        ProfileStore::Commit<BM::Store>(c.data, c.file, c.base, c.mine, [&](const BM::Store& b, const BM::Store& m, const BM::Store& t) { return BM::Store::Merge(b, m, t, &remap); }, 5000, {"browser-bookmarks 1"});
        OK(!Load(c.file).Find(link));   // the defect the transactional promotion exists for (review 9ef33c5 P1)
    }
    {   // 1. The reviewer's case: map-proven folder, actual save, reload — the link survives, on the bar, same id.
        Case c = Setup(L"p1", seed, map);
        const auto r = BP::Commit(c.data, c.file, c.base, c.mine, work1, OwnedBy(c.map));
        const BM::Store disk = Load(c.file);
        OK(r.Saved() && r.moved.size() == 2 && r.removed.has_value());
        OK(disk.Find(link) && disk.Find(link)->parent == BM::BarRoot && disk.Find(manual) && disk.Find(manual)->parent == BM::BarRoot && disk.Find(inside) && disk.Find(inside)->parent == manual);
        OK(!disk.Find(work1) && (Titles(disk, BM::BarRoot) == std::vector<std::wstring>{L"mine", L"Doc", L"My own"}) && disk == c.mine && c.base == c.mine);
        // Undo, saved for real, reloaded: everything back where it was, same ids and order.
        OK(UndoAndSave(c, r) == ProfileStore::Result::Written);
        const BM::Store back = Load(c.file);
        OK(back.Find(work1) && back.Find(link)->parent == work1 && back.Find(manual)->parent == work1 && (Titles(back, work1) == std::vector<std::wstring>{L"Doc", L"My own"}) && (Titles(back, BM::BarRoot) == std::vector<std::wstring>{L"mine"}));
        OK(back.Find(inside)->parent == manual && back.Find(mine0));
    }
    {   // 2. Another window added a child this window never saw: it moves too; nothing is deleted with the folder.
        Case c = Setup(L"p1-other", seed, map);
        BM::Store other = seed; const BM::Id unseen = other.Add(work1, BM::End, L"Unseen", L"https://unseen.example/", 600);
        CHECK(ProfileStore::WriteAtomic(c.file, Bytes(other)));
        const auto r = BP::Commit(c.data, c.file, c.base, c.mine, work1, OwnedBy(c.map));
        const BM::Store disk = Load(c.file);
        OK(r.Saved() && r.moved.size() == 3 && disk.Find(unseen) && disk.Find(unseen)->parent == BM::BarRoot && disk.Find(link)->parent == BM::BarRoot && !disk.Find(work1));
        OK(UndoAndSave(c, r) == ProfileStore::Result::Written && Load(c.file).Find(unseen)->parent == work1);
    }
    {   // 3. The save fails (file read-only): nothing written, nothing changed in memory, no undo to record.
        Case c = Setup(L"p1-fail", seed, map);
        const std::string before = Bytes(seed);
        SetFileAttributesW(c.file.c_str(), FILE_ATTRIBUTE_READONLY);
        const auto r = BP::Commit(c.data, c.file, c.base, c.mine, work1, OwnedBy(c.map));
        SetFileAttributesW(c.file.c_str(), FILE_ATTRIBUTE_NORMAL);
        std::string after; ProfileStore::ReadAll(c.file, after);
        OK(!r.Saved() && r.moved.empty() && !r.removed && after == before && c.mine == seed && c.base == seed);
    }
    {   // 4. P2: a folder the person made inside an imported one is not import-owned: emptied, it stays (empty).
        Case c = Setup(L"p2-manual", seed, map);
        const auto r = BP::Commit(c.data, c.file, c.base, c.mine, manual, OwnedBy(c.map));
        const BM::Store disk = Load(c.file);
        OK(r.Saved() && r.moved.size() == 1 && !r.removed && disk.Find(manual) && disk.Find(manual)->children.empty() && disk.Find(inside)->parent == BM::BarRoot);
        OK(UndoAndSave(c, r) == ProfileStore::Result::Written && Load(c.file).Find(inside)->parent == manual);
    }
    {   // 5. P2: v0.3.376 HTML — the old bar folder inside the mapped group has no identity of its own: it stays.
        BM::Store s; const BM::Id group = s.AddFolder(BM::OtherRoot, BM::End, L"Importés d'un fichier HTML", 5), oldBar = s.AddFolder(group, BM::End, L"Bookmarks bar", 6);
        const BM::Id a = s.Add(oldBar, BM::End, L"a", L"https://a.example/", 7);
        BI::FolderMap m; m.Prefer("html", {group, 5});
        Case c = Setup(L"p2-legacy", s, m);
        const auto r = BP::Commit(c.data, c.file, c.base, c.mine, oldBar, OwnedBy(c.map));
        const BM::Store disk = Load(c.file);
        OK(r.Saved() && !r.removed && disk.Find(oldBar) && disk.Find(a)->parent == BM::BarRoot);
        // The mapped group itself, emptied by the person through the same command, is import-owned: removed.
        BM::Store s2; const BM::Id g2 = s2.AddFolder(BM::OtherRoot, BM::End, L"Importés d'un fichier HTML", 5); const BM::Id l2 = s2.Add(g2, BM::End, L"l", L"https://l.example/", 6);
        Case c2 = Setup(L"p2-group", s2, m);
        const auto r2 = BP::Commit(c2.data, c2.file, c2.base, c2.mine, g2, OwnedBy(c2.map));
        OK(r2.Saved() && r2.removed && !Load(c2.file).Find(g2) && Load(c2.file).Find(l2)->parent == BM::BarRoot);
    }
    std::printf("bookmark promote: %d checks passed\n", checks);
    return 0;
}
