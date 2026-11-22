#pragma once
// Chat — R36: the page after an agent's change, without a hard cut (design/chat-comment-v2-retours.md, R36; skills
// apple-design / emil-design-eng). Two levels, decided by the user:
//   1. Universal fade: just before the reload, a capture of the page as shown (CapturePreview) is put above the WebView
//      in a click-through child layered window; the page reloads under it (the engine keeps the scroll position); once
//      the new page has loaded (NavigationCompleted + a short settle, or after a cap), the capture fades out in 250 ms.
//      Works for any site and any change: what the user sees goes from the old page to the new one in one dissolve.
//   2. Values glide: when the turn changed style sheets ONLY (.css), no reload: every <link rel=stylesheet> of the page
//      that serves one of those files is replaced in place by a fresh copy, under a temporary rule that gives every
//      element a 250 ms transition (cubic-bezier(.23,1,.32,1)): sizes, spacings and colours glide to their new values.
//      The page script is told (« browser-restyle » event): the frame, labels and padding / gap inspector follow the
//      gliding element frame by frame (annotation-script.h, onRestyle). Automatic fallback on (1) when a file is not a
//      linked sheet of the page (an @import, a <style>, a component), when a sheet fails to load, or when nothing answers.
// Reduced motion: (1) only, with a short fade (150 ms), no glide.
// The caller (chat-host-ui.h, AfterTurnPage) calls Reload() where it used to call view->Reload().
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <WebView2.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ChatTransition {

// ---------------------------------------------------------------- pure parts (tests/annotations-ui/page_v4_test.cpp)
inline constexpr double FadeMs = 250, ReducedFadeMs = 150, GlideMs = 250;
inline constexpr unsigned SettleMs = 50,      // after NavigationCompleted: the new page's first frames are on screen
                          LoadCapMs = 4000,   // the capture never stays longer than this after the reload started
                          CaptureCapMs = 700, // no capture by then: reload without it
                          SwapCheckMs = 400, SwapCapMs = 3500;   // the in-place swap's answer, polled until the cap

inline bool IsStyleSheet(const std::string& path) {
    if (path.size() < 5) return false;
    std::string ext = path.substr(path.size() - 4); for (auto& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".css";
}
// Level 2 only when every changed file is a plain style sheet.
inline bool StyleOnly(const std::vector<std::string>& paths) {
    if (paths.empty() || paths.size() > 32) return false;
    for (const auto& p : paths) if (!IsStyleSheet(p) || p.size() > 1024) return false;
    return true;
}
// CSS cubic-bezier(x1, y1, x2, y2) at progress t in [0, 1] (x solved by Newton, bisection as a fallback).
inline double Bezier(double x1, double y1, double x2, double y2, double t) {
    if (t <= 0) return 0; if (t >= 1) return 1;
    auto bx = [&](double s) { const double u = 1 - s; return 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s; };
    auto by = [&](double s) { const double u = 1 - s; return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s; };
    auto dx = [&](double s) { const double u = 1 - s; return 3 * u * u * x1 + 6 * u * s * (x2 - x1) + 3 * s * s * (1 - x2); };
    double s = t;
    for (int i = 0; i < 8; ++i) { const double e = bx(s) - t; if (std::fabs(e) < 1e-6) return by(s); const double d = dx(s); if (std::fabs(d) < 1e-6) break; s -= e / d; }
    double lo = 0, hi = 1; s = t;
    for (int i = 0; i < 40; ++i) { const double v = bx(s); if (std::fabs(v - t) < 1e-6) break; if (v < t) lo = s; else hi = s; s = (lo + hi) / 2; }
    return by(s);
}
// The capture's opacity (1 -> 0) `ms` after the fade started: an ease-in-out dissolve (the standard curve: the old
// page leaves softly and the new one is fully there at the end, no visible step); linear-ish and short when reduced.
inline double FadeOpacity(double ms, bool reduced) {
    const double d = reduced ? ReducedFadeMs : FadeMs;
    const double t = std::clamp(ms / d, 0.0, 1.0);
    return 1.0 - (reduced ? t : Bezier(.4, 0, .2, 1, t));
}

inline std::wstring JsString(const std::string& s) {   // UTF-8 -> a JS string literal (only ASCII escapes, the rest as \uXXXX)
    std::wstring o = L"\"";
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]); std::uint32_t cp = c; std::size_t n = 1;
        if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 7u) << 18) | ((s[i + 1] & 63u) << 12) | ((s[i + 2] & 63u) << 6) | (s[i + 3] & 63u); n = 4; }
        else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 15u) << 12) | ((s[i + 1] & 63u) << 6) | (s[i + 2] & 63u); n = 3; }
        else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 31u) << 6) | (s[i + 1] & 63u); n = 2; }
        else if (c >= 0x80) cp = 0xFFFD;
        i += n;
        if (cp >= 0x20 && cp < 0x7F && cp != '"' && cp != '\\' && cp != '<' && cp != '>') { o += wchar_t(cp); continue; }
        auto hex = [&](std::uint32_t u) { wchar_t b[8]; swprintf(b, 8, L"\\u%04x", unsigned(u)); o += b; };
        if (cp >= 0x10000) { cp -= 0x10000; hex(0xD800 + (cp >> 10)); hex(0xDC00 + (cp & 0x3FF)); } else hex(cp);
    }
    return o + L"\"";
}
// Level 2, in the page: answers "started", "nomatch" (a file is not a same-origin linked sheet), "busy" or "error"; the
// outcome is then in window.__browserRestyle.state: "pending" -> "done" | "failed" (see SwapStateScript).
inline std::wstring SwapScript(const std::vector<std::string>& paths, double ms) {
    std::wstring files = L"[";
    for (std::size_t i = 0; i < paths.size(); ++i) { if (i) files += L","; files += JsString(paths[i]); }
    files += L"]";
    return LR"JS((function (files, ms) {
'use strict';
try {
  var W = window, D = document, S0 = W.__browserRestyle;
  if (S0 && S0.state === 'pending') return 'busy';
  var links = [].slice.call(D.querySelectorAll('link[rel~="stylesheet"][href]')), hit = [], all = true;
  for (var i = 0; i < files.length; i++) {
    var f = String(files[i]).replace(/\\/g, '/').replace(/^\.?\//, '').toLowerCase(), base = f.split('/').pop(), found = false;
    for (var j = 0; j < links.length; j++) {
      var l = links[j], u = null; try { u = new URL(l.href, location.href); } catch (e) { continue; }
      if (u.origin !== location.origin || l.disabled) continue;
      var p = ''; try { p = decodeURIComponent(u.pathname).toLowerCase(); } catch (e) { p = u.pathname.toLowerCase(); }
      if (p === '/' + f || p.slice(-(f.length + 1)) === '/' + f || p.split('/').pop() === base) { found = true; if (hit.indexOf(l) < 0) hit.push(l); }
    }
    if (!found) all = false;
  }
  if (!all || !hit.length) return 'nomatch';
  var S = W.__browserRestyle = { state: 'pending' }, left = hit.length, over = false, fresh = [];
  var st = D.createElement('style'); st.setAttribute('data-browser-restyle', '');
  st.textContent = '*,*::before,*::after{transition:all ' + ms + 'ms cubic-bezier(.23,1,.32,1) 0s !important}';
  (D.head || D.documentElement).appendChild(st);
  void getComputedStyle(D.documentElement).color;   // the transition rule is in force before any value changes
  function tell() { try { W.dispatchEvent(new CustomEvent('browser-restyle', { detail: { ms: ms } })); } catch (e) {} }
  function fail() { if (over) return; over = true; for (var k = 0; k < fresh.length; k++) try { fresh[k].remove(); } catch (e) {} try { st.remove(); } catch (e) {} S.state = 'failed'; }
  tell();
  hit.forEach(function (l) {
    var n = l.cloneNode(false), u = new URL(l.href, location.href);
    u.searchParams.set('browser-restyle', String(Date.now()));
    n.onload = function () {
      if (over) return;
      if (--left) return;
      over = true; tell();
      for (var k = 0; k < hit.length; k++) try { hit[k].remove(); } catch (e) {}
      setTimeout(function () { try { st.remove(); } catch (e) {} S.state = 'done'; }, ms + 80);
    };
    n.onerror = fail;
    n.href = u.href; fresh.push(n);
    if (l.nextSibling) l.parentNode.insertBefore(n, l.nextSibling); else l.parentNode.appendChild(n);
  });
  setTimeout(function () { if (!over) fail(); }, 3000);
  return 'started';
} catch (e) { return 'error'; }
}))JS" + std::wstring(L"(") + files + L"," + std::to_wstring(int(ms)) + L")";
}
inline const wchar_t* SwapStateScript() { return L"(function(){try{var s=window.__browserRestyle;return s?String(s.state):'none'}catch(e){return 'error'}})()"; }
// The answer of an ExecuteScript (a JSON string) without its quotes.
inline std::wstring Unquote(const wchar_t* r) {
    std::wstring s = r ? r : L"";
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') return s.substr(1, s.size() - 2);
    return s;
}

