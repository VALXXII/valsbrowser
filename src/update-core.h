#pragma once
// Native self-update engine: GitHub release feed check, package download, staging, install handoff, data paths.
// Contract: build/coordination/update-core-contract.md. Link: -lwinhttp -lbcrypt.
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "update-package.h"
#include "update-types.h"

namespace UpdateCore {

// ---------------------------------------------------------------- data paths
struct DataPaths {
    std::wstring dataDir, profileDir, sessionFile, downloadsFile, logFile;
    bool installed = false;  // true when running from <root>\versions\<ver>\browser.exe
};
std::wstring DefaultInstallRoot();  // %LOCALAPPDATA%\browser
DataPaths ResolveDataPaths(const std::wstring& exePath, const std::wstring& installRoot = L"");

// ---------------------------------------------------------------- transport
struct HttpResult {
    enum Status { Ok, NotFound, Failed, Cancelled, TooLarge, Rejected } status = Failed;
    DWORD httpStatus = 0;
    std::string error;
};
class Transport {
public:
    virtual ~Transport() = default;
    // GET over HTTPS. onLength(n) is called once with Content-Length (0 = unknown) and may return false to abort.
    // sink receives body chunks; returning false aborts. Body larger than maxBytes => TooLarge.
    virtual HttpResult Get(const std::wstring& url, std::uint64_t maxBytes, const std::function<bool(std::uint64_t)>& onLength,
                           const std::function<bool(const void*, std::size_t)>& sink, const std::atomic<bool>& cancel) = 0;
    // Thread-safe: unblocks an in-flight Get (it then returns Cancelled).
    virtual void Abort() {}
};
std::shared_ptr<Transport> CreateWinHttpTransport();
// URL policy: https, port 443, no credentials. initial => github.com/<repo>/releases/...;
// redirect => github.com, objects.githubusercontent.com or release-assets.githubusercontent.com.
bool AllowedUrl(const std::wstring& url, const std::wstring& repository, bool initial);
std::wstring ManifestUrl(const std::wstring& repository);
std::wstring PackageUrl(const std::wstring& repository, const std::string& version);

// ---------------------------------------------------------------- engine
struct Config {
    std::wstring currentVersion;  // "M.m.B"; "0.0.0" or invalid => updates disabled
    std::wstring exePath;         // running browser.exe
    std::wstring installRoot;     // empty => DefaultInstallRoot()
    std::wstring repository;      // "owner/repo"; empty => BROWSER_UPDATE_REPO or VALXXII/browser-releases
    HWND notifyWindow = nullptr;
    UINT notifyMessage = 0;
    bool enabled = true, automaticCheck = true;
    DWORD startupDelayMs = 4000;
    std::shared_ptr<Transport> transport;          // null => WinHTTP
    std::function<std::int64_t()> clock;           // unix seconds; null => system time
    std::function<std::uint64_t()> ticks;          // monotonic ms; null => GetTickCount64
};

enum class ActionResult { Ignored, Handled, CloseForInstall };

// Called once by the browser early at startup (Engine::Start does it for you).
// Returns Installed exactly once for the helper-launched expected version, Error once after a rolled-back install, else Hidden.
BrowserUpdate::View NoteStartup(const Config& config);

class Engine {
public:
    enum class LastCheck { None, UpToDate, Available, Failed };
    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool Start(const Config& config);
    void Shutdown();
    BrowserUpdate::View View() const;
    bool OnNotify(WPARAM generation);
    ActionResult Perform(BrowserUpdate::Action action);
    void CheckNow();
    LastCheck LastCheckResult() const;
    bool Enabled() const { return enabled_; }

private:
    enum class Op { None, Check, Download };
    struct State {
        std::int64_t nextCheck = 0;
        std::uint32_t failures = 0;
        std::string notified;  // "available:<v>" or "ready:<v>"
    };
    void Launch(Op op, bool manual, bool startup);
    void StopWorker();
    void RunCheck(std::uint64_t gen, bool manual, bool startup);
    void RunDownload(std::uint64_t gen, UpdatePackage::Manifest manifest);
    bool Publish(std::uint64_t gen, const BrowserUpdate::View& view);
    void Post();
    bool Wait(DWORD ms);
    std::int64_t Now() const;
    std::uint64_t Ticks() const;
    State LoadState() const;
    void SaveState(const State& s) const;
    bool FindStaged(UpdatePackage::Manifest* manifestOut, std::string& version);
    std::wstring Updates() const { return root_ + L"\\updates"; }
    bool EnsureDir(const std::wstring& dir) const;
    BrowserUpdate::View Make(BrowserUpdate::Phase phase, const std::string& version, const std::wstring& error = L"") const;
    void SetView(const BrowserUpdate::View& v);  // UI thread, bumps nothing

    Config config_;
    std::wstring root_, repository_;
    std::string current_;
    UpdatePackage::Version currentVersion_;
    bool enabled_ = false, started_ = false;
    std::shared_ptr<Transport> transport_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    BrowserUpdate::View view_;
    std::uint64_t generation_ = 0;
    WPARAM session_ = 0;
    std::atomic<bool> notifyPending_{false}, cancel_{false}, shutdown_{false};
    std::thread worker_;
    Op failedOp_ = Op::None;
    LastCheck lastCheck_ = LastCheck::None;
    bool haveManifest_ = false;
    UpdatePackage::Manifest manifest_;
    std::string stagedVersion_;
};

}  // namespace UpdateCore
