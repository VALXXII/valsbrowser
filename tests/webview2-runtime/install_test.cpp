// WebView2 bootstrapper pipeline: private directory, download handling, trust decision, launch,
// cancellation, per-user single installer across processes, cleanup and the worker.
// The network is FAKE (in-memory Fetch, or a stubbed WinHTTP table: no socket). Trust runs for
// real on local signed files. The launched program is fake_setup.exe (records its command line,
// installs nothing). The Microsoft installer is never executed here. The per-user claim uses an
// isolated marker file and mutex name, never the real %LOCALAPPDATA% one.
//   install_test.exe <work dir> <fake_setup.exe> [<real MicrosoftEdgeWebview2Setup.exe to verify only>]
//   install_test.exe --first-host <work dir> <fake_setup.exe>   (child: starts an install, then « quits »)
#include "../../src/webview2-install.h"

#include <aclapi.h>
#include <tlhelp32.h>

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
static int Entries(const std::wstring& dir, const wchar_t* pattern = L"browser-wv2-*") {
    WIN32_FIND_DATAW d; HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &d); if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0; do { if (wcscmp(d.cFileName, L".") && wcscmp(d.cFileName, L"..")) ++n; } while (FindNextFileW(h, &d)); FindClose(h); return n;
}
static WI::Coordination Coord(const std::wstring& work) {
    WI::Coordination c; c.marker = work + L"\\coord\\install.lock";
    c.lock = c.marker + L".mutex";
    return c;
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
    std::wstring parent; WI::Coordination coord;
    std::vector<WR::Phase> phases;
    explicit Run(const std::wstring& work) : parent(work), coord(Coord(work)) {}
    WI::Effects Effects() {
        WI::Effects e; e.fetch = fetch; e.tempParent = parent; e.coordination = coord;
        e.verify = [this](HANDLE h, const std::wstring& path) {
            ++verifies;
            HANDLE w = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            lockedDuringVerify = w == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION && h != INVALID_HANDLE_VALUE;
            if (w != INVALID_HANDLE_VALUE) CloseHandle(w);
            return trust;
        };
        e.launch = [this](const std::wstring& exe, const std::wstring& dir, HANDLE cancel, DWORD timeout, const WI::LaunchHooks& claim) {
            ++launches; launchedExe = exe; launchedDir = dir;
            if (realLaunch) return WI::RunBootstrapper(exe, dir, cancel, timeout, claim);
            return launch;
        };
        return e;
    }
    WR::InstallReport Go(DWORD timeoutMs = WI::InstallTimeoutMs, HANDLE cancelEvent = nullptr) {
        WI::Effects e = Effects(); e.timeoutMs = timeoutMs;
        std::atomic<bool> cancel{false};
        return WI::RunInstall(e, cancel, cancelEvent, [this](WR::Phase p, std::uint64_t, std::uint64_t) { phases.push_back(p); });
    }
};
static void SetupEnv(const std::wstring& out, const wchar_t* exit, const wchar_t* sleepMs) {
    if (!out.empty()) DeleteFileW(out.c_str());
    SetEnvironmentVariableW(L"FAKE_SETUP_OUT", out.empty() ? nullptr : out.c_str());
    SetEnvironmentVariableW(L"FAKE_SETUP_EXIT", exit); SetEnvironmentVariableW(L"FAKE_SETUP_SLEEP_MS", sleepMs);
}
static bool WaitGone(DWORD pid, DWORD ms) {
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid); if (!p) return true;
    const bool gone = WaitForSingleObject(p, ms) == WAIT_OBJECT_0; CloseHandle(p); return gone;
}

