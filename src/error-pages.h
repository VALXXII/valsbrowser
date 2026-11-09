#pragma once
// C3/C4 · Pages d'erreur réseau (Paper « Browser Design », p-D-0: contexte C3 E72-0 / bloc EAZ-0;
// variantes C4 ECL-0: DNS ECY-0, refus EGJ-0, délai EGW-0, reprise EH9-0; notes EOM-0).
// Self-contained HTML documents that main shows with NavigateToString, like the HTTPS
// interstitial: a per-page random token, fonts as data: URIs, a strict CSP, no network.
// Buttons post a string (JSON) through chrome.webview.postMessage; ParseMessage() accepts it
// only with the right token and an allow-listed action. Every page-provided or URL-derived text
// is HTML-escaped (the URL cannot inject markup or script).
//
// Paper values (get_jsx): centred 420 DIP block, padding-inline 16; title 20/28 SemiBold
// #FFFFFFE6 balanced; text 14/20 #FFFFFF99 (host medium #FFFFFFE6), 8 above / 24 below;
// buttons h 32 radius 8 padding 14 gap 8, 14 medium: primary #FFFFFFE6 on #171717, secondary
// #FFFFFF14 (hover #333333); details panel #292929 radius 6 padding 8/12 gap 4 (14 medium
// #FFFFFFB3 + 14 #FFFFFF80); status row 24 above, dot 6 radius 3 (waiting #FFFFFF66 / back
// online accent #3E67FF), 14/20 #FFFFFF80 (#FFFFFFB3 when restored); auto-retry « Nouvel essai
// dans 8 s » 14/20 tabular #FFFFFF80 + 120x2 track #FFFFFF14 / fill #FFFFFF80; « Rechargement… »
// primary at 60 %. Behaviour (EOP-0, EOV-0): offline reloads once when the network returns; time-
// out retries automatically at 8, 16 and 32 s then stops; DNS and refused never auto-retry;
// focus starts on the primary action, Entrée triggers it, F5 / Ctrl+R = Réessayer.
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
enum class Kind { Offline, NameNotResolved, Refused, TimedOut, Reset, Unreachable, Generic };
enum class Action { Retry, Back, Details, Search, NetworkSettings, AutoRetry };
// Optional richer content. Fonts are data: URIs (main's DataUri of assets\fonts\InterDisplay-*.ttf).
struct Extras {
    std::wstring fontRegular, fontMedium;
    std::wstring code, detail;  // « Détails techniques »: « ERR_CONNECTION_REFUSED · 127.0.0.1:3000 », « Aucun proxy · tentative 2 · 14:32 »
    int attempt = 0;            // automatic retries already made (time-out: 8, 16, 32 s, then none)
    bool restored = false;      // offline page: the network came back, reloading
    bool reducedMotion = false;
};
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
inline Kind Classify(int status, bool online) {
    if (!online || status == COREWEBVIEW2_WEB_ERROR_STATUS_DISCONNECTED) return Kind::Offline;
    switch (status) {
    case COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED: return Kind::NameNotResolved;
    case COREWEBVIEW2_WEB_ERROR_STATUS_CANNOT_CONNECT: return Kind::Refused;
    case COREWEBVIEW2_WEB_ERROR_STATUS_TIMEOUT: return Kind::TimedOut;
    case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_RESET: case COREWEBVIEW2_WEB_ERROR_STATUS_CONNECTION_ABORTED: return Kind::Reset;
    case COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE: return Kind::Unreachable;
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
struct Copy { std::wstring title, before, after, primary, secondary; Action primaryAction = Action::Retry, secondaryAction = Action::Details; };
inline Copy CopyFor(Kind kind, const std::wstring& host) {
    switch (kind) {
    case Kind::Offline: return {L"Vous êtes hors ligne", L"Impossible d'ouvrir ", L" : cet ordinateur n'est connecté à aucun réseau. Vérifiez le Wi-Fi ou le câble, la page se rechargera d'elle-même dès le retour de la connexion.", L"Réessayer", L"Paramètres réseau", Action::Retry, Action::NetworkSettings};
    case Kind::NameNotResolved: return {L"Ce site est introuvable", L"Aucun site ne répond à l'adresse ", L". Vérifiez l'orthographe, ou lancez une recherche avec ces mots.", L"Rechercher « " + host + L" »", L"Réessayer", Action::Search, Action::Retry};
    case Kind::Refused: return {L"Le site a refusé la connexion", L"", L" ne répond pas. Le serveur est peut-être arrêté, ou un pare-feu bloque l'accès. Réessayez dans un instant.", L"Réessayer", L"Détails techniques"};
    case Kind::TimedOut: return {L"Le site met trop de temps à répondre", L"", L" est peut-être surchargé. Nous réessayons pour vous, ou vous pouvez relancer tout de suite.", L"Réessayer maintenant", L"Détails techniques"};
    case Kind::Reset: return {L"La connexion a été interrompue", L"La connexion à ", L" s'est coupée en cours de route. C'est souvent passager : réessayez.", L"Réessayer", L"Détails techniques"};
    case Kind::Unreachable: return {L"Le site est injoignable", L"Impossible de joindre ", L" : le serveur ne répond pas, ou le réseau en bloque l'accès.", L"Réessayer", L"Détails techniques"};
    default: return {L"Cette page ne s'est pas chargée", L"Une erreur a empêché l'ouverture de ", L". Réessayez dans un instant.", L"Réessayer", L"Détails techniques"};
    }
}
// Button order of the action row: « Réessayer » (whichever button carries it) comes last, on the right.
inline bool RetryLast(const Copy& c) { return c.primaryAction == Action::Retry; }
inline const wchar_t* ActionName(Action a) {
    switch (a) { case Action::Retry: return L"retry"; case Action::Back: return L"back"; case Action::Details: return L"details"; case Action::Search: return L"search"; case Action::NetworkSettings: return L"network"; case Action::AutoRetry: return L"autoRetry"; }
    return L"";
}
inline std::wstring Html(Kind kind, const std::wstring& url, const std::wstring& token, bool retrying, const Extras& extras = {}) {
    const std::wstring host = HostOf(url); const Copy c = CopyFor(kind, host);
    const bool offline = kind == Kind::Offline, details = c.secondaryAction == Action::Details;
    const int delay = retrying ? 0 : AutoRetryDelay(kind, extras.attempt);
    std::wstring h = LR"(<!doctype html><html lang="fr"><meta charset="utf-8"><meta name="color-scheme" content="dark">)"
        LR"(<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; font-src data:">)";
    h += L"<title>" + WebText::Html(c.title) + L"</title><style>";
    if (!extras.fontRegular.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + extras.fontRegular + L") format(\"truetype\");font-weight:400;font-display:block}";
    if (!extras.fontMedium.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + extras.fontMedium + L") format(\"truetype\");font-weight:500 600;font-display:block}";
    h += LR"CSS(
html,body{margin:0;height:100%;background:#171717;color:#FFFFFFE6;font-family:"Inter Display",Inter,system-ui,sans-serif;font-feature-settings:"cv01","cv02","cv03","cv04","cv13";font-synthesis:none;-webkit-font-smoothing:antialiased}
body{display:flex;align-items:center;justify-content:center;padding:0 16px;box-sizing:border-box}
main{width:100%;max-width:420px;display:flex;flex-direction:column;align-items:center;text-align:center;overflow-wrap:anywhere}
h1{margin:0;font-size:20px;line-height:28px;font-weight:600;text-wrap:balance}
p{margin:8px 0 24px;font-size:14px;line-height:20px;color:#FFFFFF99;text-wrap:balance}
p b{font-weight:500;color:#FFFFFFE6}
.actions{display:flex;flex-direction:row;flex-wrap:nowrap;justify-content:center;gap:8px;max-width:100%}
button{height:32px;padding:0 14px;border:0;border-radius:8px;font:inherit;font-size:14px;line-height:32px;font-weight:500;cursor:pointer;flex:0 1 auto;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
button[data-action=retry]{flex-shrink:0}
@media (max-width:359px){.actions{flex-direction:column;align-items:stretch;width:100%}}
button:focus-visible{outline:none;box-shadow:inset 0 0 0 1px #FFFFFF80}
.primary{background:#FFFFFFE6;color:#171717}.primary:hover{background:#FFFFFF}.primary:active{background:#FFFFFFCC}
.primary[aria-disabled=true]{opacity:.6;cursor:default}
.secondary{background:#FFFFFF14;color:#FFFFFFE6}.secondary:hover{background:#333333}.secondary:active{background:#3E3E3E}
.details{align-self:stretch;margin-top:16px;padding:8px 12px;border-radius:6px;background:#292929;display:flex;flex-direction:column;gap:4px;font-size:14px;line-height:20px}
.details[hidden]{display:none}.details b{font-weight:500;color:#FFFFFFB3}.details span{color:#FFFFFF80}
.status{display:flex;align-items:center;gap:8px;padding-top:24px;font-size:14px;line-height:20px;color:#FFFFFF80}
.status i{width:6px;height:6px;border-radius:3px;background:#FFFFFF66;flex:none}
.status.back{color:#FFFFFFB3}.status.back i{background:#3E67FF}
.retry{display:flex;flex-direction:column;align-items:center;gap:8px;padding-top:24px;font-size:14px;line-height:20px;color:#FFFFFF80;font-variant-numeric:tabular-nums}
.track{width:120px;height:2px;border-radius:1px;background:#FFFFFF14;overflow:hidden}.fill{height:2px;border-radius:1px;background:#FFFFFF80;width:0}
)CSS";
    h += extras.reducedMotion ? L"" : L"main{animation:in 160ms ease-out}@keyframes in{from{opacity:0;transform:translateY(8px)}}@media (prefers-reduced-motion:reduce){main{animation:none}}";
    h += L"</style><main role=\"main\"><h1>" + WebText::Html(c.title) + L"</h1><p>" + WebText::Html(c.before) + L"<b>" + WebText::Html(host) + L"</b>" + WebText::Html(c.after) + L"</p>";
    const std::wstring primaryLabel = retrying ? std::wstring(L"Rechargement…") : c.primary;
    // One row for every variant (user request 2026-11-09): the other action on the left,
    // « Réessayer » on the right, in DOM order so Tab follows what is seen. Labels never wrap
    // (a long « Rechercher « … » » ellipsises, « Réessayer » never shrinks); under 360 px (split
    // view halves) they stack, full width, same order.
    const std::wstring primaryButton = L"<button id=\"primary\" class=\"primary\" data-action=\"" + std::wstring(ActionName(c.primaryAction)) + L"\"" + (retrying ? L" aria-disabled=\"true\"" : L" autofocus") + L" title=\"" + WebText::Html(primaryLabel) + L"\">" + WebText::Html(primaryLabel) + L"</button>";
    const std::wstring secondaryButton = L"<button id=\"secondary\" class=\"secondary\" data-action=\"" + std::wstring(ActionName(c.secondaryAction)) + L"\"" + (details ? L" aria-expanded=\"false\" aria-controls=\"details\"" : L"") + L">" + WebText::Html(c.secondary) + L"</button>";
    h += L"<div class=\"actions\">" + (RetryLast(c) ? secondaryButton + primaryButton : primaryButton + secondaryButton) + L"</div>";
    if (details) {
        const std::wstring code = extras.code.empty() ? std::wstring(L"Code inconnu") : extras.code;
        h += L"<div id=\"details\" class=\"details\" hidden><b>" + WebText::Html(code) + L"</b>" + (extras.detail.empty() ? L"" : L"<span>" + WebText::Html(extras.detail) + L"</span>") + L"</div>";
    }
    if (offline) h += std::wstring(L"<div class=\"status") + (extras.restored ? L" back" : L"") + L"\" role=\"status\" aria-live=\"polite\"><i></i><span>" + (extras.restored ? L"Connexion rétablie" : L"En attente du réseau…") + L"</span></div>";
    if (delay) h += L"<div class=\"retry\" role=\"status\" aria-live=\"polite\"><span id=\"count\">Nouvel essai dans " + std::to_wstring(delay) + L" s</span><div class=\"track\" aria-hidden=\"true\"><div class=\"fill\" id=\"fill\"></div></div></div>";
    h += L"</main><script>\"use strict\";const T=\"" + WebText::Js(token) + L"\";let busy=" + (retrying ? L"true" : L"false") + L";";
    h += LR"JS(const send=a=>{try{window.chrome.webview.postMessage(JSON.stringify({token:T,action:a}))}catch(e){}};
const P=document.getElementById("primary"),S=document.getElementById("secondary"),D=document.getElementById("details");
const reload=a=>{if(busy)return;busy=true;if(P.dataset.action==="retry"){P.textContent="Rechargement…";P.setAttribute("aria-disabled","true")}send(a)};
P.addEventListener("click",()=>{if(P.dataset.action==="retry")reload("retry");else send(P.dataset.action)});
S.addEventListener("click",()=>{const a=S.dataset.action;if(a==="details"&&D){const open=D.hidden;D.hidden=!open;S.setAttribute("aria-expanded",String(open));send("details")}else if(a==="retry")reload("retry");else send(a)});
addEventListener("keydown",e=>{if(e.key==="F5"||((e.ctrlKey||e.metaKey)&&(e.key==="r"||e.key==="R"))){e.preventDefault();reload("retry")}});
)JS";
    if (offline) h += L"addEventListener(\"online\",()=>reload(\"retry\"));";
    if (delay) {
        h += L"{const total=" + std::to_wstring(delay) + L";let left=total;const C=document.getElementById(\"count\"),F=document.getElementById(\"fill\");";
        h += LR"JS(const tick=()=>{F.style.width=((total-left)/total*120)+"px";C.textContent="Nouvel essai dans "+left+" s";if(left<=0){if(!busy){busy=true;send("autoRetry")}return}left--;setTimeout(tick,1000)};tick()})JS";
    }
    h += L"</script></html>";
    return h;
}
// The page's message: the string given to postMessage (TryGetWebMessageAsString) or its JSON
// form. Accepted only as {"token":"<this page's token>","action":"<allow-listed>"}.
inline std::optional<Action> ParseMessage(const std::wstring& message, const std::wstring& token) {
    auto m = FlatJson::Parse(message, 512, 2); if (!m || !FlatJson::OnlyKeys(*m, {L"token", L"action"}, {L"token", L"action"})) return std::nullopt;
    if (!FlatJson::SameToken((*m)[L"token"], token)) return std::nullopt;
    const auto& a = (*m)[L"action"];
    for (Action candidate : {Action::Retry, Action::Back, Action::Details, Action::Search, Action::NetworkSettings, Action::AutoRetry}) if (a == ActionName(candidate)) return candidate;
    return std::nullopt;
}
} // namespace ErrorPages
} // namespace NativeChrome
