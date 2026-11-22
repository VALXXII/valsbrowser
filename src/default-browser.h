#pragma once
// Browser as a web browser Windows can offer in « Paramètres › Applications › Applications par défaut »
// (standard Windows 10/11 registration, per user: HKCU only, no admin). Written by the setup (src/setup-core.cpp
// Integrate) and by the update helper after every install (src/update-helper.cpp), so existing installs get it at
// their next update; removed by the uninstaller. The browser itself only READS: whether it is the default
// (Réglages › « Navigateur par défaut ») and the Default apps page it opens. Nothing here ever changes the
// person's default: Windows lets only the person do that.
//
// Every path points at the STABLE launcher <root>\browser.exe (the update helper's copy, which starts the
// `current` version with the same arguments) and the stable <root>\browser.ico, never at a versions\<v> folder.
// The open command passes the link or file after --single-argument: whatever follows is one URL or path, never
// parsed as browser switches (src/launch-args.h).
//
// Names are app-owned, stable and unlikely to clash: the « ValXXII.browser » key (same string as the taskbar
// AppUserModelID, src/taskbar-identity.h) and the ProgIDs ValXXII.BrowserHTML / ValXXII.BrowserURL /
// ValXXII.BrowserPDF. Tests write everything under a throwaway `prefix` key (HKCU\<prefix>\Software\...), never the
// real registration: production uses prefix "".
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <functional>
#include <string>
#include <vector>

