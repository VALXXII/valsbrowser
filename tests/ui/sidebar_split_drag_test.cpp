// #127 point 1 (Paper « Vue partagée dans la colonne (panneau + compact) », section 3): a tab dragged from the column
// onto the page makes a split view, through the top bar's own lift / overlay / split code (SplitDragMove,
// CommitSplitDrag, EndSplitDrag). Drives the real window procedure of src/main.cpp, no WebView2, no profile:
//   reorder in the column vs lift past its right edge (20 DIP hysteresis back), drop on the page = split (panel,
//   compact, the column open over the page in a narrow window), refusal, Escape (the card flies home to its row),
//   drop on the bookmarks grid, release outside the window (tear-off branch), and the top bar unchanged.
// Screen-free: the window and the drag overlay live at (-32000, -32000), never shown on a screen, never activated.
// Renders (the column with its empty place + the overlay's card and drop preview, the page painted white as
// tools/sidebar/compose.py does): build/sidebar-split-qa/*.png.
#include "../../src/main.cpp"
#include <chrono>
#include <cstdio>

static int checks = 0;
#define EXPECT(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

static LPARAM At(float x, float y) { return MAKELPARAM((int)std::lround(x), (int)std::lround(y)); }
static std::shared_ptr<const NativeChrome::FaviconImage> Icon(unsigned rgb) {   // a 16 DIP stand-in favicon, one colour
    auto f = std::make_shared<NativeChrome::FaviconImage>(); f->width = f->height = 32; f->stride = 32 * 4; f->pixels.assign(32 * 32 * 4, 0);
    for (int i = 0; i < 32 * 32; ++i) { unsigned char* p = &f->pixels[i * 4]; p[0] = rgb & 255; p[1] = (rgb >> 8) & 255; p[2] = (rgb >> 16) & 255; p[3] = 255; }
    return f;
}
static std::shared_ptr<Tab> MakeTab(const wchar_t* title, unsigned rgb) {
    auto t = std::make_shared<Tab>(); t->title = title; t->home = false; t->uri = std::wstring(L"https://") + title + L".test/"; t->revealed = true; t->favicon.image = Icon(rgb); return t;
}
static SBar::Box Row(const std::shared_ptr<Tab>& t) { BuildChrome(); const int i = TabIndexOf(t); EXPECT(i >= 0 && i < (int)chrome.SideColumn().tabs.size()); return chrome.SideColumn().tabs[i]; }
static void Press(const std::shared_ptr<Tab>& t) { const auto r = Row(t); const float x = r.left + std::min(40.f, r.Width() / 2); WndProc(window, WM_LBUTTONDOWN, MK_LBUTTON, At(x, (r.top + r.bottom) / 2)); EXPECT(tabDrag.tab == t); }
static void Move(float x, float y) { WndProc(window, WM_MOUSEMOVE, MK_LBUTTON, At(x, y)); }
static void Release(float x, float y) { WndProc(window, WM_LBUTTONUP, 0, At(x, y)); }
static void Escape() { WndProc(window, WM_KEYDOWN, VK_ESCAPE, 0); }
// Runs the overlay's frames (16 ms) until a release / cancel animation ends and hides it.
static void Settle() { auto now = splitAnim.last; for (int i = 0; i < 400 && splitOverlay.Visible(); ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); } EXPECT(!splitOverlay.Visible()); }
static void Reset(std::vector<std::shared_ptr<Tab>> order) {
    for (auto& t : order) { t->group = 0; t->weight = 1; } splitGroups.clear(); pages = std::move(order); current = 0; pages[0]->usedAt = ++useClock; Layout(); ApplyPages();
}
static bool Near(float a, float b, float tol = .5f) { return std::fabs(a - b) <= tol; }
static int Channel(UINT32 p, int shift) { return (int)((p >> shift) & 255); }

