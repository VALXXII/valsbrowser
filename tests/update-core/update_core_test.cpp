// Deterministic unit + engine tests for update-package / update-core. No network, no WebView2, isolated temp dirs.
#include <windows.h>

#include <vector>

#include "mock_transport.h"
#include "test_util.h"

using namespace UpdateTest;
namespace UP = UpdatePackage;
using BrowserUpdate::Action;
using BrowserUpdate::Phase;

namespace {

const std::wstring Repo = L"VALXXII/browser-releases";

std::vector<std::pair<std::string, std::string>> Payload(const std::string& tag, std::size_t bigBytes = 0) {
    std::vector<std::pair<std::string, std::string>> files = {
        {"browser.exe", "MZ fake browser " + tag},
        {"update-helper.exe", "MZ fake helper " + tag},
        {"WebView2Loader.dll", "dll"},
        {"ThorVG-LICENSE.txt", "license"},
        {"assets/fonts/Inter Display (1).ttf", "font"},
        {"assets/icons/back-96.png", std::string(1000, 'x')},
    };
    if (bigBytes) {
        std::string big(bigBytes, '\0');
        for (std::size_t i = 0; i < big.size(); ++i) big[i] = char(i * 31 + 7);
        files.push_back({"assets/big.bin", big});
    }
    return files;
}

UP::Manifest ManifestFor(const std::wstring& packageFile, const std::string& version) {
    UP::Manifest m;
    m.version = version;
    m.arch = "x64";
    m.package = UP::PackageFileName(version);
    UP::Sha256File(packageFile, m.sha256, m.size);
    return m;
}

void TestVersions() {
    UP::Version a, b;
    CHECK(UP::ParseVersion(std::string("0.3.10"), a) && a.build == 10);
    CHECK(UP::ParseVersion(std::string("0.3.9"), b) && UP::CompareVersion(a, b) > 0);  // numeric, not lexicographic
    CHECK(UP::ParseVersion(std::string("1.0.0"), b) && UP::CompareVersion(a, b) < 0);
    CHECK(UP::ParseVersion(std::string("0.0.0"), a) && UP::IsZero(a));
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.-3", "1.2.3 ", " 1.2.3", "1.2.a", "1..3",
                            "1234567890.0.0", "+1.2.3", "1.2.3\n"})
        CHECK(!UP::ParseVersion(std::string(bad), a));
    CHECK(!UP::ParseVersion(std::wstring(L"1.2.\u0663"), a));
}

void TestManifest() {
    std::string sha(64, 'a');
    std::string good = "browser-update 1\nversion 0.3.42\narch x64\npackage browser-0.3.42-win-x64.lsrupd\nsize 1234\nsha256 " + sha + "\n";
    UP::Manifest m;
    std::string error;
    CHECK(UP::ParseManifest(good, m, error) && m.version == "0.3.42" && m.size == 1234);
    CHECK(UP::FormatManifest(m) == good);
    CHECK(UP::ParseManifest(good.substr(0, good.size() - 1), m, error));  // final LF optional
    auto bad = [&](std::string from, std::string to) {
        std::string s = good;
        s.replace(s.find(from), from.size(), to);
        UP::Manifest x;
        std::string e;
        return !UP::ParseManifest(s, x, e);
    };
    CHECK(bad("browser-update 1", "browser-update 2"));
    CHECK(bad("arch x64", "arch arm64"));
    CHECK(bad("arch x64", "arch x86"));
    CHECK(bad("version 0.3.42", "version 0.3.43"));               // package name mismatch
    CHECK(bad("package browser-0.3.42-win-x64.lsrupd", "package https://evil.example/x.lsrupd"));
    CHECK(bad("size 1234", "size 0"));
    CHECK(bad("size 1234", "size 01234"));
    CHECK(bad("size 1234", "size 999999999999"));                  // > 512 MiB
    CHECK(bad(sha, std::string(64, 'A')));                         // uppercase hex
    CHECK(bad(sha, std::string(63, 'a')));
    CHECK(bad("\n", "\r\n"));                                      // CR rejected
    CHECK(bad("arch x64\n", "arch x64\nextra 1\n"));
    CHECK(bad("arch x64", "arch  x64"));
    CHECK(bad("version 0.3.42\narch", "version 0.0.0\narch"));
    CHECK(!UP::ParseManifest(good + std::string(1100, '#'), m, error));
    CHECK(!UP::ParseManifest("", m, error));
}

void TestUrls() {
    CHECK(UP::ValidRepository(Repo));
    for (auto bad : {L"", L"VALXXII", L"a/b/c", L"../b", L"a/..", L"a b/c", L"a/c?x", L"a/c#", L"a\\c"}) CHECK(!UP::ValidRepository(bad));
    std::wstring manifest = UpdateCore::ManifestUrl(Repo);
    CHECK(manifest == L"https://github.com/VALXXII/browser-releases/releases/latest/download/update-manifest-win-x64.txt");
    CHECK(UpdateCore::PackageUrl(Repo, "0.3.42") ==
          L"https://github.com/VALXXII/browser-releases/releases/download/v0.3.42/browser-0.3.42-win-x64.lsrupd");
    CHECK(UpdateCore::AllowedUrl(manifest, Repo, true));
    CHECK(!UpdateCore::AllowedUrl(L"http://github.com/VALXXII/browser-releases/releases/x", Repo, true));
    CHECK(!UpdateCore::AllowedUrl(L"https://github.com:444/VALXXII/browser-releases/releases/x", Repo, true));
    CHECK(!UpdateCore::AllowedUrl(L"https://user:pw@github.com/VALXXII/browser-releases/releases/x", Repo, true));
    CHECK(!UpdateCore::AllowedUrl(L"https://github.com/VALXXII/browser/releases/x", Repo, true));
    CHECK(!UpdateCore::AllowedUrl(L"https://github.com/VALXXII/browser-releases/releases/../../evil/x", Repo, true));
    CHECK(!UpdateCore::AllowedUrl(L"https://evil.example/VALXXII/browser-releases/releases/x", Repo, true));
    CHECK(UpdateCore::AllowedUrl(L"https://objects.githubusercontent.com/github-production-release-asset/1?x=y", Repo, false));
    CHECK(UpdateCore::AllowedUrl(L"https://release-assets.githubusercontent.com/github-production-release-asset/1", Repo, false));
    CHECK(UpdateCore::AllowedUrl(L"https://github.com:443/a", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"https://objects.githubusercontent.com.evil.example/a", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"https://github.com.evil.example/a", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"https://github.com:443@evil.example/a", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"http://objects.githubusercontent.com/a", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"https://evil.example/a", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"file:///C:/x", Repo, false));
    CHECK(!UpdateCore::AllowedUrl(L"https://github.com/a b", Repo, false));
}

