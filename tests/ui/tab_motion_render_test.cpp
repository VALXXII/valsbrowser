// Native renderer checks for tab open/close motion: presented geometry, hit regions,
// closing ghost, DPI and reduced motion. Offscreen Direct2D only; no WebView2.
#include "../../src/chrome.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace NativeChrome;
static int checks = 0;
static void Require(bool ok, const char* what) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); } }
static Motion::Time T(int ms) { return Motion::Time{} + std::chrono::milliseconds(1000+ms); }
static const Region* Find(const Chrome& c, int id) { for (auto& r : c.regions) if (r.id == id) return &r; return nullptr; }
static bool Same(const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return std::fabs(a.left-b.left) < 1e-3f && std::fabs(a.right-b.right) < 1e-3f && a.top == b.top && a.bottom == b.bottom; }
static int Gray(UINT32 p) { return p & 255; }
// Settled presented regions must equal the static (unanimated) layout exactly.
static void RequireStatic(Chrome& animated, int count, int selected, const std::wstring& root, UINT dpi, const char* what) {
    Chrome plain; Require(SUCCEEDED(plain.InitOffscreen(root, dpi, 1180, 84)), "static renderer");
    plain.Build(count, selected);
    Require(plain.regions.size() == animated.regions.size(), what);
    for (size_t i = 0; i < plain.regions.size(); ++i) Require(plain.regions[i].id == animated.regions[i].id && Same(plain.regions[i].rect, animated.regions[i].rect), what);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    const std::wstring root = argv[1], out = argv[2];
    Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    std::vector<std::wstring> two{L"Google", L"Wikipédia"}, three{L"Google", L"Wikipédia", L"Nouvel onglet"};
    for (UINT dpi : {96u, 144u}) {
        Chrome chrome; Require(SUCCEEDED(chrome.InitOffscreen(root, dpi, 1180, 84)), "offscreen renderer");
        Motion::State motion(false); motion.EnablePolish(); chrome.animateSelection = true; chrome.SetMotion(&motion);
        chrome.tabKeys = {11, 12}; motion.SelectTab(12, T(0));
        chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(0));
        Require(!chrome.TakeTabMotionStarted() && !chrome.tabMotion.NextWake(T(0)).delay, "initial strip has no entrance");
        RequireStatic(chrome, 2, 1, root, dpi, "initial presented regions equal the static layout");
        const float plusBefore = Find(chrome, 'T')->rect.left;

        // Open: the functional change (new key, selection) is immediate; presentation glides.
        chrome.tabKeys = {11, 12, 13}; motion.SelectTab(13, T(10));
        chrome.Build(3, 2, &three, T(10));
        Require(chrome.TakeTabMotionStarted() && chrome.tabMotion.NextWake(T(10)).delay, "opening requests frames");
        Require(Find(chrome, 'T')->rect.left == plusBefore, "new-tab button does not jump on open");
        Require(!Find(chrome, CloseBase+2), "a collapsed tab exposes no close target");
        float lastPlus = plusBefore; bool monotonic = true, hits = true;
        for (int ms = 10, frame = 0; ms <= 420; ms += 30, ++frame) {
            chrome.Paint(three, 2, L"", false, 0, 0, nullptr, T(ms));
            const auto* plus = Find(chrome, 'T'); monotonic &= plus->rect.left >= lastPlus - 1e-3f; lastPlus = plus->rect.left;
            hits &= chrome.Hit((plus->rect.left+plus->rect.right)/2, 22) == 'T';
            if (dpi == 96 && frame < 8) chrome.SavePng((out+L"\\open-"+std::to_wstring(frame)+L".png").c_str());
        }
        Require(monotonic, "new-tab button glides right monotonically");
        Require(hits, "the presented new-tab button is where clicks land on every frame");
        Require(!chrome.tabMotion.NextWake(T(420)).delay, "opening ends: no idle frames");
        chrome.Paint(three, 2, L"", false, 0, 0, nullptr, T(430));
        RequireStatic(chrome, 3, 2, root, dpi, "settled opening equals the static layout exactly");

        // Close the selected last tab: it collapses as a ghost with its selected fill.
        const auto closing = Design::TabStrip::Surface(2);
        const float probe = closing.left + 10;
        chrome.tabKeys = {11, 12}; motion.SelectTab(12, T(500));
        chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(500));
        Require(chrome.TakeTabMotionStarted(), "closing requests frames");
        Require(!Find(chrome, TabBase+2) && !Find(chrome, CloseBase+2), "a closing tab is never hit-testable");
        Require(Gray(chrome.SurfacePixel(probe, 30)) >= 39, "the ghost starts sharp with its selected fill");
        chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(560));
        const int mid = Gray(chrome.SurfacePixel(probe, 30));
        Require(mid > 23 && mid < 39, "mid blur-out: the ghost is partly transparent over the canvas");
        Require(chrome.ghostBlurDraws > 0 && chrome.ghostFadeDraws == 0, "the closing tab blurs (Direct2D Gaussian blur), not just fades");
        chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(660));
        Require(Gray(chrome.SurfacePixel(probe, 30)) == 23 || Gray(chrome.SurfacePixel(probe, 30)) == Gray(chrome.SurfacePixel(probe+60, 30)), "after 150 ms the ghost has fully blurred out");
        for (int ms = 500, frame = 0; ms <= 800; ms += 25, ++frame) {
            chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(ms));
            if (dpi == 96 && frame < 8) chrome.SavePng((out+L"\\close-"+std::to_wstring(frame)+L".png").c_str());
        }
        chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(900));
        Require(Gray(chrome.SurfacePixel(probe, 30)) != 41 && chrome.tabSnapshots.size() == 2, "the ghost is gone and its snapshot pruned");
        Require(!chrome.tabMotion.NextWake(T(900)).delay, "closing ends: no idle frames");
        RequireStatic(chrome, 2, 1, root, dpi, "settled closing equals the static layout exactly");

        // Rapid open then close before the opening finishes: no hit target for the ghost,
        // and the remaining strip is exact once settled.
        chrome.tabKeys = {11, 12, 14}; chrome.Build(3, 2, &three, T(1000));
        chrome.Paint(three, 2, L"", false, 0, 0, nullptr, T(1060));
        chrome.tabKeys = {11, 12}; chrome.Build(2, 1, &two, T(1080));
        Require(!Find(chrome, TabBase+2), "interrupted opening: ghost not clickable");
        chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(1600));
        RequireStatic(chrome, 2, 1, root, dpi, "interrupted open/close settles exactly");

        // Resize follows the window immediately (no lag behind the frame edge).
        chrome.width = 700; chrome.Build(2, 1, &two, T(1700));
        Require(!chrome.tabMotion.NextWake(T(1700)).delay, "resize does not animate the strip");
    }
    // Reduced motion: geometry is final at once; the new tab only cross-fades.
    {
        Chrome chrome; Require(SUCCEEDED(chrome.InitOffscreen(root, 96, 1180, 84)), "reduced renderer");
        Motion::State motion(true); motion.EnablePolish(); chrome.animateSelection = true; chrome.SetMotion(&motion);
        chrome.tabKeys = {1, 2}; motion.SelectTab(2, T(0)); chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(0));
        chrome.tabKeys = {1, 2, 3}; motion.SelectTab(3, T(10)); chrome.Paint(three, 2, L"", false, 0, 0, nullptr, T(10));
        RequireStatic(chrome, 3, 2, root, 96, "reduced: regions final immediately");
        const int start = Gray(chrome.SurfacePixel(Design::TabStrip::Surface(2).left+64, 30));
        chrome.Paint(three, 2, L"", false, 0, 0, nullptr, T(85));
        const int mid = Gray(chrome.SurfacePixel(Design::TabStrip::Surface(2).left+64, 30));
        chrome.Paint(three, 2, L"", false, 0, 0, nullptr, T(200));
        const int end = Gray(chrome.SurfacePixel(Design::TabStrip::Surface(2).left+64, 30));
        Require(start <= mid && mid <= end && end == 41 && start < end, "reduced: opaque fill cross-fades in place");
        chrome.tabKeys = {1, 2}; chrome.Paint(two, 1, L"", false, 0, 0, nullptr, T(300));
        Require(!chrome.tabMotion.NextWake(T(300)).delay && chrome.tabMotion.Entries() == 2, "reduced: closing is immediate");
    }
    std::printf("PASS: %d tab motion render checks (96/144 DPI, hit regions, ghost, reduced motion); no WebView2.\n", checks);
    CoUninitialize(); return 0;
}