namespace DefaultBrowser {

inline constexpr const wchar_t* AppKey = L"ValXXII.browser";        // StartMenuInternet key + RegisteredApplications value
inline constexpr const wchar_t* HtmlProgId = L"ValXXII.BrowserHTML";
inline constexpr const wchar_t* UrlProgId = L"ValXXII.BrowserURL";
inline constexpr const wchar_t* PdfProgId = L"ValXXII.BrowserPDF";
inline constexpr const wchar_t* AppName = L"Browser";               // the product name Windows shows
inline constexpr const wchar_t* Company = L"ValXXII";
inline constexpr const wchar_t* AppUserModelId = L"ValXXII.browser";  // == TaskbarIdentity::AppId (tests check)
// Files Windows may offer to open with Browser. .pdf: WebView2 shows PDFs itself.
inline const std::vector<std::wstring>& HtmlExtensions() { static const std::vector<std::wstring> e{L".htm", L".html", L".shtml", L".xhtml", L".svg"}; return e; }
inline constexpr const wchar_t* PdfExtension = L".pdf";

// Interface texts written into the registration (the caller passes catalog strings in the person's language).
struct Texts { std::wstring description, htmlType, pdfType, urlType; };

struct Value { std::wstring key, name, data; };   // REG_SZ `name` ("" = the key's default value) under HKCU\key
struct Layout {
    std::vector<Value> values;
    std::vector<std::wstring> ownedKeys;                              // app-owned: deleted whole on removal
    std::vector<std::pair<std::wstring, std::wstring>> sharedValues;  // (key, value name) in keys other apps share
    std::wstring command;                                             // the browser's own open command (ownership check)
};

inline std::wstring Join(const std::wstring& prefix, const std::wstring& key) { return prefix.empty() ? key : prefix + L"\\" + key; }
inline std::wstring ClientKey(const std::wstring& prefix) { return Join(prefix, std::wstring(L"Software\\Clients\\StartMenuInternet\\") + AppKey); }
inline std::wstring CapabilitiesKey(const std::wstring& prefix) { return ClientKey(prefix) + L"\\Capabilities"; }
inline std::wstring RegisteredAppsKey(const std::wstring& prefix) { return Join(prefix, L"Software\\RegisteredApplications"); }
inline std::wstring ClassesKey(const std::wstring& prefix, const std::wstring& name) { return Join(prefix, L"Software\\Classes\\" + name); }
inline std::wstring Launcher(const std::wstring& root) { return root + L"\\browser.exe"; }
inline std::wstring Icon(const std::wstring& root) { return root + L"\\browser.ico,0"; }
// What Windows runs for a link or a file: the stable launcher, the link after --single-argument.
inline std::wstring OpenCommand(const std::wstring& root) { return L"\"" + Launcher(root) + L"\" --single-argument \"%1\""; }   // i18n-exempt: command line

// The whole registration for the install at `root` (pure: tests compare it, Write/Remove apply it).
inline Layout For(const std::wstring& root, const Texts& t, const std::wstring& prefix = L"") {
    Layout l;
    const std::wstring client = ClientKey(prefix), caps = CapabilitiesKey(prefix), icon = Icon(root);
    l.command = OpenCommand(root);
    auto add = [&](const std::wstring& key, const std::wstring& name, const std::wstring& data) { l.values.push_back({key, name, data}); };
    // Software\Clients\StartMenuInternet\<AppKey>: what « Applications par défaut » lists as a web browser.
    add(client, L"", AppName);
    add(client + L"\\DefaultIcon", L"", icon);
    add(client + L"\\shell\\open\\command", L"", L"\"" + Launcher(root) + L"\"");
    add(caps, L"ApplicationName", AppName);
    add(caps, L"ApplicationDescription", t.description);
    add(caps, L"ApplicationIcon", icon);
    for (const auto& e : HtmlExtensions()) add(caps + L"\\FileAssociations", e, HtmlProgId);
    add(caps + L"\\FileAssociations", PdfExtension, PdfProgId);
    add(caps + L"\\URLAssociations", L"http", UrlProgId);
    add(caps + L"\\URLAssociations", L"https", UrlProgId);
    add(caps + L"\\StartMenu", L"StartMenuInternet", AppKey);
    // Software\RegisteredApplications: the pointer Windows follows to the capabilities (relative to HKCU).
    add(RegisteredAppsKey(prefix), AppKey, caps);
    // The ProgIDs the associations name: icon, how to open, and the app they belong to.
    struct Prog { const wchar_t* id; const std::wstring* type; bool url; };
    for (const Prog& p : {Prog{HtmlProgId, &t.htmlType, false}, Prog{UrlProgId, &t.urlType, true}, Prog{PdfProgId, &t.pdfType, false}}) {
        const std::wstring k = ClassesKey(prefix, p.id);
        add(k, L"", *p.type);
        add(k, L"AppUserModelID", AppUserModelId);
        if (p.url) add(k, L"URL Protocol", L"");   // i18n-exempt: registry value name
        add(k + L"\\Application", L"ApplicationName", AppName);
        add(k + L"\\Application", L"ApplicationIcon", icon);
        add(k + L"\\Application", L"ApplicationCompany", Company);
        add(k + L"\\Application", L"AppUserModelID", AppUserModelId);
        add(k + L"\\DefaultIcon", L"", icon);
        add(k + L"\\shell\\open\\command", L"", l.command);
        l.ownedKeys.push_back(k);
    }
    // « Ouvrir avec » lists Browser for these files (one value each in keys every browser shares).
    for (const auto& e : HtmlExtensions()) { add(ClassesKey(prefix, e) + L"\\OpenWithProgids", HtmlProgId, L""); l.sharedValues.push_back({ClassesKey(prefix, e) + L"\\OpenWithProgids", HtmlProgId}); }
    add(ClassesKey(prefix, PdfExtension) + L"\\OpenWithProgids", PdfProgId, L"");
    l.sharedValues.push_back({ClassesKey(prefix, PdfExtension) + L"\\OpenWithProgids", PdfProgId});
    l.ownedKeys.insert(l.ownedKeys.begin(), client);
    l.sharedValues.insert(l.sharedValues.begin(), {RegisteredAppsKey(prefix), AppKey});
    return l;
}

inline bool ReadString(HKEY hive, const std::wstring& key, const std::wstring& name, std::wstring& out) {
    DWORD type = 0, size = 0;
    if (RegGetValueW(hive, key.c_str(), name.empty() ? nullptr : name.c_str(), RRF_RT_REG_SZ, &type, nullptr, &size) != ERROR_SUCCESS) return false;
    std::wstring v(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(hive, key.c_str(), name.empty() ? nullptr : name.c_str(), RRF_RT_REG_SZ, &type, v.data(), &size) != ERROR_SUCCESS) return false;
    v.resize(size / sizeof(wchar_t)); while (!v.empty() && v.back() == L'\0') v.pop_back();
    out = v; return true;
}

// Writes every value (creating keys as needed). True only when all of them were written.
inline bool Write(HKEY hive, const Layout& l) {
    bool ok = true;
    for (const auto& v : l.values) {
        HKEY k = nullptr;
        if (RegCreateKeyExW(hive, v.key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) { ok = false; continue; }
        ok = RegSetValueExW(k, v.name.empty() ? nullptr : v.name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(v.data.c_str()),
                            DWORD((v.data.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS && ok;
        RegCloseKey(k);
    }
    return ok;
}

// This install's registration is in place (its open command names this root).
inline bool Owns(HKEY hive, const Layout& l, const std::wstring& prefix = L"") {
    std::wstring command;
    return ReadString(hive, ClassesKey(prefix, UrlProgId) + L"\\shell\\open\\command", L"", command) &&
           CompareStringOrdinal(command.c_str(), -1, l.command.c_str(), -1, TRUE) == CSTR_EQUAL;
}
// Any registration under this name (the Réglages row is shown only then).
inline bool Registered(HKEY hive, const std::wstring& prefix = L"") {
    std::wstring caps;
    return ReadString(hive, RegisteredAppsKey(prefix), AppKey, caps) && ReadString(hive, CapabilitiesKey(prefix), L"ApplicationName", caps);
}

// Removes the app-owned keys and our values in shared keys; nothing else. Absent parts are not an error.
inline bool Remove(HKEY hive, const Layout& l) {
    bool ok = true;
    for (const auto& k : l.ownedKeys) { const LSTATUS s = RegDeleteTreeW(hive, k.c_str()); if (s != ERROR_SUCCESS && s != ERROR_FILE_NOT_FOUND) ok = false; else RegDeleteKeyW(hive, k.c_str()); }
    for (const auto& kv : l.sharedValues) {
        const LSTATUS s = RegDeleteKeyValueW(hive, kv.first.c_str(), kv.second.c_str());
        if (s != ERROR_SUCCESS && s != ERROR_FILE_NOT_FOUND) ok = false;
    }
    return ok;
}

// Explorer and « Applications par défaut » re-read the associations (production only: tests write elsewhere).
inline void NotifyChanged() { SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr); }

// ---- Is Browser the default? ----------------------------------------------------------------------
enum class Status { NotDefault, Default };
// The ProgID that opens `protocol` for this person now ("" unknown). Faked in tests.
using Query = std::function<std::wstring(const wchar_t* protocol)>;
inline Status StatusFrom(const Query& current) {
    if (!current) return Status::NotDefault;
    for (const wchar_t* p : {L"http", L"https"})
        if (CompareStringOrdinal(current(p).c_str(), -1, UrlProgId, -1, TRUE) != CSTR_EQUAL) return Status::NotDefault;
    return Status::Default;
}
// Windows' answer (IApplicationAssociationRegistration::QueryCurrentDefault, the effective user choice). COM must be
// initialised by the caller.
inline std::wstring SystemDefault(const wchar_t* protocol) {
    IApplicationAssociationRegistration* reg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&reg))) || !reg) return L"";
    LPWSTR progId = nullptr; std::wstring out;
    if (SUCCEEDED(reg->QueryCurrentDefault(protocol, AT_URLPROTOCOL, AL_EFFECTIVE, &progId)) && progId) { out = progId; CoTaskMemFree(progId); }
    reg->Release();
    return out;
}
// Windows' Default apps page opened on Browser (Windows 11 offers « Définir par défaut » there in one click).
inline std::wstring SettingsUri() { return std::wstring(L"ms-settings:defaultapps?registeredAppUser=") + AppKey; }

}  // namespace DefaultBrowser
