#pragma once
// Browser settings: model, search engines and persistence. Pure C++17 (no
// HWND, Direct2D or WebView2), so the fixture in tests/settings_test.cpp
// exercises it directly. Stored as UTF-8 `key=value` lines in `settings`,
// next to `session`; unknown keys and malformed values keep their defaults.
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
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

struct State {
    Engine engine = Engine::DuckDuckGo;
    bool restoreTabs = true;       // reopen the previous session's tabs at launch
    std::wstring downloadFolder;   // empty: the system Downloads folder
    bool askWhereToSave = false;   // a save dialog for every download
    bool reduceMotion = false;     // on top of Windows' own "animation effects" switch
    bool operator==(const State& o) const {
        return engine == o.engine && restoreTabs == o.restoreTabs && downloadFolder == o.downloadFolder &&
               askWhereToSave == o.askWhereToSave && reduceMotion == o.reduceMotion;
    }
};
// What a panel interaction changed; the host applies and saves it.
enum class Change { Engine, RestoreTabs, DownloadFolder, AskWhereToSave, ReduceMotion };

inline void Write(std::ostream& out, const State& s) {
    out << "browser-settings 1\n"
        << "engine=" << Info(s.engine).key << '\n'
        << "restore-tabs=" << (s.restoreTabs ? 1 : 0) << '\n'
        << "download-folder=" << NativeChrome::Downloads::ToUtf8(s.downloadFolder) << '\n'
        << "ask-where-to-save=" << (s.askWhereToSave ? 1 : 0) << '\n'
        << "reduce-motion=" << (s.reduceMotion ? 1 : 0) << '\n';
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
        else if (key == "download-folder") { std::wstring folder; if (value.size() < 32768 && NativeChrome::Downloads::FromUtf8(value, folder)) s.downloadFolder = folder; }
    }
    return s;
}
// Search URL for already percent-encoded terms.
inline std::wstring SearchUrl(Engine engine, const std::wstring& encodedTerms) { return std::wstring(Info(engine).search) + encodedTerms; }
} // namespace BrowserSettings
