#pragma once
// #121 « Toujours bloquer ce site » (Paper p-V-0 « Onglet planté + partage d'écran · états », section 3, variant B):
// the sites whose screen-capture requests are refused at once, without our popover. WebView2 (SDK 1.0.4314.35) has no
// permission kind for screen capture, so the engine's permission store (the one Réglages › Autorisations reads) cannot
// hold it: Browser keeps the list itself, one file per profile (<profile data>\screen-share-blocked), and Réglages ›
// Confidentialité › Autorisations des sites shows each entry as « Partage d'écran · Bloqué » (MergeInto below; removable
// there with « Retirer les autorisations »). Only « Bloquer » is ever
// remembered, never « Choisir… ».
// Format: the header line, then one origin per line (scheme://host[:port], lower-case scheme and host, no path), UTF-8.
// Anything else is dropped on reading; the list is bounded.
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace NativeChrome {
namespace ScreenShareBlocks {
inline constexpr const char* Header = "browser-screen-share-blocked 1";
inline constexpr std::size_t MaxOrigins = 10000, MaxOriginLength = 512;

// « https://meet.google.com/ » and « HTTPS://Meet.Google.com » are « https://meet.google.com ». Empty: not an origin.
inline std::wstring Normalize(std::wstring o) {
    while (!o.empty() && o.back() == L'/') o.pop_back();
    const auto sep = o.find(L"://"); if (sep == std::wstring::npos) return L"";
    for (wchar_t& c : o) c = static_cast<wchar_t>(std::towlower(c));
    const std::wstring scheme = o.substr(0, sep), rest = o.substr(sep + 3);
    if ((scheme != L"https" && scheme != L"http") || rest.empty() || o.size() > MaxOriginLength) return L"";
    for (wchar_t c : rest) if (!(std::iswalnum(c) || c == L'.' || c == L'-' || c == L':' || c == L'[' || c == L']' || c == L'_' || c > 0x7F)) return L"";
    return o;
}
inline std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(std::max(0, n)), '\0'); if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
inline bool Contains(const std::vector<std::wstring>& list, const std::wstring& origin) {
    const std::wstring o = Normalize(origin); return !o.empty() && std::find(list.begin(), list.end(), o) != list.end();
}
// true when the list changed.
inline bool Set(std::vector<std::wstring>& list, const std::wstring& origin, bool blocked) {
    const std::wstring o = Normalize(origin); if (o.empty()) return false;
    const auto it = std::find(list.begin(), list.end(), o);
    if (blocked) { if (it != list.end() || list.size() >= MaxOrigins) return false; list.push_back(o); std::sort(list.begin(), list.end()); return true; }
    if (it == list.end()) return false;
    list.erase(it); return true;
}
inline std::vector<std::wstring> Parse(const std::string& bytes) {
    std::vector<std::wstring> out; std::size_t at = 0; bool first = true;
    while (at < bytes.size() && out.size() < MaxOrigins) {
        std::size_t end = bytes.find('\n', at); if (end == std::string::npos) end = bytes.size();
        std::string line = bytes.substr(at, end - at); at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) { first = false; if (line != Header) return {}; continue; }
        if (line.empty() || line.size() > MaxOriginLength * 4) continue;
        const std::wstring o = Normalize(Wide(line));
        if (!o.empty() && std::find(out.begin(), out.end(), o) == out.end()) out.push_back(o);
    }
    std::sort(out.begin(), out.end());
    return out;
}
inline std::string Serialize(const std::vector<std::wstring>& list) {
    std::string s = std::string(Header) + "\n";
    for (const auto& o : list) { const std::wstring n = Normalize(o); if (!n.empty()) s += Utf8(n) + "\n"; }
    return s;
}
// Réglages › Autorisations des sites: the engine's sites (BrowserSettings::Site: origin + permission rows) with this list
// merged in. Each blocked origin adds `row` (« Partage d'écran · Bloqué ») to the site of the same origin, whatever the
// engine's spelling (« http://127.0.0.1:5207/ »), or becomes a site of its own; the list stays sorted by origin.
template <class Site, class Row>
void MergeInto(std::vector<Site>& sites, const std::vector<std::wstring>& blocked, const Row& row) {
    for (const auto& o : blocked) {
        const std::wstring n = Normalize(o); if (n.empty()) continue;
        auto it = std::find_if(sites.begin(), sites.end(), [&](const Site& s) { return Normalize(s.origin) == n; });
        if (it == sites.end()) { sites.push_back(Site{n, {}}); it = sites.end() - 1; }
        it->permissions.push_back(row);
    }
    std::stable_sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.origin < b.origin; });
}
} // namespace ScreenShareBlocks
} // namespace NativeChrome
