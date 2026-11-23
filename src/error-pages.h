#pragma once
#include "i18n.h"
// C3/C4 · Pages d'erreur réseau, redesign #121 (Paper « Browser Design », page « #121 — Fenêtres du
// moteur à notre design »: Hors ligne 1LXF-0, Site non sécurisé 1LZP-0, variantes réseau 1M1W-0, certificat
// refusé 1M3Q-0, détails techniques B « fiche » 1M88-0; behaviour notes of the older C3/C4 boards EOM-0).
// Self-contained HTML documents that main shows with NavigateToString, like the HTTPS
// interstitial: a per-page random token, fonts as data: URIs, a strict CSP, no network.
// Buttons post a string (JSON) through chrome.webview.postMessage; ParseMessage() accepts it
// only with the right token and an allow-listed action. Every page-provided or URL-derived text
// is HTML-escaped (the URL cannot inject markup or script).
//
// Paper values (get_jsx): see Head() (column, type, buttons), Sheet() (technical details), the icons
// (Jiggicons glass, tinted). Status row 24 above (+16 gap), dot 6 radius 3 (waiting #FFFFFF66 / back online
// accent #3E67FF), 14/20 #FFFFFF80 (#FFFFFFB3 when restored); auto-retry « Nouvel essai dans 8 s » 14/20
// tnum #FFFFFF80 + 120x2 track #FFFFFF14 / fill #FFFFFF80; « Rechargement… » primary at 60 %.
// Behaviour (EOP-0, EOV-0): offline reloads once when the network returns (the page first turns to the
// « Reprise » state); time-out retries automatically at 8, 16 and 32 s then stops; DNS and refused never
// auto-retry; focus starts on the primary action, Entrée triggers it, F5 / Ctrl+R = Réessayer.
#include <windows.h>
#include <WebView2.h>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace NativeChrome {
// Restricted JSON: one flat object of string values (or a JSON string that contains one, as
// ICoreWebView2::get_WebMessageAsJson reports a posted string). Anything else is rejected.
namespace FlatJson {
inline bool Hex4(const std::wstring& s, size_t& i, unsigned& v) {
    if (i + 4 > s.size()) return false; v = 0;
    for (int k = 0; k < 4; ++k) { const wchar_t c = s[i++]; v <<= 4; if (c >= L'0' && c <= L'9') v |= c - L'0'; else if (c >= L'a' && c <= L'f') v |= c - L'a' + 10; else if (c >= L'A' && c <= L'F') v |= c - L'A' + 10; else return false; }
    return true;
}
inline void Space(const std::wstring& s, size_t& i) { while (i < s.size() && (s[i] == L' ' || s[i] == L'\t' || s[i] == L'\r' || s[i] == L'\n')) ++i; }
inline bool String(const std::wstring& s, size_t& i, std::wstring& out, size_t cap) {
    if (i >= s.size() || s[i] != L'"') return false; ++i; out.clear();
    while (i < s.size()) {
        const wchar_t c = s[i++];
        if (c == L'"') return true;
        if (c < 0x20) return false;
        if (c != L'\\') { out += c; } else {
            if (i >= s.size()) return false; const wchar_t e = s[i++];
            switch (e) {
            case L'"': out += L'"'; break; case L'\\': out += L'\\'; break; case L'/': out += L'/'; break;
            case L'b': out += L'\b'; break; case L'f': out += L'\f'; break; case L'n': out += L'\n'; break; case L'r': out += L'\r'; break; case L't': out += L'\t'; break;
            case L'u': { unsigned v = 0; if (!Hex4(s, i, v)) return false; out += static_cast<wchar_t>(v); break; }
            default: return false;
            }
        }
        if (out.size() > cap) return false;
    }
    return false;
}
// `maxLength` bounds the whole message, `maxKeys` the members.
inline std::optional<std::map<std::wstring, std::wstring>> Parse(std::wstring s, size_t maxLength = 1024, size_t maxKeys = 8) {
    if (s.size() > maxLength) return std::nullopt;
    size_t i = 0; Space(s, i);
    if (i < s.size() && s[i] == L'"') { std::wstring inner; if (!String(s, i, inner, maxLength)) return std::nullopt; Space(s, i); if (i != s.size()) return std::nullopt; s = inner; i = 0; Space(s, i); }
    if (i >= s.size() || s[i] != L'{') return std::nullopt; ++i;
    std::map<std::wstring, std::wstring> out; Space(s, i);
    if (i < s.size() && s[i] == L'}') { ++i; Space(s, i); return i == s.size() ? std::optional<std::map<std::wstring, std::wstring>>(out) : std::nullopt; }
    for (;;) {
        std::wstring key, value; Space(s, i);
        if (!String(s, i, key, 64)) return std::nullopt; Space(s, i);
        if (i >= s.size() || s[i] != L':') return std::nullopt; ++i; Space(s, i);
        if (!String(s, i, value, maxLength)) return std::nullopt;
        if (out.count(key) || out.size() >= maxKeys) return std::nullopt;
        out[key] = value; Space(s, i);
        if (i < s.size() && s[i] == L',') { ++i; continue; }
        if (i < s.size() && s[i] == L'}') { ++i; break; }
        return std::nullopt;
    }
    Space(s, i); if (i != s.size()) return std::nullopt;
    return out;
}
// Token comparison without an early exit on the first differing character.
inline bool SameToken(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || a.size() != b.size()) return false;
    unsigned diff = 0; for (size_t k = 0; k < a.size(); ++k) diff |= static_cast<unsigned>(a[k] ^ b[k]); return diff == 0;
}
// Only `allowed` members may appear; `required` must.
inline bool OnlyKeys(const std::map<std::wstring, std::wstring>& m, std::initializer_list<const wchar_t*> allowed, std::initializer_list<const wchar_t*> required) {
    for (auto& kv : m) { bool ok = false; for (auto* k : allowed) if (kv.first == k) ok = true; if (!ok) return false; }
    for (auto* k : required) if (!m.count(k)) return false;
    return true;
}
}
namespace WebText {
inline std::wstring Html(const std::wstring& s) {
    std::wstring r; r.reserve(s.size());
    for (wchar_t c : s) switch (c) { case L'&': r += L"&amp;"; break; case L'<': r += L"&lt;"; break; case L'>': r += L"&gt;"; break; case L'"': r += L"&quot;"; break; case L'\'': r += L"&#39;"; break; default: r += c < 0x20 ? L' ' : c; }
    return r;
}
// A JS string literal body (inside "…"): no quote, backslash, line terminator or "</script>" escapes.
inline std::wstring Js(const std::wstring& s) {
    static const wchar_t* hex = L"0123456789abcdef"; std::wstring r; r.reserve(s.size() + 8);
    for (wchar_t c : s) {
        if (c == L'\\') r += L"\\\\"; else if (c == L'"') r += L"\\\""; else if (c == L'\'') r += L"\\'";
        else if (c < 0x20 || c == 0x2028 || c == 0x2029 || c == L'<' || c == L'>' || c == L'&' || c == 0x7F) { r += L"\\u"; for (int k = 12; k >= 0; k -= 4) r += hex[(c >> k) & 15]; }
        else r += c;
    }
    return r;
}
}

