#pragma once
#include "error-pages.h"
// Onglet planté (#121): our page in place of the engine's « Cette page a un problème ». Paper « Browser Design »,
// page « #121 — Fenêtres du moteur à notre design » (p-V-0), board « Proposition — Onglet planté · A » (1LK4-0),
// block 1LK7-0. Same document model as the network error pages (src/error-pages.h, whose FlatJson / WebText /
// HostOf it reuses): shown with NavigateToString, a per-page random token, fonts as data: URIs, a strict CSP, no
// network. The buttons post {"token","action"} through chrome.webview.postMessage; ParseMessage() accepts only this
// page's token and an allow-listed action. The host name is HTML-escaped (a URL cannot inject markup or script).
//
// Paper values (get_jsx / get_computed_styles): page #171717 (--color-browser-canvas); the 420 DIP block centred,
// padding-inline 16, column, centred, gap 16 between icon / title+text / buttons; title+text column gap 8.
//   icon   48 DIP, Jiggicons « warning », glass, danger tint (body gradient #FF7B6E 85 % -> #E0483A 75 %, rim white
//          -> transparent at 60 %, mark #FFFFFF), exported verbatim below (gradient ids renamed);
//   title  20/28 SemiBold 600, #FFFFFFE6, centred, balanced;
//   text   14/20 Regular, #FFFFFF99, balanced; the host Medium 500 #FFFFFFE6;
//   buttons row gap 8, centred; each h 32, padding-inline 14, radius 8, 14/32 Medium:
//          « Recharger » #FFFFFFE6 on #171717 (focus par défaut), « Fermer l'onglet » #FF7B6E on #FF7B6E at 10 %
//          (owner's retouch). Font features cv01 cv02 cv03 cv04 cv13 throughout.
//   tab    the board's tab keeps the site's name (« lemonde.fr », 1LM8-0): the document title is the host.
//   states (Paper « Onglet planté + partage d'écran · états » 1O26-0): « Recharger » hover #FFFFFF, pressed #FFFFFFBF;
//          « Fermer l'onglet » #FF7B6E on 10 % / hover 16 % / pressed 22 %, its text unchanged. Keyboard focus: the
//          dialogs' A1 rule, a 1 px inset ring #FFFFFF80, and no press scale (owner decision 2026-11-24: the board's
//          outside ring and 0.98 press are not used). Entrance: the error pages' 160 ms (not drawn).
#include <optional>
#include <vector>
#include <string>

