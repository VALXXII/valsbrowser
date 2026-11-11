// WebView2 bootstrapper pipeline: private directory, download handling, trust decision, launch,
// cleanup and the single-flight worker. The network is FAKE (in-memory Fetch). Trust runs for real
// on local signed files; the launched program is fake_setup.exe (records its command line, installs
// nothing). The Microsoft installer is never executed here.
//   install_test.exe <work dir> <fake_setup.exe> [<real MicrosoftEdgeWebview2Setup.exe to verify only>]
#include "../../src/webview2-install.h"

#include <aclapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace WR = WebView2Runtime;
namespace WI = WebView2Install;
using O = WR::InstallReport::Outcome;

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string ReadAll(const std::wstring& path) { std::ifstream in(std::filesystem::path(path), std::ios::binary); std::stringstream s; s << in.rdbuf(); return s.str(); }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static int Entries(const std::wstring& dir) {
    WIN32_FIND_DATAW d; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &d); if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0; do { if (wcscmp(d.cFileName, L".") && wcscmp(d.cFileName, L"..")) ++n; } while (FindNextFileW(h, &d)); FindClose(h); return n;
}

class FakeFetch : public WI::Fetch {
public:
    WI::FetchResult::Status status = WI::FetchResult::Ok; DWORD code = 200;
    std::string body; DWORD chunkDelayMs = 0; int calls = 0; std::wstring lastUrl;
    std::atomic<bool> aborted{false};
    WI::FetchResult Get(const std::wstring& url, std::uint64_t maxBytes, const std::function<bool(const void*, std::size_t)>& sink,
                        const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>& cancel) override {
        ++calls; lastUrl = url;
        WI::FetchResult r;
        if (status != WI::FetchResult::Ok) { r.status = status; r.code = code; return r; }
        if (body.size() > maxBytes) { r.status = WI::FetchResult::TooLarge; return r; }
        for (size_t at = 0; at < body.size(); at += 4096) {
            if (chunkDelayMs) Sleep(chunkDelayMs);
            if (cancel.load() || aborted.load()) { r.status = WI::FetchResult::Cancelled; return r; }
            size_t n = std::min<size_t>(4096, body.size() - at);
            if (!sink(body.data() + at, n)) { r.status = WI::FetchResult::Network; return r; }
            progress(at + n, body.size());
        }
        r.status = WI::FetchResult::Ok; r.code = 200; return r;
    }
    void Abort() override { aborted = true; }
};

struct Run {
    std::shared_ptr<FakeFetch> fetch = std::make_shared<FakeFetch>();
    WI::TrustResult trust; int verifies = 0; bool lockedDuringVerify = false;
    WI::LaunchResult launch; int launches = 0; std::wstring launchedExe, launchedDir; bool realLaunch = false;
    std::wstring parent;
    std::vector<WR::Phase> phases;
    WI::Effects Effects() {
        WI::Effects e; e.fetch = fetch; e.tempParent = parent;
        e.verify = [this](HANDLE h, const std::wstring& path) {
            ++verifies;
            // Nobody can write or delete the file while the browser verifies it.
            HANDLE w = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            lockedDuringVerify = w == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION && h != INVALID_HANDLE_VALUE;
            if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
            return trust;
        };
        e.launch = [this](const std::wstring& exe, const std::wstring& dir, HANDLE cancel, DWORD timeout) {
            ++launches; launchedExe = exe; launchedDir = dir;
            if (realLaunch) return WI::RunBootstrapper(exe, dir, cancel, timeout);
            return launch;
        };
        return e;
    }
    WR::InstallReport Go() {
        WI::Effects e = Effects();
        std::atomic<bool> cancel{false};
        return WI::RunInstall(e, cancel, nullptr, [this](WR::Phase p, std::uint64_t, std::uint64_t) { phases.push_back(p); });
    }
};

