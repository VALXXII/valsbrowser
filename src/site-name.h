#pragma once
// Compact site name for small surfaces (split-view drop chip, lifted tab card):
// "GitHub", not "GitHub · Le changement est constant… · GitHub · GitHub".
// Deterministic and local: computed from the URL and the document title only, no
// network, no page script. The full document title is left untouched everywhere
// else (tab strip, history, accessibility names).
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace NativeChrome { namespace SiteName {

inline std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)std::towlower(c); return s; }
// Letters and digits only, lower case: "Le Monde.fr" -> "lemondefr".
inline std::wstring Key(const std::wstring& s) { std::wstring k; for (wchar_t c : s) if (std::iswalnum(c)) k += (wchar_t)std::towlower(c); return k; }
inline std::wstring Trim(const std::wstring& s) {
    const auto a = s.find_first_not_of(L" \t\r\n "); if (a == std::wstring::npos) return L"";
    return s.substr(a, s.find_last_not_of(L" \t\r\n ") - a + 1);
}

struct Url { std::wstring scheme, host, path; };
inline Url Parse(const std::wstring& uri) {
    Url u; const auto colon = uri.find(L':'); if (colon == std::wstring::npos) return u;
    u.scheme = Lower(uri.substr(0, colon));
    std::wstring rest = uri.substr(colon + 1);
    if (rest.rfind(L"//", 0) == 0) {
        rest = rest.substr(2); const auto end = rest.find_first_of(L"/?#");
        std::wstring authority = rest.substr(0, end); u.path = end == std::wstring::npos ? L"" : rest.substr(end);
        const auto at = authority.rfind(L'@'); if (at != std::wstring::npos) authority = authority.substr(at + 1);
        if (!authority.empty() && authority[0] != L'[') { const auto port = authority.rfind(L':'); if (port != std::wstring::npos) authority = authority.substr(0, port); }
        u.host = Lower(authority);
    } else u.path = rest;
    return u;
}
inline std::vector<std::wstring> Labels(const std::wstring& host) {
    std::vector<std::wstring> out; size_t start = 0;
    while (start <= host.size()) { const auto dot = host.find(L'.', start); out.push_back(host.substr(start, dot - start)); if (dot == std::wstring::npos) break; start = dot + 1; }
    return out;
}
// Two-label public suffixes common enough to matter here (co.uk, gouv.fr, ...).
inline bool SecondLevelSuffix(const std::wstring& a, const std::wstring& b) {
    static const wchar_t* generic[] = {L"co", L"com", L"org", L"net", L"gov", L"gouv", L"ac", L"edu", L"ne", L"or", L"go"};
    if (b.size() != 2) return false;
    for (auto g : generic) if (a == g) return true;
    return false;
}
// "www.github.com" -> "github", "news.bbc.co.uk" -> "bbc", "developer.mozilla.org" -> "mozilla".
inline std::wstring DomainLabel(const std::wstring& host, std::wstring* registrable = nullptr) {
    auto l = Labels(host); if (l.empty()) return L"";
    if (l.size() == 1) { if (registrable) *registrable = host; return l[0]; }
    size_t i = l.size() - 2;
    if (l.size() >= 3 && SecondLevelSuffix(l[l.size() - 2], l.back())) i = l.size() - 3;
    if (registrable) { std::wstring r; for (size_t k = i; k < l.size(); ++k) r += (k > i ? L"." : L"") + l[k]; *registrable = r; }
    return l[i];
}
inline bool NumericHost(const std::wstring& host) { return !host.empty() && std::all_of(host.begin(), host.end(), [](wchar_t c) { return std::iswdigit(c) || c == L'.' || c == L':'; }); }

// Exact names for sites whose title never reads as a clean brand; full host first.
inline std::wstring Known(const std::wstring& host, const std::wstring& registrable) {
    struct Entry { const wchar_t* domain; const wchar_t* name; };
    static const Entry table[] = {
        {L"developer.mozilla.org", L"MDN"}, {L"github.com", L"GitHub"}, {L"gist.github.com", L"GitHub Gist"},
        {L"youtube.com", L"YouTube"}, {L"youtu.be", L"YouTube"}, {L"wikipedia.org", L"Wikipédia"},
        {L"duckduckgo.com", L"DuckDuckGo"}, {L"stackoverflow.com", L"Stack Overflow"}, {L"linkedin.com", L"LinkedIn"},
        {L"x.com", L"X"}, {L"twitter.com", L"X"}, {L"reddit.com", L"Reddit"}, {L"mobbin.com", L"Mobbin"},
        {L"paper.design", L"Paper"}, {L"gitlab.com", L"GitLab"}, {L"openai.com", L"OpenAI"}, {L"anthropic.com", L"Anthropic"},
        {L"claude.ai", L"Claude"}, {L"notion.so", L"Notion"}, {L"amazon.fr", L"Amazon"}, {L"amazon.com", L"Amazon"},
    };
    for (auto& e : table) if (host == e.domain) return e.name;
    for (auto& e : table) if (registrable == e.domain) return e.name;
    return L"";
}
// Title split on the separators sites use between page and brand: " · ", " | ", " - ", " — ", " – ".
inline std::vector<std::wstring> Segments(const std::wstring& title) {
    static const wchar_t* seps[] = {L" · ", L" | ", L" - ", L" — ", L" – ", L" • "};
    std::vector<std::wstring> out; std::wstring rest = title;
    while (true) {
        size_t best = std::wstring::npos, len = 0;
        for (auto s : seps) { const auto p = rest.find(s); if (p != std::wstring::npos && p < best) { best = p; len = wcslen(s); } }
        if (best == std::wstring::npos) { auto t = Trim(rest); if (!t.empty()) out.push_back(t); break; }
        auto t = Trim(rest.substr(0, best)); if (!t.empty()) out.push_back(t); rest = rest.substr(best + len);
    }
    return out;
}
inline std::wstring Capitalize(std::wstring s) { if (!s.empty()) s[0] = (wchar_t)std::towupper(s[0]); return s; }

// The compact name, in this order: known site, the title segment that names the
// domain (keeps the site's own spelling, e.g. "BBC News", "Le Monde.fr"), the domain
// label, a local file's name, then the first title segment.
inline std::wstring Compact(const std::wstring& title, const std::wstring& uri) {
    const Url u = Parse(uri); const auto segments = Segments(title);
    if (u.scheme == L"file") {
        std::wstring p = u.path; const auto slash = p.find_last_of(L"/\\"); if (slash != std::wstring::npos) p = p.substr(slash + 1);
        const auto dot = p.rfind(L'.'); if (dot != std::wstring::npos && dot > 0) p = p.substr(0, dot);
        if (!p.empty()) return p;
    }
    if ((u.scheme == L"http" || u.scheme == L"https") && !u.host.empty()) {
        if (NumericHost(u.host) || u.host == L"localhost") return u.host;
        std::wstring registrable; const std::wstring label = DomainLabel(u.host, &registrable);
        const std::wstring known = Known(u.host, registrable); if (!known.empty()) return known;
        const std::wstring key = Key(label);
        if (!key.empty()) {
            for (auto& s : segments) if (Key(s) == key || Key(s) == Key(registrable)) return s;
            for (auto& s : segments) { const auto k = Key(s); if (k.rfind(key, 0) == 0 && s.size() <= 24) return s; }
            return Capitalize(label);
        }
    }
    if (!segments.empty()) return segments.front();
    return Trim(title);
}
}}
