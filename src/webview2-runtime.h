#pragma once
#include "i18n.h"
// WebView2 runtime availability at startup: classification of startup failures, discovery
// overrides, and the recovery state machine shown by the host (design/webview2-startup-recovery.md).
//
// Header-only and link-free (kernel32/advapi32 only): it compiles into every main.cpp build,
// stamped or not, and into the native fixtures. Nothing here downloads, installs or writes.
//
// Sources (WebView2 SDK 1.0.4314.35):
//   https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution
//     « Approach 2: Run GetAvailableCoreWebView2BrowserVersionString and evaluate whether the
//       versionInfo is nullptr. nullptr indicates that the WebView2 Runtime isn't installed. »
//   https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/webview2-idl
//     CreateCoreWebView2EnvironmentWithOptions error table; overrides (environment variables, then
//     [HKLM|HKCU]\Software\Policies\Microsoft\Edge\WebView2\<Option> with value name AUMID, exe
//     name or "*"); GetAvailableCoreWebView2BrowserVersionStringWithOptions honours the same
//     overrides and options and « Returns HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) if it fails to
//     find an installed WebView2 Runtime or non-stable Microsoft Edge installation ».
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace WebView2Runtime {

inline std::wstring Hex(HRESULT hr) { wchar_t b[16]; swprintf(b, 16, L"0x%08lX", static_cast<unsigned long>(hr)); return b; }

// ---------------------------------------------------------------- probe
// Result of GetAvailableCoreWebView2BrowserVersionStringWithOptions with the engine's options.
struct Probe {
    bool attempted = false;   // false: the export is missing (old loader) — nothing is known
    HRESULT hr = E_FAIL;
    std::wstring version;
    bool Found() const { return attempted && SUCCEEDED(hr) && !version.empty(); }
    // Only the documented « not found » answers mean missing; any other failure is « unknown ».
    bool Missing() const { return attempted && (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || (SUCCEEDED(hr) && version.empty())); }
};

// ---------------------------------------------------------------- overrides
// A discovery override changes WHICH runtime the loader looks for: installing the Evergreen
// runtime cannot satisfy an override that points elsewhere, and the browser never edits it.
struct Override {
    std::wstring name;     // WEBVIEW2_BROWSER_EXECUTABLE_FOLDER, ReleaseChannels, …
    std::wstring source;   // « variable d'environnement », « stratégie HKLM », « stratégie HKCU »
    bool blocking = false; // an executable folder or a channel set: the install would not help
};
struct Overrides {
    std::vector<Override> items;
    bool Any() const { return !items.empty(); }
    bool Blocking() const { for (auto& o : items) if (o.blocking) return true; return false; }
    std::wstring Describe() const {
        std::wstring s;
        for (auto& o : items) { if (!s.empty()) s += L", "; s += o.name + L" (" + o.source + L")"; }
        return s;
    }
};
using EnvReader = std::function<bool(const wchar_t* name)>;                              // present?
using PolicyReader = std::function<bool(HKEY root, const wchar_t* key, const wchar_t* value)>; // present?
inline bool EnvPresent(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) > 0; }
inline bool PolicyPresent(HKEY root, const wchar_t* key, const wchar_t* value) {
    DWORD size = 0;
    return RegGetValueW(root, key, value, RRF_RT_ANY, nullptr, nullptr, &size) == ERROR_SUCCESS;
}
// Reads presence only: values are never logged (a folder path can name the user).
// `exeName` is this process's file name (the documented AppId fallback). `appId` is the AppUserModelID an
// installed copy declares (src/taskbar-identity.h), empty for portable and dev copies: WebView2 honours policies
// keyed by either, or by "*".
inline Overrides ReadOverridesFor(const std::wstring& exeName, const std::wstring& appId, EnvReader env = EnvPresent, PolicyReader policy = PolicyPresent) {
    Overrides out;
    struct Item { const wchar_t* variable; const wchar_t* policyKey; const wchar_t* label; bool blocking; };
    static const Item items[] = {
        {L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", L"Software\\Policies\\Microsoft\\Edge\\WebView2\\BrowserExecutableFolder", L"BrowserExecutableFolder", true},
        {L"WEBVIEW2_RELEASE_CHANNELS", L"Software\\Policies\\Microsoft\\Edge\\WebView2\\ReleaseChannels", L"ReleaseChannels", true},
        {L"WEBVIEW2_CHANNEL_SEARCH_KIND", L"Software\\Policies\\Microsoft\\Edge\\WebView2\\ChannelSearchKind", L"ChannelSearchKind", false},
    };
    for (const auto& i : items) {
        if (env(i.variable)) { out.items.push_back({i.variable, TR(wv2_override_env), i.blocking}); continue; }
        for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
            if ((!exeName.empty() && policy(root, i.policyKey, exeName.c_str())) || (!appId.empty() && policy(root, i.policyKey, appId.c_str())) ||
                policy(root, i.policyKey, L"*")) {
                out.items.push_back({i.label, root == HKEY_LOCAL_MACHINE ? TR(wv2_override_hklm) : TR(wv2_override_hkcu), i.blocking});
                break;
            }
        }
    }
    return out;
}
inline Overrides ReadOverrides(const std::wstring& exeName, EnvReader env = EnvPresent, PolicyReader policy = PolicyPresent) {
    return ReadOverridesFor(exeName, L"", env, policy);
}

