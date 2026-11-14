// Profiles backend (src/profiles.h): registry, names, paths, delete / undo / crash recovery and
// window routing. Every case runs in its own sandbox folder under build\profiles-sandbox, created
// and erased by this test only (the sandbox root is checked before any recursive deletion). Real
// child processes (this exe relaunched) cover concurrent writers, open windows and crashes.
#include "../src/profiles.h"
#include <cstdio>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static int failures = 0;
static void Check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static std::wstring sandboxRoot;

static void WriteFile(const std::wstring& path, const std::string& bytes) { fs::create_directories(fs::path(path).parent_path()); std::ofstream(fs::path(path), std::ios::binary) << bytes; }
static std::string ReadFile(const std::wstring& path) { std::string s; ProfileStore::ReadAll(path, s); return s; }
// A fresh case folder, strictly inside the sandbox (refuses anything else before erasing).
static std::wstring Case(const wchar_t* name) {
    const std::wstring dir = sandboxRoot + L"\\" + name;
    const std::wstring full = Profiles::Detail::Full(dir), root = Profiles::Detail::Full(sandboxRoot);
    if (!Profiles::Detail::StartsWithI(full, root + L"\\") || full.find(L"..") != std::wstring::npos) { std::printf("FAIL refusing to clean %ls\n", full.c_str()); std::exit(2); }
    std::error_code ec; fs::remove_all(full, ec); fs::create_directories(full);
    return full;
}
// Tree snapshot: relative path -> bytes (directories as "<dir>").
static std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> m; std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) { const std::wstring rel = fs::relative(e.path(), dir).wstring(); m[rel] = e.is_directory() ? std::string("<dir>") : ReadFile(e.path().wstring()); }
    return m;
}
static Profiles::Profile Seed(const std::wstring& name, const std::wstring& icon = L"briefcase", int hue = 150) { Profiles::Profile p; p.name = name; p.icon = icon; p.hue = hue; p.intensity = .14f; return p; }
static PROCESS_INFORMATION Spawn(const std::wstring& args) {
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    if (!CreateProcessW(self, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return PROCESS_INFORMATION{};
    CloseHandle(pi.hThread); return pi;
}
static DWORD Wait(PROCESS_INFORMATION& pi, DWORD ms = 60000) { DWORD code = 99; if (!pi.hProcess) return code; WaitForSingleObject(pi.hProcess, ms); GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); pi.hProcess = nullptr; return code; }

// ---- children ----------------------------------------------------------------------------------
static int ChildCreate(const std::wstring& root, const std::wstring& tag, int n) {
    for (int i = 0; i < n; ++i) {
        auto reg = Profiles::Load(root);
        if (Profiles::Create(root, reg, Seed(tag + L" " + std::to_wstring(i))).id.empty()) return 2;
    }
    return 0;
}
// `withSession` false: a freshly opened window that has published its sidecar but not yet written
// its session file (it is written on page ready or close).
static int ChildWindow(const std::wstring& root, const std::wstring& id, const std::wstring& session, const std::wstring& event, bool withSession = true) {
    const auto paths = Profiles::ResolvePaths(root, id);
    HANDLE live = WindowSessions::Claim(paths.dataDir, session); if (!live) return 3;
    fs::create_directories(WindowSessions::Dir(paths.dataDir));
    if (withSession) ProfileStore::WriteAtomic(WindowSessions::File(paths.dataDir, session), "browser-session-2\n0\n1\n\"https://exemple.test/a\"\n0\n");
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = Profiles::WindowClass; RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(Profiles::WindowClass, L"test", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd || !Profiles::PublishWindow(paths, id, session, hwnd, {L"Boîte de réception · Messagerie", L"Météo | Lyon"}, {L"https://mail.example/", L"https://meteo.example/"}, 1)) return 4;
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, event.c_str()); if (go) WaitForSingleObject(go, 30000);
    Profiles::UnpublishWindow(paths, session, hwnd); DestroyWindow(hwnd);
    WindowSessions::Release(paths.dataDir, session, true, live);
    return 0;
}
static int ChildDeleteThenExit(const std::wstring& root, const std::wstring& id) {   // deletes, never finalizes: the process ends inside its undo window
    auto reg = Profiles::Load(root); Profiles::Tombstone t; std::wstring error;
    return Profiles::Delete(root, reg, id, t, error) ? 0 : 5;
}

// A private window in another process: our class, BrowserPrivateWindow = 1, no registry/sidecar/session.
static int ChildPrivate(const std::wstring& event) {
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = Profiles::WindowClass; RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(Profiles::WindowClass, L"private", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 6;
    Profiles::MarkPrivateWindow(hwnd);
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, event.c_str()); if (go) WaitForSingleObject(go, 30000);
    Profiles::UnmarkPrivateWindow(hwnd); DestroyWindow(hwnd); return 0;
}
// The registry and set-aside profiles as data: ids in list order.
static std::vector<std::wstring> Ids(const Profiles::Registry& reg) { std::vector<std::wstring> v; for (const auto& p : reg.items) v.push_back(p.id); return v; }

// Another process creating an environment on `id`: gets `expected` back from AcquireEngineArgs; holds it
// until `event` is set (L"-": exits at once WITHOUT releasing, like a crash).
static int ChildEngine(const std::wstring& root, const std::wstring& id, const std::wstring& wanted, const std::wstring& expected, const std::wstring& event) {
    const auto lease = Profiles::AcquireEngineArgs(root, id, wanted);
    if (!lease.error.empty() || lease.arguments != expected) return 7;
    if (event == L"-") return 0;
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, event.c_str()); if (go) WaitForSingleObject(go, 30000);
    return Profiles::ReleaseEngineArgs(root, id) ? 0 : 8;
}