static void PrivateDirAcl(const std::wstring& work) {
    WI::PrivateDir a, b;
    CHECK(a.Create(work) == ERROR_SUCCESS && b.Create(work) == ERROR_SUCCESS);
    CHECK(a.Path() != b.Path() && WI::IsPrivateDirPath(a.Path()) && a.Path().size() == work.size() + 1 + 12 + 24);
    PACL dacl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
    CHECK(GetNamedSecurityInfoW(a.Path().c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
    if (sd) GetSecurityDescriptorControl(sd, &control, &revision);
    CHECK((control & SE_DACL_PROTECTED) != 0 && dacl && dacl->AceCount == 2);
    if (sd) LocalFree(sd);
    const std::wstring path = a.Path();
    CHECK(a.Remove() && !Exists(path));
    // The remover only ever touches a folder of this exact shape.
    CHECK(!WI::IsPrivateDirPath(work) && !WI::IsPrivateDirPath(work + L"\\browser-wv2-XYZ") && !WI::IsPrivateDirPath(work + L"\\browser-wv2-0123456789abcdef0123456G"));
    CHECK(!WI::RemovePrivateDir(work));
}

// P2-1: the reviewer's adversarial case and the expected contract, with the real launcher.
static void CancelRaces(const std::wstring& work, const std::wstring& fakeSetup) {
    const std::wstring setup = work + L"\\cancel-setup.exe", out = work + L"\\cancel-ran.txt";
    CHECK(CopyFileW(fakeSetup.c_str(), setup.c_str(), FALSE));
    // 1. Cancel already signalled: never created, never ran.
    SetupEnv(out, L"0", L"300");
    HANDLE cancel = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    auto r = WI::RunBootstrapper(setup, work, cancel, 5000);
    Sleep(800);
    std::printf("cancel before launch: status=%d ran=%d\n", int(r.status), Exists(out));
    CHECK(r.status == WI::LaunchResult::Cancelled && r.pid == 0 && !Exists(out));
    // 2. Cancel between creation (suspended) and resume — here at the claim step: terminated
    //    without having run a single instruction of its own.
    ResetEvent(cancel);
    DWORD claimedPid = 0;
    WI::LaunchHooks h2; h2.created = [&](DWORD pid, std::uint64_t, DWORD, DWORD&) { claimedPid = pid; SetEvent(cancel); return WI::Claim::Claimed; };
    r = WI::RunBootstrapper(setup, work, cancel, 5000, h2);
    Sleep(800);
    std::printf("cancel while suspended: status=%d pid=%lu ran=%d\n", int(r.status), r.pid, Exists(out));
    CHECK(r.status == WI::LaunchResult::Cancelled && claimedPid && r.pid == claimedPid && !Exists(out) && WaitGone(claimedPid, 1000));
    // 3. Claim refused (another installer): never resumed.
    ResetEvent(cancel);
    WI::LaunchHooks h3; h3.created = [&](DWORD, std::uint64_t, DWORD, DWORD& busy) { busy = 7; return WI::Claim::Busy; };
    r = WI::RunBootstrapper(setup, work, cancel, 5000, h3);
    Sleep(800);
    CHECK(r.status == WI::LaunchResult::Busy && r.busyPid == 7 && !Exists(out));
    // 4. Cancel while it runs: Detached (not « cancelled »), it is not killed, its identity
    //    (pid + creation time) stays verifiable and it finishes on its own.
    ResetEvent(cancel);
    SetupEnv(out, L"0", L"1200");
    WI::Marker seen;
    std::thread later([&] { Sleep(300); SetEvent(cancel); });
    WI::LaunchHooks h4; h4.created = [&](DWORD pid, std::uint64_t created, DWORD, DWORD&) { seen.pid = pid; seen.created = created; return WI::Claim::Claimed; };
    r = WI::RunBootstrapper(setup, work, cancel, 5000, h4);
    later.join();
    const bool aliveAfter = WI::MarkerAlive(seen);
    std::printf("cancel while running: status=%d alive=%d\n", int(r.status), aliveAfter);
    CHECK(r.status == WI::LaunchResult::Detached && aliveAfter && seen.created != 0);
    CHECK(WaitGone(seen.pid, 5000) && !WI::MarkerAlive(seen));
    Sleep(200);
    CHECK(Exists(out));   // it ran to its end although the browser stopped waiting
    WR::InstallReport rep; rep.outcome = O::Detached; rep.installerRunning = true;
    CHECK(WR::InstallMessage(rep).find(L"avant") == std::wstring::npos && WR::InstallMessage(rep).find(L"continue") != std::wstring::npos);
    CloseHandle(cancel);
    DeleteFileW(setup.c_str()); DeleteFileW(out.c_str());
}

static void Pipeline(const std::wstring& work, const std::wstring& fakeSetup) {
    const std::string setupBytes = ReadAll(fakeSetup);
    const std::wstring out = work + L"\\fake-setup-out.txt";
    // Success with the real launcher: command line, no console, file locked, claim released, cleanup.
    {
        Run r(work); r.fetch->body = setupBytes; r.trust.status = WI::TrustResult::Ok; r.trust.signer = L"Microsoft Corporation"; r.realLaunch = true;
        SetupEnv(out, L"0", nullptr);
        auto rep = r.Go();
        CHECK(rep.outcome == O::Finished && rep.code == 0 && !rep.installerRunning && !rep.cleanupIncomplete && rep.residue.empty() && rep.pid != 0);
        CHECK(r.fetch->lastUrl == WI::BootstrapperUrl() && r.verifies == 1 && r.lockedDuringVerify && r.launches == 1);
        const std::string got = ReadAll(out);
        CHECK(got.find("MicrosoftEdgeWebview2Setup.exe\" /silent /install") != std::string::npos);
        CHECK(got.find("console=0") != std::string::npos && got.find("selfWritable=0") != std::string::npos);
        CHECK(!Exists(r.launchedDir) && Entries(work) == 0 && !Exists(r.coord.marker));
        CHECK(r.phases.size() >= 3 && r.phases.back() == WR::Phase::Installing && r.phases[r.phases.size() - 2] == WR::Phase::Verifying);
        SetupEnv(out, L"0x80040c01", nullptr);
        Run n(work); n.fetch->body = setupBytes; n.trust.status = WI::TrustResult::Ok; n.realLaunch = true;
        rep = n.Go();
        CHECK(rep.outcome == O::Finished && rep.code == 0x80040c01u && !Exists(n.coord.marker));
    }
    // Timeout: reported, never killed, the claim stays while it runs: a second request is refused
    // BEFORE downloading; once it exited, the next request cleans its folder and proceeds.
    {
        SetupEnv(out, L"0", L"1500");
        Run t(work); t.fetch->body = setupBytes; t.trust.status = WI::TrustResult::Ok; t.realLaunch = true;
        auto rep = t.Go(100);
        CHECK(rep.outcome == O::Timeout && rep.installerRunning && rep.pid && Exists(t.launchedDir) && Exists(t.coord.marker));
        CHECK(WI::ActiveInstall(t.coord) == rep.pid);
        Run second(work); second.fetch->body = setupBytes; second.trust.status = WI::TrustResult::Ok; second.realLaunch = true;
        auto busy = second.Go();
        CHECK(busy.outcome == O::Busy && busy.code == rep.pid && second.fetch->calls == 0 && second.launches == 0);
        CHECK(WaitGone(rep.pid, 5000));
        CHECK(WI::ActiveInstall(t.coord) == 0 && !Exists(t.coord.marker) && !Exists(t.launchedDir));   // stale: its recorded folder removed
        SetupEnv(out, L"0", nullptr);
        Run third(work); third.fetch->body = setupBytes; third.trust.status = WI::TrustResult::Ok; third.realLaunch = true;
        CHECK(third.Go().outcome == O::Finished && Entries(work) == 0);
    }
    // Every refusal stops before launch and removes the download.
    struct Case { const char* name; WI::FetchResult::Status fetch; DWORD code; WI::TrustResult::Status trust; O expected; bool verify; };
    const Case cases[] = {
        {"offline", WI::FetchResult::Network, ERROR_WINHTTP_CANNOT_CONNECT, WI::TrustResult::Ok, O::Network, false},
        {"tls", WI::FetchResult::Network, ERROR_WINHTTP_SECURE_FAILURE, WI::TrustResult::Ok, O::Network, false},
        {"http", WI::FetchResult::Http, 503, WI::TrustResult::Ok, O::Http, false},
        {"redirect", WI::FetchResult::Rejected, 0, WI::TrustResult::Ok, O::Rejected, false},
        {"too large", WI::FetchResult::TooLarge, 0, WI::TrustResult::Ok, O::TooLarge, false},
        {"deadline", WI::FetchResult::TimedOut, ERROR_TIMEOUT, WI::TrustResult::Ok, O::DownloadTimeout, false},
        {"untrusted", WI::FetchResult::Ok, 0, WI::TrustResult::Untrusted, O::Trust, true},
        {"publisher", WI::FetchResult::Ok, 0, WI::TrustResult::Publisher, O::Publisher, true},
        {"product", WI::FetchResult::Ok, 0, WI::TrustResult::Unexpected, O::Unexpected, true},
    };
    for (auto& c : cases) {
        Run r(work); r.fetch->status = c.fetch; r.fetch->code = c.code; r.fetch->body = "MZ fake"; r.trust.status = c.trust; r.trust.code = 0x800B0100;
        auto rep = r.Go();
        const bool ok = rep.outcome == c.expected && r.launches == 0 && (r.verifies == 1) == c.verify && Entries(work) == 0;
        if (!ok) std::printf("case %s: outcome=%ls launches=%d verifies=%d entries=%d\n", c.name, WR::OutcomeName(rep.outcome), r.launches, r.verifies, Entries(work));
        CHECK(ok);
        CHECK(!WR::InstallMessage(rep).empty());
    }
    {
        Run r(work); r.fetch->body = "MZ"; r.trust.status = WI::TrustResult::Ok; r.launch.status = WI::LaunchResult::Elevation; r.launch.code = ERROR_ELEVATION_REQUIRED;
        CHECK(r.Go().outcome == O::Elevation && Entries(work) == 0);
        r.launch.status = WI::LaunchResult::Failed; r.launch.code = ERROR_ACCESS_DENIED;
        auto rep = r.Go();
        CHECK(rep.outcome == O::Launch && rep.code == ERROR_ACCESS_DENIED);
    }
    {
        Run r(work); r.parent = work + L"\\missing\\deeper"; r.fetch->body = "MZ";
        auto rep = r.Go();
        CHECK(rep.outcome == O::Storage && r.fetch->calls == 0);
    }
    // No claim storage: never launched (fail closed).
    {
        SetupEnv(out, L"0", nullptr);
        Run r(work); r.coord.marker = work + L"\\no\\such\\dir\\x\\install.lock"; r.fetch->body = setupBytes; r.trust.status = WI::TrustResult::Ok; r.realLaunch = true;
        CreateFileW((work + L"\\no").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);   // a FILE named "no": the marker folder cannot exist
        auto rep = r.Go();
        Sleep(300);
        CHECK(rep.outcome == O::Launch && rep.code == ERROR_LOCK_VIOLATION && !Exists(out) && Entries(work) == 0);
        DeleteFileW((work + L"\\no").c_str());
    }
    DeleteFileW(out.c_str());
}

static void Markers(const std::wstring& work) {
    const auto c = Coord(work + L"\\markers");
    CreateDirectoryW((work + L"\\markers").c_str(), nullptr);
    CreateDirectoryW((work + L"\\markers\\coord").c_str(), nullptr);
    // Garbage: not a claim, never blocks.
    { std::ofstream(std::filesystem::path(c.marker)) << "pid=abc\ncreated=\n"; }
    CHECK(WI::ActiveInstall(c) == 0 && !Exists(c.marker));
    // A live PID with another creation time (PID reuse): stale, and nothing is killed.
    HANDLE self = GetCurrentProcess();
    WI::Marker m; m.pid = GetCurrentProcessId(); m.created = WI::ProcessCreated(self) + 1;
    CHECK(WI::WriteMarker(c.marker, m) && WI::ActiveInstall(c) == 0 && !Exists(c.marker));
    // Exact identity of a live process (no host recorded): busy, never resumed nor killed.
    m.created = WI::ProcessCreated(self);
    CHECK(WI::WriteMarker(c.marker, m) && WI::ActiveInstall(c) == m.pid);
    WI::Marker other; other.host = 1; other.hostCreated = 2; DWORD busy = 0;
    CHECK(WI::ClaimInstall(c, other, busy) == WI::Claim::Busy && busy == m.pid);
    DeleteFileW(c.marker.c_str());
    // A record whose host is alive (here: this process, seen from a different claimant) is busy.
    WI::Marker mine = WI::SelfHost();
    CHECK(WI::ClaimInstall(c, mine, busy) == WI::Claim::Claimed);
    CHECK(WI::ClaimInstall(c, other, busy) == WI::Claim::Busy && busy == mine.host);
    CHECK(WI::ClaimInstall(c, mine, busy) == WI::Claim::Claimed);   // the same host re-claims its own pid-less record
    CHECK(!WI::UpdateInstall(c, other));                            // only the owner updates it
    WI::ReleaseInstall(c, other);   // not ours: kept
    CHECK(Exists(c.marker));
    WI::ReleaseInstall(c, mine);
    CHECK(!Exists(c.marker));
    // The lock is a file opened exclusively: held by one claimant, others wait then give up (fail closed).
    {
        HANDLE held = CreateFileW(c.lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr);
        CHECK(held != INVALID_HANDLE_VALUE);
        const ULONGLONG t0 = GetTickCount64();
        CHECK(WI::ClaimInstall(c, mine, busy) == WI::Claim::Failed && GetTickCount64() - t0 >= 4500);
        CloseHandle(held);
        CHECK(WI::ClaimInstall(c, mine, busy) == WI::Claim::Claimed);
        WI::ReleaseInstall(c, mine);
    }
    // A stale marker's recorded folder is removed only if it has the private-folder shape.
    const std::wstring foreign = work + L"\\markers\\keep-me";
    CreateDirectoryW(foreign.c_str(), nullptr);
    WI::Marker dead; dead.pid = 0xFFFFFFF0; dead.created = 1; dead.dir = foreign;
    CHECK(WI::WriteMarker(c.marker, dead) && WI::ActiveInstall(c) == 0 && Exists(foreign));
    RemoveDirectoryW(foreign.c_str());
}

// ---------------------------------------------------------------- host crash around the launch (R2-1)
// A child « host » runs the real RunInstall + RunBootstrapper with the real per-user record, and is
// killed (TerminateProcess on itself, like a crash or « Fin de tâche ») at one barrier:
//   verify      before any claim (inside the trust step)
//   claimed     record written (host only), before CreateProcess
//   created     setup created SUSPENDED, before its PID is recorded
//   recorded    PID/tid recorded, before the last cancel check and ResumeThread  (the reviewer's R2-1)
//   resumed     right after ResumeThread, before `resumed=1` is recorded
//   resumed-rec after `resumed=1` is recorded
// The parent then plays the next browser start: it must never be blocked by a setup nobody will
// resume, never kill a setup, and never start a second one while one lives.
static int CrashHost(const std::wstring& work, const std::wstring& fakeSetup, const std::wstring& point) {
    SetupEnv(work + L"\\crash-ran.txt", L"0", L"1500");
    Run r(work); r.fetch->body = ReadAll(fakeSetup); r.trust.status = WI::TrustResult::Ok;
    WI::Effects e = r.Effects();
    auto die = [&](const wchar_t* at) { if (point == at) { std::printf("crash-host: dying at %ls\n", at); std::fflush(stdout); TerminateProcess(GetCurrentProcess(), 99); } };
    e.verify = [&](HANDLE, const std::wstring&) { die(L"verify"); WI::TrustResult t; t.status = WI::TrustResult::Ok; return t; };
    e.launch = [&](const std::wstring& exe, const std::wstring& dir, HANDLE cancel, DWORD timeout, const WI::LaunchHooks& hooks) {
        die(L"claimed");
        WI::LaunchHooks wrapped;
        wrapped.created = [&](DWORD pid, std::uint64_t created, DWORD tid, DWORD& busy) {
            die(L"created");
            const WI::Claim c = hooks.created(pid, created, tid, busy);
            die(L"recorded");
            return c;
        };
        wrapped.resumed = [&] { die(L"resumed"); hooks.resumed(); die(L"resumed-rec"); };
        return WI::RunBootstrapper(exe, dir, cancel, timeout, wrapped);
    };
    std::atomic<bool> cancel{false};
    WI::RunInstall(e, cancel, nullptr, [](WR::Phase, std::uint64_t, std::uint64_t) {});
    return 1;   // not reached
}
static DWORD SetupProcessOf(const std::wstring& work) {   // the fixture's own fake setup copy, if alive
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W e{}; e.dwSize = sizeof(e); DWORD pid = 0;
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (WI::Lower(e.szExeFile) != WI::Lower(WI::FileName())) continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID); if (!p) continue;
        wchar_t image[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(p, 0, image, &n) && WI::Lower(std::wstring(image, n)).find(WI::Lower(work) + L"\\browser-wv2-") == 0) pid = e.th32ProcessID;
        CloseHandle(p);
    }
    CloseHandle(snap); return pid;
}
static void CrashBarriers(const std::wstring& work, const std::wstring& self, const std::wstring& fakeSetup) {
    const auto c = Coord(work);
    const std::wstring ran = work + L"\\crash-ran.txt";
    for (const wchar_t* point : {L"verify", L"claimed", L"created", L"recorded", L"resumed", L"resumed-rec"}) {
        DeleteFileW(ran.c_str());
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + self + L"\" --crash-host \"" + work + L"\" \"" + fakeSetup + L"\" " + point;
        CHECK(CreateProcessW(self.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi));
        WaitForSingleObject(pi.hProcess, 20000); DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        const DWORD orphan = SetupProcessOf(work);
        WI::Marker before; const bool recorded = WI::ReadMarker(c.marker, before);
        // Next browser start.
        WI::Settled settled;
        const DWORD busy = WI::ActiveInstall(c, &settled);
        std::printf("[crash %ls] host exit=%lu setup=%lu recorded=%d resumed=%d -> busy=%lu adopted=%d previousSuspend=%ld\n", point, code, orphan, recorded,
                    recorded && before.resumed, busy, settled.adopted, long(settled.previousSuspend));
        CHECK(code == 99);
        const bool launched = std::wstring(point) != L"verify" && std::wstring(point) != L"claimed";
        if (!launched) {
            CHECK(orphan == 0 && busy == 0 && !Exists(c.marker) && Entries(work) == 0);   // not blocked; its download folder removed exactly
        } else {
            CHECK(orphan != 0 && busy == orphan);   // one setup, the next start waits for it (no second one)
            const std::wstring p = point;
            if (p == L"created" || p == L"recorded") CHECK(settled.adopted && settled.previousSuspend == 1);   // it had never run: now it does
            if (p == L"resumed") CHECK(settled.adopted && settled.previousSuspend == 0);                       // already running: ResumeThread was a no-op
            if (p == L"resumed-rec") CHECK(!settled.adopted);                                                  // recorded as resumed: not touched
            // A second claimant is refused while it lives, and repeated checks never resume it twice.
            Run second(work); second.fetch->body = "MZ"; second.trust.status = WI::TrustResult::Ok;
            auto rep = second.Go();
            CHECK(rep.outcome == WR::InstallReport::Outcome::Busy && second.launches == 0);
            WI::Settled again; CHECK(WI::ActiveInstall(c, &again) == orphan && !again.adopted);
            // It is never killed: it runs to its end and writes its output.
            CHECK(WaitGone(orphan, 8000) && Exists(ran));
            // Then the record and the private folder are cleaned and a new install may start.
            CHECK(WI::ActiveInstall(c) == 0 && !Exists(c.marker) && Entries(work) == 0);
        }
    }
    // A pid-less record whose host is dead and whose setup is gone (or never existed): stale.
    WI::Marker dead; dead.host = 0xFFFFFFF0; dead.hostCreated = 5; dead.dir = work + L"\\browser-wv2-00000000000000000000000a";
    CHECK(WI::WriteMarker(c.marker, dead) && WI::ActiveInstall(c) == 0 && !Exists(c.marker));
    DeleteFileW(ran.c_str());
}

