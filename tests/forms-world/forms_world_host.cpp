// tests/forms-world: the #121 isolated world (Forms::Script, src/form-memory.h) in the REAL engine, with a window that
// is never shown (no ShowWindow, tool window, off every monitor, never activated): nothing appears on screen, the mouse
// and the focus are left alone. Installed exactly like src/forms-integration.h FormsInstall (Page.enable, the binding
// in the world, the new-document script, the binding again on each new document); typing and clicks go through the
// DevTools Input domain (trusted events, like a keyboard and a mouse). Driven by run.py on local pages only.
//
//   forms_world_host.exe --base http://127.0.0.1:<port> --udf <temp profile>
//
// Checks P3 (only real typing opens), P4 (top document only), P5 (hidden field), P6/P7 (keys routed only while the host
// says a list is shown), P8 (a moving field closes), P9 (fill: the same element), P12 (only what was typed is saved),
// P14 (the engine's bubble replaced, the page's own validation kept, page text cut).
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <WebView2.h>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "form-memory.h"

namespace {
template<class T> const wchar_t* GuidText();
#define GUID_TEXT(T,S) template<> const wchar_t* GuidText<T>() { return L##S; }
GUID_TEXT(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,"{4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d}")
GUID_TEXT(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,"{6c4819f3-c9b7-4260-8127-c9f5bde7f68c}")
GUID_TEXT(ICoreWebView2ExecuteScriptCompletedHandler,"{49511172-cc67-4bca-9923-137112f4c4cc}")
GUID_TEXT(ICoreWebView2CallDevToolsProtocolMethodCompletedHandler,"{5c4889f0-5ef6-4c5a-952c-d8f1b92d0574}")
GUID_TEXT(ICoreWebView2DevToolsProtocolEventReceivedEventHandler,"{e2fda4be-5456-406c-a261-3d452138362c}")
template<class I, class... A> class Handler final : public I {
    LONG refs = 1; IID iid{}; std::function<HRESULT(A...)> fn;
public:
    explicit Handler(std::function<HRESULT(A...)> f) : fn(std::move(f)) { CLSIDFromString(GuidText<I>(), &iid); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == iid) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(A... args) override { return fn(args...); }
};
template<class I, class... A, class F> I* Callback(F f) { return new Handler<I, A...>(std::move(f)); }

std::wstring base, udf; ICoreWebView2Controller* controller = nullptr; ICoreWebView2* view = nullptr; bool finished = false;
int checks = 0, failures = 0;
struct Got { Forms::Report r; long long context = 0; };
std::vector<Got> reports;
long long context = 0; int seq = 0; std::wstring lastJs, lastEval; Vault::FillOutcome lastOutcome = Vault::FillOutcome::Failed;

void Say(const std::wstring& s) { std::printf("%s\n", Vault::Utf8(s).c_str()); std::fflush(stdout); }
void Check(bool ok, const char* what) { ++checks; if (!ok) ++failures; std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); std::fflush(stdout); }
void Finish() { if (!finished) { finished = true; PostQuitMessage(0); } }
void Cdp(const wchar_t* method, const std::wstring& params, std::function<void(HRESULT, const std::wstring&)> done = nullptr) {
    auto h = Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler, HRESULT, LPCWSTR>([done](HRESULT hr, LPCWSTR j) -> HRESULT { if (done) done(hr, j ? j : L""); return S_OK; });
    view->CallDevToolsProtocolMethod(method, params.c_str(), h); h->Release();
}
void Js(const std::wstring& expr) { lastJs.clear(); auto h = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR r) -> HRESULT { lastJs = r ? r : L""; return S_OK; }); view->ExecuteScript(expr.c_str(), h); h->Release(); }
void Eval(const std::wstring& expr) { lastEval.clear(); Cdp(L"Runtime.evaluate", Forms::EvaluateParams(context, expr), [](HRESULT hr, const std::wstring& j) { lastEval = SUCCEEDED(hr) ? j : L"error"; }); }
// A vault script in a FRESH isolated world of the top document, exactly like elements-integration.h VaultEvaluateIsolated.
void Fresh(const std::wstring& script) {
    lastOutcome = Vault::FillOutcome::Failed;
    Cdp(L"Page.getFrameTree", L"{}", [script](HRESULT hr, const std::wstring& j) {
        const std::wstring frame = SUCCEEDED(hr) ? Vault::MainFrameId(j) : L""; if (frame.empty()) return;
        Cdp(L"Page.createIsolatedWorld", Vault::IsolatedWorldParams(frame), [script](HRESULT hr2, const std::wstring& j2) {
            const long long ctx = SUCCEEDED(hr2) ? Vault::ContextIdOf(j2) : 0; if (!ctx) return;
            Cdp(L"Runtime.evaluate", Vault::IsolatedEvaluateParams(ctx, script), [](HRESULT hr3, const std::wstring& j3) { lastOutcome = Vault::ParseIsolatedFillResult(hr3, j3.c_str()); }); }); });
}
std::wstring Origin() { const auto a = base.find(L"://"); const auto b = base.find(L'/', a + 3); return b == std::wstring::npos ? base : base.substr(0, b); }
void Mouse(int x, int y) { for (const wchar_t* t : {L"mousePressed", L"mouseReleased"}) Cdp(L"Input.dispatchMouseEvent", std::wstring(L"{\"type\":\"") + t + L"\",\"x\":" + std::to_wstring(x) + L",\"y\":" + std::to_wstring(y) + L",\"button\":\"left\",\"clickCount\":1}"); }
// A click at the centre of #id (its rectangle read in the page, then a trusted mouse press and release there).
void Click(const std::wstring& id, int dy = -1) {
    auto h = Callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>([](HRESULT, LPCWSTR r) -> HRESULT {
        std::wstring s = r ? r : L""; s.erase(std::remove(s.begin(), s.end(), L'"'), s.end()); const auto c = s.find(L',');
        if (c != std::wstring::npos) Mouse(_wtoi(s.substr(0, c).c_str()), _wtoi(s.substr(c + 1).c_str()));
        return S_OK; });
    const std::wstring y = dy < 0 ? L"r.top+r.height/2" : L"r.top+" + std::to_wstring(dy);
    view->ExecuteScript((L"(function(){var e=document.getElementById('" + id + L"');var r=e.getBoundingClientRect();return Math.round(r.left+Math.min(r.width/2,40))+','+Math.round(" + y + L");})()").c_str(), h); h->Release();
}
void Type(const std::wstring& text) { for (wchar_t c : text) { const std::wstring k = Vault::JsonString(std::wstring(1, c)); Cdp(L"Input.dispatchKeyEvent", L"{\"type\":\"keyDown\",\"key\":" + k + L",\"text\":" + k + L",\"unmodifiedText\":" + k + L"}"); Cdp(L"Input.dispatchKeyEvent", L"{\"type\":\"keyUp\",\"key\":" + k + L"}"); } }
void Key(const wchar_t* key, int vk) {
    const std::wstring common = std::wstring(L"\"key\":\"") + key + L"\",\"code\":\"" + key + L"\",\"windowsVirtualKeyCode\":" + std::to_wstring(vk) + L",\"nativeVirtualKeyCode\":" + std::to_wstring(vk);
    Cdp(L"Input.dispatchKeyEvent", std::wstring(L"{\"type\":\"") + (vk == 13 ? L"keyDown\",\"text\":\"\\r" : L"rawKeyDown") + L"\"," + common + L"}");
    Cdp(L"Input.dispatchKeyEvent", L"{\"type\":\"keyUp\"," + common + L"}");
}
void Go(const wchar_t* path) { reports.clear(); view->Navigate((base + path).c_str()); }
int Count(Forms::Report::Kind k) { int n = 0; for (const auto& g : reports) if (g.r.kind == k) ++n; return n; }
const Got* Last(Forms::Report::Kind k) { const Got* l = nullptr; for (const auto& g : reports) if (g.r.kind == k) l = &g; return l; }

