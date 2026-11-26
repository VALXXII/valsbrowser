// Split view hover frames through the host (src/main.cpp; Val's « Atelier » boards « Fenêtre » 27E0-0 / 2793-0): the
// frames' two layered child windows (placed at the pane's top corners, named for screen readers), « réduire » and
// « fermer » through the real split view functions (a page leaves its group and stays a tab / closes), the site frame's
// click (its pane becomes the active one), a press moved off its glyph (nothing happens), reduced motion, and the
// pictures: the boards' scenes and the morph's poses rendered off screen (Direct2D + WIC).
// Screen-free: the main window is a never-shown popup at (-32000, -32000), so the frames' windows (its children) are
// never on a screen; the pointer is synthetic (the state machine's input and the actions window's own handler), no
// input is sent to the system and nothing takes the focus.
// Usage: split_frames_ui_test.exe [evidence folder]   (default build\atelier-a-qa\split-frames)
#include "../../src/main.cpp"
#include <chrono>
#include <cstdio>

static int checks = 0, failures = 0;
static void Check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } else std::printf("ok   %s\n", what); }
static bool Near(float a, float b, float e = .51f) { return std::fabs(a - b) <= e; }
static std::shared_ptr<const NativeChrome::FaviconImage> Icon(unsigned rgb) {   // a stand-in favicon, one colour
    auto f = std::make_shared<NativeChrome::FaviconImage>(); f->width = f->height = 32; f->stride = 32 * 4; f->pixels.assign(32 * 32 * 4, 0);
    for (int i = 0; i < 32 * 32; ++i) { unsigned char* p = &f->pixels[i * 4]; p[0] = rgb & 255; p[1] = (rgb >> 8) & 255; p[2] = (rgb >> 16) & 255; p[3] = 255; }
    return f;
}
static std::shared_ptr<Tab> MakeTab(const wchar_t* title, const wchar_t* host, unsigned rgb) {
    auto t = std::make_shared<Tab>(); t->title = title; t->home = false; t->uri = std::wstring(L"https://") + host + L"/"; t->revealed = true; t->favicon.image = Icon(rgb); return t;
}
static void Group(const std::vector<std::shared_ptr<Tab>>& members) { const int g = nextSplitGroup++; splitGroups[g] = SV::Orientation::Row; for (auto& t : members) { t->group = g; t->weight = 1; } }
// The frames on pane `p`, as if the pointer had rested at its top (the machine's input made by hand), settled.
static void HoverPane(int p) {
    auto in = SplitFramesInput(); splitFramesPanes = in.panes; in.active = true; in.pointer = true; in.buttons = false;
    in.x = (in.panes[p].left + in.panes[p].right) / 2; in.y = in.panes[p].top + 10;
    // Three seconds ago: the morph has settled by now, so a repaint at the real clock (a hover) sees them in place.
    const auto t0 = NativeChrome::Motion::Now() - std::chrono::milliseconds(3000); splitFramesMachine.SetReduced(motion.ReducedMotion());
    splitFramesMachine.Update(in, t0); splitFramesMachine.Update(in, t0 + std::chrono::milliseconds(200));
    SplitFramesPaint(NativeChrome::Motion::Now());
}
static RECT ChildRect(HWND h) { RECT r{}; GetWindowRect(h, &r); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2); return r; }
static std::wstring Text(HWND h) { wchar_t b[256]{}; GetWindowTextW(h, b, 256); return b; }
static void Click(float x, float y, float x2, float y2) { splitFrames.PointerForTest(WM_MOUSEMOVE, x, y); splitFrames.PointerForTest(WM_LBUTTONDOWN, x, y); splitFrames.PointerForTest(WM_LBUTTONUP, x2, y2); }

