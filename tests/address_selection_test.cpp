// Address field selection in the profile colour (native-ui.h EditSelection): a real single-line
// EDIT built like the browser's (main.cpp WM_CREATE / WM_CTLCOLOREDIT), driven by window messages
// only (thread keyboard state, no SendInput, no clipboard), rendered pixels read back from the
// field. Each case checks, on what Windows actually drew:
//   - no pixel of the system highlight (COLOR_HIGHLIGHT) anywhere in the field;
//   - the selected run's cell is the profile fill, the brightest glyph pixel in it is >= 4.5:1
//     against that fill (measured, not computed from constants);
//   - outside the run: no fill pixel; selection range and text unchanged by the painting.
// Usage: address_selection_test.exe <outDir> (BMP evidence: native vs profile highlight).
#define UNICODE
#define _UNICODE
#include "../src/native-ui.h"
#include "../src/design-system.h"
#include "../src/profile-color.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace D = NativeChrome::Design;
static int failures = 0;
static void Check(bool ok, const std::string& label) { if (!ok) { std::printf("FAIL %s\n", label.c_str()); ++failures; } }

static NativeUI::EditSelection selection;
static COLORREF fillNow = RGB(0, 0, 0);
static bool forcedColors = false;
static NativeUI::EditSelection::Colors Colors() { return {fillNow, RGB(255, 255, 255)}; }
static bool Forced() { return forcedColors; }
static NativeUI::AddressBackground background;
static HWND parent, edit;
static HFONT font;
static int dpi = 96;
static int S(float v) { return static_cast<int>(std::lround(v * dpi / 96.f)); }
static const COLORREF field = D::NativeGray(D::Surface::raised);
static const COLORREF ink = D::NativeGray(D::Surface::raised + (1 - D::Surface::raised) * D::Text::address);

static LRESULT CALLBACK Parent(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CTLCOLOREDIT) {   // as main.cpp
        HDC dc = reinterpret_cast<HDC>(w); RECT r; GetClientRect(edit, &r);
        SetBkMode(dc, TRANSPARENT); SetBkColor(dc, field); SetTextColor(dc, ink); SetBrushOrgEx(dc, 0, 0, nullptr);
        selection.Clip(reinterpret_cast<HWND>(l), dc);
        return reinterpret_cast<LRESULT>(background.Brush(r.right, 0, field, field));
    }
    return DefWindowProcW(h, msg, w, l);
}

static ProfileColor::Rgb8 Rgb8(COLORREF c) { return {GetRValue(c), GetGValue(c), GetBValue(c)}; }
static void Pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

struct Raster { int w = 0, h = 0; std::vector<COLORREF> px; COLORREF At(int x, int y) const { return px[y * w + x]; } };
// What is on the field's surface now (the redirection surface DWM composes), caret hidden.
static Raster Grab() {
    RECT r; GetClientRect(edit, &r); Raster out; out.w = r.right; out.h = r.bottom;
    HideCaret(edit);
    HDC src = GetDC(edit), mem = CreateCompatibleDC(src);
    BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = out.w; bi.bmiHeader.biHeight = -out.h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(src, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp); BitBlt(mem, 0, 0, out.w, out.h, src, 0, 0, SRCCOPY); GdiFlush();
    out.px.resize(static_cast<size_t>(out.w) * out.h);
    for (int i = 0; i < out.w * out.h; ++i) { const DWORD v = static_cast<DWORD*>(bits)[i]; out.px[i] = RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); ReleaseDC(edit, src);
    ShowCaret(edit);
    return out;
}
static void SaveBmp(const Raster& r, const std::wstring& path) {
    const int stride = r.w * 3 + (4 - r.w * 3 % 4) % 4; const DWORD size = stride * r.h;
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{}; fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + size;
    ih.biSize = sizeof(ih); ih.biWidth = r.w; ih.biHeight = r.h; ih.biPlanes = 1; ih.biBitCount = 24; ih.biSizeImage = size;
    std::vector<unsigned char> data(size);
    for (int y = 0; y < r.h; ++y) for (int x = 0; x < r.w; ++x) { const COLORREF c = r.At(x, r.h - 1 - y); unsigned char* p = &data[y * stride + x * 3]; p[0] = GetBValue(c); p[1] = GetGValue(c); p[2] = GetRValue(c); }
    FILE* f = _wfopen(path.c_str(), L"wb"); if (!f) return; fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f); fwrite(data.data(), 1, size, f); fclose(f);
}