void TestPaths() {
    std::string e;
    for (auto ok : {"browser.exe", "assets/fonts/Inter Display (1).ttf", "assets/a+b.png", "ThorVG-LICENSE.txt"}) CHECK(UP::ValidEntryPath(ok, e));
    for (auto bad : {"", "../x", "assets/../x", "./x", "/abs", "C:/x", "C:x", "\\\\server\\share\\x", "assets\\x", "assets//x",
                     "assets/", "CON", "assets/nul.txt", "assets/COM1.png", "assets/x.", "assets/x ", ".stage", "assets/.hidden",
                     "assets/a:b", "assets/x*", "assets/~1", "assets/\xc3\xa9.png", "assets/x\ty"})
        CHECK(!UP::ValidEntryPath(bad, e));
    CHECK(!UP::ValidEntryPath(std::string(241, 'a'), e));
    for (auto ok : {"browser.exe", "update-helper.exe", "WebView2Loader.dll", "build-info.txt", "version.json", "WebView2-NOTICE.txt",
                    "assets/x/y.png"})
        CHECK(UP::AllowedPayloadPath(ok));
    for (auto bad : {"profile/Cookies", "session", "session.tmp", "prototype.log", "evil.dll", "data/profile/x", "browser.EXE",
                     "LICENSE.txt", "versions/0.1.0/browser.exe", "current"})
        CHECK(!UP::AllowedPayloadPath(bad));
}

void TestPackage() {
    std::wstring dir = TempDir(L"package");
    std::wstring pkg = dir + L"\\p.lsrupd";
    CHECK(UP::WritePackage(pkg, "0.3.2", "x64", Payload("a")));
    UP::PackageIndex index;
    std::string error;
    CHECK(UP::ReadPackageIndex(pkg, index, error) && index.entries.size() == 6 && index.version == "0.3.2");
    std::wstring staging = dir + L"\\staging";
    CreateDirectoryW(staging.c_str(), nullptr);
    std::wstring out = staging + L"\\0.3.2";
    CHECK(UP::ExtractPackage(pkg, "0.3.2", out, nullptr, error));
    CHECK(ReadText(out + L"\\assets\\fonts\\Inter Display (1).ttf") == "font");
    CHECK(UP::VerifyStagedTree(out, "0.3.2", error));
    CHECK(!UP::VerifyStagedTree(out, "0.3.3", error));
    CHECK(!UP::ExtractPackage(pkg, "0.3.2", out, nullptr, error));  // never extracts into an existing directory
    CHECK(UP::PathExists(out + L"\\browser.exe"));                  // ... and does not remove it
    // Tamper, extra file, missing file, junction.
    WriteText(out + L"\\assets\\icons\\back-96.png", std::string(1000, 'y'));
    CHECK(!UP::VerifyStagedTree(out, "0.3.2", error));
    WriteText(out + L"\\assets\\icons\\back-96.png", std::string(1000, 'x'));
    CHECK(UP::VerifyStagedTree(out, "0.3.2", error));
    WriteText(out + L"\\assets\\extra.txt", "x");
    CHECK(!UP::VerifyStagedTree(out, "0.3.2", error));
    DeleteFileW((out + L"\\assets\\extra.txt").c_str());
    DeleteFileW((out + L"\\WebView2Loader.dll").c_str());
    CHECK(!UP::VerifyStagedTree(out, "0.3.2", error));
    WriteText(out + L"\\WebView2Loader.dll", "dll");
    CHECK(UP::VerifyStagedTree(out, "0.3.2", error));
    std::wstring outside = dir + L"\\outside";
    CreateDirectoryW(outside.c_str(), nullptr);
    if (CreateJunction(out + L"\\assets\\link", outside)) {
        CHECK(!UP::VerifyStagedTree(out, "0.3.2", error));
        RemoveDirectoryW((out + L"\\assets\\link").c_str());
        CHECK(UP::VerifyStagedTree(out, "0.3.2", error));
        // Junction as staging parent is refused.
        std::wstring linkParent = dir + L"\\linkparent";
        CHECK(CreateJunction(linkParent, outside));
        CHECK(!UP::ExtractPackage(pkg, "0.3.2", linkParent + L"\\0.3.2", nullptr, error));
        CHECK(!UP::PathExists(outside + L"\\0.3.2"));
        RemoveDirectoryW(linkParent.c_str());
        // Junction pre-planted in the staged directory path ("assets" -> outside) is detected during extraction.
        std::wstring trap = staging + L"\\trap";
        CreateDirectoryW(trap.c_str(), nullptr);
        UP::RemoveTree(trap);
    } else {
        std::printf("note: junction creation unavailable, reparse checks skipped\n");
    }
    // Structural rejections.
    auto rejects = [&](const std::vector<std::pair<std::string, std::string>>& files, const std::string& version = "0.3.2",
                       const std::string& arch = "x64") {
        std::wstring p = dir + L"\\bad.lsrupd";
        UP::WritePackage(p, version, arch, files);
        UP::PackageIndex i;
        std::string e;
        return !UP::ReadPackageIndex(p, i, e);
    };
    auto withExtra = [&](const std::string& path) {
        auto f = Payload("a");
        f.push_back({path, "x"});
        return f;
    };
    CHECK(rejects(withExtra("../evil.exe")));
    CHECK(rejects(withExtra("assets/../../evil")));
    CHECK(rejects(withExtra("C:/Windows/evil")));
    CHECK(rejects(withExtra("\\\\server\\share\\evil")));
    CHECK(rejects(withExtra("profile/Cookies")));
    CHECK(rejects(withExtra("session")));
    CHECK(rejects(withExtra("evil.dll")));
    CHECK(rejects(withExtra("BROWSER.exe")));        // not allowlisted exactly
    CHECK(rejects(withExtra("assets/ICONS/back-96.png")));  // case-insensitive duplicate
    CHECK(rejects(withExtra("assets/icons/back-96.png/x")));  // file/dir conflict
    CHECK(rejects(withExtra("assets/icons")));               // dir/file conflict
    CHECK(rejects(Payload("a"), "0.3.2", "arm64"));
    CHECK(rejects(Payload("a"), "0.3", "x64"));
    {
        auto f = Payload("a");
        f.erase(f.begin() + 1);  // no update-helper.exe
        CHECK(rejects(f));
    }
    // Byte-level corruption.
    std::string bytes = ReadBinary(pkg);
    auto rejectsBytes = [&](const std::string& data) {
        std::wstring p = dir + L"\\raw.lsrupd";
        WriteText(p, data);
        UP::PackageIndex i;
        std::string e;
        return !UP::ReadPackageIndex(p, i, e);
    };
    CHECK(!rejectsBytes(bytes));
    CHECK(rejectsBytes(bytes + "x"));                         // trailing data
    CHECK(rejectsBytes(bytes.substr(0, bytes.size() - 1)));   // truncated trailer
    CHECK(rejectsBytes(bytes.substr(0, bytes.size() / 2)));   // partial download
    CHECK(rejectsBytes("XSRUPD01" + bytes.substr(8)));
    {
        std::string huge = bytes;
        std::uint32_t count = 100000;
        std::size_t countOffset = 8 + 2 + 5 + 2 + 3;
        std::memcpy(&huge[countOffset], &count, 4);
        CHECK(rejectsBytes(huge));
    }
    // Content hash mismatch inside the package: extraction fails and leaves no staging directory.
    {
        std::string corrupt = bytes;
        auto pos = corrupt.find("MZ fake browser a");
        corrupt[pos + 3] = 'X';
        std::wstring p = dir + L"\\corrupt.lsrupd";
        WriteText(p, corrupt);
        UP::PackageIndex i;
        CHECK(UP::ReadPackageIndex(p, i, error));  // structure is fine...
        CHECK(!UP::ExtractPackage(p, "0.3.2", staging + L"\\c", nullptr, error));  // ...content is not
        CHECK(!UP::PathExists(staging + L"\\c"));
    }
    CHECK(!UP::ExtractPackage(pkg, "0.3.3", staging + L"\\v", nullptr, error));  // version mismatch with manifest
    std::atomic<bool> cancel{true};
    CHECK(!UP::ExtractPackage(pkg, "0.3.2", staging + L"\\cancelled", &cancel, error) && !UP::PathExists(staging + L"\\cancelled"));
}