namespace ErrorPages {
enum class Kind { Offline, NameNotResolved, Refused, TimedOut, Reset, Unreachable, Generic, Certificate };
enum class Action { Retry, Back, Details, Search, NetworkSettings, AutoRetry, Copy };
// Optional richer content. Fonts are data: URIs (main's DataUri of assets\fonts\*.ttf).
struct Extras {
    std::wstring fontRegular, fontMedium, fontSemiBold, fontMono;   // Inter Display 400 / 500 / 600, JetBrains Mono (only on a page with a sheet)
    std::wstring code;          // sheet « Code »: ERR_CONNECTION_REFUSED (certificate pages: CertificateCode)
    std::wstring address;       // sheet « Adresse »: the host:port that was tried (AddressOf)
    std::wstring tries;         // sheet « Essai »: « 2e tentative · 14:32 » (TriesText)
    int attempt = 0;            // automatic retries already made (time-out: 8, 16, 32 s, then none)
    bool restored = false;      // offline page: the network came back, reloading
    bool reducedMotion = false;
    std::wstring issuer;        // Certificate: the refused certificate's issuer (shown as text, bounded)
    int certStatus = -1;        // Certificate: the engine's status (cause and code)
};
// Server certificate statuses (TLS refused by the engine, never bypassed).
inline bool IsCertificate(int status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT: case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS: case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID: return true;
    default: return false;
    }
}
// The refused request is the navigation's own URL (scheme/host/port/path, case-insensitive scheme+host).
inline bool CertificateMatches(int recordedStatus, const std::wstring& requestUri, const std::wstring& target) {
    if (recordedStatus < 0 || requestUri.empty() || target.empty()) return false;
    auto norm = [](std::wstring u) { const auto s = u.find(L"://"); if (s == std::wstring::npos) return u; auto end = u.find_first_of(L"/?#", s + 3); if (end == std::wstring::npos) { u += L"/"; end = u.size() - 1; } for (size_t i = 0; i < end; ++i) u[i] = static_cast<wchar_t>(towlower(u[i])); const auto h = u.find(L'#'); return h == std::wstring::npos ? u : u.substr(0, h); };
    return norm(requestUri) == norm(target);
}
// Plain-language cause of a refused server certificate.
inline std::wstring CertificateCause(int status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT: return TR(error_cert_wrong_name);
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED: return TR(error_cert_expired);
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED: return TR(error_cert_revoked);
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS: return TR(error_cert_client_errors);
    default: return TR(error_cert_invalid);   // CERTIFICATE_IS_INVALID is generic: no finer cause claimed
    }
}
// The engine's own status name (COREWEBVIEW2_WEB_ERROR_STATUS_*): exactly what WebView2 reports,
// never a guessed Chromium NET:: code (the SDK does not expose it). No « WebView2 : » prefix (#121, sheet B).
inline const wchar_t* CertificateCode(int status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT: return L"CERTIFICATE_COMMON_NAME_IS_INCORRECT";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED: return L"CERTIFICATE_EXPIRED";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED: return L"CERTIFICATE_REVOKED";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS: return L"CLIENT_CERTIFICATE_CONTAINS_ERRORS";
    default: return L"CERTIFICATE_IS_INVALID";
    }
}
inline constexpr int AutoRetryDelaysSeconds[] = {8, 16, 32};
inline int AutoRetryDelay(Kind kind, int attempt) { return kind == Kind::TimedOut && attempt >= 0 && attempt < 3 ? AutoRetryDelaysSeconds[attempt] : 0; }

