#pragma once
// Address bar model (issue #20): readable display of the current URL with its
// security state, the visited-sites history and the suggestions shown while
// typing. Pure C++17 (no HWND, Direct2D or WebView2) so tests/address_test.cpp
// exercises it directly. The history is stored as UTF-8 tab-separated lines in
// `history`, next to `session`.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <istream>
#include <ostream>
#include <string>
#include <vector>
#include "downloads.h" // ToUtf8 / FromUtf8

namespace AddressBar {

inline std::wstring Lower(std::wstring s) { for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c)); return s; }
inline bool StartsWith(const std::wstring& s, const std::wstring& prefix) { return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0; }
inline std::wstring Trim(const std::wstring& s) {
    const auto first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return L"";
    return s.substr(first, s.find_last_not_of(L" \t\r\n") - first + 1);
}

// ---- Display ---------------------------------------------------------------
// How the address reads outside editing. Every non-secure state carries an icon
// AND a label: colour is never the only signal.
enum class Security { Empty, Secure, Insecure, CertificateError, File, Other };
enum class Tone { Primary, Secondary, Danger };
struct Run { std::wstring text; Tone tone = Tone::Secondary; bool strike = false; };
struct Display {
    Security security = Security::Empty;
    std::vector<Run> runs;
    // Leading chip label ("Non sécurisé", "Fichier"); empty: icon-only slot.
    const wchar_t* chip() const {
        switch (security) {
        case Security::Insecure: case Security::CertificateError: return L"Non sécurisé";
        case Security::File: return L"Fichier";
        default: return L"";
        }
    }
    // Icon asset name (assets\icons\<name>-<dpi>.png).
    const wchar_t* icon() const {
        switch (security) {
        case Security::Secure: return L"lock";
        case Security::Insecure: return L"lock-open";
        case Security::CertificateError: return L"danger";
        case Security::File: return L"file";
        case Security::Other: return L"favicon";
        default: return L"search";
        }
    }
    // Tooltip / accessible description of the indicator.
    const wchar_t* tip() const {
        switch (security) {
        case Security::Secure: return L"Connexion sécurisée";
        case Security::Insecure: return L"Connexion non sécurisée : les informations envoyées à ce site (mots de passe, cartes…) peuvent être lues par des tiers.";
        case Security::CertificateError: return L"Certificat non valide : ce site n'a pas pu prouver son identité. Les informations envoyées peuvent être interceptées.";
        case Security::File: return L"Fichier local";
        default: return L"";
        }
    }
};

struct Parts { std::wstring scheme, host, rest; };
// scheme://[user@]host[:port]/rest — rest keeps path, query and fragment verbatim.
inline bool Split(const std::wstring& uri, Parts& out) {
    const auto colon = uri.find(L"://");
    if (colon == std::wstring::npos || colon == 0) return false;
    out.scheme = Lower(uri.substr(0, colon));
    const auto start = colon + 3;
    auto end = uri.find_first_of(L"/?#", start);
    if (end == std::wstring::npos) end = uri.size();
    std::wstring authority = uri.substr(start, end - start);
    const auto at = authority.rfind(L'@');
    if (at != std::wstring::npos) authority = authority.substr(at + 1);
    out.host = Lower(authority);
    out.rest = uri.substr(end);
    return true;
}
// "www." is noise for reading; the rest of the host (subdomains) stays.
inline std::wstring ReadableHost(const std::wstring& host) { return StartsWith(host, L"www.") && host.size() > 4 ? host.substr(4) : host; }
inline std::wstring ReadableRest(const std::wstring& rest) { return rest == L"/" ? L"" : rest; }
// The site an http(s) URL belongs to, as icons are keyed: lower-case host without "www.".
inline std::wstring SiteOf(const std::wstring& url) {
    Parts p;
    if (!Split(url, p) || (p.scheme != L"https" && p.scheme != L"http")) return L"";
    auto host = ReadableHost(p.host); const auto colon = host.find(L':');
    return colon == std::wstring::npos ? host : host.substr(0, colon);
}