void TestDataPaths() {
    std::wstring root = TempDir(L"datapaths");
    auto installed = UpdateCore::ResolveDataPaths(root + L"\\versions\\0.3.2\\browser.exe", root);
    CHECK(installed.installed && installed.profileDir == root + L"\\data\\profile" && installed.sessionFile == root + L"\\data\\session");
    auto portable = UpdateCore::ResolveDataPaths(L"C:\\dev\\build\\v2\\browser.exe", root);
    CHECK(!portable.installed && portable.profileDir == L"C:\\dev\\build\\v2\\profile" && portable.sessionFile == L"C:\\dev\\build\\v2\\session");
    CHECK(!UpdateCore::ResolveDataPaths(root + L"\\versions\\dev\\browser.exe", root).installed);
    CHECK(!UpdateCore::ResolveDataPaths(root + L"\\versions\\0.3.2\\other.exe", root).installed);
    CHECK(!UpdateCore::ResolveDataPaths(root + L"\\x\\versions\\0.3.2\\browser.exe", root).installed);
    CHECK(!UP::PathExists(root + L"\\data"));  // pure: creates nothing
}

// ---------------------------------------------------------------- engine

UpdateCore::Engine* g_engine = nullptr;
int g_notifies = 0, g_stale = 0;
const UINT NotifyMessage = WM_APP + 7;
std::vector<BrowserUpdate::View> g_views;

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == NotifyMessage) {
        if (g_engine && g_engine->OnNotify(w)) {
            ++g_notifies;
            g_views.push_back(g_engine->View());
        } else {
            ++g_stale;
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
HWND Window() {
    static HWND w = nullptr;
    if (w) return w;
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"UpdateCoreTest";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    w = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return w;
}
template <class F>
bool PumpUntil(F done, DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (done()) return true;
        if (GetTickCount64() > end) return false;
        Sleep(5);
    }
}
void Pump(DWORD ms) { PumpUntil([] { return false; }, ms); }

struct Fixture {
    std::wstring root;
    std::shared_ptr<MockTransport> transport = std::make_shared<MockTransport>();
    std::int64_t now = 1700000000;
    UpdateCore::Config Config(const std::wstring& version = L"0.3.1") {
        UpdateCore::Config c;
        c.currentVersion = version;
        c.exePath = root + L"\\versions\\" + version + L"\\browser.exe";
        c.installRoot = root;
        c.repository = Repo;
        c.notifyWindow = Window();
        c.notifyMessage = NotifyMessage;
        c.startupDelayMs = 0;
        c.transport = transport;
        c.clock = [this] { return now; };
        return c;
    }
    void Publish(const std::string& version, std::size_t bigBytes, DWORD chunkDelay = 0, std::size_t chunk = 64 * 1024) {
        std::wstring pkg = root + L"\\..\\" + UP::Widen(version) + L".lsrupd";
        UP::WritePackage(pkg, version, "x64", Payload(version, bigBytes));
        auto m = ManifestFor(pkg, version);
        MockResponse man;
        man.body = UP::FormatManifest(m);
        transport->Set(UpdateCore::ManifestUrl(Repo), man);
        MockResponse body;
        body.body = ReadBinary(pkg);
        body.chunkDelayMs = chunkDelay;
        body.chunk = chunk;
        transport->Set(UpdateCore::PackageUrl(Repo, version), body);
    }
};

Phase PhaseOf(UpdateCore::Engine& e) { return e.View().phase; }

void TestEngineStartupQuiet() {
    Fixture f;
    f.root = TempDir(L"engine-quiet");
    {  // no release published: 404 => quiet, TTL 6h
        MockResponse nf;
        nf.status = UpdateCore::HttpResult::NotFound;
        f.transport->Set(UpdateCore::ManifestUrl(Repo), nf);
        UpdateCore::Engine e;
        g_engine = &e;
        e.Start(f.Config());
        CHECK(PumpUntil([&] { return e.LastCheckResult() != UpdateCore::Engine::LastCheck::None; }, 3000));
        CHECK(e.LastCheckResult() == UpdateCore::Engine::LastCheck::UpToDate);
        CHECK(PhaseOf(e) == Phase::Hidden);
        CHECK(ReadText(f.root + L"\\updates\\state").find("next-check " + std::to_string(f.now + 6 * 3600)) != std::string::npos);
        e.Shutdown();
    }
    {  // within TTL: no request at all
        int before = f.transport->Requests();
        UpdateCore::Engine e;
        g_engine = &e;
        e.Start(f.Config());
        Pump(200);
        e.Shutdown();
        CHECK(f.transport->Requests() == before);
    }
    {  // offline after TTL: quiet, backoff 1h then 2h
        f.transport->Clear();
        f.now += 7 * 3600;
        UpdateCore::Engine e;
        g_engine = &e;
        e.Start(f.Config());
        CHECK(PumpUntil([&] { return e.LastCheckResult() == UpdateCore::Engine::LastCheck::Failed; }, 3000));
        CHECK(PhaseOf(e) == Phase::Hidden);
        e.Shutdown();
        auto state = ReadText(f.root + L"\\updates\\state");
        CHECK(state.find("failures 1") != std::string::npos && state.find("next-check " + std::to_string(f.now + 3600)) != std::string::npos);
        int before = f.transport->Requests();
        f.now += 1800;
        UpdateCore::Engine e2;
        g_engine = &e2;
        e2.Start(f.Config());
        Pump(200);
        e2.Shutdown();
        CHECK(f.transport->Requests() == before);  // backoff respected
        f.now += 1801;
        UpdateCore::Engine e3;
        g_engine = &e3;
        e3.Start(f.Config());
        CHECK(PumpUntil([&] { return e3.LastCheckResult() == UpdateCore::Engine::LastCheck::Failed; }, 3000));
        e3.Shutdown();
        state = ReadText(f.root + L"\\updates\\state");
        CHECK(state.find("failures 2") != std::string::npos && state.find("next-check " + std::to_string(f.now + 7200)) != std::string::npos);
        // Manual check while offline surfaces an error, Retry re-checks.
        UpdateCore::Engine e4;
        g_engine = &e4;
        auto c = f.Config();
        c.automaticCheck = false;
        e4.Start(c);
        e4.CheckNow();
        CHECK(PumpUntil([&] { return PhaseOf(e4) == Phase::Error; }, 3000));
        CHECK(!e4.View().error.empty());
        f.Publish("0.3.2", 0);
        CHECK(e4.Perform(Action::Retry) == UpdateCore::ActionResult::Handled);
        CHECK(PumpUntil([&] { return PhaseOf(e4) == Phase::Available; }, 3000));
        e4.Shutdown();
    }
    // Disabled builds and invalid repositories never touch the network.
    {
        int before = f.transport->Requests();
        for (auto version : {L"0.0.0", L"dev", L""}) {
            UpdateCore::Engine e;
            g_engine = &e;
            e.Start(f.Config(version));
            CHECK(!e.Enabled());
            e.CheckNow();
            Pump(50);
            e.Shutdown();
        }
        UpdateCore::Engine e;
        auto c = f.Config();
        c.repository = L"evil/../x";
        e.Start(c);
        CHECK(!e.Enabled());
        e.Shutdown();
        CHECK(f.transport->Requests() == before);
    }
}

void TestEngineAvailableAndNotified() {
    Fixture f;
    f.root = TempDir(L"engine-available");
    f.Publish("0.3.2", 0);
    {
        UpdateCore::Engine e;
        g_engine = &e;
        e.Start(f.Config());
        CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Available; }, 3000));
        CHECK(e.View().version == L"0.3.2" && e.View().total > 0);
        e.Shutdown();
    }
    f.now += 7 * 3600;
    {  // same version is not toasted again automatically
        UpdateCore::Engine e;
        g_engine = &e;
        e.Start(f.Config());
        CHECK(PumpUntil([&] { return e.LastCheckResult() == UpdateCore::Engine::LastCheck::Available; }, 3000));
        Pump(100);
        CHECK(PhaseOf(e) == Phase::Hidden);
        e.CheckNow();  // manual still shows it
        CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Available; }, 3000));
        CHECK(e.Perform(Action::Dismiss) == UpdateCore::ActionResult::Handled && PhaseOf(e) == Phase::Hidden);
        CHECK(e.Perform(Action::Dismiss) == UpdateCore::ActionResult::Ignored);
        CHECK(e.Perform(Action::RestartInstall) == UpdateCore::ActionResult::Ignored);
        e.Shutdown();
    }
    {  // equal and older releases are "up to date" (no downgrade)
        UpdateCore::Engine e;
        g_engine = &e;
        auto c = f.Config(L"0.3.2");
        c.automaticCheck = false;
        e.Start(c);
        e.CheckNow();
        CHECK(PumpUntil([&] { return e.LastCheckResult() == UpdateCore::Engine::LastCheck::UpToDate; }, 3000));
        CHECK(PhaseOf(e) == Phase::Hidden);
        e.Shutdown();
        UpdateCore::Engine e2;
        g_engine = &e2;
        c = f.Config(L"0.4.0");
        c.automaticCheck = false;
        e2.Start(c);
        e2.CheckNow();
        CHECK(PumpUntil([&] { return e2.LastCheckResult() == UpdateCore::Engine::LastCheck::UpToDate; }, 3000));
        e2.Shutdown();
    }
    {  // malformed manifest (e.g. arbitrary URL) is an error on manual check, quiet otherwise
        MockResponse evil;
        evil.body = "browser-update 1\nversion 0.3.9\narch x64\npackage https://evil/x\nsize 1\nsha256 " + std::string(64, 'a') + "\n";
        f.transport->Set(UpdateCore::ManifestUrl(Repo), evil);
        UpdateCore::Engine e;
        g_engine = &e;
        auto c = f.Config();
        c.automaticCheck = false;
        e.Start(c);
        e.CheckNow();
        CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Error; }, 3000));
        e.Shutdown();
    }
}