// ---------------------------------------------------------------- diagnosis
enum class Stage { Loader, Export, Probe, Create, Callback };
enum class Cause {
    RuntimeMissing,       // probe: documented « not found », no blocking override
    RuntimeInconsistent,  // probe sees a runtime, creation says it is missing/uninstalled
    OverrideInvalid,      // not found while an override decides where to look
    LoaderMissing, LoaderInvalid,
    ProfileUnavailable, OptionsMismatch, DiskFull, RuntimeStartFailed, Unknown
};
struct Diagnosis {
    Cause cause = Cause::Unknown; Stage stage = Stage::Create; HRESULT hr = S_OK;
    bool offerInstall = false;
    std::wstring version;                       // probe version ("" = none)
    std::wstring title, body, technical;
};
inline const wchar_t* StageName(Stage s) {
    switch (s) {
    case Stage::Loader: return TR(wv2_stage_loader);
    case Stage::Export: return TR(wv2_stage_export);
    case Stage::Probe: return TR(wv2_stage_probe);
    case Stage::Create: return TR(wv2_stage_create);
    case Stage::Callback: return TR(wv2_stage_callback);
    }
    return L"?";
}
inline const wchar_t* CauseName(Cause c) {
    switch (c) {
    case Cause::RuntimeMissing: return L"runtime-missing";
    case Cause::RuntimeInconsistent: return L"runtime-inconsistent";
    case Cause::OverrideInvalid: return L"override";
    case Cause::LoaderMissing: return L"loader-missing";
    case Cause::LoaderInvalid: return L"loader-invalid";
    case Cause::ProfileUnavailable: return L"profile";
    case Cause::OptionsMismatch: return L"options-mismatch";
    case Cause::DiskFull: return L"disk-full";
    case Cause::RuntimeStartFailed: return L"start-failed";
    case Cause::Unknown: return L"unknown";
    }
    return L"?";
}
inline bool IsMissingCode(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED);
}