inline std::wstring PercentDecode(const std::wstring& s) {
    std::string bytes; bytes.reserve(s.size());
    const std::string utf8 = NativeChrome::Downloads::ToUtf8(s);
    auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '%' && i + 2 < utf8.size() && hex(utf8[i + 1]) >= 0 && hex(utf8[i + 2]) >= 0) { bytes += static_cast<char>(hex(utf8[i + 1]) * 16 + hex(utf8[i + 2])); i += 2; }
        else bytes += utf8[i];
    }
    std::wstring out;
    return NativeChrome::Downloads::FromUtf8(bytes, out) ? out : s;
}

// `certificateError`: the engine reported an invalid server certificate for the
// document now shown (ICoreWebView2_14::ServerCertificateErrorDetected).
inline Display Describe(const std::wstring& uri, bool certificateError = false) {
    Display d;
    if (uri.empty() || uri == L"about:blank") return d;
    Parts p;
    if (Split(uri, p) && (p.scheme == L"https" || p.scheme == L"http")) {
        if (certificateError) {
            d.security = Security::CertificateError;
            d.runs = {{p.scheme, Tone::Danger, p.scheme == L"https"}, {L"://", Tone::Secondary}, {p.host, Tone::Primary}, {ReadableRest(p.rest), Tone::Secondary}};
        } else {
            d.security = p.scheme == L"https" ? Security::Secure : Security::Insecure;
            d.runs = {{ReadableHost(p.host), Tone::Primary}, {ReadableRest(p.rest), Tone::Secondary}};
        }
    } else if (Split(uri, p) && p.scheme == L"file") {
        d.security = Security::File;
        std::wstring path = PercentDecode(uri.substr(uri.find(L"://") + 3));
        if (StartsWith(path, L"/") && path.size() > 2 && path[2] == L':') path = path.substr(1); // /C:/… -> C:/…
        const auto slash = path.find_last_of(L"/\\");
        if (slash == std::wstring::npos) d.runs = {{path, Tone::Primary}};
        else d.runs = {{path.substr(0, slash + 1), Tone::Secondary}, {path.substr(slash + 1), Tone::Primary}};
    } else {
        d.security = Security::Other;
        d.runs = {{uri, Tone::Secondary}};
    }
    d.runs.erase(std::remove_if(d.runs.begin(), d.runs.end(), [](const Run& r) { return r.text.empty(); }), d.runs.end());
    return d;
}

// ---- Typed text ------------------------------------------------------------
// Same rule as the host's Destination(): a single word with a dot (or
// localhost), or an explicit http(s) scheme, is an address; anything else searches.
inline bool LooksLikeAddress(const std::wstring& typed) {
    const std::wstring s = Trim(typed);
    if (s.empty()) return false;
    const std::wstring low = Lower(s);
    if (StartsWith(low, L"https://") || StartsWith(low, L"http://") || s == L"about:blank") return true;
    return s.find_first_of(L" \t") == std::wstring::npos && s.find(L"://") == std::wstring::npos &&
           (s.find(L'.') != std::wstring::npos || StartsWith(low, L"localhost"));
}
// RFC 3986 unreserved characters stay; every other UTF-8 byte is %XX.
inline std::wstring PercentEncode(const std::wstring& s) {
    std::wstring q;
    for (unsigned char c : NativeChrome::Downloads::ToUtf8(s)) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') q += static_cast<wchar_t>(c);
        else { static const wchar_t* digits = L"0123456789ABCDEF"; q += L'%'; q += digits[c >> 4]; q += digits[c & 15]; }
    }
    return q;
}

// ---- History ---------------------------------------------------------------
struct Visit {
    std::wstring url, title;
    int visits = 0;
    std::int64_t last = 0; // Unix ms
};
// What a suggestion shows and matches: no scheme, no "www.", no lone root "/".
inline std::wstring MatchText(const std::wstring& url) {
    Parts p;
    if (!Split(url, p) || (p.scheme != L"https" && p.scheme != L"http")) return url;
    return ReadableHost(p.host) + ReadableRest(p.rest);
}
// One entry per page: scheme and host compare case-insensitively, "/" root equals none.
inline std::wstring Key(const std::wstring& url) {
    Parts p;
    if (!Split(url, p)) return url;
    return p.scheme + L"://" + p.host + ReadableRest(p.rest);
}
inline bool Recordable(const std::wstring& url) {
    Parts p;
    return Split(url, p) && (p.scheme == L"https" || p.scheme == L"http" || p.scheme == L"file") && url.size() < 4096;
}

