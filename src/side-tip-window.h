#pragma once
// #143 · Infobulle B (src/side-tip.h): its window. A per-pixel-alpha layered popup owned by the browser window, never
// activated, click-through. Each frame is drawn offscreen (a Chrome on a WIC surface, software Direct2D, the browser's
// fonts) and handed to UpdateLayeredWindow together with its place and size (one call: the box and where it is never
// disagree, no flash), the opacity as the constant alpha. It frames on its own timer while something moves and does
// nothing at rest. UI thread only.
#include "tooltip.h"
#include "side-tip.h"
#include <memory>

namespace NativeChrome { namespace SideTip {
// What one item's tip says: line 1 its title, line 2 its site's icon and address (none: the title alone).
struct Content {
    std::wstring title, address; std::shared_ptr<const FaviconImage> icon; bool local = false;
    bool operator==(const Content& o) const { return title == o.title && address == o.address && icon == o.icon && local == o.local; }
};

class Window {
    HWND hwnd_ = nullptr, owner_ = nullptr; std::wstring root_; std::unique_ptr<Chrome> chrome_; UINT dpi_ = 0;
    SideTip::Motion motion_; Gate gate_; Content current_, previous_;
    struct Pending { int item = 0; Box anchor; Content content; bool compact = false, panelLeft = true; } pending_;
    bool reduced_ = false, windowShown_ = false; Box target_{}; RECT placed_{}; int presents_ = 0;
    static constexpr UINT_PTR FrameTimer = 1, DelayTimer = 2;
    static constexpr float margin = 6;   // room around the box for its 4 DIP slide and its scale
    static double Ms(NativeChrome::Motion::Time t) { return std::chrono::duration<double, std::milli>(t.time_since_epoch()).count(); }
    bool Ready() {
        const UINT dpi = owner_ ? GetDpiForWindow(owner_) : 96;
        if (chrome_ && dpi == dpi_) return true;
        auto c = std::make_unique<Chrome>();
        if (FAILED(c->InitOffscreen(root_, dpi, Look::maxWidth + 2 * margin, Look::Height(true) + 2 * margin))) return false;
        if (chrome_) chrome_->Dispose();
        chrome_ = std::move(c); dpi_ = dpi; return true;
    }
    void DrawContent(Chrome& c, const Content& content, float alpha, float w) {
        if (alpha <= 0) return;
        auto* t = c.RenderTarget(); const bool layer = alpha < 1;
        if (layer) t->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), alpha), nullptr);
        const PopupKit::Type type{Look::text, Look::line};
        const float left = Look::border + Look::padX, right = w - Look::border - Look::padX, top = Look::border + Look::padY;
        PopupKit::DrawText(c, content.title, {left, top, right, top + Look::line}, type, PopupKit::Hex(Look::titleRgb));
        if (!content.address.empty()) {
            const float y = top + Look::line + Look::lineGap, iy = y + (Look::line - Look::icon) / 2;
            const D2D1_RECT_F icon{left, iy, left + Look::icon, iy + Look::icon};
            if (!c.DrawSiteImage(content.icon, icon)) c.Icon(content.local ? L"file" : L"favicon", icon, Look::icon, 1);
            PopupKit::DrawText(c, content.address, {left + Look::icon + Look::iconGap, y, right, y + Look::line}, type, PopupKit::Hex(Look::addressRgb));
        }
        if (layer) t->PopLayer();
    }
    // One frame into the offscreen surface, then onto the screen.
    bool Present(const SideTip::Frame& f) {
        if (!Ready()) return false;
        Chrome& c = *chrome_; auto* t = c.RenderTarget(); if (!t) return false;
        const float s = dpi_ / 96.f, w = f.box.Width(), h = f.box.Height();
        const UINT wpx = static_cast<UINT>(std::min<float>(std::ceil((w + 2 * margin) * s), std::ceil((Look::maxWidth + 2 * margin) * s)));
        const UINT hpx = static_cast<UINT>(std::min<float>(std::ceil((h + 2 * margin) * s), std::ceil((Look::Height(true) + 2 * margin) * s)));
        if (!wpx || !hpx) return false;
        t->BeginDraw(); t->SetTransform(D2D1::Matrix3x2F::Identity()); t->Clear(D2D1::ColorF(0, 0, 0, 0));
        // The slide out from the icon and the scale about the box's edge on the icon's side (vertically centred).
        t->SetTransform(D2D1::Matrix3x2F::Scale(f.scale, f.scale, D2D1::Point2F(f.panelLeft ? 0.f : w, h / 2)) * D2D1::Matrix3x2F::Translation(margin + f.dx, margin));
        PopupKit::Fill(c, {0, 0, w, h}, Look::radius, PopupKit::Hex(Look::surfaceRgb));
        PopupKit::Outline(c, w, h, Look::radius, PopupKit::Hex(Look::borderRgb));
        if (f.previous && f.cross < 1) DrawContent(c, previous_, 1 - f.cross, w);
        DrawContent(c, current_, f.cross, w);
        t->SetTransform(D2D1::Matrix3x2F::Identity());
        if (FAILED(t->EndDraw())) return false;
        std::vector<UINT32> px(static_cast<size_t>(wpx) * hpx);
        if (!c.CopySurface(0, 0, wpx, hpx, px.data())) return false;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = static_cast<LONG>(wpx); bi.bmiHeader.biHeight = -static_cast<LONG>(hpx); bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr), mem = CreateCompatibleDC(screen); void* bits = nullptr; HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); bool ok = false;
        if (bmp && bits) {
            memcpy(bits, px.data(), px.size() * 4);
            HGDIOBJ old = SelectObject(mem, bmp);
            POINT origin{0, 0}; ClientToScreen(owner_, &origin);
            POINT dst{origin.x + static_cast<LONG>(std::lround((f.box.left - margin) * s)), origin.y + static_cast<LONG>(std::lround((f.box.top - margin) * s))};
            SIZE size{static_cast<LONG>(wpx), static_cast<LONG>(hpx)}; POINT src{0, 0};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::lround(255 * std::clamp(f.opacity, 0.f, 1.f))), AC_SRC_ALPHA};
            ok = UpdateLayeredWindow(hwnd_, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA) != FALSE;
            if (ok) { placed_ = {dst.x, dst.y, dst.x + size.cx, dst.y + size.cy}; ++presents_; }
            SelectObject(mem, old);
        }
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem); ReleaseDC(nullptr, screen);
        if (ok && !windowShown_) { SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW); windowShown_ = true; }
        return ok;
    }
    void HideWindow() { if (hwnd_ && windowShown_) ShowWindow(hwnd_, SW_HIDE); windowShown_ = false; if (hwnd_) KillTimer(hwnd_, FrameTimer); }
    void Evaluate() {
        if (!hwnd_ || !pending_.item) return;
        const auto now = NativeChrome::Motion::Now(); const double ms = Ms(now);
        const double delay = pending_.compact ? Timing::compactDelayMs : Timing::delayMs;
        const int due = gate_.Hover(pending_.item, ms, delay, motion_.Shown());
        KillTimer(hwnd_, DelayTimer);
        if (!due) { const double left = gate_.Remaining(ms, delay); if (left >= 0) SetTimer(hwnd_, DelayTimer, static_cast<UINT>(std::max(1.0, std::ceil(left))), nullptr); return; }
        if (!Ready()) return;
        const Content& c = pending_.content; const bool address = !c.address.empty(); const PopupKit::Type type{Look::text, Look::line};
        const float tw = PopupKit::Measure(*chrome_, c.title, 8192, type, false).width, aw = address ? PopupKit::Measure(*chrome_, c.address, 8192, type, false).width : 0.f;
        RECT cl{}; GetClientRect(owner_, &cl); const float k = 96.f / std::max(1u, dpi_);
        target_ = Place(pending_.anchor, Look::Width(tw, aw, address), Look::Height(address), cl.right * k, cl.bottom * k, pending_.panelLeft);
        if (pending_.item != motion_.Item()) previous_ = current_;
        current_ = c;
        motion_.Show(pending_.item, target_, pending_.panelLeft, now, reduced_);
        Step();
    }
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER: if (self) { if (w == FrameTimer) self->Step(); else if (w == DelayTimer) self->Evaluate(); } return 0;
        case WM_NCDESTROY: if (self) { self->hwnd_ = nullptr; self->windowShown_ = false; } break;
        }
        return DefWindowProcW(h, msg, w, l);
    }
