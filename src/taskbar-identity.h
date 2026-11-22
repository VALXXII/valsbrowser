#pragma once
// One taskbar identity for every installed version.
//
// Without an explicit AppUserModelID, Windows groups taskbar buttons and pins by executable path. An
// installed browser runs from <root>\versions\<v>\browser.exe, so its windows did not group with the Start
// Menu shortcut (to the stable launcher <root>\browser.exe), and pinning a running window pinned
// versions\<v>\browser.exe, a folder the next update replaces: the pin went stale after every update.
//
// Installed copies therefore declare AppId for the process and every window, with the stable launcher as
// the relaunch command (what pinning a window records), and our shortcuts to that launcher carry the same
// AppId. Portable and dev copies declare nothing: Windows keeps grouping them by their own path, apart from
// an installed copy.
// https://learn.microsoft.com/en-us/windows/win32/shell/appids
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>
#include <string>

namespace TaskbarIdentity {

// The identifier never changes (pins, grouping and the default-browser registration name it); only the name the
// person sees is « Browser », capital B (the executable, folders and keys keep « browser »).
inline constexpr const wchar_t* AppId = L"ValXXII.browser";
inline constexpr const wchar_t* ProductName = L"Browser";
inline constexpr const wchar_t* ShortcutName = L"Browser.lnk";
// The window's relaunch name is the launcher's string resource 1 (« Browser », src/update-helper.rc), never the plain
// text: window properties are stored as global atoms, which ignore case, so a plain « Browser » reads back as
// « browser » whenever any process already holds that atom (the previous version did).
inline constexpr int NameStringId = 1;
inline std::wstring NameResource(const std::wstring& launcher) { return L"@" + launcher + L",-" + std::to_wstring(NameStringId); }

struct Identity {
    std::wstring aumid, launcher, relaunch, icon, name;
    bool active() const { return !aumid.empty(); }
};

inline std::wstring Parent(const std::wstring& p) { const size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"" : p.substr(0, s); }

// `installed`: UpdateCore::ResolveDataPaths(exe).installed, i.e. exe is <root>\versions\<v>\browser.exe.
inline Identity ForInstalled(const std::wstring& exe, bool installed) {
    Identity id;
    if (!installed) return id;
    const std::wstring root = Parent(Parent(Parent(exe)));
    if (root.empty()) return id;
    id.aumid = AppId;
    id.launcher = root + L"\\browser.exe";
    id.relaunch = L"\"" + id.launcher + L"\"";
    id.icon = root + L"\\browser.ico,0";
    id.name = NameResource(id.launcher);
    return id;
}

// Before the first window is created.
inline bool ApplyToProcess(const Identity& id) { return id.active() && SUCCEEDED(SetCurrentProcessExplicitAppUserModelID(id.aumid.c_str())); }

inline bool SetString(IPropertyStore* store, const PROPERTYKEY& key, const std::wstring& value) {
    PROPVARIANT v; if (FAILED(InitPropVariantFromString(value.c_str(), &v))) return false;
    const HRESULT hr = store->SetValue(key, v); PropVariantClear(&v); return SUCCEEDED(hr);
}
inline std::wstring GetString(IPropertyStore* store, const PROPERTYKEY& key) {
    PROPVARIANT v; PropVariantInit(&v); std::wstring out;
    if (SUCCEEDED(store->GetValue(key, &v)) && v.vt == VT_LPWSTR && v.pwszVal) out = v.pwszVal;
    PropVariantClear(&v); return out;
}

// The window's own identity and how to relaunch it once pinned (the launcher, never a version folder).
inline bool ApplyToWindow(HWND window, const Identity& id) {
    if (!id.active() || !window) return false;
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) return false;
    // The relaunch properties first: Windows reads them when the AppUserModelID is set.
    const bool ok = SetString(store, PKEY_AppUserModel_RelaunchCommand, id.relaunch) &&
                    SetString(store, PKEY_AppUserModel_RelaunchDisplayNameResource, id.name) &&
                    SetString(store, PKEY_AppUserModel_RelaunchIconResource, id.icon) &&
                    SetString(store, PKEY_AppUserModel_ID, id.aumid);
    store->Release();
    return ok;
}
inline std::wstring WindowValue(HWND window, const PROPERTYKEY& key) {
    IPropertyStore* store = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&store)))) return L"";
    const std::wstring v = GetString(store, key); store->Release(); return v;
}

// ---- shortcuts (COM initialised by the caller) ----
struct Link {
    IShellLinkW* link = nullptr; IPersistFile* file = nullptr;
    explicit Link(const std::wstring& lnk, bool load) {
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) { link = nullptr; return; }
        if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) { file = nullptr; return; }
        if (load && FAILED(file->Load(lnk.c_str(), STGM_READWRITE))) { file->Release(); file = nullptr; }
    }
    ~Link() { if (file) file->Release(); if (link) link->Release(); }
    bool ok() const { return link && file; }
    std::wstring Target() const { wchar_t p[MAX_PATH * 4]{}; return ok() && SUCCEEDED(link->GetPath(p, MAX_PATH * 4, nullptr, SLGP_RAWPATH)) ? p : L""; }
};

