// Pure logic of the WebView2 startup recovery: classification, overrides, URL policy and the
// controller's state machine, driven by fake hooks (no loader, no network, no process).
#include "../../src/webview2-install.h"

#include <cstdio>
#include <map>

namespace WR = WebView2Runtime;
namespace WI = WebView2Install;

static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const HRESULT NotFound = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
static WR::Probe Missing() { WR::Probe p; p.attempted = true; p.hr = NotFound; return p; }
static WR::Probe Found(const wchar_t* v = L"157.0.4341.46") { WR::Probe p; p.attempted = true; p.hr = S_OK; p.version = v; return p; }

static void Diagnosis() {
    WR::Overrides none;
    WR::Overrides folder; folder.items.push_back({L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", L"variable d'environnement", true});
    WR::Overrides kind; kind.items.push_back({L"ChannelSearchKind", L"stratégie HKLM", false});
    // The reported bug: probe and creation both « not found », no override → install offered.
    auto d = WR::Diagnose(WR::Stage::Probe, NotFound, Missing(), none);
    CHECK(d.cause == WR::Cause::RuntimeMissing && d.offerInstall);
    CHECK(d.technical.find(L"0x80070002") != std::wstring::npos && d.technical.find(L"aucune") != std::wstring::npos);
    d = WR::Diagnose(WR::Stage::Callback, NotFound, Missing(), none);
    CHECK(d.cause == WR::Cause::RuntimeMissing && d.offerInstall);
    // A probe returning S_OK with a null version is « missing » too (distribution doc, Approach 2).
    WR::Probe empty; empty.attempted = true; empty.hr = S_OK;
    CHECK(WR::Diagnose(WR::Stage::Probe, S_OK, empty, none).cause == WR::Cause::RuntimeMissing);
    // Same code from LoadLibrary: a missing loader, never an install.
    d = WR::Diagnose(WR::Stage::Loader, NotFound, WR::Probe{}, none);
    CHECK(d.cause == WR::Cause::LoaderMissing && !d.offerInstall);
    d = WR::Diagnose(WR::Stage::Loader, HRESULT_FROM_WIN32(ERROR_BAD_EXE_FORMAT), WR::Probe{}, none);
    CHECK(d.cause == WR::Cause::LoaderInvalid && !d.offerInstall);
    d = WR::Diagnose(WR::Stage::Export, HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND), WR::Probe{}, none);
    CHECK(d.cause == WR::Cause::LoaderInvalid && !d.offerInstall);
    // Override pointing nowhere: explained, not « fixed » by an install.
    d = WR::Diagnose(WR::Stage::Probe, NotFound, Missing(), folder);
    CHECK(d.cause == WR::Cause::OverrideInvalid && !d.offerInstall && d.body.find(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER") != std::wstring::npos);
    // A channel search order alone does not hide an installed runtime: install still offered.
    CHECK(WR::Diagnose(WR::Stage::Probe, NotFound, Missing(), kind).offerInstall);
    // Probe sees a runtime, creation says missing/uninstalled: inconsistent discovery.
    d = WR::Diagnose(WR::Stage::Callback, HRESULT_FROM_WIN32(ERROR_PRODUCT_UNINSTALLED), Found(), none);
    CHECK(d.cause == WR::Cause::RuntimeInconsistent && d.offerInstall && d.version == L"157.0.4341.46");
    // Non-runtime errors never offer the install.
    const std::pair<HRESULT, WR::Cause> others[] = {
        {E_ACCESSDENIED, WR::Cause::ProfileUnavailable}, {HRESULT_FROM_WIN32(ERROR_FILE_EXISTS), WR::Cause::ProfileUnavailable},
        {HRESULT_FROM_WIN32(ERROR_INVALID_STATE), WR::Cause::OptionsMismatch}, {HRESULT_FROM_WIN32(ERROR_DISK_FULL), WR::Cause::DiskFull},
        {HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), WR::Cause::OverrideInvalid}, {E_FAIL, WR::Cause::RuntimeStartFailed},
        {E_OUTOFMEMORY, WR::Cause::Unknown}, {CO_E_NOTINITIALIZED, WR::Cause::Unknown}};
    for (auto& o : others) {
        auto x = WR::Diagnose(WR::Stage::Create, o.first, Found(), none);
        CHECK(x.cause == o.second && !x.offerInstall && !x.title.empty() && !x.body.empty());
    }
    // A probe failure that is not « not found » is unknown: creation decides.
    WR::Probe odd; odd.attempted = true; odd.hr = E_INVALIDARG;
    CHECK(!odd.Missing() && !odd.Found());
    CHECK(!WR::Diagnose(WR::Stage::Create, E_FAIL, odd, none).offerInstall);
}

static void OverridesRead() {
    std::map<std::wstring, bool> env;
    std::map<std::wstring, bool> policy;   // "<root>|<key>|<value>"
    auto envReader = [&](const wchar_t* n) { return env.count(n) > 0; };
    auto policyReader = [&](HKEY root, const wchar_t* key, const wchar_t* value) {
        return policy.count(std::wstring(root == HKEY_LOCAL_MACHINE ? L"HKLM|" : L"HKCU|") + key + L"|" + value) > 0;
    };
    CHECK(!WR::ReadOverrides(L"browser.exe", envReader, policyReader).Any());
    env[L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER"] = true;
    auto o = WR::ReadOverrides(L"browser.exe", envReader, policyReader);
    CHECK(o.Blocking() && o.items.size() == 1 && o.items[0].source == L"variable d'environnement");
    env.clear();
    policy[L"HKCU|Software\\Policies\\Microsoft\\Edge\\WebView2\\ReleaseChannels|browser.exe"] = true;
    o = WR::ReadOverrides(L"browser.exe", envReader, policyReader);
    CHECK(o.Blocking() && o.items[0].name == L"ReleaseChannels" && o.items[0].source == L"stratégie HKCU");
    policy.clear();
    policy[L"HKLM|Software\\Policies\\Microsoft\\Edge\\WebView2\\ChannelSearchKind|*"] = true;
    o = WR::ReadOverrides(L"browser.exe", envReader, policyReader);
    CHECK(o.Any() && !o.Blocking() && o.items[0].source == L"stratégie HKLM");
    // A policy for another executable does not apply.
    policy.clear();
    policy[L"HKLM|Software\\Policies\\Microsoft\\Edge\\WebView2\\BrowserExecutableFolder|other.exe"] = true;
    CHECK(!WR::ReadOverrides(L"browser.exe", envReader, policyReader).Any());
    // The real reader works read-only on this machine (whatever it finds).
    (void)WR::ReadOverrides(L"browser.exe");
}

static void UrlPolicy() {
    CHECK(WI::AllowedUrl(WI::BootstrapperUrl()));
    WI::HttpsUrl u;
    CHECK(WI::ParseHttpsUrl(WI::BootstrapperUrl(), u) && u.host == L"go.microsoft.com" && u.path == L"/fwlink/p/?LinkId=2124703");
    CHECK(WI::AllowedUrl(L"https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/068be74d/MicrosoftEdgeWebview2Setup.exe"));
    CHECK(WI::AllowedUrl(L"https://microsoft.com/x"));
    CHECK(WI::AllowedUrl(L"https://GO.Microsoft.COM:443/fwlink"));
    const wchar_t* bad[] = {
        L"http://go.microsoft.com/fwlink/p/?LinkId=2124703", L"https://evilmicrosoft.com/x", L"https://microsoft.com.evil.net/x",
        L"https://go.microsoft.com.evil.net/x", L"https://user@go.microsoft.com/x", L"https://go.microsoft.com:444/x",
        L"https://go.microsoft.com:443@evil.net/x", L"https://evil.net/?h=go.microsoft.com", L"https://evil.net#.microsoft.com",
        L"https://go.microsoft.com./x", L"https://.microsoft.com/x", L"https://a..microsoft.com/x", L"ftp://go.microsoft.com/x",
        L"https://go.microsoft.com/a b", L"https://go.microsoft.com\\@evil.net/", L"https://xn--microsft-ofa.com/x", L"https:///x", L""};
    for (auto b : bad) { const bool ok = WI::AllowedUrl(b); if (ok) std::printf("accepted bad url: %ls\n", b); CHECK(!ok); }
    CHECK(WI::ParseHttpsUrl(L"https://www.microsoft.com", u) && u.path == L"/");
    CHECK(WI::ParseHttpsUrl(L"https://www.microsoft.com?x=1#frag", u) && u.path == L"/?x=1");
}

// ---------------------------------------------------------------- controller with fake hooks
struct Fake {
    WR::Probe probe = Found();
    HRESULT prepare = S_OK; WR::Stage prepareStage = WR::Stage::Loader;
    HRESULT createImmediate = S_OK;
    std::vector<std::uint64_t> pending;   // creations awaiting their callback
    int creates = 0, probes = 0, installs = 0, cancels = 0, ready = 0, relaunches = 0, shows = 0;
    bool installStarts = true, relaunchStarts = true;
    WR::View last;
    WR::Overrides overrides;
    std::vector<std::wstring> logs;
    WR::Hooks Hooks() {
        WR::Hooks h;
        h.prepare = [this](WR::Stage& s) { s = prepareStage; return prepare; };
        h.probe = [this] { ++probes; return probe; };
        h.create = [this](std::uint64_t g) { ++creates; if (SUCCEEDED(createImmediate)) pending.push_back(g); return createImmediate; };
        h.overrides = [this] { return overrides; };
        h.startInstall = [this] { ++installs; return installStarts; };
        h.cancelInstall = [this] { ++cancels; };
        h.show = [this](const WR::View& v) { ++shows; last = v; };
        h.ready = [this] { ++ready; };
        h.relaunch = [this] { ++relaunches; return relaunchStarts; };
        h.log = [this](const std::wstring& s) { logs.push_back(s); };
        return h;
    }
};
static WR::InstallReport Finished(DWORD code) { WR::InstallReport r; r.outcome = WR::InstallReport::Outcome::Finished; r.code = code; return r; }

static void ControllerNormalStart() {
    Fake f;
    WR::Controller c(f.Hooks());
    c.Attempt();
    CHECK(f.probes == 1 && f.creates == 1 && f.shows == 0 && c.phase() == WR::Phase::Creating);
    CHECK(c.Created(f.pending.back(), S_OK));
    CHECK(f.ready == 1 && f.shows == 0 && f.installs == 0 && c.phase() == WR::Phase::Recovered);
    // A duplicate or late callback restores nothing twice.
    CHECK(!c.Created(f.pending.back(), S_OK) && f.ready == 1);
    c.Attempt();   // no-op once recovered
    CHECK(f.creates == 1);
}

static void ControllerMissingInstallRecover() {
    Fake f; f.probe = Missing();
    WR::Controller c(f.Hooks());
    c.Attempt();
    CHECK(f.creates == 0 && c.phase() == WR::Phase::Problem && f.last.canInstall && f.last.diagnosis.cause == WR::Cause::RuntimeMissing);
    // Double click: one installer.
    CHECK(c.Install());
    CHECK(!c.Install());
    CHECK(f.installs == 1 && c.phase() == WR::Phase::Downloading && !f.last.canInstall && !f.last.canRetry);
    c.Attempt();   // « Réessayer » is locked while busy
    CHECK(f.creates == 0);
    c.Progress(WR::Phase::Downloading, 50, 100);
    CHECK(f.last.status.find(L"50 %") != std::wstring::npos);
    c.Progress(WR::Phase::Verifying);
    c.Progress(WR::Phase::Installing);
    CHECK(c.phase() == WR::Phase::Installing);
    // Installer exits 0 and the runtime is now found: one creation, recovery only after it succeeds.
    f.probe = Found();
    c.InstallFinished(Finished(0));
    CHECK(f.creates == 1 && c.phase() == WR::Phase::Creating && f.ready == 0);
    CHECK(c.Created(f.pending.back(), S_OK));
    CHECK(f.ready == 1 && c.phase() == WR::Phase::Recovered && f.last.phase == WR::Phase::Recovered);
    c.InstallFinished(Finished(0));   // late report ignored
    CHECK(f.creates == 1 && f.ready == 1);
}

static void ControllerInstalledButNotDetected() {
    Fake f; f.probe = Missing();
    WR::Controller c(f.Hooks());
    c.Attempt();
    c.Install();
    c.Progress(WR::Phase::Installing);
    c.InstallFinished(Finished(0));   // « success » but still missing
    CHECK(c.phase() == WR::Phase::Problem && f.creates == 0 && !f.last.canInstall && f.last.canRetry);
    // No disabled install button left behind: the Microsoft page becomes the main action, and the
    // body no longer invites to install from here.
    CHECK(f.last.manualPrimary && f.last.diagnosis.body.find(L"reste introuvable") != std::wstring::npos);
    CHECK(f.last.diagnosis.body.find(L"Vous pouvez l'installer") == std::wstring::npos && f.last.note.find(L"code 0") != std::wstring::npos);
    CHECK(!c.Install() && f.installs == 1);   // no repair loop
    // An « already installed » non-zero exit with nothing found: shown as is, no loop either.
    Fake g; g.probe = Missing();
    WR::Controller d(g.Hooks());
    d.Attempt(); d.Install(); d.Progress(WR::Phase::Installing);
    d.InstallFinished(Finished(0x80040c01));
    CHECK(g.last.note.find(L"0x80040C01") != std::wstring::npos && !g.last.canInstall);
    // A non-zero exit but the runtime IS found afterwards: detection wins.
    Fake h; h.probe = Missing();
    WR::Controller e(h.Hooks());
    e.Attempt(); e.Install(); e.Progress(WR::Phase::Installing);
    h.probe = Found();
    e.InstallFinished(Finished(1));
    CHECK(h.creates == 1 && e.phase() == WR::Phase::Creating);
}

static void ControllerInstallFailures() {
    using O = WR::InstallReport::Outcome;
    for (O o : {O::Network, O::Http, O::Rejected, O::TooLarge, O::Storage, O::Trust, O::Publisher, O::Unexpected, O::Launch, O::Elevation}) {
        Fake f; f.probe = Missing();
        WR::Controller c(f.Hooks());
        c.Attempt(); c.Install();
        WR::InstallReport r; r.outcome = o; r.code = 404;
        c.InstallFinished(r);
        CHECK(c.phase() == WR::Phase::Problem && f.last.canRetry && !f.last.note.empty());
        CHECK(f.last.canInstall);   // second user attempt allowed (bounded)
        CHECK(c.Install());
        c.InstallFinished(r);
        CHECK(!f.last.canInstall && !c.Install() && f.installs == WR::Controller::MaxInstalls);
    }
    // The installer could not start at all.
    Fake f; f.probe = Missing(); f.installStarts = false;
    WR::Controller c(f.Hooks());
    c.Attempt();
    CHECK(!c.Install() && c.phase() == WR::Phase::Problem && !f.last.note.empty());
}

static void ControllerCallbackRaces() {
    // Immediate failure vs callback failure.
    Fake f; f.createImmediate = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    WR::Controller c(f.Hooks());
    c.Attempt();
    CHECK(c.phase() == WR::Phase::Problem && f.last.diagnosis.stage == WR::Stage::Create && f.last.diagnosis.cause == WR::Cause::OptionsMismatch && !f.last.canInstall);
    f.createImmediate = S_OK;
    c.Attempt();   // Réessayer
    CHECK(c.phase() == WR::Phase::Creating && f.creates == 2);
    CHECK(!c.Created(f.pending.back(), E_ACCESSDENIED));
    CHECK(c.phase() == WR::Phase::Problem && f.last.diagnosis.stage == WR::Stage::Callback && f.last.diagnosis.cause == WR::Cause::ProfileUnavailable);
    // A stale generation never restores: retry creates gen N+1, the old callback arrives late.
    c.Attempt();
    const auto current = f.pending.back();
    CHECK(!c.Accepts(current - 1) && !c.Created(current - 1, S_OK) && f.ready == 0);
    CHECK(c.Accepts(current) && c.Created(current, S_OK) && f.ready == 1);
    // Window closed while creating: the callback is ignored (no HWND use).
    Fake g;
    WR::Controller d(g.Hooks());
    d.Attempt();
    d.Shutdown();
    CHECK(!d.Accepts(g.pending.back()) && !d.Created(g.pending.back(), S_OK) && g.ready == 0 && g.cancels == 0);
    // Closed during the download: cancelled, late report ignored.
    Fake h; h.probe = Missing();
    WR::Controller e(h.Hooks());
    e.Attempt(); e.Install();
    e.Shutdown();
    CHECK(h.cancels == 1 && e.phase() == WR::Phase::Closed);
    e.InstallFinished(Finished(0));
    CHECK(h.creates == 0 && h.ready == 0);
    e.Attempt(); e.Install();
    CHECK(h.creates == 0 && h.installs == 1);
}

static void ControllerLoaderAndOverride() {
    Fake f; f.prepare = HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND); f.prepareStage = WR::Stage::Loader;
    WR::Controller c(f.Hooks());
    c.Attempt();
    CHECK(f.probes == 0 && f.creates == 0 && f.last.diagnosis.cause == WR::Cause::LoaderMissing && !f.last.canInstall && f.last.canRetry);
    CHECK(!c.Install() && f.installs == 0);
    f.prepare = S_OK;   // loader restored by a reinstall: Réessayer works
    c.Attempt();
    CHECK(c.phase() == WR::Phase::Creating && c.Created(f.pending.back(), S_OK) && f.ready == 1);
    Fake g; g.probe = Missing(); g.overrides.items.push_back({L"BrowserExecutableFolder", L"stratégie HKLM", true});
    WR::Controller d(g.Hooks());
    d.Attempt();
    CHECK(g.last.diagnosis.cause == WR::Cause::OverrideInvalid && !g.last.canInstall && !d.Install());
    // Old loader without the probe export: creation is still attempted and classified.
    Fake h; h.probe = WR::Probe{};
    WR::Controller e(h.Hooks());
    e.Attempt();
    CHECK(h.creates == 1);
    h.probe = WR::Probe{};
    e.Created(h.pending.back(), NotFound);
    CHECK(h.last.diagnosis.cause == WR::Cause::RuntimeMissing && h.last.canInstall);
}

static void ControllerRelaunchBound() {
    // Installed, the probe sees it, but this process still cannot open it: one restart offered.
    Fake f; f.probe = Missing();
    WR::Controller c(f.Hooks());
    c.Attempt(); c.Install(); c.Progress(WR::Phase::Installing);
    f.probe = Found();
    c.InstallFinished(Finished(0));
    CHECK(!c.Created(f.pending.back(), NotFound));
    CHECK(f.last.diagnosis.cause == WR::Cause::RuntimeInconsistent && f.last.canRelaunch && !f.last.canInstall && f.ready == 0);
    c.Relaunch();
    CHECK(f.relaunches == 1 && c.phase() == WR::Phase::Closed);
    c.Relaunch();
    CHECK(f.relaunches == 1);
    // The relaunched instance never offers another restart.
    Fake g; g.probe = Missing();
    WR::Controller d(g.Hooks(), true);
    d.Attempt(); d.Install(); d.Progress(WR::Phase::Installing);
    g.probe = Found();
    d.InstallFinished(Finished(0));
    d.Created(g.pending.back(), NotFound);
    CHECK(!g.last.canRelaunch);
    d.Relaunch();
    CHECK(g.relaunches == 0);
    // Without an install in this process there is nothing a restart would change.
    Fake h; h.createImmediate = NotFound;
    WR::Controller e(h.Hooks());
    e.Attempt();
    CHECK(!h.last.canRelaunch && h.last.canInstall);
    // A failed relaunch says so and does not close.
    Fake k; k.probe = Missing(); k.relaunchStarts = false;
    WR::Controller m(k.Hooks());
    m.Attempt(); m.Install(); m.Progress(WR::Phase::Installing);
    k.probe = Found();
    m.InstallFinished(Finished(0));
    m.Created(k.pending.back(), NotFound);
    m.Relaunch();
    CHECK(k.relaunches == 1 && m.phase() == WR::Phase::Problem && !k.last.canRelaunch && !k.last.note.empty());
}

static void LogsStayFactual() {
    Fake f; f.probe = Missing();
    WR::Controller c(f.Hooks());
    c.Attempt();
    bool sawFailure = false;
    // The « installed but not found » path logs the refusal of a new install.
    Fake g; g.probe = Missing();
    WR::Controller d(g.Hooks());
    d.Attempt(); d.Install(); d.Progress(WR::Phase::Installing); d.InstallFinished(Finished(0));
    CHECK(g.logs.back().find(L"install=no") != std::wstring::npos);
    // Closing during the install request: the controller no longer claims what the worker did
    // (its UI phase can lag); it cancels and the host logs the worker's real outcome.
    Fake h; h.probe = Missing();
    WR::Controller e(h.Hooks());
    e.Attempt(); e.Install(); e.Progress(WR::Phase::Installing); e.Shutdown();
    CHECK(h.logs.back().find(L"closed during the install request") != std::wstring::npos && h.cancels == 1);
    for (auto& l : f.logs) {
        if (l.find(L"WEBVIEW2 STARTUP FAILED") == 0 && l.find(L"0x80070002") != std::wstring::npos && l.find(L"runtime-missing") != std::wstring::npos) sawFailure = true;
        CHECK(l.find(L"http") == std::wstring::npos && l.find(L"\\") == std::wstring::npos);   // no URL, no path
    }
    CHECK(sawFailure);
}

static void ControllerInstallRunningElsewhere() {
    // Another browser process (or a previous one that quit) has a live installer: no second one.
    Fake f; f.probe = Missing();
    DWORD running = 4242;
    WR::Hooks h = f.Hooks(); h.installRunning = [&] { return running; };
    WR::Controller c(h);
    c.Attempt();
    CHECK(c.phase() == WR::Phase::Problem && !f.last.canInstall && f.last.canRetry && f.last.note.find(L"déjà en cours") != std::wstring::npos);
    CHECK(!c.Install() && f.installs == 0);
    running = 0;   // it finished: Réessayer re-evaluates
    c.Attempt();
    CHECK(f.last.canInstall && c.Install() && f.installs == 1);
    // The worker's own claim found one (race with another process): reported, no loop.
    WR::InstallReport busy; busy.outcome = WR::InstallReport::Outcome::Busy; busy.code = 99;
    running = 99;
    c.InstallFinished(busy);
    CHECK(c.phase() == WR::Phase::Problem && !f.last.canInstall && f.last.note.find(L"déjà en cours") != std::wstring::npos);
    // Never queried for non-runtime errors.
    Fake g; g.createImmediate = E_ACCESSDENIED; int asked = 0;
    WR::Hooks gh = g.Hooks(); gh.installRunning = [&] { ++asked; return DWORD(1); };
    WR::Controller d(gh);
    d.Attempt();
    CHECK(asked == 0 && !g.last.canInstall);
}

static void Messages() {
    using O = WR::InstallReport::Outcome;
    WR::InstallReport r; r.outcome = O::Cancelled;
    CHECK(WR::InstallMessage(r).find(L"rien n'a été installé") != std::wstring::npos);
    r.outcome = O::Detached; r.installerRunning = true;
    CHECK(WR::InstallMessage(r).find(L"continue en arrière-plan") != std::wstring::npos);
    r.outcome = O::DownloadTimeout;
    CHECK(WR::InstallMessage(r).find(L"5 minutes") != std::wstring::npos);
    // The status line says what Quitter really does in each phase.
    Fake f; f.probe = Missing();
    WR::Controller c(f.Hooks());
    c.Attempt(); c.Install();
    CHECK(f.last.statusDetail.find(L"annule le téléchargement") != std::wstring::npos);
    c.Progress(WR::Phase::Installing);
    CHECK(f.last.statusDetail.find(L"continue même si vous quittez") != std::wstring::npos);
    // Detached/timeout: never a second installer from this process either.
    r.outcome = O::Detached;
    c.InstallFinished(r);
    CHECK(!f.last.canInstall);
}

int main() {
    Diagnosis();
    OverridesRead();
    UrlPolicy();
    ControllerNormalStart();
    ControllerMissingInstallRecover();
    ControllerInstalledButNotDetected();
    ControllerInstallFailures();
    ControllerCallbackRaces();
    ControllerLoaderAndOverride();
    ControllerRelaunchBound();
    LogsStayFactual();
    ControllerInstallRunningElsewhere();
    Messages();
    std::printf("runtime_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
