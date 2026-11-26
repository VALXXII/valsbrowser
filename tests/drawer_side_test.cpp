// #143 (owner 2026-11-25): the docked drawers open on the tab panel's side. Pure geometry of src/native-ui.h (the docked
// window rectangle, the slide's direction, the strip keyed out, the region and its rounded corner) and src/sidebar.h (the
// dock against the column and the page box) at 100 / 125 / 150 / 175 %, both sides, plus a mirrored (WS_EX_LAYOUTRTL)
// owner: the placement is in screen pixels from the owner's client rectangle, whatever the text direction. Never shows a
// window (the RTL owner is created hidden). GDI and user32 only.
#include "../src/native-ui.h"
#include "../src/sidebar.h"
#include <cstdio>
#include <string>

namespace S = NativeChrome::Sidebar;
static int checks = 0, failures = 0;
static void Check(bool ok, const std::string& what) { ++checks; if (!ok) { std::printf("FAIL %s\n", what.c_str()); ++failures; } }
static bool Near(float a, float b, float e = .01f) { return std::fabs(a - b) <= e; }
static int Px(float dip, float scale) { return static_cast<int>(std::lround(dip * scale)); }
static std::string Pct(float scale) { return std::to_string(static_cast<int>(std::lround(scale * 100))) + " %"; }

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // ---- the docked rectangle, both sides, every scale ----
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f}) {
        const RECT owner{1000, 500, 1000 + Px(1180, scale), 500 + Px(740, scale)};
        const int column = Px(268, scale), top = owner.top + Px(40, scale), height = owner.bottom - top;
        const RECT right = NativeUI::DockedDrawerRect(owner, column, top, height, false, 0, 0);
        Check(right.right == owner.right && right.right - right.left == column && right.top == top && right.bottom == owner.bottom, "top bar, " + Pct(scale) + ": flush with the window's right edge");
        const RECT inset = NativeUI::DockedDrawerRect(owner, column, top, height, false, 0, Px(4, scale));
        Check(inset.right == owner.right - Px(4, scale), "top bar Mode Focus, " + Pct(scale) + ": on the floating panel's right edge (4 in)");
        for (float panel : {248.f, 40.f, 249.f, 360.f}) {   // complete, compact, Focus (after the 1 DIP border), the widest
            const int dock = Px(panel, scale);
            const RECT left = NativeUI::DockedDrawerRect(owner, column, top, height, true, dock, 0);
            Check(left.left == owner.left + dock && left.right - left.left == column && left.top == top && left.bottom == owner.bottom,
                  "side layout " + std::to_string(static_cast<int>(panel)) + ", " + Pct(scale) + ": docked against the panel's right edge");
            Check(left.left >= owner.left && left.right <= owner.right, "side layout, " + Pct(scale) + ": inside the window");
        }
        // The slide: the content toward its entering edge, the strip it has not entered on that side.
        for (int lead : {0, 1, column / 3, column - 1, column}) {
            Check(NativeUI::DrawerSlideOffset(lead, false) == lead && NativeUI::DrawerSlideOffset(lead, true) == -lead, "slide offset mirrored (" + std::to_string(lead) + " px)");
            int from = 0, to = 0; NativeUI::DrawerContentColumn(lead, column, false, from, to);
            Check(from == lead && to == column, "docked right: the content covers [lead, width)");
            NativeUI::DrawerContentColumn(lead, column, true, from, to);
            Check(from == 0 && to == column - lead, "docked left: the content covers [0, width - lead), it enters from the left");
        }
    }
    // ---- the region: the rounded corner only on the window's own corner, mirrored ----
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f}) {
        const int w = Px(268, scale), h = Px(700, scale), r = Px(20, scale);
        HRGN right = NativeUI::DrawerRegion(true, w, h, r, false, false), left = NativeUI::DrawerRegion(true, w, h, r, false, true);
        bool mirror = true; int cutRight = 0, cutLeft = 0;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const bool a = PtInRegion(right, x, y) != FALSE, b = PtInRegion(left, w - 1 - x, y) != FALSE;
            if (a != b) mirror = false;
            if (!a) ++cutRight;
            if (!PtInRegion(left, x, y)) ++cutLeft;
        }
        Check(mirror, "region " + Pct(scale) + ": the left one is the right one mirrored, pixel for pixel");
        Check(cutRight > 0 && cutRight == cutLeft && !PtInRegion(left, 0, h - 1) && PtInRegion(left, w - 1, h - 1) && PtInRegion(left, 0, 0) && PtInRegion(left, w - 1, 0),
              "region " + Pct(scale) + ": docked left, only the bottom-LEFT corner is rounded (" + std::to_string(cutLeft) + " px cut)");
        HRGN square = NativeUI::DrawerRegion(true, w, h, r, true, true); RECT box{}; GetRgnBox(square, &box);
        int squareCut = 0; for (int y = h - r; y < h; ++y) for (int x = 0; x < r; ++x) if (!PtInRegion(square, x, y)) ++squareCut;
        Check(squareCut == 0 && box.left == 0 && box.top == 0 && box.right == w && box.bottom == h, "region " + Pct(scale) + ": square, all four corners (against the panel)");
        HRGN none = NativeUI::DrawerRegion(false, w, h, r, false, true); GetRgnBox(none, &box);
        Check(IsRectEmpty(&box), "region: empty while nothing has entered");
        DeleteObject(right); DeleteObject(left); DeleteObject(square); DeleteObject(none);
    }
    // Q1 (Designer 2026-11-25): round only where the drawer sits in a rounded window corner.
    Check(NativeUI::DrawerOnWindowCorner(false, 0, 0, false), "right edge of the window: its corner");
    Check(!NativeUI::DrawerOnWindowCorner(false, 0, 5, false), "4 DIP in (Mode Focus): no corner");
    Check(!NativeUI::DrawerOnWindowCorner(true, 310, 0, false), "against the panel: no window corner, all four square");
    Check(NativeUI::DrawerOnWindowCorner(true, 0, 0, false), "on the window's own left edge: its corner");
    Check(NativeUI::DrawerOnWindowCorner(true, 310, 0, true), "Q1 (b) switch: rounded against the panel too");
    // ---- the side layout: the dock and the page box (complete, compact, resized, Focus), both sides ----
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f}) {
        (void)scale;   // the side layout is in DIP: the same at every scale (the drawers round it to pixels above)
        S::Input in; in.width = 1180; in.height = 740; in.tabs = 5; in.tools = 4;
        for (auto mode : {S::Mode::Complete, S::Mode::Compact}) for (float reserve : {268.f, 320.f, 332.f}) {
            in.mode = mode; in.reserve = reserve; in.reserveLeft = true; in.hidden = false; in.top = 0;
            auto L = S::Compute(in); const float col = mode == S::Mode::Complete ? 248.f : 40.f;
            Check(Near(L.drawerDock, col) && Near(L.page.left, col + reserve + 4) && Near(L.page.right, 1176), "docked " + std::string(mode == S::Mode::Complete ? "complete" : "compact") + " " + std::to_string((int)reserve) + ": the drawer against the column, the page 4 after it");
            in.reserveLeft = false; L = S::Compute(in);
            Check(Near(L.page.left, col) && Near(L.page.right, 1180 - reserve - 4), "a panel on the right: the drawer on the right, the page 4 before it");
            in.reserveLeft = true; in.hidden = true; L = S::Compute(in);
            Check(Near(L.drawerDock, col + 1) && Near(L.page.left, col + 1 + reserve + 4), "Focus: the drawer just after the shown column's 1 DIP border (Q3), the page 4 after it");
        }
        in.hidden = false; in.mode = S::Mode::Complete; in.reserve = 0; auto L = S::Compute(in);
        Check(Near(L.page.left, 248) && Near(L.page.right, 1176), "no drawer: the page against the column as before");
    }
    // ---- a mirrored (right-to-left) owner: the client rectangle in screen pixels stays well formed, the drawer inside it ----
    {
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DrawerSideRtlOwner"; RegisterClassW(&wc);
        for (DWORD ex : {0ul, static_cast<DWORD>(WS_EX_LAYOUTRTL)}) {
            HWND owner = CreateWindowExW(ex, wc.lpszClassName, L"", WS_POPUP, 300, 200, 1180, 740, nullptr, nullptr, wc.hInstance, nullptr);   // never shown
            RECT client{}; GetClientRect(owner, &client); MapWindowPoints(owner, nullptr, reinterpret_cast<POINT*>(&client), 2);   // as the drawers' OwnerClient
            RECT win{}; GetWindowRect(owner, &win);
            const RECT d = NativeUI::DockedDrawerRect(client, 268, client.top + 40, client.bottom - client.top - 40, true, 248, 0);
            const std::string kind = ex ? "right-to-left owner" : "left-to-right owner";
            Check(client.left < client.right && client.left == win.left && client.right == win.right, kind + ": its client rectangle in screen pixels, well formed");
            Check(d.left == client.left + 248 && d.right == client.left + 516, kind + ": the drawer from the panel's edge, never from the text direction");
            DestroyWindow(owner);
        }
    }
    std::printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