static void PrivateDirAcl(const std::wstring& work) {
    WI::PrivateDir a, b;
    CHECK(a.Create(work) == ERROR_SUCCESS && b.Create(work) == ERROR_SUCCESS);
    CHECK(a.Path() != b.Path() && a.Path().find(L"browser-wv2-") != std::wstring::npos && a.Path().size() == work.size() + 1 + 12 + 24);
    PACL dacl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
    CHECK(GetNamedSecurityInfoW(a.Path().c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
    if (sd) GetSecurityDescriptorControl(sd, &control, &revision);
    CHECK((control & SE_DACL_PROTECTED) != 0);   // no inherited ACEs from %TEMP%
    CHECK(dacl && dacl->AceCount == 2);           // current user + SYSTEM
    if (sd) LocalFree(sd);
    const std::wstring path = a.Path();
    CHECK(a.Remove() && !Exists(path));
}

static std::string FakeSetupBytes(const std::wstring& fakeSetup) { return ReadAll(fakeSetup); }

static void Pipeline(const std::wstring& work, const std::wstring& fakeSetup) {
    // Success with the real process launch: command line, no console, file locked, cleanup.
    {
        Run r; r.parent = work; r.fetch->body = FakeSetupBytes(fakeSetup); r.trust.status = WI::TrustResult::Ok; r.trust.signer = L"Microsoft Corporation";
        r.realLaunch = true;
        const std::wstring out = work + L"\\fake-setup-out.txt";
        DeleteFileW(out.c_str());
        SetEnvironmentVariableW(L"FAKE_SETUP_OUT", out.c_str()); SetEnvironmentVariableW(L"FAKE_SETUP_EXIT", L"0");
        auto rep = r.Go();
        CHECK(rep.outcome == O::Finished && rep.code == 0 && !rep.installerRunning && !rep.cleanupIncomplete && rep.signer == L"Microsoft Corporation");
        CHECK(r.fetch->lastUrl == WI::BootstrapperUrl() && r.verifies == 1 && r.lockedDuringVerify && r.launches == 1);
        const std::string got = ReadAll(out);
        CHECK(got.find("MicrosoftEdgeWebview2Setup.exe\" /silent /install") != std::string::npos);
        CHECK(got.find("console=0") != std::string::npos && got.find("selfWritable=0") != std::string::npos);
        CHECK(!Exists(r.launchedDir) && Entries(work) == 1);   // only fake-setup-out.txt remains
        CHECK(r.phases.size() >= 3 && r.phases.back() == WR::Phase::Installing && r.phases[r.phases.size() - 2] == WR::Phase::Verifying);
        // Non-zero exit is reported with its code.
        SetEnvironmentVariableW(L"FAKE_SETUP_EXIT", L"0x80040c01");
        Run n; n.parent = work; n.fetch->body = r.fetch->body; n.trust.status = WI::TrustResult::Ok; n.realLaunch = true;
        rep = n.Go();
        CHECK(rep.outcome == O::Finished && rep.code == 0x80040c01u);
        // Timeout: reported, the installer is NOT killed and its file is left for it.
        SetEnvironmentVariableW(L"FAKE_SETUP_EXIT", L"0"); SetEnvironmentVariableW(L"FAKE_SETUP_SLEEP_MS", L"1500");
        Run t; t.parent = work; t.fetch->body = r.fetch->body; t.trust.status = WI::TrustResult::Ok; t.realLaunch = true;
        WI::Effects e = t.Effects(); e.timeoutMs = 100;
        std::atomic<bool> cancel{false};
        rep = WI::RunInstall(e, cancel, nullptr, [](WR::Phase, std::uint64_t, std::uint64_t) {});
        CHECK(rep.outcome == O::Timeout && rep.installerRunning && Exists(t.launchedDir));
        Sleep(2000);
        DeleteFileW((t.launchedDir + L"\\" + WI::FileName()).c_str()); RemoveDirectoryW(t.launchedDir.c_str());
        SetEnvironmentVariableW(L"FAKE_SETUP_SLEEP_MS", nullptr);
        DeleteFileW(out.c_str());
    }
    // Every refusal stops before launch and removes the download.
    struct Case { const char* name; WI::FetchResult::Status fetch; DWORD code; WI::TrustResult::Status trust; O expected; bool verify; };
    const Case cases[] = {
        {"offline", WI::FetchResult::Network, ERROR_WINHTTP_CANNOT_CONNECT, WI::TrustResult::Ok, O::Network, false},
        {"tls", WI::FetchResult::Network, ERROR_WINHTTP_SECURE_FAILURE, WI::TrustResult::Ok, O::Network, false},
        {"http", WI::FetchResult::Http, 503, WI::TrustResult::Ok, O::Http, false},
        {"redirect", WI::FetchResult::Rejected, 0, WI::TrustResult::Ok, O::Rejected, false},
        {"too large", WI::FetchResult::TooLarge, 0, WI::TrustResult::Ok, O::TooLarge, false},
        {"untrusted", WI::FetchResult::Ok, 0, WI::TrustResult::Untrusted, O::Trust, true},
        {"publisher", WI::FetchResult::Ok, 0, WI::TrustResult::Publisher, O::Publisher, true},
        {"product", WI::FetchResult::Ok, 0, WI::TrustResult::Unexpected, O::Unexpected, true},
    };
    for (auto& c : cases) {
        Run r; r.parent = work; r.fetch->status = c.fetch; r.fetch->code = c.code; r.fetch->body = "MZ fake"; r.trust.status = c.trust; r.trust.code = 0x800B0100;
        auto rep = r.Go();
        const bool ok = rep.outcome == c.expected && r.launches == 0 && (r.verifies == 1) == c.verify && Entries(work) == 0;
        if (!ok) std::printf("case %s: outcome=%ls launches=%d verifies=%d entries=%d\n", c.name, WR::OutcomeName(rep.outcome), r.launches, r.verifies, Entries(work));
        CHECK(ok);
        CHECK(!WR::InstallMessage(rep).empty());
    }
    // Launch failures: refused elevation, missing program.
    {
        Run r; r.parent = work; r.fetch->body = "MZ"; r.trust.status = WI::TrustResult::Ok; r.launch.status = WI::LaunchResult::Elevation; r.launch.code = ERROR_ELEVATION_REQUIRED;
        auto rep = r.Go();
        CHECK(rep.outcome == O::Elevation && Entries(work) == 0);
        r.launch.status = WI::LaunchResult::Failed; r.launch.code = ERROR_ACCESS_DENIED;
        rep = r.Go();
        CHECK(rep.outcome == O::Launch && rep.code == ERROR_ACCESS_DENIED);
    }
    // Storage: unusable parent folder.
    {
        Run r; r.parent = work + L"\\missing\\deeper"; r.fetch->body = "MZ";
        auto rep = r.Go();
        CHECK(rep.outcome == O::Storage && r.fetch->calls == 0);
    }
}

static void Worker(const std::wstring& work) {
    // Single flight, cancel during the download, events drained in order.
    Run r; r.parent = work; r.fetch->body = std::string(400 * 1024, 'x'); r.fetch->chunkDelayMs = 5; r.trust.status = WI::TrustResult::Ok;
    WI::Installer installer;
    std::atomic<int> notified{0};
    CHECK(installer.Start(r.Effects(), [&] { ++notified; }));
    CHECK(installer.Running() && !installer.Start(r.Effects(), [] {}));
    Sleep(60);
    installer.Cancel();
    for (int i = 0; i < 300 && installer.Running(); ++i) Sleep(10);
    auto events = installer.Take();
    CHECK(!events.empty() && events.back().done && events.back().report.outcome == O::Cancelled && notified.load() >= 1);
    CHECK(r.launches == 0 && Entries(work) == 0);
    // Complete run.
    Run s; s.parent = work; s.fetch->body = "MZ small"; s.trust.status = WI::TrustResult::Ok; s.launch.status = WI::LaunchResult::Exited;
    CHECK(installer.Start(s.Effects(), [] {}));
    for (int i = 0; i < 300 && installer.Running(); ++i) Sleep(10);
    events = installer.Take();
    CHECK(!events.empty() && events.back().done && events.back().report.outcome == O::Finished);
    // Shutdown while the (fake) installer waits: bounded, no notification afterwards.
    Run t; t.parent = work; t.fetch->body = "MZ"; t.trust.status = WI::TrustResult::Ok;
    t.launch.status = WI::LaunchResult::Exited;
    WI::Effects e = t.Effects();
    e.launch = [](const std::wstring&, const std::wstring&, HANDLE cancel, DWORD) {
        WI::LaunchResult l; l.status = WaitForSingleObject(cancel, 5000) == WAIT_OBJECT_0 ? WI::LaunchResult::Cancelled : WI::LaunchResult::Exited; return l;
    };
    std::atomic<int> late{0};
    CHECK(installer.Start(e, [&] { ++late; }));
    Sleep(100);
    const ULONGLONG before = GetTickCount64();
    const int seen = late.load();
    installer.Shutdown(2000);
    CHECK(GetTickCount64() - before < 2000 && !installer.Running());
    Sleep(50);
    CHECK(late.load() == seen);   // nothing posted after Shutdown
    // « Cancelled » with the installer still running leaves its folder to it (cannot delete a running image).
    CHECK(Entries(work) == 1);
    WIN32_FIND_DATAW d; HANDLE h = FindFirstFileW((work + L"\\browser-wv2-*").c_str(), &d);
    if (h != INVALID_HANDLE_VALUE) { const std::wstring dir = work + L"\\" + d.cFileName; FindClose(h); DeleteFileW((dir + L"\\" + WI::FileName()).c_str()); RemoveDirectoryW(dir.c_str()); }
    CHECK(Entries(work) == 0);
}

static void RealTrust(const std::wstring& work, const std::wstring& fakeSetup, const std::wstring& bootstrapper) {
    auto verify = [](const std::wstring& path, bool requireBootstrapper) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { WI::TrustResult r; r.code = GetLastError(); return r; }
        auto r = WI::VerifyMicrosoftSignature(h, path, requireBootstrapper);
        CloseHandle(h);
        return r;
    };
    // Microsoft-signed (embedded Authenticode) file from the WebView2 SDK: trusted publisher, but
    // not the bootstrapper — the installer path refuses it.
    const std::wstring loader = L"deps\\webview2\\build\\native\\x64\\WebView2Loader.dll";
    if (Exists(loader)) {
        auto r = verify(loader, false);
        std::printf("trust %ls: status=%d code=0x%08lX signer=%ls microsoftRootPolicy=%d\n", loader.c_str(), r.status, r.code, r.signer.c_str(), r.root);
        CHECK(r.status == WI::TrustResult::Ok && r.signer == L"Microsoft Corporation");
        CHECK(verify(loader, true).status == WI::TrustResult::Unexpected);
        // One flipped byte in the signed content: digest mismatch.
        const std::wstring tampered = work + L"\\tampered.dll";
        std::string bytes = ReadAll(loader); bytes[bytes.size() / 2] ^= 0x01;
        { std::ofstream out(std::filesystem::path(tampered), std::ios::binary); out << bytes; }
        auto t = verify(tampered, false);
        std::printf("trust tampered: status=%d code=0x%08lX\n", t.status, t.code);
        CHECK(t.status == WI::TrustResult::Untrusted && t.code != 0);
        DeleteFileW(tampered.c_str());
    } else std::printf("SKIP SDK loader trust (deps not present)\n");
    // Unsigned program.
    auto u = verify(fakeSetup, false);
    std::printf("trust unsigned fake_setup: status=%d code=0x%08lX\n", u.status, u.code);
    CHECK(u.status == WI::TrustResult::Untrusted && u.code == static_cast<DWORD>(TRUST_E_NOSIGNATURE));
    // Valid Authenticode from another publisher.
    wchar_t local[MAX_PATH]; GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    const std::wstring python = std::wstring(local) + L"\\Programs\\Python\\Python312\\python.exe";
    if (Exists(python)) {
        auto p = verify(python, true);
        std::printf("trust python.exe: status=%d signer=%ls\n", p.status, p.signer.c_str());
        CHECK(p.status == WI::TrustResult::Publisher && p.signer != L"Microsoft Corporation");
    } else std::printf("SKIP other-publisher trust (python.exe not found)\n");
    // The real bootstrapper, downloaded beforehand from the official link and NOT executed.
    if (!bootstrapper.empty()) {
        auto b = verify(bootstrapper, true);
        std::printf("trust real bootstrapper: status=%d code=0x%08lX signer=%ls microsoftRootPolicy=%s\n", b.status, b.code, b.signer.c_str(),
                    b.root == 0 ? "default" : b.root == 1 ? "application-root" : "none");
        CHECK(b.status == WI::TrustResult::Ok && b.signer == L"Microsoft Corporation");
    } else std::printf("SKIP real bootstrapper trust (no file given)\n");
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::printf("usage: install_test <work dir> <fake_setup.exe> [bootstrapper]\n"); return 2; }
    auto full = [](const wchar_t* p) { wchar_t b[MAX_PATH]; DWORD n = GetFullPathNameW(p, MAX_PATH, b, nullptr); return n && n < MAX_PATH ? std::wstring(b, n) : std::wstring(p); };
    const std::wstring work = full(argv[1]), fakeSetup = full(argv[2]), bootstrapper = argc > 3 ? full(argv[3]) : L"";
    CreateDirectoryW(work.c_str(), nullptr);
    PrivateDirAcl(work);
    Pipeline(work, fakeSetup);
    Worker(work);
    RealTrust(work, fakeSetup, bootstrapper);
    std::printf("install_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