// Our launcher shortcut in a Start Menu / desktop folder (the name setup-core.cpp writes). One place, so the path the
// browser stamps at start (main.cpp StampTaskbarIdentity) is the one tests check.
inline std::wstring LauncherShortcut(const std::wstring& folder) { return folder + L"\\" + ShortcutName; }

// The name the file system holds for `path` (its real case), "" when absent.
inline std::wstring OnDiskName(const std::wstring& path) {
    WIN32_FIND_DATAW fd; HANDLE f = FindFirstFileW(path.c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return L"";
    FindClose(f); return fd.cFileName;
}

inline std::wstring ShortcutId(const std::wstring& lnk) {
    Link l(lnk, true); if (!l.ok()) return L"";
    IPropertyStore* store = nullptr; if (FAILED(l.link->QueryInterface(IID_PPV_ARGS(&store)))) return L"";
    const std::wstring v = GetString(store, PKEY_AppUserModel_ID); store->Release(); return v;
}

// A shortcut to `target`, carrying `id` when given (setup's Start Menu and desktop links).
inline bool WriteShortcut(const std::wstring& lnk, const std::wstring& target, const Identity* id = nullptr, const std::wstring& workingDir = L"",
                          const std::wstring& iconFile = L"", const std::wstring& description = L"") {
    Link l(lnk, false); if (!l.ok()) return false;
    l.link->SetPath(target.c_str());
    if (!workingDir.empty()) l.link->SetWorkingDirectory(workingDir.c_str());
    if (!iconFile.empty()) l.link->SetIconLocation(iconFile.c_str(), 0);
    if (!description.empty()) l.link->SetDescription(description.c_str());
    if (id && id->active()) {
        IPropertyStore* store = nullptr; if (FAILED(l.link->QueryInterface(IID_PPV_ARGS(&store)))) return false;
        const bool set = SetString(store, PKEY_AppUserModel_ID, id->aumid) && SUCCEEDED(store->Commit());
        store->Release(); if (!set) return false;
    }
    return SUCCEEDED(l.file->Save(lnk.c_str(), TRUE));
}

enum class Stamp { Stamped, AlreadyStamped, NotOurs, Missing, Failed };
// Adds the identity to an existing shortcut whose target is exactly our launcher. Anything else (another
// copy, a version folder, a stranger's link) is never touched. Shortcuts from earlier setups get it once.
inline Stamp StampShortcut(const std::wstring& lnk, const Identity& id) {
    if (!id.active()) return Stamp::NotOurs;
    if (GetFileAttributesW(lnk.c_str()) == INVALID_FILE_ATTRIBUTES) return Stamp::Missing;
    Link l(lnk, true); if (!l.ok()) return Stamp::Failed;
    if (CompareStringOrdinal(l.Target().c_str(), -1, id.launcher.c_str(), -1, TRUE) != CSTR_EQUAL) return Stamp::NotOurs;
    IPropertyStore* store = nullptr; if (FAILED(l.link->QueryInterface(IID_PPV_ARGS(&store)))) return Stamp::Failed;
    if (GetString(store, PKEY_AppUserModel_ID) == id.aumid) { store->Release(); return Stamp::AlreadyStamped; }
    const bool set = SetString(store, PKEY_AppUserModel_ID, id.aumid) && SUCCEEDED(store->Commit());
    store->Release();
    return set && SUCCEEDED(l.file->Save(lnk.c_str(), TRUE)) ? Stamp::Stamped : Stamp::Failed;
}

enum class Rename { Renamed, AlreadyNamed, NotOurs, Missing, Failed };
// Setups before the « Browser » name wrote <folder>\browser.lnk. Our own one (target exactly `launcher`) is renamed
// in place to Browser.lnk: a case-only rename of the SAME file (same path for Windows, same target, same identity,
// same contents), so the Start Menu shows « Browser » and nothing pinned from it is lost. Anything else is left.
inline Rename RenameShortcut(const std::wstring& folder, const std::wstring& launcher) {
    const std::wstring lnk = LauncherShortcut(folder), onDisk = OnDiskName(lnk);
    if (onDisk.empty()) return Rename::Missing;
    if (onDisk == ShortcutName) return Rename::AlreadyNamed;
    {
        Link l(lnk, true);
        if (!l.ok()) return Rename::Failed;
        if (CompareStringOrdinal(l.Target().c_str(), -1, launcher.c_str(), -1, TRUE) != CSTR_EQUAL) return Rename::NotOurs;
    }   // released before the rename
    const std::wstring from = folder + L"\\" + onDisk;
    if (!MoveFileExW(from.c_str(), lnk.c_str(), 0)) return Rename::Failed;
    SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, from.c_str(), lnk.c_str());
    return Rename::Renamed;
}

}  // namespace TaskbarIdentity
