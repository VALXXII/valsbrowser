#pragma once
// Where a hover tooltip goes, for every tooltip of the browser (user request 2026-11-20):
// centred horizontally on its anchor whenever possible, `gap` px under it (above when the
// bottom is short of room), slid sideways only to stay `margin` px inside the allowed area
// (browser window ∩ monitor work area); an arrow, if a tip draws one, keeps pointing at the
// anchor's centre. One function, Place(), owns the arithmetic (tests/tooltip_placement_test.cpp);
// the native Tooltip::Tip and the comctl tooltips of the panels (OnNotify) both call it.
// All values are physical screen pixels: callers scale gap / margin with the DPI (Px()).
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>

namespace NativeChrome {
namespace TooltipPlacement {

inline constexpr float gapDip = 6;     // under the anchor (Paper EW9-0)
inline constexpr float marginDip = 8;  // closest a tip comes to the window / screen edge

struct Result {
    POINT at{};       // top-left corner of the tip, screen px
    int arrowX = 0;   // anchor centre, in px from the tip's left edge (where an arrow would point)
    bool above = false;
};

inline LONG FloorHalf(LONG v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

// `anchor` the hovered / focused element, `size` the tip, `bounds` the area it must stay in.
inline Result Place(RECT anchor, SIZE size, RECT bounds, int gap, int margin) {
    Result r;
    const LONG lo = bounds.left + margin, hi = bounds.right - margin - size.cx;
    LONG x = FloorHalf(anchor.left + anchor.right - size.cx);   // centred (half a px rounds left)
    x = std::min(x, hi); x = std::max(x, lo);                   // wider than the room: from the left margin
    LONG y = anchor.bottom + gap;
    if (y + size.cy > bounds.bottom - margin && anchor.top - gap - size.cy >= bounds.top + margin) { y = anchor.top - gap - size.cy; r.above = true; }
    y = std::max<LONG>(bounds.top + margin, std::min<LONG>(y, bounds.bottom - margin - size.cy));
    r.at = {x, y};
    r.arrowX = std::clamp<LONG>(FloorHalf(anchor.left + anchor.right) - x, 0, std::max<LONG>(0, size.cx));
    return r;
}

// Area a tip may use: the anchor's monitor work area, narrowed to `within` when given and when
// they overlap (an anchor on a second screen keeps that screen's area).
inline RECT Bounds(RECT work, const RECT* within) {
    if (!within) return work;
    RECT r{}; return IntersectRect(&r, &work, within) ? r : work;
}
inline int Px(int dpi, float dip) { return static_cast<int>(std::lround(dip * dpi / 96.f)); }

inline RECT WorkArea(RECT around) { MONITORINFO mi{}; mi.cbSize = sizeof(mi); if (GetMonitorInfoW(MonitorFromRect(&around, MONITOR_DEFAULTTONEAREST), &mi)) return mi.rcWork; return around; }
// The window as drawn: GetWindowRect also counts the invisible resize borders.
inline RECT VisibleRect(HWND h) {
    RECT r{}; if (!h) return r;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) || r.right <= r.left) GetWindowRect(h, &r);
    return r;
}

// comctl tooltips (TOOLTIPS_CLASS): call from the tool window's WM_NOTIFY. On TTN_SHOW it moves
// `tip` centred under the current tool, inside the browser window, and returns true (the window
// procedure then returns TRUE so comctl keeps that position).
inline bool OnNotify(HWND tip, LPARAM l) {
    const auto* n = reinterpret_cast<const NMHDR*>(l);
    if (!tip || !n || n->hwndFrom != tip || n->code != TTN_SHOW) return false;
    TOOLINFOW ti{}; ti.cbSize = sizeof(ti);
    if (!SendMessageW(tip, TTM_GETCURRENTTOOLW, 0, reinterpret_cast<LPARAM>(&ti)) || !ti.hwnd) return false;
    RECT anchor = ti.rect;
    if (ti.uFlags & TTF_IDISHWND) GetWindowRect(reinterpret_cast<HWND>(ti.uId), &anchor);
    else MapWindowPoints(ti.hwnd, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    RECT box{}; GetWindowRect(tip, &box);
    const int dpi = static_cast<int>(GetDpiForWindow(ti.hwnd));
    const RECT window = VisibleRect(GetAncestor(ti.hwnd, GA_ROOTOWNER));
    const RECT area = Bounds(WorkArea(anchor), window.right > window.left ? &window : nullptr);
    const Result r = Place(anchor, {box.right - box.left, box.bottom - box.top}, area, Px(dpi, gapDip), Px(dpi, marginDip));
    SetWindowPos(tip, nullptr, r.at.x, r.at.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

} // namespace TooltipPlacement
} // namespace NativeChrome
