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
// sit behind "Voir plus…". Custom is last: its URL template is State::customSearch.
enum class Engine { DuckDuckGo, Google, BraveSearch, Bing, Ecosia, Qwant, Custom };
constexpr int EngineCount = 7, PrimaryEngines = 3;
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
        {L"Personnalisé", "custom", L""},
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
// What a new tab shows.
enum class NewTabPage { Blank, FrequentSites, Custom };
constexpr int NewTabPageCount = 3;
inline const ChoiceInfo& Info(NewTabPage p) {
    static const ChoiceInfo pages[NewTabPageCount] = {
        {L"Vide", L"Une page sombre, sans rien.", "blank"},
        {L"Sites fréquents", L"Vos sites les plus visités, en raccourcis.", "frequent"},
        {L"Page personnalisée", L"L'adresse de votre choix.", "custom"},
    };
    const int i = static_cast<int>(p); return pages[i >= 0 && i < NewTabPageCount ? i : 0];
}
// Where a new tab (Ctrl+T, "+") opens.
enum class TabPosition { End, NextToActive };
constexpr int TabPositionCount = 2;
inline const ChoiceInfo& Info(TabPosition p) {
    static const ChoiceInfo positions[TabPositionCount] = {
        {L"À la fin", L"Après tous les onglets ouverts.", "end"},
        {L"À côté", L"Juste après l'onglet actif.", "next"},
    };
    const int i = static_cast<int>(p); return positions[i >= 0 && i < TabPositionCount ? i : 0];
}
// Media autoplay (engine policy: applied when the browser starts).
enum class Autoplay { Muted, Allowed };
constexpr int AutoplayCount = 2;
inline const ChoiceInfo& Info(Autoplay a) {
    static const ChoiceInfo choices[AutoplayCount] = {
        {L"Sans son", L"Les vidéos démarrent muettes jusqu'à un clic.", "muted"},
        {L"Autorisée", L"Les vidéos peuvent démarrer avec le son.", "allowed"},
    };
    const int i = static_cast<int>(a); return choices[i >= 0 && i < AutoplayCount ? i : 0];
}
inline std::wstring TrimText(const std::wstring& v) {
    size_t a = 0, b = v.size();
    while (a < b && iswspace(v[a])) ++a;
    while (b > a && iswspace(v[b - 1])) --b;
    return v.substr(a, b - a);
}
inline bool StartsWithNoCase(const std::wstring& v, const wchar_t* prefix) {
    for (size_t i = 0; prefix[i]; ++i) if (i >= v.size() || towlower(v[i]) != towlower(prefix[i])) return false;
    return true;
}
// A custom search engine: an http(s) URL with %s where the query goes. Returns
// the cleaned template, or empty when it cannot be used.
inline std::wstring SearchTemplate(const std::wstring& raw) {
    std::wstring v = TrimText(raw);
    if (v.empty() || v.size() > 2048 || v.find(L"%s") == std::wstring::npos) return L"";
    if (!StartsWithNoCase(v, L"https://") && !StartsWithNoCase(v, L"http://")) {
        if (v.find(L"://") != std::wstring::npos) return L"";
        v = L"https://" + v;
    }
    for (wchar_t c : v) if (iswspace(c) || c < 0x20) return L"";
    const size_t host = v.find(L"://") + 3;
    if (host >= v.size() || v[host] == L'/' || v[host] == L'%') return L"";
    return v;
}
// The custom new tab page: an http(s) address ("exemple.fr" gets https://). Empty when invalid.
inline std::wstring HomepageUrl(const std::wstring& raw) {
    std::wstring v = TrimText(raw);
    if (v.empty() || v.size() > 2048) return L"";
    if (!StartsWithNoCase(v, L"https://") && !StartsWithNoCase(v, L"http://")) {
        if (v.find(L"://") != std::wstring::npos) return L"";
        v = L"https://" + v;
    }
    for (wchar_t c : v) if (iswspace(c) || c < 0x20) return L"";
    const size_t host = v.find(L"://") + 3; size_t end = v.find_first_of(L"/?#", host); if (end == std::wstring::npos) end = v.size();
    const std::wstring name = v.substr(host, end - host);
    if (name.empty() || (name.find(L'.') == std::wstring::npos && !StartsWithNoCase(name, L"localhost"))) return L"";
    return v;
}
// Bookmarks bar under the toolbar: shown once it holds a bookmark (default), always, or never.
enum class BookmarksBar { Auto, Always, Never };
constexpr int BookmarksBarCount = 3;
inline const ChoiceInfo& Info(BookmarksBar b) {
    static const ChoiceInfo choices[BookmarksBarCount] = {
        {L"Si elle contient des favoris", L"La barre apparaît dès le premier favori.", "auto"},
        {L"Toujours", L"La barre reste affichée, même vide.", "always"},
        {L"Jamais", L"Les favoris restent dans le menu et le tiroir Favoris.", "never"},
    };
    const int i = static_cast<int>(b); return choices[i >= 0 && i < BookmarksBarCount ? i : 0];
}
inline const wchar_t* ShortLabel(BookmarksBar b) { return b == BookmarksBar::Always ? L"Toujours" : b == BookmarksBar::Never ? L"Jamais" : L"Automatique"; }
// Accent colour (#RRGGBB): groundwork for a future choice; the caret reads it today.
constexpr std::uint32_t DefaultAccent = 0x3E67FF;
inline std::string AccentText(std::uint32_t rgb) { char t[8]; const char* hex = "0123456789ABCDEF"; t[0] = '#'; for (int i = 0; i < 6; ++i) t[1 + i] = hex[(rgb >> (20 - 4 * i)) & 15]; t[7] = 0; return t; }
inline bool ParseAccent(const std::string& v, std::uint32_t& out) {
    if (v.size() != 7 || v[0] != '#') return false;
    std::uint32_t x = 0;
    for (size_t i = 1; i < 7; ++i) { const char c = v[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (d < 0) return false; x = x * 16 + static_cast<std::uint32_t>(d); }
    out = x; return true;
}

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
    BookmarksBar bookmarksBar = BookmarksBar::Auto;
    std::uint32_t accent = DefaultAccent; // #RRGGBB
    std::wstring customSearch;     // Engine::Custom: http(s) template with %s (SearchTemplate)
    NewTabPage newTabPage = NewTabPage::Blank;
    std::wstring homepage;         // NewTabPage::Custom (HomepageUrl)
    TabPosition tabPosition = TabPosition::End;
    bool confirmCloseTabs = true;  // ask before closing a window with several tabs
    bool closeWithLastTab = false; // closing the last tab closes the window (else a blank tab stays)
    bool adBlock = true;           // bundled content blocker
    bool httpsOnly = false;        // upgrade to https, warn before an http-only site
    bool blockThirdPartyCookies = false;
    bool memorySaver = true;       // background tabs ask the engine to trim memory
    bool muteBackground = false;   // only the active tab plays sound
    Autoplay autoplay = Autoplay::Muted;
    bool grayscaleText = false;    // page text in grayscale antialiasing, like the chrome (engine flag)
    // Annotations et agents (dev/preview builds with the annotation mode; read by src/annotation-mode.h).
    bool annotationConnectPrompt = true; // « Connecter un agent » opens at the mode's launch while no agent is connected
    int annotationConnectDeclines = 0;   // « Plus tard » answers, 0..MaxConnectDeclines (at the max the modal stops opening)
    int annotationRetentionDays = 30;    // done annotations kept that long: one of RetentionChoices
    bool operator==(const State& o) const {
        return engine == o.engine && restoreTabs == o.restoreTabs && downloadFolder == o.downloadFolder &&
               askWhereToSave == o.askWhereToSave && reduceMotion == o.reduceMotion && searchSuggestions == o.searchSuggestions &&
               tracking == o.tracking && passwords == o.passwords && autofill == o.autofill && clearOnExit == o.clearOnExit &&
               sleepMinutes == o.sleepMinutes && zoomPercent == o.zoomPercent && customSearch == o.customSearch &&
               newTabPage == o.newTabPage && homepage == o.homepage && tabPosition == o.tabPosition &&
               confirmCloseTabs == o.confirmCloseTabs && closeWithLastTab == o.closeWithLastTab && adBlock == o.adBlock &&
               httpsOnly == o.httpsOnly && blockThirdPartyCookies == o.blockThirdPartyCookies && memorySaver == o.memorySaver &&
               muteBackground == o.muteBackground && autoplay == o.autoplay && grayscaleText == o.grayscaleText &&
               bookmarksBar == o.bookmarksBar && accent == o.accent &&
               annotationConnectPrompt == o.annotationConnectPrompt && annotationConnectDeclines == o.annotationConnectDeclines &&
               annotationRetentionDays == o.annotationRetentionDays;
    }
};
// What a panel interaction changed; the host applies and saves it.
enum class Change { Engine, RestoreTabs, DownloadFolder, AskWhereToSave, ReduceMotion,
                    SearchSuggestions, Tracking, Passwords, Autofill, ClearOnExit, Sleep, Zoom,
                    CustomSearch, NewTabPage, Homepage, TabPosition, ConfirmCloseTabs, CloseWithLastTab,
                    AdBlock, HttpsOnly, ThirdPartyCookies, MemorySaver, MuteBackground, Autoplay, BookmarksBar, GrayscaleText,
                    AnnotationConnectPrompt, AnnotationRetention };