static std::wstring Text() { const int n = GetWindowTextLengthW(edit); std::wstring s(n + 1, 0); GetWindowTextW(edit, s.data(), n + 1); s.resize(n); return s; }
static void Sel(DWORD& a, DWORD& b) { SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b)); }
// Oracle: the EDIT's OWN rendering of the same state (forced colours = the native highlight, the
// helper inactive), repainted through WM_PAINT. Nothing about the geometry is computed here: the
// native image says which pixels are the highlight (cell = exact COLOR_HIGHLIGHT, glyphs = blends
// toward COLOR_HIGHLIGHTTEXT), wherever bidi, fallback fonts or scrolling put them.
static Raster Native() {
    forcedColors = true; InvalidateRect(edit, nullptr, TRUE); UpdateWindow(edit); Pump();
    const Raster n = Grab();
    forcedColors = false; InvalidateRect(edit, nullptr, TRUE); UpdateWindow(edit); Pump();
    return n;
}
// Coverage of COLOR_HIGHLIGHTTEXT over COLOR_HIGHLIGHT in a native pixel, -1 if it is not such a blend.
static double HighlightCoverage(COLORREF c) {
    const COLORREF H = GetSysColor(COLOR_HIGHLIGHT), T = GetSysColor(COLOR_HIGHLIGHTTEXT);
    const int h[3] = {GetRValue(H), GetGValue(H), GetBValue(H)}, t[3] = {GetRValue(T), GetGValue(T), GetBValue(T)}, p[3] = {GetRValue(c), GetGValue(c), GetBValue(c)};
    int k = 0; for (int i = 1; i < 3; ++i) if (std::abs(t[i] - h[i]) > std::abs(t[k] - h[k])) k = i;
    const double a = double(p[k] - h[k]) / (t[k] - h[k]);
    if (a < -.01 || a > 1.01) return -1;
    for (int i = 0; i < 3; ++i) if (std::abs(h[i] + a * (t[i] - h[i]) - p[i]) > 2.5) return -1;
    return a;
}
static int Diff(COLORREF a, COLORREF b) { return (std::max)({std::abs(GetRValue(a) - GetRValue(b)), std::abs(GetGValue(a) - GetGValue(b)), std::abs(GetBValue(a) - GetBValue(b))}); }

struct Measure { int cellPx = 0, cellWrong = 0, highlightPx = 0, changedOutside = 0, glyphPx = 0, glyphWrong = 0, unselectedInk = 0; double glyphContrast = 0; RECT box{9999, 9999, -1, -1}; };
static Measure Analyse(const Raster& ours, const Raster& native, COLORREF fill) {
    Measure m; const COLORREF H = GetSysColor(COLOR_HIGHLIGHT);
    // Columns holding the native cell colour: the selected run(s) as the EDIT drew them.
    std::vector<bool> col(native.w, false);
    for (int x = 0; x < native.w; ++x) for (int y = 0; y < native.h; ++y) if (native.At(x, y) == H) { col[x] = true; break; }
    double best = 0;
    for (int y = 0; y < ours.h; ++y) for (int x = 0; x < ours.w; ++x) {
        const COLORREF o = ours.At(x, y), n = native.At(x, y);
        if (o == H) { ++m.highlightPx; m.box = {(std::min)(m.box.left, (LONG)x), (std::min)(m.box.top, (LONG)y), (std::max)(m.box.right, (LONG)x), (std::max)(m.box.bottom, (LONG)y)}; }
        const double a = col[x] ? HighlightCoverage(n) : -1;
        if (n == H) { ++m.cellPx; if (o != fill) ++m.cellWrong; continue; }
        if (a >= 0) {   // a glyph pixel inside the native highlight: same coverage, our colours (white text)
            ++m.glyphPx;
            const int e[3] = {int(std::lround(GetRValue(fill) + a * (255 - GetRValue(fill)))), int(std::lround(GetGValue(fill) + a * (255 - GetGValue(fill)))), int(std::lround(GetBValue(fill) + a * (255 - GetBValue(fill))))};
            if (Diff(o, RGB(e[0], e[1], e[2])) > 3) ++m.glyphWrong;
            best = (std::max)(best, ProfileColor::Contrast(Rgb8(o), Rgb8(fill)));
            continue;
        }
        if (Diff(o, n) > 1) ++m.changedOutside;   // everything else is the EDIT's own pixel (GDI rounding: 1)
        else if (o != field) ++m.unselectedInk;
    }
    m.glyphContrast = best; return m;
}