// True when the failure gets our page; false leaves the engine's own (cancelled navigations,
// certificate errors handled by the HTTPS logic, authentication challenges).
inline bool Handles(int status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT: case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS: case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID:
    case COREWEBVIEW2_WEB_ERROR_STATUS_VALID_AUTHENTICATION_CREDENTIALS_REQUIRED: case COREWEBVIEW2_WEB_ERROR_STATUS_VALID_PROXY_AUTHENTICATION_REQUIRED:
        return false;
    default: return true;
    }
}
// A failed navigation whose document IS the server's own answer: WebView2 reports IsSuccess FALSE for an
// HTTP 4xx/5xx response (ICoreWebView2NavigationCompletedEventArgs::IsSuccess: « HTTP server responds with
// 4xx »), with WebErrorStatus UNKNOWN, and its document is the page the site sent (a 403 challenge, a 404, a
// 503 maintenance page...). HttpStatusCode (args2) is 0 when no response was received (DNS, refused,
// reset...) and a success code for a cut response: those stay transport failures. Only UNKNOWN + a real
// 4xx/5xx keeps the site's page; any other status (certificate, reset, aborted, timeout...) is unchanged.
inline bool KeepsServerPage(bool success, int webErrorStatus, int httpStatusCode) {
    return !success && webErrorStatus == COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN && httpStatusCode >= 400 && httpStatusCode <= 599;
}
inline Kind Classify(int status, bool online) {
    if (!online || status == COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED) return Kind::Offline;
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED: return Kind::NameNotResolved;
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT: return Kind::Refused;
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT: return Kind::TimedOut;
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET: case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED: return Kind::Reset;
    case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE: return Kind::Unreachable;
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT: case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS: case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED:
    case COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID: return Kind::Certificate;
    default: return Kind::Generic;
    }
}
inline const wchar_t* Code(int status) {
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED: return L"ERR_NAME_NOT_RESOLVED";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT: return L"ERR_CONNECTION_REFUSED";
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT: return L"ERR_TIMED_OUT";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET: return L"ERR_CONNECTION_RESET";
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED: return L"ERR_CONNECTION_ABORTED";
    case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE: return L"ERR_ADDRESS_UNREACHABLE";
    case COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED: return L"ERR_INTERNET_DISCONNECTED";
    default: return L"ERR_FAILED";
    }
}
// Host shown in the copy: « lemonde.fr » for https://www.lemonde.fr/planete/, port kept
// (« localhost:3000 »); the raw text when it is not a URL.
inline std::wstring HostOf(const std::wstring& url) {
    size_t start = url.find(L"://"); start = start == std::wstring::npos ? 0 : start + 3;
    size_t end = url.find_first_of(L"/?#", start); std::wstring host = url.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (const size_t at = host.rfind(L'@'); at != std::wstring::npos) host = host.substr(at + 1);  // never show credentials
    if (host.size() > 4 && (host.compare(0, 4, L"www.") == 0 || host.compare(0, 4, L"WWW.") == 0)) host = host.substr(4);
    if (host.size() > 253) host = host.substr(0, 253) + L"…";
    return host.empty() ? url.substr(0, 253) : host;
}
// Sheet « Adresse »: the authority that was tried, credentials removed, with its port (the scheme's
// default when the URL has none): « localhost:3000 », « www.lemonde.fr:443 ». The board's resolved IP and
// « sans proxy » are not reported by WebView2 for a failed navigation, so they are never claimed.
inline std::wstring AddressOf(const std::wstring& url) {
    const size_t s = url.find(L"://"); if (s == std::wstring::npos) return L"";
    std::wstring scheme = url.substr(0, s); for (auto& ch : scheme) ch = static_cast<wchar_t>(towlower(ch));
    const size_t end = url.find_first_of(L"/?#", s + 3);
    std::wstring a = url.substr(s + 3, end == std::wstring::npos ? std::wstring::npos : end - s - 3);
    if (const size_t at = a.rfind(L'@'); at != std::wstring::npos) a = a.substr(at + 1);
    if (a.empty()) return L"";
    const size_t close = a.rfind(L']'), colon = a.rfind(L':');
    if (colon == std::wstring::npos || (close != std::wstring::npos && colon < close)) { if (scheme == L"https") a += L":443"; else if (scheme == L"http") a += L":80"; }
    if (a.size() > 253) a = a.substr(0, 253) + L"…";
    return a;
}
// Sheet « Essai »: the attempt (consecutive failures of this address in the tab) and the local time.
inline std::wstring TriesText(int tries, const std::wstring& clock) {
    std::wstring s = tries <= 1 ? std::wstring(TR(error_attempt_first)) : TRF(error_attempt_nth, std::to_wstring(tries));
    if (!clock.empty()) s += L" · " + clock;
    return s;
}
// `body` is a whole sentence: {0} is the host (escaped, then set in bold by Html()), {1} the certificate cause.
struct Copy { std::wstring title, body, primary, secondary; Action primaryAction = Action::Retry, secondaryAction = Action::Details; };
inline Copy CopyFor(Kind kind, const std::wstring& host) {
    switch (kind) {
    case Kind::Offline: return {TR(error_offline_title), TR(error_offline_body), TR(downloads_action_retry), TR(error_network_settings), Action::Retry, Action::NetworkSettings};
    case Kind::NameNotResolved: return {TR(error_dns_title), TR(error_dns_body), TRF(error_search_host, host), TR(downloads_action_retry), Action::Search, Action::Retry};
    case Kind::Refused: return {TR(error_refused_title), TR(error_refused_body), TR(downloads_action_retry), TR(error_details)};
    case Kind::TimedOut: return {TR(error_timeout_title), TR(error_timeout_body), TR(error_retry_now), TR(error_details)};
    case Kind::Reset: return {TR(error_reset_title), TR(error_reset_body), TR(downloads_action_retry), TR(error_details)};
    // TLS refused (never bypassed): no « continuer », no http; Retour or a real new attempt.
    case Kind::Certificate: return {TR(error_cert_title), TR(error_cert_body), TR(downloads_action_retry), TR(common_back), Action::Retry, Action::Back};
    case Kind::Unreachable: return {TR(error_unreachable_title), TR(error_unreachable_body), TR(downloads_action_retry), TR(error_details)};
    default: return {TR(error_generic_title), TR(error_generic_body), TR(downloads_action_retry), TR(error_details)};
    }
}
// Button order of the action row (Val's decision 2026-11-23, ticket #121, as the boards draw it): the primary
// action on the left (« Réessayer », « Rechercher », « Réessayer maintenant »), the other one on the right; only
// « Retour » (certificate pages) stays left of « Réessayer ». Replaces the 2026-11-09 order (« Réessayer » right).
inline bool PrimaryLast(const Copy& c) { return c.secondaryAction == Action::Back; }
inline const wchar_t* ActionName(Action a) {
    switch (a) { case Action::Retry: return L"retry"; case Action::Back: return L"back"; case Action::Details: return L"details"; case Action::Search: return L"search"; case Action::NetworkSettings: return L"network"; case Action::AutoRetry: return L"autoRetry"; case Action::Copy: return L"copy"; }
    return L"";
}

