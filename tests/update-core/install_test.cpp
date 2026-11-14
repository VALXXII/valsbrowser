// End-to-end install/rollback/migration fixture with FAKE browsers in isolated temp roots.
// Real update-helper.exe + real Engine (inside fake browser.exe) + DiskTransport. No WebView2, no real profile.
// Usage: install_test.exe <fixtureDir>   (fixtureDir holds fake-0.3.1.exe, fake-0.3.2.exe, fake-0.3.2-crash.exe, update-helper.exe)
#include <windows.h>

#include <vector>

#include "../../src/update-core.h"
#include "test_util.h"

using namespace UpdateTest;
namespace UP = UpdatePackage;

namespace {

std::wstring g_fixtures;

std::wstring Fx(const std::wstring& name) { return g_fixtures + L"\\" + name; }
void Copy(const std::wstring& from, const std::wstring& to) { CHECK(CopyFileW(from.c_str(), to.c_str(), FALSE)); }
void MkDirs(const std::wstring& path) {
    std::wstring partial;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\') {
            partial = path.substr(0, i);
            if (partial.size() > 2) CreateDirectoryW(partial.c_str(), nullptr);
        }
    }
}

int Run(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs, DWORD* pidOut = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    if (pidOut) *pidOut = pi.dwProcessId;
    DWORD exitCode = 999;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return int(exitCode);
}

bool WaitFor(const std::wstring& file, const std::string& needle, DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        if (ReadText(file).find(needle) != std::string::npos) return true;
        Sleep(100);
    }
    return false;
}

void Feed(const std::wstring& feed, const std::wstring& browserExe, const std::string& version) {
    MkDirs(feed);
    std::vector<std::pair<std::string, std::string>> files = {
        {"browser.exe", ReadBinary(browserExe)},
        {"update-helper.exe", ReadBinary(Fx(L"update-helper.exe"))},
        {"WebView2Loader.dll", "fake loader"},
        {"ThorVG-LICENSE.txt", "license"},
        {"assets/fonts/x.ttf", "font"},
    };
    std::wstring pkg = feed + L"\\" + UP::Widen(UP::PackageFileName(version));
    CHECK(UP::WritePackage(pkg, version, "x64", files));
    UP::Manifest m;
    m.version = version;
    m.arch = "x64";
    m.package = UP::PackageFileName(version);
    UP::Sha256File(pkg, m.sha256, m.size);
    CHECK(WriteText(feed + L"\\" + UP::Widen(UP::ManifestAssetName), UP::FormatManifest(m)));
}

struct Canaries {
    std::vector<std::pair<std::wstring, std::string>> files;  // relative path, hash
    void Make(const std::wstring& base, const std::vector<std::wstring>& rel) {
        int i = 0;
        for (const auto& r : rel) {
            std::wstring p = base + L"\\" + r;
            MkDirs(p.substr(0, p.find_last_of(L'\\')));
            std::string content = "canary-" + std::to_string(GetTickCount64()) + "-" + std::to_string(i) + std::string(3000, char('a' + i % 26));
            ++i;
            WriteText(p, content);
            files.push_back({r, HashOf(p)});
        }
    }
    bool Same(const std::wstring& base) const {
        for (const auto& f : files)
            if (HashOf(base + L"\\" + f.first) != f.second) {
                std::printf("canary mismatch: %ls\n", f.first.c_str());
                return false;
            }
        return true;
    }
};
const std::vector<std::wstring> ProfileFiles = {L"profile\\EBWebView\\Default\\Network\\Cookies", L"profile\\EBWebView\\Default\\Login Data",
                                                L"profile\\EBWebView\\Local State", L"profile\\EBWebView\\lockfile",
                                                L"profile\\Unknown Future\\data.bin"};

void InstalledLayout(const std::wstring& root, Canaries& c) {
    MkDirs(root + L"\\versions\\0.3.1");
    Copy(Fx(L"fake-0.3.1.exe"), root + L"\\versions\\0.3.1\\browser.exe");
    Copy(Fx(L"update-helper.exe"), root + L"\\versions\\0.3.1\\update-helper.exe");
    WriteText(root + L"\\current", "0.3.1\n");
    MkDirs(root + L"\\data");
    c.Make(root + L"\\data", ProfileFiles);
    c.Make(root + L"\\data", {L"unknown-root-file.dat"});
    WriteText(root + L"\\data\\session", "browser-session-1\n0\n1\n\"https://example.org\"\n");
}