// ---------------------------------------------------------------- Quitter while the worker stalls (R3-1)
// The reviewer's case: a child host runs the real Installer + RunInstall; a barrier stalls the
// worker right after the setup was created and recorded (before its last cancel check), the user
// quits — Installer::Shutdown(1500) exactly like StopEngineStart — and the process exits while the
// worker is still stalled. Variants: `resumed` (barrier after ResumeThread, before `resumed=1` is
// recorded: the installer had started and must keep running), `locked` (as `created`, but the parent
// holds the per-user lock so the withdrawal cannot be recorded).
static int QuitHost(const std::wstring& work, const std::wstring& fakeSetup, const std::wstring& point) {
    Run r(work); r.fetch->body = ReadAll(fakeSetup); r.trust.status = WI::TrustResult::Ok;
    WI::Effects e = r.Effects();
    const std::wstring signal = work + L"\\at-barrier.txt";
    auto barrier = [&] { std::ofstream(std::filesystem::path(signal)) << "x"; Sleep(4000); };
    e.launch = [&](const std::wstring& exe, const std::wstring& dir, HANDLE cancel, DWORD timeout, const WI::LaunchHooks& hooks) {
        WI::LaunchHooks h = hooks;
        h.created = [&](DWORD pid, std::uint64_t created, DWORD tid, DWORD& busy) {
            const WI::Claim c = hooks.created(pid, created, tid, busy);
            if (point != L"resumed") barrier();
            return c;
        };
        h.resumed = [&] { if (point == L"resumed") barrier(); hooks.resumed(); };
        return WI::RunBootstrapper(exe, dir, cancel, timeout, h);
    };
    WI::Installer installer;
    installer.Start(e, [] {});
    const ULONGLONG until = GetTickCount64() + 15000;
    while (GetFileAttributesW(signal.c_str()) == INVALID_FILE_ATTRIBUTES && GetTickCount64() < until) Sleep(5);
    Sleep(300);
    if (point == L"locked") { while (GetFileAttributesW((work + L"\\lock-held.txt").c_str()) == INVALID_FILE_ATTRIBUTES && GetTickCount64() < until) Sleep(5); }
    const auto st = installer.Shutdown(1500);   // « Quitter », as StopEngineStart
    std::printf("quit-host %ls: stage=%ls detached=%d alreadyResumed=%d terminatedSuspended=%d withdrawalRecorded=%d\n", point.c_str(),
                WI::StageName(st.stage), st.detached, st.alreadyResumed, st.terminatedSuspended, st.withdrawalRecorded);
    std::fflush(stdout);
    const int code = (st.alreadyResumed ? 1 : 0) | (st.terminatedSuspended ? 2 : 0) | (st.withdrawalRecorded ? 4 : 0) | (st.detached ? 8 : 0);
    ExitProcess(UINT(code));   // end of wWinMain, worker still stalled
}
static void QuitBarriers(const std::wstring& work, const std::wstring& self, const std::wstring& fakeSetup) {
    const auto c = Coord(work);
    const std::wstring ran = work + L"\\quit-ran.txt", signal = work + L"\\at-barrier.txt", held = work + L"\\lock-held.txt";
    for (const wchar_t* point : {L"created", L"locked", L"resumed"}) {
        DeleteFileW(ran.c_str()); DeleteFileW(signal.c_str()); DeleteFileW(held.c_str());
        SetupEnv(ran, L"0", L"7000");   // long enough to still run when the next check comes
        STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + self + L"\" --quit-host \"" + work + L"\" \"" + fakeSetup + L"\" " + point;
        CHECK(CreateProcessW(self.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi));
        HANDLE lockHandle = INVALID_HANDLE_VALUE;
        if (std::wstring(point) == L"locked") {   // hold the per-user lock while the child quits
            const ULONGLONG until = GetTickCount64() + 15000;
            while (GetFileAttributesW(signal.c_str()) == INVALID_FILE_ATTRIBUTES && GetTickCount64() < until) Sleep(5);
            while (lockHandle == INVALID_HANDLE_VALUE && GetTickCount64() < until) { lockHandle = CreateFileW(c.lock.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr); if (lockHandle == INVALID_HANDLE_VALUE) Sleep(5); }
            std::ofstream(std::filesystem::path(held)) << "x";
        }
        WaitForSingleObject(pi.hProcess, 30000); DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        if (lockHandle != INVALID_HANDLE_VALUE) CloseHandle(lockHandle);
        Sleep(800);
        const bool ranBefore = Exists(ran);
        WI::Settled s1; const DWORD b1 = WI::ActiveInstall(c, &s1);
        Sleep(2500);
        const bool ranAfter = Exists(ran);
        const DWORD live = SetupProcessOf(work);
        std::printf("[quit %ls] host code=%lu ranBefore=%d check: busy=%lu adopted=%d previousSuspend=%ld residue=%lu ranAfter=%d liveSetup=%lu\n", point, code,
                    ranBefore, b1, s1.adopted, long(s1.previousSuspend), s1.cancelledResidue, ranAfter, live);
        if (std::wstring(point) == L"resumed") {
            // Resumed before the cancel: it had started, keeps running, is tracked (busy) and never killed.
            CHECK((code & 1) && !(code & 2) && !(code & 4));
            CHECK(s1.adopted && s1.previousSuspend == 0);   // record lacked resumed=1: ResumeThread was a no-op
            CHECK(b1 != 0 && ranAfter);
            Run second(work); second.fetch->body = "MZ"; second.trust.status = WI::TrustResult::Ok;
            CHECK(second.Go().outcome == WR::InstallReport::Outcome::Busy && second.launches == 0);
            CHECK(WaitGone(b1, 12000) && WI::ActiveInstall(c) == 0 && Entries(work) == 0);
        } else {
            // Quitter before the resume: the gate terminated the never-resumed setup; nothing ever runs it.
            CHECK(!(code & 1) && (code & 2) && (code & 8));
            if (std::wstring(point) == L"created") CHECK(code & 4);   // withdrawal recorded
            else CHECK(!(code & 4));                                  // lock held: not recorded, and still safe
            CHECK(!s1.adopted && b1 == 0 && !ranBefore && !ranAfter && live == 0 && s1.cancelledResidue == 0);
            CHECK(!Exists(c.marker) && Entries(work) == 0);
        }
    }
    DeleteFileW(ran.c_str()); DeleteFileW(signal.c_str()); DeleteFileW(held.c_str());
}
// A record withdrawn before resume, whose suspended setup survived (termination impossible): never
// adopted, never an active install; a withdrawn-but-resumed record is impossible by construction
// and, if seen, is tracked as running (never killed).
static void CancelledRecords(const std::wstring& work, const std::wstring& fakeSetup) {
    const auto c = Coord(work);
    const std::wstring dir = work + L"\\browser-wv2-0000000000000000000000cc", image = dir + L"\\" + WI::FileName(), ran = work + L"\\cancelled-ran.txt";
    CreateDirectoryW(dir.c_str(), nullptr);
    CHECK(CopyFileW(fakeSetup.c_str(), image.c_str(), FALSE));
    SetupEnv(ran, L"0", nullptr);
    std::wstring cmd = L"\"" + image + L"\""; STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    CHECK(CreateProcessW(image.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi));
    WI::Marker m; m.host = 0xFFFFFFF0; m.hostCreated = 9; m.pid = pi.dwProcessId; m.created = WI::ProcessCreated(pi.hProcess); m.tid = pi.dwThreadId; m.dir = dir; m.cancelled = true;
    CHECK(WI::WriteMarker(c.marker, m));
    WI::Settled st; const DWORD busy = WI::ActiveInstall(c, &st);
    Sleep(600);
    CHECK(busy == 0 && !st.adopted && st.cancelledResidue == pi.dwProcessId && !Exists(ran) && !Exists(c.marker));
    TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000);   // our own fixture process
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    WI::RemovePrivateDir(dir);
    // cancelled + resumed (cannot be produced by the protocol): tracked as a running install.
    WI::Marker r2; r2.pid = GetCurrentProcessId(); r2.created = WI::ProcessCreated(GetCurrentProcess()); r2.resumed = true; r2.cancelled = true;
    CHECK(WI::WriteMarker(c.marker, r2) && WI::ActiveInstall(c) == r2.pid);
    DeleteFileW(c.marker.c_str());
    // UpdateInstall never clears a recorded withdrawal.
    WI::Marker mine = WI::SelfHost(); DWORD b = 0;
    CHECK(WI::ClaimInstall(c, mine, b) == WI::Claim::Claimed && WI::MarkCancelled(c));
    mine.resumed = false; CHECK(WI::UpdateInstall(c, mine));
    WI::Marker back; CHECK(WI::ReadMarker(c.marker, back) && back.cancelled);
    WI::ReleaseInstall(c, mine);
    CHECK(!Exists(c.marker));
    DeleteFileW(ran.c_str());
}
// Cancel before creation: a stalled verification, Shutdown, then the worker reaches the launch.
static void CancelBeforeCreate(const std::wstring& work, const std::wstring& fakeSetup) {
    const std::wstring ran = work + L"\\early-ran.txt";
    SetupEnv(ran, L"0", nullptr);
    Run r(work); r.fetch->body = ReadAll(fakeSetup); r.trust.status = WI::TrustResult::Ok; r.realLaunch = true;
    WI::Effects e = r.Effects();
    e.verify = [](HANDLE, const std::wstring&) { Sleep(800); WI::TrustResult t; t.status = WI::TrustResult::Ok; return t; };
    WI::Installer installer;
    WR::InstallReport finalReport; std::mutex m;
    CHECK(installer.Start(e, [] {}, [&](const WR::InstallReport& rep) { std::lock_guard<std::mutex> g(m); finalReport = rep; }));
    Sleep(300);
    const auto st = installer.Shutdown(100);
    CHECK(st.detached && !st.alreadyResumed && !st.terminatedSuspended);
    Sleep(2000);
    { std::lock_guard<std::mutex> g(m); CHECK(finalReport.outcome == WR::InstallReport::Outcome::Cancelled && finalReport.pid == 0); }
    CHECK(r.launches == 0 && !Exists(ran) && SetupProcessOf(work) == 0 && Entries(work) == 0 && !Exists(Coord(work).marker));
}

