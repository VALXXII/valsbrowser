#pragma once
// Onboarding — the hand-off to the browser (steps 2 and 3 of the end of the flow): the onboarding window
// shrinks and fades out, the browser window grows in and fades in, slightly overlapping.
//
// Real top-level windows cannot be scaled (the browser hosts WebView2), so both parts are played with pictures
// of them: a snapshot of the window (PrintWindow, full content, device pixels — crisp at any DPI) is shown in a
// layered stand-in window exactly over it, and that stand-in is drawn scaled and faded with per-pixel alpha
// (UpdateLayeredWindow), rounded like the window. The real onboarding window is hidden only once its stand-in is
// on screen; the real browser window is created, shown and painted CLOAKED (DWM keeps it invisible but composes
// it), photographed, and uncovered under the last frame of its stand-in — nobody sees an unpainted browser, and
// no white or black frame comes between.
//
// The stage runs on its own thread: the browser's start (window creation, settings, engine) keeps the UI thread
// busy for a moment, and the animation must not wait for it. If the browser's picture is not there when its
// turn comes, the stand-in simply starts when it arrives; if it never does (or a capture fails), the real window
// is shown the plain way. Times and scales: src/onboarding-tuning.h (ExitPlan).
#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include "onboarding-tuning.h"

namespace OnboardingHandoff {

struct Snapshot {
    int width = 0, height = 0; RECT rect{}; float radius = 0;   // device pixels; rect: where the window is on screen
    std::vector<std::uint32_t> bgrx;
    bool Valid() const { return width > 0 && height > 0 && bgrx.size() == static_cast<std::size_t>(width) * height; }
};
inline void Cloak(HWND window, bool on) { BOOL value = on ? TRUE : FALSE; DwmSetWindowAttribute(window, 13 /* DWMWA_CLOAK */, &value, sizeof(value)); }
// A picture of `window` as the compositor holds it. Empty when it cannot be taken or shows nothing at all.
inline Snapshot Capture(HWND window, float radiusPx) {
    Snapshot s; if (!window || !GetWindowRect(window, &s.rect)) return {};
    const int w = s.rect.right - s.rect.left, h = s.rect.bottom - s.rect.top; if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return {};
    HDC screen = GetWindowDC(window); HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (bitmap && bits) {
        HGDIOBJ old = SelectObject(memory, bitmap);
        ok = PrintWindow(window, memory, 2 /* PW_RENDERFULLCONTENT */) != FALSE; GdiFlush();
        if (ok) { s.bgrx.assign(static_cast<const std::uint32_t*>(bits), static_cast<const std::uint32_t*>(bits) + static_cast<std::size_t>(w) * h); s.width = w; s.height = h; s.radius = radiusPx; }
        SelectObject(memory, old);
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory); ReleaseDC(window, screen);
    if (!ok) return {};
    // A picture with a single colour is a window that has not drawn yet: not something to show.
    const std::uint32_t first = s.bgrx.front() & 0xFFFFFF; bool flat = true;
    for (std::size_t i = 0; i < s.bgrx.size() && flat; i += 97) flat = (s.bgrx[i] & 0xFFFFFF) == first;
    return flat ? Snapshot{} : s;
}
// Review panel only (« Rejouer » of « 7 → navigateur »): a stand-in for the browser window, its canvas and
// toolbar band, where the browser opens (1180 x 815 DIP, centred on the monitor of `near`).
inline Snapshot Placeholder(HWND nearWindow) {
    MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(nearWindow, MONITOR_DEFAULTTONEAREST), &mi);
    const float scale = (GetDpiForWindow(nearWindow) ? GetDpiForWindow(nearWindow) : 96) / 96.f;
    Snapshot s; s.width = std::min<int>(static_cast<int>(1180 * scale), mi.rcWork.right - mi.rcWork.left); s.height = std::min<int>(static_cast<int>(815 * scale), mi.rcWork.bottom - mi.rcWork.top); s.radius = 8 * scale;
    s.rect.left = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - s.width) / 2; s.rect.top = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - s.height) / 2; s.rect.right = s.rect.left + s.width; s.rect.bottom = s.rect.top + s.height;
    s.bgrx.assign(static_cast<std::size_t>(s.width) * s.height, 0xFF0E0E0E);
    const int band = static_cast<int>(84 * scale);
    for (int y = 0; y < std::min(band, s.height); ++y) for (int x = 0; x < s.width; ++x) s.bgrx[static_cast<std::size_t>(y) * s.width + x] = 0xFF171717;
    for (int y = static_cast<int>(44 * scale); y < static_cast<int>(76 * scale) && y < s.height; ++y) for (int x = static_cast<int>(150 * scale); x < s.width - static_cast<int>(150 * scale); ++x) s.bgrx[static_cast<std::size_t>(y) * s.width + x] = 0xFF292929;
    return s;
}