// Annotations: the connect modal stops opening by itself after this many « Plus tard » (src/annotation-mode.h).
inline constexpr int MaxConnectDeclines = 3;
inline constexpr int RetentionChoices[] = {7, 30, 90};
inline constexpr int RetentionCount = 3;
inline std::wstring RetentionLabel(int days) { return std::to_wstring(days) + L" jours"; }
// « Proposer de connecter un agent au lancement du mode » as the switch shows it: on while the modal would
// still open by itself (not turned off, and fewer than MaxConnectDeclines « Plus tard »).
inline bool ConnectPromptActive(const State& s) { return s.annotationConnectPrompt && s.annotationConnectDeclines < MaxConnectDeclines; }
// Flipping that switch: off = no more prompts; on again = prompts back and the « Plus tard » count restarts at 0.
inline void SetConnectPrompt(State& s, bool on) { s.annotationConnectPrompt = on; if (on) s.annotationConnectDeclines = 0; }

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
        << "zoom-percent=" << s.zoomPercent << '\n'
        << "custom-search=" << NativeChrome::Downloads::ToUtf8(s.customSearch) << '\n'
        << "new-tab-page=" << Info(s.newTabPage).key << '\n'
        << "homepage=" << NativeChrome::Downloads::ToUtf8(s.homepage) << '\n'
        << "tab-position=" << Info(s.tabPosition).key << '\n'
        << "confirm-close-tabs=" << (s.confirmCloseTabs ? 1 : 0) << '\n'
        << "close-with-last-tab=" << (s.closeWithLastTab ? 1 : 0) << '\n'
        << "ad-block=" << (s.adBlock ? 1 : 0) << '\n'
        << "https-only=" << (s.httpsOnly ? 1 : 0) << '\n'
        << "block-third-party-cookies=" << (s.blockThirdPartyCookies ? 1 : 0) << '\n'
        << "memory-saver=" << (s.memorySaver ? 1 : 0) << '\n'
        << "mute-background=" << (s.muteBackground ? 1 : 0) << '\n'
        << "autoplay=" << Info(s.autoplay).key << '\n'
        << "grayscale-text=" << (s.grayscaleText ? 1 : 0) << '\n'
        << "bookmarks-bar=" << Info(s.bookmarksBar).key << '\n'
        << "accent=" << AccentText(s.accent) << '\n'
        << "annotation-connect-prompt=" << (s.annotationConnectPrompt ? 1 : 0) << '\n'
        << "annotation-connect-declines=" << s.annotationConnectDeclines << '\n'
        << "annotation-retention-days=" << s.annotationRetentionDays << '\n';
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
        else if (key == "confirm-close-tabs") flag(value, s.confirmCloseTabs);
        else if (key == "close-with-last-tab") flag(value, s.closeWithLastTab);
        else if (key == "ad-block") flag(value, s.adBlock);
        else if (key == "https-only") flag(value, s.httpsOnly);
        else if (key == "block-third-party-cookies") flag(value, s.blockThirdPartyCookies);
        else if (key == "memory-saver") flag(value, s.memorySaver);
        else if (key == "mute-background") flag(value, s.muteBackground);
        else if (key == "grayscale-text") flag(value, s.grayscaleText);
        else if (key == "annotation-launcher") {}   // V2 « Afficher le lanceur d'annotations »: since V3 the launcher only exists in the mode (read, ignored, not written back)
        else if (key == "annotation-connect-prompt") flag(value, s.annotationConnectPrompt);
        else if (key == "annotation-connect-declines") { for (int n = 0; n <= MaxConnectDeclines; ++n) if (value == std::to_string(n)) s.annotationConnectDeclines = n; }
        else if (key == "annotation-retention-days") { for (int d : RetentionChoices) if (value == std::to_string(d)) s.annotationRetentionDays = d; }
        else if (key == "new-tab-page") { for (int i = 0; i < NewTabPageCount; ++i) if (value == Info(static_cast<NewTabPage>(i)).key) s.newTabPage = static_cast<NewTabPage>(i); }
        else if (key == "tab-position") { for (int i = 0; i < TabPositionCount; ++i) if (value == Info(static_cast<TabPosition>(i)).key) s.tabPosition = static_cast<TabPosition>(i); }
        else if (key == "autoplay") { for (int i = 0; i < AutoplayCount; ++i) if (value == Info(static_cast<Autoplay>(i)).key) s.autoplay = static_cast<Autoplay>(i); }
        else if (key == "custom-search") { std::wstring v; if (value.size() < 4096 && NativeChrome::Downloads::FromUtf8(value, v)) s.customSearch = SearchTemplate(v); }
        else if (key == "homepage") { std::wstring v; if (value.size() < 4096 && NativeChrome::Downloads::FromUtf8(value, v)) s.homepage = HomepageUrl(v); }
        else if (key == "bookmarks-bar") { for (int i = 0; i < BookmarksBarCount; ++i) if (value == Info(static_cast<BookmarksBar>(i)).key) s.bookmarksBar = static_cast<BookmarksBar>(i); }
        else if (key == "accent") { std::uint32_t rgb = 0; if (ParseAccent(value, rgb)) s.accent = rgb; }
        else if (key == "download-folder") { std::wstring folder; if (value.size() < 32768 && NativeChrome::Downloads::FromUtf8(value, folder)) s.downloadFolder = folder; }
    }
    // A custom choice without its address falls back to the default.
    if (s.engine == Engine::Custom && s.customSearch.empty()) s.engine = Engine::DuckDuckGo;
    if (s.newTabPage == NewTabPage::Custom && s.homepage.empty()) s.newTabPage = NewTabPage::Blank;
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

// Search URL for already percent-encoded terms. A custom template gets the
// terms at each %s; an unusable one falls back to DuckDuckGo.
inline std::wstring SearchUrl(Engine engine, const std::wstring& encodedTerms, const std::wstring& customSearch = L"") {
    if (engine == Engine::Custom) {
        const std::wstring t = SearchTemplate(customSearch);
        if (t.empty()) return std::wstring(Info(Engine::DuckDuckGo).search) + encodedTerms;
        std::wstring out; size_t at = 0, hit;
        while ((hit = t.find(L"%s", at)) != std::wstring::npos) { out += t.substr(at, hit - at) + encodedTerms; at = hit + 2; }
        return out + t.substr(at);
    }
    return std::wstring(Info(engine).search) + encodedTerms;
}
} // namespace BrowserSettings
