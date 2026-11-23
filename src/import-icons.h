#pragma once
// Site icons of imported bookmarks (src/browser-import.h « Site icons »): what an import leaves in the profile's
// <data>\favicons, where main.cpp's SiteIcon looks a site's icon up for the bookmarks bar, its menus, the drawer and
// the suggestions. Before this, only a visit wrote there: bookmarks imported from another browser showed no icon.
//  * Save(): the source's own icons (read with the bookmarks), each as <data>\favicons\<site>.png, never over a file
//    already there (a visit's icon is newer than any import). The bytes are kept as the source had them (PNG, or ICO
//    / GIF / JPEG / BMP): Windows' decoder reads them by content, whatever the extension.
//  * Missing(): the import's sites that still have no icon, what sits on the bar first, with the address to ask.
//  * Fetch(): one site's icon from the site itself (worker thread): <origin>/favicon.ico, else the first raster icon
//    its home page declares (<link rel="icon" …>). A plain GET: no cookie, no credentials, no certificate exception,
//    redirects followed by hand (never https → http), bounded sizes and times. A site that gives none keeps the
//    generic glyph, as before.
// Pure of the browser's globals (tests: tests/browser-import/import_icons_test.cpp). Nothing here logs an address.
#include <windows.h>
#include <winhttp.h>
#include <functional>
#include <string>
#include <vector>
#include "browser-import.h"