namespace NativeChrome {
namespace CrashPage {
// ErrorPageState::kind of the host while this page is on screen (ErrorPages::Kind values stay below it).
inline constexpr int PageKind = 100;
enum class Action { Reload, CloseTab };
struct Extras {
    // data: URIs of assets\fonts\InterDisplay-*.woff2 (Inter 4.1, extras/woff-hinted: the same release as the .ttf).
    // WOFF2 because NavigateToString refuses a page carrying the three TTF weights (E_INVALIDARG at 1.66 M characters,
    // tests/crash-screenshare/engine_probe.py); the three WOFF2 weights take about 0.57 M.
    std::wstring fontRegular, fontMedium, fontSemiBold;
    bool reducedMotion = false;
};
inline const wchar_t* ActionName(Action a) { return a == Action::Reload ? L"reload" : L"close"; }

// Paper 1LX8-0 (« Icône — warning (Jiggicons, verre, danger) »), exported with get_jsx. A paid Jiggicons icon: one line, as
// declared in tools/public-mirror/paid-icons (the public copy gets a free substitute in its place).
inline constexpr const wchar_t* Icon = LR"SVG(<svg width="48" height="48" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" aria-hidden="true"><defs><linearGradient id="g0" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#FF7B6E" stop-opacity="0.85"/><stop offset="1" stop-color="#E0483A" stop-opacity="0.75"/></linearGradient></defs><g transform="scale(0.09375)"><path d="M240.26,186.1,152.81,34.23h0a28.74,28.74,0,0,0-49.62,0L15.74,186.1a27.45,27.45,0,0,0,0,27.71A28.31,28.31,0,0,0,40.55,228h174.9a28.31,28.31,0,0,0,24.79-14.19A27.45,27.45,0,0,0,240.26,186.1Zm-20.8,15.7a4.46,4.46,0,0,1-4,2.2H40.55a4.46,4.46,0,0,1-4-2.2,3.56,3.56,0,0,1,0-3.73L124,46.2a4.77,4.77,0,0,1,8,0l87.44,151.87A3.56,3.56,0,0,1,219.46,201.8ZM116,136V104a12,12,0,0,1,24,0v32a12,12,0,0,1-24,0Zm28,40a16,16,0,1,1-16-16A16,16,0,0,1,144,176Z" fill="url(#g0)"/></g></svg>)SVG";

// The whole document. `url`: the address that crashed (its host is named in the text).
inline std::wstring Html(const std::wstring& url, const std::wstring& token, const Extras& x = {}) {
    const std::wstring host = ErrorPages::HostOf(url);
    std::wstring h = std::wstring(L"<!doctype html><html lang=\"") + (I18n::Current() == I18n::Locale::En ? L"en" : L"fr") + LR"("><meta charset="utf-8"><meta name="color-scheme" content="dark">)"
        LR"(<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; font-src data:">)";
    h += L"<title>" + WebText::Html(host) + L"</title><style>";
    if (!x.fontRegular.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + x.fontRegular + L") format(\"woff2\");font-weight:400;font-display:block}";
    if (!x.fontMedium.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + x.fontMedium + L") format(\"woff2\");font-weight:500;font-display:block}";
    if (!x.fontSemiBold.empty()) h += L"@font-face{font-family:\"Inter Display\";src:url(" + x.fontSemiBold + L") format(\"woff2\");font-weight:600;font-display:block}";
    h += LR"CSS(
html,body{margin:0;height:100%;background:#171717;color:#FFFFFFE6;font-family:"Inter Display",Inter,system-ui,sans-serif;font-feature-settings:"cv01","cv02","cv03","cv04","cv13";font-synthesis:none;-webkit-font-smoothing:antialiased}
body{display:flex;align-items:center;justify-content:center;padding:0 16px;box-sizing:border-box}
main{width:100%;max-width:420px;display:flex;flex-direction:column;align-items:center;gap:16px;text-align:center;overflow-wrap:anywhere}
.icon{display:flex;justify-content:center}.icon svg{display:block;flex:none}
.text{display:flex;flex-direction:column;align-items:center;gap:8px}
h1{margin:0;font-size:20px;line-height:28px;font-weight:600;color:#FFFFFFE6;text-wrap:balance}
p{margin:0;font-size:14px;line-height:20px;font-weight:400;color:#FFFFFF99;text-wrap:balance}
p b{font-weight:500;color:#FFFFFFE6}
.actions{display:flex;flex-direction:row;flex-wrap:nowrap;justify-content:center;gap:8px;max-width:100%}
button{height:32px;padding:0 14px;border:0;border-radius:8px;font:inherit;font-size:14px;line-height:32px;font-weight:500;cursor:pointer;white-space:nowrap;flex:none}
button:focus-visible{outline:none;box-shadow:inset 0 0 0 1px #FFFFFF80}
.primary{background:#FFFFFFE6;color:#171717}.primary:hover{background:#FFFFFF}.primary:active{background:#FFFFFFBF}
.danger{background:rgba(255,123,110,.1);color:#FF7B6E}.danger:hover{background:rgba(255,123,110,.16)}.danger:active{background:rgba(255,123,110,.22)}
)CSS";
    h += x.reducedMotion ? L"" : L"main{animation:in 160ms ease-out}@keyframes in{from{opacity:0;transform:translateY(8px)}}@media (prefers-reduced-motion:reduce){main{animation:none}}";
    // The sentence is escaped first, then the escaped host (in bold) goes into it: page text never becomes markup.
    const std::wstring body = I18n::FormatText(WebText::Html(TR(crash_body)).c_str(), {L"<b>" + WebText::Html(host) + L"</b>"});
    h += L"</style><main role=\"main\"><div class=\"icon\">" + std::wstring(Icon) + L"</div><div class=\"text\"><h1>" + WebText::Html(TR(crash_title)) + L"</h1><p>" + body + L"</p></div>";
    h += L"<div class=\"actions\"><button id=\"reload\" class=\"primary\" data-action=\"reload\" autofocus>" + WebText::Html(TR(crash_reload)) + L"</button>"
         L"<button id=\"close\" class=\"danger\" data-action=\"close\">" + WebText::Html(TR(crash_close_tab)) + L"</button></div>";
    h += L"</main><script>\"use strict\";const T=\"" + WebText::Js(token) + L"\";let busy=false;";
    h += LR"JS(const send=a=>{if(busy)return;busy=true;try{window.chrome.webview.postMessage(JSON.stringify({token:T,action:a}))}catch(e){busy=false}};
document.getElementById("reload").addEventListener("click",()=>send("reload"));
document.getElementById("close").addEventListener("click",()=>send("close"));
)JS";
    h += L"</script></html>";
    return h;
}
// The page's message (TryGetWebMessageAsString or its JSON form). Accepted only as
// {"token":"<this page's token>","action":"reload"|"close"}.
inline std::optional<Action> ParseMessage(const std::wstring& message, const std::wstring& token) {
    auto m = FlatJson::Parse(message, 512, 2); if (!m || !FlatJson::OnlyKeys(*m, {L"token", L"action"}, {L"token", L"action"})) return std::nullopt;
    if (!FlatJson::SameToken((*m)[L"token"], token)) return std::nullopt;
    const auto& a = (*m)[L"action"];
    for (Action candidate : {Action::Reload, Action::CloseTab}) if (a == ActionName(candidate)) return candidate;
    return std::nullopt;
}
// DevTools Page.getNavigationHistory's answer -> the id of the entry `back` places before the current one, or -1.
// A string-aware scan (titles and urls may hold any text): currentIndex at depth 1, entries[i].id at depth 3.
inline long long HistoryEntryBefore(const std::wstring& json, int back) {
    long long current = -1; std::vector<long long> ids; int depth = 0, entriesDepth = -1; std::wstring key; bool keyPending = false;
    for (size_t i = 0; i < json.size(); ++i) {
        const wchar_t c = json[i];
        if (c == L'"') {
            std::wstring s; ++i;
            for (; i < json.size() && json[i] != L'"'; ++i) { if (json[i] == L'\\') { ++i; if (i < json.size()) s += json[i]; } else s += json[i]; }
            size_t j = i + 1; while (j < json.size() && (json[j] == L' ' || json[j] == L'\t' || json[j] == L'\r' || json[j] == L'\n')) ++j;
            if (j < json.size() && json[j] == L':') { key = s; keyPending = true; i = j; } else keyPending = false;
            continue;
        }
        if (c == L'{' || c == L'[') { ++depth; if (c == L'[' && depth == 2 && keyPending && key == L"entries") entriesDepth = depth; keyPending = false; continue; }
        if (c == L'}' || c == L']') { if (depth == entriesDepth && c == L']') entriesDepth = -1; --depth; keyPending = false; continue; }
        if (c == L'-' || (c >= L'0' && c <= L'9')) {
            size_t j = i; while (j < json.size() && (json[j] == L'-' || (json[j] >= L'0' && json[j] <= L'9'))) ++j;
            const long long v = _wtoi64(json.substr(i, j - i).c_str()); i = j - 1;
            if (keyPending && depth == 1 && key == L"currentIndex") current = v;
            else if (keyPending && entriesDepth > 0 && depth == entriesDepth + 1 && key == L"id") ids.push_back(v);
            keyPending = false; continue;
        }
        if (c == L',') keyPending = false;
    }
    const long long at = current - back;
    return current >= 0 && current < static_cast<long long>(ids.size()) && at >= 0 ? ids[static_cast<size_t>(at)] : -1;
}
// The host's two decisions, as pure rules (tests/crash_screenshare_test.cpp).
// 1) The tab's renderer exited (ProcessFailed RENDER_PROCESS_EXITED).
enum class OnExit {
    ReloadAfterHang,   // « Recharger » of the frozen-page window ended it on purpose: the page reloads
    ReshowPage,        // the renderer of OUR page died (memory, killed again): our page again, never the site behind it
    ReloadDocument,    // the native home or the debug sandbox: its own document back
    ShowPage           // a site's renderer died: our page replaces the engine's
};
inline OnExit ExitResponse(bool hangReloading, bool pageOn, bool homeOrInternal) {
    if (hangReloading) return OnExit::ReloadAfterHang;
    if (pageOn) return OnExit::ReshowPage;
    if (homeOrInternal) return OnExit::ReloadDocument;
    return OnExit::ShowPage;
}
// 2) A navigation starts while our page is on screen. `pending`: our own document is (re)loading (shown, shown again
// after its renderer died, or brought back by history): let it through. Otherwise a reload of our page goes back to
// the crashed entry, a history step goes to the entry before it (unless it is the one we started).
enum class OnNavigation { Allow, ToCrashedEntry, ToEntryBefore };
inline OnNavigation NavigationWhileShown(bool pending, bool blank, int navigationKind, bool recharging, bool skipping, bool ownPage) {
    if (pending) return OnNavigation::Allow;
    if (navigationKind == COREWEBVIEW2_NAVIGATION_KIND_RELOAD && blank) return OnNavigation::ToCrashedEntry;
    if (navigationKind == COREWEBVIEW2_NAVIGATION_KIND_BACK_OR_FORWARD && !recharging && !skipping && !ownPage) return OnNavigation::ToEntryBefore;
    return OnNavigation::Allow;
}
// « Page qui ne répond plus »: what happens when the wait after « Attendre » (or after a report the window could not
// show: the tab hidden) is over. The engine reports a frozen page ONCE (measured 24/11, engine_probe.py hang-again: no
// second report in 60 s, new input or not), so the window comes back from here. Stop: the tab closed, the page answered
// again, it is being reloaded or crashed, or another document is shown; Later: the tab is not on screen (asked once it
// is); Ask: the window again (unless it is already open or queued).
inline constexpr unsigned HangAskAgainMs = 15000;   // the engine's own delay (Chromium's hung-renderer delay: its first report came 16.6 s after the input)
enum class HangNext { Stop, Later, Ask };
inline HangNext AfterWait(bool closed, bool stillFrozen, bool reloadingOrCrashed, bool sameDocument, bool onScreen, bool alreadyAsking) {
    if (closed || !stillFrozen || reloadingOrCrashed || !sameDocument || alreadyAsking) return HangNext::Stop;
    return onScreen ? HangNext::Ask : HangNext::Later;
}
// Which engine failures get this page: only the tab's own document process dying (RENDER_PROCESS_EXITED: crash,
// out of memory, killed, launch failure...). An unresponsive renderer (RENDER_PROCESS_UNRESPONSIVE) may still come
// back and the board's « s'est arrêté d'un coup » would be false; a frame's process (FRAME_RENDER_PROCESS_EXITED)
// leaves the page itself running; GPU / utility / helper processes are restarted by the engine; the browser
// process exiting ends every tab (handled by main's engine failure path).
inline bool Shows(int processFailedKind) { return processFailedKind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED; }
} // namespace CrashPage
} // namespace NativeChrome
