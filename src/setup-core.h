#pragma once
// browser-<version>-win-x64-setup.exe: logic without UI (design/windows-setup-plan.md).
//
// Per-user install into the existing updater layout (%LOCALAPPDATA%\browser, src/update-core.h ResolveDataPaths):
// the embedded .lsrupd is copied out and staged by UpdatePackage (the updater's validator), then the payload's own
// update-helper.exe --install performs the install (versions\<v>, stable launcher, bin\ bridge, atomic `current`,
// ack, rollback). This file adds only what an update never needed: inspection of an existing install, shortcuts,
// <root>\uninstall.exe, the HKCU uninstall entry, and the uninstall itself. No admin, no HKLM, no service, no
// autostart, no default browser, no agent configuration, no import, no download.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "setup-payload.h"

namespace SetupCore {

struct Env {
    std::wstring root;          // %LOCALAPPDATA%\browser
    std::wstring programs;      // Start Menu\Programs (per user)
    std::wstring desktop;       // per-user Desktop
    std::wstring uninstallKey;  // subkey of HKCU (src/setup-uninstall-key.h)
    std::wstring temp;          // where the uninstaller re-runs itself
};
// Production: known folders + UpdateCore::DefaultInstallRoot(). SETUP_TESTING builds only: BROWSER_SETUP_TEST_ROOT=<T>
// redirects programs/desktop/temp under <T> and the key (the root follows LOCALAPPDATA like the browser's own).
Env DefaultEnv();

enum class Kind { Fresh, Update, Repair, Newer, Unsafe };
struct Inspection {
    Kind kind = Kind::Fresh;
    std::string installed;  // launchable `current`, if any
};
Inspection Inspect(const Env& env, const std::string& version);

// The installed browser (or one of its profiles) is in use: profile lockfiles held, or versions\<current>\browser.exe
// mapped by a running process. Read-only probes; nothing is closed or killed.
bool BrowserRunning(const Env& env);

struct Options {
    bool desktop = false;  // explicit opt-in
    bool launch = true;
};

enum class Error {
    None, Damaged, Unsafe, Newer, Running, Disk, Cancelled, Helper, StartFailed, Integration,
    Busy,  // another install/uninstall of the same root holds <root>\install.lock
};
struct Result {
    Error error = Error::None;
    std::string status;  // updates\last-result status: installed | unconfirmed | failed
    std::string detail;  // log text (reason / Win32 step)
    bool startMenuForeign = false;  // a « browser » Start Menu link of another copy exists: left untouched
    bool Ok() const { return error == Error::None; }
};

enum class Step { Prepare, Install, Integrate };
using Progress = std::function<void(Step, double)>;

// Full install from this setup file (`self`). Before the helper runs: cancellable, nothing outside updates\ written.
Result Install(const Env& env, const std::wstring& self, const SetupPayload::Layout& layout, const Options& options,
               const std::atomic<bool>* cancel, const Progress& progress);

// Shortcuts, uninstaller and uninstall entry for the installed `version` (also used to repair them).
Result Integrate(const Env& env, const std::wstring& self, const SetupPayload::Layout& layout, const Options& options);

// Path of an existing shortcut's target ("" if none / unreadable).
std::wstring ShortcutTarget(const std::wstring& lnk);

struct UninstallReport {
    Error error = Error::None;
    std::string detail;
    bool dataKept = true, leftovers = false;  // leftovers: a file still in use (e.g. bridge run by an agent) stays
};
UninstallReport Uninstall(const Env& env, bool removeData);

// Shared small helpers (also used by the UI and tests).
std::string LastResultField(const Env& env, const std::string& key);
bool ReparseFree(const std::wstring& path);  // path absent, or a plain (non reparse) object

}  // namespace SetupCore