void TestEngineDownload() {
    Fixture f;
    f.root = TempDir(L"engine-download");
    f.Publish("0.3.2", 3 * 1024 * 1024, 15, 64 * 1024);  // ~49 chunks * 15 ms ≈ 0.75 s + extraction
    UpdateCore::Engine e;
    g_engine = &e;
    auto c = f.Config();
    c.automaticCheck = false;
    e.Start(c);
    e.CheckNow();
    CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Available; }, 3000));
    g_views.clear();
    int notifiesBefore = g_notifies;
    CHECK(e.Perform(Action::Download) == UpdateCore::ActionResult::Handled);
    CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Ready; }, 20000));
    int notifies = g_notifies - notifiesBefore;
    CHECK(notifies >= 3 && notifies <= 60);  // bounded (<= 10 Hz) yet live
    std::uint64_t last = 0, total = 0;
    bool monotonic = true, sawUnknownEta = false;
    for (const auto& v : g_views) {
        if (v.phase != Phase::Downloading) continue;
        monotonic = monotonic && v.received >= last;
        last = v.received;
        total = v.total;
        if (v.secondsRemaining < 0) sawUnknownEta = true;
        if (v.secondsRemaining >= 0) CHECK(v.secondsRemaining < 60);
    }
    CHECK(monotonic && total > 3 * 1024 * 1024 && sawUnknownEta);
    std::string error;
    CHECK(UP::VerifyStagedTree(f.root + L"\\updates\\staging\\0.3.2", "0.3.2", error));
    CHECK(!UP::PathExists(f.root + L"\\updates\\downloads\\browser-0.3.2-win-x64.lsrupd.partial"));
    e.Shutdown();
    // Restart: staged payload => Ready once, not repeated after that.
    {
        UpdateCore::Engine e2;
        g_engine = &e2;
        e2.Start(f.Config());
        CHECK(PumpUntil([&] { return PhaseOf(e2) == Phase::Ready; }, 3000));
        e2.Shutdown();
        UpdateCore::Engine e3;
        g_engine = &e3;
        e3.Start(f.Config());
        Pump(300);
        CHECK(PhaseOf(e3) == Phase::Hidden);
        e3.CheckNow();
        CHECK(PhaseOf(e3) == Phase::Ready);
        e3.Shutdown();
    }
    // A tampered staged payload is not offered.
    WriteText(f.root + L"\\updates\\staging\\0.3.2\\browser.exe", "tampered");
    {
        UpdateCore::Engine e4;
        g_engine = &e4;
        auto c4 = f.Config();
        c4.automaticCheck = true;
        f.now += 7 * 3600;
        e4.Start(c4);
        CHECK(PumpUntil([&] { return e4.LastCheckResult() != UpdateCore::Engine::LastCheck::None; }, 3000));
        CHECK(PhaseOf(e4) != Phase::Ready);
        e4.Shutdown();
    }
}

