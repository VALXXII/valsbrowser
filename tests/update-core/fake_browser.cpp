// Fake browser.exe for isolated install/rollback fixtures. Never loads WebView2.
// Build: -DFAKE_VERSION=L"0.3.2" -DFAKE_CRASH=0|1
//   (no args)            launched by helper/launcher: NoteStartup (unless crash build) and log the result.
//   --echo <args...>     log arguments (launcher pass-through test).
//   --hold <ms>          log, sleep, exit (a "running browser").
//   --update <root> <feed>  run the real Engine against DiskTransport(feed): check, download, RestartInstall,
//                           write a session file like the host would, sleep, exit.
#include <windows.h>
#include <shellapi.h>

#include <string>

#include "mock_transport.h"

#ifndef FAKE_VERSION
#define FAKE_VERSION L"0.3.1"
#endif
#ifndef FAKE_CRASH
#define FAKE_CRASH 0
#endif

namespace {
std::wstring Parent(const std::wstring& p) { return p.substr(0, p.find_last_of(L'\\')); }
std::wstring g_root;
void Log(const std::string& line) {
    if (g_root.empty()) return;
    HANDLE f = CreateFileW((g_root + L"\\fake-launches.txt").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string s = line + "\n";
    DWORD put = 0;
    WriteFile(f, s.data(), DWORD(s.size()), &put, nullptr);
    CloseHandle(f);
}
std::string Ver() { return UpdatePackage::Narrow(FAKE_VERSION); }
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t self[4096];
    GetModuleFileNameW(nullptr, self, 4096);
    std::wstring exe = self, dir = Parent(exe);
    bool installed = Parent(Parent(dir)).size() > 3 && _wcsicmp(Parent(dir).substr(Parent(dir).find_last_of(L'\\') + 1).c_str(), L"versions") == 0;
    if (installed) g_root = Parent(Parent(dir));
    else g_root = Parent(dir) + L"\\root";  // portable fixture: <tmp>\portable\browser.exe logs to <tmp>\root

    if (argc >= 2 && std::wstring(argv[1]) == L"--echo") {
        std::string line = "echo " + Ver();
        for (int i = 2; i < argc; ++i) line += " [" + UpdatePackage::Narrow(argv[i]) + "]";
        Log(line);
        return 0;
    }
    if (argc >= 3 && std::wstring(argv[1]) == L"--hold") {
        Log("hold " + Ver());
        Sleep(DWORD(_wtoi(argv[2])));
        Log("hold-exit " + Ver());
        return 0;
    }
    if (argc >= 4 && std::wstring(argv[1]) == L"--update") {
        UpdateCore::Config c;
        c.currentVersion = FAKE_VERSION;
        c.exePath = exe;
        c.installRoot = argv[2];
        c.repository = L"VALXXII/browser-releases";
        c.automaticCheck = false;
        c.startupDelayMs = 0;
        c.transport = std::make_shared<UpdateTest::DiskTransport>(argv[3], c.repository);
        UpdateCore::Engine engine;
        engine.Start(c);
        auto waitPhase = [&](BrowserUpdate::Phase p) {
            for (int i = 0; i < 400; ++i) {
                if (engine.View().phase == p) return true;
                Sleep(25);
            }
            return false;
        };
        engine.CheckNow();
        if (!waitPhase(BrowserUpdate::Phase::Available)) return Log("update: no Available"), 4;
        engine.Perform(BrowserUpdate::Action::Download);
        if (!waitPhase(BrowserUpdate::Phase::Ready)) return Log("update: no Ready " + UpdatePackage::Narrow(engine.View().error)), 5;
        auto result = engine.Perform(BrowserUpdate::Action::RestartInstall);
        if (result != UpdateCore::ActionResult::CloseForInstall)
            return Log("update: install refused " + UpdatePackage::Narrow(engine.View().error)), 6;
        engine.Shutdown();
        // Host behaviour: save session (atomic), then exit. The helper must wait for this exact process.
        auto paths = UpdateCore::ResolveDataPaths(exe, c.installRoot);
        UpdatePackage::AtomicWriteFile(paths.sessionFile, "session-from-" + Ver() + "\n");
        Sleep(800);
        Log("old-exit " + Ver());
        return 0;
    }
#if FAKE_CRASH
    Log("crash " + Ver());
    return 3;
#else
    UpdateCore::Config c;
    c.currentVersion = FAKE_VERSION;
    c.exePath = exe;
    c.installRoot = installed ? g_root : L"";
    BrowserUpdate::View v;
    if (installed) v = UpdateCore::NoteStartup(c);
    Log("start " + Ver() + " phase=" + std::to_string(int(v.phase)) + " data=" +
        UpdatePackage::Narrow(UpdateCore::ResolveDataPaths(exe, installed ? g_root : L"").dataDir));
    Sleep(300);
    return 0;
#endif
}
