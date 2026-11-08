#pragma once
// Browser settings: model, search engines and persistence. Pure C++17 (no
// HWND, Direct2D or WebView2), so the fixture in tests/settings_test.cpp
// exercises it directly. Stored as UTF-8 `key=value` lines in `settings`,
// next to `session`; unknown keys and malformed values keep their defaults.
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include <cwctype>
#include "downloads.h" // ToUtf8 / FromUtf8

namespace BrowserSettings {
// Display order of the panel: the first three are always listed, the rest
// sit behind "Voir plus…".
enum class Engine { DuckDuckGo, Google, BraveSearch, Bing, Ecosia, Qwant };
constexpr int EngineCount = 6, PrimaryEngines = 3;
struct EngineInfo {
    const wchar_t* name;   // shown in the panel
    const char* key;       // persisted identifier
    const wchar_t* search; // query URL prefix; the percent-encoded terms are appended
};
inline const EngineInfo& Info(Engine engine) {
    static const EngineInfo engines[EngineCount] = {
        {L"DuckDuckGo", "duckduckgo", L"https://duckduckgo.com/?q="},
        {L"Google", "google", L"https://www.google.com/search?q="},
        {L"Brave Search", "brave", L"https://search.brave.com/search?q="},
        {L"Bing", "bing", L"https://www.bing.com/search?q="},
        {L"Ecosia", "ecosia", L"https://www.ecosia.org/search?q="},
        {L"Qwant", "qwant", L"https://www.qwant.com/?q="},
    };
    const int i = static_cast<int>(engine);
    return engines[i >= 0 && i < EngineCount ? i : 0];
}
inline bool Primary(Engine engine) { return static_cast<int>(engine) < PrimaryEngines; }

// WebView2 tracking prevention levels (COREWEBVIEW2_TRACKING_PREVENTION_LEVEL order).
enum class Tracking { Off, Basic, Balanced, Strict };
constexpr int TrackingCount = 4;
struct ChoiceInfo { const wchar_t* label; const wchar_t* description; const char* key; };
inline const ChoiceInfo& Info(Tracking t) {
    static const ChoiceInfo levels[TrackingCount] = {
        {L"Désactivé", L"Aucun blocage. Les sites peuvent vous suivre.", "off"},
        {L"Basique", L"Bloque les traqueurs malveillants seulement.", "basic"},
        {L"Équilibré", L"Bloque aussi les traqueurs des sites que vous ne visitez pas.", "balanced"},
        {L"Strict", L"Bloque presque tout. Certains sites peuvent mal s'afficher.", "strict"},
    };
    const int i = static_cast<int>(t); return levels[i >= 0 && i < TrackingCount ? i : 2];
}
// Background tabs go to sleep after this many minutes out of sight (0 = never).
constexpr int SleepChoices[] = {0, 30, 120, 360};
constexpr int SleepCount = 4;
inline std::wstring SleepLabel(int minutes) {
    if (minutes <= 0) return L"Jamais";
    if (minutes < 60) return L"Après " + std::to_wstring(minutes) + L" min";
    return L"Après " + std::to_wstring(minutes / 60) + L" h";
}
// Default page zoom, in percent.
constexpr int ZoomChoices[] = {80, 90, 100, 110, 125, 150};
constexpr int ZoomCount = 6;
inline std::wstring ZoomLabel(int percent) { return std::to_wstring(percent) + L" %"; }

struct State {
    Engine engine = Engine::DuckDuckGo;
    bool restoreTabs = true;       // reopen the previous session's tabs at launch
    std::wstring downloadFolder;   // empty: the system Downloads folder
    bool askWhereToSave = false;   // a save dialog for every download
    bool reduceMotion = false;     // on top of Windows' own "animation effects" switch
    bool searchSuggestions = true; // the address bar's suggestion list
    Tracking tracking = Tracking::Balanced;
    bool passwords = true;         // offer to save and fill passwords
    bool autofill = true;          // fill forms (name, address, e-mail)
    bool clearOnExit = false;      // clear browsing data when the browser closes
    int sleepMinutes = 120;        // one of SleepChoices
    int zoomPercent = 100;         // one of ZoomChoices
    bool operator==(const State& o) const {
        return engine == o.engine && restoreTabs == o.restoreTabs && downloadFolder == o.downloadFolder &&
               askWhereToSave == o.askWhereToSave && reduceMotion == o.reduceMotion && searchSuggestions == o.searchSuggestions &&
               tracking == o.tracking && passwords == o.passwords && autofill == o.autofill && clearOnExit == o.clearOnExit &&
               sleepMinutes == o.sleepMinutes && zoomPercent == o.zoomPercent;
    }
};
// What a panel interaction changed; the host applies and saves it.
enum class Change { Engine, RestoreTabs, DownloadFolder, AskWhereToSave, ReduceMotion,
                    SearchSuggestions, Tracking, Passwords, Autofill, ClearOnExit, Sleep, Zoom };

inline void Write(std::ostream& out, const State& s) {
    out << "browser-settings 1\n"
        << "engine=" << Info(s.engine).key << '\n'
        << "restore-tabs=" << (s.restoreTabs ? 1 : 0) << '\n'
        << "download-folder=" << NativeChrome::Downloads::ToUtf8(s.downloadFolder) << '\n'
        << "ask-where-to-save=" << (s.askWhereToSave ? 1 : 0) << '\n'
        << "reduce-motion=" << (s.reduceMotion ? 1 : 0) << '\n'
        << "search-suggestions=" << (s.searchSuggestions ? 1 : 0) << '\n'
        << "tracking=" << Info(s.tracking).key << '\n'
        << "passwords=" << (s.passwords ? 1 : 0) << '\n'
        << "autofill=" << (s.autofill ? 1 : 0) << '\n'
        << "clear-on-exit=" << (s.clearOnExit ? 1 : 0) << '\n'
        << "sleep-minutes=" << s.sleepMinutes << '\n'
        << "zoom-percent=" << s.zoomPercent << '\n';
}
inline State Read(std::istream& in) {
    State s; std::string line;
    if (!std::getline(in, line)) return s;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != "browser-settings 1") return s;
    auto flag = [](const std::string& v, bool& out) { if (v == "1") out = true; else if (v == "0") out = false; };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('='); if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "engine") { for (int i = 0; i < EngineCount; ++i) if (value == Info(static_cast<Engine>(i)).key) s.engine = static_cast<Engine>(i); }
        else if (key == "restore-tabs") flag(value, s.restoreTabs);
        else if (key == "ask-where-to-save") flag(value, s.askWhereToSave);
        else if (key == "reduce-motion") flag(value, s.reduceMotion);
        else if (key == "search-suggestions") flag(value, s.searchSuggestions);
        else if (key == "passwords") flag(value, s.passwords);
        else if (key == "autofill") flag(value, s.autofill);
        else if (key == "clear-on-exit") flag(value, s.clearOnExit);
        else if (key == "tracking") { for (int i = 0; i < TrackingCount; ++i) if (value == Info(static_cast<Tracking>(i)).key) s.tracking = static_cast<Tracking>(i); }
        else if (key == "sleep-minutes") { for (int m : SleepChoices) if (value == std::to_string(m)) s.sleepMinutes = m; }
        else if (key == "zoom-percent") { for (int z : ZoomChoices) if (value == std::to_string(z)) s.zoomPercent = z; }
        else if (key == "download-folder") { std::wstring folder; if (value.size() < 32768 && NativeChrome::Downloads::FromUtf8(value, folder)) s.downloadFolder = folder; }
    }
    return s;
}
// ---- Site permissions (settings › Autorisations) ----------------------------
// What a site may use. The host maps these to COREWEBVIEW2_PERMISSION_KIND.
enum class Permission { Camera, Microphone, Location, Notifications, Clipboard, Other };
enum class PermissionState { Ask, Allow, Block };
struct SitePermission { Permission kind = Permission::Other; PermissionState state = PermissionState::Ask; };
struct Site { std::wstring origin; std::vector<SitePermission> permissions; };
inline const wchar_t* PermissionName(Permission p) {
    switch (p) {
    case Permission::Camera: return L"Caméra";
    case Permission::Microphone: return L"Micro";
    case Permission::Location: return L"Position";
    case Permission::Notifications: return L"Notifications";
    case Permission::Clipboard: return L"Presse-papiers";
    default: return L"Autre";
    }
}
// The value shown in a row agrees with the noun: « Caméra · Autorisée », « Micro · Autorisé ».
inline std::wstring PermissionValue(Permission p, PermissionState s) {
    if (s == PermissionState::Ask) return L"Demander";
    const bool feminine = p == Permission::Camera || p == Permission::Location || p == Permission::Notifications;
    const bool plural = p == Permission::Notifications;
    std::wstring word = s == PermissionState::Allow ? L"Autorisé" : L"Bloqué";
    if (feminine) word += L"e";
    if (plural) word += L"s";
    return word;
}
// "Caméra, micro" / "Notifications bloquées": the list's second line for a site.
inline std::wstring SiteSummary(const Site& site) {
    std::wstring allowed, blocked;
    for (const auto& perm : site.permissions) {
        std::wstring name = PermissionName(perm.kind);
        if (perm.state == PermissionState::Allow) { if (!allowed.empty()) { name[0] = static_cast<wchar_t>(towlower(name[0])); allowed += L", "; } allowed += name; }
        else if (perm.state == PermissionState::Block) { if (!blocked.empty()) { name[0] = static_cast<wchar_t>(towlower(name[0])); blocked += L", "; } blocked += name; }
    }
    if (!blocked.empty()) blocked += L" bloqué" + std::wstring(site.permissions.size() > 1 ? L"s" : L"");
    if (allowed.empty()) return blocked;
    return blocked.empty() ? allowed : allowed + L" · " + blocked;
}

// Search URL for already percent-encoded terms.
inline std::wstring SearchUrl(Engine engine, const std::wstring& encodedTerms) { return std::wstring(Info(engine).search) + encodedTerms; }
} // namespace BrowserSettings