// Two real processes: the first « host » starts an install whose (fake) installer keeps running,
// then quits; this process must not launch a second one until it is gone.
static int FirstHost(const std::wstring& work, const std::wstring& fakeSetup) {
    SetupEnv(work + L"\\first-ran.txt", L"0", L"2500");
    Run r(work); r.fetch->body = ReadAll(fakeSetup); r.trust.status = WI::TrustResult::Ok; r.realLaunch = true;
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread quit([&] { Sleep(600); SetEvent(cancel); });   // « Quitter » while the installer runs
    auto rep = r.Go(WI::InstallTimeoutMs, cancel);
    quit.join();
    std::printf("first host: outcome=%ls pid=%lu residue=%ls\n", WR::OutcomeName(rep.outcome), rep.pid, rep.residue.c_str());
    return rep.outcome == O::Detached && rep.installerRunning ? 0 : 1;
}
static void SecondHost(const std::wstring& work, const std::wstring& self, const std::wstring& fakeSetup) {
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + self + L"\" --first-host \"" + work + L"\" \"" + fakeSetup + L"\"";
    CHECK(CreateProcessW(self.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi));
    WaitForSingleObject(pi.hProcess, 10000); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    CHECK(code == 0);   // the first host exited while its installer still runs
    const auto c = Coord(work);
    const DWORD running = WI::ActiveInstall(c);
    std::printf("second host: running installer pid=%lu\n", running);
    CHECK(running != 0);
    Run second(work); second.fetch->body = ReadAll(fakeSetup); second.trust.status = WI::TrustResult::Ok; second.realLaunch = true;
    auto rep = second.Go();
    CHECK(rep.outcome == O::Busy && rep.code == running && second.fetch->calls == 0 && second.launches == 0);
    CHECK(WaitGone(running, 8000));
    CHECK(Exists(work + L"\\first-ran.txt"));   // the first installer completed on its own
    CHECK(WI::ActiveInstall(c) == 0 && Entries(work) == 0);   // its marker and folder are cleaned by the next check
    SetupEnv(work + L"\\third-ran.txt", L"0", nullptr);
    Run third(work); third.fetch->body = ReadAll(fakeSetup); third.trust.status = WI::TrustResult::Ok; third.realLaunch = true;
    CHECK(third.Go().outcome == O::Finished && Exists(work + L"\\third-ran.txt"));
    DeleteFileW((work + L"\\first-ran.txt").c_str()); DeleteFileW((work + L"\\third-ran.txt").c_str());
}

