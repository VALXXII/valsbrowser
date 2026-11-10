// Interprocess profile store (src/profile-store.h) and the bookmarks three-way merge.
// Unit cases on Bookmarks::Store::Merge, then adversarial runs with REAL processes (this exe
// re-launched as children): two writers at once on the same profile, a writer killed while
// holding the lock, and a reader that must never see a half-written file.
#include "../src/bookmarks.h"
#include "../src/profile-store.h"
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace BM = Bookmarks;
static int failures = 0;
static void Check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static BM::Store Merge(const BM::Store& b, const BM::Store& m, const BM::Store& t) { return BM::Store::Merge(b, m, t); }
static BM::Store Load(const std::wstring& path) { std::string bytes; ProfileStore::ReadAll(path, bytes); std::istringstream in(bytes); return BM::Store::Read(in); }
static std::set<std::wstring> Titles(const BM::Store& s) { std::set<std::wstring> t; for (BM::Id root : {BM::BarRoot, BM::OtherRoot}) for (BM::Id top : s.Root(root).children) for (BM::Id id : s.Subtree(top)) t.insert(s.Find(id)->title); return t; }

// Child: `n` transactions adding "<tag>-i"; every third one also removes its own "<tag>-(i-2)".
static int Writer(const std::wstring& dir, const std::wstring& tag, int n) {
    const std::wstring path = dir + L"\\bookmarks";
    BM::Store base = Load(path), mine = base;
    for (int i = 0; i < n; ++i) {
        mine.Add(BM::BarRoot, BM::End, tag + L"-" + std::to_wstring(i), L"https://exemple.test/" + tag + L"/" + std::to_wstring(i), 1000 + i);
        if (i % 3 == 2) for (BM::Id id : mine.Root(BM::BarRoot).children) if (mine.Find(id)->title == tag + L"-" + std::to_wstring(i - 2)) { mine.Remove(id); break; }
        if (ProfileStore::Commit<BM::Store>(dir, path, base, mine, Merge, 20000) == ProfileStore::Result::Failed) return 2;
    }
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 5 && std::wstring(argv[1]) == L"writer") return Writer(argv[2], argv[3], _wtoi(argv[4]));
    if (argc >= 4 && std::wstring(argv[1]) == L"hold") {   // take the lock, say so, then wait to be killed
        ProfileStore::Lock lock(argv[2]); HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]);
        if (!lock.Held() || !ready) return 3;
        SetEvent(ready); Sleep(INFINITE); return 0;
    }

    // ---- unit: three-way merge -------------------------------------------------------------
    {
        BM::Store base; const BM::Id a = base.Add(BM::BarRoot, BM::End, L"A", L"https://a.test/", 1); const BM::Id b = base.Add(BM::BarRoot, BM::End, L"B", L"https://b.test/", 2);
        BM::Store mine = base, theirs = base;
        const BM::Id x = mine.Add(BM::BarRoot, BM::End, L"X", L"https://x.test/", 10);
        const BM::Id y = theirs.Add(BM::BarRoot, BM::End, L"Y", L"https://y.test/", 11);
        Check(x == y, "fixture: both windows allocated the same new id");
        std::map<BM::Id, BM::Id> remap; const BM::Store r = BM::Store::Merge(base, mine, theirs, &remap);
        Check(Titles(r) == std::set<std::wstring>{L"A", L"B", L"X", L"Y"} && remap.count(x) == 1 && r.Find(remap[x])->title == L"X" && r.Find(y)->title == L"Y", "both additions kept, colliding id remapped");
        BM::Store theirsDeleted = base; theirsDeleted.Remove(a);
        Check(!Titles(Merge(base, base, theirsDeleted)).count(L"A"), "a deletion in another window is never resurrected by a stale window");
        BM::Store mineDeleted = base; mineDeleted.Remove(b); BM::Store theirsRenamed = base; theirsRenamed.Rename(b, L"B2");
        Check(!Titles(Merge(base, mineDeleted, theirsRenamed)).count(L"B2"), "this window's deletion applies even after a rename elsewhere");
        BM::Store mineRenamed = base; mineRenamed.Rename(a, L"A2"); BM::Store theirsMoved = base; theirsMoved.Move(a, BM::OtherRoot, BM::End);
        const BM::Store rm = Merge(base, mineRenamed, theirsMoved);
        Check(rm.Find(a) && rm.Find(a)->title == L"A2" && rm.Find(a)->parent == BM::OtherRoot, "a rename here and a move there both apply");
        BM::Store mineMoved = base; mineMoved.Move(b, BM::BarRoot, 0);
        Check(Merge(base, mineMoved, base).Root(BM::BarRoot).children.front() == b, "a reorder is replayed");
        // An id reused for a different node elsewhere: this window's edit must not land on it.
        BM::Store gone = base; gone.Remove(a); BM::Store reused; reused.Add(BM::BarRoot, BM::End, L"Nouveau", L"https://n.test/", 99);
        BM::Store stale = base; stale.Rename(a, L"Ancien renommé");
        const BM::Store rr = Merge(base, stale, reused);
        bool untouched = false; for (BM::Id id : rr.Root(BM::BarRoot).children) if (rr.Find(id)->title == L"Nouveau") untouched = true;
        Check(untouched && !Titles(rr).count(L"Ancien renommé"), "an edit never lands on a different node that reused an id");
        BM::Store folderMine = base; const BM::Id f = folderMine.AddFolder(BM::BarRoot, BM::End, L"Dossier", 20); folderMine.Add(f, BM::End, L"Dedans", L"https://in.test/", 21);
        const BM::Store rf = Merge(base, folderMine, base);
        bool nested = false; for (BM::Id id : rf.Subtree(f)) if (rf.Find(id)->title == L"Dedans" && rf.Find(id)->parent == f) nested = true;
        Check(nested, "a new folder and its new child arrive together");
        BM::Store parentGone = base; const BM::Id g = parentGone.AddFolder(BM::BarRoot, BM::End, L"G", 30); BM::Store baseG = parentGone, theirsNoG = parentGone; theirsNoG.Remove(g);
        BM::Store mineInG = baseG; mineInG.Add(g, BM::End, L"Orphelin", L"https://o.test/", 31);
        const BM::Store ro = Merge(baseG, mineInG, theirsNoG); bool rescued = false;
        for (BM::Id id : ro.Root(BM::OtherRoot).children) if (ro.Find(id)->title == L"Orphelin") rescued = true;
        Check(rescued, "an addition into a folder deleted elsewhere is kept in « Autres favoris », not lost");
    }

    // ---- unit: history and settings merges ---------------------------------------------------
    {
        using H = AddressBar::History; H base; base.Record(L"https://a.test/", L"A", 1); base.Record(L"https://b.test/", L"B", 2);
        H mine = base, theirs = base; mine.Record(L"https://a.test/", L"A", 10); theirs.Record(L"https://a.test/", L"A", 11); theirs.Record(L"https://c.test/", L"C", 12);
        H r = H::Merge(base, mine, theirs); int a = 0; bool c = false; for (const auto& v : r.Items()) { if (v.url == L"https://a.test/") a = v.visits; if (v.url == L"https://c.test/") c = true; }
        Check(a == 3 && c, "history: visits from both windows add up, the other window's page kept");
        H gone = base; gone.Remove(L"https://b.test/"); bool back = false; for (const auto& v : H::Merge(base, base, gone).Items()) if (v.url == L"https://b.test/") back = true;
        Check(!back, "history: a page removed in another window is not resurrected");
        H cleared = base; cleared.Clear(); Check(H::Merge(base, cleared, theirs).Items().size() == 1, "history: « Effacer » here removes the pages it knew, keeps only the newer one from elsewhere");
        const std::string b = "browser-settings 1\nzoom-percent=100\nad-block=1\n", m = "browser-settings 1\nzoom-percent=125\nad-block=1\n", t = "browser-settings 1\nzoom-percent=100\nad-block=0\n";
        Check(ProfileStore::MergeKeyValues(b, m, t) == "browser-settings 1\nzoom-percent=125\nad-block=0\n", "settings: each window's own change kept, field by field");
    }
    // ---- adversarial: real processes ---------------------------------------------------------
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring dir = std::filesystem::absolute(L"build/profile-store/profile").wstring();
    std::error_code ec; std::filesystem::remove_all(dir, ec); std::filesystem::create_directories(dir, ec);
    auto spawn = [&](const std::wstring& args) {
        std::wstring cmd = L"\"" + std::wstring(self) + L"\" " + args; STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        if (!CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return PROCESS_INFORMATION{};
        CloseHandle(pi.hThread); return pi;
    };
    {
        const int n = 60;
        PROCESS_INFORMATION a = spawn(L"writer \"" + dir + L"\" alpha " + std::to_wstring(n)), b = spawn(L"writer \"" + dir + L"\" beta " + std::to_wstring(n));
        // A third party keeps reading meanwhile: every read must be a complete file.
        int reads = 0, torn = 0;
        while (WaitForSingleObject(a.hProcess, 0) == WAIT_TIMEOUT || WaitForSingleObject(b.hProcess, 0) == WAIT_TIMEOUT) {
            std::string bytes; if (ProfileStore::ReadAll(dir + L"\\bookmarks", bytes) && !bytes.empty()) { std::istringstream in(bytes); bool damaged = false; BM::Store::Read(in, &damaged); ++reads; if (damaged) ++torn; }
            Sleep(1);
        }
        DWORD ea = 1, eb = 1; GetExitCodeProcess(a.hProcess, &ea); GetExitCodeProcess(b.hProcess, &eb); CloseHandle(a.hProcess); CloseHandle(b.hProcess);
        Check(ea == 0 && eb == 0, "two concurrent writer processes finished their transactions");
        const auto titles = Titles(Load(dir + L"\\bookmarks"));
        bool all = true; int expected = 0;
        for (const wchar_t* tag : {L"alpha", L"beta"}) for (int i = 0; i < n; ++i) {
            const bool removed = i + 2 < n && (i + 2) % 3 == 2;
            const bool present = titles.count(std::wstring(tag) + L"-" + std::to_wstring(i)) == 1;
            if (present == removed) all = false;
            if (!removed) ++expected;
        }
        Check(all && static_cast<int>(titles.size()) == expected, "no lost addition and no resurrected deletion across two processes");
        std::printf("     concurrent reads: %d, torn: %d\n", reads, torn);
        Check(reads > 0 && torn == 0, "a concurrent reader never sees a half-written file");
    }
    {
        const std::wstring eventName = L"Local\\browser-profile-store-test-" + std::to_wstring(GetCurrentProcessId());
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
        PROCESS_INFORMATION h = spawn(L"hold \"" + dir + L"\" " + eventName);
        const bool held = WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0;
        { ProfileStore::Lock busy(dir, 200); Check(held && !busy.Held(), "while another process holds the lock, a transaction waits (no write)"); }
        HANDLE keep = CreateMutexW(nullptr, FALSE, ProfileStore::MutexName(dir).c_str());   // keeps the object alive across the kill
        TerminateProcess(h.hProcess, 9); WaitForSingleObject(h.hProcess, 5000); CloseHandle(h.hProcess); CloseHandle(ready);
        ProfileStore::Lock after(dir, 2000);
        Check(after.Held() && after.Abandoned(), "a writer killed while holding the lock: the next one gets it (abandoned), no deadlock");
        bool damaged = true; { std::string bytes; ProfileStore::ReadAll(dir + L"\\bookmarks", bytes); std::istringstream in(bytes); BM::Store::Read(in, &damaged); }
        Check(!damaged, "after the crash the file is still complete");
        CloseHandle(keep);
    }
    bool leftovers = false; for (auto& e : std::filesystem::directory_iterator(dir, ec)) if (e.path().wstring().find(L".tmp") != std::wstring::npos) leftovers = true;
    Check(!leftovers, "no temp file left behind");
    std::printf(failures ? "FAIL profile store: %d failures\n" : "PASS profile store: merge, 2 processes, crash, torn reads\n", failures);
    return failures ? 1 : 0;
}