// The column (real chrome state) + the page (white, as compose.py) + the overlay frame the drag shows now.
static void Render(const std::wstring& path) {
    splitAnim.plateOpacity.Snap(1); splitAnim.cardOpacity.Snap(1); splitAnim.cardScale.Snap(splitAnim.cardScale.target); RenderSplitOverlay();   // settled (≈ 200 ms in)
    std::vector<std::wstring> titles; std::vector<std::shared_ptr<const NativeChrome::FaviconImage>> icons;
    for (auto& t : pages) { titles.push_back(t->title); icons.push_back(t->favicon.image); }
    chrome.SetFavicons(std::move(icons)); chrome.sideHome.assign(pages.size(), false); chrome.sideDetails.assign(pages.size(), L"");
    BuildChrome(); chrome.Paint(titles, current, Active()->uri, false, 0, 0);
    auto* t = chrome.RenderTarget(); const auto c = ContentBox(); ID2D1SolidColorBrush* white = nullptr;
    t->BeginDraw(); t->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &white);
    if (white) { t->FillRoundedRectangle(D2D1::RoundedRect({c.left, c.top, c.right, c.bottom}, 8, 8), white); white->Release(); }
    splitOverlay.painter.Draw(t, splitAnim.frame); EXPECT(SUCCEEDED(t->EndDraw()));
    EXPECT(SUCCEEDED(chrome.SavePng(path.c_str())));
}