// ---- Icons: Jiggicons, glass variant, as tinted on the #121 boards (glass part dark, never grey; 48 DIP,
// viewBox 24). Path data and fills are the boards' own (get_jsx), unused gradients left out.
// i18n-exempt-begin: SVG markup (path data, no text)
inline const wchar_t* IconWifiOff() {   // « Hors ligne » 1M60-0: wifi-off, danger
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="wo" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF8A7E"/><stop offset="1" stop-color="#E0483A"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M216.88,207.93a12,12,0,1,1-17.76,16.14L147.78,167.6a56.06,56.06,0,0,0-52.71,7.1,12,12,0,0,1-14.14-19.4,79.35,79.35,0,0,1,41.92-15.12L103.51,118.9a104.18,104.18,0,0,0-40.06,19.55,12,12,0,0,1-14.9-18.81A128.46,128.46,0,0,1,85.61,99.21l-17.31-19a151.14,151.14,0,0,0-36.68,22.28A12,12,0,1,1,16.39,83.91a175.52,175.52,0,0,1,35-22.38L39.12,48.07A12,12,0,1,1,56.88,31.93ZM128,188a16,16,0,1,0,16,16A16,16,0,0,0,128,188Zm64.55-49.55a12,12,0,0,0,14.9-18.81A127.27,127.27,0,0,0,170,99.05a12,12,0,0,0-7.87,22.67A103.62,103.62,0,0,1,192.55,138.45Zm47.06-54.54A176.33,176.33,0,0,0,128,44c-3.94,0-7.93.13-11.86.39a12,12,0,1,0,1.59,24c3.4-.23,6.86-.34,10.27-.34a152.24,152.24,0,0,1,96.38,34.46,12,12,0,1,0,15.23-18.55Z" fill="url(#wo)"/></g></svg>)SVG";
}
inline const wchar_t* IconWifiCheck() {   // « Reprise » 1M74-0: wifi-checkmark, accent
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="wc" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#7A95FF"/><stop offset="1" stop-color="#3E67FF"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M144,204a16,16,0,1,1-16-16A16,16,0,0,1,144,204ZM239.61,83.91a176,176,0,0,0-223.22,0,12,12,0,1,0,15.23,18.55,152,152,0,0,1,192.76,0,12,12,0,1,0,15.23-18.55Zm-32.16,35.73a128,128,0,0,0-158.9,0,12,12,0,0,0,14.9,18.81,104,104,0,0,1,129.1,0,12,12,0,0,0,14.9-18.81ZM175.07,155.3a80.05,80.05,0,0,0-94.14,0,12,12,0,0,0,14.14,19.4,56,56,0,0,1,65.86,0,12,12,0,1,0,14.14-19.4Z" fill="url(#wc)"/></g></svg>)SVG";
}
inline const wchar_t* IconGlobeSearch() {   // « Introuvable » 1M6G-0: globe-search, danger
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="gs" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF8A7E"/><stop offset="1" stop-color="#E0483A"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M224.49,176.49,209,192l15.52,15.51a12,12,0,0,1-17,17L192,209l-15.51,15.52a12,12,0,0,1-17-17L175,192l-15.52-15.51a12,12,0,0,1,17-17L192,175l15.51-15.52a12,12,0,0,1,17,17ZM236,128a12,12,0,0,1-12,12H100.54c4.37,49.56,34.63,74.37,35,74.63h0A12,12,0,0,1,128,236,108,108,0,1,1,236,128ZM158.21,49.64A144.3,144.3,0,0,1,179.56,116h31.57A84.22,84.22,0,0,0,158.21,49.64ZM117.64,63.23A119.74,119.74,0,0,0,100.53,116h54.94c-3-33.9-18.09-56.22-27.45-67A107.75,107.75,0,0,0,117.64,63.23ZM44.87,116H76.44A144.3,144.3,0,0,1,97.79,49.64,84.22,84.22,0,0,0,44.87,116Zm31.57,24H44.87a84.22,84.22,0,0,0,52.92,66.36A144.3,144.3,0,0,1,76.44,140Z" fill="url(#gs)"/></g></svg>)SVG";
}
inline const wchar_t* IconServerError() {   // « Connexion refusée » 1M6O-0: server-error, danger
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="se" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF8A7E"/><stop offset="1" stop-color="#E0483A"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M208,36H48A20,20,0,0,0,28,56V200a20,20,0,0,0,20,20H208a20,20,0,0,0,20-20V56A20,20,0,0,0,208,36Zm-4,24v56H52V60ZM52,196V140H204v56ZM160,88a16,16,0,1,1,16,16A16,16,0,0,1,160,88Zm32,80a16,16,0,1,1-16-16A16,16,0,0,1,192,168Z" fill="url(#se)"/></g></svg>)SVG";
}
inline const wchar_t* IconHourglass() {   // « Délai dépassé » 1M6Y-0: hourglass, danger
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="hg" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF8A7E"/><stop offset="1" stop-color="#E0483A"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M204,75.64V40a20,20,0,0,0-20-20H72A20,20,0,0,0,52,40V76a20.1,20.1,0,0,0,8,16l48,36L60,164a20.1,20.1,0,0,0-8,16v36a20,20,0,0,0,20,20H184a20,20,0,0,0,20-20V180.36a20.13,20.13,0,0,0-7.94-16L147.9,128l48.16-36.4A20.13,20.13,0,0,0,204,75.64ZM180,212H76V182l52-39,52,39.33Zm0-138.35L128,113,76,74V44H180Z" fill="url(#hg)"/></g></svg>)SVG";
}
inline const wchar_t* IconShieldError() {   // « Certificat refusé » 1M7E-0 (and sheet B 1M93-0): shield-error, danger
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="sh" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF8A7E"/><stop offset="1" stop-color="#E0483A"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M116,132V96a12,12,0,0,1,24,0v36a12,12,0,0,1-24,0Zm12,56a16,16,0,1,0-16-16A16,16,0,0,0,128,188ZM228,56v56c0,54.29-26.32,87.22-48.4,105.29-23.71,19.39-47.44,26-48.44,26.29a12.1,12.1,0,0,1-6.32,0c-1-.28-24.73-6.9-48.44-26.29C54.32,199.22,28,166.29,28,112V56A20,20,0,0,1,48,36H208A20,20,0,0,1,228,56Zm-24,4H52v52c0,35.71,13.09,64.69,38.91,86.15A126.14,126.14,0,0,0,128,219.38a126.28,126.28,0,0,0,37.09-21.23C190.91,176.69,204,147.71,204,112Z" fill="url(#sh)"/></g></svg>)SVG";
}
inline const wchar_t* IconLockOpen() {   // « Site non sécurisé » 1M69-0: lock-open, danger (the board's lock body is a red glass, not the dark tint)
    return LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><defs><linearGradient id="la" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF8A7E"/><stop offset="1" stop-color="#E0483A"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M208,76H100V56a28,28,0,0,1,28-28c13.51,0,25.65,9.62,28.24,22.39a12,12,0,1,0,23.52-4.78C174.87,21.5,153.1,4,128,4A52.06,52.06,0,0,0,76,56V76H48A20,20,0,0,0,28,96V208a20,20,0,0,0,20,20H208a20,20,0,0,0,20-20V96A20,20,0,0,0,208,76Zm-4,128H52V100H204Z" fill="url(#la)"/></g></svg>)SVG";
}
// The sheet's copy button (1MAI-0): 14 px, #FFFFFF80.
inline const wchar_t* IconCopy() {
    return LR"SVG(<svg width="14" height="14" viewBox="0 0 256 256" aria-hidden="true" focusable="false"><path d="M216,28H88A12,12,0,0,0,76,40V76H40A12,12,0,0,0,28,88V216a12,12,0,0,0,12,12H168a12,12,0,0,0,12-12V180h36a12,12,0,0,0,12-12V40A12,12,0,0,0,216,28ZM156,204H52V100H156Zm48-48H180V88a12,12,0,0,0-12-12H100V52H204Z" fill="#FFFFFF80"/></svg>)SVG";
}
// i18n-exempt-end
// The board's icon of each variant. Reset, Unreachable and Generic have no board icon yet (none drawn
// on the #121 page): they keep the layout without one until the design gives theirs.
inline const wchar_t* IconFor(Kind kind) {
    switch (kind) {
    case Kind::Offline: return IconWifiOff();
    case Kind::NameNotResolved: return IconGlobeSearch();
    case Kind::Refused: return IconServerError();
    case Kind::TimedOut: return IconHourglass();
    case Kind::Certificate: return IconShieldError();
    default: return nullptr;
    }
}