public:
    ~Window() { Dispose(); }
    bool Create(HINSTANCE instance, HWND owner, const std::wstring& root) {
        owner_ = owner; root_ = root;
        WNDCLASSW wc{}; wc.lpfnWndProc = Proc; wc.hInstance = instance; wc.lpszClassName = L"BrowserSideTip"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, instance, this);
        return hwnd_ != nullptr;
    }
    void Dispose() { if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); hwnd_ = nullptr; if (chrome_) { chrome_->Dispose(); chrome_.reset(); } }
    HWND Handle() const { return hwnd_; }
    void SetReducedMotion(bool reduced) { reduced_ = reduced; }
    // The pointer (or the keyboard focus) on a tab row or a favourite tile of the column: `item` (its region id), its
    // plate / row / tile `anchor` (window DIP), what its tip says. The compact column shows it at once; the complete one
    // after 500 ms of rest, at once while a tip is on screen. A gap between items (item 0) keeps the tip where it is.
    void Hover(int item, const Box& anchor, const Content& content, bool compact, bool panelLeft) {
        if (!hwnd_ || !item) return;
        const auto same = [](const Box& a, const Box& b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; };
        if (item == pending_.item && motion_.Shown() && motion_.Item() == item && content == pending_.content && same(anchor, pending_.anchor) && panelLeft == pending_.panelLeft) return;   // the pointer moving on the same item: nothing to measure or draw
        pending_ = {item, anchor, content, compact, panelLeft};
        Evaluate();
    }
    // The pointer left the column, or reached a control with its own tooltip: the tip fades (100 ms).
    void Leave() { gate_.Reset(); pending_.item = 0; if (hwnd_) KillTimer(hwnd_, DelayTimer); motion_.Hide(NativeChrome::Motion::Now()); Step(); }
    // A click, a menu opening: the tip fades and stays away until the pointer reaches another item.
    void Dismiss(int item = 0) { gate_.Dismiss(item); pending_.item = 0; if (hwnd_) KillTimer(hwnd_, DelayTimer); motion_.Hide(NativeChrome::Motion::Now()); Step(); }
    // At once, no fade (the layout changed under it, the window went away).
    void HideNow() { gate_.Reset(); pending_.item = 0; if (hwnd_) KillTimer(hwnd_, DelayTimer); motion_.Clear(); HideWindow(); }
    // One frame now; again on the timer while something moves.
    void Step() {
        if (!hwnd_) return;
        const auto now = NativeChrome::Motion::Now(); const auto f = motion_.At(now);
        if (!f.visible) { HideWindow(); return; }
        Present(f);
        if (motion_.Moving(now)) SetTimer(hwnd_, FrameTimer, 8, nullptr); else KillTimer(hwnd_, FrameTimer);
    }
    // ---- tests ----
    bool Shown() const { return motion_.Shown(); }
    bool WindowShown() const { return windowShown_; }
    bool Waiting() const { return pending_.item && !motion_.Shown(); }
    int Item() const { return motion_.Item(); }
    const SideTip::Motion& MotionForTest() const { return motion_; }
    Box Target() const { return target_; }
    RECT Placed() const { return placed_; }   // screen px of the last frame (the box plus its margin)
    int Presents() const { return presents_; }
    const Content& Current() const { return current_; }
    static constexpr float Margin() { return margin; }
    bool SaveForTest(const wchar_t* png) { return chrome_ && SUCCEEDED(chrome_->SavePng(png)); }
};

}}  // namespace NativeChrome::SideTip
