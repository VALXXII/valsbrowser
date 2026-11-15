// End-to-end install/rollback/migration fixture with FAKE browsers in isolated temp roots.
// Real update-helper.exe + real Engine (inside fake browser.exe) + DiskTransport. No WebView2, no real profile.
// Usage: install_test.exe <fixtureDir>   (fixtureDir holds fake-0.3.1.exe, fake-0.3.2.exe, fake-0.3.2-crash.exe, update-helper.exe)
#include <windows.h>

#include <cstdio>
#include <memory>
#include <vector>

#include "../../src/annotations.h"
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

// ---------------------------------------------------------------- stable bridge (<root>\bin, plan B item 3)
// Real browser-annotations-mcp.exe fixtures (bridge-<v>.exe, distinct bytes per version). A LiveBridge is an agent's
// MCP server process started from <root>\bin with stdio pipes, exactly like Claude/Codex run it: it maps the bin
// file for its whole life. Nothing here ever kills it before the test is over.
const std::wstring BridgeRel = L"assets\\bin\\browser-annotations-mcp.exe";

struct LiveBridge {
    PROCESS_INFORMATION pi{};
    HANDLE in = nullptr, out = nullptr;
    bool started = false;
    LiveBridge(const std::wstring& exe, const std::wstring& store) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE childIn = nullptr, childOut = nullptr;
        if (!CreatePipe(&childIn, &in, &sa, 0) || !CreatePipe(&out, &childOut, &sa, 0)) return;
        SetHandleInformation(in, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = childIn;
        si.hStdOutput = childOut;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        std::wstring cmd = L"\"" + exe + L"\" --store \"" + store + L"\"";
        started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != 0;
        CloseHandle(childIn);
        CloseHandle(childOut);
    }
    bool Alive() const { return started && WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT; }
    // One real JSON-RPC round trip on the live process; returns the response line ("" when the process is gone).
    std::string Request(int id, const std::string& method, const std::string& extraParams) {
        if (!Alive()) return "";
        std::string req = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"" + method + "\",\"params\":{" +
                          extraParams + "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\","
                          "\"io.modelcontextprotocol/clientCapabilities\":{}}}}\n";
        DWORD put = 0;
        if (!WriteFile(in, req.data(), DWORD(req.size()), &put, nullptr)) return "";
        std::string line;
        char c;
        DWORD got = 0;
        while (line.size() < 1 << 20 && ReadFile(out, &c, 1, &got, nullptr) && got == 1 && c != '\n') line += c;
        return line.find("\"id\":" + std::to_string(id)) != std::string::npos ? line : "";
    }
    bool Answers(int id) { return Request(id, "tools/list", "").find("browser_annotations_list") != std::string::npos; }
    std::string Tool(int id, const std::string& name, const std::string& argsJson) {
        return Request(id, "tools/call", "\"name\":\"" + name + "\",\"arguments\":" + argsJson + ",");
    }
    void Close() {  // the agent closes stdin: the bridge exits by itself
        if (in) CloseHandle(in);
        if (started) {
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        if (out) CloseHandle(out);
        in = out = nullptr;
        started = false;
    }
    ~LiveBridge() { Close(); }
};

void FeedWithBridge(const std::wstring& feed, const std::wstring& browserExe, const std::string& version, const std::wstring& bridgeExe) {
    MkDirs(feed);
    std::vector<std::pair<std::string, std::string>> files = {
        {"browser.exe", ReadBinary(browserExe)},
        {"update-helper.exe", ReadBinary(Fx(L"update-helper.exe"))},
        {"WebView2Loader.dll", "fake loader"},
        {"ThorVG-LICENSE.txt", "license"},
        {"assets/fonts/x.ttf", "font"},
        {"assets/bin/browser-annotations-mcp.exe", ReadBinary(bridgeExe)},
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

// Installed 0.3.1 shipping bridge-0.3.1, already published to <root>\bin, with the stable launcher.
void InstalledWithBridge(const std::wstring& root, Canaries& c) {
    InstalledLayout(root, c);
    MkDirs(root + L"\\versions\\0.3.1\\assets\\bin");
    Copy(Fx(L"bridge-0.3.1.exe"), root + L"\\versions\\0.3.1\\" + BridgeRel);
    MkDirs(root + L"\\bin");
    Copy(Fx(L"bridge-0.3.1.exe"), root + L"\\bin\\browser-annotations-mcp.exe");
    Copy(Fx(L"update-helper.exe"), root + L"\\browser.exe");
}

std::wstring BinBridge(const std::wstring& root) { return root + L"\\bin\\browser-annotations-mcp.exe"; }
bool BinIs(const std::wstring& root, const wchar_t* fixture) {
    return UP::PathExists(BinBridge(root)) && HashOf(BinBridge(root)) == HashOf(Fx(fixture));
}
int CountAside(const std::wstring& root) {
    int n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((root + L"\\bin\\.*-*.exe").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do ++n; while (FindNextFileW(f, &fd));
    FindClose(f);
    return n;
}
// True once no update-helper-<nonce>.exe copy is running any more (a killed or finished helper).
bool HelperGone(const std::wstring& root, DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    while (GetTickCount64() < end) {
        bool running = false;
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((root + L"\\updates\\helper\\update-helper-*.exe").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                HANDLE h = CreateFileW((root + L"\\updates\\helper\\" + fd.cFileName).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h == INVALID_HANDLE_VALUE) running = true;
                else CloseHandle(h);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
        if (!running) return true;
        Sleep(200);
    }
    return false;
}
void SetFault(const wchar_t* point) { SetEnvironmentVariableW(L"BROWSER_HELPER_FAULT", point); }
int Update(const std::wstring& root, const std::wstring& feed) {
    return Run(root + L"\\versions\\0.3.1\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000);
}

void TestBridgeUpdateWithLiveAgent() {
    std::wstring s = TempDir(L"e2e-bridge-live"), root = s + L"\\root", feed = s + L"\\feed", feed3 = s + L"\\feed3";
    Canaries c;
    InstalledWithBridge(root, c);
    MkDirs(s + L"\\store");
    LiveBridge agent(BinBridge(root), s + L"\\store");
    CHECK(agent.Answers(1));
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    CHECK(Update(root, feed) == 0);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(ReadText(root + L"\\current") == "0.3.2\n");
    CHECK(BinIs(root, L"bridge-0.3.2.exe"));            // final path holds the new version's bridge
    CHECK(agent.Alive() && agent.Answers(2));          // the running agent was neither killed nor broken
    CHECK(CountAside(root) == 1);                      // its file was moved aside, still mapped
    CHECK(c.Same(root + L"\\data"));
    {
        LiveBridge fresh(BinBridge(root), s + L"\\store");  // a newly started agent gets the new bridge
        CHECK(fresh.Answers(3));
    }
    // Second update while the SAME agent (started at 0.3.1) still runs: 0.3.1 is pruned, bin stays valid.
    FeedWithBridge(feed3, Fx(L"fake-0.3.3.exe"), "0.3.3", Fx(L"bridge-0.3.3.exe"));
    CHECK(Run(root + L"\\versions\\0.3.2\\browser.exe", L"--update \"" + root + L"\" \"" + feed3 + L"\"", 60000) == 0);
    CHECK(WaitFor(root + L"\\updates\\last-result", "version 0.3.3", 60000));
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(BinIs(root, L"bridge-0.3.3.exe"));
    CHECK(!UP::PathExists(root + L"\\versions\\0.3.1"));  // pruned: the agent never depended on it
    CHECK(agent.Alive() && agent.Answers(4));
    CHECK(c.Same(root + L"\\data"));
    // Agent gone: the next launcher start removes the leftover aside copies.
    agent.Close();
    CHECK(Run(root + L"\\browser.exe", L"--echo leftovers", 10000) == 0);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.3 [leftovers]", 10000));
    CHECK(CountAside(root) == 0);
    CHECK(BinIs(root, L"bridge-0.3.3.exe"));
}

void TestBridgeRollbackWithLiveAgent() {
    std::wstring s = TempDir(L"e2e-bridge-rollback"), root = s + L"\\root", feed = s + L"\\feed";
    Canaries c;
    InstalledWithBridge(root, c);
    MkDirs(s + L"\\store");
    LiveBridge agent(BinBridge(root), s + L"\\store");
    CHECK(agent.Answers(1));
    FeedWithBridge(feed, Fx(L"fake-0.3.2-crash.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    CHECK(Update(root, feed) == 0);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.1 phase=7", 60000));
    CHECK(ReadText(root + L"\\current") == "0.3.1\n");
    CHECK(BinIs(root, L"bridge-0.3.1.exe"));  // the restored current never keeps the failed version's bridge
    CHECK(agent.Alive() && agent.Answers(2));
    CHECK(c.Same(root + L"\\data"));
}

// Failure at each publication step, then a clean retry: current, bin and the live agent stay consistent.
void TestBridgeFaults() {
    for (const wchar_t* fault : {L"bin-copy", L"bin-aside", L"bin-final", L"current-write"}) {
        std::wstring s = TempDir(std::wstring(L"e2e-bridge-fault-") + fault), root = s + L"\\root", feed = s + L"\\feed";
        Canaries c;
        InstalledWithBridge(root, c);
        MkDirs(s + L"\\store");
        LiveBridge agent(BinBridge(root), s + L"\\store");
        FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
        SetFault(fault);
        CHECK(Update(root, feed) == 0);
        SetFault(nullptr);
        CHECK(WaitFor(root + L"\\updates\\last-result", "status failed", 60000));
        CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.1", 30000));  // previous relaunched
        CHECK(HelperGone(root, 30000));
        std::printf("fault %ls: current=%s bin=0.3.1:%d alive=%d\n", fault, ReadText(root + L"\\current").c_str(),
                    BinIs(root, L"bridge-0.3.1.exe"), agent.Alive());
        CHECK(ReadText(root + L"\\current") == "0.3.1\n");
        CHECK(BinIs(root, L"bridge-0.3.1.exe"));  // final path present, previous bytes
        CHECK(agent.Alive() && agent.Answers(1));
        CHECK(c.Same(root + L"\\data"));
        CHECK(Update(root, feed) == 0);  // retry without fault
        CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
        CHECK(BinIs(root, L"bridge-0.3.2.exe") && ReadText(root + L"\\current") == "0.3.2\n");
        CHECK(agent.Alive() && agent.Answers(2));
    }
}

// Helper killed at three points (no result written, nothing relaunched): the next launcher start repairs bin to
// match `current`, and a new update succeeds.
void TestBridgeKills() {
    struct Case { const wchar_t* fault; const char* current; const wchar_t* binAfterRepair; };
    for (const Case& k : {Case{L"kill-after-aside", "0.3.1\n", L"bridge-0.3.1.exe"},
                          Case{L"kill-after-publish", "0.3.1\n", L"bridge-0.3.1.exe"},
                          Case{L"kill-after-switch", "0.3.2\n", L"bridge-0.3.2.exe"}}) {
        std::wstring s = TempDir(std::wstring(L"e2e-bridge-") + k.fault), root = s + L"\\root", feed = s + L"\\feed";
        Canaries c;
        InstalledWithBridge(root, c);
        MkDirs(s + L"\\store");
        LiveBridge agent(BinBridge(root), s + L"\\store");
        FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
        SetFault(k.fault);
        CHECK(Update(root, feed) == 0);
        SetFault(nullptr);
        CHECK(WaitFor(root + L"\\updates\\helper.log", "install ", 60000));
        Sleep(500);
        CHECK(HelperGone(root, 60000));
        CHECK(ReadText(root + L"\\current") == k.current);
        if (std::wstring(k.fault) == L"kill-after-aside") CHECK(!UP::PathExists(BinBridge(root)));  // the one gap state
        CHECK(agent.Alive() && agent.Answers(1));
        CHECK(Run(root + L"\\browser.exe", L"--echo repair", 10000) == 0);  // launcher start
        CHECK(BinIs(root, k.binAfterRepair));
        CHECK(agent.Alive() && agent.Answers(2));
        CHECK(c.Same(root + L"\\data"));
        if (std::string(k.current) == "0.3.1\n") {  // reinstall after the crash
            CHECK(Update(root, feed) == 0);
            CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
            CHECK(BinIs(root, L"bridge-0.3.2.exe") && ReadText(root + L"\\current") == "0.3.2\n");
        }
    }
}

void TestBridgePortableFirstInstallAndRollback() {
    {
        std::wstring s = TempDir(L"e2e-bridge-portable"), root = s + L"\\root", feed = s + L"\\feed", portable = s + L"\\portable";
        MkDirs(portable);
        Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\browser.exe");
        FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
        CHECK(Run(portable + L"\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000) == 0);
        CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
        CHECK(BinIs(root, L"bridge-0.3.2.exe"));
        CHECK(!UP::IsReparsePoint(root + L"\\bin"));
    }
    {
        std::wstring s = TempDir(L"e2e-bridge-portable-rollback"), root = s + L"\\root", feed = s + L"\\feed", portable = s + L"\\portable";
        MkDirs(portable);
        Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\browser.exe");
        FeedWithBridge(feed, Fx(L"fake-0.3.2-crash.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
        CHECK(Run(portable + L"\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000) == 0);
        CHECK(WaitFor(root + L"\\updates\\last-result", "status failed", 60000));
        CHECK(WaitFor(root + L"\\fake-launches.txt", "start 0.3.1 phase=0", 20000));
        CHECK(!UP::PathExists(root + L"\\current"));
        CHECK(!UP::PathExists(BinBridge(root)));  // no current version: no bridge left from the failed one
    }
}

void TestBridgeBinJunctionRefused() {
    std::wstring s = TempDir(L"e2e-bridge-junction"), root = s + L"\\root", feed = s + L"\\feed", elsewhere = s + L"\\elsewhere";
    Canaries c;
    InstalledLayout(root, c);
    MkDirs(elsewhere);
    WriteText(elsewhere + L"\\keep.txt", "untouched");
    CHECK(CreateJunction(root + L"\\bin", elsewhere));
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    CHECK(Update(root, feed) == 0);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status failed", 60000));
    CHECK(ReadText(root + L"\\updates\\last-result").find("bridge-publish") != std::string::npos);
    CHECK(ReadText(root + L"\\current") == "0.3.1\n");
    CHECK(!UP::PathExists(elsewhere + L"\\browser-annotations-mcp.exe"));
    CHECK(ReadText(elsewhere + L"\\keep.txt") == "untouched");
    RemoveDirectoryW((root + L"\\bin").c_str());  // the junction only
}

// Review P2-1: a junction anywhere under root (above the bridge source or at bin) is never followed: nothing from
// outside root is published, nothing outside root is written or deleted.
void TestBridgeJunctionAboveSource() {
    std::wstring s = TempDir(L"e2e-bridge-src-junction"), root = s + L"\\root", outside = s + L"\\outside";
    Canaries c;
    InstalledWithBridge(root, c);
    MkDirs(outside + L"\\assets\\bin");
    Copy(Fx(L"bridge-0.3.3.exe"), outside + L"\\assets\\bin\\browser-annotations-mcp.exe");  // external canary
    const std::string canary = HashOf(outside + L"\\assets\\bin\\browser-annotations-mcp.exe");
    std::wstring assets = root + L"\\versions\\0.3.1\\assets";
    DeleteFileW((assets + L"\\bin\\browser-annotations-mcp.exe").c_str());
    RemoveDirectoryW((assets + L"\\bin").c_str());
    std::wstring keep = root + L"\\versions\\0.3.1\\assets-real";
    CHECK(MoveFileExW(assets.c_str(), keep.c_str(), 0));
    CHECK(CreateJunction(assets, outside + L"\\assets"));
    // bin holds 0.3.1 bytes; a launcher start must not replace them with the external file.
    CHECK(Run(root + L"\\browser.exe", L"--echo src-junction", 10000) == 0);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.1 [src-junction]", 10000));
    CHECK(BinIs(root, L"bridge-0.3.1.exe"));
    // Same with bin empty: still nothing published from outside.
    DeleteFileW(BinBridge(root).c_str());
    CHECK(Run(root + L"\\browser.exe", L"--echo src-junction-2", 10000) == 0);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.1 [src-junction-2]", 10000));
    CHECK(!UP::PathExists(BinBridge(root)));
    CHECK(HashOf(outside + L"\\assets\\bin\\browser-annotations-mcp.exe") == canary);
    RemoveDirectoryW(assets.c_str());  // the junction only
}

void TestLauncherBinJunction() {
    std::wstring s = TempDir(L"e2e-launcher-bin-junction"), root = s + L"\\root", elsewhere = s + L"\\elsewhere";
    Canaries c;
    InstalledWithBridge(root, c);
    DeleteFileW(BinBridge(root).c_str());
    RemoveDirectoryW((root + L"\\bin").c_str());
    MkDirs(elsewhere);
    WriteText(elsewhere + L"\\keep.txt", "untouched");
    CHECK(CreateJunction(root + L"\\bin", elsewhere));
    CHECK(Run(root + L"\\browser.exe", L"--echo bin-junction", 10000) == 0);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.1 [bin-junction]", 10000));
    CHECK(!UP::PathExists(elsewhere + L"\\browser-annotations-mcp.exe") && !UP::PathExists(elsewhere + L"\\.publish-lock"));
    CHECK(ReadText(elsewhere + L"\\keep.txt") == "untouched");
    RemoveDirectoryW((root + L"\\bin").c_str());
}

// Review P2-2: real concurrency. The install pauses between publishing bin and switching `current`, holding the
// bin lock; a launcher started meanwhile waits, then reads `current` again under the lock: it launches 0.3.2 and
// leaves bin on 0.3.2 (a stale 0.3.1 read would have reverted bin to 0.3.1).
void TestLauncherInstallBarrier() {
    std::wstring s = TempDir(L"e2e-launcher-barrier"), root = s + L"\\root", feed = s + L"\\feed";
    Canaries c;
    InstalledWithBridge(root, c);
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    SetFault(L"pause-after-publish");
    CHECK(Update(root, feed) == 0);
    SetFault(nullptr);
    CHECK(WaitFor(root + L"\\updates\\helper.log", "bridge published to bin for 0.3.2", 60000));
    CHECK(ReadText(root + L"\\current") == "0.3.1\n");  // inside the window
    const ULONGLONG t0 = GetTickCount64();
    CHECK(Run(root + L"\\browser.exe", L"--echo barrier", 30000) == 0);
    const ULONGLONG waited = GetTickCount64() - t0;
    std::printf("launcher waited %llu ms for the install's bin lock\n", (unsigned long long)waited);
    CHECK(waited >= 1000);
    CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.2 [barrier]", 10000));
    CHECK(ReadText(root + L"\\fake-launches.txt").find("echo 0.3.1 [barrier]") == std::string::npos);
    CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(BinIs(root, L"bridge-0.3.2.exe") && ReadText(root + L"\\current") == "0.3.2\n");
}

// Review finding 3: first transition from an install whose launcher predates the bridge (helper built from
// 9f73bdd, the release tooling before plan B). The new helper replaces the launcher BEFORE publishing bin, so a crash
// right after the publication is repaired by the next launcher start.
void TestFirstTransitionOldLauncher() {
    {
        std::wstring s = TempDir(L"e2e-old-launcher-kill"), root = s + L"\\root", feed = s + L"\\feed";
        Canaries c;
        InstalledLayout(root, c);  // 0.3.1 without bridge, no bin
        Copy(Fx(L"update-helper-preB.exe"), root + L"\\browser.exe");
        FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
        SetFault(L"kill-after-publish");
        CHECK(Update(root, feed) == 0);
        SetFault(nullptr);
        CHECK(WaitFor(root + L"\\updates\\helper.log", "bridge published to bin for 0.3.2", 60000));
        CHECK(HelperGone(root, 60000));
        CHECK(ReadText(root + L"\\current") == "0.3.1\n");
        CHECK(BinIs(root, L"bridge-0.3.2.exe"));                                   // the mismatch the review found
        CHECK(HashOf(root + L"\\browser.exe") == HashOf(Fx(L"update-helper.exe")));  // launcher already replaced
        CHECK(Run(root + L"\\browser.exe", L"--echo old-launcher", 10000) == 0);
        CHECK(WaitFor(root + L"\\fake-launches.txt", "echo 0.3.1 [old-launcher]", 10000));
        CHECK(!UP::PathExists(BinBridge(root)));  // 0.3.1 ships no bridge: none left in bin
        CHECK(Update(root, feed) == 0);
        CHECK(WaitFor(root + L"\\updates\\last-result", "status installed", 60000));
        CHECK(BinIs(root, L"bridge-0.3.2.exe") && ReadText(root + L"\\current") == "0.3.2\n");
    }
    {
        // Launcher cannot be replaced (busy): the install aborts before publishing anything.
        std::wstring s = TempDir(L"e2e-old-launcher-busy"), root = s + L"\\root", feed = s + L"\\feed";
        Canaries c;
        InstalledLayout(root, c);
        Copy(Fx(L"update-helper-preB.exe"), root + L"\\browser.exe");
        FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
        SetFault(L"launcher-busy");
        CHECK(Update(root, feed) == 0);
        SetFault(nullptr);
        CHECK(WaitFor(root + L"\\updates\\last-result", "status failed", 60000));
        CHECK(ReadText(root + L"\\updates\\last-result").find("launcher busy") != std::string::npos);
        CHECK(HelperGone(root, 30000));
        CHECK(ReadText(root + L"\\current") == "0.3.1\n");
        CHECK(!UP::PathExists(BinBridge(root)));
        CHECK(HashOf(root + L"\\browser.exe") == HashOf(Fx(L"update-helper-preB.exe")));
    }
}

// ---------------------------------------------------------------- portable store migration with a live agent
// (plan B items 4 + 6). Real release-flavoured bridge (bridge-release.exe: BROWSER_CHANNEL_RELEASE) running from the
// portable folder's assets\bin, as a friend's agent would after connecting from the extracted ZIP. The install root
// is <tmp>\browser and the bridge sees LOCALAPPDATA=<tmp>, exactly like %LOCALAPPDATA%\browser in real life.
namespace BA = BrowserAnnotations;

struct PortableAgent {
    std::wstring s, root, portable, portableLog, installedLog;
    std::string id;
    std::unique_ptr<LiveBridge> agent;
    explicit PortableAgent(const std::wstring& name) {
        s = TempDir(name);
        root = s + L"\\browser";
        portable = s + L"\\portable";
        portableLog = s + L"\\root\\fake-launches.txt";  // fake browser: portable fixtures log to <tmp>\root
        installedLog = root + L"\\fake-launches.txt";
        MkDirs(portable + L"\\assets\\bin");
        MkDirs(s + L"\\root");  // the portable fake browser's log folder
        Copy(Fx(L"fake-0.3.1.exe"), portable + L"\\browser.exe");
        Copy(Fx(L"bridge-release.exe"), portable + L"\\" + BridgeRel);
        BA::Annotation a;
        a.site = "https://boutique.test";
        a.note = "Le bouton Payer";
        a.source.url = "https://boutique.test/checkout";
        a.target.selector = "button.pay";
        a.target.tag = "button";
        CHECK(BA::Store(portable + L"\\annotations").WriteAnnotation(a, nullptr, false).ok());
        id = a.id;
        // The agent's environment: LOCALAPPDATA decides where the bridge looks for the installed copy.
        wchar_t saved[4096];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", saved, 4096);
        SetEnvironmentVariableW(L"LOCALAPPDATA", s.c_str());
        agent = std::make_unique<LiveBridge>(portable + L"\\" + BridgeRel, L"");  // no --store: default store
        SetEnvironmentVariableW(L"LOCALAPPDATA", n && n < 4096 ? saved : nullptr);
    }
    int Update(const std::wstring& feed) {
        return Run(portable + L"\\browser.exe", L"--update \"" + root + L"\" \"" + feed + L"\"", 60000);
    }
    std::string Claim(int rpc) {
        const std::string r = agent->Tool(rpc, "browser_annotation_claim", "{\"id\":\"" + id + "\"}");
        const std::size_t at = r.find("\"claim_token\":\"");
        return at == std::string::npos ? "" : r.substr(at + 15, r.find('"', at + 15) - at - 15);
    }
    std::string Resolve(int rpc, const std::string& token) {
        return agent->Tool(rpc, "browser_annotation_resolve", "{\"id\":\"" + id + "\",\"claim_token\":\"" + token +
                                                                  "\",\"outcome\":\"resolved\",\"summary\":\"Bouton corrigé\"}");
    }
    BA::Status StatusIn(const std::wstring& store) const {
        BA::Entry e;
        return BA::Store(store).Find(id, e).ok() ? e.state.status : BA::Status::Open;
    }
    std::string LeaseIn(const std::wstring& store) const {  // token of the lease file, "" when none
        const std::string key = BA::SiteKey("https://boutique.test");
        const std::string text = ReadText(store + L"\\" + std::wstring(key.begin(), key.end()) + L"\\" +
                                          std::wstring(id.begin(), id.end()) + L"\\claim");
        const std::size_t at = text.find("\"token\":\"");
        return at == std::string::npos ? "" : text.substr(at + 9, text.find('"', at + 9) - at - 9);
    }
    std::wstring Installed() const { return root + L"\\data\\annotations"; }
    bool PointerNames(const std::wstring& r) const {
        const std::string t = ReadText(portable + L"\\annotations.migrated");
        return t == "browser-annotations-source 1\nroot " + UP::Narrow(r) + "\n";
    }
    std::wstring Source() const { return portable + L"\\annotations"; }
    std::string RecordState() const {
        const std::string t = ReadText(root + L"\\data\\annotations-migration");
        const std::size_t at = t.find("\nstate ");
        return at == std::string::npos ? "" : t.substr(at + 7, t.find('\n', at + 7) - at - 7);
    }
};
bool IsError(const std::string& response) { return response.find("\"isError\":true") != std::string::npos; }

void TestMigrationCommitWithLiveAgent() {
    PortableAgent t(L"e2e-mig-commit");
    const std::wstring feed = t.s + L"\\feed", feed3 = t.s + L"\\feed3";
    const std::string token = t.Claim(1);
    CHECK(!token.empty() && t.LeaseIn(t.Source()) == token);
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    CHECK(t.Update(feed) == 0);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(t.RecordState() == "committed");
    CHECK(t.PointerNames(t.root));  // source pointer -> this install (written during the copy)
    CHECK(t.LeaseIn(t.Installed()) == token);  // the lease travelled with the copy
    // The SAME running agent, original token: served by the installed store from its next request on.
    const std::string r = t.Resolve(2, token);
    CHECK(!IsError(r));
    CHECK(t.StatusIn(t.Installed()) == BA::Status::Resolved);
    CHECK(t.StatusIn(t.Source()) == BA::Status::Claimed);  // the abandoned source is left as it was
    // Two later updates: the redirect stays valid (record + same forward rules), the store is intact.
    FeedWithBridge(feed3, Fx(L"fake-0.3.3.exe"), "0.3.3", Fx(L"bridge-0.3.3.exe"));
    CHECK(Run(t.root + L"\\versions\\0.3.2\\browser.exe", L"--update \"" + t.root + L"\" \"" + feed3 + L"\"", 60000) == 0);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "version 0.3.3", 60000));
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status installed", 60000));
    const std::string list = t.agent->Tool(3, "browser_annotation_get", "{\"id\":\"" + t.id + "\"}");
    CHECK(!IsError(list) && list.find("Bouton corrig") != std::string::npos);
}

// Helper killed after the new version started but before the commit: no redirect, the source stays authoritative
// (the agent keeps writing there), the installed copy refuses mutations; the next launcher start re-copies the
// source (including writes made after the crash), commits, and the same agent continues with its newest token.
void TestMigrationKillBeforeCommit() {
    PortableAgent t(L"e2e-mig-kill");
    const std::wstring feed = t.s + L"\\feed";
    const std::string first = t.Claim(1);
    CHECK(!first.empty());
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    SetFault(L"kill-before-commit");
    CHECK(t.Update(feed) == 0);
    SetFault(nullptr);
    CHECK(WaitFor(t.root + L"\\updates\\helper.log", "new version launched", 60000));
    CHECK(HelperGone(t.root, 60000));
    CHECK(t.RecordState() == "copied");
    {
        const ULONGLONG t0 = GetTickCount64();
        bool freed = false;
        while (!freed && GetTickCount64() - t0 < 15000) {
            HANDLE g = CreateFileW((t.Source() + L".write-gate").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            if (g != INVALID_HANDLE_VALUE) { CloseHandle(g); freed = true; } else Sleep(50);
        }
        // The kernel closes a killed process's handles shortly after its image is released: an agent request in
        // that window gets the retryable Busy. Never held for good (no stale lock after a crash).
        std::printf("gate after helper kill: %s after %llu ms\n", freed ? "free" : "STILL HELD",
                    (unsigned long long)(GetTickCount64() - t0));
        CHECK(freed);
    }
    CHECK(t.PointerNames(t.root));  // present, but freezes nothing: the record is not committed
    // The agent is still on the source: release + claim again there (a write AFTER the copy).
    {
        const std::string rel = t.agent->Tool(2, "browser_annotation_release", "{\"id\":\"" + t.id + "\",\"claim_token\":\"" + first +
                                                                                   "\",\"reason\":\"test\"}");
        if (IsError(rel)) std::printf("release after crash: %s\n", rel.substr(0, 400).c_str());
        CHECK(!IsError(rel));
    }
    const std::string second = t.Claim(3);
    CHECK(!second.empty() && second != first && t.LeaseIn(t.Source()) == second);
    CHECK(t.LeaseIn(t.Installed()) == first);  // stale copy, not served to anyone
    BA::Annotation other;
    other.site = "https://boutique.test";
    other.note = "installée";
    other.source.url = "https://boutique.test/";
    CHECK(BA::Store(t.Installed()).WriteAnnotation(other, nullptr, false).code == BA::Code::Busy);  // installed copy frozen
    // Bounded recovery at the next launcher start.
    CHECK(Run(t.root + L"\\browser.exe", L"--echo recover", 10000) == 0);
    CHECK(WaitFor(t.installedLog, "echo 0.3.2 [recover]", 10000));
    CHECK(t.RecordState() == "committed");
    CHECK(t.LeaseIn(t.Installed()) == second);  // nothing written after the crash was lost
    CHECK(!IsError(t.Resolve(4, second)));
    CHECK(t.StatusIn(t.Installed()) == BA::Status::Resolved);
}

void TestMigrationRollbackNeverRedirects() {
    PortableAgent t(L"e2e-mig-rollback");
    const std::wstring feed = t.s + L"\\feed";
    const std::string token = t.Claim(1);
    FeedWithBridge(feed, Fx(L"fake-0.3.2-crash.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    CHECK(t.Update(feed) == 0);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status failed", 60000));
    CHECK(WaitFor(t.portableLog, "start 0.3.1 phase=0", 20000));
    CHECK(!UP::PathExists(t.root + L"\\data"));  // the stale pointer left in the source freezes nothing (no record)
    CHECK(!IsError(t.Resolve(2, token)));
    CHECK(t.StatusIn(t.Source()) == BA::Status::Resolved);
}

// During the copy window the helper holds the gate: the agent's requests get a retryable error and change nothing;
// right after the commit the same agent works on the installed store.
void TestMigrationBusyDuringCopy() {
    PortableAgent t(L"e2e-mig-busy");
    const std::wstring feed = t.s + L"\\feed";
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    SetFault(L"pause-after-migration");
    CHECK(t.Update(feed) == 0);
    SetFault(nullptr);
    CHECK(WaitFor(t.root + L"\\updates\\helper.log", "portable data migrated", 60000));
    const std::string during = t.agent->Tool(1, "browser_annotation_claim", "{\"id\":\"" + t.id + "\"}");
    CHECK(IsError(during) && during.find("Migration des annotations") != std::string::npos);
    CHECK(t.LeaseIn(t.Source()).empty());
    // Reads too: the store is resolved and read under the gate (bridge level), never from a store being copied.
    const std::string read = t.agent->Tool(5, "browser_annotation_get", "{\"id\":\"" + t.id + "\"}");
    CHECK(IsError(read) && read.find("version installée en cours") != std::string::npos);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status installed", 60000));
    const std::string token = t.Claim(2);
    CHECK(!token.empty() && t.LeaseIn(t.Installed()) == token && t.LeaseIn(t.Source()).empty());
}


// Review P2 (gate fail-open): an unusable gate (a directory planted at its name) never lets the helper copy without
// the lock: the install aborts before anything is copied, and the agent gets an explicit error instead of a write.
void TestMigrationGateUnusable() {
    PortableAgent t(L"e2e-mig-gate-dir");
    const std::wstring feed = t.s + L"\\feed";
    DeleteFileW((t.Source() + L".write-gate").c_str());
    CHECK(CreateDirectoryW((t.Source() + L".write-gate").c_str(), nullptr));
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    CHECK(t.Update(feed) == 0);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status failed", 60000));
    CHECK(ReadText(t.root + L"\\updates\\last-result").find("annotations-gate-unusable") != std::string::npos);
    CHECK(!UP::PathExists(t.root + L"\\data") && !UP::PathExists(t.root + L"\\current"));
    const std::string r = t.agent->Tool(1, "browser_annotation_claim", "{\"id\":\"" + t.id + "\"}");
    CHECK(IsError(r) && r.find("inutilisable") != std::string::npos && t.LeaseIn(t.Source()).empty());
    RemoveDirectoryW((t.Source() + L".write-gate").c_str());
    CHECK(!t.Claim(2).empty());  // plain gate again: the source works
}

// Review P2 (commit split): the commit write fails after the ack. Nothing is half-visible: no redirect, the source
// stays writable for the same agent; the next launcher start re-copies (newest token) and commits atomically.
void TestMigrationCommitWriteFails() {
    PortableAgent t(L"e2e-mig-commit-fails");
    const std::wstring feed = t.s + L"\\feed";
    const std::string first = t.Claim(1);
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    SetFault(L"commit-write");
    CHECK(t.Update(feed) == 0);
    SetFault(nullptr);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(t.RecordState() == "copied" && t.PointerNames(t.root));
    CHECK(!IsError(t.agent->Tool(2, "browser_annotation_release", "{\"id\":\"" + t.id + "\",\"claim_token\":\"" + first +
                                                                      "\",\"reason\":\"test\"}")));  // source still the store
    const std::string second = t.Claim(3);
    CHECK(!second.empty() && t.LeaseIn(t.Source()) == second);
    CHECK(Run(t.root + L"\\browser.exe", L"--echo recover", 10000) == 0);
    CHECK(WaitFor(t.installedLog, "echo 0.3.2 [recover]", 10000));
    CHECK(t.RecordState() == "committed" && t.LeaseIn(t.Installed()) == second);
    CHECK(!IsError(t.Resolve(4, second)) && t.StatusIn(t.Installed()) == BA::Status::Resolved);
    BA::Annotation late;
    late.site = "https://boutique.test";
    late.note = "tardive";
    late.source.url = "https://boutique.test/";
    CHECK(BA::Store(t.Source()).WriteAnnotation(late, nullptr, false).code == BA::Code::Busy);  // source now frozen
}

// Review P2 (recovery without a usable install): the first portable install dies after the copy, BEFORE the switch
// (record « copied », no current). A retry that fails before switching must not commit: the source stays writable
// for the same running agent. A later valid retry finishes the migration with the newest writes and token.
void TestMigrationPreSwitchKillThenFailedRetry() {
    PortableAgent t(L"e2e-mig-preswitch");
    const std::wstring feed = t.s + L"\\feed";
    const std::string first = t.Claim(1);
    FeedWithBridge(feed, Fx(L"fake-0.3.2.exe"), "0.3.2", Fx(L"bridge-0.3.2.exe"));
    SetFault(L"kill-after-migration");
    CHECK(t.Update(feed) == 0);
    SetFault(nullptr);
    CHECK(WaitFor(t.root + L"\\updates\\helper.log", "portable data migrated", 60000));
    CHECK(HelperGone(t.root, 60000));
    CHECK(t.RecordState() == "copied" && !UP::PathExists(t.root + L"\\current"));
    // Failed retry: the helper starts (recovery runs first), then fails before its switch.
    SetFault(L"current-write");
    CHECK(t.Update(feed) == 0);
    SetFault(nullptr);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status failed", 60000));
    CHECK(HelperGone(t.root, 60000));
    CHECK(t.RecordState() == "copied" && !UP::PathExists(t.root + L"\\current"));  // nothing committed
    const std::string rel = t.agent->Tool(2, "browser_annotation_release", "{\"id\":\"" + t.id + "\",\"claim_token\":\"" + first +
                                                                               "\",\"reason\":\"test\"}");
    CHECK(!IsError(rel));  // the source is still writable (not frozen)
    const std::string second = t.Claim(3);
    CHECK(!second.empty() && t.LeaseIn(t.Source()) == second);
    // Valid retry: installed, then the interrupted migration is finished from the source after the ack.
    CHECK(t.Update(feed) == 0);
    CHECK(WaitFor(t.root + L"\\updates\\last-result", "status installed", 60000));
    CHECK(HelperGone(t.root, 60000));
    CHECK(t.RecordState() == "committed" && t.LeaseIn(t.Installed()) == second);
    CHECK(!IsError(t.Resolve(4, second)) && t.StatusIn(t.Installed()) == BA::Status::Resolved);
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
    TestBridgeUpdateWithLiveAgent();
    TestBridgeRollbackWithLiveAgent();
    TestBridgeFaults();
    TestBridgeKills();
    TestBridgePortableFirstInstallAndRollback();
    TestBridgeBinJunctionRefused();
    TestBridgeJunctionAboveSource();
    TestLauncherBinJunction();
    TestLauncherInstallBarrier();
    TestFirstTransitionOldLauncher();
    TestMigrationCommitWithLiveAgent();
    TestMigrationKillBeforeCommit();
    TestMigrationRollbackNeverRedirects();
    TestMigrationBusyDuringCopy();
    TestMigrationGateUnusable();
    TestMigrationCommitWriteFails();
    TestMigrationPreSwitchKillThenFailedRetry();
    return Report("install_test");
}