// ---- Page shell shared by the error pages and the « Ce site n'est pas sécurisé » page.
// #121 layout (offline 1LXI-0, polished by Val): a 420 DIP column, 16 between icon / title+text /
// buttons / sheet, 8 between title and text. Title 20/28 Inter Display SemiBold #FFFFFFE6 balanced; text
// 14/20 Regular #FFFFFF99 (host Medium #FFFFFFE6); buttons h 32 radius 8 padding 14, 14 Medium. Title,
// text and buttons carry the boards' cv01-04/cv13; the sheet, the status and the countdown do not.
inline std::wstring Face(const wchar_t* family, const std::wstring& uri, const wchar_t* weight) {
    return std::wstring(L"@font-face{font-family:\"") + family + L"\";src:url(" + uri + L") format(\"truetype\");font-weight:" + weight + L";font-display:block}";
}
inline std::wstring Head(const std::wstring& title, const Extras& x, bool mono) {
    std::wstring h = std::wstring(L"<!doctype html><html lang=\"") + (I18n::Current() == I18n::Locale::En ? L"en" : L"fr") + LR"("><meta charset="utf-8"><meta name="color-scheme" content="dark">)"
        LR"(<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; font-src data:">)";
    h += L"<title>" + WebText::Html(title) + L"</title><style>";
    if (!x.fontRegular.empty()) h += Face(L"Inter Display", x.fontRegular, L"400");
    if (!x.fontMedium.empty()) h += Face(L"Inter Display", x.fontMedium, x.fontSemiBold.empty() ? L"500 600" : L"500");
    if (!x.fontSemiBold.empty()) h += Face(L"Inter Display", x.fontSemiBold, L"600");
    if (mono && !x.fontMono.empty()) h += Face(L"JetBrains Mono", x.fontMono, L"400");
    h += LR"CSS(