class Stage {
    struct Shared {
        std::mutex lock; Snapshot leaving, entering; bool hasEntering = false, preview = false;
        Onboarding::Tuning::ExitPlan plan; HWND reveal = nullptr, notify = nullptr; UINT message = 0;
        HANDLE shown = nullptr; std::atomic<bool> active{false}, stop{false};
        ~Shared() { if (shown) CloseHandle(shown); }
    };
    std::shared_ptr<Shared> shared_;

    struct Layer {
        HWND window = nullptr; HDC memory = nullptr; HBITMAP dib = nullptr; HGDIOBJ old = nullptr; ID2D1Bitmap* picture = nullptr; Snapshot shot;
        bool Make(const Snapshot& s, ID2D1DCRenderTarget* target) {
            shot = s;
            window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST, L"Static", L"", WS_POPUP, s.rect.left, s.rect.top, s.width, s.height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);   // i18n-exempt: system window class
            if (!window) return false;
            HDC screen = GetDC(nullptr); memory = CreateCompatibleDC(screen);
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(info.bmiHeader); info.bmiHeader.biWidth = s.width; info.bmiHeader.biHeight = -s.height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr; dib = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0); ReleaseDC(nullptr, screen);
            if (!dib) return false;
            old = SelectObject(memory, dib);
            const RECT all{0, 0, s.width, s.height}; if (FAILED(target->BindDC(memory, &all))) return false;
            const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
            return SUCCEEDED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(s.width), static_cast<UINT32>(s.height)), s.bgrx.data(), static_cast<UINT32>(s.width) * 4, props, &picture)) && picture;
        }
        // The picture scaled about its centre, at `alpha`, with the window's rounded corners; then on screen.
        void Present(ID2D1DCRenderTarget* target, ID2D1Factory* factory, float scale, float alpha, bool show) {
            if (!window || !picture) return;
            const RECT all{0, 0, shot.width, shot.height}; target->BindDC(memory, &all);
            const float w = static_cast<float>(shot.width), h = static_cast<float>(shot.height);
            target->BeginDraw(); target->SetTransform(D2D1::Matrix3x2F::Identity()); target->Clear(D2D1::ColorF(0, 0, 0, 0));
            ID2D1RoundedRectangleGeometry* shape = nullptr; ID2D1Layer* layer = nullptr;
            if (alpha > 0 && SUCCEEDED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0, 0, w, h), shot.radius, shot.radius), &shape)) && SUCCEEDED(target->CreateLayer(nullptr, &layer))) {
                target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(w / 2, h / 2)));
                target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), std::min(1.f, std::max(0.f, alpha))), layer);
                target->DrawBitmap(picture, D2D1::RectF(0, 0, w, h), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target->PopLayer();
            }
            target->EndDraw();
            if (layer) layer->Release();
            if (shape) shape->Release();
            POINT at{shot.rect.left, shot.rect.top}, zero{0, 0}; SIZE size{shot.width, shot.height}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            UpdateLayeredWindow(window, nullptr, &at, &size, memory, &zero, 0, &blend, ULW_ALPHA);
            if (show && !IsWindowVisible(window)) ShowWindow(window, SW_SHOWNOACTIVATE);
        }
        void Free() {
            if (picture) { picture->Release(); picture = nullptr; }
            if (memory) { if (old) SelectObject(memory, old); DeleteDC(memory); memory = nullptr; }
            if (dib) { DeleteObject(dib); dib = nullptr; }
            if (window) { DestroyWindow(window); window = nullptr; }
        }
    };
    static double NowMs() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }
    static void Pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    static DWORD WINAPI Run(LPVOID param) {
        std::unique_ptr<std::shared_ptr<Shared>> hold(static_cast<std::shared_ptr<Shared>*>(param)); Shared& s = **hold;
        ID2D1Factory* factory = nullptr; ID2D1DCRenderTarget* target = nullptr; Layer out, in; bool outReady = false, inReady = false;
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory)) && SUCCEEDED(factory->CreateDCRenderTarget(&props, &target))) outReady = out.Make(s.leaving, target);
        if (outReady) out.Present(target, factory, 1, 1, true);
        DwmFlush(); SetEvent(s.shown);   // the real window may go now: its picture is on screen
        const Onboarding::Tuning::ExitPlan p = s.plan; const double start = NowMs(); const float browserDelay = p.browserStart - p.windowStart;
        auto ease = [&](float t) { return Onboarding::Tuning::Bezier(p.x1, p.y1, p.x2, p.y2, t); };
        double enterStart = -1, enterEnd = -1; bool revealed = false; HWND reveal = nullptr;
        for (;;) {
            Pump();
            const float ms = static_cast<float>(NowMs() - start);
            if (outReady) {
                const float w = ease(p.windowMs > 0 ? std::min(1.f, ms / p.windowMs) : 1.f);
                if (ms >= p.windowMs) { out.Free(); outReady = false; } else out.Present(target, factory, 1 + (p.windowScale - 1) * w, 1 - w, true);
            }
            if (!inReady && enterStart < 0 && target) {
                std::lock_guard<std::mutex> guard(s.lock);
                if (s.hasEntering) { inReady = in.Make(s.entering, target); reveal = s.reveal; enterStart = std::max<double>(ms, browserDelay); if (!inReady) enterStart = ms; }
            }
            if (enterStart >= 0 && ms >= enterStart) {
                const float u = p.browserMs > 0 ? std::min(1.f, static_cast<float>((ms - enterStart) / p.browserMs)) : 1.f, b = ease(u);
                if (inReady) in.Present(target, factory, p.browserScale + (1 - p.browserScale) * b, b, true);
                if (u >= 1 && enterEnd < 0) enterEnd = ms;
            }
            if (enterEnd >= 0 && !revealed) {
                // The real, painted window comes up under the last frame of its picture; then the picture goes.
                if (reveal) { Cloak(reveal, false); DwmFlush(); DwmFlush(); DwmFlush(); }
                revealed = true;
                if (!s.preview) break;
            }
            if (revealed && s.preview) {   // review: the stand-in stays a moment, then leaves
                const float gone = static_cast<float>((ms - enterEnd - 700) / 200);
                if (gone >= 1) break;
                if (gone > 0 && inReady) in.Present(target, factory, 1, 1 - gone, true);
            }
            if (s.stop.load() || (enterStart < 0 && ms > p.windowMs + 6000)) break;   // the browser never came: its plain start shows it
            DwmFlush();
        }
        in.Free(); out.Free();
        if (target) target->Release();
        if (factory) factory->Release();
        s.active = false;
        if (s.notify) PostMessageW(s.notify, s.message, 0, 0);
        return 0;
    }