static void Worker(const std::wstring& work) {
    Run r(work); r.fetch->body = std::string(400 * 1024, 'x'); r.fetch->chunkDelayMs = 5; r.trust.status = WI::TrustResult::Ok;
    WI::Installer installer;
    std::atomic<int> notified{0}, finals{0};
    CHECK(installer.Start(r.Effects(), [&] { ++notified; }, [&](const WR::InstallReport&) { ++finals; }));
    CHECK(installer.Running() && !installer.Start(r.Effects(), [] {}));   // double click: one installer
    Sleep(60);
    CHECK(installer.CurrentStage() == WI::Stage::Downloading);
    installer.Cancel();
    for (int i = 0; i < 300 && installer.Running(); ++i) Sleep(10);
    auto events = installer.Take();
    CHECK(!events.empty() && events.back().done && events.back().report.outcome == O::Cancelled && notified.load() >= 1 && finals == 1);
    CHECK(r.launches == 0 && Entries(work) == 0);
    // Shutdown while the (fake) installer runs: bounded, no UI notification afterwards, but the
    // real outcome still reaches the final log.
    Run t(work); t.fetch->body = "MZ"; t.trust.status = WI::TrustResult::Ok;
    WI::Effects e = t.Effects();
    e.launch = [](const std::wstring&, const std::wstring&, HANDLE cancel, DWORD, const WI::LaunchHooks&) {
        WI::LaunchResult l; l.pid = 1234;
        l.status = WaitForSingleObject(cancel, 5000) == WAIT_OBJECT_0 ? WI::LaunchResult::Detached : WI::LaunchResult::Exited; return l;
    };
    std::atomic<int> late{0}; WR::InstallReport finalReport; std::mutex m;
    CHECK(installer.Start(e, [&] { ++late; }, [&](const WR::InstallReport& rep) { std::lock_guard<std::mutex> g(m); finalReport = rep; }));
    Sleep(150);
    const int seen = late.load();
    const ULONGLONG before = GetTickCount64();
    auto state = installer.Shutdown(2000);
    CHECK(GetTickCount64() - before < 2000 && !installer.Running() && !state.detached && !state.dir.empty());
    Sleep(50);
    CHECK(late.load() == seen);
    { std::lock_guard<std::mutex> g(m); CHECK(finalReport.outcome == O::Detached && finalReport.installerRunning && finalReport.residue == state.dir); }
    // The folder is left to the « running » installer (its pid 1234 is not ours): remove it here.
    CHECK(Entries(work) == 1);
    WI::RemovePrivateDir(work + L"\\" + state.dir);
    CHECK(Entries(work) == 0);
}