// A window of another process writing its own window set `rounds` times (slots `<tag>a` active, `<tag>b`
// cached), leaving its set name in <root>\token-<tag>; it stays open until `event` (L"-": exits at once).
static int ChildSetWriter(const std::wstring& root, const std::wstring& tag, const std::wstring& otherProfile, int rounds, const std::wstring& event) {
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = Profiles::WindowClass; RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(Profiles::WindowClass, L"set", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr); if (!hwnd) return 6;
    const std::vector<Profiles::WindowSlot> slots = {{Profiles::DefaultId, tag + L"a", Profiles::WorkspaceState::Active}, {otherProfile, tag + L"b", Profiles::WorkspaceState::Cached}};
    for (int i = 0; i < rounds; ++i) if (Profiles::WriteWindowSet(root, hwnd, slots) != Profiles::SetResult::Written) return 9;
    if (!ProfileStore::WriteAtomic(root + L"\\token-" + tag, Profiles::Detail::Utf8(Profiles::WindowSetName(hwnd)))) return 10;
    if (event != L"-") { HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, event.c_str()); if (go) WaitForSingleObject(go, 30000); }
    DestroyWindow(hwnd); return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 7 && std::wstring(argv[1]) == L"setwriter") return ChildSetWriter(argv[2], argv[3], argv[4], _wtoi(argv[5]), argv[6]);
    if (argc >= 7 && std::wstring(argv[1]) == L"engine") return ChildEngine(argv[2], argv[3], argv[4], argv[5], argv[6]);
    if (argc >= 3 && std::wstring(argv[1]) == L"purge") { Profiles::PurgeTrash(argv[2]); return 0; }
    if (argc >= 3 && std::wstring(argv[1]) == L"private") return ChildPrivate(argv[2]);
    if (argc >= 5 && std::wstring(argv[1]) == L"create") return ChildCreate(argv[2], argv[3], _wtoi(argv[4]));
    if (argc >= 6 && std::wstring(argv[1]) == L"window") return ChildWindow(argv[2], argv[3], argv[4], argv[5]);
    if (argc >= 6 && std::wstring(argv[1]) == L"window-new") return ChildWindow(argv[2], argv[3], argv[4], argv[5], false);
    if (argc >= 4 && std::wstring(argv[1]) == L"delete-exit") return ChildDeleteThenExit(argv[2], argv[3]);

    sandboxRoot = Profiles::Detail::Full(fs::absolute(L"build\\profiles-sandbox").wstring());
    if (sandboxRoot.find(L"\\build\\profiles-sandbox") == std::wstring::npos) { std::printf("FAIL unexpected sandbox %ls\n", sandboxRoot.c_str()); return 2; }
    fs::create_directories(sandboxRoot);
    using namespace Profiles;

    { // First run: the legacy data becomes « Personnel » without anything being moved.
        const auto root = Case(L"first-run");
        WriteFile(root + L"\\history", "browser-history 1\nx\n"); WriteFile(root + L"\\profile\\Default\\Cookies", "cookie"); WriteFile(root + L"\\settings", "browser-settings 1\nzoom-percent=110\n");
        const auto before = Snapshot(root);
        auto reg = Load(root);
        auto after = Snapshot(root); after.erase(L"profiles.txt");
        Check(reg.items.size() == 1 && reg.items[0].id == DefaultId && reg.items[0].name == L"Personnel" && reg.items[0].icon == L"house" && reg.items[0].neutral && !reg.readOnly, "first run: default « Personnel » (house, neutral)");
        Check(after == before, "first run: every legacy file and folder untouched (zero migration)");
        const auto text = ReadFile(root + L"\\profiles.txt");
        Check(text.rfind("browser-profiles 1\n", 0) == 0 && text.find("p=default|house|0|0|1|Personnel") != std::string::npos, "profiles.txt written with the default record");
        auto again = Load(root);
        Check(ReadFile(root + L"\\profiles.txt") == text && again.items.size() == 1, "loading again changes nothing");
        const auto p = ResolvePaths(root, DefaultId);
        Check(p.dataDir == root && p.profileDir == root + L"\\profile" && p.settingsFile == root + L"\\settings" && p.sessionFile == root + L"\\session" && p.logFile.empty(), "default paths are the legacy layout");
        const auto q = ResolvePaths(root, L"p0123456789ab");
        Check(q.dataDir == root + L"\\profiles\\p0123456789ab" && q.profileDir == q.dataDir + L"\\profile" && q.settingsFile == q.dataDir + L"\\settings" && q.logFile == q.dataDir + L"\\browser.log", "other profiles: own folder, own WebView2 data, settings and log");
        Check(ResolvePaths(root, L"..\\evil").dataDir == root && !ValidId(L"..") && !ValidId(L"a\\b") && !ValidId(L"P1") && !ValidId(L"x") && ValidId(L"default"), "ids are never paths");
    }
    { // Fail closed: an unknown format is never overwritten.
        const auto root = Case(L"unknown-format");
        const std::string future = "browser-profiles 9\np=default|house|0|0|1|Personnel|extra\n";
        WriteFile(root + L"\\profiles.txt", future);
        auto reg = Load(root);
        Check(reg.readOnly && reg.error == L"unknown-format" && reg.Find(DefaultId), "unknown registry version: read-only, default still shown");
        Check(!Save(root, reg) && Create(root, reg, Seed(L"Travail")).id.empty() && ReadFile(root + L"\\profiles.txt") == future, "nothing is ever written over it");
    }
    { // Damaged: good records kept, exact backup made, lost folders adopted back.
        const auto root = Case(L"damaged");
        fs::create_directories(root + L"\\profiles\\pfolderonly01");
        WriteFile(root + L"\\profiles.txt", "browser-profiles 1\np=default|house|0|0|1|Personnel\np=pwork00000001|briefcase|150|140|0|Travail\ngarbage line\n");
        auto reg = Load(root);
        bool backup = false; for (const auto& e : fs::directory_iterator(root)) backup = backup || e.path().filename().wstring().rfind(L"profiles.txt.damaged-", 0) == 0;
        Check(!reg.readOnly && reg.Find(L"pwork00000001") && reg.Find(DefaultId), "damaged registry: every readable profile kept");
        Check(backup, "damaged registry copied aside byte for byte before rewriting");
        Check(reg.Find(L"pfolderonly01") && reg.Find(L"pfolderonly01")->name == L"Profil 2", "a profile folder missing from the registry comes back (« Profil 2 »)");
    }
    { // Names (Paper P03).
        Registry reg; Profile a = Seed(L"Travail"); a.id = L"pa"; Profile b = Seed(L"Été"); b.id = L"pb"; reg.items = {a, b};
        Check(ValidateName(reg, L"   ", L"") == NameError::Empty && ValidateName(reg, L"\u00A0", L"") == NameError::Empty, "empty and blank names refused");
        Check(ValidateName(reg, std::wstring(33, L'a'), L"") == NameError::TooLong && ValidateName(reg, std::wstring(32, L'a'), L"") == NameError::None, "33 characters refused, 32 accepted");
        std::wstring emoji; for (int i = 0; i < 32; ++i) emoji += L"\U0001F6B2";
        Check(ValidateName(reg, emoji, L"") == NameError::None && ValidateName(reg, emoji + L"\U0001F6B2", L"") == NameError::TooLong, "a surrogate pair counts as one character");
        Check(ValidateName(reg, L"travail", L"") == NameError::Duplicate && ValidateName(reg, L"  TRAVAIL ", L"") == NameError::Duplicate && ValidateName(reg, L"Ete", L"") == NameError::Duplicate, "duplicates ignore case, spaces and accents");
        Check(ValidateName(reg, L"Travail", L"pa") == NameError::None, "a profile may keep its own name");
        Check(UniqueDefaultName(reg) == L"Nouveau profil" && TrimName(L" a\tb ") == L"a b", "default name and trimming");
        Check(FirstUnusedIcon(reg) == L"house" && FurthestHue(reg) >= 300 && FurthestHue(reg) <= 360, "defaults: first unused icon, hue furthest from the others");
        reg.last = L"pb";
        Check(Active(reg, L"pa") == L"pa" && Active(reg, L"") == L"pb" && Active(reg, L"pzzzz") == L"pb", "active profile: requested, else last used");
        reg.last.clear(); Check(Active(reg, L"") == L"pa", "no last used and no default: the first profile");
    }
    { // Create / Update / stale-window merge.
        const auto root = Case(L"create");
        auto reg = Load(root);
        NameError e = NameError::None;
        const auto work = Create(root, reg, Seed(L"Travail"), &e);
        Check(!work.id.empty() && ValidId(work.id) && fs::is_directory(ResolvePaths(root, work.id).dataDir) && reg.Find(work.id) && reg.items.back().id == work.id, "create: new id, own folder, appended to the list");
        const auto dirs = std::distance(fs::directory_iterator(root + L"\\profiles"), fs::directory_iterator{});
        Check(Create(root, reg, Seed(L"travail"), &e).id.empty() && e == NameError::Duplicate && std::distance(fs::directory_iterator(root + L"\\profiles"), fs::directory_iterator{}) == dirs, "duplicate name: refused, no folder left behind");
        auto stale = Load(root);   // another window's snapshot
        Profile edited = *reg.Find(work.id); edited.name = L"Bureau"; edited.hue = 30;
        Check(Update(root, reg, edited) && Load(root).Find(work.id)->name == L"Bureau", "update: renamed and recoloured");
        const auto fam = Create(root, stale, Seed(L"Famille", L"users-three", 40));
        auto merged = Load(root);
        Check(!fam.id.empty() && merged.Find(fam.id) && merged.Find(work.id)->name == L"Bureau" && merged.Find(work.id)->hue == 30, "a stale window's creation merges without undoing the other window's edit");
        Check(SetLast(root, merged, fam.id) && Load(root).last == fam.id, "last used profile persisted");
    }
    { // Two processes creating profiles at the same time.
        const auto root = Case(L"concurrent");
        Load(root);
        auto a = Spawn(L"create \"" + root + L"\" Alpha 8"), b = Spawn(L"create \"" + root + L"\" Beta 8");
        const DWORD ea = Wait(a), eb = Wait(b);
        auto reg = Load(root); bool folders = true; for (const auto& p : reg.items) if (p.id != DefaultId) folders = folders && fs::is_directory(ResolvePaths(root, p.id).dataDir);
        Check(ea == 0 && eb == 0 && reg.items.size() == 17 && folders, "two writer processes: all 16 profiles kept, each with its folder");
    }
    { // Delete + Undo + Finalize of an ordinary profile.
        const auto root = Case(L"delete-undo");
        auto reg = Load(root);
        const auto work = Create(root, reg, Seed(L"Travail")), fam = Create(root, reg, Seed(L"Famille", L"users-three", 40));
        const auto dir = ResolvePaths(root, work.id).dataDir;
        WriteFile(dir + L"\\history", "browser-history 1\nvisit\n"); WriteFile(dir + L"\\profile\\Default\\Cookies", std::string("\0\1binary", 8));
        WriteFile(dir + L"\\sessions\\w0001", "browser-session-2\n0\n1\n\"https://intranet.example/\"\n0\n");
        const auto before = Snapshot(dir);
        Tombstone t; std::wstring error;
        Check(Delete(root, reg, work.id, t, error) && !reg.Find(work.id) && !Load(root).Find(work.id) && !fs::exists(dir), "delete: profile removed from the list, its folder set aside");
        Check(Detail::SafeTrashPath(root, t.trashDir) && Snapshot(t.trashDir + L"\\data\\" + work.id) == before, "set aside: moved byte for byte into profiles\\.trash");
        Check(t.closedSessions == std::vector<std::wstring>{L"w0001"} && t.hold, "the closed window sessions are kept for « Rouvrir ses fenêtres »");
        PurgeTrash(root);
        Check(fs::exists(t.trashDir), "a tomb inside its undo window is never purged by another start");
        const bool undone = Undo(root, reg, t);
        Check(undone && reg.Find(work.id) && reg.items[1].id == work.id && Snapshot(dir) == before && !fs::exists(t.trashDir), "undo: same place in the list, files byte for byte, sessions back");
        Tombstone t2; Check(Delete(root, reg, fam.id, t2, error), "delete again");
        Check(Finalize(root, t2) && !fs::exists(t2.trashDir) && !fs::exists(ResolvePaths(root, fam.id).dataDir) && fs::exists(dir), "finalize erases only that tomb");
        Tombstone t3; Check(Delete(root, reg, work.id, t3, error) && reg.items.size() == 1, "delete down to one profile");
        Tombstone t4; Check(!Delete(root, reg, DefaultId, t4, error) && error == L"last" && fs::exists(root + L"\\profiles.txt"), "the last profile can never be deleted");
        Finalize(root, t3);
    }
    { // Undo refused on collision; nothing overwritten.
        const auto root = Case(L"collision");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail"));
        Tombstone t; std::wstring error; Delete(root, reg, work.id, t, error);
        const auto dir = ResolvePaths(root, work.id).dataDir; WriteFile(dir + L"\\intrus", "new");
        Check(!Undo(root, reg, t) && ReadFile(dir + L"\\intrus") == "new" && fs::exists(t.trashDir) && !reg.Find(work.id), "undo onto an existing folder: refused, both kept");
        Finalize(root, t);
    }
    { // A file in use: the delete is refused and NOTHING moves.
        const auto root = Case(L"in-use");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail"));
        const auto dir = ResolvePaths(root, work.id).dataDir; WriteFile(dir + L"\\history", "keep");
        HANDLE f = CreateFileW((dir + L"\\history").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Tombstone t; std::wstring error;
        const bool deleted = Delete(root, reg, work.id, t, error);
        CloseHandle(f);
        bool trashEmpty = true; std::error_code ec; for (auto& e : fs::directory_iterator(root + L"\\profiles\\.trash", ec)) { (void)e; trashEmpty = false; }
        Check(!deleted && error == L"in-use" && ReadFile(dir + L"\\history") == "keep" && reg.Find(work.id) && Load(root).Find(work.id) && trashEmpty, "in use: « Rien n'a été effacé », profile and files intact, no tomb left");
    }
    { // Open windows: refused until they are closed.
        const auto root = Case(L"open-windows");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail"));
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, L"Local\\profiles-test-go-1");
        auto child = Spawn(L"window \"" + root + L"\" " + work.id + L" w0002 Local\\profiles-test-go-1");
        std::vector<WindowInfo> wins; for (int i = 0; i < 200 && (wins = LiveWindows(root, work.id)).empty(); ++i) Sleep(20);
        Sleep(100); wins = LiveWindows(root, work.id);
        Check(wins.size() == 1 && wins[0].sessionId == L"w0002" && wins[0].hwnd && wins[0].titles.size() == 2 && wins[0].titles[1] == L"Météo | Lyon" && wins[0].active == 1, "live window: HWND validated (pid, class, profile tag) with its tab titles");
        Check(OpenWindowCount(root, work.id) == 1 && OpenWindowCount(root, DefaultId) == 0, "window counts are per profile");
        Tombstone t; std::wstring error;
        Check(!Delete(root, reg, work.id, t, error) && error == L"open-windows" && reg.Find(work.id), "delete refused while a window of the profile is open");
        // Cold-start restore across profiles: never while another window of any profile is open.
        const auto fam = Create(root, reg, Seed(L"Famille", L"users-three"));
        WriteFile(WindowSessions::File(ResolvePaths(root, fam.id).dataDir, L"w0004"), "browser-session-2\n0\n1\n\"https://famille.example/\"\n0\n");
        Check(ProfilesWithOrphans(root, reg, DefaultId, L"w9999").empty(), "orphans of other profiles are not relaunched while a window is open elsewhere");
        Check(ProfilesWithOrphans(root, reg, work.id, L"w0002") == std::vector<std::wstring>{fam.id}, "this window excluded: the other profile's orphan session is listed for relaunch");
        SetEvent(go); Wait(child); CloseHandle(go);
        Check(LiveWindows(root, work.id).empty(), "closed window no longer listed");
        // A stale sidecar (HWND of another process/class) is never trusted.
        const auto paths = ResolvePaths(root, work.id);
        HANDLE live = WindowSessions::Claim(paths.dataDir, L"w0003");
        ProfileStore::WriteAtomic(paths.dataDir + L"\\sessions\\w0003.hwnd", "browser-window 1\npid=" + std::to_string(GetCurrentProcessId()) + "\nhwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(GetDesktopWindow())) + "\nprofile=" + Detail::Utf8(work.id) + "\n");
        ProfileStore::WriteAtomic(WindowSessions::File(paths.dataDir, L"w0003"), "browser-session-2\n0\n1\n\"https://intranet.example/\"\n0\n");
        wins = LiveWindows(root, work.id);
        Check(wins.size() == 1 && !wins[0].hwnd && wins[0].urls == std::vector<std::wstring>{L"https://intranet.example/"}, "stale or foreign HWND rejected; window still counted from its session");
        CloseHandle(live);
    }
    { // A window just opened: Live mutex + sidecar, NO session file yet (runtime bug from integration).
        const auto root = Case(L"new-window");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail"));
        const auto paths = ResolvePaths(root, work.id);
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, L"Local\\profiles-test-go-2");
        auto child = Spawn(L"window-new \"" + root + L"\" " + work.id + L" w0010 Local\\profiles-test-go-2");
        std::vector<WindowInfo> wins; for (int i = 0; i < 200 && (wins = LiveWindows(root, work.id)).empty(); ++i) Sleep(20);
        Sleep(100); wins = LiveWindows(root, work.id);
        Check(!fs::exists(WindowSessions::File(paths.dataDir, L"w0010")) && fs::exists(paths.dataDir + L"\\sessions\\w0010.hwnd"), "fixture: sidecar published, no session file yet");
        Check(wins.size() == 1 && wins[0].sessionId == L"w0010" && wins[0].hwnd && wins[0].titles.size() == 2 && OpenWindowCount(root, work.id) == 1, "a window without a session file is listed once, HWND validated");
        Check(FocusOrLaunch(root, work.id, root + L"\\no-such-browser.exe"), "routing brings that window forward (no launch attempted: the exe does not exist)");
        Tombstone t; std::wstring error;
        Check(!Delete(root, reg, work.id, t, error) && error == L"open-windows" && reg.Find(work.id), "delete refused while that window is open");
        Check(ProfilesWithOrphans(root, reg, DefaultId, L"w9999").empty(), "cold-start restore sees that window as open");
        // Stale, foreign and malformed sidecars beside it are ignored.
        WriteFile(paths.dataDir + L"\\sessions\\w0011.hwnd", "browser-window 1\npid=1\nhwnd=1\nprofile=" + Detail::Utf8(work.id) + "\n");   // no Live mutex: crashed/closed window
        WriteFile(paths.dataDir + L"\\sessions\\bad id.hwnd", "garbage");
        WriteFile(paths.dataDir + L"\\sessions\\.hwnd", "garbage");
        HANDLE live = WindowSessions::Claim(paths.dataDir, L"w0012");   // open, but its sidecar is garbage
        WriteFile(paths.dataDir + L"\\sessions\\w0012.hwnd", "not a sidecar\x01\x02");
        wins = LiveWindows(root, work.id);
        int validated = 0, unknown = 0; for (const auto& w : wins) (w.hwnd ? validated : unknown)++;
        Check(wins.size() == 2 && validated == 1 && unknown == 1 && OpenWindowCount(root, work.id) == 2, "stale and malformed sidecars ignored; an open window with a garbage sidecar is counted, never trusted");
        CloseHandle(live);
        SetEvent(go); Wait(child); CloseHandle(go);
        Check(LiveWindows(root, work.id).empty() && !fs::exists(paths.dataDir + L"\\sessions\\w0010.hwnd"), "after close: no window listed, sidecar removed");
        Check(Delete(root, reg, work.id, t, error), "delete allowed once the window is closed (stale sidecar does not block it)");
        Finalize(root, t);
    }
    { // Deleting the default profile (legacy root layout): only its own entries move.
        const auto root = Case(L"default-delete");
        WriteFile(root + L"\\history", "h"); WriteFile(root + L"\\history.damaged-20260101", "old"); WriteFile(root + L"\\bookmarks.bak", "b"); WriteFile(root + L"\\profile\\Default\\Cookies", "c");
        WriteFile(root + L"\\sessions\\w0009", "s"); WriteFile(root + L"\\settings", "browser-settings 1\n"); WriteFile(root + L"\\passwords", "v");
        WriteFile(root + L"\\browser.exe", "exe"); WriteFile(root + L"\\assets\\fonts\\a.ttf", "font"); WriteFile(root + L"\\prototype.log", "log"); WriteFile(root + L"\\profilesnotes.txt", "keep");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail"));
        WriteFile(ResolvePaths(root, work.id).dataDir + L"\\history", "w");
        const auto before = Snapshot(root);
        Tombstone t; std::wstring error;
        Check(Delete(root, reg, DefaultId, t, error) && !reg.Find(DefaultId), "the default profile can be deleted when another exists");
        Check(!fs::exists(root + L"\\history") && !fs::exists(root + L"\\history.damaged-20260101") && !fs::exists(root + L"\\profile") && !fs::exists(root + L"\\sessions") && !fs::exists(root + L"\\passwords"), "its own entries and their backups moved");
        Check(ReadFile(root + L"\\browser.exe") == "exe" && ReadFile(root + L"\\assets\\fonts\\a.ttf") == "font" && ReadFile(root + L"\\prototype.log") == "log" && ReadFile(root + L"\\profilesnotes.txt") == "keep" && ReadFile(ResolvePaths(root, work.id).dataDir + L"\\history") == "w", "executable, assets, logs, other files and other profiles untouched");
        Check(t.closedSessions == std::vector<std::wstring>{L"w0009"} && Load(root).Find(DefaultId) == nullptr, "the default is not recreated by a later start");
        Check(Undo(root, reg, t) && reg.items.front().id == DefaultId, "undo puts the default back first");
        auto after = Snapshot(root); auto expected = before; after.erase(L"profiles.txt"); expected.erase(L"profiles.txt"); after.erase(L"profiles"); expected.erase(L"profiles");
        for (auto it = after.begin(); it != after.end();) it = it->first.rfind(L"profiles\\", 0) == 0 ? after.erase(it) : std::next(it);
        for (auto it = expected.begin(); it != expected.end();) it = it->first.rfind(L"profiles\\", 0) == 0 ? expected.erase(it) : std::next(it);
        Check(after == expected, "undo restores the root byte for byte");
        // Partial failure: the second entry is locked, the first must come back.
        HANDLE f = CreateFileW((root + L"\\settings").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Tombstone t2; const bool ok = Delete(root, reg, DefaultId, t2, error); CloseHandle(f);
        Check(!ok && error == L"in-use" && ReadFile(root + L"\\history") == "h" && fs::exists(root + L"\\profile\\Default\\Cookies") && reg.Find(DefaultId), "multi-entry set-aside interrupted by a locked file: every moved entry rolled back");
    }
    { // Crash recovery.
        const auto root = Case(L"crash");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail")), fam = Create(root, reg, Seed(L"Famille", L"users-three"));
        WriteFile(ResolvePaths(root, work.id).dataDir + L"\\history", "w");
        auto child = Spawn(L"delete-exit \"" + root + L"\" " + work.id);
        Check(Wait(child) == 0, "child process deleted a profile and exited inside its undo window");
        Check(!Load(root).Find(work.id) && !fs::exists(ResolvePaths(root, work.id).dataDir) && fs::is_empty(root + L"\\profiles\\.trash"), "owner gone: the next start makes the deletion final (« si le navigateur se ferme »)");
        // Interrupted mid-move (journal « moving »): rolled back, metadata kept.
        const auto famDir = ResolvePaths(root, fam.id).dataDir; WriteFile(famDir + L"\\history", "f");
        const std::wstring tomb = root + L"\\profiles\\.trash\\" + fam.id + L"-1";
        Detail::Journal j; j.state = "moving"; j.profile = *Load(root).Find(fam.id); j.index = 1; j.deadlineWall = Detail::WallNow(); j.moves = {{famDir, tomb + L"\\data\\" + fam.id}};
        // Worst case: the registry already lost the record when the crash happened; it must come back.
        { auto r = Load(root); r.items.erase(std::remove_if(r.items.begin(), r.items.end(), [&](const Profile& p) { return p.id == fam.id; }), r.items.end()); Save(root, r); }
        fs::create_directories(tomb + L"\\data");
        Check(Detail::WriteJournal(root, tomb, j) && MoveFileExW(famDir.c_str(), (tomb + L"\\data\\" + fam.id).c_str(), 0), "fixture: journal « moving » written, folder half-moved");
        auto rec = Load(root);
        Check(ReadFile(famDir + L"\\history") == "f" && rec.Find(fam.id) && rec.Find(fam.id)->name == L"Famille" && !fs::exists(tomb), "crash while moving: files rolled back and the profile listed again with its name");
        // Crash after « aside » but before the registry write: still listed, so it is put back, not erased.
        const std::wstring tomb2 = root + L"\\profiles\\.trash\\" + fam.id + L"-2"; fs::create_directories(tomb2 + L"\\data");
        j.state = "aside"; j.moves = {{famDir, tomb2 + L"\\data\\" + fam.id}}; j.holdName = L"Local\\profiles-test-no-owner";
        Check(Detail::WriteJournal(root, tomb2, j) && MoveFileExW(famDir.c_str(), (tomb2 + L"\\data\\" + fam.id).c_str(), 0), "fixture: journal « aside », registry not yet updated");
        PurgeTrash(root);
        Check(ReadFile(famDir + L"\\history") == "f" && !fs::exists(tomb2), "crash before the registry write: data put back, never erased while still listed");
    }
    { // A data root copied elsewhere (portable → installed migration) recovers inside its own copy.
        const auto src = Case(L"relocate-src");
        auto reg = Load(src); const auto fam = Create(src, reg, Seed(L"Famille", L"users-three"));
        const auto famDir = ResolvePaths(src, fam.id).dataDir; WriteFile(famDir + L"\\history", "f");
        const std::wstring tomb = src + L"\\profiles\\.trash\\" + fam.id + L"-1";
        Detail::Journal j; j.state = "moving"; j.profile = *reg.Find(fam.id); j.index = 1; j.moves = {{famDir, tomb + L"\\data\\" + fam.id}};
        fs::create_directories(tomb + L"\\data");
        Check(Detail::WriteJournal(src, tomb, j) && MoveFileExW(famDir.c_str(), (tomb + L"\\data\\" + fam.id).c_str(), 0), "fixture: interrupted delete in the source root");
        Check(ReadFile(tomb + L"\\journal.txt").find("move=profiles\\") != std::string::npos && ReadFile(tomb + L"\\journal.txt").find(":") == std::string::npos, "journal paths are relative to the data root (no drive, no absolute path)");
        const auto dst = Case(L"relocate-dst"); fs::remove_all(dst);
        fs::copy(src, dst, fs::copy_options::recursive);
        const auto srcBefore = Snapshot(src);
        auto moved = Load(dst);
        Check(ReadFile(ResolvePaths(dst, fam.id).dataDir + L"\\history") == "f" && moved.Find(fam.id) && Snapshot(src) == srcBefore, "the copy rolls back inside its own root; the source root is not touched");
        Detail::Journal bad; bad.state = "aside"; bad.profile = j.profile; bad.moves = {{L"C:\\Windows\\x", tomb + L"\\data\\x"}};
        std::wstring p;
        Check(!Detail::WriteJournal(src, tomb, bad) && !Detail::ResolveInRoot(src, L"..\\x", p) && !Detail::ResolveInRoot(src, L"C:\\x", p) && !Detail::ResolveInRoot(src, L"a\\..\\..\\x", p) && Detail::ResolveInRoot(src, L"profiles\\p1", p), "a journal path can never leave its data root");
    }
    { // Deletion guards: only the tomb itself, never through a junction.
        const auto root = Case(L"guards");
        const std::wstring trash = root + L"\\profiles\\.trash"; fs::create_directories(trash + L"\\ptomb-1");
        Check(Detail::SafeTrashPath(root, trash + L"\\ptomb-1") && !Detail::SafeTrashPath(root, trash) && !Detail::SafeTrashPath(root, root) && !Detail::SafeTrashPath(root, trash + L"\\ptomb-1\\data") && !Detail::SafeTrashPath(root, trash + L"\\..\\..") && !Detail::SafeTrashPath(root, L"C:\\Windows"), "only <root>\\profiles\\.trash\\<tomb> is ever erased");
        WriteFile(root + L"\\outside\\precious", "keep");
        const std::wstring link = trash + L"\\ptomb-1\\link";
        std::wstring cmd = L"cmd.exe /c mklink /J \"" + link + L"\" \"" + root + L"\\outside\" >nul";
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
        const bool junction = (GetFileAttributesW(link.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        Detail::DeleteTree(trash + L"\\ptomb-1");
        Check(junction && !fs::exists(trash + L"\\ptomb-1") && ReadFile(root + L"\\outside\\precious") == "keep", "a junction inside a tomb is removed, its target never entered");
    }
    { // Every store main.cpp keeps in the data dir is part of the default profile's own entries.
        std::set<std::wstring> known(DefaultEntries().begin(), DefaultEntries().end()); bool all = true; int seen = 0;
        std::regex re(R"(dataDir\+L\"\\\\([A-Za-z0-9_.-]+))");
        for (const auto& e : fs::directory_iterator(L"src")) {
            std::ifstream in(e.path()); std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) { ++seen; const std::string n = (*it)[1]; if (!known.count(std::wstring(n.begin(), n.end()))) { std::printf("     missing default entry: %s\n", n.c_str()); all = false; } }
        }
        Check(seen > 0 && all, "DefaultEntries covers every dataDir+L\"\\…\" store in src");
    }
    { // Several pending deletes, undone in ANY order: the list comes back in its original order.
        const auto root = Case(L"multi-undo");
        auto reg = Load(root);
        const auto b = Create(root, reg, Seed(L"Travail")), c = Create(root, reg, Seed(L"Famille", L"users-three", 40)), d = Create(root, reg, Seed(L"Sport", L"bicycle", 260));
        for (const auto& p : reg.items) WriteFile(ResolvePaths(root, p.id).dataDir + L"\\history", "h " + Detail::Utf8(p.id));
        const auto original = Ids(Load(root));
        Check(original.size() == 4 && original[0] == DefaultId, "fixture: Personnel, Travail, Famille, Sport");
        // All 6 x 6 orders of deleting then undoing Travail, Famille, Sport (3 tombs pending at once).
        std::vector<int> del = {0, 1, 2}; const std::wstring ids[3] = {b.id, c.id, d.id}; int combos = 0, good = 0; std::string firstBad;
        do {
            std::vector<int> und = {0, 1, 2};
            do {
                ++combos; Tombstone t[3]; std::wstring error; bool ok = true;
                for (int k : del) { auto r = Load(root); ok = Delete(root, r, ids[k], t[k], error) && ok; }
                ok = ok && Ids(Load(root)) == std::vector<std::wstring>{DefaultId};
                for (int k : und) { auto r = Load(root); ok = Undo(root, r, t[k]) && ok; }
                const auto now = Ids(Load(root));
                bool files = true; for (const auto& id : original) files = files && ReadFile(ResolvePaths(root, id).dataDir + L"\\history") == "h " + Detail::Utf8(id);
                if (ok && now == original && files) ++good;
                else if (firstBad.empty()) { firstBad = "del"; for (int k : del) firstBad += " " + std::to_string(k); firstBad += " undo"; for (int k : und) firstBad += " " + std::to_string(k); }
                for (auto& x : t) if (x.hold) { CloseHandle(x.hold); x.hold = nullptr; }
                if (Ids(Load(root)) != original) break;   // do not cascade a failure into the next cases
            } while (std::next_permutation(und.begin(), und.end()));
        } while (std::next_permutation(del.begin(), del.end()));
        if (!firstBad.empty()) std::printf("     first failing order: %s\n", firstBad.c_str());
        Check(combos == 36 && good == 36, "3 pending deletes, all 36 delete/undo orders: original order and files restored");
        // The case the old index-only reinsert got wrong: delete Travail, then Famille; undo Travail first.
        Tombstone tb, tc; std::wstring error; { auto r = Load(root); Delete(root, r, b.id, tb, error); } { auto r = Load(root); Delete(root, r, c.id, tc, error); }
        { auto r = Load(root); Undo(root, r, tb); } { auto r = Load(root); Undo(root, r, tc); }
        Check(Ids(Load(root)) == original, "delete Travail, delete Famille, undo Travail, undo Famille: Personnel, Travail, Famille, Sport");
        // With the default profile among them (its entries live at the root).
        Tombstone t0, td; { auto r = Load(root); Check(Delete(root, r, DefaultId, t0, error), "delete Personnel while Sport is pending too"); } { auto r = Load(root); Delete(root, r, d.id, td, error); }
        { auto r = Load(root); Undo(root, r, td); } { auto r = Load(root); Undo(root, r, t0); }
        Check(Ids(Load(root)) == original && ReadFile(root + L"\\history") == "h default", "Personnel and Sport undone out of order: original order, root data back");
        // A journal written before `order` existed still uses its index.
        Registry r; r.items = Load(root).items; Profile ghost = Seed(L"Ancien"); ghost.id = L"pold00000001";
        Detail::Reinsert(r, ghost, 2); Check(r.items.size() == 5 && r.items[2].id == ghost.id, "older journal (no order): index fallback");
    }
    { // The undo window lasts as long as the owner's hold, whatever the deadline (the notice may pause).
        const auto root = Case(L"hold-outlives-deadline");
        auto reg = Load(root); const auto work = Create(root, reg, Seed(L"Travail"));
        WriteFile(ResolvePaths(root, work.id).dataDir + L"\\history", "keep");
        Tombstone t; std::wstring error;
        Check(Delete(root, reg, work.id, t, error) && t.hold, "delete with its hold");
        Detail::Journal j; Check(Detail::ReadJournal(root, t.trashDir, j), "journal readable");
        j.deadlineWall = 1; Check(Detail::WriteJournal(root, t.trashDir, j), "simulated: deadline long expired (more than 60 s ago)");
        PurgeTrash(root); auto other = Spawn(L"purge \"" + root + L"\""); const DWORD code = Wait(other); Load(root);
        Check(code == 0 && fs::exists(t.trashDir) && ReadFile(t.trashDir + L"\\data\\" + work.id + L"\\history") == "keep", "hold alive past the deadline: never purged (this process, another process, Load)");
        auto r = Load(root);
        Check(Undo(root, r, t) && r.Find(work.id) && ReadFile(ResolvePaths(root, work.id).dataDir + L"\\history") == "keep", "undo still works after the deadline while the hold lives");
        Tombstone t2; r = Load(root);
        Check(Delete(root, r, work.id, t2, error), "deleted again");
        Detail::Journal j2; Detail::ReadJournal(root, t2.trashDir, j2); j2.deadlineWall = 1; Detail::WriteJournal(root, t2.trashDir, j2);
        CloseHandle(t2.hold); t2.hold = nullptr;   // owner gone (closed or crashed)
        PurgeTrash(root);
        Check(!fs::exists(t2.trashDir) && !Load(root).Find(work.id) && !fs::exists(ResolvePaths(root, work.id).dataDir), "after the hold is released: purged at the next pass, profile gone for good");
        r = Load(root); const auto fam = Create(root, r, Seed(L"Famille", L"users-three"));
        auto child = Spawn(L"delete-exit \"" + root + L"\" " + fam.id); Check(Wait(child) == 0, "a process deletes, then exits inside its undo window");
        Load(root);
        Check(!Load(root).Find(fam.id) && !fs::exists(ResolvePaths(root, fam.id).dataDir) && [&] { std::error_code ec; for (auto& e : fs::directory_iterator(root + L"\\profiles\\.trash", ec)) { (void)e; return false; } return true; }(), "owner exited (hold gone): its tomb is purged even before the deadline");
    }
    { // Private windows: counted by class + BrowserPrivateWindow = 1 + live owner, across processes.
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = WindowClass; RegisterClassW(&wc);
        const int base = PrivateWindowCount();   // a real private browser window may be open on this machine
        HWND mine = CreateWindowW(WindowClass, L"p", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        HWND normal = CreateWindowW(WindowClass, L"n", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        WNDCLASSW other{}; other.lpfnWndProc = DefWindowProcW; other.hInstance = wc.hInstance; other.lpszClassName = L"NotTheBrowser"; RegisterClassW(&other);
        HWND foreign = CreateWindowW(L"NotTheBrowser", L"f", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        SetPropW(foreign, PrivateWindowProperty, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
        SetPropW(normal, PrivateWindowProperty, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(2)));   // not exactly 1
        Check(std::wstring(PrivateWindowProperty) == L"BrowserPrivateWindow" && PrivateWindowCount() == base, "unmarked, wrong-value and foreign-class windows are not private windows");
        MarkPrivateWindow(mine);
        Check(PrivateWindowCount() == base + 1 && PrivateWindowCount(mine) == base, "a marked window of our class counts (and can be excluded)");
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, L"Local\\profiles-test-private-1");
        auto child = Spawn(L"private Local\\profiles-test-private-1");
        int seen = 0; for (int i = 0; i < 200 && (seen = PrivateWindowCount()) < base + 2; ++i) Sleep(20);
        Check(seen == base + 2, "a private window of another process counts");
        SetEvent(go); Wait(child); CloseHandle(go);
        Check(PrivateWindowCount() == base + 1, "closed in the other process: no longer counted");
        UnmarkPrivateWindow(mine); DestroyWindow(mine); DestroyWindow(normal); DestroyWindow(foreign);
        Check(PrivateWindowCount() == base, "unmarked / destroyed: back to the count before");
    }
    { // Same-window workspaces: one HWND hosting several profiles, each with its own claim and sidecar.
        const auto root = Case(L"workspaces");
        auto reg = Load(root); const auto b = Create(root, reg, Seed(L"Travail"));
        const auto pa = ResolvePaths(root, DefaultId), pb = ResolvePaths(root, b.id);
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = WindowClass; RegisterClassW(&wc);
        HWND win = CreateWindowW(WindowClass, L"w", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        HANDLE la = WindowSessions::Claim(pa.dataDir, L"wa1"), lb = WindowSessions::Claim(pb.dataDir, L"wb1");
        const ULONG_PTR token = WindowToken(win);
        Check(token != 0 && WindowToken(win) == token && reinterpret_cast<ULONG_PTR>(GetPropW(win, L"BrowserWindowToken")) == token, "the window token is minted once and kept (property BrowserWindowToken)");
        Check(PublishWorkspace(pa, DefaultId, L"wa1", win, {L"Accueil"}, {L"https://a.example/"}, 0, WorkspaceState::Active) == PublishResult::Published
              && PublishWorkspace(pb, b.id, L"wb1", win, {L"Intranet"}, {L"https://b.example/"}, 0, WorkspaceState::Cached) == PublishResult::Published, "one HWND publishes a workspace in each profile");
        auto wa = LiveWindows(root, DefaultId), wb = LiveWindows(root, b.id);
        Check(wa.size() == 1 && wa[0].hwnd == win && !wa[0].cached && wa[0].token == token && wb.size() == 1 && wb[0].hwnd == win && wb[0].cached && wb[0].titles == std::vector<std::wstring>{L"Intranet"},
              "each profile validates the same HWND with its own state (active / cached)");
        Check(ActiveIn(root, DefaultId).size() == 1 && CachedIn(root, DefaultId).empty() && ActiveIn(root, b.id).empty() && CachedIn(root, b.id).size() == 1, "ActiveIn / CachedIn");
        WriteFile(WindowSessions::File(pb.dataDir, L"wb1"), "browser-session-2\n0\n1\n\"https://b.example/\"\n0\n");   // a cached workspace has saved its session
        UnpublishWindow(pb, L"wb1", win);
        wa = LiveWindows(root, DefaultId); wb = LiveWindows(root, b.id);
        Check(wa.size() == 1 && wa[0].hwnd == win && wb.size() == 1 && !wb[0].hwnd && GetPropW(win, L"BrowserWindowToken"), "unpublishing one workspace leaves the other valid and the token in place (its claim still counts)");
        PublishWindow(pb, b.id, L"wb1", win, {}, {}, 0, WorkspaceState::Cached);
        for (int i = 0; i < 50; ++i) PublishWindow(pa, DefaultId, L"wa1", win, {L"t" + std::to_wstring(i)}, {L"https://a.example/"}, 0);
        Check(LiveWindows(root, b.id).size() == 1 && LiveWindows(root, b.id)[0].hwnd == win && LiveWindows(root, DefaultId)[0].titles == std::vector<std::wstring>{L"t49"}, "re-publishing one workspace 50 times never disturbs the other");
        Tombstone t; std::wstring error; reg = Load(root);
        Check(OpenWindowCount(root, b.id) == 1 && !Delete(root, reg, b.id, t, error) && error == L"open-windows", "a cached workspace keeps Delete refusing (its claim is live)");
        // Stale or forged sidecars: counted (the claim is live) but never trusted.
        const std::wstring side = WindowSessions::Dir(pb.dataDir) + L"\\wb1.hwnd";
        auto forge = [&](unsigned long long tok, DWORD pid, const char* head) {
            std::string s = std::string(head) + "\npid=" + std::to_string(pid) + "\nhwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(win)) + "\ntoken=" + std::to_string(tok) + "\nprofile=" + Detail::Utf8(b.id) + "\nstate=cached\nactive-time=1\nactive=0\n";
            WriteFile(side, s); const auto w = LiveWindows(root, b.id); return w.size() == 1 && !w[0].hwnd; };
        Check(forge(token ^ 2, GetCurrentProcessId(), "browser-window 2"), "wrong token: not trusted, still counted");
        Check(forge(token, GetCurrentProcessId() + 4, "browser-window 2"), "wrong pid (reused handle): not trusted");
        Check(forge(token, GetCurrentProcessId(), "browser-window 9"), "unknown sidecar format: not trusted");
        { std::string s = "browser-window 2\npid=" + std::to_string(GetCurrentProcessId()) + "\nhwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(win)) + "\ntoken=" + std::to_string(token) + "\nprofile=" + Detail::Utf8(b.id) + "\nstate=weird\nactive-time=1\nactive=0\n";
          WriteFile(side, s); Check(!LiveWindows(root, b.id)[0].hwnd, "unknown state: not trusted"); }
        // An older build's « browser-window 1 » sidecar (profile tag property) is still read.
        SetPropW(win, L"BrowserProfileTag", reinterpret_cast<HANDLE>(Detail::ProfileTag(b.id)));
        WriteFile(side, "browser-window 1\npid=" + std::to_string(GetCurrentProcessId()) + "\nhwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(win)) + "\nprofile=" + Detail::Utf8(b.id) + "\nactive-time=1\nactive=0\n");
        Check(LiveWindows(root, b.id)[0].hwnd == win && !LiveWindows(root, b.id)[0].cached, "v1 sidecar from an older build: still validated (as active)");
        RemovePropW(win, L"BrowserProfileTag");
        // Private windows never publish nor write a set.
        const std::vector<WindowSlot> privSlots = {{DefaultId, L"wp1", WorkspaceState::Active}};
        HWND priv = CreateWindowW(WindowClass, L"p", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr); MarkPrivateWindow(priv);
        Check(PublishWorkspace(pa, DefaultId, L"wp1", priv, {}, {}, 0, WorkspaceState::Active) == PublishResult::Private && !fs::exists(WindowSessions::Dir(pa.dataDir) + L"\\wp1.hwnd")
              && WriteWindowSet(root, priv, privSlots) == SetResult::Private && !fs::exists(root + L"\\windows"), "a private window never publishes a sidecar nor writes a window set");
        DestroyWindow(priv);
        // Window sets: cached slots come back in the SAME window, never alone nor as separate windows.
        WriteFile(WindowSessions::File(pa.dataDir, L"wa1"), "browser-session-2\n0\n1\n\"https://a.example/\"\n0\n");
        WriteFile(WindowSessions::File(pb.dataDir, L"wb1"), "browser-session-2\n0\n1\n\"https://b.example/\"\n0\n");
        const std::vector<WindowSlot> twoActive = {{DefaultId, L"wa1", WorkspaceState::Active}, {b.id, L"wb1", WorkspaceState::Active}}, noActive = {{DefaultId, L"wa1", WorkspaceState::Cached}},
                                      twice = {{DefaultId, L"wa1", WorkspaceState::Active}, {DefaultId, L"wa2", WorkspaceState::Cached}}, good = {{DefaultId, L"wa1", WorkspaceState::Active}, {b.id, L"wb1", WorkspaceState::Cached}};
        Check(WriteWindowSet(root, win, twoActive) == SetResult::Invalid && WriteWindowSet(root, win, noActive) == SetResult::Invalid && WriteWindowSet(root, win, twice) == SetResult::Invalid, "invalid sets refused (two actives, none, a profile twice)");
        Check(WriteWindowSet(root, win, good) == SetResult::Written && fs::exists(root + L"\\windows\\" + WindowSetName(win)), "set written atomically under <root>\\windows\\<token>");
        WindowSet found; Check(FindWindowSet(root, b.id, L"wb1", found) && found.token == WindowSetName(win) && found.slots.size() == 2 && found.slots[0].state == WorkspaceState::Active, "FindWindowSet by any of its slots");
        Check(CachedSlotSessions(root, b.id) == std::vector<std::wstring>{L"wb1"} && CachedSlotSessions(root, DefaultId).empty(), "the cached slot is held by its set; the active one is not");
        // The window closes (crash or normal close: claims gone, set and session files stay).
        WindowSessions::Release(pb.dataDir, L"wb1", true, lb); WindowSessions::Release(pa.dataDir, L"wa1", true, la);
        UnpublishWindow(pa, L"wa1"); UnpublishWindow(pb, L"wb1");
        reg = Load(root);
        Check(ProfilesWithOrphans(root, reg, L"pnobody00001", L"w9") == std::vector<std::wstring>{DefaultId}, "cold start: only the set's active profile is relaunched; the cached slot is not a separate window");
        auto alone = AdoptWorkspace(root, b.id, L"", L"", true);
        Check(alone.live && alone.id != L"wb1" && alone.relaunch.empty() && fs::exists(WindowSessions::File(pb.dataDir, L"wb1")), "a Travail window started alone neither adopts nor relaunches the cached slot");
        WindowSessions::Release(pb.dataDir, alone.id, false, alone.live);
        auto off = AdoptWorkspace(root, b.id, L"", L"", false);
        Check(fs::exists(WindowSessions::File(pb.dataDir, L"wb1")), "restoration off: the cached slot is not dropped either");
        WindowSessions::Release(pb.dataDir, off.id, false, off.live);
        auto owner = AdoptWorkspace(root, DefaultId, L"", L"", true);
        WindowSet set; const bool hasSet = owner.id == L"wa1" && FindWindowSet(root, DefaultId, owner.id, set);
        HANDLE slot = hasSet ? ClaimSlot(root, set.slots[1]) : nullptr;
        Check(hasSet && slot && !ClaimSlot(root, set.slots[1]) && WindowSessions::Live(pb.dataDir, L"wb1"), "the adopter of the active session finds the set and claims the cached slot (once)");
        Check(ProfilesWithOrphans(root, reg, good).empty(), "its own cached slot is not « another window open »");
        HWND win2 = CreateWindowW(WindowClass, L"w2", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        Check(WriteWindowSet(root, win2, set.slots) == SetResult::Written && RetireWindowSet(root, set.token) == SetResult::Removed && ReadWindowSets(root).size() == 1 && ReadWindowSets(root)[0].token == WindowSetName(win2), "rewritten under the new window's token, the old one retired");
        CloseHandle(slot); WindowSessions::Release(pa.dataDir, owner.id, true, owner.live);
        // A set whose active session is gone holds nothing: its cached sessions are ordinary orphans again.
        DeleteFileW(WindowSessions::File(pa.dataDir, L"wa1").c_str());
        Check(CachedSlotSessions(root, b.id).empty() && ProfilesWithOrphans(root, reg, L"pnobody00001", L"w9") == std::vector<std::wstring>{b.id}, "active session gone: the cached session is restored as an orphan, never lost");
        // Malformed / unknown set files are ignored and never deleted.
        WriteFile(root + L"\\windows\\00000000000000aa", "browser-window-set 1\nslot=default|wa1|active\nslot=" + Detail::Utf8(b.id) + "|wb1|active\n");
        WriteFile(root + L"\\windows\\00000000000000bb", "browser-window-set 7\nslot=default|wa1|active\n");
        WriteFile(root + L"\\windows\\00000000000000cc", "browser-window-set 1\nslot=default|wa1|active\nextra=1\n");
        Check(ReadWindowSets(root).size() == 1 && fs::exists(root + L"\\windows\\00000000000000aa") && fs::exists(root + L"\\windows\\00000000000000bb") && fs::exists(root + L"\\windows\\00000000000000cc"), "malformed or unknown sets: ignored, kept as they are");
        Check(RetireWindowSet(root, L"..\\x") == SetResult::Invalid && RetireWindowSet(root, L"0000000000000000") == SetResult::Invalid, "a set name is a token, never a path");
        DestroyWindow(win2); DestroyWindow(win);
    }
    { // Membership cannot be spoofed: the token alone proves nothing without a live claim, the right profile and a normal window.
        const auto root = Case(L"workspace-spoof");
        auto reg = Load(root); const auto b = Create(root, reg, Seed(L"Travail")), c = Create(root, reg, Seed(L"Famille", L"users-three"));
        const auto pb = ResolvePaths(root, b.id), pc = ResolvePaths(root, c.id);
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = WindowClass; RegisterClassW(&wc);
        HWND win = CreateWindowW(WindowClass, L"w", WS_OVERLAPPED, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance, nullptr);
        HANDLE lb = WindowSessions::Claim(pb.dataDir, L"ws1");
        Check(PublishWindow(pb, b.id, L"ws1", win, {}, {}, 0, WorkspaceState::Cached), "this window caches Travail");
        // A sidecar for Famille naming this window and its real token, but with no live claim: not a workspace at all.
        Check(PublishWindow(pc, c.id, L"wfake", win, {}, {}, 0, WorkspaceState::Cached) && LiveWindows(root, c.id).empty() && OpenWindowCount(root, c.id) == 0, "a sidecar without a live session claim is not membership (the token is not enough)");
        // The Travail sidecar copied into Famille's folder (profile line says Travail): not trusted for Famille.
        HANDLE lc = WindowSessions::Claim(pc.dataDir, L"ws1");
        CopyFileW((WindowSessions::Dir(pb.dataDir) + L"\\ws1.hwnd").c_str(), (WindowSessions::Dir(pc.dataDir) + L"\\ws1.hwnd").c_str(), FALSE);
        const auto famille = LiveWindows(root, c.id);
        Check(famille.size() == 1 && !famille[0].hwnd && CachedIn(root, c.id).empty(), "a sidecar bound to another profile is never trusted for this one");
        // A window that turns private: its sidecars stop validating; it can no longer publish nor write a set.
        MarkPrivateWindow(win); const std::vector<WindowSlot> mineOnly = {{b.id, L"ws1", WorkspaceState::Active}};
        Check(!LiveWindows(root, b.id)[0].hwnd && !PublishWindow(pb, b.id, L"ws1", win, {}, {}, 0, WorkspaceState::Cached) && WriteWindowSet(root, win, mineOnly) == SetResult::Private,"stale private window: not trusted, publish and set writes refused");
        UnmarkPrivateWindow(win);
        CloseHandle(lb); CloseHandle(lc); DestroyWindow(win);
        Check(LiveWindows(root, b.id).empty(), "claim released: the sidecar alone no longer counts");
    }
    { // Window sets written by several processes at once; retire only what is not another live window's.
        const auto root = Case(L"window-sets-concurrent");
        auto reg = Load(root); const auto b = Create(root, reg, Seed(L"Travail"));
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, L"Local\\profiles-test-sets-1");
        std::vector<PROCESS_INFORMATION> kids;
        for (int i = 0; i < 4; ++i) kids.push_back(Spawn(L"setwriter \"" + root + L"\" w" + std::to_wstring(i) + L"x " + b.id + L" 40 Local\\profiles-test-sets-1"));
        bool written = false; for (int i = 0; i < 400 && !written; ++i) { written = true; for (int k = 0; k < 4; ++k) written = written && fs::exists(root + L"\\token-w" + std::to_wstring(k) + L"x"); if (!written) Sleep(25); }
        const auto sets = ReadWindowSets(root);
        bool exact = sets.size() == 4; for (const auto& s : sets) exact = exact && s.slots.size() == 2 && s.slots[0].state == WorkspaceState::Active;
        Check(written && exact, "4 processes x 40 rewrites at once: 4 valid sets, none torn or mixed");
        const std::wstring t0 = Detail::Wide(ReadFile(root + L"\\token-w0x"));
        Check(RetireWindowSet(root, t0) == SetResult::InUse && fs::exists(root + L"\\windows\\" + t0), "another process's live window keeps its set (retire refused: InUse)");
        WindowSet found; Check(FindWindowSet(root, b.id, L"w2xb", found) && found.slots[0].session == L"w2xa", "each set restores its own slots");
        SetEvent(go); for (auto& k : kids) Wait(k); CloseHandle(go);
        Check(RetireWindowSet(root, t0) == SetResult::Removed && !fs::exists(root + L"\\windows\\" + t0) && ReadWindowSets(root).size() == 3, "its window gone: the set can be retired by its adopter");
        auto crash = Spawn(L"setwriter \"" + root + L"\" w9x " + b.id + L" 1 -"); Check(Wait(crash) == 0, "a window writes its set and dies");
        const std::wstring t9 = Detail::Wide(ReadFile(root + L"\\token-w9x"));
        Check(FindWindowSet(root, DefaultId, L"w9xa", found) && found.token == t9, "a crashed window's set is still found by whoever adopts its active session");
    }
    { // Engine-args lease: idempotent per process; changes apply only once every holder released.
        const auto root = Case(L"engine-args-lease");
        auto reg = Load(root); const auto b = Create(root, reg, Seed(L"Travail"));
        const auto one = AcquireEngineArgs(root, b.id, L"--a"), again = AcquireEngineArgs(root, b.id, L"--b");
        Check(one.arguments == L"--a" && again.arguments == L"--b" && !again.shared, "same process re-acquiring (environment recreated after cold): its own new arguments, no self-sharing");
        const std::string record = ReadFile(ResolvePaths(root, b.id).dataDir + L"\\engine-args");
        Check(record.find("holder=") != std::string::npos && std::count(record.begin(), record.end(), '\n') == 3,"one holder line per process, never duplicated");
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, L"Local\\profiles-test-engine-2");
        auto other = Spawn(L"engine \"" + root + L"\" " + b.id + L" --c --b Local\\profiles-test-engine-2");
        const auto holders = [&] { const auto t = ReadFile(ResolvePaths(root, b.id).dataDir + L"\\engine-args"); size_t n = 0, at = 0; while ((at = t.find("holder=", at)) != std::string::npos) { ++n; ++at; } return n; };
        for (int i = 0; i < 400 && holders() < 2; ++i) Sleep(25);
        Check(ReleaseEngineArgs(root, b.id) && AcquireEngineArgs(root, b.id, L"--d").arguments == L"--b", "this process released and re-acquired while the other still holds: the arguments in force stay");
        ReleaseEngineArgs(root, b.id);
        SetEvent(go); Check(Wait(other) == 0, "the other process got the arguments in force and released after its engine exit");
        CloseHandle(go);
        Check(AcquireEngineArgs(root, b.id, L"--e").arguments == L"--e" && ReleaseEngineArgs(root, b.id), "no holder left: the changed flags apply");
        Check(AcquireEngineArgs(root, L"..\\x", L"--z").error == L"invalid" && !ReleaseEngineArgs(root, L"..\\x"), "invalid profile id: an error, never a path");
    }
    { // Engine arguments: every environment on one user-data folder gets the same arguments.
        const auto root = Case(L"engine-args");
        auto reg = Load(root); const auto b = Create(root, reg, Seed(L"Travail"));
        auto first = AcquireEngineArgs(root, b.id, L"--autoplay-policy=user-gesture-required --lang=fr");
        Check(first.error.empty() && !first.shared && first.arguments == L"--autoplay-policy=user-gesture-required --lang=fr", "first environment: its own arguments recorded");
        HANDLE go = CreateEventW(nullptr, TRUE, FALSE, L"Local\\profiles-test-engine-1");
        auto other = Spawn(L"engine \"" + root + L"\" " + b.id + L" \"--autoplay-policy=no-user-gesture-required --lang=fr\" \"--autoplay-policy=user-gesture-required --lang=fr\" Local\\profiles-test-engine-1");
        Sleep(300); SetEvent(go); Check(Wait(other) == 0, "another process while this one holds it: gets the arguments in force, not its changed settings");
        CloseHandle(go);
        Check(AcquireEngineArgs(root, b.id, L"--changed").arguments == L"--changed", "only holder (this process): changed settings apply");
        Check(ReleaseEngineArgs(root, b.id), "released");
        auto crash = Spawn(L"engine \"" + root + L"\" " + b.id + L" \"--from-crash\" \"--from-crash\" -"); Check(Wait(crash) == 0, "a process takes it then dies without releasing");
        Check(AcquireEngineArgs(root, b.id, L"--fresh").arguments == L"--fresh", "dead holder pruned: the new arguments apply");
        ReleaseEngineArgs(root, b.id);
        WriteFile(ResolvePaths(root, b.id).dataDir + L"\\engine-args", "browser-engine-args 9\nargs=x\n");
        const auto unknown = AcquireEngineArgs(root, b.id, L"--w");
        Check(unknown.error == L"unknown-format" && unknown.arguments == L"--w" && ReadFile(ResolvePaths(root, b.id).dataDir + L"\\engine-args") == "browser-engine-args 9\nargs=x\n", "unknown engine-args format: reported, never overwritten");
        Check(std::find(DefaultEntries().begin(), DefaultEntries().end(), L"engine-args") != DefaultEntries().end(), "the default profile's engine-args moves with it on delete");
    }
    { // WorkspaceCache: the policy the integration calls (no separate model).
        using namespace WorkspaceCache;
        std::vector<Entry> e = {{L"a", 10, true, false}, {L"b", 20, true, false}, {L"c", 30, true, false}, {L"d", 40, true, false}};
        Check(ToCold(e, L"d") == std::vector<std::wstring>{L"a"} && DefaultLive == 3, "4 live, bound 3: the least recently used one goes cold");
        Check(ToCold(e, L"a") == std::vector<std::wstring>{L"b"}, "the shown workspace is never made cold, even if it is the oldest");
        e[0].pinned = true; e[1].pinned = true;
        Check(ToCold(e, L"d") == std::vector<std::wstring>{L"c"}, "pinned (downloads, pending dialog) skipped");
        e[2].pinned = true;
        Check(ToCold(e, L"d").empty(), "all others pinned: the cache exceeds its bound");
        std::vector<Entry> f = {{L"a", 10, true, false}, {L"b", 20, false, false}};
        const std::vector<Entry> three = {{L"a", 1, true, false}, {L"b", 2, true, false}, {L"c", 3, true, false}};
        Check(ToCold(f, L"c").empty() && ToCold(three, L"n") == std::vector<std::wstring>{L"a"}, "a workspace being created counts as live");
        Check(ToCold(e, L"d", 0).size() <= 1, "bound below 1 is treated as 1");
        const std::vector<Entry> five = {{L"a", 1, true, false}, {L"b", 2, true, true}, {L"c", 3, true, false}, {L"d", 4, false, false}, {L"e", 5, true, false}};
        Check(ToCold(five, L"e", 2) == std::vector<std::wstring>{L"a", L"c"}, "bound 2 with a pinned one: the oldest unpinned live ones go cold; cold ones are ignored");
        Check(ToCold(five, L"d") == std::vector<std::wstring>{L"a", L"c"}, "returning to a cold workspace counts it live: two others go cold, the pinned one stays");
        const std::vector<Entry> sameTime = {{L"b", 7, true, false}, {L"a", 7, true, false}, {L"c", 9, true, false}};
        Check(ToCold(sameTime, L"c", 2) == std::vector<std::wstring>{L"a"}, "ties broken by id: deterministic");
        const std::vector<Entry> tie = {{L"x", 5, false, false}, {L"y", 5, true, false}, {L"z", 1, true, false}}, alone1 = {{L"only", 1, true, false}};
        Check(Successor(e, L"d") == L"c" && Successor(tie, L"q") == L"y" && Successor(alone1, L"only").empty(), "successor: most recent other, live before cold on ties, none when alone");
    }
    std::printf(failures ? "\n%d check(s) FAILED\n" : "\nall profiles checks passed\n", failures);
    return failures ? 1 : 0;
}