struct Step { UINT delay; std::function<void()> run; };
std::vector<Step> steps; size_t next = 0;
void Run() {
    if (next >= steps.size()) { std::printf("%s forms world in the engine: %d checks, %d failed\n", failures ? "FAIL" : "PASS", checks, failures); Finish(); return; }
    SetTimer(nullptr, 0, steps[next].delay, [](HWND, UINT, UINT_PTR id, DWORD) { KillTimer(nullptr, id); const auto s = steps[next++]; s.run(); Run(); });
}
void Scenario() {
    using K = Forms::Report::Kind;
    steps = {
        {100, [] { Go(L"/polar"); }},
        {1500, [] { Click(L"email"); }},
        {300, [] { reports.clear(); Type(L"va"); }},
        {700, [] {
            const Got* t = Last(K::Type);
            Check(t && t->r.value == L"va" && t->r.field == L"email" && t->r.w > 200 && t->r.h > 20 && t->r.dpr > 0 && t->context > 0 && !t->r.password, "P3: trusted typing in an e-mail field reports « va » with the field's geometry and the world's context");
            if (t) { context = t->context; seq = t->r.seq; }
            Js(L"window.__keys.join(',')"); }},
        {300, [] { Check(lastJs == L"\"v,a\"", "the page received the two letters (nothing swallowed while no list is shown)"); reports.clear(); Key(L"ArrowDown", 40); }},
        {400, [] { Check(Count(K::Select) == 0, "P6: no list shown by the host: ↓ is not routed (the page keeps it)"); Eval(Forms::ShownCall(seq, 3)); }},
        {300, [] { reports.clear(); Js(L"window.__keys=[];1"); Key(L"ArrowDown", 40); Key(L"Enter", 13); }},
        {600, [] {
            const Got* s = Last(K::Select); const Got* p = Last(K::Pick);
            Check(s && s->r.index == 0 && p && p->r.index == 0, "list shown: ↓ selects the first row, Entrée picks it (routed by the world)");
            Js(L"window.__keys.join(',')+'|'+window.__submits"); }},
        {300, [] { Check(lastJs == L"\"|0\"", "↓ and Entrée never reached the page, the form was not submitted"); Eval(Forms::FillCall(seq, L"val@atelier-lumen.fr")); }},
        {400, [] { Check(Forms::ParseFillAnswer(0, lastEval.c_str()) == Forms::FillAnswer::Filled, "P9: the fill answers « filled » for the same, focused, unmoved element"); Js(L"document.getElementById('email').value"); }},
        {300, [] { Check(lastJs == L"\"val@atelier-lumen.fr\"", "the field holds the chosen e-mail"); Eval(Forms::FillCall(seq, L"autre@exemple.test")); }},
        {300, [] { Check(Forms::ParseFillAnswer(0, lastEval.c_str()) == Forms::FillAnswer::Stale, "a second fill for the same list is refused (stale)"); reports.clear();
                   Js(L"var e=document.getElementById('email');e.value='';e.focus();document.execCommand('insertText',false,'va');1"); }},
        {700, [] { Check(Count(K::Type) == 0, "P3/T4: a page typing by itself (execCommand) opens nothing"); Js(L"document.getElementById('email').value=''"); }},
        {200, [] { Click(L"email"); }},
        {300, [] { Type(L"valentin.m"); }},
        {500, [] { reports.clear(); Click(L"go"); }},
        {900, [] {
            const Got* i = Last(K::Invalid);
            Check(i && i->r.message == L"email-at" && i->r.w > 200, "P14: an e-mail without @ reports « email-at » under the field (our bubble, the engine's cancelled)");
            Js(L"String(window.__submits)"); }},
        {300, [] { Check(lastJs == L"\"0\"", "the invalid form is not submitted"); reports.clear(); Type(L"@"); }},
        {500, [] { Check(Count(K::Unbubble) >= 1, "the bubble goes when the person types"); Key(L"Escape", 27); }},
        // P12: what the person typed and sent is reported; what the page planted is not.
        {300, [] { Go(L"/polar"); }},
        {1500, [] { Click(L"email"); }},
        {300, [] { Type(L"camille.fictif@exemple.test"); }},
        {600, [] { Key(L"Escape", 27); reports.clear(); Click(L"go"); }},
        {900, [] { const Got* s = Last(K::Save); Check(s && s->r.values.size() == 1 && s->r.values[0] == L"camille.fictif@exemple.test", "P12: the e-mail typed and sent is reported once"); Go(L"/planted"); }},
        {1500, [] { Click(L"name"); }},
        {300, [] { Type(L"Camille"); }},
        {400, [] { reports.clear(); Click(L"go"); }},
        {900, [] { bool planted = false; for (const auto& g : reports) for (const auto& v : g.r.values) planted = planted || v.find(L"planted") != std::wstring::npos; Check(!planted, "T8: an e-mail the page planted is never reported"); Go(L"/own"); }},
        {1500, [] { reports.clear(); Click(L"go"); }},
        {900, [] { Check(Count(K::Invalid) == 0, "P14: a page that handles « invalid » itself keeps its own validation (no bubble from us)"); Js(L"document.title"); }},
        {300, [] { Check(lastJs.rfind(L"\"own:", 0) == 0, "the page's own handler ran"); Go(L"/custom"); }},
        {1500, [] { reports.clear(); Click(L"go"); }},
        {900, [] { const Got* i = Last(K::Invalid); const std::wstring text = i ? Forms::InvalidText(i->r.message, i->r.argA, i->r.argB) : L"";
                   Check(i && i->r.message == L"custom" && text.size() <= 150 && text.find(L'\x202E') == std::wstring::npos && text.find(L'\n') == std::wstring::npos, "T9: a page message is one line of 150 characters at most, without text-direction characters"); Go(L"/frame"); }},
        {1800, [] { Click(L"f", 16); }},
        {300, [] { reports.clear(); Type(L"va"); }},
        {700, [] { Check(Count(K::Type) == 0, "P4/T5: a field in a frame of another origin opens nothing"); Go(L"/invisible"); }},
        {1500, [] { Click(L"email"); }},
        {300, [] { reports.clear(); Type(L"va"); }},
        {700, [] { Check(Count(K::Type) == 0, "P5/T6: a nearly transparent field opens nothing"); Js(L"document.getElementById('email').value"); }},
        {300, [] { Check(lastJs == L"\"va\"", "(the letters did reach that field: the check above is not vacuous)"); Go(L"/moving"); }},
        {1500, [] { Click(L"email"); }},
        {300, [] { reports.clear(); Type(L"v"); }},
        {800, [] { Check(Count(K::Type) == 1 && Count(K::Close) >= 1, "P8/T2: a field that moves after the keystroke closes the list at once"); Go(L"/polar"); }},
        {1500, [] { Click(L"email"); }},
        {300, [] { reports.clear(); Type(L"va"); }},
        {600, [] { const Got* t = Last(K::Type); if (t) { context = t->context; seq = t->r.seq; } Eval(Forms::ShownCall(seq, 3)); }},
        {300, [] { reports.clear(); Js(L"window.__keys=[];1"); Key(L"ArrowDown", 40); Key(L"Delete", 46); }},
        {500, [] { const Got* d = Last(K::Remove); Check(d && d->r.index == 0, "Suppr on the row chosen with ↓ reports a removal of that row (board « états »)"); Js(L"window.__keys.join(',')"); }},
        {300, [] { Check(lastJs == L"\"\"", "neither ↓ nor Suppr reached the page"); reports.clear(); Key(L"Escape", 27); }},
        {500, [] { Check(Count(K::Close) == 1, "Échap closes the list (the world says so)"); Go(L"/idstep"); }},
        // #121: through the key, an identifier-only step gets the account's username, with no secret in the page.
        {1500, [] { Click(L"login"); }},
        {300, [] { Fresh(Vault::UserStepScript(Origin(), L"val@atelier-lumen.fr")); }},
        {600, [] { Check(lastOutcome == Vault::FillOutcome::User, "key path, identifier-only step: the identifier script answers « user »"); Js(L"document.getElementById('login').value"); }},
        {300, [] { Check(lastJs == L"\"val@atelier-lumen.fr\"", "the account's username is in the identifier field (no password needed, none decrypted)"); Go(L"/login"); }},
        {1500, [] { Click(L"login"); }},
        {300, [] { Fresh(Vault::UserStepScript(Origin(), L"val@atelier-lumen.fr")); }},
        {600, [] { Check(lastOutcome == Vault::FillOutcome::NeedsPassword, "a form with a visible password field answers « password » (the full fill follows, as before)"); Js(L"document.getElementById('login').value"); }},
        {300, [] { Check(lastJs == L"\"\"", "nothing is written by the identifier script on a login form"); Fresh(Vault::FillScript(Origin(), L"val@atelier-lumen.fr", L"Fictif-1!")); }},
        {600, [] { Check(lastOutcome == Vault::FillOutcome::Filled, "then FillScript fills username and password (unchanged path)"); Js(L"document.getElementById('login').value+'|'+document.getElementById('p').value"); }},
        {300, [] { Check(lastJs == L"\"val@atelier-lumen.fr|Fictif-1!\"", "both fields filled"); Go(L"/search"); }},
        {1500, [] { Click(L"q"); }},
        {300, [] { Fresh(Vault::UserStepScript(Origin(), L"val@atelier-lumen.fr")); }},
        {600, [] { Check(lastOutcome == Vault::FillOutcome::NoField, "a field that is not an identifier (a search box) gets nothing: the usual « no field » answer"); Js(L"document.getElementById('q').value"); }},
        {300, [] { Check(lastJs == L"\"\"", "the search box stays empty"); Go(L"/formless"); }},
        {1500, [] { Click(L"email"); }},
        {300, [] { Fresh(Vault::UserStepScript(Origin(), L"val@atelier-lumen.fr")); }},
        {600, [] { Check(lastOutcome == Vault::FillOutcome::User, "an identifier field outside any form (script-driven login page) also gets the username"); Fresh(Vault::UserStepScript(L"https://autre.exemple.test", L"val@atelier-lumen.fr")); }},
        {600, [] { Check(lastOutcome == Vault::FillOutcome::Origin, "another origin than the account's: refused before anything is written"); Go(L"/dates"); }},
        {1500, [] { reports.clear(); Js(L"document.getElementById('day').reportValidity();1"); }},
        {700, [] { const Got* i = Last(K::Invalid); Check(i && i->r.message == L"missing-value" && Forms::InvalidText(i->r.message, i->r.argA, i->r.argB) == L"Choisissez une valeur.", "a required date left empty: « Choisissez une valeur. » (owner decision)"); }},
    };
    Run();
}
void Receive(const wchar_t* event, std::function<void(const std::wstring&)> f) {
    ICoreWebView2DevToolsProtocolEventReceiver* r = nullptr;
    if (FAILED(view->GetDevToolsProtocolEventReceiver(event, &r)) || !r) { Say(L"no receiver"); return; }
    EventRegistrationToken token{};
    auto h = Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler, ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs*>(
        [f](ICoreWebView2*, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* a) -> HRESULT {
            LPWSTR j = nullptr; if (FAILED(a->get_ParameterObjectAsJson(&j)) || !j) return S_OK;
            const std::wstring json = j; CoTaskMemFree(j); f(json); return S_OK; });
    r->add_DevToolsProtocolEventReceived(h, &token); h->Release(); r->Release();
}
void OnView() {
    Receive(L"Runtime.bindingCalled", [](const std::wstring& json) { Got g; if (Forms::ParseReport(json, g.r, g.context)) reports.push_back(g); });
    auto rebind = [](const std::wstring&) { Cdp(L"Runtime.addBinding", Forms::BindingParams()); };
    Receive(L"Page.frameNavigated", rebind); Receive(L"Page.domContentEventFired", rebind);
    Cdp(L"Emulation.setFocusEmulationEnabled", L"{\"enabled\":true}");   // the never-shown window behaves as focused
    Cdp(L"Page.enable", L"{}");
    Cdp(L"Runtime.addBinding", Forms::BindingParams());
    Cdp(L"Page.addScriptToEvaluateOnNewDocument", Forms::ScriptParams(), [](HRESULT hr, const std::wstring&) { if (FAILED(hr)) { Say(L"SCRIPT NOT INSTALLED"); Finish(); return; } Scenario(); });
}
LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
} // namespace