void TestEngineCancelAndFailures() {
    Fixture f;
    f.root = TempDir(L"engine-cancel");
    f.Publish("0.3.2", 4 * 1024 * 1024, 40, 64 * 1024);  // slow: ~2.6 s
    UpdateCore::Engine e;
    g_engine = &e;
    auto c = f.Config();
    c.automaticCheck = false;
    e.Start(c);
    e.CheckNow();
    CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Available; }, 3000));
    e.Perform(Action::Download);
    CHECK(PumpUntil([&] { return e.View().received > 256 * 1024; }, 5000));
    ULONGLONG t0 = GetTickCount64();
    CHECK(e.Perform(Action::Cancel) == UpdateCore::ActionResult::Handled);
    CHECK(GetTickCount64() - t0 < 1000);  // prompt
    CHECK(PhaseOf(e) == Phase::Available);
    Pump(300);
    CHECK(PhaseOf(e) == Phase::Available);  // no stale progress after cancel
    CHECK(!UP::PathExists(f.root + L"\\updates\\downloads\\browser-0.3.2-win-x64.lsrupd.partial"));
    CHECK(!UP::PathExists(f.root + L"\\updates\\staging\\0.3.2"));
    CHECK(!e.OnNotify(12345));  // foreign/stale session id
    // Prompt shutdown during download.
    e.Perform(Action::Download);
    CHECK(PumpUntil([&] { return e.View().received > 128 * 1024; }, 5000));
    t0 = GetTickCount64();
    e.Shutdown();
    CHECK(GetTickCount64() - t0 < 1000);
    CHECK(e.Perform(Action::Download) == UpdateCore::ActionResult::Ignored);

    // Hash mismatch, wrong Content-Length, oversize body: error, nothing staged, Retry recovers.
    Fixture g;
    g.root = TempDir(L"engine-failures");
    g.Publish("0.3.2", 0);
    auto good = std::string();
    {
        auto pkgUrl = UpdateCore::PackageUrl(Repo, "0.3.2");
        UP::Manifest m;
        std::string err;
        std::string manifestText;
        UP::ReadSmallFile(g.root + L"\\..\\0.3.2.lsrupd", 1 << 24, good);
        auto variants = std::vector<MockResponse>(3);
        variants[0].body = good;
        variants[0].body[20] ^= 1;                    // hash mismatch
        variants[1].body = good;
        variants[1].declaredLength = good.size() + 1;  // wrong length header
        variants[2].body = good + "extra";             // longer than manifest size
        for (auto& v : variants) {
            g.transport->Set(pkgUrl, v);
            UpdateCore::Engine x;
            g_engine = &x;
            auto cx = g.Config();
            cx.automaticCheck = false;
            x.Start(cx);
            x.CheckNow();
            CHECK(PumpUntil([&] { return PhaseOf(x) == Phase::Available; }, 3000));
            x.Perform(Action::Download);
            CHECK(PumpUntil([&] { return PhaseOf(x) == Phase::Error; }, 5000));
            CHECK(!UP::PathExists(g.root + L"\\updates\\staging\\0.3.2"));
            CHECK(!UP::PathExists(g.root + L"\\updates\\downloads\\browser-0.3.2-win-x64.lsrupd.partial"));
            MockResponse fixed;
            fixed.body = good;
            g.transport->Set(pkgUrl, fixed);
            CHECK(x.Perform(Action::Retry) == UpdateCore::ActionResult::Handled);
            CHECK(PumpUntil([&] { return PhaseOf(x) == Phase::Ready; }, 5000));
            x.Shutdown();
            UP::RemoveTree(g.root + L"\\updates\\staging\\0.3.2");
        }
        // A package whose payload holds a profile entry is rejected even with matching hash/size.
        std::wstring evilPkg = g.root + L"\\..\\evil.lsrupd";
        auto files = Payload("evil");
        files.push_back({"profile/Cookies", "stolen"});
        UP::WritePackage(evilPkg, "0.3.2", "x64", files);
        auto m2 = ManifestFor(evilPkg, "0.3.2");
        MockResponse man, pkg;
        man.body = UP::FormatManifest(m2);
        pkg.body = ReadBinary(evilPkg);
        g.transport->Set(UpdateCore::ManifestUrl(Repo), man);
        g.transport->Set(pkgUrl, pkg);
        UpdateCore::Engine x;
        g_engine = &x;
        auto cx = g.Config();
        cx.automaticCheck = false;
        x.Start(cx);
        x.CheckNow();
        CHECK(PumpUntil([&] { return PhaseOf(x) == Phase::Available; }, 3000));
        x.Perform(Action::Download);
        CHECK(PumpUntil([&] { return PhaseOf(x) == Phase::Error; }, 5000));
        CHECK(!UP::PathExists(g.root + L"\\updates\\staging\\0.3.2"));
        x.Shutdown();
    }
}

