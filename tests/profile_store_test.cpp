// Interprocess profile store (src/profile-store.h) and the bookmarks three-way merge.
// Unit cases on Bookmarks::Store::Merge, then adversarial runs with REAL processes (this exe
// re-launched as children): two writers at once on the same profile, a writer killed while
// holding the lock, and a reader that must never see a half-written file.
#include "../src/bookmarks.h"
#include "../src/profile-store.h"
#include "../src/window-sessions.h"
#include "../src/trace-erase.h"
#include <fstream>
#include <chrono>
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
    // Child: start like a window (Adopt), report what it took, keep it until `release` is signalled.
    if (argc >= 6 && std::wstring(argv[1]) == L"adopt") {
        const std::wstring dir = argv[2], requested = std::wstring(argv[3]) == L"-" ? L"" : argv[3];
        HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, argv[4]), release = OpenEventW(SYNCHRONIZE, FALSE, argv[5]);
        if (go) WaitForSingleObject(go, 10000);
        auto a = WindowSessions::Adopt(dir, L"", requested, true);
        std::ofstream out(std::filesystem::path(dir).parent_path() / (L"result-" + std::to_wstring(GetCurrentProcessId())));
        std::string line(a.id.begin(), a.id.end()); out << line << "\n";
        for (auto& id : a.relaunch) out << std::string(id.begin(), id.end()) << "\n";
        out.close();
        if (release) WaitForSingleObject(release, 20000);
        return a.requested ? 10 : a.adopted ? 11 : 12;
    }
    // Child: an open window (claimed + session file) that closes when `go` is signalled.
    if (argc >= 5 && std::wstring(argv[1]) == L"release") {
        HANDLE live = WindowSessions::Claim(argv[2], argv[3]);
        ProfileStore::WriteAtomic(WindowSessions::File(argv[2], argv[3]), "browser-session-2\n0\n1\n\"https://exemple.test/r\"\n0\n");
        HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, argv[4]); if (go) WaitForSingleObject(go, 10000);
        return WindowSessions::Release(argv[2], argv[3], false, live) ? 1 : 0;
    }
    if (argc >= 4 && std::wstring(argv[1]) == L"release-hold") {
        HANDLE live = WindowSessions::Claim(argv[2], argv[3]); (void)live;
        ProfileStore::WriteAtomic(WindowSessions::File(argv[2], argv[3]), "browser-session-2\n0\n1\n\"https://exemple.test/c\"\n0\n");
        Sleep(INFINITE); return 0;
    }
    // Child « window B »: loads the history file, records one visit, commits through the store.
    if (argc >= 5 && std::wstring(argv[1]) == L"add-visit") {
        const std::wstring dir = argv[2], path = dir + L"\\history"; std::string bytes; ProfileStore::ReadAll(path, bytes); std::istringstream in(bytes);
        AddressBar::History base = AddressBar::History::Read(in), mine = base; mine.Record(argv[3], L"B", _wtoi64(argv[4]));
        return ProfileStore::CommitWith<AddressBar::History>(dir, path, base, mine, [](std::istream& i, bool*) { return AddressBar::History::Read(i); }, [](std::ostream& o, const AddressBar::History& h) { h.Write(o); },
            [](const AddressBar::History& b0, const AddressBar::History& m0, const AddressBar::History& t0) { return AddressBar::History::Merge(b0, m0, t0); }) == ProfileStore::Result::Written ? 0 : 1;
    }
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
    {   // Downloads list: live rows stay, removals and other windows' rows respected.
        namespace DL = NativeChrome::Downloads;
        auto row = [](int id, const wchar_t* path, std::int64_t started) { DL::Item i; i.id = id; i.path = path; i.uri = L"https://d.test/f"; i.state = DL::State::Completed; i.total = 10; i.started = started; return i; };
        std::vector<DL::Item> base{row(1, L"C:/d/a", 100), row(2, L"C:/d/b", 90)}, mine = base, theirs = base;
        mine.erase(mine.begin() + 1); DL::Item live = row(9, L"C:/d/live", 300); live.state = DL::State::InProgress; mine.insert(mine.begin(), live);
        theirs.erase(theirs.begin()); theirs.insert(theirs.begin(), row(5, L"C:/d/c", 200));
        int next = 50; const auto r = DL::MergeHistory(base, mine, theirs, next);
        std::vector<std::wstring> paths; for (auto& i : r) paths.push_back(i.path);
        Check(paths == std::vector<std::wstring>{L"C:/d/live", L"C:/d/c"} && r[1].id == 50, "downloads: this window's live row kept, both removals respected, the other window's row joins with a fresh id");
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
    // ---- window sessions: adoption, release, crash, handover --------------------------------------
    {
        const std::wstring sdata = std::filesystem::absolute(L"build/profile-store/sessions-profile").wstring();
        std::filesystem::remove_all(std::filesystem::path(sdata).parent_path() / L"results", ec);
        std::filesystem::remove_all(sdata, ec); std::filesystem::create_directories(WindowSessions::Dir(sdata), ec);
        for (auto& e : std::filesystem::directory_iterator(std::filesystem::path(sdata).parent_path(), ec)) if (e.path().filename().wstring().rfind(L"result-", 0) == 0) std::filesystem::remove(e.path(), ec);
        const std::vector<std::wstring> orphans{L"wa", L"wb", L"wc"};
        for (auto& id : orphans) ProfileStore::WriteAtomic(WindowSessions::File(sdata, id), "browser-session 1\nhttps://exemple.test/" + std::string(id.begin(), id.end()) + "\n");
        const std::wstring go = L"Local\\bps-go-" + std::to_wstring(GetCurrentProcessId()), rel = L"Local\\bps-rel-" + std::to_wstring(GetCurrentProcessId());
        HANDLE goE = CreateEventW(nullptr, TRUE, FALSE, go.c_str()), relE = CreateEventW(nullptr, TRUE, FALSE, rel.c_str());
        auto adopt = [&](const std::wstring& requested) { return spawn(L"adopt \"" + sdata + L"\" " + requested + L" " + go + L" " + rel); };
        PROCESS_INFORMATION p1 = adopt(L"-"), p2 = adopt(L"-");
        Sleep(300); SetEvent(goE);   // both start at the same moment
        auto results = [&]() { std::vector<std::vector<std::wstring>> r; for (auto& e : std::filesystem::directory_iterator(std::filesystem::path(sdata).parent_path(), ec)) if (e.path().filename().wstring().rfind(L"result-", 0) == 0) { std::ifstream in(e.path()); std::string l; std::vector<std::wstring> v; while (std::getline(in, l)) if (!l.empty()) v.push_back(std::wstring(l.begin(), l.end())); r.push_back(v); } return r; };
        for (int i = 0; i < 100 && results().size() < 2; ++i) Sleep(50);
        const auto r = results(); std::multiset<std::wstring> adopted, relaunched;
        for (auto& v : r) if (!v.empty()) { adopted.insert(v[0]); for (size_t k = 1; k < v.size(); ++k) relaunched.insert(v[k]); }
        std::multiset<std::wstring> all; for (auto& id : adopted) if (std::find(orphans.begin(), orphans.end(), id) != orphans.end()) all.insert(id);
        all.insert(relaunched.begin(), relaunched.end());
        Check(r.size() == 2 && adopted.size() == 2 && *adopted.begin() != *adopted.rbegin(), "two simultaneous starts adopt two different sessions");
        Check(all == std::multiset<std::wstring>(orphans.begin(), orphans.end()), "every orphan session is adopted or relaunched exactly once (no double adoption, no double relaunch)");
        Check(WindowSessions::OthersOpen(sdata, L"nobody") && WindowSessions::Orphans(sdata).empty(), "sessions being relaunched are reserved: no third start can take them");
        std::wstring firstId, secondId; for (auto& id : adopted) if (std::find(orphans.begin(), orphans.end(), id) != orphans.end()) firstId = secondId = id;
        HANDLE mine = nullptr;   // this test plays a third, independent open window
        HANDLE none = nullptr; Check(!WindowSessions::Release(sdata, firstId, true, none) && std::filesystem::exists(WindowSessions::File(sdata, firstId)), "update handoff (#60): the session is kept");
        (void)mine;
        // Crash: kill the adopter of `secondId` before any write: its session comes back as an orphan.
        SetEvent(relE); WaitForSingleObject(p1.hProcess, 10000); WaitForSingleObject(p2.hProcess, 10000); CloseHandle(p1.hProcess); CloseHandle(p2.hProcess);
        bool back = false; for (auto& id : WindowSessions::Orphans(sdata)) if (id == secondId) back = true;
        Check(back, "a window gone without closing (crash) leaves its session to be restored");
        // Handover (A7/D4): written before the launch; a launch that dies before claiming keeps it.
        const std::wstring hand = WindowSessions::NewId();
        Check(WindowSessions::WriteHandover(sdata, hand, "browser-session 1\nhttps://exemple.test/transmis\n"), "handover written durably before the launch");
        ResetEvent(goE); ResetEvent(relE); SetEvent(goE);
        PROCESS_INFORMATION h1 = adopt(hand);
        for (int i = 0; i < 100 && !WindowSessions::Live(sdata, hand); ++i) Sleep(20);
        PROCESS_INFORMATION h2 = adopt(hand);   // a retry while the first one is up
        WaitForSingleObject(h2.hProcess, 0); Sleep(500);
        DWORD c2 = 0; SetEvent(relE); WaitForSingleObject(h2.hProcess, 10000); GetExitCodeProcess(h2.hProcess, &c2);
        Check(c2 == 12, "a duplicate launch for a session already open opens a fresh window, never a second one on that session");
        TerminateProcess(h1.hProcess, 9); WaitForSingleObject(h1.hProcess, 5000); CloseHandle(h1.hProcess); CloseHandle(h2.hProcess);
        std::string kept; ProfileStore::ReadAll(WindowSessions::File(sdata, hand), kept);
        Check(kept.find("transmis") != std::string::npos && !WindowSessions::Live(sdata, hand), "a handed-over window killed before saving: its URLs are kept (orphan, restored next start)");
        // Restoration off: the newest orphan is adopted (kept for later), the others are dropped.
        auto off = WindowSessions::Adopt(sdata, L"", L"", false);
        Check(off.adopted && off.relaunch.empty() && WindowSessions::Orphans(sdata).empty(), "restoration off: nothing relaunched, no orphan left to resurrect later");
        CloseHandle(off.live); CloseHandle(goE); CloseHandle(relE);
        // Decision S2 (deterministic): a window closed while another is open never comes back; of windows
        // closing at once exactly one, the last to take the lock, keeps its session.
        auto reset = [&]() { for (auto& e : std::filesystem::directory_iterator(WindowSessions::Dir(sdata), ec)) std::filesystem::remove(e.path(), ec); };
        auto open2 = [&](HANDLE& hx, HANDLE& hy) {
            hx = WindowSessions::Claim(sdata, L"wx"); hy = WindowSessions::Claim(sdata, L"wy");
            ProfileStore::WriteAtomic(WindowSessions::File(sdata, L"wx"), "browser-session-2\n0\n1\n\"https://exemple.test/x\"\n0\n");
            ProfileStore::WriteAtomic(WindowSessions::File(sdata, L"wy"), "browser-session-2\n0\n1\n\"https://exemple.test/y\"\n0\n");
        };
        auto only = [&](const std::wstring& id) { auto l = WindowSessions::List(sdata); std::string text; ProfileStore::ReadAll(WindowSessions::File(sdata, id), text); return l.size() == 1 && l[0].id == id && text.find("exemple.test/" + std::string(1, (char)id.back())) != std::string::npos; };
        for (int gapMs : {0, 2000}) {
            reset(); HANDLE hx = nullptr, hy = nullptr; open2(hx, hy);
            const bool first = WindowSessions::Release(sdata, L"wx", false, hx); Sleep(gapMs); const bool last = WindowSessions::Release(sdata, L"wy", false, hy);
            Check(first && !last && only(L"wy") && !hx && !hy, gapMs ? "closed 2 s apart: only the last window keeps its session (with its URL)" : "closed 0 s apart: only the last window keeps its session (with its URL)");
            auto a = WindowSessions::Adopt(sdata, L"", L"", true);
            Check(a.id == L"wy" && a.relaunch.empty(), "next start: the last window only, the other one is not resurrected");
            CloseHandle(a.live);
        }
        {   // Two real processes releasing at the same instant (barrier): exactly one keeps its session.
            reset(); const std::wstring go = L"Local\\bps-rel2-" + std::to_wstring(GetCurrentProcessId()); HANDLE goE = CreateEventW(nullptr, TRUE, FALSE, go.c_str());
            PROCESS_INFORMATION a = spawn(L"release \"" + sdata + L"\" wx " + go), b = spawn(L"release \"" + sdata + L"\" wy " + go);
            for (int i = 0; i < 100 && !(WindowSessions::Live(sdata, L"wx") && WindowSessions::Live(sdata, L"wy")); ++i) Sleep(20);
            SetEvent(goE); WaitForSingleObject(a.hProcess, 10000); WaitForSingleObject(b.hProcess, 10000);
            DWORD ca = 9, cb = 9; GetExitCodeProcess(a.hProcess, &ca); GetExitCodeProcess(b.hProcess, &cb); CloseHandle(a.hProcess); CloseHandle(b.hProcess); CloseHandle(goE);
            const auto l = WindowSessions::List(sdata);
            Check(ca + cb == 1 && l.size() == 1, "two processes closing at once: exactly one session kept, never both lost, never both kept");
        }
        {   // Crash before release: the session stays (restored); crash after release of another: last one kept.
            reset(); PROCESS_INFORMATION h = spawn(L"release-hold \"" + sdata + L"\" wcrash");
            for (int i = 0; i < 100 && !WindowSessions::Live(sdata, L"wcrash"); ++i) Sleep(20);
            TerminateProcess(h.hProcess, 9); WaitForSingleObject(h.hProcess, 5000); CloseHandle(h.hProcess);
            auto a = WindowSessions::Adopt(sdata, L"", L"", true);
            Check(a.id == L"wcrash", "a window that crashed (no release) is restored");
            CloseHandle(a.live);
        }
    }
    {   // Decision S3 on bookmarks: a future format is never overwritten (same rule as the vault).
        const std::wstring f = dir + L"\\bookmarks-s3"; const std::string future = "browser-bookmarks 2\nnew\n"; ProfileStore::WriteAtomic(f, future);
        BM::Store b0, m0; m0.Add(BM::BarRoot, BM::End, L"Ici", L"https://ici.test/", 1); const BM::Store mBefore = m0;
        const auto res = ProfileStore::Commit<BM::Store>(dir, f, b0, m0, Merge, 5000, {"browser-bookmarks 1"});
        std::string after; ProfileStore::ReadAll(f, after);
        Check(res == ProfileStore::Result::UnknownFormat && after == future && m0 == mBefore && b0 == BM::Store{}, "S3 bookmarks: a future format is refused, bytes and memory untouched");
        std::filesystem::remove(f, ec);
    }
    {   // A5 across windows (review S5): window A erases a period while window B (a real process) added a visit A never saw.
        using H = AddressBar::History; const std::wstring hdir = dir + L"\\a5"; std::filesystem::create_directories(hdir, ec); const std::wstring hp = hdir + L"\\history";
        H seed; seed.Record(L"https://ancien.test/", L"old", 100); seed.Record(L"https://recent.test/", L"recent", 1000);
        { std::ostringstream o; seed.Write(o); ProfileStore::WriteAtomic(hp, o.str()); }
        auto load = [&]() { std::string b; ProfileStore::ReadAll(hp, b); std::istringstream in(b); return H::Read(in); };
        auto has = [](const H& h, const wchar_t* u) { for (const auto& v : h.Items()) if (v.url == u) return true; return false; };
        H aBase = load(), aMine = aBase;   // window A loaded before B's visit
        H bBase = load(), bMine = bBase;   // window B (in memory) also open
        PROCESS_INFORMATION child = spawn(L"add-visit \"" + hdir + L"\" https://b-nouveau.test/ 2000"); WaitForSingleObject(child.hProcess, 10000); DWORD cc = 9; GetExitCodeProcess(child.hProcess, &cc); CloseHandle(child.hProcess);
        Check(cc == 0 && has(load(), L"https://b-nouveau.test/"), "fixture: window B (real process) recorded a visit after A loaded");
        const auto out = TraceErase::History(dir, hp, 500, aMine, aBase);
        const H disk = load();
        Check(out == TraceErase::Outcome::Done && !has(disk, L"https://recent.test/") && !has(disk, L"https://b-nouveau.test/") && has(disk, L"https://ancien.test/"), "A5: the period is erased on disk, including B's visit A never saw; outside the period kept");
        Check(!has(aMine, L"https://b-nouveau.test/") && has(aMine, L"https://ancien.test/"), "A5: window A's suggestions match the disk");
        // Window B saves/activates later: nothing erased comes back.
        bMine.Record(L"https://ancien.test/", L"old", 3000);   // B keeps browsing outside the erase
        ProfileStore::CommitWith<H>(dir, hp, bBase, bMine, [](std::istream& i, bool*) { return H::Read(i); }, [](std::ostream& o, const H& h) { h.Write(o); }, [](const H& b0, const H& m0, const H& t0) { return H::Merge(b0, m0, t0); });
        const H after = load();
        Check(!has(after, L"https://recent.test/") && !has(after, L"https://b-nouveau.test/") && has(after, L"https://ancien.test/") && !has(bMine, L"https://recent.test/"), "A5: window B's next save resurrects nothing and its memory drops the erased entries");
        // Busy and unknown format: honest outcomes, nothing changed.
        { HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr), done = CreateEventW(nullptr, TRUE, FALSE, nullptr); struct Ctx { std::wstring d; HANDLE r, x; } ctx{dir, ready, done};
          HANDLE th = CreateThread(nullptr, 0, [](LPVOID q) -> DWORD { auto* c = static_cast<Ctx*>(q); ProfileStore::Lock l(c->d); SetEvent(c->r); WaitForSingleObject(c->x, 10000); return 0; }, &ctx, 0, nullptr);
          WaitForSingleObject(ready, 5000); std::string before; ProfileStore::ReadAll(hp, before); H m = aMine, b0 = aBase;
          const auto busy = TraceErase::History(dir, hp, 0, m, b0, 200); std::string now2; ProfileStore::ReadAll(hp, now2);
          Check(busy == TraceErase::Outcome::Busy && now2 == before && m == aMine, "A5: lock busy reported, disk and memory unchanged");
          SetEvent(done); WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(ready); CloseHandle(done); }
        ProfileStore::WriteAtomic(hp, "browser-history 9\nfuture\n");
        { H m = aMine, b0 = aBase; Check(TraceErase::History(dir, hp, 0, m, b0) == TraceErase::Outcome::UnknownFormat && m == aMine, "A5: an unknown history format is not erased silently (reported, untouched)"); std::string t; ProfileStore::ReadAll(hp, t); Check(t == "browser-history 9\nfuture\n", "A5: the unknown file is byte-identical"); }
        // Known format but damaged (a malformed line outside the period; a cut last line): never rewritten
        // from what could be read, bytes preserved (review S3 / A5).
        { H good; good.Record(L"https://hors-periode.test/", L"o", 50); good.Record(L"https://dans-periode.test/", L"i", 900); std::ostringstream o; good.Write(o);
          const std::string damagedLine = o.str() + "pas\tune\tligne\n"; ProfileStore::WriteAtomic(hp, damagedLine);
          H m = aMine, b0 = aBase; Check(TraceErase::History(dir, hp, 500, m, b0) == TraceErase::Outcome::Failed && m == aMine, "A5: a damaged line in a known history format fails, nothing erased from memory");
          std::string t; ProfileStore::ReadAll(hp, t); Check(t == damagedLine, "A5: the damaged file is byte-identical (no data outside the period lost)");
          const std::string cut = o.str().substr(0, o.str().size() - 3); ProfileStore::WriteAtomic(hp, cut);
          H m2 = aMine, b2 = aBase; Check(TraceErase::History(dir, hp, 500, m2, b2) == TraceErase::Outcome::Failed, "A5: a file cut mid-line fails"); std::string t2; ProfileStore::ReadAll(hp, t2); Check(t2 == cut, "A5: the cut file is byte-identical");
          // A real write failure: the temp name is occupied by a directory.
          ProfileStore::WriteAtomic(hp, o.str()); const std::wstring tmp = hp + L".tmp" + std::to_wstring(GetCurrentProcessId()); std::filesystem::create_directories(tmp, ec);
          H m3 = aMine, b3 = aBase; Check(TraceErase::History(dir, hp, 500, m3, b3) == TraceErase::Outcome::Failed && m3 == aMine, "A5: a real write failure is reported, memory unchanged");
          std::string t3; ProfileStore::ReadAll(hp, t3); Check(t3 == o.str(), "A5: after the failed write the file is intact"); std::filesystem::remove_all(tmp, ec);
          // The regular history save never rewrites a damaged file either.
          ProfileStore::WriteAtomic(hp, damagedLine); ProfileStore::DamagedStampForTest() = L"histtest";
          H hb = aBase, hm = aMine; hm.Record(L"https://nouveau.test/", L"n", 5000);
          const auto rs = ProfileStore::CommitWith<H>(dir, hp, hb, hm, [](std::istream& i, bool* dmg) { std::string b((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>()); std::istringstream i2(b); auto h = H::Read(i2); if (dmg) *dmg = !b.empty() && ProfileStore::RecordLines(b) != h.Items().size(); return h; }, [](std::ostream& w, const H& h) { h.Write(w); }, [](const H& x, const H& y, const H& z) { return H::Merge(x, y, z); }, 5000, {"browser-history 1"}, false);
          std::string t4; ProfileStore::ReadAll(hp, t4); std::string bk; ProfileStore::ReadAll(hp + L".damaged-histtest", bk);
          Check(rs == ProfileStore::Result::Failed && t4 == damagedLine && bk == damagedLine, "history save: a damaged file is backed up exactly and never rewritten");
          ProfileStore::DamagedStampForTest().clear(); std::filesystem::remove(hp + L".damaged-histtest", ec); std::filesystem::remove(hp + L".damaged-last", ec); }
        // Review F2: a lone surrogate in a page title is written readable; the file stays writable and erasable.
        { H lone; lone.Record(L"https://titre.test/", std::wstring(L"x") + wchar_t(0xD800) + L"y", 10); std::ostringstream o; lone.Write(o); ProfileStore::WriteAtomic(hp, o.str());
          std::string b; ProfileStore::ReadAll(hp, b); std::istringstream in(b); const H back = H::Read(in);
          Check(back.Items().size() == 1 && ProfileStore::RecordLines(b) == 1 && back.Items()[0].title == std::wstring(L"x") + wchar_t(0xFFFD) + L"y", "F2: a lone surrogate title is stored as U+FFFD, never as a damaging line");
          H m = back, b0 = back; Check(TraceErase::History(dir, hp, 0, m, b0) == TraceErase::Outcome::Done, "F2: that history can still be erased"); }
        // Review F7: an erase removes the exact backups of damaged copies of the file.
        { ProfileStore::WriteAtomic(hp + L".damaged-20261112-000000", "browser-history 1\n1\t1\thttps://secret.test/\tS\nbad\n"); ProfileStore::WriteAtomic(hp + L".damaged-last", "x");
          H m = aMine, b0 = aBase; H empty; { std::ostringstream o; empty.Write(o); ProfileStore::WriteAtomic(hp, o.str()); }
          Check(TraceErase::History(dir, hp, 0, m, b0) == TraceErase::Outcome::Done && !std::filesystem::exists(hp + L".damaged-20261112-000000") && !std::filesystem::exists(hp + L".damaged-last"), "F7: an erase leaves no backup copy of the history"); }
        // Downloads: a row added elsewhere in the period goes, rows outside stay, live rows stay.
        namespace DL = NativeChrome::Downloads; const std::wstring dp = hdir + L"\\downloads";
        auto row = [](int id, const wchar_t* path, std::int64_t started) { DL::Item i; i.id = id; i.path = path; i.uri = L"https://d.test/f"; i.state = DL::State::Completed; i.total = 10; i.started = started; return i; };
        std::vector<DL::Item> fileRows{row(1, L"C:/d/vieux", 100), row(2, L"C:/d/recent", 1000)};
        auto writeRows = [&](const std::vector<DL::Item>& r) { std::ostringstream o; DL::WriteHistory(o, r); ProfileStore::WriteAtomic(dp, o.str()); };
        writeRows(fileRows); std::vector<DL::Item> base = fileRows, mine = fileRows; DL::Item live = row(9, L"C:/d/encours", 3000); live.state = DL::State::InProgress; mine.insert(mine.begin(), live);
        fileRows.push_back(row(3, L"C:/d/ailleurs", 2000)); writeRows(fileRows);   // another window's row A never saw
        int next = 100; const auto o2 = TraceErase::Downloads(dir, dp, 500, mine, base, next);
        std::string db; ProfileStore::ReadAll(dp, db); std::istringstream din(db); int ig = 0; const auto onDisk = DL::ReadHistory(din, ig);
        bool vieux = false, other = false; for (auto& d : onDisk) { if (d.path == L"C:/d/vieux") vieux = true; if (d.path == L"C:/d/ailleurs" || d.path == L"C:/d/recent") other = true; }
        Check(o2 == TraceErase::Outcome::Done && vieux && !other && !mine.empty() && mine.front().path == L"C:/d/encours", "A5 downloads: the period erased on disk (another window's row too), older kept, live row untouched");
    }
    bool leftovers = false; for (auto& e : std::filesystem::directory_iterator(dir, ec)) if (e.path().wstring().find(L".tmp") != std::wstring::npos) leftovers = true;
    Check(!leftovers, "no temp file left behind");
    std::printf(failures ? "FAIL profile store: %d failures\n" : "PASS profile store: merge, 2 processes, crash, torn reads\n", failures);
    return failures ? 1 : 0;
}