// ---------------------------------------------------------------- WinHTTP, stubbed (no socket)
namespace Stub {
std::atomic<int> sends{0}, setOptionCalls{0}; DWORD failOption = 0; DWORD trickleMs = 0; int redirects = 0;
HINTERNET WINAPI Open(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) { return reinterpret_cast<HINTERNET>(1); }
BOOL WINAPI Timeouts(HINTERNET, int, int, int, int) { return TRUE; }
BOOL WINAPI SetOption(HINTERNET, DWORD option, LPVOID, DWORD) { ++setOptionCalls; if (option == failOption) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; } return TRUE; }
HINTERNET WINAPI Connect(HINTERNET, LPCWSTR, INTERNET_PORT, DWORD) { return reinterpret_cast<HINTERNET>(2); }
HINTERNET WINAPI OpenRequest(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD) { return reinterpret_cast<HINTERNET>(3); }
BOOL WINAPI Send(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR) { ++sends; return TRUE; }
BOOL WINAPI Receive(HINTERNET, LPVOID) { return TRUE; }
BOOL WINAPI Query(HINTERNET, DWORD info, LPCWSTR, LPVOID buffer, LPDWORD size, LPDWORD) {
    if (info == (WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER)) { *static_cast<DWORD*>(buffer) = redirects-- > 0 ? 302 : 200; return TRUE; }
    if (info == WINHTTP_QUERY_LOCATION) { const wchar_t* l = L"https://evil.example.net/x.exe"; wcscpy(static_cast<wchar_t*>(buffer), l); *size = DWORD(wcslen(l) * 2); return TRUE; }
    return FALSE;   // no Content-Length
}
BOOL WINAPI Read(HINTERNET, LPVOID buffer, DWORD, LPDWORD got) { Sleep(trickleMs); *static_cast<char*>(buffer) = 'x'; *got = 1; return TRUE; }
BOOL WINAPI Close(HINTERNET) { return TRUE; }
WI::Api Table() {
    WI::Api a = WI::Api::Get();
    a.http = true; a.Open = Open; a.SetTimeouts = Timeouts; a.SetOption = SetOption; a.Connect = Connect; a.OpenRequest = OpenRequest;
    a.SendRequest = Send; a.ReceiveResponse = Receive; a.QueryHeaders = Query; a.ReadData = Read; a.CloseHandle = Close;
    return a;
}
}  // namespace Stub
static void HttpFailClosed() {
    auto sink = [](const void*, std::size_t) { return true; };
    std::atomic<bool> cancel{false};
    // Per-request DISABLE_FEATURE refused: nothing is sent.
    Stub::failOption = WINHTTP_OPTION_DISABLE_FEATURE; Stub::sends = 0;
    { WI::WinHttpFetch f(Stub::Table()); auto r = f.Get(WI::BootstrapperUrl(), WI::MaxBytes, sink, nullptr, cancel);
      CHECK(r.status == WI::FetchResult::Rejected && Stub::sends == 0); }
    // Session redirect policy refused: the session is unusable, nothing is sent.
    Stub::failOption = WINHTTP_OPTION_REDIRECT_POLICY; Stub::sends = 0;
    { WI::WinHttpFetch f(Stub::Table()); auto r = f.Get(WI::BootstrapperUrl(), WI::MaxBytes, sink, nullptr, cancel);
      CHECK(r.status == WI::FetchResult::Rejected && Stub::sends == 0); }
    // A redirect outside microsoft.com is refused before reading any body.
    Stub::failOption = 0; Stub::sends = 0; Stub::redirects = 1;
    { WI::WinHttpFetch f(Stub::Table()); int bytes = 0; auto r = f.Get(WI::BootstrapperUrl(), WI::MaxBytes, [&](const void*, std::size_t n) { bytes += int(n); return true; }, nullptr, cancel);
      CHECK(r.status == WI::FetchResult::Rejected && Stub::sends == 1 && bytes == 0); }
    // A trickling body hits the total deadline (injected: 300 ms instead of 5 min).
    Stub::redirects = 0; Stub::trickleMs = 20;
    { WI::WinHttpFetch f(Stub::Table(), 300); const ULONGLONG t0 = GetTickCount64();
      auto r = f.Get(WI::BootstrapperUrl(), WI::MaxBytes, sink, nullptr, cancel);
      CHECK(r.status == WI::FetchResult::TimedOut && GetTickCount64() - t0 < 2000); }
    // Cancelled mid-trickle.
    { WI::WinHttpFetch f(Stub::Table(), 60000); std::atomic<bool> c{false};
      std::thread t([&] { Sleep(100); c = true; });
      auto r = f.Get(WI::BootstrapperUrl(), WI::MaxBytes, sink, nullptr, c); t.join();
      CHECK(r.status == WI::FetchResult::Cancelled); }
    // An initial URL outside the policy is never requested.
    Stub::sends = 0;
    { WI::WinHttpFetch f(Stub::Table()); CHECK(f.Get(L"https://evil.example.net/", WI::MaxBytes, sink, nullptr, cancel).status == WI::FetchResult::Rejected && Stub::sends == 0); }
    Stub::trickleMs = 0;
}