class History {
public:
    static constexpr size_t Limit = 1000;
    const std::vector<Visit>& Items() const { return items_; }
    // A completed top-level navigation. Returns false when nothing was recorded.
    bool Record(const std::wstring& url, const std::wstring& title, std::int64_t now) {
        if (!Recordable(url)) return false;
        auto* v = Find(url);
        if (!v) {
            if (items_.size() >= Limit) Evict();
            items_.push_back({url, L"", 0, 0}); v = &items_.back();
        }
        ++v->visits; v->last = now; // the first spelling of the URL is kept
        if (!title.empty()) v->title = title;
        return true;
    }
    bool SetTitle(const std::wstring& url, const std::wstring& title) {
        auto* v = Find(url);
        if (!v || title.empty() || v->title == title) return false;
        v->title = title; return true;
    }
    bool Remove(const std::wstring& url) {
        const auto key = Key(url); const auto before = items_.size();
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Visit& v) { return Key(v.url) == key; }), items_.end());
        return items_.size() != before;
    }
    // Removes every page of a host (the aggregated "github.com" suggestion).
    bool RemoveHost(const std::wstring& host) {
        const auto before = items_.size();
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Visit& v) { Parts p; return Split(v.url, p) && ReadableHost(p.host) == host; }), items_.end());
        return items_.size() != before;
    }
    // Whether any remembered page still belongs to `site` (see SiteOf).
    bool HasSite(const std::wstring& site) const { for (auto& v : items_) if (SiteOf(v.url) == site) return true; return false; }
    void Clear() { items_.clear(); }
    void Write(std::ostream& out) const {
        out << "browser-history 1\n";
        for (auto& v : items_) out << v.visits << '\t' << v.last << '\t' << NativeChrome::Downloads::ToUtf8(v.url) << '\t' << NativeChrome::Downloads::ToUtf8(Clean(v.title)) << '\n';
    }
    static History Read(std::istream& in) {
        History h; std::string line;
        if (!std::getline(in, line)) return h;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line != "browser-history 1") return h;
        while (std::getline(in, line) && h.items_.size() < Limit) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> f; size_t start = 0;
            for (size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) f.push_back(line.substr(start, tab - start));
            f.push_back(line.substr(start));
            if (f.size() != 4) continue;
            Visit v;
            try { v.visits = std::max(1, std::stoi(f[0])); v.last = std::stoll(f[1]); } catch (...) { continue; }
            if (!NativeChrome::Downloads::FromUtf8(f[2], v.url) || !NativeChrome::Downloads::FromUtf8(f[3], v.title) || !Recordable(v.url) || h.Find(v.url)) continue;
            h.items_.push_back(std::move(v));
        }
        return h;
    }
private:
    std::vector<Visit> items_;
    static std::wstring Clean(std::wstring s) { for (auto& c : s) if (c == L'\t' || c == L'\n' || c == L'\r') c = L' '; return s; }
    Visit* Find(const std::wstring& url) {
        const auto key = Key(url);
        for (auto& v : items_) if (Key(v.url) == key) return &v;
        return nullptr;
    }
    void Evict() {
        auto worst = std::min_element(items_.begin(), items_.end(), [](const Visit& a, const Visit& b) { return a.visits != b.visits ? a.visits < b.visits : a.last < b.last; });
        if (worst != items_.end()) items_.erase(worst);
    }
};

// ---- Suggestions -----------------------------------------------------------
enum class Kind { Site, Address, Search };
struct Suggestion {
    Kind kind = Kind::Search;
    std::wstring url;   // Site: the page; Address/Search: empty (the host resolves them)
    std::wstring text;  // main label (MatchText for sites, the typed text otherwise)
    std::wstring title; // Site: page title, may be empty
    size_t match = std::wstring::npos, matchLength = 0; // highlighted range of `text`
    bool host = false;  // Site row aggregating every page of one host
    std::wstring site;  // Site/Address: the site whose remembered favicon the row shows
};
struct Suggestions {
    std::vector<Suggestion> rows;
    std::wstring completion; // inline completion appended after the typed text (selected)
};
constexpr size_t MaxSites = 4;

