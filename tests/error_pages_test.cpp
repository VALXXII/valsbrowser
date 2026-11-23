// C3/C4 network error pages: classification, copy, escaping, token-gated message bridge.
// Pure functions: no window, no WebView2. Built and run by test-sandbox.ps1.
#include "../src/error-pages.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace NativeChrome;
using ErrorPages::Action; using ErrorPages::Kind;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static bool Has(const std::wstring& s, const std::wstring& part) { return s.find(part) != std::wstring::npos; }
static size_t Count(const std::wstring& s, const std::wstring& part) { size_t n = 0; for (size_t i = s.find(part); i != std::wstring::npos; i = s.find(part, i + 1)) ++n; return n; }
static std::wstring Msg(const std::wstring& token, const std::wstring& action) { return L"{\"token\":\"" + token + L"\",\"action\":\"" + action + L"\"}"; }
// A bundled font as main's DataUri builds it (base64 data: URI), read from the repository.
static std::wstring FontUri(const char* file) {
    std::ifstream in(std::string("assets/fonts/") + file, std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Require(!bytes.empty(), "bundled font readable (run from the repository root)");
    static const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; std::wstring r = L"data:font/ttf;base64,";
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (static_cast<unsigned char>(bytes[i]) << 16) | (i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) << 8 : 0) | (i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0);
        r += static_cast<wchar_t>(digits[(n >> 18) & 63]); r += static_cast<wchar_t>(digits[(n >> 12) & 63]); r += i + 1 < bytes.size() ? static_cast<wchar_t>(digits[(n >> 6) & 63]) : L'='; r += i + 2 < bytes.size() ? static_cast<wchar_t>(digits[n & 63]) : L'=';
    }
    return r;
}
static size_t Utf8Size(const std::wstring& s) { return static_cast<size_t>(WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr)); }
// The text between the opening tag that contains `marker` and the next '<'.
static std::wstring TextAfter(const std::wstring& s, const std::wstring& marker) { const auto at = s.find(marker); if (at == std::wstring::npos) return L""; const auto open = s.find(L'>', at); const auto close = s.find(L'<', open); return s.substr(open + 1, close - open - 1); }