// The install is offered only for a runtime-discovery failure reported BY the WebView2 discovery
// calls themselves (probe or environment creation), never for a loader, profile or option error,
// and never while an override chooses the runtime. A 0x80070002 from LoadLibrary is a loader error.
inline Diagnosis Diagnose(Stage stage, HRESULT hr, const Probe& probe, const Overrides& overrides) {
    Diagnosis d; d.stage = stage; d.hr = hr; d.version = probe.Found() ? probe.version : L"";
    if (stage == Stage::Loader) {
        d.cause = hr == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)
                      ? Cause::LoaderMissing : Cause::LoaderInvalid;
    } else if (stage == Stage::Export) {
        d.cause = Cause::LoaderInvalid;
    } else if ((probe.Missing() && (stage == Stage::Probe || IsMissingCode(hr))) || (!probe.attempted && IsMissingCode(hr))) {
        // (no probe export in an old loader: creation's own « not found » is all we know)
        d.cause = overrides.Blocking() ? Cause::OverrideInvalid : Cause::RuntimeMissing;
    } else if (IsMissingCode(hr)) {
        // Creation says missing while the probe found one (or could not tell).
        d.cause = overrides.Blocking() ? Cause::OverrideInvalid : Cause::RuntimeInconsistent;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)) {
        d.cause = Cause::OverrideInvalid;   // documented: an \Edge\Application folder was given
    } else if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_FILE_EXISTS)) {
        d.cause = Cause::ProfileUnavailable;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_STATE)) {
        d.cause = Cause::OptionsMismatch;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_DISK_FULL)) {
        d.cause = Cause::DiskFull;
    } else if (hr == E_FAIL) {
        d.cause = Cause::RuntimeStartFailed;
    } else {
        d.cause = Cause::Unknown;
    }
    d.offerInstall = d.cause == Cause::RuntimeMissing || d.cause == Cause::RuntimeInconsistent;
    switch (d.cause) {
    case Cause::RuntimeMissing:
        d.title = TR(wv2_missing_title);
        d.body = TR(wv2_missing_body);
        break;
    case Cause::RuntimeInconsistent:
        d.title = TR(wv2_inconsistent_title);
        d.body = TR(wv2_inconsistent_body);
        break;
    case Cause::OverrideInvalid:
        d.title = TR(wv2_override_title);
        d.body = overrides.Any() ? TRF(wv2_override_body_list, overrides.Describe()) : std::wstring(TR(wv2_override_body));
        break;
    case Cause::LoaderMissing:
        d.title = TR(wv2_loader_missing_title);
        d.body = TR(wv2_loader_missing_body);
        break;
    case Cause::LoaderInvalid:
        d.title = TR(wv2_loader_invalid_title);
        d.body = TR(wv2_loader_invalid_body);
        break;
    case Cause::ProfileUnavailable:
        d.title = TR(wv2_profile_title);
        d.body = TR(wv2_profile_body);
        break;
    case Cause::OptionsMismatch:
        d.title = TR(wv2_options_title);
        d.body = TR(wv2_options_body);
        break;
    case Cause::DiskFull:
        d.title = TR(wv2_disk_title);
        d.body = TR(wv2_disk_body);
        break;
    case Cause::RuntimeStartFailed:
    case Cause::Unknown:
        d.title = TR(wv2_start_title);
        d.body = TR(wv2_start_body);
        break;
    }
    d.technical = TRF(wv2_technical, StageName(stage), Hex(hr), d.version.empty() ? std::wstring(TR(wv2_version_none)) : d.version);
    return d;
}

// ---------------------------------------------------------------- install report
// Filled by the installer (webview2-install.h); interpreted here so the controller stays testable.
struct InstallReport {
    enum class Outcome { Finished, Cancelled, Network, Http, Rejected, TooLarge, Storage, Trust, Publisher, Unexpected,
                         Launch, Elevation, Timeout,
                         Detached,          // launched, then the browser stopped waiting: the installer keeps running
                         Busy,              // another installer of this user is running (code = its PID)
                         DownloadTimeout }; // the download exceeded its total deadline
    Outcome outcome = Outcome::Network;
    DWORD code = 0;               // exit code (Finished), HTTP status, WinVerifyTrust / Win32 error
    bool installerRunning = false; // the Microsoft installer process was left running (timeout / shutdown)
    bool cleanupIncomplete = false; // the downloaded file or its directory could not be removed
    std::wstring signer;           // signing certificate CN when the signature was checked
    DWORD pid = 0;                 // installer process, when one was created
    std::wstring residue;          // private folder name (not path) left in %TEMP%, if any
};
inline std::wstring InstallMessage(const InstallReport& r) {
    using O = InstallReport::Outcome;
    wchar_t code[16]; swprintf(code, 16, L"0x%08lX", static_cast<unsigned long>(r.code));
    switch (r.outcome) {
    case O::Finished: return r.code == 0 ? std::wstring(TR(wv2_install_finished_ok)) : TRF(wv2_install_finished_code, code);
    case O::Cancelled: return TR(wv2_install_cancelled);
    case O::Detached: return TR(wv2_install_detached);
    case O::Busy: return TR(wv2_install_busy);
    case O::DownloadTimeout: return TR(wv2_install_download_timeout);
    case O::Network: return TR(wv2_install_network);
    case O::Http: return TRF(wv2_install_http, std::to_wstring(r.code));
    case O::Rejected: return TR(wv2_install_rejected);
    case O::TooLarge: return TR(wv2_install_too_large);
    case O::Storage: return TRF(wv2_install_storage, code);
    case O::Trust: return TRF(wv2_install_trust, code);
    case O::Publisher: return TR(wv2_install_publisher);
    case O::Unexpected: return TR(wv2_install_unexpected);
    case O::Launch: return TRF(wv2_install_launch, code);
    case O::Elevation: return TR(wv2_install_elevation);
    case O::Timeout: return TR(wv2_install_timeout);
    }
    return L"";
}
inline const wchar_t* OutcomeName(InstallReport::Outcome o) {
    using O = InstallReport::Outcome;
    switch (o) {
    case O::Finished: return L"finished"; case O::Cancelled: return L"cancelled"; case O::Network: return L"network";
    case O::Http: return L"http"; case O::Rejected: return L"rejected"; case O::TooLarge: return L"too-large";
    case O::Storage: return L"storage"; case O::Trust: return L"trust"; case O::Publisher: return L"publisher";
    case O::Unexpected: return L"unexpected"; case O::Launch: return L"launch"; case O::Elevation: return L"elevation";
    case O::Timeout: return L"timeout"; case O::Detached: return L"detached"; case O::Busy: return L"busy";
    case O::DownloadTimeout: return L"download-timeout";
    }
    return L"?";
}