static std::wstring outDir;
static void Expect(const std::string& label, bool selected = true, const wchar_t* save = nullptr) {
    Pump();
    DWORD a0, b0; Sel(a0, b0); const std::wstring before = Text();
    const Raster r = Grab(); if (save && !outDir.empty()) SaveBmp(r, outDir + L"/" + save);
    DWORD a1, b1; Sel(a1, b1);
    Check(a0 == a1 && b0 == b1 && before == Text(), label + ": painting leaves selection and text unchanged");
    const Raster n = Native();
    DWORD a2, b2; Sel(a2, b2); Check(a2 == a1 && b2 == b1, label + ": native reference keeps the selection");
    const Measure m = Analyse(r, n, fillNow);
    auto box = [&] { return " in x " + std::to_string(m.box.left) + ".." + std::to_string(m.box.right) + " y " + std::to_string(m.box.top) + ".." + std::to_string(m.box.bottom); };
    Check(m.highlightPx == 0, label + ": no system-highlight pixel (" + std::to_string(m.highlightPx) + box() + ")");
    Check(m.changedOutside == 0, label + ": nothing outside the native highlight changes (" + std::to_string(m.changedOutside) + ")");
    Check(m.cellWrong == 0, label + ": every native highlight cell pixel is the profile fill (" + std::to_string(m.cellWrong) + "/" + std::to_string(m.cellPx) + " wrong)");
    Check(m.glyphWrong == 0, label + ": selected glyphs keep the native coverage in white (" + std::to_string(m.glyphWrong) + "/" + std::to_string(m.glyphPx) + " wrong)");
    if (selected && a1 != b1) {
        Check(m.cellPx > 0, label + ": the native field shows a highlight to follow");
        Check(m.glyphPx == 0 || m.glyphContrast >= 4.5, label + ": rendered selected text >= 4.5:1 on the fill (" + std::to_string(m.glyphContrast) + ")");
        const std::wstring hex = ProfileColor::Hex(Rgb8(fillNow)); const std::string hex8(hex.begin(), hex.end());
        std::printf("  %-52s sel %lu-%lu  fill %s  cell %d px  glyph %d px  text/fill %.2f:1\n", label.c_str(), static_cast<unsigned long>(a1), static_cast<unsigned long>(b1), hex8.c_str(), m.cellPx, m.glyphPx, m.glyphContrast);
    } else Check(m.cellPx == 0, label + ": no native highlight expected");
}
static void Keys(bool ctrl, bool shift) { BYTE k[256]; GetKeyboardState(k); k[VK_CONTROL] = k[VK_LCONTROL] = ctrl ? 0x80 : 0; k[VK_SHIFT] = k[VK_LSHIFT] = shift ? 0x80 : 0; SetKeyboardState(k); }
static void Key(WORD vk, bool ctrl = false, bool shift = false) { Keys(ctrl, shift); SendMessageW(edit, WM_KEYDOWN, vk, 1); SendMessageW(edit, WM_KEYUP, vk, 0xC0000001); Keys(false, false); }
// Through the browser's own role: Accent::SelectionCurrent() is what SetAccents / the fade write, Selection() what main.cpp paints.
static COLORREF Ref(ProfileColor::Rgb8 c) { D::Accent::SelectionCurrent() = {c.r / 255.f, c.g / 255.f, c.b / 255.f}; const D::Rgb f = D::Accent::Selection(); return RGB(std::lround(f.r * 255), std::lround(f.g * 255), std::lround(f.b * 255)); }