int main() {
    // Classify: WebView2 status → Paper variant; offline wins over everything.
    Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED, true) == Kind::NameNotResolved, "DNS");
    // A site's own HTTP error page (403 challenge, 404, 429, 503) is kept; transport failures are not.
    for (int code : {400, 403, 404, 429, 500, 503, 599}) Require(ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, code), "HTTP 4xx/5xx with UNKNOWN keeps the server's page");
    Require(!ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, 0), "UNKNOWN without any response (refused, cut before headers) stays an error page");
    Require(!ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, 200), "a cut 200 response stays an error page");
    Require(!ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, 302) && !ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, 600), "only 400-599");
    Require(!ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID, 403) && !ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET, 503) && !ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT, 404), "certificate / reset / timeout are never hidden by a status code");
    Require(!ErrorPages::KeepsServerPage(true, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, 404), "a successful navigation is not this case");
    Require(!ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN, 399) && !ErrorPages::KeepsServerPage(false, COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED, 404), "399 and a cancelled navigation are not kept");
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
    ErrorPages::Extras back; back.restored = true;
    auto restored = ErrorPages::Html(Kind::Offline, L"https://lemonde.fr", token, true, back);
    Require(Has(restored, L"Connexion rétablie") && Has(restored, L"Rechargement…") && Has(restored, L"aria-disabled=\"true\""), "reprise state");
    // #121 « Reprise » (1M20-0): wifi-checkmark in the accent, the shorter text; the live page turns to it on « online ».
    Require(Has(restored, L"<body class=\"back\">") && Has(restored, L"id=\"wc\"") && Has(restored, L"#7A95FF") && Has(restored, L"#1C2452") && Has(restored, L"Vérifiez le Wi-Fi ou le câble.</p>"), "reprise: accent icon and shorter text shown");
    Require(Has(offline, L"<body><main") && Has(offline, L".back .off,body:not(.back) .on{display:none}") && Has(offline, L"classList.add(\"back\")") && Has(offline, L"const BACK=\"Connexion rétablie\""), "offline: the page turns to the reprise state when the network returns");
    Require(Has(offline, L"id=\"wo\"") && Has(offline, L"#4A1E1A") && Has(offline, L"<div class=\"icon off\">"), "offline: wifi-off glass icon, dark glass part");
    auto dns = ErrorPages::Html(Kind::NameNotResolved, L"https://lemond.fr/", token, false);
    Require(Has(dns, L"Ce site est introuvable") && Has(dns, L"Rechercher « lemond.fr »") && Has(dns, L"data-action=\"search\"") && !Has(dns, L"Nouvel essai"), "DNS: search, never auto retry");
    ErrorPages::Extras ex; ex.code = L"ERR_CONNECTION_REFUSED"; ex.address = ErrorPages::AddressOf(L"http://localhost:3000"); ex.tries = ErrorPages::TriesText(2, L"14:32");
    auto refused = ErrorPages::Html(Kind::Refused, L"http://localhost:3000", token, false, ex);
    Require(Has(refused, L"Le site a refusé la connexion") && Has(refused, L"aria-expanded=\"false\"") && Has(refused, L"ERR_CONNECTION_REFUSED") && !Has(refused, L"Nouvel essai"), "refused: details disclosure, no auto retry");
    // #121 sheet B on « connexion refusée » (1MDG-0): Code (mono, copy) / Adresse / Essai, hidden until « Détails techniques ».
    Require(Has(refused, L"<div id=\"details\" class=\"sheet\" hidden>") && TextAfter(refused, L"class=\"v code\"") == L"ERR_CONNECTION_REFUSED" && Has(refused, L"data-action=\"copy\" aria-label=\"Copier le code\""), "refused: sheet with the code and its copy button");
    Require(Has(refused, L">Adresse</span><span class=\"v\">localhost:3000</span>") && Has(refused, L">Essai</span><span class=\"v\">2e tentative · 14:32</span>"), "refused: Adresse and Essai rows");
    Require(Has(refused, L".sheet{width:340px;max-width:100%;box-sizing:border-box;border:1px solid #FFFFFF14;border-radius:8px") && Has(refused, L".row{display:flex;align-items:center;gap:12px;padding:12px}") && Has(refused, L".k{flex:none;width:64px;font-size:13px;line-height:18px;color:#FFFFFF66}") && Has(refused, L".v{flex:1 1 0;min-width:0;font-size:13px;line-height:18px;color:#FFFFFFCC}") && Has(refused, L"font-family:\"JetBrains Mono\",system-ui,sans-serif;font-size:12px") && Has(refused, L".sep{display:block;flex:none;height:1px;margin:0 12px;background:#FFFFFF0D}"), "sheet: the board's frame, rows, label and value styles");
    Require(Has(refused, L"id=\"se\"") && !Has(refused, L"background:#292929"), "refused: server-error icon, the old filled panel is gone");
    Require(ErrorPages::AddressOf(L"https://www.lemonde.fr/planete/") == L"www.lemonde.fr:443" && ErrorPages::AddressOf(L"http://user:pw@[::1]:8080/x") == L"[::1]:8080" && ErrorPages::AddressOf(L"http://exemple.test") == L"exemple.test:80" && ErrorPages::AddressOf(L"pas une adresse").empty(), "Adresse: the authority tried, with its port, never credentials");
    Require(ErrorPages::TriesText(1, L"09:05") == L"1re tentative · 09:05" && ErrorPages::TriesText(3, L"") == L"3e tentative", "Essai: attempt and time");
    for (int attempt = 0; attempt < 4; ++attempt) {
        ErrorPages::Extras e; e.attempt = attempt;
        auto page = ErrorPages::Html(Kind::TimedOut, L"https://impots.gouv.fr", token, false, e);
        const int delay = ErrorPages::AutoRetryDelay(Kind::TimedOut, attempt);
        Require(delay == (attempt == 0 ? 8 : attempt == 1 ? 16 : attempt == 2 ? 32 : 0), "8, 16, 32 s then stop");
        Require(Has(page, L"Nouvel essai dans " + std::to_wstring(delay) + L" s") == (delay != 0), "countdown shown only while retries remain");
    }
    for (Kind k : {Kind::Reset, Kind::Unreachable, Kind::Generic}) { auto p = ErrorPages::Html(k, L"https://exemple.test", token, false); Require(Has(p, L"<h1>") && Has(p, L"Réessayer") && !Has(p, L"<div class=\"icon"), "derived variants render (no board icon yet)"); }
    // #121 icons (Jiggicons glass, tinted on the boards): one per variant, 48 DIP.
    Require(Has(dns, L"id=\"gs\"") && Has(ErrorPages::Html(Kind::TimedOut, L"https://a.test", token, false), L"id=\"hg\"") && Has(dns, L"<svg width=\"48\" height=\"48\" viewBox=\"0 0 24 24\" aria-hidden=\"true\""), "icons: globe-search, hourglass, 48 DIP, hidden from screen readers");
    // #121 layout (1LXI-0): 16 between icon / title+text / buttons / sheet, 8 between title and text; no margins left.
    Require(Has(offline, L"main{width:100%;max-width:420px;display:flex;flex-direction:column;align-items:center;gap:16px") && Has(offline, L".text{display:flex;flex-direction:column;align-items:center;gap:8px") && Has(offline, L"p{margin:0;font-size:14px;line-height:20px;color:#FFFFFF99") && Has(offline, L"h1{margin:0;font-size:20px;line-height:28px;font-weight:600"), "layout: 16 / 8 gaps");
    Require(Has(offline, L".status{display:flex;align-items:center;gap:8px;padding-top:24px") && Has(ErrorPages::Html(Kind::TimedOut, L"https://a.test", token, false), L"font-feature-settings:\"tnum\""), "status and countdown as on the boards");
    // One action row for every variant, in the boards' order (2026-11-23): the primary action left, the other right,
    // except « Retour » (certificate) left of « Réessayer »; DOM = Tab order; never wrapped (a long DNS label
    // ellipsises); only a viewport under 360 px stacks them.
    for (Kind k : {Kind::Offline, Kind::NameNotResolved, Kind::Refused, Kind::TimedOut, Kind::Reset, Kind::Unreachable, Kind::Generic, Kind::Certificate}) {
        for (bool retrying : {false, true}) {
            auto p = ErrorPages::Html(k, L"https://un-nom-de-domaine-vraiment-tres-long-pour-tester.exemple.test/", token, retrying);
            const auto row = p.find(L"<div class=\"actions\">"), first = p.find(L"<button", row), second = p.find(L"<button", first + 1), end = p.find(L"</div>", row);
            Require(row != std::wstring::npos && second < end, "actions: both buttons in one row");
            const bool primaryFirst = k != Kind::Certificate;
            Require((p.find(L"class=\"primary\"", first) < p.find(L">", first)) == primaryFirst, "actions: the boards' order (primary left; Retour left of Réessayer)");
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
    Require(Has(offline, L"h1,p,.actions button{font-feature-settings:\"cv01\",\"cv02\",\"cv03\",\"cv04\",\"cv13\"}") && !Has(offline, L"html,body{margin:0;height:100%;background:#171717;color:#FFFFFFE6;font-family:\"Inter Display\",Inter,system-ui,sans-serif;font-feature"), "cv features on title, text and buttons only (the sheet and status have none on the boards)");
    for (auto* page : {&offline, &dns, &refused, &escaped}) Require(!Has(*page, L"drop-shadow") && !Has(*page, L"text-shadow") && Count(*page, L"box-shadow") == Count(*page, L"box-shadow:inset"), "no decorative shadow (inset focus ring only)");
    ErrorPages::Extras fonts; fonts.fontRegular = L"data:font/ttf;base64,AAAA"; fonts.fontMedium = L"data:font/ttf;base64,BBBB";
    Require(Has(ErrorPages::Html(Kind::Offline, L"https://a.test", token, false, fonts), L"src:url(data:font/ttf;base64,BBBB)"), "fonts as data: URIs");
    fonts.fontSemiBold = L"data:font/ttf;base64,CCCC"; fonts.fontMono = L"data:font/ttf;base64,DDDD";
    Require(Has(ErrorPages::Html(Kind::Offline, L"https://a.test", token, false, fonts), L"base64,CCCC) format(\"truetype\");font-weight:600") && Has(ErrorPages::Html(Kind::Offline, L"https://a.test", token, false, fonts), L"base64,BBBB) format(\"truetype\");font-weight:500;"), "titles in Inter Display SemiBold (600), Medium for 500");
    Require(!Has(ErrorPages::Html(Kind::Offline, L"https://a.test", token, false, fonts), L"DDDD") && Has(ErrorPages::Html(Kind::Refused, L"https://a.test", token, false, fonts), L"font-family:\"JetBrains Mono\";src:url(data:font/ttf;base64,DDDD)"), "JetBrains Mono only on a page with a sheet");
    {   // NavigateToString refuses a document over ~1.5 MB (it becomes a base64 data: URL capped at 2 MB; measured by
        // tests/error-pages/run.py: 1 108 832 bytes shown, 1 663 648 refused with E_INVALIDARG). The heaviest pages, with
        // the files the browser embeds (main's PageFonts: whole Regular / Medium, reduced SemiBold / mono), stay under 1.5 MB.
        ErrorPages::Extras real; real.fontRegular = FontUri("InterDisplay-Regular.ttf"); real.fontMedium = FontUri("InterDisplay-Medium.ttf"); real.fontSemiBold = FontUri("InterDisplay-SemiBold-Latin.ttf"); real.fontMono = FontUri("JetBrainsMono-Codes.ttf");
        real.certStatus = COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS; real.issuer = std::wstring(400, L'é'); real.code = L"ERR_CONNECTION_REFUSED"; real.address = std::wstring(300, L'a'); real.tries = ErrorPages::TriesText(12, L"23:59");
        const std::wstring longUrl = L"https://" + std::wstring(300, L'x') + L".exemple.test/" + std::wstring(2000, L'y');
        size_t heaviest = 0;
        for (Kind k : {Kind::Certificate, Kind::Refused, Kind::TimedOut, Kind::Offline, Kind::NameNotResolved}) heaviest = (std::max)(heaviest, Utf8Size(ErrorPages::Html(k, longUrl, token, false, real)));
        heaviest = (std::max)(heaviest, Utf8Size(ErrorPages::InsecureHtml(std::wstring(253, L'x'), token, real)));
        Require(heaviest < 1500000u, "fonts + page under what NavigateToString accepts (1.5 MB kept)");
        std::printf("heaviest error page with the real fonts: %zu bytes (kept under 1500000)\n", heaviest);
    }

    // Message bridge.
    Require(ErrorPages::ParseMessage(Msg(token, L"retry"), token) == Action::Retry, "retry accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"search"), token) == Action::Search, "search accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"network"), token) == Action::NetworkSettings, "network accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"autoRetry"), token) == Action::AutoRetry, "autoRetry accepted");
    Require(ErrorPages::ParseMessage(Msg(token, L"copy"), token) == Action::Copy, "copy accepted (the host copies the code it shows)");
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
    {   // TLS refused by the engine (CANCEL, never bypassed): our page, real cause, no « continue ».
        Require(ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID, true) == Kind::Certificate && ErrorPages::Classify(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED, true) == Kind::Certificate, "certificate statuses classify as Certificate");
        ErrorPages::Extras x; x.certStatus = COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED; x.issuer = L"<script>alert(1)</script> CA";
        const std::wstring h = ErrorPages::Html(Kind::Certificate, L"https://exemple.test:8443/compte", L"tok", false, x);
        Require(h.find(L"La connexion sécurisée n") != std::wstring::npos && h.find(L"pas pu être établie") != std::wstring::npos && h.find(L"a expiré") != std::wstring::npos && h.find(L"CERTIFICATE_EXPIRED") != std::wstring::npos && h.find(L"NET::") == std::wstring::npos, "certificate page: title, real cause and code");
        Require(h.find(L"bloquée : ") != std::wstring::npos && h.find(L"bloquée :") == std::wstring::npos, "certificate page: narrow no-break space before the colon (never a lone « : » at a line start)");
        Require(h.find(L"<script>alert(1)") == std::wstring::npos && h.find(L"&lt;script&gt;") != std::wstring::npos, "certificate page: issuer escaped");
        Require(h.find(L"Continuer") == std::wstring::npos && h.find(L"http://") == std::wstring::npos && h.find(L"rien n&#39;a été envoyé") == std::wstring::npos && h.find(L"rien n'a été envoyé") == std::wstring::npos, "certificate page: no continue, no http, no absolute promise");
        Require(h.find(L"data-action=\"back\"") != std::wstring::npos && h.find(L"data-action=\"retry\"") != std::wstring::npos, "certificate page: Retour and a real Réessayer only");
        // #121 sheet B (1MAC-0): no « WebView2 : » prefix, Code (mono, copy) then Émetteur, always shown; shield-error icon.
        Require(!Has(h, L"WebView2") && !Has(h, L"Émis par") && TextAfter(h, L"class=\"v code\"") == L"CERTIFICATE_EXPIRED" && Has(h, L">Émetteur</span><span class=\"v\">&lt;script&gt;alert(1)&lt;/script&gt; CA</span>") && Has(h, L"<div class=\"sheet\">") && Has(h, L"id=\"sh\""), "certificate page: sheet B with code and issuer");
        for (int s : {COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID, COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_EXPIRED, COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED, COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT, COREWEBVIEW2_WEB_ERROR_STATUS_CLIENT_CERTIFICATE_CONTAINS_ERRORS}) {
            ErrorPages::Extras y; y.certStatus = s; const std::wstring code = ErrorPages::CertificateCode(s); const auto page = ErrorPages::Html(Kind::Certificate, L"https://intranet.example.com/", L"tok", false, y);
            Require(code.rfind(L"WebView2", 0) != 0 && Has(page, L"class=\"sheet" + std::wstring(code.size() > 29 ? L" wide" : L"") + L"\">"), "certificate sheet: 340 wide, 420 when the code does not fit (as the boards)");
            Require(!Has(page, L">Émetteur<"), "certificate sheet: no Émetteur row without an issuer (board: émetteur non disponible)");
        }
        Require(std::wstring(ErrorPages::CertificateCode(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT)).size() > ErrorPages::SheetCodeFit && std::wstring(ErrorPages::CertificateCode(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_REVOKED)).size() <= ErrorPages::SheetCodeFit, "sheet width rule matches the boards (420 for the long codes only)");
        Require(ErrorPages::CertificateMatches(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID, L"HTTPS://Exemple.test/a#x", L"https://exemple.test/a"), "certificate: the refused request is the navigation's URL");
        Require(!ErrorPages::CertificateMatches(COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID, L"https://cdn.exemple.test/lib.js", L"https://exemple.test/a"), "certificate: a sub-resource's refusal never replaces the page");
        Require(!ErrorPages::CertificateMatches(-1, L"https://exemple.test/a", L"https://exemple.test/a"), "certificate: nothing recorded, no page");
    }
    {   // « Ce site n'est pas sécurisé » (1LZS-0): same shell, lock-open icon, Revenir (focused, 2 px ring) then Continuer.
        const auto p = ErrorPages::InsecureHtml(L"bibliotheque-municipale.fr", L"{AB}", fonts);
        Require(Has(p, L"<h1>Ce site n&#39;est pas sécurisé</h1>") &&Has(p, L"<b>bibliotheque-municipale.fr</b>") && Has(p, L"id=\"la\"") && Has(p, L"gap:16px") && Has(p, L"gap:8px"), "insecure page: #121 layout and lock-open icon");
        Require(p.find(L"id=\"back\" class=\"primary\" autofocus") < p.find(L"id=\"go\" class=\"secondary\"") && Has(p, L"button:focus-visible{outline:2px solid #FFFFFFE6;outline-offset:2px}"), "insecure page: Revenir first and focused, the board's ring");
        Require(Has(p, L"const t=\"{AB}\"") && Has(p, L"postMessage(t+\":\"+a)") && Has(p, L"Content-Security-Policy"), "insecure page: token:action protocol unchanged, strict CSP");
        const auto evil = ErrorPages::InsecureHtml(L"<img src=x onerror=alert(1)>", L"\"</script><script>alert(2)//", {});
        Require(!Has(evil, L"<img") && Count(evil, L"</script>") == 1 && !Has(evil, L"<script>alert"), "insecure page: host and token escaped");
    }
    std::printf("PASS error pages: %d checks (classification, copy, escaping, token, allowlist)\n", checks);
    return 0;
}