// ---------------------------------------------------------------- controller
// Phases seen by the user. Busy phases lock every button except Quitter.
enum class Phase { Idle, Creating, Problem, Downloading, Verifying, Installing, Detecting, Recovered, Closed };
inline bool Busy(Phase p) { return p == Phase::Downloading || p == Phase::Verifying || p == Phase::Installing || p == Phase::Detecting || p == Phase::Creating; }

struct View {
    Phase phase = Phase::Idle;
    Diagnosis diagnosis;
    bool canInstall = false, canRetry = false, canRelaunch = false;
    std::wstring status;   // progress line while busy
    std::wstring statusDetail;   // what Quitter does in this phase
    bool manualPrimary = false;  // the install is exhausted: the Microsoft page is the main action
    std::wstring note;     // result of the last install attempt ("" = none)
    std::uint64_t bytes = 0, total = 0;
};

struct Hooks {
    // Loader + export resolution: S_OK, or the failing stage and code.
    std::function<HRESULT(Stage& failedStage)> prepare;
    std::function<Probe()> probe;
    // Starts CreateCoreWebView2EnvironmentWithOptions tagged with `generation`; the immediate HRESULT.
    // Completion arrives later through Controller::Created(generation, hr).
    std::function<HRESULT(std::uint64_t generation)> create;
    std::function<Overrides()> overrides;
    std::function<bool()> startInstall;     // async; false = could not start (reported as Launch)
    std::function<DWORD()> installRunning;  // PID of a live installer launched by any browser process of this user, or 0
    std::function<void()> cancelInstall;
    std::function<void(const View&)> show;  // problem/progress UI (also called with Phase::Recovered to hide it)
    std::function<void()> ready;            // environment created: restore the session (called once)
    std::function<bool()> relaunch;         // start a fresh instance of this exe; true = started
    std::function<void(const std::wstring&)> log;
};

class Controller {
public:
    static constexpr int MaxInstalls = 2;   // user-initiated attempts per process; no automatic loop

    explicit Controller(Hooks hooks, bool relaunched = false) : h_(std::move(hooks)), relaunched_(relaunched) {}

    const View& Current() const { return view_; }
    Phase phase() const { return view_.phase; }
    std::uint64_t Generation() const { return generation_; }
    int Installs() const { return installs_; }
    bool ReadyFired() const { return readyFired_; }
    // Is `generation` the creation still awaited? (The host keeps the environment only then.)
    bool Accepts(std::uint64_t generation) const { return view_.phase == Phase::Creating && generation == generation_; }