static void RealTrust(const std::wstring& work, const std::wstring& fakeSetup, const std::wstring& bootstrapper) {
    auto open = [](const std::wstring& path) { return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); };
    auto verify = [&](const std::wstring& path, bool requireBootstrapper) {
        HANDLE h = open(path);
        if (h == INVALID_HANDLE_VALUE) { WI::TrustResult r; r.code = GetLastError(); return r; }
        auto r = WI::VerifyMicrosoftSignature(h, path, requireBootstrapper);
        CloseHandle(h);
        return r;
    };
    auto original = [&](const std::wstring& path) { HANDLE h = open(path); std::wstring s; if (h != INVALID_HANDLE_VALUE) { if (!WI::ReadOriginalFilename(h, s)) s = L"(none)"; CloseHandle(h); } return s; };
    const std::wstring loader = L"deps\\webview2\\build\\native\\x64\\WebView2Loader.dll";
    if (Exists(loader)) {
        auto r = verify(loader, false);
        std::printf("trust %ls: status=%d code=0x%08lX signer=%ls microsoftRootPolicy=%d original=%ls\n", loader.c_str(), r.status, r.code, r.signer.c_str(), r.root, original(loader).c_str());
        CHECK(r.status == WI::TrustResult::Ok && r.signer == L"Microsoft Corporation");
        CHECK(WI::Lower(original(loader)) == L"webview2loader.dll");
        CHECK(verify(loader, true).status == WI::TrustResult::Unexpected);
        const std::wstring tampered = work + L"\\tampered.dll";
        std::string bytes = ReadAll(loader); bytes[bytes.size() / 2] ^= 0x01;
        { std::ofstream out(std::filesystem::path(tampered), std::ios::binary); out << bytes; }
        auto t = verify(tampered, false);
        std::printf("trust tampered: status=%d code=0x%08lX\n", t.status, t.code);
        CHECK(t.status == WI::TrustResult::Untrusted && t.code != 0);
        // Truncated image: the resource parser fails closed.
        { std::ofstream out(std::filesystem::path(tampered), std::ios::binary); out << bytes.substr(0, 600); }
        CHECK(original(tampered) == L"(none)");
        DeleteFileW(tampered.c_str());
    } else std::printf("SKIP SDK loader trust (deps not present)\n");
    auto u = verify(fakeSetup, false);
    std::printf("trust unsigned fake_setup: status=%d code=0x%08lX original=%ls\n", u.status, u.code, original(fakeSetup).c_str());
    CHECK(u.status == WI::TrustResult::Untrusted && u.code == static_cast<DWORD>(TRUST_E_NOSIGNATURE));
    wchar_t local[MAX_PATH]; GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    const std::wstring python = std::wstring(local) + L"\\Programs\\Python\\Python312\\python.exe";
    if (Exists(python)) {
        auto p = verify(python, true);
        std::printf("trust python.exe: status=%d signer=%ls\n", p.status, p.signer.c_str());
        CHECK(p.status == WI::TrustResult::Publisher && p.signer != L"Microsoft Corporation");
    } else std::printf("SKIP other-publisher trust (python.exe not found)\n");
    if (!bootstrapper.empty()) {
        auto b = verify(bootstrapper, true);
        std::printf("trust real bootstrapper: status=%d code=0x%08lX signer=%ls microsoftRootPolicy=%s original=%ls\n", b.status, b.code, b.signer.c_str(),
                    b.root == 0 ? "default" : b.root == 1 ? "application-root" : "none", original(bootstrapper).c_str());
        CHECK(b.status == WI::TrustResult::Ok && b.signer == L"Microsoft Corporation" && b.root == 1);
    } else std::printf("SKIP real bootstrapper trust (no file given)\n");
}