namespace ImportIcons {

namespace BI = BrowserImport;

inline std::wstring Folder(const std::wstring& dataDir) { return dataDir + L"\\favicons"; }
inline std::wstring FileOf(const std::wstring& dataDir, const std::wstring& site) { return Folder(dataDir) + L"\\" + AddressBar::SiteFileName(site) + L".png"; }
inline bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// One site's icon, only if the site has none yet. False: not written (already there, not an image, the profile folder
// gone, an I/O error). The profile folder itself is never created: a profile deleted meanwhile stays deleted.
inline bool SaveOne(const std::wstring& dataDir, const std::wstring& site, const std::string& bytes) {
    if (dataDir.empty() || site.empty() || !BI::Icon::Raster(bytes)) return false;
    const std::wstring folder = Folder(dataDir), path = FileOf(dataDir, site);
    if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    const DWORD a = GetFileAttributesW(folder.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    if (Exists(path)) return false;
    wchar_t suffix[48]; swprintf(suffix, 48, L".%lu-%lu.import", GetCurrentProcessId(), GetCurrentThreadId());
    const std::wstring temp = path + suffix;
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool wrote = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &put, nullptr) && put == bytes.size();
    CloseHandle(f);
    if (wrote && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) return true;   // never REPLACE_EXISTING: a file that appeared meanwhile wins
    DeleteFileW(temp.c_str());
    return false;
}
// Every icon of `icons` that `SaveOne` wrote, by site.
inline std::vector<std::wstring> Save(const std::wstring& dataDir, const BI::Icons& icons) {
    std::vector<std::wstring> written;
    for (const auto& [site, bytes] : icons) if (SaveOne(dataDir, site, bytes)) written.push_back(site);
    return written;
}

// A site to ask: its key (AddressBar::SiteOf) and « scheme://host[:port] » of one of its bookmarks.
struct Want { std::wstring site, origin; };
// « scheme://host[:port] » of an http(s) address (user info dropped), empty when it is anything else.
inline std::wstring OriginOf(const std::wstring& url) {
    AddressBar::Parts p;
    if (!AddressBar::Split(url, p) || (p.scheme != L"http" && p.scheme != L"https") || p.host.empty() || p.host.size() > 255) return L"";
    for (wchar_t c : p.host) if (c <= 0x20 || c >= 0x7f || c == L'/' || c == L'\\' || c == L'?' || c == L'#' || c == L'@') return L"";
    return p.scheme + L"://" + p.host;
}
// The sites of `roots` (bar first, BI::SitesOf) without an icon file in <data>\favicons and not `covered` (an icon the
// browser already has for them, such as a shipped one), at most `limit`.
inline std::vector<Want> Missing(const std::wstring& dataDir, const std::vector<BI::Item>& roots, const std::function<bool(const std::wstring&)>& covered, std::size_t limit = 1000) {
    std::vector<Want> out;
    const BI::Icon::Sites sites = BI::SitesOf(roots);
    for (const std::wstring& site : sites.order) {
        if (out.size() >= limit) break;
        if (Exists(FileOf(dataDir, site)) || (covered && covered(site))) continue;
        const auto& links = sites.links.at(site);
        std::wstring origin;   // https first when the site has both
        for (const auto& url : links) { const std::wstring o = OriginOf(url); if (!o.empty() && (origin.empty() || (o.rfind(L"https:", 0) == 0 && origin.rfind(L"https:", 0) != 0))) origin = o; }
        if (!origin.empty()) out.push_back({site, origin});
    }
    return out;
}

// ---- Fetching (worker threads) ---------------------------------------------------------------------------
namespace Bound {
inline constexpr std::size_t page = 512u << 10;   // a home page: only its start is read (the <head>)
inline constexpr int hops = 4;                    // redirects followed per request
inline constexpr int links = 4;                   // declared icons tried per site
}
// WinHTTP, resolved from System32 at first use: the browser's main program has no WinHTTP import (its test hosts
// compile main.cpp without it), as src/webview2-install.h does. `ready` false: nothing is ever fetched.
struct Http {
    bool ready = false;
    decltype(&::WinHttpOpen) Open = nullptr; decltype(&::WinHttpSetTimeouts) SetTimeouts = nullptr; decltype(&::WinHttpSetOption) SetOption = nullptr;
    decltype(&::WinHttpCrackUrl) CrackUrl = nullptr; decltype(&::WinHttpConnect) Connect = nullptr; decltype(&::WinHttpOpenRequest) OpenRequest = nullptr;
    decltype(&::WinHttpSendRequest) SendRequest = nullptr; decltype(&::WinHttpReceiveResponse) ReceiveResponse = nullptr;
    decltype(&::WinHttpQueryHeaders) QueryHeaders = nullptr; decltype(&::WinHttpReadData) ReadData = nullptr; decltype(&::WinHttpCloseHandle) CloseHandle = nullptr;
    static const Http& Get() {
        static const Http api = [] {
            Http a; HMODULE m = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto get = [&](auto& fn, const char* name) { fn = m ? reinterpret_cast<std::remove_reference_t<decltype(fn)>>(reinterpret_cast<void (*)()>(GetProcAddress(m, name))) : nullptr; return fn != nullptr; };
            a.ready = get(a.Open, "WinHttpOpen") & get(a.SetTimeouts, "WinHttpSetTimeouts") & get(a.SetOption, "WinHttpSetOption") & get(a.CrackUrl, "WinHttpCrackUrl") &
                      get(a.Connect, "WinHttpConnect") & get(a.OpenRequest, "WinHttpOpenRequest") & get(a.SendRequest, "WinHttpSendRequest") &
                      get(a.ReceiveResponse, "WinHttpReceiveResponse") & get(a.QueryHeaders, "WinHttpQueryHeaders") & get(a.ReadData, "WinHttpReadData") & get(a.CloseHandle, "WinHttpCloseHandle");
            return a;
        }();
        return api;
    }
};
inline void CloseSession(HINTERNET s) { if (s && Http::Get().ready) Http::Get().CloseHandle(s); }
// A WinHTTP session for the fetches (CloseSession when done). `direct`: no proxy at all (tests on the loopback);
// otherwise the system's proxy settings, as the browser's own updater uses them.
inline HINTERNET OpenSession(bool direct = false) {
    const Http& h = Http::Get(); if (!h.ready) return nullptr;
    const wchar_t* agent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)";   // i18n-exempt: HTTP user agent
    HINTERNET s = direct ? h.Open(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)
                         : h.Open(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s && !direct) s = h.Open(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return nullptr;
    h.SetTimeouts(s, 5000, 5000, 5000, 8000);   // resolve, connect, send, receive (ms)
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!h.SetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        h.SetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    return s;
}
namespace detail {
struct Url { bool secure = false; std::wstring host, path; INTERNET_PORT port = 0; };
// An absolute http(s) address WinHTTP can ask: printable ASCII only, no user info.
inline bool Crack(const std::wstring& url, Url& out) {
    if (url.empty() || url.size() > 2048 || !Http::Get().ready) return false;
    for (wchar_t c : url) if (c <= 0x20 || c >= 0x7f) return false;
    URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = uc.dwUserNameLength = uc.dwPasswordLength = DWORD(-1);
    if (!Http::Get().CrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) return false;
    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    if ((uc.lpszUserName && uc.dwUserNameLength) || (uc.lpszPassword && uc.dwPasswordLength) || !uc.lpszHostName || !uc.dwHostNameLength) return false;
    out.secure = uc.nScheme == INTERNET_SCHEME_HTTPS; out.port = uc.nPort;
    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.path.assign(uc.lpszUrlPath ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : std::wstring());
    if (uc.lpszExtraInfo) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    const auto hash = out.path.find(L'#'); if (hash != std::wstring::npos) out.path.resize(hash);
    if (out.path.empty() || out.path[0] != L'/') out.path.insert(out.path.begin(), L'/');
    return true;
}
inline std::wstring Origin(const std::wstring& url) {
    const auto start = url.find(L"://"); if (start == std::wstring::npos) return L"";
    const auto end = url.find_first_of(L"/?#", start + 3);
    return url.substr(0, end == std::wstring::npos ? url.size() : end);
}
inline std::wstring Lower(std::wstring s) { for (auto& c : s) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32); return s; }
} // namespace detail
// `target` resolved against the page at `base` (an absolute http(s) address): absolute, scheme-relative, root-relative
// or relative. Empty when it is any other scheme or not an address WinHTTP could ask.
inline std::wstring Resolve(const std::wstring& base, std::wstring target) {
    target = AddressBar::Trim(target);
    const auto hash = target.find(L'#'); if (hash != std::wstring::npos) target.resize(hash);
    if (target.empty()) return L"";
    for (auto& c : target) if (c == L'\\') c = L'/';
    const std::wstring low = detail::Lower(target.substr(0, 8));
    std::wstring out;
    if (low.rfind(L"http://", 0) == 0 || low.rfind(L"https://", 0) == 0) out = target;
    else if (target.rfind(L"//", 0) == 0) out = base.substr(0, base.find(L':')) + L":" + target;
    else if (target.find(L':') != std::wstring::npos && target.find(L':') < target.find(L'/')) return L"";   // another scheme (data:, javascript:, …)
    else if (target[0] == L'/') out = detail::Origin(base) + target;
    else {
        std::wstring dir = base.substr(0, base.find_first_of(L"?#", base.find(L"://") + 3));
        const auto slash = dir.find_last_of(L'/');
        if (slash == std::wstring::npos || slash < base.find(L"://") + 3) dir = detail::Origin(base) + L"/"; else dir.resize(slash + 1);
        out = dir + target;
    }
    std::wstring encoded;   // spaces in an href are common: encoded, anything else unprintable refuses the address
    for (wchar_t c : out) { if (c == L' ') encoded += L"%20"; else if (c < 0x20 || c >= 0x7f) return L""; else encoded += c; }
    detail::Url u; return detail::Crack(encoded, u) ? encoded : L"";
}
// The icons a page declares in its <link> tags, best first: « icon » / « shortcut icon » in document order (the one
// closest to Icon::target wide when sizes are given), then « apple-touch-icon ». SVG and mask icons are skipped (the
// browser draws raster icons only). Inline « data:image/… » icons are kept as they are. At most Bound::links.
inline std::vector<std::wstring> IconLinks(const std::string& html, const std::wstring& base) {
    const std::wstring text = BI::DecodeHtmlBytes(html.substr(0, Bound::page)), low = BI::Html::Lower(text);
    struct Found { int rank; std::int64_t score; std::size_t at; std::wstring url; };
    std::vector<Found> found;
    for (std::size_t at = 0; (at = low.find(L"<link", at)) != std::wstring::npos && found.size() < 64; at += 5) {
        if (at + 5 < low.size() && !iswspace(low[at + 5])) continue;
        const auto gt = low.find(L'>', at); if (gt == std::wstring::npos) break;
        const std::wstring tag = text.substr(at + 5, gt - at - 5);
        std::wstring rel = BI::Html::Lower(BI::Html::Attribute(tag, L"rel")), type = BI::Html::Lower(BI::Html::Attribute(tag, L"type"));
        const std::wstring href = BI::Html::Attribute(tag, L"href");
        std::vector<std::wstring> tokens; { std::wstring t; for (wchar_t c : rel + L" ") { if (iswspace(c)) { if (!t.empty()) tokens.push_back(t); t.clear(); } else t += c; } }
        auto has = [&](const wchar_t* k) { return std::find(tokens.begin(), tokens.end(), k) != tokens.end(); };
        int rank = has(L"icon") ? 0 : (has(L"apple-touch-icon") || has(L"apple-touch-icon-precomposed")) ? 1 : -1;
        if (rank < 0 || has(L"mask-icon") || type.find(L"svg") != std::wstring::npos || href.empty()) continue;
        std::wstring path = detail::Lower(href); const auto q = path.find_first_of(L"?#"); if (q != std::wstring::npos) path.resize(q);
        if (path.size() >= 4 && path.compare(path.size() - 4, 4, L".svg") == 0) continue;
        std::int64_t score = BI::Icon::Score(0);
        const std::wstring sizes = BI::Html::Lower(BI::Html::Attribute(tag, L"sizes"));
        if (!sizes.empty() && sizes.find(L"any") == std::wstring::npos) {
            const std::int64_t w = std::wcstoll(sizes.c_str(), nullptr, 10);   // « 32x32 » (the first size given)
            score = BI::Icon::Score(w);
        }
        std::wstring url;
        if (detail::Lower(href.substr(0, 11)) == L"data:image/") url = href;
        else url = Resolve(base, href);
        if (!url.empty()) found.push_back({rank, score, at, url});
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.rank != b.rank ? a.rank < b.rank : a.score != b.score ? a.score < b.score : a.at < b.at; });
    std::vector<std::wstring> out;
    for (const auto& f : found) if (out.size() < static_cast<std::size_t>(Bound::links) && std::find(out.begin(), out.end(), f.url) == out.end()) out.push_back(f.url);
    return out;
}
// GET `url`: the body of a 200 answer, at most `limit` bytes (`cut`: a longer body is cut there, else refused).
// `landed`: the address that answered (after redirects). Redirects are followed by hand, Bound::hops at most, to http
// or https only and never from https to http. No cookie, no authentication, no credentials ever sent.
inline std::string Get(HINTERNET session, const std::wstring& start, std::size_t limit, bool cut, const wchar_t* accept, std::wstring* landed = nullptr) {
    const Http& h = Http::Get();
    if (!session || !h.ready) return {};
    std::wstring url = start; bool wasSecure = false;
    for (int hop = 0; hop <= Bound::hops; ++hop) {
        detail::Url u;
        if (!detail::Crack(url, u) || (wasSecure && !u.secure)) return {};
        wasSecure = u.secure;
        HINTERNET connection = h.Connect(session, u.host.c_str(), u.port, 0);
        if (!connection) return {};
        HINTERNET request = h.OpenRequest(connection, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0);
        auto done = [&] { if (request) h.CloseHandle(request); h.CloseHandle(connection); };
        DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;   // never the Windows account's credentials, not even on the intranet
        if (!request || !h.SetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) || !h.SetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon))) { done(); return {}; }
        const std::wstring headers = std::wstring(L"Accept: ") + accept;   // i18n-exempt: HTTP header
        if (!h.SendRequest(request, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !h.ReceiveResponse(request, nullptr)) { done(); return {}; }
        DWORD status = 0, size = sizeof(status);
        h.QueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            wchar_t location[2048]; DWORD bytes = sizeof(location);
            const bool found = h.QueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location, &bytes, WINHTTP_NO_HEADER_INDEX);
            done();
            if (!found) return {};
            url = Resolve(url, std::wstring(location, bytes / sizeof(wchar_t)));
            if (url.empty()) return {};
            continue;
        }
        if (status != 200) { done(); return {}; }
        std::string body; bool over = false;
        for (;;) {
            char chunk[16384]; DWORD got = 0;
            if (!h.ReadData(request, chunk, sizeof(chunk), &got)) { body.clear(); break; }
            if (got == 0) break;
            if (body.size() + got > limit) { if (cut) body.append(chunk, limit - body.size()); else over = true; break; }
            body.append(chunk, got);
        }
        done();
        if (over) return {};
        if (landed) *landed = url;
        return body;
    }
    return {};   // too many redirects
}
// One site's icon (`origin`: « scheme://host[:port] »), or empty: its /favicon.ico, else the icons its home page
// declares, best first. Only a raster image within BI::Icon::bytes is ever returned.
inline std::string Fetch(HINTERNET session, const std::wstring& origin) {
    const wchar_t* image = L"image/png,image/x-icon,image/vnd.microsoft.icon,image/gif,image/jpeg,image/bmp;q=0.9,*/*;q=0.5";   // i18n-exempt: HTTP header value
    std::string bytes = Get(session, origin + L"/favicon.ico", BI::Icon::bytes, false, image);
    if (BI::Icon::Raster(bytes)) return bytes;
    std::wstring page;
    const std::string html = Get(session, origin + L"/", Bound::page, true, L"text/html,application/xhtml+xml;q=0.9,*/*;q=0.5", &page);   // i18n-exempt: HTTP header value
    if (html.empty()) return {};
    for (const std::wstring& link : IconLinks(html, page.empty() ? origin + L"/" : page)) {
        bytes = detail::Lower(link.substr(0, 5)) == L"data:" ? BI::Icon::DataImage(link) : Get(session, link, BI::Icon::bytes, false, image);
        if (BI::Icon::Raster(bytes)) return bytes;
    }
    return {};
}

} // namespace ImportIcons