void TestNoteStartup() {
    std::wstring root = TempDir(L"note-startup");
    CreateDirectoryW((root + L"\\updates").c_str(), nullptr);
    std::string nonce(32, 'c');
    WriteText(root + L"\\updates\\pending", "browser-pending 1\nnonce " + nonce + "\nversion 0.3.2\n");
    UpdateCore::Config c;
    c.installRoot = root;
    c.currentVersion = L"0.3.2";
    c.exePath = root + L"\\versions\\0.3.1\\browser.exe";  // wrong exe
    CHECK(UpdateCore::NoteStartup(c).phase == Phase::Hidden);
    c.currentVersion = L"0.3.1";
    CHECK(UpdateCore::NoteStartup(c).phase == Phase::Hidden);  // wrong version
    c.currentVersion = L"0.3.2";
    c.exePath = root + L"\\versions\\0.3.2\\browser.exe";
    auto v = UpdateCore::NoteStartup(c);
    CHECK(v.phase == Phase::Installed && v.version == L"0.3.2");
    CHECK(ReadText(root + L"\\updates\\ack-" + UP::Widen(nonce)).find("pid " + std::to_string(GetCurrentProcessId())) != std::string::npos);
    CHECK(UpdateCore::NoteStartup(c).phase == Phase::Hidden);  // exactly once
    DeleteFileW((root + L"\\updates\\pending").c_str());
    WriteText(root + L"\\updates\\last-result", "browser-result 1\nstatus failed\nversion 0.3.3\nreason x\nreported 0\n");
    v = UpdateCore::NoteStartup(c);
    CHECK(v.phase == Phase::Error && v.version == L"0.3.3" && !v.error.empty());
    CHECK(UpdateCore::NoteStartup(c).phase == Phase::Hidden);
}

std::wstring LowerW(std::wstring s) {
    for (auto& c : s) c = towlower(c);
    return s;
}