// ---------------------------------------------------------------- the host side
namespace detail {
// The handlers' interface ids (WebView2.h), as main.cpp's GUID_TEXT list: this header stands alone (unit-tested).
inline constexpr const wchar_t* ScriptDone = L"{49511172-cc67-4bca-9923-137112f4c4cc}";
inline constexpr const wchar_t* CaptureDone = L"{697e05e9-3d8f-45fa-96f4-8ffe1ededaf5}";
inline constexpr const wchar_t* NavigationDone = L"{d33a35bf-1c49-4f98-93ab-006e0533fe1c}";
template<class I, class... A> class Fn final : public I {
    LONG refs_ = 1; IID iid_{}; std::function<HRESULT(A...)> fn_;
public:
    Fn(const wchar_t* iid, std::function<HRESULT(A...)> f) : fn_(std::move(f)) { CLSIDFromString(iid, &iid_); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id == IID_IUnknown || id == iid_) { *out = static_cast<I*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn_ ? fn_(a...) : S_OK; }
};
template<class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

// The capture, decoded to premultiplied BGRA at the WebView's size.
inline bool Decode(IStream* png, int w, int h, std::vector<std::uint32_t>& out) {
    IWICImagingFactory* wic = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* frame = nullptr; IWICBitmapScaler* sc = nullptr; IWICFormatConverter* cv = nullptr;
    bool ok = false; LARGE_INTEGER zero{}; png->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromStream(png, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) && SUCCEEDED(dec->GetFrame(0, &frame))) {
        UINT fw = 0, fh = 0; frame->GetSize(&fw, &fh);
        IWICBitmapSource* src = frame;
        if ((int(fw) != w || int(fh) != h) && SUCCEEDED(wic->CreateBitmapScaler(&sc)) && SUCCEEDED(sc->Initialize(frame, UINT(w), UINT(h), WICBitmapInterpolationModeCubic))) src = sc;
        if (SUCCEEDED(wic->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(src, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            out.assign(std::size_t(w) * std::size_t(h), 0);
            ok = SUCCEEDED(cv->CopyPixels(nullptr, UINT(w) * 4, UINT(out.size() * 4), reinterpret_cast<BYTE*>(out.data())));
        }
    }
    Rel(cv); Rel(sc); Rel(frame); Rel(dec); Rel(wic);
    return ok;
}
// The WebView's own child window (z-order: the capture goes right above it, under the native layers above the page).
inline HWND WebViewWindow(HWND parent, const RECT& bounds) {
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c)) continue;
        wchar_t cls[64]{}; GetClassNameW(c, cls, 64);
        if (wcsncmp(cls, L"Chrome_WidgetWin", 16) != 0) continue;
        RECT r{}; GetWindowRect(c, &r); MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&r), 2);
        if (std::abs(r.left - bounds.left) <= 2 && std::abs(r.top - bounds.top) <= 2 && std::abs(r.right - bounds.right) <= 2 && std::abs(r.bottom - bounds.bottom) <= 2) return c;
    }
    return nullptr;
}
}