public:
    bool Active() const { return shared_ && shared_->active.load(); }
    // Starts the window's exit with its picture. Returns once the picture is on screen (the caller then hides the
    // real window). `entering`: review only, a stand-in for the browser; `notify`: told when everything is over.
    bool Start(Snapshot leaving, const Onboarding::Tuning::ExitPlan& plan, const Snapshot* entering = nullptr, HWND notify = nullptr, UINT message = 0) {
        if (Active() || !leaving.Valid()) return false;
        auto s = std::make_shared<Shared>(); s->leaving = std::move(leaving); s->plan = plan; s->notify = notify; s->message = message;
        if (entering && entering->Valid()) { s->entering = *entering; s->hasEntering = true; s->preview = true; }
        s->shown = CreateEventW(nullptr, TRUE, FALSE, nullptr); s->active = true;
        auto* param = new std::shared_ptr<Shared>(s);
        HANDLE thread = CreateThread(nullptr, 0, Run, param, 0, nullptr);
        if (!thread) { delete param; return false; }
        CloseHandle(thread); shared_ = s;
        WaitForSingleObject(s->shown, 1000);
        return true;
    }
    // The browser window is painted (cloaked): its picture takes its turn, then `reveal` is uncovered.
    bool Enter(Snapshot entering, HWND reveal) {
        if (!Active() || !entering.Valid()) return false;
        std::lock_guard<std::mutex> guard(shared_->lock);
        if (shared_->hasEntering) return false;
        shared_->entering = std::move(entering); shared_->reveal = reveal; shared_->hasEntering = true; return true;
    }
    void Stop() { if (shared_) shared_->stop = true; }
};

} // namespace OnboardingHandoff