// A long-running browser finds a newer release without restart; TTL/back-off respected; never interrupts.
void TestAutoCheck() {
    Fixture f;
    f.root = TempDir(L"engine-autocheck");
    MockResponse nf;
    nf.status = UpdateCore::HttpResult::NotFound;
    f.transport->Set(UpdateCore::ManifestUrl(Repo), nf);
    UpdateCore::Engine e;
    g_engine = &e;
    e.Start(f.Config());
    CHECK(PumpUntil([&] { return e.LastCheckResult() == UpdateCore::Engine::LastCheck::UpToDate; }, 3000));
    Pump(150);  // let the startup worker finish
    const int afterStartup = f.transport->Requests();
    // Next due in 6 h: the host arms ONE timer for ~6 h (never a high-rate wake).
    DWORD delay = e.AutoCheckDelayMs();
    CHECK(delay == 6u * 3600u * 1000u);
    f.now += 3600;
    CHECK(e.AutoCheckDelayMs() == 5u * 3600u * 1000u);
    CHECK(!e.AutoCheck());  // TTL: no request
    Pump(50);
    CHECK(f.transport->Requests() == afterStartup);
    f.now += 4 * 3600 + 3590;  // 10 s before due: floor 15 min, still no request
    CHECK(e.AutoCheckDelayMs() == UpdateCore::Engine::AutoCheckMinMs);
    CHECK(!e.AutoCheck());
    // A release appears while the browser keeps running; once due, the quiet check finds it.
    f.Publish("0.3.2", 2 * 1024 * 1024, 20, 64 * 1024);
    f.now += 11;
    CHECK(e.AutoCheck());
    CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Available; }, 3000));
    CHECK(e.View().version == L"0.3.2");
    // Visible offer: no further automatic checks, delay stays bounded (>= 15 min).
    f.now += 7 * 3600;
    int before = f.transport->Requests();
    CHECK(!e.AutoCheck());
    CHECK(e.AutoCheckDelayMs() >= UpdateCore::Engine::AutoCheckMinMs && e.AutoCheckDelayMs() <= UpdateCore::Engine::AutoCheckMaxMs);
    // Download in progress is never cancelled by an auto check.
    CHECK(e.Perform(Action::Download) == UpdateCore::ActionResult::Handled);
    CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Downloading; }, 3000));
    CHECK(!e.AutoCheck());
    CHECK(PhaseOf(e) == Phase::Downloading);
    CHECK(PumpUntil([&] { return PhaseOf(e) == Phase::Ready; }, 10000));
    CHECK(!e.AutoCheck());  // Ready offer is not replaced by a check
    CHECK(f.transport->Requests() == before + 1);  // only the package download
    // Dismissed version stays quiet on later automatic checks.
    CHECK(e.Perform(Action::Dismiss) == UpdateCore::ActionResult::Handled);
    e.Shutdown();
    CHECK(!e.AutoCheck() && e.AutoCheckDelayMs() == 0);  // shut down: nothing to arm
    {
        UpdateCore::Engine e2;
        g_engine = &e2;
        auto c = f.Config();
        c.automaticCheck = false;  // no startup check; only AutoCheck
        e2.Start(c);
        CHECK(e2.AutoCheckDelayMs() == 0);  // automatic checks disabled => host arms nothing
        e2.Shutdown();
    }
    {  // offline auto check: quiet, back-off 1 h, then retried only when due
        Fixture g;
        g.root = TempDir(L"engine-autocheck-offline");
        UpdateCore::Engine e3;
        g_engine = &e3;
        e3.Start(g.Config());
        CHECK(PumpUntil([&] { return e3.LastCheckResult() == UpdateCore::Engine::LastCheck::Failed; }, 3000));
        Pump(50);
        CHECK(PhaseOf(e3) == Phase::Hidden);
        CHECK(e3.AutoCheckDelayMs() == 3600u * 1000u);
        int r0 = g.transport->Requests();
        g.now += 1800;
        CHECK(!e3.AutoCheck());
        g.now += 1801;
        CHECK(e3.AutoCheck());
        CHECK(PumpUntil([&] { return g.transport->Requests() == r0 + 1; }, 3000));
        Pump(100);
        CHECK(PhaseOf(e3) == Phase::Hidden);
        CHECK(e3.AutoCheckDelayMs() == 2u * 3600u * 1000u);
        CHECK(!e3.AutoCheck());  // busy guard / not due: no overlapping check
        // Clean close while an automatic check is in flight (slow transport) is bounded.
        MockResponse slow;
        slow.body = std::string(1024, 'x');
        slow.chunk = 1;
        slow.chunkDelayMs = 50;
        g.transport->Set(UpdateCore::ManifestUrl(Repo), slow);
        g.now += 3 * 3600;
        CHECK(e3.AutoCheck());
        Pump(100);
        ULONGLONG t0 = GetTickCount64();
        e3.Shutdown();
        CHECK(GetTickCount64() - t0 < 2000);
    }
}