// `typed` is what the user typed (without any inline completion). `inlineAllowed`
// is false right after a deletion, so Backspace can remove the completion.
inline Suggestions Suggest(const std::wstring& typed, const History& history, bool inlineAllowed = true) {
    Suggestions out;
    const std::wstring query = Trim(typed);
    if (query.empty()) return out;
    const std::wstring q = Lower(query);
    struct Candidate { Suggestion row; int tier; int visits; std::int64_t last; };
    std::vector<Candidate> found;
    auto consider = [&](Suggestion row, const std::wstring& title, int visits, std::int64_t last) {
        const std::wstring text = Lower(row.text);
        const auto at = text.find(q);
        int tier = -1;
        if (at == 0) tier = 0;
        else if (at != std::wstring::npos) tier = (text[at - 1] == L'.' || text[at - 1] == L'/') ? 1 : 2;
        else if (Lower(title).find(q) != std::wstring::npos) tier = 3;
        if (tier < 0) return;
        if (at != std::wstring::npos) { row.match = at; row.matchLength = q.size(); }
        found.push_back({std::move(row), tier, visits, last});
    };
    // Host rows: one per visited host, summing its pages, so "git" offers github.com
    // even when only deep pages were visited (two or more). A visited root page becomes that row.
    struct HostTotal { std::wstring host, title, url; int visits = 0, pages = 0; std::int64_t last = 0; bool root = false; };
    std::vector<HostTotal> hosts;
    for (auto& v : history.Items()) {
        Parts p;
        if (!Split(v.url, p) || (p.scheme != L"https" && p.scheme != L"http")) continue;
        const std::wstring host = ReadableHost(p.host);
        auto it = std::find_if(hosts.begin(), hosts.end(), [&](const HostTotal& h) { return h.host == host; });
        if (it == hosts.end()) { hosts.push_back({host, L"", p.scheme + L"://" + p.host + L"/", 0, 0, false}); it = hosts.end() - 1; }
        it->visits += v.visits; ++it->pages; it->last = std::max(it->last, v.last);
        if (ReadableRest(p.rest).empty()) { it->root = true; it->title = v.title; it->url = v.url; }
    }
    for (auto& h : hosts) {
        if (!h.root && h.pages < 2) continue; // a single deep page is its own row
        Suggestion row; row.kind = Kind::Site; row.url = h.url; row.text = h.host; row.title = h.title; row.host = !h.root; row.site = SiteOf(h.url);
        consider(row, h.title, h.visits, h.last);
    }
    for (auto& v : history.Items()) {
        const std::wstring text = MatchText(v.url);
        Parts p;
        if (Split(v.url, p) && (p.scheme == L"https" || p.scheme == L"http") && ReadableRest(p.rest).empty()) continue; // the host row
        Suggestion row; row.kind = Kind::Site; row.url = v.url; row.text = text; row.title = v.title; row.site = SiteOf(v.url);
        consider(row, v.title, v.visits, v.last);
    }
    std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.visits != b.visits) return a.visits > b.visits;
        if (a.last != b.last) return a.last > b.last;
        return a.row.text.size() < b.row.text.size();
    });
    for (auto& c : found) { if (out.rows.size() == MaxSites) break; out.rows.push_back(std::move(c.row)); }

    const bool address = LooksLikeAddress(query);
    // Inline completion: only to a whole host, so Enter opens what the field shows.
    if (inlineAllowed && !out.rows.empty() && out.rows.front().match == 0 && query.size() < out.rows.front().text.size() &&
        out.rows.front().text.find_first_of(L"/?#") == std::wstring::npos && query == typed)
        out.completion = out.rows.front().text.substr(query.size());
    const bool exact = !out.rows.empty() && Lower(out.rows.front().text) == q;
    if (address && out.completion.empty() && !exact) {
        Suggestion go; go.kind = Kind::Address; go.text = query;
        go.site = SiteOf(StartsWith(Lower(query), L"http://") || StartsWith(Lower(query), L"https://") ? query : L"https://" + query);
        out.rows.insert(out.rows.begin(), go);
    }
    Suggestion search; search.kind = Kind::Search; search.text = query;
    // Row 0 is what Enter opens. Without an inline completion (or an exact site, or an
    // address) that must be what the field shows: a search for the typed words.
    if (out.completion.empty() && !exact && !address) out.rows.insert(out.rows.begin(), search);
    else out.rows.push_back(search);
    return out;
}

} // namespace AddressBar