// A board scene (Paper « Fenêtre »): the canvas, the two panes (#A5A5A5, radius 6) under the 81 DIP chrome, the frames.
static bool Scene(const std::wstring& root, const std::wstring& file, UINT dpi, const std::vector<SV::Box>& panes, int pane, float progress, bool reduced,
                  const std::wstring& name, const std::wstring& address, unsigned rgb, int hover = 0) {
    NativeChrome::Chrome off; if (FAILED(off.InitOffscreen(root, dpi, 1180, 815))) return false;
    NativeChrome::SplitFrames::Frames frames; frames.painter.write = off.WriteFactory(); frames.painter.fonts = off.FontCollection();
    auto* t = off.RenderTarget(); ID2D1SolidColorBrush* b = nullptr; t->BeginDraw();
    const float c = NativeChrome::Design::Surface::canvas; t->Clear(D2D1::ColorF(c, c, c, 1));
    if (SUCCEEDED(t->CreateSolidColorBrush(D2D1::ColorF(0xA5 / 255.f, 0xA5 / 255.f, 0xA5 / 255.f), &b))) {
        for (auto& p : panes) t->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p.left, p.top, p.right, p.bottom), 6, 6), b); b->Release(); }
    NativeChrome::SplitFrames::Content content; content.pane = panes[pane]; content.name = name; content.address = address; content.favicon = Icon(rgb);
    content.progress = progress; content.reduced = reduced; content.hover = hover;
    // The window clips each frame to its final box (the pane's corner): the same clip here.
    const auto site = frames.painter.Site(content); const auto actions = NativeChrome::SplitFrames::LayoutActions(content.pane);
    if (site.shown) { t->PushAxisAlignedClip(D2D1::RectF(site.frame.left, site.frame.top, site.frame.right, site.frame.bottom), D2D1_ANTIALIAS_MODE_ALIASED); frames.painter.DrawSite(t, site, content); t->PopAxisAlignedClip(); }
    if (actions.shown) { t->PushAxisAlignedClip(D2D1::RectF(actions.frame.left, actions.frame.top, actions.frame.right, actions.frame.bottom), D2D1_ANTIALIAS_MODE_ALIASED); frames.painter.DrawActions(t, actions, content); t->PopAxisAlignedClip(); }
    if (FAILED(t->EndDraw())) return false;
    if (FAILED(off.SavePng(file.c_str()))) return false;
    // Pixel probes at progress 1 (DIP): the canvas inside each frame, the pane just under it, the rounded inner corner.
    if (progress >= 1 && !reduced) {
        auto g = [&](float x, float y) { return static_cast<int>((off.SurfacePixel(x, y) >> 8) & 255); };
        const int canvas = 0x17, pane = 0xA5;
        Check(std::abs(g(actions.frame.right - 2, actions.frame.top + 2) - canvas) <= 1 && std::abs(g(actions.frame.right - 2, actions.frame.bottom + 2) - pane) <= 1, "picture: the actions frame is the canvas, the page right under it");
        Check(std::abs(g(site.frame.left + 2, site.frame.top + 2) - canvas) <= 1 && std::abs(g(site.frame.right + 2, site.frame.top + 12) - pane) <= 1, "picture: the site frame is the canvas, hugging its text");
        Check(g(site.frame.right - .5f, site.frame.bottom - .5f) > canvas + 40 && g(actions.frame.left + .5f, actions.frame.bottom - .5f) > canvas + 40, "picture: the inner bottom corners are rounded (6)");
        Check(std::abs(g(site.frame.left + .5f, site.frame.bottom - .5f) - canvas) <= 2 && std::abs(g(actions.frame.right - .5f, actions.frame.bottom - .5f) - canvas) <= 2, "picture: square on the pane's own side");
        int peak = 0; for (float y = actions.minimize.top; y < actions.minimize.bottom; y += .5f) for (float x = actions.minimize.left; x < actions.minimize.right; x += .5f) peak = std::max(peak, g(x, y));
        Check(peak > canvas + 70 && peak <= canvas + (255 - canvas) * 0x82 / 255 + 3, "picture: the minimize glyph at white 51 % (#FFFFFF82), never brighter");
    }
    return true;
}

