#pragma once
// The per-user « Apps & features » entry written by browser-<v>-win-x64-setup.exe (src/setup-core.cpp) and kept
// current by update-helper.exe after an update (DisplayVersion only). HKCU only, one app-owned key; nothing else in
// the registry is read or written by either.
//
// Test builds (SETUP_TESTING for the setup, UPDATE_HELPER_FAULTS for the fixture helper) redirect the key under
// HKCU\Software\browser-setup-tests\<leaf of BROWSER_SETUP_TEST_ROOT> so that fixtures never touch the real entry.
// Release builds compile no redirection at all.
#include <windows.h>

#include <string>

#include "default-browser.h"
#include "i18n.h"

namespace SetupKey {

constexpr const wchar_t* AppKeyName = L"VALXXII.browser";

inline std::wstring TestRoot() {
#if defined(SETUP_TESTING) || defined(UPDATE_HELPER_FAULTS)
    wchar_t v[1024];
    DWORD n = GetEnvironmentVariableW(L"BROWSER_SETUP_TEST_ROOT", v, 1024);
    if (n && n < 1024) return std::wstring(v, n);
#endif
    return L"";
}

// Subkey of HKEY_CURRENT_USER.
inline std::wstring UninstallKey() {
    const std::wstring test = TestRoot();
    if (!test.empty()) {
        std::wstring leaf = test.substr(test.find_last_of(L"\\/") + 1);
        return L"Software\\browser-setup-tests\\" + leaf + L"\\Uninstall\\" + AppKeyName;
    }
    return std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + AppKeyName;
}

inline bool ReadString(HKEY key, const wchar_t* name, std::wstring& out) {
    wchar_t buffer[2048];
    DWORD type = 0, size = sizeof(buffer) - sizeof(wchar_t);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size) != ERROR_SUCCESS || type != REG_SZ)
        return false;
    buffer[size / sizeof(wchar_t)] = 0;
    out = buffer;
    while (!out.empty() && out.back() == 0) out.pop_back();
    return true;
}

inline bool WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

// After an update: DisplayVersion follows `current` and DisplayName is the product name « Browser » (entries written
// before the capital B follow at the next update), only when the entry exists and names exactly this root.
inline void RefreshDisplayVersion(const std::wstring& root, const std::string& version) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, UninstallKey().c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    std::wstring location;
    if (ReadString(key, L"InstallLocation", location) && CompareStringOrdinal(location.c_str(), -1, root.c_str(), -1, TRUE) == CSTR_EQUAL)
        WriteString(key, L"DisplayVersion", std::wstring(version.begin(), version.end())) && WriteString(key, L"DisplayName", DefaultBrowser::AppName);
    RegCloseKey(key);
}

// Where Browser's default-browser registration (src/default-browser.h) lives: "" = the real HKCU\Software keys, in
// release builds. Test builds write under HKCU\Software\browser-setup-tests\<leaf>\DefaultBrowser, and NOTHING at all
// without BROWSER_SETUP_TEST_ROOT (a fixture can never touch the person's registration).
// Release builds register ONLY the person's real install: <Local AppData>\browser as Windows resolves it (the shell
// folder, which a LOCALAPPDATA environment override cannot redirect). A portable copy, or a release-built helper run
// by a test fixture on a throwaway root, never takes over Browser's registration: on 2026-11-22 an update e2e did,
// and Windows then offered « Browser » pointing at a fixture copy that opened nothing.
inline bool IsRealInstallRoot(const std::wstring& root) {
    wchar_t local[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local)) || !local[0]) return false;
    auto full = [](const std::wstring& p) { wchar_t b[32768]; const DWORD n = GetFullPathNameW(p.c_str(), 32768, b, nullptr);
        std::wstring s = n && n < 32768 ? std::wstring(b, n) : L""; while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back(); return s; };
    const std::wstring a = full(std::wstring(local) + L"\\browser"), b = full(root);
    return !a.empty() && !b.empty() && CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
inline bool DefaultBrowserPrefix(const std::wstring& root, std::wstring& prefix) {
#if defined(SETUP_TESTING) || defined(UPDATE_HELPER_FAULTS)
    (void)root;
    const std::wstring test = TestRoot();
    if (test.empty()) return false;
    prefix = L"Software\\browser-setup-tests\\" + test.substr(test.find_last_of(L"\\/") + 1) + L"\\DefaultBrowser";
    return true;
#else
    prefix.clear();
    return IsRealInstallRoot(root);
#endif
}

// After an install (setup) and after every update (helper): Browser registered with Windows as a web browser for the
// install at `root`, texts in the process language. Rewritten each time, so existing installs get it at their next
// update and a repaired or moved root is always current.
inline bool RegisterDefaultBrowser(const std::wstring& root) {
    std::wstring prefix;
    if (!DefaultBrowserPrefix(root, prefix)) return false;
    const DefaultBrowser::Texts t{TR(default_browser_description), TR(default_browser_html_type), TR(default_browser_pdf_type), TR(default_browser_url_type)};
    const bool ok = DefaultBrowser::Write(HKEY_CURRENT_USER, DefaultBrowser::For(root, t, prefix));
    if (prefix.empty()) DefaultBrowser::NotifyChanged();
    return ok;
}
// Uninstall: removed entirely, unless the registration names another copy (left to that copy).
inline void UnregisterDefaultBrowser(const std::wstring& root) {
    std::wstring prefix;
    if (!DefaultBrowserPrefix(root, prefix)) return;
    const DefaultBrowser::Layout l = DefaultBrowser::For(root, {}, prefix);
    if (DefaultBrowser::Registered(HKEY_CURRENT_USER, prefix) && !DefaultBrowser::Owns(HKEY_CURRENT_USER, l, prefix)) return;
    DefaultBrowser::Remove(HKEY_CURRENT_USER, l);
    if (prefix.empty()) DefaultBrowser::NotifyChanged();
}

}  // namespace SetupKey