void TestInstalledUpdate() {
    std::wstring s = TempDir(L"e2e-installed"), root = s + L"\\root", feed = s + L"\\feed";
    Canaries c;
    InstalledLayout(root, c);
    Feed(feed, Fx(L"fake-0.3.2.exe"), "0.3.2");
    int code = Run(root + L"\\versions\\0.3.1\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000);
    CHECK(code == 0);
    std::wstring log = root + L"\\fake-launches.txt";
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(ReadText(root + L"\\current") == "0.3.2\n");
    CHECK(ReadText(root + L"\\previous") == "installed 0.3.1\n");
    CHECK(HashOf(root + L"\\versions\\0.3.2\\browser.exe") == HashOf(Fx(L"fake-0.3.2.exe")));
    CHECK(UP::PathExists(root + L"\\versions\\0.3.1\\browser.exe"));  // previous retained
    CHECK(HashOf(root + L"\\browser.exe") == HashOf(Fx(L"update-helper.exe")));  // stable launcher
    CHECK(!UP::PathExists(root + L"\\updates\\staging\\0.3.2"));
    CHECK(!UP::PathExists(root + L"\\updates\\pending"));
    CHECK(c.Same(root + L"\\data"));
    CHECK(ReadText(root + L"\\data\\session") == "session-from-0.3.1\n");  // host save before exit is what the new version gets
    CHECK(!UP::PathExists(root + L"\\versions\\0.3.2\\profile") && !UP::PathExists(root + L"\\versions\\0.3.2\\session"));
    std::string text = ReadText(log);
    auto oldExit = text.find("old-exit 0.3.1"), started = text.find("start 0.3.2 phase=6");
    CHECK(oldExit != std::string::npos && started != std::string::npos && oldExit < started);  // waited for exact PID
    std::string error;
    CHECK(UP::VerifyStagedTree(root + L"\\versions\\0.3.2", "0.3.2", error));
    // Stable launcher passes structured arguments to the current version.
    CHECK(Run(root + L"\\browser.exe", L"--echo \"a b\" \"c\\\"d\" \"\" plain", 10000) == 0);
    CHECK(WaitFor(log, "echo 0.3.2 [a b] [c\"d] [] [plain]", 10000));
}

void TestPortableMigration(const std::wstring& exeName = L"browser.exe", const std::wstring& tag = L"") {
    std::wstring s = TempDir(L"e2e-portable" + tag), root = s + L"\\root", feed = s + L"\\feed", portable = s + L"\\portable";
    MkDirs(portable);
    Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\" + exeName);
    Canaries c;
    c.Make(portable, ProfileFiles);
    c.Make(portable, {L"downloads"});
    WriteText(portable + L"\\downloads.tmp", "transient");
    Feed(feed, Fx(L"fake-0.3.2.exe"), "0.3.2");
    // Simulate a WebView2 process still holding the profile lock for a while after the browser exits.
    HANDLE lock = CreateFileW((portable + L"\\profile\\EBWebView\\lockfile").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    int code = Run(portable + L"\\" + exeName, L"--update \"" + root + L"\" \"" + feed + L"\"", 60000);
    CHECK(code == 0);
    Sleep(2500);
    CHECK(!UP::PathExists(root + L"\\data"));  // nothing copied while locked
    CHECK(!UP::PathExists(root + L"\\current"));
    CloseHandle(lock);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(ReadText(root + L"\\current") == "0.3.2\n");
    CHECK(c.Same(root + L"\\data"));  // copied
    CHECK(c.Same(portable));          // source untouched
    CHECK(ReadText(portable + L"\\session") == "session-from-0.3.1\n");
    CHECK(ReadText(root + L"\\data\\session") == "session-from-0.3.1\n");
    CHECK(!UP::PathExists(root + L"\\data\\downloads.tmp"));
    CHECK(ReadText(root + L"\\data\\migrated-from.txt").find("portable") != std::string::npos);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.2 phase=6", 10000));
    CHECK(ReadText(root + L"\\previous") == "portable " + UP::Narrow(portable + L"\\" + exeName) + "\n");
    auto paths = UpdateCore::ResolveDataPaths(root + L"\\versions\\0.3.2\\browser.exe", root);
    CHECK(paths.installed && paths.profileDir == root + L"\\data\\profile");
}

// Profiles (src/profiles.h): the registry, its recovery copies and every other profile's folder (own
// WebView2 data, history, sessions, and a pending deletion in profiles\.trash) migrate with the
// default profile. An open WebView2 lock of ANOTHER profile delays the copy like the default's does.
void TestPortableProfilesMigration() {
    std::wstring s = TempDir(L"e2e-portable-profiles"), root = s + L"\\root", feed = s + L"\\feed", portable = s + L"\\portable";
    MkDirs(portable);
    Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\browser.exe");
    Canaries c;
    c.Make(portable, ProfileFiles);
    c.Make(portable, {L"profiles.txt", L"profiles.txt.damaged-20261114-091500", L"profiles.txt.damaged-last",
                      L"profiles\\pa1b2c3d4e5f6\\profile\\EBWebView\\Local State", L"profiles\\pa1b2c3d4e5f6\\profile\\EBWebView\\lockfile",
                      L"profiles\\pa1b2c3d4e5f6\\history", L"profiles\\pa1b2c3d4e5f6\\settings", L"profiles\\pa1b2c3d4e5f6\\sessions\\w0001",
                      L"profiles\\.trash\\p0f0e0d0c0b0a-123\\journal.txt", L"profiles\\.trash\\p0f0e0d0c0b0a-123\\data\\p0f0e0d0c0b0a\\history"});
    WriteText(portable + L"\\profiles.txt.tmp4242", "transient");
    Feed(feed, Fx(L"fake-0.3.2.exe"), "0.3.2");
    HANDLE lock = CreateFileW((portable + L"\\profiles\\pa1b2c3d4e5f6\\profile\\EBWebView\\lockfile").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    int code = Run(portable + L"\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000);
    CHECK(code == 0);
    Sleep(2500);
    CHECK(!UP::PathExists(root + L"\\data"));  // nothing copied while another profile's WebView2 still holds its lock
    CloseHandle(lock);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(c.Same(root + L"\\data"));  // registry, recovery copies, profile folders and the pending deletion all copied
    CHECK(c.Same(portable));          // source untouched
    CHECK(!UP::PathExists(root + L"\\data\\profiles.txt.tmp4242"));  // transient write temp not copied
    auto paths = UpdateCore::ResolveDataPaths(root + L"\\versions\\0.3.2\\browser.exe", root);
    CHECK(paths.installed && UP::PathExists(paths.dataDir + L"\\profiles\\pa1b2c3d4e5f6\\history"));
}

// Real-world failure (Defender/Search indexer on the dev machine): a scanner opens a file the helper has just copied
// into <root>\data.migrating-<nonce>, and NTFS refuses to rename that directory to <root>\data (ERROR_ACCESS_DENIED),
// which used to abort the update with "migration-failed". Reproduced deterministically: the test keeps the portable
// session busy so the helper is parked between the profile copy and the publish, opens a copied profile file like a
// scanner (no FILE_SHARE_DELETE), lets the helper reach the rename, then releases the scanner handle.
void TestPortableMigrationScannerHold() {
    std::wstring s = TempDir(L"e2e-portable-scanned"), root = s + L"\\root", feed = s + L"\\feed", portable = s + L"\\portable";
    const std::wstring exeName = L"browser-main-78f5340.exe";
    MkDirs(portable);
    Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\" + exeName);
    Canaries c;
    c.Make(portable, ProfileFiles);
    c.Make(portable, {L"downloads"});
    Feed(feed, Fx(L"fake-0.3.2.exe"), "0.3.2");
    HANDLE lock = CreateFileW((portable + L"\\profile\\EBWebView\\lockfile").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    CHECK(Run(portable + L"\\" + exeName, L"--update \"" + root + L"\" \"" + feed + L"\"", 60000) == 0);
    // The browser saved its session on exit; keep it busy (sharing violation, retried by the helper's copy).
    HANDLE session = CreateFileW((portable + L"\\session").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(session != INVALID_HANDLE_VALUE);
    CloseHandle(lock);
    // Wait for the helper's work copy of the profile, then hold one copied file like an on-access scanner.
    HANDLE scanner = INVALID_HANDLE_VALUE;
    for (ULONGLONG end = GetTickCount64() + 30000; scanner == INVALID_HANDLE_VALUE && GetTickCount64() < end; Sleep(20)) {
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((root + L"\\data.migrating-*").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE) continue;
        FindClose(find);
        scanner = CreateFileW((root + L"\\" + fd.cFileName + L"\\profile\\EBWebView\\Local State").c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    CHECK(scanner != INVALID_HANDLE_VALUE);
    CloseHandle(session);  // the helper copies the session and reaches the rename while the scanner still holds its file
    Sleep(1500);
    CHECK(!UP::PathExists(root + L"\\data"));  // never published partially
    CHECK(!UP::PathExists(root + L"\\current"));
    CloseHandle(scanner);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(ReadText(root + L"\\updates\\helper.log").find("portable data published after") != std::string::npos);  // it did wait
    CHECK(ReadText(root + L"\\current") == "0.3.2\n");
    CHECK(c.Same(root + L"\\data"));
    CHECK(c.Same(portable));  // source untouched
    CHECK(ReadText(root + L"\\data\\session") == "session-from-0.3.1\n");
    CHECK(ReadText(root + L"\\data\\migrated-from.txt").find("portable") != std::string::npos);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.2 phase=6", 10000));
    WIN32_FIND_DATAW leftover;
    HANDLE find = FindFirstFileW((root + L"\\data.migrating-*").c_str(), &leftover);
    CHECK(find == INVALID_HANDLE_VALUE);  // work copy consumed by the rename, nothing left behind
    if (find != INVALID_HANDLE_VALUE) FindClose(find);
}

void TestInstalledRollback() {
    std::wstring s = TempDir(L"e2e-rollback"), root = s + L"\\root", feed = s + L"\\feed";
    Canaries c;
    InstalledLayout(root, c);
    Feed(feed, Fx(L"fake-0.3.2-crash.exe"), "0.3.2");
    CHECK(Run(root + L"\\versions\\0.3.1\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000) == 0);
    std::wstring log = root + L"\\fake-launches.txt";
    CHECK(WaitFor(log, "crash 0.3.2", 60000));
    CHECK(WaitFor(log, "start 0.3.1 phase=7", 60000));  // previous relaunched and shows the failure once
    CHECK(ReadText(root + L"\\current") == "0.3.1\n");
    CHECK(!UP::PathExists(root + L"\\versions\\0.3.2"));
    CHECK(UP::PathExists(root + L"\\versions\\0.3.1\\browser.exe"));
    CHECK(c.Same(root + L"\\data"));
    CHECK(ReadText(root + L"\\updates\\last-result").find("reported 1") != std::string::npos);
}

void TestPortableRollback(const std::wstring& exeName = L"browser.exe", const std::wstring& tag = L"") {
    std::wstring s = TempDir(L"e2e-portable-rollback" + tag), root = s + L"\\root", feed = s + L"\\feed", portable = s + L"\\portable";
    MkDirs(portable);
    Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\" + exeName);
    Canaries c;
    c.Make(portable, ProfileFiles);
    Feed(feed, Fx(L"fake-0.3.2-crash.exe"), "0.3.2");
    CHECK(Run(portable + L"\\" + exeName, L"--update \"" + root + L"\" \"" + feed + L"\"", 60000) == 0);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status failed", 60000));
    CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.1 phase=0", 20000));  // portable relaunched
    CHECK(!UP::PathExists(root + L"\\current"));
    CHECK(!UP::PathExists(root + L"\\data"));  // migrated copy discarded; portable stays authoritative
    CHECK(c.Same(portable));
}

std::uint64_t CreatedOf(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{};
    GetProcessTimes(process, &c, &e, &k, &u);
    return (std::uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}

void TestHelperRejections() {
    std::wstring s = TempDir(L"e2e-helper"), root = s + L"\\root";
    Canaries c;
    InstalledLayout(root, c);
    MkDirs(root + L"\\updates\\staging");
    MkDirs(root + L"\\updates\\helper");
    std::wstring pkg = s + L"\\p.lsrupd";
    UP::WritePackage(pkg, "0.3.2", "x64",
                     {{"browser.exe", ReadBinary(Fx(L"fake-0.3.2.exe"))}, {"update-helper.exe", ReadBinary(Fx(L"update-helper.exe"))}});
    std::string error;
    CHECK(UP::ExtractPackage(pkg, "0.3.2", root + L"\\updates\\staging\\0.3.2", nullptr, error));
    std::string nonce(32, 'a');
    std::wstring helper = root + L"\\updates\\helper\\update-helper-x.exe";
    Copy(Fx(L"update-helper.exe"), helper);
    auto plan = [&](const std::string& n, const std::wstring& previousExe, const std::string& kind) {
        return "browser-install 1\nnonce " + n + "\nversion 0.3.2\npid " + std::to_string(GetCurrentProcessId()) + "\ncreated " +
               std::to_string(CreatedOf(GetCurrentProcess())) + "\nprevious-kind " + kind + "\nprevious-version 0.3.1\nprevious-exe " +
               UP::Narrow(previousExe) + "\n";
    };
    std::wstring planPath = root + L"\\updates\\install-" + UP::Widen(nonce);
    std::wstring prevExe = root + L"\\versions\\0.3.1\\browser.exe";
    CHECK(Run(helper, L"--install BADNONCE", 10000) == 9);
    WriteText(planPath, plan(std::string(32, 'b'), prevExe, "installed"));
    CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);  // nonce mismatch inside plan
    WriteText(planPath, plan(nonce, L"C:\\Windows\\System32\\browser.exe", "installed"));
    CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);  // installed previous outside root
    WriteText(planPath, plan(nonce, root + L"\\versions\\0.3.1\\browser.exe", "portable"));
    CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);  // portable path inside root
    // Portable names are flexible (browser-main-<hash>.exe) but still constrained.
    MkDirs(s + L"\\portable");
    for (auto name : {L"script.cmd", L"evil name.exe", L"update-helper.exe", L"a\"b.exe", L".exe", L"x.exe.txt"})
        WriteText(s + L"\\portable\\" + name, "x");
    for (auto bad : {L"\\portable\\script.cmd", L"\\portable\\evil name.exe", L"\\portable\\update-helper.exe",
                     L"\\portable\\missing-78f5340.exe", L"\\portable\\x.exe.txt"}) {
        WriteText(planPath, plan(nonce, s + bad, "portable"));
        CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);
    }
    WriteText(planPath, plan(nonce, L"\\\\server\\share\\browser-main-78f5340.exe", "portable"));
    CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);  // UNC
    WriteText(planPath, plan(nonce, L"\\\\?\\" + s + L"\\portable\\browser-main.exe", "portable"));
    CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);  // device path
    std::wstring junction = s + L"\\portable-link";
    if (CreateJunction(junction, s + L"\\portable")) {
        WriteText(s + L"\\portable\\browser-main-78f5340.exe", "x");
        WriteText(planPath, plan(nonce, junction + L"\\browser-main-78f5340.exe", "portable"));
        CHECK(Run(helper, L"--install " + UP::Widen(nonce), 10000) == 12);  // parent is a reparse point
        RemoveDirectoryW(junction.c_str());
    }
    CHECK(UP::PathExists(root + L"\\updates\\staging\\0.3.2") && ReadText(root + L"\\current") == "0.3.1\n");  // nothing applied
    WriteText(planPath, plan(nonce, prevExe, "installed"));
    Copy(Fx(L"update-helper.exe"), s + L"\\elsewhere-helper.exe");
    CHECK(Run(s + L"\\elsewhere-helper.exe", L"--install " + UP::Widen(nonce), 10000) == 10);  // not under updates\helper
    // Tampered staged payload: aborted before any switch; previous relaunched (pid in plan is this test process,
    // whose image differs from previous-exe, so the helper treats the original browser as already gone).
    WriteText(root + L"\\updates\\staging\\0.3.2\\browser.exe", "tampered");
    CHECK(Run(helper, L"--install " + UP::Widen(nonce), 30000) == 21);
    CHECK(ReadText(root + L"\\current") == "0.3.1\n");
    CHECK(!UP::PathExists(root + L"\\versions\\0.3.2"));
    CHECK(!UP::PathExists(planPath));  // plan is single-use
    CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.1 phase=7", 10000));
    CHECK(c.Same(root + L"\\data"));
    CHECK(ReadText(root + L"\\updates\\helper.log").find("stage-invalid") != std::string::npos);
}

void TestLauncherBroken() {
    std::wstring s = TempDir(L"e2e-launcher"), root = s + L"\\root";
    MkDirs(root + L"\\versions\\0.3.1");
    Copy(Fx(L"update-helper.exe"), root + L"\\browser.exe");
    Copy(Fx(L"fake-0.3.1.exe"), root + L"\\versions\\0.3.1\\browser.exe");
    WriteText(root + L"\\current", "0.3.1\n");
    CHECK(Run(root + L"\\browser.exe", L"--echo x", 10000) == 0);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.1 [x]", 10000));
    // A corrupted pointer never resolves outside versions\ (the launcher would show an error box; not executed here
    // because a modal MessageBox would block the unattended run).
    for (auto bad : {"..\\..\\evil\n", "0.3.1\\..\\..\\x\n", "C:\\x\n"}) {
        UP::Version v;
        std::string t = bad;
        t.pop_back();
        CHECK(!UP::ParseVersion(t, v));
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    g_fixtures = FullPath(argv[1]);
    TestHelperRejections();
    TestLauncherBroken();
    TestInstalledUpdate();
    TestInstalledRollback();
    TestPortableMigration();
    TestPortableRollback();
    // Real portable naming used by current dev installs (build\v2\browser-main-78f5340.exe).
    TestPortableMigration(L"browser-main-78f5340.exe", L"-renamed");
    TestPortableRollback(L"browser-main-78f5340.exe", L"-renamed");
    TestPortableMigrationScannerHold();
    TestPortableProfilesMigration();
    TestPortableRollback(L"browser-url-ease.exe", L"-renamed2");
    return Report("install_test");
}
