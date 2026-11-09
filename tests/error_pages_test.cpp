// C3/C4 network error pages: classification, copy, escaping, token-gated message bridge.
// Pure functions: no window, no WebView2. Built and run by test-sandbox.ps1.
#include "../src/error-pages.h"
#include <cstdio>
#include <cstdlib>

using namespace NativeChrome;
using ErrorPages::Action; using ErrorPages::Kind;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static bool Has(const std::wstring& s, const std::wstring& part) { return s.find(part) != std::wstring::npos; }
static size_t Count(const std::wstring& s, const std::wstring& part) { size_t n = 0; for (size_t i = s.find(part); i != std::wstring::npos; i = s.find(part, i + 1)) ++n; return n; }
static std::wstring Msg(const std::wstring& token, const std::wstring& action) { return L"{\"token\":\"" + token + L"\",\"action\":\"" + action + L"\"}"; }

int main() {
    // Classify: WebView2 status → Paper variant; offline wins over everything.
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED, true) == Kind::NameNotResolved, "DNS");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT, true) == Kind::Refused, "refused");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT, true) == Kind::TimedOut, "timeout");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET, true) == Kind::Reset, "reset");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED, true) == Kind::Reset, "aborted = reset");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE, true) == Kind::Unreachable, "unreachable");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED, true) == Kind::Offline, "disconnected = offline");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_UNEXPECTED_ERROR, true) == Kind::Generic, "generic");
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED, false) == Kind::Offline, "offline overrides DNS");
    Require(!ErrorPages::Handles(COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED), "cancelled navigation keeps no page");
    Require(!ErrorPages::Handles(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID), "certificate errors are the HTTPS logic's");
    Require(!ErrorPages::Handles(COREWEBVIEW2_WEB_ERROR_STATUS_VALID_AUTHENTICATION_CREDENTIALS_REQUIRED), "auth challenge is not an error page");
    Require(ErrorPages::Handles(COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT), "timeout handled");
    Require(std::wstring(ErrorPages::Code(COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT)) == L"ERR_CONNECTION_REFUSED", "code");

    // Host shown as in Paper.
    Require(ErrorPages::HostOf(L"https://www.lemonde.fr/planete/") == L"lemonde.fr", "www stripped");
    Require(ErrorPages::HostOf(L"http://localhost:3000/app") == L"localhost:3000", "port kept");
    Require(ErrorPages::HostOf(L"https://user:secret@exemple.test/x") == L"exemple.test", "credentials never shown");

    // Copy per variant (Paper C3/C4).
    const std::wstring token = L"{6B0E7A2C-0D3E-4E86-9C55-0A1B2C3D4E5F}";
    auto offline = ErrorPages::Html(Kind::Offline, L"https://www.lemonde.fr/planete/", token, false);
    Require(Has(offline, L"Vous êtes hors ligne") && Has(offline, L"<b>lemonde.fr</b>") && Has(offline, L"En attente du réseau…") && Has(offline, L"Paramètres réseau"), "offline copy");
    Require(Has(offline, L"addEventListener(\"online\""), "offline page posts retry when the network returns");
    Require(Has(offline, L"autofocus"), "primary action focused");
    auto restored = ErrorPages::Html(Kind::Offline, L"https://lemonde.fr", token, true, ErrorPages::Extras{L"", L"", L"", L"", 0, true});
    Require(Has(restored, L"Connexion rétablie") && Has(restored, L"Rechargement…") && Has(restored, L"aria-disabled=\"true\""), "reprise state");
    auto dns = ErrorPages::Html(Kind::NameNotResolved, L"https://lemond.fr/", token, false);
    Require(Has(dns, L"Ce site est introuvable") && Has(dns, L"Rechercher « lemond.fr »") && Has(dns, L"data-action=\"search\"") && !Has(dns, L"Nouvel essai"), "DNS: search, never auto retry");
    ErrorPages::Extras ex; ex.code = L"ERR_CONNECTION_REFUSED · 127.0.0.1:3000"; ex.detail = L"Aucun proxy · tentative 2 · 14:32";
    auto refused = ErrorPages::Html(Kind::Refused, L"http://localhost:3000", token, false, ex);
    Require(Has(refused, L"Le site a refusé la connexion") && Has(refused, L"aria-expanded=\"false\"") && Has(refused, L"ERR_CONNECTION_REFUSED · 127.0.0.1:3000") && !Has(refused, L"Nouvel essai"), "refused: details disclosure, no auto retry");
    for (int attempt = 0; attempt < 4; ++attempt) {
        ErrorPages::Extras e; e.attempt = attempt;
        auto page = ErrorPages::Html(Kind::TimedOut, L"https://impots.gouv.fr", token, false, e);
        const int delay = ErrorPages::AutoRetryDelay(Kind::TimedOut, attempt);
        Require(delay == (attempt == 0 ? 8 : attempt == 1 ? 16 : attempt == 2 ? 32 : 0), "8, 16, 32 s then stop");
        Require(Has(page, L"Nouvel essai dans " + std::to_wstring(delay) + L" s") == (delay != 0), "countdown shown only while retries remain");
    }
    for (Kind k : {Kind::Reset, Kind::Unreachable, Kind::Generic}) { auto p = ErrorPages::Html(k, L"https://exemple.test", token, false); Require(Has(p, L"<h1>") && Has(p, L"Réessayer"), "derived variants render"); }
    // One action row for every variant: the other action left, « Réessayer » right (DOM = Tab order),
    // never wrapped (a long DNS label ellipsises); only a viewport under 360 px stacks them.
    for (Kind k : {Kind::Offline, Kind::NameNotResolved, Kind::Refused, Kind::TimedOut, Kind::Reset, Kind::Unreachable, Kind::Generic}) {
        for (bool retrying : {false, true}) {
            auto p = ErrorPages::Html(k, L"https://un-nom-de-domaine-vraiment-tres-long-pour-tester.exemple.test/", token, retrying);
            const auto row = p.find(L"<div class=\"actions\">"), first = p.find(L"<button", row), second = p.find(L"<button", first + 1), end = p.find(L"</div>", row);
            Require(row != std::wstring::npos && second < end, "actions: both buttons in one row");
            Require(p.find(L"data-action=\"retry\"", second) < p.find(L">", second), "actions: Réessayer is the right-hand button");
        }
    }
    {
        auto p = ErrorPages::Html(Kind::Reset, L"https://exemple.test", token, false);
        Require(Has(p, L".actions{display:flex;flex-direction:row;flex-wrap:nowrap") && Has(p, L"white-space:nowrap") && Has(p, L"text-overflow:ellipsis") && Has(p, L"button[data-action=retry]{flex-shrink:0}") && Has(p, L"@media (max-width:359px){.actions{flex-direction:column"), "actions: one row, no wrap, ellipsis, narrow fallback only");
    }

    // Injection: the URL is text, never markup or script; the token cannot break the script.
    const std::wstring evil = L"https://x.test/\"><script>alert(1)</script><img src=x onerror=alert(2)>";
    auto escaped = ErrorPages::Html(Kind::Generic, evil, token, false);
    Require(!Has(escaped, L"<script>alert") && !Has(escaped, L"<img") && Count(escaped, L"<script>") == 1, "URL HTML-escaped");
    auto host = ErrorPages::Html(Kind::NameNotResolved, L"<b onmouseover=alert(1)>", token, false);
    Require(!Has(host, L"<b onmouseover") && Has(host, L"&lt;b onmouseover"), "raw host escaped (title and button)");
    auto weird = ErrorPages::Html(Kind::Generic, L"https://x.test", L"\"</script><script>alert(1)//", false);
    Require(Count(weird, L"</script>") == 1 && !Has(weird, L"<script>alert"), "token escaped inside the script");
    Require(Has(offline, L"Content-Security-Policy") && Has(offline, L"default-src 'none'"), "strict CSP");
    for (auto* page : {&offline, &dns, &refused, &escaped}) Require(!Has(*page, L"drop-shadow") && !Has(*page, L"text-shadow") && Count(*page, L"box-shadow") == Count(*page, L"box-shadow:inset"), "no decorative shadow (inset focus ring only)");
    ErrorPages::Extras fonts; fonts.fontRegular = L"data:font/ttf;base64,AAAA"; fonts.fontMedium = L"data:font/ttf;base64,BBBB";
    Require(Has(ErrorPages::Html(Kind::Offline, L"https://a.test", token, false, fonts), L"src:url(data:font/ttf;base64,BBBB)"), "fonts as data: URIs");

    // Message bridge.
    Require(ErrorPages::ParseMessage(Msg(token, L"retry"), token) == Action::Retry, "retry accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"search"), token) == Action::Search, "search accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"network"), token) == Action::NetworkSettings, "network accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"autoRetry"), token) == Action::AutoRetry, "autoRetry accepted");
    Require(ErrorPages::ParseMessage(L"\"{\\\"token\\\":\\\"" + token + L"\\\",\\\"action\\\":\\\"back\\\"}\"", token) == Action::Back, "JSON form of the posted string accepted");
    Require(ErrorPages::ParseMessage(L" { \"action\" : \"retry\" , \"token\" : \"" + token + L"\" } ", token) == Action::Retry, "whitespace and key order");
    Require(!ErrorPages::ParseMessage(Msg(L"{00000000-0000-0000-0000-000000000000}", L"retry"), token), "wrong token");
    Require(!ErrorPages::ParseMessage(L"{\"action\":\"retry\"}", token), "missing token");
    Require(!ErrorPages::ParseMessage(Msg(L"", L"retry"), L""), "empty token never matches");
    Require(!ErrorPages::ParseMessage(Msg(token, L"navigate"), token), "unknown action");
    Require(!ErrorPages::ParseMessage(Msg(token, L"Retry"), token), "case-sensitive allowlist");
    Require(!ErrorPages::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":\"retry\",\"url\":\"https://evil.test\"}", token), "extra member rejected");
    Require(!ErrorPages::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":\"retry\",\"action\":\"back\"}", token), "duplicate member rejected");
    Require(!ErrorPages::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":{\"x\":1}}", token), "nested value rejected");
    Require(!ErrorPages::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":\"retry\"", token), "unterminated object");
    Require(!ErrorPages::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":\"retry\"} x", token), "trailing garbage");
    Require(!ErrorPages::ParseMessage(token + L":retry", token), "legacy token:action form rejected");
    Require(!ErrorPages::ParseMessage(L"{\"token\":\"" + token + L"\",\"action\":\"" + std::wstring(600, L'a') + L"\"}", token), "length bounded");
    Require(!ErrorPages::ParseMessage(L"", token) && !ErrorPages::ParseMessage(L"null", token) && !ErrorPages::ParseMessage(L"[]", token), "non-objects rejected");
    std::printf("PASS error pages: %d checks (classification, copy, escaping, token, allowlist)\n", checks);
    return 0;
}