    // Startup and « Réessayer »: loader → probe → create. Normal path: one probe, one create, no UI.
    void Attempt() {
        if (view_.phase == Phase::Closed || view_.phase == Phase::Recovered || Busy(view_.phase)) return;
        Stage failed = Stage::Loader;
        HRESULT hr = h_.prepare ? h_.prepare(failed) : S_OK;
        if (FAILED(hr)) { Problem(failed, hr, Probe{}); return; }
        Probe p = h_.probe ? h_.probe() : Probe{};
        Log(L"WEBVIEW2 PROBE " + (p.attempted ? Hex(p.hr) : std::wstring(L"unavailable")) + L" version=" + (p.version.empty() ? L"-" : p.version));
        if (p.Missing()) { Problem(Stage::Probe, p.hr, p); return; }
        StartCreate(p);
    }
    // Environment completion (or immediate failure path inside the host's create): stale or late
    // callbacks are ignored. Returns true when the host must keep the environment.
    bool Created(std::uint64_t generation, HRESULT hr) {
        if (view_.phase == Phase::Closed || generation != generation_ || view_.phase != Phase::Creating) {
            Log(L"WEBVIEW2 stale environment callback ignored gen=" + std::to_wstring(generation));
            return false;
        }
        if (FAILED(hr)) {
            Probe p = h_.probe ? h_.probe() : Probe{};
            Problem(Stage::Callback, hr, p);
            return false;
        }
        view_.phase = Phase::Recovered;
        if (problemShown_) { Log(L"WEBVIEW2 RECOVERY environment created"); Show(); }
        if (!readyFired_) { readyFired_ = true; if (h_.ready) h_.ready(); }
        return true;
    }
    // « Installer WebView2 ». Ignored unless offered and idle: a double click starts one installer.
    bool Install() {
        if (!view_.canInstall || view_.phase != Phase::Problem) return false;
        ++installs_;
        view_.phase = Phase::Downloading; view_.canInstall = view_.canRetry = view_.canRelaunch = false;
        view_.status = TR(wv2_status_downloading); view_.bytes = view_.total = 0; view_.note.clear();
        view_.statusDetail = TR(wv2_status_quit_cancels);
        Log(L"WEBVIEW2 RECOVERY install requested attempt=" + std::to_wstring(installs_));
        Show();
        if (!h_.startInstall || !h_.startInstall()) {
            InstallReport r; r.outcome = InstallReport::Outcome::Launch; r.code = ERROR_BUSY;
            InstallFinished(r);
            return false;
        }
        return true;
    }
    void Progress(Phase phase, std::uint64_t bytes = 0, std::uint64_t total = 0) {
        if (view_.phase != Phase::Downloading && view_.phase != Phase::Verifying && view_.phase != Phase::Installing) return;
        if (phase != Phase::Downloading && phase != Phase::Verifying && phase != Phase::Installing) return;
        view_.phase = phase; view_.bytes = bytes; view_.total = total;
        if (phase == Phase::Downloading) {
            view_.status = TR(wv2_status_downloading);
            if (total) view_.status += L" " + I18n::Percent(static_cast<long long>(bytes * 100 / total));
        } else if (phase == Phase::Verifying) view_.status = TR(wv2_status_verifying);
        else view_.status = TR(wv2_status_installing);
        view_.statusDetail = phase == Phase::Installing
            ? TR(wv2_status_continues)
            : TR(wv2_status_quit_cancels);
        Show();
    }
    void InstallFinished(const InstallReport& r) {
        if (view_.phase != Phase::Downloading && view_.phase != Phase::Verifying && view_.phase != Phase::Installing) {
            Log(L"WEBVIEW2 RECOVERY late install report ignored"); return;
        }
        Log(std::wstring(L"WEBVIEW2 RECOVERY install ") + OutcomeName(r.outcome) + L" code=" + Hex(static_cast<HRESULT>(r.code)) +
            (r.signer.empty() ? L"" : L" signer=" + r.signer) + (r.installerRunning ? L" installer-still-running" : L"") +
            (r.cleanupIncomplete ? L" cleanup-incomplete" : L""));
        view_.note = InstallMessage(r);
        installerRan_ = installerRan_ || r.outcome == InstallReport::Outcome::Finished || r.outcome == InstallReport::Outcome::Timeout;
        // Whatever the exit code says, only detection decides.
        view_.phase = Phase::Detecting; view_.status = TR(wv2_status_detecting); Show();
        Probe p = h_.probe ? h_.probe() : Probe{};
        Log(L"WEBVIEW2 RECOVERY redetect " + (p.attempted ? Hex(p.hr) : std::wstring(L"unavailable")) + L" version=" + (p.version.empty() ? L"-" : p.version));
        view_.phase = Phase::Problem;   // so StartCreate / Problem can run
        if (p.Found()) { StartCreate(p); return; }
        // The Microsoft installer ran to its end and the runtime is still not found: report it as is
        // and stop offering the install (no repair loop).
        if (r.installerRunning) noMoreInstall_ = true;   // never a second installer next to a running one (also enforced across processes)
        const bool notDetected = r.outcome == InstallReport::Outcome::Finished;
        if (notDetected) { noMoreInstall_ = true; exhausted_ = true; }
        Problem(Stage::Probe, p.attempted ? p.hr : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), p);
    }
    void Relaunch() {
        if (!view_.canRelaunch || view_.phase != Phase::Problem) return;
        Log(L"WEBVIEW2 RECOVERY relaunch requested");
        if (h_.relaunch && h_.relaunch()) { view_.phase = Phase::Closed; return; }
        view_.canRelaunch = false; view_.note = TR(wv2_relaunch_failed_note); Show();
    }
    // Window closing: no callback is honoured afterwards; a running download is cancelled.
    void Shutdown() {
        if (view_.phase == Phase::Closed) return;
        const bool installing = view_.phase == Phase::Downloading || view_.phase == Phase::Verifying || view_.phase == Phase::Installing;
        view_.phase = Phase::Closed;
        if (!installing) return;
        // The UI phase can lag the worker (PostMessage): the host logs what the worker really did.
        // A resumed Microsoft installer is never killed; one that never ran is never resumed.
        Log(L"WEBVIEW2 RECOVERY window closed during the install request");
        if (h_.cancelInstall) h_.cancelInstall();
    }

