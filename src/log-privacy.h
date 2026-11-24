#pragma once
// browser.log privacy (audit SEC-05): what a log line may keep, and how big the file may grow.
// Release builds reduce every address in a line to its origin (scheme://host[:port]): the path, query,
// fragment and any user:password@ part never reach the disk; data:, javascript: and similar addresses
// keep their scheme only. The file is capped: past Cap bytes it is moved to "<log>.old" (one
// generation, replaced each time), so the log never grows without bound.
#include <windows.h>
#include <cwctype>
#include <string>

namespace LogPrivacy {

constexpr unsigned long long Cap = 1024ull * 1024ull;   // 1 MiB per generation

inline bool SchemeChar(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.'; }
inline bool Stop(wchar_t c) { return std::iswspace(c) || c == L'"' || c == L'\'' || c == L'<' || c == L'>'; }
inline bool OpaqueScheme(std::wstring s) {
    for (auto& c : s) c = (wchar_t)std::towlower(c);
    return s == L"data" || s == L"javascript" || s == L"blob" || s == L"about" || s == L"mailto" || s == L"view-source" || s == L"filesystem";
}

// Every "scheme://authority..." becomes "scheme://host[:port]"; every "data:..." (and the other opaque
// schemes above, at the start of a word) becomes "data:". Words without an address are kept as they are.
inline std::wstring Redact(const std::wstring& s) {
    std::wstring out; out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        // A scheme starts a word: the beginning of the line, or after any character a scheme cannot hold.
        const bool wordStart = i == 0 || !SchemeChar(s[i - 1]);
        if (wordStart && std::iswalpha(s[i])) {
            size_t j = i; while (j < s.size() && SchemeChar(s[j])) ++j;
            if (j < s.size() && s[j] == L':') {
                const std::wstring scheme = s.substr(i, j - i);
                size_t end = j + 1; while (end < s.size() && !Stop(s[end])) ++end;
                if (OpaqueScheme(scheme)) {
                    std::wstring lower = scheme; for (auto& c : lower) c = (wchar_t)std::towlower(c);
                    if (lower == L"data" || lower == L"javascript") { out += scheme + L":"; break; }   // content may hold spaces: the rest of the line goes
                    // blob:https://site/uuid and view-source:https://site/page keep the inner origin.
                    const std::wstring inner = s.substr(j + 1, end - j - 1);
                    out += scheme + L":";
                    if (inner.find(L"://") != std::wstring::npos) out += Redact(inner);
                    i = end; continue;
                }
                if (s.compare(j, 3, L"://") == 0) {
                    size_t a = j + 3, b = a;
                    while (b < end && s[b] != L'/' && s[b] != L'?' && s[b] != L'#' && s[b] != L'\\') ++b;
                    std::wstring authority = s.substr(a, b - a);
                    const size_t at = authority.rfind(L'@'); if (at != std::wstring::npos) authority.erase(0, at + 1);
                    out += scheme + L"://" + authority;
                    i = end; continue;
                }
            }
        }
        out += s[i++];
    }
    return out;
}

// Moves the log to "<log>.old" once it is past the cap. Called before an append; cheap (one attribute query).
inline void RotateIfLarge(const std::wstring& log, unsigned long long cap = Cap) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(log.c_str(), GetFileExInfoStandard, &a)) return;
    const unsigned long long size = ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    if (size <= cap) return;
    MoveFileExW(log.c_str(), (log + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);   // best effort: another window may hold it a moment
}

// « Effacer les données »: the log and its previous generation go. True when neither remains.
inline bool Erase(const std::wstring& log) {
    bool ok = true;
    for (const std::wstring& f : {log, log + L".old"})
        if (!DeleteFileW(f.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) ok = false;
    return ok;
}

}  // namespace LogPrivacy