// Review M3: how a transition ended, told once to the caller (UI thread): Glided = the sheets were swapped in place,
// no reload (the page script and its marks are still there: the caller puts its « done » mark itself); Reloaded = the
// page was reloaded (with the fade or plainly: the usual re-arm path follows); Cancelled = a newer transition replaced it.
enum class Outcome { Glided, Reloaded, Cancelled };
using Done = std::function<void(Outcome)>;

class Runner {
public:
    static Runner& Get() { static Runner r; return r; }
    // Instead of view->Reload() after a turn. False: nothing could be done (no view).
    bool Reload(ICoreWebView2Controller* controller, ICoreWebView2* view, const std::vector<std::string>& changedPaths, bool reduced, Done done = nullptr) {
        if (!view) return false;
        exec_ = nullptr; reload_ = nullptr;
        Start(controller, view, changedPaths, reduced, std::move(done));
        return true;
    }
    // Tests: the same machine with the page replaced by two functions (no WebView2): `exec` runs a script and answers
    // (hr, result as ExecuteScript gives it), `reload` stands for view->Reload(). No controller: no capture, no overlay.
    using Exec = std::function<bool(const std::wstring& script, std::function<void(HRESULT, const std::wstring&)> answer)>;
    void ReloadForTest(const std::vector<std::string>& changedPaths, bool reduced, Exec exec, std::function<void()> reload, Done done) {
        exec_ = std::move(exec); reload_ = std::move(reload);
        Start(nullptr, nullptr, changedPaths, reduced, std::move(done));
    }
    // Measures for the real-window pass (H1 / QA): the last transition's path and timings.
    struct Stats { std::wstring path; double captureMs = -1, reloadAt = -1, loadedAt = -1, fadeStart = -1, fadeEnd = -1; int fadeFrames = 0; };
    const Stats& Last() const { return stats_; }
    bool Busy() const { return overlay_ != nullptr || swapping_; }

private:
    Done done_; Exec exec_; std::function<void()> reload_;
    void Start(ICoreWebView2Controller* controller, ICoreWebView2* view, const std::vector<std::string>& changedPaths, bool reduced, Done done) {
        Cancel();   // a previous transition ends where it is (its capture goes at once; its caller hears « Cancelled »)
        reduced_ = reduced; ++serial_;
        Keep(controller, view); done_ = std::move(done);
        if (!reduced && StyleOnly(changedPaths)) { Swap(changedPaths); return; }
        Fade();
    }
    // Runs a script in the page (or the test's stand-in); false: it could not be started.
    bool Run(const std::wstring& script, std::function<void(HRESULT, const std::wstring&)> answer) {
        if (exec_) return exec_(script, std::move(answer));
        if (!view_) return false;
        auto* cb = new detail::Fn<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(detail::ScriptDone, [answer](HRESULT hr, LPCWSTR r) -> HRESULT {
            answer(hr, r ? std::wstring(r) : std::wstring()); return S_OK; });
        const bool ok = SUCCEEDED(view_->ExecuteScript(script.c_str(), cb));
        cb->Release(); return ok;
    }
    void Finish(Outcome o) {
        Done d = std::move(done_); done_ = nullptr;
        Release();
        if (d) d(o);
    }
    ICoreWebView2Controller* controller_ = nullptr; ICoreWebView2* view_ = nullptr; EventRegistrationToken navToken_{}; bool navHooked_ = false;
    HWND overlay_ = nullptr, timerWnd_ = nullptr; HDC mem_ = nullptr; HBITMAP dib_ = nullptr; HGDIOBJ old_ = nullptr; SIZE size_{};
    bool reduced_ = false, swapping_ = false, reloaded_ = false, fading_ = false; unsigned serial_ = 0;
    double t0_ = 0, fadeT0_ = 0, swapStart_ = 0; Stats stats_;
    enum : UINT_PTR { TFade = 1, TSettle = 2, TCap = 3, TSwap = 4, TCapture = 5 };