int main() {
    const auto started = std::chrono::steady_clock::now();
    wchar_t cwd[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, cwd); const std::wstring root = cwd, out = root + L"\\build\\sidebar-split-qa";
    CreateDirectoryW((root + L"\\build").c_str(), nullptr); CreateDirectoryW(out.c_str(), nullptr);
    dataDir = out + L"\\data"; CreateDirectoryW(dataDir.c_str(), nullptr); DeleteFileW(BookmarksFile().c_str());   // the grid drop saves here
    EXPECT(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"sidebar split drag test", WS_POPUP, -32000, -32000, 1180, 815, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    EXPECT(window && !IsWindowVisible(window));
    EXPECT(SUCCEEDED(chrome.InitOffscreen(root, 96, 1180, 815)));
    EXPECT(splitOverlay.Init(GetModuleHandleW(nullptr), window, chrome.Factory(), chrome.WriteFactory(), chrome.FontCollection()));
    motion.SetReducedMotion(false);   // the system's « animation effects » setting must not decide the run (reduced motion: step 11)

    settings.tabsPosition = BrowserSettings::TabsPosition::Side; ApplyLayout(); EXPECT(chrome.sidebar);
    auto yt = MakeTab(L"Youtube", 0xE5322D), luma = MakeTab(L"Luma", 0xE24B3B), paper = MakeTab(L"Paper", 0x111111);
    luma->title = L"Luma · Calendrier des événements"; EXPECT(SiteLabel(luma) != luma->title);   // the top bar's card would say the short name
    Reset({yt, luma, paper}); EXPECT(!chrome.sidebarOverlay && SidebarEffective() == SBar::Mode::Complete);
    const float edge = ContentBox().left; EXPECT(Near(edge, chrome.SidebarZoneLeft()) && Near(SidebarDropEdge(), edge));

    // 1. Reorder stays in the column: a drag that never passes its right edge moves the row, nothing lifts.
    { const auto r0 = Row(yt); Press(paper); Move(120, r0.top + 4);
      EXPECT(tabDrag.active && !tabDrag.lifted && chrome.sidebarDrag == 2 && chrome.sidebarInsert == 0 && chrome.liftedIndex == -1 && !splitOverlay.Visible());
      Release(120, r0.top + 4); EXPECT(!tabDrag.tab && pages[0] == paper && pages[1] == yt && pages[2] == luma && splitGroups.empty()); }
    Reset({yt, luma, paper});

    // 2. 6 DIP before anything moves (a click otherwise, motion spec). The lift line is the column's right edge; 20 DIP
    //    back inside it, the reorder resumes; Escape restores.
    { const auto r = Row(luma); const float x0 = r.left + 40, y0 = (r.top + r.bottom) / 2; Press(luma);
      Move(x0 + 5, y0); EXPECT(!tabDrag.active); Move(x0, y0 + 5); EXPECT(!tabDrag.active); Move(x0 + 6, y0); EXPECT(tabDrag.active); Escape(); EXPECT(!tabDrag.tab); }
    Press(luma); Move(edge - 1, 300);
    EXPECT(tabDrag.active && !tabDrag.lifted && chrome.sidebarDrag == 1);                        // still the column
    Move(edge, 300);
    EXPECT(tabDrag.lifted && chrome.liftedIndex == 1 && chrome.sidebarDrag == -1 && chrome.sidebarInsert == -1);   // lifted: its row's place empty
    EXPECT(tabDrag.target.kind == SV::Kind::Split && tabDrag.target.side == SV::Side::Left && splitOverlay.Visible());
    {   // the card is the row lifted: its title (not the short site name), fitted to it, 32 high, grabbed where it was pressed
        const auto card = splitAnim.card.Value(); EXPECT(splitAnim.frame.card.shown && splitAnim.frame.card.title == luma->title);
        EXPECT(tabDrag.cardW > 8 + 16 + 8 + 10 && tabDrag.cardW <= 240 && Near(card.W(), tabDrag.cardW) && Near(card.H(), 32));
        EXPECT(Near(card.left, edge - tabDrag.grab) && card.left < edge && card.right > edge); }
    Move(edge - 10, 300); EXPECT(tabDrag.lifted && tabDrag.target.kind == SV::Kind::None);        // hysteresis: still lifted, no target
    Move(edge - 21, 300); EXPECT(!tabDrag.lifted && chrome.liftedIndex == -1 && chrome.sidebarDrag == 1 && !splitOverlay.Visible());
    Escape(); EXPECT(!tabDrag.tab && pages[1] == luma && splitGroups.empty() && chrome.sidebarDrag == -1);

    // 3. Dropped on the right half of the page: Youtube (shown) and Paper side by side, Paper active, the pair together.
    Press(paper); Move(900, 400);
    EXPECT(tabDrag.lifted && chrome.liftedIndex == 2 && tabDrag.target.kind == SV::Kind::Split && tabDrag.target.side == SV::Side::Right);
    { const auto c = ContentBox(); const auto p = tabDrag.target.preview; EXPECT(Near(p.right, c.right) && Near(p.top, c.top) && Near(p.bottom, c.bottom) && p.left > (c.left + c.right) / 2 - 4); }
    Render(out + L"\\split-drag-panel.png");
    {   // lifted: x1.02 (spring 1.0 / 0.15 s) around the grab point, which stays under the pointer.
        EXPECT(splitAnim.cardScale.target == 102 && Near(splitAnim.anchorX, tabDrag.grab / tabDrag.cardW, .001f));
        auto now = splitAnim.last; for (int i = 0; i < 20; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
        const auto b = splitAnim.frame.card.box; EXPECT(Near(b.W(), tabDrag.cardW * 1.02f, .3f) && Near(b.H(), 32 * 1.02f, .1f));
        EXPECT(Near(b.left + b.W() * splitAnim.anchorX, 900, .3f) && Near(b.top + b.H() * splitAnim.anchorY, 400, .3f)); }
    {   // pixels: the row's empty place (1 DIP #3A3A3A outline, the canvas inside), the drop preview (16 % accent on white,
        // 2 DIP accent outline) and the card (#2A2A2A, Paper 24BU-0), the top bar's drawing.
        const auto r = Row(paper); const float my = (r.top + r.bottom) / 2;
        const UINT32 outline = chrome.SurfacePixel(r.left + .5f, my), inside = chrome.SurfacePixel(r.left + 40, my);
        EXPECT(std::abs(Channel(outline, 8) - 0x3A) <= 2 && std::abs(Channel(inside, 8) - 0x17) <= 2);
        const auto p = tabDrag.target.preview; const UINT32 tint = chrome.SurfacePixel((p.left + p.right) / 2, p.bottom - 40), ring = chrome.SurfacePixel(p.left + 1, p.bottom - 40);
        const auto a = NativeChrome::Design::Accent::Get();
        EXPECT(std::abs(Channel(tint, 16) - (int)std::lround(255 * (1 - .16f) + a.r * 255 * .16f)) <= 3 && std::abs(Channel(tint, 0) - (int)std::lround(255 * (1 - .16f) + a.b * 255 * .16f)) <= 3);
        EXPECT(std::abs(Channel(ring, 16) - (int)std::lround(a.r * 255)) <= 3 && std::abs(Channel(ring, 0) - (int)std::lround(a.b * 255)) <= 3);
        const auto card = splitAnim.frame.card.box; const UINT32 fill = chrome.SurfacePixel(card.left + 4, card.top + 16);
        EXPECT(std::abs(Channel(fill, 8) - 0x2A) <= 1); }
    {   // the zone morphs (motion spec § 4): to the bottom zone in position and size (spring 1.0 / 0.2 s), never fading
        // between zones; outside any zone (the column's edge band) it fades out in 100 ms.
        const auto right = tabDrag.target.preview; Move(600, 780); EXPECT(tabDrag.target.kind == SV::Kind::Split && tabDrag.target.side == SV::Side::Bottom);
        const auto bottom = tabDrag.target.preview; auto v = splitAnim.plate.Value();
        EXPECT(Near(splitAnim.plate.t.target, bottom.top) && Near(splitAnim.plate.l.target, bottom.left) && Near(v.left, right.left) && Near(v.top, right.top));   // not a jump
        EXPECT(splitAnim.plateOpacity.target == 1 && splitAnim.frame.plate.shown);
        auto now = splitAnim.last; for (int i = 0; i < 3; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
        v = splitAnim.plate.Value(); EXPECT(v.left < right.left - 20 && v.left > bottom.left + 20 && v.top > right.top + 20 && splitAnim.frame.plate.opacity > .99f);   // between the two
        for (int i = 0; i < 10; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
        v = splitAnim.plate.Value();   // response 0.2 s: at 208 ms under 3 % of the way left (a 0.3 s spring would still be ~7 % off)
        EXPECT(std::fabs(v.left - bottom.left) < .03f * (right.left - bottom.left) && std::fabs(v.top - bottom.top) < .03f * (bottom.top - right.top));
        for (int i = 0; i < 12; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
        v = splitAnim.plate.Value(); EXPECT(Near(v.left, bottom.left) && Near(v.top, bottom.top) && Near(v.right, bottom.right) && Near(v.bottom, bottom.bottom));   // settled
        Move(edge - 10, 400); EXPECT(tabDrag.lifted && tabDrag.target.kind == SV::Kind::None && splitAnim.plateOpacity.target == 0 && Near(splitAnim.fade, .10f, .001f));
        now = splitAnim.last; for (int i = 0; i < 7; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
        EXPECT(splitAnim.plateOpacity.x < .05f && !splitAnim.frame.plate.shown);   // gone in ~100 ms
        Move(900, 400); EXPECT(tabDrag.target.side == SV::Side::Right); }
    Release(900, 400);
    EXPECT(!tabDrag.tab && splitGroups.size() == 1 && yt->group && yt->group == paper->group && !luma->group && Active() == paper);
    EXPECT(pages[0] == yt && pages[1] == paper && pages[2] == luma && chrome.liftedIndex == -1);
    EXPECT(chrome.split.shown && chrome.split.handles.size() == 1 && chrome.split.active.left > (ContentBox().left + ContentBox().right) / 2 - 4);
    Settle(); EXPECT(chrome.liftedIndex == -1);
    Reset({yt, luma, paper});

    // 4. Escape while lifted: nothing changes; the card flies home to its row in the column (its place empty until it lands).
    Press(luma); Move(700, 400); EXPECT(tabDrag.lifted);
    Escape();
    { const auto r = Row(luma); EXPECT(!tabDrag.tab && splitGroups.empty() && pages[1] == luma && splitAnim.returning && chrome.liftedIndex == 1);
      EXPECT(Near(splitAnim.card.l.target, r.left) && Near(splitAnim.card.t.target, r.top) && Near(splitAnim.card.r.target, r.right) && Near(splitAnim.card.b.target, r.bottom));
      Settle(); EXPECT(chrome.liftedIndex == -1 && Near(splitAnim.card.Value().left, r.left, 1)); }

    // 5. Released lifted but back over the column's edge band (no target): cancelled, the tab flies home, nothing split.
    Press(luma); Move(700, 400); Move(edge - 5, 400); EXPECT(tabDrag.lifted && tabDrag.target.kind == SV::Kind::None);
    Release(edge - 5, 400); EXPECT(!tabDrag.tab && splitGroups.empty() && pages[1] == luma); Settle();

    // 6. Onto the bookmarks grid (answer 9): a new bookmark, the order unchanged, never a split.
    settings.bookmarksBar = BrowserSettings::BookmarksBar::Always; BookmarksChanged(); Layout();
    { const auto g = chrome.SideColumn().gridBlock; EXPECT(!g.Empty() && g.right <= edge);
      Press(paper); Move((g.left + g.right) / 2, (g.top + g.bottom) / 2); EXPECT(!tabDrag.lifted);
      Release((g.left + g.right) / 2, (g.top + g.bottom) / 2);
      EXPECT(!tabDrag.tab && bookmarks.Matching(paper->uri).size() == 1 && pages[2] == paper && splitGroups.empty()); }
    settings.bookmarksBar = BrowserSettings::BookmarksBar::Auto; BookmarksChanged(); Layout();

    // 7. Released outside the window: the tear-off branch, lifted or not, never a split nor a reorder. A private window
    //    keeps the tab (no new window is launched from a test); the new window itself is for the real-window pass.
    privateWindow = true;
    Press(luma); Move(900, 400); Move(1300, 400); EXPECT(tabDrag.lifted && tabDrag.target.kind == SV::Kind::Split);   // past the window's right edge
    Release(1300, 400); EXPECT(!tabDrag.tab && splitGroups.empty() && pages[1] == luma); Settle();
    { const auto r0 = Row(yt); Press(paper); Move(120, r0.top + 4); EXPECT(chrome.sidebarInsert == 0); Move(-60, r0.top + 4);
      Release(-60, r0.top + 4); EXPECT(!tabDrag.tab && pages[0] == yt && pages[2] == paper && splitGroups.empty()); }   // left of the window: no reorder
    privateWindow = false;

    // 8. Compact (« Panneau compact »): the same, its 48 DIP edge; the card keeps its title though the row is an icon.
    settings.tabsPosition = BrowserSettings::TabsPosition::Compact; ApplyLayout(); Reset({yt, luma, paper});
    EXPECT(chrome.sidebar && SidebarEffective() == SBar::Mode::Compact && !chrome.sidebarOverlay);
    { const float e = ContentBox().left; EXPECT(e < 80 && Near(SidebarDropEdge(), e));
      Press(luma); Move(e - 1, 300); EXPECT(tabDrag.active && !tabDrag.lifted && chrome.sidebarDrag == 1);
      Move(e + 1, 300); EXPECT(tabDrag.lifted && chrome.liftedIndex == 1 && splitAnim.frame.card.title == luma->title && tabDrag.cardW > 32);
      Move(400, 700); EXPECT(tabDrag.target.kind == SV::Kind::Split && tabDrag.target.side == SV::Side::Bottom);
      Render(out + L"\\split-drag-compact.png");
      { const auto r = Row(luma); const UINT32 outline = chrome.SurfacePixel(r.left + .5f, (r.top + r.bottom) / 2), inside = chrome.SurfacePixel((r.left + r.right) / 2, (r.top + r.bottom) / 2);
        std::printf("compact row (the empty place): %.0f x %.0f\n", r.Width(), r.Height());
        EXPECT(std::abs(Channel(outline, 8) - 0x3A) <= 2 && std::abs(Channel(inside, 8) - 0x17) <= 2); }
      Release(400, 700);
      EXPECT(splitGroups.size() == 1 && yt->group == luma->group && splitGroups[yt->group] == SV::Orientation::Column && Active() == luma); Settle();
      // Escape in compact: home to its icon cell.
      Reset({yt, luma, paper}); Press(paper); Move(600, 400); EXPECT(tabDrag.lifted); Escape();
      const auto r = Row(paper); EXPECT(splitAnim.returning && Near(splitAnim.card.r.target - splitAnim.card.l.target, r.Width())); Settle(); }

    // 9. A narrow window: the side panel is compact by itself; its fold button opens the complete column over the page.
    //    That column is column: no lift inside it (the page under it is not the target); past its edge the row lifts,
    //    the column closes and the whole page takes the drop. Too narrow for two sites side by side: refused; stacked: fine.
    settings.tabsPosition = BrowserSettings::TabsPosition::Side; ApplyLayout(); chrome.width = 700; Reset({yt, luma, paper});
    EXPECT(SidebarEffective() == SBar::Mode::Compact);
    sidebarHover.Open(); Layout(); EXPECT(chrome.sidebarOverlay);
    { const float over = SidebarDropEdge(); EXPECT(over > 200 && ContentBox().left < 80);
      Press(paper); Move(ContentBox().left + 20, 300); EXPECT(tabDrag.active && !tabDrag.lifted);   // over the page, but inside the open column
      Move(over - 1, 300); EXPECT(!tabDrag.lifted && chrome.sidebarOverlay);
      Move(over + 1, 300); EXPECT(tabDrag.lifted && !chrome.sidebarOverlay && !sidebarHover.open && chrome.liftedIndex == 2);
      Move(650, 400); EXPECT(tabDrag.target.kind == SV::Kind::Refuse && splitAnim.frame.pill.shown);   // side by side: no room
      Release(650, 400); EXPECT(splitGroups.empty() && pages[2] == paper); Settle();
      Press(paper); Move(400, 780); EXPECT(tabDrag.lifted && tabDrag.target.kind == SV::Kind::Split && tabDrag.target.side == SV::Side::Bottom);
      Release(400, 780); EXPECT(splitGroups.size() == 1 && paper->group && paper->group == yt->group); Settle(); }
    chrome.width = 1180;

    // 10. The top bar is unchanged: the strip's card (short site name, the tab's width), lift below the address row.
    settings.tabsPosition = BrowserSettings::TabsPosition::Top; ApplyLayout(); EXPECT(!chrome.sidebar); Reset({yt, luma, paper});
    { BuildChrome(); const auto s = chrome.strip.Surface(2);
      WndProc(window, WM_LBUTTONDOWN, MK_LBUTTON, At(s.left + 20, 20)); Move(s.left + 40, 30); Move(1000, 70); EXPECT(tabDrag.active && !tabDrag.lifted);
      Move(1000, 450); EXPECT(tabDrag.lifted && tabDrag.target.kind == SV::Kind::Split && Near(splitAnim.card.Value().W(), chrome.strip.tab) && splitAnim.frame.card.title == SiteLabel(paper));
      Release(1000, 450); EXPECT(splitGroups.size() == 1 && paper->group == yt->group); Settle(); }

    // 11. Reduced motion (Windows « animation effects » off or Browser's own setting), from the column: the drag stays 1:1,
    //     no x1.02; the zone jumps from one zone to the next and fades in over 80 ms; Escape puts the tab back at once.
    settings.tabsPosition = BrowserSettings::TabsPosition::Side; ApplyLayout(); Reset({yt, luma, paper}); motion.SetReducedMotion(true);
    { Press(luma); Move(900, 400); EXPECT(tabDrag.lifted && tabDrag.target.side == SV::Side::Right && splitAnim.cardScale.target == 100 && Near(splitAnim.fade, .08f, .001f));
      auto now = splitAnim.last; for (int i = 0; i < 6; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
      EXPECT(splitAnim.plateOpacity.x > .95f && Near(splitAnim.frame.card.box.W(), tabDrag.cardW));
      Move(600, 780); const auto bottom = tabDrag.target.preview; const auto v = splitAnim.plate.Value();
      EXPECT(tabDrag.target.side == SV::Side::Bottom && Near(v.left, bottom.left) && Near(v.top, bottom.top) && splitAnim.plateOpacity.x == 0 && splitAnim.plateOpacity.target == 1);   // jumped, fading in
      now = splitAnim.last; for (int i = 0; i < 6; ++i) { now += std::chrono::milliseconds(16); SplitFrame(now); }
      EXPECT(splitAnim.plateOpacity.x > .95f);   // in by ~80 ms
      Escape(); EXPECT(!tabDrag.tab && !splitOverlay.Visible() && chrome.liftedIndex == -1 && splitGroups.empty()); }
    motion.SetReducedMotion(false);

    // 12. Fix's real window (25 nov.): under 900 DIP in a side layout the chat card goes over the page, so its WebView window
    //     must stay above the page's in the sibling order (KeepChatAbovePages). Two stand-in child windows of the browser
    //     window (no WebView2 here): the page's, put back on top as a page WebView created or re-created would be, and the
    //     chat's. Crossing below 900, resizing below it, a tab switch, a drawer opening and closing (the real Historique
    //     drawer, created hidden), compact; crossing back above 900 or the top bar: the chat beside the page, nothing forced.
    {   motion.SetReducedMotion(false); settings.tabsPosition = BrowserSettings::TabsPosition::Side; ApplyLayout(); chrome.width = 1180; Reset({yt, luma, paper});
        HWND page = CreateWindowExW(0, L"STATIC", L"page", WS_CHILD | WS_VISIBLE, 300, 50, 600, 600, window, nullptr, nullptr, nullptr);
        HWND chat = CreateWindowExW(0, L"STATIC", L"chat", WS_CHILD | WS_VISIBLE, 700, 40, 380, 700, window, nullptr, nullptr, nullptr);
        EXPECT(page && chat);
        auto above = [&](HWND a, HWND b) { for (HWND h = GetWindow(window, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) { if (h == a) return true; if (h == b) return false; } return false; };
        auto pageOnTop = [&] { SetWindowPos(page, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); EXPECT(above(page, chat)); };
        chatWindowForTest = chat;
        pageOnTop(); Layout(); EXPECT(!ChatOverlaysPage() && above(page, chat));                       // 1180: beside the page, left alone
        chrome.width = 880; Layout(); EXPECT(ChatOverlaysPage() && above(chat, page));                 // crossing below 900
        pageOnTop(); chrome.width = 860; Layout(); EXPECT(above(chat, page));                           // resizing below it
        pageOnTop(); chrome.width = 820; Layout(); EXPECT(above(chat, page));
        pageOnTop(); ApplyPages(); EXPECT(above(chat, page));                                           // a page's WebView created (its Layout / ApplyPages)
        pageOnTop(); Select(1); EXPECT(current == 1 && above(chat, page));                              // a tab switch
        pageOnTop(); Select(0); EXPECT(above(chat, page));
        {   wchar_t cwd[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, cwd); base = cwd; CreateHistoryPanel(); EXPECT(historyDrawer.Window() != nullptr);
            pageOnTop(); ShowHistory(false); SyncDrawerReserve(); EXPECT(historyDrawer.Open() && DrawerReserveDip() > 0 && above(chat, page));   // a drawer opens
            pageOnTop(); historyDrawer.Hide(false, false); SyncDrawerReserve(); EXPECT(!historyDrawer.Open() && DrawerReserveDip() == 0 && above(chat, page)); }   // and closes
        SetTabsPosition(BrowserSettings::TabsPosition::Compact); pageOnTop(); Layout(); EXPECT(chrome.sidebar && ChatOverlaysPage() && above(chat, page));   // compact too
        SetTabsPosition(BrowserSettings::TabsPosition::Side); pageOnTop(); chrome.width = 1180; Layout(); EXPECT(!ChatOverlaysPage() && above(page, chat));   // crossing back above 900
        SetTabsPosition(BrowserSettings::TabsPosition::Top); chrome.width = 820; pageOnTop(); Layout(); EXPECT(!ChatOverlaysPage() && above(page, chat));   // the top bar: never over the page
        ShowWindow(chat, SW_HIDE); SetTabsPosition(BrowserSettings::TabsPosition::Side); pageOnTop(); Layout(); EXPECT(above(page, chat));   // the chat closed: nothing to lift
        chatWindowForTest = nullptr; DestroyWindow(chat); DestroyWindow(page); chrome.width = 1180; }

    {   // #143 (Val 25 nov., the page over the panel's right edge): an edge drag, every DIP from 420 down to 100 and back to
        // 300, started complete then compact, through the real SidebarEdgeDown / Move / Up and Layout: the page the WebView
        // gets (ContentBox, in device pixels too) starts where the chrome lays the column out (side.page) and where it is
        // drawn (sidebarColumn), and the column is laid out at that width (or at 194, faded at the edge, past 160 toward the
        // window's edge). The window is never shown here, so the fold runs at once (its spring: tests/sidebar_test.cpp).
        motion.SetReducedMotion(false); SetTabsPosition(BrowserSettings::TabsPosition::Side); chrome.width = 1180; Reset({yt, luma, paper}); Layout();
        int bad = 0, steps = 0; const float k = GetDpiForWindow(window) / 96.f;
        for (int compactStart = 0; compactStart < 2; ++compactStart) {
            SetTabsPosition(compactStart ? BrowserSettings::TabsPosition::Compact : BrowserSettings::TabsPosition::Side); Layout();
            const auto from = SidebarEffective(); edgeClickAt = 0;
            EXPECT(chrome.sidebarResizable && SidebarEdgeDown(chrome.side.column - 2) && SidebarEdgeDragging());
            auto check = [&](float x) {
                SidebarEdgeMove(x); ++steps; const auto c = ContentBox(); const RECT px = PixelRect(c); const auto& L = chrome.side;
                bool ok = Near(c.left, L.page.left) && Near(c.left, chrome.sidebarColumn) && px.left == std::lround(chrome.sidebarColumn * k);
                ok = ok && (L.mode == SBar::Mode::Compact ? L.column <= c.left + .01f : (L.column <= c.left + .5f || Near(L.column, SBar::Complete::minimumLayout)));
                if (L.mode == SBar::Mode::Complete && L.column <= c.left + .5f) ok = ok && L.address.right <= c.left - 7.5f && L.toggle.right <= c.left - 7.5f;
                if (!ok && ++bad <= 5) std::printf("  edge drag (%s start) at %.0f: page %.2f, layout page %.2f, drawn %.2f, laid %.2f\n", compactStart ? "compact" : "complete", x, c.left, L.page.left, chrome.sidebarColumn, L.column);
            };
            for (float x = 420; x >= 100; x -= 1) check(x);
            for (float x = 100; x <= 300; x += 1) check(x);
            SidebarEdgeUp(false); Layout(); EXPECT(!SidebarEdgeDragging() && SidebarEffective() == from);   // Échap: undone
        }
        std::printf("edge drag: %d steps, page edge = column edge at each\n", steps); EXPECT(bad == 0);
        SetTabsPosition(BrowserSettings::TabsPosition::Side); Layout();
    }
    pages.clear(); splitOverlay.Dispose(); DestroyWindow(window); window = nullptr;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::printf("PASS sidebar_split_drag_test: %d checks (%.0f ms): column reorder vs lift at the edge (20 DIP back), 6 DIP threshold, split on drop (panel, compact, narrow column over the page), refusal, Escape home to the row, grid bookmark, tear-off branch, top bar unchanged, the chat above the page under 900 DIP (z-order), motion (x1.02 at the grab point, zone morph 0.2 s, 100 ms fade-out, reduced motion 80 ms fade); renders in build/sidebar-split-qa\n", checks, ms);
    return 0;
}