int main(int argc, char** argv) {

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) { std::puts("FAIL COM"); return 1; }
    wchar_t cwd[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, cwd); const std::wstring root = cwd;
    std::wstring out = root + L"\\build\\atelier-a-qa\\split-frames";
    if (argc > 1) { wchar_t w[MAX_PATH]{}; MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, w, MAX_PATH); out = w; }
    CreateDirectoryW((root + L"\\build").c_str(), nullptr); CreateDirectoryW((root + L"\\build\\atelier-a-qa").c_str(), nullptr); CreateDirectoryW(out.c_str(), nullptr);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"split frames test", WS_POPUP, -32000, -32000, 1180, 815, nullptr, nullptr, hi, nullptr);
    Check(window && !IsWindowVisible(window), "the main window: a popup at -32000, never shown");
    Check(SUCCEEDED(chrome.InitOffscreen(root, 96, 1180, 815)), "chrome off screen");
    chrome.width = 1180; chrome.height = 815; motion.SetReducedMotion(false);
    SplitFramesInit(hi);
    Check(splitFrames.SiteWindow() && splitFrames.ActionsWindow() && GetParent(splitFrames.ActionsWindow()) == window && !IsWindowVisible(splitFrames.ActionsWindow()),
          "two layered child windows of the main window, hidden");
    Check((GetWindowLongW(splitFrames.ActionsWindow(), GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_NOACTIVATE)) == (WS_EX_LAYERED | WS_EX_NOACTIVATE), "layered, never activated");

    auto yt = MakeTab(L"YouTube", L"youtube.com", 0xFF0000), paper = MakeTab(L"Paper", L"paper.design", 0x4E7BFF), luma = MakeTab(L"Luma", L"lu.ma", 0xFFFFFF), gh = MakeTab(L"GitHub", L"github.com", 0x24292E);
    pages = {yt, paper, luma}; current = 1; Group({yt, paper}); ApplyPages();
    {   auto v = CurrentSplit(); Check(v.group && v.a.slots.size() == 2, "a split view of two panes (YouTube | Paper)"); }
    // ---- No grey frame around the active pane any more (Val 2026-11-26): the hover frames name the pages. The chrome
    // painted off screen: only the canvas around both pages (in the 4 DIP gap, under them, at the window's edge), after a
    // click (pointer) as after F6 / Tab (keyboard, where the ring used to be the strong one).
    for (bool keyboard : {false, true}) {
        keyboardModality = keyboard; SyncSplitDecor(); BuildChrome();
        std::vector<std::wstring> titles; for (auto& t : pages) titles.push_back(t->title);
        chrome.Paint(titles, current, Active()->uri, false, 0, 0);
        auto v = CurrentSplit(); const auto box = v.a.slots[v.a.SlotOf(v.active)].box; int lit = 0, probes = 0;
        auto probe = [&](float x, float y) { ++probes; const int g = static_cast<int>((chrome.SurfacePixel(x, y) >> 8) & 255); if (std::abs(g - 0x17) > 2) ++lit; };
        for (float y = box.top + 12; y < box.bottom - 12; y += 2) for (float d : {.5f, 1.5f, 2.5f}) { probe(box.left - d, y); probe(box.right + d, y); }
        for (float x = box.left + 12; x < box.right - 12; x += 2) for (float d : {.5f, 1.5f, 2.5f}) probe(x, box.bottom + d);
        Check(probes > 1000 && lit == 0, keyboard ? "keyboard (F6): no frame around the active pane, the canvas all around it" : "pointer: no frame around the active pane, the canvas all around it");
        chrome.SavePng((out + (keyboard ? L"\\no-active-ring-keyboard.png" : L"\\no-active-ring-pointer.png")).c_str());
    }
    keyboardModality = false;
    // The real poll: this test's window is not in front, so nothing shows and nothing polls.
    SplitFramesEvaluate(); Check(!splitFrames.Visible() && splitFramesMachine.Pane() == -1 && !splitFramesPolling, "window not in front: no frames, no pointer poll");

    // ---- Placement on the right pane (board 27E0-0) and the left one (2793-0)
    HoverPane(1);
    {   const auto pane = splitFramesPanes[1]; const RECT a = ChildRect(splitFrames.ActionsWindow()), s = ChildRect(splitFrames.SiteWindow());
        Check(splitFrames.Visible() && splitFramesMachine.Pane() == 1, "pointer at the right pane's top: the frames show there");
        Check(Near(static_cast<float>(a.right), pane.right) && Near(static_cast<float>(a.top), pane.top) && a.right - a.left == 58 && a.bottom - a.top == 24, "actions window: 58 x 24 at the pane's top-right corner");
        Check(Near(static_cast<float>(s.left), pane.left) && Near(static_cast<float>(s.top), pane.top) && s.bottom - s.top == 24 && s.right < a.left, "site window: at the pane's top-left corner, before the actions");
        Check(Text(splitFrames.ActionsWindow()) == L"Réduire ou fermer Paper" && Text(splitFrames.SiteWindow()) == L"Paper, paper.design", "screen readers: « Réduire ou fermer Paper », « Paper, paper.design »");
    }
    HoverPane(0);
    {   const RECT s = ChildRect(splitFrames.SiteWindow()); Check(splitFramesMachine.Pane() == 0 && Near(static_cast<float>(s.left), splitFramesPanes[0].left) && Text(splitFrames.SiteWindow()) == L"YouTube, youtube.com", "the left pane: its own frames, its own site"); }

    // ---- Hover and press on the actions frame
    HoverPane(1);
    {   const auto a = splitFrames.Actions(); const float y = a.frame.top + 12;
        splitFrames.PointerForTest(WM_MOUSEMOVE, (a.minimize.left + a.minimize.right) / 2, y); Check(splitFrames.Hover() == NativeChrome::SplitFrames::Minimize, "hover: « réduire »");
        splitFrames.PointerForTest(WM_MOUSEMOVE, (a.close.left + a.close.right) / 2, y); Check(splitFrames.Hover() == NativeChrome::SplitFrames::Close, "hover: « fermer »");
        splitFrames.PointerForTest(WM_MOUSELEAVE, 0, 0); Check(splitFrames.Hover() == NativeChrome::SplitFrames::NoAction, "leave: no hover");
        Click((a.minimize.left + a.minimize.right) / 2, y, (a.close.left + a.close.right) / 2, y);
        Check(paper->group && yt->group && pages.size() == 3, "pressed on « réduire », released on « fermer »: nothing happens");
    }

    // ---- « réduire » on the active pane of two: the split ends, Paper stays a tab, YouTube is shown
    HoverPane(1);
    {   const auto a = splitFrames.Actions(); const float x = (a.minimize.left + a.minimize.right) / 2, y = a.frame.top + 12;
        Click(x, y, x, y);
        Check(!paper->group && !yt->group && splitGroups.empty() && pages.size() == 3 && !paper->closed, "« réduire »: Paper leaves the split view and stays a tab");
        Check(Active() == yt && yt->shown && !paper->shown, "the other page stays on screen (YouTube, alone now)");
        Check(!splitFrames.Visible() && splitFramesMachine.Pane() == -1, "the frames leave at once");
    }
    // ---- « réduire » on an inactive middle pane of three: the group keeps two, one run of tabs
    pages = {yt, paper, luma, gh}; current = 0; Group({yt, paper, luma}); ApplyPages();
    HoverPane(1);
    {   const auto a = splitFrames.Actions(); const float x = (a.minimize.left + a.minimize.right) / 2, y = a.frame.top + 12;
        std::printf("     3 panes: %d listed, frames on %d, actions at %.1f\n", static_cast<int>(splitFramesPanes.size()), splitFramesMachine.Pane(), a.frame.left);
        Click(x, y, x, y);
        auto v = CurrentSplit();
        std::printf("     after: group %d, %d members, paper group %d, active %ls, order", v.group, static_cast<int>(v.members.size()), paper->group, Active() ? Active()->title.c_str() : L"-");
        for (auto& t : pages) std::printf(" %ls", t->title.c_str()); std::printf("\n");
        Check(!paper->group && v.group && v.members.size() == 2 && v.members[0] == yt && v.members[1] == luma && Active() == yt, "middle pane reduced: YouTube | Luma stay split, YouTube still active");
        Check(TabIndexOf(yt) == 0 && TabIndexOf(luma) == 1 && TabIndexOf(paper) == 2 && TabIndexOf(gh) == 3, "Paper goes right after the group (the group stays one run of tabs)");
    }
    // ---- « fermer »: the page closes like its tab's close button
    HoverPane(1);   // Luma
    {   const auto a = splitFrames.Actions(); const float x = (a.close.left + a.close.right) / 2, y = a.frame.top + 12;
        Click(x, y, x, y);
        Check(luma->closed && TabIndexOf(luma) < 0 && pages.size() == 3 && splitGroups.empty() && Active() == yt, "« fermer »: Luma closes, YouTube stays (alone)");
        Check(!splitFrames.Visible(), "the frames leave with it");
    }
    // ---- The site frame: a click makes its pane the active one
    current = 0; Group({yt, paper}); ApplyPages(); HoverPane(1);
    Check(Active() == yt, "YouTube active, frames on Paper");
    if (splitFrames.activate) splitFrames.activate();
    Check(Active() == paper && CurrentSplit().group, "a click on the site frame: Paper becomes the active pane, the split stays");
    // ---- Reduced motion (« Réduire les animations »): the machine follows the app's setting
    motion.SetReducedMotion(true); SplitFramesEvaluate(); Check(splitFramesMachine.Reduced(), "reduced motion: the frames fade, no travel");
    motion.SetReducedMotion(false); SplitFramesEvaluate();
    splitFrames.Hide(); splitFramesMachine.Dismiss();

    // ---- Pictures: the boards' two scenes at 100 / 125 / 150 %, the morph's poses, reduced motion, hover
    const std::vector<SV::Box> panes{{4, 81, 588, 811}, {592, 81, 1176, 811}};
    for (UINT dpi : {96u, 120u, 144u}) {
        const std::wstring tag = L"-" + std::to_wstring(dpi * 100 / 96);
        Check(Scene(root, out + L"\\frames-right" + tag + L".png", dpi, panes, 1, 1, false, L"Paper", L"paper.design", 0x4E7BFF), "scene: frames on the right pane (27E0-0)");
        Check(Scene(root, out + L"\\frames-left" + tag + L".png", dpi, panes, 0, 1, false, L"Youtube", L"youtube.com", 0xFF0000), "scene: frames on the left pane (2793-0)");
    }
    for (int k : {0, 15, 30, 50, 70, 85}) {
        // Progress p as the morph spring lays it out over time (the clock's fraction of Settle(0.32)).
        const float p = NativeChrome::SplitFrames::Timing::enterSpring.At(k / 100.f);
        Scene(root, out + L"\\morph-" + std::to_wstring(k) + L".png", 96, panes, 1, p, false, L"Paper", L"paper.design", 0x4E7BFF);
    }
    Check(Scene(root, out + L"\\reduced-50.png", 96, panes, 1, .5f, true, L"Paper", L"paper.design", 0x4E7BFF), "scene: reduced motion halfway (a fade)");
    Check(Scene(root, out + L"\\hover-close.png", 96, panes, 1, 1, false, L"Paper", L"paper.design", 0x4E7BFF, NativeChrome::SplitFrames::Close), "scene: « fermer » hovered");
    Check(Scene(root, out + L"\\narrow.png", 96, {{4, 81, 380, 811}, {384, 81, 760, 811}}, 1, 1, false, L"Un très long titre de page", L"un-sous-domaine.exemple-public.fr", 0x4E7BFF), "scene: a narrow pane (trimmed)");

    pages.clear(); splitGroups.clear(); splitFrames.Dispose(); DestroyWindow(window); window = nullptr;
    std::printf("\n%d checks, %d failures\n", checks, failures);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), failures ? 1 : 0);   // returning after including main.cpp can hang (global teardown)
    return failures ? 1 : 0;
}
