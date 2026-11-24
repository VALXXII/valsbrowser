// Mode Focus drawers' antialiased top corners (src/drawer-corners.h; user request 2026-11-23): the coverage mask, the
// cut the keyed drawer's paint keys out (only the partly covered pixels), the layered corner windows (owned by the drawer,
// click-through, the trailing one fixed while it slides, the leading one at rest only, hidden with it) and a composite of the result over a white and a
// dark page at 100 / 125 / 150 / 175 % (evidence PNGs when an output folder is given). GDI and user32 only.
#include "../src/drawer-corners.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace DC = NativeChrome::DrawerCorners;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }

// Writes a 24-bit BMP (no PNG encoder here; the evidence step converts it).
static void SaveBmp(const std::wstring& path, int w, int h, const std::vector<unsigned>& px) {
    FILE* f = _wfopen(path.c_str(), L"wb"); if (!f) return;
    const int row = (w * 3 + 3) & ~3; BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + row * h;
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = -h; ih.biPlanes = 1; ih.biBitCount = 24;
    fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
    std::vector<unsigned char> line(row);
    for (int y = 0; y < h; ++y) { for (int x = 0; x < w; ++x) { const unsigned p = px[y * w + x]; line[x * 3] = p & 255; line[x * 3 + 1] = (p >> 8) & 255; line[x * 3 + 2] = (p >> 16) & 255; } fwrite(line.data(), 1, row, f); }
    fclose(f);
}

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring out = argc > 1 ? argv[1] : L"";
    // ---- the coverage mask ----
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f}) {
        const float r = 6 * scale; const auto m = DC::TopLeft(r);
        Require(m.size == (int)std::ceil(r) && m.coverage.size() == size_t(m.size * m.size), "mask: ceil(radius) square");
        Require(m.coverage[0] < .2f, "mask: the corner pixel is (almost) outside the arc");
        Require(m.coverage[(m.size - 1) * m.size + m.size - 1] == 1.f, "mask: the pixel diagonal to the arc's centre is covered");
        bool symmetric = true, monotonic = true;
        for (int y = 0; y < m.size; ++y) for (int x = 0; x < m.size; ++x) {
            if (std::fabs(m.coverage[y * m.size + x] - m.coverage[x * m.size + y]) > 1e-6f) symmetric = false;
            if (x + 1 < m.size && m.coverage[y * m.size + x + 1] + 1e-6f < m.coverage[y * m.size + x]) monotonic = false;
        }
        Require(symmetric && monotonic, "mask: symmetric about the diagonal, coverage grows inward");
        // Partial pixels exist (an antialiased edge, not a staircase) and the area matches the analytic corner.
        int partial = 0; double area = 0; for (float c : m.coverage) { if (c > 0 && c < 1) ++partial; area += 1 - c; }
        const double exact = r * r * (1 - 3.14159265358979 / 4); Require(partial >= m.size && std::fabs(area - exact) < .05 * exact + .1, "mask: partial pixels along the arc, the cut area = r^2 (1 - pi/4)");
    }
    {   const auto none = DC::TopLeft(0); Require(none.size == 0, "radius 0: no mask"); HRGN c = DC::Cut(none, 0, 100); RECT b{}; Require(GetRgnBox(c, &b) == NULLREGION, "radius 0: nothing cut"); DeleteObject(c); }
    // ---- the cut: exactly the pixels that are not fully covered, at both top corners ----
    {   const auto m = DC::TopLeft(7.5f); HRGN cut = DC::Cut(m, 10, 342);   // a 332 px column starting at x 10
        bool ok = true;
        for (int y = 0; y < m.size; ++y) for (int x = 0; x < m.size; ++x) {
            const bool partial = m.coverage[y * m.size + x] < 1.f;
            if (!!PtInRegion(cut, 10 + x, y) != partial || !!PtInRegion(cut, 341 - x, y) != partial) ok = false;
        }
        RECT box{}; GetRgnBox(cut, &box);
        Require(ok && box.left == 10 && box.right == 342 && box.top == 0 && box.bottom <= m.size, "cut: the partial pixels of both top corners, nothing else");
        Require(!PtInRegion(cut, 10 + m.size, 0) && !PtInRegion(cut, 100, 0) && !PtInRegion(cut, 10, m.size), "cut: the top edge and the side below the arc stay the drawer's");
        DeleteObject(cut);
        HRGN narrow = DC::Cut(m, 0, 10); RECT nb{}; Require(GetRgnBox(narrow, &nb) == NULLREGION, "a column narrower than two corners: no cut"); DeleteObject(narrow); }
    // ---- the layered corner windows over a real (hidden-owner) drawer ----
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"DrawerCornersTestOwner"; RegisterClassW(&wc);
    HWND drawer = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"drawer", WS_POPUP, -4000, 100, 332, 600, nullptr, nullptr, wc.hInstance, nullptr);
    Require(drawer != nullptr, "test drawer window");
    {   DC::Pair pair; const RECT window{-4000, 100, -4000 + 332, 700};
        pair.SetRadius(6 * 1.5f);
        HRGN cut = pair.CutRegion(0, 332); RECT b{};
        Require(GetRgnBox(cut, &b) != NULLREGION && b.left == 0 && b.right == 332 && b.top == 0, "CutRegion: the two top corners the paint keys out"); DeleteObject(cut);
        pair.Place(drawer, window, 0, 332, .11f, .09f, true);
        for (int i = 0; i < 2; ++i) {
            HWND c = pair.Window(i); Require(c && IsWindowVisible(c) && pair.Shown(i), "corner window shown");
            Require(GetWindow(c, GW_OWNER) == drawer, "owned by the drawer (always just above it)");
            const LONG_PTR ex = GetWindowLongPtrW(c, GWL_EXSTYLE); Require((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT) && (ex & WS_EX_NOACTIVATE), "layered, click-through, never activated");
            RECT r{}; GetWindowRect(c, &r); Require(r.right - r.left == 9 && r.bottom - r.top == 9 && r.top == 100 && (i == 0 ? r.left == -4000 : r.right == -4000 + 332), "on the drawer's top corners at rest (9 px at 150 %)");
        }
        // The keyed drawer slides (PR #117: its window never moves, the content is drawn `lead` px right of its place): the
        // trailing corner stays on the window's fixed right edge, the leading one steps away until rest (a moved window
        // would reach the screen a composition after the present), the keyed corners follow the content column.
        pair.HideLeading(); pair.Place(drawer, window, 132, 332, .11f, .09f, false);
        RECT tr{}; GetWindowRect(pair.Window(1), &tr);
        Require(!IsWindowVisible(pair.Window(0)) && IsWindowVisible(pair.Window(1)) && tr.right == -4000 + 332 && tr.top == 100, "sliding: only the trailing corner, never moved");
        HRGN slide = pair.CutRegion(132, 332); RECT sb{}; GetRgnBox(slide, &sb);
        Require(sb.left == 132 && sb.right == 332 && PtInRegion(slide, 132, 0) && !PtInRegion(slide, 131, 0), "sliding: the keyed corners follow the content's left edge"); DeleteObject(slide);
        pair.Place(drawer, window, 0, 332, .11f, .09f, true); RECT l{}; GetWindowRect(pair.Window(0), &l);
        Require(IsWindowVisible(pair.Window(0)) && l.left == -4000 && l.top == 100, "back at rest: the leading corner on the content's left edge");
        pair.Place(drawer, window, 320, 332, .11f, .09f, false); Require(!IsWindowVisible(pair.Window(0)) && !IsWindowVisible(pair.Window(1)), "a column narrower than its two corners: none");
        pair.Place(drawer, window, 0, 332, .11f, .09f, true);
        pair.Hide(); Require(!IsWindowVisible(pair.Window(0)) && !IsWindowVisible(pair.Window(1)), "hidden with the drawer");
        pair.SetRadius(0); HRGN off = pair.CutRegion(0, 332); RECT ob{};
        Require(GetRgnBox(off, &ob) == NULLREGION, "radius 0 (focus mode off): nothing keyed"); DeleteObject(off);
        pair.Place(drawer, window, 0, 332, .11f, .09f, true); Require(!IsWindowVisible(pair.Window(0)) && !IsWindowVisible(pair.Window(1)), "radius 0: nothing shown");
    }
    // ---- composite evidence: drawer (canvas #171717, 1 px #FFFFFF05 leading hairline) over a white and a dark page ----
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f}) for (unsigned page : {0xFFFFFFu, 0x2B2B2Bu}) {
        const auto m = DC::TopLeft(6 * scale); const int W = (int)std::lround(60 * scale), H = (int)std::lround(30 * scale), left = (int)std::lround(10 * scale), top = (int)std::lround(8 * scale), right = W - left;
        const float c = 23 / 255.f, edge = c + (1 - c) * .02f;
        std::vector<unsigned> px(W * H, page);
        HRGN cut = DC::Cut(m, left, right);
        for (int y = top; y < H; ++y) for (int x = left; x < right; ++x) {
            const int ly = y - top; const bool inCut = !!PtInRegion(cut, x, ly);
            const float g = x == left ? edge : c; const unsigned G = (unsigned)std::lround(g * 255);
            if (!inCut) px[y * W + x] = G | (G << 8) | (G << 16);   // the drawer's own pixels
            else {   // the corner window's pixel over the page: premultiplied colour at its coverage
                const int mx = x < W / 2 ? x - left : right - 1 - x; const float a = (ly < m.size && mx < m.size) ? m.coverage[ly * m.size + mx] : 0;
                auto blend = [&](unsigned p, int s) { const float b = ((p >> s) & 255) / 255.f; return (unsigned)std::lround((g * a + b * (1 - a)) * 255) << s; };
                px[y * W + x] = blend(page, 0) | blend(page, 8) | blend(page, 16);
            }
        }
        DeleteObject(cut);
        // The arc is antialiased: along the top-left arc at least one pixel per row in (page, drawer) strictly between.
        if (page == 0xFFFFFFu) {   // over white: along the arc every row of the corner has tones strictly between drawer and page
            int rows = 0;
            for (int y = top; y < top + m.size; ++y) for (int x = left; x < left + m.size; ++x) { const unsigned v = px[y * W + x] & 255; if (v > 40 && v < 250) { ++rows; break; } }
            Require(rows >= m.size - 2, "composite: antialiased arc (intermediate tones on the corner's rows)");
        }
        if (!out.empty()) SaveBmp(out + L"\\drawer-corner-" + std::to_wstring((int)std::lround(scale * 100)) + (page == 0xFFFFFFu ? L"-white" : L"-dark") + L".bmp", W, H, px);
    }
    DestroyWindow(drawer);
    std::printf("PASS drawer corners: %d checks (coverage mask, cut, layered corner windows, composite at 4 DPI)\n", checks);
    return 0;
}