html,body{margin:0;height:100%;background:#171717;color:#FFFFFFE6;font-family:"Inter Display",Inter,system-ui,sans-serif;font-synthesis:none;-webkit-font-smoothing:antialiased}
body{display:flex;align-items:center;justify-content:center;padding:0 16px;box-sizing:border-box}
main{width:100%;max-width:420px;display:flex;flex-direction:column;align-items:center;gap:16px;text-align:center;overflow-wrap:anywhere}
.icon{display:flex;justify-content:center}.icon svg{display:block;flex:none}
.text{display:flex;flex-direction:column;align-items:center;gap:8px;max-width:100%}
h1{margin:0;font-size:20px;line-height:28px;font-weight:600;text-wrap:balance}
p{margin:0;font-size:14px;line-height:20px;color:#FFFFFF99;text-wrap:balance}
p b{font-weight:500;color:#FFFFFFE6}
.actions{display:flex;flex-direction:row;flex-wrap:nowrap;justify-content:center;gap:8px;max-width:100%}
button{height:32px;padding:0 14px;border:0;border-radius:8px;font:inherit;font-size:14px;line-height:32px;font-weight:500;cursor:pointer;flex:0 1 auto;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
h1,p,.actions button{font-feature-settings:"cv01","cv02","cv03","cv04","cv13"}
button[data-action=retry]{flex-shrink:0}
@media (max-width:359px){.actions{flex-direction:column;align-items:stretch;width:100%}}
.primary{background:#FFFFFFE6;color:#171717}.primary:hover{background:#FFFFFF}.primary:active{background:#FFFFFFCC}
.primary[aria-disabled=true]{opacity:.6;cursor:default}
.secondary{background:#FFFFFF14;color:#FFFFFFE6}.secondary:hover{background:#333333}.secondary:active{background:#3E3E3E}
)CSS";
    return h;
}
// Sheet B « fiche » (Paper 1M88-0 › B 1MAC-0; refused 1MDG-0; certificates 1MBN-0…1MD3-0): a 1 px #FFFFFF14
// frame, radius 8, no fill; rows padding 12 gap 12, label 13/18 #FFFFFF66 64 wide, value 13/18 #FFFFFFCC,
// the code in JetBrains Mono 12/18 on one clipped line with a 16x18 copy button; 1 px #FFFFFF0D rules inset
// 12. 340 wide, the column's 420 when the code does not fit (CERTIFICATE_COMMON_NAME_IS_INCORRECT,
// CLIENT_CERTIFICATE_CONTAINS_ERRORS on the board): 340 - 2 - 24 - 64 - 12 - 12 - 16 = 210 DIP = 29
// JetBrains Mono 12 px advances (7.2 DIP).
inline constexpr size_t SheetCodeFit = 29;
inline const wchar_t* SheetCss() {
    return LR"CSS(
.sheet{width:340px;max-width:100%;box-sizing:border-box;border:1px solid #FFFFFF14;border-radius:8px;display:flex;flex-direction:column;text-align:left}
.sheet.wide{width:420px}.sheet[hidden]{display:none}
.row{display:flex;align-items:center;gap:12px;padding:12px}
.sep{display:block;flex:none;height:1px;margin:0 12px;background:#FFFFFF0D}
.k{flex:none;width:64px;font-size:13px;line-height:18px;color:#FFFFFF66}
.v{flex:1 1 0;min-width:0;font-size:13px;line-height:18px;color:#FFFFFFCC}
.v.code{overflow:clip;white-space:pre;font-family:"JetBrains Mono",system-ui,sans-serif;font-size:12px;overflow-wrap:normal}
.sheet button{flex:none;width:16px;height:18px;padding:0;border-radius:6px;background:none;display:flex;align-items:center;justify-content:center;line-height:0;overflow:visible}
)CSS";
}
inline std::wstring Sheet(const std::wstring& code, const std::vector<std::pair<std::wstring, std::wstring>>& rows, bool collapsed) {
    const bool wide = code.size() > SheetCodeFit;
    std::wstring h = std::wstring(L"<div") + (collapsed ? L" id=\"details\"" : L"") + L" class=\"sheet" + (wide ? L" wide" : L"") + L"\"" + (collapsed ? L" hidden" : L"") + L">";   // i18n-exempt: HTML attributes
    h += L"<div class=\"row\"><span class=\"k\">" + WebText::Html(TR(error_sheet_code)) + L"</span><span class=\"v code\">" + WebText::Html(code) + L"</span>";
    h += L"<button type=\"button\" data-action=\"copy\" aria-label=\"" + WebText::Html(TR(error_copy_code)) + L"\">" + IconCopy() + L"</button></div>";
    for (const auto& row : rows) if (!row.second.empty()) h += L"<i class=\"sep\"></i><div class=\"row\"><span class=\"k\">" + WebText::Html(row.first) + L"</span><span class=\"v\">" + WebText::Html(row.second) + L"</span></div>";
    return h + L"</div>";
}

inline std::wstring Html(Kind kind, const std::wstring& url, const std::wstring& token, bool retrying, const Extras& extras = {}) {
    const std::wstring host = HostOf(url); const Copy c = CopyFor(kind, host);
    const bool offline = kind == Kind::Offline, details = c.secondaryAction == Action::Details, certificate = kind == Kind::Certificate;
    const bool sheet = details || certificate, restored = offline && extras.restored;
    const int delay = retrying ? 0 : AutoRetryDelay(kind, extras.attempt);
    std::wstring h = Head(c.title, extras, sheet);
    h += LR"CSS(button:focus-visible{outline:none;box-shadow:inset 0 0 0 1px #FFFFFF80}
.status{display:flex;align-items:center;gap:8px;padding-top:24px;font-size:14px;line-height:20px;color:#FFFFFF80}
.status i{width:6px;height:6px;border-radius:3px;background:#FFFFFF66;flex:none}
.back .status{color:#FFFFFFB3}.back .status i{background:#3E67FF}
.back .off,body:not(.back) .on{display:none}
.retry{display:flex;flex-direction:column;align-items:center;gap:8px;padding-top:24px;font-size:14px;line-height:20px;color:#FFFFFF80;font-feature-settings:"tnum"}
.track{width:120px;height:2px;border-radius:1px;background:#FFFFFF14;overflow:hidden}.fill{height:2px;border-radius:1px;background:#FFFFFF80;width:0}
)CSS";
    if (sheet) h += SheetCss();
    h += extras.reducedMotion ? L"" : L"main{animation:in 160ms ease-out}@keyframes in{from{opacity:0;transform:translateY(8px)}}@media (prefers-reduced-motion:reduce){main{animation:none}}";
    // The sentence is escaped first, then the escaped host (in bold) and cause go into it: page text never becomes markup.
    auto sentence = [&](const wchar_t* text) { return I18n::FormatText(WebText::Html(text).c_str(), {L"<b>" + WebText::Html(host) + L"</b>", WebText::Html(certificate ? CertificateCause(extras.certStatus) : std::wstring())}); };
    h += std::wstring(L"</style><body") + (restored ? L" class=\"back\"" : L"") + L"><main role=\"main\">";
    // Offline also carries the « Reprise » state (1M20-0: wifi-checkmark, shorter text), shown when the network returns.
    if (offline) h += std::wstring(L"<div class=\"icon off\">") + IconWifiOff() + L"</div><div class=\"icon on\">" + IconWifiCheck() + L"</div>";   // i18n-exempt: CSS classes
    else if (const wchar_t* icon = IconFor(kind)) h += std::wstring(L"<div class=\"icon\">") + icon + L"</div>";
    h += L"<div class=\"text\"><h1>" + WebText::Html(c.title) + L"</h1>";
    if (offline) h += L"<p class=\"off\">" + sentence(c.body.c_str()) + L"</p><p class=\"on\">" + sentence(TR(error_offline_body_restored)) + L"</p>";
    else h += L"<p>" + sentence(c.body.c_str()) + L"</p>";
    h += L"</div>";
    const std::wstring primaryLabel = retrying ? std::wstring(TR(error_reloading)) : c.primary;
    // One row for every variant, in DOM order so Tab follows what is seen (PrimaryLast). Labels never wrap
    // (a long « Rechercher « … » » ellipsises, « Réessayer » never shrinks); under 360 px (split view
    // halves) they stack, full width, same order.
    const std::wstring primaryButton = L"<button id=\"primary\" class=\"primary\" data-action=\"" + std::wstring(ActionName(c.primaryAction)) + L"\"" + (retrying ? L" aria-disabled=\"true\"" : L" autofocus" /* i18n-exempt: HTML attribute */) + L" title=\"" + WebText::Html(primaryLabel) + L"\">" + WebText::Html(primaryLabel) + L"</button>";
    const std::wstring secondaryButton = L"<button id=\"secondary\" class=\"secondary\" data-action=\"" + std::wstring(ActionName(c.secondaryAction)) + L"\"" + (details ? L" aria-expanded=\"false\" aria-controls=\"details\"" : L"") + L">" + WebText::Html(c.secondary) + L"</button>";
    h += L"<div class=\"actions\">" + (PrimaryLast(c) ? secondaryButton + primaryButton : primaryButton + secondaryButton) + L"</div>";
    if (details) h += Sheet(extras.code.empty() ? std::wstring(TR(error_unknown_code)) : extras.code, {{TR(error_sheet_address), extras.address}, {TR(error_sheet_attempt), extras.tries}}, true);
    if (certificate) {   // technical facts, escaped and bounded (never page-provided)
        std::wstring issuer = extras.issuer; if (issuer.size() > 200) issuer = issuer.substr(0, 200) + L"…";
        h += Sheet(CertificateCode(extras.certStatus), {{TR(error_sheet_issuer), issuer}}, false);
    }
    if (offline) h += L"<div class=\"status\" role=\"status\" aria-live=\"polite\"><i></i><span id=\"st\">" + WebText::Html(restored ? TR(error_connection_back) : TR(error_waiting_network)) + L"</span></div>";
    if (delay) h += L"<div class=\"retry\" role=\"status\" aria-live=\"polite\"><span id=\"count\">" + WebText::Html(TRF(error_retry_in, std::to_wstring(delay))) + L"</span><div class=\"track\" aria-hidden=\"true\"><div class=\"fill\" id=\"fill\"></div></div></div>";
    h += L"</main><script>\"use strict\";const T=\"" + WebText::Js(token) + L"\";let busy=" + (retrying ? L"true" : L"false") + L";";
    // Texts the script writes itself, from the catalog (escaped for a JS string), only where the page uses them.
    h += L"const RELOADING=\"" + WebText::Js(TR(error_reloading)) + L"\";";
    if (delay) h += L"const RETRY_IN=\"" + WebText::Js(TR(error_retry_in)) + L"\";";
    if (offline) h += L"const BACK=\"" + WebText::Js(TR(error_connection_back)) + L"\";";
    h += LR"JS(const send=a=>{try{window.chrome.webview.postMessage(JSON.stringify({token:T,action:a}))}catch(e){}};
const P=document.getElementById("primary"),S=document.getElementById("secondary"),D=document.getElementById("details");
const reload=a=>{if(busy)return;busy=true;if(P.dataset.action==="retry"){P.textContent=RELOADING;P.setAttribute("aria-disabled","true")}send(a)};
P.addEventListener("click",()=>{if(P.dataset.action==="retry")reload("retry");else send(P.dataset.action)});
S.addEventListener("click",()=>{const a=S.dataset.action;if(a==="details"&&D){const open=D.hidden;D.hidden=!open;S.setAttribute("aria-expanded",String(open));send("details")}else if(a==="retry")reload("retry");else send(a)});
for(const C of document.querySelectorAll("[data-action=copy]"))C.addEventListener("click",()=>send("copy"));
addEventListener("keydown",e=>{if(e.key==="F5"||((e.ctrlKey||e.metaKey)&&(e.key==="r"||e.key==="R"))){e.preventDefault();reload("retry")}});
)JS";
    if (offline) h += LR"JS(addEventListener("online",()=>{document.body.classList.add("back");document.getElementById("st").textContent=BACK;reload("retry")});)JS";
    if (delay) {
        h += L"{const total=" + std::to_wstring(delay) + L";let left=total;const C=document.getElementById(\"count\"),F=document.getElementById(\"fill\");";
        h += LR"JS(const tick=()=>{F.style.width=((total-left)/total*120)+"px";C.textContent=RETRY_IN.replace("{0}",left);if(left<=0){if(!busy){busy=true;send("autoRetry")}return}left--;setTimeout(tick,1000)};tick()})JS";
    }
    h += L"</script></html>";
    return h;
}
// « Ce site n'est pas sécurisé » (HTTPS uniquement, Paper 1LZS-0): the same shell with the lock-open icon,
// « Revenir » (primary, focused, the board's 2 px #FFFFFFE6 ring 2 px out) then « Continuer quand même ».
// Its buttons post « <token>:back » / « <token>:continue » (main's InterstitialMessage).
inline std::wstring InsecureHtml(const std::wstring& host, const std::wstring& token, const Extras& extras = {}) {
    const std::wstring title = TR(insecure_title);
    std::wstring h = Head(title, extras, false);
    h += L"button:focus-visible{outline:2px solid #FFFFFFE6;outline-offset:2px}";
    h += extras.reducedMotion ? L"" : L"main{animation:in 150ms ease-out}@keyframes in{from{opacity:0}}@media (prefers-reduced-motion:reduce){main{animation:none}}";
    h += std::wstring(L"</style><body><main role=\"main\"><div class=\"icon\">") + IconLockOpen() + L"</div><div class=\"text\"><h1>" + WebText::Html(title) + L"</h1><p>";
    // The host is escaped, then put in bold inside the escaped sentence (never the other way round).
    h += I18n::FormatText(WebText::Html(TR(insecure_body)).c_str(), {L"<b>" + WebText::Html(host) + L"</b>"});
    h += L"</p></div><div class=\"actions\"><button id=\"back\" class=\"primary\" autofocus>" + WebText::Html(TR(insecure_back)) + L"</button><button id=\"go\" class=\"secondary\">" + WebText::Html(TR(insecure_continue)) + L"</button></div></main>";
    h += L"<script>\"use strict\";const t=\"" + WebText::Js(token) + L"\";";
    h += LR"JS(const send=a=>window.chrome.webview.postMessage(t+":"+a);document.getElementById("back").onclick=()=>send("back");document.getElementById("go").onclick=()=>send("continue");</script></html>)JS";
    return h;
}
// The page's message: the string given to postMessage (TryGetWebMessageAsString) or its JSON
// form. Accepted only as {"token":"<this page's token>","action":"<allow-listed>"}.
inline std::optional<Action> ParseMessage(const std::wstring& message, const std::wstring& token) {
    auto m = FlatJson::Parse(message, 512, 2); if (!m || !FlatJson::OnlyKeys(*m, {L"token", L"action"}, {L"token", L"action"})) return std::nullopt;
    if (!FlatJson::SameToken((*m)[L"token"], token)) return std::nullopt;
    const auto& a = (*m)[L"action"];
    for (Action candidate : {Action::Retry, Action::Back, Action::Details, Action::Search, Action::NetworkSettings, Action::AutoRetry, Action::Copy}) if (a == ActionName(candidate)) return candidate;
    return std::nullopt;
}
} // namespace ErrorPages
} // namespace NativeChrome
