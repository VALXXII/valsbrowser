#pragma once
// Browser-wide interface language preference (design/browser-languages-plan.md).
// One small file at the GLOBAL data root, shared by every profile and window:
//   <dataDir>\ui-language      « browser-ui-language 1 » then « language=fr » or « language=en »
// (portable: next to browser.exe; installed: <root>\data, copied once by the portable migration).
// Read once at startup, before any window or dialog, to pin the process language (src/i18n.h);
// written atomically (ProfileStore::WriteAtomic) when the user picks a language in Réglages. The
// new language applies at the next start of a window: an open window never switches half-way.
// Anything unexpected falls back to French with a distinct status (logged, never shown as saved).
#include <string>
#include "i18n.h"
#include "profile-store.h"

namespace UiLanguage {

inline constexpr const wchar_t* FileName = L"ui-language";
inline constexpr const char* Header = "browser-ui-language 1";
inline constexpr const char* HeaderFamily = "browser-ui-language ";

enum class Status { Ok, Missing, Unreadable, UnknownVersion, Invalid };
struct Loaded { I18n::Locale locale = I18n::Locale::Fr; Status status = Status::Missing; };

inline const char* Key(I18n::Locale l) { return l == I18n::Locale::En ? "en" : "fr"; }
inline const char* StatusText(Status s) {
    switch (s) {
    case Status::Ok: return "ok"; case Status::Missing: return "missing"; case Status::Unreadable: return "unreadable";
    case Status::UnknownVersion: return "unknown-version"; default: return "invalid";
    }
}
inline std::string Format(I18n::Locale l) { return std::string(Header) + "\nlanguage=" + Key(l) + "\n"; }

// Pure parse of the file's bytes. Empty bytes = no preference (Missing).
inline Loaded Parse(const std::string& bytes) {
    Loaded r;
    if (bytes.empty()) return r;
    std::size_t at = 0;
    auto line = [&](std::string& out) {
        if (at >= bytes.size()) return false;
        std::size_t end = bytes.find('\n', at); if (end == std::string::npos) end = bytes.size();
        out = bytes.substr(at, end - at); if (!out.empty() && out.back() == '\r') out.pop_back();
        at = end + 1; return true;
    };
    std::string first; line(first);
    if (first.size() >= 3 && static_cast<unsigned char>(first[0]) == 0xEF && static_cast<unsigned char>(first[1]) == 0xBB && static_cast<unsigned char>(first[2]) == 0xBF) first.erase(0, 3);
    if (first != Header) { r.status = first.rfind(HeaderFamily, 0) == 0 ? Status::UnknownVersion : Status::Invalid; return r; }
    r.status = Status::Invalid;   // until a valid language line is found
    std::string l;
    while (line(l)) {
        if (l == "language=fr") { r.locale = I18n::Locale::Fr; r.status = Status::Ok; }
        else if (l == "language=en") { r.locale = I18n::Locale::En; r.status = Status::Ok; }
        else if (l.rfind("language=", 0) == 0) { r.locale = I18n::Locale::Fr; r.status = Status::Invalid; }
    }
    if (r.status != Status::Ok) r.locale = I18n::Locale::Fr;
    return r;
}

inline std::wstring PathIn(const std::wstring& dataDir) { return dataDir + L"\\" + FileName; }

inline Loaded Load(const std::wstring& dataDir) {
    std::string bytes;
    if (!ProfileStore::ReadAll(PathIn(dataDir), bytes)) { Loaded r; r.status = Status::Unreadable; return r; }
    return Parse(bytes);
}
// True only once the complete file is in place. False: nothing claims it was saved.
inline bool Save(const std::wstring& dataDir, I18n::Locale l) { return ProfileStore::WriteAtomic(PathIn(dataDir), Format(l)); }

// Autonyms: each language is named in itself, whatever the interface language.
inline const wchar_t* Autonym(I18n::Locale l) { return l == I18n::Locale::En ? L"English" : L"Français"; }

} // namespace UiLanguage