    static double Now() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) * 1000.0 / double(f.QuadPart); }
    double Since() const { return Now() - t0_; }
    void Keep(ICoreWebView2Controller* c, ICoreWebView2* v) {
        if (c) c->AddRef(); if (v) v->AddRef();
        detail::Rel(controller_); detail::Rel(view_); controller_ = c; view_ = v;
        t0_ = Now(); stats_ = Stats{};
    }
    static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCHITTEST) return HTTRANSPARENT;
        if (m == WM_TIMER) { Get().OnTimer(UINT_PTR(w)); return 0; }
        return DefWindowProcW(h, m, w, l);
    }
    static const wchar_t* Class() {
        static bool done = false; const wchar_t* name = L"BrowserChatTransition";
        if (!done) { WNDCLASSW c{}; c.lpfnWndProc = Proc; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = name; c.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&c); done = true; }
        return name;
    }
    HWND Timers() {
        if (!timerWnd_ || !IsWindow(timerWnd_)) timerWnd_ = CreateWindowExW(0, Class(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        return timerWnd_;
    }
    void After(UINT_PTR id, unsigned ms) { if (HWND t = Timers()) SetTimer(t, id, ms, nullptr); }
    void Stop(UINT_PTR id) { if (timerWnd_) KillTimer(timerWnd_, id); }

    // i18n-exempt-begin: stats_.path is a QA measurement code (Runner::Last), never shown to the user
    // ---- level 2: the style sheets swapped in place
    void Swap(const std::vector<std::string>& paths) {
        swapping_ = true; stats_.path = L"glide";
        const unsigned serial = serial_;
        const bool started = Run(SwapScript(paths, GlideMs), [this, serial](HRESULT hr, const std::wstring& r) {
            if (serial != serial_ || !swapping_) return;
            const std::wstring a = SUCCEEDED(hr) ? Unquote(r.c_str()) : L"error";
            if (a == L"started") { swapStart_ = Now(); After(TSwap, SwapCheckMs); }
            else { swapping_ = false; stats_.path = L"fade (glide: " + a + L")"; Fade(); } });
        if (!started && serial == serial_ && swapping_) { swapping_ = false; Fade(); }
    }
    void CheckSwap() {
        if (!swapping_ || (!view_ && !exec_)) return;
        const unsigned serial = serial_;
        const bool started = Run(SwapStateScript(), [this, serial](HRESULT hr, const std::wstring& r) {
            if (serial != serial_ || !swapping_) return;
            const std::wstring s = SUCCEEDED(hr) ? Unquote(r.c_str()) : L"error";
            if (s == L"done") { swapping_ = false; stats_.fadeEnd = Since(); Finish(Outcome::Glided); return; }   // M3: the caller's « done »
            if (s == L"pending" && Now() - swapStart_ < SwapCapMs) { After(TSwap, SwapCheckMs); return; }
            swapping_ = false; stats_.path = L"fade (glide: " + s + L")"; Fade(); });
        if (!started && serial == serial_ && swapping_) { swapping_ = false; Fade(); }
    }

    // ---- level 1: capture, reload under it, dissolve
    void Fade() {
        if (stats_.path.empty()) stats_.path = L"fade";
        if (!controller_ || !view_) { PlainReload(); return; }
        IStream* stream = SHCreateMemStream(nullptr, 0);
        if (!stream) { PlainReload(); return; }
        const unsigned serial = serial_;
        After(TCapture, CaptureCapMs);
        auto* cb = new detail::Fn<ICoreWebView2CapturePreviewCompletedHandler, HRESULT>(detail::CaptureDone, [this, serial, stream](HRESULT hr) -> HRESULT {
            Stop(TCapture);
            if (serial == serial_ && !reloaded_) { if (SUCCEEDED(hr)) { stats_.captureMs = Since(); Cover(stream); } StartReload(); }
            stream->Release(); return S_OK; });
        if (FAILED(view_->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, cb))) { Stop(TCapture); stream->Release(); PlainReload(); }
        cb->Release();
    }
    void PlainReload() { reloaded_ = true; stats_.reloadAt = Since(); if (reload_) reload_(); else if (view_) view_->Reload(); Finish(Outcome::Reloaded); }
    // The capture above the WebView, opaque, on screen before the reload starts.
    void Cover(IStream* png) {
        HWND parent = nullptr; RECT b{};
        if (!controller_ || FAILED(controller_->get_ParentWindow(&parent)) || !parent || FAILED(controller_->get_Bounds(&b))) return;
        const int w = b.right - b.left, h = b.bottom - b.top; if (w < 1 || h < 1 || w > 16384 || h > 16384) return;
        std::vector<std::uint32_t> px; if (!detail::Decode(png, w, h, px)) return;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr; HDC screen = GetDC(nullptr);
        dib_ = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); mem_ = CreateCompatibleDC(screen); ReleaseDC(nullptr, screen);
        if (!dib_ || !mem_ || !bits) { DropBitmap(); return; }
        for (auto& p : px) p |= 0xFF000000u;   // the page is opaque: never let the old frame show through its own pixels
        memcpy(bits, px.data(), px.size() * 4); old_ = SelectObject(mem_, dib_); size_ = SIZE{w, h};
        overlay_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, Class(), L"", WS_CHILD | WS_CLIPSIBLINGS, b.left, b.top, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!overlay_) { DropBitmap(); return; }
        HWND web = detail::WebViewWindow(parent, b), above = web ? GetWindow(web, GW_HWNDPREV) : nullptr;
        SetWindowPos(overlay_, above ? above : HWND_TOP, b.left, b.top, w, h, SWP_NOACTIVATE);
        if (!Alpha(255)) { DestroyWindow(overlay_); overlay_ = nullptr; DropBitmap(); return; }
        ShowWindow(overlay_, SW_SHOWNA);
        DwmFlushSafe();
    }
    static void DwmFlushSafe() { using F = HRESULT(WINAPI*)(); static F f = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"), "DwmFlush"))); if (f) f(); }
    bool Alpha(int a) {
        if (!overlay_ || !mem_) return false;
        BLENDFUNCTION bf{AC_SRC_OVER, 0, BYTE(std::clamp(a, 0, 255)), AC_SRC_ALPHA}; POINT src{0, 0}; SIZE sz = size_;
        return UpdateLayeredWindow(overlay_, nullptr, nullptr, &sz, mem_, &src, 0, &bf, ULW_ALPHA) != FALSE;
    }
    void StartReload() {
        if (!view_ || reloaded_) return;
        reloaded_ = true;
        if (!overlay_) { PlainReload(); return; }
        const unsigned serial = serial_;
        auto* h = new detail::Fn<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(detail::NavigationDone,
            [this, serial](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (serial == serial_ && overlay_ && !fading_) { stats_.loadedAt = Since(); Unhook(); After(TSettle, SettleMs); }
                return S_OK; });
        if (SUCCEEDED(view_->add_NavigationCompleted(h, &navToken_))) navHooked_ = true;
        h->Release();
        After(TCap, LoadCapMs);
        stats_.reloadAt = Since();
        if (FAILED(view_->Reload())) StartFade();
    }
    void Unhook() { if (navHooked_ && view_) view_->remove_NavigationCompleted(navToken_); navHooked_ = false; }
    void StartFade() {
        Stop(TCap); Stop(TSettle); Unhook();
        if (!overlay_) { Finish(Outcome::Reloaded); return; }
        if (fading_) return;
        fading_ = true; fadeT0_ = Now(); stats_.fadeStart = Since();
        After(TFade, USER_TIMER_MINIMUM);
    }
    void FadeStep() {
        const double ms = Now() - fadeT0_, o = FadeOpacity(ms, reduced_);
        ++stats_.fadeFrames;
        if (o <= 0.002) { stats_.fadeEnd = Since(); Stop(TFade); Finish(Outcome::Reloaded); return; }
        Alpha(int(std::lround(o * 255)));
    }
    void OnTimer(UINT_PTR id) {
        if (id == TFade) { FadeStep(); return; }
        Stop(id);
        if (id == TSettle || id == TCap) StartFade();
        else if (id == TSwap) CheckSwap();
        else if (id == TCapture) { if (!reloaded_) { stats_.path += L" (no capture)"; StartReload(); } }
    }
    // i18n-exempt-end
    void DropBitmap() { if (mem_) { if (old_) SelectObject(mem_, old_); DeleteDC(mem_); } if (dib_) DeleteObject(dib_); mem_ = nullptr; dib_ = nullptr; old_ = nullptr; }
    void Release() {
        Stop(TFade); Stop(TSettle); Stop(TCap); Stop(TSwap); Stop(TCapture); Unhook();
        if (overlay_) { DestroyWindow(overlay_); overlay_ = nullptr; }
        DropBitmap(); fading_ = false; swapping_ = false; reloaded_ = false;
        detail::Rel(controller_); detail::Rel(view_);
    }
    void Cancel() { Done d = std::move(done_); done_ = nullptr; if (overlay_ || swapping_ || controller_ || view_) Release(); if (d) d(Outcome::Cancelled); }
};

// R36: the call that replaces view->Reload() after an agent's turn (chat-host-ui.h, AfterTurnPage). changedPaths: the
// files the turn changed (UTF-8, relative to the project folder, '/' separators); reduced: prefers-reduced-motion.
// done (review M3): told once how it ended (Outcome).
inline bool Reload(ICoreWebView2Controller* controller, ICoreWebView2* view, const std::vector<std::string>& changedPaths, bool reduced, Done done = nullptr) {
    return Runner::Get().Reload(controller, view, changedPaths, reduced, std::move(done));
}
} // namespace ChatTransition