// Legacy portable exe (old shortcut target) hands over to the installed current version only when safe.
void TestPortableForward() {
    std::wstring base = TempDir(L"forward"), root = base + L"\\root", portable = base + L"\\portable";
    std::wstring legacy = portable + L"\\browser-main-78f5340.exe";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(portable.c_str(), nullptr);
    WriteText(legacy, "MZ legacy");
    auto target = root + L"\\versions\\0.3.6\\browser.exe";
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());  // nothing installed
    CreateDirectoryW((root + L"\\versions").c_str(), nullptr);
    CreateDirectoryW((root + L"\\versions\\0.3.6").c_str(), nullptr);
    CreateDirectoryW((root + L"\\data").c_str(), nullptr);
    WriteText(target, "MZ new");
    WriteText(root + L"\\current", "0.3.6\n");
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());  // no migration record
    WriteText(root + L"\\data\\migrated-from.txt", UP::Narrow(base + L"\\other") + "\n");
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());  // migrated from another folder
    WriteText(root + L"\\data\\migrated-from.txt", UP::Narrow(portable) + "\n");
    CHECK(UpdateCore::PortableForwardTarget(legacy, root) == FullPath(target));
    CHECK(LowerW(UpdateCore::PortableForwardTarget(base + L"\\PORTABLE\\Browser-Main-78f5340.exe", root)) == LowerW(FullPath(target)));
    // Installed kind never forwards (no loop), neither does anything inside the root (launcher).
    CHECK(UpdateCore::PortableForwardTarget(target, root).empty());
    CHECK(UpdateCore::PortableForwardTarget(root + L"\\browser.exe", root).empty());
    // Bogus pointers are rejected.
    for (const char* bad : {"..\\..\\evil\n", "0.3.9\n", "0.0.0\n", "x\n", "C:\\Windows\n", ""}) {
        WriteText(root + L"\\current", bad);
        CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());
    }
    DeleteFileW((root + L"\\current").c_str());
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());  // rolled-back first install deletes current
    WriteText(root + L"\\current", "0.3.6\n");
    // A failed install of that version (rollback) is never forwarded to; another version's failure is irrelevant.
    CreateDirectoryW((root + L"\\updates").c_str(), nullptr);
    WriteText(root + L"\\updates\\last-result", "browser-result 1\nstatus failed\nversion 0.3.6\nreason no-startup-ack\nreported 0\n");
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());
    WriteText(root + L"\\updates\\last-result", "browser-result 1\nstatus failed\nversion 0.3.7\nreason x\nreported 1\n");
    CHECK(UpdateCore::PortableForwardTarget(legacy, root) == FullPath(target));
    WriteText(root + L"\\updates\\last-result", "browser-result 1\nstatus installed\nversion 0.3.6\nreason ack\nreported 1\n");
    CHECK(UpdateCore::PortableForwardTarget(legacy, root) == FullPath(target));
    // Target must be a regular file in a real directory.
    DeleteFileW(target.c_str());
    CreateDirectoryW(target.c_str(), nullptr);
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());
    RemoveDirectoryW(target.c_str());
    CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());  // missing exe
    // Reparse points (junctions) anywhere on the path are refused.
    std::wstring elsewhere = base + L"\\elsewhere";
    CreateDirectoryW(elsewhere.c_str(), nullptr);
    WriteText(elsewhere + L"\\browser.exe", "MZ planted");
    RemoveDirectoryW((root + L"\\versions\\0.3.6").c_str());
    if (CreateJunction(root + L"\\versions\\0.3.6", elsewhere)) {
        CHECK(UpdateCore::PortableForwardTarget(legacy, root).empty());
        RemoveDirectoryW((root + L"\\versions\\0.3.6").c_str());
    } else {
        CHECK(false);  // junction creation must work for this guard to be tested
    }
    std::string legacyBody;
    CHECK(UP::ReadSmallFile(legacy, 64, legacyBody) && legacyBody == "MZ legacy");  // pure: legacy untouched
}
// A file just written into a directory and still open elsewhere (on-access scanner, indexer) makes NTFS refuse to
// rename the directory with ERROR_ACCESS_DENIED. UP::MoveDirectory retries that, bounded, and never replaces.
void TestMoveDirectoryTransientHold() {
    std::wstring s = TempDir(L"move-directory"), from = s + L"\\work", to = s + L"\\data";
    CreateDirectoryW(from.c_str(), nullptr);
    CHECK(WriteText(from + L"\\session", "payload"));
    auto hold = [](const std::wstring& file) {  // scanner-like handle: read/write sharing, no FILE_SHARE_DELETE
        return CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    };
    HANDLE scanner = hold(from + L"\\session");
    CHECK(scanner != INVALID_HANDLE_VALUE);
    // Precondition (the production failure): a plain rename is refused while the file is held.
    CHECK(!MoveFileExW(from.c_str(), to.c_str(), 0) && GetLastError() == ERROR_ACCESS_DENIED);
    // Held for good: bounded failure, nothing moved.
    int attempts = 0;
    ULONGLONG start = GetTickCount64();
    CHECK(!UP::MoveDirectory(from, to, 600, &attempts));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(GetTickCount64() - start >= 500 && GetTickCount64() - start < 5000 && attempts > 1);
    CHECK(UP::PathExists(from + L"\\session") && !UP::PathExists(to));
    // Released while retrying: the rename completes with the content intact.
    HANDLE release = CreateThread(nullptr, 0, [](LPVOID h) -> DWORD { Sleep(700); CloseHandle(HANDLE(h)); return 0; }, scanner, 0, nullptr);
    CHECK(release != nullptr);
    attempts = 0;
    CHECK(UP::MoveDirectory(from, to, 10000, &attempts));
    CHECK(attempts > 1);  // it really had to wait for the holder
    WaitForSingleObject(release, INFINITE);
    CloseHandle(release);
    CHECK(!UP::PathExists(from) && ReadText(to + L"\\session") == "payload");
    // Never replaces: an existing destination fails at once and both trees stay as they were.
    CreateDirectoryW(from.c_str(), nullptr);
    CHECK(WriteText(from + L"\\session", "other"));
    start = GetTickCount64();
    CHECK(!UP::MoveDirectory(from, to, 10000, &attempts));
    CHECK(GetTickCount64() - start < 2000 && attempts == 1);
    CHECK(ReadText(to + L"\\session") == "payload" && ReadText(from + L"\\session") == "other");
    // Missing source: not transient, fails at once.
    CHECK(!UP::MoveDirectory(s + L"\\missing", s + L"\\elsewhere", 10000, &attempts) && attempts == 1);
}
}  // namespace

int wmain() {
    TestVersions();
    TestManifest();
    TestUrls();
    TestPaths();
    TestPackage();
    TestDataPaths();
    TestNoteStartup();
    TestEngineStartupQuiet();
    TestEngineAvailableAndNotified();
    TestEngineDownload();
    TestEngineCancelAndFailures();
    TestAutoCheck();
    TestPortableForward();
    TestMoveDirectoryTransientHold();
    g_engine = nullptr;
    return Report("update_core_test");
}