private:
    void StartCreate(const Probe& p) {
        lastProbe_ = p;
        view_.phase = Phase::Creating;
        const std::uint64_t gen = ++generation_;
        HRESULT hr = h_.create ? h_.create(gen) : E_NOTIMPL;
        if (FAILED(hr) && view_.phase == Phase::Creating && gen == generation_) Problem(Stage::Create, hr, p);
    }
    void Problem(Stage stage, HRESULT hr, const Probe& p) {
        Overrides o = h_.overrides ? h_.overrides() : Overrides{};
        view_.diagnosis = Diagnose(stage, hr, p, o);
        view_.phase = Phase::Problem; view_.status.clear(); view_.statusDetail.clear();
        view_.canRetry = true;
        const DWORD running = view_.diagnosis.offerInstall && h_.installRunning ? h_.installRunning() : 0;
        // A runtime that the probe sees but this process cannot open after an install: one restart
        // (instead of a second install).
        view_.canRelaunch = !relaunched_ && installerRan_ && p.Found() && view_.diagnosis.cause == Cause::RuntimeInconsistent;
        view_.canInstall = view_.diagnosis.offerInstall && installs_ < MaxInstalls && !noMoreInstall_ && !view_.canRelaunch && !running;
        if (running) {
            view_.note = InstallMessage(InstallReport{InstallReport::Outcome::Busy, running});
            Log(L"WEBVIEW2 RECOVERY installer already running pid=" + std::to_wstring(running));
        }
        // Installer done, runtime still not found: no install button any more; the honest next steps.
        view_.manualPrimary = exhausted_ && view_.diagnosis.offerInstall;
        if (view_.manualPrimary) {
            view_.diagnosis.body = TR(wv2_still_missing_body);
        }
        if (view_.canRelaunch) view_.note += (view_.note.empty() ? L"" : L" ") + std::wstring(TR(wv2_restart_to_use));
        Log(std::wstring(L"WEBVIEW2 STARTUP FAILED stage=") + StageName(stage) + L" code=" + Hex(hr) + L" cause=" + CauseName(view_.diagnosis.cause) +
            L" install=" + (view_.canInstall ? L"offered" : L"no") + (o.Any() ? L" overrides=" + o.Describe() : L""));
        problemShown_ = true;
        Show();
    }
    void Show() { if (h_.show) h_.show(view_); }
    void Log(const std::wstring& s) { if (h_.log) h_.log(s); }

    Hooks h_;
    View view_;
    Probe lastProbe_;
    std::uint64_t generation_ = 0;
    int installs_ = 0;
    bool exhausted_ = false;
    bool relaunched_ = false, readyFired_ = false, installerRan_ = false, noMoreInstall_ = false, problemShown_ = false;
};

}  // namespace WebView2Runtime