int wmain(int argc, wchar_t** argv) {
    auto full = [](const wchar_t* p) { wchar_t b[MAX_PATH]; DWORD n = GetFullPathNameW(p, MAX_PATH, b, nullptr); return n && n < MAX_PATH ? std::wstring(b, n) : std::wstring(p); };
    if (argc == 4 && std::wstring(argv[1]) == L"--first-host") return FirstHost(full(argv[2]), full(argv[3]));
    if (argc == 5 && std::wstring(argv[1]) == L"--crash-host") return CrashHost(full(argv[2]), full(argv[3]), argv[4]);
    if (argc == 5 && std::wstring(argv[1]) == L"--quit-host") return QuitHost(full(argv[2]), full(argv[3]), argv[4]);
    if (argc < 3) { std::printf("usage: install_test <work dir> <fake_setup.exe> [bootstrapper]\n"); return 2; }
    const std::wstring work = full(argv[1]), fakeSetup = full(argv[2]), bootstrapper = argc > 3 ? full(argv[3]) : L"";
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH);
    CreateDirectoryW(work.c_str(), nullptr);
    PrivateDirAcl(work);
    CancelRaces(work, fakeSetup);
    Pipeline(work, fakeSetup);
    Markers(work);
    SecondHost(work, self, fakeSetup);
    CrashBarriers(work, self, fakeSetup);
    QuitBarriers(work, self, fakeSetup);
    CancelledRecords(work, fakeSetup);
    CancelBeforeCreate(work, fakeSetup);
    Worker(work);
    HttpFailClosed();
    RealTrust(work, fakeSetup, bootstrapper);
    std::printf("install_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