static void Build(int newDpi) {
    if (parent) DestroyWindow(parent);
    if (font) DeleteObject(font);
    dpi = newDpi;
    // On screen (GDI clips direct draws to the visible region: an off-monitor field never receives them), topmost,
    // never activated: the user's foreground window and keyboard focus are not touched (SetFocus is per thread).
    parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, L"AddressSelectionTest", L"", WS_POPUP, 8, 8, S(420), S(40), nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, S(32), S(4), S(360), S(32), parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    SendMessageW(edit, EM_SETLIMITTEXT, 8192, 0); SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    font = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Inter Display");
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    Check(selection.Attach(edit, Colors, Forced), "attach");
    SetWindowTextW(edit, L"https://www.wikipedia.org/wiki/Accueil");
    SetFocus(edit); Pump(); UpdateWindow(parent); Pump();
    Check(GetFocus() == edit, "field focused");
}

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc > 1) { int n = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0); outDir.resize(n); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, outDir.data(), n); outDir.resize(n - 1); }
    WNDCLASSW wc{}; wc.lpfnWndProc = Parent; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"AddressSelectionTest"; RegisterClassW(&wc);
    std::printf("system highlight #%06lX (left untouched)\n", static_cast<unsigned long>(GetSysColor(COLOR_HIGHLIGHT)));
    const DWORD sysBefore = GetSysColor(COLOR_HIGHLIGHT);

    // Profile fills, as the browser derives them (profile-editor.h FillOf -> Accent::Selection()).
    struct Case { const char* name; ProfileColor::Rgb8 fill; };
    const Case cases[] = {
        {"teal (screenshot, hue 190 / .12)", ProfileColor::AccentFill(190, .12f)},
        {"warm orange (hue 50 / .16)", ProfileColor::AccentFill(50, .16f)},
        {"neutral Personnel", ProfileColor::NeutralFill()},
        {"extreme yellow (hue 100 / max)", ProfileColor::AccentFill(100, ProfileColor::MaxChroma)},
        {"faint violet (hue 300 / .02)", ProfileColor::AccentFill(300, .02f)},
    };

    for (int d : {96, 144, 192}) {
        Build(d);
        const std::string at = " @" + std::to_string(d) + "dpi";
        for (const auto& c : cases) {
            fillNow = Ref(c.fill);
            selection.Refresh(); Pump();
            SendMessageW(edit, EM_SETSEL, 0, -1);   // FocusAddress / Ctrl+L / Ctrl+A
            const bool shoot = d == 96 && c.fill == ProfileColor::AccentFill(190, .12f);
            Expect(std::string(c.name) + " select all" + at, true, shoot ? L"after-full-teal-96.bmp" : nullptr);
            SendMessageW(edit, EM_SETSEL, 8, 17);   // partial
            Expect(std::string(c.name) + " partial" + at, true, shoot ? L"after-partial-teal-96.bmp" : nullptr);
        }
        fillNow = Ref(ProfileColor::AccentFill(190, .12f)); selection.Refresh();
        // Repaint through WM_PAINT (DefWindowProc path), not only direct draws.
        SendMessageW(edit, EM_SETSEL, 4, 20); InvalidateRect(edit, nullptr, TRUE); UpdateWindow(edit); Expect("WM_PAINT repaint" + at);
        // Keyboard: Shift+arrows, Shift+End, Home, Shift+Ctrl+Left (word).
        SendMessageW(edit, EM_SETSEL, 5, 5); Key(VK_RIGHT, false, true); Key(VK_RIGHT, false, true);
        { DWORD a, b; Sel(a, b); Check(a == 5 && b == 7, "Shift+Right extends natively" + at); } Expect("Shift+Right x2" + at);
        Key(VK_END, false, true); Expect("Shift+End" + at);
        Key(VK_LEFT, true, true); Expect("Ctrl+Shift+Left" + at);
        Key(VK_HOME); { DWORD a, b; Sel(a, b); Check(a == 0 && b == 0, "Home collapses natively" + at); } Expect("Home (no selection)" + at, false);
        // Mouse drag and double-click.
        RECT cr; GetClientRect(edit, &cr); const int y = cr.bottom / 2;
        SendMessageW(edit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(S(10), y)); SendMessageW(edit, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(S(120), y)); SendMessageW(edit, WM_LBUTTONUP, 0, MAKELPARAM(S(120), y));
        { DWORD a, b; Sel(a, b); Check(b > a, "mouse drag selects natively" + at); } Expect("mouse drag" + at);
        SendMessageW(edit, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(S(150), y)); SendMessageW(edit, WM_LBUTTONUP, 0, MAKELPARAM(S(150), y)); Expect("double-click word" + at);
        // Editing: typing replaces, delete + undo, no clipboard.
        SendMessageW(edit, EM_SETSEL, 8, 11); SendMessageW(edit, WM_CHAR, L'x', 1); Check(Text() == L"https://x.wikipedia.org/wiki/Accueil", "typing replaces the selection" + at); Expect("typed over selection" + at, false);
        SendMessageW(edit, WM_UNDO, 0, 0); Expect("undo" + at);
        SendMessageW(edit, EM_SETSEL, 0, 8); SendMessageW(edit, WM_CLEAR, 0, 0); Expect("delete selection" + at, false);
        SendMessageW(edit, WM_UNDO, 0, 0); Expect("undo delete" + at);
        // Long URL, scrolled to its end, and Unicode.
        std::wstring longUrl = L"https://exemple.fr/café/日本語/مرحبا/"; while (longUrl.size() < 400) longUrl += L"segment-de-chemin/";
        SetWindowTextW(edit, longUrl.c_str()); const int n = static_cast<int>(longUrl.size());
        SendMessageW(edit, EM_SETSEL, n - 12, n); SendMessageW(edit, EM_SCROLLCARET, 0, 0); Expect("long URL scrolled, end selected" + at, true, d == 96 ? L"after-long-scrolled-96.bmp" : nullptr);
        SendMessageW(edit, EM_SETSEL, 0, -1); Key(VK_LEFT, false, true); Expect("long URL select all then Shift+Left" + at);
        SendMessageW(edit, EM_SETSEL, 18, 24); SendMessageW(edit, EM_SCROLLCARET, 0, 0); Expect("Unicode run" + at);
        // Independent review counterexamples (2026-11-15), verbatim: an Arabic IDN URL (the logical run is
        // reordered visually by the EDIT) and a valid maximum-length address (EM_POSFROMCHAR wraps at 16 bits).
        SetWindowTextW(edit, L"https://مثال.إختبار/مسار");
        SendMessageW(edit, EM_SETSEL, 8, 12); Expect("Arabic IDN domain selection" + at, true, d == 96 ? L"after-arabic-idn-96.bmp" : nullptr);
        for (auto r : {std::pair<int, int>{6, 10}, {9, 14}, {13, 19}, {0, 23}}) { SendMessageW(edit, EM_SETSEL, r.first, r.second); Expect("Arabic IDN " + std::to_string(r.first) + "-" + std::to_string(r.second) + at); }
        Key(VK_HOME); Key(VK_RIGHT, false, true); for (int i = 0; i < 10; ++i) Key(VK_RIGHT, false, true); Expect("Arabic IDN Shift+Right x11 across the bidi boundary" + at);
        std::wstring maxUrl = L"https://example.com/" + std::wstring(7900, L'W');
        SetWindowTextW(edit, maxUrl.c_str()); const int maxN = int(maxUrl.size());
        SendMessageW(edit, EM_SETSEL, maxN - 5, maxN); SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        Expect("7919-char URL all selected after end scroll" + at, true, d == 96 ? L"after-max-url-96.bmp" : nullptr);
        Key(VK_LEFT, false, true); Key(VK_LEFT, true, true); Expect("7919-char URL Shift+Left, Ctrl+Shift+Left" + at);
        SendMessageW(edit, EM_SETSEL, maxN - 40, maxN - 3); Expect("7919-char URL partial near the end" + at);
        // Mixed scripts: Latin, accented, CJK, Arabic and Hebrew runs in one URL.
        SetWindowTextW(edit, L"https://exemple.fr/café/日本語/مرحبا/x?q=שלום&n=12");
        for (auto r : {std::pair<int, int>{18, 24}, {27, 33}, {30, 40}, {36, 44}}) { SendMessageW(edit, EM_SETSEL, r.first, r.second); Expect("mixed bidi " + std::to_string(r.first) + "-" + std::to_string(r.second) + at, true, d == 96 && r.first == 30 ? L"after-mixed-bidi-96.bmp" : nullptr); }
        // Live colour change (profile switch / fade step): the run repaints in the new colour, no stale old fill.
        SetWindowTextW(edit, L"https://www.wikipedia.org/wiki/Accueil"); SendMessageW(edit, EM_SETSEL, 0, -1); Pump();
        fillNow = Ref(ProfileColor::AccentFill(50, .16f)); selection.Refresh(); Expect("live profile switch teal -> orange" + at);
        { const COLORREF old = Ref(ProfileColor::AccentFill(190, .12f)); const Raster r = Grab(); int stale = 0; for (COLORREF c : r.px) stale += c == old; Check(stale == 0, "no stale previous fill after switch" + at); }
        for (float t : {.25f, .5f, .75f}) {   // steps of the 120 ms fade between two fills (sRGB mix, clamped by Accent::Selection)
            const auto a = ProfileColor::AccentFill(190, .12f), b = ProfileColor::NeutralFill();
            const D::Rgb m = D::Accent::Mix({a.r / 255.f, a.g / 255.f, a.b / 255.f}, {b.r / 255.f, b.g / 255.f, b.b / 255.f}, t);
            fillNow = Ref({static_cast<std::uint8_t>(std::lround(m.r * 255)), static_cast<std::uint8_t>(std::lround(m.g * 255)), static_cast<std::uint8_t>(std::lround(m.b * 255))});
            selection.Refresh(); Expect("fade step " + std::to_string(t) + at);
        }
        // Focus loss: the EDIT hides its selection; nothing of ours remains.
        SetFocus(parent); Pump(); { const Raster r = Grab(); int fill = 0, hi = 0; for (COLORREF c : r.px) { fill += c == fillNow; hi += c == GetSysColor(COLOR_HIGHLIGHT); } Check(fill == 0 && hi == 0, "focus lost: no highlight of any kind" + at); }
        SetFocus(edit); SendMessageW(edit, EM_SETSEL, 0, -1); Expect("focus regained, select all" + at);
        // Forced colours (high contrast): native system highlight kept, nothing of ours.
        forcedColors = true; SendMessageW(edit, EM_SETSEL, 0, 0); SendMessageW(edit, EM_SETSEL, 0, -1); Pump();
        { const Raster r = Grab(); if (d == 96 && !outDir.empty()) SaveBmp(r, outDir + L"/before-native-highlight-96.bmp"); int fill = 0, hi = 0; for (COLORREF c : r.px) { fill += c == fillNow; hi += c == GetSysColor(COLOR_HIGHLIGHT); } Check(fill == 0 && hi > 0, "forced colours: native highlight, no profile fill" + at); }
        forcedColors = false; SendMessageW(edit, EM_SETSEL, 0, 0); SendMessageW(edit, EM_SETSEL, 0, -1); Expect("forced colours off again" + at);
    }
    Check(GetSysColor(COLOR_HIGHLIGHT) == sysBefore, "system highlight colour untouched");
    // Two independent fields (each window is its own process in the browser; here two objects in one process):
    // each paints only its own colour.
    {
        Build(96); HWND first = edit; fillNow = Ref(ProfileColor::AccentFill(190, .12f)); SendMessageW(first, EM_SETSEL, 0, -1); Expect("window A teal");
        static NativeUI::EditSelection other; HWND second = CreateWindowExW(0, L"EDIT", L"https://example.com", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 200, 30, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        other.Attach(second, [] { return NativeUI::EditSelection::Colors{RGB(0x74, 0x74, 0x74), RGB(255, 255, 255)}; }, Forced);
        SetFocus(second); SendMessageW(second, EM_SETSEL, 0, -1); Pump();
        Check(!selection.Active() && other.Active(), "only the focused field paints a selection");
        DestroyWindow(second); SetFocus(first);
    }
    DestroyWindow(parent);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("PASS address selection follows the profile colour: full/partial, keyboard, mouse, edit/undo, long/Unicode, live switch, fade, focus, forced colours, 96/144/192 DPI");
    return 0;
}