int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i + 1 < argc; i += 2) { const std::wstring k = argv[i], v = argv[i + 1]; if (k == L"--base") base = v; else if (k == L"--udf") udf = v; }
    if (base.empty() || udf.empty()) { Say(L"usage: --base URL --udf DIR"); return 2; }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"FormsWorldHost"; RegisterClassW(&wc);
    // Never shown: the page renders, nothing appears on screen and the focus is never taken.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"forms world", WS_POPUP, -32000, -32000, 1024, 768, nullptr, nullptr, wc.hInstance, nullptr);
    HMODULE loader = LoadLibraryW(L"WebView2Loader.dll");
    using CreateFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);
    auto create = loader ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
    if (!create) { Say(L"WebView2Loader.dll missing"); return 2; }
    auto created = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>([hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
        auto made = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>([](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT {
            if (FAILED(hr2) || !c) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            controller = c; c->AddRef(); RECT r{0, 0, 1024, 768}; c->put_Bounds(r); c->put_IsVisible(TRUE);
            if (FAILED(c->get_CoreWebView2(&view)) || !view) { Say(L"ENGINE FAILED"); Finish(); return S_OK; }
            OnView(); return S_OK; });
        env->CreateCoreWebView2Controller(hwnd, made); made->Release(); return S_OK; });
    if (FAILED(create(nullptr, udf.c_str(), nullptr, created))) { Say(L"ENGINE FAILED"); return 2; }
    created->Release();
    SetTimer(nullptr, 0, 120000, [](HWND, UINT, UINT_PTR, DWORD) { Say(L"TIMEOUT"); ++failures; Finish(); });
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if (view) view->Release();
    if (controller) { controller->Close(); controller->Release(); }
    return failures ? 1 : 0;
}
